//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     mne_description_parser.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     February 2026
 * @brief    Implementation of @ref MNELIB::MNEDescriptionParser.
 *
 * Implements the tokeniser, the @c { ... } stanza state machine and the
 * per-keyword field assignment used to populate one
 * @c FIFFLIB::AverageDescription or @c MNELIB::CovDescription per stanza.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "mne_description_parser.h"
#include <utils/logger/log.h>

#include <utils/string_utils.h>


#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace MNELIB;

//=============================================================================================================
// STATIC HELPERS
//=============================================================================================================

void MNEDescriptionParser::skipComments(std::istream& in)
{
    // A '#' at a word start comments out the rest of the line
    for (int c = in.peek(); c != std::char_traits<char>::eof(); c = in.peek()) {
        if (c == '#') {
            in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else if (std::isspace(c)) {
            in.get();
        } else {
            return;
        }
    }
}

//=============================================================================================================

std::string MNEDescriptionParser::nextWord(std::istream& in)
{
    skipComments(in);

    std::string word;
    char c = 0;
    if (!in.get(c)) {
        return word; // EOF
    }
    // Quoted string
    if (c == '"') {
        std::getline(in, word, '"');
        return word;
    }
    // Regular word
    word.push_back(c);
    while (in.get(c)) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            break;
        }
        if (c == '#') {
            in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            break;
        }
        word.push_back(c);
    }
    return word;
}

//=============================================================================================================

bool MNEDescriptionParser::getInt(std::istream& in, int& val)
{
    const std::string word = nextWord(in);
    if (word.empty()) {
        UTILSLIB::Log::warning("MNEDescriptionParser: expected integer, got EOF");
        return false;
    }
    const std::optional<int> parsed = UTILSLIB::toInt(word);
    if (!parsed) {
        UTILSLIB::Log::warning("MNEDescriptionParser: bad integer: {}", word);
        return false;
    }
    val = *parsed;
    return true;
}

//=============================================================================================================

bool MNEDescriptionParser::getFloat(std::istream& in, float& val)
{
    const std::string word = nextWord(in);
    if (word.empty()) {
        UTILSLIB::Log::warning("MNEDescriptionParser: expected float, got EOF");
        return false;
    }
    const std::optional<double> parsed = UTILSLIB::toDouble(word);
    if (!parsed) {
        UTILSLIB::Log::warning("MNEDescriptionParser: bad float: {}", word);
        return false;
    }
    val = static_cast<float>(*parsed);
    return true;
}

//=============================================================================================================

bool MNEDescriptionParser::parseRejectionParam(std::string_view keyword, std::istream& in,
                                               FIFFLIB::RejectionParams& rej, bool& ok)
{
    ok = true;
    float fval;

    if (UTILSLIB::equalsIgnoreCase(keyword, "gradReject")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.megGradReject = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "magReject")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.megMagReject = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "eegReject")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.eegReject = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "eogReject")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.eogReject = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "ecgReject")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.ecgReject = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "gradFlat")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.megGradFlat = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "magFlat")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.megMagFlat = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "eegFlat")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.eegFlat = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "eogFlat")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.eogFlat = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "ecgFlat")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.ecgFlat = fval;
    } else if (UTILSLIB::equalsIgnoreCase(keyword, "stimIgnore")) {
        ok = getFloat(in, fval);
        if (ok)
            rej.stimIgnore = std::fabs(fval);
    } else {
        return false; // not a rejection keyword
    }
    return true; // was a rejection keyword (ok indicates parse success)
}

//=============================================================================================================
// AVERAGE DESCRIPTION PARSER
//=============================================================================================================

bool MNEDescriptionParser::parseAverageFile(const std::filesystem::path& fileName, FIFFLIB::AverageDescription& desc)
{
    std::ifstream in(fileName);
    if (!in) {
        UTILSLIB::Log::warning("MNEDescriptionParser: cannot open {}", UTILSLIB::utf8FromPath(fileName));
        return false;
    }

    desc = FIFFLIB::AverageDescription();

    bool expectBrace = false;
    bool bminSet = false, bmaxSet = false;
    bool inAverage = false;
    bool inCategory = false;
    FIFFLIB::AverageCategory currentCat;

    std::string word;
    while (!(word = nextWord(in)).empty()) {
        if (expectBrace) {
            if (word == "{") {
                expectBrace = false;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: expected '{{', got {}", word);
                return false;
            }
            continue;
        }

        if (word == "{") {
            UTILSLIB::Log::warning("MNEDescriptionParser: unexpected '{{'");
            return false;
        }

        if (word == "}") {
            if (inCategory) {
                currentCat.doBaseline = bminSet && bmaxSet;
                // Validate category
                if (currentCat.comment.empty()) {
                    UTILSLIB::Log::warning("MNEDescriptionParser: category name missing");
                    return false;
                }
                if (currentCat.tmin >= currentCat.tmax) {
                    UTILSLIB::Log::warning("MNEDescriptionParser: illegal time range for {}", currentCat.comment);
                    return false;
                }
                if (currentCat.events.empty()) {
                    UTILSLIB::Log::warning("MNEDescriptionParser: no events for {}", currentCat.comment);
                    return false;
                }
                if (currentCat.doBaseline && currentCat.bmin > currentCat.bmax) {
                    UTILSLIB::Log::warning("MNEDescriptionParser: illegal baseline for {}", currentCat.comment);
                    return false;
                }
                if (!currentCat.prevIgnore)
                    currentCat.prevIgnore = currentCat.ignore;
                if (!currentCat.nextIgnore)
                    currentCat.nextIgnore = currentCat.ignore;
                desc.categories.push_back(currentCat);
                currentCat = FIFFLIB::AverageCategory();
                inCategory = false;
                bminSet = bmaxSet = false;
            } else if (inAverage) {
                if (desc.comment.empty())
                    desc.comment = "Average";
                inAverage = false;
                break; // done
            }
            continue;
        }

        // Top-level: "average" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "average")) {
            if (inAverage || inCategory) {
                UTILSLIB::Log::warning("MNEDescriptionParser: nested average");
                return false;
            }
            inAverage = true;
            expectBrace = true;
            continue;
        }

        // Category start
        if (UTILSLIB::equalsIgnoreCase(word, "category") ||
            UTILSLIB::equalsIgnoreCase(word, "condition")) {
            if (!inAverage || inCategory) {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced category");
                return false;
            }
            inCategory = true;
            expectBrace = true;
            bminSet = bmaxSet = false;
            continue;
        }

        // "name" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "name")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                UTILSLIB::Log::warning("MNEDescriptionParser: name requires a value");
                return false;
            }
            if (inCategory)
                currentCat.comment = val;
            else if (inAverage)
                desc.comment = val;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced name");
                return false;
            }
            continue;
        }

        // "outfile" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "outfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                UTILSLIB::Log::warning("MNEDescriptionParser: outfile requires a value");
                return false;
            }
            if (inAverage && !inCategory)
                desc.filename = val;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced outfile");
                return false;
            }
            continue;
        }

        // "eventfile" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "eventfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                UTILSLIB::Log::warning("MNEDescriptionParser: eventfile requires a value");
                return false;
            }
            if (inAverage && !inCategory)
                desc.eventFile = val;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced eventfile");
                return false;
            }
            continue;
        }

        // "logfile" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "logfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                UTILSLIB::Log::warning("MNEDescriptionParser: logfile requires a value");
                return false;
            }
            if (inAverage && !inCategory)
                desc.logFile = val;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced logfile");
                return false;
            }
            continue;
        }

        // "fixskew" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "fixskew")) {
            if (inAverage && !inCategory)
                desc.fixSkew = true;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced fixskew");
                return false;
            }
            continue;
        }

        // --- Category-level keywords ---
        if (UTILSLIB::equalsIgnoreCase(word, "tmin")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inCategory)
                currentCat.tmin = fval;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced tmin");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "tmax")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inCategory)
                currentCat.tmax = fval;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced tmax");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "basemin") ||
            UTILSLIB::equalsIgnoreCase(word, "bmin")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inCategory) {
                currentCat.bmin = fval;
                bminSet = true;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced basemin");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "basemax") ||
            UTILSLIB::equalsIgnoreCase(word, "bmax")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inCategory) {
                currentCat.bmax = fval;
                bmaxSet = true;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced basemax");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "event")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: event numbers must be positive");
                return false;
            }
            if (inCategory)
                currentCat.events.push_back(static_cast<unsigned int>(ival));
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced event");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "nextevent")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: nextevent must be positive");
                return false;
            }
            if (inCategory)
                currentCat.nextEvent = static_cast<unsigned int>(ival);
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced nextevent");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "prevevent")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: prevevent must be positive");
                return false;
            }
            if (inCategory)
                currentCat.prevEvent = static_cast<unsigned int>(ival);
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced prevevent");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "ignore")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival < 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: ignore numbers must be positive or zero");
                return false;
            }
            if (inCategory)
                currentCat.ignore = static_cast<unsigned int>(ival);
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced ignore");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "prevignore")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival < 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: ignore numbers must be positive or zero");
                return false;
            }
            if (inCategory)
                currentCat.prevIgnore = static_cast<unsigned int>(ival);
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced prevignore");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "nextignore")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival < 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: ignore numbers must be positive or zero");
                return false;
            }
            if (inCategory)
                currentCat.nextIgnore = static_cast<unsigned int>(ival);
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced nextignore");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "mask")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: mask must be positive");
                return false;
            }
            if (inCategory) {
                currentCat.ignore = static_cast<unsigned int>(ival);
                currentCat.ignore = ~currentCat.ignore;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced mask");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "prevmask")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: mask numbers must be positive");
                return false;
            }
            if (inCategory) {
                currentCat.prevIgnore = static_cast<unsigned int>(ival);
                currentCat.prevIgnore = ~currentCat.prevIgnore;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced prevmask");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "nextmask")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: mask numbers must be positive");
                return false;
            }
            if (inCategory) {
                currentCat.nextIgnore = static_cast<unsigned int>(ival);
                currentCat.nextIgnore = ~currentCat.nextIgnore;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced nextmask");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "delay")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inCategory)
                currentCat.delay = fval;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced delay");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "stderr")) {
            if (inCategory)
                currentCat.doStdErr = true;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced stderr");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "abs")) {
            if (inCategory)
                currentCat.doAbs = true;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced abs");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "color")) {
            float r, g, b;
            if (!getFloat(in, r) || !getFloat(in, g) || !getFloat(in, b))
                return false;
            if (r < 0.0f || r > 1.0f || g < 0.0f || g > 1.0f || b < 0.0f || b > 1.0f) {
                UTILSLIB::Log::warning("MNEDescriptionParser: color components must be within 0 ... 1");
                return false;
            }
            if (inCategory) {
                currentCat.color[0] = r;
                currentCat.color[1] = g;
                currentCat.color[2] = b;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced color");
                return false;
            }
            continue;
        }

        // Try rejection parameters (average-level only)
        bool parseOk;
        if (parseRejectionParam(word, in, desc.rej, parseOk)) {
            if (!(inAverage && !inCategory)) {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced {}", word);
                return false;
            }
            if (!parseOk)
                return false;
            continue;
        }

        UTILSLIB::Log::warning("MNEDescriptionParser: unknown keyword {} in {}", word, UTILSLIB::utf8FromPath(fileName));
    }

    if (desc.categories.empty()) {
        UTILSLIB::Log::warning("MNEDescriptionParser: no categories found in {}", UTILSLIB::utf8FromPath(fileName));
        return false;
    }

    return true;
}

//=============================================================================================================
// COVARIANCE DESCRIPTION PARSER
//=============================================================================================================

bool MNEDescriptionParser::parseCovarianceFile(const std::filesystem::path& fileName, CovDescription& desc)
{
    std::ifstream in(fileName);
    if (!in) {
        UTILSLIB::Log::warning("MNEDescriptionParser: cannot open {}", UTILSLIB::utf8FromPath(fileName));
        return false;
    }

    desc = CovDescription();

    bool expectBrace = false;
    bool bminSet = false, bmaxSet = false;
    bool inCov = false;
    bool seenCov = false;
    bool inDef = false;
    CovDefinition currentDef;

    std::string word;
    while (!(word = nextWord(in)).empty()) {
        if (expectBrace) {
            if (word == "{") {
                expectBrace = false;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: expected '{{', got {}", word);
                return false;
            }
            continue;
        }

        if (word == "{") {
            UTILSLIB::Log::warning("MNEDescriptionParser: unexpected '{{'");
            return false;
        }

        if (word == "}") {
            if (inDef) {
                currentDef.doBaseline = bminSet && bmaxSet;
                if (currentDef.tmin >= currentDef.tmax) {
                    UTILSLIB::Log::warning("MNEDescriptionParser: illegal time range in def");
                    return false;
                }
                if (currentDef.doBaseline && currentDef.bmin > currentDef.bmax) {
                    UTILSLIB::Log::warning("MNEDescriptionParser: illegal baseline in def");
                    return false;
                }
                desc.defs.push_back(currentDef);
                currentDef = CovDefinition();
                inDef = false;
                bminSet = bmaxSet = false;
            } else if (inCov) {
                inCov = false;
                break;
            }
            continue;
        }

        if (UTILSLIB::equalsIgnoreCase(word, "cov")) {
            if (inCov || inDef) {
                UTILSLIB::Log::warning("MNEDescriptionParser: nested cov");
                return false;
            }
            inCov = true;
            seenCov = true;
            expectBrace = true;
            continue;
        }

        if (UTILSLIB::equalsIgnoreCase(word, "def")) {
            if (!inCov || inDef) {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced def");
                return false;
            }
            inDef = true;
            expectBrace = true;
            bminSet = bmaxSet = false;
            continue;
        }

        // Cov-level keywords
        if (UTILSLIB::equalsIgnoreCase(word, "outfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                UTILSLIB::Log::warning("MNEDescriptionParser: outfile requires a value");
                return false;
            }
            if (inCov && !inDef)
                desc.filename = val;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced outfile");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "eventfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                UTILSLIB::Log::warning("MNEDescriptionParser: eventfile requires a value");
                return false;
            }
            if (inCov && !inDef)
                desc.eventFile = val;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced eventfile");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "logfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                UTILSLIB::Log::warning("MNEDescriptionParser: logfile requires a value");
                return false;
            }
            if (inCov && !inDef)
                desc.logFile = val;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced logfile");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "keepsamplemean")) {
            if (inCov && !inDef)
                desc.removeSampleMean = false;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced keepsamplemean");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "fixskew")) {
            if (inCov && !inDef)
                desc.fixSkew = true;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced fixskew");
                return false;
            }
            continue;
        }

        // Def-level keywords
        if (UTILSLIB::equalsIgnoreCase(word, "tmin")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inDef)
                currentDef.tmin = fval;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced tmin");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "tmax")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inDef)
                currentDef.tmax = fval;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced tmax");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "basemin") ||
            UTILSLIB::equalsIgnoreCase(word, "bmin")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inDef) {
                currentDef.bmin = fval;
                bminSet = true;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced basemin");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "basemax") ||
            UTILSLIB::equalsIgnoreCase(word, "bmax")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inDef) {
                currentDef.bmax = fval;
                bmaxSet = true;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced basemax");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "event")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: event must be positive");
                return false;
            }
            if (inDef)
                currentDef.events.push_back(static_cast<unsigned int>(ival));
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced event");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "ignore")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival < 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: ignore numbers must be positive or zero");
                return false;
            }
            if (inDef)
                currentDef.ignore = static_cast<unsigned int>(ival);
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced ignore");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "mask")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                UTILSLIB::Log::warning("MNEDescriptionParser: mask numbers must be positive");
                return false;
            }
            if (inDef) {
                currentDef.ignore = static_cast<unsigned int>(ival);
                currentDef.ignore = ~currentDef.ignore;
            } else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced mask");
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "delay")) {
            float fval;
            if (!getFloat(in, fval))
                return false;
            if (inDef)
                currentDef.delay = fval;
            else {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced delay");
                return false;
            }
            continue;
        }

        // Rejection parameters (cov-level only)
        bool parseOk;
        if (parseRejectionParam(word, in, desc.rej, parseOk)) {
            if (!(inCov && !inDef)) {
                UTILSLIB::Log::warning("MNEDescriptionParser: misplaced {}", word);
                return false;
            }
            if (!parseOk)
                return false;
            continue;
        }

        UTILSLIB::Log::warning("MNEDescriptionParser: unknown keyword {} in {}", word, UTILSLIB::utf8FromPath(fileName));
    }

    if (!seenCov) {
        UTILSLIB::Log::warning("MNEDescriptionParser: nothing useful read from {}", UTILSLIB::utf8FromPath(fileName));
        return false;
    }

    return true;
}
