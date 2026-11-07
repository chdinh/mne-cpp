//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2013-2026 MNE-CPP Authors
 *
 * @file     fiff_evoked_set.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Florian Schlembach <fschlembach@web.de>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
 *           Andreas Griesshammer <ag@fieldlineinc.com>;
 *           Gabriel Motta <gabrielbenmotta@gmail.com>
 * @since    0.1.0
 * @date     March 2013
 * @brief    Implementation of @ref FiffEvokedSet: read / write of multi-condition ``*-ave.fif'' files and the ave-description parser.
 *
 * Owns one @ref FiffInfo and one @ref FiffEvoked per @c FIFFB_EVOKED
 * block. The ave-description structs (@ref RejectionParams,
 * @ref AverageCategory, @ref AverageDescription) feed the batch
 * averaging path.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fiff_evoked_set.h"

#include <utils/generics/log.h>
#include <utils/string_utils.h>
#include "fiff_events.h"
#include "fiff_raw_data.h"
#include "fiff_tag.h"
#include "fiff_dir_node.h"
#include "fiff_evoked_file.h"
#include "fiff_stream.h"
#include "fiff_file.h"
#include "fiff_constants.h"

//=============================================================================================================
// EIGEN INCLUDES
//=============================================================================================================

#include <Eigen/SparseCore>

#include <cmath>
#include <numeric>
#include <vector>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QFile>
#include <QDebug>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FIFFLIB;
using namespace Eigen;
using namespace UTILSLIB;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

FiffEvokedSet::FiffEvokedSet()
{
    qRegisterMetaType<FIFFLIB::FiffEvokedSet>("FIFFLIB::FiffEvokedSet");
    qRegisterMetaType<FIFFLIB::FiffEvokedSet::SPtr>("FIFFLIB::FiffEvokedSet::SPtr");
}

//=============================================================================================================

FiffEvokedSet::FiffEvokedSet(const std::filesystem::path& path,
                             const std::vector<FiffEvoked::Condition>& conditions,
                             const FiffEvoked::Baseline& baseline,
                             bool proj)
: FiffEvokedSet(read(path, conditions, baseline, proj).value())
{
}

//=============================================================================================================

FiffEvokedSet::FiffEvokedSet(const FiffEvokedSet& p_FiffEvokedSet)
: info(p_FiffEvokedSet.info)
, evoked(p_FiffEvokedSet.evoked)
{
}

//=============================================================================================================

FiffEvokedSet::~FiffEvokedSet()
{
}

//=============================================================================================================

void FiffEvokedSet::clear()
{
    info.clear();
    evoked.clear();
}

//=============================================================================================================

FiffEvokedSet FiffEvokedSet::pick_channels(const StringList& include,
                                           const StringList& exclude) const
{
    FiffEvokedSet res;

    //
    //   Update info to match the channel selection
    //
    RowVectorXi sel = FiffInfo::pick_channels(this->info.ch_names, include, exclude);
    if (sel.cols() > 0) {
        res.info = this->info.pick_info(sel);
    } else {
        res.info = this->info;
    }

    QList<FiffEvoked>::ConstIterator ev;
    for (ev = evoked.begin(); ev != evoked.end(); ++ev)
        res.evoked.push_back(ev->pick_channels(include, exclude));

    return res;
}

//=============================================================================================================

bool FiffEvokedSet::compensate_to(fiff_int_t to)
{
    const qint32 now = info.get_current_comp();

    if (now == to) {
        qInfo("Data is already compensated as desired.\n");
        return false;
    }

    FiffCtfComp ctf_comp;
    if (!info.make_compensator(now, to, ctf_comp)) {
        qWarning("[FiffEvokedSet::compensate_to] Cannot change compensation from grade %d to %d.", now, to);
        return false;
    }

    for (FiffEvoked& response : evoked) {
        response.data = ctf_comp.data->data * response.data;
        response.info.set_current_comp(to);
    }
    info.set_current_comp(to);

    return true;
}

//=============================================================================================================

bool FiffEvokedSet::find_evoked(const FiffEvokedSet& p_FiffEvokedSet) const
{
    if (!p_FiffEvokedSet.evoked.size()) {
        qWarning("No evoked response data sets in %s\n", p_FiffEvokedSet.info.filename.c_str());
        return false;
    } else
        qInfo("\nFound %lld evoked response data sets in %s :\n", static_cast<long long>(p_FiffEvokedSet.evoked.size()), p_FiffEvokedSet.info.filename.c_str());

    for (qint32 i = 0; i < p_FiffEvokedSet.evoked.size(); ++i) {
        qInfo("%s (%s)\n", p_FiffEvokedSet.evoked.at(i).comment.toUtf8().constData(), p_FiffEvokedSet.evoked.at(i).aspectKindToString().toUtf8().constBegin());
    }

    return true;
}

//=============================================================================================================

Expected<FiffEvokedSet> FiffEvokedSet::read(const std::filesystem::path& path,
                                            const std::vector<FiffEvoked::Condition>& conditions,
                                            const FiffEvoked::Baseline& baseline,
                                            bool proj)
{
    auto file = FiffEvokedFile::open(path);
    if (!file) {
        return std::unexpected(file.error());
    }
    std::vector<int> sets;
    for (const FiffEvoked::Condition& condition : conditions) {
        const auto found = file->find(condition, FIFFV_ASPECT_AVERAGE);
        if (!found) {
            return std::unexpected(found.error());
        }
        sets.push_back(*found);
    }
    if (conditions.empty()) {
        sets.resize(static_cast<std::size_t>(file->size()));
        std::iota(sets.begin(), sets.end(), 0);
    }

    FiffEvokedSet set;
    set.info = file->info;
    for (const int i : sets) {
        auto evoked = file->read(i, baseline, proj);
        if (!evoked) {
            return std::unexpected(evoked.error());
        }
        set.evoked.push_back(std::move(*evoked));
    }
    return set;
}

//=============================================================================================================

Expected<void> FiffEvokedSet::save(const std::filesystem::path& path) const
{
    auto pStream = path.empty() ? nullptr : FiffStream::start_file(path);
    if (!pStream) {
        return fail(ErrorCode::Io, "Cannot write the evoked data to '{}'", utf8FromPath(path));
    }

    pStream->write_evoked_set(*this);
    pStream->end_file();
    pStream->close();
    return {};
}

//=============================================================================================================

FiffEvokedSet FiffEvokedSet::computeGrandAverage(const QList<FiffEvokedSet>& evokedSets)
{
    FiffEvokedSet grandAvg;

    if (evokedSets.isEmpty()) {
        qWarning() << "[FiffEvokedSet::computeGrandAverage] No evoked sets provided.";
        return grandAvg;
    }

    int nCat = evokedSets.first().evoked.size();
    for (const FiffEvokedSet& eset : evokedSets) {
        nCat = qMin(nCat, static_cast<int>(eset.evoked.size()));
    }

    for (int j = 0; j < nCat; ++j) {
        // Drop every channel marked bad in any data set, then keep the common channels.
        StringList bads;
        for (const FiffEvokedSet& eset : evokedSets) {
            for (const std::string& bad : eset.evoked[j].info.bads) {
                if (!UTILSLIB::contains(bads, bad)) {
                    bads.push_back(bad);
                }
            }
        }
        StringList common;
        for (const std::string& name : evokedSets.first().evoked[j].info.ch_names) {
            bool inAll = !UTILSLIB::contains(bads, name);
            for (const FiffEvokedSet& eset : evokedSets) {
                inAll = inAll && UTILSLIB::contains(eset.evoked[j].info.ch_names, name);
            }
            if (inAll) {
                common.push_back(name);
            }
        }

        QList<FiffEvoked> picked;
        for (const FiffEvokedSet& eset : evokedSets) {
            picked.append(eset.evoked[j].pick_channels(common));
        }
        FiffEvoked avg = FiffEvoked::combine(picked, FiffEvoked::CombineWeights::Equal);
        if (avg.isEmpty()) {
            continue;
        }
        avg.nave = static_cast<fiff_int_t>(evokedSets.size());
        avg.comment = QString("Grand average (n = %1)").arg(avg.nave);
        grandAvg.info = avg.info;
        grandAvg.evoked.append(avg);
    }

    return grandAvg;
}

//=============================================================================================================

void FiffEvokedSet::subtractBaseline(Eigen::MatrixXd& epoch, int bminSamp, int bmaxSamp)
{
    if (bminSamp < 0)
        bminSamp = 0;
    if (bmaxSamp >= epoch.cols())
        bmaxSamp = static_cast<int>(epoch.cols()) - 1;
    if (bminSamp >= bmaxSamp)
        return;

    int nBase = bmaxSamp - bminSamp + 1;
    for (int c = 0; c < epoch.rows(); ++c) {
        double baseVal = epoch.row(c).segment(bminSamp, nBase).mean();
        epoch.row(c).array() -= baseVal;
    }
}

//=============================================================================================================

FiffEvokedSet FiffEvokedSet::computeAverages(const FiffRawData& raw,
                                             const AverageDescription& desc,
                                             const MatrixXi& events,
                                             QString& log)
{
    FiffEvokedSet evokedSet;
    evokedSet.info = raw.info;

    float sfreq = raw.info.sfreq;
    int nchan = raw.info.nchan;

    log.clear();
    log += QString("Averaging: %1\n").arg(desc.comment);

    // Process each category
    for (int j = 0; j < desc.categories.size(); ++j) {
        const AverageCategory& cat = desc.categories[j];

        // Compute sample indices
        int minSamp = static_cast<int>(std::round(cat.tmin * sfreq));
        int maxSamp = static_cast<int>(std::round(cat.tmax * sfreq));
        int ns = maxSamp - minSamp + 1;
        int delaySamp = static_cast<int>(std::round(cat.delay * sfreq));

        // Baseline sample range (relative to epoch start)
        int bminSamp = 0, bmaxSamp = 0;
        if (cat.doBaseline) {
            bminSamp = static_cast<int>(std::round(cat.bmin * sfreq)) - minSamp;
            bmaxSamp = static_cast<int>(std::round(cat.bmax * sfreq)) - minSamp;
        }

        // Accumulator
        MatrixXd sumData = MatrixXd::Zero(nchan, ns);
        MatrixXd sumSqData = MatrixXd::Zero(nchan, ns);
        int nave = 0;

        log += QString("\n  Category: %1\n").arg(cat.comment);
        log += QString("    t = %1 ... %2 ms\n").arg(1000.0 * cat.tmin, 0, 'f', 1).arg(1000.0 * cat.tmax, 0, 'f', 1);

        // Iterate over events
        for (int k = 0; k < events.rows(); ++k) {
            if (!FiffEvents::match_event(cat, events, k))
                continue;

            int evSample = events(k, 0);
            int epochStart = evSample + delaySamp + minSamp;
            int epochEnd = evSample + delaySamp + maxSamp;

            // Check bounds
            if (epochStart < raw.first_samp || epochEnd > raw.last_samp)
                continue;

            // Read epoch
            MatrixXd epochData;
            MatrixXd epochTimes;
            if (!raw.read_raw_segment(epochData, epochTimes, epochStart, epochEnd)) {
                log += QString("    Error reading epoch at sample %1\n").arg(evSample);
                continue;
            }

            // Artifact rejection
            QString rejReason;
            if (!checkArtifacts(epochData, raw.info, raw.info.bads, desc.rej, rejReason)) {
                log += QString("    %1 %2 %3 %4 [%5] %6 [omit]\n")
                           .arg(evSample, 7)
                           .arg(static_cast<float>(evSample) / sfreq, -10, 'f', 3)
                           .arg(events(k, 1), 3)
                           .arg(events(k, 2), 3)
                           .arg(cat.comment)
                           .arg(rejReason);
                continue;
            }

            // Baseline correction
            if (cat.doBaseline) {
                subtractBaseline(epochData, bminSamp, bmaxSamp);
            }

            // Absolute value
            if (cat.doAbs) {
                epochData = epochData.cwiseAbs();
            }

            // Accumulate
            sumData += epochData;
            if (cat.doStdErr) {
                sumSqData += epochData.cwiseProduct(epochData);
            }
            nave++;

            log += QString("    %1 %2 %3 %4 [%5]\n")
                       .arg(evSample, 7)
                       .arg(static_cast<float>(evSample) / sfreq, -10, 'f', 3)
                       .arg(events(k, 1), 3)
                       .arg(events(k, 2), 3)
                       .arg(cat.comment);
        }

        // Compute average
        FiffEvoked evoked;
        evoked.comment = cat.comment;
        evoked.first = minSamp;
        evoked.last = maxSamp;
        evoked.nave = nave;

        // Build times vector
        RowVectorXf times(ns);
        for (int s = 0; s < ns; ++s)
            times(s) = static_cast<float>(minSamp + s) / sfreq;
        evoked.times = times;

        if (nave > 0) {
            evoked.data = sumData / static_cast<double>(nave);
        } else {
            evoked.data = MatrixXd::Zero(nchan, ns);
        }

        evoked.info = raw.info;

        evokedSet.evoked.append(evoked);
        log += QString("    nave = %1\n").arg(nave);
    }

    return evokedSet;
}

//=============================================================================================================

bool FiffEvokedSet::checkArtifacts(const MatrixXd& epoch,
                                   const FiffInfo& info,
                                   const StringList& bads,
                                   const RejectionParams& rej,
                                   QString& reason)
{
    for (int c = 0; c < epoch.rows(); ++c) {
        // Skip bad channels
        if (UTILSLIB::contains(bads, info.ch_names[c]))
            continue;

        double minVal = epoch.row(c).minCoeff();
        double maxVal = epoch.row(c).maxCoeff();
        double pp = maxVal - minVal;

        int chKind = info.chs[c].kind;
        int chUnit = info.chs[c].unit;

        if (chKind == FIFFV_MEG_CH) {
            if (chUnit == FIFF_UNIT_T) {
                // Magnetometer
                if (rej.megMagReject > 0 && pp > rej.megMagReject) {
                    reason = QString("%1 : %2 fT > %3 fT")
                                 .arg(info.ch_names[c])
                                 .arg(pp * 1e15, 0, 'f', 1)
                                 .arg(rej.megMagReject * 1e15, 0, 'f', 1);
                    return false;
                }
                if (rej.megMagFlat > 0 && pp < rej.megMagFlat) {
                    reason = QString("%1 : %2 fT < %3 fT (flat)")
                                 .arg(info.ch_names[c])
                                 .arg(pp * 1e15, 0, 'f', 1)
                                 .arg(rej.megMagFlat * 1e15, 0, 'f', 1);
                    return false;
                }
            } else {
                // Gradiometer
                if (rej.megGradReject > 0 && pp > rej.megGradReject) {
                    reason = QString("%1 : %2 fT/cm > %3 fT/cm")
                                 .arg(info.ch_names[c])
                                 .arg(pp * 1e13, 0, 'f', 1)
                                 .arg(rej.megGradReject * 1e13, 0, 'f', 1);
                    return false;
                }
                if (rej.megGradFlat > 0 && pp < rej.megGradFlat) {
                    reason = QString("%1 : %2 fT/cm < %3 fT/cm (flat)")
                                 .arg(info.ch_names[c])
                                 .arg(pp * 1e13, 0, 'f', 1)
                                 .arg(rej.megGradFlat * 1e13, 0, 'f', 1);
                    return false;
                }
            }
        } else if (chKind == FIFFV_EEG_CH) {
            if (rej.eegReject > 0 && pp > rej.eegReject) {
                reason = QString("%1 : %2 uV > %3 uV")
                             .arg(info.ch_names[c])
                             .arg(pp * 1e6, 0, 'f', 1)
                             .arg(rej.eegReject * 1e6, 0, 'f', 1);
                return false;
            }
            if (rej.eegFlat > 0 && pp < rej.eegFlat) {
                reason = QString("%1 : %2 uV < %3 uV (flat)")
                             .arg(info.ch_names[c])
                             .arg(pp * 1e6, 0, 'f', 1)
                             .arg(rej.eegFlat * 1e6, 0, 'f', 1);
                return false;
            }
        } else if (chKind == FIFFV_EOG_CH) {
            if (rej.eogReject > 0 && pp > rej.eogReject) {
                reason = QString("%1 : %2 uV > %3 uV (EOG)")
                             .arg(info.ch_names[c])
                             .arg(pp * 1e6, 0, 'f', 1)
                             .arg(rej.eogReject * 1e6, 0, 'f', 1);
                return false;
            }
            if (rej.eogFlat > 0 && pp < rej.eogFlat) {
                reason = QString("%1 : EOG flat").arg(info.ch_names[c]);
                return false;
            }
        } else if (chKind == FIFFV_ECG_CH) {
            if (rej.ecgReject > 0 && pp > rej.ecgReject) {
                reason = QString("%1 : %2 mV > %3 mV (ECG)")
                             .arg(info.ch_names[c])
                             .arg(pp * 1e3, 0, 'f', 2)
                             .arg(rej.ecgReject * 1e3, 0, 'f', 2);
                return false;
            }
            if (rej.ecgFlat > 0 && pp < rej.ecgFlat) {
                reason = QString("%1 : ECG flat").arg(info.ch_names[c]);
                return false;
            }
        }
    }
    return true;
}
