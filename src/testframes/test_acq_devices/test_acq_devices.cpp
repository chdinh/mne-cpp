//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     test_acq_devices.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Tests for the vendor device sources of mne_acq_devices, run against their simulators.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <acq/acq_convert.h>
#include <acq/acq_device.h>
#include <acq/acq_device_registry.h>
#include <acq/devices/acq_devices.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QtTest>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace std::chrono_literals;

//=============================================================================================================
// STATIC HELPERS
//=============================================================================================================

namespace
{

/** Everything a Lossless reader delivered until EndOfStream, in physical units, frame after frame. */
struct Recording
{
    std::size_t channels = 0;
    std::vector<double> samples;
    std::vector<AcqEvent> events;
    std::vector<double> scale; /**< Per channel. */

    [[nodiscard]] std::size_t frames() const
    {
        return channels == 0 ? 0 : samples.size() / channels;
    }

    [[nodiscard]] double at(std::size_t frame, std::size_t channel) const
    {
        return samples[frame * channels + channel];
    }
};

/** Everything a Lossless reader of @p device delivers from start() until EndOfStream. */
Recording record_device(AcqDevice& device)
{
    Recording out;
    const auto info = device.stream().info();
    out.channels = info->channelCount();
    for (const AcqChannelInfo& channel : info->channels) {
        out.scale.push_back(channel.scale);
    }
    // Replays as fast as they are read wait for the reader instead of overrunning it
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device.stream().reader(AcqReader::ReadMode::Lossless, gating);
    if (!reader || !device.start().ok()) {
        return out;
    }
    while (true) {
        auto view = reader->acquire(1, 10s);
        if (!view) {
            if (view.error() != AcqErrorCode::EndOfStream) {
                qWarning("acquire: %s", view.error().text().c_str());
            }
            break;
        }
        for (std::size_t p = 0; p < view->partCount(); ++p) {
            const AcqBlockPart& part = view->part(p);
            for (std::size_t s = 0; s < part.samples; ++s) {
                for (std::size_t c = 0; c < out.channels; ++c) {
                    if (info->format == AcqSampleFormat::Int16) {
                        std::int16_t raw = 0;
                        std::memcpy(&raw, part.at(c, s), sizeof(raw));
                        out.samples.push_back(raw * out.scale[c] + info->channels[c].offset);
                    } else if (info->format == AcqSampleFormat::Int32) {
                        std::int32_t raw = 0;
                        std::memcpy(&raw, part.at(c, s), sizeof(raw));
                        out.samples.push_back(raw * out.scale[c] + info->channels[c].offset);
                    } else if (info->format == AcqSampleFormat::Float64) {
                        double v = 0.0;
                        std::memcpy(&v, part.at(c, s), sizeof(v));
                        out.samples.push_back(v * out.scale[c] + info->channels[c].offset);
                    } else {
                        float v = 0.0F;
                        std::memcpy(&v, part.at(c, s), sizeof(v));
                        out.samples.push_back(v * out.scale[c] + info->channels[c].offset);
                    }
                }
            }
        }
        for (std::size_t e = 0; e < view->events().size(); ++e) {
            out.events.push_back(view->events()[e]);
        }
        static_cast<void>(reader->release(*view));
    }
    return out;
}

Recording record(const std::string& uri)
{
    auto device = AcqDevice::open(uri);
    if (!device) {
        qWarning("open(%s): %s", uri.c_str(), device.error().text().c_str());
        return {};
    }
    return record_device(*device);
}

AcqErrorCode openError(const std::string& uri)
{
    auto device = AcqDevice::open(uri);
    return device ? AcqErrorCode::None : device.error().code();
}

#if !defined(_WIN32)
/**
 * A Neuromag data server and collector on this machine, enough for the client: the measurement info of three
 * channels (two MEG, STI 014) at 600 Hz on every connection, and on the second one a data buffer from the
 * measurement file (big-endian), one in a datagram, and the end of the file.
 */
class FakeDacq
{
public:
    FakeDacq()
    {
        char dir[] = "/tmp/nmXXXXXX"; // short: socket paths are limited to about 100 characters
        if (::mkdtemp(dir) == nullptr) {
            return;
        }
        m_root = dir;
        ::mkdir((m_root + "/sockets").c_str(), 0700);
        ::mkdir((m_root + "/raw").c_str(), 0700);
        write_file();

        m_server = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        const sockaddr_un address = unix_address(m_root + "/sockets/dacq_server");
        if (m_server < 0 || ::bind(m_server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return;
        }
        m_listener = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in tcp{};
        tcp.sin_family = AF_INET;
        tcp.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(tcp);
        if (m_listener < 0 || ::bind(m_listener, reinterpret_cast<sockaddr*>(&tcp), sizeof(tcp)) != 0 || ::listen(m_listener, 4) != 0 ||
            ::getsockname(m_listener, reinterpret_cast<sockaddr*>(&tcp), &length) != 0) {
            return;
        }
        m_port = ntohs(tcp.sin_port);
        m_threads.emplace_back([this] { serve_data(); });
        m_threads.emplace_back([this] { serve_collector(); });
    }

    ~FakeDacq()
    {
        m_stop = true;
        ::shutdown(m_listener, SHUT_RDWR);
        ::close(m_listener);
        ::close(m_server);
        for (std::thread& thread : m_threads) {
            thread.join();
        }
        std::filesystem::remove_all(m_root);
    }

    FakeDacq(const FakeDacq&) = delete;
    FakeDacq& operator=(const FakeDacq&) = delete;

    [[nodiscard]] bool ok() const
    {
        return m_port > 0;
    }

    [[nodiscard]] const std::string& root() const
    {
        return m_root;
    }

    [[nodiscard]] int port() const
    {
        return m_port;
    }

    [[nodiscard]] std::vector<std::string> commands()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_commands;
    }

private:
    static sockaddr_un unix_address(const std::string& path)
    {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
        return address;
    }

    static bool readable(int fd)
    {
        pollfd p{fd, POLLIN, 0};
        return ::poll(&p, 1, 50) > 0 && (p.revents & POLLIN) != 0;
    }

    static std::vector<std::byte> ints(std::initializer_list<std::int32_t> values)
    {
        std::vector<std::byte> data(values.size() * 4);
        std::memcpy(data.data(), std::data(values), data.size());
        return data;
    }

    static std::vector<std::byte> channel(int scan, const char* name, int kind, int coil, int unit, float cal, float range)
    {
        std::vector<std::byte> data(96);
        const std::int32_t head[3] = {scan, scan, kind};
        std::memcpy(data.data(), head, sizeof(head));
        std::memcpy(data.data() + 12, &range, 4);
        std::memcpy(data.data() + 16, &cal, 4);
        std::memcpy(data.data() + 20, &coil, 4);
        std::memcpy(data.data() + 72, &unit, 4);
        std::memcpy(data.data() + 80, name, std::strlen(name));
        return data;
    }

    /** Samples @p first..first+3 of the three channels. */
    static std::vector<std::int32_t> samples(int first)
    {
        std::vector<std::int32_t> values;
        for (int s = first; s < first + 4; ++s) {
            values.insert(values.end(), {100 + s, -s, s >= 5 ? 3 : 0});
        }
        return values;
    }

    void write_file() const
    {
        std::ofstream file(m_root + "/raw/meas.fif", std::ios::binary);
        for (std::int32_t value : samples(0)) {
            const auto bits = static_cast<std::uint32_t>(value);
            const std::array<char, 4> big = {static_cast<char>(bits >> 24), static_cast<char>(bits >> 16), static_cast<char>(bits >> 8),
                                             static_cast<char>(bits)};
            file.write(big.data(), 4);
        }
    }

    void send(const sockaddr_un& to, int kind, int type, const std::vector<std::byte>& data, int loc = -1)
    {
        const std::int32_t message[6] = {kind, type, static_cast<std::int32_t>(data.size()), loc, -1, -1};
        ::sendto(m_server, message, sizeof(message), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        if (loc < 0 && !data.empty()) {
            ::sendto(m_server, data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        }
    }

    void serve_data()
    {
        int session = 0;
        while (!m_stop) {
            if (!readable(m_server)) {
                continue;
            }
            sockaddr_un from{};
            socklen_t length = sizeof(from);
            std::int32_t id = 0;
            if (::recvfrom(m_server, &id, sizeof(id), 0, reinterpret_cast<sockaddr*>(&from), &length) != sizeof(id)) {
                continue;
            }
            const std::int32_t ok = 0;
            ::sendto(m_server, &ok, sizeof(ok), 0, reinterpret_cast<const sockaddr*>(&from), length);
            if (id <= 0) {
                continue;
            }
            const float sfreq = 600.0F;
            std::vector<std::byte> rate(4);
            std::memcpy(rate.data(), &sfreq, 4);
            send(from, 104, 3, ints({101}));
            send(from, 200, 3, ints({3}));
            send(from, 201, 4, rate);
            send(from, 203, 30, channel(1, "MEG 0111", 1, 3024, 112, 1.0F, 1.0F));
            send(from, 203, 30, channel(2, "MEG 0112", 1, 3012, 201, 2.0F, 0.5F));
            send(from, 203, 30, channel(3, "STI 014", 3, 0, -1, 1.0F, 1.0F));
            send(from, 105, 3, ints({101}));
            if (++session == 2) {
                const std::string path = m_root + "/raw/meas.fif";
                std::vector<std::byte> name(path.size() + 1);
                std::memcpy(name.data(), path.c_str(), path.size());
                send(from, 1, 10, name);
                send(from, 300, 3, std::vector<std::byte>(48), 0);
                const std::vector<std::int32_t> second = samples(4);
                std::vector<std::byte> data(48);
                std::memcpy(data.data(), second.data(), data.size());
                send(from, 300, 3, data);
                send(from, 2, 0, {});
            }
        }
    }

    void serve_collector()
    {
        while (!m_stop) {
            const int fd = ::accept(m_listener, nullptr, nullptr);
            if (fd < 0) {
                return;
            }
            std::string pending;
            std::array<char, 256> buffer{};
            while (!m_stop) {
                if (!readable(fd)) {
                    continue;
                }
                const ssize_t got = ::recv(fd, buffer.data(), buffer.size(), 0);
                if (got <= 0) {
                    break;
                }
                pending.append(buffer.data(), static_cast<std::size_t>(got));
                for (std::size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
                    std::string line = pending.substr(0, end);
                    pending.erase(0, end + 1);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                        line.pop_back();
                    }
                    if (line.starts_with("pass ") || line.starts_with("name ")) {
                        line.resize(4);
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_commands.push_back(line);
                    }
                    ::send(fd, "OK\n", 3, 0);
                }
            }
            ::close(fd);
        }
    }

    std::string m_root;
    int m_server = -1;
    int m_listener = -1;
    int m_port = 0;
    std::atomic<bool> m_stop{false};
    std::mutex m_mutex;
    std::vector<std::string> m_commands;
    std::vector<std::thread> m_threads;
};

/** Appends @p value to @p out, big-endian if @p big. */
template<typename T>
void append_value(std::vector<std::byte>& out, T value, bool big = false)
{
    std::array<std::byte, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    if (big) {
        std::reverse(bytes.begin(), bytes.end());
    }
    out.insert(out.end(), bytes.begin(), bytes.end());
}

/** Appends a FIFF tag whose data are words (or a channel record's words and name). */
void append_fiff_tag(std::vector<std::byte>& out, std::int32_t kind, std::int32_t type, const std::vector<std::byte>& data, bool big)
{
    for (const std::int32_t word : {kind, type, static_cast<std::int32_t>(data.size()), 0}) {
        append_value(out, word, big);
    }
    out.insert(out.end(), data.begin(), data.end());
}

std::vector<std::byte> fiff_words(std::initializer_list<std::int32_t> words, bool big)
{
    std::vector<std::byte> out;
    for (const std::int32_t word : words) {
        append_value(out, word, big);
    }
    return out;
}

/** A channel record (fiffChInfoRec) in little-endian byte order. */
std::vector<std::byte> fiff_channel(std::int32_t kind, std::int32_t coil, std::int32_t unit, float cal, const char* name)
{
    std::vector<std::byte> out;
    for (const std::int32_t word : {1, 1, kind}) {
        append_value(out, word);
    }
    append_value(out, 1.0F); // range
    append_value(out, cal);
    append_value(out, coil);
    for (int i = 0; i < 12; ++i) {
        append_value(out, i == 11 ? 1.0F : 0.0F);
    }
    append_value(out, unit);
    append_value(out, std::int32_t{0});
    std::array<char, 16> chars{};
    std::strncpy(chars.data(), name, chars.size() - 1);
    for (const char c : chars) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

/**
 * A FieldTrip buffer server as neuromag2ft fills it: a header with the channel names, the Neuromag measurement
 * info (little-endian tags) and an isotrak file (big-endian FIFF) for two MEG channels and STI 014 at 600 Hz,
 * float32. After the stream started it holds 8 samples and two events (one with a string value), then it gets
 * a new header.
 */
class FakeFtBuffer
{
public:
    FakeFtBuffer()
    {
        m_listener = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in tcp{};
        tcp.sin_family = AF_INET;
        tcp.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(tcp);
        if (m_listener < 0 || ::bind(m_listener, reinterpret_cast<sockaddr*>(&tcp), sizeof(tcp)) != 0 || ::listen(m_listener, 4) != 0 ||
            ::getsockname(m_listener, reinterpret_cast<sockaddr*>(&tcp), &length) != 0) {
            return;
        }
        m_port = ntohs(tcp.sin_port);
        m_thread = std::thread([this] { serve(); });
    }

    ~FakeFtBuffer()
    {
        m_stop = true;
        ::shutdown(m_listener, SHUT_RDWR);
        ::close(m_listener);
        m_thread.join();
    }

    FakeFtBuffer(const FakeFtBuffer&) = delete;
    FakeFtBuffer& operator=(const FakeFtBuffer&) = delete;

    [[nodiscard]] int port() const
    {
        return m_port;
    }

    [[nodiscard]] std::vector<std::uint16_t> commands()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_commands;
    }

private:
    static std::vector<std::byte> header()
    {
        std::vector<std::byte> meas; // the acquisition's tags in its (little-endian) byte order
        append_fiff_tag(meas, 100, 31, std::vector<std::byte>(20), false);
        append_fiff_tag(meas, 101, 3, fiff_words({-1}, false), false);
        append_fiff_tag(meas, 104, 3, fiff_words({100}, false), false);
        append_fiff_tag(meas, 104, 3, fiff_words({101}, false), false);
        append_fiff_tag(meas, 200, 3, fiff_words({3}, false), false);
        std::vector<std::byte> sfreq;
        append_value(sfreq, 600.0F);
        append_fiff_tag(meas, 201, 4, sfreq, false);
        append_fiff_tag(meas, 203, 30, fiff_channel(1, 3022, 112, 1e-13F, "MEG 0111"), false);
        append_fiff_tag(meas, 203, 30, fiff_channel(1, 3012, 201, 2e-13F, "MEG 0112"), false);
        append_fiff_tag(meas, 203, 30, fiff_channel(3, 0, -1, 1.0F, "STI 014"), false);
        append_fiff_tag(meas, 105, 3, fiff_words({101}, false), false);
        append_fiff_tag(meas, 104, 3, fiff_words({102}, false), false);

        std::vector<std::byte> isotrak; // a FIFF file with one cardinal point
        append_fiff_tag(isotrak, 100, 31, std::vector<std::byte>(20), true);
        append_fiff_tag(isotrak, 104, 3, fiff_words({107}, true), true);
        std::vector<std::byte> point = fiff_words({1, 2}, true);
        for (const float r : {0.0F, 0.1F, 0.0F}) {
            append_value(point, r, true);
        }
        append_fiff_tag(isotrak, 213, 33, point, true);
        append_fiff_tag(isotrak, 105, 3, fiff_words({107}, true), true);

        const std::string names("MEG 0111\0MEG 0112\0STI 014\0", 26);
        std::vector<std::byte> chunks;
        const auto chunk = [&](std::uint32_t type, std::span<const std::byte> data) {
            append_value(chunks, type);
            append_value(chunks, static_cast<std::uint32_t>(data.size()));
            chunks.insert(chunks.end(), data.begin(), data.end());
        };
        chunk(1, std::as_bytes(std::span(names)));
        chunk(8, meas);
        chunk(9, isotrak);

        std::vector<std::byte> out;
        for (const std::uint32_t word : {3U, 0U, 0U}) {
            append_value(out, word);
        }
        append_value(out, 600.0F);
        append_value(out, std::uint32_t{9}); // float32
        append_value(out, static_cast<std::uint32_t>(chunks.size()));
        out.insert(out.end(), chunks.begin(), chunks.end());
        return out;
    }

    static std::vector<std::byte> events()
    {
        std::vector<std::byte> out;
        const auto event = [&](std::uint32_t value_type, std::span<const std::byte> value, std::int32_t sample) {
            const std::string type = "trigger";
            for (const std::uint32_t word : {0U, static_cast<std::uint32_t>(type.size()), value_type, 1U}) {
                append_value(out, word);
            }
            for (const std::int32_t word : {sample, 0, 0}) {
                append_value(out, word);
            }
            append_value(out, static_cast<std::uint32_t>(type.size() + value.size()));
            const auto chars = std::as_bytes(std::span(type));
            out.insert(out.end(), chars.begin(), chars.end());
            out.insert(out.end(), value.begin(), value.end());
        };
        const std::int32_t code = 7;
        event(7, std::as_bytes(std::span(&code, 1)), 2);
        const std::string text = "start";
        event(0, std::as_bytes(std::span(text)), 4);
        return out;
    }

    static std::vector<std::byte> samples()
    {
        std::vector<std::byte> out;
        for (const std::uint32_t word : {3U, 8U, 9U, 8U * 3U * 4U}) {
            append_value(out, word);
        }
        for (int s = 0; s < 8; ++s) {
            for (const float v : {1e-12F * static_cast<float>(100 + s), -1e-11F * static_cast<float>(s), s >= 5 ? 3.0F : 0.0F}) {
                append_value(out, v);
            }
        }
        return out;
    }

    static bool read_all(int fd, std::byte* data, std::size_t size)
    {
        while (size > 0) {
            const ssize_t got = ::recv(fd, data, size, 0);
            if (got <= 0) {
                return false;
            }
            data += got;
            size -= static_cast<std::size_t>(got);
        }
        return true;
    }

    void answer(int fd, std::uint16_t command, const std::vector<std::byte>& payload)
    {
        std::vector<std::byte> out;
        append_value(out, std::uint16_t{1});
        append_value(out, command);
        append_value(out, static_cast<std::uint32_t>(payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
        static_cast<void>(::send(fd, out.data(), out.size(), 0));
    }

    void serve()
    {
        const int fd = ::accept(m_listener, nullptr, nullptr);
        if (fd < 0) {
            return;
        }
        int waits = 0;
        while (!m_stop) {
            std::array<std::byte, 8> def{};
            if (!read_all(fd, def.data(), def.size())) {
                break;
            }
            std::uint16_t command = 0;
            std::uint32_t size = 0;
            std::memcpy(&command, def.data() + 2, 2);
            std::memcpy(&size, def.data() + 4, 4);
            std::vector<std::byte> payload(size);
            if (!read_all(fd, payload.data(), size)) {
                break;
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_commands.push_back(command);
            }
            if (command == 0x201) {
                answer(fd, 0x204, header());
            } else if (command == 0x402) {
                // The stream starts empty; then the samples and events arrive; then a new header resets the counts
                const std::uint32_t samples = waits == 0 ? 0 : waits == 1 ? 8
                                                                          : 0;
                const std::uint32_t count = waits == 0 ? 0 : waits == 1 ? 2
                                                                        : 0;
                ++waits;
                std::vector<std::byte> counts;
                append_value(counts, samples);
                append_value(counts, count);
                answer(fd, 0x404, counts);
            } else if (command == 0x203) {
                answer(fd, 0x204, events());
            } else if (command == 0x202) {
                answer(fd, 0x204, samples());
            } else {
                answer(fd, 0x205, {});
            }
        }
        ::close(fd);
    }

    int m_listener = -1;
    int m_port = 0;
    std::atomic<bool> m_stop{false};
    std::mutex m_mutex;
    std::vector<std::uint16_t> m_commands;
    std::thread m_thread;
};

/**
 * A Fastrak at the far end of a pseudo-terminal: it records the commands it receives, answers the first "C"
 * with two updates of stations 1 and 2 (between them a station that is not used and lines that are no
 * records), then falls silent like a pressed stylus button, and answers the next "C" with one more update.
 */
class FakeFastrak
{
public:
    FakeFastrak()
    {
        m_master = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (m_master < 0 || ::grantpt(m_master) != 0 || ::unlockpt(m_master) != 0 || ::ptsname(m_master) == nullptr) {
            return;
        }
        m_port = ::ptsname(m_master);
        m_thread = std::thread([this] { serve(); });
    }

    ~FakeFastrak()
    {
        m_stop.store(true);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_master >= 0) {
            ::close(m_master);
        }
    }

    FakeFastrak(const FakeFastrak&) = delete;
    FakeFastrak& operator=(const FakeFastrak&) = delete;

    [[nodiscard]] const std::string& port() const
    {
        return m_port;
    }

    [[nodiscard]] std::vector<std::string> commands()
    {
        std::lock_guard lock(m_mutex);
        return m_commands;
    }

private:
    void send(std::string_view text) const
    {
        static_cast<void>(::write(m_master, text.data(), text.size()));
    }

    void serve()
    {
        std::string pending;
        int starts = 0;
        while (!m_stop.load()) {
            pollfd p{m_master, POLLIN, 0};
            if (::poll(&p, 1, 20) <= 0 || (p.revents & POLLIN) == 0) {
                std::this_thread::sleep_for(5ms); // no reader on the terminal yet
                continue;
            }
            std::array<char, 256> chunk{};
            const ssize_t got = ::read(m_master, chunk.data(), chunk.size());
            if (got <= 0) {
                continue;
            }
            pending.append(chunk.data(), static_cast<std::size_t>(got));
            for (std::size_t end = pending.find('\r'); end != std::string::npos; end = pending.find('\r')) {
                const std::string command = pending.substr(0, end);
                pending.erase(0, end + 1);
                {
                    std::lock_guard lock(m_mutex);
                    m_commands.push_back(command);
                }
                if (command != "C") {
                    continue;
                }
                if (++starts == 1) {
                    // Inches; station 1 with Euler angles, station 2 with a quaternion and fields run together
                    send("01   1.0000   2.0000  -3.0000   90.000    0.000    0.000\r\n"
                         "\r\n"
                         "03   9.0000   9.0000   9.0000    0.000    0.000    0.000\r\n"
                         "02 0.8829-0.3055-1.0000 0 0 0 2\r\n"
                         "bad record\r\n"
                         "01   0.0000   0.0000   1.0000\r\n"
                         "02   0.0000   1.0000   0.0000   0.000   0.000   0.000\r\n");
                } else {
                    send("01   2.0000   0.0000   0.0000    0.000    0.000    0.000\r\n"
                         "02   0.0000   2.0000   0.0000    0.000    0.000    0.000\r\n");
                }
            }
        }
    }

    int m_master = -1;
    std::string m_port;
    std::atomic<bool> m_stop{false};
    std::mutex m_mutex;
    std::vector<std::string> m_commands;
    std::thread m_thread;
};
#endif

} // namespace

//=============================================================================================================
/**
 * DECLARE CLASS TestAcqDevices
 *
 * @brief Vendor device sources against their simulators.
 */
class TestAcqDevices : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testBrainAmpScheme();
    void testBrainAmpStreamLayout();
    void testBrainAmpMatchesSynthetic();
    void testBrainAmpWaitsForGatingReader();
    void testBrainAmpResolution();
    void testBrainAmpClipsAtFullScale();
    void testBrainAmpNeedsAmplifiers();
    void testBrainAmpRealtimePacing();
    void testBrainAmpInvalidParameters_data();
    void testBrainAmpInvalidParameters();
    void testBrainAmpHardwareUnavailable();
    void testTmsiStreamLayout();
    void testTmsiMatchesSynthetic();
    void testTmsiRate_data();
    void testTmsiRate();
    void testTmsiChannelSubset();
    void testTmsiImpedances();
    void testTmsiWaitsForGatingReader();
    void testTmsiInvalidParameters_data();
    void testTmsiInvalidParameters();
    void testTmsiHardwareUnavailable();
    void testGusbampStreamLayout();
    void testGusbampChainsUnits();
    void testGusbampMatchesSynthetic();
    void testGusbampImpedances();
    void testGusbampWaitsForGatingReader();
    void testGusbampInvalidParameters_data();
    void testGusbampInvalidParameters();
    void testGusbampHardwareUnavailable();
    void testEegoStreamLayout();
    void testEegoMatchesSynthetic();
    void testEegoImpedances();
    void testEegoWaitsForGatingReader();
    void testEegoInvalidParameters_data();
    void testEegoInvalidParameters();
    void testEegoHardwareUnavailable();
    void testEegoDriverReadsTheSdk();
    void testNeuromagStreamLayout();
    void testNeuromagMatchesSynthetic();
    void testNeuromagWaitsForGatingReader();
    void testNeuromagInvalidParameters_data();
    void testNeuromagInvalidParameters();
    void testNeuromagHardwareUnavailable();
    void testNeuromagReadsTheDataServer();
    void testPolhemusStreamLayout();
    void testPolhemusSimulatorSweepsTheStylus();
    void testPolhemusButtonPressIsAnEvent();
    void testPolhemusInvalidParameters_data();
    void testPolhemusInvalidParameters();
    void testPolhemusHardwareUnavailable();
    void testPolhemusReadsTheSerialPort();
    void testFtBufferStreamLayout();
    void testFtBufferMatchesNeuromag();
    void testFtBufferWaitsForGatingReader();
    void testFtBufferInvalidParameters_data();
    void testFtBufferInvalidParameters();
    void testFtBufferServerUnavailable();
    void testFtBufferReadsTheServer();
};

//=============================================================================================================

void TestAcqDevices::initTestCase()
{
    QVERIFY(registerBrainAmpScheme().ok());
    QVERIFY(registerTmsiScheme().ok());
    QVERIFY(register_gusbamp_scheme().ok());
    QVERIFY(register_eego_scheme().ok());
    QVERIFY(register_neuromag_scheme().ok());
    QVERIFY(register_polhemus_scheme().ok());
    QVERIFY(register_ftbuffer_scheme().ok());
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpScheme()
{
    const auto schemes = AcqDeviceRegistry::instance().schemes();
    const auto it = std::find_if(schemes.begin(), schemes.end(), [](const AcqSchemeInfo& s) { return s.scheme == "brainamp"; });
    QVERIFY(it != schemes.end());
    QCOMPARE(it->exampleUri, std::string("brainamp://?simulate=1"));
    for (const char* name : {"simulate", "channels", "block", "resolution", "lowpass", "dc", "seed", "amplifiers", "realtime", "duration"}) {
        QVERIFY2(std::any_of(it->parameters.begin(), it->parameters.end(), [&](const AcqParameterInfo& p) { return p.name == name; }), name);
    }
    // Registering twice reports the first result
    QVERIFY(registerBrainAmpScheme().ok());
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpStreamLayout()
{
    auto device = AcqDevice::open("brainamp://?simulate=1&channels=40");
    QVERIFY(device);
    QCOMPARE(device->description().backend, std::string("brainamp"));
    QVERIFY(device->description().device.find("simulated") != std::string::npos);

    // The amplifier's wire format: 16-bit channels followed by the marker word, 5 kHz
    const auto info = device->stream().info();
    QCOMPARE(info->format, AcqSampleFormat::Int16);
    QCOMPARE(info->layout, AcqLayout::Interleaved);
    QCOMPARE(info->sfreq, 5000.0);
    QCOMPARE(info->channelCount(), std::size_t(41));
    QCOMPARE(info->channels.front().name, std::string("EEG 001"));
    QCOMPARE(info->channels[39].name, std::string("EEG 040"));
    QCOMPARE(info->channels.front().kind, AcqChannelKind::Eeg);
    QCOMPARE(info->channels.front().unit, AcqUnit::Volt);
    QCOMPARE(info->channels.front().scale, 100e-9);
    QCOMPARE(info->channels.back().name, std::string("STI 014"));
    QCOMPARE(info->channels.back().kind, AcqChannelKind::Stim);
    QCOMPARE(info->channels.back().scale, 1.0);
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpMatchesSynthetic()
{
    // The simulated amplifier digitises the synthetic session of the same seed
    const Recording amp = record("brainamp://?simulate=1&channels=8&seed=3&realtime=0&duration=2&block=100");
    const Recording ref = record("synthetic://?eeg=8&sfreq=5000&seed=3&realtime=0&duration=2&block=100");
    QCOMPARE(amp.channels, std::size_t(9));
    QCOMPARE(ref.channels, std::size_t(9));
    QCOMPARE(amp.frames(), std::size_t(10000));
    QCOMPARE(ref.frames(), amp.frames());

    const double step = 100e-9;
    double worst = 0.0;
    for (std::size_t s = 0; s < amp.frames(); ++s) {
        for (std::size_t c = 0; c < 8; ++c) {
            worst = std::max(worst, std::abs(amp.at(s, c) - ref.at(s, c)));
        }
        QCOMPARE(amp.at(s, 8), ref.at(s, 8));
    }
    QVERIFY2(worst <= 0.5 * step + 1e-12, qPrintable(QString::number(worst)));

    // Marker onsets become events, as the trigger codes of the session
    QVERIFY(ref.events.size() >= 1);
    QCOMPARE(amp.events.size(), ref.events.size());
    for (std::size_t e = 0; e < ref.events.size(); ++e) {
        QCOMPARE(amp.events[e].sample, ref.events[e].sample);
        QCOMPARE(amp.events[e].code, ref.events[e].code);
    }
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpWaitsForGatingReader()
{
    // Four seconds are more than the ring holds; the replay waits while the reader is busy
    auto device = AcqDevice::open("brainamp://?simulate=1&channels=4&realtime=0&duration=4");
    QVERIFY(device);
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(reader);
    QVERIFY(device->start().ok());
    std::this_thread::sleep_for(300ms);

    std::size_t frames = 0;
    while (true) {
        auto view = reader->acquire(1, 10s);
        if (!view) {
            QCOMPARE(view.error().code(), AcqErrorCode::EndOfStream);
            break;
        }
        frames += view->sampleCount();
        QVERIFY(reader->release(*view).ok());
    }
    QCOMPARE(frames, std::size_t(20000));
    QVERIFY(device->lastError().ok());
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpResolution()
{
    const Recording amp = record("brainamp://?simulate=1&channels=2&seed=1&realtime=0&duration=0.2&resolution=10uV");
    QCOMPARE(amp.scale.front(), 10e-6);
    QVERIFY(amp.frames() > 0);
    bool nonZero = false;
    for (std::size_t s = 0; s < amp.frames(); ++s) {
        const double steps = amp.at(s, 0) / 10e-6;
        QVERIFY(std::abs(steps - std::round(steps)) < 1e-6);
        nonZero |= amp.at(s, 0) != 0.0;
    }
    QVERIFY(nonZero);
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpClipsAtFullScale()
{
    // 100 nV steps cover +-3.28 mV; a session 1000 times stronger saturates instead of wrapping around
    const Recording amp = record("brainamp://?simulate=1&channels=4&seed=2&realtime=0&duration=0.5&gain=1000");
    QVERIFY(amp.frames() > 0);
    double low = 0.0;
    double high = 0.0;
    for (std::size_t s = 0; s < amp.frames(); ++s) {
        for (std::size_t c = 0; c < 4; ++c) {
            low = std::min(low, amp.at(s, c));
            high = std::max(high, amp.at(s, c));
        }
    }
    QCOMPARE(low, -32768 * 100e-9);
    QCOMPARE(high, 32767 * 100e-9);
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpNeedsAmplifiers()
{
    // 64 channels need two 32-channel amplifiers
    auto device = AcqDevice::open("brainamp://?simulate=1&channels=64&amplifiers=1");
    QVERIFY(!device);
    QCOMPARE(device.error().code(), AcqErrorCode::DeviceNotFound);
    QVERIFY(device.error().text().find("2 amplifiers") != std::string::npos);
    QVERIFY(AcqDevice::open("brainamp://?simulate=1&channels=64&amplifiers=2"));
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpRealtimePacing()
{
    auto device = AcqDevice::open("brainamp://?simulate=1&channels=4&block=50");
    QVERIFY(device);
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless);
    QVERIFY(reader);
    QVERIFY(device->start().ok());
    const auto begin = std::chrono::steady_clock::now();
    std::size_t samples = 0;
    while (samples < 1000) {
        auto view = reader->acquire(1, 5s);
        QVERIFY(view);
        QCOMPARE(view->sampleCount() % 50, std::size_t(0)); // whole driver blocks
        samples += view->sampleCount();
        static_cast<void>(reader->release(*view));
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    QVERIFY(device->stop().ok());
    QVERIFY2(seconds >= 0.15, qPrintable(QString::number(seconds))); // 1000 samples at 5 kHz take 0.2 s
    QCOMPARE(reader->stats().lost, std::uint64_t(0));
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpInvalidParameters_data()
{
    QTest::addColumn<QString>("uri");
    QTest::newRow("no channels") << "brainamp://?simulate=1&channels=0";
    QTest::newRow("too many channels") << "brainamp://?simulate=1&channels=129";
    QTest::newRow("resolution") << "brainamp://?simulate=1&resolution=1mV";
    QTest::newRow("lowpass") << "brainamp://?simulate=1&lowpass=500";
    QTest::newRow("block") << "brainamp://?simulate=1&block=0";
    QTest::newRow("amplifiers") << "brainamp://?simulate=1&amplifiers=5";
    QTest::newRow("target") << "brainamp://usb?simulate=1";
}

void TestAcqDevices::testBrainAmpInvalidParameters()
{
    QFETCH(QString, uri);
    QCOMPARE(openError(uri.toStdString()), AcqErrorCode::InvalidParameter);
}

//=============================================================================================================

void TestAcqDevices::testBrainAmpHardwareUnavailable()
{
#if defined(_WIN32) && defined(MNE_ACQ_BRAINAMP_HARDWARE)
    QSKIP("built with the BrainAmp driver interface");
#else
    auto device = AcqDevice::open("brainamp://");
    QVERIFY(!device);
    QCOMPARE(device.error().code(), AcqErrorCode::BackendUnavailable);
    QVERIFY(device.error().text().find("simulate=1") != std::string::npos);
#endif
}

//=============================================================================================================

void TestAcqDevices::testTmsiStreamLayout()
{
    auto device = AcqDevice::open("tmsi://?simulate=1&exg=8");
    QVERIFY(device);
    QCOMPARE(device->description().backend, std::string("tmsi"));
    QVERIFY(device->description().device.find("simulated Refa") != std::string::npos);

    // The SDK's format: an int32 per channel; ExG, bipolar and auxiliary inputs, digital inputs, test ramp
    const auto info = device->stream().info();
    QCOMPARE(info->format, AcqSampleFormat::Int32);
    QCOMPARE(info->layout, AcqLayout::Interleaved);
    QCOMPARE(info->sfreq, 2048.0);
    QCOMPARE(info->channelCount(), std::size_t(18));
    const std::vector<std::pair<std::size_t, std::string>> names = {{0, "EEG 001"}, {7, "EEG 008"}, {8, "BIP 1"}, {11, "BIP 4"}, {12, "AUX 1"}, {15, "AUX 4"}, {16, "STI 014"}, {17, "SAW"}};
    for (const auto& [index, name] : names) {
        QCOMPARE(info->channels[index].name, name);
    }
    QCOMPARE(info->channels[0].kind, AcqChannelKind::Eeg);
    QCOMPARE(info->channels[0].unit, AcqUnit::Volt);
    QCOMPARE(info->channels[0].scale, 0.0715256 * 1e-6);
    // Gain, offset and exponent: (raw * 1 + 100) * 1e-6 V
    QCOMPARE(info->channels[12].scale, 1e-6);
    QCOMPARE(info->channels[12].offset, 100e-6);
    QCOMPARE(info->channels[16].kind, AcqChannelKind::Stim);
    QCOMPARE(info->channels[16].unit, AcqUnit::None);
    QCOMPARE(info->channels[16].scale, 1.0);
    QCOMPARE(info->channels[17].kind, AcqChannelKind::Misc);
}

//=============================================================================================================

void TestAcqDevices::testTmsiMatchesSynthetic()
{
    // The simulated amplifier digitises the synthetic session of the same seed
    const Recording amp = record("tmsi://?simulate=1&exg=8&rate=1024&seed=3&realtime=0&duration=2");
    const Recording ref = record("synthetic://?eeg=16&sfreq=1024&seed=3&realtime=0&duration=2&block=8");
    QCOMPARE(amp.channels, std::size_t(18));
    QCOMPARE(ref.channels, std::size_t(17));
    QCOMPARE(amp.frames(), std::size_t(2048));
    QCOMPARE(ref.frames(), amp.frames());

    for (std::size_t s = 0; s < amp.frames(); ++s) {
        for (std::size_t c = 0; c < 16; ++c) {
            QVERIFY2(std::abs(amp.at(s, c) - ref.at(s, c)) <= 0.5 * amp.scale[c] + 1e-12, qPrintable(QString::number(c)));
        }
        QCOMPARE(amp.at(s, 16), ref.at(s, 16));
        QCOMPARE(amp.at(s, 17), static_cast<double>(s)); // the test ramp counts samples
    }
    QVERIFY(ref.events.size() >= 1);
    QCOMPARE(amp.events.size(), ref.events.size());
    for (std::size_t e = 0; e < ref.events.size(); ++e) {
        QCOMPARE(amp.events[e].sample, ref.events[e].sample);
        QCOMPARE(amp.events[e].code, ref.events[e].code);
    }
}

//=============================================================================================================

void TestAcqDevices::testTmsiRate_data()
{
    QTest::addColumn<double>("requested");
    QTest::addColumn<double>("rate");
    QTest::newRow("base") << 2048.0 << 2048.0;
    QTest::newRow("nearest above") << 1000.0 << 1024.0;
    QTest::newRow("nearest below") << 300.0 << 256.0;
    QTest::newRow("above the base") << 5000.0 << 2048.0;
    QTest::newRow("below the lowest") << 10.0 << 128.0;
}

void TestAcqDevices::testTmsiRate()
{
    // The amplifier divides its base rate by a power of two; the stream reports the rate it runs at
    QFETCH(double, requested);
    QFETCH(double, rate);
    auto device = AcqDevice::open(std::format("tmsi://?simulate=1&exg=4&rate={}", requested));
    QVERIFY(device);
    QCOMPARE(device->stream().info()->sfreq, rate);
}

//=============================================================================================================

void TestAcqDevices::testTmsiChannelSubset()
{
    const Recording amp = record("tmsi://?simulate=1&exg=8&channels=3&realtime=0&duration=0.5&seed=3");
    QCOMPARE(amp.channels, std::size_t(3));
    QCOMPARE(amp.frames(), std::size_t(1024));
    QVERIFY(amp.events.empty()); // the digital inputs are not delivered

    auto tooMany = AcqDevice::open("tmsi://?simulate=1&exg=8&channels=19");
    QVERIFY(!tooMany);
    QCOMPARE(tooMany.error().code(), AcqErrorCode::InvalidParameter);
    QVERIFY(tooMany.error().text().find("has 18") != std::string::npos);
}

//=============================================================================================================

void TestAcqDevices::testTmsiImpedances()
{
    auto device = AcqDevice::open("tmsi://?simulate=1&exg=8&seed=5");
    QVERIFY(device);
    auto impedances = device->measureImpedances();
    QVERIFY(impedances);
    QCOMPARE(impedances->size(), std::size_t(8));
    for (std::size_t i = 0; i < impedances->size(); ++i) {
        QCOMPARE((*impedances)[i].stream, std::size_t(0));
        QCOMPARE((*impedances)[i].channel, i);
        QVERIFY((*impedances)[i].ohms >= 1e3 && (*impedances)[i].ohms <= 20e3);
    }

    // Only the delivered ExG channels, and not while streaming
    auto subset = AcqDevice::open("tmsi://?simulate=1&exg=8&seed=5&channels=3");
    QVERIFY(subset);
    auto three = subset->measureImpedances();
    QVERIFY(three);
    QCOMPARE(three->size(), std::size_t(3));
    QCOMPARE(three->back().ohms, (*impedances)[2].ohms);
    QVERIFY(subset->start().ok());
    QCOMPARE(subset->measureImpedances().error().code(), AcqErrorCode::InvalidState);
}

//=============================================================================================================

void TestAcqDevices::testTmsiWaitsForGatingReader()
{
    // Four seconds are more than the ring holds; the replay waits while the reader is busy
    auto device = AcqDevice::open("tmsi://?simulate=1&exg=4&realtime=0&duration=4");
    QVERIFY(device);
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(reader);
    QVERIFY(device->start().ok());
    std::this_thread::sleep_for(300ms);

    std::size_t frames = 0;
    while (true) {
        auto view = reader->acquire(1, 10s);
        if (!view) {
            QCOMPARE(view.error().code(), AcqErrorCode::EndOfStream);
            break;
        }
        frames += view->sampleCount();
        QVERIFY(reader->release(*view).ok());
    }
    QCOMPARE(frames, std::size_t(8192));
    QVERIFY(device->lastError().ok());
}

//=============================================================================================================

void TestAcqDevices::testTmsiInvalidParameters_data()
{
    QTest::addColumn<QString>("uri");
    QTest::newRow("rate") << "tmsi://?simulate=1&rate=0";
    QTest::newRow("block") << "tmsi://?simulate=1&block=0";
    QTest::newRow("exg") << "tmsi://?simulate=1&exg=0";
    QTest::newRow("average") << "tmsi://?simulate=1&average=maybe";
    QTest::newRow("target") << "tmsi://usb?simulate=1";
}

void TestAcqDevices::testTmsiInvalidParameters()
{
    QFETCH(QString, uri);
    QCOMPARE(openError(uri.toStdString()), AcqErrorCode::InvalidParameter);
}

//=============================================================================================================

void TestAcqDevices::testTmsiHardwareUnavailable()
{
#if defined(_WIN32)
    QSKIP("Windows looks for the TMSi SDK");
#else
    auto device = AcqDevice::open("tmsi://");
    QVERIFY(!device);
    QCOMPARE(device.error().code(), AcqErrorCode::BackendUnavailable);
    QVERIFY(device.error().text().find("simulate=1") != std::string::npos);
#endif
}

//=============================================================================================================

void TestAcqDevices::testGusbampStreamLayout()
{
    auto device = AcqDevice::open("gusbamp://?simulate=1&inputs=1-4&trigger=1");
    QVERIFY(device);
    QCOMPARE(device->description().backend, std::string("gusbamp"));
    QVERIFY(device->description().device.find("simulated g.USBamp") != std::string::npos);

    // The API's format: a float per channel in microvolts, the master's trigger line last
    const auto info = device->stream().info();
    QCOMPARE(info->format, AcqSampleFormat::Float32);
    QCOMPARE(info->layout, AcqLayout::Interleaved);
    QCOMPARE(info->sfreq, 1200.0);
    QCOMPARE(info->channelCount(), std::size_t(5));
    QCOMPARE(info->channels[0].name, std::string("EEG 001"));
    QCOMPARE(info->channels[3].name, std::string("EEG 004"));
    QCOMPARE(info->channels[0].kind, AcqChannelKind::Eeg);
    QCOMPARE(info->channels[0].unit, AcqUnit::Volt);
    QCOMPARE(info->channels[0].scale, 1e-6);
    QCOMPARE(info->channels[4].name, std::string("STI 014"));
    QCOMPARE(info->channels[4].kind, AcqChannelKind::Stim);
    QCOMPARE(info->channels[4].scale, 1.0);
}

//=============================================================================================================

void TestAcqDevices::testGusbampChainsUnits()
{
    // The inputs of the master come first, then those of each slave
    auto device = AcqDevice::open("gusbamp://UB-1,UB-2,UB-3?simulate=1&inputs=2,5");
    QVERIFY(device);
    QVERIFY(device->description().device.find("UB-1 (master), UB-2, UB-3") != std::string::npos);
    const auto info = device->stream().info();
    QCOMPARE(info->channelCount(), std::size_t(6));
    QCOMPARE(info->channels.back().name, std::string("EEG 006"));
}

//=============================================================================================================

void TestAcqDevices::testGusbampMatchesSynthetic()
{
    // Two simulated units digitise the synthetic session of the same seed; the transfer headers are skipped
    const Recording amp = record("gusbamp://UB-1,UB-2?simulate=1&inputs=1-4&trigger=1&rate=256&seed=3&realtime=0&duration=2");
    const Recording ref = record("synthetic://?eeg=8&sfreq=256&seed=3&realtime=0&duration=2&block=16");
    QCOMPARE(amp.channels, std::size_t(9));
    QCOMPARE(ref.channels, std::size_t(9));
    QCOMPARE(amp.frames(), std::size_t(512));
    QCOMPARE(ref.frames(), amp.frames());

    for (std::size_t s = 0; s < amp.frames(); ++s) {
        for (std::size_t c = 0; c < 9; ++c) {
            QVERIFY2(std::abs(amp.at(s, c) - ref.at(s, c)) <= 1e-6 * std::abs(ref.at(s, c)) + 1e-15, qPrintable(QString::number(c)));
        }
    }
    QVERIFY(ref.events.size() >= 1);
    QCOMPARE(amp.events.size(), ref.events.size());
    for (std::size_t e = 0; e < ref.events.size(); ++e) {
        QCOMPARE(amp.events[e].sample, ref.events[e].sample);
        QCOMPARE(amp.events[e].code, ref.events[e].code);
    }
}

//=============================================================================================================

void TestAcqDevices::testGusbampImpedances()
{
    auto device = AcqDevice::open("gusbamp://UB-1,UB-2?simulate=1&seed=5");
    QVERIFY(device);
    auto impedances = device->measureImpedances();
    QVERIFY(impedances);
    QCOMPARE(impedances->size(), std::size_t(32));
    for (std::size_t i = 0; i < impedances->size(); ++i) {
        QCOMPARE((*impedances)[i].channel, i);
        QVERIFY((*impedances)[i].ohms >= 1e3 && (*impedances)[i].ohms <= 20e3);
    }

    // Each unit measures the set-up inputs, and not while streaming
    auto subset = AcqDevice::open("gusbamp://UB-1,UB-2?simulate=1&seed=5&inputs=3");
    QVERIFY(subset);
    auto two = subset->measureImpedances();
    QVERIFY(two);
    QCOMPARE(two->size(), std::size_t(2));
    QCOMPARE((*two)[0].ohms, (*impedances)[2].ohms);
    QCOMPARE((*two)[1].ohms, (*impedances)[18].ohms);
    QVERIFY(subset->start().ok());
    QCOMPARE(subset->measureImpedances().error().code(), AcqErrorCode::InvalidState);
}

//=============================================================================================================

void TestAcqDevices::testGusbampWaitsForGatingReader()
{
    // Four seconds are more than the ring holds; the replay waits while the reader is busy
    auto device = AcqDevice::open("gusbamp://?simulate=1&inputs=1-4&rate=256&realtime=0&duration=4");
    QVERIFY(device);
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(reader);
    QVERIFY(device->start().ok());
    std::this_thread::sleep_for(300ms);

    std::size_t frames = 0;
    while (true) {
        auto view = reader->acquire(1, 10s);
        if (!view) {
            QCOMPARE(view.error().code(), AcqErrorCode::EndOfStream);
            break;
        }
        frames += view->sampleCount();
        QVERIFY(reader->release(*view).ok());
    }
    QCOMPARE(frames, std::size_t(1024));
    QVERIFY(device->lastError().ok());
}

//=============================================================================================================

void TestAcqDevices::testGusbampInvalidParameters_data()
{
    QTest::addColumn<QString>("uri");
    QTest::newRow("rate") << "gusbamp://?simulate=1&rate=1000";
    QTest::newRow("input 0") << "gusbamp://?simulate=1&inputs=0";
    QTest::newRow("input 17") << "gusbamp://?simulate=1&inputs=1-17";
    QTest::newRow("descending range") << "gusbamp://?simulate=1&inputs=5-3";
    QTest::newRow("not a number") << "gusbamp://?simulate=1&inputs=x";
    QTest::newRow("empty inputs") << "gusbamp://?simulate=1&inputs=";
    QTest::newRow("trigger") << "gusbamp://?simulate=1&trigger=maybe";
    QTest::newRow("five units") << "gusbamp://A,B,C,D,E?simulate=1";
    QTest::newRow("twice the same unit") << "gusbamp://A,B,A?simulate=1";
    QTest::newRow("empty serial") << "gusbamp://A,,B?simulate=1";
}

void TestAcqDevices::testGusbampInvalidParameters()
{
    QFETCH(QString, uri);
    QCOMPARE(openError(uri.toStdString()), AcqErrorCode::InvalidParameter);
}

//=============================================================================================================

void TestAcqDevices::testGusbampHardwareUnavailable()
{
#if defined(_WIN32)
    QSKIP("Windows looks for the g.USBamp API");
#else
    auto device = AcqDevice::open("gusbamp://");
    QVERIFY(!device);
    QCOMPARE(device.error().code(), AcqErrorCode::BackendUnavailable);
    QVERIFY(device.error().text().find("simulate=1") != std::string::npos);
#endif
}

//=============================================================================================================

void TestAcqDevices::testEegoStreamLayout()
{
    auto device = AcqDevice::open("eego://?simulate=1&reference=8&bipolar=2");
    QVERIFY(device);
    QCOMPARE(device->description().backend, std::string("eego"));
    QVERIFY(device->description().device.find("simulated EE") != std::string::npos);

    // The SDK's format: doubles in volts; referential and bipolar inputs, trigger word, sample counter
    const auto info = device->stream().info();
    QCOMPARE(info->format, AcqSampleFormat::Float64);
    QCOMPARE(info->layout, AcqLayout::Interleaved);
    QCOMPARE(info->sfreq, 512.0);
    QCOMPARE(info->channelCount(), std::size_t(12));
    const std::vector<std::pair<std::size_t, std::string>> names = {{0, "EEG 001"}, {7, "EEG 008"}, {8, "BIP 1"}, {9, "BIP 2"}, {10, "STI 014"}, {11, "COUNTER"}};
    for (const auto& [index, name] : names) {
        QCOMPARE(info->channels[index].name, name);
    }
    QCOMPARE(info->channels[0].kind, AcqChannelKind::Eeg);
    QCOMPARE(info->channels[0].unit, AcqUnit::Volt);
    QCOMPARE(info->channels[0].scale, 1.0);
    QCOMPARE(info->channels[8].unit, AcqUnit::Volt);
    QCOMPARE(info->channels[10].kind, AcqChannelKind::Stim);
    QCOMPARE(info->channels[10].unit, AcqUnit::None);
    QCOMPARE(info->channels[11].kind, AcqChannelKind::Misc);
    const std::vector<std::pair<std::string, std::string>> ranges = {{"reference_range", "1 V"}, {"bipolar_range", "4 V"}};
    QVERIFY(info->properties == ranges);
}

//=============================================================================================================

void TestAcqDevices::testEegoMatchesSynthetic()
{
    // The simulated amplifier digitises the synthetic session of the same seed
    const Recording amp = record("eego://?simulate=1&reference=8&bipolar=2&rate=500&seed=3&realtime=0&duration=2");
    const Recording ref = record("synthetic://?eeg=10&sfreq=500&seed=3&realtime=0&duration=2&block=5");
    QCOMPARE(amp.channels, std::size_t(12));
    QCOMPARE(ref.channels, std::size_t(11));
    QCOMPARE(amp.frames(), std::size_t(1000));
    QCOMPARE(ref.frames(), amp.frames());

    for (std::size_t s = 0; s < amp.frames(); ++s) {
        for (std::size_t c = 0; c < 11; ++c) {
            QCOMPARE(amp.at(s, c), ref.at(s, c));
        }
        QCOMPARE(amp.at(s, 11), static_cast<double>(s)); // the sample counter
    }
    QVERIFY(ref.events.size() >= 1);
    QCOMPARE(amp.events.size(), ref.events.size());
    for (std::size_t e = 0; e < ref.events.size(); ++e) {
        QCOMPARE(amp.events[e].sample, ref.events[e].sample);
        QCOMPARE(amp.events[e].code, ref.events[e].code);
    }
}

//=============================================================================================================

void TestAcqDevices::testEegoImpedances()
{
    auto device = AcqDevice::open("eego://?simulate=1&reference=8&bipolar=2&seed=5");
    QVERIFY(device);
    auto impedances = device->measureImpedances();
    QVERIFY(impedances);
    QCOMPARE(impedances->size(), std::size_t(8));
    for (std::size_t i = 0; i < impedances->size(); ++i) {
        QCOMPARE((*impedances)[i].channel, i);
        QVERIFY((*impedances)[i].ohms >= 1e3 && (*impedances)[i].ohms <= 20e3);
    }
    QVERIFY(device->start().ok());
    QCOMPARE(device->measureImpedances().error().code(), AcqErrorCode::InvalidState);
}

//=============================================================================================================

void TestAcqDevices::testEegoWaitsForGatingReader()
{
    // Four seconds are more than the ring holds; the replay waits while the reader is busy
    auto device = AcqDevice::open("eego://?simulate=1&reference=4&bipolar=0&rate=1000&realtime=0&duration=4");
    QVERIFY(device);
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(reader);
    QVERIFY(device->start().ok());
    std::this_thread::sleep_for(300ms);

    std::size_t frames = 0;
    while (true) {
        auto view = reader->acquire(1, 10s);
        if (!view) {
            QCOMPARE(view.error().code(), AcqErrorCode::EndOfStream);
            break;
        }
        frames += view->sampleCount();
        QVERIFY(reader->release(*view).ok());
    }
    QCOMPARE(frames, std::size_t(4000));
    QVERIFY(device->lastError().ok());
}

//=============================================================================================================

void TestAcqDevices::testEegoInvalidParameters_data()
{
    QTest::addColumn<QString>("uri");
    QTest::newRow("rate") << "eego://?simulate=1&rate=600";
    QTest::newRow("reference range") << "eego://?simulate=1&reference_range=2";
    QTest::newRow("bipolar range") << "eego://?simulate=1&bipolar_range=9";
    QTest::newRow("no inputs") << "eego://?simulate=1&reference=0";
    QTest::newRow("realtime") << "eego://?simulate=1&realtime=maybe";
}

void TestAcqDevices::testEegoInvalidParameters()
{
    QFETCH(QString, uri);
    QCOMPARE(openError(uri.toStdString()), AcqErrorCode::InvalidParameter);
}

//=============================================================================================================

void TestAcqDevices::testEegoHardwareUnavailable()
{
    auto device = AcqDevice::open("eego://?sdk=/no/such/dir/eego-SDK");
    QVERIFY(!device);
    QCOMPARE(device.error().code(), AcqErrorCode::BackendUnavailable);
    QVERIFY(device.error().text().find("simulate=1") != std::string::npos);
}

//=============================================================================================================

void TestAcqDevices::testEegoDriverReadsTheSdk()
{
    // The driver against a stand-in SDK library with known samples
    const std::string sdk = "eego://?sdk=" EEGO_SDK_STUB;
    auto device = AcqDevice::open(sdk + "&rate=1000&reference_range=0.75");
    QVERIFY2(device, device ? "" : device.error().text().c_str());
    QCOMPARE(device->description().device, std::string("ANT Neuro EE-STUB STUB-1"));
    const auto info = device->stream().info();
    QCOMPARE(info->sfreq, 1000.0);
    QCOMPARE(info->channelCount(), std::size_t(7));
    QCOMPARE(info->channels[4].name, std::string("BIP 1"));
    QCOMPARE(info->channels[5].name, std::string("STI 014"));

    auto impedances = device->measureImpedances();
    QVERIFY(impedances);
    QCOMPARE(impedances->size(), std::size_t(4));
    QCOMPARE((*impedances)[3].ohms, 4e3);

    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(reader);
    QVERIFY(device->start().ok());
    std::vector<double> frames;
    std::vector<AcqEvent> events;
    while (frames.size() < 7 * 100) {
        auto view = reader->acquire(1, 5s);
        QVERIFY(view);
        for (std::size_t p = 0; p < view->partCount(); ++p) {
            const AcqBlockPart& part = view->part(p);
            for (std::size_t s = 0; s < part.samples; ++s) {
                for (std::size_t c = 0; c < 7; ++c) {
                    double v = 0.0;
                    std::memcpy(&v, part.at(c, s), sizeof(v));
                    frames.push_back(v);
                }
            }
        }
        for (std::size_t e = 0; e < view->events().size(); ++e) {
            events.push_back(view->events()[e]);
        }
        QVERIFY(reader->release(*view).ok());
    }
    QVERIFY(device->stop().ok());
    for (std::size_t s = 0; s < 100; ++s) {
        QCOMPARE(frames[s * 7 + 0], 1e-6 + static_cast<double>(s) * 1e-9);
        QCOMPARE(frames[s * 7 + 4], 5e-6 + static_cast<double>(s) * 1e-9);
        QCOMPARE(frames[s * 7 + 6], static_cast<double>(s));
    }
    QVERIFY(!events.empty());
    QCOMPARE(events.front().sample, std::uint64_t(20));
    QCOMPARE(events.front().code, 3);

    // The SDK's own refusals
    QCOMPARE(openError(sdk + "&rate=512"), AcqErrorCode::InvalidParameter);
    QCOMPARE(openError("eego://OTHER-2?sdk=" EEGO_SDK_STUB), AcqErrorCode::DeviceNotFound);
}

//=============================================================================================================

void TestAcqDevices::testNeuromagStreamLayout()
{
    auto device = AcqDevice::open("neuromag://?simulate=1&meg=4&eeg=2");
    QVERIFY(device);
    QCOMPARE(device->description().backend, std::string("neuromag"));
    QVERIFY(device->description().device.find("simulated Vectorview") != std::string::npos);

    // The acquisition's format: an int32 per channel, MEG, EEG, then STI 014, as the measurement info describes them
    const auto info = device->stream().info();
    QCOMPARE(info->format, AcqSampleFormat::Int32);
    QCOMPARE(info->sfreq, 1000.0);
    QCOMPARE(info->channelCount(), std::size_t(7));
    QCOMPARE(info->channels[0].name, std::string("MEG 0001"));
    QCOMPARE(info->channels[0].kind, AcqChannelKind::Meg);
    QCOMPARE(info->channels[0].unit, AcqUnit::Tesla);
    QCOMPARE(info->channels[0].scale, static_cast<double>(1e-15F));
    QVERIFY(info->channels[0].fiff);
    QCOMPARE(info->channels[0].fiff->coilType, 3024);
    QCOMPARE(info->channels[0].fiff->coordFrame, 1); // device
    QCOMPARE(info->channels[0].fiff->scannerNo, 1);
    QCOMPARE(info->channels[4].name, std::string("EEG 001"));
    QCOMPARE(info->channels[4].kind, AcqChannelKind::Eeg);
    QCOMPARE(info->channels[4].fiff->coordFrame, 4); // head
    QCOMPARE(info->channels[6].name, std::string("STI 014"));
    QCOMPARE(info->channels[6].kind, AcqChannelKind::Stim);
    // The whole measurement info travels with the stream, for the FIFF bridge
    QCOMPARE(info->extensions.size(), std::size_t(1));
    QCOMPARE(info->extensions[0].schema, std::string("fiff.MeasInfo/1"));
    QVERIFY(!info->extensions[0].data.empty());
}

//=============================================================================================================

void TestAcqDevices::testNeuromagMatchesSynthetic()
{
    // The simulated acquisition digitises the synthetic session of the same seed, MEG first
    const Recording amp = record("neuromag://?simulate=1&meg=4&eeg=2&rate=500&seed=3&realtime=0&duration=2");
    const Recording ref = record("synthetic://?meg=4&eeg=2&sfreq=500&seed=3&realtime=0&duration=2&block=100");
    QCOMPARE(amp.channels, std::size_t(7));
    QCOMPARE(ref.channels, std::size_t(7));
    QCOMPARE(amp.frames(), std::size_t(1000));
    QCOMPARE(ref.frames(), amp.frames());

    const std::array<std::size_t, 7> source = {2, 3, 4, 5, 0, 1, 6};
    for (std::size_t s = 0; s < amp.frames(); ++s) {
        for (std::size_t c = 0; c < 7; ++c) {
            QVERIFY2(std::abs(amp.at(s, c) - ref.at(s, source[c])) <= 0.5 * amp.scale[c] + 1e-30, qPrintable(QString::number(c)));
        }
    }
    QVERIFY(ref.events.size() >= 1);
    QCOMPARE(amp.events.size(), ref.events.size());
    for (std::size_t e = 0; e < ref.events.size(); ++e) {
        QCOMPARE(amp.events[e].sample, ref.events[e].sample);
        QCOMPARE(amp.events[e].code, ref.events[e].code);
    }
}

//=============================================================================================================

void TestAcqDevices::testNeuromagWaitsForGatingReader()
{
    // Six seconds are more than the ring holds; the replay waits while the reader is busy
    auto device = AcqDevice::open("neuromag://?simulate=1&meg=4&realtime=0&duration=6");
    QVERIFY(device);
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(reader);
    QVERIFY(device->start().ok());
    std::this_thread::sleep_for(300ms);

    std::size_t frames = 0;
    while (true) {
        auto view = reader->acquire(1, 10s);
        if (!view) {
            QCOMPARE(view.error().code(), AcqErrorCode::EndOfStream);
            break;
        }
        frames += view->sampleCount();
        QVERIFY(reader->release(*view).ok());
    }
    QCOMPARE(frames, std::size_t(6000));
    QVERIFY(device->lastError().ok());
}

//=============================================================================================================

void TestAcqDevices::testNeuromagInvalidParameters_data()
{
    QTest::addColumn<QString>("uri");
    QTest::newRow("target") << "neuromag://megpc?simulate=1";
    QTest::newRow("rate") << "neuromag://?simulate=1&rate=0";
    QTest::newRow("buffer") << "neuromag://?simulate=1&buffer=-1";
    QTest::newRow("autostart") << "neuromag://?simulate=1&autostart=maybe";
    QTest::newRow("timeout") << "neuromag://?simulate=1&timeout=-1";
    QTest::newRow("port") << "neuromag://?port=0";
}

void TestAcqDevices::testNeuromagInvalidParameters()
{
    QFETCH(QString, uri);
    QCOMPARE(openError(uri.toStdString()), AcqErrorCode::InvalidParameter);
}

//=============================================================================================================

void TestAcqDevices::testNeuromagHardwareUnavailable()
{
#if defined(_WIN32)
    QCOMPARE(openError("neuromag://"), AcqErrorCode::PlatformUnsupported);
#else
    auto device = AcqDevice::open("neuromag://?root=/no/such/dacq&autostart=0&timeout=0.5");
    QVERIFY(!device);
    QCOMPARE(device.error().code(), AcqErrorCode::DeviceNotFound);
    QVERIFY(device.error().text().find("/no/such/dacq") != std::string::npos);
#endif
}

//=============================================================================================================

void TestAcqDevices::testNeuromagReadsTheDataServer()
{
#if defined(_WIN32)
    QSKIP("the Neuromag acquisition runs on Linux");
#else
    FakeDacq dacq;
    QVERIFY(dacq.ok());
    const std::string uri = std::format("neuromag://?root={}&port={}&buffer=4", dacq.root(), dacq.port());
    auto device = AcqDevice::open(uri);
    QVERIFY2(device, device ? "" : device.error().text().c_str());
    const auto info = device->stream().info();
    QCOMPARE(info->sfreq, 600.0);
    QCOMPARE(info->channelCount(), std::size_t(3));
    QCOMPARE(info->channels[0].name, std::string("MEG 0111"));
    QCOMPARE(info->channels[0].unit, AcqUnit::Tesla);
    QCOMPARE(info->channels[1].name, std::string("MEG 0112"));
    QCOMPARE(info->channels[1].unit, AcqUnit::TeslaPerMeter);
    QCOMPARE(info->channels[1].scale, static_cast<double>(2.0F) * static_cast<double>(0.5F));
    QCOMPARE(info->channels[2].kind, AcqChannelKind::Stim);

    const Recording recorded = record_device(*device);
    // A buffer in the measurement file (big-endian), then one in the datagram; the closed file ends the stream
    QCOMPARE(recorded.frames(), std::size_t(8));
    for (std::size_t s = 0; s < 8; ++s) {
        QCOMPARE(recorded.at(s, 0), static_cast<double>(100 + s));
        QCOMPARE(recorded.at(s, 1), static_cast<double>(-static_cast<int>(s)));
        QCOMPARE(recorded.at(s, 2), s >= 5 ? 3.0 : 0.0);
    }
    QCOMPARE(recorded.events.size(), std::size_t(1));
    QCOMPARE(recorded.events[0].sample, std::uint64_t(5));
    QCOMPARE(recorded.events[0].code, 3);
    device->close();

    // The collector set the buffer and started and stopped both measurements: the info, then the data
    const std::vector<std::string> expected = {"pass", "name", "vara maxBuflen 4", "setu", "meas", "stop",
                                               "pass", "name", "vara maxBuflen 4", "setu", "meas", "stop"};
    QCOMPARE(dacq.commands(), expected);
#endif
}

//=============================================================================================================

void TestAcqDevices::testPolhemusStreamLayout()
{
    auto device = AcqDevice::open("polhemus://?simulate=1&stations=4,1,2");
    QVERIFY(device);
    QCOMPARE(device->description().backend, std::string("polhemus"));
    const auto info = device->stream().info();
    QCOMPARE(info->name, std::string("poses"));
    QCOMPARE(info->format, AcqSampleFormat::Float32);
    QCOMPARE(info->sfreq, 40.0); // 120 records per second shared by three stations
    QCOMPARE(info->channelCount(), std::size_t(21));
    const std::array<const char*, 7> names = {"X", "Y", "Z", "QW", "QX", "QY", "QZ"};
    for (std::size_t c = 0; c < 21; ++c) {
        const AcqChannelInfo& channel = info->channels[c];
        QCOMPARE(channel.name, std::format("{} {}", std::array{1, 2, 4}[c / 7], names[c % 7]));
        QCOMPARE(channel.kind, c % 7 < 3 ? AcqChannelKind::Position : AcqChannelKind::Orientation);
        QCOMPARE(channel.unit, c % 7 < 3 ? AcqUnit::Meter : AcqUnit::None);
    }
}

//=============================================================================================================

void TestAcqDevices::testPolhemusSimulatorSweepsTheStylus()
{
    // One second of 120 records for two stations; centimetres and inches describe the same sweep
    const Recording cm = record("polhemus://?simulate=1&units=cm&realtime=0&duration=1&seed=7");
    const Recording in = record("polhemus://?simulate=1&units=in&realtime=0&duration=1&seed=7");
    QCOMPARE(cm.channels, std::size_t(14));
    QCOMPARE(cm.frames(), std::size_t(60));
    QCOMPARE(in.frames(), cm.frames());
    QVERIFY(cm.events.empty());
    for (std::size_t f = 0; f < cm.frames(); ++f) {
        for (std::size_t c = 0; c < 14; ++c) {
            QVERIFY2(std::abs(cm.at(f, c) - in.at(f, c)) < 2e-5, qPrintable(QString("frame %1 channel %2").arg(f).arg(c)));
        }
        // The tracker circles at 10 cm without turning; the stylus turns about z as it sweeps
        QVERIFY(std::abs(std::hypot(cm.at(f, 7), cm.at(f, 8)) - 0.10) < 1e-4);
        QVERIFY(std::abs(cm.at(f, 10) - 1.0) < 1e-6);
        QVERIFY(std::abs(std::hypot(cm.at(f, 3), cm.at(f, 6)) - 1.0) < 1e-5);
        QVERIFY(std::abs(cm.at(f, 4)) < 1e-6 && std::abs(cm.at(f, 5)) < 1e-6);
    }
    // The stylus moves from frame to frame
    QVERIFY(std::abs(cm.at(1, 0) - cm.at(0, 0)) > 1e-3);
}

//=============================================================================================================

void TestAcqDevices::testPolhemusButtonPressIsAnEvent()
{
    // A press every quarter second (15 updates) pauses the output; the pause repeats the last poses in a frame
    // with the event, and the output starts again. The press with the last record ends the session instead.
    const Recording r = record("polhemus://?simulate=1&button=0.25&pause=80&duration=1");
    QCOMPARE(r.frames(), std::size_t(63));
    QCOMPARE(r.events.size(), std::size_t(3));
    for (std::size_t e = 0; e < r.events.size(); ++e) {
        const std::size_t at = 16 * e + 15;
        QCOMPARE(r.events[e].sample, std::uint64_t(at));
        QCOMPARE(r.events[e].code, 1);
        for (std::size_t c = 0; c < r.channels; ++c) {
            QCOMPARE(r.at(at, c), r.at(at - 1, c));
        }
    }
}

//=============================================================================================================

void TestAcqDevices::testPolhemusInvalidParameters_data()
{
    QTest::addColumn<QString>("uri");
    QTest::newRow("target") << "polhemus://COM3?simulate=1";
    QTest::newRow("station") << "polhemus://?simulate=1&stations=5";
    QTest::newRow("repeated station") << "polhemus://?simulate=1&stations=1,1";
    QTest::newRow("no station") << "polhemus://?simulate=1&stations=";
    QTest::newRow("units") << "polhemus://?simulate=1&units=mm";
    QTest::newRow("hemisphere zero") << "polhemus://?simulate=1&hemisphere=0,0,0";
    QTest::newRow("hemisphere short") << "polhemus://?simulate=1&hemisphere=1,2";
    QTest::newRow("baud") << "polhemus://?simulate=1&baud=0";
    QTest::newRow("pause") << "polhemus://?simulate=1&pause=0";
}

void TestAcqDevices::testPolhemusInvalidParameters()
{
    QFETCH(QString, uri);
    QCOMPARE(openError(uri.toStdString()), AcqErrorCode::InvalidParameter);
}

//=============================================================================================================

void TestAcqDevices::testPolhemusHardwareUnavailable()
{
#if defined(_WIN32)
    QCOMPARE(openError("polhemus://COM250"), AcqErrorCode::DeviceNotFound);
#else
    auto device = AcqDevice::open("polhemus:///no/such/port");
    QVERIFY(!device);
    QCOMPARE(device.error().code(), AcqErrorCode::DeviceNotFound);
    QVERIFY(device.error().text().find("/no/such/port") != std::string::npos);
#endif
}

//=============================================================================================================

void TestAcqDevices::testPolhemusReadsTheSerialPort()
{
#if defined(_WIN32)
    QSKIP("pseudo-terminals are POSIX");
#else
    FakeFastrak fastrak;
    QVERIFY(!fastrak.port().empty());
    auto device = AcqDevice::open(std::format("polhemus://{}?hemisphere=0,0,1", fastrak.port()));
    QVERIFY2(device, device ? "" : device.error().text().c_str());
    QVERIFY(device->description().device.find(fastrak.port()) != std::string::npos);
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless);
    QVERIFY(reader);
    QVERIFY(device->start().ok());

    std::vector<float> frames;
    std::vector<AcqEvent> events;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (frames.size() < 4 * 14 && std::chrono::steady_clock::now() < deadline) {
        auto view = reader->acquire(1, 100ms);
        if (!view) {
            continue;
        }
        const std::size_t first = frames.size();
        frames.resize(first + 14 * view->sampleCount());
        QVERIFY(decode(*view, AcqMatrixRef<float>::columnMajor(frames.data() + first, 14, view->sampleCount())).ok());
        for (std::size_t e = 0; e < view->events().size(); ++e) {
            events.push_back(view->events()[e]);
        }
        QVERIFY(reader->release(*view).ok());
    }
    device->close();
    QCOMPARE(frames.size(), std::size_t(4 * 14));

    // Inches to metres; azimuth 90 degrees turns about z; the quaternion 0 0 0 2 is normalised
    const auto at = [&](std::size_t frame, std::size_t channel) {
        return static_cast<double>(frames[frame * 14 + channel]);
    };
    const std::array<double, 14> first = {0.0254, 0.0508, -0.0762, std::sqrt(0.5), 0, 0, std::sqrt(0.5),
                                          0.8829 * 0.0254, -0.3055 * 0.0254, -0.0254, 0, 0, 0, 1};
    for (std::size_t c = 0; c < 14; ++c) {
        QVERIFY2(std::abs(at(0, c) - first[c]) < 1e-6, qPrintable(QString::number(c)));
    }
    // A record without angles keeps the identity
    QVERIFY(std::abs(at(1, 2) - 0.0254) < 1e-6 && at(1, 3) == 1.0);
    QVERIFY(std::abs(at(1, 8) - 0.0254) < 1e-6);
    // The silence after the second update was a button press: a frame at its poses, then the next update
    QCOMPARE(events.size(), std::size_t(1));
    QCOMPARE(events[0].sample, std::uint64_t(2));
    QCOMPARE(events[0].code, 1);
    for (std::size_t c = 0; c < 14; ++c) {
        QCOMPARE(at(2, c), at(1, c));
    }
    QVERIFY(std::abs(at(3, 0) - 0.0508) < 1e-6 && std::abs(at(3, 8) - 0.0508) < 1e-6);

    const std::vector<std::string> expected = {"P", "l1,1", "l2,1", "l3,0", "l4,0", "O1,2,4,1", "H1,0,0,1", "O2,2,4,1", "H2,0,0,1", "C", "C", "P"};
    QVERIFY(QTest::qWaitFor([&] { return fastrak.commands().size() >= expected.size(); }, 2000));
    QCOMPARE(fastrak.commands(), expected);
#endif
}

//=============================================================================================================

void TestAcqDevices::testFtBufferStreamLayout()
{
    auto device = AcqDevice::open("ftbuffer://?simulate=1&meg=4&eeg=2&rate=500");
    QVERIFY2(device, device ? "" : device.error().text().c_str());
    QCOMPARE(device->description().backend, std::string("ftbuffer"));
    // neuromag2ft puts the acquisition's channels and measurement info, and floats in physical units
    const auto info = device->stream().info();
    QCOMPARE(info->format, AcqSampleFormat::Float32);
    QCOMPARE(info->sfreq, 500.0);
    QCOMPARE(info->channelCount(), std::size_t(7));
    QCOMPARE(info->channels[0].name, std::string("MEG 0001"));
    QCOMPARE(info->channels[0].kind, AcqChannelKind::Meg);
    QCOMPARE(info->channels[0].unit, AcqUnit::Tesla);
    QCOMPARE(info->channels[0].scale, 1.0);
    QVERIFY(info->channels[0].fiff.has_value());
    QCOMPARE(info->channels[4].name, std::string("EEG 001"));
    QCOMPARE(info->channels[6].name, std::string("STI 014"));
    QCOMPARE(info->channels[6].kind, AcqChannelKind::Stim);
    QCOMPARE(info->extensions.size(), std::size_t(1));
    QCOMPARE(info->extensions[0].schema, std::string("fiff.MeasInfo/1"));
}

//=============================================================================================================

void TestAcqDevices::testFtBufferMatchesNeuromag()
{
    // The relay puts the acquisition's samples times calibration and range
    const Recording relayed = record("ftbuffer://?simulate=1&meg=4&eeg=2&rate=500&block=100&seed=3&realtime=0&duration=2");
    const Recording direct = record("neuromag://?simulate=1&meg=4&eeg=2&rate=500&seed=3&realtime=0&duration=2");
    QCOMPARE(relayed.channels, std::size_t(7));
    QCOMPARE(relayed.frames(), std::size_t(1000));
    QCOMPARE(direct.frames(), relayed.frames());
    for (std::size_t s = 0; s < relayed.frames(); ++s) {
        for (std::size_t c = 0; c < 7; ++c) {
            QVERIFY2(std::abs(relayed.at(s, c) - direct.at(s, c)) <= 1e-6 * std::abs(direct.at(s, c)) + 1e-30, qPrintable(QString::number(c)));
        }
    }
    QVERIFY(!direct.events.empty());
    QCOMPARE(relayed.events.size(), direct.events.size());
    for (std::size_t e = 0; e < direct.events.size(); ++e) {
        QCOMPARE(relayed.events[e].sample, direct.events[e].sample);
        QCOMPARE(relayed.events[e].code, direct.events[e].code);
    }
}

//=============================================================================================================

void TestAcqDevices::testFtBufferWaitsForGatingReader()
{
    // Six seconds are more than the stream's ring holds; the relay waits while the reader is busy
    auto device = AcqDevice::open("ftbuffer://?simulate=1&meg=4&realtime=0&duration=6");
    QVERIFY(device);
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless, gating);
    QVERIFY(reader);
    QVERIFY(device->start().ok());
    std::this_thread::sleep_for(300ms);

    std::size_t frames = 0;
    while (true) {
        auto view = reader->acquire(1, 10s);
        if (!view) {
            QCOMPARE(view.error().code(), AcqErrorCode::EndOfStream);
            break;
        }
        frames += view->sampleCount();
        QVERIFY(reader->release(*view).ok());
    }
    QCOMPARE(frames, std::size_t(6000));
    QVERIFY(device->lastError().ok());
}

//=============================================================================================================

void TestAcqDevices::testFtBufferInvalidParameters_data()
{
    QTest::addColumn<QString>("uri");
    QTest::newRow("target") << "ftbuffer://localhost?simulate=1";
    QTest::newRow("port") << "ftbuffer://localhost:0";
    QTest::newRow("port text") << "ftbuffer://localhost:ft";
    QTest::newRow("host") << "ftbuffer://:1972";
    QTest::newRow("timeout") << "ftbuffer://?simulate=1&timeout=0";
    QTest::newRow("channels") << "ftbuffer://?simulate=1&meg=0";
    QTest::newRow("rate") << "ftbuffer://?simulate=1&rate=0";
}

void TestAcqDevices::testFtBufferInvalidParameters()
{
    QFETCH(QString, uri);
    QCOMPARE(openError(uri.toStdString()), AcqErrorCode::InvalidParameter);
}

//=============================================================================================================

void TestAcqDevices::testFtBufferServerUnavailable()
{
    auto device = AcqDevice::open("ftbuffer://127.0.0.1:1?timeout=0.5");
    QVERIFY(!device);
    QCOMPARE(device.error().code(), AcqErrorCode::DeviceNotFound);
    QVERIFY(device.error().text().find("127.0.0.1:1") != std::string::npos);
}

//=============================================================================================================

void TestAcqDevices::testFtBufferReadsTheServer()
{
#if defined(_WIN32)
    QSKIP("the fake server uses POSIX sockets");
#else
    FakeFtBuffer buffer;
    QVERIFY(buffer.port() > 0);
    auto device = AcqDevice::open(std::format("ftbuffer://127.0.0.1:{}", buffer.port()));
    QVERIFY2(device, device ? "" : device.error().text().c_str());
    const auto info = device->stream().info();
    QCOMPARE(info->sfreq, 600.0);
    QCOMPARE(info->channelCount(), std::size_t(3));
    QCOMPARE(info->channels[0].name, std::string("MEG 0111"));
    QCOMPARE(info->channels[1].unit, AcqUnit::TeslaPerMeter);
    QCOMPARE(info->channels[1].scale, 1.0);
    QCOMPARE(info->channels[2].kind, AcqChannelKind::Stim);

    // The isotrak point joined the measurement info: a big-endian FIFF_DIG_POINT tag of 20 bytes
    const std::vector<std::byte>& file = info->extensions.at(0).data;
    std::vector<std::byte> dig;
    for (const std::int32_t word : {213, 33, 20}) {
        append_value(dig, word, true);
    }
    QVERIFY(std::search(file.begin(), file.end(), dig.begin(), dig.end()) != file.end());

    const Recording recorded = record_device(*device);
    QCOMPARE(recorded.frames(), std::size_t(8));
    for (std::size_t s = 0; s < 8; ++s) {
        QCOMPARE(static_cast<float>(recorded.at(s, 0)), 1e-12F * static_cast<float>(100 + s));
        QCOMPARE(static_cast<float>(recorded.at(s, 1)), -1e-11F * static_cast<float>(s));
    }
    // The numeric event of the buffer, then the change of STI 014; the event with a text value is not one
    QCOMPARE(recorded.events.size(), std::size_t(2));
    QCOMPARE(recorded.events[0].sample, std::uint64_t(2));
    QCOMPARE(recorded.events[0].code, 7);
    QCOMPARE(recorded.events[1].sample, std::uint64_t(5));
    QCOMPARE(recorded.events[1].code, 3);
    device->close();

    // Header, the counts at start, the counts with data, events, samples, and the new header's counts
    const std::vector<std::uint16_t> expected = {0x201, 0x402, 0x402, 0x203, 0x202, 0x402};
    QCOMPARE(buffer.commands(), expected);
#endif
}

//=============================================================================================================
// MAIN
//=============================================================================================================

QTEST_GUILESS_MAIN(TestAcqDevices)
#include "test_acq_devices.moc"
