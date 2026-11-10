//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     brainflow_backend.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    brainflow:// : any board BrainFlow supports, through BrainFlow's library when it is installed.
 *
 * The target is a BrainFlow board id or name (synthetic, cyton, ...); the parameters are BrainFlow's input
 * parameters (serial_port, ip_address, ...). One input stream "eeg" carries BrainFlow's rows as channels, a double
 * each in BrainFlow's units: microvolts for EEG, EMG, ECG and EOG (scale 1e-6 to volts), degrees Celsius for
 * temperature, seconds since 1970 for the timestamp row. The timestamp row is the device clock, and a nonzero
 * marker row value is an event at that sample.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "brainflow_library.h"

#include "../acq_devices.h"
#include "../../acq_backend.h"
#include "../../acq_device_registry.h"
#include "../../acq_thread.h"
#include "../../detail/acq_shared_library.h"
#include "../../detail/acq_uri_params.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>
#include <thread>

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

constexpr std::chrono::milliseconds kPoll(2);
constexpr std::chrono::seconds kClockRefresh(1);
constexpr int kBrainFlowBuffer = 450000; // samples BrainFlow keeps, as its bindings ask for
constexpr int kNoBoard = -100;           // BrainFlow's master_board default
constexpr int kUnsupportedBoard = 14;    // BrainFlow's UNSUPPORTED_BOARD_ERROR

// BrainFlow's input parameters
constexpr std::array<const char*, 10> kTextParams = {"serial_port", "mac_address", "ip_address", "ip_address_aux", "ip_address_anc",
                                                     "other_info", "serial_number", "file", "file_aux", "file_anc"};
constexpr std::array<const char*, 6> kIntParams = {"ip_port", "ip_port_aux", "ip_port_anc", "ip_protocol", "timeout", "master_board"};

/** A role of BrainFlow's rows; the first role of a row wins, as the ExG roles share rows. */
struct Role
{
    const char* key;
    const char* prefix; // channel name prefix; a single row is named prefix alone
    AcqChannelKind kind;
    AcqUnit unit;
    double scale;
};

constexpr Role kRoles[] = {
    {"eeg_channels", "EEG", AcqChannelKind::Eeg, AcqUnit::Volt, 1e-6},
    {"emg_channels", "EMG", AcqChannelKind::Emg, AcqUnit::Volt, 1e-6},
    {"ecg_channels", "ECG", AcqChannelKind::Ecg, AcqUnit::Volt, 1e-6},
    {"eog_channels", "EOG", AcqChannelKind::Eog, AcqUnit::Volt, 1e-6},
    {"accel_channels", "ACC", AcqChannelKind::Accelerometer, AcqUnit::Unknown, 1.0},
    {"gyro_channels", "GYRO", AcqChannelKind::Gyroscope, AcqUnit::Unknown, 1.0},
    {"eda_channels", "EDA", AcqChannelKind::Gsr, AcqUnit::Unknown, 1.0},
    {"temperature_channels", "TEMP", AcqChannelKind::Temperature, AcqUnit::DegreeCelsius, 1.0},
    {"ppg_channels", "PPG", AcqChannelKind::Misc, AcqUnit::Unknown, 1.0},
    {"optical_channels", "OPT", AcqChannelKind::Misc, AcqUnit::Unknown, 1.0},
    {"resistance_channels", "RES", AcqChannelKind::Misc, AcqUnit::Unknown, 1.0},
    {"magnetometer_channels", "MAG", AcqChannelKind::Misc, AcqUnit::Unknown, 1.0},
    {"rotation_channels", "ROT", AcqChannelKind::Misc, AcqUnit::Unknown, 1.0},
    {"analog_channels", "ANALOG", AcqChannelKind::Misc, AcqUnit::Unknown, 1.0},
    {"other_channels", "OTHER", AcqChannelKind::Misc, AcqUnit::Unknown, 1.0},
    {"package_num_channel", "PACKAGE", AcqChannelKind::Misc, AcqUnit::None, 1.0},
    {"battery_channel", "BATTERY", AcqChannelKind::Misc, AcqUnit::None, 1.0},
    {"timestamp_channel", "TIMESTAMP", AcqChannelKind::Timestamp, AcqUnit::Second, 1.0},
    {"marker_channel", "MARKER", AcqChannelKind::Marker, AcqUnit::None, 1.0},
};

struct Config
{
    std::string board; // id or name
    std::vector<std::string> library;
    std::string params; // BrainFlow's input parameters as JSON
    AcqThreadPolicy thread;
};

AcqError invalid(std::string what)
{
    return {AcqErrorCode::InvalidParameter, std::move(what)};
}

void appendJsonString(std::string& out, std::string_view text)
{
    out += '"';
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += std::format("\\u{:04x}", static_cast<unsigned>(c));
        } else {
            out += c;
        }
    }
    out += '"';
}

AcqExpected<Config> parseConfig(const AcqUri& uri)
{
    auto thread = paramThreadPolicy(uri);
    if (!thread) {
        return thread.error();
    }
    Config c;
    c.board = uri.target;
    if (c.board.empty()) {
        return invalid("brainflow:// needs a board id or name, e.g. brainflow://synthetic");
    }
    if (const auto library = uri.param("library")) {
        c.library = {std::string(*library)};
    } else {
        c.library = {acqEnvironment("MNE_CPP_BRAINFLOW_LIB")};
        if (const std::string pip = acqPythonPackageDir("brainflow", "lib"); !pip.empty()) {
            c.library.push_back(pip + "/" + acqSharedLibraryName("BoardController"));
        }
        c.library.push_back(acqSharedLibraryName("BoardController"));
    }
    // Every parameter is in the JSON: BrainFlow refuses a missing one
    c.params = "{";
    for (const char* key : kTextParams) {
        c.params += std::format("\"{}\":", key);
        appendJsonString(c.params, uri.param(key).value_or(""));
        c.params += ',';
    }
    for (const char* key : kIntParams) {
        const std::string_view name(key);
        const auto value = paramInt(uri, key, name == "master_board" ? kNoBoard : 0, name == "master_board" ? kNoBoard : 0, INT32_MAX);
        if (!value) {
            return value.error();
        }
        c.params += std::format("\"{}\":{},", key, *value);
    }
    c.params.back() = '}';
    c.thread = std::move(*thread);
    return c;
}

/** @return The board id of @p board, a number or one of BrainFlow's board names. */
AcqExpected<int> boardId(const BrainFlowLibrary& library, const std::string& board)
{
    int id = 0;
    const auto [end, error] = std::from_chars(board.data(), board.data() + board.size(), id);
    if (error == std::errc() && end == board.data() + board.size()) {
        return id;
    }
    return library.find_board(board);
}

/** @return The stream of @p description: a channel per row. */
AcqStreamInfo streamInfo(const BrainFlowDescription& description, int board)
{
    AcqStreamInfo info;
    info.name = "eeg";
    info.sourceId = std::format("brainflow:{}", board);
    info.format = AcqSampleFormat::Float64;
    info.layout = AcqLayout::Interleaved;
    info.sfreq = description.sampling_rate;
    info.channels.resize(description.rows);
    std::vector<bool> named(description.rows, false);
    for (const Role& role : kRoles) {
        const std::span<const int> rows = description.role(role.key);
        const bool eeg = std::string_view(role.key) == "eeg_channels" && description.eeg_names.size() == rows.size();
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const auto row = static_cast<std::size_t>(rows[r]);
            if (named[row]) {
                continue;
            }
            named[row] = true;
            AcqChannelInfo& channel = info.channels[row];
            channel.name = eeg ? description.eeg_names[r] : std::string(role.key).ends_with("_channel") ? role.prefix
                                                                                                        : std::format("{} {:03}", role.prefix, r + 1);
            channel.kind = role.kind;
            channel.unit = role.unit;
            channel.scale = role.scale;
        }
    }
    for (std::size_t row = 0; row < description.rows; ++row) {
        if (!named[row]) {
            info.channels[row].name = std::format("ROW {:03}", row);
            info.channels[row].kind = AcqChannelKind::Misc;
        }
    }
    return info;
}

class BrainFlowBackend final : public AcqBackend
{
public:
    static AcqExpected<std::unique_ptr<AcqBackend>> open(const AcqUri& uri)
    {
        auto config = parseConfig(uri);
        if (!config) {
            return config.error();
        }
        auto library = BrainFlowLibrary::load(config->library);
        if (!library) {
            return library.error();
        }
        auto board = boardId(**library, config->board);
        if (!board) {
            return board.error();
        }
        auto description = (*library)->description(*board);
        if (!description) {
            return description.error();
        }
        if (description->sampling_rate <= 0 || description->rows == 0) {
            return AcqError(AcqErrorCode::DeviceIo, std::format("BrainFlow board {} describes no samples", *board));
        }
        if (const int code = (*library)->prepare_session(*board, config->params.c_str()); code != 0) {
            if (code == kUnsupportedBoard) { // described, but not built into this platform's BrainFlow
                return AcqError(AcqErrorCode::PlatformUnsupported,
                                std::format("BrainFlow {} on this platform does not support board {} ({})", (*library)->version(), description->name, *board));
            }
            return brainflow_error(code, "prepare_session");
        }
        std::unique_ptr<BrainFlowBackend> backend(new BrainFlowBackend(std::move(*config), std::move(*library), *board, *description, uri.toString()));
        AcqStreamOptions options;
        options.capacitySamples = static_cast<std::size_t>(std::max(4 * description->sampling_rate, 1024));
        auto writer = backend->addInputStream(streamInfo(*description, *board), options);
        if (!writer) {
            return writer.error();
        }
        backend->m_writer = *writer;
        return std::unique_ptr<AcqBackend>(std::move(backend));
    }

    ~BrainFlowBackend() override
    {
        stop();
        m_library->release_session(m_board, m_config.params.c_str());
    }

    BrainFlowBackend(const BrainFlowBackend&) = delete;
    BrainFlowBackend& operator=(const BrainFlowBackend&) = delete;

    AcqDeviceDescription description() const override
    {
        return {"brainflow", m_library->version(), std::format("BrainFlow {} (board {})", m_name, m_board), m_uri};
    }

    AcqError start() override
    {
        if (const int code = m_library->start_stream(kBrainFlowBuffer, "", m_board, m_config.params.c_str()); code != 0) {
            return brainflow_error(code, "start_stream");
        }
        m_stop.store(false, std::memory_order_relaxed);
        m_clocked = false;
        auto thread = start_thread(m_config.thread, [this] { run(); });
        if (!thread) {
            m_library->stop_stream(m_board, m_config.params.c_str());
            return thread.error();
        }
        m_thread = std::move(*thread);
        return {};
    }

    void stop() override
    {
        m_stop.store(true, std::memory_order_release);
        if (m_thread.joinable()) {
            m_thread.join();
            m_library->stop_stream(m_board, m_config.params.c_str());
        }
    }

private:
    BrainFlowBackend(Config config, std::shared_ptr<const BrainFlowLibrary> library, int board, const BrainFlowDescription& description, std::string uri)
    : m_config(std::move(config))
    , m_library(std::move(library))
    , m_board(board)
    , m_name(description.name)
    , m_uri(std::move(uri))
    , m_rows(description.rows)
    , m_max_block(static_cast<std::size_t>(std::max(description.sampling_rate, 256)))
    {
        if (const auto row = description.role("timestamp_channel"); !row.empty()) {
            m_timestamp_row = static_cast<std::size_t>(row.front());
        }
        if (const auto row = description.role("marker_channel"); !row.empty()) {
            m_marker_row = static_cast<std::size_t>(row.front());
        }
        m_sfreq = description.sampling_rate;
        m_data.reserve(m_rows * m_max_block);
    }

    void run()
    {
        while (!m_stop.load(std::memory_order_acquire)) {
            int available = 0;
            if (const int code = m_library->get_board_data_count(0, &available, m_board, m_config.params.c_str()); code != 0) {
                reportError(brainflow_error(code, "get_board_data_count"));
                return;
            }
            if (available <= 0) {
                std::this_thread::sleep_for(kPoll);
                continue;
            }
            const std::size_t samples = std::min(static_cast<std::size_t>(available), m_max_block);
            m_data.resize(m_rows * samples);
            if (const int code = m_library->get_board_data(static_cast<int>(samples), 0, m_data.data(), m_board, m_config.params.c_str()); code != 0) {
                reportError(brainflow_error(code, "get_board_data"));
                return;
            }
            if (!write(samples)) {
                return;
            }
        }
    }

    /** Transposes BrainFlow's rows of @p samples samples into the ring, with their markers and clock. */
    bool write(std::size_t samples)
    {
        AcqExpected<AcqWriteView> view = AcqError(AcqErrorCode::Timeout);
        while (!view && view.error() == AcqErrorCode::Timeout) {
            if (m_stop.load(std::memory_order_acquire)) {
                return false;
            }
            view = m_writer->beginWrite(samples, kPoll); // a gating reader may not have freed the space yet
        }
        if (!view) {
            reportError(view.error());
            return false;
        }
        // BrainFlow's buffer is row after row; the stream is sample after sample
        std::size_t done = 0;
        for (std::size_t p = 0; p < view->partCount(); ++p) {
            const AcqWritePart& part = view->part(p);
            for (std::size_t s = 0; s < part.samples; ++s, ++done) {
                for (std::size_t r = 0; r < m_rows; ++r) {
                    std::memcpy(part.at(r, s), &m_data[r * samples + done], sizeof(double));
                }
            }
        }
        const std::uint64_t first = m_writer->writePosition();
        if (m_marker_row) {
            const double* markers = m_data.data() + *m_marker_row * samples;
            for (std::size_t s = 0; s < samples; ++s) {
                if (markers[s] != 0.0) {
                    if (AcqError e = m_writer->pushEvent({first + s, 0, static_cast<std::int32_t>(std::lround(markers[s]))}); !e.ok()) {
                        reportError(e);
                        return false;
                    }
                }
            }
        }
        m_writer->commit(samples);
        if (m_timestamp_row) {
            publishClock(first + samples - 1, m_data[*m_timestamp_row * samples + samples - 1]);
        }
        return true;
    }

    /** Anchors the clock at @p sample, taken at @p timestamp seconds since 1970; refreshed once a second. */
    void publishClock(std::uint64_t sample, double timestamp)
    {
        const auto steady = std::chrono::steady_clock::now();
        if (m_clocked && steady - m_last_clock < kClockRefresh) {
            return;
        }
        m_clocked = true;
        m_last_clock = steady;
        const auto steadyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(steady.time_since_epoch()).count();
        const auto utcNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        AcqClockModel clock;
        clock.sfreq = m_sfreq;
        clock.anchorSample = sample;
        clock.utcOffsetNs = utcNs - steadyNs;
        clock.anchorHostNs = std::llround(timestamp * 1e9) - clock.utcOffsetNs;
        clock.source = AcqClockSource::Device;
        m_writer->setClock(clock);
    }

    Config m_config;
    std::shared_ptr<const BrainFlowLibrary> m_library;
    int m_board = 0;
    std::string m_name;
    std::string m_uri;
    std::size_t m_rows = 0;
    std::size_t m_max_block = 0; // samples of a read
    double m_sfreq = 0.0;
    std::optional<std::size_t> m_timestamp_row;
    std::optional<std::size_t> m_marker_row;
    std::vector<double> m_data; // BrainFlow's rows of the last read, reused
    AcqWriter* m_writer = nullptr;
    bool m_clocked = false;
    std::chrono::steady_clock::time_point m_last_clock;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};

AcqSchemeInfo schemeInfo()
{
    AcqSchemeInfo info;
    info.scheme = "brainflow";
    info.description = "Any board BrainFlow supports, through BrainFlow's BoardController library (BrainFlow 5, loaded at run time); "
                       "the target is a BrainFlow board id or name, the parameters are BrainFlow's input parameters.";
    info.exampleUri = "brainflow://synthetic";
    info.parameters = {
        {"library", "string", "", "Path of BoardController; default MNE_CPP_BRAINFLOW_LIB, then pip's brainflow package, then the system loader."},
        {"serial_port", "string", "", "BrainFlow input parameter: serial port, e.g. /dev/ttyUSB0 or COM3."},
        {"mac_address", "string", "", "BrainFlow input parameter: Bluetooth MAC address."},
        {"ip_address", "string", "", "BrainFlow input parameter: IP address."},
        {"ip_address_aux", "string", "", "BrainFlow input parameter: auxiliary IP address."},
        {"ip_address_anc", "string", "", "BrainFlow input parameter: ancillary IP address."},
        {"ip_port", "int", "0", "BrainFlow input parameter: IP port."},
        {"ip_port_aux", "int", "0", "BrainFlow input parameter: auxiliary IP port."},
        {"ip_port_anc", "int", "0", "BrainFlow input parameter: ancillary IP port."},
        {"ip_protocol", "int", "0", "BrainFlow input parameter: 0 none, 1 UDP, 2 TCP."},
        {"other_info", "string", "", "BrainFlow input parameter: board-specific settings."},
        {"timeout", "int", "0", "BrainFlow input parameter: timeout in seconds for device discovery."},
        {"serial_number", "string", "", "BrainFlow input parameter: serial number."},
        {"file", "string", "", "BrainFlow input parameter: file, e.g. for the playback board."},
        {"file_aux", "string", "", "BrainFlow input parameter: auxiliary file."},
        {"file_anc", "string", "", "BrainFlow input parameter: ancillary file."},
        {"master_board", "int", "-100", "BrainFlow input parameter: board id behind the streaming and playback boards."},
        {"cpus", "string", "", "Comma-separated CPUs for the acquisition thread."},
        {"priority", "string", "normal", "Acquisition thread priority: normal, high or realtime."},
    };
    return info;
}

/** Registers brainflow:// when the library is loaded. */
struct Registrar
{
    Registrar() noexcept
    {
        static_cast<void>(register_brainflow_scheme());
    }
};

const Registrar kRegistrar;

} // namespace

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

AcqError ACQLIB::register_brainflow_scheme()
{
    static const AcqError result = AcqDeviceRegistry::instance().registerScheme(schemeInfo(), &BrainFlowBackend::open);
    return result;
}
