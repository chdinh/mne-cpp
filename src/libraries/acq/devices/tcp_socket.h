//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     tcp_socket.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November, 2026
 * @brief    A blocking TCP client connection with timeouts, for devices that serve their data over TCP.
 */

#ifndef ACQ_DEVICES_TCP_SOCKET_H
#define ACQ_DEVICES_TCP_SOCKET_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../acq_error.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

//=============================================================================================================
// DEFINE NAMESPACE ACQLIB::detail
//=============================================================================================================

namespace ACQLIB::detail
{

/** A connected TCP socket; move-only, closed on destruction. */
class TcpSocket
{
public:
    TcpSocket() = default;
    ~TcpSocket();

    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    /**
     * @param[in] host      Host name or address.
     * @param[in] port      Port.
     * @param[in] timeout   Maximum wait for the connection.
     *
     * @return The connection, or DeviceNotFound if nothing answers.
     */
    [[nodiscard]] static AcqExpected<TcpSocket> connect(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout);

    /**
     * @param[in] data   Bytes to send, all of them.
     *
     * @return DeviceIo if the connection fails.
     */
    AcqError write(std::span<const std::byte> data);

    /**
     * Reads exactly @p buffer.size() bytes.
     *
     * @param[out] buffer    Destination.
     * @param[in]  timeout   Maximum wait for the first byte; the rest must follow within it too.
     *
     * @return Timeout if nothing arrived (nothing is consumed then), DeviceIo if the connection closed or failed.
     */
    AcqError read(std::span<std::byte> buffer, std::chrono::milliseconds timeout);

private:
#ifdef _WIN32
    std::uintptr_t m_socket = ~std::uintptr_t{0};
#else
    int m_socket = -1;
#endif
};

} // namespace ACQLIB::detail

#endif // ACQ_DEVICES_TCP_SOCKET_H
