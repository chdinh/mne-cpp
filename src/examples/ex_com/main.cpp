//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     main.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.5.0
 * @date     November 2026
 * @brief    Communication library: the observer and command patterns behind the real-time command dispatch.
 *
 * Runs without an mne_rt_server. Every step checks its result and the example exits non-zero on a mismatch.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include <com/rt_command/command_pattern.h>
#include <com/rt_command/observer_pattern.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QCoreApplication>

//=============================================================================================================
// STL INCLUDES
//=============================================================================================================

#include <cstdlib>
#include <format>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace COMLIB;

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
    void update(Subject* subject) override
    {
        m_seen = static_cast<Counter*>(subject)->value();
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
    AppendCommand(std::vector<std::string>& target, std::string text)
    : m_target(target)
    , m_text(std::move(text))
    {
    }
    void execute() override
    {
        m_target.push_back(m_text);
    }

private:
    std::vector<std::string>& m_target;
    std::string m_text;
};
//! [command_pattern_usage]

} // namespace

//=============================================================================================================
// MAIN
//=============================================================================================================

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    Counter counter;
    LastSeen observer;
    counter.attach(&observer);
    counter.increment();
    counter.increment();
    counter.detach(&observer);
    counter.increment();
    ok &= expect(observer.seen() == 2, std::format("observer saw {} before detaching (2)", observer.seen()));

    std::vector<std::string> journal;
    std::vector<std::unique_ptr<ICommand>> queue;
    queue.push_back(std::make_unique<AppendCommand>(journal, "a"));
    queue.push_back(std::make_unique<AppendCommand>(journal, "b"));
    for (const auto& command : queue) {
        command->execute();
    }
    ok &= expect(journal == std::vector<std::string>{"a", "b"}, "commands executed in order");

    std::println("{}", ok ? "All com checks passed." : "com checks FAILED.");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
