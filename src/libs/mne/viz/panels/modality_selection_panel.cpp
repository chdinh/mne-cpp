//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     modality_selection_panel.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    ModalitySelectionPanel class definition.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "modality_selection_panel.h"
#include "../host/settings_store.h"
#include <mne/base/string_utils.h>
#include <mne/fiff/fiff_constants.h>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <algorithm>
#include <array>
#include <utility>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace mne::viz;

//=============================================================================================================
// DEFINE STATIC METHODS
//=============================================================================================================

namespace
{

constexpr std::array<const char*, 6> kModalities{"MAG", "GRAD", "EEG", "EOG", "STIM", "MISC"};
constexpr const char* kPrefix = "modality";

const char* modality_of(const mne::FiffChInfo& channel)
{
    if (channel.unit == FIFF_UNIT_T) {
        return "MAG";
    }
    if (channel.unit == FIFF_UNIT_T_M) {
        return "GRAD";
    }
    switch (channel.kind) {
        case FIFFV_EEG_CH:
            return "EEG";
        case FIFFV_EOG_CH:
            return "EOG";
        case FIFFV_STIM_CH:
            return "STIM";
        case FIFFV_MISC_CH:
            return "MISC";
        default:
            return nullptr;
    }
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

ModalitySelectionPanel::ModalitySelectionPanel(const std::vector<FiffChInfo>& channels)
: Panel("ModalitySelectionView", "Modality Selection")
{
    set_width(330);
    for (const FiffChInfo& channel : channels) {
        const char* modality = modality_of(channel);
        if (modality && std::ranges::find(m_present, modality) == m_present.end()) {
            m_present.emplace_back(modality);
        }
    }
    for (const char* modality : kModalities) {
        m_modalities[modality] = true;
    }
    m_listener = form().add_listener([this](const std::string& key, const std::optional<SettingValue>& value) {
        if (m_updating || !value || !key.starts_with(kPrefix)) {
            return;
        }
        m_modalities[key.substr(std::char_traits<char>::length(kPrefix))] = form().get<bool>(key);
        if (m_callback) {
            m_callback(m_modalities);
        }
    });
    rebuild();
}

//=============================================================================================================

ModalitySelectionPanel::~ModalitySelectionPanel()
{
    form().remove_listener(m_listener);
}

//=============================================================================================================

void ModalitySelectionPanel::set_modality_map(const std::map<std::string, bool>& modalities)
{
    m_modalities = modalities;
    rebuild();
}

//=============================================================================================================

void ModalitySelectionPanel::load(const SettingsStore& store, const std::string& path)
{
    if (path.empty()) {
        return;
    }
    for (const char* modality : kModalities) {
        const std::string key = settings_key(path, std::string(kPrefix) + modality);
        m_modalities[modality] = store.read_bool(key, m_modalities.contains(modality) ? m_modalities.at(modality) : true);
    }
    rebuild();
}

//=============================================================================================================

void ModalitySelectionPanel::save(SettingsStore& store, const std::string& path) const
{
    if (path.empty()) {
        return;
    }
    for (const auto& [modality, on] : m_modalities) {
        store.set_value(settings_key(path, kPrefix + modality), on);
    }
}

//=============================================================================================================

void ModalitySelectionPanel::rebuild()
{
    const bool updating = std::exchange(m_updating, true);
    std::vector<std::string> keys;
    keys.reserve(form().fields().size());
    for (const SettingField& field : form().fields()) {
        keys.push_back(field.key);
    }
    for (const std::string& key : keys) {
        form().remove(key);
    }
    for (const auto& [modality, on] : m_modalities) {
        const bool present = std::ranges::any_of(m_present, [&modality](const std::string& type) { return mne::iequals(type, modality); });
        if (present) {
            SettingField field;
            field.key = kPrefix + modality;
            field.kind = SettingField::Kind::Bool;
            field.label = modality;
            field.default_value = on;
            field.persistent = false;
            form().add(std::move(field));
        }
    }
    m_updating = updating;
}
