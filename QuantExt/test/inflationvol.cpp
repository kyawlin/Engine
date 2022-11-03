#include "toplevelfixture.hpp"
#include <boost/test/unit_test.hpp>

#include <ql/indexes/inflation/aucpi.hpp>
#include <ql/indexes/inflation/euhicp.hpp>
#include <ql/math/interpolations/bilinearinterpolation.hpp>
#include <ql/math/matrix.hpp>
#include <ql/pricingengines/blackcalculator.hpp>
#include <ql/termstructures/inflation/inflationhelpers.hpp>
#include <ql/termstructures/inflationtermstructure.hpp>
#include <ql/termstructures/volatility/inflation/cpivolatilitystructure.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>
#include <qle/pricingengines/cpiblackcapfloorengine.hpp>
#include <qle/termstructures/inflation/piecewisezeroinflationcurve.hpp>
#include <qle/termstructures/interpolatedcpivolatilitysurface.hpp>
#include <qle/termstructures/strippedcpivolatilitystructure.hpp>
#include <qle/utilities/inflation.hpp>

using namespace boost::unit_test_framework;
using namespace QuantLib;
using namespace std;

namespace {

struct CommonData {

    Date today;
    Real tolerance;
    DayCounter dayCounter;
    Calendar fixingCalendar;
    BusinessDayConvention bdc;
    std::vector<Period> zeroCouponPillars{1 * Years, 2 * Years, 3 * Years, 5 * Years};
    std::vector<Rate> zeroCouponQuotes{0.06, 0.04, 0.03, 0.02};

    boost::shared_ptr<SimpleQuote> flatZero = boost::make_shared<SimpleQuote>(0.01);
    Period obsLag;
    Handle<YieldTermStructure> discountTS;

    std::map<Date, Rate> cpiFixings{{Date(1, May, 2021), 97.8744653499849},
                                    {Date(1, Jun, 2021), 98.0392156862745},
                                    {Date(1, Jul, 2021), 98.1989155376188},
                                    {Date(1, Aug, 2021), 98.3642120151039},
                                    {Date(1, Sep, 2021), 98.5297867331921},
                                    {Date(1, Oct, 2021), 98.6902856945937},
                                    {Date(1, Nov, 2021), 98.8564092866721},
                                    {Date(1, Dec, 2021), 99.0174402961208},
                                    {Date(1, Jan, 2022), 99.1841145816863},
                                    {Date(1, Feb, 2022), 99.3510694270946},
                                    {Date(1, Mar, 2022), 99.5021088919576},
                                    {Date(1, Apr, 2022), 99.6695990114986},
                                    {Date(1, May, 2022), 99.8319546569845},
                                    {Date(1, Jun, 2022), 100},
                                    {Date(1, July, 2022), 104}};

    std::vector<Rate> strikes{0.02, 0.04, 0.06, 0.08};
    std::vector<Period> tenors{1 * Years, 2 * Years, 3 * Years};

    vector<vector<Handle<Quote>>> vols{
        {Handle<Quote>(boost::make_shared<SimpleQuote>(0.3)), Handle<Quote>(boost::make_shared<SimpleQuote>(0.32)),
         Handle<Quote>(boost::make_shared<SimpleQuote>(0.34)), Handle<Quote>(boost::make_shared<SimpleQuote>(0.36))},
        {Handle<Quote>(boost::make_shared<SimpleQuote>(0.35)), Handle<Quote>(boost::make_shared<SimpleQuote>(0.37)),
         Handle<Quote>(boost::make_shared<SimpleQuote>(0.39)), Handle<Quote>(boost::make_shared<SimpleQuote>(0.41))},
        {Handle<Quote>(boost::make_shared<SimpleQuote>(0.40)), Handle<Quote>(boost::make_shared<SimpleQuote>(0.42)),
         Handle<Quote>(boost::make_shared<SimpleQuote>(0.44)), Handle<Quote>(boost::make_shared<SimpleQuote>(0.46))}};

    std::vector<double> cStrikes;

    QuantLib::Matrix cPrices;

    std::vector<double> fStrikes;
    QuantLib::Matrix fPrices;

    CommonData()
        : today(15, Aug, 2022), tolerance(1e-6), dayCounter(Actual365Fixed()), fixingCalendar(NullCalendar()),
          bdc(ModifiedFollowing), obsLag(2, Months),
          discountTS(Handle<YieldTermStructure>(
              boost::make_shared<FlatForward>(0, NullCalendar(), Handle<Quote>(flatZero), dayCounter))){

          };
};

boost::shared_ptr<ZeroInflationCurve>
buildZeroInflationCurve(CommonData& cd, bool useLastKnownFixing, const boost::shared_ptr<ZeroInflationIndex>& index,
                        const bool isInterpolated, const boost::shared_ptr<Seasonality>& seasonality = nullptr,
                        const QuantLib::Date& startDate = Date()) {
    Date today = Settings::instance().evaluationDate();
    Date start = startDate;
    if (startDate == Date()) {
        start = today;
    }
    DayCounter dc = cd.dayCounter;

    BusinessDayConvention bdc = ModifiedFollowing;

    std::vector<boost::shared_ptr<QuantExt::ZeroInflationTraits::helper>> helpers;
    for (size_t i = 0; i < cd.zeroCouponQuotes.size(); ++i) {
        Date maturity = start + cd.zeroCouponPillars[i];
        Rate quote = cd.zeroCouponQuotes[i];
        boost::shared_ptr<QuantExt::ZeroInflationTraits::helper> instrument =
            boost::make_shared<ZeroCouponInflationSwapHelper>(
                Handle<Quote>(boost::make_shared<SimpleQuote>(quote)), cd.obsLag, maturity, cd.fixingCalendar, bdc, dc,
                index, isInterpolated ? CPI::Linear : CPI::Flat, Handle<YieldTermStructure>(cd.discountTS), start);
        helpers.push_back(instrument);
    }
    Rate baseRate = QuantExt::ZeroInflation::guessCurveBaseRate(useLastKnownFixing, start, cd.zeroCouponPillars[0],
                                                                cd.dayCounter, cd.obsLag, cd.zeroCouponQuotes[0],
                                                                cd.obsLag, cd.dayCounter, index, isInterpolated);
    boost::shared_ptr<ZeroInflationCurve> curve = boost::make_shared<QuantExt::PiecewiseZeroInflationCurve<Linear>>(
        today, cd.fixingCalendar, dc, cd.obsLag, index->frequency(), baseRate, helpers, 1e-10, index,
        useLastKnownFixing);
    if (seasonality) {
        curve->setSeasonality(seasonality);
    }
    return curve;
}

boost::shared_ptr<CPIVolatilitySurface> buildVolSurface(CommonData& cd,
                                                        const boost::shared_ptr<ZeroInflationIndex>& index,
                                                        const QuantLib::Date& startDate = QuantLib::Date()) {
    auto surface = boost::make_shared<QuantExt::InterpolatedCPIVolatilitySurface<Bilinear>>(
        cd.tenors, cd.strikes, cd.vols, index, 0, cd.fixingCalendar, ModifiedFollowing, cd.dayCounter, cd.obsLag,
        startDate);
    surface->enableExtrapolation();
    return surface;
}

boost::shared_ptr<CPIVolatilitySurface> buildVolSurfaceFromPrices(CommonData& cd,
                                                                  const boost::shared_ptr<ZeroInflationIndex>& index,
                                                                  const bool useLastKnownFixing,
                                                                  const Date& startDate = Date()) {
    boost::shared_ptr<InterpolatedCPICapFloorTermPriceSurface<QuantLib::Bilinear>> cpiPriceSurfacePtr =
        boost::make_shared<InterpolatedCPICapFloorTermPriceSurface<QuantLib::Bilinear>>(
        1.0, 0.0, cd.obsLag, cd.fixingCalendar, cd.bdc, cd.dayCounter, index, CPI::AsIndex,
        cd.discountTS, cd.cStrikes, cd.fStrikes, cd.tenors, cd.cPrices, cd.fPrices);

    boost::shared_ptr<QuantExt::CPIBlackCapFloorEngine> engine = boost::make_shared<QuantExt::CPIBlackCapFloorEngine>(
        cd.discountTS, QuantLib::Handle<QuantLib::CPIVolatilitySurface>(), useLastKnownFixing);

    QuantLib::Handle<CPICapFloorTermPriceSurface> cpiPriceSurfaceHandle(cpiPriceSurfacePtr);
    boost::shared_ptr<QuantExt::StrippedCPIVolatilitySurface<QuantLib::Bilinear>> cpiCapFloorVolSurface;
    cpiCapFloorVolSurface = boost::make_shared<QuantExt::StrippedCPIVolatilitySurface<QuantLib::Bilinear>>(
        QuantExt::PriceQuotePreference::CapFloor, cpiPriceSurfaceHandle, index, engine, startDate);

    cpiCapFloorVolSurface->enableExtrapolation();
    return cpiCapFloorVolSurface;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(QuantExtTestSuite, qle::test::TopLevelFixture)

BOOST_AUTO_TEST_SUITE(InflationCPIVolatilityTest)

BOOST_AUTO_TEST_CASE(testVolatiltiySurface) {
    // Test case when the ZCIIS and Cap/Floors start today with using todays fixing
    CommonData cd;
    Date today(15, Aug, 2022);
    cd.today = today;
    cd.obsLag = 2 * Months;
    Settings::instance().evaluationDate() = today;
    std::map<Date, double> fixings{{Date(1, Mar, 2022), 100.0}};
    Date lastKnownFixing(1, Jul, 2022);

    boost::shared_ptr<ZeroInflationIndex> curveBuildIndex = boost::make_shared<QuantLib::EUHICPXT>(false);
    for (const auto& [date, fixing] : cd.cpiFixings) {
        curveBuildIndex->addFixing(date, fixing);
    }

    auto curve = buildZeroInflationCurve(cd, true, curveBuildIndex, false, nullptr);

    auto index = curveBuildIndex->clone(Handle<ZeroInflationTermStructure>(curve));

    BOOST_CHECK_EQUAL(curve->baseDate(), lastKnownFixing);

    auto volSurface = buildVolSurface(cd, index);

    // Expect the base fixing date of the cap/floor today - 2M 
    BOOST_CHECK_EQUAL(volSurface->baseDate(), Date(1, Jun, 2022));

    double baseCPI = index->fixing(volSurface->baseDate());

    BOOST_CHECK_CLOSE(baseCPI, 100.0, cd.tolerance);

    Matrix cPrices(cd.strikes.size(), cd.tenors.size(), 0.0);
    Matrix fPrices(cd.strikes.size(), cd.tenors.size(), 0.0);

    for (size_t i = 0; i < cd.strikes.size(); ++i) {
        for (size_t j = 0; j < cd.tenors.size(); ++j) {
            double expectedVol = cd.vols[j][i]->value();
            Date optionFixingDate = volSurface->baseDate() + cd.tenors[j];
            Date optionPaymentDate = today + cd.tenors[j];

            double vol = volSurface->volatility(optionFixingDate, cd.strikes[i], 0 * Days, false);
            BOOST_CHECK_CLOSE(vol, expectedVol, cd.tolerance);
            double ttm = cd.dayCounter.yearFraction(volSurface->baseDate(), optionFixingDate);
            double atmf = index->fixing(optionFixingDate) / baseCPI;
            double strike = std::pow(1 + cd.strikes[i], ttm);
            double discountFactor = cd.discountTS->discount(optionPaymentDate);
            double volTimeFrom = cd.dayCounter.yearFraction(lastKnownFixing, optionFixingDate);

            QuantLib::BlackCalculator callPricer(Option::Call, strike, atmf, sqrt(volTimeFrom) * vol, discountFactor);
            QuantLib::BlackCalculator putPricer(Option::Put, strike, atmf, sqrt(volTimeFrom) * vol, discountFactor);

            cPrices[i][j] = callPricer.value();
            fPrices[i][j] = putPricer.value();
        }
    }

    cd.cPrices = cPrices;
    cd.fPrices = fPrices;
    cd.cStrikes = cd.strikes;
    cd.fStrikes = cd.strikes;

    auto priceSurface = buildVolSurfaceFromPrices(cd, index, true);

    for (size_t i = 0; i < cd.strikes.size(); ++i) {
        for (size_t j = 0; j < cd.tenors.size(); ++j) {
            double expectedVol = cd.vols[j][i]->value();
            Date optionFixingDate = priceSurface->baseDate() + cd.tenors[j];
            double vol = priceSurface->volatility(optionFixingDate, cd.strikes[i], 0 * Days, false);
            BOOST_CHECK_CLOSE(vol, expectedVol, cd.tolerance);
        }
    }

    {
        // Pricing seasoned cap/floors

        Date seasonedStartDate(15, Aug, 2021);
        Date seasonedMaturity(15, Aug, 2024);
        Date seasonedBaseFixingDate(1, Jun, 2021);
        Date seasonedFixingDate(1, Jun, 2024);
        double seasonedStrike = 0.03;
        double seasonedBaseCPI = index->fixing(seasonedBaseFixingDate);

        double K = pow(1 + seasonedStrike, cd.dayCounter.yearFraction(seasonedBaseFixingDate, seasonedFixingDate));
        double atm = index->fixing(seasonedFixingDate) / seasonedBaseCPI;

        double adjustedStrike = std::pow(K * seasonedBaseCPI / baseCPI,
                                         1.0 / cd.dayCounter.yearFraction(volSurface->baseDate(), seasonedFixingDate)) -
                                1.0;

        double volTimeFrom = cd.dayCounter.yearFraction(lastKnownFixing, seasonedFixingDate);
        double vol = volSurface->volatility(seasonedFixingDate, adjustedStrike, 0 * Days, false);
        double discountFactor = cd.discountTS->discount(seasonedMaturity);
        QuantLib::BlackCalculator callPricer(Option::Call, K, atm, sqrt(volTimeFrom) * vol, discountFactor);

        boost::shared_ptr<QuantExt::CPIBlackCapFloorEngine> engine =
            boost::make_shared<QuantExt::CPIBlackCapFloorEngine>(
                cd.discountTS, Handle<QuantLib::CPIVolatilitySurface>(volSurface), true);

        QuantLib::CPICapFloor cap(Option::Call, 1.0, seasonedStartDate, Null<double>(), seasonedMaturity,
                                  cd.fixingCalendar, cd.bdc, cd.fixingCalendar, cd.bdc, seasonedStrike,
                                  index, cd.obsLag, CPI::Flat);

        cap.setPricingEngine(engine);

        BOOST_CHECK_CLOSE(cap.NPV(), callPricer.value(), cd.tolerance);
    }
}

BOOST_AUTO_TEST_CASE(testVolatiltiySurfaceWithStartDate) {
    // Test case when the ZCIIS and Cap/Floors don't start today
    // but depend on the publishing schedule of the fixings
    CommonData cd;
    Date today(15, July, 2022);
    cd.today = today;
    cd.obsLag = 3 * Months;
    Settings::instance().evaluationDate() = today;
    std::map<Date, double> fixings{{Date(1, Mar, 2022), 100.0}};
    // the Q2 fixing not published yet, the zciis swaps and caps start on 15th Jun and
    // reference on the Q1 fixing
    Date startDate(15, Jun, 2022);
    Date lastKnownFixing(1, Jan, 2022);

    boost::shared_ptr<ZeroInflationIndex> curveBuildIndex = boost::make_shared<QuantLib::AUCPI>(Quarterly, true, false);
    for (const auto& [date, fixing] : fixings) {
        curveBuildIndex->addFixing(date, fixing);
    }

    auto curve = buildZeroInflationCurve(cd, true, curveBuildIndex, false, nullptr, startDate);

    auto index = curveBuildIndex->clone(Handle<ZeroInflationTermStructure>(curve));

    BOOST_CHECK_EQUAL(curve->baseDate(), lastKnownFixing);

    BOOST_CHECK_EQUAL(curve->dates()[1], Date(1, Jan, 2023));
    BOOST_CHECK_CLOSE(curve->data()[0], cd.zeroCouponQuotes[0], cd.tolerance);
    BOOST_CHECK_CLOSE(curve->data()[1], cd.zeroCouponQuotes[0], cd.tolerance);
    BOOST_CHECK_CLOSE(curve->data()[2], cd.zeroCouponQuotes[1], cd.tolerance);

    auto volSurface = buildVolSurface(cd, index, startDate);
    
    BOOST_CHECK_EQUAL(volSurface->baseDate(), Date(1, Jan, 2022));
    
    double baseCPI = index->fixing(volSurface->baseDate());

    BOOST_CHECK_CLOSE(baseCPI, 100.0, cd.tolerance);

    Matrix cPrices(cd.strikes.size(), cd.tenors.size(), 0.0);
    Matrix fPrices(cd.strikes.size(), cd.tenors.size(), 0.0);

    for (size_t i = 0; i < cd.strikes.size(); ++i) {
        for (size_t j = 0; j < cd.tenors.size(); ++j) {
            double expectedVol = cd.vols[j][i]->value();
            Date optionFixingDate = volSurface->baseDate() + cd.tenors[j];
            Date optionPaymentDate = startDate + cd.tenors[j];

            double vol = volSurface->volatility(optionFixingDate, cd.strikes[i], 0 * Days, false);
            BOOST_CHECK_CLOSE(vol, expectedVol, cd.tolerance);
            double ttm = cd.dayCounter.yearFraction(volSurface->baseDate(), optionFixingDate);
            double atmf = index->fixing(optionFixingDate) / baseCPI;
            double strike = std::pow(1 + cd.strikes[i], ttm);
            double discountFactor = cd.discountTS->discount(optionPaymentDate);
            double volTimeFrom = cd.dayCounter.yearFraction(lastKnownFixing, optionFixingDate);
            QuantLib::BlackCalculator callPricer(Option::Call, strike, atmf, sqrt(volTimeFrom) * vol, discountFactor);
            QuantLib::BlackCalculator putPricer(Option::Put, strike, atmf, sqrt(volTimeFrom) * vol, discountFactor);
            
            cPrices[i][j] = callPricer.value();
            fPrices[i][j] = putPricer.value();
        }   
    }

    cd.cPrices = cPrices;
    cd.fPrices = fPrices;
    cd.cStrikes = cd.strikes;
    cd.fStrikes = cd.strikes;

    auto priceSurface = buildVolSurfaceFromPrices(cd, index, true, startDate);

    for (size_t i = 0; i < cd.strikes.size(); ++i) {
        for (size_t j = 0; j < cd.tenors.size(); ++j) {
            double expectedVol = cd.vols[j][i]->value();
            Date optionFixingDate = priceSurface->baseDate() + cd.tenors[j];
            double vol = priceSurface->volatility(optionFixingDate, cd.strikes[i], 0 * Days, false);
            BOOST_CHECK_CLOSE(vol, expectedVol, cd.tolerance);
        }
    }

    

}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
