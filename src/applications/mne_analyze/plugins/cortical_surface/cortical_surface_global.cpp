//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *

 * @file     cortical_surface_global.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.3.0
 * @date     May, 2026
 *
 * @brief    Build-info accessors for the CorticalSurface plugin.
 */

#include "cortical_surface_global.h"

namespace CORTICALSURFACEPLUGIN
{

const char* buildDateTime()
{
    return BASELIB::dateTimeNow();
}
const char* buildHash()
{
    return BASELIB::gitHash();
}
const char* buildHashLong()
{
    return BASELIB::gitHashLong();
}

} // namespace CORTICALSURFACEPLUGIN
