//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     log.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Library logging facade with a replaceable sink and a capture sink for tests.
 *
 * Libraries report through @ref UTILSLIB::Log instead of @c qWarning and friends, so the
 * interface is standard C++ only. By default every message is forwarded to Qt's message
 * handler, where applications format it (for example with @ref UTILSLIB::MNELogger).
 * @ref UTILSLIB::LogCapture swallows and records messages for the lifetime of a scope.
 */

#ifndef UTILS_LOG_H
#define UTILS_LOG_H

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "../utils_global.h"

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================================================
// DEFINE NAMESPACE UTILSLIB
//=============================================================================================================

namespace UTILSLIB
{

//=============================================================================================================
/**
 * @brief Writes library messages to the current sink; thread-safe.
 *
 * @snippet ex_utils/main.cpp log_usage
 */
class UTILSSHARED_EXPORT Log
{
public:
    /** Severity, matching Qt's debug, info, warning and critical messages. */
    enum class Level
    {
        Debug,
        Info,
        Warning,
        Critical
    };

    /** Receives every message; may be called from any thread. */
    using Sink = std::function<void(Level level, std::string_view message)>;

    Log() = delete;

    //=========================================================================================================
    /**
     * Hands @p message to the current sink.
     *
     * @param[in] level     Severity.
     * @param[in] message   UTF-8 text.
     */
    static void write(Level level, std::string_view message);

    static void debug(std::string_view message)
    {
        write(Level::Debug, message);
    }

    static void info(std::string_view message)
    {
        write(Level::Info, message);
    }

    static void warning(std::string_view message)
    {
        write(Level::Warning, message);
    }

    static void critical(std::string_view message)
    {
        write(Level::Critical, message);
    }

    //=========================================================================================================
    /**
     * Replaces the sink.
     *
     * @param[in] sink   The new sink; an empty one restores forwarding to Qt's message handler.
     *
     * @return The sink that was installed before, empty if messages were forwarded to Qt.
     */
    static Sink setSink(Sink sink);
};

//=============================================================================================================
/**
 * @brief Records every message instead of passing it on, while it exists; replaces @c QTest::ignoreMessage.
 *
 * The sink installed before is restored on destruction, so captures nest when destroyed in reverse order.
 *
 * @snippet ex_utils/main.cpp log_capture_usage
 */
class UTILSSHARED_EXPORT LogCapture
{
public:
    /** One recorded message. */
    struct Entry
    {
        Log::Level level = Log::Level::Debug;
        std::string message;
    };

    LogCapture();
    ~LogCapture();

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    /** @return The messages recorded so far, oldest first. */
    std::vector<Entry> entries() const;

    //=========================================================================================================
    /**
     * @param[in] level      Severity to count.
     * @param[in] contains   Only messages containing this text; empty counts all.
     *
     * @return The number of recorded messages of @p level that contain @p contains.
     */
    int count(Log::Level level, std::string_view contains = {}) const;

    /** Forgets the messages recorded so far. */
    void clear();

private:
    struct State;
    std::shared_ptr<State> m_state;
    Log::Sink m_previous;
};

} // namespace UTILSLIB

#endif // UTILS_LOG_H
