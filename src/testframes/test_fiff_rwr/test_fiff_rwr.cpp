//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2015-2026 MNE-CPP Authors
 *
 * @file     test_fiff_rwr.cpp
 * @author   Gabriel Motta <gabrielbenmotta@gmail.com>;
 *           Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>
 * @since    0.1.0
 * @date     December, 2015
 * @brief    Test for reading writing reading a fiff raw file
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <utils/generics/mne_logger.h>

#include <fiff/fiff.h>
#include <fiff/fiff_dig_point_set.h>
#include <mne/mne_forward_solution.h>
#include <utils/byte_stream.h>

#include <iostream>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QtTest>
#include <QBuffer>
#include <QDirIterator>
#include <QTemporaryDir>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FIFFLIB;
using namespace Eigen;

//=============================================================================================================
// DEFINE LOCAL FUNCTIONS
//=============================================================================================================

namespace
{

QString testDataDir()
{
    return QCoreApplication::applicationDirPath() + "/../resources/data/mne-cpp-test-data";
}

std::uint64_t fnv1a(const QByteArray& bytes)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : bytes)
        hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
    return hash;
}

// File bytes with the file and block IDs zeroed, since they carry the time of writing
QByteArray stableBytes(QByteArray bytes)
{
    QBuffer buffer(&bytes);
    FiffStream stream(&buffer);
    if (!stream.open())
        return {};
    const QList<FiffDirEntry::SPtr> dir = stream.dir();
    stream.close();
    for (const FiffDirEntry::SPtr& entry : dir) {
        if (entry->pos >= 0 && (entry->kind == FIFF_FILE_ID || entry->kind == FIFF_BLOCK_ID || entry->kind == FIFF_PARENT_FILE_ID || entry->kind == FIFF_PARENT_BLOCK_ID))
            bytes.replace(entry->pos + 16, entry->size, QByteArray(entry->size, '\0'));
    }
    return bytes;
}

// Kind, type and size of every tag in order: computed float data may round differently per compiler
QByteArray tagLayout(QByteArray bytes)
{
    QBuffer buffer(&bytes);
    FiffStream stream(&buffer);
    if (!stream.open())
        return {};
    QByteArray layout;
    for (const FiffDirEntry::SPtr& entry : stream.dir())
        layout += QByteArray::number(entry->kind) + ':' + QByteArray::number(entry->type) + ':' + QByteArray::number(entry->size) + ';';
    stream.close();
    return layout;
}

} // namespace

//=============================================================================================================
/**
 * DECLARE CLASS TestFiffRWR
 *
 * @brief The TestFiffRWR class provides read write read fiff verification tests
 *
 */
class TestFiffRWR : public QObject
{
    Q_OBJECT

public:
    TestFiffRWR();

private slots:
    void initTestCase();
    void compareData();
    void compareTimes();
    void compareInfo();
    void tagsRewriteByteIdentical_data();
    void tagsRewriteByteIdentical();
    void readsWithoutQtDevice();
    void opensAndStartsFilesByPath();
    void writersStable_data();
    void writersStable();
    void cleanupTestCase();

private:
    double dEpsilon;
    QTemporaryDir m_tempDir;

    FiffRawData rawFirstInRaw;

    MatrixXd mFirstInData;
    MatrixXd mFirstInTimes;

    FiffRawData rawSecondInRaw;

    MatrixXd mSecondInData;
    MatrixXd mSecondInTimes;
};

//=============================================================================================================

TestFiffRWR::TestFiffRWR()
: dEpsilon(0.000001)
{
}

//=============================================================================================================

void TestFiffRWR::initTestCase()
{
    qInstallMessageHandler(UTILSLIB::MNELogger::customLogWriter);
    qDebug() << "dEpsilon" << dEpsilon;

    QFile t_fileIn(QCoreApplication::applicationDirPath() + "/../resources/data/mne-cpp-test-data/MEG/sample/sample_audvis_trunc_raw.fif");
    QVERIFY(m_tempDir.isValid());
    QFile t_fileOut(m_tempDir.filePath("sample_audvis_trunc_raw_test_rwr_out.fif"));

    //*********************************************************************************************************
    // First Read & Write
    //*********************************************************************************************************

    printf(">>>>>>>>>>>>>>>>>>>>>>>>> Read & Write >>>>>>>>>>>>>>>>>>>>>>>>>\n");

    //
    //   Setup for reading the raw data
    //
    rawFirstInRaw = FiffRawData(t_fileIn);

    //
    //   Set up pick list: MEG + STI 014 - bad channels
    //
    //
    bool bWantMeg = true;
    bool bWantEeg = false;
    bool bWantStim = false;
    StringList include{"STI 014"};

    MatrixXi vPicks = rawFirstInRaw.info.pick_types(bWantMeg, bWantEeg, bWantStim, include, rawFirstInRaw.info.bads); // prefer member function
    if (vPicks.cols() == 0) {
        include = {"STI101", "STI201", "STI301"};
        vPicks = rawFirstInRaw.info.pick_types(bWantMeg, bWantEeg, bWantStim, include, rawFirstInRaw.info.bads); // prefer member function
        if (vPicks.cols() == 0) {
            printf("channel list may need modification\n");
        }
    }
    //
    RowVectorXd vCals;

    FiffStream::SPtr outfid = FiffStream::start_writing_raw(t_fileOut, rawFirstInRaw.info, vCals /*, vPicks*/);

    //
    //   Set up the reading parameters
    //
    float fQuantumSec = 1.0f; //read and write in 1 sec junks
    fiff_int_t from = rawFirstInRaw.first_samp;
    fiff_int_t to = rawFirstInRaw.last_samp;
    fiff_int_t quantum = ceil(fQuantumSec * rawFirstInRaw.info.sfreq);

    //
    //   To read the whole file at once set
    //
    //quantum     = to - from + 1;
    //
    //
    //   Read and write all the data
    //
    bool bFirstBuffer = true;

    fiff_int_t first, last;

    for (first = from; first < to; first += quantum) {
        last = first + quantum - 1;
        if (last > to) {
            last = to;
        }

        if (!rawFirstInRaw.read_raw_segment(mFirstInData, mFirstInTimes, first, last /*,vPicks*/)) {
            printf("error during read_raw_segment\n");
        }
        //
        //   You can add your own miracle here
        //
        printf("Writing...");
        if (bFirstBuffer) {
            if (first > 0)
                outfid->write_int(FIFF_FIRST_SAMPLE, &first);
            bFirstBuffer = false;
        }
        outfid->write_raw_buffer(mFirstInData, vCals);
        printf("[done]\n");
    }

    outfid->finish_writing_raw();

    printf("<<<<<<<<<<<<<<<<<<<<<<<<< Read & Write Finished <<<<<<<<<<<<<<<<<<<<<<<<<\n");

    //*********************************************************************************************************
    // Second Read
    //*********************************************************************************************************

    printf(">>>>>>>>>>>>>>>>>>>>>>>>> Read Again >>>>>>>>>>>>>>>>>>>>>>>>>\n");

    //
    //   Setup for reading the raw data from out file
    //
    rawSecondInRaw = FiffRawData(t_fileOut);

    //
    //   Set up pick list: MEG + STI 014 - bad channels
    //
    //

    vPicks = rawSecondInRaw.info.pick_types(bWantMeg, bWantEeg, bWantStim, include, rawSecondInRaw.info.bads); // prefer member function
    if (vPicks.cols() == 0) {
        include = {"STI101", "STI201", "STI301"};
        vPicks = rawSecondInRaw.info.pick_types(bWantMeg, bWantEeg, bWantStim, include, rawSecondInRaw.info.bads); // prefer member function
        if (vPicks.cols() == 0) {
            printf("channel list may need modification\n");
        }
    }

    //
    //   Read all the data
    //

    for (first = from; first < to; first += quantum) {
        last = first + quantum - 1;
        if (last > to) {
            last = to;
        }

        if (!rawSecondInRaw.read_raw_segment(mSecondInData, mSecondInTimes, first, last /*,vPicks*/)) {
            printf("error during read_raw_segment\n");
        }
    }

    printf("<<<<<<<<<<<<<<<<<<<<<<<<< Read Again Finished <<<<<<<<<<<<<<<<<<<<<<<<<\n");
}

//=============================================================================================================

void TestFiffRWR::compareData()
{
    MatrixXd mDataDiff = mFirstInData - mSecondInData;
    //    std::cout << "\tCompare data:\n";
    //    std::cout << "\tFirst data\n" << mFirstInData.block(0,0,4,4) << "\n";
    //    std::cout << "\tSecond data\n" << mSecondInData.block(0,0,4,4) << "\n";

    QVERIFY(mDataDiff.sum() < dEpsilon);
}

//=============================================================================================================

void TestFiffRWR::compareTimes()
{
    MatrixXd mTimesDiff = mFirstInTimes - mSecondInTimes;
    //    std::cout << "\tCompare Times:\n";
    //    std::cout << "\tFirst times\n" << mFirstInTimes.block(0,0,1,4) << "\n";
    //    std::cout << "\tSecond times\n" << mSecondInTimes.block(0,0,1,4) << "\n";

    QVERIFY(mTimesDiff.sum() < dEpsilon);
}

//=============================================================================================================

void TestFiffRWR::compareInfo()
{
    //Sampling frequency
    std::cout << "[1] Sampling Frequency Check\n";
    QVERIFY(rawFirstInRaw.info.sfreq == rawSecondInRaw.info.sfreq);

    //Projection
    std::cout << "[2] Projection Check\n";
    QVERIFY(rawFirstInRaw.info.projs.size() == rawSecondInRaw.info.projs.size());

    for (qint32 i = 0; i < rawFirstInRaw.info.projs.size(); ++i) {
        std::cout << "Projector " << i << std::endl;
        MatrixXd mTmp = rawFirstInRaw.info.projs[i].data->data - rawSecondInRaw.info.projs[i].data->data;
        QVERIFY(mTmp.sum() < dEpsilon);
    }

    //Compensators
    std::cout << "[3] Compensator Check\n";
    QVERIFY(rawFirstInRaw.info.comps.size() == rawSecondInRaw.info.comps.size());

    for (qint32 i = 0; i < rawFirstInRaw.info.comps.size(); ++i) {
        std::cout << "Compensator " << i << std::endl;
        MatrixXd mTmp = rawFirstInRaw.info.comps[i].data->data - rawSecondInRaw.info.comps[i].data->data;
        QVERIFY(mTmp.sum() < dEpsilon);
    }
}

//=============================================================================================================

void TestFiffRWR::tagsRewriteByteIdentical_data()
{
    QTest::addColumn<QString>("path");
    QDirIterator it(testDataDir(), {"*.fif"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList paths;
    while (it.hasNext())
        paths << it.next();
    paths.sort();
    for (const QString& path : std::as_const(paths))
        QTest::newRow(qPrintable(QDir(testDataDir()).relativeFilePath(path))) << path;
}

//=============================================================================================================

void TestFiffRWR::tagsRewriteByteIdentical()
{
    QFETCH(QString, path);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray original = file.readAll();
    file.close();

    FiffStream stream(&file);
    QVERIFY(stream.open());
    QVERIFY(!stream.dir().isEmpty());

    // Every tag read and written again must reproduce the bytes in the file, header and payload
    QByteArray rewritten;
    QBuffer buffer(&rewritten);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    FiffStream out(&buffer);
    for (const FiffDirEntry::SPtr& entry : stream.dir()) {
        // The stored directory ends with a sentinel entry
        if (entry->pos < 0)
            continue;
        FiffTag::UPtr tag;
        QVERIFY(stream.read_tag(tag, entry->pos));
        rewritten.clear();
        buffer.seek(0);
        out.write_tag(tag, 0);
        const QByteArray expected = original.mid(entry->pos, 16 + entry->size);
        if (rewritten != expected)
            QFAIL(qPrintable(QString("tag kind %1 type %2 at %3 differs").arg(entry->kind).arg(entry->type).arg(entry->pos)));
    }
    stream.close();
}

//=============================================================================================================

void TestFiffRWR::readsWithoutQtDevice()
{
    // A stream on a plain file byte stream sees the same directory and tags as one on a QFile
    const QString path = testDataDir() + "/MEG/sample/sample_audvis-cov.fif";
    QFile qtFile(path);
    FiffStream qtStream(&qtFile);
    QVERIFY(qtStream.open());

    auto bytes = UTILSLIB::FileByteStream::open(path.toStdString(), UTILSLIB::FileByteStream::Mode::Read);
    QVERIFY(bytes);
    FiffStream stream(std::move(bytes));
    QVERIFY(stream.device() == nullptr);
    QVERIFY(stream.open());
    QCOMPARE(stream.nent(), qtStream.nent());
    QCOMPARE(stream.id().machid[0], qtStream.id().machid[0]);

    for (int i = 0; i < stream.nent(); ++i) {
        const FiffDirEntry::SPtr& entry = stream.dir()[i];
        if (entry->pos < 0)
            continue;
        FiffTag::UPtr tag;
        FiffTag::UPtr qtTag;
        QVERIFY(stream.read_tag(tag, entry->pos));
        QVERIFY(qtStream.read_tag(qtTag, entry->pos));
        QCOMPARE(tag->kind, qtTag->kind);
        QCOMPARE(QByteArray(tag->data(), tag->size()), QByteArray(qtTag->data(), qtTag->size()));
    }

    FiffCov cov;
    QVERIFY(stream.read_cov(stream.dirtree(), FIFFV_MNE_NOISE_COV, cov));
    QCOMPARE(cov.dim, 366);
    qtStream.close();
}

//=============================================================================================================

void TestFiffRWR::opensAndStartsFilesByPath()
{
    const std::string path = (testDataDir() + "/MEG/sample/all-trans.fif").toStdString();
    FiffStream::SPtr in = FiffStream::open(path);
    QVERIFY(in);
    QVERIFY(in->device() == nullptr);
    QCOMPARE(in->streamName(), QString::fromStdString(path));
    QVERIFY(in->nent() > 0);
    QVERIFY(!FiffStream::open(m_tempDir.filePath("missing.fif").toStdString()));

    // A file started by path is complete on disk once the stream is closed, before the stream goes away
    const std::string outPath = m_tempDir.filePath("by-path.fif").toStdString();
    FiffStream::SPtr out = FiffStream::start_file(outPath);
    QVERIFY(out);
    for (const FiffDirEntry::SPtr& entry : in->dir()) {
        FiffTag::UPtr tag;
        if (entry->kind == FIFF_COORD_TRANS && in->read_tag(tag, entry->pos))
            out->write_tag(tag);
    }
    out->end_file();
    out->close();
    FiffStream::SPtr back = FiffStream::open(outPath);
    QVERIFY(back);
    int transforms = 0;
    for (const FiffDirEntry::SPtr& entry : back->dir())
        transforms += entry->kind == FIFF_COORD_TRANS ? 1 : 0;
    QVERIFY(transforms > 0);
    QVERIFY(!FiffStream::start_file(m_tempDir.filePath("no/dir/out.fif").toStdString()));
}

//=============================================================================================================

void TestFiffRWR::writersStable_data()
{
    QTest::addColumn<QString>("writer");
    QTest::addColumn<qulonglong>("hash");
    QTest::addColumn<qlonglong>("size");
    QTest::addColumn<bool>("layoutOnly");
    QTest::newRow("raw") << "raw" << qulonglong(0x7315ac1531102ed1ULL) << qlonglong(12036488) << false;
    QTest::newRow("evoked") << "evoked" << qulonglong(0xf88ccee03ee4b16eULL) << qlonglong(2581535) << true;
    QTest::newRow("covariance") << "cov" << qulonglong(0xc41f39abd7c6cd27ULL) << qlonglong(545712) << false;
    QTest::newRow("transform") << "trans" << qulonglong(0x9199331869f76d3eULL) << qlonglong(212) << false;
    QTest::newRow("digitizer") << "dig" << qulonglong(0xc59bdb7c1024cdc8ULL) << qlonglong(5468) << false;
    QTest::newRow("forward") << "fwd" << qulonglong(0x176c6bb765acd4afULL) << qlonglong(55417952) << true;
}

//=============================================================================================================

void TestFiffRWR::writersStable()
{
    QFETCH(QString, writer);
    QFETCH(qulonglong, hash);
    QFETCH(qlonglong, size);
    QFETCH(bool, layoutOnly);

    // Pins the bytes each writer produces for a fixture, so moving the stream layer cannot change the files
    QByteArray bytes;
    QBuffer out(&bytes);
    const QString meg = testDataDir() + "/MEG/sample/";
    if (writer == "raw") {
        QFile in(meg + "sample_audvis_trunc_raw.fif");
        const FiffRawData raw(in);
        QVERIFY(raw.save(out));
    } else if (writer == "evoked") {
        QFile in(meg + "sample_audvis-ave.fif");
        const FiffEvokedSet evoked(in);
        QVERIFY(evoked.save(out));
    } else if (writer == "cov") {
        QFile in(meg + "sample_audvis-cov.fif");
        const FiffCov cov(in);
        const QString path = m_tempDir.filePath("stable-cov.fif");
        QVERIFY(cov.save(path));
        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly));
        bytes = written.readAll();
    } else if (writer == "trans") {
        QFile in(meg + "all-trans.fif");
        FiffCoordTrans trans(in);
        QVERIFY(trans.write(out));
    } else if (writer == "dig") {
        QFile in(meg + "sample_audvis_trunc_raw.fif");
        FiffDigPointSet dig(in);
        QVERIFY(dig.size() > 0);
        QVERIFY(dig.write(out));
    } else if (writer == "fwd") {
        QFile in(testDataDir() + "/Result/ref-sample_audvis-meg-eeg-oct-6-fwd.fif");
        const MNELIB::MNEForwardSolution fwd(in);
        QVERIFY(fwd.write(out));
    }

    const QByteArray stable = stableBytes(bytes);
    QVERIFY(!stable.isEmpty());
    QCOMPARE(qlonglong(stable.size()), size);
    QCOMPARE(qulonglong(fnv1a(layoutOnly ? tagLayout(stable) : stable)), hash);
}

//=============================================================================================================

void TestFiffRWR::cleanupTestCase()
{
}

//=============================================================================================================
// MAIN
//=============================================================================================================

QTEST_GUILESS_MAIN(TestFiffRWR)
#include "test_fiff_rwr.moc"
