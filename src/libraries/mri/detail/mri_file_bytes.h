//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     mri_file_bytes.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Internal whole-file and gzip reading for the MRI readers; not installed.
 */

#ifndef MRI_FILE_BYTES_H
#define MRI_FILE_BYTES_H

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE MRILIB::detail
//=============================================================================================================

namespace MRILIB::detail
{

//=============================================================================================================
/**
 * @param[in]  path   UTF-8 path of the file.
 * @param[in]  gzip   True to inflate a gzip file.
 * @param[out] error  Why the file could not be read.
 *
 * @return The (inflated) file content, or nothing on error. A gzip stream that ends early is an error.
 */
std::optional<std::vector<std::byte>> readFileBytes(const std::string& path, bool gzip, std::string& error);

} // namespace MRILIB::detail

#endif // MRI_FILE_BYTES_H
