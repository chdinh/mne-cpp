//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     log.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Definition of @ref UTILSLIB::Log and @ref UTILSLIB::LogCapture.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "log.h"

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QDebug>
#include <QUtf8StringView>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <algorithm>
#include <mutex>
#include <utility>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace UTILSLIB;

//=============================================================================================================
// DEFINE STATICS AND LOCAL METHODS
//=============================================================================================================

namespace
{

struct SinkSlot
{
    std::mutex mutex;
    std::shared_ptr<const Log::Sink> sink;
};

SinkSlot& sinkSlot()
{
    static SinkSlot slot;
    return slot;
}

void forwardToQt(Log::Level level, std::string_view message)
{
    const QUtf8StringView text(message);
    switch (level) {
        case Log::Level::Debug:
            qDebug().noquote() << text;
            break;
        case Log::Level::Info:
            qInfo().noquote() << text;
            break;
        case Log::Level::Warning:
            qWarning().noquote() << text;
            break;
        case Log::Level::Critical:
            qCritical().noquote() << text;
            break;
    }
}

} // namespace

//=============================================================================================================

struct LogCapture::State
{
    mutable std::mutex mutex;
    std::vector<Entry> entries;
};

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

void Log::write(Level level, std::string_view message)
{
    std::shared_ptr<const Sink> sink;
    {
        std::lock_guard<std::mutex> lock(sinkSlot().mutex);
        sink = sinkSlot().sink;
    }
    // Called outside the lock, so a sink may itself change the sink.
    if (sink) {
        (*sink)(level, message);
    } else {
        forwardToQt(level, message);
    }
}

//=============================================================================================================

Log::Sink Log::setSink(Sink sink)
{
    std::shared_ptr<const Sink> next = sink ? std::make_shared<const Sink>(std::move(sink)) : nullptr;
    std::lock_guard<std::mutex> lock(sinkSlot().mutex);
    std::swap(sinkSlot().sink, next);
    return next ? *next : Sink();
}

//=============================================================================================================

LogCapture::LogCapture()
: m_state(std::make_shared<State>())
{
    m_previous = Log::setSink([state = m_state](Log::Level level, std::string_view message) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->entries.push_back({level, std::string(message)});
    });
}

//=============================================================================================================

LogCapture::~LogCapture()
{
    Log::setSink(std::move(m_previous));
}

//=============================================================================================================

std::vector<LogCapture::Entry> LogCapture::entries() const
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->entries;
}

//=============================================================================================================

int LogCapture::count(Log::Level level, std::string_view contains) const
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return static_cast<int>(std::count_if(m_state->entries.begin(), m_state->entries.end(), [&](const Entry& entry) {
        return entry.level == level && entry.message.find(contains) != std::string::npos;
    }));
}

//=============================================================================================================

void LogCapture::clear()
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->entries.clear();
}
