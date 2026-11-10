//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2020-2026 MNE-CPP Authors
 *
 * @file     ftbuffer_client.cpp
 * @author   Gabriel Motta <gabrielbenmotta@gmail.com>;
 *           Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    0.1.0
 * @date     February, 2020
 * @brief    The FieldTrip buffer protocol over TCP.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "ftbuffer_transport.h"

#include "../tcp_socket.h"

#include <array>
#include <cstring>
#include <format>

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

constexpr std::size_t kMessageDefBytes = 8;
constexpr std::uint32_t kMaxPayload = 1U << 30; // a corrupt size must not allocate gigabytes

class FtBufferClient final : public FtBufferTransport
{
public:
    FtBufferClient(TcpSocket socket, std::string name)
    : m_socket(std::move(socket))
    , m_name(std::move(name))
    {
    }

    std::string description() const override
    {
        return m_name;
    }

    AcqExpected<FtMessage> request(std::uint16_t command, std::span<const std::byte> payload, std::chrono::milliseconds timeout) override
    {
        std::array<std::byte, kMessageDefBytes> def{};
        const auto size = static_cast<std::uint32_t>(payload.size());
        std::memcpy(def.data(), &ft::kVersion, 2);
        std::memcpy(def.data() + 2, &command, 2);
        std::memcpy(def.data() + 4, &size, 4);
        if (AcqError e = m_socket.write(def); !e.ok()) {
            return e;
        }
        if (AcqError e = m_socket.write(payload); !e.ok()) {
            return e;
        }
        if (AcqError e = m_socket.read(def, timeout); !e.ok()) {
            return e.code() == AcqErrorCode::Timeout ? AcqError(AcqErrorCode::DeviceIo, "the FieldTrip buffer does not answer") : e;
        }
        std::uint16_t version = 0;
        FtMessage answer;
        std::uint32_t length = 0;
        std::memcpy(&version, def.data(), 2);
        std::memcpy(&answer.command, def.data() + 2, 2);
        std::memcpy(&length, def.data() + 4, 4);
        if (version != ft::kVersion || length > kMaxPayload) {
            return AcqError(AcqErrorCode::DeviceIo, std::format("{} does not speak the FieldTrip buffer protocol", m_name));
        }
        answer.payload.resize(length);
        if (AcqError e = m_socket.read(answer.payload, timeout); !e.ok()) {
            return AcqError(AcqErrorCode::DeviceIo, "the FieldTrip buffer's answer is incomplete");
        }
        return answer;
    }

private:
    TcpSocket m_socket;
    std::string m_name;
};

} // namespace

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

AcqExpected<std::unique_ptr<FtBufferTransport>> ACQLIB::detail::connect_ftbuffer(const std::string& host, std::uint16_t port,
                                                                                 std::chrono::milliseconds timeout)
{
    auto socket = TcpSocket::connect(host, port, timeout);
    if (!socket) {
        return AcqError(socket.error().code(), "no FieldTrip buffer: " + socket.error().text());
    }
    return std::unique_ptr<FtBufferTransport>(std::make_unique<FtBufferClient>(std::move(*socket), std::format("{}:{}", host, port)));
}
