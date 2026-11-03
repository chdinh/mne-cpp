//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     main.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    mne_acq_bench: performance suite of the acquisition library.
 *
 * Measures the per-call cost of the stream protocol, commit-to-wake latency, sustained ring throughput over a
 * channel sweep and the converters, and writes a table and optionally a JSON report. The measured code is
 * standard C++ against the ACQLIB API only, so the tool also builds against an older mne_acq for A/B runs.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <acq/acq.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <chrono>
#include <complex>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef MNE_CPP_VERSION // set by the MNE-CPP build; absent in the standalone A/B build (scripts/ci/acq_bench)
#define MNE_CPP_VERSION "standalone"
#endif

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace std::chrono_literals;

namespace
{
using Clock = std::chrono::steady_clock;

struct Metric
{
    std::string name;
    double value = 0.0;
    const char* unit = "";
    bool higherIsBetter = false;
};

struct Settings
{
    bool quick = false;
    int repeats = 3;
};

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

AcqStreamInfo streamInfo(std::size_t channels, AcqSampleFormat format, AcqLayout layout = AcqLayout::Interleaved,
                         AcqDirection direction = AcqDirection::Input)
{
    AcqStreamInfo info;
    info.name = "bench";
    info.format = format;
    info.layout = layout;
    info.direction = direction;
    info.sfreq = 1000.0;
    for (std::size_t c = 0; c < channels; ++c) {
        AcqChannelInfo ch;
        ch.name = "ch" + std::to_string(c);
        ch.scale = 1e-6;
        ch.offset = 0.5;
        info.channels.push_back(ch);
    }
    return info;
}

AcqStreamOptions ringOptions(std::size_t capacity)
{
    AcqStreamOptions options;
    options.capacitySamples = capacity;
    options.lockMemory = false;
    return options;
}

/**
 * Shortest time of @p call over @p trials trials of @p calls calls each, in seconds per call. Interrupts and core
 * migrations only ever add time, so the minimum is the stable estimate of the cost itself.
 */
template<typename Call>
double minSecondsPerCall(int trials, int calls, Call call)
{
    double best = 1e300;
    for (int t = 0; t < trials; ++t) {
        const auto start = Clock::now();
        for (int i = 0; i < calls; ++i) {
            call();
        }
        best = std::min(best, secondsSince(start) / calls);
    }
    return best;
}

/** Best of the repeats: the least disturbed run, which is what a regression check compares. */
template<typename Measure>
double best(const Settings& settings, bool higherIsBetter, Measure measure)
{
    double result = measure();
    for (int r = 1; r < settings.repeats && result >= 0.0; ++r) {
        const double v = measure();
        result = v < 0.0 ? v : (higherIsBetter ? std::max(result, v) : std::min(result, v)); // a loss is never hidden
    }
    return result;
}

/** ns per beginWrite + commit + @p readers x (acquire + release), one thread, one-sample blocks. */
double opCost(int readers, int ops)
{
    auto writer = AcqWriter::create(streamInfo(1, AcqSampleFormat::Float32), ringOptions(4096));
    std::vector<AcqReader> all;
    all.reserve(static_cast<std::size_t>(readers));
    for (int i = 0; i < readers; ++i) {
        all.push_back(std::move(writer->stream().reader(AcqReader::ReadMode::Lossless).value()));
    }
    return 1e9 * minSecondsPerCall(50, ops / 50, [&] {
               static_cast<void>(writer->beginWrite(1));
               writer->commit(1);
               for (AcqReader& reader : all) {
                   auto view = reader.acquire(1, 0ns);
                   static_cast<void>(reader.release(*view));
               }
           });
}

/**
 * Commit-to-wake latency of a reader in @p mode: the producer commits one sample carrying its commit time every
 * 50 us; the reader subtracts it from the time it returned from acquire(). @return p50, p99, p99.9 in ns.
 */
std::array<double, 3> wakeLatency(AcqReader::ReadMode mode, int commits)
{
    auto writer = AcqWriter::create(streamInfo(2, AcqSampleFormat::Int32), ringOptions(65536));
    auto reader = writer->stream().reader(mode);
    std::vector<std::int64_t> latencies;
    latencies.reserve(static_cast<std::size_t>(commits));
    std::thread consumer([&] {
        for (int i = 0; i < commits; ++i) {
            auto view = reader->acquire(1, 5s, 1);
            const std::int64_t woke = nowNs();
            if (!view) {
                return;
            }
            std::int32_t words[2] = {};
            std::memcpy(&words[0], view->part(0).at(0, 0), sizeof(std::int32_t));
            std::memcpy(&words[1], view->part(0).at(1, 0), sizeof(std::int32_t));
            const std::int64_t stamp =
                (static_cast<std::int64_t>(words[0]) << 32) | static_cast<std::int64_t>(static_cast<std::uint32_t>(words[1]));
            latencies.push_back(woke - stamp);
            static_cast<void>(reader->release(*view));
        }
    });
    for (int i = 0; i < commits; ++i) {
        const auto due = Clock::now() + 50us;
        while (Clock::now() < due) {
        }
        auto view = writer->beginWrite(1);
        const std::int64_t stamp = nowNs();
        const auto hi = static_cast<std::int32_t>(stamp >> 32);
        const auto lo = static_cast<std::int32_t>(static_cast<std::uint32_t>(stamp & 0xFFFFFFFF));
        std::memcpy(view->part(0).at(0, 0), &hi, sizeof(hi));
        std::memcpy(view->part(0).at(1, 0), &lo, sizeof(lo));
        writer->commit(1);
    }
    consumer.join();
    std::sort(latencies.begin(), latencies.end());
    const auto at = [&](double q) {
        return latencies.empty() ? 0.0 : static_cast<double>(latencies[static_cast<std::size_t>(q * static_cast<double>(latencies.size() - 1))]);
    };
    return {at(0.5), at(0.99), at(0.999)};
}

/**
 * Sustained throughput of an unpaced producer into a reader that reads every byte; the reader gates the
 * producer, so nothing is lost and the number is the end-to-end rate. @return GB/s, or -1 if a sample was lost.
 */
double ringThroughput(std::size_t channels, std::uint64_t bytes)
{
    const std::size_t frame = channels * sizeof(std::int16_t);
    const std::size_t block = std::max<std::size_t>(1, 65536 / frame);
    auto writer = AcqWriter::create(streamInfo(channels, AcqSampleFormat::Int16),
                                    ringOptions(std::max<std::size_t>(65536, 16 * block)));
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless, gating);
    const std::uint64_t total = std::max<std::uint64_t>(bytes / frame / block, 1) * block;
    // Every byte of sample s is fillByte(s), so the reader can check what it reads without a reference copy.
    const auto fillByte = [block](std::uint64_t s) {
        return static_cast<std::uint8_t>((s / block) % 251 + 1);
    };
    std::uint64_t byteSum = 0;
    std::uint64_t wrong = 0;
    std::uint64_t received = 0;
    std::thread consumer([&] {
        while (true) {
            auto view = reader->acquire(1, 5s);
            if (!view) {
                return;
            }
            std::uint64_t s = view->firstSample();
            for (std::size_t p = 0; p < view->partCount(); ++p) {
                const AcqBlockPart& part = view->part(p);
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(part.data);
                std::uint64_t sum = 0;
                for (std::size_t k = 0; k < part.samples * frame; ++k) {
                    sum += bytes[k];
                }
                for (std::size_t i = 0; i < part.samples; ++i, ++s) {
                    wrong += bytes[i * frame] != fillByte(s) || bytes[i * frame + frame - 1] != fillByte(s) ? 1U : 0U;
                }
                byteSum += sum;
            }
            received += view->sampleCount();
            static_cast<void>(reader->release(*view));
        }
    });
    std::uint64_t expectedSum = 0;
    const auto start = Clock::now();
    for (std::uint64_t written = 0; written < total; written += block) {
        auto view = writer->beginWrite(block, 10s);
        if (!view) {
            break;
        }
        for (std::size_t p = 0; p < view->partCount(); ++p) {
            std::memset(view->part(p).data, fillByte(written), view->part(p).samples * frame);
        }
        writer->commit(block);
        expectedSum += static_cast<std::uint64_t>(fillByte(written)) * block * frame;
    }
    writer->finish();
    consumer.join();
    const double s = secondsSince(start);
    if (received != total || reader->stats().lost != 0 || wrong != 0 || byteSum != expectedSum) {
        return -1.0;
    }
    return static_cast<double>(total * frame) / s / 1e9;
}

/** decode() of a 64 x 4096 interleaved block into a column-major T matrix. @return G output elements/s. */
template<typename T>
double decodeRate(AcqSampleFormat format, int trials)
{
    constexpr std::size_t kChannels = 64;
    constexpr std::size_t kBlock = 4096;
    auto writer = AcqWriter::create(streamInfo(kChannels, format), ringOptions(8 * kBlock));
    auto reader = writer->stream().reader(AcqReader::ReadMode::Lossless);
    auto in = writer->beginWrite(kBlock);
    for (std::size_t p = 0; p < in->partCount(); ++p) {
        std::memset(in->part(p).data, 1, in->part(p).samples * kChannels * bytesPerSample(format));
    }
    writer->commit(kBlock);
    auto view = reader->acquire(kBlock, 0ns);
    std::vector<T> out(kChannels * kBlock);
    const auto target = AcqMatrixRef<T>::columnMajor(out.data(), kChannels, kBlock);
    const double rate = static_cast<double>(kChannels * kBlock) / 1e9 /
        minSecondsPerCall(trials, 4, [&] { static_cast<void>(decode(*view, target)); });
    static_cast<void>(reader->release(*view));
    return rate;
}

/** encode() of a column-major float 64 x 4096 block into an output stream. @return G input elements/s. */
double encodeRate(AcqSampleFormat format, int trials)
{
    constexpr std::size_t kChannels = 64;
    constexpr std::size_t kBlock = 4096;
    auto writer = AcqWriter::create(streamInfo(kChannels, format, AcqLayout::Interleaved, AcqDirection::Output),
                                    ringOptions(8 * kBlock));
    std::vector<float> in(kChannels * kBlock, 0.25F);
    const auto source = AcqMatrixRef<const float>::columnMajor(in.data(), kChannels, kBlock);
    auto view = writer->beginWrite(kBlock);
    return static_cast<double>(kChannels * kBlock) / 1e9 /
        minSecondsPerCall(trials, 4, [&] { static_cast<void>(encode(source, *view)); });
}

std::vector<Metric> run(const Settings& s)
{
    std::vector<Metric> metrics;
    const int ops = s.quick ? 200000 : 2000000;
    for (const int readers : {1, 2, 4, 8}) {
        metrics.push_back({"op_cost_" + std::to_string(readers) + "_readers",
                           best(s, false, [&] { return opCost(readers, ops); }), "ns", false});
    }
    const int commits = s.quick ? 2000 : 20000;
    const struct
    {
        AcqReader::ReadMode mode;
        const char* name;
    } modes[] = {{AcqReader::ReadMode::LowLatency, "spin"}, {AcqReader::ReadMode::Lossless, "block"}};
    for (const auto& m : modes) {
        std::array<double, 3> bestLatency{1e18, 1e18, 1e18};
        for (int r = 0; r < s.repeats; ++r) {
            const auto l = wakeLatency(m.mode, commits);
            for (std::size_t i = 0; i < 3; ++i) {
                bestLatency[i] = std::min(bestLatency[i], l[i]);
            }
        }
        const std::string prefix = std::string("wake_") + m.name + "_";
        metrics.push_back({prefix + "p50", bestLatency[0], "ns", false});
        metrics.push_back({prefix + "p99", bestLatency[1], "ns", false});
        metrics.push_back({prefix + "p999", bestLatency[2], "ns", false});
    }
    const std::uint64_t bytes = s.quick ? (std::uint64_t(256) << 20) : (std::uint64_t(4) << 30);
    for (const std::size_t channels : {1U, 64U, 1024U, 4096U}) {
        // Best of several shorter streams: a stream is two threads, and one slow placement must not set the value.
        Settings streams = s;
        streams.repeats = 8 * s.repeats;
        metrics.push_back({"ring_int16_" + std::to_string(channels) + "_ch",
                           best(streams, true, [&] { return ringThroughput(channels, bytes / 8); }), "GB/s", true});
    }
    const int trials = s.quick ? 50 : 200;
    for (const AcqSampleFormat f : {AcqSampleFormat::Int16, AcqSampleFormat::Int24Packed, AcqSampleFormat::Int32,
                                    AcqSampleFormat::Float32}) {
        metrics.push_back({"decode_" + std::string(sampleFormatName(f)) + "_to_float",
                           best(s, true, [&] { return decodeRate<float>(f, trials); }), "Gelem/s", true});
    }
    metrics.push_back({"decode_ComplexInt16_to_cfloat",
                       best(s, true, [&] { return decodeRate<std::complex<float>>(AcqSampleFormat::ComplexInt16, trials); }),
                       "Gelem/s", true});
    for (const AcqSampleFormat f : {AcqSampleFormat::Int16, AcqSampleFormat::Float32}) {
        metrics.push_back({"encode_float_to_" + std::string(sampleFormatName(f)),
                           best(s, true, [&] { return encodeRate(f, trials); }), "Gelem/s", true});
    }
    return metrics;
}

bool writeJson(const QString& path, const std::vector<Metric>& metrics, const Settings& s)
{
    QJsonArray list;
    for (const Metric& m : metrics) {
        list.append(QJsonObject{{"name", QString::fromStdString(m.name)},
                                {"value", m.value},
                                {"unit", QString::fromLatin1(m.unit)},
                                {"better", m.higherIsBetter ? "higher" : "lower"}});
    }
    const QJsonObject report{{"tool", "mne_acq_bench"},
                             {"version", MNE_CPP_VERSION},
                             {"build", buildHash()},
                             {"quick", s.quick},
                             {"repeats", s.repeats},
                             {"threads", static_cast<int>(std::thread::hardware_concurrency())},
                             {"metrics", list}};
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
        file.write(QJsonDocument(report).toJson()) > 0;
}
} // namespace

//=============================================================================================================
// MAIN
//=============================================================================================================

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("mne_acq_bench");
    QCoreApplication::setApplicationVersion(MNE_CPP_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Performance suite of the MNE-CPP acquisition library: per-call cost of the stream protocol, commit-to-wake "
        "latency (spinning and blocking readers), sustained ring throughput over a channel sweep, and the "
        "decode/encode converters. Every value is the best of --repeats runs.");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption quickOpt("quick", "Short runs (about 10 s), for CI regression checks.");
    const QCommandLineOption repeatsOpt("repeats", "Runs per measurement (default 3).", "n", "3");
    const QCommandLineOption jsonOpt("json", "Also write the results as JSON.", "file");
    parser.addOptions({quickOpt, repeatsOpt, jsonOpt});
    parser.process(app);

    Settings settings;
    settings.quick = parser.isSet(quickOpt);
    bool ok = false;
    settings.repeats = parser.value(repeatsOpt).toInt(&ok);
    if (!ok || settings.repeats < 1) {
        qCritical("--repeats needs a positive integer, got '%s'", qPrintable(parser.value(repeatsOpt)));
        return 1;
    }

    QTextStream out(stdout);
    out << "mne_acq_bench " << MNE_CPP_VERSION << " (ACQLIB " << buildHash() << "), "
        << std::thread::hardware_concurrency() << " hardware threads\n";
    out.flush();
    const std::vector<Metric> metrics = run(settings);
    bool lost = false;
    for (const Metric& m : metrics) {
        out << "  " << QString::fromStdString(m.name).leftJustified(34) << QString::number(m.value, 'g', 4).rightJustified(12)
            << ' ' << m.unit << '\n';
        lost |= m.value < 0.0;
    }
    out.flush();
    if (parser.isSet(jsonOpt) && !writeJson(parser.value(jsonOpt), metrics, settings)) {
        qCritical("Cannot write %s", qPrintable(parser.value(jsonOpt)));
        return 1;
    }
    if (lost) {
        qCritical("A ring throughput run lost or corrupted samples");
        return 1;
    }
    return 0;
}
