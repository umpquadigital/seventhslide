// Throttle: at most one call per interval, and never the last one lost.

#include "test_qt_app.hpp"

#include <catch2/catch_test_macros.hpp>

#include "throttle.hpp"

#include <QTest>

#include <chrono>

using seventhslide::app::Throttle;
using namespace std::chrono_literals;

TEST_CASE("Throttle calls at once after a quiet spell", "[app][throttle]")
{
    seventhslide::test::ensure_qt_application();
    int calls = 0;
    Throttle throttle(100ms, [&] { ++calls; });
    throttle.poke();
    CHECK(calls == 1);
}

TEST_CASE("Throttle folds pokes inside the interval into one call at its end", "[app][throttle]")
{
    seventhslide::test::ensure_qt_application();
    int calls = 0;
    Throttle throttle(100ms, [&] { ++calls; });
    throttle.poke();
    for (int i = 0; i < 5; ++i) {
        throttle.poke();
    }
    CHECK(calls == 1);  // the rest wait for the interval
    REQUIRE(QTest::qWaitFor([&] { return calls == 2; }, 1000));
    QTest::qWait(250);
    CHECK(calls == 2);  // nothing more was asked for
}

TEST_CASE("Throttle holds a steady stream of pokes to its rate", "[app][throttle]")
{
    // A playing clip's clock, 30 ticks a second, pushed at most every 250 ms.
    seventhslide::test::ensure_qt_application();
    int calls = 0;
    Throttle throttle(250ms, [&] { ++calls; });
    QTimer ticks;
    QObject::connect(&ticks, &QTimer::timeout, [&] { throttle.poke(); });
    ticks.start(33);
    QTest::qWait(1000);
    ticks.stop();
    INFO("calls=" << calls);
    CHECK(calls >= 3);
    CHECK(calls <= 6);
}
