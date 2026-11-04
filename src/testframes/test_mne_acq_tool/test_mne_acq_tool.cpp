//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     test_mne_acq_tool.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Command-line tests of every mne_acq subcommand, with the output checked against the source itself.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <acq/acq.h>

#include <fiff/fiff_raw_data.h>

#include <lsl/lsl_stream_discovery.h>
#include <lsl/lsl_stream_inlet.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace FIFFLIB;
using namespace std::chrono_literals;

namespace
{

struct ToolResult
{
    int exitCode = -1;
    QString out;
    QString err;
};

QString toolPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/../bin/mne_acq");
}

ToolResult runTool(const QStringList& arguments, int timeoutMs = 60000)
{
    QProcess process;
    process.start(toolPath(), arguments);
    ToolResult result;
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        result.err = QStringLiteral("timed out");
        return result;
    }
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.out = QString::fromUtf8(process.readAllStandardOutput());
    result.err = QString::fromUtf8(process.readAllStandardError());
    return result;
}

/** All samples of @p uri in physical units as float, column-major channels x samples, with the channel names. */
std::vector<float> readSource(const std::string& uri, std::vector<std::string>& names)
{
    auto device = AcqDevice::open(uri);
    if (!device) {
        return {};
    }
    auto reader = device->stream().reader(AcqReader::ReadMode::Lossless);
    if (!reader || !device->start().ok()) {
        return {};
    }
    names.clear();
    for (const AcqChannelInfo& channel : reader->stream().info()->channels) {
        names.push_back(channel.name);
    }
    std::vector<float> values;
    for (;;) {
        auto lease = reader->lease(1, 1s);
        if (!lease) {
            break;
        }
        const AcqBlockView& view = **lease;
        std::vector<float> block(view.channelCount() * view.sampleCount());
        if (!decode(view, AcqMatrixRef<float>::columnMajor(block.data(), view.channelCount(), view.sampleCount())).ok()) {
            return {};
        }
        values.insert(values.end(), block.begin(), block.end());
    }
    device->close();
    return values;
}

} // namespace

//=============================================================================================================
/**
 * Runs the built mne_acq next to the test binaries.
 */
class TestMneAcqTool : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void usageErrors();
    void listShowsSchemesAndSources();
    void infoShowsStreamsAndChannels();
    void infoFailsOnBadUri();
    void recordWritesTheSource();
    void recordFailsOnUnwritablePath();
    void streamPublishesLsl();
};

//=============================================================================================================

void TestMneAcqTool::initTestCase()
{
    if (!QFile::exists(toolPath()) && !QFile::exists(toolPath() + QStringLiteral(".exe"))) {
        QSKIP("mne_acq was not built");
    }
}

//=============================================================================================================

void TestMneAcqTool::usageErrors()
{
    QCOMPARE(runTool({}).exitCode, 1);
    QCOMPARE(runTool({QStringLiteral("frobnicate")}).exitCode, 1);
    QCOMPARE(runTool({QStringLiteral("info")}).exitCode, 1);
    QCOMPARE(runTool({QStringLiteral("record"), QStringLiteral("synthetic://")}).exitCode, 1);
    QCOMPARE(runTool({QStringLiteral("list"), QStringLiteral("--timeout"), QStringLiteral("-1")}).exitCode, 1);
    QCOMPARE(runTool({QStringLiteral("record"), QStringLiteral("synthetic://"), QStringLiteral("x.fif"),
                      QStringLiteral("--duration"), QStringLiteral("soon")})
                 .exitCode,
             1);
    const ToolResult noOutput = runTool({QStringLiteral("stream"), QStringLiteral("synthetic://")});
    QCOMPARE(noOutput.exitCode, 1);
    QVERIFY(noOutput.err.contains(QStringLiteral("--lsl")));

    const ToolResult help = runTool({QStringLiteral("--help")});
    QCOMPARE(help.exitCode, 0);
    for (const char* command : {"list", "info", "record", "stream"}) {
        QVERIFY2(help.out.contains(QStringLiteral("mne_acq %1").arg(QLatin1String(command))), command);
    }
}

//=============================================================================================================

void TestMneAcqTool::listShowsSchemesAndSources()
{
    const ToolResult list = runTool({QStringLiteral("list"), QStringLiteral("--timeout"), QStringLiteral("0.2")});
    QCOMPARE(list.exitCode, 0);
    for (const char* scheme : {"synthetic://", "fiff://", "lsl://"}) {
        QVERIFY2(list.out.contains(QLatin1String(scheme)), qPrintable(list.out));
    }
    // The synthetic source is always reachable.
    QVERIFY2(list.out.section(QStringLiteral("Sources"), 1).contains(QStringLiteral("synthetic://")), qPrintable(list.out));
}

//=============================================================================================================

void TestMneAcqTool::infoShowsStreamsAndChannels()
{
    const ToolResult info = runTool({QStringLiteral("info"), QStringLiteral("synthetic://?eeg=3&sfreq=500")});
    QCOMPARE(info.exitCode, 0);
    QVERIFY2(info.out.contains(QStringLiteral("backend   synthetic")), qPrintable(info.out));
    QVERIFY2(info.out.contains(QStringLiteral("float32, 500 Hz")), qPrintable(info.out));
    for (const char* channel : {"EEG 001", "EEG 002", "EEG 003", "STI 014"}) {
        QVERIFY2(info.out.contains(QLatin1String(channel)), qPrintable(info.out));
    }
    QVERIFY(info.out.contains(QRegularExpression(QStringLiteral("EEG 001\\s+EEG\\s+V"))));
    QVERIFY(info.out.contains(QRegularExpression(QStringLiteral("STI 014\\s+stim"))));
}

//=============================================================================================================

void TestMneAcqTool::infoFailsOnBadUri()
{
    const ToolResult unknown = runTool({QStringLiteral("info"), QStringLiteral("nope://x")});
    QCOMPARE(unknown.exitCode, 2);
    QVERIFY(unknown.err.contains(QStringLiteral("nope://")));
    QCOMPARE(runTool({QStringLiteral("info"), QStringLiteral("fiff:///does/not/exist_raw.fif")}).exitCode, 2);
}

//=============================================================================================================

void TestMneAcqTool::recordWritesTheSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cli_raw.fif"));
    const std::string uri = "synthetic://?eeg=4&sfreq=1000&duration=2&realtime=0&seed=11";
    const ToolResult record = runTool({QStringLiteral("record"), QString::fromStdString(uri), path});
    QVERIFY2(record.exitCode == 0, qPrintable(record.out + record.err));
    QVERIFY2(record.out.contains(QStringLiteral("Recorded 2000 samples (0 lost)")), qPrintable(record.out));

    std::vector<std::string> names;
    const std::vector<float> expected = readSource(uri, names);
    QCOMPARE(expected.size(), names.size() * 2000);

    QFile file(path);
    FiffRawData raw(file);
    QCOMPARE(raw.last_samp - raw.first_samp + 1, 2000);
    Eigen::MatrixXd data, times;
    QVERIFY(raw.read_raw_segment(data, times));
    int compared = 0;
    for (std::size_t c = 0; c < names.size(); ++c) {
        if (names[c].rfind("EEG", 0) != 0) {
            continue;
        }
        // FIFFLIB drops the spaces of channel names on read.
        const auto row = static_cast<Eigen::Index>(raw.info.ch_names.indexOf(QString::fromStdString(names[c]).remove(QLatin1Char(' '))));
        QVERIFY2(row >= 0, names[c].c_str());
        for (std::size_t s = 0; s < 2000; ++s) {
            const double want = expected[s * names.size() + c];
            const double got = data(row, static_cast<Eigen::Index>(s));
            if (std::abs(got - want) > 1e-6 * std::max(1e-12, std::abs(want))) {
                QFAIL(qPrintable(QStringLiteral("%1 sample %2: %3 != %4").arg(names[c].c_str()).arg(s).arg(got).arg(want)));
            }
        }
        ++compared;
    }
    QCOMPARE(compared, 4);
}

//=============================================================================================================

void TestMneAcqTool::recordFailsOnUnwritablePath()
{
    const ToolResult record = runTool({QStringLiteral("record"), QStringLiteral("synthetic://?duration=1&realtime=0"),
                                       QStringLiteral("/does/not/exist/cli_raw.fif")});
    QCOMPARE(record.exitCode, 2);
    QVERIFY(!record.err.isEmpty());
}

//=============================================================================================================

void TestMneAcqTool::streamPublishesLsl()
{
    const std::string uri = "synthetic://?eeg=4&sfreq=1000&seed=5";
    const QString name = QStringLiteral("mne_acq_cli_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QProcess tool;
    tool.start(toolPath(), {QStringLiteral("stream"), QString::fromStdString(uri), QStringLiteral("--lsl"), QStringLiteral("--name"), name, QStringLiteral("--duration"), QStringLiteral("6")});
    QVERIFY(tool.waitForStarted());

    const auto found = LSLLIB::resolve_stream("name", name.toStdString(), 10.0, 1);
    QVERIFY2(!found.empty(), qPrintable(QString::fromUtf8(tool.readAllStandardError())));
    QCOMPARE(found.front().channel_count(), 5);
    QCOMPARE(found.front().nominal_srate(), 1000.0);

    LSLLIB::stream_inlet inlet(found.front());
    inlet.open_stream();
    std::vector<std::vector<float>> received;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (received.size() < 500 && std::chrono::steady_clock::now() < deadline) {
        auto chunk = inlet.pull_chunk_float();
        received.insert(received.end(), chunk.begin(), chunk.end());
        std::this_thread::sleep_for(10ms);
    }
    inlet.close_stream();
    QVERIFY2(received.size() >= 500, qPrintable(QString::number(received.size())));
    QVERIFY(tool.waitForFinished(20000));
    QCOMPARE(tool.exitCode(), 0);
    QVERIFY(QString::fromUtf8(tool.readAllStandardOutput()).contains(QStringLiteral("Published")));

    // The inlet joins mid-stream: find its first sample in the seeded source, then compare all received samples.
    std::vector<std::string> names;
    const std::vector<float> expected = readSource(uri + "&realtime=0&duration=10", names);
    const std::size_t channels = names.size();
    QCOMPARE(channels, std::size_t(5));
    const std::size_t total = expected.size() / channels;
    std::size_t offset = total;
    for (std::size_t s = 0; s + received.size() <= total; ++s) {
        if (std::equal(received.front().begin(), received.front().end(), expected.begin() + static_cast<std::ptrdiff_t>(s * channels))) {
            offset = s;
            break;
        }
    }
    QVERIFY2(offset < total, "first received sample is not in the source");
    for (std::size_t s = 0; s < received.size(); ++s) {
        QVERIFY(std::equal(received[s].begin(), received[s].end(),
                           expected.begin() + static_cast<std::ptrdiff_t>((offset + s) * channels)));
    }
}

//=============================================================================================================
// MAIN
//=============================================================================================================

QTEST_GUILESS_MAIN(TestMneAcqTool)
#include "test_mne_acq_tool.moc"
