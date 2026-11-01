//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_stream_info.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    AcqStreamInfo: format, layout, rate and per-channel description of one acquisition stream.
 *
 * The description is complete enough to convert to and from FIFFLIB::FiffInfo without loss
 * (@ref acq_fiff.h) and generic enough to describe streams that FIFF cannot, such as segmented
 * complex-valued MR receiver data (D12). Nothing is guessed (G12): an unknown unit is
 * AcqUnit::Unknown, an unknown range is NaN, an irregular stream has sfreq 0.
 *
 * Metadata changes are versioned at a sample index: a producer publishes a new AcqStreamInfo with
 * @c version + 1 and @c validFromSample, and every AcqBlockView names the version it belongs to.
 *
 * Describing an MR-style segmented complex-int16 stream (32 receive coils, 512 samples per readout,
 * ISMRMRD acquisition header per readout):
 * @code
 * ACQLIB::AcqStreamInfo info;
 * info.name = "rx";
 * info.format = ACQLIB::AcqSampleFormat::ComplexInt16;
 * info.layout = ACQLIB::AcqLayout::Planar;               // one DMA buffer per coil
 * info.sfreq = 1.0e6;                                     // 1 us dwell time inside a readout
 * for (int coil = 0; coil < 32; ++coil) {
 *     ACQLIB::AcqChannelInfo ch;
 *     ch.name = "coil" + std::to_string(coil);
 *     ch.kind = ACQLIB::AcqChannelKind::MrReceive;
 *     ch.unit = ACQLIB::AcqUnit::Volt;
 *     ch.scale = 1.0 / 32768.0;                           // ADC full scale -> 1 V
 *     info.channels.push_back(ch);
 * }
 * info.segmentation.mode = ACQLIB::AcqSegmentation::Mode::Triggered;
 * info.segmentation.samplesPerSegment = 512;              // 0 would mean variable length
 * info.segmentation.headerBytes = 340;                    // sizeof(ISMRMRD_AcquisitionHeader)
 * info.segmentation.headerSchema = "ismrmrd.AcquisitionHeader/1";
 * @endcode
 */

#ifndef ACQ_STREAM_INFO_H
#define ACQ_STREAM_INFO_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_sample_format.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * Signal kind of a channel. Mapped to and from FIFF channel kinds by @ref acq_fiff.h.
 */
enum class AcqChannelKind : std::uint8_t
{
    Unknown = 0,
    Eeg,
    Meg, /**< Magnetometer or gradiometer; FIFF coil type distinguishes them. */
    Opm, /**< Optically pumped magnetometer. */
    Ecg,
    Eog,
    Emg,
    Seeg,
    Ecog,
    Dbs,
    Fnirs,
    Respiration,
    Gsr,
    Stim, /**< Trigger / digital input channel carrying event codes. */
    Accelerometer,
    Gyroscope,
    Temperature,
    Timestamp, /**< Device timestamp row (e.g. BrainFlow timestamp channel). */
    Marker,    /**< Device marker row; also delivered as AcqEvent. */
    MrReceive, /**< MR receive-coil channel (complex, segmented). */
    Misc
};

//=============================================================================================================
/**
 * SI unit of the physical value of a channel (value = raw * scale + offset, times 10^unitExponent).
 */
enum class AcqUnit : std::uint8_t
{
    Unknown = 0, /**< Not stated by the source; never guessed. */
    None,        /**< Dimensionless (counts, trigger codes). */
    Volt,
    Tesla,
    TeslaPerMeter,
    AmpereMeter,
    MeterPerSecondSquared,
    RadianPerSecond,
    DegreeCelsius,
    Second,
    Ohm,
    Molar /**< fNIRS concentrations. */
};

//=============================================================================================================
/**
 * FIFF-specific channel fields, kept so that FiffChInfo round-trips without loss. Plain integers and
 * floats with FIFF semantics (FIFFV_* constants), so no FIFF header is needed here.
 */
struct AcqFiffChannel
{
    int kind = 0;                /**< FIFFV_*_CH. */
    int coilType = 0;            /**< FIFFV_COIL_*. */
    int logicalNo = 0;           /**< logno. */
    int scannerNo = 0;           /**< scanno. */
    int unit = 0;                /**< FIFF_UNIT_*. */
    int unitMul = 0;             /**< FIFF_UNITM_*. */
    int coordFrame = 0;          /**< FIFFV_COORD_*. */
    float cal = 1.0f;            /**< FIFF cal. */
    float range = 1.0f;          /**< FIFF range. */
    std::array<float, 12> loc{}; /**< Position r0 and orientation ex, ey, ez. */
};

//=============================================================================================================
/**
 * Description of one channel.
 */
struct AcqChannelInfo
{
    std::string name;                                           /**< Unique within the stream. */
    AcqChannelKind kind = AcqChannelKind::Unknown;              /**< Signal kind. */
    AcqUnit unit = AcqUnit::Unknown;                            /**< SI unit of the physical value. */
    int unitExponent = 0;                                       /**< Power of ten of the unit (e.g. -6 for uV). */
    double scale = 1.0;                                         /**< Physical = raw * scale + offset. */
    double offset = 0.0;                                        /**< See scale. */
    double rangeMin = std::numeric_limits<double>::quiet_NaN(); /**< Physical input range, NaN if unknown. */
    double rangeMax = std::numeric_limits<double>::quiet_NaN(); /**< Physical input range, NaN if unknown. */
    std::optional<AcqFiffChannel> fiff;                         /**< Present for FIFF-described channels. */
};

//=============================================================================================================
/**
 * Segmentation of a stream: continuous data, or segments (triggered readouts, epochs) each preceded
 * by a header that AcqBlockView::segments() delivers alongside the samples (data-path rule 7).
 */
struct AcqSegmentation
{
    /** Continuous or segmented. */
    enum class Mode : std::uint8_t
    {
        Continuous = 0, /**< No segment headers. */
        Triggered = 1   /**< Samples come in segments started by a trigger; each has an AcqSegmentHeader. */
    };

    Mode mode = Mode::Continuous;        /**< Continuous or segmented. */
    std::uint32_t samplesPerSegment = 0; /**< Fixed segment length, 0 for variable length. */
    std::uint32_t headerBytes = 0;       /**< Size of the device-specific header payload per segment. */
    std::string headerSchema;            /**< Identifier of the payload format, e.g. "ismrmrd.AcquisitionHeader/1". */
};

//=============================================================================================================
/**
 * Complete description of one stream at one metadata version.
 */
struct AcqStreamInfo
{
    std::string name;                                            /**< Stream name, unique within its source (e.g. "eeg"). */
    std::string sourceId;                                        /**< Device serial, file path or LSL source id. */
    AcqSampleFormat format = AcqSampleFormat::Float32;           /**< Native sample format stored in the ring. */
    AcqLayout layout = AcqLayout::Interleaved;                   /**< Ring layout. */
    double sfreq = 0.0;                                          /**< Nominal rate in Hz; 0 for irregular streams. */
    std::vector<AcqChannelInfo> channels;                        /**< One entry per channel, in ring order. */
    AcqSegmentation segmentation;                                /**< Continuous unless stated otherwise. */
    std::vector<std::string> eventLabels;                        /**< Optional names of AcqEvent::code values, index = code. */
    std::vector<std::pair<std::string, std::string>> properties; /**< Free-form device properties (firmware, ...). */

    std::uint32_t version = 0;         /**< Metadata version; incremented on every change. */
    std::uint64_t validFromSample = 0; /**< First sample index this version describes. */

    /**
     * Opaque serialized FIFF measurement-info block (everything of FiffInfo that is not per-channel:
     * digitizer points, projectors, compensators, transforms, measurement date). Written and read only
     * by @ref acq_fiff.h; makes the FiffInfo round trip lossless without exposing FIFF types here.
     */
    std::vector<std::byte> fiffMeasInfo;

    /** @return Number of channels. */
    [[nodiscard]] std::size_t channelCount() const noexcept
    {
        return channels.size();
    }

    /** @return Bytes of one frame (all channels of one sample index). */
    [[nodiscard]] std::size_t frameBytes() const noexcept
    {
        return channels.size() * bytesPerSample(format);
    }
};

} // namespace ACQLIB

#endif // ACQ_STREAM_INFO_H
