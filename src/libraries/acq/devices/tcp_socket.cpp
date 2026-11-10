//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     tcp_socket.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November, 2026
 * @brief    TcpSocket on BSD sockets and Winsock.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "tcp_socket.h"

#include <cstring>
#include <format>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace ACQLIB::detail;

//=============================================================================================================
// STATIC HELPERS
//=============================================================================================================

namespace
{

#ifdef _WIN32
using Native = SOCKET;
constexpr Native kInvalid = INVALID_SOCKET;

int last_error() noexcept
{
    return WSAGetLastError();
}

void close_native(Native s) noexcept
{
    closesocket(s);
}

/** @return 1 when @p s is ready for @p events within @p timeout, 0 on timeout, -1 on failure. */
int wait_for(Native s, short events, std::chrono::milliseconds timeout) noexcept
{
    WSAPOLLFD p{s, events, 0};
    return WSAPoll(&p, 1, static_cast<INT>(timeout.count()));
}

bool started() noexcept
{
    static const bool ok = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
#else
using Native = int;
constexpr Native kInvalid = -1;

int last_error() noexcept
{
    return errno;
}

void close_native(Native s) noexcept
{
    ::close(s);
}

int wait_for(Native s, short events, std::chrono::milliseconds timeout) noexcept
{
    pollfd p{s, events, 0};
    int ready = 0;
    do {
        ready = ::poll(&p, 1, static_cast<int>(timeout.count()));
    } while (ready < 0 && errno == EINTR);
    return ready;
}

bool started() noexcept
{
    return true;
}
#endif

/** Puts @p s into non-blocking mode; reads and writes then wait in wait_for(). */
bool set_non_blocking(Native s) noexcept
{
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    const int flags = ::fcntl(s, F_GETFL, 0);
    return flags >= 0 && ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool would_block(int error) noexcept
{
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return error == EINPROGRESS || error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
#endif
}

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

TcpSocket::~TcpSocket()
{
    if (m_socket != static_cast<decltype(m_socket)>(kInvalid)) {
        close_native(static_cast<Native>(m_socket));
    }
}

//=============================================================================================================

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
: m_socket(std::exchange(other.m_socket, static_cast<decltype(m_socket)>(kInvalid)))
{
}

//=============================================================================================================

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
    if (this != &other) {
        TcpSocket closing(std::move(*this));
        m_socket = std::exchange(other.m_socket, static_cast<decltype(m_socket)>(kInvalid));
    }
    return *this;
}

//=============================================================================================================

AcqExpected<TcpSocket> TcpSocket::connect(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout)
{
    if (!started()) {
        return AcqError(AcqErrorCode::PlatformUnsupported, "the sockets library is not available");
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || found == nullptr) {
        return AcqError(AcqErrorCode::DeviceNotFound, std::format("unknown host {}", host));
    }
    std::string why = "no address";
    TcpSocket socket;
    for (const addrinfo* a = found; a != nullptr; a = a->ai_next) {
        const Native s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kInvalid) {
            continue;
        }
        socket = TcpSocket();
        socket.m_socket = static_cast<decltype(socket.m_socket)>(s);
        if (!set_non_blocking(s)) {
            continue;
        }
#ifdef SO_NOSIGPIPE
        int no_signal = 1; // a closed peer is an error, not a signal
        ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &no_signal, sizeof(no_signal));
#endif
        if (::connect(s, a->ai_addr, static_cast<socklen_t>(a->ai_addrlen)) != 0 && !would_block(last_error())) {
            why = std::format("error {}", last_error());
            continue;
        }
        if (wait_for(s, POLLOUT, timeout) <= 0) {
            why = "no answer";
            continue;
        }
        int error = 0;
        socklen_t length = sizeof(error);
        ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
        if (error != 0) {
            why = std::format("error {}", error);
            continue;
        }
        int on = 1; // requests are small and answered one by one
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
        ::freeaddrinfo(found);
        return socket;
    }
    ::freeaddrinfo(found);
    return AcqError(AcqErrorCode::DeviceNotFound, std::format("cannot connect to {}:{} ({})", host, port, why));
}

//=============================================================================================================

AcqError TcpSocket::write(std::span<const std::byte> data)
{
    const auto s = static_cast<Native>(m_socket);
    while (!data.empty()) {
#ifdef _WIN32
        const int sent = ::send(s, reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()), 0);
#elif defined(MSG_NOSIGNAL)
        const auto sent = ::send(s, data.data(), data.size(), MSG_NOSIGNAL);
#else
        const auto sent = ::send(s, data.data(), data.size(), 0); // SO_NOSIGPIPE is set
#endif
        if (sent < 0) {
            if (would_block(last_error()) && wait_for(s, POLLOUT, std::chrono::milliseconds(1000)) > 0) {
                continue;
            }
            return {AcqErrorCode::DeviceIo, std::format("cannot send (error {})", last_error())};
        }
        data = data.subspan(static_cast<std::size_t>(sent));
    }
    return {};
}

//=============================================================================================================

AcqError TcpSocket::read(std::span<std::byte> buffer, std::chrono::milliseconds timeout)
{
    const auto s = static_cast<Native>(m_socket);
    bool started_reading = false;
    while (!buffer.empty()) {
        const int ready = wait_for(s, POLLIN, timeout);
        if (ready == 0) {
            if (!started_reading) {
                return AcqError(AcqErrorCode::Timeout);
            }
            return {AcqErrorCode::DeviceIo, "the answer stopped halfway"};
        }
        if (ready < 0) {
            return {AcqErrorCode::DeviceIo, std::format("cannot wait for data (error {})", last_error())};
        }
#ifdef _WIN32
        const int got = ::recv(s, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
#else
        const auto got = ::recv(s, buffer.data(), buffer.size(), 0);
#endif
        if (got == 0) {
            return {AcqErrorCode::DeviceIo, "the server closed the connection"};
        }
        if (got < 0) {
            if (would_block(last_error())) {
                continue;
            }
            return {AcqErrorCode::DeviceIo, std::format("cannot receive (error {})", last_error())};
        }
        started_reading = true;
        buffer = buffer.subspan(static_cast<std::size_t>(got));
    }
    return {};
}
