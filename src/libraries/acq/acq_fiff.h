//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_fiff.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Lossless conversion between AcqStreamInfo and FIFFLIB::FiffInfo.
 *
 * This is the only ACQLIB header that exposes FIFF (and therefore Qt) types (D9). Include it only in
 * code that already uses mne_fiff, and link mne_fiff yourself.
 *
 * Round trip: per-channel fields map to AcqChannelInfo (name, kind, unit, scale = cal * range) and
 * are kept verbatim in AcqChannelInfo::fiff; everything else of FiffInfo is stored serialized in
 * AcqStreamInfo::fiffMeasInfo. toFiffInfo(fromFiffInfo(x)) equals x field by field (AC-T2.1-2).
 * When a channel was edited in AcqStreamInfo, the edited per-channel fields win.
 */

#ifndef ACQ_FIFF_H
#define ACQ_FIFF_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_error.h"
#include "acq_sample_format.h"
#include "acq_stream_info.h"

#include <fiff/fiff_info.h>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * Describes a FIFF measurement as a stream.
 *
 * @param[in] info     FIFF measurement info.
 * @param[in] format   Native format the producer will store (FIFF replay: the file's data type).
 *
 * @return The stream description, version 0.
 */
ACQSHARED_EXPORT AcqStreamInfo fromFiffInfo(const FIFFLIB::FiffInfo& info,
                                            AcqSampleFormat format = AcqSampleFormat::Float32);

//=============================================================================================================
/**
 * Builds FIFF measurement info for a stream, e.g. for recording any source to FIFF.
 *
 * @param[in] info   Stream description. Channels without AcqChannelInfo::fiff get FIFF fields derived
 *                   from kind and unit; an AcqUnit::Unknown channel becomes FIFFV_MISC_CH with
 *                   FIFF_UNIT_NONE rather than a guessed unit.
 *
 * @return The FIFF info, or UnsupportedFormat for complex or segmented streams (no FIFF representation).
 */
ACQSHARED_EXPORT AcqExpected<FIFFLIB::FiffInfo> toFiffInfo(const AcqStreamInfo& info);

} // namespace ACQLIB

#endif // ACQ_FIFF_H
