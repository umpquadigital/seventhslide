#include <catch2/catch_test_macros.hpp>

#include "gui_thread.hpp"
#include "test_qt_app.hpp"

#include <QCoreApplication>
#include <QObject>
#include <QThread>

#include <atomic>
#include <chrono>
#include <thread>

using seventhslide::app::call_on_gui_thread;
using seventhslide::app::g_gui_calls_closed;

TEST_CASE("call_on_gui_thread runs inline on the GUI thread", "[app][threads]")
{
    seventhslide::test::ensure_qt_application();
    QObject context;
    const auto* gui = QThread::currentThread();
    const auto ran_on = call_on_gui_thread(&context, [] { return QThread::currentThread(); });
    REQUIRE(ran_on == gui);
}

TEST_CASE("call_on_gui_thread marshals a worker's call to the GUI thread", "[app][threads]")
{
    seventhslide::test::ensure_qt_application();
    QObject context;
    const auto* gui = QThread::currentThread();

    std::atomic<bool> finished{false};
    const QThread* ran_on = nullptr;
    int value = 0;
    std::thread worker([&] {
        const auto result = call_on_gui_thread(&context, [] {
            return std::pair<const QThread*, int>{QThread::currentThread(), 42};
        });
        ran_on = result.first;
        value = result.second;
        finished = true;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!finished && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    worker.join();
    REQUIRE(finished);
    REQUIRE(ran_on == gui);
    REQUIRE(value == 42);
}

TEST_CASE("call_on_gui_thread stops waiting once the app is quitting", "[app][threads]")
{
    seventhslide::test::ensure_qt_application();
    QObject context;
    g_gui_calls_closed = true;
    int value = -1;
    // The GUI thread never pumps events here; without the flag this would hang.
    std::thread worker([&] { value = call_on_gui_thread(&context, [] { return 7; }); });
    worker.join();
    g_gui_calls_closed = false;
    REQUIRE(value == 0);
    // Drop the call that was queued but never ran.
    QCoreApplication::processEvents();
}
