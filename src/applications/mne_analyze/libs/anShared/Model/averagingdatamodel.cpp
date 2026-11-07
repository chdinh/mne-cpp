//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2020-2026 MNE-CPP Authors
 *
 * @file     averagingdatamodel.cpp
 * @author   Gabriel Motta <gabrielbenmotta@gmail.com>;
 *           Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    0.1.6
 * @date     September, 2020
 * @brief    Definition of the AveragingDataModel Class.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "averagingdatamodel.h"

#include <fiff/fiff_evoked_set.h>
#include <utils/string_utils.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QFile>
#ifdef WASMBUILD
#include <QFileDialog>
#include <QTemporaryDir>
#endif

//=============================================================================================================
// Eigen INCLUDES
//=============================================================================================================

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ANSHAREDLIB;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

AveragingDataModel::AveragingDataModel(QSharedPointer<FIFFLIB::FiffEvokedSet> pEvokedSet,
                                       QObject* parent)
: AbstractModel(parent)
, m_pFiffEvokedSet(pEvokedSet)
, m_bFromFile(false)
{
}

//=============================================================================================================

AveragingDataModel::AveragingDataModel(const QString& sFilePath,
                                       const QByteArray& byteLoadedData,
                                       QObject* parent)
: AbstractModel(sFilePath, parent)
, m_bFromFile(true)
{
    Q_UNUSED(byteLoadedData);

    m_pFiffEvokedSet = QSharedPointer<FIFFLIB::FiffEvokedSet>::create(sFilePath.toStdU16String());
}

//=============================================================================================================

AveragingDataModel::~AveragingDataModel()
{
}

//=============================================================================================================

QVariant AveragingDataModel::data(const QModelIndex& index,
                                  int role) const
{
    Q_UNUSED(index);
    Q_UNUSED(role);

    return QVariant::fromValue(m_pFiffEvokedSet);
}

//=============================================================================================================

Qt::ItemFlags AveragingDataModel::flags(const QModelIndex& index) const
{
    return QAbstractItemModel::flags(index);
}

//=============================================================================================================

int AveragingDataModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

//=============================================================================================================

int AveragingDataModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

//=============================================================================================================

void AveragingDataModel::setEvokedSet(QSharedPointer<FIFFLIB::FiffEvokedSet> pEvokedSet)
{
    m_pFiffEvokedSet = pEvokedSet;
}

//=============================================================================================================

bool AveragingDataModel::isFromFile()
{
    return m_bFromFile;
}

//=============================================================================================================

QSharedPointer<FIFFLIB::FiffInfo> AveragingDataModel::getFiffInfo()
{
    QSharedPointer<FIFFLIB::FiffInfo> pInfo = QSharedPointer<FIFFLIB::FiffInfo>(new FIFFLIB::FiffInfo(m_pFiffEvokedSet->info));
    return pInfo;
}

//=============================================================================================================

QSharedPointer<FIFFLIB::FiffEvokedSet> AveragingDataModel::getEvokedSet()
{
    return m_pFiffEvokedSet;
}

//=============================================================================================================

bool AveragingDataModel::saveToFile(const QString& sPath)
{
    if (!m_pFiffEvokedSet || m_pFiffEvokedSet->evoked.isEmpty()) {
        return false;
    }
#ifdef WASMBUILD
    // The browser offers the bytes as a download; sPath has no meaning there.
    Q_UNUSED(sPath)
    QTemporaryDir dir;
    const QString tmpPath = dir.filePath("evoked-ave.fif");
    if (!dir.isValid() || !m_pFiffEvokedSet->save(tmpPath.toStdU16String())) {
        return false;
    }
    QFile file(tmpPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QFileDialog::saveFileContent(file.readAll(), getModelName());
    return true;
#else
    return m_pFiffEvokedSet->save(sPath.toStdU16String());
#endif
}
