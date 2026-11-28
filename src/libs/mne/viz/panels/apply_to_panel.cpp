//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     apply_to_panel.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    ApplyToPanel class definition.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "apply_to_panel.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

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

constexpr const char* kAll = "selectAll";
constexpr const char* kClear = "clearSelected";

/** A view: its field key, label and the name its users know it by. */
struct ViewSpec
{
    const char* key;   /**< Field key. */
    const char* label; /**< Check box text. */
    const char* name;  /**< Name in selected_views(). */
};

constexpr std::array<ViewSpec, 3> kViews{{
    {"signalViewer", "Signal Viewer", "signalview"},
    {"butterflyView", "Averaging Butterfly View", "butterflyview"},
    {"layoutView", "Averaging 2D Layout View", "layoutview"},
}};

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

ApplyToPanel::ApplyToPanel()
: Panel("ApplyToView", "Apply to View")
{
    set_width(330);
    SettingsForm& f = form();
    for (const ViewSpec& view : kViews) {
        SettingField field;
        field.key = view.key;
        field.kind = SettingField::Kind::Bool;
        field.label = view.label;
        field.default_value = true;
        field.group = "Apply to View";
        field.persistent = false;
        f.add(std::move(field));
    }
    f.add_action(kAll, "Select All", "Apply to View");
    f.add_action(kClear, "Clear Selected", "Apply to View");
    m_listener = f.add_listener([this](const std::string& key, const std::optional<SettingValue>& value) {
        if (value) {
            return;
        }
        if (key == kAll) {
            select_all();
        } else if (key == kClear) {
            clear();
        }
    });
}

//=============================================================================================================

ApplyToPanel::~ApplyToPanel()
{
    form().remove_listener(m_listener);
}

//=============================================================================================================

void ApplyToPanel::select_all()
{
    for (const ViewSpec& view : kViews) {
        form().set_value(view.key, true);
    }
}

//=============================================================================================================

void ApplyToPanel::clear()
{
    for (const ViewSpec& view : kViews) {
        form().set_value(view.key, false);
    }
}

//=============================================================================================================

std::vector<std::string> ApplyToPanel::selected_views() const
{
    std::vector<std::string> views{"null"};
    views.reserve(kViews.size() + 1);
    for (const ViewSpec& view : kViews) {
        if (form().get<bool>(view.key)) {
            views.emplace_back(view.name);
        }
    }
    return views;
}
