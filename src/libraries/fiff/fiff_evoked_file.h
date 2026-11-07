//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     fiff_evoked_file.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Internal reader shared by FiffEvoked::read and FiffEvokedSet::read; not installed.
 */

#ifndef FIFF_EVOKED_FILE_H
#define FIFF_EVOKED_FILE_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fiff_dir_node.h"
#include "fiff_evoked.h"
#include "fiff_info.h"
#include "fiff_stream.h"

#include <utils/error.h>

#include <QList>
#include <QStringList>

#include <filesystem>
#include <string>

//=============================================================================================================
// DEFINE NAMESPACE FIFFLIB
//=============================================================================================================

namespace FIFFLIB
{

//=============================================================================================================
/**
 * An evoked-response file opened once, so that several of its data sets are read in one pass.
 */
class FiffEvokedFile
{
public:
    /** @return The opened file, or why it holds no evoked data. */
    static UTILSLIB::Expected<FiffEvokedFile> open(const std::filesystem::path& path);

    /** @return The number of evoked data sets. */
    qsizetype size() const
    {
        return m_sets.size();
    }

    /** @return The index of the data set selected by @p condition, or why there is none. */
    UTILSLIB::Expected<int> find(const FiffEvoked::Condition& condition, fiff_int_t kind) const;

    /** @return Data set @p set, calibrated, projected and baseline corrected as requested. */
    UTILSLIB::Expected<FiffEvoked> read(int set, const FiffEvoked::Baseline& baseline, bool proj);

    FiffInfo info; /**< The measurement info shared by all data sets. */

private:
    FiffEvokedFile(FiffStream stream, std::string file)
    : m_stream(std::move(stream))
    , m_file(std::move(file))
    {
    }

    FiffStream m_stream;
    std::string m_file;
    QList<FiffDirNode::SPtr> m_sets;
    QStringList m_comments;
    QList<fiff_int_t> m_kinds;
};

} // namespace FIFFLIB

#endif // FIFF_EVOKED_FILE_H
