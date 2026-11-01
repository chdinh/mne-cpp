//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_backend.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqBackend: the interface a device, file or network backend implements behind an AcqSource.
 *
 * Users hold an AcqSource; backend authors derive from AcqBackend. A backend creates its streams while
 * it is being opened (addStream), runs its own acquisition thread between start() and stop(), and
 * writes into the streams through their AcqStreamWriter. AcqSource owns the backend and enforces the
 * state machine, so a backend only implements the transitions.
 */

#ifndef ACQ_BACKEND_H
#define ACQ_BACKEND_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_error.h"
#include "acq_stream.h"
#include "acq_stream_info.h"

#include <memory>
#include <span>
#include <string>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * What a source is, for display and provenance.
 */
struct AcqSourceDescription
{
    std::string backend;        /**< Backend name, e.g. "synthetic", "brainflow". */
    std::string backendVersion; /**< Version of the runtime-loaded library, empty if none. */
    std::string device;         /**< Device model and serial, file path or stream name. */
    std::string uri;            /**< Canonical URI that reopens this source. */
};

//=============================================================================================================
/**
 * Base class of all backends.
 */
class ACQSHARED_EXPORT AcqBackend
{
public:
    virtual ~AcqBackend();
    AcqBackend(const AcqBackend&) = delete;
    AcqBackend& operator=(const AcqBackend&) = delete;

    /** @return Description of the opened device. */
    [[nodiscard]] virtual AcqSourceDescription description() const = 0;

    /** Starts acquisition (typically starts the acquisition thread). Called in state Opened or Stopped. */
    virtual AcqError start() = 0;

    /** Stops acquisition and joins the acquisition thread before returning. Called in state Running. */
    virtual void stop() = 0;

    /** Releases the device. Called once, after stop(). The default does nothing. */
    virtual void close();

    /** @return The streams created by addStream(), in creation order. */
    [[nodiscard]] std::span<const AcqStream> streams() const noexcept;

protected:
    AcqBackend();

    //=========================================================================================================
    /**
     * Creates a stream. Only while opening, before the first start().
     *
     * @param[in] info      Stream description.
     * @param[in] options   Ring options, e.g. an adopted DMA buffer.
     *
     * @return The producer handle, owned by the backend base; valid until the backend is destroyed.
     */
    AcqExpected<AcqStreamWriter*> addStream(AcqStreamInfo info, AcqStreamOptions options = {});

    /** Reports a fatal acquisition error from any thread; the source moves to state Failed. */
    void reportError(AcqError error) noexcept;

private:
    friend class AcqSource;
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
};

} // namespace ACQLIB

#endif // ACQ_BACKEND_H
