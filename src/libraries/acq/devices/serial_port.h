//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     serial_port.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November, 2026
 * @brief    A raw 8N1 serial port without flow control, for devices that talk over RS-232 or USB-serial.
 */

#ifndef ACQ_DEVICES_SERIAL_PORT_H
#define ACQ_DEVICES_SERIAL_PORT_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../acq_error.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB::detail
//=============================================================================================================

namespace ACQLIB::detail
{

/** An open serial port; move-only, closed on destruction. */
class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(SerialPort&& other) noexcept;
    SerialPort& operator=(SerialPort&& other) noexcept;
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    /**
     * @param[in] name   Device path ("/dev/cu.usbserial-1", "/dev/ttyUSB0") or port name ("COM3").
     * @param[in] baud   Bits per second.
     *
     * @return The port in raw mode, DeviceNotFound if it does not exist, PermissionDenied, InvalidParameter for
     *         an unsupported baud rate, or DeviceIo (e.g. another program has it).
     */
    [[nodiscard]] static AcqExpected<SerialPort> open(const std::string& name, int baud);

    /** @return Whether the port is open. */
    [[nodiscard]] bool is_open() const noexcept;

    /**
     * Writes all of @p data.
     *
     * @param[in] data   Bytes to send.
     *
     * @return DeviceIo if the port fails.
     */
    AcqError write(std::string_view data);

    /**
     * Waits for bytes and reads what is there, at most @p buffer.size().
     *
     * @param[out] buffer    Destination.
     * @param[in]  timeout   Maximum wait.
     *
     * @return The bytes read, Timeout if none arrived, DeviceIo if the port fails or went away.
     */
    [[nodiscard]] AcqExpected<std::size_t> read(std::span<char> buffer, std::chrono::milliseconds timeout);

private:
#ifdef _WIN32
    void* m_handle = nullptr;
#else
    int m_fd = -1;
#endif
};

/** @return The serial ports of this machine, USB-serial adapters first. */
std::vector<std::string> list_serial_ports();

} // namespace ACQLIB::detail

#endif // ACQ_DEVICES_SERIAL_PORT_H
