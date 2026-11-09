//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     report.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.2.1
 * @date     May 2026
 * @brief    Implementation of @ref REPORTLIB::Report.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "report.h"

#include <base/log.h>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
#include <utility>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace REPORTLIB;
using namespace BASELIB;

//=============================================================================================================
// DEFINE STATIC METHODS
//=============================================================================================================

namespace
{

constexpr std::string_view kStyle =
    "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
    "max-width: 960px; margin: 0 auto; padding: 20px; color: #333; }\n"
    "h1 { color: #2c3e50; border-bottom: 2px solid #3498db; padding-bottom: 10px; }\n"
    "h2 { color: #2980b9; margin-top: 30px; }\n"
    "table { border-collapse: collapse; width: 100%; margin: 10px 0; }\n"
    "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n"
    "th { background-color: #3498db; color: white; }\n"
    "tr:nth-child(even) { background-color: #f2f2f2; }\n"
    "dl { margin: 10px 0; }\n"
    "dt { font-weight: bold; margin-top: 8px; }\n"
    "dd { margin-left: 20px; color: #555; }\n"
    "pre { background-color: #f8f8f8; border: 1px solid #ddd; padding: 12px; border-radius: 4px; overflow-x: auto; }\n"
    "code { font-family: 'SF Mono', Menlo, Monaco, monospace; font-size: 0.9em; }\n"
    ".footer { margin-top: 40px; padding-top: 10px; border-top: 1px solid #ddd; color: #999; font-size: 0.85em; }\n";

/** Appends an element whose text content is escaped, e.g. <td>text</td>. */
void appendElement(std::string& html, std::string_view tag, std::string_view text, std::string_view indent = "")
{
    std::format_to(std::back_inserter(html), "{}<{}>{}</{}>\n", indent, tag, html_escape(text), tag);
}

} // namespace

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

std::string REPORTLIB::html_escape(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&#39;";
                break;
            default:
                escaped += c;
        }
    }
    return escaped;
}

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

Report::Report(std::string title)
: m_title(std::move(title))
{
}

//=============================================================================================================

void Report::add_section(std::string_view title, std::string html)
{
    m_sections.push_back({std::string(title), std::move(html)});
}

//=============================================================================================================

void Report::add_text(std::string_view title, std::string_view text)
{
    add_section(title, "<p>" + html_escape(text) + "</p>");
}

//=============================================================================================================

void Report::add_table(std::string_view title, const StringList& headers, const std::vector<StringList>& rows)
{
    std::string html = "<table>\n<thead><tr>\n";
    for (const std::string& header : headers) {
        appendElement(html, "th", header, "  ");
    }
    html += "</tr></thead>\n<tbody>\n";
    for (const StringList& row : rows) {
        html += "<tr>\n";
        for (const std::string& cell : row) {
            appendElement(html, "td", cell, "  ");
        }
        html += "</tr>\n";
    }
    html += "</tbody>\n</table>";
    add_section(title, std::move(html));
}

//=============================================================================================================

void Report::add_key_value(std::string_view title, const std::vector<std::pair<std::string, std::string>>& pairs)
{
    std::string html = "<dl>\n";
    for (const auto& [key, value] : pairs) {
        appendElement(html, "dt", key, "  ");
        appendElement(html, "dd", value, "  ");
    }
    html += "</dl>";
    add_section(title, std::move(html));
}

//=============================================================================================================

void Report::add_code(std::string_view title, std::string_view code)
{
    add_section(title, "<pre><code>" + html_escape(code) + "</code></pre>");
}

//=============================================================================================================

std::size_t Report::section_count() const
{
    return m_sections.size();
}

//=============================================================================================================

const std::string& Report::title() const
{
    return m_title;
}

//=============================================================================================================

std::string Report::to_html() const
{
    std::string html = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
                       "<meta charset=\"UTF-8\">\n"
                       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    appendElement(html, "title", m_title);
    html += "<style>\n";
    html += kStyle;
    html += "</style>\n</head>\n<body>\n";
    appendElement(html, "h1", m_title);
    for (const Section& section : m_sections) {
        appendElement(html, "h2", section.title);
        html += section.html;
        html += '\n';
    }
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    std::format_to(std::back_inserter(html), "<div class=\"footer\">\n<p>Generated by MNE-CPP on {:%FT%TZ}</p>\n</div>\n", now);
    html += "</body>\n</html>\n";
    return html;
}

//=============================================================================================================

bool Report::save(const std::filesystem::path& path) const
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        Log::warning("[Report::save] Cannot open: {}", utf8FromPath(path));
        return false;
    }
    file << to_html();
    return static_cast<bool>(file);
}
