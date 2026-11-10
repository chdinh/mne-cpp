//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     serial_port.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November, 2026
 * @brief    SerialPort on termios (POSIX) and the communications API (Windows).
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "serial_port.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
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

#ifndef _WIN32
/** @return The rank of a port name: USB-serial adapters (which digitizers use) before built-in ports. */
int rank(std::string_view name)
{
    for (std::string_view usb : {"usbserial", "ttyUSB", "usbmodem", "ttyACM"}) {
        if (name.find(usb) != std::string_view::npos) {
            return 0;
        }
    }
    return 1;
}

AcqError system_error(const std::string& what)
{
    const int error = errno;
    const AcqErrorCode code = error == ENOENT || error == ENXIO ? AcqErrorCode::DeviceNotFound
        : error == EACCES || error == EPERM                     ? AcqErrorCode::PermissionDenied
                                                                : AcqErrorCode::DeviceIo;
    return {code, std::format("{}: {}", what, std::strerror(error))};
}

/** @return The termios speed of @p baud, or B0 if termios has none. */
speed_t speed_of(int baud)
{
    constexpr std::array<std::pair<int, speed_t>, 10> kSpeeds = {
        {{1200, B1200}, {2400, B2400}, {4800, B4800}, {9600, B9600}, {19200, B19200}, {38400, B38400}, {57600, B57600}, {115200, B115200}, {230400, B230400}, {0, B0}}};
    const auto it = std::find_if(kSpeeds.begin(), kSpeeds.end(), [baud](const auto& s) { return s.first == baud; });
    return it == kSpeeds.end() ? B0 : it->second;
}
#endif

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

SerialPort::SerialPort(SerialPort&& other) noexcept
#ifdef _WIN32
: m_handle(std::exchange(other.m_handle, nullptr))
#else
: m_fd(std::exchange(other.m_fd, -1))
#endif
{
}

//=============================================================================================================

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept
{
    if (this != &other) {
        SerialPort closing(std::move(*this));
#ifdef _WIN32
        m_handle = std::exchange(other.m_handle, nullptr);
#else
        m_fd = std::exchange(other.m_fd, -1);
#endif
    }
    return *this;
}

//=============================================================================================================

#ifdef _WIN32

SerialPort::~SerialPort()
{
    if (m_handle != nullptr) {
        FlushFileBuffers(m_handle); // the last command (e.g. stop the output) must reach the device
        CloseHandle(m_handle);
    }
}

//=============================================================================================================

bool SerialPort::is_open() const noexcept
{
    return m_handle != nullptr;
}

//=============================================================================================================

AcqExpected<SerialPort> SerialPort::open(const std::string& name, int baud)
{
    if (baud <= 0) {
        return AcqError(AcqErrorCode::InvalidParameter, std::format("baud rate {} of {}", baud, name));
    }
    const std::string path = name.starts_with("\\\\.\\") ? name : "\\\\.\\" + name; // COM10 and above need the prefix
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            return AcqError(AcqErrorCode::DeviceNotFound, std::format("no serial port {}", name));
        }
        return AcqError(AcqErrorCode::DeviceIo, std::format("cannot open serial port {}{} (error {})", name, error == ERROR_ACCESS_DENIED ? ", another program has it" : "", error));
    }
    SerialPort port;
    port.m_handle = handle;
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD; // read() returns what is there
    if (!GetCommState(handle, &dcb)) {
        return AcqError(AcqErrorCode::DeviceIo, std::format("{} is not a serial port", name));
    }
    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(handle, &dcb) || !SetCommTimeouts(handle, &timeouts)) {
        return AcqError(AcqErrorCode::InvalidParameter, std::format("{} refuses {} baud 8N1", name, baud));
    }
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return port;
}

//=============================================================================================================

AcqError SerialPort::write(std::string_view data)
{
    while (!data.empty()) {
        DWORD written = 0;
        if (!WriteFile(m_handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)) {
            return {AcqErrorCode::DeviceIo, std::format("serial write failed (error {})", GetLastError())};
        }
        data.remove_prefix(written);
    }
    return {};
}

//=============================================================================================================

AcqExpected<std::size_t> SerialPort::read(std::span<char> buffer, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        DWORD errors = 0;
        COMSTAT status{};
        if (!ClearCommError(m_handle, &errors, &status)) {
            return AcqError(AcqErrorCode::DeviceIo, std::format("serial port went away (error {})", GetLastError()));
        }
        if (status.cbInQue > 0) {
            DWORD got = 0;
            const DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(buffer.size(), status.cbInQue));
            if (!ReadFile(m_handle, buffer.data(), wanted, &got, nullptr)) {
                return AcqError(AcqErrorCode::DeviceIo, std::format("serial read failed (error {})", GetLastError()));
            }
            return std::size_t{got};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return AcqError(AcqErrorCode::Timeout);
        }
        Sleep(1);
    }
}

//=============================================================================================================

std::vector<std::string> ACQLIB::detail::list_serial_ports()
{
    std::vector<std::string> ports;
    std::array<char, 65536> devices{};
    const DWORD length = QueryDosDeviceA(nullptr, devices.data(), static_cast<DWORD>(devices.size()));
    for (const char* name = devices.data(); name < devices.data() + length && *name != '\0'; name += std::strlen(name) + 1) {
        if (std::string_view(name).starts_with("COM")) {
            ports.emplace_back(name);
        }
    }
    std::ranges::sort(ports, [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
    return ports;
}

#else

SerialPort::~SerialPort()
{
    if (m_fd >= 0) {
        ::tcdrain(m_fd); // closing a non-blocking port discards unsent output, such as the command that stops the device
        ::close(m_fd);
    }
}

//=============================================================================================================

bool SerialPort::is_open() const noexcept
{
    return m_fd >= 0;
}

//=============================================================================================================

AcqExpected<SerialPort> SerialPort::open(const std::string& name, int baud)
{
    const speed_t speed = speed_of(baud);
    if (speed == B0) {
        return AcqError(AcqErrorCode::InvalidParameter, std::format("baud rate {} of {}: 1200 to 230400", baud, name));
    }
    SerialPort port;
    port.m_fd = ::open(name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (port.m_fd < 0) {
        return system_error("cannot open serial port " + name);
    }
    termios tty{};
    if (::tcgetattr(port.m_fd, &tty) != 0) {
        return AcqError(AcqErrorCode::DeviceIo, std::format("{} is not a serial port", name));
    }
    ::cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~static_cast<tcflag_t>(CSTOPB | CRTSCTS);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    if (::cfsetispeed(&tty, speed) != 0 || ::cfsetospeed(&tty, speed) != 0 || ::tcsetattr(port.m_fd, TCSANOW, &tty) != 0) {
        return system_error(std::format("{} refuses {} baud 8N1", name, baud));
    }
    ::tcflush(port.m_fd, TCIOFLUSH);
    return port;
}

//=============================================================================================================

AcqError SerialPort::write(std::string_view data)
{
    while (!data.empty()) {
        const ssize_t written = ::write(m_fd, data.data(), data.size());
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                pollfd p{m_fd, POLLOUT, 0};
                ::poll(&p, 1, 100);
                continue;
            }
            return system_error("serial write failed");
        }
        data.remove_prefix(static_cast<std::size_t>(written));
    }
    return {};
}

//=============================================================================================================

AcqExpected<std::size_t> SerialPort::read(std::span<char> buffer, std::chrono::milliseconds timeout)
{
    pollfd p{m_fd, POLLIN, 0};
    const int ready = ::poll(&p, 1, static_cast<int>(timeout.count()));
    if (ready < 0) {
        return errno == EINTR ? AcqExpected<std::size_t>(AcqError(AcqErrorCode::Timeout)) : system_error("serial poll failed");
    }
    if (ready == 0) {
        return AcqError(AcqErrorCode::Timeout);
    }
    if ((p.revents & (POLLERR | POLLNVAL)) != 0) {
        return AcqError(AcqErrorCode::DeviceIo, "the serial port went away");
    }
    const ssize_t got = ::read(m_fd, buffer.data(), buffer.size());
    if (got < 0) {
        return errno == EAGAIN || errno == EINTR ? AcqExpected<std::size_t>(AcqError(AcqErrorCode::Timeout)) : system_error("serial read failed");
    }
    if (got == 0) {
        return AcqError(AcqErrorCode::DeviceIo, "the serial port went away");
    }
    return static_cast<std::size_t>(got);
}

//=============================================================================================================

std::vector<std::string> ACQLIB::detail::list_serial_ports()
{
    std::vector<std::string> usb;
    std::vector<std::string> other;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", error)) {
        const std::string name = entry.path().filename().string();
#ifdef __APPLE__
        const bool serial = name.starts_with("cu.") && name != "cu.Bluetooth-Incoming-Port";
#else
        const bool serial = name.starts_with("ttyUSB") || name.starts_with("ttyACM") || name.starts_with("ttyS");
#endif
        if (serial) {
            (rank(name) == 0 ? usb : other).push_back(entry.path().string());
        }
    }
    std::ranges::sort(usb);
    std::ranges::sort(other);
    usb.insert(usb.end(), other.begin(), other.end());
    return usb;
}

#endif
