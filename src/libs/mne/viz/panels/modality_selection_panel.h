//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     modality_selection_panel.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    3.0.0
 * @date     November 2026
 * @brief    ModalitySelectionPanel switches the channel modalities a plot shows on and off.
 */

#ifndef VIZ_MODALITY_SELECTION_PANEL_H
#define VIZ_MODALITY_SELECTION_PANEL_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../viz_global.h"
#include "panel.h"
#include <mne/fiff/fiff_ch_info.h>

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
 * @brief Tool panel with a check box per channel modality (MAG, GRAD, EEG, EOG, STIM, MISC) of a recording.
 *
 * The modality map holds every modality, also those the recording lacks; the form offers those it has.
 *
 * @since 3.0.0
 */
class VIZSHARED_EXPORT ModalitySelectionPanel : public Panel
{
public:
    //=========================================================================================================
    /**
     * Constructs the panel with all modalities on.
     *
     * @param[in] channels   Channels of the recording; their modalities get a check box.
     */
    explicit ModalitySelectionPanel(const std::vector<FiffChInfo>& channels = {});

    ~ModalitySelectionPanel() override;

    //=========================================================================================================
    /**
     * @param[in] callback   Called with the modality map when the user switched a modality.
     */
    void set_callback(std::function<void(const std::map<std::string, bool>& modalities)> callback)
    {
        m_callback = std::move(callback);
    }

    //=========================================================================================================
    /**
     * @return Whether each modality is shown, by name ("MAG", "GRAD", ...).
     */
    const std::map<std::string, bool>& modality_map() const
    {
        return m_modalities;
    }

    //=========================================================================================================
    /**
     * Replaces the modality map, without a report.
     *
     * @param[in] modalities   Whether each modality is shown, by name.
     */
    void set_modality_map(const std::map<std::string, bool>& modalities);

    //=========================================================================================================
    /**
     * @return The modalities the recording has, in the order of its channels.
     */
    const std::vector<std::string>& channel_modalities() const
    {
        return m_present;
    }

    //=========================================================================================================
    /**
     * Reads the modalities stored under the keys of ModalitySelectionView ("modalityMAG", ...).
     *
     * @param[in] store   Settings store.
     * @param[in] path    Settings path.
     */
    void load(const SettingsStore& store, const std::string& path) override;

    //=========================================================================================================
    /**
     * Stores the modalities under the keys of ModalitySelectionView.
     *
     * @param[in] store   Settings store.
     * @param[in] path    Settings path.
     */
    void save(SettingsStore& store, const std::string& path) const override;

private:
    void rebuild();

    std::vector<std::string> m_present;                                 /**< Modalities of the recording. */
    std::map<std::string, bool> m_modalities;                           /**< Whether each modality is shown. */
    std::function<void(const std::map<std::string, bool>&)> m_callback; /**< Called on changes. */
    int m_listener = 0;                                                 /**< Listener on the form. */
    bool m_updating = false;                                            /**< Whether the panel changes the form itself. */
};

} // namespace mne::viz

#endif // VIZ_MODALITY_SELECTION_PANEL_H
