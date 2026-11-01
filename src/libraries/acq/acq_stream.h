//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_stream.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqStream (consumer handle) and AcqStreamWriter (producer handle) of one lock-free stream ring.
 *
 * A stream has one sample rate and one native format; a source with EEG at 1 kHz and an accelerometer
 * at 50 Hz exposes two streams (data-path rule 6). The ring is single-producer / multi-reader,
 * page-aligned, a power of two in samples and memory-locked where the OS allows it. It either owns its
 * memory or adopts an external buffer such as a device DMA region (data-path rule 4).
 *
 * The producer side never waits: AcqStreamWriter::commit() is one release store (plus a notify only
 * when a reader is blocked), and old samples are overwritten whether or not a reader has consumed
 * them; slow readers learn the exact number of samples they lost.
 *
 * Producing without a backend, e.g. for the in-memory DMA emulation or a test:
 * @code
 * auto writer = ACQLIB::AcqStreamWriter::create(info);   // info: an AcqStreamInfo
 * auto reader = writer->stream().reader(ACQLIB::AcqReader::ReadMode::Lossless);
 * ACQLIB::AcqWritableBlock block = writer->beginWrite(256);
 * fill(block);                                           // write native samples, at most two parts
 * writer->commit(256);                                   // publish
 * @endcode
 */

#ifndef ACQ_STREAM_H
#define ACQ_STREAM_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_block_view.h"
#include "acq_clock_model.h"
#include "acq_error.h"
#include "acq_reader.h"
#include "acq_stream_info.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

namespace detail
{
class AcqStreamCore;
}

//=============================================================================================================
/**
 * An externally owned sample buffer adopted as the ring (zero-copy DMA). Its size in samples must be a
 * power of two and its start page-aligned; the ring calls @c release when the last handle is gone.
 */
struct AcqExternalBuffer
{
    std::byte* data = nullptr;     /**< Start of the buffer. */
    std::size_t bytes = 0;         /**< Size in bytes = capacity samples * frame bytes (interleaved) or per layout. */
    std::function<void()> release; /**< Called once after the ring no longer uses the buffer; may be empty. */
};

//=============================================================================================================
/**
 * Ring and real-time options of a stream. Fixed at creation (control path).
 */
struct AcqStreamOptions
{
    std::size_t capacitySamples = 0;  /**< Rounded up to a power of two; 0 = max(65536, 2 s of data). */
    std::size_t maxReaders = 8;       /**< Reader slots, allocated at creation. */
    std::size_t eventCapacity = 4096; /**< Event ring entries (power of two). */
    std::size_t segmentCapacity = 0;  /**< Segment header ring entries; 0 = derived from capacity for segmented streams. */
    bool lockMemory = true;           /**< mlock/VirtualLock the ring; best effort, reported in stats. */
    bool hugePages = false;           /**< Back the ring with huge pages where available. */
    AcqExternalBuffer external;       /**< Adopt this buffer instead of allocating one. */
};

//=============================================================================================================
/**
 * Producer-side counters of a stream.
 */
struct AcqStreamStats
{
    std::uint64_t committed = 0;     /**< Samples committed since creation (= next write position). */
    std::uint64_t commits = 0;       /**< commit() calls. */
    std::uint64_t events = 0;        /**< Events pushed since creation. */
    std::uint64_t segments = 0;      /**< Segment headers pushed since creation. */
    std::size_t capacitySamples = 0; /**< Effective ring capacity. */
    std::size_t activeReaders = 0;   /**< Readers attached right now. */
    bool memoryLocked = false;       /**< Whether lockMemory succeeded. */
    bool adoptedBuffer = false;      /**< Whether the ring is an adopted external buffer. */
};

//=============================================================================================================
/**
 * Consumer handle of a stream. Cheap to copy (shared ownership of the ring), so readers stay valid
 * after the source that produced the stream is closed; they then return EndOfStream.
 */
class ACQSHARED_EXPORT AcqStream
{
public:
    AcqStream() noexcept;

    /** @return true when attached to a ring. */
    [[nodiscard]] bool isValid() const noexcept;

    /** @return The current metadata version (control path; copies under a lock-free snapshot). */
    [[nodiscard]] std::shared_ptr<const AcqStreamInfo> info() const;

    /** @return The metadata version valid at @p sample, or the oldest retained one. */
    [[nodiscard]] std::shared_ptr<const AcqStreamInfo> infoAt(std::uint64_t sample) const;

    /** @return The current clock model. */
    [[nodiscard]] AcqClockModel clock() const noexcept;

    /** @return Producer-side counters. */
    [[nodiscard]] AcqStreamStats stats() const noexcept;

    //=========================================================================================================
    /**
     * Attaches a new reader (control path: may be called before or after start()).
     *
     * @param[in] mode      Consumption mode.
     * @param[in] options   Wake-up strategy and start position.
     *
     * @return The reader, or TooManyReaders.
     */
    [[nodiscard]] AcqExpected<AcqReader> reader(AcqReader::ReadMode mode,
                                                const AcqReaderOptions& options = {}) const;

private:
    friend class AcqReader;
    friend class AcqStreamWriter;
    explicit AcqStream(std::shared_ptr<detail::AcqStreamCore> core) noexcept;

    std::shared_ptr<detail::AcqStreamCore> m_core;
};

//=============================================================================================================
/**
 * Writable region returned by AcqStreamWriter::beginWrite(): one or two parts (wrap-around), with the
 * same strides as AcqBlockPart.
 */
struct AcqWritableBlock
{
    /** One contiguous writable run. */
    struct Part
    {
        std::byte* data = nullptr;        /**< Channel 0, sample 0 of the run. */
        std::size_t samples = 0;          /**< Samples per channel. */
        std::ptrdiff_t sampleStride = 0;  /**< Bytes between samples of one channel. */
        std::ptrdiff_t channelStride = 0; /**< Bytes between channels of one sample. */
    };

    std::array<Part, 2> parts{};   /**< parts[1].samples == 0 without wrap. */
    std::uint64_t firstSample = 0; /**< Sample index of parts[0], sample 0. */
};

//=============================================================================================================
/**
 * Producer handle of a stream. Move-only; exactly one per ring. All members except create() and
 * updateInfo() are wait-free and allocation-free.
 */
class ACQSHARED_EXPORT AcqStreamWriter
{
public:
    //=========================================================================================================
    /**
     * Creates a ring for @p info.
     *
     * @param[in] info      Stream description (version 0).
     * @param[in] options   Ring and real-time options.
     *
     * @return The writer, or InvalidParameter, InvalidBuffer, OutOfMemory.
     */
    [[nodiscard]] static AcqExpected<AcqStreamWriter> create(AcqStreamInfo info, AcqStreamOptions options = {});

    AcqStreamWriter() noexcept;
    ~AcqStreamWriter(); /**< Equals finish(). */
    AcqStreamWriter(AcqStreamWriter&& other) noexcept;
    AcqStreamWriter& operator=(AcqStreamWriter&& other) noexcept;
    AcqStreamWriter(const AcqStreamWriter&) = delete;
    AcqStreamWriter& operator=(const AcqStreamWriter&) = delete;

    /** @return The consumer handle of this ring. */
    [[nodiscard]] AcqStream stream() const noexcept;

    /** @return Next sample index to be written. */
    [[nodiscard]] std::uint64_t writePosition() const noexcept;

    //=========================================================================================================
    /**
     * Returns the region for the next @p samples samples. Never waits; the region may still be visible
     * to readers that lag by more than the capacity, which they detect as loss.
     *
     * @param[in] samples   Samples per channel. @pre samples <= capacity
     *
     * @return The writable region (already in place for an adopted buffer).
     */
    [[nodiscard]] AcqWritableBlock beginWrite(std::size_t samples) noexcept;

    /** Publishes @p samples samples written since the last commit (one release store). */
    void commit(std::size_t samples) noexcept;

    /**
     * Attaches @p event (absolute sample index, not decreasing). Published with the next commit(). Like
     * samples, old events are overwritten; a lagging reader counts them in AcqReaderStats::eventsLost.
     */
    void pushEvent(const AcqEvent& event) noexcept;

    /**
     * Starts a segment at header.firstSample with its device-specific @p payload (truncated or zero-padded
     * to AcqSegmentation::headerBytes). Published with the next commit(); overwritten like events.
     */
    void pushSegment(const AcqSegmentHeader& header, std::span<const std::byte> payload) noexcept;

    /** Publishes a new clock model (sequence counter, no lock). */
    void setClock(const AcqClockModel& clock) noexcept;

    /**
     * Publishes metadata version info.version = current + 1 valid from @p fromSample. Allocates; meant
     * for rare changes (channel renamed, gain switched), not per block.
     */
    AcqError updateInfo(AcqStreamInfo info, std::uint64_t fromSample);

    /** Marks the end of the stream; readers drain and then receive EndOfStream. Idempotent. */
    void finish() noexcept;

private:
    explicit AcqStreamWriter(std::shared_ptr<detail::AcqStreamCore> core) noexcept;

    std::shared_ptr<detail::AcqStreamCore> m_core;
};

} // namespace ACQLIB

#endif // ACQ_STREAM_H
