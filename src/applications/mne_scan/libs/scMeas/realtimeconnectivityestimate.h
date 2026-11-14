//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2016-2026 MNE-CPP Authors
 *
 * @file     realtimeconnectivityestimate.h
 * @author   Gabriel Motta <gabrielbenmotta@gmail.com>;
 *           Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>
 * @since    0.1.0
 * @date     October, 2016
 * @brief    Contains the declaration of the RealTimeConnectivityEstimate class.
 */

#ifndef REALTIMECONNECTIVITYESTIMATE_H
#define REALTIMECONNECTIVITYESTIMATE_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "scmeas_global.h"
#include <memory>
#include "measurement.h"

//=============================================================================================================
// FORWARD DECLARATIONS
//=============================================================================================================

namespace CONNECTIVITYLIB
{
class Network;
}

namespace FIFFLIB
{
class FiffInfo;
}

namespace MNELIB
{
class MNEForwardSolution;
}

namespace MNELIB
{
class MNEBem;
}

namespace FSLIB
{
class FsSurfaceSet;
class FsAnnotationSet;
}

//=============================================================================================================
// DEFINE NAMESPACE SCMEASLIB
//=============================================================================================================

namespace SCMEASLIB
{

//=========================================================================================================
/**
 * RealTimeConnectivityEstimate
 *
 * @brief Real-time source estimate measurement.
 */
class SCMEASSHARED_EXPORT RealTimeConnectivityEstimate : public Measurement
{
public:
    typedef QSharedPointer<RealTimeConnectivityEstimate> SPtr;            /**< Shared pointer type for RealTimeConnectivityEstimate. */
    typedef QSharedPointer<const RealTimeConnectivityEstimate> ConstSPtr; /**< Const shared pointer type for RealTimeConnectivityEstimate. */

    //=========================================================================================================
    /**
     * Constructs a RealTimeConnectivityEstimate.
     *
     * @param[in] parent     the QObject parent of this measurement.
     */
    RealTimeConnectivityEstimate(QObject* parent = 0);

    //=========================================================================================================
    /**
     * Destroys the RealTimeConnectivityEstimate.
     */
    virtual ~RealTimeConnectivityEstimate();

    //=========================================================================================================
    /**
     * Sets the annotation set.
     *
     * @param[in] annotSet   the annotation set to set.
     */
    inline void setAnnotSet(const std::shared_ptr<FSLIB::FsAnnotationSet>& annotSet);

    //=========================================================================================================
    /**
     * Returns the annotation set.
     *
     * @return the annotation set.
     */
    inline std::shared_ptr<FSLIB::FsAnnotationSet>& getAnnotSet();

    //=========================================================================================================
    /**
     * Sets the sensor surface.
     *
     * @param[in] sensorSurface   the sensor surface.
     */
    inline void setSensorSurface(const std::shared_ptr<MNELIB::MNEBem>& sensorSurface);

    //=========================================================================================================
    /**
     * Returns the sensor surface.
     *
     * @return the sensor surfac.
     */
    inline std::shared_ptr<MNELIB::MNEBem>& getSensorSurface();

    //=========================================================================================================
    /**
     * Sets the sensor surface.
     *
     * @param[in] surfSet   the surface set to set.
     */
    inline void setSurfSet(const std::shared_ptr<FSLIB::FsSurfaceSet>& surfSet);

    //=========================================================================================================
    /**
     * Returns the surface set.
     *
     * @return the surface set.
     */
    inline std::shared_ptr<FSLIB::FsSurfaceSet>& getSurfSet();

    //=========================================================================================================
    /**
     * Sets the forward solution.
     *
     * @param[in] fwdSolution   the forward solution to set.
     */
    inline void setFwdSolution(const std::shared_ptr<MNELIB::MNEForwardSolution>& fwdSolution);

    //=========================================================================================================
    /**
     * Returns the forward solution.
     *
     * @return the forward solution.
     */
    inline std::shared_ptr<MNELIB::MNEForwardSolution>& getFwdSolution();

    //=========================================================================================================
    /**
     * Attaches a value to the sample array vector.
     * This method is inherited by Measurement.
     *
     * @param[in] v the value which is attached to the sample array vector.
     */
    virtual void setValue(const CONNECTIVITYLIB::Network& v);

    //=========================================================================================================
    /**
     * Returns the current value set.
     * This method is inherited by Measurement.
     *
     * @return the last attached value.
     */
    virtual QSharedPointer<CONNECTIVITYLIB::Network>& getValue();

    //=========================================================================================================
    /**
     * Returns whether RealTimeConnectivityEstimate contains values
     *
     * @return whether RealTimeConnectivityEstimate contains values.
     */
    inline bool isInitialized() const;

    //=========================================================================================================
    /**
     * Sets the current FiffInfo.
     *
     * @param[in] p_fiffInfo the new FiffInfo..
     */
    void setFiffInfo(const std::shared_ptr<FIFFLIB::FiffInfo>& p_fiffInfo);

    //=========================================================================================================
    /**
     * Returns the current FiffInfo.
     *
     * @return the current FiffInfo.
     */
    std::shared_ptr<FIFFLIB::FiffInfo> getFiffInfo();

private:
    mutable QMutex m_qMutex; /**< Mutex to ensure thread safety. */

    std::shared_ptr<FIFFLIB::FiffInfo> m_pFiffInfo; /**< The Fiff info. */

    std::shared_ptr<FSLIB::FsAnnotationSet> m_pAnnotSet;        /**< FsAnnotation set. Needed for visualization. */
    std::shared_ptr<FSLIB::FsSurfaceSet> m_pSurfSet;            /**< FsSurface set. Needed for visualization. */
    std::shared_ptr<MNELIB::MNEForwardSolution> m_pFwdSolution; /**< Forward solution. Needed for visualization. */
    std::shared_ptr<MNELIB::MNEBem> m_pSensorSurface;           /**< The sensor surface. Needed for visualization. */

    QSharedPointer<CONNECTIVITYLIB::Network> m_pNetwork; /**< The network/connectivity estimate. */
    bool m_bInitialized;                                 /**< Is initialized. */
};

//=============================================================================================================
// INLINE DEFINITIONS
//=============================================================================================================

inline void RealTimeConnectivityEstimate::setAnnotSet(const std::shared_ptr<FSLIB::FsAnnotationSet>& annotSet)
{
    QMutexLocker locker(&m_qMutex);
    m_pAnnotSet = annotSet;
}

//=============================================================================================================

inline std::shared_ptr<FSLIB::FsAnnotationSet>& RealTimeConnectivityEstimate::getAnnotSet()
{
    QMutexLocker locker(&m_qMutex);
    return m_pAnnotSet;
}

//=============================================================================================================

inline void RealTimeConnectivityEstimate::setSensorSurface(const std::shared_ptr<MNELIB::MNEBem>& annotSet)
{
    QMutexLocker locker(&m_qMutex);
    m_pSensorSurface = annotSet;
}

//=============================================================================================================

inline std::shared_ptr<MNELIB::MNEBem>& RealTimeConnectivityEstimate::getSensorSurface()
{
    QMutexLocker locker(&m_qMutex);
    return m_pSensorSurface;
}

//=============================================================================================================

inline void RealTimeConnectivityEstimate::setSurfSet(const std::shared_ptr<FSLIB::FsSurfaceSet>& surfSet)
{
    QMutexLocker locker(&m_qMutex);
    m_pSurfSet = surfSet;
}

//=============================================================================================================

inline std::shared_ptr<FSLIB::FsSurfaceSet>& RealTimeConnectivityEstimate::getSurfSet()
{
    QMutexLocker locker(&m_qMutex);
    return m_pSurfSet;
}

//=============================================================================================================

inline void RealTimeConnectivityEstimate::setFwdSolution(const std::shared_ptr<MNELIB::MNEForwardSolution>& fwdSolution)
{
    QMutexLocker locker(&m_qMutex);
    m_pFwdSolution = fwdSolution;
}

//=============================================================================================================

inline std::shared_ptr<MNELIB::MNEForwardSolution>& RealTimeConnectivityEstimate::getFwdSolution()
{
    QMutexLocker locker(&m_qMutex);
    return m_pFwdSolution;
}

//=============================================================================================================

inline bool RealTimeConnectivityEstimate::isInitialized() const
{
    QMutexLocker locker(&m_qMutex);
    return m_bInitialized;
}

//=============================================================================================================

inline void RealTimeConnectivityEstimate::setFiffInfo(const std::shared_ptr<FIFFLIB::FiffInfo>& p_fiffInfo)
{
    QMutexLocker locker(&m_qMutex);
    m_pFiffInfo = p_fiffInfo;
}

//=============================================================================================================

inline std::shared_ptr<FIFFLIB::FiffInfo> RealTimeConnectivityEstimate::getFiffInfo()
{
    QMutexLocker locker(&m_qMutex);
    return m_pFiffInfo;
}
} // NAMESPACE

#endif // REALTIMECONNECTIVITYESTIMATE_H
