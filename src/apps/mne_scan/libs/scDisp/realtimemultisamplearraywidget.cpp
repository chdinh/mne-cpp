//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2012-2026 MNE-CPP Authors
 *
 * @file     realtimemultisamplearraywidget.cpp
 * @author   Gabriel Motta <gabrielbenmotta@gmail.com>;
 *           Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Lars Debor <Lars.Debor@tu-ilmenau.de>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>
 * @since    0.1.0
 * @date     July, 2012
 * @brief    Definition of the RealTimeMultiSampleArrayWidget Class.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <mne/base/log.h>
#include "realtimemultisamplearraywidget.h"

#include <mne/viz/backends/qt/kit/panelwidget.h>
#include <mne/viz/panels/channel_selection_panel.h>
#include <mne/viz/panels/raw_view_panel.h>
#include <mne/viz/panels/scaling_panel.h>
#include <mne/viz/backends/qt/qtconversions.h>
#include <mne/viz/backends/qt/viewers/helpers/channelinfomodel.h>
#include <mne/viz/backends/qt/viewers/rtfiffrawview.h>
#include <mne/viz/panels/trigger_detection_panel.h>
#include <scMeas/realtimemultisamplearray.h>
#include <mne/dsp/filterkernel.h>
#include <mne/fiff/fiff_info.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QAction>
#include <QColor>
#include <QDate>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QDir>
#include <QSettings>
#include <QToolBar>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <map>
#include <memory>
#include <string>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace mne::scan;
using namespace mne::viz;
using namespace mne::viz::qt;

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

RealTimeMultiSampleArrayWidget::RealTimeMultiSampleArrayWidget(QSharedPointer<QTime>& pTime,
                                                               QWidget* parent)
: MeasurementWidget(parent)
, m_iMaxFilterTapSize(-1)
{
    Q_UNUSED(pTime)

    qRegisterMetaType<QMap<int, QList<QPair<int, double>>>>();
}

//=============================================================================================================

RealTimeMultiSampleArrayWidget::~RealTimeMultiSampleArrayWidget()
{
    QSettings settings("MNECPP");

    if (m_pChannelDataView && m_pRTMSA) {
        settings.setValue(QString("RTMSAW/showHideBad"), m_pChannelDataView->getBadChannelHideStatus());
    }
}

//=============================================================================================================

void RealTimeMultiSampleArrayWidget::update(mne::scan::Measurement::SPtr pMeasurement)
{
    if (!m_pRTMSA) {
        m_pRTMSA = qSharedPointerDynamicCast<RealTimeMultiSampleArray>(pMeasurement);
    }

    if (m_pRTMSA) {
        if (m_pRTMSA->isChInit() && !m_pFiffInfo) {
            m_pFiffInfo = m_pRTMSA->info();
            m_iMaxFilterTapSize = m_pRTMSA->getMultiSampleArray().first().cols();

            if (!m_bDisplayWidgetsInitialized) {
                initDisplayControllWidgets();
            }
        }
        if (!m_pRTMSA->getMultiSampleArray().isEmpty()) {
            //Add data to table view
            m_pChannelDataView->addData(m_pRTMSA->getMultiSampleArray());
        }
    }
}

//=============================================================================================================

void RealTimeMultiSampleArrayWidget::initDisplayControllWidgets()
{
    if (m_pFiffInfo) {
        //Create table view and set layout
        m_pChannelDataView = new RtFiffRawView(QString("MNESCAN/RTMSAW"), this);
        m_pChannelDataView->hide();

        QVBoxLayout* rtmsaLayout = new QVBoxLayout(this);
        rtmsaLayout->setContentsMargins(0, 0, 0, 0);
        this->setLayout(rtmsaLayout);
        this->setMinimumSize(300, 50);

        // Prepare actions
        QToolBar* pToolBar = new QToolBar(this);

        QAction* pActionSelectSensors = new QAction(QIcon(":/images/selectSensors.png"), tr("Show the channel selection view"), this);
        pActionSelectSensors->setToolTip(tr("Show the channel selection view"));
        connect(pActionSelectSensors, &QAction::triggered,
                this, &RealTimeMultiSampleArrayWidget::showSensorSelectionWidget);
        pActionSelectSensors->setVisible(true);
        pToolBar->addAction(pActionSelectSensors);

        m_pActionHideBad = new QAction(QIcon(":/images/hideBad.png"), tr("Toggle bad channel visibility"), this);
        m_pActionHideBad->setStatusTip(tr("Toggle bad channel visibility"));
        connect(m_pActionHideBad.data(), &QAction::triggered,
                this, &RealTimeMultiSampleArrayWidget::onHideBadChannels);
        m_pActionHideBad->setVisible(true);
        pToolBar->addAction(m_pActionHideBad);

        // Add toolbar and channel data view
        rtmsaLayout->addWidget(pToolBar);
        rtmsaLayout->addWidget(m_pChannelDataView);

        // Init channel view
        QSettings settings("MNECPP");
        QString sRTMSAWName = m_pRTMSA->getName();

        m_pChannelDataView->show();
        m_pChannelDataView->init(m_pFiffInfo);

        if (settings.value(QString("RTMSAW/showHideBad"), false).toBool()) {
            this->onHideBadChannels();
        }

        //Init channel selection manager
        m_pChannelInfoModel = std::make_shared<ChannelInfoModel>(m_pFiffInfo,
                                                                 this);

        // The panel reports to the views and the channel info model before its window restores the stored files
        m_pChannelSelection = std::make_shared<viz::ChannelSelectionPanel>();
        m_pChannelSelection->set_channel_table(ChannelInfoModel::sharedTable(m_pChannelInfoModel));
        viz::ChannelSelectionPanel::Callbacks callbacks;
        callbacks.layout_loaded = [this](const std::map<std::string, viz::Point>& layout) {
            m_pChannelInfoModel->setLayout(layout);
        };
        callbacks.channels_shown = [this](const std::vector<std::string>& channels) {
            m_pChannelDataView->showSelectedChannelsOnly(channels);
        };
        m_pChannelSelection->set_callbacks(std::move(callbacks));
        m_pChannelSelectionView = std::make_shared<PanelWidget>(m_pChannelSelection, QString("MNESCAN/RTMSAW"), this);
        m_pChannelSelectionView->setWindowFlag(Qt::Window);
        m_pChannelSelectionView->setWindowTitle(tr(QString("%1: Channel Selection Window").arg(sRTMSAWName).toUtf8()));

        connect(m_pChannelInfoModel.get(), &ChannelInfoModel::channelsMappedToLayout,
                this, [this](const std::vector<std::string>& names) {
                    m_pChannelSelection->set_mapped_channels(names);
                });

        connect(m_pChannelDataView.data(), &RtFiffRawView::channelMarkingChanged,
                this, [this]() {
                    m_pChannelSelection->update_bad_channels();
                });

        m_pChannelInfoModel->setLayout(m_pChannelSelection->layout_map());

        //Init control widgets
        QList<QWidget*> lControlWidgets;


        // The control widgets may outlive this widget in the quick control view
        const QPointer<RealTimeMultiSampleArrayWidget> self(this);

        // Quick control scaling
        auto pScaling = std::make_shared<viz::ScalingPanel>(m_pFiffInfo->get_channel_types());
        pScaling->set_callback([self](const std::map<int, float>& scaleMap) {
            if (self) {
                self->m_pChannelDataView->setScalingMap(scaleMap);
            }
        });
        auto* pScalingView = new PanelWidget(pScaling, QString("MNESCAN/RTMSAW"), this);
        pScalingView->setObjectName("group_tab_View_Scaling");
        lControlWidgets.append(pScalingView);

        m_pChannelDataView->setScalingMap(pScaling->scale_map());

        // Quick control channel data settings
        auto pChannelDataSettings = std::make_shared<viz::RawViewPanel>();
        viz::RawViewPanel::Callbacks viewCallbacks;
        viewCallbacks.signal_color_changed = [self](const viz::Color& color) {
            if (self) {
                self->m_pChannelDataView->setSignalColor(toQColor(color));
            }
        };
        viewCallbacks.background_color_changed = [self](const viz::Color& color) {
            if (self) {
                self->m_pChannelDataView->setBackgroundColor(toQColor(color));
            }
        };
        viewCallbacks.zoom_changed = [self](double zoom) {
            if (self) {
                self->m_pChannelDataView->setZoom(zoom);
            }
        };
        viewCallbacks.window_size_changed = [self](int seconds) {
            if (self) {
                self->m_pChannelDataView->setWindowSize(seconds);
            }
        };
        viewCallbacks.time_spacer_changed = [self](int milliseconds) {
            if (self) {
                self->m_pChannelDataView->setDistanceTimeSpacer(milliseconds);
            }
        };
        viewCallbacks.screenshot_wanted = [self](const std::string& imageType) {
            if (self) {
                self->onMakeScreenshot(QString::fromStdString(imageType));
            }
        };
        pChannelDataSettings->set_callbacks(std::move(viewCallbacks));
        auto* pChannelDataSettingsView = new PanelWidget(pChannelDataSettings, QString("MNESCAN/RTMSAW"), this);
        pChannelDataSettingsView->setObjectName("group_tab_View_General");
        lControlWidgets.append(pChannelDataSettingsView);

        m_pChannelDataView->setZoom(pChannelDataSettings->zoom());
        m_pChannelDataView->setWindowSize(pChannelDataSettings->window_size());
        m_pChannelDataView->setDistanceTimeSpacer(pChannelDataSettings->time_spacer());
        m_pChannelDataView->setBackgroundColor(toQColor(pChannelDataSettings->background_color()));
        m_pChannelDataView->setSignalColor(toQColor(pChannelDataSettings->signal_color()));

        // Quick control trigger detection settings
        auto triggers = std::make_shared<TriggerDetectionPanel>();
        TriggerDetectionPanel::Callbacks triggerCallbacks;
        triggerCallbacks.trigger_info_changed = [view = m_pChannelDataView](const std::map<double, std::string>& colors, bool active,
                                                                            const std::string& channel, double threshold) {
            if (view) {
                std::map<double, QColor> qColors;
                for (const auto& [type, color] : colors) {
                    qColors[type] = QColor(QString::fromStdString(color.substr(0, 7)));
                }
                view->triggerInfoChanged(qColors, active, channel, threshold);
            }
        };
        triggerCallbacks.reset_trigger_counter = [view = m_pChannelDataView] {
            if (view) {
                view->resetTriggerCounter();
            }
        };
        triggers->set_callbacks(std::move(triggerCallbacks));
        auto* pTriggerDetectionView = new PanelWidget(triggers, QString("MNESCAN/RTMSAW"), this);
        pTriggerDetectionView->setObjectName("group_tab_View_Triggers");
        lControlWidgets.append(pTriggerDetectionView);

        connect(m_pChannelDataView.data(), &RtFiffRawView::triggerDetected,
                pTriggerDetectionView, [triggers](int total, const std::map<int, std::vector<std::pair<int, double>>>& detected) {
                    triggers->set_detected_triggers(total, detected);
                });

        if (m_pFiffInfo) {
            triggers->init(*m_pFiffInfo);
        }

        emit displayControlWidgetsChanged(lControlWidgets, sRTMSAWName);

        //Initialized
        m_bDisplayWidgetsInitialized = true;
    }
}

//=============================================================================================================

void RealTimeMultiSampleArrayWidget::showSensorSelectionWidget()
{
    if (m_pChannelSelectionView->isActiveWindow()) {
        m_pChannelSelectionView->hide();
    } else {
        m_pChannelSelectionView->activateWindow();
        m_pChannelSelectionView->show();
    }
}

//=============================================================================================================

void RealTimeMultiSampleArrayWidget::onMakeScreenshot(const QString& imageType)
{
    // Create file name
    QString sDate = QDate::currentDate().toString("yyyy_MM_dd");
    QString sTime = QTime::currentTime().toString("hh_mm_ss");

    if (!QDir("./Screenshots").exists()) {
        QDir().mkdir("./Screenshots");
    }

    //
    // Only PNG and SVG can be written; any other value would leave the file
    // name empty and be passed on to the view regardless.
    //
    QString sSuffix;
    if (imageType.contains("SVG")) {
        sSuffix = "svg";
    } else if (imageType.contains("PNG")) {
        sSuffix = "png";
    } else {
        mne::Log::warning("[RealTimeMultiSampleArrayWidget::onMakeScreenshot] Unsupported image type {} - no screenshot was taken.", imageType.toStdString());
        return;
    }

    if (m_pChannelDataView.isNull()) {
        mne::Log::warning("[RealTimeMultiSampleArrayWidget::onMakeScreenshot] No data view - no screenshot was taken.");
        return;
    }

    m_pChannelDataView->takeScreenshot(QString("./Screenshots/%1-%2-DataView.%3")
                                           .arg(sDate, sTime, sSuffix));
}

//=============================================================================================================

void RealTimeMultiSampleArrayWidget::onHideBadChannels()
{
    m_pChannelDataView->hideBadChannels();

    if (m_pActionHideBad->toolTip() == "Show all bad channels") {
        m_pActionHideBad->setIcon(QIcon(":/images/hideBad.png"));
        m_pActionHideBad->setToolTip("Hide all bad channels");
        m_pActionHideBad->setStatusTip(tr("Hide all bad channels"));
    } else {
        m_pActionHideBad->setIcon(QIcon(":/images/showBad.png"));
        m_pActionHideBad->setToolTip("Show all bad channels");
        m_pActionHideBad->setStatusTip(tr("Show all bad channels"));
    }
}

//=============================================================================================================

void RealTimeMultiSampleArrayWidget::updateViewport()
{
    if (m_pChannelDataView) {
        m_pChannelDataView->updateViewport();
    }
}
