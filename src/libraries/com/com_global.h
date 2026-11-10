//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2012-2026 MNE-CPP Authors
 *
 * @file     com_global.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    0.1.0
 * @date     December, 2012
 * @brief    Export macros, build-info entry points and namespace anchor for the @c COMLIB communication library.
 *
 * This header defines @ref COMSHARED_EXPORT, expanding to the export decoration when building the shared library,
 * the import one when consuming it, and to nothing for the static build configuration (@c STATICBUILD). It also
 * declares the @c buildDateTime / @c buildHash / @c buildHashLong entry points.
 */

#ifndef COMMUNICATION_GLOBAL_H
#define COMMUNICATION_GLOBAL_H

//=============================================================================================================
// DEFINES
//=============================================================================================================

#if defined(STATICBUILD)
#define COMSHARED_EXPORT
#elif defined(_WIN32)
#if defined(MNE_COM_LIBRARY)
#define COMSHARED_EXPORT __declspec(dllexport) /**< Exported when building the shared library. */
#else
#define COMSHARED_EXPORT __declspec(dllimport) /**< Imported by clients of the shared library. */
#endif
#else
#define COMSHARED_EXPORT __attribute__((visibility("default"))) /**< Default visibility on ELF/Mach-O. */
#endif

//=============================================================================================================
/**
 * @namespace COMLIB
 * @brief Byte transports to devices and servers in standard C++: TCP connections, UDP sockets and serial ports,
 *        each with timeouts and std::error_code results.
 */
namespace COMLIB
{

//=============================================================================================================
/**
 * @brief Compile-time date/time stamp baked into the @c COMLIB binary.
 *
 * Resolved from the @c __DATE__ / @c __TIME__ macros at build time. Used by About dialogs and bug
 * reports to disambiguate identically-versioned builds.
 *
 * @return Null-terminated build date/time string with static storage duration.
 */
COMSHARED_EXPORT const char* buildDateTime();

//=============================================================================================================
/**
 * @brief Abbreviated git commit hash (typically 7 hex chars) of the source tree this library was built from.
 *
 * Populated by the @c MNE_GIT_HASH_SHORT compile definition set by the
 * top-level CMake project. Returns an empty string for tree-only builds.
 *
 * @return Null-terminated abbreviated git hash string with static storage duration.
 */
COMSHARED_EXPORT const char* buildHash();

//=============================================================================================================
/**
 * @brief Full-length git commit hash (40 hex chars) of the source tree this library was built from.
 *
 * Populated by the @c MNE_GIT_HASH_LONG compile definition; intended for
 * scripted provenance checks where the abbreviated hash is ambiguous.
 *
 * @return Null-terminated full git hash string with static storage duration.
 */
COMSHARED_EXPORT const char* buildHashLong();
}

#endif // COM_GLOBAL_H
