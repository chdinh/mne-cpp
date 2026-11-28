//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     scaling_panel.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    ScalingPanel class definition.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "scaling_panel.h"
#include "../host/settings_store.h"
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

constexpr const char* kLink = "linkMagGrad";
constexpr const char* kFactor = "magGradLink";

/** A scale of the form: its channel kind or unit, settings key, type name and range in readable units. */
struct ScaleSpec
{
    int id;            /**< Channel kind, or unit of MEG channels. */
    const char* key;   /**< Field and settings key. */
    const char* type;  /**< Channel type as named by FiffInfo::get_channel_types(). */
    const char* label; /**< Label of the field. */
    const char* unit;  /**< Unit shown after the number. */
    double factor;     /**< SI value of one shown unit. */
    double minimum;    /**< Lowest shown value. */
    double maximum;    /**< Highest shown value. */
    double fallback;   /**< Default shown value. */
    double step;       /**< Spin box step. */
    int decimals;      /**< Decimals shown. */
    bool standard;     /**< Whether the panel starts with the scale. */
};

constexpr std::array<ScaleSpec, 8> kScales{{
    {FIFF_UNIT_T, "scaleMAG", "mag", "MAG", " pT", 1e-12, 0.2, 20.0, 3.0, 0.1, 2, true},
    {FIFF_UNIT_T_M, "scaleGRAD", "grad", "GRAD", " fT/cm", 1e-13, 30.0, 5000.0, 300.0, 10.0, 0, true},
    {FIFFV_EEG_CH, "scaleEEG", "eeg", "EEG", " uV", 1e-6, 30.0, 1000.0, 100.0, 10.0, 0, true},
    {FIFFV_EOG_CH, "scaleEOG", "eog", "EOG", " uV", 1e-6, 1.0, 100.0, 1.0, 1.0, 1, true},
    {FIFFV_ECG_CH, "scaleECG", "ecg", "ECG", " mV", 1e-3, 10.0, 1000.0, 10.0, 10.0, 0, true},
    {FIFFV_EMG_CH, "scaleEMG", "emg", "EMG", " mV", 1e-3, 1.0, 100.0, 1.0, 1.0, 1, false},
    {FIFFV_STIM_CH, "scaleSTIM", "stim", "STIM", "", 1e-3, 1.0, 99999.0, 1.0, 1.0, 0, true},
    {FIFFV_MISC_CH, "scaleMISC", "misc", "MISC", "", 1e-3, 1.0, 100.0, 1.0, 1.0, 1, true},
}};

const ScaleSpec* spec_of_key(const std::string& key)
{
    const auto it = std::ranges::find_if(kScales, [&key](const ScaleSpec& spec) { return key == spec.key; });
    return it == kScales.end() ? nullptr : &*it;
}

const ScaleSpec& spec_of_id(int id)
{
    return *std::ranges::find_if(kScales, [id](const ScaleSpec& spec) { return spec.id == id; });
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

ScalingPanel::ScalingPanel(std::vector<std::string> channel_types)
: Panel("ScalingView", "Scaling")
, m_types(std::move(channel_types))
{
    for (const ScaleSpec& spec : kScales) {
        if (spec.standard) {
            m_scales[spec.id] = static_cast<float>(spec.fallback * spec.factor);
        }
    }
    m_listener = form().add_listener([this](const std::string& key, const std::optional<SettingValue>& value) {
        if (!m_updating && value) {
            changed(key);
        }
    });
    rebuild();
}

//=============================================================================================================

ScalingPanel::~ScalingPanel()
{
    form().remove_listener(m_listener);
}

//=============================================================================================================

void ScalingPanel::set_scale_map(const std::map<int, float>& scales)
{
    m_scales = scales;
    rebuild();
}

//=============================================================================================================

void ScalingPanel::set_mag_grad_link(bool linked)
{
    if (!form().field(kLink)) {
        return;
    }
    const bool updating = std::exchange(m_updating, true);
    form().set_value(kLink, linked);
    if (linked) {
        form().set_value(kFactor, 100.0 * form().get<double>("scaleMAG") / form().get<double>("scaleGRAD"));
    }
    m_updating = updating;
}

//=============================================================================================================

void ScalingPanel::load(const SettingsStore& store, const std::string& path)
{
    if (path.empty()) {
        return;
    }
    for (const ScaleSpec& spec : kScales) {
        const std::string key = settings_key(path, spec.key);
        if (store.contains(key)) {
            m_scales[spec.id] = static_cast<float>(store.read_double(key, spec.fallback * spec.factor));
        }
    }
    rebuild();
}

//=============================================================================================================

void ScalingPanel::save(SettingsStore& store, const std::string& path) const
{
    if (path.empty()) {
        return;
    }
    for (const ScaleSpec& spec : kScales) {
        if (const auto it = m_scales.find(spec.id); it != m_scales.end()) {
            store.set_value(settings_key(path, spec.key), static_cast<double>(it->second));
        }
    }
}

//=============================================================================================================

void ScalingPanel::rebuild()
{
    const bool updating = std::exchange(m_updating, true);
    const bool linked = form().get<bool>(kLink);
    const double factor = form().get<double>(kFactor, 2.0);
    std::vector<std::string> keys;
    keys.reserve(form().fields().size());
    for (const SettingField& field : form().fields()) {
        keys.push_back(field.key);
    }
    for (const std::string& key : keys) {
        form().remove(key);
    }
    for (const ScaleSpec& spec : kScales) {
        if (!m_scales.contains(spec.id) || !shown(spec.type)) {
            continue;
        }
        SettingField field;
        field.key = spec.key;
        field.kind = SettingField::Kind::Double;
        field.label = spec.label;
        field.unit = spec.unit;
        field.default_value = spec.fallback;
        field.minimum = spec.minimum;
        field.maximum = spec.maximum;
        field.step = spec.step;
        field.decimals = spec.decimals;
        field.persistent = false;
        form().add(std::move(field));
    }
    if (form().field("scaleMAG") && form().field("scaleGRAD")) {
        SettingField link;
        link.key = kLink;
        link.kind = SettingField::Kind::Bool;
        link.label = "Link MAGs -> GRADs";
        link.default_value = linked;
        link.persistent = false;
        form().add(std::move(link));
        SettingField distance;
        distance.key = kFactor;
        distance.kind = SettingField::Kind::Double;
        distance.label = "MAG-GRAD link";
        distance.unit = " cm";
        distance.default_value = factor;
        distance.minimum = 0.1;
        distance.maximum = 8.0;
        distance.step = 0.1;
        distance.decimals = 2;
        distance.visible_if = kLink;
        distance.persistent = false;
        form().add(std::move(distance));
    }
    show_scales();
    m_updating = updating;
}

//=============================================================================================================

void ScalingPanel::show_scales()
{
    for (const auto& [id, scale] : m_scales) {
        const auto spec = std::ranges::find_if(kScales, [id](const ScaleSpec& s) { return s.id == id; });
        if (spec != kScales.end() && form().field(spec->key)) {
            form().set_value(spec->key, static_cast<double>(scale) / spec->factor);
        }
    }
}

//=============================================================================================================

bool ScalingPanel::shown(const std::string& type) const
{
    return std::ranges::any_of(m_types, [&type](const std::string& t) { return t == "all" || t == type; });
}

//=============================================================================================================

void ScalingPanel::changed(const std::string& key)
{
    const bool linked = form().get<bool>(kLink);
    const auto follow = [this](int id, double shown) {
        const ScaleSpec& spec = spec_of_id(id);
        const bool updating = std::exchange(m_updating, true);
        form().set_value(spec.key, shown);
        m_updating = updating;
        m_scales[id] = static_cast<float>(form().get<double>(spec.key) * spec.factor);
    };
    if (const ScaleSpec* spec = spec_of_key(key)) {
        m_scales[spec->id] = static_cast<float>(form().get<double>(key) * spec->factor);
        if (linked && spec->id == FIFF_UNIT_T) {
            follow(FIFF_UNIT_T_M, form().get<double>("scaleMAG") / (form().get<double>(kFactor) / 100.0));
        } else if (linked && spec->id == FIFF_UNIT_T_M) {
            follow(FIFF_UNIT_T, form().get<double>("scaleGRAD") * (form().get<double>(kFactor) / 100.0));
        }
        report();
    } else if (key == kFactor) {
        if (linked) {
            follow(FIFF_UNIT_T_M, form().get<double>("scaleMAG") / (form().get<double>(kFactor) / 100.0));
            report();
        }
    } else if (key == kLink && linked) {
        set_mag_grad_link(true);
    }
}

//=============================================================================================================

void ScalingPanel::report() const
{
    if (m_callback) {
        m_callback(m_scales);
    }
}
