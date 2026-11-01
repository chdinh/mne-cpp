//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_clock_model.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqClockModel: relation between the sample index of a stream and host time.
 *
 * The sample index is the time base of every stream (data-path rule 5). Host time is derived from it
 * by a linear model anchored at one sample, with an estimated rate correction and a stated
 * uncertainty, so that clock drift of a device never silently shifts events. The producer publishes
 * the model; every AcqBlockView carries the snapshot that was valid when it was acquired.
 */

#ifndef ACQ_CLOCK_MODEL_H
#define ACQ_CLOCK_MODEL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"

#include <cmath>
#include <cstdint>
#include <limits>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * What the anchor of the clock model is derived from.
 */
enum class AcqClockSource : std::uint8_t
{
    Nominal = 0, /**< Only the nominal rate is known; host times are extrapolated from start(). */
    Host = 1,    /**< Host receive times of blocks, smoothed. */
    Device = 2,  /**< Timestamps sent by the device (BrainFlow timestamp row, vendor counters). */
    Lsl = 3,     /**< LSL timestamps with time_correction applied. */
    External = 4 /**< External reference (10 MHz clock, PTP, sequence trigger). */
};

//=============================================================================================================
/**
 * Linear sample-index <-> host-time model. Trivially copyable; published by the producer without a
 * lock (sequence counter) and copied into every block view.
 *
 * hostNs(s) = anchorHostNs + (s - anchorSample) * 1e9 / (sfreq * rateCorrection)
 *
 * Host time is nanoseconds of std::chrono::steady_clock; @c utcOffsetNs converts it to UTC.
 */
struct AcqClockModel
{
    double sfreq = 0.0;                                             /**< Nominal sampling frequency in Hz. */
    double rateCorrection = 1.0;                                    /**< Estimated actual / nominal rate. */
    double uncertaintyNs = std::numeric_limits<double>::infinity(); /**< One-sigma uncertainty of hostNs(). */
    std::uint64_t anchorSample = 0;                                 /**< Sample index of the anchor. */
    std::int64_t anchorHostNs = 0;                                  /**< steady_clock time of the anchor sample. */
    std::int64_t utcOffsetNs = 0;                                   /**< UTC minus steady_clock at the anchor. */
    AcqClockSource source = AcqClockSource::Nominal;                /**< Origin of the anchor. */

    /** @return Host steady_clock time of sample @p sample in nanoseconds. @pre sfreq > 0 */
    [[nodiscard]] std::int64_t hostNs(std::uint64_t sample) const noexcept
    {
        const double ds = static_cast<double>(sample) - static_cast<double>(anchorSample);
        return anchorHostNs + static_cast<std::int64_t>(std::llround(ds * 1e9 / (sfreq * rateCorrection)));
    }

    /** @return The sample index closest to host time @p hostTimeNs, clamped at 0. @pre sfreq > 0 */
    [[nodiscard]] std::uint64_t sampleAt(std::int64_t hostTimeNs) const noexcept
    {
        const double ds = static_cast<double>(hostTimeNs - anchorHostNs) * sfreq * rateCorrection / 1e9;
        const double s = static_cast<double>(anchorSample) + ds;
        return s <= 0.0 ? 0U : static_cast<std::uint64_t>(std::llround(s));
    }
};

} // namespace ACQLIB

#endif // ACQ_CLOCK_MODEL_H
