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

#include <ored/utilities/parsers.hpp>
#include <ored/utilities/strike.hpp>

#include <ql/errors.hpp>

#include <boost/regex.hpp>

namespace ore {
namespace data {

Strike parseStrike(const std::string& s) {
    boost::regex m1("^(ATM|atm)");
    boost::regex m1b("^(ATMF|atmf)");
    boost::regex m2("^(ATM|atm)(\\+|\\-)([0-9]+[.]?[0-9]*)");
    boost::regex m3("^(\\+|\\-)?([0-9]+[.]?[0-9]*)");
    boost::regex m4("^(\\+|\\-)?([0-9]+[.]?[0-9]*)(d|D)");
    boost::regex m4b("(d|D)");
    boost::regex m5("^(\\+|\\-)?([0-9]+[.]?[0-9]*)(c|C)");
    boost::regex m5b("^(c|C)");
    boost::regex m6("^(\\+|\\-)?([0-9]+[.]?[0-9]*)(p|P)");
    boost::regex m6b("^(p|P)");
    boost::regex m7("^(\\+|\\-)?([0-9]+[.]?[0-9]*)(bf|BF)");
    boost::regex m7b("^(bf|BF)");
    boost::regex m8("^(\\+|\\-)?([0-9]+[.]?[0-9]*)(rr|RR)");
    boost::regex m8b("^(rr|RR)");
    Strike result;
    if (boost::regex_match(s, m1)) {
        result.type = Strike::Type::ATM;
        result.value = 0.0;
        return result;
    }
    if (boost::regex_match(s, m1b)) {
        result.type = Strike::Type::ATMF;
        result.value = 0.0;
        return result;
    }
    if (boost::regex_match(s, m2)) {
        result.type = Strike::Type::ATM_Offset;
        result.value = parseReal(regex_replace(s, m1, std::string("")));
        return result;
    }
    if (boost::regex_match(s, m3)) {
        result.type = Strike::Type::Absolute;
        result.value = parseReal(s);
        return result;
    }
    if (boost::regex_match(s, m4)) {
        result.type = Strike::Type::Delta;
        result.value = parseReal(regex_replace(s, m4b, std::string("")));
        return result;
    }
    if (boost::regex_match(s, m5)) {
        result.type = Strike::Type::DeltaCall;
        result.value = parseReal(regex_replace(s, m5b, std::string("")));
        return result;
    }
    if (boost::regex_match(s, m6)) {
        result.type = Strike::Type::DeltaPut;
        result.value = parseReal(regex_replace(s, m6b, std::string("")));
        return result;
    }
    if (boost::regex_match(s, m7)) {
        result.type = Strike::Type::BF;
        result.value = parseReal(regex_replace(s, m7b, std::string("")));;
        QL_REQUIRE(result.value == 10 || result.value == 25, result.value << " is not a supported BF quote");
        return result;
    }
    if (boost::regex_match(s, m8)) {
        result.type = Strike::Type::RR;
        result.value = parseReal(regex_replace(s, m8b, std::string("")));;
        QL_REQUIRE(result.value == 10 || result.value == 25, result.value << " is not a supported RR quote");
        return result;
    }
    QL_FAIL("could not parse strike given by " << s);
}

std::ostream& operator<<(std::ostream& out, const Strike& s) {
    switch (s.type) {
    case Strike::Type::ATM:
        out << "ATM";
        break;
    case Strike::Type::ATMF:
        out << "ATMF";
        break;
    case Strike::Type::ATM_Offset:
        out << "ATM_Offset";
        break;
    case Strike::Type::Absolute:
        out << "Absolute";
        break;
    case Strike::Type::Delta:
        out << "Delta";
        break;
    default:
        out << "UNKNOWN";
        break;
    }
    if (s.type == Strike::Type::ATM_Offset || s.type == Strike::Type::Absolute || s.type == Strike::Type::Delta) {
        if (s.value >= 0.0)
            out << "+";
        else
            out << "-";
        out << s.value;
    }
    return out;
}
} // namespace data
} // namespace ore
