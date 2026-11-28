//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     average_selection_panel.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    AverageSelectionPanel class definition.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "average_selection_panel.h"
#include "../host/settings_store.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <string>
#include <utility>
#include <vector>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace mne::viz;

//=============================================================================================================
// DEFINE STATIC METHODS
//=============================================================================================================

namespace
{

constexpr const char* kActive = "active/";
constexpr const char* kColor = "color/";
constexpr const char* kColorGroup = "averageColorMap";
constexpr const char* kActivationGroup = "averageActivationMap";

std::string child_key(const std::string& group, const std::string& name)
{
    std::string key = group;
    key += '/';
    key += name;
    return key;
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

AverageSelectionPanel::AverageSelectionPanel()
: Panel("AverageSelectionView", "Average Selection")
{
    set_width(330);
    m_listener = form().add_listener([this](const std::string& key, const std::optional<SettingValue>& value) {
        if (!m_updating && value) {
            changed(key);
        }
    });
}

//=============================================================================================================

AverageSelectionPanel::~AverageSelectionPanel()
{
    form().remove_listener(m_listener);
}

//=============================================================================================================

void AverageSelectionPanel::set_average_colors(const std::map<std::string, Color>& colors)
{
    m_colors = colors;
    rebuild();
}

//=============================================================================================================

void AverageSelectionPanel::set_average_activation(const std::map<std::string, bool>& activation)
{
    m_activation = activation;
    rebuild();
}

//=============================================================================================================

void AverageSelectionPanel::load(const SettingsStore& store, const std::string& path)
{
    if (path.empty()) {
        return;
    }
    const std::string colorGroup = settings_key(path, kColorGroup);
    for (const std::string& name : store.child_keys(colorGroup)) {
        if (const std::optional<Color> color = parse_setting_color(store.read_string(child_key(colorGroup, name), {}))) {
            m_colors[name] = *color;
        }
    }
    const std::string activationGroup = settings_key(path, kActivationGroup);
    for (const std::string& name : store.child_keys(activationGroup)) {
        m_activation[name] = store.read_bool(child_key(activationGroup, name), true);
    }
    rebuild();
}

//=============================================================================================================

void AverageSelectionPanel::save(SettingsStore& store, const std::string& path) const
{
    if (path.empty()) {
        return;
    }
    const std::string colorGroup = settings_key(path, kColorGroup);
    for (const auto& [name, color] : m_colors) {
        store.set_value(child_key(colorGroup, name), setting_color(color));
    }
    const std::string activationGroup = settings_key(path, kActivationGroup);
    for (const auto& [name, active] : m_activation) {
        store.set_value(child_key(activationGroup, name), active);
    }
}

//=============================================================================================================

void AverageSelectionPanel::rebuild()
{
    // The colours and activations arrive one after the other; the form waits until they match
    if (m_colors.size() != m_activation.size()) {
        return;
    }
    const bool updating = std::exchange(m_updating, true);
    std::vector<std::string> keys;
    keys.reserve(form().fields().size());
    for (const SettingField& field : form().fields()) {
        keys.push_back(field.key);
    }
    for (const std::string& key : keys) {
        form().remove(key);
    }
    int count = 0;
    for (const auto& [name, color] : m_colors) {
        if (count++ >= kMaxAverages) {
            break;
        }
        const auto active = m_activation.find(name);
        SettingField show;
        show.key = kActive + name;
        show.kind = SettingField::Kind::Bool;
        show.label = name;
        show.default_value = active != m_activation.end() && active->second;
        show.persistent = false;
        form().add(std::move(show));
        SettingField tint;
        tint.key = kColor + name;
        tint.kind = SettingField::Kind::Color;
        tint.label = "Color:";
        tint.default_value = setting_color(color);
        tint.persistent = false;
        form().add(std::move(tint));
    }
    m_updating = updating;
}

//=============================================================================================================

void AverageSelectionPanel::changed(const std::string& key)
{
    if (key.starts_with(kActive)) {
        m_activation[key.substr(std::char_traits<char>::length(kActive))] = form().get<bool>(key);
        if (m_callbacks.activation_changed) {
            m_callbacks.activation_changed(m_activation);
        }
    } else if (key.starts_with(kColor)) {
        if (const std::optional<Color> color = parse_setting_color(form().get<std::string>(key))) {
            m_colors[key.substr(std::char_traits<char>::length(kColor))] = *color;
            if (m_callbacks.colors_changed) {
                m_callbacks.colors_changed(m_colors);
            }
        }
    }
}
