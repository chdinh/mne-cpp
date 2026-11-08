//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     mne_layout.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.4.0
 * @date     October 2026
 * @brief    Implementation of @ref MNELIB::MNELayout and @ref MNELIB::MNELayoutPort (MNE-C mne_layout.c).
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "mne_layout.h"

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================


//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <ranges>
#include <sstream>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace MNELIB;
using namespace UTILSLIB;

//=============================================================================================================
// DEFINE STATIC METHODS
//=============================================================================================================

namespace
{

bool isInside(const MNELayoutPort& port, const QRectF& area)
{
    return port.xmin >= area.left() && port.xmax <= area.right() && port.ymin >= area.top() && port.ymax <= area.bottom();
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

std::string MNELayoutPort::cleanName(std::string_view name)
{
    std::string clean;
    clean.reserve(name.size());
    for (const char c : name.substr(0, name.find('-'))) {
        if (c != ' ') {
            clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return clean;
}

//=============================================================================================================

bool MNELayoutPort::contains(std::string_view channel) const
{
    return UTILSLIB::contains(names, cleanName(channel));
}

//=============================================================================================================

std::optional<MNELayout> MNELayout::read(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) {
        qWarning("MNELayout::read - Cannot open %s", utf8FromPath(path).c_str());
        return std::nullopt;
    }

    MNELayout layout;
    layout.fileName = utf8FromPath(path);
    bool haveExtent = false;
    std::string raw;
    while (std::getline(file, raw)) {
        const std::string line(trimmed(raw));
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        std::istringstream fields(line);
        if (!haveExtent) {
            double v[4];
            if (!(fields >> v[0] >> v[1] >> v[2] >> v[3])) {
                qWarning("MNELayout::read - Illegal extent in %s", utf8FromPath(path).c_str());
                return std::nullopt;
            }
            layout.m_extent = QRectF(QPointF(v[0], v[2]), QPointF(v[1], v[3]));
            haveExtent = true;
            continue;
        }
        MNELayoutPort port;
        double x0 = 0.0;
        double y0 = 0.0;
        double w = 0.0;
        double h = 0.0;
        if (!(fields >> port.portno >> x0 >> y0 >> w >> h)) {
            qWarning("MNELayout::read - Bad layout item in %s: %s", utf8FromPath(path).c_str(), line.c_str());
            return std::nullopt;
        }
        port.xmin = static_cast<float>(x0);
        port.xmax = static_cast<float>(x0 + std::fabs(w));
        port.ymin = static_cast<float>(y0);
        port.ymax = static_cast<float>(y0 + std::fabs(h));
        port.invert = h < 0;
        // The names are the rest of the line and may contain spaces ("MEG 0113:MEG 0112").
        std::string rest;
        std::getline(fields, rest);
        port.label = trimmed(rest);
        for (const std::string& name : split(port.label, ':', true)) {
            port.names.push_back(MNELayoutPort::cleanName(name));
        }
        layout.ports.push_back(std::move(port));
    }
    if (!haveExtent) {
        qWarning("MNELayout::read - No extent in %s", utf8FromPath(path).c_str());
        return std::nullopt;
    }
    layout.m_visible = layout.m_extent;
    return layout;
}

//=============================================================================================================

std::map<std::string, QPointF> MNELayout::channelPositions() const
{
    std::map<std::string, QPointF> positions;
    for (const MNELayoutPort& port : ports) {
        for (const std::string& name : split(port.label, ':', true)) {
            positions.insert_or_assign(std::string(trimmed(name)), QPointF(port.xmin, port.ymin));
        }
    }
    return positions;
}

//=============================================================================================================

int MNELayout::matchPorts(std::string_view channel)
{
    int nmatch = 0;
    for (MNELayoutPort& port : ports) {
        port.matched = !channel.empty() && port.contains(channel) && isVisible(port);
        nmatch += port.matched ? 1 : 0;
    }
    return nmatch;
}

//=============================================================================================================

int MNELayout::matchPorts(const StringList& channels)
{
    m_matches = Eigen::MatrixXi::Zero(static_cast<Eigen::Index>(ports.size()), static_cast<Eigen::Index>(channels.size()));
    int nmatch = 0;
    for (std::size_t p = 0; p < ports.size(); ++p) {
        MNELayoutPort& port = ports[p];
        port.matched = false;
        if (!isVisible(port)) {
            continue;
        }
        for (std::size_t c = 0; c < channels.size(); ++c) {
            if (port.contains(channels[c])) {
                m_matches(static_cast<Eigen::Index>(p), static_cast<Eigen::Index>(c)) = 1;
                port.matched = true;
                ++nmatch;
            }
        }
    }
    return nmatch;
}

//=============================================================================================================

int MNELayout::portAt(const QPointF& point) const
{
    const auto it = std::find_if(ports.cbegin(), ports.cend(), [&point](const MNELayoutPort& port) {
        return point.x() > port.xmin && point.x() < port.xmax && point.y() > port.ymin && point.y() < port.ymax;
    });
    return it == ports.cend() ? -1 : static_cast<int>(it - ports.cbegin());
}

//=============================================================================================================

int MNELayout::confine(const QRectF& area)
{
    const QRectF requested = area.normalized();
    QRectF covered;
    int nmatch = 0;
    for (const MNELayoutPort& port : ports) {
        if (isInside(port, requested)) {
            covered = nmatch == 0 ? port.rect() : covered.united(port.rect());
            ++nmatch;
        }
    }
    if (nmatch > 0) {
        const double margin = 0.01 * covered.width();
        m_visible = covered.adjusted(-margin, -margin, margin, margin);
    }
    return nmatch;
}

//=============================================================================================================

bool MNELayout::isVisible(const MNELayoutPort& port) const
{
    return isInside(port, m_visible);
}
