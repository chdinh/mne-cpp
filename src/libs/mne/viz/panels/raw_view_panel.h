//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     raw_view_panel.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    RawViewPanel sets how a raw data view looks: channels shown, time window, spacers and colours.
 */

#ifndef VIZ_RAW_VIEW_PANEL_H
#define VIZ_RAW_VIEW_PANEL_H

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
 * @brief Tool panel of a raw or evoked data view: the number of channels shown at once, the time window, the
 * distance of the time spacers, the signal and background colours and a screenshot button.
 *
 * The settings keys are those of FiffRawViewSettings, so stored settings carry over.
 *
 * @since 3.0.0
 */
class VIZSHARED_EXPORT RawViewPanel : public Panel
{
public:
    /** What the panel reports. */
    struct Callbacks
    {
        std::function<void(double channels)> zoom_changed;                    /**< The number of channels shown changed. */
        std::function<void(int seconds)> window_size_changed;                 /**< The time window changed. */
        std::function<void(int milliseconds)> time_spacer_changed;            /**< The distance of the time spacers changed. */
        std::function<void(const Color& color)> signal_color_changed;         /**< The signal colour changed. */
        std::function<void(const Color& color)> background_color_changed;     /**< The background colour changed. */
        std::function<void(const std::string& image_type)> screenshot_wanted; /**< The user asked for a screenshot ("PNG" or "SVG"). */
    };

    //=========================================================================================================
    /**
     * Constructs the panel: 16 channels, 10 s, spacers every 1000 ms, blue on white.
     *
     * @param[in] shown   Controls to show, by the names FiffRawViewSettings::setWidgetList() took:
     *                    "numberChannels", "windowSize", "distanceSpacers", "backgroundColor", "signalColor"
     *                    and "screenshot"; none shows all. Hidden settings keep their stored values.
     */
    explicit RawViewPanel(const std::vector<std::string>& shown = {});

    ~RawViewPanel() override;

    //=========================================================================================================
    /**
     * @param[in] callbacks   What to call on changes.
     */
    void set_callbacks(Callbacks callbacks)
    {
        m_callbacks = std::move(callbacks);
    }

    /** @return The number of channels shown at once. */
    double zoom() const;

    /** @param[in] channels   Number of channels shown at once, without a report. */
    void set_zoom(double channels);

    /** @return The time window in seconds. */
    int window_size() const;

    /** @param[in] seconds   Time window, without a report. */
    void set_window_size(int seconds);

    /** @return The distance of the time spacers in milliseconds. */
    int time_spacer() const;

    /** @param[in] milliseconds   Distance of the time spacers (100, 200, 500 or 1000), without a report. */
    void set_time_spacer(int milliseconds);

    /** @return The signal colour. */
    Color signal_color() const;

    /** @param[in] color   Signal colour, without a report. */
    void set_signal_color(const Color& color);

    /** @return The background colour. */
    Color background_color() const;

    /** @param[in] color   Background colour, without a report. */
    void set_background_color(const Color& color);

    /** @return The image type of screenshots, "PNG" or "SVG". */
    std::string image_type() const;

    //=========================================================================================================
    /**
     * Restores the stored settings without reports.
     *
     * @param[in] store   Settings store.
     * @param[in] path    Settings path.
     */
    void load(const SettingsStore& store, const std::string& path) override;

private:
    void changed(const std::string& key);
    void set_silently(const std::string& key, const SettingValue& value);

    Callbacks m_callbacks;   /**< What to call on changes. */
    int m_listener = 0;      /**< Listener on the form. */
    bool m_updating = false; /**< Whether the panel changes the form itself. */
};

} // namespace mne::viz

#endif // VIZ_RAW_VIEW_PANEL_H
