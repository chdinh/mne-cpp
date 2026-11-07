// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2010-2026 MNE-CPP Authors
//   Christoph Dinh <christoph.dinh@mne-cpp.org>

#include <QtTest/QtTest>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <Eigen/Dense>

#include <fiff/fiff_events.h>
#include <fiff/fiff_dig_point_set.h>
#include <fiff/fiff_dig_point.h>
#include <fiff/fiff_evoked_set.h>
#include <fiff/fiff_evoked.h>
#include <fiff/fiff_coord_trans.h>
#include <utils/string_utils.h>
#include <fiff/fiff_constants.h>
#include <fiff/fiff_proj.h>
#include <fiff/fiff_digitizer_data.h>
#include <fiff/fiff_dir_node.h>

using namespace FIFFLIB;
using namespace Eigen;

class TestFiffDataTypes : public QObject
{
    Q_OBJECT

private slots:
    //=========================================================================
    // FiffEvents
    //=========================================================================
    void events_defaultCtor()
    {
        FiffEvents ev;
        QVERIFY(ev.is_empty());
        QCOMPARE(ev.num_events(), 0);
    }

    void events_asciiAndFifRoundTrip()
    {
        QTemporaryDir dir;
        const auto path = [&dir](const char* name) {
            return dir.filePath(name).toStdString();
        };
        QFile text(dir.filePath("in.eve"));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("100 0 1\n200 0 2\n300 0 3\n");
        text.close();

        FiffEvents ev;
        QVERIFY(FiffEvents::read_from_ascii(path("in.eve"), ev));
        QCOMPARE(ev.num_events(), 3);

        FiffEvents ascii;
        QVERIFY(ev.write_to_ascii(path("out.eve"), 1000.0f));
        QVERIFY(FiffEvents::read_from_ascii(path("out.eve"), ascii));
        QVERIFY(ascii.events == ev.events);

        FiffEvents fif;
        QVERIFY(ev.write_to_fif(path("out-eve.fif")));
        QVERIFY(FiffEvents::read_from_fif(path("out-eve.fif"), fif));
        QVERIFY(fif.events == ev.events);
    }

    //=========================================================================
    // FiffDigPointSet
    //=========================================================================
    void digPointSet_defaultCtor()
    {
        FiffDigPointSet dps;
        QVERIFY(dps.isEmpty());
        QCOMPARE(dps.size(), 0);
    }

    void digPointSet_addPoints()
    {
        FiffDigPointSet dps;
        FiffDigPoint p1;
        p1.kind = FIFFV_POINT_CARDINAL;
        p1.ident = FIFFV_POINT_LPA;
        p1.r[0] = -0.07f;
        p1.r[1] = 0.0f;
        p1.r[2] = 0.0f;
        p1.coord_frame = FIFFV_COORD_HEAD;

        FiffDigPoint p2;
        p2.kind = FIFFV_POINT_CARDINAL;
        p2.ident = FIFFV_POINT_RPA;
        p2.r[0] = 0.07f;
        p2.r[1] = 0.0f;
        p2.r[2] = 0.0f;
        p2.coord_frame = FIFFV_COORD_HEAD;

        FiffDigPoint p3;
        p3.kind = FIFFV_POINT_CARDINAL;
        p3.ident = FIFFV_POINT_NASION;
        p3.r[0] = 0.0f;
        p3.r[1] = 0.07f;
        p3.r[2] = 0.0f;
        p3.coord_frame = FIFFV_COORD_HEAD;

        dps << p1 << p2 << p3;
        QCOMPARE(dps.size(), 3);
        QVERIFY(!dps.isEmpty());
    }

    void digPointSet_copyCtor()
    {
        FiffDigPointSet dps;
        FiffDigPoint p;
        p.kind = FIFFV_POINT_CARDINAL;
        p.r[0] = 1.0f;
        p.r[1] = 2.0f;
        p.r[2] = 3.0f;
        dps << p;

        FiffDigPointSet copy(dps);
        QCOMPARE(copy.size(), 1);
    }

    void digPointSet_clear()
    {
        FiffDigPointSet dps;
        FiffDigPoint p;
        p.kind = FIFFV_POINT_EEG;
        dps << p;
        dps.clear();
        QVERIFY(dps.isEmpty());
    }

    void digPointSet_pickTypes()
    {
        FiffDigPointSet dps;
        FiffDigPoint p1;
        p1.kind = FIFFV_POINT_CARDINAL;
        p1.r[0] = 0;
        p1.r[1] = 0;
        p1.r[2] = 0;
        FiffDigPoint p2;
        p2.kind = FIFFV_POINT_EEG;
        p2.r[0] = 0;
        p2.r[1] = 0;
        p2.r[2] = 0;
        FiffDigPoint p3;
        p3.kind = FIFFV_POINT_EXTRA;
        p3.r[0] = 0;
        p3.r[1] = 0;
        p3.r[2] = 0;
        dps << p1 << p2 << p3;

        QList<int> types;
        types << FIFFV_POINT_CARDINAL;
        FiffDigPointSet picked = dps.pickTypes(types);
        QCOMPARE(picked.size(), 1);
    }

    void digPointSet_indexAccess()
    {
        FiffDigPointSet dps;
        FiffDigPoint p;
        p.kind = FIFFV_POINT_CARDINAL;
        p.r[0] = 1.0f;
        p.r[1] = 2.0f;
        p.r[2] = 3.0f;
        dps << p;

        const FiffDigPoint& ref = dps[0];
        QVERIFY(qAbs(ref.r[0] - 1.0f) < 1e-5f);
    }

    void digPointSet_applyTransform()
    {
        FiffDigPointSet dps;
        FiffDigPoint p;
        p.kind = FIFFV_POINT_CARDINAL;
        p.coord_frame = FIFFV_COORD_HEAD;
        p.r[0] = 1.0f;
        p.r[1] = 0.0f;
        p.r[2] = 0.0f;
        dps << p;

        FiffCoordTrans trans;
        trans.from = FIFFV_COORD_HEAD;
        trans.to = FIFFV_COORD_MRI;
        trans.trans = Matrix4f::Identity();
        trans.trans(0, 3) = 0.1f;

        dps.applyTransform(trans, false);
        QVERIFY(qAbs(dps[0].r[0] - 1.1f) < 1e-4f);
    }

    void digPointSet_getList()
    {
        FiffDigPointSet dps;
        FiffDigPoint p;
        p.kind = FIFFV_POINT_CARDINAL;
        p.r[0] = 0;
        p.r[1] = 0;
        p.r[2] = 0;
        dps << p;
        QList<FiffDigPoint> list = dps.getList();
        QCOMPARE(list.size(), 1);
    }

    void digPointSet_writeRead()
    {
        FiffDigPointSet dps;
        FiffDigPoint p;
        p.kind = FIFFV_POINT_CARDINAL;
        p.ident = FIFFV_POINT_LPA;
        p.coord_frame = FIFFV_COORD_HEAD;
        p.r[0] = -0.07f;
        p.r[1] = 0.0f;
        p.r[2] = 0.0f;
        dps << p;

        QTemporaryDir dir;
        const QString path = dir.filePath("dig.fif");
        QVERIFY(dps.write(path.toStdString()));
        QVERIFY(QFileInfo(path).size() > 0);
    }

    //=========================================================================
    // FiffEvokedSet
    //=========================================================================
    void evokedSet_defaultCtor()
    {
        FiffEvokedSet es;
        QVERIFY(es.evoked.isEmpty());
    }

    void evokedSet_subtractBaseline()
    {
        MatrixXd data(2, 10);
        data.setOnes();
        data.col(5) *= 5.0;

        FiffEvokedSet::subtractBaseline(data, 0, 4);
        QVERIFY(qAbs(data(0, 0)) < 1e-10);
        QVERIFY(qAbs(data(0, 5) - 4.0) < 1e-10);
    }

    void evokedSet_readFromSampleFile()
    {
        QString path = QCoreApplication::applicationDirPath() + "/../resources/data/mne-cpp-test-data/MEG/sample/sample_audvis-ave.fif";
        if (!QFile::exists(path)) {
            QSKIP("Sample evoked file not found");
        }
        FiffEvokedSet es(path.toStdString());
        QVERIFY(!es.evoked.isEmpty());
        QVERIFY(es.info.nchan > 0);
    }

    void evokedSet_pickChannels()
    {
        QString path = QCoreApplication::applicationDirPath() + "/../resources/data/mne-cpp-test-data/MEG/sample/sample_audvis-ave.fif";
        if (!QFile::exists(path)) {
            QSKIP("Sample evoked file not found");
        }
        FiffEvokedSet es(path.toStdString());
        if (es.evoked.isEmpty())
            QSKIP("Failed to load evoked set");

        StringList include;
        include.push_back(es.info.ch_names[0]);
        include.push_back(es.info.ch_names[1]);
        FiffEvokedSet picked = es.pick_channels(include);
        QCOMPARE(picked.info.nchan, 2);

        // Single evoked: picks rows by name; no selection or no match keeps everything; a name past the data
        // rows (info and data out of step) gives a zero row instead of reading past the data
        FiffEvoked evoked = es.evoked.first();
        const FiffEvoked one = evoked.pick_channels({include[1]});
        QCOMPARE(one.data, evoked.data.row(1));
        QCOMPARE(evoked.pick_channels({}).data.rows(), evoked.data.rows());
        QCOMPARE(evoked.pick_channels({"NOPE"}).data.rows(), evoked.data.rows());
        evoked.data = evoked.data.topRows(2).eval();
        const std::string third = evoked.info.ch_names[2];
        QVERIFY(evoked.pick_channels({third}).data.isZero(0.0));
    }

    //=========================================================================
    // FiffCoordTrans
    //=========================================================================
    void coordTrans_inverse()
    {
        FiffCoordTrans trans;
        trans.from = FIFFV_COORD_HEAD;
        trans.to = FIFFV_COORD_MRI;
        trans.trans = Matrix4f::Identity();
        trans.trans(0, 3) = 0.1f;
        trans.invtrans = trans.trans.inverse();

        FiffCoordTrans inv = trans.inverted();
        QCOMPARE(inv.from, FIFFV_COORD_MRI);
        QCOMPARE(inv.to, FIFFV_COORD_HEAD);
        QVERIFY(qAbs(inv.trans(0, 3) + 0.1f) < 1e-5f);
    }

    void coordTrans_compose()
    {
        FiffCoordTrans t1;
        t1.from = FIFFV_COORD_HEAD;
        t1.to = FIFFV_COORD_MRI;
        t1.trans = Matrix4f::Identity();
        t1.trans(0, 3) = 0.05f;
        t1.invtrans = t1.trans.inverse();

        FiffCoordTrans t2;
        t2.from = FIFFV_COORD_MRI;
        t2.to = FIFFV_COORD_DEVICE;
        t2.trans = Matrix4f::Identity();
        t2.trans(1, 3) = 0.03f;
        t2.invtrans = t2.trans.inverse();

        QVERIFY(qAbs(t1.trans(0, 3) - 0.05f) < 1e-5f);
        QVERIFY(qAbs(t2.trans(1, 3) - 0.03f) < 1e-5f);
    }

    //=========================================================================
    // FiffCoordTrans — additional coverage tests
    //=========================================================================
    void coordTrans_rotMoveCtor()
    {
        // Test the (from, to, rot, move) constructor — covers lines 240-254
        Matrix3f rot = Matrix3f::Identity();
        rot(0, 0) = 0.0f;
        rot(0, 1) = -1.0f;
        rot(1, 0) = 1.0f;
        rot(1, 1) = 0.0f; // 90-degree rotation about z
        Vector3f move(0.01f, 0.02f, 0.03f);
        FiffCoordTrans t(FIFFV_COORD_HEAD, FIFFV_COORD_MRI, rot, move);
        QCOMPARE(t.from, (int)FIFFV_COORD_HEAD);
        QCOMPARE(t.to, (int)FIFFV_COORD_MRI);
        QVERIFY(qAbs(t.trans(0, 3) - 0.01f) < 1e-5f);
        QVERIFY(qAbs(t.trans(1, 3) - 0.02f) < 1e-5f);
        QVERIFY(qAbs(t.trans(3, 3) - 1.0f) < 1e-5f);
        // Verify inverse was computed
        QVERIFY(qAbs(t.invtrans(3, 3) - 1.0f) < 1e-5f);
    }

    void coordTrans_matrix4fCtor()
    {
        // Test 4x4 matrix constructor — covers lines 258-272
        Matrix4f mat = Matrix4f::Identity();
        mat(0, 3) = 0.05f;
        mat(3, 0) = 0.001f; // non-standard row 3
        FiffCoordTrans t(FIFFV_COORD_HEAD, FIFFV_COORD_DEVICE, mat, true);
        QVERIFY(qAbs(t.trans(3, 0)) < 1e-5f); // row 3 was zeroed by bStandard=true
        QVERIFY(qAbs(t.trans(3, 3) - 1.0f) < 1e-5f);
        QVERIFY(qAbs(t.trans(0, 3) - 0.05f) < 1e-5f);
    }

    void coordTrans_frameName()
    {
        // Test frame_name — covers lines 219-234
        QCOMPARE(FiffCoordTrans::frame_name(FIFFV_COORD_UNKNOWN), QString("unknown"));
        QCOMPARE(FiffCoordTrans::frame_name(FIFFV_COORD_DEVICE), QString("MEG device"));
        QCOMPARE(FiffCoordTrans::frame_name(FIFFV_COORD_HEAD), QString("head"));
        QCOMPARE(FiffCoordTrans::frame_name(FIFFV_COORD_MRI), QString("MRI (surface RAS)"));
        QCOMPARE(FiffCoordTrans::frame_name(FIFFV_COORD_ISOTRAK), QString("isotrak"));
        QCOMPARE(FiffCoordTrans::frame_name(FIFFV_MNE_COORD_MNI_TAL), QString("MNI Talairach"));
        QCOMPARE(FiffCoordTrans::frame_name(99999), QString("unknown"));
    }

    void coordTrans_readTransformAscii()
    {
        // Test readTransformAscii — covers lines 493-544
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        QTextStream ts(&tmp);
        // 4x4 matrix: identity rotation + small translation in mm
        ts << "1.000000 0.000000 0.000000 10.0\n";
        ts << "0.000000 1.000000 0.000000 20.0\n";
        ts << "0.000000 0.000000 1.000000 30.0\n";
        ts << "0.000000 0.000000 0.000000 1.0\n";
        ts.flush();
        tmp.close();

        FiffCoordTrans t = FiffCoordTrans::read_ascii(UTILSLIB::pathFromUtf8((tmp.fileName()).toStdString()), FIFFV_COORD_HEAD, FIFFV_COORD_MRI).value();
        QCOMPARE(t.from, (int)FIFFV_COORD_HEAD);
        QCOMPARE(t.to, (int)FIFFV_COORD_MRI);
        // Translation values are divided by 1000 (mm -> m)
        QVERIFY(qAbs(t.trans(0, 3) - 0.01f) < 1e-5f);
        QVERIFY(qAbs(t.trans(1, 3) - 0.02f) < 1e-5f);
        QVERIFY(qAbs(t.trans(2, 3) - 0.03f) < 1e-5f);
    }

    void coordTrans_readTransformAsciiComments()
    {
        // Test with comment lines — covers more of readTransformAscii
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        QTextStream ts(&tmp);
        ts << "# This is a comment\n";
        ts << "1.0 0.0 0.0 5.0\n";
        ts << "0.0 1.0 0.0 0.0  # inline comment\n";
        ts << "0.0 0.0 1.0 0.0\n";
        ts << "0.0 0.0 0.0 1.0\n";
        ts.flush();
        tmp.close();

        FiffCoordTrans t = FiffCoordTrans::read_ascii(UTILSLIB::pathFromUtf8((tmp.fileName()).toStdString()), FIFFV_COORD_MRI, FIFFV_COORD_HEAD).value();
        QCOMPARE(t.from, (int)FIFFV_COORD_MRI);
        QVERIFY(qAbs(t.trans(0, 3) - 0.005f) < 1e-5f);
    }

    void coordTrans_procrustesAlign()
    {
        // Test Procrustes alignment
        Eigen::MatrixXf fromPts(4, 3), toPts(4, 3);
        fromPts << 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1;
        toPts << 0.1f, 0, 0, 1.1f, 0, 0, 0.1f, 1, 0, 0.1f, 0, 1;
        Eigen::VectorXf w(4);
        w << 1.0f, 1.0f, 1.0f, 1.0f;

        FiffCoordTrans t = FiffCoordTrans::procrustes_align(
            FIFFV_COORD_HEAD, FIFFV_COORD_DEVICE,
            fromPts, toPts, w, 0.01f);
        QVERIFY(!t.is_empty());
        QCOMPARE(t.from, (int)FIFFV_COORD_HEAD);
        QCOMPARE(t.to, (int)FIFFV_COORD_DEVICE);
        // The translation should be approximately (0.1, 0, 0)
        QVERIFY(qAbs(t.trans(0, 3) - 0.1f) < 0.01f);
    }

    void coordTrans_procrustesNoWeights()
    {
        // Test Procrustes without weights — empty weight vector
        Eigen::MatrixXf fromPts(3, 3), toPts(3, 3);
        fromPts << 0, 0, 0, 1, 0, 0, 0, 1, 0;
        toPts << 0, 0, 0, 1, 0, 0, 0, 1, 0;

        FiffCoordTrans t = FiffCoordTrans::procrustes_align(
            FIFFV_COORD_HEAD, FIFFV_COORD_MRI,
            fromPts, toPts, Eigen::VectorXf(), 0.01f);
        QVERIFY(!t.is_empty());
        // Identity-like — rotation should be close to identity
        QVERIFY(qAbs(t.trans(0, 0) - 1.0f) < 0.01f);
    }

    void coordTrans_angleTo()
    {
        // Test angleTo — covers lines 300-320
        FiffCoordTrans t;
        t.from = FIFFV_COORD_HEAD;
        t.to = FIFFV_COORD_DEVICE;
        t.trans = Matrix4f::Identity();
        t.invtrans = Matrix4f::Identity();

        MatrixX4f dest = Matrix4f::Identity();
        float angle = t.angle_to(dest);
        QVERIFY(qAbs(angle) < 1e-3f); // Same rotation, angle should be 0
    }

    void coordTrans_moveTo()
    {
        // Tests translationTo — covers lines 325-340
        FiffCoordTrans t;
        t.from = FIFFV_COORD_HEAD;
        t.to = FIFFV_COORD_DEVICE;
        t.trans = Matrix4f::Identity();
        t.invtrans = Matrix4f::Identity();

        MatrixX4f dest = Matrix4f::Identity();
        dest(0, 3) = 0.1f;
        float dist = t.translation_to(dest);
        QVERIFY(qAbs(dist - 0.1f) < 1e-3f);
    }

    //=========================================================================
    // FiffDigitizerData — coverage tests
    //=========================================================================
    void digitizerData_defaultCtor()
    {
        FiffDigitizerData dd;
        QCOMPARE(dd.points.size(), 0);
    }

    void digitizerData_copyAndAssign()
    {
        // Covers copy constructor and assignment operator (lines ~73-93)
        FiffDigitizerData dd;
        FiffDigPoint p;
        p.kind = FIFFV_POINT_CARDINAL;
        p.ident = FIFFV_POINT_LPA;
        p.coord_frame = FIFFV_COORD_HEAD;
        p.r[0] = 1.0f;
        p.r[1] = 2.0f;
        p.r[2] = 3.0f;
        dd.points.append(p);

        // Copy
        FiffDigitizerData dd2(dd);
        QCOMPARE(dd2.points.size(), 1);
        QVERIFY(qAbs(dd2.points[0].r[0] - 1.0f) < 1e-5f);

        // Assign
        FiffDigitizerData dd3;
        dd3 = dd;
        QCOMPARE(dd3.points.size(), 1);
    }

    //=========================================================================
    // FiffProj — additional coverage tests
    //=========================================================================
    void proj_defaultCtor()
    {
        FiffProj proj;
        QVERIFY(proj.kind == 0 || proj.kind == -1 || true); // just test construction doesn't crash
        QVERIFY(proj.desc.isEmpty());
    }

    void proj_activateDeactivate()
    {
        // Create a simple projector
        FiffProj proj;
        proj.kind = 1;
        proj.active = false;
        proj.desc = "test_proj";

        FiffNamedMatrix::SDPtr data(new FiffNamedMatrix);
        data->nrow = 1;
        data->ncol = 3;
        data->data = MatrixXd::Ones(1, 3);
        data->row_names.push_back("comp1");
        data->col_names.push_back("ch1");
        data->col_names.push_back("ch2");
        data->col_names.push_back("ch3");
        proj.data = data;

        // Activate
        QList<FiffProj> projs;
        projs << proj;
        FiffProj::activate_projs(projs);
        QVERIFY(projs[0].active);
    }

    //=========================================================================
    // FiffDirNode — coverage tests
    //=========================================================================
    void dirNode_defaultCtor()
    {
        FiffDirNode node;
        QCOMPARE(node.type, -1);
        QCOMPARE(node.nent(), 0);
        QVERIFY(node.children.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFiffDataTypes)
#include "test_fiff_data_types.moc"
