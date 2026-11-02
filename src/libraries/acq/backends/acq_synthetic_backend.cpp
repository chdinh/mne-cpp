//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_synthetic_backend.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    synthetic:// : a seeded auditory-oddball EEG/MEG session.
 *
 * Every sample is a pure function of the seed, the sample index and the channel, so a recording does not
 * depend on block size, pacing or restarts. Signal per channel c (1 model unit = 1 uV on EEG, 20 fT on MEG):
 *
 *     x_c(s) = noise * 10 * N(0, 1) + 8 * sin(2 pi alpha t + d(t) + phi_c) + evoked * w_c * sum_k erp_k(s)
 *
 * with t = s / sfreq and d(t) a smooth phase drift (cosine-interpolated random values every 0.5 s, within
 * +-1.5 pi), so the rhythm is not phase-locked to the stimuli, as in real recordings. w_c = cos(pi c / n) (a dipolar topography over the n channels of one kind). Trial k starts at
 * (k + 1 + 0.1 u_k) * interval seconds; it is a target with probability targets, a standard otherwise.
 * Standards evoke an N100 (-5 at 100 ms, sigma 25 ms) and a P200 (+4 at 200 ms, sigma 40 ms); targets add
 * a P300 (+8 at 350 ms, sigma 80 ms). The stim channel holds the trial code (1 standard, 2 target) for 5 ms.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../detail/acq_synthetic_backend.h"

#include "../detail/acq_uri_params.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace ACQLIB::detail;

namespace
{

using Clock = std::chrono::steady_clock;

constexpr double kPi = 3.14159265358979323846;
constexpr double kEegUnit = 1e-6;   // V per model unit
constexpr double kMegUnit = 20e-15; // T per model unit
constexpr double kNoiseRms = 10.0;
constexpr double kAlphaAmplitude = 8.0;
constexpr double kDriftKnotSeconds = 0.5;
constexpr double kDriftRange = 1.5 * 3.14159265358979323846;
constexpr double kWindowSeconds = 0.8; // evoked responses have decayed below 1e-4 of their peak by then
constexpr double kJitter = 0.1;
constexpr double kPulseSeconds = 0.005;
constexpr std::int32_t kStandard = 1;
constexpr std::int32_t kTarget = 2;

struct Component
{
    double amplitude;
    double latency;
    double width;

    [[nodiscard]] double at(double t) const noexcept
    {
        const double z = (t - latency) / width;
        return amplitude * std::exp(-0.5 * z * z);
    }
};

constexpr Component kN100{-5.0, 0.100, 0.025};
constexpr Component kP200{4.0, 0.200, 0.040};
constexpr Component kP300{8.0, 0.350, 0.080};

enum Stream : std::uint64_t
{
    NoiseStream = 1,
    PhaseStream = 2,
    TrialStream = 3,
    JitterStream = 4,
    DriftStream = 5
};

constexpr std::uint64_t mix(std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

constexpr std::uint64_t hash(std::uint64_t seed, Stream stream, std::uint64_t a, std::uint64_t b = 0) noexcept
{
    return mix(mix(mix(seed ^ (stream * 0xD6E8FEB86659FD93ULL)) ^ a) ^ b);
}

double uniform(std::uint64_t h) noexcept
{
    return static_cast<double>(h >> 11U) * 0x1.0p-53;
}

//=============================================================================================================

struct Config
{
    std::size_t eeg = 8;
    std::size_t meg = 0;
    bool stim = true;
    double sfreq = 1000.0;
    std::size_t block = 0;
    bool realtime = true;
    double speed = 1.0;
    std::uint64_t totalSamples = 0;
    std::uint64_t seed = 0;
    double noise = 1.0;
    double alpha = 10.0;
    double interval = 1.0;
    double targets = 0.2;
    double evoked = 1.0;
    std::uint64_t pulse = 1;
};

AcqError invalid(std::string what)
{
    return {AcqErrorCode::InvalidParameter, std::move(what)};
}

AcqExpected<Config> parseConfig(const AcqUri& uri)
{
    Config c;
    if (!uri.target.empty()) {
        return invalid("target '" + uri.target + "': synthetic:// takes no target, only parameters");
    }
    const auto eeg = paramInt(uri, "eeg", 8, 0, 4096);
    const auto meg = paramInt(uri, "meg", 0, 0, 4096);
    const auto stim = paramBool(uri, "stim", true);
    const auto sfreq = paramDouble(uri, "sfreq", 1000.0, 0.001, 1e6);
    const auto block = paramInt(uri, "block", 0, 1, 1 << 20);
    const auto realtime = paramBool(uri, "realtime", true);
    const auto speed = paramDouble(uri, "speed", 1.0, 0.01, 1e4);
    const auto duration = paramDouble(uri, "duration", 0.0, 0.0, 1e7);
    const auto seed = paramInt(uri, "seed", 0, 0, INT64_MAX);
    const auto noise = paramDouble(uri, "noise", 1.0, 0.0, 1e3);
    const auto alpha = paramDouble(uri, "alpha", 10.0, 0.0, 5e5);
    const auto interval = paramDouble(uri, "interval", 1.0, 0.0, 3600.0);
    const auto targets = paramDouble(uri, "targets", 0.2, 0.0, 1.0);
    const auto evoked = paramDouble(uri, "evoked", 1.0, 0.0, 1e3);
    for (const AcqError& error : {eeg.error(), meg.error(), stim.error(), sfreq.error(), block.error(),
                                  realtime.error(), speed.error(), duration.error(), seed.error(), noise.error(), alpha.error(),
                                  interval.error(), targets.error(), evoked.error()}) {
        if (!error.ok()) {
            return error;
        }
    }
    c.eeg = static_cast<std::size_t>(*eeg);
    c.meg = static_cast<std::size_t>(*meg);
    c.stim = *stim;
    c.sfreq = *sfreq;
    c.block = *block > 0 ? static_cast<std::size_t>(*block) : static_cast<std::size_t>(std::max(1.0, std::round(c.sfreq / 100.0)));
    c.realtime = *realtime;
    c.speed = *speed;
    c.totalSamples = static_cast<std::uint64_t>(std::llround(*duration * c.sfreq));
    c.seed = static_cast<std::uint64_t>(*seed);
    c.noise = *noise;
    c.alpha = *alpha;
    c.interval = *interval;
    c.targets = *targets;
    c.evoked = *evoked;
    c.pulse = static_cast<std::uint64_t>(std::max(1.0, std::ceil(kPulseSeconds * c.sfreq - 1e-9)));

    if (c.eeg + c.meg == 0 && !c.stim) {
        return invalid("eeg=0, meg=0 and stim=0 leave synthetic:// without channels");
    }
    if (c.alpha >= c.sfreq / 2.0) {
        return invalid("alpha=" + std::to_string(c.alpha) + " must stay below the Nyquist frequency sfreq/2");
    }
    if (c.interval > 0.0 && c.interval * c.sfreq < 2.0 * static_cast<double>(c.pulse)) {
        return invalid("interval=" + std::to_string(c.interval) + " is shorter than two 5 ms trigger pulses");
    }
    return c;
}

//=============================================================================================================

struct Trial
{
    std::uint64_t onset = 0;
    std::int32_t code = 0;
};

/** The signal model; stateless after construction, so any sample can be computed in any order. */
class Model
{
public:
    explicit Model(const Config& config)
    : m_c(config)
    , m_analog(config.eeg + config.meg)
    , m_window(static_cast<std::uint64_t>(std::ceil(kWindowSeconds * config.sfreq)))
    {
        for (std::size_t c = 0; c < m_analog; ++c) {
            const bool eeg = c < m_c.eeg;
            const std::size_t index = eeg ? c : c - m_c.eeg;
            const std::size_t count = eeg ? m_c.eeg : m_c.meg;
            m_weight.push_back(std::cos(kPi * static_cast<double>(index) / static_cast<double>(count)));
            m_phase.push_back(2.0 * kPi * uniform(hash(m_c.seed, PhaseStream, c)));
            m_unit.push_back(eeg ? kEegUnit : kMegUnit);
        }
    }

    [[nodiscard]] std::size_t channels() const noexcept
    {
        return m_analog + (m_c.stim ? 1U : 0U);
    }

    [[nodiscard]] Trial trial(std::uint64_t k) const noexcept
    {
        const double start = static_cast<double>(k + 1) + kJitter * uniform(hash(m_c.seed, JitterStream, k));
        Trial t;
        t.onset = static_cast<std::uint64_t>(std::llround(start * m_c.interval * m_c.sfreq));
        t.code = uniform(hash(m_c.seed, TrialStream, k)) < m_c.targets ? kTarget : kStandard;
        return t;
    }

    /** @return Upper bound of the trials that touch a block of n samples. */
    [[nodiscard]] std::size_t maxTrials(std::size_t n) const noexcept
    {
        if (m_c.interval <= 0.0) {
            return 0;
        }
        return static_cast<std::size_t>(static_cast<double>(n + m_window) / (m_c.interval * m_c.sfreq)) + 4;
    }

    /** Collects the trials whose response or trigger overlaps samples [first, last]. Allocation-free within maxTrials. */
    void trials(std::uint64_t first, std::uint64_t last, std::vector<Trial>& out) const
    {
        out.clear();
        if (m_c.interval <= 0.0) {
            return;
        }
        const double perTrial = m_c.interval * m_c.sfreq;
        const std::uint64_t low = first > m_window ? first - m_window : 0;
        const double kLow = std::floor(static_cast<double>(low) / perTrial) - 2.0;
        const auto kMin = static_cast<std::uint64_t>(std::max(0.0, kLow));
        const auto kMax = static_cast<std::uint64_t>(static_cast<double>(last) / perTrial);
        for (std::uint64_t k = kMin; k <= kMax; ++k) {
            const Trial t = trial(k);
            if (t.onset <= last && t.onset + m_window > first) {
                out.push_back(t);
            }
        }
    }

    /** @return The phase drift of the rhythm at t seconds. */
    [[nodiscard]] double drift(double t) const noexcept
    {
        const double knot = t / kDriftKnotSeconds;
        const double k = std::floor(knot);
        const double w = 0.5 - 0.5 * std::cos(kPi * (knot - k));
        const auto index = static_cast<std::uint64_t>(k);
        const double a = 2.0 * uniform(hash(m_c.seed, DriftStream, index)) - 1.0;
        const double b = 2.0 * uniform(hash(m_c.seed, DriftStream, index + 1)) - 1.0;
        return kDriftRange * (a + (b - a) * w);
    }

    /** Writes the frame of sample s (all channels, physical units) to out. */
    void frame(std::uint64_t s, const std::vector<Trial>& trials, float* out) const noexcept
    {
        double erp = 0.0;
        float trigger = 0.0F;
        for (const Trial& t : trials) {
            if (s < t.onset || s - t.onset >= m_window) {
                continue;
            }
            const double latency = static_cast<double>(s - t.onset) / m_c.sfreq;
            erp += kN100.at(latency) + kP200.at(latency) + (t.code == kTarget ? kP300.at(latency) : 0.0);
            if (s - t.onset < m_c.pulse) {
                trigger = static_cast<float>(t.code);
            }
        }
        erp *= m_c.evoked;

        const double cycles = m_c.alpha * static_cast<double>(s) / m_c.sfreq;
        const double alphaPhase = 2.0 * kPi * (cycles - std::floor(cycles)) + drift(static_cast<double>(s) / m_c.sfreq);
        for (std::size_t c = 0; c < m_analog; c += 2) {
            double z[2] = {0.0, 0.0};
            if (m_c.noise > 0.0) {
                const std::uint64_t h = hash(m_c.seed, NoiseStream, s, c / 2);
                const double u1 = (static_cast<double>(h >> 32U) + 0.5) * 0x1.0p-32;
                const double u2 = (static_cast<double>(h & 0xFFFFFFFFULL) + 0.5) * 0x1.0p-32;
                const double r = std::sqrt(-2.0 * std::log(u1));
                z[0] = r * std::cos(2.0 * kPi * u2);
                z[1] = r * std::sin(2.0 * kPi * u2);
            }
            for (std::size_t j = 0; j < 2 && c + j < m_analog; ++j) {
                const std::size_t ch = c + j;
                double v = m_c.noise * kNoiseRms * z[j] + m_weight[ch] * erp;
                if (m_c.alpha > 0.0) {
                    v += kAlphaAmplitude * std::sin(alphaPhase + m_phase[ch]);
                }
                out[ch] = static_cast<float>(v * m_unit[ch]);
            }
        }
        if (m_c.stim) {
            out[m_analog] = trigger;
        }
    }

private:
    Config m_c;
    std::size_t m_analog;
    std::uint64_t m_window;
    std::vector<double> m_weight;
    std::vector<double> m_phase;
    std::vector<double> m_unit;
};

//=============================================================================================================

AcqStreamInfo streamInfo(const Config& c)
{
    AcqStreamInfo info;
    info.name = "synthetic";
    info.sourceId = "synthetic:seed=" + std::to_string(c.seed);
    info.format = AcqSampleFormat::Float32;
    info.sfreq = c.sfreq;
    info.eventLabels = {"", "standard", "target"};
    auto numbered = [](const char* prefix, std::size_t i) {
        std::string digits = std::to_string(i + 1);
        return std::string(prefix) + std::string(digits.size() < 3 ? 3 - digits.size() : 0, '0') + digits;
    };
    auto add = [&info](std::string name, AcqChannelKind kind, AcqUnit unit) {
        AcqChannelInfo channel;
        channel.name = std::move(name);
        channel.kind = kind;
        channel.unit = unit;
        info.channels.push_back(std::move(channel));
    };
    for (std::size_t i = 0; i < c.eeg; ++i) {
        add(numbered("EEG ", i), AcqChannelKind::Eeg, AcqUnit::Volt);
    }
    for (std::size_t i = 0; i < c.meg; ++i) {
        add(numbered("MEG ", i), AcqChannelKind::Meg, AcqUnit::Tesla);
    }
    if (c.stim) {
        add("STI 014", AcqChannelKind::Stim, AcqUnit::None);
    }
    return info;
}

//=============================================================================================================

class SyntheticBackend final : public AcqBackend
{
public:
    static AcqExpected<std::unique_ptr<AcqBackend>> open(const AcqUri& uri)
    {
        auto config = parseConfig(uri);
        if (!config) {
            return config.error();
        }
        std::unique_ptr<SyntheticBackend> backend(new SyntheticBackend(*config, uri.toString()));
        AcqStreamOptions options;
        options.capacitySamples = 4 * config->block > 65536 ? 4 * config->block : 0;
        auto writer = backend->addInputStream(streamInfo(*config), options);
        if (!writer) {
            return writer.error();
        }
        backend->m_writer = *writer;
        return std::unique_ptr<AcqBackend>(std::move(backend));
    }

    ~SyntheticBackend() override
    {
        stop();
    }

    SyntheticBackend(const SyntheticBackend&) = delete;
    SyntheticBackend& operator=(const SyntheticBackend&) = delete;

    AcqDeviceDescription description() const override
    {
        return {"synthetic", "", "Synthetic oddball session, seed " + std::to_string(m_config.seed), m_uri};
    }

    AcqError start() override
    {
        m_stop.store(false, std::memory_order_relaxed);
        AcqClockModel clock;
        clock.sfreq = m_config.sfreq;
        clock.anchorSample = m_next;
        clock.anchorHostNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        clock.source = AcqClockSource::Nominal;
        m_writer->setClock(clock);
        m_thread = std::thread([this] { run(); });
        return {};
    }

    void stop() override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex); // pairs with the wait in run(), so no wake-up is lost
            m_stop.store(true, std::memory_order_release);
        }
        m_wake.notify_all();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    SyntheticBackend(const Config& config, std::string uri)
    : m_config(config)
    , m_model(config)
    , m_uri(std::move(uri))
    {
    }

    void run()
    {
        const std::size_t channels = m_model.channels();
        std::vector<float> frame(channels);
        std::vector<Trial> trials;
        trials.reserve(m_model.maxTrials(m_config.block));
        const Clock::time_point start = Clock::now();
        const std::uint64_t startSample = m_next;
        while (true) {
            std::size_t n = m_config.block;
            if (m_config.totalSamples > 0) {
                if (m_next >= m_config.totalSamples) {
                    m_writer->finish();
                    return;
                }
                n = static_cast<std::size_t>(std::min<std::uint64_t>(n, m_config.totalSamples - m_next));
            }
            auto view = m_writer->beginWrite(n);
            if (!view) {
                reportError(view.error());
                return;
            }
            m_model.trials(m_next, m_next + n - 1, trials);
            std::uint64_t s = m_next;
            for (std::size_t p = 0; p < view->partCount(); ++p) {
                const AcqWritePart& part = view->part(p);
                for (std::size_t i = 0; i < part.samples; ++i, ++s) {
                    m_model.frame(s, trials, frame.data());
                    std::memcpy(part.at(0, i), frame.data(), channels * sizeof(float));
                }
            }

            if (m_config.realtime) {
                const std::chrono::duration<double> due(static_cast<double>(m_next + n - startSample) /
                                                        (m_config.sfreq * m_config.speed));
                const auto deadline = start + std::chrono::duration_cast<Clock::duration>(due);
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait_until(lock, deadline, [this] { return m_stop.load(std::memory_order_acquire); });
            }
            if (m_stop.load(std::memory_order_acquire)) {
                return; // the block is dropped; a restart regenerates it from m_next
            }

            for (const Trial& t : trials) {
                if (t.onset >= m_next && t.onset < m_next + n) {
                    const AcqError pushed = m_writer->pushEvent({t.onset, static_cast<std::uint32_t>(m_config.pulse), t.code});
                    if (!pushed.ok()) {
                        reportError(pushed);
                        return;
                    }
                }
            }
            m_writer->commit(n);
            m_next += n;
        }
    }

    Config m_config;
    Model m_model;
    std::string m_uri;
    AcqWriter* m_writer = nullptr;
    std::uint64_t m_next = 0; // next sample index; owned by the producer thread while it runs
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::atomic<bool> m_stop{false};
};

//=============================================================================================================

AcqSchemeInfo schemeInfo()
{
    AcqSchemeInfo info;
    info.scheme = "synthetic";
    info.description = "Seeded auditory-oddball EEG/MEG session: noise, an alpha rhythm and evoked responses "
                       "(N100, P200; P300 on targets) with trigger events.";
    info.exampleUri = "synthetic://";
    info.parameters = {
        {"eeg", "int", "8", "EEG channels (V)."},
        {"meg", "int", "0", "Magnetometer channels (T)."},
        {"stim", "bool", "1", "Add the trigger channel STI 014."},
        {"sfreq", "double", "1000", "Sampling rate in Hz."},
        {"block", "int", "auto", "Samples per block; auto = 10 ms."},
        {"realtime", "bool", "1",
         "Pace blocks by the clock. 0 generates as fast as possible, so readers lose samples once they fall a "
         "whole ring behind."},
        {"speed", "double", "1", "Pacing factor of realtime, e.g. 100 replays a session 100 times faster."},
        {"duration", "double", "0", "Session length in seconds, then end of stream; 0 runs until stopped."},
        {"seed", "int", "0", "Seed; equal seeds give bit-identical data."},
        {"noise", "double", "1", "Gain of the background noise (1 = 10 uV / 200 fT rms)."},
        {"alpha", "double", "10", "Frequency of the background rhythm in Hz; 0 disables it."},
        {"interval", "double", "1", "Stimulus onset asynchrony in seconds (plus up to 10 % jitter); 0 disables trials."},
        {"targets", "double", "0.2", "Probability that a trial is a target."},
        {"evoked", "double", "1", "Gain of the evoked responses."},
    };
    return info;
}

} // namespace

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

AcqError ACQLIB::detail::registerSyntheticScheme(AcqDeviceRegistry& registry)
{
    return registry.registerScheme(schemeInfo(), &SyntheticBackend::open, [](std::chrono::milliseconds) {
        return std::vector<AcqDiscoveredDevice>{
            {"synthetic://", "Synthetic oddball session", "synthetic", "8 EEG + STI 014, 1000 Hz, seeded", true}};
    });
}
