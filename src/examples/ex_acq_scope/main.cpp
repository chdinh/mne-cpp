//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     main.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Live multi-channel, multi-rate scope of a streaming source with a closed-loop stimulator on the same clock.
 *
 * Opens an acquisition source by URI (default: the seeded simulator with 8 EEG channels and a trigger channel at
 * 1 kHz, 2 fNIRS pairs at 10 Hz, 8 ECoG contacts and 2 ERG electrodes at 2 kHz) and sweeps every channel of every
 * stream across the same 5 s window as the samples arrive, like the monitor of an amplifier, with the stimulus
 * triggers marked. Each stream keeps its own rate: the slow fNIRS trace advances in 100 ms steps, the ECoG and ERG
 * traces at twice the EEG rate, all aligned in time because the streams share one clock. Each view is converted to
 * physical units with decode(), whatever the device's format and layout.
 *
 * A closed-loop stimulator answers every trigger with a biphasic current pulse 200 ms later (1 mA after targets,
 * 0.5 mA after standards). It writes the pulse into an output stream with beginWriteAt() and encode(); an emulated
 * DAC plays that stream on the input's clock and the bottom trace shows what it played. The output ring is kept
 * small, so many pulses cross its end and are written in two parts (marked "wrap"): every played sample is checked
 * against the scheduled pulse, so a pulse that is truncated, shifted or written wrongly across the end of the ring
 * shows up in the trace and in the mismatch count.
 *
 * Run it without arguments to watch the simulator, with a URI to watch another source, or with
 * --seconds <s> to stop after s seconds (it then exits non-zero if any played sample was wrong).
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <acq/acq.h>

#include <disp/plots/helpers/figureexporter.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace DISPLIB;
using namespace std::chrono_literals;

namespace
{

constexpr double kWindowSeconds = 5.0;
constexpr double kDelaySeconds = 0.2;
constexpr double kPhaseSeconds = 0.06; // per phase of the biphasic pulse
constexpr double kMaxViewSeconds = 0.1;
constexpr double kFullScale = 2e-3; // A, output range of the stimulator
constexpr std::int32_t kStandard = 1;
constexpr std::int32_t kTarget = 2;

struct Marker
{
    std::uint64_t sample = 0;
    std::int32_t code = 0;
    bool wrapped = false; // pulse markers: written in two parts across the end of the ring
};

/** Display unit of a channel: factor from SI and its label. */
struct DisplayUnit
{
    double factor = 1.0;
    const char* label = "";
};

DisplayUnit displayUnit(AcqUnit unit)
{
    switch (unit) {
        case AcqUnit::Volt:
            return {1e6, "uV"};
        case AcqUnit::Tesla:
            return {1e15, "fT"};
        case AcqUnit::Ampere:
            return {1e3, "mA"};
        case AcqUnit::Molar:
            return {1e6, "uM"};
        default:
            return {};
    }
}

/** Rolling history of one stream in physical units. */
struct Trace
{
    std::string title;
    double sfreq = 0.0;
    std::uint64_t window = 0;   // samples per sweep
    std::size_t rows = 0;       // channels, on the first stream plus the played-back stimulator
    std::size_t length = 0;     // history length, a power of two above the window
    std::vector<float> history; // frames of `rows` values, sample s at (s & (length - 1))
    std::uint64_t newest = 0;   // one past the newest sample in the history
    std::uint64_t oldest = 0;   // first valid sample in the history
    std::vector<std::string> names;
    std::vector<DisplayUnit> units;

    Trace(const AcqStreamInfo& info, std::size_t extraRows)
    : title(QString("%1  %2 Hz").arg(QString::fromStdString(info.name)).arg(info.sfreq).toStdString())
    , sfreq(info.sfreq)
    , window(static_cast<std::uint64_t>(std::llround(kWindowSeconds * info.sfreq)))
    , rows(info.channelCount() + extraRows)
    , length(std::bit_ceil(static_cast<std::size_t>(window) + 1))
    , history(length * rows, 0.0F)
    {
        for (const AcqChannelInfo& channel : info.channels) {
            names.push_back(channel.name);
            units.push_back(displayUnit(channel.unit));
        }
    }

    /** Appends @p n frames of @p channels values, plus one value per frame from @p extra if given. */
    void append(std::uint64_t first, std::size_t n, const float* frames, std::size_t channels, const float* extra)
    {
        if (first > newest) {
            oldest = first; // a gap: never show the stale samples in between
        }
        for (std::size_t i = 0; i < n; ++i) {
            float* frame = &history[((first + i) & (length - 1)) * rows];
            std::memcpy(frame, frames + i * channels, channels * sizeof(float));
            if (extra != nullptr) {
                frame[channels] = extra[i];
            }
        }
        newest = first + n;
        oldest = std::max(oldest, newest > length ? newest - length : 0);
    }
};

/** Everything the acquisition thread hands to the display, guarded by one mutex. */
struct Shared
{
    std::mutex mutex;
    std::vector<Trace> traces; // the first stream (with the stimulator output) first, then the device's others
    std::deque<Marker> triggers;
    std::deque<Marker> pulses;

    std::atomic<std::uint64_t> lost{0};
    std::atomic<std::uint64_t> views{0};
    std::atomic<std::uint64_t> wrappedViews{0};
    std::atomic<std::uint64_t> pulsesScheduled{0};
    std::atomic<std::uint64_t> pulsesWrapped{0};
    std::atomic<std::uint64_t> pulsesRefused{0};
    std::atomic<std::uint64_t> playedMismatches{0};
    std::atomic<bool> stop{false};
};

/** Biphasic pulse: cathodic first, then anodic. */
double pulseValue(std::uint64_t phase, std::size_t phaseSamples, double amplitude)
{
    return phase < phaseSamples ? -amplitude : amplitude;
}

struct Scheduled
{
    std::uint64_t at = 0;
    double amplitude = 0.0;
};

/** Reads everything @p reader has now into trace @p index; used for the device's further streams. */
void drainInto(AcqReader& reader, std::size_t index, std::vector<float>& block, Shared& shared)
{
    const std::size_t channels = reader.channelCount();
    const std::size_t maxView = block.size() / channels;
    while (true) {
        auto view = reader.acquire(0, 0ns, maxView);
        if (!view) {
            return;
        }
        const std::size_t n = view->sampleCount();
        const std::uint64_t first = view->firstSample();
        shared.lost.fetch_add(view->lost(), std::memory_order_relaxed);
        const bool decoded = n > 0 && decode(*view, AcqMatrixRef<float>::columnMajor(block.data(), channels, n)).ok();
        (void)reader.release(*view);
        if (!decoded) {
            return;
        }
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.traces[index].append(first, n, block.data(), channels, nullptr);
    }
}

/** Acquisition thread: reads the source, schedules pulses, plays the output ring, fills the histories. */
void acquire(AcqReader& input, std::vector<AcqReader>& others, AcqWriter& stimulator, AcqReader& dac, Shared& shared)
{
    const std::size_t channels = input.channelCount();
    const double sfreq = shared.traces[0].sfreq;
    const auto delay = static_cast<std::uint64_t>(std::llround(kDelaySeconds * sfreq));
    const auto phaseSamples = static_cast<std::size_t>(std::llround(kPhaseSeconds * sfreq));
    const std::size_t pulseSamples = 2 * phaseSamples;
    const auto maxView = static_cast<std::size_t>(std::llround(kMaxViewSeconds * sfreq));
    const double lsb = kFullScale / 32767.0;

    std::vector<float> block(maxView * channels);
    std::vector<float> played(maxView);
    std::vector<double> pulse(pulseSamples);
    std::deque<Scheduled> scheduled;
    std::uint64_t next = 0; // next input sample the DAC has to play
    std::vector<std::vector<float>> otherBlocks;
    for (std::size_t k = 0; k < others.size(); ++k) {
        const auto maxOther = std::max<std::size_t>(1, static_cast<std::size_t>(kMaxViewSeconds * shared.traces[k + 1].sfreq));
        otherBlocks.emplace_back(maxOther * others[k].channelCount());
    }

    while (!shared.stop.load(std::memory_order_relaxed)) {
        auto view = input.acquire(1, 100ms, maxView);
        if (!view) {
            if (view.error() == AcqErrorCode::EndOfStream) {
                break;
            }
            continue;
        }
        const std::uint64_t first = view->firstSample();
        const std::size_t n = view->sampleCount();
        shared.views.fetch_add(1, std::memory_order_relaxed);
        shared.wrappedViews.fetch_add(view->partCount() > 1 ? 1U : 0U, std::memory_order_relaxed);
        shared.lost.fetch_add(view->lost(), std::memory_order_relaxed);
        if (first > next) {
            (void)dac.consumeIdle(static_cast<std::size_t>(first - next)); // samples lost on input: play silence
        }
        next = first + n;

        if (const AcqError error = decode(*view, AcqMatrixRef<float>::columnMajor(block.data(), channels, n));
            !error.ok()) {
            std::fprintf(stderr, "Cannot decode: %s\n", error.text().c_str());
            (void)input.release(*view);
            break;
        }

        std::vector<Marker> newTriggers;
        for (std::size_t e = 0; e < view->events().size(); ++e) {
            const AcqEvent& event = view->events()[e];
            if (event.code != kStandard && event.code != kTarget) {
                continue;
            }
            newTriggers.push_back({event.sample, event.code, false});

            //! [acq_scope_stimulate]
            // Answer the trigger with a biphasic pulse on the exact output sample, delay samples later.
            const std::uint64_t at = event.sample + delay;
            const double amplitude = event.code == kTarget ? 1e-3 : 0.5e-3;
            auto write = stimulator.beginWriteAt(at, pulseSamples, 0ns);
            if (!write) {
                shared.pulsesRefused.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            for (std::size_t j = 0; j < pulseSamples; ++j) {
                pulse[j] = pulseValue(write->firstSample() + j - at, phaseSamples, amplitude);
            }
            (void)encode(AcqMatrixRef<const double>::rowMajor(pulse.data(), 1, pulseSamples), *write);
            const bool wrapped = write->partCount() > 1;
            stimulator.commit(pulseSamples);
            //! [acq_scope_stimulate]
            scheduled.push_back({at, amplitude});
            shared.pulsesScheduled.fetch_add(1, std::memory_order_relaxed);
            shared.pulsesWrapped.fetch_add(wrapped ? 1U : 0U, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.pulses.push_back({at, event.code, wrapped});
        }
        (void)input.release(*view);

        // The DAC plays the output samples of the same instants on the same clock.
        std::fill(played.begin(), played.begin() + static_cast<std::ptrdiff_t>(n), 0.0F);
        std::size_t got = 0;
        if (auto out = dac.acquire(0, 0ns, n)) {
            got = out->sampleCount();
            if (got > 0) {
                (void)decode(*out, AcqMatrixRef<float>::rowMajor(played.data(), 1, got));
            }
            (void)dac.release(*out);
        }
        if (got < n) {
            (void)dac.consumeIdle(n - got);
        }
        while (!scheduled.empty() && scheduled.front().at + pulseSamples <= first) {
            scheduled.pop_front();
        }
        std::uint64_t mismatches = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t t = first + i;
            double expected = 0.0;
            for (const Scheduled& p : scheduled) {
                if (t >= p.at && t < p.at + pulseSamples) {
                    expected = pulseValue(t - p.at, phaseSamples, p.amplitude);
                }
            }
            mismatches += std::abs(static_cast<double>(played[i]) - expected) > lsb ? 1U : 0U;
        }
        shared.playedMismatches.fetch_add(mismatches, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            Trace& trace = shared.traces[0];
            trace.append(first, n, block.data(), channels, played.data());
            shared.triggers.insert(shared.triggers.end(), newTriggers.begin(), newTriggers.end());
            const std::uint64_t windowStart = trace.newest > trace.length ? trace.newest - trace.length : 0;
            while (!shared.triggers.empty() && shared.triggers.front().sample < windowStart) {
                shared.triggers.pop_front();
            }
            while (!shared.pulses.empty() && shared.pulses.front().sample < windowStart) {
                shared.pulses.pop_front();
            }
        }
        for (std::size_t k = 0; k < others.size(); ++k) {
            drainInto(others[k], k + 1, otherBlocks[k], shared);
        }
    }
}

//=============================================================================================================
/**
 * Sweep display: each channel is drawn left to right and overwritten at the cursor, every stream at its own rate
 * across the same 5 s. Streams with more samples than pixels are drawn as one min/max pair per pixel, slower ones
 * as lines through their samples.
 */
class ScopeView : public QWidget
{
public:
    explicit ScopeView(Shared& shared)
    : m_shared(shared)
    {
        setMinimumSize(900, 600);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, QColor(16, 18, 22));
        setPalette(p);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const int plotWidth = std::max(1, width() - kLabelWidth - 8);

        std::lock_guard<std::mutex> lock(m_shared.mutex);
        const Trace& base = m_shared.traces[0];
        if (base.newest == 0) {
            painter.setPen(Qt::gray);
            painter.drawText(rect(), Qt::AlignCenter, "waiting for samples ...");
            return;
        }
        std::size_t rows = 0;
        for (const Trace& trace : m_shared.traces) {
            rows += trace.rows;
        }
        const double rowHeight = static_cast<double>(height() - 20) / static_cast<double>(rows);
        if (m_ranges.size() != m_shared.traces.size()) {
            m_ranges.resize(m_shared.traces.size());
            m_rangeSweeps.assign(m_shared.traces.size(), 0);
        }

        std::size_t row = 0;
        double outTop = 0.0;
        for (std::size_t t = 0; t < m_shared.traces.size(); ++t) {
            const Trace& trace = m_shared.traces[t];
            const double top = 10.0 + static_cast<double>(row) * rowHeight;
            if (t > 0) {
                painter.setPen(QColor(60, 64, 72));
                painter.drawLine(QPointF(4, top), QPointF(width() - 4, top));
            }
            painter.setPen(QColor(120, 160, 220));
            painter.drawText(QRectF(kLabelWidth, top, plotWidth, 14), Qt::AlignRight | Qt::AlignTop,
                             QString::fromStdString(trace.title));
            if (t == 0) {
                outTop = top + static_cast<double>(trace.rows - 1) * rowHeight;
            }
            drawTrace(painter, trace, t, top, rowHeight, plotWidth);
            row += trace.rows;
        }

        // Triggers across all streams, pulses on the output trace; the cursor marks the newest sample.
        const std::uint64_t newest = base.newest;
        const std::uint64_t window = base.window;
        auto xOf = [&](std::uint64_t phase) {
            return kLabelWidth + static_cast<double>(phase) * plotWidth / static_cast<double>(window);
        };
        for (const Marker& m : m_shared.triggers) {
            if (m.sample + window < newest) {
                continue;
            }
            const double x = xOf(m.sample % window);
            painter.setPen(QPen(m.code == kTarget ? QColor(240, 80, 80, 170) : QColor(90, 140, 255, 170), 1, Qt::DashLine));
            painter.drawLine(QPointF(x, 10), QPointF(x, height() - 10));
            painter.drawText(QPointF(x + 3, 22), m.code == kTarget ? "T" : "S");
        }
        painter.setPen(QColor(255, 200, 120));
        for (const Marker& m : m_shared.pulses) {
            if (m.wrapped && m.sample + window >= newest && m.sample < newest) {
                painter.drawText(QPointF(xOf(m.sample % window) + 2, outTop + 12), "wrap");
            }
        }
        painter.setPen(QPen(QColor(255, 255, 255, 200), 2));
        const double cursor = xOf(newest % window);
        painter.drawLine(QPointF(cursor, 4), QPointF(cursor, height() - 4));
        painter.setPen(QColor(120, 120, 130));
        painter.drawText(QRectF(kLabelWidth, height() - 18, plotWidth, 16), Qt::AlignRight,
                         QString("t = %1 s, %2 s per sweep")
                             .arg(static_cast<double>(newest) / base.sfreq, 0, 'f', 1)
                             .arg(kWindowSeconds, 0, 'f', 0));
    }

private:
    static constexpr int kLabelWidth = 110;

    /** Draws the rows of stream @p t from @p top on, holding each row's sensitivity for a whole sweep. */
    void drawTrace(QPainter& painter, const Trace& trace, std::size_t t, double top, double rowHeight, int plotWidth)
    {
        const std::uint64_t newest = trace.newest;
        const std::uint64_t window = trace.window;
        const std::uint64_t sweepStart = newest - newest % window;
        const std::uint64_t cursorPhase = newest % window;
        // Sample shown at sweep phase p: this sweep left of the cursor, the previous one right of it.
        auto sampleAt = [&](std::uint64_t phase) {
            return sweepStart + phase < newest ? sweepStart + phase : sweepStart + phase - window;
        };
        auto shown = [&](std::uint64_t s) {
            return newest > 0 && s >= trace.oldest && s < newest && s + window >= newest;
        };
        auto value = [&](std::uint64_t s, std::size_t r) {
            return trace.history[(s & (trace.length - 1)) * trace.rows + r];
        };
        const bool dense = window >= static_cast<std::uint64_t>(plotWidth);
        std::vector<float>& ranges = m_ranges[t];
        ranges.resize(trace.rows, 0.0F);
        const std::uint64_t sweep = window > 0 ? newest / window : 0;

        std::vector<QPointF> points;
        for (std::size_t r = 0; r < trace.rows; ++r) {
            const bool output = t == 0 && r + 1 == trace.rows;
            const double centre = top + (static_cast<double>(r) + 0.5) * rowHeight;
            float rangeMax = 0.0F;
            // Points in the units of the row: x in pixels, y as the raw value, scaled once the range is known.
            std::vector<std::vector<QPointF>> runs(1);
            auto breakRun = [&] {
                if (!runs.back().empty()) {
                    runs.emplace_back();
                }
            };
            if (dense) {
                for (int x = 0; x < plotWidth; ++x) {
                    const auto from = static_cast<std::uint64_t>(x) * window / static_cast<std::uint64_t>(plotWidth);
                    const auto to = std::max(from + 1, static_cast<std::uint64_t>(x + 1) * window / static_cast<std::uint64_t>(plotWidth));
                    float a = std::numeric_limits<float>::max();
                    float b = std::numeric_limits<float>::lowest();
                    std::uint64_t at = 0;
                    std::uint64_t bt = 0;
                    bool any = false;
                    for (std::uint64_t phase = from; phase < to; ++phase) {
                        const std::uint64_t s = sampleAt(phase);
                        if (!shown(s)) {
                            continue;
                        }
                        const float v = value(s, r);
                        if (v < a) {
                            a = v;
                            at = phase;
                        }
                        if (v > b) {
                            b = v;
                            bt = phase;
                        }
                        any = true;
                    }
                    if (!any) {
                        breakRun();
                        continue;
                    }
                    // The extremes in time order, so an edge is one stroke instead of a down-up-down zig-zag.
                    runs.back().emplace_back(kLabelWidth + x, at <= bt ? a : b);
                    runs.back().emplace_back(kLabelWidth + x, at <= bt ? b : a);
                    rangeMax = std::max({rangeMax, std::abs(a), std::abs(b)});
                }
            } else {
                for (std::uint64_t phase = 0; phase < window; ++phase) {
                    const std::uint64_t s = sampleAt(phase);
                    if (phase == cursorPhase || !shown(s)) {
                        breakRun(); // never join the new sweep to the old one across the cursor
                    }
                    if (!shown(s)) {
                        continue;
                    }
                    const float v = value(s, r);
                    runs.back().emplace_back(kLabelWidth + static_cast<double>(phase) * plotWidth / static_cast<double>(window), v);
                    rangeMax = std::max(rangeMax, std::abs(v));
                }
            }
            if ((sweep != m_rangeSweeps[t] || ranges[r] == 0.0F) && rangeMax > 0.0F) {
                ranges[r] = niceRange(rangeMax);
            }
            const float range = output ? static_cast<float>(1.2e-3) : (ranges[r] > 0.0F ? ranges[r] : 1.0F);
            const double gain = 0.45 * rowHeight / static_cast<double>(range);

            const DisplayUnit unit = output ? displayUnit(AcqUnit::Ampere) : trace.units[r];
            painter.setPen(output ? QColor(255, 160, 60) : (unit.factor == 1.0 ? QColor(230, 200, 80) : QColor(150, 220, 170)));
            for (const std::vector<QPointF>& run : runs) {
                if (run.size() < 2) {
                    continue;
                }
                points.clear();
                for (const QPointF& p : run) {
                    points.emplace_back(p.x(), centre - gain * p.y());
                }
                painter.drawPolyline(points.data(), static_cast<int>(points.size()));
            }

            painter.setPen(QColor(170, 170, 180));
            const QString name = QString::fromStdString(output ? std::string("STIM OUT") : trace.names[r]);
            const QString scale = unit.label[0] != '\0'
                ? QString("%1 %2").arg(static_cast<double>(range) * unit.factor, 0, 'g', 3).arg(unit.label)
                : QString("%1").arg(static_cast<double>(range), 0, 'g', 3);
            painter.drawText(QRectF(4, centre - rowHeight / 2, kLabelWidth - 8, rowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                             rowHeight >= 22 ? QString("%1\n+-%2").arg(name, scale) : name);
        }
        m_rangeSweeps[t] = sweep;
    }

    /** Smallest 1-2-5 step at or above @p value. */
    static float niceRange(float value)
    {
        const double decade = std::pow(10.0, std::floor(std::log10(static_cast<double>(value))));
        for (const double step : {1.0, 2.0, 5.0, 10.0}) {
            if (step * decade >= static_cast<double>(value)) {
                return static_cast<float>(step * decade);
            }
        }
        return value;
    }

    Shared& m_shared;
    std::vector<std::vector<float>> m_ranges; // per stream and row, held for a sweep
    std::vector<std::uint64_t> m_rangeSweeps;
};

QString statusText(const Shared& s)
{
    return QString("views %1 (%2 across the end of the input ring), %3 samples lost  |  pulses %4 scheduled, "
                   "%5 written across the end of the output ring, %6 refused  |  played samples off the pulse: %7")
        .arg(s.views.load())
        .arg(s.wrappedViews.load())
        .arg(s.lost.load())
        .arg(s.pulsesScheduled.load())
        .arg(s.pulsesWrapped.load())
        .arg(s.pulsesRefused.load())
        .arg(s.playedMismatches.load());
}

} // namespace

//=============================================================================================================
// MAIN
//=============================================================================================================

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    std::string uri = "synthetic://?eeg=8&fnirs=2&ecog=8&erg=2&seed=3&interval=0.7";
    double seconds = 0.0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seconds" && i + 1 < argc) {
            char* end = nullptr;
            seconds = std::strtod(argv[++i], &end);
            if (*end != '\0' || !(seconds > 0.0)) {
                std::fprintf(stderr, "--seconds needs a positive number, got %s\n", argv[i]);
                return 1;
            }
        } else {
            uri = arg;
        }
    }

    auto device = AcqDevice::open(uri);
    if (!device) {
        std::fprintf(stderr, "Cannot open %s: %s\n", uri.c_str(), device.error().text().c_str());
        return 1;
    }
    const auto info = device->stream().info();
    if (isComplex(info->format) || info->sfreq <= 0.0) {
        std::fprintf(stderr, "%s needs a regularly sampled real-valued stream\n", uri.c_str());
        return 1;
    }
    const double sfreq = info->sfreq;

    //! [acq_scope_output]
    // Stimulator output on the input's clock; a small ring makes pulses cross its end often.
    AcqStreamInfo outInfo;
    outInfo.name = "stim";
    outInfo.direction = AcqDirection::Output;
    outInfo.format = AcqSampleFormat::Int16;
    outInfo.sfreq = sfreq;
    outInfo.channels.resize(1);
    outInfo.channels[0].name = "STIM OUT";
    outInfo.channels[0].kind = AcqChannelKind::StimCurrent;
    outInfo.channels[0].unit = AcqUnit::Ampere;
    outInfo.channels[0].scale = kFullScale / 32767.0;
    outInfo.channels[0].rangeMin = -kFullScale;
    outInfo.channels[0].rangeMax = kFullScale;
    outInfo.underrunFill = AcqUnderrunFill::Zero;
    AcqStreamOptions outOptions;
    outOptions.capacitySamples = std::bit_ceil(static_cast<std::size_t>(
        std::llround((kMaxViewSeconds + kDelaySeconds + 2.0 * kPhaseSeconds) * sfreq) + 1));
    auto stimulator = AcqWriter::create(outInfo, outOptions);
    if (!stimulator) {
        std::fprintf(stderr, "Cannot create the stimulator: %s\n", stimulator.error().text().c_str());
        return 1;
    }
    AcqReaderOptions gating;
    gating.gating = true; // the emulated DAC: it consumes what was written or plays fill
    auto dac = stimulator->stream().reader(AcqReader::ReadMode::Lossless, gating);
    //! [acq_scope_output]
    auto input = device->stream().reader(AcqReader::ReadMode::Lossless);
    Shared shared;
    shared.traces.emplace_back(*info, 1);
    // Every further real-valued stream at its own rate: fNIRS, ECoG, ERG, ...
    std::vector<AcqReader> others;
    for (std::size_t i = 1; i < device->streams().size(); ++i) {
        const AcqStream stream = device->stream(i);
        const auto streamInfo = stream.info();
        if (streamInfo->direction != AcqDirection::Input || isComplex(streamInfo->format) || streamInfo->sfreq <= 0.0) {
            continue;
        }
        if (auto reader = stream.reader(AcqReader::ReadMode::Lossless)) {
            others.push_back(std::move(*reader));
            shared.traces.emplace_back(*streamInfo, 0);
        }
    }
    if (!dac || !input || !device->start().ok()) {
        std::fprintf(stderr, "Cannot start %s\n", uri.c_str());
        return 1;
    }

    std::thread worker(acquire, std::ref(*input), std::ref(others), std::ref(*stimulator), std::ref(*dac),
                       std::ref(shared));

    QWidget window;
    QString title = QString::fromStdString(device->description().device);
    for (const Trace& trace : shared.traces) {
        title += QString("  |  %1").arg(QString::fromStdString(trace.title));
    }
    window.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&window);
    auto* scope = new ScopeView(shared);
    auto* status = new QLabel;
    status->setWordWrap(true);
    layout->addWidget(scope, 1);
    layout->addWidget(status);
    window.resize(1400, 900);

    QTimer refresh;
    QObject::connect(&refresh, &QTimer::timeout, [&] {
        status->setText(statusText(shared));
        scope->update();
    });
    refresh.start(33);
    if (seconds > 0.0) {
        QTimer::singleShot(static_cast<int>(seconds * 1000.0), &app, [&] {
            status->setText(statusText(shared));
            FigureExporter::save(window, "acq_scope",
                                 QString("Live sweep of %1 streams at their own rates on one clock: %2. The "
                                         "stimulator output is played back below the first stream; %3 of %4 "
                                         "pulses crossed the end of the output ring.")
                                     .arg(shared.traces.size())
                                     .arg(title)
                                     .arg(shared.pulsesWrapped.load())
                                     .arg(shared.pulsesScheduled.load()),
                                 window.size());
            app.quit();
        });
    }
    window.show();
    app.exec();

    shared.stop = true;
    worker.join();
    device->close();
    std::printf("%s\n", statusText(shared).toStdString().c_str());
    const bool ok = shared.playedMismatches == 0 && shared.lost == 0 && shared.pulsesRefused == 0;
    std::printf("%s\n", ok ? "PASS: every played sample on its scheduled pulse" : "FAIL: see the counters above");
    return ok ? 0 : 1;
}
