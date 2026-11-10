//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     udp_socket.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November, 2026
 * @brief    UdpSocket on BSD sockets and Winsock.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "udp_socket.h"

#include "detail/socket_platform.h"

#include <utility>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace COMLIB;
using namespace COMLIB::detail;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

UdpSocket::~UdpSocket()
{
    if (m_socket != kInvalidHandle) {
        close_native(static_cast<Native>(m_socket));
    }
}

//=============================================================================================================

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
: m_socket(std::exchange(other.m_socket, kInvalidHandle))
{
}

//=============================================================================================================

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this != &other) {
        UdpSocket closing(std::move(*this));
        m_socket = std::exchange(other.m_socket, kInvalidHandle);
    }
    return *this;
}

//=============================================================================================================

std::expected<UdpSocket, std::error_code> UdpSocket::bind(const std::string& address, std::uint16_t port)
{
    if (!started()) {
        return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    if (::inet_pton(AF_INET, address.c_str(), &local.sin_addr) != 1) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    const Native s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kInvalid) {
        return std::unexpected(last_error());
    }
    UdpSocket socket;
    socket.m_socket = static_cast<Handle>(s);
    int size = 4 << 20; // a burst of datagrams must not overflow while the reader waits
    ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), sizeof(size));
    if (::bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        return std::unexpected(last_error());
    }
    return socket;
}

//=============================================================================================================

std::uint16_t UdpSocket::port() const noexcept
{
    sockaddr_in local{};
    socklen_t length = sizeof(local);
    if (::getsockname(static_cast<Native>(m_socket), reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        return 0;
    }
    return ntohs(local.sin_port);
}

//=============================================================================================================

std::expected<std::size_t, std::error_code> UdpSocket::receive(std::span<std::byte> buffer, std::chrono::milliseconds timeout)
{
    const auto s = static_cast<Native>(m_socket);
    const int ready = wait_for(s, POLLIN, timeout);
    if (ready == 0) {
        return std::unexpected(std::make_error_code(std::errc::timed_out));
    }
    if (ready < 0) {
        return std::unexpected(last_error());
    }
#ifdef _WIN32
    const int got = ::recv(s, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (got < 0 && WSAGetLastError() == WSAEMSGSIZE) {
        return buffer.size();
    }
#else
    const auto got = ::recv(s, buffer.data(), buffer.size(), 0);
#endif
    if (got < 0) {
        return std::unexpected(last_error());
    }
    return static_cast<std::size_t>(got);
}
