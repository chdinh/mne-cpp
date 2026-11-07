//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     error.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Error: why a reader or writer failed, returned in std::expected.
 */

#ifndef UTILS_ERROR_H
#define UTILS_ERROR_H

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <string>

//=============================================================================================================
// DEFINE NAMESPACE UTILSLIB
//=============================================================================================================

namespace UTILSLIB
{

//=============================================================================================================
/**
 * What went wrong, so callers can react without parsing the message.
 */
enum class ErrorCode
{
    Io,      /**< The file cannot be opened, read or written. */
    Format,  /**< The file is not of the expected kind or is damaged. */
    NotFound /**< The file is valid but does not hold what was asked for. */
};

//=============================================================================================================
/**
 * @brief The error value of the std::expected that readers and writers return.
 *
 * @snippet ex_fiff_api/main.cpp fiff_coord_trans_usage
 */
struct Error
{
    ErrorCode code;      /**< The kind of failure. */
    std::string message; /**< Names the cause and the file, ready for printing. */
};

} // namespace UTILSLIB

#endif // UTILS_ERROR_H
