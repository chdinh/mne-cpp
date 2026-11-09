//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021-2026 MNE-CPP Authors
 *
 * @file     base_global.cpp
 * @author   Gabriel Motta <gabrielbenmotta@gmail.com>;
 *           Juan GPC <jgarciaprieto@mgh.harvard.edu>;
 *           Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     August 2021
 * @brief    Out-of-line definitions for the BASELIB build-info accessors declared in @ref base_global.h.
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

#include "base_global.h"

//=============================================================================================================
// DEFINE METHODS
//=============================================================================================================

const char* BASELIB::buildDateTime()
{
    return BASELIB::dateTimeNow();
}

//=============================================================================================================

const char* BASELIB::buildHash()
{
    return BASELIB::gitHash();
}

//=============================================================================================================

const char* BASELIB::buildHashLong()
{
    return BASELIB::gitHashLong();
}
