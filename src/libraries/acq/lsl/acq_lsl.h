//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_lsl.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    The lsl:// source: reads a Lab Streaming Layer stream.
 *
 * lsl://NAME opens the LSL stream called NAME (or the first stream matching type= or source_id=) as a device
 * with one input stream. liblsl is loaded at runtime when it is installed: MNE_CPP_LIBLSL (a file path), the
 * library that pylsl ships (pip install pylsl), then the system library paths. Without liblsl the in-tree
 * LSLLIB implementation is used; it reads only float32 streams from MNE-CPP outlets, not from liblsl outlets,
 * and has no timestamps. AcqDeviceDescription::backendVersion names the path in use ("liblsl 1.16" or
 * "in-tree"); library=liblsl or library=intree forces one.
 *
 * Samples keep the stream's format (float32, double64, int32, int16; int8 is widened to int16 and int64 is
 * read as double). Channel names, kinds and units come from the stream description (desc/channels/channel
 * with label, type and unit). With liblsl the clock model maps samples to LSL timestamps plus
 * time_correction(), refreshed every second; the in-tree path stamps arrival times on the host.
 * Irregular-rate and string (marker) streams are not supported yet.
 *
 * @code
 * ACQLIB::registerLslScheme(); // needed only where static linking drops the library's initialiser
 * auto device = ACQLIB::AcqDevice::open("lsl://BioSemi?timeout=5");
 * @endcode
 */

#ifndef ACQ_LSL_H
#define ACQ_LSL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_lsl_global.h"
#include "../acq_error.h"

#include <string>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * Registers the lsl:// scheme with AcqDeviceRegistry::instance(). Loading the library registers it already;
 * calling this keeps a static link from dropping the backend. Thread-safe; later calls return the first result.
 *
 * @return Success, or the error of the registration.
 */
ACQLSLSHARED_EXPORT AcqError registerLslScheme();

//=============================================================================================================
/**
 * @return "liblsl <version> (<path>)" for a liblsl that loads, otherwise "in-tree" and why liblsl is missing.
 */
ACQLSLSHARED_EXPORT std::string lslRuntime();

} // namespace ACQLIB

#endif // ACQ_LSL_H
