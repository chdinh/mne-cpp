//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     inv_hpi_data_updater.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Implementation of the HPI pre-processing front-end.
 *
 * Implements the cached state machine: detect @c FiffInfo changes
 * (channel list, bad-channel list, dig-point list), rebuild the
 * channel selection, project the SSP matrix into the good-channel
 * subspace, apply it to the data buffer and rebuild the
 * @ref InvSensorSet from the updated coil list. Exposes pre-projected
 * data, projectors, sensors and digitised coil positions as read-only
 * accessors consumed by @ref InvHpiFit.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "inv_hpi_data_updater.h"

#include <vector>
#include <utils/string_utils.h>
#include <fiff/fiff_ch_info.h>
#include <fiff/fiff_info.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

//=============================================================================================================
// EIGEN INCLUDES
//=============================================================================================================

#include <Eigen/Dense>
#include <QDebug>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace INVLIB;
using namespace FIFFLIB;
using namespace Eigen;

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

//=============================================================================================================

InvHpiDataUpdater::InvHpiDataUpdater(const FiffInfo::SPtr pFiffInfo)
: m_sensors(InvSensorSet())
{
    updateBadChannels(pFiffInfo);
    updateChannels(pFiffInfo);
    updateHpiDigitizer(pFiffInfo->dig);
    updateSensors(m_lChannels);
}

//=============================================================================================================

void InvHpiDataUpdater::updateBadChannels(FiffInfo::SPtr pFiffInfo)
{
    m_lBads = pFiffInfo->bads;
}

//=============================================================================================================

void InvHpiDataUpdater::updateChannels(FiffInfo::SPtr pFiffInfo)
{
    // Get the indices of inner layer channels and exclude bad channels and create channellist
    const int iNumCh = pFiffInfo->nchan;
    m_lChannels.clear();
    m_vecInnerind.clear();
    for (int i = 0; i < iNumCh; ++i) {
        if (pFiffInfo->chs[i].chpos.coil_type == FIFFV_COIL_BABY_MAG ||
            pFiffInfo->chs[i].chpos.coil_type == FIFFV_COIL_VV_PLANAR_T1 ||
            pFiffInfo->chs[i].chpos.coil_type == FIFFV_COIL_VV_PLANAR_T2 ||
            pFiffInfo->chs[i].chpos.coil_type == FIFFV_COIL_VV_PLANAR_T3) {
            // Check if the sensor is bad, if not append to innerind

            if (!(UTILSLIB::contains(pFiffInfo->bads, pFiffInfo->ch_names.at(i)))) {
                m_vecInnerind.append(i);
                m_lChannels.push_back(pFiffInfo->chs[i]);
            }
        }
    }
}

//=============================================================================================================

void InvHpiDataUpdater::updateSensors(const std::vector<FIFFLIB::FiffChInfo>& lChannels)
{
    const Accuracy accuracy = Accuracy::high;
    m_sensors = m_sensorSetCreator.updateSensorSet(lChannels, accuracy);
}

//=============================================================================================================

void InvHpiDataUpdater::updateHpiDigitizer(const FiffDigPointSet& dig)
{
    const FiffDigPointSet coils = dig.pick_types({FIFFV_POINT_HPI});
    if (coils.empty()) {
        qWarning().noquote().nospace() << "InvHpiFit::updateHpiDigitizer - No HPI coils digitized. Returning.";
        return;
    }
    m_matHpiDigitizer = MatrixXd(static_cast<Eigen::Index>(coils.size()), 3);
    for (std::size_t i = 0; i < coils.size(); ++i) {
        m_matHpiDigitizer.row(static_cast<Eigen::Index>(i)) << coils[i].r[0], coils[i].r[1], coils[i].r[2];
    }
}

//=============================================================================================================

void InvHpiDataUpdater::checkForUpdate(const FiffInfo::SPtr pFiffInfo)
{
    const bool bUpdate = checkIfChanged(pFiffInfo->bads, pFiffInfo->chs);
    if (bUpdate) {
        updateBadChannels(pFiffInfo);
        updateChannels(pFiffInfo);
        updateHpiDigitizer(pFiffInfo->dig);
        updateSensors(m_lChannels);
    }
}

//=============================================================================================================

bool InvHpiDataUpdater::checkIfChanged(const StringList& lBads, const std::vector<FIFFLIB::FiffChInfo>& lChannels)
{
    bool bUpdate = false;
    if (!(m_lBads == lBads) || !(m_lChannels == lChannels)) {
        bUpdate = true;
    }
    return bUpdate;
}

//=============================================================================================================

void InvHpiDataUpdater::prepareDataAndProjectors(const MatrixXd& matData, const MatrixXd& matProjectors)
{
    prepareData(matData);
    prepareProjectors(matProjectors);
    m_matDataProjected = m_matProjectors * m_matInnerdata;
}

//=============================================================================================================

void InvHpiDataUpdater::prepareData(const Eigen::MatrixXd& matData)
{
    // extract data for channels to use
    m_matInnerdata = MatrixXd(m_vecInnerind.size(), matData.cols());

    for (int j = 0; j < m_vecInnerind.size(); ++j) {
        m_matInnerdata.row(j) << matData.row(m_vecInnerind[j]);
    }
}

//=============================================================================================================

void InvHpiDataUpdater::prepareProjectors(const Eigen::MatrixXd& matProjectors)
{
    // check if m_vecInnerInd is alreadz initialized
    if (m_vecInnerind.size() == 0) {
        qWarning().noquote().nospace() << "InvHpiFit::updateProjectors - No channels. Returning.";
        return;
    }

    //Create new projector based on the excluded channels, first exclude the rows then the columns
    MatrixXd matProjectorsRows(m_vecInnerind.size(), matProjectors.cols());
    MatrixXd matProjectorsInnerind(m_vecInnerind.size(), m_vecInnerind.size());

    for (int i = 0; i < matProjectorsRows.rows(); ++i) {
        matProjectorsRows.row(i) = matProjectors.row(m_vecInnerind.at(i));
    }

    for (int i = 0; i < matProjectorsInnerind.cols(); ++i) {
        matProjectorsInnerind.col(i) = matProjectorsRows.col(m_vecInnerind.at(i));
    }
    m_matProjectors = matProjectorsInnerind;
    return;
}
