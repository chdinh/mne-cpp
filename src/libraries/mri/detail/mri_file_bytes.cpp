//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     mri_file_bytes.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Internal whole-file and gzip reading for the MRI readers.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "mri_file_bytes.h"

#include <utils/string_utils.h>

#include <zlib.h>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <fstream>
#include <iterator>

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

std::optional<std::vector<std::byte>> MRILIB::detail::readFileBytes(const std::string& path, bool gzip, std::string& error)
{
    std::ifstream file(UTILSLIB::pathFromUtf8(path), std::ios::binary);
    if (!file) {
        error = "could not open " + path;
        return std::nullopt;
    }
    std::vector<std::byte> content;
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    content.resize(static_cast<std::size_t>(fileSize > 0 ? fileSize : 0));
    if (!file.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(content.size()))) {
        error = "could not read " + path;
        return std::nullopt;
    }
    if (!gzip) {
        return content;
    }
    if (content.empty()) {
        error = "file is empty: " + path;
        return std::nullopt;
    }

    z_stream strm = {};
    // MAX_WBITS + 16 selects the gzip wrapper
    if (inflateInit2(&strm, MAX_WBITS + 16) != Z_OK) {
        error = "inflateInit2 failed";
        return std::nullopt;
    }
    strm.next_in = reinterpret_cast<Bytef*>(content.data());
    strm.avail_in = static_cast<uInt>(content.size());

    constexpr std::size_t kChunk = 256 * 1024;
    std::vector<std::byte> raw;
    int ret = Z_OK;
    do {
        raw.resize(raw.size() + kChunk);
        strm.next_out = reinterpret_cast<Bytef*>(raw.data() + raw.size() - kChunk);
        strm.avail_out = static_cast<uInt>(kChunk);

        ret = inflate(&strm, Z_NO_FLUSH);
        // With all input consumed and output room left, a stream that has not ended is cut short
        const bool truncated = ret != Z_STREAM_END && strm.avail_in == 0 && strm.avail_out > 0;
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR || truncated) {
            error = "inflate failed for " + path + " - zlib error: " + std::to_string(truncated ? Z_BUF_ERROR : ret);
            inflateEnd(&strm);
            return std::nullopt;
        }
    } while (ret != Z_STREAM_END);

    raw.resize(raw.size() - strm.avail_out);
    inflateEnd(&strm);
    return raw;
}
