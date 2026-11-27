#pragma once

// Waiting in a test for something to happen, rather than for a fixed time.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTest>
#include <QThread>

#include <chrono>
#include <utility>

namespace seventhslide::test {

/// Run the event loop until ``done``, or ``ms`` pass: whether it came.
template <class Done>
bool wait_until(Done&& done, int ms = 20000)
{
    return QTest::qWaitFor(std::forward<Done>(done), ms);
}

template <class Done>
bool wait_until(Done&& done, std::chrono::milliseconds timeout)
{
    return wait_until(std::forward<Done>(done), static_cast<int>(timeout.count()));
}

/// Run the event loop until ``busy`` has said no for a quarter of a second
/// (a message in flight on loopback takes far less), or ``ms`` pass: whether
/// it settled. It asks ``busy`` between runs of up to 20 ms of events, so a
/// message answered within one run is not seen: links that keep each other
/// alive every 150 ms settle.
template <class Busy>
bool settle_while(Busy&& busy, int ms = 20000)
{
    QElapsedTimer timer;
    timer.start();
    QElapsedTimer quiet;
    quiet.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
        if (busy()) {
            quiet.restart();
        } else if (quiet.elapsed() >= 250) {
            return true;
        }
    }
    return false;
}

/// Run the event loop for ``ms``, whatever happens or does not: a wait that
/// gives what must not happen the time to, or lets time pass on purpose.
/// It sleeps while nothing is queued, as ``wait_until`` does.
inline void run_events_for(int ms)
{
    QTest::qWait(ms);
}

}  // namespace seventhslide::test
