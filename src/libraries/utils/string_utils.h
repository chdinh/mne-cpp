//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     string_utils.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Text and path helpers in standard C++ for the QString calls the libraries use most.
 *
 * Strings are UTF-8; case and whitespace handling covers ASCII only, which is what file formats and channel
 * names need.
 */

#ifndef UTILS_STRING_UTILS_H
#define UTILS_STRING_UTILS_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "utils_global.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE UTILSLIB
//=============================================================================================================

namespace UTILSLIB
{

/**
 * @param[in] text   Any text.
 *
 * @return @p text without leading and trailing ASCII whitespace, like QString::trimmed().
 */
UTILSSHARED_EXPORT std::string_view trimmed(std::string_view text);

/**
 * @param[in] text        Any text.
 * @param[in] separator   Field separator.
 * @param[in] skipEmpty   Whether to drop empty fields, like Qt::SkipEmptyParts.
 *
 * @return The fields of @p text, like QString::split(); empty text gives one empty field unless skipped.
 */
UTILSSHARED_EXPORT std::vector<std::string> split(std::string_view text, char separator, bool skipEmpty = false);

/**
 * @param[in] text   UTF-8 text.
 *
 * @return @p text with ASCII letters in lower case; other bytes unchanged.
 */
UTILSSHARED_EXPORT std::string toLower(std::string_view text);

/**
 * @param[in] a   UTF-8 text.
 * @param[in] b   UTF-8 text.
 *
 * @return Whether @p a and @p b are equal when ASCII letters are compared without case.
 */
UTILSSHARED_EXPORT bool equalsIgnoreCase(std::string_view a, std::string_view b);

/**
 * @param[in] text   A decimal number with optional surrounding whitespace, independent of the locale.
 *
 * @return The value, or nothing if @p text is not entirely a finite or infinite number, like QString::toDouble().
 */
UTILSSHARED_EXPORT std::optional<double> toDouble(std::string_view text);

/**
 * @param[in] text   A decimal integer with optional surrounding whitespace.
 *
 * @return The value, or nothing if @p text is not entirely an int in range, like QString::toInt().
 */
UTILSSHARED_EXPORT std::optional<int> toInt(std::string_view text);

/**
 * @param[in] utf8   A path in UTF-8.
 *
 * @return The path; unlike std::filesystem::path(std::string), correct for non-ASCII names on Windows too.
 */
UTILSSHARED_EXPORT std::filesystem::path pathFromUtf8(std::string_view utf8);

/**
 * @param[in] path   Any path.
 *
 * @return @p path in UTF-8 on every platform.
 */
UTILSSHARED_EXPORT std::string utf8FromPath(const std::filesystem::path& path);

} // namespace UTILSLIB

#endif // UTILS_STRING_UTILS_H
