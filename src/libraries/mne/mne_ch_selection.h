//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     mne_ch_selection.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Named subset of channels (e.g. @c "Left-temporal") loaded from an MNE @c .sel file.
 *
 * @ref MNELIB::MNEChSelection represents one entry of the channel
 * selection files shipped with the MNE C tools (per-region groupings of
 * MEG / EEG channels). Used by browsers and analyzers to drive
 * topographic layouts and quick channel subsets.
 */

#ifndef MNE_CH_SELECTION_H
#define MNE_CH_SELECTION_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "mne_global.h"

#include <Eigen/Core>

#include <span>
#include <string>
#include <vector>

namespace FIFFLIB
{
class FiffChInfo;
}

//=============================================================================================================
// DEFINE NAMESPACE MNELIB
//=============================================================================================================

namespace MNELIB
{

class MNEDeriv;

/**
 * Channel selection.
 *
 * @snippet ex_mne_api/main.cpp mne_raw_data_usage
 */
class MNESHARED_EXPORT MNEChSelection
{
public:
    MNEChSelection() = default;
    ~MNEChSelection() = default;

    //=========================================================================================================
    /**
     * Create a user selection of the given channel names (MNE-C @c mne_ch_selection_these).
     *
     * @param[in] selname  Name of the selection.
     * @param[in] names    Channel names.
     *
     * @return The selection; call assign_channels() before picking with it.
     */
    static MNEChSelection these(std::string selname, std::span<const std::string> names);

    //=========================================================================================================
    /**
     * Match the defined channels against the channels of a recording
     * (MNE-C @c mne_ch_selection_assign_chs and @c mne_ch_selection_assign_chs_info).
     *
     * A definition matches a channel name ignoring case, with or without the
     * definition's own spaces, and also the part of a channel name before a dash.
     * Definitions without such a channel are looked up among the valid derived channels.
     *
     * @param[in] chs     Channels of the recording.
     * @param[in] deriv   Derivations matched to the recording, or nullptr.
     *
     * @return The number of definitions matched to a recorded channel (derivations excluded).
     */
    int assign_channels(std::span<const FIFFLIB::FiffChInfo> chs, const MNEDeriv* deriv = nullptr);

    //=========================================================================================================
    /**
     * @return The number of picked channels (entries of @ref chspick).
     */
    int nchan() const
    {
        return static_cast<int>(chspick.size());
    }

    std::string name;           /**< Name of this selection. */
    StringList chdef;           /**< Channel definitions (may contain regular expressions). */
    StringList chspick;         /**< Translated into channel names using the present data. */
    StringList chspick_nospace; /**< The same without spaces. */
    Eigen::VectorXi pick;       /**< Corresponding channels in raw data (< 0 indicates missing). */
    Eigen::VectorXi pick_deriv; /**< Corresponding derivations in raw data. */
    int nderiv = 0;             /**< How many derivations in the above. */
    Eigen::VectorXi ch_kind;    /**< Kinds of the channels corresponding to picks. */
    int kind = 0;               /**< Loaded from file or created here? */
};

} // namespace MNELIB

#endif // MNE_CH_SELECTION_H
