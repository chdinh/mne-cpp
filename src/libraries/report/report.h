//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     report.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.2.1
 * @date     May 2026
 * @brief    HTML report builder, mne-cpp's counterpart of @c mne.Report for summarising analysis pipelines.
 *
 * @ref REPORTLIB::Report collects titled sections (free text, tables, key-value pairs, preformatted code) and emits a
 * self-contained HTML document that opens directly in a browser or can be attached to an email. The output has no
 * dependencies (inline CSS, no JavaScript), so it stays readable in restricted environments where MEG/EEG processing
 * reports are typically reviewed. All text is HTML-escaped; strings are UTF-8.
 */

#ifndef REPORTLIB_REPORT_H
#define REPORTLIB_REPORT_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "report_global.h"

#include <base/string_utils.h>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE REPORTLIB
//=============================================================================================================

namespace REPORTLIB
{

//=============================================================================================================
/**
 * @brief Simple HTML report builder for MEG/EEG analysis results.
 *
 * @snippet ex_report/main.cpp report_usage
 */
class REPORTSHARED_EXPORT Report
{
public:
    //=========================================================================================================
    /**
     * Constructs an empty report.
     *
     * @param[in] title  Report title, used for the HTML page title and the main heading.
     */
    explicit Report(std::string title = "MNE-CPP Report");

    //=========================================================================================================
    /**
     * Adds a paragraph of plain text.
     *
     * @param[in] title  Section heading.
     * @param[in] text   Text; it is HTML-escaped.
     */
    void add_text(std::string_view title, std::string_view text);

    //=========================================================================================================
    /**
     * Adds a table.
     *
     * @param[in] title    Section heading.
     * @param[in] headers  Column headers.
     * @param[in] rows     Cells, one list per row.
     */
    void add_table(std::string_view title, const BASELIB::StringList& headers, const std::vector<BASELIB::StringList>& rows);

    //=========================================================================================================
    /**
     * Adds a list of key-value pairs.
     *
     * @param[in] title  Section heading.
     * @param[in] pairs  (key, value) pairs, in display order.
     */
    void add_key_value(std::string_view title, const std::vector<std::pair<std::string, std::string>>& pairs);

    //=========================================================================================================
    /**
     * Adds a preformatted code block.
     *
     * @param[in] title  Section heading.
     * @param[in] code   Code text; it is HTML-escaped.
     */
    void add_code(std::string_view title, std::string_view code);

    //=========================================================================================================
    /**
     * @return Number of sections added so far.
     */
    std::size_t section_count() const;

    //=========================================================================================================
    /**
     * @return Self-contained HTML document with the title, all sections in the order they were added, and a
     *         footer with the generation time (UTC).
     */
    std::string to_html() const;

    //=========================================================================================================
    /**
     * Writes @ref to_html to a file.
     *
     * @param[in] path  Output file; an existing file is overwritten.
     *
     * @return True on success, false if the file could not be written.
     */
    bool save(const std::filesystem::path& path) const;

    //=========================================================================================================
    /**
     * @return The title passed at construction.
     */
    const std::string& title() const;

private:
    void add_section(std::string_view title, std::string html);

    /** A titled block of ready-to-insert HTML. */
    struct Section
    {
        std::string title; /**< Heading, not yet escaped. */
        std::string html;  /**< Escaped HTML content. */
    };

    std::string m_title;             /**< Page title and main heading. */
    std::vector<Section> m_sections; /**< Sections in the order they were added. */
};

//=============================================================================================================
/**
 * Escapes the characters that are special in HTML text and attribute values: & < > " '.
 *
 * @param[in] text   UTF-8 text.
 *
 * @return The escaped text.
 */
REPORTSHARED_EXPORT std::string html_escape(std::string_view text);

} // namespace REPORTLIB

#endif // REPORTLIB_REPORT_H
