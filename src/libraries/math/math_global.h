//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     math_global.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Export/import macros and build-stamp accessors for MATHLIB.
 *
 * MATHLIB is the numerical-helpers layer of mne-cpp: linear algebra
 * (@ref UTILSLIB::Linalg), general numerics and baseline rescaling
 * (@ref UTILSLIB::Numerics), K-means clustering (@ref UTILSLIB::KMeans),
 * multi-taper spectral estimation (@ref UTILSLIB::Spectral),
 * Nelder–Mead simplex optimisation
 * (@ref UTILSLIB::SimplexAlgorithm), sphere fitting (@ref UTILSLIB::Sphere)
 * and thin-plate-spline warping (@ref UTILSLIB::Warp). It depends only on
 * Eigen, Qt core and UTILSLIB and is therefore safe to pull into any
 * higher-level library (FIFFLIB, FWDLIB, INVERSELIB, CONNECTIVITYLIB).
 *
 * This header defines the @c MATHSHARED_EXPORT visibility macro that
 * toggles between symbol export and import depending on
 * whether the library itself or a consumer is being compiled, plus the
 * @c STATICBUILD short-circuit used on WebAssembly and other static
 * deployments. It also exposes @c buildDateTime(), @c buildHash() and
 * @c buildHashLong() so applications can stamp produced files with the
 * exact MATHLIB revision they were linked against.
 */

#ifndef MATH_GLOBAL_H
#define MATH_GLOBAL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <utils/buildinfo.h>

//=============================================================================================================
// DEFINES
//=============================================================================================================

#if defined(STATICBUILD)
#define MATHSHARED_EXPORT
#elif defined(_WIN32)
#if defined(MNE_MATH_LIBRARY)
#define MATHSHARED_EXPORT __declspec(dllexport) /**< Exported when building the shared library. */
#else
#define MATHSHARED_EXPORT __declspec(dllimport) /**< Imported by clients of the shared library. */
#endif
#else
#define MATHSHARED_EXPORT __attribute__((visibility("default"))) /**< Default visibility on ELF/Mach-O. */
#endif

//=============================================================================================================
/**
 * @namespace MATHLIB
 * @brief     Mathematical algorithms and geometry (linear algebra, optimization, spectral estimation).
 */
namespace MATHLIB
{

//=============================================================================================================
/**
 * Returns build date and time.
 *
 * @return Static C string with the compile date and time, e.g. "Sep 30 2026 14:05:12".
 */
MATHSHARED_EXPORT const char* buildDateTime();

//=============================================================================================================
/**
 * Returns abbreviated build git hash.
 *
 * @return Static C string with the short git commit hash, or a "not defined" notice if unavailable at build time.
 */
MATHSHARED_EXPORT const char* buildHash();

//=============================================================================================================
/**
 * Returns full build git hash.
 *
 * @return Static C string with the full git commit hash, or a "not defined" notice if unavailable at build time.
 */
MATHSHARED_EXPORT const char* buildHashLong();
}

#endif // MATH_GLOBAL_H
