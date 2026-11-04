//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     main.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    mne_acq: list, inspect, record and re-publish acquisition sources from the command line.
 *
 *   mne_acq list [--timeout s]                         schemes and reachable sources with ready-to-use URIs
 *   mne_acq info <uri>                                 device, streams and channels of a source
 *   mne_acq record <uri> <file.fif> [--duration s]     record the first stream to FIFF
 *   mne_acq stream <uri> --lsl [--name n] [--duration s]   publish the first stream as an LSL outlet
 *
 * record and stream run until the duration has passed, the source ends (e.g. a file replay without loop) or
 * Ctrl+C. Exit codes: 0 success, 1 usage error, 2 the source or the output failed.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <acq/acq.h>
#include <acq/fiff/acq_fiff_replay.h>
#include <acq/fiff/acq_recorder.h>
#include <acq/lsl/acq_lsl.h>

#include <lsl/lsl_stream_info.h>
#include <lsl/lsl_stream_outlet.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <thread>
#include <vector>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace std::chrono_literals;

namespace
{

constexpr int kUsageError = 1;
constexpr int kRunError = 2;

std::atomic<bool> g_interrupted{false};

/** Standard output; flushed when the program ends. */
QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

/** Standard error; flushed when the program ends. */
QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

extern "C" void onInterrupt(int /*signal*/)
{
    g_interrupted.store(true);
}

const char* formatName(AcqSampleFormat format)
{
    switch (format) {
        case AcqSampleFormat::Int16:
            return "int16";
        case AcqSampleFormat::Int24Packed:
            return "int24";
        case AcqSampleFormat::Int32:
            return "int32";
        case AcqSampleFormat::Float32:
            return "float32";
        case AcqSampleFormat::Float64:
            return "float64";
        case AcqSampleFormat::ComplexInt16:
            return "complex int16";
        case AcqSampleFormat::ComplexFloat32:
            return "complex float32";
    }
    return "?";
}

const char* kindName(AcqChannelKind kind)
{
    static constexpr const char* kNames[] = {"unknown", "EEG", "MEG", "OPM", "ECG", "EOG", "ERG", "EMG",
                                             "sEEG", "ECoG", "DBS", "fNIRS", "respiration", "GSR", "stim",
                                             "accelerometer", "gyroscope", "temperature", "timestamp", "marker"};
    const auto index = static_cast<std::size_t>(kind);
    return index < std::size(kNames) ? kNames[index] : "other";
}

std::string unitName(const AcqChannelInfo& channel)
{
    static constexpr const char* kUnits[] = {"?", "", "V", "A", "T", "T/m", "Am", "m/s^2", "rad/s", "degC",
                                             "s", "Ohm", "W", "Pa", "M"};
    const auto index = static_cast<std::size_t>(channel.unit);
    std::string unit = index < std::size(kUnits) ? kUnits[index] : "?";
    if (channel.unitExponent != 0 && !unit.empty()) {
        unit = "1e" + std::to_string(channel.unitExponent) + " " + unit;
    }
    return unit;
}

/** Opens @p uri, printing the error; schemes of the bridge libraries are registered first. */
AcqExpected<AcqDevice> openSource(const QString& uri)
{
    registerFiffScheme();
    registerLslScheme();
    auto device = AcqDevice::open(uri.toStdString());
    if (!device) {
        err() << QString::asprintf("mne_acq: %s: %s\n", qPrintable(uri), device.error().text().c_str());
    }
    return device;
}

/** Waits until @p done(), the duration (0 = none) has passed or Ctrl+C. */
template<typename Done>
void waitUntil(double seconds, Done done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!done() && !g_interrupted.load() && (seconds <= 0.0 || std::chrono::steady_clock::now() < deadline)) {
        std::this_thread::sleep_for(20ms);
    }
}

//=============================================================================================================

int list(double timeout)
{
    registerFiffScheme();
    registerLslScheme();
    AcqDeviceRegistry& registry = AcqDeviceRegistry::instance();
    out() << QString::asprintf("Schemes:\n");
    for (const AcqSchemeInfo& scheme : registry.schemes()) {
        out() << QString::asprintf("  %-14s %s\n", (scheme.scheme + "://").c_str(), scheme.description.c_str());
    }
    out() << QString::asprintf("Sources (searched for %.1f s):\n", timeout);
    const auto found = registry.discover(std::chrono::milliseconds(static_cast<long long>(timeout * 1000.0)));
    if (found.empty()) {
        out() << QString::asprintf("  none\n");
    }
    for (const AcqDiscoveredDevice& device : found) {
        out() << QString::asprintf("  %s  %s (%s)%s\n", device.uri.c_str(), device.label.c_str(), device.detail.c_str(),
                                   device.available ? "" : " [unavailable]");
    }
    return 0;
}

//=============================================================================================================

int info(const QString& uri)
{
    auto device = openSource(uri);
    if (!device) {
        return kRunError;
    }
    const AcqDeviceDescription description = device->description();
    out() << QString::asprintf("%s\n  backend   %s%s\n  device    %s\n", description.uri.c_str(), description.backend.c_str(),
                               description.backendVersion.empty() ? "" : (" (" + description.backendVersion + ")").c_str(),
                               description.device.c_str());
    for (const AcqStream& stream : device->streams()) {
        const auto streamInfo = stream.info();
        out() << QString::asprintf("Stream \"%s\": %zu channels, %s, %g Hz%s%s\n", streamInfo->name.c_str(), streamInfo->channels.size(),
                                   formatName(streamInfo->format), streamInfo->sfreq,
                                   streamInfo->clockDomain.empty() ? "" : ", clock ", streamInfo->clockDomain.c_str());
        for (const AcqChannelInfo& channel : streamInfo->channels) {
            out() << QString::asprintf("  %-16s %-12s %s\n", channel.name.c_str(), kindName(channel.kind), unitName(channel).c_str());
        }
    }
    device->close();
    return 0;
}

//=============================================================================================================

int record(const QString& uri, const QString& path, double duration)
{
    auto device = openSource(uri);
    if (!device) {
        return kRunError;
    }
    AcqRecorderOptions options;
    options.device = device->description();
    auto recorder = AcqRecorder::start(device->stream(), path.toStdString(), options);
    if (!recorder) {
        err() << QString::asprintf("mne_acq: %s: %s\n", qPrintable(path), recorder.error().text().c_str());
        return kRunError;
    }
    if (const AcqError started = device->start(); !started.ok()) {
        err() << QString::asprintf("mne_acq: %s\n", started.text().c_str());
        return kRunError;
    }
    waitUntil(duration, [&] { return !recorder->isRecording(); });
    device->close();
    const AcqError stopped = recorder->stop();
    const AcqRecorderStats stats = recorder->stats();
    out() << QString::asprintf("Recorded %llu samples (%llu lost) and %llu events to %zu file(s):\n",
                               static_cast<unsigned long long>(stats.samples), static_cast<unsigned long long>(stats.lost),
                               static_cast<unsigned long long>(stats.events), recorder->files().size());
    for (const std::string& file : recorder->files()) {
        out() << QString::asprintf("  %s\n", file.c_str());
    }
    if (!stopped.ok()) {
        err() << QString::asprintf("mne_acq: %s\n", stopped.text().c_str());
        return kRunError;
    }
    return 0;
}

//=============================================================================================================

int streamLsl(const QString& uri, const QString& name, double duration)
{
    auto device = openSource(uri);
    if (!device) {
        return kRunError;
    }
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless);
    if (!reader) {
        err() << QString::asprintf("mne_acq: %s\n", reader.error().text().c_str());
        return kRunError;
    }
    const auto streamInfo = reader->stream().info();
    const std::size_t channels = streamInfo->channels.size();
    const std::string type = channels > 0 ? kindName(streamInfo->channels.front().kind) : "unknown";
    LSLLIB::stream_outlet outlet(LSLLIB::stream_info(name.isEmpty() ? streamInfo->name : name.toStdString(), type,
                                                     static_cast<int>(channels), streamInfo->sfreq,
                                                     LSLLIB::ChannelFormat::Float32, device->description().uri));
    if (const AcqError started = device->start(); !started.ok()) {
        err() << QString::asprintf("mne_acq: %s\n", started.text().c_str());
        return kRunError;
    }
    out() << QString::asprintf("Publishing %s as LSL stream \"%s\" (%zu channels, %g Hz, float32)\n", qPrintable(uri),
                               outlet.info().name().c_str(), channels, streamInfo->sfreq);
    out().flush();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(duration);
    std::vector<float> values;
    std::vector<std::vector<float>> chunk;
    std::uint64_t published = 0;
    bool ended = false;
    while (!ended && !g_interrupted.load() && (duration <= 0.0 || std::chrono::steady_clock::now() < deadline)) {
        auto view = reader->acquire(1, 100ms);
        if (!view) {
            ended = view.error().code() != AcqErrorCode::Timeout;
            continue;
        }
        const std::size_t samples = view->sampleCount();
        values.resize(samples * channels);
        // channels x samples, column-major: sample by sample, as LSL sends it
        const AcqError decoded = decode(*view, AcqMatrixRef<float>::columnMajor(values.data(), channels, samples));
        static_cast<void>(reader->release(*view)); // clears the view
        if (!decoded.ok()) {
            err() << QString::asprintf("mne_acq: %s\n", decoded.text().c_str());
            return kRunError;
        }
        chunk.assign(samples, std::vector<float>(channels));
        for (std::size_t s = 0; s < chunk.size(); ++s) {
            std::copy_n(values.data() + s * channels, channels, chunk[s].begin());
        }
        outlet.push_chunk(chunk);
        published += chunk.size();
    }
    device->close();
    out() << QString::asprintf("Published %llu samples\n", static_cast<unsigned long long>(published));
    return 0;
}

} // namespace

//=============================================================================================================
// MAIN
//=============================================================================================================

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("mne_acq");
    QCoreApplication::setApplicationVersion(MNE_CPP_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "List, inspect, record and re-publish acquisition sources.\n\n"
        "  mne_acq list [--timeout s]\n"
        "  mne_acq info <uri>\n"
        "  mne_acq record <uri> <file.fif> [--duration s]\n"
        "  mne_acq stream <uri> --lsl [--name n] [--duration s]\n\n"
        "URIs: synthetic://?eeg=32&sfreq=1000, fiff:///data/raw.fif?loop=1, lsl://NAME, ... (see mne_acq list).");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("command", "list, info, record or stream.");
    parser.addPositionalArgument("uri", "Source URI.", "[uri]");
    parser.addPositionalArgument("file", "Output file of record.", "[file]");
    const QCommandLineOption timeoutOpt("timeout", "Seconds to search for sources (list, default 1).", "s", "1");
    const QCommandLineOption durationOpt("duration", "Seconds to run (record, stream); 0 until the source ends or Ctrl+C.", "s", "0");
    const QCommandLineOption lslOpt("lsl", "Publish as a Lab Streaming Layer outlet (stream).");
    const QCommandLineOption nameOpt("name", "Name of the published stream (stream; default: the stream's name).", "n");
    parser.addOptions({timeoutOpt, durationOpt, lslOpt, nameOpt});
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString command = args.value(0);
    bool ok = true;
    const double timeout = parser.value(timeoutOpt).toDouble(&ok);
    bool durationOk = true;
    const double duration = parser.value(durationOpt).toDouble(&durationOk);
    if (!ok || timeout < 0.0 || !durationOk || duration < 0.0) {
        err() << QString::asprintf("mne_acq: --timeout and --duration need non-negative seconds\n");
        return kUsageError;
    }
    std::signal(SIGINT, onInterrupt);
    std::signal(SIGTERM, onInterrupt);

    if (command == QLatin1String("list") && args.size() == 1) {
        return list(timeout);
    }
    if (command == QLatin1String("info") && args.size() == 2) {
        return info(args[1]);
    }
    if (command == QLatin1String("record") && args.size() == 3) {
        return record(args[1], args[2], duration);
    }
    if (command == QLatin1String("stream") && args.size() == 2) {
        if (!parser.isSet(lslOpt)) {
            err() << QString::asprintf("mne_acq: stream needs an output, e.g. --lsl\n");
            return kUsageError;
        }
        return streamLsl(args[1], parser.value(nameOpt), duration);
    }
    err() << QString::asprintf("%s\n", qPrintable(parser.helpText()));
    return kUsageError;
}
