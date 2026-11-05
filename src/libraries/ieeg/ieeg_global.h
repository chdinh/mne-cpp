//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     ieeg_global.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.4.0
 * @date     October 2026
 * @brief    IEEGLIB shared-library export macro and build-info accessors for the intracranial EEG library.
 *
 * IEEGLIB holds what is specific to intracranial recordings, sEEG depth leads and ECoG strips and
 * grids: the implantation (contacts grouped into leads), bipolar re-referencing along the leads,
 * and the mapping of contact values onto the cortical surface.
 */

#ifndef IEEG_GLOBAL_H
#define IEEG_GLOBAL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <utils/buildinfo.h>

//=============================================================================================================
// DEFINES
//=============================================================================================================

#if defined(STATICBUILD)
#define IEEGSHARED_EXPORT
#elif defined(_WIN32)
#if defined(MNE_IEEG_LIBRARY)
#define IEEGSHARED_EXPORT __declspec(dllexport) /**< Exported when building the shared library. */
#else
#define IEEGSHARED_EXPORT __declspec(dllimport) /**< Imported by clients of the shared library. */
#endif
#else
#define IEEGSHARED_EXPORT __attribute__((visibility("default"))) /**< Default visibility on ELF/Mach-O. */
#endif

//=============================================================================================================
/**
 * @namespace IEEGLIB
 * @brief     Intracranial EEG (sEEG, ECoG): implantation, bipolar referencing, cortical mapping.
 */
namespace IEEGLIB
{

//=============================================================================================================
/**
 * Returns build date and time.
 *
 * @return Static C string with the build timestamp.
 */
IEEGSHARED_EXPORT const char* buildDateTime();

//=============================================================================================================
/**
 * Returns abbreviated build git hash.
 *
 * @return Static C string with the short git commit hash.
 */
IEEGSHARED_EXPORT const char* buildHash();

//=============================================================================================================
/**
 * Returns full build git hash.
 *
 * @return Static C string with the full git commit hash.
 */
IEEGSHARED_EXPORT const char* buildHashLong();

} // namespace IEEGLIB

#endif // IEEG_GLOBAL_H
