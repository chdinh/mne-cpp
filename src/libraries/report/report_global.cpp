//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     report_global.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Out-of-line definitions for the REPORTLIB build-info accessors declared in @ref report_global.h.
 *
 * Kept as a separate translation unit so the @c __DATE__ and
 * @c __TIME__ macros are expanded when this file is compiled
 * rather than when a downstream header is included — the
 * compile-time @c constexpr helpers in @ref buildinfo.h would
 * otherwise pick up the consumer's preprocessor invocation time
 * and report misleading build timestamps to bug reporters.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "report_global.h"

//=============================================================================================================
// DEFINE METHODS
//=============================================================================================================

const char* REPORTLIB::buildDateTime()
{
    return BASELIB::dateTimeNow();
}

//=============================================================================================================

const char* REPORTLIB::buildHash()
{
    return BASELIB::gitHash();
}

//=============================================================================================================

const char* REPORTLIB::buildHashLong()
{
    return BASELIB::gitHashLong();
}
