//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     polhemus_connection.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.2.1
 * @date     May 2026
 * @brief    Polhemus digitizer connection abstraction.
 *
 *           Two backends share one signal interface:
 *
 *           1. **Mock** — emits a scripted sweep over a 10 cm sphere
 *              every @c kMockTickIntervalMs. Always available so apps
 *              and tests run on machines without a real digitizer.
 *
 *           2. **Fastrak / FastSCAN** — opens a `QSerialPort`, sends
 *              the continuous-output command, and decodes the ASCII
 *              record stream via @ref FastrakParser.
 *
 *           Backend selection is implicit: an empty @c portName picks
 *           mock; a non-empty name picks the hardware backend.
 *           @ref autoDetectPortName scans @ref availablePorts for a
 *           likely Fastrak (FTDI USB-serial) and returns it, so the UI
 *           can offer a one-click "connect" without asking the user to
 *           type a device path.
 */

#ifndef ACQ_POLHEMUS_CONNECTION_H
#define ACQ_POLHEMUS_CONNECTION_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_polhemus_global.h"
#include "fastrak_parser.h"

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QMap>
#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QString>
#include <QStringList>
#include <QTimer>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB
//=============================================================================================================

namespace ACQLIB
{

//=============================================================================================================
/**
 * @brief Settings used to open a real Polhemus serial connection.
 *
 * @snippet ex_acq_polhemus/main.cpp polhemus_coregistration_usage
 */
struct ACQPOLHEMUSSHARED_EXPORT PolhemusSerialConfig
{
    int baudRate = 115200;
    FastrakParser::Units units = FastrakParser::Units::Inches;
    /** Hemisphere vector for all stations. The Fastrak tracks sensors
     *  within the half-space defined by this direction from the transmitter.
     *  Example: (0,0,1) = +Z (superior), (0,0,-1) = -Z (inferior).
     *  Set to a non-zero vector to send Fastrak "H<n>,x,y,z\r" for
     *  stations 1-4 at connect time. Default (0,0,0) = don't change. */
    Eigen::Vector3f hemisphere = Eigen::Vector3f::Zero();
    /** Stream control command sent right after opening the port. The
     *  factory default for Fastrak is `"C\r"` (continuous ASCII output);
     *  use `"P\r"` if the application drives polled mode itself. */
    QByteArray streamCommand = QByteArrayLiteral("C\r");
};

//=============================================================================================================
/**
 * @brief Polhemus digitizer connection (mock + serial-port backends).
 *
 * @snippet ex_acq_polhemus/main.cpp polhemus_coregistration_usage
 */
class ACQPOLHEMUSSHARED_EXPORT PolhemusConnection : public QObject
{
    Q_OBJECT

public:
    explicit PolhemusConnection(QObject* parent = nullptr);
    ~PolhemusConnection() override;

    /**
     * Open the connection.
     *
     * @param portName  Empty → mock backend. Non-empty → hardware backend.
     * @param cfg       Serial transport configuration; ignored by the
     *                  mock backend.
     *
     * @return True if connected (or already connected), false if the backend failed to open.
     */
    bool open(const QString& portName,
              const PolhemusSerialConfig& cfg = PolhemusSerialConfig{});

    /** Close the active connection. Safe to call when already closed. */
    void close();

    bool isConnected() const;
    QString backendName() const;

    //=========================================================================================================
    /**
     * @brief Enumerate every serial port currently visible to the OS.
     *
     * Convenience wrapper around @c QSerialPortInfo::availablePorts that
     * returns just the system port names (e.g. `/dev/cu.usbserial-AB0`,
     * `COM3`) so callers can populate a combo box without pulling in
     * `QSerialPortInfo` themselves.
     *
     * @return System port names of all visible serial ports; empty if none.
     */
    static QStringList availablePorts();

    //=========================================================================================================
    /**
     * @brief Best-effort auto-detection of an attached Fastrak/FastSCAN.
     *
     * Polhemus Fastrak/FastSCAN units ship with an FTDI USB-serial
     * bridge (vendor id 0x0F44 for newer Polhemus-branded units, 0x0403
     * for the generic FTDI chip used in older units). This scan returns
     * the first matching port name, or an empty string if no candidate
     * is found.
     *
     * @return Port name of a Polhemus-vendor port, else the first FTDI port, else an empty string.
     */
    static QString autoDetectPortName();

signals:
    /**
     * @param station     Polhemus station id (1..4 for FastSCAN).
     * @param position    Sample point in metres, sensor-frame.
     * @param orientation Sensor orientation; identity if N/A.
     */
    void pointReceived(int station, const Eigen::Vector3f& position, const Eigen::Quaternionf& orientation);

    /** Emitted whenever @c ACQLIB::PolhemusConnection::isConnected "isConnected" changes. */
    void connectedChanged(bool isConnected);

    /** Non-fatal protocol/IO error (already logged by the backend). */
    void errorOccurred(const QString& message);

    /**
     * Emitted when the continuous stream pauses (pen button pressed).
     * The last received position/orientation are provided so the caller
     * can record the capture without a separate GUI button click.
     *
     * After emitting, the connection automatically restarts the
     * continuous stream so the live feed resumes on button release.
     */
    void penButtonPressed(int station, const Eigen::Vector3f& position, const Eigen::Quaternionf& orientation);

private slots:
    void onMockTick();
    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError err);
    void onStreamPauseTimeout();

private:
    bool openMock();
    void closeMock();

    bool openSerial(const QString& portName, const PolhemusSerialConfig& cfg);
    void closeSerial();
    void drainParser();

    bool m_isConnected = false;
    QString m_backendName;

    // Mock backend ------------------------------------------------------------
    QTimer m_mockTimer;
    int m_mockSampleIdx = 0;

    // Serial backend ----------------------------------------------------------
    QSerialPort* m_pSerial = nullptr;
    FastrakParser m_parser;

    // Pen-button detection (stream-pause timeout) -----------------------------
    QTimer m_streamPauseTimer;
    struct StationSample
    {
        Eigen::Vector3f position = Eigen::Vector3f::Zero();
        Eigen::Quaternionf orientation = Eigen::Quaternionf::Identity();
    };
    QMap<int, StationSample> m_lastSamples;
};

} // namespace ACQLIB

#endif // ACQ_POLHEMUS_CONNECTION_H
