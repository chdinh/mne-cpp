//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     bids_dataset_description.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.1.0
 * @date     March 2026
 * @brief    Implementation of @ref BIDSLIB::BidsDatasetDescription — JSON I/O for the root ``dataset_description.json`` sidecar.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "bids_dataset_description.h"
#include <utils/logger/log.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace BIDSLIB;

//=============================================================================================================
// STATIC METHODS
//=============================================================================================================

BidsDatasetDescription BidsDatasetDescription::read(const QString& sFilePath)
{
    BidsDatasetDescription desc;

    QFile file(sFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        UTILSLIB::Log::warning("[BidsDatasetDescription::read] Cannot open {}", sFilePath.toStdString());
        return desc;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        UTILSLIB::Log::warning("[BidsDatasetDescription::read] Parse error in {} : {}", sFilePath.toStdString(), error.errorString().toStdString());
        return desc;
    }

    QJsonObject json = doc.object();
    desc.name = json.value(QStringLiteral("Name")).toString();
    desc.bidsVersion = json.value(QStringLiteral("BIDSVersion")).toString();
    desc.datasetType = json.value(QStringLiteral("DatasetType")).toString();
    desc.license = json.value(QStringLiteral("License")).toString();

    return desc;
}

//=============================================================================================================

bool BidsDatasetDescription::write(const QString& sFilePath,
                                   const BidsDatasetDescription& desc)
{
    QJsonObject json;

    json[QStringLiteral("Name")] = desc.name;
    json[QStringLiteral("BIDSVersion")] = desc.bidsVersion;

    if (!desc.datasetType.isEmpty())
        json[QStringLiteral("DatasetType")] = desc.datasetType;
    if (!desc.license.isEmpty())
        json[QStringLiteral("License")] = desc.license;

    QFile file(sFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        UTILSLIB::Log::warning("[BidsDatasetDescription::write] Cannot open {} for writing", sFilePath.toStdString());
        return false;
    }

    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}
