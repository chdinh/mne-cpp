//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     fiff_annotations.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.2.0
 * @date     April 2026
 * @brief    Implementation of @ref FiffAnnotation / @ref FiffAnnotations, including the JSON and CSV serializers.
 *
 * FiffStream reads and writes them under @c FIFFB_MNE_ANNOTATIONS. The JSON and
 * CSV paths exist so the same annotations can be exchanged with BIDS
 * sidecars and external scoring tools that do not parse FIFF.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fiff_annotations.h"
#include "fiff_annotations_json.h"

#include <utils/string_utils.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FIFFLIB;
using UTILSLIB::ErrorCode;
using UTILSLIB::Expected;
using UTILSLIB::fail;

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

QJsonArray FIFFLIB::namesToJson(const std::vector<std::string>& names)
{
    QJsonArray array;
    for (const std::string& name : names) {
        array.append(QString::fromStdString(name));
    }
    return array;
}

//=============================================================================================================

std::vector<std::string> FIFFLIB::namesFromJson(const QJsonArray& array)
{
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValueConstRef name : array) {
        names.push_back(name.toString().toStdString());
    }
    return names;
}

//=============================================================================================================

QJsonObject FIFFLIB::extrasToJson(const FiffAnnotation::Extras& extras)
{
    QJsonObject object;
    for (const auto& [key, value] : extras) {
        object.insert(QString::fromStdString(key), std::visit([](const auto& v) -> QJsonValue {
                          using T = std::decay_t<decltype(v)>;
                          if constexpr (std::is_same_v<T, std::monostate>) {
                              return QJsonValue(QJsonValue::Null);
                          } else if constexpr (std::is_same_v<T, std::string>) {
                              return QString::fromStdString(v);
                          } else if constexpr (std::is_same_v<T, std::int64_t>) {
                              return static_cast<qint64>(v);
                          } else {
                              return v;
                          }
                      },
                                                              value));
    }
    return object;
}

//=============================================================================================================

FiffAnnotation::Extras FIFFLIB::extrasFromJson(const QJsonObject& object)
{
    FiffAnnotation::Extras extras;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QJsonValue value = it.value();
        FiffAnnotation::Extra extra;
        if (value.isBool()) {
            extra = value.toBool();
        } else if (value.isDouble()) {
            const double number = value.toDouble();
            // JSON has one number type; integral values come back as int, as from Python's json
            if (std::trunc(number) == number && std::abs(number) < 9.007199254740992e15) {
                extra = static_cast<std::int64_t>(number);
            } else {
                extra = number;
            }
        } else if (value.isString()) {
            extra = value.toString().toStdString();
        } else if (!value.isNull()) {
            continue;
        }
        extras.insert_or_assign(it.key().toStdString(), std::move(extra));
    }
    return extras;
}

//=============================================================================================================

namespace
{

Expected<void> writeBytes(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        return fail(ErrorCode::Io, "cannot write {}", UTILSLIB::utf8FromPath(path));
    }
    return {};
}

Expected<std::string> readBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::Io, "cannot read {}", UTILSLIB::utf8FromPath(path));
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

std::string lowerExtension(const std::filesystem::path& path)
{
    return UTILSLIB::toLower(UTILSLIB::utf8FromPath(path.extension()));
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

void FiffAnnotations::append(double onset, double duration, std::string description, std::vector<std::string> ch_names)
{
    m_annotations.push_back({onset, duration, std::move(description), std::move(ch_names), {}, {}});
}

//=============================================================================================================

int FiffAnnotations::onset_to_sample(std::size_t index, double sfreq, int first_sample) const
{
    return static_cast<int>(m_annotations[index].onset * sfreq) + first_sample;
}

//=============================================================================================================

int FiffAnnotations::end_to_sample(std::size_t index, double sfreq, int first_sample) const
{
    const FiffAnnotation& a = m_annotations[index];
    return static_cast<int>((a.onset + a.duration) * sfreq) + first_sample;
}

//=============================================================================================================

FiffAnnotations FiffAnnotations::select(std::string_view prefix) const
{
    FiffAnnotations result;
    std::ranges::copy_if(m_annotations, std::back_inserter(result.m_annotations), [prefix](const FiffAnnotation& a) {
        return a.description.starts_with(prefix);
    });
    return result;
}

//=============================================================================================================

FiffAnnotations FiffAnnotations::select_by_channel(std::string_view ch_name) const
{
    FiffAnnotations result;
    std::ranges::copy_if(m_annotations, std::back_inserter(result.m_annotations), [ch_name](const FiffAnnotation& a) {
        return a.ch_names.empty() || std::ranges::find(a.ch_names, ch_name) != a.ch_names.end();
    });
    return result;
}

//=============================================================================================================

FiffAnnotations FiffAnnotations::crop(double tmin, double tmax) const
{
    FiffAnnotations result;
    for (const FiffAnnotation& a : m_annotations) {
        if (a.onset > tmax || a.onset + a.duration < tmin) {
            continue;
        }
        FiffAnnotation clipped = a;
        if (clipped.onset < tmin) {
            clipped.duration -= tmin - clipped.onset;
            clipped.onset = tmin;
        }
        clipped.duration = std::min(clipped.duration, tmax - clipped.onset);
        result.m_annotations.push_back(std::move(clipped));
    }
    return result;
}

//=============================================================================================================

Expected<FiffAnnotations> FiffAnnotations::read_json(const std::filesystem::path& path)
{
    const Expected<std::string> bytes = readBytes(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(*bytes), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(ErrorCode::Format, "{} is not an annotation JSON file: {}", UTILSLIB::utf8FromPath(path), parseError.errorString().toStdString());
    }

    FiffAnnotations annot;
    const QJsonArray entries = doc.object().value(QStringLiteral("annotations")).toArray();
    for (const QJsonValueConstRef entry : entries) {
        const QJsonObject obj = entry.toObject();
        annot.m_annotations.push_back({obj.value(QStringLiteral("onset")).toDouble(0.0),
                                       obj.value(QStringLiteral("duration")).toDouble(0.0),
                                       obj.value(QStringLiteral("description")).toString().toStdString(),
                                       namesFromJson(obj.value(QStringLiteral("channel_names")).toArray()),
                                       obj.value(QStringLiteral("comment")).toString().toStdString(),
                                       extrasFromJson(obj.value(QStringLiteral("extras")).toObject())});
    }
    return annot;
}

//=============================================================================================================

Expected<void> FiffAnnotations::write_json(const std::filesystem::path& path) const
{
    QJsonArray arr;
    for (const FiffAnnotation& a : m_annotations) {
        QJsonObject obj;
        obj[QStringLiteral("onset")] = a.onset;
        obj[QStringLiteral("duration")] = a.duration;
        obj[QStringLiteral("description")] = QString::fromStdString(a.description);
        obj[QStringLiteral("channel_names")] = namesToJson(a.ch_names);
        obj[QStringLiteral("comment")] = QString::fromStdString(a.comment);
        obj[QStringLiteral("extras")] = extrasToJson(a.extras);
        arr.append(obj);
    }
    QJsonObject root;
    root[QStringLiteral("annotations")] = arr;
    return writeBytes(path, QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString());
}

//=============================================================================================================

Expected<FiffAnnotations> FiffAnnotations::read_csv(const std::filesystem::path& path)
{
    const Expected<std::string> bytes = readBytes(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    FiffAnnotations annot;
    const UTILSLIB::StringList lines = UTILSLIB::split(*bytes, '\n');
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string_view line = UTILSLIB::trimmed(lines[i]);
        // The description may hold commas, so only the first two separate fields
        const std::size_t first = line.find(',');
        const std::size_t second = first == std::string_view::npos ? first : line.find(',', first + 1);
        if (second == std::string_view::npos) {
            continue;
        }
        const std::optional<double> onset = UTILSLIB::toDouble(line.substr(0, first));
        const std::optional<double> duration = UTILSLIB::toDouble(line.substr(first + 1, second - first - 1));
        if (onset && duration) {
            annot.append(*onset, *duration, std::string(line.substr(second + 1)));
        }
    }
    return annot;
}

//=============================================================================================================

Expected<void> FiffAnnotations::write_csv(const std::filesystem::path& path) const
{
    // std::format writes the shortest text that reads back to the same double
    std::string text = "onset,duration,description\n";
    for (const FiffAnnotation& a : m_annotations) {
        std::format_to(std::back_inserter(text), "{},{},{}\n", a.onset, a.duration, a.description);
    }
    return writeBytes(path, text);
}

//=============================================================================================================

Expected<FiffAnnotations> FiffAnnotations::read(const std::filesystem::path& path)
{
    const std::string extension = lowerExtension(path);
    if (extension == ".json") {
        return read_json(path);
    }
    if (extension == ".csv") {
        return read_csv(path);
    }
    if (Expected<FiffAnnotations> json = read_json(path); json || json.error().code == ErrorCode::Io) {
        return json;
    }
    return read_csv(path);
}

//=============================================================================================================

Expected<void> FiffAnnotations::write(const std::filesystem::path& path) const
{
    return lowerExtension(path) == ".csv" ? write_csv(path) : write_json(path);
}
