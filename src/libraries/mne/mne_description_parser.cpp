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

#include <utils/string_utils.h>

#include <QDebug>

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
        qWarning() << "MNEDescriptionParser: expected integer, got EOF";
        return false;
    }
    const std::optional<int> parsed = UTILSLIB::toInt(word);
    if (!parsed) {
        qWarning() << "MNEDescriptionParser: bad integer:" << word.c_str();
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
        qWarning() << "MNEDescriptionParser: expected float, got EOF";
        return false;
    }
    const std::optional<double> parsed = UTILSLIB::toDouble(word);
    if (!parsed) {
        qWarning() << "MNEDescriptionParser: bad float:" << word.c_str();
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
        qWarning() << "MNEDescriptionParser: cannot open" << UTILSLIB::utf8FromPath(fileName).c_str();
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
                qWarning() << "MNEDescriptionParser: expected '{', got" << word.c_str();
                return false;
            }
            continue;
        }

        if (word == "{") {
            qWarning() << "MNEDescriptionParser: unexpected '{'";
            return false;
        }

        if (word == "}") {
            if (inCategory) {
                currentCat.doBaseline = bminSet && bmaxSet;
                // Validate category
                if (currentCat.comment.empty()) {
                    qWarning() << "MNEDescriptionParser: category name missing";
                    return false;
                }
                if (currentCat.tmin >= currentCat.tmax) {
                    qWarning() << "MNEDescriptionParser: illegal time range for" << currentCat.comment;
                    return false;
                }
                if (currentCat.events.empty()) {
                    qWarning() << "MNEDescriptionParser: no events for" << currentCat.comment;
                    return false;
                }
                if (currentCat.doBaseline && currentCat.bmin > currentCat.bmax) {
                    qWarning() << "MNEDescriptionParser: illegal baseline for" << currentCat.comment;
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
                qWarning() << "MNEDescriptionParser: nested average";
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
                qWarning() << "MNEDescriptionParser: misplaced category";
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
                qWarning() << "MNEDescriptionParser: name requires a value";
                return false;
            }
            if (inCategory)
                currentCat.comment = val;
            else if (inAverage)
                desc.comment = val;
            else {
                qWarning() << "MNEDescriptionParser: misplaced name";
                return false;
            }
            continue;
        }

        // "outfile" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "outfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                qWarning() << "MNEDescriptionParser: outfile requires a value";
                return false;
            }
            if (inAverage && !inCategory)
                desc.filename = val;
            else {
                qWarning() << "MNEDescriptionParser: misplaced outfile";
                return false;
            }
            continue;
        }

        // "eventfile" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "eventfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                qWarning() << "MNEDescriptionParser: eventfile requires a value";
                return false;
            }
            if (inAverage && !inCategory)
                desc.eventFile = val;
            else {
                qWarning() << "MNEDescriptionParser: misplaced eventfile";
                return false;
            }
            continue;
        }

        // "logfile" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "logfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                qWarning() << "MNEDescriptionParser: logfile requires a value";
                return false;
            }
            if (inAverage && !inCategory)
                desc.logFile = val;
            else {
                qWarning() << "MNEDescriptionParser: misplaced logfile";
                return false;
            }
            continue;
        }

        // "fixskew" keyword
        if (UTILSLIB::equalsIgnoreCase(word, "fixskew")) {
            if (inAverage && !inCategory)
                desc.fixSkew = true;
            else {
                qWarning() << "MNEDescriptionParser: misplaced fixskew";
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
                qWarning() << "MNEDescriptionParser: misplaced tmin";
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
                qWarning() << "MNEDescriptionParser: misplaced tmax";
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
                qWarning() << "MNEDescriptionParser: misplaced basemin";
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
                qWarning() << "MNEDescriptionParser: misplaced basemax";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "event")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                qWarning() << "MNEDescriptionParser: event numbers must be positive";
                return false;
            }
            if (inCategory)
                currentCat.events.push_back(static_cast<unsigned int>(ival));
            else {
                qWarning() << "MNEDescriptionParser: misplaced event";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "nextevent")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                qWarning() << "MNEDescriptionParser: nextevent must be positive";
                return false;
            }
            if (inCategory)
                currentCat.nextEvent = static_cast<unsigned int>(ival);
            else {
                qWarning() << "MNEDescriptionParser: misplaced nextevent";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "prevevent")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                qWarning() << "MNEDescriptionParser: prevevent must be positive";
                return false;
            }
            if (inCategory)
                currentCat.prevEvent = static_cast<unsigned int>(ival);
            else {
                qWarning() << "MNEDescriptionParser: misplaced prevevent";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "ignore")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival < 0) {
                qWarning() << "MNEDescriptionParser: ignore numbers must be positive or zero";
                return false;
            }
            if (inCategory)
                currentCat.ignore = static_cast<unsigned int>(ival);
            else {
                qWarning() << "MNEDescriptionParser: misplaced ignore";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "prevignore")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival < 0) {
                qWarning() << "MNEDescriptionParser: ignore numbers must be positive or zero";
                return false;
            }
            if (inCategory)
                currentCat.prevIgnore = static_cast<unsigned int>(ival);
            else {
                qWarning() << "MNEDescriptionParser: misplaced prevignore";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "nextignore")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival < 0) {
                qWarning() << "MNEDescriptionParser: ignore numbers must be positive or zero";
                return false;
            }
            if (inCategory)
                currentCat.nextIgnore = static_cast<unsigned int>(ival);
            else {
                qWarning() << "MNEDescriptionParser: misplaced nextignore";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "mask")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                qWarning() << "MNEDescriptionParser: mask must be positive";
                return false;
            }
            if (inCategory) {
                currentCat.ignore = static_cast<unsigned int>(ival);
                currentCat.ignore = ~currentCat.ignore;
            } else {
                qWarning() << "MNEDescriptionParser: misplaced mask";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "prevmask")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                qWarning() << "MNEDescriptionParser: mask numbers must be positive";
                return false;
            }
            if (inCategory) {
                currentCat.prevIgnore = static_cast<unsigned int>(ival);
                currentCat.prevIgnore = ~currentCat.prevIgnore;
            } else {
                qWarning() << "MNEDescriptionParser: misplaced prevmask";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "nextmask")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                qWarning() << "MNEDescriptionParser: mask numbers must be positive";
                return false;
            }
            if (inCategory) {
                currentCat.nextIgnore = static_cast<unsigned int>(ival);
                currentCat.nextIgnore = ~currentCat.nextIgnore;
            } else {
                qWarning() << "MNEDescriptionParser: misplaced nextmask";
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
                qWarning() << "MNEDescriptionParser: misplaced delay";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "stderr")) {
            if (inCategory)
                currentCat.doStdErr = true;
            else {
                qWarning() << "MNEDescriptionParser: misplaced stderr";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "abs")) {
            if (inCategory)
                currentCat.doAbs = true;
            else {
                qWarning() << "MNEDescriptionParser: misplaced abs";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "color")) {
            float r, g, b;
            if (!getFloat(in, r) || !getFloat(in, g) || !getFloat(in, b))
                return false;
            if (r < 0.0f || r > 1.0f || g < 0.0f || g > 1.0f || b < 0.0f || b > 1.0f) {
                qWarning() << "MNEDescriptionParser: color components must be within 0 ... 1";
                return false;
            }
            if (inCategory) {
                currentCat.color[0] = r;
                currentCat.color[1] = g;
                currentCat.color[2] = b;
            } else {
                qWarning() << "MNEDescriptionParser: misplaced color";
                return false;
            }
            continue;
        }

        // Try rejection parameters (average-level only)
        bool parseOk;
        if (parseRejectionParam(word, in, desc.rej, parseOk)) {
            if (!(inAverage && !inCategory)) {
                qWarning() << "MNEDescriptionParser: misplaced" << word.c_str();
                return false;
            }
            if (!parseOk)
                return false;
            continue;
        }

        qWarning() << "MNEDescriptionParser: unknown keyword" << word.c_str() << "in" << UTILSLIB::utf8FromPath(fileName).c_str();
    }

    if (desc.categories.empty()) {
        qWarning() << "MNEDescriptionParser: no categories found in" << UTILSLIB::utf8FromPath(fileName).c_str();
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
        qWarning() << "MNEDescriptionParser: cannot open" << UTILSLIB::utf8FromPath(fileName).c_str();
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
                qWarning() << "MNEDescriptionParser: expected '{', got" << word.c_str();
                return false;
            }
            continue;
        }

        if (word == "{") {
            qWarning() << "MNEDescriptionParser: unexpected '{'";
            return false;
        }

        if (word == "}") {
            if (inDef) {
                currentDef.doBaseline = bminSet && bmaxSet;
                if (currentDef.tmin >= currentDef.tmax) {
                    qWarning() << "MNEDescriptionParser: illegal time range in def";
                    return false;
                }
                if (currentDef.doBaseline && currentDef.bmin > currentDef.bmax) {
                    qWarning() << "MNEDescriptionParser: illegal baseline in def";
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
                qWarning() << "MNEDescriptionParser: nested cov";
                return false;
            }
            inCov = true;
            seenCov = true;
            expectBrace = true;
            continue;
        }

        if (UTILSLIB::equalsIgnoreCase(word, "def")) {
            if (!inCov || inDef) {
                qWarning() << "MNEDescriptionParser: misplaced def";
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
                qWarning() << "MNEDescriptionParser: outfile requires a value";
                return false;
            }
            if (inCov && !inDef)
                desc.filename = val;
            else {
                qWarning() << "MNEDescriptionParser: misplaced outfile";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "eventfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                qWarning() << "MNEDescriptionParser: eventfile requires a value";
                return false;
            }
            if (inCov && !inDef)
                desc.eventFile = val;
            else {
                qWarning() << "MNEDescriptionParser: misplaced eventfile";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "logfile")) {
            const std::string val = nextWord(in);
            if (val.empty()) {
                qWarning() << "MNEDescriptionParser: logfile requires a value";
                return false;
            }
            if (inCov && !inDef)
                desc.logFile = val;
            else {
                qWarning() << "MNEDescriptionParser: misplaced logfile";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "keepsamplemean")) {
            if (inCov && !inDef)
                desc.removeSampleMean = false;
            else {
                qWarning() << "MNEDescriptionParser: misplaced keepsamplemean";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "fixskew")) {
            if (inCov && !inDef)
                desc.fixSkew = true;
            else {
                qWarning() << "MNEDescriptionParser: misplaced fixskew";
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
                qWarning() << "MNEDescriptionParser: misplaced tmin";
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
                qWarning() << "MNEDescriptionParser: misplaced tmax";
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
                qWarning() << "MNEDescriptionParser: misplaced basemin";
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
                qWarning() << "MNEDescriptionParser: misplaced basemax";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "event")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                qWarning() << "MNEDescriptionParser: event must be positive";
                return false;
            }
            if (inDef)
                currentDef.events.push_back(static_cast<unsigned int>(ival));
            else {
                qWarning() << "MNEDescriptionParser: misplaced event";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "ignore")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival < 0) {
                qWarning() << "MNEDescriptionParser: ignore numbers must be positive or zero";
                return false;
            }
            if (inDef)
                currentDef.ignore = static_cast<unsigned int>(ival);
            else {
                qWarning() << "MNEDescriptionParser: misplaced ignore";
                return false;
            }
            continue;
        }
        if (UTILSLIB::equalsIgnoreCase(word, "mask")) {
            int ival;
            if (!getInt(in, ival))
                return false;
            if (ival <= 0) {
                qWarning() << "MNEDescriptionParser: mask numbers must be positive";
                return false;
            }
            if (inDef) {
                currentDef.ignore = static_cast<unsigned int>(ival);
                currentDef.ignore = ~currentDef.ignore;
            } else {
                qWarning() << "MNEDescriptionParser: misplaced mask";
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
                qWarning() << "MNEDescriptionParser: misplaced delay";
                return false;
            }
            continue;
        }

        // Rejection parameters (cov-level only)
        bool parseOk;
        if (parseRejectionParam(word, in, desc.rej, parseOk)) {
            if (!(inCov && !inDef)) {
                qWarning() << "MNEDescriptionParser: misplaced" << word.c_str();
                return false;
            }
            if (!parseOk)
                return false;
            continue;
        }

        qWarning() << "MNEDescriptionParser: unknown keyword" << word.c_str() << "in" << UTILSLIB::utf8FromPath(fileName).c_str();
    }

    if (!seenCov) {
        qWarning() << "MNEDescriptionParser: nothing useful read from" << UTILSLIB::utf8FromPath(fileName).c_str();
        return false;
    }

    return true;
}
