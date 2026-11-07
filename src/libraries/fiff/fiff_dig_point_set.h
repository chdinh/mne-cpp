//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2016-2026 MNE-CPP Authors
 *
 * @file     fiff_dig_point_set.h
 * @author   Jana Kiesel <jana.kiesel@tu-ilmenau.de>;
 *           Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
 *           Ruben Doerfel <doerfelruben@aol.com>;
 *           Juan GPC <jgarciaprieto@mgh.harvard.edu>;
 *           Gabriel Motta <gabrielbenmotta@gmail.com>
 * @since    0.1.0
 * @date     July 2016
 * @brief    Container for the FIFF_DIG_POINT records of a measurement (a parsed FIFFB_ISOTRAK block).
 *
 * @ref FIFFLIB::FiffDigPointSet holds the head-coordinate point cloud associated
 * with one recording: cardinal fiducials, HPI coil positions, EEG
 * electrodes and the extra head-shape samples. It is what
 * @ref FIFFLIB::FiffStream returns when asked for the contents of an
 * @c FIFFB_ISOTRAK / @c FIFFB_HPI_MEAS block, and what
 * @ref FIFFLIB::FiffDigitizerData consumes when constructing a digitization
 * view for the registration GUIs. Round-trips with the @c info['dig']
 * list in MNE-Python.
 */

#ifndef FIFFLIB_FIFF_DIG_POINT_SET_H
#define FIFFLIB_FIFF_DIG_POINT_SET_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fiff_global.h"

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QSharedPointer>
#include <QStringList>

#include "fiff_stream.h"
#include "fiff_dig_point.h"

#include <utils/error.h>

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

//=============================================================================================================
// EIGEN INCLUDES
//=============================================================================================================

//=============================================================================================================
// FORWARD DECLARATIONS
//=============================================================================================================

//=============================================================================================================
// DEFINE NAMESPACE FIFFLIB
//=============================================================================================================

namespace FIFFLIB
{

//=============================================================================================================
// FIFFLIB FORWARD DECLARATIONS
//=============================================================================================================

class FiffDigPoint;
class FiffDirNode;

//=============================================================================================================
/**
 * @brief Collection of @ref FiffDigPoint records as parsed from a FIFFB_ISOTRAK block.
 *
 * A contiguous range of points with the container interface of std::vector, plus
 * the selection, transformation and file operations of mne.io.Info["dig"].
 *
 * @snippet ex_fiff_api/main.cpp fiff_dig_point_set_usage
 */

class FIFFSHARED_EXPORT FiffDigPointSet
{
public:
    using SPtr = QSharedPointer<FiffDigPointSet>;                     /**< Shared pointer type for FiffDigPointSet. */
    using UPtr = std::unique_ptr<FiffDigPointSet>;                    /**< Unique pointer type for FiffDigPointSet. */
    using value_type = FiffDigPoint;                                  /**< Element type. */
    using iterator = std::vector<FiffDigPoint>::iterator;             /**< Iterator. */
    using const_iterator = std::vector<FiffDigPoint>::const_iterator; /**< Constant iterator. */

    //=========================================================================================================
    /**
     * Constructs an empty set.
     */
    FiffDigPointSet() = default;

    //=========================================================================================================
    /**
     * Constructs a set holding @p points.
     *
     * @param[in] points   The digitizer points.
     */
    explicit FiffDigPointSet(std::vector<FiffDigPoint> points);

    //=========================================================================================================
    /**
     * Reads the digitizer points of a FIFF file, like read(); throws UTILSLIB::Error if it cannot.
     *
     * @param[in] path   The file.
     */
    explicit FiffDigPointSet(const std::filesystem::path& path);

    //=========================================================================================================
    /**
     * Reads the digitizer points of a FIFF file, like mne.io.read_info(path)["dig"].
     *
     * @param[in] path   The file.
     *
     * @return The points, or why the file cannot be read.
     */
    [[nodiscard]] static UTILSLIB::Expected<FiffDigPointSet> read(const std::filesystem::path& path);

    //=========================================================================================================
    /**
     * Reads the digitizer points of the isotrak block of a FIFF stream, opening it if needed.
     *
     * @param[in, out] stream   The stream.
     *
     * @return The points, which are none if the stream holds no isotrak block, or why the stream cannot be read.
     */
    [[nodiscard]] static UTILSLIB::Expected<FiffDigPointSet> read(FiffStream& stream);

    //=========================================================================================================
    /**
     * Reads a Polhemus Isotrak digitizer file, as mne.channels.read_dig_polhemus_isotrak does.
     *
     * The three fiducials become cardinal points (LPA, nasion, RPA). The remaining
     * points become HPI coils for an .elp file and head shape points for .hsp/.eeg
     * files, or EEG electrodes when @p chNames names them. Electrodes are numbered by
     * the last three characters of their names when all of those are numbers, and
     * 1..n otherwise. All points are in digitizer coordinates (FIFFV_COORD_UNKNOWN).
     *
     * @param[in]  path     The .elp, .hsp or .eeg file.
     * @param[out] dig      The digitizer points.
     * @param[in]  chNames  Electrode names, one per non-fiducial point, or empty.
     * @param[in]  unit     Unit of the file: "m", "cm" or "mm".
     *
     * @return false if the file cannot be read, the extension or unit is unknown, or
     *         the number of names does not match the number of points.
     */
    static bool readPolhemusIsotrak(const QString& path,
                                    FiffDigPointSet& dig,
                                    const QStringList& chNames = QStringList(),
                                    const QString& unit = QStringLiteral("m"));

    //=========================================================================================================
    /**
     * Reads the points of a Polhemus FastSCAN .txt file in metres, as
     * mne.channels.read_polhemus_fastscan does.
     *
     * @param[in]  path           The .txt file.
     * @param[out] points         The points, one per row, in digitizer coordinates.
     * @param[in]  unit           Unit of the file: "m", "cm" or "mm".
     * @param[in]  requireHeader  Reject files whose "%" header does not name FastSCAN.
     *
     * @return false if the file cannot be read, the extension, unit or header is
     *         wrong, or a row does not hold three numbers.
     */
    static bool readPolhemusFastscan(const QString& path,
                                     Eigen::MatrixX3d& points,
                                     const QString& unit = QStringLiteral("mm"),
                                     bool requireHeader = true);

    //=========================================================================================================
    /**
     * Reads a BrainVision CapTrak .bvct file, as mne.channels.read_dig_captrak does.
     *
     * The electrodes named Nasion, LPA and RPA become the cardinal points, every other
     * electrode an EEG point; millimetres are converted to metres. The points are in
     * digitizer coordinates (FIFFV_COORD_UNKNOWN), numbered as by
     * readPolhemusIsotrak; a repeated name keeps its first place and its last position.
     *
     * @param[in]  path     The .bvct file.
     * @param[out] dig      The digitizer points.
     * @param[out] chNames  If not null, the electrode names in the order of the EEG points.
     *
     * @return false if the file cannot be parsed or lacks one of the fiducials.
     */
    static bool readCaptrak(const QString& path, FiffDigPointSet& dig, QStringList* chNames = nullptr);

    //=========================================================================================================
    /**
     * Reads the coordinates.xml of an EGI MFF recording, as mne.channels.read_dig_egi does.
     *
     * Sensors of type 0 become EEG points named "EEG <number>", the reference (type 1)
     * is numbered after the electrodes before it, and the nasion and periauricular
     * points (type 2) become the cardinal points. Unlike mne.channels.read_dig_egi,
     * which keeps the file's centimetres as if they were metres, the positions are
     * converted to metres (as mne.io.read_raw_egi does).
     *
     * @param[in]  path     The coordinates.xml file.
     * @param[out] dig      The digitizer points, in digitizer coordinates.
     * @param[out] chNames  If not null, the electrode names in the order of the EEG points.
     *
     * @return false if the file cannot be parsed or lacks one of the fiducials.
     */
    static bool readEgi(const QString& path, FiffDigPointSet& dig, QStringList* chNames = nullptr);

    //=========================================================================================================
    /**
     * Reads a Localite .csv file ("#,name,x,y,z" in millimetres after a header row), as
     * mne.channels.read_dig_localite does.
     *
     * @param[in]  path     The .csv file.
     * @param[out] dig      The digitizer points, in digitizer coordinates.
     * @param[out] chNames  If not null, the electrode names in the order of the EEG points.
     * @param[in]  nasion   Name of the point that is the nasion, or empty.
     * @param[in]  lpa      Name of the point that is the left preauricular point, or empty.
     * @param[in]  rpa      Name of the point that is the right preauricular point, or empty.
     *
     * @return false if the file cannot be read, a row is malformed or a named fiducial is missing.
     */
    static bool readLocalite(const QString& path,
                             FiffDigPointSet& dig,
                             QStringList* chNames = nullptr,
                             const QString& nasion = QString(),
                             const QString& lpa = QString(),
                             const QString& rpa = QString());

    //=========================================================================================================
    /**
     * Reads a Neuroscan .dat electrode file ("name number x y z" per line), as
     * mne.channels.read_dig_dat does: point numbers 78, 76 and 82 are the nasion,
     * LPA and RPA, 67 (the centroid) is skipped, and the coordinates are kept as they are.
     *
     * @param[in]  path     The .dat file.
     * @param[out] dig      The digitizer points, in digitizer coordinates.
     * @param[out] chNames  If not null, the electrode names in the order of the EEG points.
     *
     * @return false if the file cannot be read or a line does not hold five entries.
     */
    static bool readNeuroscanDat(const QString& path, FiffDigPointSet& dig, QStringList* chNames = nullptr);

    /** @return Whether the set holds no point. */
    bool empty() const noexcept
    {
        return m_points.empty();
    }

    /** @return The number of points. */
    std::size_t size() const noexcept
    {
        return m_points.size();
    }

    /** Removes every point. */
    void clear() noexcept
    {
        m_points.clear();
    }

    /** Appends a point. */
    void push_back(const FiffDigPoint& point)
    {
        m_points.push_back(point);
    }

    /** @return The first point; the set must not be empty. */
    const FiffDigPoint& front() const
    {
        return m_points.front();
    }

    /** @return The last point; the set must not be empty. */
    const FiffDigPoint& back() const
    {
        return m_points.back();
    }

    /** @return Point @p idx; @p idx must be below size(). */
    const FiffDigPoint& operator[](std::size_t idx) const
    {
        return m_points[idx];
    }

    /** @copydoc operator[](std::size_t) const */
    FiffDigPoint& operator[](std::size_t idx)
    {
        return m_points[idx];
    }

    iterator begin() noexcept
    {
        return m_points.begin();
    } /**< @return First point. */
    iterator end() noexcept
    {
        return m_points.end();
    } /**< @return Past the last point. */
    const_iterator begin() const noexcept
    {
        return m_points.begin();
    } /**< @return First point. */
    const_iterator end() const noexcept
    {
        return m_points.end();
    } /**< @return Past the last point. */

    /** @return The points, contiguous in memory. */
    std::span<const FiffDigPoint> points() const noexcept
    {
        return m_points;
    }

    //=========================================================================================================
    /**
     * Writes the points to a FIFF file, replacing it, like mne.io.write_info writes info["dig"].
     *
     * @param[in] path   The file.
     *
     * @return Nothing, or why the file cannot be written.
     */
    [[nodiscard]] UTILSLIB::Expected<void> write(const std::filesystem::path& path) const;

    //=========================================================================================================
    /**
     * Writes the points as an isotrak block inside a measurement info block.
     *
     * @param[in, out] stream   The stream.
     */
    void write_to_stream(FiffStream& stream) const;

    //=========================================================================================================
    /**
     * @param[in] kinds   Point kinds to keep: FIFFV_POINT_CARDINAL, FIFFV_POINT_HPI, FIFFV_POINT_EEG,
     *                    FIFFV_POINT_EXTRA or FIFFV_POINT_ECG.
     *
     * @return The points of those kinds, in their order in this set.
     */
    FiffDigPointSet pick_types(std::span<const int> kinds) const;

    /** @copydoc pick_types(std::span<const int>) const */
    FiffDigPointSet pick_types(std::initializer_list<int> kinds) const
    {
        return pick_types(std::span<const int>(kinds.begin(), kinds.size()));
    }

    //=========================================================================================================
    /**
     * Applies a coordinate transformation to every point.
     *
     * @param[in] coordTrans   The transformation.
     * @param[in] inverse      Apply its inverse instead.
     */
    void apply_transform(const FiffCoordTrans& coordTrans, bool inverse = false);

    //=========================================================================================================
    /**
     * Moves every point that shares the frame of the LPA, nasion and RPA points into the Neuromag head
     * frame defined by these fiducials (mne.channels.transform_to_head).
     *
     * @return false if a fiducial is missing or the fiducials are in different frames.
     */
    bool transform_to_head();

private:
    std::vector<FiffDigPoint> m_points;
};

} // namespace FIFFLIB

#ifndef metatype_fiffdigpointset
#define metatype_fiffdigpointset
Q_DECLARE_METATYPE(FIFFLIB::FiffDigPointSet); /**< Provides QT META type declaration of the FIFFLIB::FiffDigPointSet type. For signal/slot usage.*/
#endif

#ifndef metatype_fiffdigpointset_sptr
#define metatype_fiffdigpointset_sptr
Q_DECLARE_METATYPE(FIFFLIB::FiffDigPointSet::SPtr); /**< Provides QT META type declaration of the FIFFLIB::FiffDigPointSet::SPtr type. For signal/slot usage.*/
#endif

#endif // FIFFLIB_FIFF_DIG_POINT_SET_H
