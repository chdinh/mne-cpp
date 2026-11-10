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

#include <acq/acq_device.h>
#include <acq/acq_device_registry.h>
#include <acq/devices/acq_devices.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <thread>
#include <vector>

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

Recording record(const std::string& uri)
{
    Recording out;
    auto device = AcqDevice::open(uri);
    if (!device) {
        qWarning("open(%s): %s", uri.c_str(), device.error().text().c_str());
        return out;
    }
    const auto info = device->stream().info();
    out.channels = info->channelCount();
    for (const AcqChannelInfo& channel : info->channels) {
        out.scale.push_back(channel.scale);
    }
    // Replays as fast as they are read wait for the reader instead of overrunning it
    AcqReaderOptions gating;
    gating.gating = true;
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless, gating);
    if (!reader || !device->start().ok()) {
        return out;
    }
    while (true) {
        auto view = reader->acquire(1, 10s);
        if (!view) {
            if (view.error() != AcqErrorCode::EndOfStream) {
                qWarning("acquire(%s): %s", uri.c_str(), view.error().text().c_str());
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

AcqErrorCode openError(const std::string& uri)
{
    auto device = AcqDevice::open(uri);
    return device ? AcqErrorCode::None : device.error().code();
}

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
};

//=============================================================================================================

void TestAcqDevices::initTestCase()
{
    QVERIFY(registerBrainAmpScheme().ok());
    QVERIFY(registerTmsiScheme().ok());
    QVERIFY(register_gusbamp_scheme().ok());
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
// MAIN
//=============================================================================================================

QTEST_GUILESS_MAIN(TestAcqDevices)
#include "test_acq_devices.moc"
