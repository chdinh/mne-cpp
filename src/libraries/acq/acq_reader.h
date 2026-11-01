//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_reader.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqReader: one consumer of a stream with its own position, mode and exact loss count.
 *
 * Any number of readers (up to AcqStreamOptions::maxReaders) consume the same ring independently:
 * a recorder, a display and a closed-loop controller each read at their own pace. The producer never
 * waits for them (D2, data-path rule 3). After start(), acquire() and release() perform no heap
 * allocation, take no lock and emit no Qt signal.
 *
 * Zero-copy loop:
 * @code
 * auto reader = stream.reader(ACQLIB::AcqReader::ReadMode::Lossless);
 * while (running) {
 *     auto view = reader->acquire(256, std::chrono::milliseconds(100));
 *     if (!view) { if (view.error() == ACQLIB::AcqErrorCode::Timeout) continue; break; }
 *     for (std::size_t p = 0; p < view->partCount(); ++p)
 *         process(view->map<float>(p));                   // channels x samples, in the ring
 *     if (!reader->release(*view).ok()) discardLastResult();  // producer overtook the view
 * }
 * @endcode
 */

#ifndef ACQ_READER_H
#define ACQ_READER_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_block_view.h"
#include "acq_convert.h"
#include "acq_error.h"

#include <Eigen/Core>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

namespace detail
{
class AcqStreamCore;
}

class AcqStream;

//=============================================================================================================
/**
 * How a waiting reader is woken.
 */
enum class AcqWait : std::uint8_t
{
    Block = 0,        /**< std::atomic::wait; no CPU while idle. Default for Lossless and Latest. */
    Spin = 1,         /**< Busy-poll; lowest latency, one core. Default for LowLatency. */
    SpinThenBlock = 2 /**< Busy-poll for AcqReaderOptions::spinFor, then block. */
};

//=============================================================================================================
/**
 * Per-reader options.
 */
struct AcqReaderOptions
{
    AcqWait wait = AcqWait::Block;                                    /**< Wake-up strategy; LowLatency forces Spin unless set. */
    std::chrono::nanoseconds spinFor = std::chrono::microseconds(50); /**< Spin window of SpinThenBlock. */
    bool startAtOldest = false;                                       /**< Start at the oldest retained sample instead of the newest. */
};

//=============================================================================================================
/**
 * Counters of one reader. Exact, maintained without locks.
 */
struct AcqReaderStats
{
    std::uint64_t position = 0;     /**< Next sample index this reader will deliver. */
    std::uint64_t delivered = 0;    /**< Samples delivered in views. */
    std::uint64_t lost = 0;         /**< Samples overwritten before this reader could acquire them. */
    std::uint64_t skipped = 0;      /**< Samples skipped on purpose by a Latest reader. */
    std::uint64_t overruns = 0;     /**< Views invalidated while held (release() returned Overrun). */
    std::uint64_t eventsLost = 0;   /**< Events overwritten before this reader could acquire them. */
    std::uint64_t segmentsLost = 0; /**< Segment headers overwritten likewise. */
    std::uint64_t views = 0;        /**< Views acquired. */
};

//=============================================================================================================
/**
 * Result of the copying read() convenience calls.
 */
struct AcqReadResult
{
    std::uint64_t firstSample = 0; /**< Sample index of column 0. */
    std::size_t samples = 0;       /**< Columns written. */
    std::uint64_t lost = 0;        /**< Samples lost before column 0. */
    std::uint32_t infoVersion = 0; /**< AcqStreamInfo::version of the samples. */
};

//=============================================================================================================
/**
 * A consumer of one AcqStream. Move-only; destroying it frees its reader slot.
 */
class ACQSHARED_EXPORT AcqReader
{
public:
    /**
     * Consumption mode of a reader.
     */
    enum class ReadMode : std::uint8_t
    {
        Lossless = 0,  /**< Every sample in order; an overrun is reported with its exact count and the reader resumes at the oldest retained sample. */
        Latest = 1,    /**< Always the newest samples; older ones are skipped (counted, not lost). For displays. */
        LowLatency = 2 /**< Like Lossless, woken by busy-poll for the shortest commit-to-wake latency. */
    };

    AcqReader() noexcept;
    ~AcqReader();
    AcqReader(AcqReader&& other) noexcept;
    AcqReader& operator=(AcqReader&& other) noexcept;
    AcqReader(const AcqReader&) = delete;
    AcqReader& operator=(const AcqReader&) = delete;

    /** @return true when attached to a stream. */
    [[nodiscard]] bool isValid() const noexcept;

    /** @return The consumption mode. */
    [[nodiscard]] ReadMode mode() const noexcept;

    /** @return The stream this reader consumes. */
    [[nodiscard]] AcqStream stream() const;

    //=========================================================================================================
    /**
     * Waits until at least @p minSamples are available (Latest: the newest @p minSamples), then returns a
     * view of up to @p maxSamples. Only one view per reader may be held at a time.
     *
     * @param[in] minSamples   Samples per channel required before returning; 0 returns immediately.
     * @param[in] timeout      Maximum wait.
     * @param[in] maxSamples   Upper bound of the view, 0 for everything available.
     *
     * @return The view, or Timeout, EndOfStream, SourceStopped, InvalidState (a view is already held).
     */
    [[nodiscard]] AcqExpected<AcqBlockView> acquire(std::size_t minSamples, std::chrono::nanoseconds timeout,
                                                    std::size_t maxSamples = 0) noexcept;

    //=========================================================================================================
    /**
     * Returns @p view to the ring and advances the reader. Clears @p view.
     *
     * @param[in, out] view   A view returned by this reader's acquire().
     *
     * @return Success, or Overrun when the producer overwrote part of the view while it was held; its
     *         samples count as lost and anything computed from them must be discarded.
     */
    AcqError release(AcqBlockView& view) noexcept;

    //=========================================================================================================
    /**
     * Copying convenience: waits for @p samples samples, converts them into @p out (resized to
     * channels x samples only if its shape differs) and releases the view.
     *
     * @param[in] samples   Samples per channel.
     * @param[out] out      Caller-owned destination.
     * @param[in] timeout   Maximum wait.
     * @param[in] scaling   Raw or physical values.
     *
     * @return Position and loss of the block, or the error of acquire()/release().
     */
    [[nodiscard]] AcqExpected<AcqReadResult> read(std::size_t samples, Eigen::MatrixXf& out,
                                                  std::chrono::nanoseconds timeout,
                                                  AcqScaling scaling = AcqScaling::Physical);

    /** @copydoc read(std::size_t, Eigen::MatrixXf&, std::chrono::nanoseconds, AcqScaling) */
    [[nodiscard]] AcqExpected<AcqReadResult> read(std::size_t samples, Eigen::MatrixXd& out,
                                                  std::chrono::nanoseconds timeout,
                                                  AcqScaling scaling = AcqScaling::Physical);

    /** @copydoc read(std::size_t, Eigen::MatrixXf&, std::chrono::nanoseconds, AcqScaling) */
    [[nodiscard]] AcqExpected<AcqReadResult> read(std::size_t samples, Eigen::MatrixXcf& out,
                                                  std::chrono::nanoseconds timeout,
                                                  AcqScaling scaling = AcqScaling::Physical);

    /** @return Samples committed and not yet acquired by this reader (capped at the ring capacity). */
    [[nodiscard]] std::size_t available() const noexcept;

    /** @return This reader's counters. */
    [[nodiscard]] AcqReaderStats stats() const noexcept;

private:
    friend class AcqStream;
    AcqReader(std::shared_ptr<detail::AcqStreamCore> core, std::uint32_t slot) noexcept;

    std::shared_ptr<detail::AcqStreamCore> m_core;
    std::uint32_t m_slot = 0;
};

} // namespace ACQLIB

#endif // ACQ_READER_H
