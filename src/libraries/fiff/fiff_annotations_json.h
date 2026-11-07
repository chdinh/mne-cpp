//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     fiff_annotations_json.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    JSON form of annotation channel names and extras, shared by the FIFF and JSON annotation files (internal).
 */

#ifndef FIFF_ANNOTATIONS_JSON_H
#define FIFF_ANNOTATIONS_JSON_H

#include "fiff_annotations.h"

#include <QJsonArray>
#include <QJsonObject>

#include <string>
#include <vector>

namespace FIFFLIB
{

/** @return The names as a JSON array of strings. */
QJsonArray namesToJson(const std::vector<std::string>& names);

/** @return The strings of a JSON array. */
std::vector<std::string> namesFromJson(const QJsonArray& array);

/** @return The extras as a JSON object. */
QJsonObject extrasToJson(const FiffAnnotation::Extras& extras);

/** @return The extras of a JSON object; values other than null, bool, number or string are skipped. */
FiffAnnotation::Extras extrasFromJson(const QJsonObject& object);

} // namespace FIFFLIB

#endif // FIFF_ANNOTATIONS_JSON_H
