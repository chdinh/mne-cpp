//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_source.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqSource: an opened device, file or network endpoint with its input and output streams (control path).
 *
 * Lifecycle: open() -> Opened -> start() -> Running -> stop() -> Stopped -> (start() again) ...
 * -> close() -> Closed. stop() joins the backend threads (output streams emit their underrun fill
 * from then on), close() releases the device, the destructor equals close(). A backend error moves the source to Failed; lastError() names it.
 * Streams and readers obtained from a source stay valid after close() and report EndOfStream.
 */

#ifndef ACQ_SOURCE_H
#define ACQ_SOURCE_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_backend.h"
#include "acq_error.h"
#include "acq_stream.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * State of a source.
 */
enum class AcqSourceState : std::uint8_t
{
    Opened = 0,  /**< Device connected and described; streams exist; no data flows. */
    Running = 1, /**< Acquiring. */
    Stopped = 2, /**< Acquisition stopped; can be restarted. */
    Failed = 3,  /**< The backend reported a fatal error; only close() is allowed. */
    Closed = 4   /**< Device released. */
};

//=============================================================================================================
/**
 * Snapshot of a source and its streams (control path, allocates).
 */
struct AcqSourceStats
{
    AcqSourceState state = AcqSourceState::Closed; /**< State at the time of the snapshot. */
    std::vector<AcqStreamStats> streams;           /**< One entry per stream. */
};

//=============================================================================================================
/**
 * An opened source. Move-only handle that owns its backend.
 */
class ACQSHARED_EXPORT AcqSource
{
public:
    //=========================================================================================================
    /**
     * Opens a source through AcqSourceRegistry, e.g. "synthetic://", "fiff:///data/raw.fif?realtime=1".
     *
     * @param[in] uri   Source URI.
     *
     * @return The opened source, or InvalidUri, UnknownScheme, InvalidParameter, BackendUnavailable, ...
     */
    [[nodiscard]] static AcqExpected<AcqSource> open(std::string_view uri);

    /** Wraps an already opened backend. @pre backend != nullptr */
    explicit AcqSource(std::unique_ptr<AcqBackend> backend);

    AcqSource() noexcept;
    ~AcqSource(); /**< Equals close(). */
    AcqSource(AcqSource&& other) noexcept;
    AcqSource& operator=(AcqSource&& other) noexcept;
    AcqSource(const AcqSource&) = delete;
    AcqSource& operator=(const AcqSource&) = delete;

    /** Starts acquisition and output. @return InvalidState unless Opened or Stopped, or the backend's error. */
    AcqError start();

    /** Stops acquisition and output and joins the backend threads. No-op unless Running. */
    AcqError stop();

    /** Stops if needed and releases the device. Idempotent. */
    void close();

    /** @return The current state. */
    [[nodiscard]] AcqSourceState state() const noexcept;

    /** @return All streams of the source. */
    [[nodiscard]] std::span<const AcqStream> streams() const noexcept;

    /** @return Stream @p index, an invalid stream if out of range. Index 0 is the primary stream. */
    [[nodiscard]] AcqStream stream(std::size_t index = 0) const;

    /** @return The stream named @p name, an invalid stream if there is none. */
    [[nodiscard]] AcqStream findStream(std::string_view name) const;

    /** @return Description of the source. */
    [[nodiscard]] AcqSourceDescription description() const;

    /** @return State and per-stream counters. */
    [[nodiscard]] AcqSourceStats stats() const;

    /** @return The last error reported by the backend or a failed transition; success if none. */
    [[nodiscard]] AcqError lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
};

} // namespace ACQLIB

#endif // ACQ_SOURCE_H
