//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     acq_view.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    mne_acq view: live traces of a source in a ChannelDataView.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "acq_view.h"

#include <acq/acq_eigen.h>
#include <acq/fiff/acq_fiff.h>

#include <disp/viewers/channeldataview.h>
#include <disp/viewers/helpers/channeldatamodel.h>

#include <algorithm>
#include <chrono>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace ACQLIB;
using namespace DISPLIB;
using namespace std::chrono_literals;

//=============================================================================================================
// DEFINE GLOBAL METHODS
//=============================================================================================================

int runAcqView(AcqDevice& device, double window, double duration, const QString& screenshot)
{
    QTextStream err(stderr);
    auto reader = device.stream().reader(AcqReader::ReadMode::Lossless);
    if (!reader) {
        err << "mne_acq: " << reader.error().text().c_str() << "\n";
        return 2;
    }
    const auto streamInfo = reader->stream().info();
    auto fiffInfo = toFiffInfo(*streamInfo);
    if (!fiffInfo) {
        err << "mne_acq: " << fiffInfo.error().text().c_str() << "\n";
        return 2;
    }
    const auto channels = static_cast<Eigen::Index>(streamInfo->channels.size());
    const double sfreq = streamInfo->sfreq;
    const auto windowSamples = static_cast<int>(window * sfreq);

    ChannelDataView view(QStringLiteral("MNEACQ/view"));
    view.setWindowTitle(QStringLiteral("mne_acq view %1").arg(QString::fromStdString(device.description().uri)));
    view.resize(1200, 700);
    view.init(QSharedPointer<FIFFLIB::FiffInfo>::create(*fiffInfo));
    view.model()->setMaxStoredSamples(4 * windowSamples);
    view.setWindowSize(static_cast<float>(window));
    view.show();

    if (const AcqError started = device.start(); !started.ok()) {
        err << "mne_acq: " << started.text().c_str() << "\n";
        return 2;
    }

    QVector<ChannelRhiView::EventMarker> events;
    Eigen::MatrixXd block;
    std::uint64_t next = 0;
    bool ended = false;
    bool shotSaved = true;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        bool added = false;
        for (;;) {
            auto acquired = reader->acquire(0, 0ms);
            if (!acquired) {
                const AcqErrorCode code = acquired.error().code();
                ended = code == AcqErrorCode::EndOfStream || code == AcqErrorCode::DeviceStopped;
                break;
            }
            AcqBlockView& acquiredView = *acquired;
            if (acquiredView.sampleCount() == 0) {
                static_cast<void>(reader->release(acquiredView));
                break;
            }
            block.resize(channels, static_cast<Eigen::Index>(acquiredView.sampleCount()));
            const AcqError decoded = decode(acquiredView, ref(block));
            for (const auto* part : {&acquiredView.events().first, &acquiredView.events().second}) {
                for (const AcqEvent& event : *part) {
                    events.append({static_cast<int>(event.sample), event.code, QColor(), QString::number(event.code)});
                }
            }
            next = acquiredView.firstSample() + acquiredView.sampleCount();
            if (reader->release(acquiredView).ok() && decoded.ok()) {
                view.addData(block);
                added = true;
            }
        }
        if (added) {
            const int first = view.model()->firstSample();
            events.erase(std::remove_if(events.begin(), events.end(),
                                        [first](const ChannelRhiView::EventMarker& e) { return e.sample < first; }),
                         events.end());
            view.setFileBounds(first, static_cast<int>(next) - 1);
            view.setEvents(events);
            view.scrollToSample(qMax(0, static_cast<int>(next) - windowSamples), false);
        }
        const bool timeUp = duration > 0.0 && elapsed.elapsed() >= static_cast<qint64>(duration * 1000.0);
        if (ended || timeUp || !view.isVisible()) {
            timer.stop();
            shotSaved = screenshot.isEmpty() || view.grab().save(screenshot);
            QCoreApplication::quit();
        }
    });
    timer.start(33);
    QApplication::exec();
    device.close();

    if (!shotSaved) {
        err << "mne_acq: cannot write " << screenshot << "\n";
        return 2;
    }
    QTextStream(stdout) << "Showed " << next << " samples\n";
    return 0;
}
