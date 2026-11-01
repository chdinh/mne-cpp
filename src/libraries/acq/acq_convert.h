//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_convert.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Vectorised conversion of native samples into caller-owned Eigen matrices (T2.8).
 *
 * The converters read any native format and layout and write a channels x samples matrix that the
 * caller owns and sizes; they never allocate. With AcqScaling::Physical each channel is mapped to
 * raw * scale + offset of its stream info; with AcqScaling::Raw the numeric sample value is written
 * unchanged. Results are bit-exact against the scalar reference converter used in the tests.
 */

#ifndef ACQ_CONVERT_H
#define ACQ_CONVERT_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_global.h"
#include "acq_block_view.h"
#include "acq_error.h"
#include "acq_sample_format.h"

#include <Eigen/Core>

#include <cstddef>
#include <span>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * Whether converters apply the per-channel scale and offset.
 */
enum class AcqScaling : std::uint8_t
{
    Raw = 0,     /**< Numeric sample value, no scaling. */
    Physical = 1 /**< raw * scale + offset, in the channel's AcqUnit. */
};

//=============================================================================================================
/**
 * Converts all samples of @p view into columns [firstColumn, firstColumn + view.sampleCount()) of @p out.
 *
 * @param[in] view          Source view. Real formats only for the real overloads.
 * @param[out] out          Destination, channels x at least firstColumn + view.sampleCount() columns.
 * @param[in] scaling       Raw or physical values.
 * @param[in] firstColumn   First destination column.
 *
 * @return ShapeMismatch if @p out is too small, UnsupportedFormat for a complex view; otherwise success.
 */
ACQSHARED_EXPORT AcqError convert(const AcqBlockView& view, Eigen::Ref<Eigen::MatrixXf> out,
                                  AcqScaling scaling = AcqScaling::Physical, Eigen::Index firstColumn = 0) noexcept;

/** @copydoc convert(const AcqBlockView&, Eigen::Ref<Eigen::MatrixXf>, AcqScaling, Eigen::Index) */
ACQSHARED_EXPORT AcqError convert(const AcqBlockView& view, Eigen::Ref<Eigen::MatrixXd> out,
                                  AcqScaling scaling = AcqScaling::Physical, Eigen::Index firstColumn = 0) noexcept;

/** Complex overload; accepts every format (real formats yield a zero imaginary part). */
ACQSHARED_EXPORT AcqError convert(const AcqBlockView& view, Eigen::Ref<Eigen::MatrixXcf> out,
                                  AcqScaling scaling = AcqScaling::Physical, Eigen::Index firstColumn = 0) noexcept;

//=============================================================================================================
/**
 * Converts one contiguous part described by raw pointers and strides; the building block of the view
 * overloads, exposed for adopted (DMA) buffers that are not read through an AcqReader.
 *
 * @param[in] part       Source run.
 * @param[in] format     Native format of the run.
 * @param[in] scales     Per-channel scale, size = out.rows(); ignored for AcqScaling::Raw.
 * @param[in] offsets    Per-channel offset, size = out.rows(); ignored for AcqScaling::Raw.
 * @param[out] out       Destination block, channels x part.samples.
 * @param[in] scaling    Raw or physical values.
 *
 * @return ShapeMismatch or UnsupportedFormat on invalid arguments; otherwise success.
 */
ACQSHARED_EXPORT AcqError convertPart(const AcqBlockPart& part, AcqSampleFormat format,
                                      std::span<const double> scales, std::span<const double> offsets,
                                      Eigen::Ref<Eigen::MatrixXf> out, AcqScaling scaling) noexcept;

} // namespace ACQLIB

#endif // ACQ_CONVERT_H
