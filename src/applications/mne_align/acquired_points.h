// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2010-2026 MNE-CPP Authors
//   Christoph Dinh <christoph.dinh@mne-cpp.org>

// Thin forwarding header — the implementation now lives in mne_utils.
#ifndef MNE_ALIGN_ACQUIRED_POINTS_H
#define MNE_ALIGN_ACQUIRED_POINTS_H

#include <acq/polhemus/acquired_points.h>

namespace MNEALIGN
{
using ACQLIB::PointKind;
using ACQLIB::FiducialId;
using ACQLIB::DigitizedPoint;
using ACQLIB::AcquiredPoints;
} // namespace MNEALIGN

#endif // MNE_ALIGN_ACQUIRED_POINTS_H
