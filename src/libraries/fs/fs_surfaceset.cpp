//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     fs_surfaceset.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Implementation of @ref FSLIB::FsSurfaceSet: bundles the lh + rh @ref FSLIB::FsSurface for one subject and surface kind.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fs_surfaceset.h"

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <utils/string_utils.h>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FSLIB;
using namespace Eigen;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

FsSurfaceSet::FsSurfaceSet()
{
}

//=============================================================================================================

FsSurfaceSet::FsSurfaceSet(const std::string& subject_id, qint32 hemi, const std::string& surf, const std::filesystem::path& subjects_dir)
{
    FsSurface t_Surface;
    if (hemi == 0 || hemi == 1) {
        if (FsSurface::read(subject_id, hemi, surf, subjects_dir, t_Surface))
            insert(t_Surface);
    } else if (hemi == 2) {
        if (FsSurface::read(subject_id, 0, surf, subjects_dir, t_Surface))
            insert(t_Surface);
        if (FsSurface::read(subject_id, 1, surf, subjects_dir, t_Surface))
            insert(t_Surface);
    }

    calcOffset();
}

//=============================================================================================================

FsSurfaceSet::FsSurfaceSet(const std::filesystem::path& path, qint32 hemi, const std::string& surf)
{
    FsSurface t_Surface;
    if (hemi == 0 || hemi == 1) {
        if (FsSurface::read(path, hemi, surf, t_Surface))
            insert(t_Surface);
    } else if (hemi == 2) {
        if (FsSurface::read(path, 0, surf, t_Surface))
            insert(t_Surface);
        if (FsSurface::read(path, 1, surf, t_Surface))
            insert(t_Surface);
    }

    calcOffset();
}

//=============================================================================================================

FsSurfaceSet::FsSurfaceSet(const FsSurface& p_LHSurface, const FsSurface& p_RHSurface)
{
    if (p_LHSurface.hemi() == 0)
        m_qMapSurfs.insert(0, p_LHSurface);
    else
        qWarning("Left hemisphere id is not 0. LH surface not assigned!");

    if (p_RHSurface.hemi() == 1)
        m_qMapSurfs.insert(1, p_RHSurface);
    else
        qWarning("Right hemisphere id is not 1. RH surface not assigned!");

    calcOffset();
}

//=============================================================================================================

FsSurfaceSet::FsSurfaceSet(const std::filesystem::path& p_sLHFileName, const std::filesystem::path& p_sRHFileName)
{
    FsSurfaceSet t_SurfaceSet;
    if (FsSurfaceSet::read(p_sLHFileName, p_sRHFileName, t_SurfaceSet))
        *this = t_SurfaceSet;
}

//=============================================================================================================

FsSurfaceSet::~FsSurfaceSet()
{
}

//=============================================================================================================

void FsSurfaceSet::clear()
{
    m_qMapSurfs.clear();
}

//=============================================================================================================

void FsSurfaceSet::insert(const FsSurface& p_Surface)
{
    if (p_Surface.empty())
        return;

    qint32 hemi = p_Surface.hemi();
    m_qMapSurfs.remove(hemi);

    m_qMapSurfs.insert(hemi, p_Surface);
}

//=============================================================================================================

bool FsSurfaceSet::read(const std::filesystem::path& p_sLHFileName, const std::filesystem::path& p_sRHFileName, FsSurfaceSet& p_SurfaceSet)
{
    p_SurfaceSet.clear();

    for (const std::filesystem::path* file : {&p_sLHFileName, &p_sRHFileName}) {
        FsSurface t_Surface;
        if (FsSurface::read(*file, t_Surface))
            p_SurfaceSet.m_qMapSurfs.insert(t_Surface.hemi(), t_Surface);
    }

    if (p_SurfaceSet.m_qMapSurfs.isEmpty())
        return false;

    p_SurfaceSet.calcOffset();

    return true;
}

//=============================================================================================================

namespace
{

qint32 hemiIndex(qint32 idx)
{
    if (idx != 0 && idx != 1) {
        qWarning("Warning: Index is not '0' or '1'! Returning '0'.");
        return 0;
    }
    return idx;
}

qint32 hemiIndex(std::string_view idt)
{
    if (idt == "rh")
        return 1;
    if (idt != "lh")
        qWarning("Warning: Identifier is not 'lh' or 'rh'! Returning 'lh'.");
    return 0;
}

const FsSurface& surfaceOrEmpty(const QMap<qint32, FsSurface>& surfs, qint32 hemi)
{
    static const FsSurface empty;
    const auto it = surfs.constFind(hemi);
    return it != surfs.constEnd() ? it.value() : empty;
}

} // namespace

//=============================================================================================================

const FsSurface& FsSurfaceSet::operator[](qint32 idx) const
{
    return surfaceOrEmpty(m_qMapSurfs, hemiIndex(idx));
}

//=============================================================================================================

FsSurface& FsSurfaceSet::operator[](qint32 idx)
{
    return m_qMapSurfs[hemiIndex(idx)];
}

//=============================================================================================================

const FsSurface& FsSurfaceSet::operator[](std::string_view idt) const
{
    return surfaceOrEmpty(m_qMapSurfs, hemiIndex(idt));
}

//=============================================================================================================

FsSurface& FsSurfaceSet::operator[](std::string_view idt)
{
    return m_qMapSurfs[hemiIndex(idt)];
}

//=============================================================================================================

void FsSurfaceSet::calcOffset()
{
    //
    // Correct inflated offset
    //
    if (m_qMapSurfs.size() == 2 && m_qMapSurfs.begin().value().surf() == "inflated") {
        float xOffset = m_qMapSurfs.find(0).value().rr().col(0).maxCoeff() - m_qMapSurfs.find(1).value().rr().col(0).minCoeff();
        Vector3f vecLhOffset, vecRhOffset;
        vecLhOffset << (xOffset / 2.0f), 0, 0;
        vecRhOffset << (-xOffset / 2.0f), 0, 0;
        m_qMapSurfs.find(0).value().offset() = vecLhOffset;
        m_qMapSurfs.find(1).value().offset() = vecRhOffset;
    }
}
