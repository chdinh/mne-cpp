//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     average_selection_panel.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    AverageSelectionPanel shows or hides each average and sets its colour.
 */

#ifndef VIZ_AVERAGE_SELECTION_PANEL_H
#define VIZ_AVERAGE_SELECTION_PANEL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../viz_global.h"
#include "../figures/style.h"
#include "panel.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <functional>
#include <map>
#include <string>
#include <utility>

//=============================================================================================================
// DEFINE NAMESPACE mne::viz
//=============================================================================================================

namespace mne::viz
{

//=============================================================================================================
/**
 * @brief Tool panel of the averages of an evoked set: a check box and a colour per average (at most ten).
 *
 * The panel lists the averages once their colours and activations name the same averages.
 *
 * @since 3.0.0
 */
class VIZSHARED_EXPORT AverageSelectionPanel : public Panel
{
public:
    /** Most averages the form lists. */
    static constexpr int kMaxAverages = 10;

    /** What the panel reports. */
    struct Callbacks
    {
        std::function<void(const std::map<std::string, Color>& colors)> colors_changed;        /**< The user changed a colour. */
        std::function<void(const std::map<std::string, bool>& activation)> activation_changed; /**< The user showed or hid an average. */
    };

    //=========================================================================================================
    /**
     * Constructs the panel without averages.
     */
    AverageSelectionPanel();

    ~AverageSelectionPanel() override;

    //=========================================================================================================
    /**
     * @param[in] callbacks   What to call on changes.
     */
    void set_callbacks(Callbacks callbacks)
    {
        m_callbacks = std::move(callbacks);
    }

    //=========================================================================================================
    /**
     * @return The colour of each average, by name.
     */
    const std::map<std::string, Color>& average_colors() const
    {
        return m_colors;
    }

    //=========================================================================================================
    /**
     * @return Whether each average is shown, by name.
     */
    const std::map<std::string, bool>& average_activation() const
    {
        return m_activation;
    }

    //=========================================================================================================
    /**
     * Replaces the colours, without a report.
     *
     * @param[in] colors   Colour of each average, by name.
     */
    void set_average_colors(const std::map<std::string, Color>& colors);

    //=========================================================================================================
    /**
     * Replaces the activations, without a report.
     *
     * @param[in] activation   Whether each average is shown, by name.
     */
    void set_average_activation(const std::map<std::string, bool>& activation);

    //=========================================================================================================
    /**
     * Reads the colours and activations stored by AverageSelectionView (groups "averageColorMap" and
     * "averageActivationMap"); needs a store that lists its keys.
     *
     * @param[in] store   Settings store.
     * @param[in] path    Settings path.
     */
    void load(const SettingsStore& store, const std::string& path) override;

    //=========================================================================================================
    /**
     * Stores the colours and activations as AverageSelectionView did.
     *
     * @param[in] store   Settings store.
     * @param[in] path    Settings path.
     */
    void save(SettingsStore& store, const std::string& path) const override;

private:
    void rebuild();
    void changed(const std::string& key);

    std::map<std::string, Color> m_colors;    /**< Colour of each average. */
    std::map<std::string, bool> m_activation; /**< Whether each average is shown. */
    Callbacks m_callbacks;                    /**< What to call on changes. */
    int m_listener = 0;                       /**< Listener on the form. */
    bool m_updating = false;                  /**< Whether the panel changes the form itself. */
};

} // namespace mne::viz

#endif // VIZ_AVERAGE_SELECTION_PANEL_H
