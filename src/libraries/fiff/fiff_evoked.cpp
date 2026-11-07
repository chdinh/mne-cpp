//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2013-2026 MNE-CPP Authors
 *
 * @file     fiff_evoked.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Florian Schlembach <fschlembach@web.de>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
 *           Andreas Griesshammer <ag@fieldlineinc.com>;
 *           Gabriel Motta <gabrielbenmotta@gmail.com>
 * @since    0.1.0
 * @date     March 2013
 * @brief    Implementation of @ref FiffEvoked: read / write of one FIFFB_EVOKED block plus baseline / projector / pick operations.
 *
 * Drop-in counterpart of @c mne.Evoked in MNE-Python.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fiff_evoked.h"

#include <utils/generics/log.h>
#include <utils/string_utils.h>
#include "fiff_stream.h"
#include "fiff_tag.h"
#include "fiff_dir_node.h"

#include <math/numerics.h>
#include <QDebug>

#include <cmath>
#include <format>
//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FIFFLIB;
using namespace UTILSLIB;
using namespace Eigen;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

FiffEvoked::FiffEvoked()
: nave(-1)
, aspect_kind(-1)
, first(-1)
, last(-1)
, baseline(qMakePair(-1.0f, -1.0f))
{
}

//=============================================================================================================

FiffEvoked::FiffEvoked(const std::filesystem::path& path,
                       const Condition& condition,
                       const Baseline& baseline,
                       bool proj,
                       fiff_int_t kind)
: FiffEvoked(read(path, condition, baseline, proj, kind).value())
{
}

//=============================================================================================================

FiffEvoked::FiffEvoked(const FiffEvoked& p_FiffEvoked)
: info(p_FiffEvoked.info)
, nave(p_FiffEvoked.nave)
, aspect_kind(p_FiffEvoked.aspect_kind)
, first(p_FiffEvoked.first)
, last(p_FiffEvoked.last)
, comment(p_FiffEvoked.comment)
, times(p_FiffEvoked.times)
, data(p_FiffEvoked.data)
, proj(p_FiffEvoked.proj)
, baseline(p_FiffEvoked.baseline)
{
}

//=============================================================================================================

FiffEvoked::~FiffEvoked()
{
}

//=============================================================================================================

void FiffEvoked::clear()
{
    info.clear();
    nave = -1;
    aspect_kind = -1;
    first = -1;
    last = -1;
    comment = QString("");
    times = RowVectorXf();
    data = MatrixXd();
    proj = MatrixXd();
}

//=============================================================================================================

FiffEvoked FiffEvoked::pick_channels(const StringList& include,
                                     const StringList& exclude) const
{
    if (include.size() == 0 && exclude.size() == 0)
        return FiffEvoked(*this);

    RowVectorXi sel = FiffInfo::pick_channels(this->info.ch_names, include, exclude);
    if (sel.cols() == 0) {
        qWarning("Warning : No channels match the selection.\n");
        return FiffEvoked(*this);
    }

    FiffEvoked res(*this);
    //
    //   Modify the measurement info
    //
    res.info = FiffInfo(res.info.pick_info(sel));
    //
    //   Create the reduced data set
    //
    MatrixXd selBlock = MatrixXd::Zero(sel.cols(), res.data.cols());
    for (qint32 l = 0; l < sel.cols(); ++l) {
        if (sel(0, l) < res.data.rows()) {
            selBlock.block(l, 0, 1, selBlock.cols()) = res.data.block(sel(0, l), 0, 1, selBlock.cols());
        } else {
            qWarning("FiffEvoked::pick_channels - Warning : Selected channel index out of bound.\n");
        }
    }
    res.data.resize(sel.cols(), res.data.cols());
    res.data = selBlock;

    return res;
}

//=============================================================================================================

Expected<FiffEvoked> FiffEvoked::read(const std::filesystem::path& path,
                                      const Condition& condition,
                                      const Baseline& baseline,
                                      bool proj,
                                      fiff_int_t kind)
{
    auto t_pStream = FiffStream::open(path);
    if (!t_pStream) {
        return std::unexpected(t_pStream.error());
    }
    const std::string file = utf8FromPath(path);
    QString t_sFileName = t_pStream->streamName();

    qInfo("Reading %s ...\n", t_sFileName.toUtf8().constData());
    //
    //   Read the measurement info
    //
    FiffInfo info;
    FiffDirNode::SPtr meas;
    if (t_pStream->dirtree()->dir_tree_find(FIFFB_MEAS).isEmpty()) {
        return fail(ErrorCode::NotFound, "No measurement in {}", file);
    }
    if (!t_pStream->read_meas_info(t_pStream->dirtree(), info, meas)) {
        return fail(ErrorCode::Format, "No measurement info in {}", file);
    }
    info.filename = t_sFileName.toStdString(); //move fname storage to read_meas_info member function
    //
    //   Locate the data of interest
    //
    if (meas->dir_tree_find(FIFFB_PROCESSED_DATA).isEmpty()) {
        return fail(ErrorCode::NotFound, "No processed data in {}", file);
    }
    QList<FiffDirNode::SPtr> evoked_node = meas->dir_tree_find(FIFFB_EVOKED);
    if (evoked_node.isEmpty()) {
        return fail(ErrorCode::NotFound, "No evoked data in {}", file);
    }

    int setno = -1;
    if (const int* index = std::get_if<int>(&condition)) {
        setno = *index;
    } else {
        const QString wanted = QString::fromStdString(std::get<std::string>(condition));
        QStringList comments;
        QList<fiff_int_t> aspect_kinds;
        QString t;
        t_pStream->get_evoked_entries(evoked_node, comments, aspect_kinds, t);
        setno = static_cast<int>(comments.size());
        for (qsizetype i = 0; i < comments.size(); ++i) {
            if (comments[i] == wanted && aspect_kinds[i] == kind) {
                setno = static_cast<int>(i);
                break;
            }
        }
    }
    if (setno < 0 || setno >= evoked_node.size()) {
        return fail(ErrorCode::NotFound, "No evoked data set {} of kind {} in {}",
                    std::visit([](const auto& c) { return std::format("{}", c); }, condition), kind, file);
    }

    FiffEvoked p_FiffEvoked;
    FiffDirNode::SPtr my_evoked = evoked_node[setno];

    //
    //   Identify the aspects
    //
    QList<FiffDirNode::SPtr> aspects = my_evoked->dir_tree_find(FIFFB_ASPECT);

    if (aspects.size() > 1)
        qInfo("\tMultiple (%lld) aspects found. Taking first one.\n", static_cast<long long>(aspects.size()));

    FiffDirNode::SPtr my_aspect = aspects[0];

    //
    //   Now find the data in the evoked block
    //
    fiff_int_t nchan = 0;
    float sfreq = -1.0f;
    QList<FiffChInfo> chs;
    fiff_int_t tag_kind, pos, first = 0, last = 0;
    FiffTag::UPtr t_pTag;
    QString comment("");
    qint32 k;
    for (k = 0; k < my_evoked->nent(); ++k) {
        tag_kind = my_evoked->dir[k]->kind;
        pos = my_evoked->dir[k]->pos;
        switch (tag_kind) {
            case FIFF_COMMENT:
                t_pStream->read_tag(t_pTag, pos);
                comment = QString::fromStdString(t_pTag->toString());
                break;
            case FIFF_FIRST_SAMPLE:
                t_pStream->read_tag(t_pTag, pos);
                first = *t_pTag->toInt();
                break;
            case FIFF_LAST_SAMPLE:
                t_pStream->read_tag(t_pTag, pos);
                last = *t_pTag->toInt();
                break;
            case FIFF_NCHAN:
                t_pStream->read_tag(t_pTag, pos);
                nchan = *t_pTag->toInt();
                break;
            case FIFF_SFREQ:
                t_pStream->read_tag(t_pTag, pos);
                sfreq = *t_pTag->toFloat();
                break;
            case FIFF_CH_INFO:
                t_pStream->read_tag(t_pTag, pos);
                chs.append(t_pTag->toChInfo());
                break;
        }
    }
    if (comment.isEmpty())
        comment = QString("No comment");

    //
    //   Local channel information?
    //
    if (nchan > 0) {
        if (chs.size() == 0) {
            return fail(ErrorCode::Format, "Local channel information missing in {}", file);
        }
        if (chs.size() != nchan) {
            return fail(ErrorCode::Format, "{} channels but {} channel definitions in {}", nchan, chs.size(), file);
        }
        info.chs = chs;
        info.nchan = nchan;
        qInfo("\tFound channel information in evoked data. nchan = %d\n", nchan);
        if (sfreq > 0.0f)
            info.sfreq = sfreq;
    }
    qint32 nsamp = last - first + 1;
    qInfo("\tFound the data of interest:\n");
    qInfo("\t\tt = %10.2f ... %10.2f ms (%s)\n", 1000 * static_cast<float>(first) / info.sfreq, 1000 * static_cast<float>(last) / info.sfreq, comment.toUtf8().constData());
    if (info.comps.size() > 0)
        qInfo("\t\t%lld CTF compensation matrices available\n", static_cast<long long>(info.comps.size()));

    //
    // Read the data in the aspect block
    //
    fiff_int_t aspect_kind = -1;
    fiff_int_t nave = -1;
    QList<FiffTag> epoch;
    for (k = 0; k < my_aspect->nent(); ++k) {
        tag_kind = my_aspect->dir[k]->kind;
        pos = my_aspect->dir[k]->pos;

        switch (tag_kind) {
            case FIFF_COMMENT:
                t_pStream->read_tag(t_pTag, pos);
                comment = QString::fromStdString(t_pTag->toString());
                break;
            case FIFF_ASPECT_KIND:
                t_pStream->read_tag(t_pTag, pos);
                aspect_kind = *t_pTag->toInt();
                break;
            case FIFF_NAVE:
                t_pStream->read_tag(t_pTag, pos);
                nave = *t_pTag->toInt();
                break;
            case FIFF_EPOCH:
                t_pStream->read_tag(t_pTag, pos);
                epoch.append(FiffTag(*t_pTag));
                break;
        }
    }
    if (nave == -1)
        nave = 1;
    qInfo("\t\tnave = %d - aspect type = %d\n", nave, aspect_kind);

    qint32 nepoch = epoch.size();
    if (nepoch != 1 && nepoch != info.nchan) {
        return fail(ErrorCode::Format, "{} epoch tags for {} channels in {}", nepoch, info.nchan, file);
    }
    MatrixXd all_data;
    if (nepoch == 1) {
        //
        //   Only one epoch
        //
        all_data = epoch[0].toFloatMatrix().cast<double>();
        all_data.transposeInPlace();
        //
        //   May need a transpose if the number of channels is one
        //
        if (all_data.cols() == 1 && info.nchan == 1)
            all_data.transposeInPlace();
    } else {
        //
        //   Put the old style epochs together: one channel per tag
        //
        all_data.resize(nepoch, epoch[0].size() / static_cast<int>(sizeof(float)));
        for (k = 0; k < nepoch; ++k) {
            const int n = epoch[k].size() / static_cast<int>(sizeof(float));
            if (n != all_data.cols()) {
                return fail(ErrorCode::Format, "Old style epochs of lengths {} and {} in {}", n, all_data.cols(), file);
            }
            all_data.row(k) = Map<const RowVectorXf>(epoch[k].toFloat(), n).cast<double>();
        }
    }
    if (all_data.cols() != nsamp) {
        return fail(ErrorCode::Format, "{} samples instead of {} in {}", all_data.cols(), nsamp, file);
    }

    //
    //   Calibrate
    //
    qInfo("\n\tPreprocessing...\n");
    qInfo("\t%d channels remain after picking\n", info.nchan);

    using T = Eigen::Triplet<double>;
    std::vector<T> tripletList;
    tripletList.reserve(info.nchan);
    for (k = 0; k < info.nchan; ++k)
        tripletList.push_back(T(k, k, info.chs[k].cal));
    SparseMatrix<double> cals(info.nchan, info.nchan);
    cals.setFromTriplets(tripletList.begin(), tripletList.end());

    all_data = cals * all_data;

    RowVectorXf times = RowVectorXf(last - first + 1);
    for (k = 0; k < times.size(); ++k)
        times[k] = static_cast<float>(first + k) / info.sfreq;

    //
    // Set up projection
    //
    if (info.projs.size() == 0 || !proj) {
        qInfo("\tNo projector specified for these data.\n");
        p_FiffEvoked.proj = MatrixXd();
    } else {
        //   Create the projector
        MatrixXd projection;
        qint32 nproj = info.make_projector(projection);
        if (nproj == 0) {
            qWarning("\tThe projection vectors do not apply to these channels\n");
            p_FiffEvoked.proj = MatrixXd();
        } else {
            qInfo("\tCreated an SSP operator (subspace dimension = %d)\n", nproj);
            p_FiffEvoked.proj = projection;
        }

        //   The projection items have been activated
        FiffProj::activate_projs(info.projs);
    }

    if (p_FiffEvoked.proj.rows() > 0) {
        all_data = p_FiffEvoked.proj * all_data;
        qInfo("\tSSP projectors applied to the evoked data\n");
    }

    // Put it all together
    p_FiffEvoked.info = info;
    p_FiffEvoked.nave = nave;
    p_FiffEvoked.aspect_kind = aspect_kind;
    p_FiffEvoked.first = first;
    p_FiffEvoked.last = last;
    p_FiffEvoked.comment = comment;
    p_FiffEvoked.times = times;
    p_FiffEvoked.data = all_data;

    if (baseline && baseline->first != baseline->second) {
        QPair<float, float> window(baseline->first, baseline->second);
        p_FiffEvoked.applyBaselineCorrection(window);
    } else {
        qInfo("\tNo baseline correction applied\n");
    }

    return p_FiffEvoked;
}

//=============================================================================================================

void FiffEvoked::setInfo(const FiffInfo& p_info,
                         bool applyProj)
{
    info = p_info;
    //
    // Set up projection
    //
    if (info.projs.size() == 0 || !applyProj) {
        qInfo("\tNo projector specified for these data.\n");
        this->proj = MatrixXd();
    } else {
        //   Create the projector
        MatrixXd projection;
        qint32 nproj = info.make_projector(projection);
        if (nproj == 0) {
            qWarning("\tThe projection vectors do not apply to these channels\n");
            this->proj = MatrixXd();
        } else {
            qInfo("\tCreated an SSP operator (subspace dimension = %d)\n", nproj);
            this->proj = projection;
        }

        //   The projection items have been activated
        FiffProj::activate_projs(info.projs);
    }
}

//=============================================================================================================

FiffEvoked& FiffEvoked::operator+=(const MatrixXd& newData)
{
    //Init matrix if necessary
    if (nave == -1 || nave == 0) {
        data = MatrixXd::Zero(newData.rows(), newData.cols());
    }

    if (data.cols() == newData.cols() && data.rows() == newData.rows()) {
        //Revert old averaging
        data = data * nave;

        //Do new averaging
        data += newData;
        if (nave <= 0) {
            nave = 1;
        } else {
            nave++;
        }

        data /= nave;
    }

    return *this;
}

//=============================================================================================================

FiffEvoked FiffEvoked::combine(const QList<FiffEvoked>& evokeds, CombineWeights weights)
{
    VectorXd w(evokeds.size());
    double naveSum = 0.0;
    for (const FiffEvoked& e : evokeds) {
        naveSum += e.nave;
    }
    for (int i = 0; i < evokeds.size(); ++i) {
        w(i) = weights == CombineWeights::Nave ? evokeds[i].nave / naveSum
                                               : 1.0 / static_cast<double>(evokeds.size());
    }
    return combine(evokeds, w);
}

//=============================================================================================================

FiffEvoked FiffEvoked::combine(const QList<FiffEvoked>& evokeds, const VectorXd& weights)
{
    if (evokeds.isEmpty() || weights.size() != evokeds.size()) {
        qWarning() << "[FiffEvoked::combine] weights must be the same size as the evoked list.";
        return FiffEvoked();
    }
    const FiffEvoked& ref = evokeds.first();
    for (const FiffEvoked& e : evokeds) {
        if (e.info.ch_names != ref.info.ch_names || e.times.size() != ref.times.size() || (e.times - ref.times).cwiseAbs().maxCoeff() > 1e-6f) {
            qWarning() << "[FiffEvoked::combine] All evoked data must have the same channels and times.";
            return FiffEvoked();
        }
    }

    FiffEvoked res(ref);
    res.data = MatrixXd::Zero(ref.data.rows(), ref.data.cols());
    double invNave = 0.0;
    QString comment;
    for (int i = 0; i < evokeds.size(); ++i) {
        const FiffEvoked& e = evokeds[i];
        const double w = weights(i);
        res.data += w * e.data;
        invNave += w * w / e.nave;
        for (const std::string& bad : e.info.bads) {
            if (!UTILSLIB::contains(res.info.bads, bad)) {
                res.info.bads.push_back(bad);
            }
        }

        const QString sign = w >= 0.0 ? QString() : QString("-");
        const QString weight = qAbs(qAbs(w) - 1.0) <= 1e-8 + 1e-5 ? QString()
                                                                  : QString::number(qAbs(w), 'f', 3);
        const QString multiplier = weight.isEmpty() ? QString() : QString::fromUtf8(" × ");
        const QString thisComment = e.comment.contains(" + ") ? QString("(%1)").arg(e.comment)
            : e.comment.isEmpty()                             ? QString("unknown")
                                                              : e.comment;
        if (i == 0) {
            comment += sign + weight + multiplier + thisComment;
        } else {
            comment += QString(" %1 ").arg(sign.isEmpty() ? QString("+") : sign) + weight + multiplier + thisComment;
        }
    }
    res.comment = comment.replace(" - - ", " + ");
    res.nave = static_cast<fiff_int_t>(std::lround(1.0 / invNave));
    return res;
}

//=============================================================================================================

void FiffEvoked::applyBaselineCorrection(QPair<float, float>& p_baseline)
{
    // Skip baseline correction if sentinel value (-1, -1) or equal bounds are passed
    if (p_baseline.first == p_baseline.second) {
        qInfo("\tNo baseline correction applied\n");
        return;
    }

    // Run baseline correction
    qInfo("Applying baseline correction ... (mode: mean)\n");
    this->data = Numerics::rescale(this->data, this->times, p_baseline, "mean");
    this->baseline = p_baseline;
}
