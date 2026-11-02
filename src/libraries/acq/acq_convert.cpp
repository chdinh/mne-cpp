//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_convert.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    decode(), encode() and the bit-field converters.
 *
 * Each converter is one loop nest per (format, destination type), instantiated from templates: the format
 * is a compile-time parameter, so the inner loops over channels and samples contain no branch on it and the
 * compiler vectorises them. Blocks of kBlock samples keep source and destination of a block in cache when
 * the destination is transposed relative to the ring. The library is built with floating-point
 * contraction off, so the documented arithmetic (multiply, then add, no FMA) holds on every target.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_convert.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;

namespace
{

constexpr std::size_t kBlock = 64;

template<typename T>
struct IsComplex : std::false_type
{
};
template<typename T>
struct IsComplex<std::complex<T>> : std::true_type
{
};

//=============================================================================================================
// NATIVE ELEMENT ACCESS
//=============================================================================================================

/** Properties of a format; integer formats also give their word width. */
template<AcqSampleFormat F>
struct Native;

template<>
struct Native<AcqSampleFormat::Int16>
{
    static constexpr bool integer = true;
    static constexpr bool complex = false;
    static constexpr int bits = 16;
};
template<>
struct Native<AcqSampleFormat::Int24Packed>
{
    static constexpr bool integer = true;
    static constexpr bool complex = false;
    static constexpr int bits = 24;
};
template<>
struct Native<AcqSampleFormat::Int32>
{
    static constexpr bool integer = true;
    static constexpr bool complex = false;
    static constexpr int bits = 32;
};
template<>
struct Native<AcqSampleFormat::Float32>
{
    static constexpr bool integer = false;
    static constexpr bool complex = false;
};
template<>
struct Native<AcqSampleFormat::Float64>
{
    static constexpr bool integer = false;
    static constexpr bool complex = false;
};
template<>
struct Native<AcqSampleFormat::ComplexInt16>
{
    static constexpr bool integer = true;
    static constexpr bool complex = true;
    static constexpr int bits = 16;
};
template<>
struct Native<AcqSampleFormat::ComplexFloat32>
{
    static constexpr bool integer = false;
    static constexpr bool complex = true;
};

/** Bytes of one integer component (one of I/Q for complex formats). */
template<AcqSampleFormat F>
constexpr std::size_t componentBytes() noexcept
{
    return F == AcqSampleFormat::Int24Packed ? 3 : static_cast<std::size_t>(Native<F>::bits) / 8;
}

/** Integer word of component @p k (unsigned, little-endian). */
template<AcqSampleFormat F>
std::uint32_t loadWord(const std::byte* element, int k) noexcept
{
    const std::byte* p = element + static_cast<std::size_t>(k) * componentBytes<F>();
    if constexpr (F == AcqSampleFormat::Int24Packed) {
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[0])) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[1])) << 8U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[2])) << 16U);
    } else if constexpr (Native<F>::bits == 32) {
        std::uint32_t w = 0;
        std::memcpy(&w, p, sizeof(w));
        return w;
    } else {
        std::uint16_t w = 0;
        std::memcpy(&w, p, sizeof(w));
        return w;
    }
}

template<AcqSampleFormat F>
void storeWord(std::byte* element, int k, std::uint32_t w) noexcept
{
    std::byte* p = element + static_cast<std::size_t>(k) * componentBytes<F>();
    if constexpr (F == AcqSampleFormat::Int24Packed) {
        p[0] = static_cast<std::byte>(w & 0xFFU);
        p[1] = static_cast<std::byte>((w >> 8U) & 0xFFU);
        p[2] = static_cast<std::byte>((w >> 16U) & 0xFFU);
    } else if constexpr (Native<F>::bits == 32) {
        std::memcpy(p, &w, sizeof(w));
    } else {
        const auto h = static_cast<std::uint16_t>(w);
        std::memcpy(p, &h, sizeof(h));
    }
}

/** Analog integer of component @p k with @p d digital bits on top: signExtend(low W-d bits) << d. */
template<AcqSampleFormat F>
std::int64_t loadInteger(const std::byte* element, int k, int d) noexcept
{
    constexpr int kBits = Native<F>::bits;
    const auto w = static_cast<std::uint64_t>(loadWord<F>(element, k));
    const int analog = kBits - d;
    const std::uint64_t low = w & ((std::uint64_t(1) << analog) - 1);
    const std::uint64_t sign = std::uint64_t(1) << (analog - 1);
    const auto value = static_cast<std::int64_t>(low ^ sign) - static_cast<std::int64_t>(sign);
    return value * (std::int64_t(1) << d);
}

template<AcqSampleFormat F>
double loadFloating(const std::byte* element, int k) noexcept
{
    if constexpr (F == AcqSampleFormat::Float64) {
        double v = 0;
        std::memcpy(&v, element, sizeof(v));
        return v;
    } else {
        float v = 0;
        std::memcpy(&v, element + static_cast<std::size_t>(k) * sizeof(float), sizeof(v));
        return v;
    }
}

/** Component @p k as Real, optionally scaled: Real(raw) * Real(scale) [+ Real(offset) for the real part]. */
template<AcqSampleFormat F, typename Real>
Real component(const std::byte* element, int k, int d, bool physical, Real scale, Real offset) noexcept
{
    Real v = 0;
    if constexpr (Native<F>::integer) {
        v = static_cast<Real>(loadInteger<F>(element, k, d));
    } else {
        v = static_cast<Real>(loadFloating<F>(element, k));
    }
    if (physical) {
        v = v * scale;
        if (k == 0) {
            v = v + offset;
        }
    }
    return v;
}

//=============================================================================================================
// DECODE
//=============================================================================================================

template<AcqSampleFormat F, typename T>
void decodeRun(const AcqBlockPart& part, std::size_t firstChannel, std::span<const double> scales,
               std::span<const double> offsets, std::span<const std::uint8_t> digital, const AcqMatrixRef<T>& out,
               std::size_t firstColumn, AcqScaling scaling) noexcept
{
    using Real = std::conditional_t<IsComplex<T>::value, float, T>;
    const bool physical = scaling == AcqScaling::Physical;
    for (std::size_t s0 = 0; s0 < part.samples; s0 += kBlock) {
        const std::size_t s1 = std::min(part.samples, s0 + kBlock);
        for (std::size_t r = 0; r < out.rows; ++r) {
            const std::size_t c = firstChannel + r;
            const Real scale = physical ? static_cast<Real>(scales[c]) : Real(1);
            const Real offset = physical ? static_cast<Real>(offsets[c]) : Real(0);
            const int d = digital.empty() ? 0 : digital[c];
            T* row = out.data + static_cast<std::ptrdiff_t>(r) * out.rowStride;
            for (std::size_t s = s0; s < s1; ++s) {
                const std::byte* element = part.at(c, s);
                T& dst = row[static_cast<std::ptrdiff_t>(firstColumn + s) * out.colStride];
                if constexpr (IsComplex<T>::value) {
                    const Real re = component<F, Real>(element, 0, d, physical, scale, offset);
                    Real im = 0;
                    if constexpr (Native<F>::complex) {
                        im = component<F, Real>(element, 1, 0, physical, scale, offset);
                    }
                    dst = T(re, im);
                } else {
                    dst = component<F, Real>(element, 0, d, physical, scale, offset);
                }
            }
        }
    }
}

template<typename T>
using DecodeFn = void (*)(const AcqBlockPart&, std::size_t, std::span<const double>, std::span<const double>,
                          std::span<const std::uint8_t>, const AcqMatrixRef<T>&, std::size_t, AcqScaling) noexcept;

template<typename T>
DecodeFn<T> decoderFor(AcqSampleFormat format) noexcept
{
    switch (format) {
        case AcqSampleFormat::Int16:
            return &decodeRun<AcqSampleFormat::Int16, T>;
        case AcqSampleFormat::Int24Packed:
            return &decodeRun<AcqSampleFormat::Int24Packed, T>;
        case AcqSampleFormat::Int32:
            return &decodeRun<AcqSampleFormat::Int32, T>;
        case AcqSampleFormat::Float32:
            return &decodeRun<AcqSampleFormat::Float32, T>;
        case AcqSampleFormat::Float64:
            return &decodeRun<AcqSampleFormat::Float64, T>;
        case AcqSampleFormat::ComplexInt16:
            if constexpr (IsComplex<T>::value) {
                return &decodeRun<AcqSampleFormat::ComplexInt16, T>;
            }
            return nullptr;
        case AcqSampleFormat::ComplexFloat32:
            if constexpr (IsComplex<T>::value) {
                return &decodeRun<AcqSampleFormat::ComplexFloat32, T>;
            }
            return nullptr;
    }
    return nullptr;
}

/** Channels of a range; @return false if it does not fit @p channels. */
bool resolveRange(const AcqChannelRange& range, std::size_t channels, std::size_t& first, std::size_t& count) noexcept
{
    if (range.first > channels) {
        return false;
    }
    first = range.first;
    count = range.count == std::numeric_limits<std::size_t>::max() ? channels - first : range.count;
    return count <= channels - first;
}

template<typename T>
AcqError decodeView(const AcqBlockView& view, const AcqMatrixRef<T>& out, const AcqConvertOptions& options) noexcept
{
    if (!view.memory().hostAccessible()) {
        return AcqErrorCode::DeviceMemory;
    }
    std::size_t first = 0;
    std::size_t count = 0;
    if (!resolveRange(options.channels, view.channelCount(), first, count) || out.data == nullptr ||
        out.rows != count || out.cols < options.firstColumn + view.sampleCount()) {
        return AcqErrorCode::ShapeMismatch;
    }
    const DecodeFn<T> fn = decoderFor<T>(view.format());
    if (fn == nullptr) {
        return AcqErrorCode::UnsupportedFormat;
    }
    const AcqMatrixRef<T> rows{out.data, count, out.cols, out.rowStride, out.colStride};
    std::size_t column = options.firstColumn;
    for (std::size_t p = 0; p < view.partCount(); ++p) {
        fn(view.part(p), first, view.scales(), view.offsets(), view.digitalBits(), rows, column, options.scaling);
        column += view.part(p).samples;
    }
    return {};
}

//=============================================================================================================
// ENCODE
//=============================================================================================================

/** Physical or raw value to the native value of one component; counts clipping in @p clipped. */
template<AcqSampleFormat F>
void encodeComponent(double value, std::byte* element, int k, int d, bool physical, double scale, double offset,
                     double lo, double hi, std::uint64_t& clipped) noexcept
{
    if (std::isnan(value)) {
        value = 0.0;
        physical = false;
        ++clipped;
    }
    if (physical) {
        if (value < lo || value > hi) {
            value = value < lo ? lo : hi;
            ++clipped;
        }
        value = (value - (k == 0 ? offset : 0.0)) / scale;
    }
    if constexpr (F == AcqSampleFormat::Float64) {
        if (std::isinf(value)) {
            value = std::copysign(std::numeric_limits<double>::max(), value);
            ++clipped;
        }
        std::memcpy(element, &value, sizeof(value));
    } else if constexpr (!Native<F>::integer) {
        constexpr double kMax = std::numeric_limits<float>::max();
        if (value > kMax || value < -kMax) {
            value = std::copysign(kMax, value);
            ++clipped;
        }
        const auto v = static_cast<float>(value);
        std::memcpy(element + static_cast<std::size_t>(k) * sizeof(float), &v, sizeof(v));
    } else {
        constexpr int kBits = Native<F>::bits;
        const int analog = kBits - d;
        const double top = std::ldexp(1.0, analog - 1);
        // Default rounding mode: nearest, ties to even.
        double q = std::nearbyint(std::ldexp(value, -d));
        if (q < -top || q > top - 1.0) {
            q = q < -top ? -top : top - 1.0;
            ++clipped;
        }
        const std::uint64_t lowMask = (std::uint64_t(1) << analog) - 1;
        const auto low = static_cast<std::uint64_t>(static_cast<std::int64_t>(q)) & lowMask;
        const auto old = static_cast<std::uint64_t>(loadWord<F>(element, k));
        storeWord<F>(element, k, static_cast<std::uint32_t>((old & ~lowMask) | low));
    }
}

template<AcqSampleFormat F, typename T>
void encodeRun(const AcqMatrixRef<const T>& in, std::size_t firstColumn, const AcqWritePart& part,
               std::size_t firstChannel, std::span<const double> scales, std::span<const double> offsets,
               std::span<const double> rangeMin, std::span<const double> rangeMax, std::span<const std::uint8_t> digital,
               AcqScaling scaling, std::uint64_t& clipped) noexcept
{
    const bool physical = scaling == AcqScaling::Physical;
    for (std::size_t s0 = 0; s0 < part.samples; s0 += kBlock) {
        const std::size_t s1 = std::min(part.samples, s0 + kBlock);
        for (std::size_t r = 0; r < in.rows; ++r) {
            const std::size_t c = firstChannel + r;
            const double scale = physical ? scales[c] : 1.0;
            const double offset = physical ? offsets[c] : 0.0;
            const double lo = std::isnan(rangeMin[c]) ? -std::numeric_limits<double>::infinity() : rangeMin[c];
            const double hi = std::isnan(rangeMax[c]) ? std::numeric_limits<double>::infinity() : rangeMax[c];
            const int d = digital.empty() ? 0 : digital[c];
            const T* row = in.data + static_cast<std::ptrdiff_t>(r) * in.rowStride;
            for (std::size_t s = s0; s < s1; ++s) {
                std::byte* element = part.at(c, s);
                const T& value = row[static_cast<std::ptrdiff_t>(firstColumn + s) * in.colStride];
                if constexpr (IsComplex<T>::value) {
                    encodeComponent<F>(value.real(), element, 0, d, physical, scale, offset, lo, hi, clipped);
                    if constexpr (Native<F>::complex) {
                        encodeComponent<F>(value.imag(), element, 1, 0, physical, scale, offset, lo, hi, clipped);
                    } else if (value.imag() != 0.0F) {
                        ++clipped;
                    }
                } else {
                    encodeComponent<F>(static_cast<double>(value), element, 0, d, physical, scale, offset, lo, hi,
                                       clipped);
                    if constexpr (Native<F>::complex) {
                        std::uint64_t ignored = 0;
                        encodeComponent<F>(0.0, element, 1, 0, physical, scale, offset, lo, hi, ignored);
                    }
                }
            }
        }
    }
}

template<typename T>
using EncodeFn = void (*)(const AcqMatrixRef<const T>&, std::size_t, const AcqWritePart&, std::size_t,
                          std::span<const double>, std::span<const double>, std::span<const double>,
                          std::span<const double>, std::span<const std::uint8_t>, AcqScaling, std::uint64_t&) noexcept;

template<typename T>
EncodeFn<T> encoderFor(AcqSampleFormat format) noexcept
{
    switch (format) {
        case AcqSampleFormat::Int16:
            return &encodeRun<AcqSampleFormat::Int16, T>;
        case AcqSampleFormat::Int24Packed:
            return &encodeRun<AcqSampleFormat::Int24Packed, T>;
        case AcqSampleFormat::Int32:
            return &encodeRun<AcqSampleFormat::Int32, T>;
        case AcqSampleFormat::Float32:
            return &encodeRun<AcqSampleFormat::Float32, T>;
        case AcqSampleFormat::Float64:
            return &encodeRun<AcqSampleFormat::Float64, T>;
        case AcqSampleFormat::ComplexInt16:
            return &encodeRun<AcqSampleFormat::ComplexInt16, T>;
        case AcqSampleFormat::ComplexFloat32:
            return &encodeRun<AcqSampleFormat::ComplexFloat32, T>;
    }
    return nullptr;
}

template<typename T>
AcqExpected<AcqEncodeResult> encodeView(const AcqMatrixRef<const T>& in, const AcqWriteView& view,
                                        const AcqConvertOptions& options) noexcept
{
    if (!view.memory().hostAccessible()) {
        return AcqErrorCode::DeviceMemory;
    }
    std::size_t first = 0;
    std::size_t count = 0;
    if (!resolveRange(options.channels, view.channelCount(), first, count) || in.data == nullptr ||
        in.rows != count || in.cols < options.firstColumn + view.sampleCount()) {
        return AcqErrorCode::ShapeMismatch;
    }
    const EncodeFn<T> fn = encoderFor<T>(view.format());
    if (fn == nullptr) {
        return AcqErrorCode::UnsupportedFormat;
    }
    const AcqMatrixRef<const T> rows{in.data, count, in.cols, in.rowStride, in.colStride};
    AcqEncodeResult result;
    std::size_t column = options.firstColumn;
    for (std::size_t p = 0; p < view.partCount(); ++p) {
        fn(rows, column, view.part(p), first, view.scales(), view.offsets(), view.rangeMin(), view.rangeMax(),
           view.digitalBits(), options.scaling, result.clipped);
        column += view.part(p).samples;
    }
    return result;
}

//=============================================================================================================
// BIT FIELDS
//=============================================================================================================

/** Word bits of a format's channel word; 0 for floating-point formats. */
int integerWordBits(AcqSampleFormat format) noexcept
{
    switch (format) {
        case AcqSampleFormat::Int16:
        case AcqSampleFormat::ComplexInt16:
            return 16;
        case AcqSampleFormat::Int24Packed:
            return 24;
        case AcqSampleFormat::Int32:
            return 32;
        default:
            return 0;
    }
}

/** Fields must lie inside their channel's digital bits, at the top of the word. */
template<typename View>
AcqError checkFields(const View& view, std::span<const AcqBitField> fields) noexcept
{
    const int bits = integerWordBits(view.format());
    if (bits == 0) {
        return AcqErrorCode::UnsupportedFormat;
    }
    for (const AcqBitField& field : fields) {
        if (field.channel >= view.channelCount() || field.width == 0) {
            return AcqErrorCode::InvalidParameter;
        }
        const int digital = view.digitalBits()[field.channel];
        if (field.firstBit + field.width > bits || field.firstBit < bits - digital) {
            return AcqErrorCode::InvalidParameter;
        }
    }
    return {};
}

std::uint32_t wordOf(const std::byte* element, AcqSampleFormat format) noexcept
{
    switch (format) {
        case AcqSampleFormat::Int24Packed:
            return loadWord<AcqSampleFormat::Int24Packed>(element, 0);
        case AcqSampleFormat::Int32:
            return loadWord<AcqSampleFormat::Int32>(element, 0);
        default:
            return loadWord<AcqSampleFormat::Int16>(element, 0);
    }
}

void setWord(std::byte* element, AcqSampleFormat format, std::uint32_t w) noexcept
{
    switch (format) {
        case AcqSampleFormat::Int24Packed:
            storeWord<AcqSampleFormat::Int24Packed>(element, 0, w);
            break;
        case AcqSampleFormat::Int32:
            storeWord<AcqSampleFormat::Int32>(element, 0, w);
            break;
        default:
            storeWord<AcqSampleFormat::Int16>(element, 0, w);
            break;
    }
}

std::uint32_t fieldMask(const AcqBitField& field) noexcept
{
    return field.width >= 32 ? 0xFFFFFFFFU : (std::uint32_t(1) << field.width) - 1;
}

} // namespace

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

AcqError ACQLIB::decode(const AcqBlockView& view, const AcqMatrixRef<float>& out,
                        const AcqConvertOptions& options) noexcept
{
    return decodeView(view, out, options);
}

AcqError ACQLIB::decode(const AcqBlockView& view, const AcqMatrixRef<double>& out,
                        const AcqConvertOptions& options) noexcept
{
    return decodeView(view, out, options);
}

AcqError ACQLIB::decode(const AcqBlockView& view, const AcqMatrixRef<std::complex<float>>& out,
                        const AcqConvertOptions& options) noexcept
{
    return decodeView(view, out, options);
}

//=============================================================================================================

AcqExpected<AcqEncodeResult> ACQLIB::encode(const AcqMatrixRef<const float>& in, const AcqWriteView& view,
                                            const AcqConvertOptions& options) noexcept
{
    return encodeView(in, view, options);
}

AcqExpected<AcqEncodeResult> ACQLIB::encode(const AcqMatrixRef<const double>& in, const AcqWriteView& view,
                                            const AcqConvertOptions& options) noexcept
{
    return encodeView(in, view, options);
}

AcqExpected<AcqEncodeResult> ACQLIB::encode(const AcqMatrixRef<const std::complex<float>>& in,
                                            const AcqWriteView& view, const AcqConvertOptions& options) noexcept
{
    return encodeView(in, view, options);
}

//=============================================================================================================

AcqError ACQLIB::decodeBits(const AcqBlockView& view, std::span<const AcqBitField> fields,
                            const AcqMatrixRef<std::uint32_t>& out, std::size_t firstColumn) noexcept
{
    if (!view.memory().hostAccessible()) {
        return AcqErrorCode::DeviceMemory;
    }
    if (AcqError error = checkFields(view, fields); !error.ok()) {
        return error;
    }
    if (out.data == nullptr || out.rows != fields.size() || out.cols < firstColumn + view.sampleCount()) {
        return AcqErrorCode::ShapeMismatch;
    }
    std::size_t column = firstColumn;
    for (std::size_t p = 0; p < view.partCount(); ++p) {
        const AcqBlockPart& part = view.part(p);
        for (std::size_t f = 0; f < fields.size(); ++f) {
            const AcqBitField& field = fields[f];
            std::uint32_t* row = out.data + static_cast<std::ptrdiff_t>(f) * out.rowStride;
            for (std::size_t s = 0; s < part.samples; ++s) {
                const std::uint32_t w = wordOf(part.at(field.channel, s), view.format());
                row[static_cast<std::ptrdiff_t>(column + s) * out.colStride] = (w >> field.firstBit) & fieldMask(field);
            }
        }
        column += part.samples;
    }
    return {};
}

//=============================================================================================================

AcqExpected<AcqEncodeResult> ACQLIB::encodeBits(const AcqMatrixRef<const std::uint32_t>& in,
                                                std::span<const AcqBitField> fields, const AcqWriteView& view,
                                                std::size_t firstColumn) noexcept
{
    if (!view.memory().hostAccessible()) {
        return AcqErrorCode::DeviceMemory;
    }
    if (AcqError error = checkFields(view, fields); !error.ok()) {
        return error;
    }
    if (in.data == nullptr || in.rows != fields.size() || in.cols < firstColumn + view.sampleCount()) {
        return AcqErrorCode::ShapeMismatch;
    }
    AcqEncodeResult result;
    std::size_t column = firstColumn;
    for (std::size_t p = 0; p < view.partCount(); ++p) {
        const AcqWritePart& part = view.part(p);
        for (std::size_t f = 0; f < fields.size(); ++f) {
            const AcqBitField& field = fields[f];
            const std::uint32_t mask = fieldMask(field);
            const std::uint32_t* row = in.data + static_cast<std::ptrdiff_t>(f) * in.rowStride;
            for (std::size_t s = 0; s < part.samples; ++s) {
                const std::uint32_t value = row[static_cast<std::ptrdiff_t>(column + s) * in.colStride];
                result.clipped += (value & ~mask) != 0 ? 1U : 0U;
                std::byte* element = part.at(field.channel, s);
                const std::uint32_t w = wordOf(element, view.format());
                setWord(element, view.format(), (w & ~(mask << field.firstBit)) | ((value & mask) << field.firstBit));
            }
        }
        column += part.samples;
    }
    return result;
}

//=============================================================================================================

AcqError ACQLIB::decodePart(const AcqBlockPart& part, AcqSampleFormat format, std::span<const double> scales,
                            std::span<const double> offsets, const AcqMatrixRef<float>& out,
                            AcqScaling scaling) noexcept
{
    if (out.data == nullptr || out.cols < part.samples ||
        (scaling == AcqScaling::Physical && (scales.size() != out.rows || offsets.size() != out.rows))) {
        return AcqErrorCode::ShapeMismatch;
    }
    const DecodeFn<float> fn = decoderFor<float>(format);
    if (fn == nullptr) {
        return AcqErrorCode::UnsupportedFormat;
    }
    fn(part, 0, scales, offsets, {}, out, 0, scaling);
    return {};
}
