//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_block_view.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqBlockView: zero-copy view of committed samples, events and segment headers of a stream.
 *
 * AcqReader::acquire() returns a view into the stream ring: one contiguous part, or two when the
 * block wraps around the end of the ring. The view owns nothing and does not allocate; it is valid
 * until it is passed to AcqReader::release(). The producer never waits for readers, so a view held
 * longer than the ring's headroom can be overwritten; release() detects this and returns
 * AcqErrorCode::Overrun, in which case the data read from the view must be discarded.
 */

#ifndef ACQ_BLOCK_VIEW_H
#define ACQ_BLOCK_VIEW_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_clock_model.h"
#include "acq_sample_format.h"

#include <Eigen/Core>

#include <array>
#include <cassert>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

class AcqReader;

//=============================================================================================================
/**
 * An event at a sample index (trigger, marker, annotation onset). Fixed size, stored in a ring next
 * to the samples. Text labels are resolved through AcqStreamInfo::eventLabels.
 */
struct AcqEvent
{
    std::uint64_t sample = 0;   /**< Sample index of the onset. */
    std::uint32_t duration = 0; /**< Duration in samples, 0 for an instantaneous event. */
    std::int32_t code = 0;      /**< Event code (trigger value, marker id). */
};

//=============================================================================================================
/**
 * Generic header of one segment of a segmented stream. The device-specific payload of
 * AcqSegmentation::headerBytes is delivered by AcqBlockView::segmentPayload().
 */
struct AcqSegmentHeader
{
    std::uint64_t firstSample = 0; /**< Sample index of the first sample of the segment. */
    std::uint32_t sampleCount = 0; /**< Samples per channel in the segment. */
    std::uint32_t flags = 0;       /**< Device-defined flags (e.g. last readout of a slice). */
    std::int64_t deviceTime = 0;   /**< Device time stamp of the trigger, device units. */
    std::uint64_t counter = 0;     /**< Device segment counter; gaps reveal dropped segments. */
};

//=============================================================================================================
/**
 * Up to two contiguous spans that together form one logical sequence (the second is empty unless the
 * sequence wraps around the end of a ring).
 *
 * @tparam T   Element type.
 */
template<typename T>
struct AcqSpanPair
{
    std::span<T> first;  /**< Elements up to the end of the ring. */
    std::span<T> second; /**< Elements from the start of the ring; empty without wrap. */

    /** @return Total number of elements. */
    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return first.size() + second.size();
    }

    /** @return true when there are no elements. */
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return size() == 0;
    }

    /** @return Element @p i of the logical sequence. */
    constexpr T& operator[](std::size_t i) const noexcept
    {
        return i < first.size() ? first[i] : second[i - first.size()];
    }
};

//=============================================================================================================
/**
 * One contiguous run of samples inside the ring. Element (channel c, sample s) of the run starts at
 * data + c * channelStride + s * sampleStride (strides in bytes; interleaved and planar rings differ
 * only in their strides).
 */
struct AcqBlockPart
{
    const std::byte* data = nullptr;  /**< First byte of channel 0, sample 0 of the run. */
    std::size_t samples = 0;          /**< Samples per channel in the run. */
    std::ptrdiff_t sampleStride = 0;  /**< Bytes between consecutive samples of one channel. */
    std::ptrdiff_t channelStride = 0; /**< Bytes between consecutive channels of one sample. */
};

//=============================================================================================================
/**
 * Zero-copy view of a block acquired from an AcqReader.
 */
class AcqBlockView
{
public:
    /** Strided channels x samples matrix view of one part, without copy. */
    template<typename T>
    using Map = Eigen::Map<const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>, Eigen::Unaligned,
                           Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>>;

    /** Creates an empty view. */
    AcqBlockView() noexcept = default;

    /** @return true when the view holds no samples, events or segments. */
    [[nodiscard]] bool empty() const noexcept
    {
        return m_sampleCount == 0 && m_events.empty() && m_segments.empty();
    }

    /** @return Sample index of the first sample in the view. */
    [[nodiscard]] std::uint64_t firstSample() const noexcept
    {
        return m_firstSample;
    }

    /** @return Samples per channel in the view (sum over the parts). */
    [[nodiscard]] std::size_t sampleCount() const noexcept
    {
        return m_sampleCount;
    }

    /** @return Samples lost to overrun between the previous view of this reader and this one. */
    [[nodiscard]] std::uint64_t lost() const noexcept
    {
        return m_lost;
    }

    /** @return Samples deliberately skipped by a Latest reader before this view (not a loss). */
    [[nodiscard]] std::uint64_t skipped() const noexcept
    {
        return m_skipped;
    }

    /** @return Number of channels. */
    [[nodiscard]] std::size_t channelCount() const noexcept
    {
        return m_channelCount;
    }

    /** @return Native sample format. */
    [[nodiscard]] AcqSampleFormat format() const noexcept
    {
        return m_format;
    }

    /** @return Ring layout. */
    [[nodiscard]] AcqLayout layout() const noexcept
    {
        return m_layout;
    }

    /** @return AcqStreamInfo::version that describes the samples of this view. */
    [[nodiscard]] std::uint32_t infoVersion() const noexcept
    {
        return m_infoVersion;
    }

    /** @return 1, or 2 when the view wraps around the end of the ring; 0 for a view without samples. */
    [[nodiscard]] std::size_t partCount() const noexcept
    {
        return m_partCount;
    }

    /** @return Part @p i. @pre i < partCount() */
    [[nodiscard]] const AcqBlockPart& part(std::size_t i) const noexcept
    {
        assert(i < m_partCount);
        return m_parts[i];
    }

    /**
     * Maps part @p i as a channels x samples Eigen matrix of the native element type, without copy.
     *
     * @tparam T   int16_t, int32_t, float, double or std::complex<float>; must match format().
     * @param[in] i   Part index. @pre i < partCount()
     *
     * @return Strided read-only map into the ring.
     */
    template<typename T>
    [[nodiscard]] Map<T> map(std::size_t i) const noexcept
    {
        static_assert(std::is_arithmetic_v<T> || std::is_same_v<T, std::complex<float>>,
                      "map<T>() needs an Eigen scalar type; use the converters for int24 and complex int16");
        assert(AcqSampleTraits<T>::format == m_format);
        const AcqBlockPart& p = part(i);
        return Map<T>(reinterpret_cast<const T*>(p.data), static_cast<Eigen::Index>(m_channelCount),
                      static_cast<Eigen::Index>(p.samples),
                      Eigen::Stride<Eigen::Dynamic, Eigen::Dynamic>(p.sampleStride / static_cast<std::ptrdiff_t>(sizeof(T)),
                                                                    p.channelStride / static_cast<std::ptrdiff_t>(sizeof(T))));
    }

    /** @return Events whose sample index lies in [firstSample(), firstSample() + sampleCount()). */
    [[nodiscard]] const AcqSpanPair<const AcqEvent>& events() const noexcept
    {
        return m_events;
    }

    /** @return Headers of the segments that start inside this view (segmented streams only). */
    [[nodiscard]] const AcqSpanPair<const AcqSegmentHeader>& segments() const noexcept
    {
        return m_segments;
    }

    /**
     * @param[in] i   Segment index. @pre i < segments().size()
     *
     * @return The device-specific header payload of segment @p i (AcqSegmentation::headerBytes bytes).
     */
    [[nodiscard]] std::span<const std::byte> segmentPayload(std::size_t i) const noexcept
    {
        const std::size_t offset = i * m_segmentPayloadBytes;
        return offset < m_segmentPayloads.first.size()
            ? m_segmentPayloads.first.subspan(offset, m_segmentPayloadBytes)
            : m_segmentPayloads.second.subspan(offset - m_segmentPayloads.first.size(), m_segmentPayloadBytes);
    }

    /** @return Clock model valid for this view. */
    [[nodiscard]] const AcqClockModel& clock() const noexcept
    {
        return m_clock;
    }

    /** @return Per-channel scale of infoVersion(); valid until release(). */
    [[nodiscard]] std::span<const double> scales() const noexcept
    {
        return m_scales;
    }

    /** @return Per-channel offset of infoVersion(); valid until release(). */
    [[nodiscard]] std::span<const double> offsets() const noexcept
    {
        return m_offsets;
    }

private:
    friend class AcqReader;

    std::array<AcqBlockPart, 2> m_parts{};
    std::size_t m_partCount = 0;
    std::uint64_t m_firstSample = 0;
    std::size_t m_sampleCount = 0;
    std::uint64_t m_lost = 0;
    std::uint64_t m_skipped = 0;
    std::size_t m_channelCount = 0;
    AcqSampleFormat m_format = AcqSampleFormat::Float32;
    AcqLayout m_layout = AcqLayout::Interleaved;
    std::uint32_t m_infoVersion = 0;
    AcqSpanPair<const AcqEvent> m_events;
    AcqSpanPair<const AcqSegmentHeader> m_segments;
    AcqSpanPair<const std::byte> m_segmentPayloads;
    std::size_t m_segmentPayloadBytes = 0;
    AcqClockModel m_clock;
    std::span<const double> m_scales;
    std::span<const double> m_offsets;
    std::uint64_t m_ticket = 0; /**< Reader-private validation token checked by release(). */
};

} // namespace ACQLIB

#endif // ACQ_BLOCK_VIEW_H
