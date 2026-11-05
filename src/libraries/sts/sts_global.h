//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     sts_global.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.2.0
 * @date     April 2026
 * @brief    STSLIB shared-library export macro and build-info accessors for the statistical-tests library.
 *
 * STSLIB is the mne-cpp library that performs frequentist inference on
 * sensor- and source-level M/EEG data. It bundles classic parametric
 * tests (one-sample, paired and two-sample Student t-tests, one-way
 * ANOVA), the family of multiple-comparison corrections used in the
 * neuroimaging literature (Bonferroni, Holm-Bonferroni, Benjamini-Hochberg
 * FDR), Maris-Oostenveld cluster permutation testing with sign-flip and
 * label-shuffle nulls, Threshold-Free Cluster Enhancement, regularised
 * covariance estimators (Ledoit-Wolf, OAS, PCA, factor analysis,
 * cross-validated auto-select) and the peak-error / spatial-dispersion
 * source-localisation metrics used for inverse-solution evaluation.
 *
 * This header only exposes the @c STSSHARED_EXPORT decoration and the
 * three @c buildDateTime / @c buildHash accessors that let downstream
 * tools stamp the exact STSLIB binary used to produce a result; all
 * statistical machinery lives in the per-method headers in this folder.
 */

#ifndef STS_GLOBAL_H
#define STS_GLOBAL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <utils/buildinfo.h>

//=============================================================================================================
// DEFINES
//=============================================================================================================

#if defined(STATICBUILD)
#define STSSHARED_EXPORT
#elif defined(_WIN32)
#if defined(MNE_STS_LIBRARY)
#define STSSHARED_EXPORT __declspec(dllexport) /**< Exported when building the shared library. */
#else
#define STSSHARED_EXPORT __declspec(dllimport) /**< Imported by clients of the shared library. */
#endif
#else
#define STSSHARED_EXPORT __attribute__((visibility("default"))) /**< Default visibility on ELF/Mach-O. */
#endif

//=============================================================================================================
/**
 * @namespace STSLIB
 * @brief     Statistical testing (t-tests, F-tests, cluster permutation, multiple comparison correction).
 */
namespace STSLIB
{

//=============================================================================================================
/**
 * Returns build date and time.
 *
 * @return Static C string with the build timestamp.
 */
STSSHARED_EXPORT const char* buildDateTime();

//=============================================================================================================
/**
 * Returns abbreviated build git hash.
 *
 * @return Static C string with the short git commit hash.
 */
STSSHARED_EXPORT const char* buildHash();

//=============================================================================================================
/**
 * Returns full build git hash.
 *
 * @return Static C string with the full git commit hash.
 */
STSSHARED_EXPORT const char* buildHashLong();

}

#endif // STS_GLOBAL_H
