//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     apply_to_panel.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    ApplyToPanel chooses the views that settings apply to.
 */

#ifndef VIZ_APPLY_TO_PANEL_H
#define VIZ_APPLY_TO_PANEL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../viz_global.h"
#include "panel.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <string>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE mne::viz
//=============================================================================================================

namespace mne::viz
{

//=============================================================================================================
/**
 * @brief Tool panel with a check box per view (signal viewer, averaging butterfly and 2D layout view) that
 * settings changed elsewhere apply to, and buttons to tick all or none.
 *
 * @since 3.0.0
 */
class VIZSHARED_EXPORT ApplyToPanel : public Panel
{
public:
    //=========================================================================================================
    /**
     * Constructs the panel with all views ticked.
     */
    ApplyToPanel();

    ~ApplyToPanel() override;

    //=========================================================================================================
    /**
     * Ticks all views.
     */
    void select_all();

    //=========================================================================================================
    /**
     * Unticks all views.
     */
    void clear();

    //=========================================================================================================
    /**
     * @return "null", followed by the names of the ticked views: "signalview", "butterflyview", "layoutview".
     */
    std::vector<std::string> selected_views() const;

private:
    int m_listener = 0; /**< Listener on the form. */
};

} // namespace mne::viz

#endif // VIZ_APPLY_TO_PANEL_H
