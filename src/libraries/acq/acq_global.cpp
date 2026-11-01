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

#include <utils/buildinfo.h>

//=============================================================================================================
// DEFINE METHODS
//=============================================================================================================

const char* ACQLIB::buildDateTime()
{
    return UTILSLIB::dateTimeNow();
}

//=============================================================================================================

const char* ACQLIB::buildHash()
{
    return UTILSLIB::gitHash();
}

//=============================================================================================================

const char* ACQLIB::buildHashLong()
{
    return UTILSLIB::gitHashLong();
}
