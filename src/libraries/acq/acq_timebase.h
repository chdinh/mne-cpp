//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_timebase.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqTimebase: sample-exact relation between streams that share a hardware clock (clock domain).
 *
 * AcqClockModel relates a stream to host time with microsecond-class uncertainty; that is enough for
 * EEG markers but not for MR, where RF transmit, T/R switch, gradients and receive must line up to the
 * sample. Streams whose sample clocks derive from one oscillator (channels of one card, cards on a
 * star-hub, cards locked to a common 10 MHz reference) form a clock domain. Within a domain every
 * stream has an integer number of domain ticks per sample and an origin tick, so the mapping between
 * any two of its streams is exact integer arithmetic, without host time and without drift.
 *
 * The domain tick rate is chosen so that every stream of the domain has an integer tick count per
 * sample (least common multiple of the sample rates; e.g. 10 GHz for a 1.25 GS/s DAC and an 80 MS/s
 * ADC; 2^63 ticks at 10 GHz are 29 years). The origin tick is the domain tick at which sample 0 crosses
 * the connector, so known converter latencies (ADC pipeline, DAC output delay, trigger delay) are part
 * of it and the mapping refers to physical instants.
 *
 * Lock states: a shared reference makes frequencies equal (FrequencyLocked) but leaves the start offset
 * of separately triggered cards unknown; a common start trigger (star-hub) or a calibration with a sync
 * marker (alignTo) makes the mapping sample-exact (PhaseLocked). A reference loss is published as Free,
 * and mapSampleExact() then returns NotSynchronized instead of a wrong sample.
 *
 * Sample-exact playout of an RF pulse 2 ms after an event detected in a receive stream:
 * @code
 * const ACQLIB::AcqTimebase& rxTb = view.timebase();                // from the input view
 * const ACQLIB::AcqTimebase txTb = tx.timebase();                    // output stream of the same domain
 * const auto twoMs = static_cast<std::int64_t>(rxTb.tickRate * 2e-3);
 * auto at = ACQLIB::mapSampleExact(rxTb, txTb, eventSample, twoMs);
 * if (at && at->fraction == 0.0 && at->sample >= writer.earliestWritableSample()) {
 *     auto out = writer.beginWriteAt(at->sample, pulseSamples, std::chrono::microseconds(200));
 *     // encode the pulse and the T/R switch bit into *out, then writer.commit(pulseSamples)
 * }
 * @endcode
 */

#ifndef ACQ_TIMEBASE_H
#define ACQ_TIMEBASE_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_error.h"

#include <cstdint>
#include <string_view>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * How far a stream's timebase is known relative to its domain.
 */
enum class AcqLockState : std::uint8_t
{
    Free = 0,            /**< No common oscillator, or the reference was lost; no exact mapping. */
    FrequencyLocked = 1, /**< Same oscillator, origin offset unknown (separately started cards). */
    PhaseLocked = 2      /**< Same oscillator and known origin; mapping is sample-exact. */
};

//=============================================================================================================
/**
 * Position of a stream's samples on its domain's tick axis. Trivially copyable; published by the
 * backend or a calibration through AcqStream::setTimebase() and copied into every view.
 */
struct AcqTimebase
{
    std::uint64_t domain = 0;                 /**< acqClockDomainId(name); 0 = own free-running oscillator. */
    double tickRate = 0.0;                    /**< Domain ticks per second (informational). */
    std::uint64_t ticksPerSample = 1;         /**< Integer by construction of the domain tick rate. */
    std::int64_t originTick = 0;              /**< Domain tick at which sample 0 crosses the connector. */
    std::uint64_t originUncertaintyTicks = 0; /**< 0 for a common start trigger; calibration residual otherwise. */
    AcqLockState lock = AcqLockState::Free;   /**< See AcqLockState. */

    /** @return Domain tick of sample @p sample. */
    [[nodiscard]] constexpr std::int64_t tickOf(std::uint64_t sample) const noexcept
    {
        return originTick + static_cast<std::int64_t>(sample * ticksPerSample);
    }
};

//=============================================================================================================
/**
 * @return The stable domain id of @p name (FNV-1a 64, never 0), e.g. from the URI parameter
 *         clock_domain=scanner, so independently opened devices that the user wired to the same
 *         reference end up in the same domain.
 */
[[nodiscard]] constexpr std::uint64_t acqClockDomainId(std::string_view name) noexcept
{
    std::uint64_t h = 14695981039346656037ULL;
    for (const char c : name) {
        h = (h ^ static_cast<std::uint8_t>(c)) * 1099511628211ULL;
    }
    return h == 0 ? 1 : h;
}

//=============================================================================================================
/**
 * A sample position: integer index and the sub-sample fraction of the exact instant, for fractional-delay
 * filters or to decide whether an instant is representable.
 */
struct AcqSamplePosition
{
    std::uint64_t sample = 0; /**< Last sample at or before the instant. */
    double fraction = 0.0;    /**< (instant - sample) in samples, in [0, 1); exactly 0 when on a sample. */
};

//=============================================================================================================
/**
 * Maps @p sample of stream @p from, shifted by @p offsetTicks, to the stream @p to of the same domain.
 * Pure integer arithmetic, allocation-free; usable on the data path.
 *
 * @param[in] from          Timebase of the source stream.
 * @param[in] to            Timebase of the target stream.
 * @param[in] sample        Sample index in @p from.
 * @param[in] offsetTicks   Delay added to the instant of @p sample, in domain ticks (may be negative).
 *
 * @return The position in @p to; NotSynchronized when the domains differ or either side is not
 *         PhaseLocked; BeforeStart when the instant precedes sample 0 of @p to.
 */
[[nodiscard]] inline AcqExpected<AcqSamplePosition> mapSampleExact(const AcqTimebase& from, const AcqTimebase& to,
                                                                   std::uint64_t sample,
                                                                   std::int64_t offsetTicks = 0) noexcept
{
    if (from.domain == 0 || from.domain != to.domain || from.lock != AcqLockState::PhaseLocked ||
        to.lock != AcqLockState::PhaseLocked || to.ticksPerSample == 0) {
        return AcqErrorCode::NotSynchronized;
    }
    const std::int64_t delta = from.tickOf(sample) + offsetTicks - to.originTick;
    if (delta < 0) {
        return AcqErrorCode::BeforeStart;
    }
    const auto d = static_cast<std::uint64_t>(delta);
    return AcqSamplePosition{d / to.ticksPerSample,
                             static_cast<double>(d % to.ticksPerSample) / static_cast<double>(to.ticksPerSample)};
}

//=============================================================================================================
/**
 * Calibrates @p target against @p reference from one sync marker: the marker that @p target emitted
 * (output) or recorded (input) at @p targetSample is the same physical edge that @p reference recorded at
 * @p referenceSample (e.g. a loop-back of the trigger or clock-sync line into a receive channel).
 *
 * @param[in] target            Timebase to calibrate; must share the domain of @p reference.
 * @param[in] targetSample      Sample of the marker in the target stream.
 * @param[in] reference         PhaseLocked timebase that defines the domain origin.
 * @param[in] referenceSample   Sample at which the reference observed the marker.
 * @param[in] subTicks          Sub-sample correction from edge interpolation, in domain ticks.
 * @param[in] residualTicks     Uncertainty of the edge detection, stored in originUncertaintyTicks.
 *
 * @return The PhaseLocked target timebase, or NotSynchronized when the domains differ or the reference is
 *         not PhaseLocked.
 */
[[nodiscard]] inline AcqExpected<AcqTimebase> alignTo(AcqTimebase target, std::uint64_t targetSample,
                                                      const AcqTimebase& reference, std::uint64_t referenceSample,
                                                      std::int64_t subTicks = 0,
                                                      std::uint64_t residualTicks = 0) noexcept
{
    if (reference.domain == 0 || target.domain != reference.domain || reference.lock != AcqLockState::PhaseLocked) {
        return AcqErrorCode::NotSynchronized;
    }
    target.originTick =
        reference.tickOf(referenceSample) + subTicks - static_cast<std::int64_t>(targetSample * target.ticksPerSample);
    target.originUncertaintyTicks = residualTicks;
    target.lock = AcqLockState::PhaseLocked;
    return target;
}

} // namespace ACQLIB

#endif // ACQ_TIMEBASE_H
