//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     test_rt_server_fiff_replay.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Loopback test of mne_rt_server's FIFF simulator: the buffers a client receives equal the file.
 *
 * Starts mne_rt_server, points its FIFF simulator at the test data with the command client, starts streaming
 * to a data client and compares the received buffers with read_raw_segment of the same samples.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <com/rt_client/rt_cmd_client.h>
#include <com/rt_client/rt_data_client.h>
#include <fiff/fiff_constants.h>
#include <fiff/fiff_file.h>
#include <fiff/fiff_raw_data.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QtTest>

//=============================================================================================================
// EIGEN INCLUDES
//=============================================================================================================

#include <Eigen/Core>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace COMLIB;
using namespace FIFFLIB;

//=============================================================================================================
/**
 * DECLARE CLASS TestRtServerFiffReplay
 */
class TestRtServerFiffReplay : public QObject
{
    Q_OBJECT

private slots:
    void streamsTheFile();
    void cleanupTestCase();

private:
    QProcess m_server;
};

//=============================================================================================================

void TestRtServerFiffReplay::streamsTheFile()
{
    const QString base = QCoreApplication::applicationDirPath() + "/..";
    const QString file = base + "/resources/data/mne-cpp-test-data/MEG/sample/sample_audvis_trunc_raw.fif";
    if (!QFile::exists(file)) {
        QSKIP("MNE-CPP test data not found");
    }
    m_server.setProcessChannelMode(QProcess::MergedChannels);
    m_server.start(base + "/bin/mne_rt_server", {});
    QVERIFY(m_server.waitForStarted(10000));
    QByteArray output;
    while (!output.contains("fiff data port") && m_server.waitForReadyRead(30000)) {
        output += m_server.readAll();
    }
    QVERIFY2(output.contains("fiff data port"), output.constData());
    QVERIFY2(output.contains("Fiff File Simulator"), output.constData());

    RtCmdClient command;
    command.connectToHost(QStringLiteral("127.0.0.1"), 4217);
    QVERIFY(command.waitForConnected(5000));
    command.requestCommands();
    QVERIFY2(command.hasCommand(QStringLiteral("simfile")),
             qPrintable(QStringLiteral("help reply: ") + command.readAvailableData().left(400) + "\nserver: " +
                        QString::fromUtf8(output + m_server.readAll())));
    command[QStringLiteral("simfile")].pValues()[0].setValue(file);
    command[QStringLiteral("simfile")].send();
    QVERIFY(command.waitForDataAvailable(10000));
    QVERIFY2(command.readAvailableData().contains(QStringLiteral("succe")), "simfile was not accepted");
    const int bufsize = 100;
    command[QStringLiteral("bufsize")].pValues()[0].setValue(bufsize);
    command[QStringLiteral("bufsize")].send();
    QVERIFY(command.waitForDataAvailable(10000));
    command.readAvailableData();
    command[QStringLiteral("accel")].pValues()[0].setValue(100.0);
    command[QStringLiteral("accel")].send();
    QVERIFY(command.waitForDataAvailable(10000));
    command.readAvailableData();

    RtDataClient data;
    data.connectToHost(QStringLiteral("127.0.0.1"), 4218);
    QVERIFY(data.waitForConnected(5000));
    const qint32 id = data.getClientId();
    QVERIFY(id >= 0);
    command[QStringLiteral("measinfo")].pValues()[0].setValue(id);
    command[QStringLiteral("measinfo")].send();
    const FiffInfo::SPtr info = data.readMetadata().m_pInfo;
    QVERIFY(info);

    FiffRawData raw(file.toStdString());
    QCOMPARE(info->nchan, raw.info.nchan);
    QCOMPARE(info->sfreq, raw.info.sfreq * 100.0F); // the acceleration factor scales the advertised rate

    command[QStringLiteral("start")].pValues()[0].setValue(id);
    command[QStringLiteral("start")].send();
    // Buffers go out calibrated, as read_raw_segment returns them, and follow each other without gaps. A client
    // joins a running stream, so the first buffer it receives may lie anywhere in the file.
    const auto segment = [&raw](int b) {
        Eigen::MatrixXd expected, times;
        const fiff_int_t first = raw.first_samp + b * bufsize;
        return raw.read_raw_segment(expected, times, first, first + bufsize - 1) ? Eigen::MatrixXf(expected.cast<float>())
                                                                                 : Eigen::MatrixXf();
    };
    const int buffers = (raw.last_samp - raw.first_samp + 1) / bufsize;
    int offset = -1;
    // At 100 times real time a buffer is due every 1.7 ms, faster than the server polls its socket.
    for (int b = 0; b < 30; ++b) {
        Eigen::MatrixXf received;
        fiff_int_t kind = 0;
        for (int tag = 0; tag < 10 && kind != FIFF_DATA_BUFFER; ++tag) {
            data.readRawBuffer(info->nchan, received, kind); // clients skip other tags, e.g. the end of the info block
        }
        QCOMPARE(kind, FIFF_DATA_BUFFER);
        QCOMPARE(received.cols(), Eigen::Index(bufsize));
        for (int k = 0; offset < 0 && k < buffers; ++k) {
            if (received == segment(k)) {
                offset = k;
            }
        }
        QVERIFY2(offset >= 0, "the received buffer is no segment of the file");
        QVERIFY(received == segment(offset + b));
    }
    // A client that stops reading must not keep the server from answering commands.
    QThread::msleep(1000);
    QElapsedTimer reply;
    reply.start();
    command[QStringLiteral("stop-all")].send();
    QVERIFY2(reply.elapsed() < 2000, qPrintable(QStringLiteral("stop-all took %1 ms").arg(reply.elapsed())));
    data.disconnectFromHost();
    command.disconnectFromHost();
}

//=============================================================================================================

void TestRtServerFiffReplay::cleanupTestCase()
{
    if (m_server.state() == QProcess::NotRunning) {
        qWarning("mne_rt_server exited early (%d): %s", m_server.exitCode(), m_server.readAll().constData());
    } else {
        m_server.kill();
        m_server.waitForFinished(10000);
    }
}

QTEST_GUILESS_MAIN(TestRtServerFiffReplay)
#include "test_rt_server_fiff_replay.moc"
