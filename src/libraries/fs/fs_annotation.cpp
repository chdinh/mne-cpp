//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     fs_annotation.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Implementation of @ref FSLIB::FsAnnotation: parses FreeSurfer .annot per-vertex parcellation files and their embedded colortable.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fs_annotation.h"
#include <base/log.h>
#include "fs_label.h"
#include "fs_surface.h"

#include <iostream>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QDebug>
#include <algorithm>
#include <numeric>

#include <base/byte_stream.h>

#include <format>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FSLIB;
using namespace Eigen;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

FsAnnotation::FsAnnotation()
: m_iHemi(-1)
{
}

//=============================================================================================================

FsAnnotation::FsAnnotation(const std::filesystem::path& p_sFileName)
: m_iHemi(-1)
{
    FsAnnotation::read(p_sFileName, *this);
}

//=============================================================================================================

FsAnnotation::FsAnnotation(const std::string& subject_id, qint32 hemi, const std::string& atlas, const std::filesystem::path& subjects_dir)
: m_iHemi(-1)
{
    FsAnnotation::read(subject_id, hemi, atlas, subjects_dir, *this);
}

//=============================================================================================================

FsAnnotation::FsAnnotation(const std::filesystem::path& path, qint32 hemi, const std::string& atlas)
: m_iHemi(-1)
{
    FsAnnotation::read(path, hemi, atlas, *this);
}

//=============================================================================================================

FsAnnotation::~FsAnnotation()
{
}

//=============================================================================================================

void FsAnnotation::clear()
{
    m_sFileName.clear();
    m_sFilePath.clear();
    m_iHemi = -1;
    m_Vertices = VectorXi::Zero(0);
    m_LabelIds = VectorXi::Zero(0);
    m_Colortable.clear();
}

//=============================================================================================================

bool FsAnnotation::read(const std::string& subject_id, qint32 hemi, const std::string& atlas, const std::filesystem::path& subjects_dir, FsAnnotation& p_Annotation)
{
    if (hemi != 0 && hemi != 1)
        return false;

    return read(subjects_dir / BASELIB::pathFromUtf8(subject_id) / "label", hemi, atlas, p_Annotation);
}

//=============================================================================================================

bool FsAnnotation::read(const std::filesystem::path& path, qint32 hemi, const std::string& atlas, FsAnnotation& p_Annotation)
{
    if (hemi != 0 && hemi != 1)
        return false;

    return read(path / BASELIB::pathFromUtf8(std::format("{}.{}.annot", hemi == 0 ? "lh" : "rh", atlas)), p_Annotation);
}

//=============================================================================================================

bool FsAnnotation::read(const std::filesystem::path& p_sFileName, FsAnnotation& p_Annotation)
{
    p_Annotation.clear();

    BASELIB::Log::info("Reading annotation...\n");
    auto bytes = BASELIB::read_file_bytes(p_sFileName);
    if (!bytes) {
        BASELIB::Log::warning("\tError: Couldn't open the file");
        return false;
    }
    BASELIB::MemoryByteStream stream(std::move(*bytes));
    const auto readInt = [&stream]() {
        qint32 value = 0;
        stream.readBigEndian(value);
        return value;
    };
    // A length-prefixed string; FreeSurfer counts the terminating NUL
    const auto readString = [&stream, &readInt]() {
        const qint32 len = readInt();
        std::string text(static_cast<std::size_t>(std::clamp<qint64>(len, 0, static_cast<qint64>(stream.size() - stream.pos()))), '\0');
        stream.read(text.data(), text.size());
        if (!text.empty() && text.back() == '\0')
            text.pop_back();
        return text;
    };
    const auto readColor = [&stream](FsColortable& ct, qint32 row) {
        qint32 rgba[4] = {};
        stream.readBigEndian(rgba, 4);
        for (qint32 j = 0; j < 4; ++j)
            ct.table(row, j) = rgba[j];
        ct.table(row, 4) = rgba[0] + rgba[1] * 256 + rgba[2] * 65536 + rgba[3] * 16777216;
    };

    const qint32 numEl = readInt();
    if (numEl < 0 || static_cast<std::uint64_t>(numEl) * 8 > stream.size() - stream.pos()) {
        BASELIB::Log::warning("\tError: Truncated annotation file");
        return false;
    }
    // Vertex number and label id interleaved
    std::vector<qint32> pairs(2 * static_cast<std::size_t>(numEl));
    stream.readBigEndian(pairs.data(), pairs.size());
    p_Annotation.m_Vertices.resize(numEl);
    p_Annotation.m_LabelIds.resize(numEl);
    for (qint32 i = 0; i < numEl; ++i) {
        p_Annotation.m_Vertices[i] = pairs[2 * static_cast<std::size_t>(i)];
        p_Annotation.m_LabelIds[i] = pairs[2 * static_cast<std::size_t>(i) + 1];
    }

    FsColortable& ct = p_Annotation.m_Colortable;
    if (readInt()) {
        ct.clear();
        qint32 numEntries = readInt();
        if (numEntries > 0) {
            BASELIB::Log::info("\tReading from Original Version\n");
            ct.numEntries = numEntries;
            ct.orig_tab = readString();
            ct.struct_names.assign(static_cast<std::size_t>(numEntries), std::string());
            ct.table = MatrixXi(numEntries, 5);
            for (qint32 i = 0; i < numEntries; ++i) {
                ct.struct_names[static_cast<std::size_t>(i)] = readString();
                readColor(ct, i);
            }
        } else {
            const qint32 version = -numEntries;
            if (version != 2)
                BASELIB::Log::warning("\tError! Does not handle version {}", version);
            else
                BASELIB::Log::info("\tReading from version {}\n", version);

            numEntries = readInt();
            if (numEntries < 0) {
                BASELIB::Log::warning("\tError: Invalid colortable size {}", numEntries);
                return false;
            }
            ct.numEntries = numEntries;
            ct.orig_tab = readString();
            ct.struct_names.assign(static_cast<std::size_t>(numEntries), std::string());
            ct.table = MatrixXi(numEntries, 5);

            const qint32 numEntriesToRead = readInt();
            for (qint32 i = 0; i < numEntriesToRead; ++i) {
                const qint32 structure = readInt();
                if (structure < 0 || structure >= numEntries) {
                    BASELIB::Log::warning("\tError! Read entry, index {}", structure);
                    return false;
                }
                std::string& name = ct.struct_names[static_cast<std::size_t>(structure)];
                if (!name.empty())
                    BASELIB::Log::warning("Error! Duplicate Structure {}", structure);
                name = readString();
                readColor(ct, structure);
            }
        }
        BASELIB::Log::info("\tcolortable with {} entries read\n\t(originally {})\n", ct.numEntries, ct.orig_tab);
    } else {
        BASELIB::Log::warning("\tError! No colortable stored");
    }

    p_Annotation.m_sFileName = BASELIB::utf8FromPath(p_sFileName.filename());
    p_Annotation.m_sFilePath = BASELIB::utf8FromPath(p_sFileName);
    p_Annotation.m_iHemi = p_Annotation.m_sFileName.starts_with("lh.") ? 0 : 1;

    BASELIB::Log::info("[done]\n");

    return true;
}

//=============================================================================================================

bool FsAnnotation::toLabels(const FsSurface& p_surf,
                            std::vector<FsLabel>& p_qListLabels,
                            std::vector<RowVector4i>& p_qListLabelRGBAs,
                            const BASELIB::StringList& lLabelPicks) const
{
    if (this->m_iHemi != p_surf.hemi()) {
        BASELIB::Log::warning("FsAnnotation and surface hemisphere (annot = {}; surf = {}) do not match!\n", this->m_iHemi, p_surf.hemi());
        return false;
    }

    if (m_LabelIds.size() == 0) {
        BASELIB::Log::warning("FsAnnotation doesn't' contain data!\n");
        return false;
    }

    BASELIB::Log::info("Converting labels from annotation...");

    //n_read = 0
    //labels = list()
    //label_colors = list()

    VectorXi label_ids = m_Colortable.getLabelIds();
    const BASELIB::StringList& label_names = m_Colortable.getNames();
    MatrixX4i label_rgbas = m_Colortable.getRGBAs();

    // load the vertex positions from surface
    MatrixX3f vert_pos = p_surf.rr();


    //    std::cout << label_ids;

    const std::size_t firstNew = p_qListLabels.size();
    qint32 label_id, count;
    RowVector4i label_rgba;
    VectorXi vertices;
    VectorXd values;
    MatrixX3f pos;
    std::string name;
    for (qint32 i = 0; i < label_rgbas.rows(); ++i) {
        label_id = label_ids[i];
        label_rgba = label_rgbas.row(i);
        count = 0;
        vertices.resize(m_LabelIds.size());
        //Where
        for (qint32 j = 0; j < m_LabelIds.size(); ++j) {
            if (m_LabelIds[j] == label_id) {
                vertices[count] = j;
                ++count;
            }
        }
        // check if label is part of cortical surface
        if (count == 0)
            continue;
        vertices.conservativeResize(count);

        pos.resize(count, 3);
        for (qint32 j = 0; j < count; ++j)
            pos.row(j) = vert_pos.row(vertices[j]);

        values = VectorXd::Ones(count);
        name = std::format("{}-{}", label_names[static_cast<std::size_t>(i)], this->m_iHemi == 0 ? "lh" : "rh");

        // put it all together
        if (lLabelPicks.empty()) {
            //t_tris
            p_qListLabels.emplace_back(vertices, pos, values, this->m_iHemi, name, label_id);
            // store the color
            p_qListLabelRGBAs.push_back(label_rgba);
        } else if (BASELIB::contains(lLabelPicks, name)) {
            //t_tris
            p_qListLabels.emplace_back(vertices, pos, values, this->m_iHemi, name, label_id);
            // store the color
            p_qListLabelRGBAs.push_back(label_rgba);
        }
    }

    //    for label_id, label_name, label_rgba in
    //            zip(label_ids, label_names, label_rgbas):
    //        vertices = np.where(annot == label_id)[0]
    //        if len(vertices) == 0:
    //            # label is not part of cortical surface
    //            continue
    //        pos = vert_pos[vertices, :]
    //        values = np.zeros(len(vertices))
    //        name = label_name + '-' + hemi
    //        label = FsLabel(vertices, pos, values, hemi, name=name)
    //        labels.append(label)

    //        # store the color
    //        label_rgba = tuple(label_rgba / 255.)
    //        label_colors.append(label_rgba)

    //    n_read = len(labels) - n_read
    //    logger.info('   read %d labels from %s' % (n_read, fname))

    //# sort the labels and colors by label name
    //names = [label.name for label in labels]
    //labels, label_colors = zip(*((label, color) for (name, label, color)
    //                           in sorted(zip(names, labels, label_colors))))
    //# convert tuples to lists
    //labels = list(labels)
    //label_colors = list(label_colors)

    // mne.read_labels_from_annot sorts the labels by name; sort an index, then apply it by moving
    std::vector<std::size_t> order(p_qListLabels.size() - firstNew);
    std::iota(order.begin(), order.end(), firstNew);
    std::ranges::stable_sort(order, {}, [&p_qListLabels](std::size_t k) -> const std::string& { return p_qListLabels[k].name; });
    std::vector<FsLabel> labels;
    std::vector<RowVector4i> rgbas;
    labels.reserve(order.size());
    rgbas.reserve(order.size());
    for (const std::size_t k : order) {
        labels.push_back(std::move(p_qListLabels[k]));
        rgbas.push_back(p_qListLabelRGBAs[k]);
    }
    std::ranges::move(labels, p_qListLabels.begin() + static_cast<std::ptrdiff_t>(firstNew));
    std::ranges::copy(rgbas, p_qListLabelRGBAs.begin() + static_cast<std::ptrdiff_t>(firstNew));

    BASELIB::Log::info("[done]\n");

    return true;
}
