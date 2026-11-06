//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     byte_stream.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Definition of @ref UTILSLIB::MemoryByteStream and @ref UTILSLIB::FileByteStream.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "byte_stream.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <algorithm>
#include <cstring>
#include <system_error>
#include <utility>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace UTILSLIB;

//=============================================================================================================
// DEFINE STATICS AND LOCAL METHODS
//=============================================================================================================

namespace
{

bool seekFile(std::FILE* file, std::uint64_t position)
{
#ifdef _WIN32
    return _fseeki64(file, static_cast<long long>(position), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(position), SEEK_SET) == 0;
#endif
}

std::FILE* openFile(const std::filesystem::path& path, FileByteStream::Mode mode)
{
#ifdef _WIN32
    const auto* flags = mode == FileByteStream::Mode::Read ? L"rb" : (mode == FileByteStream::Mode::Write ? L"w+b" : L"r+b");
    std::FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), flags) == 0 ? file : nullptr;
#else
    const auto* flags = mode == FileByteStream::Mode::Read ? "rb" : (mode == FileByteStream::Mode::Write ? "w+b" : "r+b");
    return std::fopen(path.c_str(), flags);
#endif
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

MemoryByteStream::MemoryByteStream(std::vector<std::byte> data)
: m_data(std::move(data))
{
}

//=============================================================================================================

std::size_t MemoryByteStream::read(void* data, std::size_t bytes)
{
    const std::size_t n = std::min(bytes, m_data.size() - m_pos);
    if (n > 0) {
        std::memcpy(data, m_data.data() + m_pos, n);
        m_pos += n;
    }
    return n;
}

//=============================================================================================================

std::size_t MemoryByteStream::write(const void* data, std::size_t bytes)
{
    if (bytes == 0) {
        return 0;
    }
    if (m_pos + bytes > m_data.size()) {
        m_data.resize(m_pos + bytes);
    }
    std::memcpy(m_data.data() + m_pos, data, bytes);
    m_pos += bytes;
    return bytes;
}

//=============================================================================================================

bool MemoryByteStream::seek(std::uint64_t position)
{
    if (position > m_data.size()) {
        return false;
    }
    m_pos = static_cast<std::size_t>(position);
    return true;
}

//=============================================================================================================

std::uint64_t MemoryByteStream::pos() const
{
    return m_pos;
}

//=============================================================================================================

std::uint64_t MemoryByteStream::size() const
{
    return m_data.size();
}

//=============================================================================================================

std::unique_ptr<FileByteStream> FileByteStream::open(const std::filesystem::path& path, Mode mode)
{
    std::FILE* file = openFile(path, mode);
    if (!file) {
        return nullptr;
    }
    std::unique_ptr<FileByteStream> stream(new FileByteStream(file, mode, path));
    std::error_code ec;
    const std::uintmax_t bytes = std::filesystem::file_size(path, ec);
    stream->m_size = ec ? 0 : static_cast<std::uint64_t>(bytes);
    return stream;
}

//=============================================================================================================

FileByteStream::FileByteStream(std::FILE* file, Mode mode, std::filesystem::path path)
: m_file(file)
, m_mode(mode)
, m_path(std::move(path))
{
}

//=============================================================================================================

FileByteStream::~FileByteStream()
{
    close();
}

//=============================================================================================================

void FileByteStream::close()
{
    if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
    }
}

//=============================================================================================================

void FileByteStream::switchDirection(bool writing)
{
    if (writing != m_lastWrite) {
        seekFile(m_file, m_pos);
        m_lastWrite = writing;
    }
}

//=============================================================================================================

std::size_t FileByteStream::read(void* data, std::size_t bytes)
{
    if (!m_file) {
        return 0;
    }
    switchDirection(false);
    const std::size_t n = std::fread(data, 1, bytes, m_file);
    m_pos += n;
    return n;
}

//=============================================================================================================

std::size_t FileByteStream::write(const void* data, std::size_t bytes)
{
    if (!m_file || m_mode == Mode::Read || bytes == 0) {
        return 0;
    }
    switchDirection(true);
    const std::size_t n = std::fwrite(data, 1, bytes, m_file);
    m_pos += n;
    m_size = std::max(m_size, m_pos);
    return n;
}

//=============================================================================================================

bool FileByteStream::seek(std::uint64_t position)
{
    if (!m_file || position > m_size || !seekFile(m_file, position)) {
        return false;
    }
    m_pos = position;
    return true;
}

//=============================================================================================================

std::uint64_t FileByteStream::pos() const
{
    return m_pos;
}

//=============================================================================================================

std::uint64_t FileByteStream::size() const
{
    return m_size;
}

//=============================================================================================================

bool FileByteStream::isOpen() const
{
    return m_file != nullptr;
}

//=============================================================================================================

bool FileByteStream::reopen()
{
    if (m_file)
        return true;
    // A written file is opened for update, so reopening does not truncate it
    m_file = openFile(m_path, m_mode == Mode::Read ? Mode::Read : Mode::ReadWrite);
    if (!m_file)
        return false;
    std::error_code ec;
    const std::uintmax_t bytes = std::filesystem::file_size(m_path, ec);
    m_size = ec ? 0 : static_cast<std::uint64_t>(bytes);
    m_pos = 0;
    m_lastWrite = false;
    return true;
}

//=============================================================================================================

bool FileByteStream::flush()
{
    return m_file && std::fflush(m_file) == 0;
}
