//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_global.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Definitions of the ACQLIB build-info accessors declared in @ref acq_global.h.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"

// Standalone (embedded) builds do not pass the git hashes of the MNE-CPP super-build.
#ifndef MNE_GIT_HASH_SHORT
#define MNE_GIT_HASH_SHORT "unknown"
#endif
#ifndef MNE_GIT_HASH_LONG
#define MNE_GIT_HASH_LONG "unknown"
#endif

//=============================================================================================================
// DEFINE METHODS
//=============================================================================================================

const char* ACQLIB::buildDateTime()
{
    return __DATE__ " " __TIME__;
}

//=============================================================================================================

const char* ACQLIB::buildHash()
{
    return MNE_GIT_HASH_SHORT;
}

//=============================================================================================================

const char* ACQLIB::buildHashLong()
{
    return MNE_GIT_HASH_LONG;
}
