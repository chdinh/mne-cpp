//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     main.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Polhemus digitiser: parse Fastrak records, register captured fiducials and collect digitised points.
 *
 * Parses a hand-written Fastrak record, registers three fiducials captured from the mock digitiser (no hardware
 * needed) onto known model positions, and keeps a list of digitised points. Every step checks its result and the
 * example exits non-zero on a mismatch.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <acq/polhemus/acquired_points.h>
#include <acq/polhemus/fastrak_parser.h>
#include <acq/polhemus/polhemus_connection.h>
#include <acq/polhemus/polhemus_coregistration.h>

#include <cmath>
#include <cstdlib>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QMatrix4x4>
#include <QTimer>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;

namespace
{

bool expect(bool condition, const QString& what)
{
    qInfo().noquote() << (condition ? "  ok  " : "  FAIL") << what;
    return condition;
}

} // namespace

//=============================================================================================================
// MAIN
//=============================================================================================================

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    //! [fastrak_parser_usage]
    FastrakParser parser;
    parser.setUnits(FastrakParser::Units::Centimetres);
    parser.append("01   12.50  -3.25   8.00\r\n02  1.0"); // the second record is still incomplete
    FastrakSample sample;
    const bool complete = parser.nextSample(sample);
    const bool pending = parser.nextSample(sample);
    //! [fastrak_parser_usage]
    ok &= expect(complete && !pending && sample.station == 1 && std::fabs(sample.position.x() - 0.125f) < 1e-6f && std::fabs(sample.position.y() + 0.0325f) < 1e-6f,
                 "FastrakParser converts centimetres to metres and waits for the partial record");

    //! [polhemus_coregistration_usage]
    PolhemusConnection connection;
    PolhemusSerialConfig serial;        // 115200 baud, inches; the hemisphere and stream command a Fastrak needs
    connection.open(QString(), serial); // empty port name = mock backend sweeping a 10 cm sphere; e.g. "/dev/tty.usbserial" for a Fastrak
    PolhemusCoregistration coreg;
    coreg.setConnection(&connection); // station 1 = pen, station 2 = head tracker

    // Model fiducials are the pen fiducials turned 90 deg about z and moved 5 cm in x; the registration must find that transform
    QMatrix4x4 truth;
    truth.translate(0.05f, 0.0f, 0.0f);
    truth.rotate(90.0f, 0.0f, 0.0f, 1.0f);
    const QList<FiducialId> fiducials = {FiducialId::NAS, FiducialId::LPA, FiducialId::RPA};
    QList<QVector3D> penAt;
    for (const FiducialId id : fiducials) {
        QEventLoop wait; // let the next mock pen sample arrive, as the user would move the pen
        QTimer::singleShot(350, &wait, &QEventLoop::quit);
        wait.exec();
        coreg.captureCurrentPenPositionAsFiducial(id);
        penAt.append(coreg.penPosition());
        coreg.setModelFiducial(id, truth.map(coreg.penPosition()));
    }
    const bool registered = coreg.computeRegistration();
    connection.close();
    //! [polhemus_coregistration_usage]
    const QVector3D mapped = coreg.worldToModel().map(penAt[1]);
    ok &= expect(registered && coreg.acquiredPoints()->hasAllFiducials() && coreg.acquiredPoints()->countOf(PointKind::Fiducial) == 3 && (mapped - truth.map(penAt[1])).length() < 1e-5f,
                 "PolhemusCoregistration maps the captured LPA onto its model position from the mock digitiser");

    //! [acquired_points_usage]
    AcquiredPoints points;
    points.append({PointKind::HeadShape, "HSP-1", 1, QVector3D(0.0f, 0.08f, 0.05f)});
    points.append({PointKind::Eeg, "Cz", 1, QVector3D(0.0f, 0.0f, 0.09f)});
    points.undoLast(PointKind::HeadShape);
    const DigitizedPoint& kept = points.points().first(); // kind, label, FIFF ident, position (m)
    //! [acquired_points_usage]
    ok &= expect(points.points().size() == 1 && kept.label == "Cz" && kept.position.z() == 0.09f && points.countOf(PointKind::Eeg) == 1 && !points.hasAllFiducials(), "AcquiredPoints keeps Cz after undoing the head-shape point");

    qInfo() << (ok ? "All Polhemus checks passed." : "Polhemus checks FAILED.");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
