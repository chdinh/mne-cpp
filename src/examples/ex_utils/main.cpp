//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     main.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.4.0
 * @date     October 2026
 * @brief    Utilities library: observers, commands, matrix files, montages, layouts, selections and reports.
 *
 * Every step checks its result (round trips, known electrode positions) and the
 * example exits non-zero on a mismatch.
 * All files are written to a temporary directory.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <utils/generics/commandpattern.h>
#include <utils/generics/observerpattern.h>
#include <utils/ioutils.h>
#include <utils/layoutloader.h>
#include <utils/layoutmaker.h>
#include <utils/montage/standard_montage.h>
#include <utils/python_runner.h>
#include <utils/python_test_helper.h>
#include <utils/report.h>
#include <utils/selectionio.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace UTILSLIB;
using namespace Eigen;

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

namespace
{

bool expect(bool condition, std::string_view what)
{
    std::println("{} {}", condition ? "  ok  " : "  FAIL", what);
    return condition;
}

//=============================================================================================================

//! [observer_pattern_usage]
// A subject notifies every attached observer; observers pull the state they need
class Counter : public Subject
{
public:
    void increment()
    {
        ++m_value;
        notify();
    }
    int value() const
    {
        return m_value;
    }

private:
    int m_value = 0;
};

class LastSeen : public IObserver
{
public:
    void update(Subject* pSubject) override
    {
        m_seen = static_cast<Counter*>(pSubject)->value();
    }
    int seen() const
    {
        return m_seen;
    }

private:
    int m_seen = -1;
};
//! [observer_pattern_usage]

//! [command_pattern_usage]
// A command wraps an action so it can be queued or replayed later
class AppendCommand : public ICommand
{
public:
    AppendCommand(QStringList& target, QString text)
    : m_target(target)
    , m_text(std::move(text))
    {
    }
    void execute() override
    {
        m_target.append(m_text);
    }

private:
    QStringList& m_target;
    QString m_text;
};
//! [command_pattern_usage]

} // namespace

//=============================================================================================================
// MAIN
//=============================================================================================================

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    bool ok = true;

    Counter counter;
    LastSeen observer;
    counter.attach(&observer);
    counter.increment();
    counter.increment();
    counter.detach(&observer);
    counter.increment();
    ok &= expect(observer.seen() == 2, std::format("observer saw {} before detaching (2)", observer.seen()));

    QStringList journal;
    QList<AppendCommand*> queue = {new AppendCommand(journal, "a"), new AppendCommand(journal, "b")};
    for (ICommand* command : queue) {
        command->execute();
    }
    qDeleteAll(queue);
    ok &= expect(journal == QStringList({"a", "b"}), "commands executed in order");

    //! [ioutils_usage]
    MatrixXd matrix(2, 3);
    matrix << 1.5, -2.0, 3.25, 0.0, 1e-6, 42.0;
    const QString matrixPath = dir.filePath("matrix.txt");
    IOUtils::write_eigen_matrix(matrix, matrixPath, "example matrix");
    MatrixXd readBack;
    IOUtils::read_eigen_matrix(readBack, matrixPath);
    //! [ioutils_usage]
    ok &= expect(readBack.rows() == 2 && readBack.cols() == 3 && (readBack - matrix).cwiseAbs().maxCoeff() < 1e-12,
                 "IOUtils text matrix round trip");

    //! [selection_io_usage]
    QMultiMap<QString, QStringList> selections;
    selections.insert("Left-temporal", {"MEG0111", "MEG0112", "MEG0113"});
    selections.insert("Vertex", {"MEG0711"});
    const QString selPath = dir.filePath("groups.sel");
    SelectionIO::writeMNESelFile(selPath, selections);
    QMultiMap<QString, QStringList> reread;
    SelectionIO::readMNESelFile(selPath, reread);
    //! [selection_io_usage]
    ok &= expect(reread.size() == 2 && reread.value("Left-temporal") == QStringList({"MEG0111", "MEG0112", "MEG0113"}),
                 "SelectionIO .sel round trip");

    //! [standard_montage_usage]
    const QList<ElectrodePosition> montage1020 = StandardMontage::getMontage(StandardMontage::System::Standard_1020);
    Vector3d cz;
    const bool found = StandardMontage::findElectrode("Cz", cz); // mne's standard_1005 positions, metres, MNI frame
    //! [standard_montage_usage]
    ok &= expect(montage1020.size() == StandardMontage::electrodeCount(StandardMontage::System::Standard_1020) && found && (cz - Vector3d(0.0004009, -0.009167, 0.100244)).norm() < 1e-12,
                 std::format("10-20 montage: {} electrodes, Cz at (0.4, -9.2, 100.2) mm", montage1020.size()));

    //! [layout_maker_usage]
    // Azimuthal equidistant projection around the fitted sphere: Cz lands near the centre, the nose points up
    QList<QVector<float>> points3d;
    QStringList names;
    for (const ElectrodePosition& electrode : montage1020) {
        points3d.append({static_cast<float>(electrode.pos.x()), static_cast<float>(electrode.pos.y()), static_cast<float>(electrode.pos.z())});
        names.append(electrode.name);
    }
    QList<QVector<float>> layout2d;
    QFile layoutFile(dir.filePath("1020.lout"));
    LayoutMaker::makeLayout(points3d, layout2d, names, layoutFile, true, 20.0f, 5.0f, 4.0f, true);
    //! [layout_maker_usage]
    const int iCz = names.indexOf("Cz");
    const int iFp1 = names.indexOf("Fp1");
    const int iO1 = names.indexOf("O1");
    const int iT8 = names.indexOf("T8");
    // Box corners: the box of an electrode at (x, y) starts at (x - w/2, y - h/2)
    const float czX = layout2d[iCz][0] + 2.5f;
    const float czY = layout2d[iCz][1] + 2.0f;
    ok &= expect(layout2d.size() == names.size() && std::hypot(czX, czY) < 1.0f && layout2d[iFp1][1] > layout2d[iCz][1] && layout2d[iO1][1] < layout2d[iCz][1] && layout2d[iFp1][0] < layout2d[iCz][0] && layout2d[iT8][0] > layout2d[iCz][0],
                 std::format("LayoutMaker: Cz at ({:g}, {:g}), Fp1 front-left, O1 back, T8 right", czX, czY));

    QFile capFile(dir.filePath("cap.elc"));
    ok &= capFile.open(QIODevice::WriteOnly | QIODevice::Text);
    capFile.write("NumberPositions=\t2\nUnitPosition\tmm\nPositions\nFp1 :\t-29.4\t83.9\t-7.0\nCz :\t0.0\t0.0\t87.0\n"
                  "Labels\nFp1\tCz\n");
    capFile.close();
    //! [layout_loader_usage]
    // ANT .elc electrode file: names, 3-D positions and their unit
    QStringList capNames;
    QList<QVector<float>> cap3D;
    QList<QVector<float>> cap2D;
    QString capUnit;
    LayoutLoader::readAsaElcFile(capFile.fileName(), capNames, cap3D, cap2D, capUnit);
    //! [layout_loader_usage]
    ok &= expect(capNames == QStringList({"Fp1", "Cz"}) && capUnit == "mm" && cap3D.size() == 2 && cap3D[1] == QVector<float>({0.0f, 0.0f, 87.0f}),
                 "LayoutLoader reads the electrode names, positions and unit of an .elc file");

    //! [report_usage]
    Report report("Example report");
    report.addText("Summary", "All utilities were exercised.");
    report.addTable("Montage", {"Electrode", "z (m)"}, {{"Cz", "0.087"}, {"Fp1", "0.033"}});
    const QString html = report.toHtml();
    report.save(dir.filePath("report.html"));
    //! [report_usage]
    ok &= expect(report.sectionCount() == 2 && html.contains("<table") && html.contains("Fp1") && QFile::exists(dir.filePath("report.html")),
                 "Report renders text and table sections to HTML");

    //! [python_runner_usage]
    PythonRunner python; // "python3" from PATH; a venv is configured via PythonRunnerConfig
    QStringList progress;
    python.setProgressCallback([&](float pct, const QString& msg) { progress << QString("%1 %2").arg(pct).arg(msg); });
    const PythonRunnerResult run = python.runCode("import sys\nprint('[progress] 50% half')\nprint(sum(map(int, sys.argv[1:])))", {"2", "3"});
    //! [python_runner_usage]
    //! [python_test_helper_usage]
    PythonTestHelper helper;                                                    // oracle access for tests
    const MatrixXd fromNumpy = helper.evalMatrix("print('1 2'); print('3 4')"); // one line per row
    //! [python_test_helper_usage]
    if (python.isPythonAvailable()) {
        ok &= expect(run.success && run.stdOut.trimmed().endsWith("5") && progress == QStringList({"50 half"}) && fromNumpy == (Matrix2d() << 1, 2, 3, 4).finished(),
                     "PythonRunner passes arguments and parses [progress] lines; PythonTestHelper reads a matrix");
    } else {
        std::println("  skip  PythonRunner (python3 not found)");
    }

    std::println("{}", ok ? "All utils checks passed." : "utils checks FAILED.");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
