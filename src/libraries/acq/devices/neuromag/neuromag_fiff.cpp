//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2012-2026 MNE-CPP Authors
 *
 * @file     neuromag_fiff.cpp
 * @author   Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
 *           Matti Hamalainen <msh@nmr.mgh.harvard.edu>;
 *           Gabriel Motta <gabrielbenmotta@gmail.com>;
 *           Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    0.1.0
 * @date     July, 2012
 * @brief    FIFF tags of the Neuromag acquisition stream without the FIFF library.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "neuromag_fiff.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB::detail;

//=============================================================================================================
// STATIC HELPERS
//=============================================================================================================

namespace
{

constexpr int kTypeByte = 1;
constexpr int kTypeShort = 2;
constexpr int kTypeDouble = 5;
constexpr int kTypeJulian = 6;
constexpr int kTypeUshort = 7;
constexpr int kTypeUint = 8;
constexpr int kTypeDauPack16 = 16;
constexpr int kTypeComplexFloat = 20;
constexpr int kTypeComplexDouble = 21;
constexpr int kTypeOldPack = 23;
constexpr int kTypeDirEntry = 32;
constexpr int kTypeDigPoint = 33;
constexpr int kTypeCoordTrans = 35;
constexpr int kTypeDigString = 36;
constexpr int kBaseMask = 0x00000FFF;
constexpr int kMatrix = 0x40000000;
constexpr int kSparseMask = 0x00FF0000;
constexpr std::size_t kNameOffset = 80; // the channel name follows 20 words
constexpr std::int32_t kFileVersion = (1 << 16) | 3;

std::int32_t byteswap(std::int32_t value) noexcept
{
    auto bits = static_cast<std::uint32_t>(value);
    bits = (bits >> 24) | ((bits >> 8) & 0x0000FF00U) | ((bits << 8) & 0x00FF0000U) | (bits << 24);
    return static_cast<std::int32_t>(bits);
}

void swap_words(std::span<std::byte> data, std::size_t width) noexcept
{
    for (std::size_t i = 0; i + width <= data.size(); i += width) {
        std::reverse(data.begin() + static_cast<std::ptrdiff_t>(i), data.begin() + static_cast<std::ptrdiff_t>(i + width));
    }
}

/** @return Bytes per element of a base type, 0 for byte strings and types without a known layout. */
std::size_t element_width(int base) noexcept
{
    switch (base) {
        case kTypeShort:
        case kTypeUshort:
        case kTypeDauPack16:
            return 2;
        case fiff::kTypeInt:
        case fiff::kTypeFloat:
        case kTypeJulian:
        case kTypeUint:
        case kTypeComplexFloat:
        case fiff::kTypeId:
        case kTypeDirEntry:
        case kTypeDigPoint:
        case kTypeCoordTrans:
        case kTypeDigString:
            return 4;
        case kTypeDouble:
        case kTypeComplexDouble:
            return 8;
        default:
            return 0;
    }
}

template<typename T>
T load(std::span<const std::byte> data, std::size_t offset) noexcept
{
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

template<typename T>
void store(std::vector<std::byte>& out, T value)
{
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

/** Appends a tag in big-endian byte order. */
void append_tag(std::vector<std::byte>& file, int kind, int type, std::span<const std::byte> native, int next = 0)
{
    std::vector<std::byte> data(native.begin(), native.end());
    if constexpr (std::endian::native == std::endian::little) {
        swap_tag_bytes(type, data, false);
    }
    for (std::int32_t word : {kind, type, static_cast<std::int32_t>(data.size()), next}) {
        if constexpr (std::endian::native == std::endian::little) {
            word = byteswap(word);
        }
        store(file, word);
    }
    file.insert(file.end(), data.begin(), data.end());
}

std::vector<std::byte> int_data(std::int32_t value)
{
    std::vector<std::byte> data;
    store(data, value);
    return data;
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

std::optional<std::int32_t> NeuromagTag::as_int() const noexcept
{
    if (data.size() < sizeof(std::int32_t)) {
        return std::nullopt;
    }
    return load<std::int32_t>(data, 0);
}

//=============================================================================================================

std::optional<float> NeuromagTag::as_float() const noexcept
{
    if (data.size() < sizeof(float)) {
        return std::nullopt;
    }
    return load<float>(data, 0);
}

//=============================================================================================================

std::optional<NeuromagChannel> NeuromagChannel::parse(std::span<const std::byte> data)
{
    if (data.size() < kSize) {
        return std::nullopt;
    }
    NeuromagChannel ch;
    ch.scan_no = load<std::int32_t>(data, 0);
    ch.log_no = load<std::int32_t>(data, 4);
    ch.kind = load<std::int32_t>(data, 8);
    ch.range = load<float>(data, 12);
    ch.cal = load<float>(data, 16);
    ch.coil_type = load<std::int32_t>(data, 20);
    for (std::size_t i = 0; i < ch.loc.size(); ++i) {
        ch.loc[i] = load<float>(data, 24 + 4 * i);
    }
    ch.unit = load<std::int32_t>(data, 72);
    ch.unit_mul = load<std::int32_t>(data, 76);
    const auto* name = reinterpret_cast<const char*>(data.data() + kNameOffset);
    ch.name.assign(name, strnlen(name, kSize - kNameOffset));
    return ch;
}

//=============================================================================================================

std::vector<std::byte> NeuromagChannel::serialize() const
{
    std::vector<std::byte> data;
    data.reserve(kSize);
    store(data, scan_no);
    store(data, log_no);
    store(data, kind);
    store(data, range);
    store(data, cal);
    store(data, coil_type);
    for (const float value : loc) {
        store(data, value);
    }
    store(data, unit);
    store(data, unit_mul);
    std::array<char, kSize - kNameOffset> chars{};
    std::copy_n(name.begin(), std::min(name.size(), chars.size() - 1), chars.begin());
    const auto* bytes = reinterpret_cast<const std::byte*>(chars.data());
    data.insert(data.end(), bytes, bytes + chars.size());
    return data;
}

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

void ACQLIB::detail::swap_tag_bytes(int type, std::span<std::byte> data, bool from_big) noexcept
{
    const int base = type & kBaseMask;
    if ((type & kMatrix) != 0) {
        const std::size_t width = element_width(base);
        if (width == 4 || (type & kSparseMask) != 0) {
            swap_words(data, 4); // elements, indices and dimensions are words
            return;
        }
        if (width != 8 || data.size() < 4) {
            return;
        }
        // Dense matrices of doubles end with their dimensions and then their number
        std::int32_t ndim = load<std::int32_t>(data, data.size() - 4);
        const bool native_big = std::endian::native == std::endian::big;
        if (from_big != native_big) {
            ndim = byteswap(ndim);
        }
        const std::size_t trailer = 4 * (static_cast<std::size_t>(std::max(ndim, 0)) + 1);
        if (trailer > data.size()) {
            return;
        }
        swap_words(data.first(data.size() - trailer), 8);
        swap_words(data.last(trailer), 4);
        return;
    }
    switch (base) {
        case fiff::kTypeChInfo:
            swap_words(data.first(std::min(data.size(), kNameOffset)), 4);
            return;
        case kTypeOldPack:
            swap_words(data.first(std::min<std::size_t>(data.size(), 8)), 4);
            if (data.size() > 8) {
                swap_words(data.subspan(8), 2);
            }
            return;
        case kTypeByte:
        case fiff::kTypeString:
        case fiff::kTypeVoid:
            return;
        default:
            if (const std::size_t width = element_width(base); width > 1) {
                swap_words(data, width);
            }
            return;
    }
}

//=============================================================================================================

void ACQLIB::detail::big_to_native(int type, std::span<std::byte> data) noexcept
{
    if constexpr (std::endian::native == std::endian::little) {
        swap_tag_bytes(type, data, true);
    }
}

//=============================================================================================================

std::vector<std::byte> ACQLIB::detail::meas_info_file(std::span<const NeuromagTag> meas_info)
{
    std::vector<std::byte> file;
    std::vector<std::byte> id;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const std::int32_t word : {kFileVersion, 0, 0, static_cast<std::int32_t>(seconds), 0}) {
        store(id, word);
    }
    append_tag(file, fiff::kFileId, fiff::kTypeId, id);
    append_tag(file, fiff::kDirPointer, fiff::kTypeInt, int_data(fiff::kNextNone));
    append_tag(file, fiff::kBlockStart, fiff::kTypeInt, int_data(fiff::kBlockMeas));
    for (const NeuromagTag& tag : meas_info) {
        append_tag(file, tag.kind, tag.type, tag.data);
    }
    append_tag(file, fiff::kBlockEnd, fiff::kTypeInt, int_data(fiff::kBlockMeas));
    append_tag(file, fiff::kNop, fiff::kTypeVoid, {}, fiff::kNextNone);
    return file;
}
