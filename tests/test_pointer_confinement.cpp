// The pointer held on an area of a window (pointer_confinement.hpp): one hold
// at a time, each let go when dropped or when the system lets it go, and the
// area in the system's own pixels.

#include <catch2/catch_test_macros.hpp>

#include "fake_pointer_confinement.hpp"
#include "pointer_confinement.hpp"
#include "test_qt_app.hpp"

#ifdef SEVENTHSLIDE_HAS_NATIVE_POINTER_CONFINEMENT
#include "pointer_confinement_native.hpp"
#endif

#include <QSignalSpy>
#include <QWindow>

using seventhslide::test::FakePointerConfinement;
using seventhslide::test::PointerHoldLog;

TEST_CASE("A new pointer hold ends the one before it", "[pointer-confinement]")
{
    seventhslide::test::ensure_qt_application();
    QWindow window;
    PointerHoldLog log;
    FakePointerConfinement confinement(log);

    auto first = confinement.hold(window, QRectF(10, 10, 100, 50));
    REQUIRE(first != nullptr);
    QSignalSpy first_lost(first.get(), &seventhslide::app::PointerHold::lost);
    CHECK(log.holds[0].held);

    // Two at once would fight over the pointer, or be refused outright.
    auto second = confinement.hold(window, QRectF(20, 20, 100, 50));
    REQUIRE(second != nullptr);
    CHECK_FALSE(log.holds[0].held);
    CHECK(first_lost.count() == 1);
    CHECK(log.holds[1].held);

    // The one ended does nothing more.
    first->move(QRectF(0, 0, 10, 10));
    CHECK(log.holds[0].moves == 0);
    second->move(QRectF(30, 30, 100, 50));
    CHECK(log.holds[1].moves == 1);
    CHECK(log.holds[1].area == QRectF(30, 30, 100, 50));
}

TEST_CASE("A pointer hold lets go when dropped, and says when the system lets go",
          "[pointer-confinement]")
{
    seventhslide::test::ensure_qt_application();
    QWindow window;
    PointerHoldLog log;
    FakePointerConfinement confinement(log);

    auto dropped = confinement.hold(window, QRectF(0, 0, 100, 50));
    QSignalSpy dropped_lost(dropped.get(), &seventhslide::app::PointerHold::lost);
    dropped.reset();
    CHECK_FALSE(log.holds[0].held);
    // Its holder dropped it: there is no one to tell.
    CHECK(dropped_lost.count() == 0);

    auto broken = confinement.hold(window, QRectF(0, 0, 100, 50));
    QSignalSpy broken_lost(broken.get(), &seventhslide::app::PointerHold::lost);
    REQUIRE(log.live != nullptr);
    log.live->system_lets_go();
    CHECK_FALSE(log.holds[1].held);
    CHECK(broken_lost.count() == 1);
}

TEST_CASE("No native pointer confinement where the windowing system has none",
          "[pointer-confinement]")
{
    seventhslide::test::ensure_qt_application();
    // The tests run on the offscreen platform, which holds no pointer.
    CHECK(seventhslide::app::make_native_pointer_confinement() == nullptr);
}

#ifdef SEVENTHSLIDE_HAS_NATIVE_POINTER_CONFINEMENT
TEST_CASE("A held area in the system's pixels is rounded inward", "[pointer-confinement]")
{
    seventhslide::test::ensure_qt_application();
    QWindow window;
    // Before it has a platform window there is nowhere to hold.
    CHECK(seventhslide::app::native_window_area(window, QRectF(0, 0, 100, 50)).isEmpty());

    window.resize(400, 300);
    window.create();
    REQUIRE(window.handle() != nullptr);
    REQUIRE(window.devicePixelRatio() == 1.0);
    // Every pixel in it is the area's: a part-covered edge pixel is left out.
    CHECK(seventhslide::app::native_window_area(window, QRectF(10.5, 20.25, 100.25, 50.5))
          == QRect(11, 21, 99, 49));
    CHECK(seventhslide::app::native_window_area(window, QRectF(10, 20, 100, 50))
          == QRect(10, 20, 100, 50));
    // Nothing left of it.
    CHECK(seventhslide::app::native_window_area(window, QRectF(10.2, 20, 0.6, 50)).isEmpty());
}
#endif
