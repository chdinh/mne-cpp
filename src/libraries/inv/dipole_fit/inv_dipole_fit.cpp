//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     inv_dipole_fit.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Implementation of the sequential ECD-fit driver.
 *
 * Implements the time-bin loop that calls into the Nelder-Mead /
 * guess-grid optimiser, reads averaged or raw data through @c MNELIB
 * and assembles the per-time-point @ref InvEcd records into the output
 * @ref InvEcdSet. Refactored from @c fit_dipoles.c / @c fit_dipoles_raw
 * in the MNE-C reference implementation.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "inv_dipole_fit.h"
#include <base/log.h>
#include <base/string_utils.h>
#include <mne/mne_meas_data_set.h>
#include "inv_guess_data.h"

#include <memory>
#include <vector>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace INVLIB;
using namespace MNELIB;
using namespace FWDLIB;

//=============================================================================================================
// CONSTANTS
//=============================================================================================================

static constexpr float SEG_LEN = 10.0f;

//=============================================================================================================
// STATIC DEFINITIONS
//=============================================================================================================

/**
 * @brief Create a channel selection containing exactly the named channels.
 *
 * Refactored: mne_ch_selection_these (mne_ch_selection.c)
 *
 * @param[in] selname  Name for the selection.
 * @param[in] names    Channel names to include.
 * @param[in] nch      Number of channels.
 *
 * @return The new channel selection.
 */
static std::unique_ptr<MNEChSelection> mne_ch_selection_these(const QString& selname, const StringList& names, int nch)
{
    auto sel = std::make_unique<MNEChSelection>();
    sel->name = selname.toStdString();
    sel->ndef = nch;
    sel->kind = MNE_CH_SELECTION_USER;

    for (int c = 0; c < nch; c++)
        sel->chdef.push_back(names[c]);

    return sel;
}

/**
 * @brief Assign raw data channels to a channel selection.
 *
 * Matches the selection's channel names against the raw data's channel
 * info (including derived channels), filling the pick indices and
 * channel-kind fields.
 *
 * Refactored: mne_ch_selection_assign_chs (mne_ch_selection.c)
 *
 * @param[in,out] sel   The channel selection to populate.
 * @param[in]     data  The raw data whose channels are matched.
 *
 * @return Number of directly matched channels (excluding derivations).
 */
static int mne_ch_selection_assign_chs(MNEChSelection* sel,
                                       MNERawData* data)
{
    if (!sel || !data)
        return 0;

    auto info = data->info.get();
    sel->chspick = sel->chdef;
    sel->chspick_nospace = sel->chdef;
    for (auto& name : sel->chspick_nospace)
        name = std::string(BASELIB::trimmed(name));
    sel->nchan = sel->ndef;

    sel->pick.setConstant(sel->nchan, -1);
    sel->pick_deriv.setConstant(sel->nchan, -1);
    sel->ch_kind.setConstant(sel->nchan, -1);

    for (int c = 0; c < sel->nchan; c++) {
        for (int rc = 0; rc < info->nchan; rc++) {
            if (BASELIB::equalsIgnoreCase(sel->chspick[c], info->chInfo[rc].ch_name) ||
                BASELIB::equalsIgnoreCase(sel->chspick_nospace[c], info->chInfo[rc].ch_name)) {
                sel->pick[c] = rc;
                sel->ch_kind[c] = info->chInfo[rc].kind;
                break;
            }
        }
    }
    /*
     * Maybe the derivations will help
     */
    sel->nderiv = 0;
    if (data->deriv_matched) {
        const StringList& deriv_names = data->deriv_matched->deriv_data->rowlist;
        int nderiv = data->deriv_matched->deriv_data->nrow;

        for (int c = 0; c < sel->nchan; c++) {
            if (sel->pick[c] == -1) {
                for (int d = 0; d < nderiv; d++) {
                    if (BASELIB::equalsIgnoreCase(sel->chspick[c], deriv_names[d]) &&
                        data->deriv_matched->valid.size() > 0 && data->deriv_matched->valid[d]) {
                        sel->pick_deriv[c] = d;
                        sel->ch_kind[c] = data->deriv_matched->chs[d].kind;
                        sel->nderiv++;
                        break;
                    }
                }
            }
        }
    }
    /*
     * Try simple channels again without the part after dashes
     */
    for (int c = 0; c < sel->nchan; c++) {
        if (sel->pick[c] == -1 && sel->pick_deriv[c] == -1) {
            for (int rc = 0; rc < info->nchan; rc++) {
                const QString chName = QString::fromStdString(info->chInfo[rc].ch_name);
                QString dash = chName.mid(chName.indexOf("-") + 1);
                if (!dash.isNull()) {
                    if (BASELIB::equalsIgnoreCase(sel->chspick[c], info->chInfo[rc].ch_name) ||
                        BASELIB::equalsIgnoreCase(sel->chspick_nospace[c], info->chInfo[rc].ch_name)) {
                        sel->pick[c] = rc;
                        sel->ch_kind[c] = info->chInfo[rc].kind;
                        break;
                    }
                }
            }
        }
    }
    int nch = 0;
    for (int c = 0; c < sel->nchan; c++) {
        if (sel->pick[c] >= 0)
            nch++;
    }
    if (sel->nderiv > 0)
        BASELIB::Log::info("Selection \"{}\" has {} matched derived channels.", sel->name, sel->nderiv);
    return nch;
}

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

InvDipoleFit::InvDipoleFit(InvDipoleFitSettings* p_settings)
: settings(p_settings)
{
}

//=============================================================================================================
InvEcdSet InvDipoleFit::calculateFit() const
{
    InvEcdSet set;

    BASELIB::Log::info("---- Setting up...\n");
    std::unique_ptr<FwdEegSphereModel> eeg_model;
    if (settings->include_eeg) {
        eeg_model = FwdEegSphereModel::setup_eeg_sphere_model(settings->eeg_model_file.toStdU16String(), settings->eeg_model_name.toStdString(), settings->eeg_sphere_rad);
        if (!eeg_model)
            return set;
    }

    std::unique_ptr<InvDipoleFitData> fit_data(InvDipoleFitData::setup_dipole_fit_data(
        settings->mriname,
        settings->measname,
        settings->bemname,
        &settings->r0,
        eeg_model.get(),
        settings->accurate,
        settings->badname,
        settings->noisename,
        settings->grad_std,
        settings->mag_std,
        settings->eeg_std,
        settings->mag_reg,
        settings->grad_reg,
        settings->eeg_reg,
        settings->diagnoise,
        settings->projnames,
        settings->include_meg,
        settings->include_eeg));
    if (!fit_data)
        return set;

    eeg_model.release(); // ownership transferred to fit_data->eeg_model

    fit_data->fit_mag_dipoles = settings->fit_mag_dipoles;

    std::unique_ptr<MNERawData> raw;
    std::unique_ptr<MNEMeasData> data;
    std::unique_ptr<MNEChSelection> sel;

    if (settings->is_raw) {
        BASELIB::Log::info("\n---- Opening a raw data file...\n");
        raw.reset(MNERawData::open_file(settings->measname.toStdU16String(), true, false, settings->filter));
        if (!raw)
            return set;
        /*
     * Set up a channel selection to pick data from the raw file
     */
        sel = mne_ch_selection_these("fit", fit_data->ch_names, fit_data->nmeg + fit_data->neeg);
        mne_ch_selection_assign_chs(sel.get(), raw.get());
        for (int c = 0; c < sel->nchan; c++)
            if (sel->pick[c] < 0) {
                BASELIB::Log::critical("All desired channels were not available");
                return set;
            }
        BASELIB::Log::info("\tChannel selection created.");
        /*
         * Let's be a little generous here
         */
        float t1 = raw->first_samp / raw->info->sfreq;
        float t2 = (raw->first_samp + raw->nsamp - 1) / raw->info->sfreq;
        if (settings->tmin < t1 + settings->integ)
            settings->tmin = t1 + settings->integ;
        if (settings->tmax > t2 - settings->integ)
            settings->tmax = t2 - settings->integ;
        if (settings->tstep < 0)
            settings->tstep = 1.0f / raw->info->sfreq;

        BASELIB::Log::info("\tOpened raw data file {} : {} MEG and {} EEG", settings->measname.toStdString(), fit_data->nmeg, fit_data->neeg);
    } else {
        BASELIB::Log::info("\n---- Reading data...\n");
        data = MNEMeasData::mne_read_meas_data(settings->measname.toStdU16String(),
                                               settings->setno,
                                               nullptr,
                                               nullptr,
                                               fit_data->ch_names,
                                               fit_data->nmeg + fit_data->neeg);
        if (!data)
            return set;
        if (settings->do_baseline)
            data->adjust_baselines(settings->bmin, settings->bmax);
        else
            BASELIB::Log::info("\tNo baseline setting in effect.");
        if (settings->tmin < data->current->tmin + settings->integ / 2.0f)
            settings->tmin = data->current->tmin + settings->integ / 2.0f;
        if (settings->tmax > data->current->tmin + (data->current->np - 1) * data->current->tstep - settings->integ / 2.0f)
            settings->tmax = data->current->tmin + (data->current->np - 1) * data->current->tstep - settings->integ / 2.0f;
        if (settings->tstep < 0)
            settings->tstep = data->current->tstep;

        BASELIB::Log::info("\tRead data set {} from {} : {} MEG and {} EEG", settings->setno, settings->measname.toStdString(), fit_data->nmeg, fit_data->neeg);
        if (!settings->noisename.isEmpty()) {
            BASELIB::Log::info("Scaling the noise covariance...");
            if (InvDipoleFitData::scale_noise_cov(fit_data.get(), data->current->nave) < 0)
                return set;
        }
    }

    /*
     * Proceed to computing the fits
     */
    BASELIB::Log::info("\n---- Computing the forward solution for the guesses...\n");
    auto guess = std::make_unique<InvGuessData>(settings->guessname,
                                                settings->guess_surfname,
                                                settings->guess_mindist, settings->guess_exclude, settings->guess_grid, fit_data.get(), settings->guess_rad);
    if (guess->nguess == 0) {
        BASELIB::Log::critical("Could not create the initial guesses.");
        return set;
    }

    BASELIB::Log::info("\n---- Fitting : {:7.1f} ... {:7.1f} ms (step: {:6.1f} ms integ: {:6.1f} ms)\n", 1000 * settings->tmin, 1000 * settings->tmax, 1000 * settings->tstep, 1000 * settings->integ);

    if (raw) {
        if (!fit_dipoles_raw(settings->measname, raw.get(), sel.get(), fit_data.get(), guess.get(), settings->tmin, settings->tmax, settings->tstep, settings->integ, settings->verbose, set))
            return set;
    } else {
        if (!fit_dipoles(settings->measname, data.get(), fit_data.get(), guess.get(), settings->tmin, settings->tmax, settings->tstep, settings->integ, settings->verbose, set))
            return set;
    }
    BASELIB::Log::info("{} dipoles fitted", set.size());

    return set;
}

//=============================================================================================================

bool InvDipoleFit::fit_dipoles(const QString& dataname, MNEMeasData* data, InvDipoleFitData* fit, InvGuessData* guess, float tmin, float tmax, float tstep, float integ, int verbose, InvEcdSet& p_set)
{
    Eigen::VectorXf one(data->nchan);
    InvEcdSet set;
    InvEcd dip;
    constexpr int report_interval = 10;

    set.dataname = dataname;

    if (verbose)
        BASELIB::Log::info("Fitting...");
    for (int s = 0; tmin + s * tstep < tmax; s++) {
        float time = tmin + s * tstep;
        if (data->current->getValuesAtTime(time, integ, data->nchan, false, one.data()) < 0) {
            BASELIB::Log::warning("Cannot pick time: {:7.1f} ms", 1000.0f * time);
            continue;
        }

        if (!InvDipoleFitData::fit_one(fit, guess, time, one, verbose, dip))
            BASELIB::Log::warning("t = {:7.1f} ms : fit error", 1000.0f * time);
        else {
            set.addEcd(dip);
            if (verbose)
                dip.print();
            else {
                if (set.size() % report_interval == 0)
                    BASELIB::Log::info("{}..", set.size());
            }
        }
    }
    if (!verbose)
        BASELIB::Log::info("[done]");
    p_set = set;
    return true;
}

//=============================================================================================================

bool InvDipoleFit::fit_dipoles_raw(const QString& dataname, MNERawData* raw, MNEChSelection* sel, InvDipoleFitData* fit, InvGuessData* guess, float tmin, float tmax, float tstep, float integ, int verbose, InvEcdSet& p_set)
{
    const int nchan = sel->nchan;
    const float sfreq = raw->info->sfreq;
    const float myinteg = integ > 0.0f ? 2 * integ : 0.1f;
    const int overlap = static_cast<int>(std::ceil(myinteg * sfreq));
    const int length = static_cast<int>(SEG_LEN * sfreq);
    const int step = length - overlap;
    const int stepo = step + overlap / 2;
    int start = raw->first_samp;
    constexpr int report_interval = 10;

    Eigen::VectorXf one(nchan);

    // Row-major storage compatible with float** interface
    std::vector<float> storage(static_cast<std::size_t>(nchan) * length);
    std::vector<float*> rows(nchan);
    for (int i = 0; i < nchan; ++i)
        rows[i] = storage.data() + i * length;
    float** data = rows.data();

    InvEcd dip;
    InvEcdSet set;
    set.dataname = dataname;

    /*
     * Load the initial data segment
     */
    float stime = start / sfreq;
    if (raw->pick_data_filt(sel, start, length, data) < 0)
        return false;
    if (verbose)
        BASELIB::Log::info("Fitting...");
    for (int s = 0; tmin + s * tstep < tmax; s++) {
        float time = tmin + s * tstep;
        int picks = time * sfreq - start;
        if (picks > stepo) {
            start = start + step;
            if (raw->pick_data_filt(sel, start, length, data) < 0)
                return false;
            picks = time * sfreq - start;
            stime = start / sfreq;
        }
        if (MNEMeasDataSet::getValuesFromChannelData(time, integ, data, length, nchan, stime, sfreq, false, one.data()) < 0) {
            BASELIB::Log::warning("Cannot pick time: {:8.3f} s", time);
            continue;
        }
        if (!InvDipoleFitData::fit_one(fit, guess, time, one, verbose, dip))
            BASELIB::Log::warning("t = {:8.3f} s : fit error", time);
        else {
            set.addEcd(dip);
            if (verbose)
                dip.print();
            else {
                if (set.size() % report_interval == 0)
                    BASELIB::Log::info("{}..", set.size());
            }
        }
    }
    if (!verbose)
        BASELIB::Log::info("[done]");
    p_set = set;
    return true;
}

//=============================================================================================================

bool InvDipoleFit::fit_dipoles_raw(const QString& dataname, MNERawData* raw, MNEChSelection* sel, InvDipoleFitData* fit, InvGuessData* guess, float tmin, float tmax, float tstep, float integ, int verbose)
{
    InvEcdSet set;
    return fit_dipoles_raw(dataname, raw, sel, fit, guess, tmin, tmax, tstep, integ, verbose, set);
}
