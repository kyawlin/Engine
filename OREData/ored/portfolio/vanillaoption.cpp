/*
 Copyright (C) 2018 Quaternion Risk Management Ltd
 All rights reserved.
*/

/*! \file ored/portfolio/vanillaoption.hpp
\brief vanilla option representation
\ingroup tradedata
*/

#include <ored/portfolio/builders/vanillaoption.hpp>
#include <ored/portfolio/builders/quantovanillaoption.hpp>
#include <ored/portfolio/vanillaoption.hpp>
#include <ored/utilities/log.hpp>
#include <ored/utilities/currencycheck.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <ql/instruments/quantovanillaoption.hpp>
#include <qle/instruments/vanillaforwardoption.hpp>
#include <qle/instruments/cashsettledeuropeanoption.hpp>

using namespace QuantLib;
using QuantExt::CashSettledEuropeanOption;

namespace ore {
namespace data {

void VanillaOptionTrade::build(const boost::shared_ptr<ore::data::EngineFactory>& engineFactory) {
    Currency ccy = parseCurrencyWithMinors(currency_);
    QL_REQUIRE(tradeActions().empty(), "TradeActions not supported for VanillaOption");

    // If underlying currency is empty, then set to payment currency by default.
    // If non-empty, then check if the currencies are different for a Quanto payoff
    Currency underlyingCurrency = underlyingCurrency_.empty() ? ccy : parseCurrencyWithMinors(underlyingCurrency_);
    bool sameCcy = underlyingCurrency == ccy;

    // Payoff
    Option::Type type = parseOptionType(option_.callPut());
    boost::shared_ptr<StrikedTypePayoff> payoff(new PlainVanillaPayoff(type, strike_));
    QuantLib::Exercise::Type exerciseType = parseExerciseType(option_.style());
    QL_REQUIRE(option_.exerciseDates().size() == 1, "Invalid number of excercise dates");
    expiryDate_ = parseDate(option_.exerciseDates().front());
    // Set the maturity date equal to the expiry date. It may get updated below if option is cash settled with
    // payment after expiry.
    maturity_ = expiryDate_;
    // Exercise
    boost::shared_ptr<Exercise> exercise;
    switch (exerciseType) {
    case QuantLib::Exercise::Type::European: {
        exercise = boost::make_shared<EuropeanExercise>(expiryDate_);
        break;
    }
    case QuantLib::Exercise::Type::American: {
        exercise = boost::make_shared<AmericanExercise>(expiryDate_, option_.payoffAtExpiry());
        break;
    }
    default:
        QL_FAIL("Option Style " << option_.style() << " is not supported");
    }
    // Create the instrument and then populate the name for the engine builder.
    boost::shared_ptr<Instrument> vanilla;
    string tradeTypeBuilder = tradeType_;
    Settlement::Type settlementType = parseSettlementType(option_.settlement());

    // For Quanto, check for European and Cash, except for an FX underlying
    if (!sameCcy) {
        QL_REQUIRE(exerciseType == Exercise::Type::European, "Option exercise must be European for a Quanto payoff.");
        if (settlementType == Settlement::Type::Physical) {
            QL_REQUIRE(assetClassUnderlying_ == AssetClass::FX,
                       "Physically settled Quanto options are allowed only for an FX underlying.");
        }
    }

    if (exerciseType == Exercise::European && settlementType == Settlement::Cash) {
        // We have a European cash settled option.

        // Get the payment date.
        const boost::optional<OptionPaymentData>& opd = option_.paymentData();
        Date paymentDate = expiryDate_;
        if (opd) {
            if (opd->rulesBased()) {
                const Calendar& cal = opd->calendar();
                QL_REQUIRE(cal != Calendar(), "Need a non-empty calendar for rules based payment date.");
                paymentDate = cal.advance(expiryDate_, opd->lag(), Days, opd->convention());
            } else {
                const vector<Date>& dates = opd->dates();
                QL_REQUIRE(dates.size() == 1, "Need exactly one payment date for cash settled European option.");
                paymentDate = dates[0];
            }
            QL_REQUIRE(paymentDate >= expiryDate_, "Payment date must be greater than or equal to expiry date.");
        }

        if (paymentDate > expiryDate_) {
            QL_REQUIRE(sameCcy, "Payment date must equal expiry date for a Quanto payoff. Trade: " << id() << ".");

            // Build a QuantExt::CashSettledEuropeanOption if payment date is strictly greater than expiry.

            // Has the option been marked as exercised
            const boost::optional<OptionExerciseData>& oed = option_.exerciseData();
            bool exercised = false;
            Real exercisePrice = Null<Real>();
            if (oed) {
                QL_REQUIRE(oed->date() == expiryDate_, "The supplied exercise date ("
                                                           << io::iso_date(oed->date())
                                                           << ") should equal the option's expiry date ("
                                                           << io::iso_date(expiryDate_) << ").");
                exercised = true;
                exercisePrice = oed->price();
            }

            // If automatic exercise, we will need an index fixing on the expiry date.
            if (option_.isAutomaticExercise()) {
                QL_REQUIRE(index_, "Option trade " << id() << " has automatic exercise so we need a valid index.");
                // If index name has not been populated, use logic here to populate it from the index object.
                string indexName = indexName_;
                if (indexName.empty()) {
                    indexName = index_->name();
                    if (assetClassUnderlying_ == AssetClass::EQ)
                        indexName = "EQ-" + indexName;
                }
                requiredFixings_.addFixingDate(expiryDate_, indexName, paymentDate);
            }

            // Build the instrument
            vanilla = boost::make_shared<CashSettledEuropeanOption>(
                type, strike_, expiryDate_, paymentDate, option_.isAutomaticExercise(), index_, exercised, exercisePrice);

            // Allow for a separate pricing engine that takes care of payment on a date after expiry. Do this by
            // appending 'EuropeanCS' to the trade type.
            tradeTypeBuilder = tradeType_ + "EuropeanCS";

            // Update the maturity date.
            maturity_ = paymentDate;

        } else {
            if (forwardDate_ == QuantLib::Date()) {
                // If payment date is not greater than expiry, build QuantLib::VanillaOption.
                if (sameCcy)
                    vanilla = boost::make_shared<QuantLib::VanillaOption>(payoff, exercise);
                else {
                    vanilla = boost::make_shared<QuantLib::QuantoVanillaOption>(payoff, exercise);
                    if (assetClassUnderlying_ == AssetClass::EQ)
                        tradeTypeBuilder = "QuantoEquityOption";
                    else if (assetClassUnderlying_ == AssetClass::COM)
                        tradeTypeBuilder = "QuantoCommodityOption";
                    else
                        QL_FAIL("Option Quanto payoff not supported for " << assetClassUnderlying_ << " class.");
                }
            } else {
                QL_REQUIRE(sameCcy, "Quanto payoff is not currently supported for Forward Options: Trade " << id());
                vanilla = boost::make_shared<QuantExt::VanillaForwardOption>(payoff, exercise, forwardDate_);
            }
        }

    } else {
        if (forwardDate_ == QuantLib::Date()) {
            // If not European or not cash settled, build QuantLib::VanillaOption.
            if (sameCcy)
                vanilla = boost::make_shared<QuantLib::VanillaOption>(payoff, exercise);
            else
                vanilla = boost::make_shared<QuantLib::QuantoVanillaOption>(payoff, exercise);
        } else {
            QL_REQUIRE(exerciseType == QuantLib::Exercise::Type::European, "Only European Forward Options currently supported");
            vanilla = boost::make_shared<QuantExt::VanillaForwardOption>(payoff, exercise, forwardDate_);
        }

        if (sameCcy)
            tradeTypeBuilder = tradeType_ + (exerciseType == QuantLib::Exercise::Type::European ? "" : "American");
        else
            tradeTypeBuilder = "QuantoFxOption";
    }
    // Generally we need to set the pricing engine here even if the option is expired at build time, since the valuation date
    // might change after build, and we get errors for the edge case valuation date = expiry date for Europen options.
    // We keep the previous behaviour for expired American style options for now, because of engine builders that rely on the
    // expiry date being in the future e.g. AmericanOptionFDEngineBuilder.
    string configuration = Market::defaultConfiguration;
    bool skipEngine = (vanilla->isExpired() && exerciseType == QuantLib::Exercise::American);
    if (skipEngine) {
       DLOG("No engine attached for option on trade " << id() << " with expiry date " << io::iso_date(expiryDate_)
                                                       << " because it is expired and american style.");
    } else {
        boost::shared_ptr<EngineBuilder> builder = engineFactory->builder(tradeTypeBuilder);
        QL_REQUIRE(builder, "No builder found for " << tradeTypeBuilder);

        if (sameCcy) {
            boost::shared_ptr<VanillaOptionEngineBuilder> vanillaOptionBuilder =
                boost::dynamic_pointer_cast<VanillaOptionEngineBuilder>(builder);
            QL_REQUIRE(vanillaOptionBuilder != nullptr, "No engine builder found for trade type " << tradeTypeBuilder);

            vanilla->setPricingEngine(vanillaOptionBuilder->engine(assetName_, ccy, expiryDate_));

            configuration = vanillaOptionBuilder->configuration(MarketContext::pricing);
        } else {
            boost::shared_ptr<QuantoVanillaOptionEngineBuilder> quantoVanillaOptionBuilder =
                boost::dynamic_pointer_cast<QuantoVanillaOptionEngineBuilder>(builder);
            QL_REQUIRE(quantoVanillaOptionBuilder != nullptr, "No (Quanto) engine builder found for trade type "
                                                                << tradeTypeBuilder);

            vanilla->setPricingEngine(quantoVanillaOptionBuilder->engine(assetName_, underlyingCurrency, ccy, expiryDate_));

            configuration = quantoVanillaOptionBuilder->configuration(MarketContext::pricing);
        }
    }
    Position::Type positionType = parsePositionType(option_.longShort());
    Real bsInd = (positionType == QuantLib::Position::Long ? 1.0 : -1.0);
    Real mult = quantity_ * bsInd;

    // If premium data is provided
    // 1) build the fee trade and pass it to the instrument wrapper for pricing
    // 2) add fee payment as additional trade leg for cash flow reporting
    std::vector<boost::shared_ptr<Instrument>> additionalInstruments;
    std::vector<Real> additionalMultipliers;
    if (option_.premiumPayDate() != "" && option_.premiumCcy() != "") {
        Real premiumAmount = -bsInd * option_.premium(); // pay if long, receive if short
        // Premium could be in minor currency units, convert if needed
        Currency premiumCurrency = parseCurrencyWithMinors(option_.premiumCcy());
        premiumAmount = convertMinorToMajorCurrency(option_.premiumCcy(), premiumAmount);

        Date premiumDate = parseDate(option_.premiumPayDate());
        addPayment(additionalInstruments, additionalMultipliers, mult, premiumDate, premiumAmount, premiumCurrency, ccy,
                   engineFactory, configuration);
        DLOG("option premium added for vanilla option " << id());
    }
    instrument_ = boost::shared_ptr<InstrumentWrapper>(
        new VanillaInstrument(vanilla, mult, additionalInstruments, additionalMultipliers));
    npvCurrency_ = currency_;

    // Notional - we really need todays spot to get the correct notional.
    // But rather than having it move around we use strike * quantity
    notional_ = strike_ * quantity_;
    notionalCurrency_ = currency_;
}

void VanillaOptionTrade::fromXML(XMLNode* node) { Trade::fromXML(node); }

XMLNode* VanillaOptionTrade::toXML(XMLDocument& doc) {
    XMLNode* node = Trade::toXML(doc);
    return node;
}

} // namespace data
} // namespace ore
