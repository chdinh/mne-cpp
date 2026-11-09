//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     string_utils.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Definition of the text and path helpers in base/string_utils.h.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "string_utils.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <algorithm>
#include <charconv>
#include <cmath>
#include <clocale>
#include <cstdlib>
#include <limits>

#ifdef __APPLE__
#include <xlocale.h>
#endif

//=============================================================================================================
// DEFINE STATICS AND LOCAL METHODS
//=============================================================================================================

namespace
{

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

double strtodC(const char* text)
{
#ifdef _WIN32
    static _locale_t c = _create_locale(LC_NUMERIC, "C");
    return _strtod_l(text, nullptr, c);
#else
    static locale_t c = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    return strtod_l(text, nullptr, c);
#endif
}

/** [+-]? (digits [. digits?] | . digits) ([eE] [+-]? digits)?, so every standard library parses the same set. */
bool isDecimal(std::string_view text)
{
    std::size_t i = 0;
    const auto digits = [&] {
        const std::size_t start = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
        }
        return i - start;
    };
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        ++i;
    }
    std::size_t mantissa = digits();
    if (i < text.size() && text[i] == '.') {
        ++i;
        mantissa += digits();
    }
    if (mantissa == 0) {
        return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        if (digits() == 0) {
            return false;
        }
    }
    return i == text.size();
}

/** from_chars takes neither a leading '+' nor inf/nan spelled the way QString accepts them. */
std::string_view stripPlus(std::string_view text)
{
    return text.size() > 1 && text.front() == '+' && text[1] != '-' && text[1] != '+' ? text.substr(1) : text;
}

} // namespace

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

std::string_view BASELIB::trimmed(std::string_view text)
{
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

//=============================================================================================================

BASELIB::StringList BASELIB::split(std::string_view text, char separator, bool skipEmpty)
{
    StringList fields;
    for (std::size_t start = 0;;) {
        const std::size_t end = text.find(separator, start);
        const std::string_view field = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!skipEmpty || !field.empty()) {
            fields.emplace_back(field);
        }
        if (end == std::string_view::npos) {
            return fields;
        }
        start = end + 1;
    }
}

//=============================================================================================================

int BASELIB::indexOf(const StringList& list, std::string_view name)
{
    const auto it = std::find(list.begin(), list.end(), name);
    return it == list.end() ? -1 : static_cast<int>(it - list.begin());
}

//=============================================================================================================

bool BASELIB::contains(const StringList& list, std::string_view name)
{
    return std::find(list.begin(), list.end(), name) != list.end();
}

//=============================================================================================================

std::string BASELIB::join(const StringList& list, std::string_view separator)
{
    std::string out;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (i > 0)
            out += separator;
        out += list[i];
    }
    return out;
}

//=============================================================================================================

std::string BASELIB::toLower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}

//=============================================================================================================

bool BASELIB::equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

//=============================================================================================================

std::optional<double> BASELIB::toDouble(std::string_view text)
{
    text = stripPlus(trimmed(text));
    const std::string lowered = toLower(text);
    const bool negative = !lowered.empty() && lowered.front() == '-';
    const std::string_view word = negative ? std::string_view(lowered).substr(1) : std::string_view(lowered);
    if (word == "inf") {
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    if (word == "nan" && !negative) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (!isDecimal(text)) {
        return std::nullopt;
    }
    // Floating-point std::from_chars needs macOS 26 with libc++, and streams disagree on denormals across
    // standard libraries, so the C library parses in the "C" locale whatever the application set.
    const std::string copy(text);
    const double value = strtodC(copy.c_str());
    const bool nonZeroDigit = std::any_of(text.begin(), std::find_if(text.begin(), text.end(), [](char c) { return c == 'e' || c == 'E'; }),
                                          [](char c) { return c >= '1' && c <= '9'; });
    if (std::isinf(value) || (value == 0.0 && nonZeroDigit)) {
        return std::nullopt; // overflow or underflow to zero; QString rejects both and keeps denormals
    }
    return value;
}

//=============================================================================================================

std::optional<int> BASELIB::toInt(std::string_view text)
{
    text = stripPlus(trimmed(text));
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

//=============================================================================================================

std::filesystem::path BASELIB::pathFromUtf8(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

//=============================================================================================================

std::string BASELIB::utf8FromPath(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}
