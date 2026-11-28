//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     scaling_panel.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    ScalingPanel sets the amplitude scale per channel type.
 */

#ifndef VIZ_SCALING_PANEL_H
#define VIZ_SCALING_PANEL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../viz_global.h"
#include "panel.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE mne::viz
//=============================================================================================================

namespace mne::viz
{

//=============================================================================================================
/**
 * @brief Tool panel of the amplitude scales of data plots, one per channel type.
 *
 * The scales are kept by channel kind (FIFFV_EEG_CH, ...), magnetometers and gradiometers by unit
 * (FIFF_UNIT_T, FIFF_UNIT_T_M), in SI units, as viz::channel_scale() reads them. The form shows them in
 * readable units (pT, fT/cm, uV, mV). Optionally the gradiometer scale follows the magnetometer scale and
 * vice versa, linked by a factor.
 *
 * @since 3.0.0
 */
class VIZSHARED_EXPORT ScalingPanel : public Panel
{
public:
    //=========================================================================================================
    /**
     * Constructs the panel with the default scales of MEG, EEG, EOG, ECG, STIM and MISC channels.
     *
     * @param[in] channel_types   Channel types to show scales for, as named by FiffInfo::get_channel_types()
     *                            ("mag", "grad", "eeg", "eog", "ecg", "emg", "stim", "misc"); "all" shows all.
     */
    explicit ScalingPanel(std::vector<std::string> channel_types = {"all"});

    ~ScalingPanel() override;

    //=========================================================================================================
    /**
     * @param[in] callback   Called with all scales when the user changed one.
     */
    void set_callback(std::function<void(const std::map<int, float>& scales)> callback)
    {
        m_callback = std::move(callback);
    }

    //=========================================================================================================
    /**
     * @return The scales by channel kind or unit, in SI units.
     */
    const std::map<int, float>& scale_map() const
    {
        return m_scales;
    }

    //=========================================================================================================
    /**
     * Replaces the scales, without a report; the form shows the shown channel types among them.
     *
     * @param[in] scales   Scales by channel kind or unit, in SI units.
     */
    void set_scale_map(const std::map<int, float>& scales);

    //=========================================================================================================
    /**
     * Links the gradiometer to the magnetometer scale, without a report. The link factor is set from the
     * current scales.
     *
     * @param[in] linked   Whether a change of either scale changes the other.
     */
    void set_mag_grad_link(bool linked);

    //=========================================================================================================
    /**
     * Reads the scales stored under the keys of ScalingView ("scaleMAG", "scaleGRAD", ...).
     *
     * @param[in] store   Settings store.
     * @param[in] path    Settings path.
     */
    void load(const SettingsStore& store, const std::string& path) override;

    //=========================================================================================================
    /**
     * Stores the scales under the keys of ScalingView.
     *
     * @param[in] store   Settings store.
     * @param[in] path    Settings path.
     */
    void save(SettingsStore& store, const std::string& path) const override;

private:
    void rebuild();
    void show_scales();
    bool shown(const std::string& type) const;
    void changed(const std::string& key);
    void report() const;

    std::vector<std::string> m_types;                            /**< Channel types shown. */
    std::map<int, float> m_scales;                               /**< Scales by channel kind or unit. */
    std::function<void(const std::map<int, float>&)> m_callback; /**< Called on changes. */
    int m_listener = 0;                                          /**< Listener on the form. */
    bool m_updating = false;                                     /**< Whether the panel changes the form itself. */
};

} // namespace mne::viz

#endif // VIZ_SCALING_PANEL_H
