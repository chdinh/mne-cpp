//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     fs_surface.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Implementation of @ref FSLIB::FsSurface: reads FreeSurfer triangle / quad surface files into vertex+face matrices.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fs_surface.h"
#include <utils/logger/log.h>
#include <fiff/fiff_byte_swap.h>

#include <iostream>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <utils/string_utils.h>

#include <format>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FSLIB;
using namespace Eigen;
using namespace UTILSLIB;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

FsSurface::FsSurface()
: m_iHemi(-1)
, m_vecOffset(Vector3f::Zero(3))
{
}

//=============================================================================================================

FsSurface::FsSurface(const std::filesystem::path& p_sFile)
: m_iHemi(-1)
, m_vecOffset(Vector3f::Zero(3))
{
    FsSurface::read(p_sFile, *this);
}

//=============================================================================================================

FsSurface::FsSurface(const std::string& subject_id, qint32 hemi, const std::string& surf, const std::filesystem::path& subjects_dir)
: m_iHemi(-1)
, m_vecOffset(Vector3f::Zero(3))
{
    FsSurface::read(subject_id, hemi, surf, subjects_dir, *this);
}

//=============================================================================================================

FsSurface::FsSurface(const std::filesystem::path& path, qint32 hemi, const std::string& surf)
: m_iHemi(-1)
, m_vecOffset(Vector3f::Zero(3))
{
    FsSurface::read(path, hemi, surf, *this);
}

//=============================================================================================================

FsSurface::~FsSurface()
{
}

//=============================================================================================================

void FsSurface::clear()
{
    m_sFilePath.clear();
    m_sFileName.clear();
    m_iHemi = -1;
    m_sSurf.clear();
    m_matRR.resize(0, 3);
    m_matTris.resize(0, 3);
    m_matNN.resize(0, 3);
    m_vecCurv.resize(0);
}

//=============================================================================================================

MatrixX3f FsSurface::compute_normals(const MatrixX3f& rr, const MatrixX3i& tris)
{
    Log::info("\tcomputing normals\n");
    // first, compute triangle normals
    MatrixX3f r1(tris.rows(), 3);
    MatrixX3f r2(tris.rows(), 3);
    MatrixX3f r3(tris.rows(), 3);

    for (qint32 i = 0; i < tris.rows(); ++i) {
        r1.row(i) = rr.row(tris(i, 0));
        r2.row(i) = rr.row(tris(i, 1));
        r3.row(i) = rr.row(tris(i, 2));
    }

    MatrixX3f x = r2 - r1;
    MatrixX3f y = r3 - r1;
    MatrixX3f tri_nn(x.rows(), y.cols());
    tri_nn.col(0) = x.col(1).cwiseProduct(y.col(2)) - x.col(2).cwiseProduct(y.col(1));
    tri_nn.col(1) = x.col(2).cwiseProduct(y.col(0)) - x.col(0).cwiseProduct(y.col(2));
    tri_nn.col(2) = x.col(0).cwiseProduct(y.col(1)) - x.col(1).cwiseProduct(y.col(0));

    //   Triangle normals and areas
    MatrixX3f tmp = tri_nn.cwiseProduct(tri_nn);
    VectorXf normSize = tmp.rowwise().sum();
    normSize = normSize.cwiseSqrt();

    for (qint32 i = 0; i < normSize.size(); ++i)
        if (normSize(i) != 0)
            tri_nn.row(i) /= normSize(i);

    MatrixX3f nn = MatrixX3f::Zero(rr.rows(), 3);

    for (qint32 p = 0; p < tris.rows(); ++p) {
        Vector3i verts = tris.row(p);
        for (qint32 j = 0; j < verts.size(); ++j)
            nn.row(verts(j)) += tri_nn.row(p);
    }

    tmp = nn.cwiseProduct(nn);
    normSize = tmp.rowwise().sum();
    normSize = normSize.cwiseSqrt();

    for (qint32 i = 0; i < normSize.size(); ++i)
        if (normSize(i) != 0)
            nn.row(i) /= normSize(i);

    return nn;
}

//=============================================================================================================

bool FsSurface::read(const std::string& subject_id, qint32 hemi, const std::string& surf, const std::filesystem::path& subjects_dir, FsSurface& p_Surface, bool p_bLoadCurvature)
{
    if (hemi != 0 && hemi != 1)
        return false;

    return read(subjects_dir / pathFromUtf8(subject_id) / "surf", hemi, surf, p_Surface, p_bLoadCurvature);
}

//=============================================================================================================

bool FsSurface::read(const std::filesystem::path& path, qint32 hemi, const std::string& surf, FsSurface& p_Surface, bool p_bLoadCurvature)
{
    if (hemi != 0 && hemi != 1)
        return false;

    return read(path / pathFromUtf8(std::format("{}.{}", hemi == 0 ? "lh" : "rh", surf)), p_Surface, p_bLoadCurvature);
}

//=============================================================================================================

Expected<FsSurface::Geometry> FsSurface::read_geometry(const std::filesystem::path& file)
{
    auto bytes = read_file_bytes(file);
    if (!bytes)
        return std::unexpected(bytes.error());
    MemoryByteStream stream(std::move(*bytes));
    const auto remaining = [&stream]() {
        return static_cast<qint64>(stream.size() - stream.pos());
    };
    const auto invalid = [&file](std::string_view what) {
        return std::unexpected(Error(ErrorCode::Format, std::format("{} surface file {}", what, utf8FromPath(file))));
    };

    //   Magic numbers to identify QUAD and TRIANGLE files
    constexpr qint32 NEW_QUAD_FILE_MAGIC_NUMBER = 16777213;
    constexpr qint32 TRIANGLE_FILE_MAGIC_NUMBER = 16777214;
    constexpr qint32 QUAD_FILE_MAGIC_NUMBER = 16777215;

    const qint32 magic = FsSurface::fread3(stream);
    qint32 nvert = 0;
    MatrixXf verts;
    MatrixXi faces;

    if (magic == QUAD_FILE_MAGIC_NUMBER || magic == NEW_QUAD_FILE_MAGIC_NUMBER) {
        nvert = FsSurface::fread3(stream);
        const qint32 nquad = FsSurface::fread3(stream);
        // New-format curvature files start with the same magic number
        if (nvert <= 0 || qint64(nvert) * (magic == QUAD_FILE_MAGIC_NUMBER ? 6 : 12) + qint64(nquad) * 12 > remaining())
            return invalid("Invalid or truncated");

        // vertices, one column per vertex
        verts.resize(3, nvert);
        if (magic == QUAD_FILE_MAGIC_NUMBER) {
            std::vector<qint16> coords(3 * static_cast<std::size_t>(nvert));
            stream.readBigEndian(coords.data(), coords.size());
            for (std::size_t i = 0; i < coords.size(); ++i)
                verts.data()[i] = static_cast<float>(coords[i]) / 100;
        } else {
            stream.readBigEndian(verts.data(), 3 * static_cast<std::size_t>(nvert));
        }

        //  Each quad is split into two triangles
        const VectorXi quadIdx = FsSurface::fread3_many(stream, nquad * 4);
        faces.resize(2 * nquad, 3);
        for (qint32 k = 0; k < nquad; ++k) {
            const int* quad = quadIdx.data() + 4 * k;
            if ((quad[0] % 2) == 0) {
                faces.row(2 * k) << quad[0], quad[1], quad[3];
                faces.row(2 * k + 1) << quad[2], quad[3], quad[1];
            } else {
                faces.row(2 * k) << quad[0], quad[1], quad[2];
                faces.row(2 * k + 1) << quad[0], quad[2], quad[3];
            }
        }
    } else if (magic == TRIANGLE_FILE_MAGIC_NUMBER) {
        // Two text lines: creator and an empty line
        for (int newlines = 0; newlines < 2 && !stream.atEnd();) {
            char c = 0;
            stream.read(&c, 1);
            newlines += c == '\n' ? 1 : 0;
        }
        qint32 nface = 0;
        stream.readBigEndian(nvert);
        stream.readBigEndian(nface);
        if (nvert <= 0 || nface < 0 || (qint64(nvert) + nface) * 12 > remaining())
            return invalid("Invalid or truncated");

        verts.resize(3, nvert);
        stream.readBigEndian(verts.data(), 3 * static_cast<std::size_t>(nvert));

        Matrix<int, Dynamic, Dynamic, RowMajor> rowFaces(nface, 3);
        stream.readBigEndian(rowFaces.data(), 3 * static_cast<std::size_t>(nface));
        faces = rowFaces;
    } else {
        return invalid(std::format("Bad magic number ({}) in", magic));
    }
    if (faces.size() > 0 && (faces.minCoeff() < 0 || faces.maxCoeff() >= nvert))
        return invalid("Invalid or truncated");

    Geometry geometry;
    geometry.rr = verts.transpose() * 0.001f;
    geometry.tris = faces;
    return geometry;
}

//=============================================================================================================

bool FsSurface::read(const std::filesystem::path& p_sFile, FsSurface& p_Surface, bool p_bLoadCurvature)
{
    p_Surface.clear();

    // The name is <hemi>.<surf>, e.g. lh.white
    const std::string file = utf8FromPath(p_sFile);
    const std::string name = utf8FromPath(p_sFile.filename());
    if (!name.starts_with("lh.") && !name.starts_with("rh."))
        return false;

    Log::info("Reading surface...\n");
    auto geometry = read_geometry(p_sFile);
    if (!geometry) {
        Log::warning("\t{}", geometry.error().what());
        return false;
    }

    p_Surface.m_iHemi = name.starts_with("lh.") ? 0 : 1;
    p_Surface.m_matRR = std::move(geometry->rr);
    p_Surface.m_matTris = std::move(geometry->tris);
    p_Surface.m_matNN = compute_normals(p_Surface.m_matRR, p_Surface.m_matTris);
    p_Surface.m_sFilePath = file.substr(0, file.size() - name.size());
    p_Surface.m_sFileName = name;
    p_Surface.m_sSurf = name.substr(3);

    //Load curvature
    if (p_bLoadCurvature) {
        Log::info("\t");
        p_Surface.m_vecCurv = FsSurface::read_curv(p_sFile.parent_path() / (p_Surface.m_iHemi == 0 ? "lh.curv" : "rh.curv"));
    }

    Log::info("\tRead a surface with {} vertices from {}\n[done]\n", static_cast<int>(p_Surface.m_matRR.rows()), file);

    return true;
}

//=============================================================================================================

VectorXf FsSurface::read_curv(const std::filesystem::path& p_sFileName)
{
    VectorXf curv;

    Log::info("Reading curvature...");
    auto bytes = read_file_bytes(p_sFileName);
    if (!bytes) {
        Log::warning("\tError: Couldn't open the curvature file");
        return curv;
    }
    MemoryByteStream stream(std::move(*bytes));

    qint32 vnum = FsSurface::fread3(stream);
    constexpr qint32 NEW_VERSION_MAGIC_NUMBER = 16777215;

    if (vnum == NEW_VERSION_MAGIC_NUMBER) {
        qint32 fnum = 0;
        qint32 valsPerVertex = 0;
        stream.readBigEndian(vnum);
        stream.readBigEndian(fnum);
        stream.readBigEndian(valsPerVertex);
        if (vnum < 0 || static_cast<std::uint64_t>(vnum) * 4 > stream.size() - stream.pos()) {
            Log::warning("\tError: Truncated curvature file");
            return VectorXf();
        }
        curv.resize(vnum);
        stream.readBigEndian(curv.data(), static_cast<std::size_t>(vnum));
    } else {
        FsSurface::fread3(stream); // number of faces
        if (vnum < 0 || static_cast<std::uint64_t>(vnum) * 2 > stream.size() - stream.pos()) {
            Log::warning("\tError: Truncated curvature file");
            return VectorXf();
        }
        std::vector<qint16> values(static_cast<std::size_t>(vnum));
        stream.readBigEndian(values.data(), values.size());
        curv.resize(vnum);
        for (qint32 i = 0; i < vnum; ++i)
            curv(i) = static_cast<float>(values[static_cast<std::size_t>(i)]) / 100;
    }

    Log::info("[done]\n");

    return curv;
}

//=============================================================================================================

qint32 FsSurface::fread3(ByteStream& stream)
{
    unsigned char bytes[3] = {};
    stream.read(bytes, 3);
    return (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
}

//=============================================================================================================

VectorXi FsSurface::fread3_many(ByteStream& stream, qint32 count)
{
    VectorXi res(count);
    for (qint32 i = 0; i < count; ++i)
        res[i] = FsSurface::fread3(stream);
    return res;
}
