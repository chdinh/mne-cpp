//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_fiff.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Conversion between AcqStreamInfo and FIFFLIB::FiffInfo.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_fiff.h"

#include <fiff/fiff_constants.h>
#include <fiff/fiff_file.h>
#include <fiff/fiff_proj.h>
#include <fiff/fiff_stream.h>

#include <cmath>
#include <cstring>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QBuffer>
#include <QByteArray>

//=============================================================================================================
// EIGEN INCLUDES
//=============================================================================================================

#include <Eigen/Core>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace FIFFLIB;

//=============================================================================================================
// DEFINE LOCAL
//=============================================================================================================

namespace
{

constexpr const char* kMeasInfoSchema = "fiff.MeasInfo/1";

struct KindMap
{
    AcqChannelKind kind;
    int fiff;
};

// First match wins in both directions, so the canonical FIFF kind of an AcqChannelKind comes first.
constexpr KindMap kKinds[] = {
    {AcqChannelKind::Eeg, FIFFV_EEG_CH},
    {AcqChannelKind::Meg, FIFFV_MEG_CH},
    {AcqChannelKind::Meg, FIFFV_REF_MEG_CH},
    {AcqChannelKind::Opm, FIFFV_MEG_CH},
    {AcqChannelKind::Ecg, FIFFV_ECG_CH},
    {AcqChannelKind::Eog, FIFFV_EOG_CH},
    {AcqChannelKind::Emg, FIFFV_EMG_CH},
    {AcqChannelKind::Seeg, FIFFV_SEEG_CH},
    {AcqChannelKind::Ecog, FIFFV_ECOG_CH},
    {AcqChannelKind::Dbs, FIFFV_DBS_CH},
    {AcqChannelKind::Fnirs, FIFFV_FNIRS_CH},
    {AcqChannelKind::Respiration, FIFFV_RESP_CH},
    {AcqChannelKind::Gsr, FIFFV_GALVANIC_CH},
    {AcqChannelKind::Stim, FIFFV_STIM_CH},
    {AcqChannelKind::Temperature, FIFFV_TEMPERATURE_CH},
    {AcqChannelKind::Misc, FIFFV_MISC_CH},
};

struct UnitMap
{
    AcqUnit unit;
    int fiff;
};

constexpr UnitMap kUnits[] = {
    {AcqUnit::None, FIFF_UNIT_NONE},
    {AcqUnit::Volt, FIFF_UNIT_V},
    {AcqUnit::Ampere, FIFF_UNIT_A},
    {AcqUnit::Tesla, FIFF_UNIT_T},
    {AcqUnit::TeslaPerMeter, FIFF_UNIT_T_M},
    {AcqUnit::AmpereMeter, FIFF_UNIT_AM},
    {AcqUnit::DegreeCelsius, FIFF_UNIT_CEL},
    {AcqUnit::Second, FIFF_UNIT_SEC},
    {AcqUnit::Ohm, FIFF_UNIT_OHM},
    {AcqUnit::Watt, FIFF_UNIT_W},
    {AcqUnit::Pascal, FIFF_UNIT_PA},
    {AcqUnit::Molar, FIFF_UNIT_MOL},
};

int fiffKind(AcqChannelKind kind)
{
    for (const KindMap& m : kKinds) {
        if (m.kind == kind) {
            return m.fiff;
        }
    }
    return FIFFV_MISC_CH;
}

AcqChannelKind acqKind(int fiff)
{
    for (const KindMap& m : kKinds) {
        if (m.fiff == fiff) {
            return m.kind;
        }
    }
    return AcqChannelKind::Unknown;
}

int fiffUnit(AcqUnit unit)
{
    for (const UnitMap& m : kUnits) {
        if (m.unit == unit) {
            return m.fiff;
        }
    }
    return FIFF_UNIT_NONE;
}

AcqUnit acqUnit(int fiff)
{
    for (const UnitMap& m : kUnits) {
        if (m.fiff == fiff) {
            return m.unit;
        }
    }
    return AcqUnit::Unknown;
}

// MNE-Python tells oxy- from deoxyhaemoglobin by the coil type and names the channels "S1_D1 hbo".
int coilType(const AcqChannelInfo& channel, int kind)
{
    if (kind == FIFFV_EEG_CH) {
        return FIFFV_COIL_EEG;
    }
    if (kind == FIFFV_FNIRS_CH) {
        const auto endsWith = [&](const char* suffix) {
            const std::size_t n = std::strlen(suffix);
            return channel.name.size() >= n && channel.name.compare(channel.name.size() - n, n, suffix) == 0;
        };
        return endsWith("hbo") ? FIFFV_COIL_FNIRS_HBO
            : endsWith("hbr")  ? FIFFV_COIL_FNIRS_HBR
                               : FIFFV_COIL_FNIRS_CW_AMPLITUDE;
    }
    return FIFFV_COIL_NONE;
}

QByteArray serialize(const FiffInfo& info)
{
    // The blocks of the source file are not copied: the payload must not depend on files elsewhere.
    FiffInfo copy = info;
    copy.filename.clear();
    auto memory = std::make_unique<UTILSLIB::MemoryByteStream>();
    const UTILSLIB::MemoryByteStream& written = *memory;
    Eigen::RowVectorXd cals;
    FiffStream::SPtr stream = FiffStream::start_writing_raw(std::move(memory), copy, cals, defaultMatrixXi, false);
    stream->finish_writing_raw();
    return QByteArray(reinterpret_cast<const char*>(written.data().data()), static_cast<qsizetype>(written.size()));
}

// FiffStream drops the spaces of names read from name lists ("MEG 0113" -> "MEG0113"); restore them from the channels.
std::string channelName(const std::vector<std::string>& channels, const std::string& name)
{
    for (const std::string& channel : channels) {
        std::string compact = channel;
        std::erase(compact, ' ');
        if (compact == name) {
            return channel;
        }
    }
    return name;
}

bool deserialize(const std::vector<std::byte>& data, FiffInfo& info)
{
    QByteArray bytes(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
    QBuffer buffer(&bytes);
    FiffStream stream(&buffer);
    if (!stream.open()) {
        return false;
    }
    FiffDirNode::SPtr node;
    return stream.read_meas_info(stream.dirtree(), info, node);
}

void restoreNames(FiffInfo& info)
{
    for (std::string& bad : info.bads) {
        bad = channelName(info.ch_names, bad);
    }
    for (FiffProj& proj : info.projs) {
        if (proj.data != nullptr) {
            for (std::string& name : proj.data->col_names) {
                name = channelName(info.ch_names, name);
            }
        }
    }
}

} // namespace

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

AcqStreamInfo ACQLIB::fromFiffInfo(const FiffInfo& info, AcqSampleFormat format)
{
    AcqStreamInfo stream;
    stream.name = "raw";
    stream.sourceId = info.filename;
    stream.format = format;
    stream.layout = AcqLayout::Interleaved;
    stream.sfreq = static_cast<double>(info.sfreq);
    stream.channels.reserve(static_cast<std::size_t>(info.chs.size()));
    for (const FiffChInfo& ch : info.chs) {
        AcqChannelInfo channel;
        channel.name = ch.ch_name;
        channel.kind = acqKind(ch.kind);
        channel.unit = acqUnit(ch.unit);
        channel.unitExponent = ch.unit_mul;
        channel.scale = static_cast<double>(ch.cal) * static_cast<double>(ch.range);
        AcqFiffChannel fiff;
        fiff.kind = ch.kind;
        fiff.coilType = ch.chpos.coil_type;
        fiff.logicalNo = ch.logNo;
        fiff.scannerNo = ch.scanNo;
        fiff.unit = ch.unit;
        fiff.unitMul = ch.unit_mul;
        fiff.coordFrame = ch.coord_frame;
        fiff.cal = ch.cal;
        fiff.range = ch.range;
        for (std::size_t i = 0; i < 3; ++i) {
            const auto e = static_cast<Eigen::Index>(i);
            fiff.loc[i] = ch.chpos.r0[e];
            fiff.loc[3 + i] = ch.chpos.ex[e];
            fiff.loc[6 + i] = ch.chpos.ey[e];
            fiff.loc[9 + i] = ch.chpos.ez[e];
        }
        channel.fiff = fiff;
        stream.channels.push_back(std::move(channel));
    }
    const QByteArray bytes = serialize(info);
    AcqExtension extension;
    extension.schema = kMeasInfoSchema;
    extension.data.resize(static_cast<std::size_t>(bytes.size()));
    std::memcpy(extension.data.data(), bytes.constData(), extension.data.size());
    stream.extensions.push_back(std::move(extension));
    return stream;
}

//=============================================================================================================

AcqExpected<FiffInfo> ACQLIB::toFiffInfo(const AcqStreamInfo& info)
{
    if (isComplex(info.format) || info.segmentation.mode != AcqSegmentation::Mode::Continuous) {
        return AcqError(AcqErrorCode::UnsupportedFormat, "FIFF raw data holds continuous real samples only");
    }
    FiffInfo fiff;
    bool fromFiff = false;
    for (const AcqExtension& extension : info.extensions) {
        if (extension.schema == kMeasInfoSchema) {
            if (!deserialize(extension.data, fiff)) {
                return AcqError(AcqErrorCode::InvalidParameter, "corrupt fiff.MeasInfo/1 extension");
            }
            fromFiff = true;
        }
    }
    if (!fromFiff) {
        fiff.highpass = 0.0f;
        fiff.lowpass = static_cast<float>(info.sfreq / 2.0);
        fiff.proj_id = 0;
        fiff.meas_date[0] = -1;
        fiff.meas_date[1] = 0;
    }
    fiff.sfreq = static_cast<float>(info.sfreq);
    fiff.chs.clear();
    fiff.ch_names.clear();
    int index = 0;
    for (const AcqChannelInfo& channel : info.channels) {
        FiffChInfo ch;
        ch.ch_name = channel.name;
        ch.scanNo = ++index;
        if (channel.fiff) {
            const AcqFiffChannel& f = *channel.fiff;
            ch.kind = f.kind;
            ch.chpos.coil_type = f.coilType;
            ch.logNo = f.logicalNo;
            ch.unit = f.unit;
            ch.coord_frame = f.coordFrame;
            for (std::size_t i = 0; i < 3; ++i) {
                const auto e = static_cast<Eigen::Index>(i);
                ch.chpos.r0[e] = f.loc[i];
                ch.chpos.ex[e] = f.loc[3 + i];
                ch.chpos.ey[e] = f.loc[6 + i];
                ch.chpos.ez[e] = f.loc[9 + i];
            }
            const bool unchanged = channel.scale == static_cast<double>(f.cal) * static_cast<double>(f.range) &&
                channel.unitExponent == f.unitMul;
            ch.cal = unchanged ? f.cal : static_cast<float>(channel.scale * std::pow(10.0, channel.unitExponent));
            ch.range = unchanged ? f.range : 1.0f;
            ch.unit_mul = unchanged ? f.unitMul : FIFF_UNITM_NONE;
        } else {
            // MNE-Python applies cal * range but not unit_mul, so the power of ten goes into cal.
            ch.kind = channel.unit == AcqUnit::Unknown ? FIFFV_MISC_CH : fiffKind(channel.kind);
            ch.unit = fiffUnit(channel.unit);
            ch.unit_mul = FIFF_UNITM_NONE;
            ch.logNo = index;
            ch.chpos.coil_type = coilType(channel, ch.kind);
            ch.coord_frame = ch.kind == FIFFV_EEG_CH ? FIFFV_COORD_HEAD : FIFFV_COORD_UNKNOWN;
            ch.cal = static_cast<float>(channel.scale * std::pow(10.0, channel.unitExponent));
            ch.range = 1.0f;
        }
        fiff.chs.append(ch);
        fiff.ch_names.push_back(ch.ch_name);
    }
    fiff.nchan = static_cast<fiff_int_t>(fiff.chs.size());
    restoreNames(fiff);
    return fiff;
}
