//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     mne_description_parser.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     February 2026
 * @brief    Parser for the legacy MNE-C @c .ave / @c .cov plain-text description files.
 *
 * @ref MNELIB::MNEDescriptionParser tokenises the @c "average { ... }"
 * / @c "cov { ... }" stanzas used by @c mne_process_raw and turns them
 * into @c FIFFLIB::AverageDescription / @c MNELIB::CovDescription records that the epoch /
 * covariance machinery downstream can act on.
 */

#ifndef MNE_DESCRIPTION_PARSER_H
#define MNE_DESCRIPTION_PARSER_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "mne_global.h"
#include "mne_process_description.h"

#include <QString>
#include <QTextStream>

//=============================================================================================================
// DEFINE NAMESPACE MNELIB
//=============================================================================================================

namespace MNELIB
{

//=============================================================================================================
/**
 * Parses MNE-C style averaging (.ave) and covariance (.cov) description files.
 *
 * File format is a keyword-value text format with nested blocks:
 *   average { ... category { ... } ... }
 *   cov { ... def { ... } ... }
 *
 * Lines starting with '#' are comments.
 *
 * @snippet ex_mne_api/main.cpp mne_description_parser_usage
 */
class MNESHARED_EXPORT MNEDescriptionParser
{
public:
    /**
     * Parse an averaging description file.
     *
     * @param[in] fileName  Path to the .ave file.
     * @param[out] desc     Parsed description.
     * @return true if successful, false on error.
     */
    static bool parseAverageFile(const std::filesystem::path& fileName, FIFFLIB::AverageDescription& desc);

    /**
     * Parse a covariance description file.
     *
     * @param[in] fileName  Path to the .cov file.
     * @param[out] desc     Parsed description.
     * @return true if successful, false on error.
     */
    static bool parseCovarianceFile(const std::filesystem::path& fileName, CovDescription& desc);

private:
    /**
     * Skip comment lines (starting with '#') and whitespace.
     */
    static void skipComments(std::istream& in);

    /**
     * Read the next whitespace-delimited word, handling quoted strings.
     * Returns empty string on EOF.
     */
    static std::string nextWord(std::istream& in);

    /**
     * Read and parse an integer value.
     */
    static bool getInt(std::istream& in, int& val);

    /**
     * Read and parse a float value.
     */
    static bool getFloat(std::istream& in, float& val);

    /**
     * Parse rejection parameters common to both .ave and .cov files.
     * Returns true if the keyword was handled, false otherwise (unrecognized keyword).
     */
    static bool parseRejectionParam(std::string_view keyword, std::istream& in,
                                    FIFFLIB::RejectionParams& rej, bool& ok);
};

} // namespace MNELIB

#endif // MNE_DESCRIPTION_PARSER_H
