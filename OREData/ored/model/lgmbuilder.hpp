/*
 Copyright (C) 2016 Quaternion Risk Management Ltd
 All rights reserved.

 This file is part of ORE, a free-software/open-source library
 for transparent pricing and risk analysis - http://opensourcerisk.org

 ORE is free software: you can redistribute it and/or modify it
 under the terms of the Modified BSD License.  You should have received a
 copy of the license along with this program.
 The license is also available online at <http://opensourcerisk.org>

 This program is distributed on the basis that it will form a useful
 contribution to risk analytics and model standardisation, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE. See the license for more details.
*/

/*! \file ored/model/lgmbuilder.hpp
    \brief Build an lgm model
    \ingroup models
*/

#pragma once

#include <map>
#include <ostream>
#include <vector>

#include <qle/models/lgm.hpp>

#include <ored/model/irlgmdata.hpp>
#include <ored/model/modelbuilder.hpp>

namespace ore {
namespace data {
using namespace QuantLib;


//! Observer class for LgmBuilder
/*! 
  This class holds all observables, except the swaption vol surface,
  for an LgmBuilder, and contains an update flag to indicate any 
  changes since it was last called

  \ingroup models
*/
class LgmObserver : public Observer, public Observable {
public:
    LgmObserver() : updated_(true) {};

    //! Add an observable
    void addObserver(boost::shared_ptr<Observable> observable);
    //! Observer interface
    void update();
    //! Returns true if has been updated since the last call
    bool hasUpdated();

private:
    //! Flag to indicate if updated since last call
    bool updated_;

};

//! Builder for a Linear Gauss Markov model component
/*!
  This class is a utility that turns a Linear Gauss Markov
  model description into an interest rate model parametrisation which
  can be used to instantiate a CrossAssetModel.

  \ingroup models
 */
class LgmBuilder : public ModelBuilder {
public:
    /*! The configuration should refer to the calibration configuration here,
      alternative discounting curves are then usually set in the pricing
      engines for swaptions etc. */
    LgmBuilder(const boost::shared_ptr<ore::data::Market>& market, const boost::shared_ptr<IrLgmData>& data,
               const std::string& configuration = Market::defaultConfiguration, Real bootstrapTolerance = 0.001);
    //! Return calibration error
    Real error() {
        calculate();
        return error_;
    }

    //! \name Inspectors
    //@{
    std::string currency() { return data_->ccy(); }
    boost::shared_ptr<QuantExt::LGM>& model() {
        calculate();
        return model_;
    }
    boost::shared_ptr<QuantExt::IrLgm1fParametrization>& parametrization() { return parametrization_; }
    RelinkableHandle<YieldTermStructure> discountCurve() { return discountCurve_; }
    std::vector<boost::shared_ptr<BlackCalibrationHelper>> swaptionBasket() {
        calculate();
        return swaptionBasket_;
    }


    void forceRecalculate() override;
    //@}
private:
    void performCalculations() const override;
    void buildSwaptionBasket() const;
    // updates the swaption vol cache, and returns a bool - true if cache changed
    bool updateSwaptionVolCache() const;

    boost::shared_ptr<ore::data::Market> market_;
    const std::string configuration_;
    boost::shared_ptr<IrLgmData> data_;
    Real bootstrapTolerance_;
    mutable Real error_;
    boost::shared_ptr<QuantExt::LGM> model_;
    Array params_;
    boost::shared_ptr<QuantLib::PricingEngine> swaptionEngine_;
    boost::shared_ptr<QuantExt::IrLgm1fParametrization> parametrization_;
    RelinkableHandle<YieldTermStructure> discountCurve_;
    mutable std::vector<boost::shared_ptr<BlackCalibrationHelper>> swaptionBasket_;
    mutable Array swaptionExpiries_;
    mutable Array swaptionMaturities_;

    Handle<QuantLib::SwaptionVolatilityStructure> svts_;
    Handle<SwapIndex> swapIndex_, shortSwapIndex_;

    // TODO: Move CalibrationErrorType, optimizer and end criteria parameters to data
    boost::shared_ptr<OptimizationMethod> optimizationMethod_;
    EndCriteria endCriteria_;
    BlackCalibrationHelper::CalibrationErrorType calibrationErrorType_;

    // Cache the swation volatilities
    mutable std::vector<QuantLib::Real> swaptionVolCache_;

    bool forceCalibration_ = false;

    // LGM Oberver
    boost::shared_ptr<LgmObserver> lgmObserver_;
};
} // namespace data
} // namespace ore
