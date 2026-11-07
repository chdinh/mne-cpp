//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2012-2026 MNE-CPP Authors
 *
 * @file     fiff.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Christof Pieloth <pieloth@labp.htwk-leipzig.de>;
 *           Florian Schlembach <fschlembach@web.de>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
 *           Juan GPC <jgarciaprieto@mgh.harvard.edu>;
 *           Gabriel Motta <gabrielbenmotta@gmail.com>
 * @since    0.1.0
 * @date     August 2012
 * @brief    Static MATLAB-style FIFF facade: thin wrapper functions kept for parity with the historical mne-matlab toolbox API.
 *
 * The MNE-Matlab toolbox exposes file I/O as flat functions
 * (@c fiff_open, @c fiff_read_raw_segment, @c fiff_read_evoked, ...).
 * @c FIFF mirrors that surface as a static-method facade so code ported
 * from Matlab compiles unchanged. New C++ code should call the
 * underlying class methods directly (@ref FIFFLIB::FiffStream, @ref FIFFLIB::FiffRawData,
 * @ref FIFFLIB::FiffEvoked, ...) — the wrapper only exists for the
 * backwards-compatible path.
 *
 * Including @c fiff.h is also the single-include shortcut that pulls in
 * every public FIFFLIB header in one go, which is convenient for
 * prototyping and for tools that need the full type universe.
 */

#ifndef FIFF_H
#define FIFF_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fiff_global.h"
#include "fiff_constants.h"
#include "fiff_coord_trans.h"
#include "fiff_dir_node.h"
#include "fiff_explain.h"
#include "fiff_dir_entry.h"
#include "fiff_named_matrix.h"
#include "fiff_tag.h"
#include "fiff_types.h"
#include "fiff_proj.h"
#include "fiff_ctf_comp.h"
#include "fiff_info.h"
#include "fiff_raw_data.h"
#include "fiff_raw_dir.h"
#include "fiff_stream.h"
#include "fiff_evoked_set.h"
#include "fiff_events.h"

//=============================================================================================================
// EIGEN INCLUDES
//=============================================================================================================

#include <Eigen/Core>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QIODevice>
#include <QList>
#include <QStringList>

//=============================================================================================================
// DEFINE NAMESPACE FIFFLIB
//=============================================================================================================

namespace FIFFLIB
{

//=============================================================================================================
// FORWARD DECLARATIONS
//=============================================================================================================

//=============================================================================================================
/**
 * @brief MATLAB-style static facade re-exporting every public FIFFLIB reader/writer for parity with the mne-matlab toolbox.
 *
 * Only wrappers — no state. Each method forwards to the matching method
 * on @ref FiffStream / @ref FiffRawData / @ref FiffEvoked /
 * @ref FiffEvokedSet / @ref FiffCov / @ref FiffInfo. Prefer the
 * underlying classes for new code; this class exists so Matlab-style
 * ports compile unchanged.
 *
 * @snippet ex_fiff_api/main.cpp fiff_facade_usage
 */
class FIFFSHARED_EXPORT Fiff
{
public:
    //=========================================================================================================
    /**
     * dtor
     */
    virtual ~Fiff()
    {
    }

    //Alphabetic ordered MNE Toolbox fiff_function


    //=========================================================================================================
    /**
     * fiff_dir_tree_find
     *
     * Wrapper for the FiffDirNode dir_tree_find member function
     *
     * Find nodes of the given kind from a directory tree structure
     *
     * @param[in] p_Node     The directory tree structure.
     * @param[in] p_kind     The given kind.
     *
     * @return the found nodes.
     */
    inline static std::vector<FiffDirNode::SPtr> dir_tree_find(const FiffDirNode::SPtr& p_Node, fiff_int_t p_kind)
    {
        return p_Node->dir_tree_find(p_kind);
    }

    //=========================================================================================================
    /**
     * fiff_invert_transform
     *
     * Wrapper for the static FiffCoordTrans::invert_transform function
     *
     * Invert a coordinate transformation
     *
     * @param[in] p_Transform    The transformation which should be inverted.
     *
     * @return true if succeeded, false otherwise.
     */
    inline static bool invert_transform(FiffCoordTrans& p_Transform)
    {
        return p_Transform.invert_transform();
    }

    //=========================================================================================================
    /**
     * fiff_open
     *
     * Wrapper for the FiffStream open member function
     *
     * Opens a fif file and provides the directory of tags
     *
     * @param[in] p_IODevice    A fiff IO device like a fiff QFile or QTCPSocket.
     * @param[out] p_pStream    file which is openened.
     *
     * @return true if succeeded, false otherwise.
     */
    static bool open(QIODevice& p_IODevice, std::unique_ptr<FiffStream>& p_pStream)
    {
        p_pStream = std::make_unique<FiffStream>(&p_IODevice);

        return p_pStream->open();
    }

    //=========================================================================================================
    /**
     * fiff_pick_channels
     *
     * Wrapper for the FiffInfo::pick_channels static function
     *
     * Make a selector to pick desired channels from data
     *
     * @param[in] ch_names  - The channel name list to consult.
     * @param[in] include   - Channels to include (if empty, include all available).
     * @param[in] exclude   - Channels to exclude (if empty, do not exclude any).
     * @return the selector matrix (row Vector).
     */
    inline static Eigen::RowVectorXi pick_channels(const StringList& ch_names,
                                                   const StringList& include = {},
                                                   const StringList& exclude = {})
    {
        return FiffInfo::pick_channels(ch_names, include, exclude);
    }

    //=========================================================================================================
    /**
     * fiff_pick_channels_evoked
     *
     * Wrapper for the FiffEvokedDataSet pick_channels_evoked member function
     *
     * Pick desired channels from evoked-response data
     *
     * @param[in] orig       The original data.
     * @param[in] include   - Channels to include (if empty, include all available).
     * @param[in] exclude   - Channels to exclude (if empty, do not exclude any).
     *
     * @return the desired fiff evoked data set.
     */
    inline static FiffEvokedSet pick_channels(FiffEvokedSet& orig,
                                              const StringList& include = {},
                                              const StringList& exclude = {})
    {
        return orig.pick_channels(include, exclude);
    }

    //=========================================================================================================
    /**
     * fiff_find_evoked
     *
     * Wrapper for the FiffEvokedDataSet find_evoked member function
     *
     * Find evoked data sets
     *
     * @param[out] orig   The read evoked data set.
     *
     * @return true when any set was found, false otherwise.
     */
    inline static bool find_evoked(FiffEvokedSet& orig)
    {
        return orig.find_evoked(orig);
    }

    //=========================================================================================================
    /**
     * fiff_pick_info
     *
     * Wrapper for the FiffInfo pick_info member function
     *
     * Pick desired channels from measurement info
     *
     * @param[in] info   The original data.
     * @param[in] sel    List of channels to select.
     *
     * @return Info modified according to sel.
     */
    inline static FiffInfo pick_info(const FiffInfo& info,
                                     const Eigen::RowVectorXi& sel = defaultVectorXi)
    {
        return info.pick_info(sel);
    }

    //=========================================================================================================
    /**
     * fiff_pick_types
     *
     * Wrapper for the FiffInfo pick_types member function
     *
     * Create a selector to pick desired channel types from data
     *
     * @param[in] info       The measurement info.
     * @param[in] meg        Include MEG channels.
     * @param[in] eeg        Include EEG channels.
     * @param[in] stim       Include stimulus channels.
     * @param[in] include    Additional channels to include (if empty, do not add any).
     * @param[in] exclude    Channels to exclude (if empty, do not exclude any).
     *
     * @return the selector matrix (row vector).
     */
    inline static Eigen::RowVectorXi pick_types(FiffInfo& info,
                                                bool meg,
                                                bool eeg = false,
                                                bool stim = false,
                                                const StringList& include = {},
                                                const StringList& exclude = {})
    {
        return info.pick_types(meg, eeg, stim, include, exclude);
    }

    //=========================================================================================================
    /**
     * fiff_read_raw_segment
     *
     * Wrapper for the FiffRawData read_raw_segment member function
     *
     * Read a specific raw data segment
     *
     * @param[in] raw        structure returned by fiff_setup_read_raw.
     * @param[out] data      returns the data matrix (channels x samples).
     * @param[out] times     returns the time values corresponding to the samples.
     * @param[in] from       first sample to include. If omitted, defaults to the first sample in data (optional).
     * @param[in] to         last sample to include. If omitted, defaults to the last sample in data (optional).
     * @param[in] sel        channel selection vector (optional).
     *
     * @return true if succeeded, false otherwise.
     */
    inline static bool read_raw_segment(FiffRawData& raw,
                                        Eigen::MatrixXd& data,
                                        Eigen::MatrixXd& times,
                                        fiff_int_t from = -1,
                                        fiff_int_t to = -1,
                                        Eigen::MatrixXi sel = defaultMatrixXi)
    {
        return raw.read_raw_segment(data, times, from, to, sel);
    }

    /**
     * fiff_read_raw_segment
     *
     * Wrapper for the FiffRawData read_raw_segment member function
     *
     * Read a specific raw data segment
     *
     * @param[in] raw        structure returned by fiff_setup_read_raw.
     * @param[out] data      returns the data matrix (channels x samples).
     * @param[out] times     returns the time values corresponding to the samples.
     * @param[out] mult      matrix to store used multiplication matrix (compensator,projection,calibration).
     * @param[in] from       first sample to include. If omitted, defaults to the first sample in data (optional).
     * @param[in] to         last sample to include. If omitted, defaults to the last sample in data (optional).
     * @param[in] sel        channel selection vector (optional).
     *
     * @return true if succeeded, false otherwise.
     */
    inline static bool read_raw_segment(FiffRawData& raw,
                                        Eigen::MatrixXd& data,
                                        Eigen::MatrixXd& times,
                                        Eigen::SparseMatrix<double>& mult,
                                        fiff_int_t from = -1,
                                        fiff_int_t to = -1,
                                        Eigen::MatrixXi sel = defaultMatrixXi)
    {
        return raw.read_raw_segment(data, times, mult, from, to, sel);
    }


    //=========================================================================================================
    /**
     * fiff_setup_read_raw
     *
     * Wrapper for the FiffStream::setup_read_raw static function
     *
     * Read information about raw data file
     *
     * @param[in] path               The raw data file.
     * @param[out] data              The raw data information - contains the opened fiff file.
     * @param[in] allow_maxshield    Accept unprocessed MaxShield data.
     *
     * @return true if succeeded, false otherwise.
     */
    inline static bool setup_read_raw(const std::filesystem::path& path, FiffRawData& data, bool allow_maxshield = false)
    {
        return FiffStream::setup_read_raw(path, data, allow_maxshield);
    }

    //=========================================================================================================
    /**
     * fiff_split_name_list
     *
     * Wrapper for the FiffStream::split_name_list static function
     *
     * Splits a string by looking for seperator ":"
     *
     * @param[in] p_sNameList    string to split.
     *
     * @return the splitted string list.
     */
    inline static StringList split_name_list(std::string_view p_sNameList)
    {
        return FiffStream::split_name_list(p_sNameList);
    }


    //=========================================================================================================
    /**
     * fiff_start_file
     *
     * Wrapper for the FiffStream::start_file static function
     *
     * Opens a fiff file for writing and writes the compulsory header tags
     *
     * @param[in] p_IODevice   A fiff IO device like a fiff QFile or QTCPSocket.
     *
     * @return The opened file.
     */
    inline static std::unique_ptr<FiffStream> start_file(QIODevice& p_IODevice)
    {
        return FiffStream::start_file(p_IODevice);
    }

    //=========================================================================================================
    /**
     * fiff_start_writing_raw
     *
     * Wrapper for the FiffStream::start_writing_raw static function
     *
     * function [fid,cals] = fiff_start_writing_raw(name,info,sel)
     *
     * @param[in] path           The file to create.
     * @param[in] info           The measurement info block of the source file.
     * @param[out] cals          Thecalibration matrix.
     * @param[in] sel            Which channels will be included in the output file (optional).
     *
     * @return the started fiff file.
     */
    inline static std::unique_ptr<FiffStream> start_writing_raw(const std::filesystem::path& path,
                                                                const FiffInfo& info,
                                                                Eigen::RowVectorXd& cals,
                                                                Eigen::MatrixXi sel = defaultMatrixXi)
    {
        return FiffStream::start_writing_raw(path, info, cals, sel);
    }
};

//=============================================================================================================
// INLINE DEFINITIONS
//=============================================================================================================
} // NAMESPACE

#endif // FIFF_H
