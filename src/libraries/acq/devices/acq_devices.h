//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_devices.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Sources for vendor EEG amplifiers, each with a simulator of its wire format.
 *
 * Each amplifier is a URI scheme of AcqDeviceRegistry. The streams carry the amplifier's own sample format
 * (BrainAmp: 16-bit channels in steps of the chosen resolution, then the marker word, at 5 kHz; TMSi: a 32-bit
 * integer per channel with the SDK's gain, offset and exponent; g.USBamp: a float per channel in microvolts), so readers
 * see exactly what the driver delivers. simulate=1 replaces the vendor driver with a simulator that digitises
 * a synthetic:// session in that format, which runs on every platform and needs no hardware; seed, realtime
 * and duration work as for synthetic://. The real driver interfaces are Windows-only: BrainAmp's is compiled when
 * the vendor's SDK header is found (CMake variable MNE_BRAINAMP_SDK_DIR), TMSi's and g.USBamp's load the vendor's
 * system DLL at run time. Without them, opening without simulate=1 fails with BackendUnavailable.
 *
 * @code
 * ACQLIB::registerBrainAmpScheme(); // needed only where static linking drops the library's initialiser
 * auto device = ACQLIB::AcqDevice::open("brainamp://?simulate=1&channels=32");
 * @endcode
 */

#ifndef ACQ_DEVICES_H
#define ACQ_DEVICES_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_devices_global.h"
#include "../acq_error.h"

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

/**
 * Registers the brainamp:// scheme (Brain Products BrainAmp) with AcqDeviceRegistry::instance(). Loading the
 * library registers it already; calling this keeps a static link from dropping the backend. Thread-safe;
 * later calls return the first result.
 *
 * @return Success, or the error of the registration.
 */
ACQDEVICESSHARED_EXPORT AcqError registerBrainAmpScheme();

/**
 * Registers the tmsi:// scheme (TMSi Refa and Porti) with AcqDeviceRegistry::instance(), as
 * registerBrainAmpScheme() does for brainamp://.
 *
 * @return Success, or the error of the registration.
 */
ACQDEVICESSHARED_EXPORT AcqError registerTmsiScheme();

/**
 * Registers the gusbamp:// scheme (g.tec g.USBamp, up to four synchronised) with AcqDeviceRegistry::instance(), as
 * registerBrainAmpScheme() does for brainamp://.
 *
 * @return Success, or the error of the registration.
 */
ACQDEVICESSHARED_EXPORT AcqError register_gusbamp_scheme();

} // namespace ACQLIB

#endif // ACQ_DEVICES_H
