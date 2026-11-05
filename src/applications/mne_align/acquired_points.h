// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2010-2026 MNE-CPP Authors
//   Christoph Dinh <christoph.dinh@mne-cpp.org>

// Forwarding header for the digitiser types, plus conversions to the Qt types the 3D view uses.
#ifndef MNE_ALIGN_ACQUIRED_POINTS_H
#define MNE_ALIGN_ACQUIRED_POINTS_H

#include <acq/polhemus/acquired_points.h>

#include <Eigen/Geometry>
#include <QQuaternion>
#include <QVector3D>

namespace MNEALIGN
{
using ACQLIB::PointKind;
using ACQLIB::FiducialId;
using ACQLIB::DigitizedPoint;
using ACQLIB::AcquiredPoints;

inline QVector3D toQt(const Eigen::Vector3f& v)
{
    return QVector3D(v.x(), v.y(), v.z());
}

inline QQuaternion toQt(const Eigen::Quaternionf& q)
{
    return QQuaternion(q.w(), q.x(), q.y(), q.z());
}

inline Eigen::Vector3f toEigen(const QVector3D& v)
{
    return Eigen::Vector3f(v.x(), v.y(), v.z());
}
} // namespace MNEALIGN

#endif // MNE_ALIGN_ACQUIRED_POINTS_H
