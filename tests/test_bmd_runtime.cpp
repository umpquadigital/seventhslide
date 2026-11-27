#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/render/bmd_runtime.hpp>

#include <QSet>
#include <QString>

using seventhslide::render::BmdDisplayModeInfo;
using seventhslide::render::bmd_list_devices;
using seventhslide::render::bmd_list_display_modes;
using seventhslide::render::bmd_missing_message;
using seventhslide::render::bmd_no_device_message;
using seventhslide::render::bmd_runtime_available;
using seventhslide::render::bmd_runtime_refresh;
using seventhslide::render::bmd_runtime_version;

// Unlike NDI, Blackmagic has THREE states, and all three have to behave:
//   (a) no driver - CI, and most build machines
//   (b) driver, no card - a developer box with Desktop Video installed
//   (c) driver and a card - the only state that can actually send frames
// Everything below passes in all three.

TEST_CASE("Blackmagic probe is safe with no driver installed", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    const bool available = bmd_runtime_available();
    if (available) {
        // The driver answered, so it must have told us who it is.
        REQUIRE_FALSE(bmd_runtime_version().isEmpty());
    } else {
        REQUIRE(bmd_runtime_version().isEmpty());
        // State (a): enumeration must be empty rather than crash.
        REQUIRE(bmd_list_devices().empty());
    }
}

TEST_CASE("Blackmagic probe is stable across calls and refresh", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    const bool first = bmd_runtime_available();
    REQUIRE(bmd_runtime_available() == first);

    // Refresh exists so an operator can install Desktop Video without
    // restarting. It must never downgrade a driver already loaded, and must
    // not crash when there is nothing to find.
    bmd_runtime_refresh();
    REQUIRE(bmd_runtime_available() >= first);
}

TEST_CASE("Blackmagic messages tell the two failures apart", "[render][bmd]")
{
    // "No driver" and "driver but no card" are different problems with
    // different fixes, so they must never collapse into one string.
    REQUIRE_FALSE(bmd_missing_message().isEmpty());
    REQUIRE_FALSE(bmd_no_device_message().isEmpty());
    REQUIRE(bmd_missing_message() != bmd_no_device_message());
    REQUIRE(bmd_missing_message().contains(QStringLiteral("Desktop Video")));
}

TEST_CASE("enumerated Blackmagic devices are well formed", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    // In state (b) this is legitimately empty and the loop simply does not run.
    QSet<QString> ids;
    for (const auto& d : bmd_list_devices()) {
        REQUIRE_FALSE(d.id.isEmpty());     // the persistence key
        REQUIRE_FALSE(d.label.isEmpty());  // what the operator picks from
        REQUIRE(d.can_output);             // bmd_list_devices filters to outputs
        REQUIRE_FALSE(ids.contains(d.id));  // ids must be unique to be a key
        ids.insert(d.id);
    }
}

TEST_CASE("display modes are only offered for a device that exists", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    REQUIRE(bmd_list_display_modes(QString()).empty());
    REQUIRE(bmd_list_display_modes(QStringLiteral("no-such-device")).empty());

    for (const auto& d : bmd_list_devices()) {
        const auto modes = bmd_list_display_modes(d.id);
        // A device that can output must offer at least one mode to output in.
        REQUIRE_FALSE(modes.empty());
        for (const auto& m : modes) {
            REQUIRE(m.key.size() == 4);  // the mode's four-character code
            REQUIRE_FALSE(m.name.isEmpty());
            REQUIRE(m.width > 0);
            REQUIRE(m.height > 0);
        }
    }
}

// Pure arithmetic, so this is the one piece of mode handling that can be
// pinned down on a machine with no hardware at all.
TEST_CASE("display mode frame rate drives the keepalive cadence", "[render][bmd]")
{
    BmdDisplayModeInfo p30;
    p30.frame_duration = 1001;
    p30.time_scale = 30000;
    REQUIRE(p30.fps() > 29.9);
    REQUIRE(p30.fps() < 30.0);
    REQUIRE(p30.tick_interval_ms() == 33);

    BmdDisplayModeInfo p25;
    p25.frame_duration = 1;
    p25.time_scale = 25;
    REQUIRE(p25.fps() == 25.0);
    // The OffscreenSink base assumes ~30 fps; a 25 fps mode must not inherit it.
    REQUIRE(p25.tick_interval_ms() == 40);

    BmdDisplayModeInfo p60;
    p60.frame_duration = 1000;
    p60.time_scale = 60000;
    REQUIRE(p60.tick_interval_ms() == 16);

    // A driver that reports nothing usable must not divide by zero.
    BmdDisplayModeInfo unknown;
    REQUIRE(unknown.fps() == 0.0);
    REQUIRE(unknown.tick_interval_ms() == 33);
}
