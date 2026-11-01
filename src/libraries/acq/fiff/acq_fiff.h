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
 * Part of the separate bridge library mne_acq_fiff, the only ACQLIB component that depends on FIFF
 * (and therefore Qt); mne_acq itself stays standard-library only.
 *
 * Round trip: per-channel fields map to AcqChannelInfo (name, kind, unit, scale = cal * range) and
 * are kept verbatim in AcqChannelInfo::fiff; everything else of FiffInfo is stored serialized in an
 * AcqExtension with schema "fiff.MeasInfo/1". toFiffInfo(fromFiffInfo(x)) equals x field by field.
 * When a channel was edited in AcqStreamInfo, the edited per-channel fields win.
 */

#ifndef ACQ_FIFF_H
#define ACQ_FIFF_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../acq_error.h"
#include "../acq_sample_format.h"
#include "../acq_stream_info.h"

#include <fiff/fiff_info.h>

//=============================================================================================================
// DEFINES
//=============================================================================================================

#if defined(STATICBUILD)
#define ACQFIFFSHARED_EXPORT
#elif defined(_WIN32)
#if defined(MNE_ACQ_FIFF_LIBRARY)
#define ACQFIFFSHARED_EXPORT __declspec(dllexport) /**< Exported when building mne_acq_fiff. */
#else
#define ACQFIFFSHARED_EXPORT __declspec(dllimport) /**< Imported by clients of mne_acq_fiff. */
#endif
#else
#define ACQFIFFSHARED_EXPORT __attribute__((visibility("default"))) /**< Default visibility on ELF/Mach-O. */
#endif

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
ACQFIFFSHARED_EXPORT AcqStreamInfo fromFiffInfo(const FIFFLIB::FiffInfo& info,
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
ACQFIFFSHARED_EXPORT AcqExpected<FIFFLIB::FiffInfo> toFiffInfo(const AcqStreamInfo& info);

} // namespace ACQLIB

#endif // ACQ_FIFF_H
