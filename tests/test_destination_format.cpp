// A destination's format: drawn as its output is, or no taller than a height
// of its own in the same shape, or (NDI) as sound alone. The size rule, what
// the OffscreenSink base does with it (on a sink with no transport, so CI
// needs no runtime), and how the host keeps, checks and applies it.

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "destination_rows.hpp"
#include "output_host_test_helpers.hpp"
#include "recording_sink.hpp"
#include "test_qt_app.hpp"
#include "test_wait.hpp"

#include <seventhslide/render/destination.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/output_window.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QPointer>
#include <QSize>
#include <QString>

using seventhslide::render::DestinationKind;
using seventhslide::render::OutputWindowHost;
using seventhslide::render::destination_send_size;
using seventhslide::scene::SceneFrame;
using seventhslide::test::RecordingSink;
using seventhslide::test::run_events_for;

namespace {

[[nodiscard]] seventhslide::scene::SharedSceneFrame canvas(int w, int h)
{
    SceneFrame frame;
    frame.output_id = "out-main";
    frame.canvas_width = w;
    frame.canvas_height = h;
    return seventhslide::scene::share_frame(frame);
}

/// A stored destination record of ``type`` on "out-main", with ``extra``.
[[nodiscard]] nlohmann::json record(const char* id, const char* type,
                                    nlohmann::json extra = nlohmann::json::object())
{
    extra["id"] = id;
    extra["output"] = "out-main";
    extra["type"] = type;
    extra["enabled"] = false;
    return extra;
}

}  // namespace

TEST_CASE("A destination is drawn no taller than its limit, in its full shape",
          "[render][destinations]")
{
    const QSize hd(1920, 1080);
    // As it is in full: no limit, or one it already fits.
    CHECK(destination_send_size(hd, 0) == hd);
    CHECK(destination_send_size(hd, 1080) == hd);
    // Never larger: there is nothing more to send.
    CHECK(destination_send_size(hd, 2160) == hd);
    // Smaller, in the same shape, to the nearest even width.
    CHECK(destination_send_size(hd, 720) == QSize(1280, 720));
    CHECK(destination_send_size(hd, 480) == QSize(854, 480));
    // An odd height is evened down.
    CHECK(destination_send_size(hd, 361) == QSize(640, 360));
    // Any shape: portrait, 4:3, an odd screen.
    CHECK(destination_send_size(QSize(1080, 1920), 720) == QSize(406, 720));
    CHECK(destination_send_size(QSize(1024, 768), 360) == QSize(480, 360));
    CHECK(destination_send_size(QSize(1920, 1200), 1080) == QSize(1728, 1080));
    // Nothing to scale, and never under 2.
    CHECK(destination_send_size(QSize(), 720) == QSize());
    CHECK(destination_send_size(hd, 1) == QSize(4, 2));
}

TEST_CASE("Only some kinds choose their size, and only NDI sends sound alone",
          "[render][destinations]")
{
    using seventhslide::render::destination_kind_sends_sound_alone;
    using seventhslide::render::destination_kind_sizable;
    CHECK(destination_kind_sizable(DestinationKind::Display));
    CHECK(destination_kind_sizable(DestinationKind::Ndi));
    CHECK(destination_kind_sizable(DestinationKind::Vcam));
    // A card's video mode decides its size.
    CHECK_FALSE(destination_kind_sizable(DestinationKind::Bmd));

    CHECK(destination_kind_sends_sound_alone(DestinationKind::Ndi));
    CHECK_FALSE(destination_kind_sends_sound_alone(DestinationKind::Display));
    CHECK_FALSE(destination_kind_sends_sound_alone(DestinationKind::Bmd));
    CHECK_FALSE(destination_kind_sends_sound_alone(DestinationKind::Vcam));
}

TEST_CASE("An offscreen sink draws no taller than its limit as its canvas changes",
          "[render][offscreen][destinations]")
{
    seventhslide::test::ensure_qt_application();
    RecordingSink sink;
    // Before it starts, the transport opens at its own size.
    sink.set_max_height(360);
    REQUIRE(sink.start(QStringLiteral("t"), QSize(1920, 1080)));
    CHECK(sink.output_size() == QSize(640, 360));
    CHECK(sink.last().size() == QSize(640, 360));

    // Each frame's canvas is followed, through the limit.
    sink.present(canvas(1280, 720));
    CHECK(sink.output_size() == QSize(640, 360));
    sink.present(canvas(1024, 768));
    CHECK(sink.output_size() == QSize(480, 360));
    CHECK(sink.last().size() == QSize(480, 360));

    // Lifted on a live sink, it is the canvas's again at once.
    sink.set_max_height(0);
    CHECK(sink.output_size() == QSize(1024, 768));
    CHECK(sink.last().size() == QSize(1024, 768));

    // One it already fits changes nothing.
    sink.set_max_height(1080);
    CHECK(sink.output_size() == QSize(1024, 768));
}

TEST_CASE("An offscreen sink with video off draws and delivers nothing",
          "[render][offscreen][destinations]")
{
    seventhslide::test::ensure_qt_application();

    SECTION("switched off while live, and back on")
    {
        RecordingSink sink;
        REQUIRE(sink.start(QStringLiteral("t"), QSize(320, 180)));
        REQUIRE(sink.deliveries() > 0);
        sink.set_video_enabled(false);
        const int before = sink.deliveries();
        run_events_for(120);  // past several keepalive ticks
        sink.present(canvas(640, 360));
        CHECK(sink.deliveries() == before);
        CHECK(sink.is_live());

        // Back on: the current picture at once, at the size it has now.
        sink.set_video_enabled(true);
        CHECK(sink.deliveries() > before);
        CHECK(sink.last().size() == QSize(640, 360));
        const int resumed = sink.deliveries();
        run_events_for(120);
        CHECK(sink.deliveries() > resumed);
    }

    SECTION("started with it off")
    {
        RecordingSink sink;
        sink.set_video_enabled(false);
        REQUIRE(sink.start(QStringLiteral("t"), QSize(320, 180)));
        CHECK(sink.is_live());
        run_events_for(120);
        CHECK(sink.deliveries() == 0);
    }
}

TEST_CASE("A destination's format is kept with it, and checked by its kind",
          "[render][destinations]")
{
    seventhslide::test::ensure_qt_application();
    OutputWindowHost host;
    host.replace_destinations(nlohmann::json::array({
        record("n", "ndi", {{"audio", false}}),
        record("d", "display"),
        record("b", "bmd"),
    }));
    const auto find = [&host](const char* id) {
        const auto* d = host.registry().find(QString::fromLatin1(id));
        REQUIRE(d != nullptr);
        return *d;
    };
    // As the output is, until changed.
    CHECK(find("n").max_height == 0);
    CHECK(find("n").video);

    // Sound alone is the sound: it turns the sound on, and keeps it on.
    REQUIRE(host.set_destination_format(QStringLiteral("n"), 720, false));
    CHECK(find("n").max_height == 720);
    CHECK_FALSE(find("n").video);
    CHECK(find("n").audio);
    CHECK_FALSE(host.set_destination_audio(QStringLiteral("n"), false));
    CHECK_FALSE(host.last_error().isEmpty());
    CHECK(find("n").audio);

    // A display has a size of its own, but its picture is all it sends.
    REQUIRE(host.set_destination_format(QStringLiteral("d"), 720, true));
    CHECK(find("d").max_height == 720);
    CHECK_FALSE(host.set_destination_format(QStringLiteral("d"), 720, false));
    CHECK(find("d").video);
    // A Blackmagic card's video mode decides its size.
    CHECK_FALSE(host.set_destination_format(QStringLiteral("b"), 720, true));
    CHECK(host.set_destination_format(QStringLiteral("b"), 0, true));
    CHECK_FALSE(host.set_destination_format(QStringLiteral("n"), -1, true));
    CHECK_FALSE(host.set_destination_format(QStringLiteral("nope"), 0, true));

    // What the list shows, and what the next launch reads back.
    const auto row = seventhslide::app::destination_rows(host).front().toMap();
    CHECK(row.value(QStringLiteral("maxHeight")).toInt() == 720);
    CHECK_FALSE(row.value(QStringLiteral("video")).toBool());
    CHECK(row.value(QStringLiteral("sizable")).toBool());
    CHECK(row.value(QStringLiteral("soundAloneCapable")).toBool());
    OutputWindowHost reloaded;
    reloaded.replace_destinations(host.destination_records());
    CHECK(seventhslide::app::destination_rows(reloaded) == seventhslide::app::destination_rows(host));
}

TEST_CASE("A stored format a kind cannot have is read back as it can",
          "[render][destinations]")
{
    seventhslide::test::ensure_qt_application();
    OutputWindowHost host;
    host.replace_destinations(nlohmann::json::array({
        // One that does not say its format.
        record("old", "ndi"),
        record("n", "ndi", {{"video", false}, {"audio", false}}),
        record("d", "display", {{"video", false}, {"max_height", 720}}),
        record("b", "bmd", {{"max_height", 720}}),
        record("neg", "vcam", {{"max_height", -5}}),
    }));
    const auto find = [&host](const char* id) {
        const auto* d = host.registry().find(QString::fromLatin1(id));
        REQUIRE(d != nullptr);
        return *d;
    };
    CHECK(find("old").max_height == 0);
    CHECK(find("old").video);
    // Sound alone with its sound off would send nothing.
    CHECK(find("n").audio);
    CHECK(find("d").video);
    CHECK(find("d").max_height == 720);
    CHECK(find("b").max_height == 0);
    CHECK(find("neg").max_height == 0);
}

TEST_CASE("An audience window draws no taller than its limit, filling its screen",
          "[render][window][destinations]")
{
    seventhslide::test::ensure_qt_application();
    OutputWindowHost host;
    const QString dest = seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));
    host.present(QStringLiteral("out-main"), canvas(1920, 1080));
    const QPointer<seventhslide::render::OutputWindow> win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);
    const QSize full = win->snapshot().size();
    REQUIRE(full.height() >= 4);

    // The same window, drawing smaller in the screen's shape.
    const int half = full.height() / 2;
    REQUIRE(host.set_destination_format(dest, half, true));
    REQUIRE(win != nullptr);
    CHECK(host.window(QStringLiteral("out-main")) == win.data());
    CHECK(win->snapshot().size() == destination_send_size(full, half));

    // A display has nothing to send without its picture.
    CHECK_FALSE(host.set_destination_format(dest, half, false));

    // Kept when it opens again.
    REQUIRE(host.set_destination_enabled(dest, false));
    REQUIRE(host.set_destination_enabled(dest, true));
    host.present(QStringLiteral("out-main"), canvas(1920, 1080));
    REQUIRE(host.window(QStringLiteral("out-main")) != nullptr);
    CHECK(host.window(QStringLiteral("out-main"))->snapshot().size()
          == destination_send_size(full, half));

    REQUIRE(host.set_destination_format(dest, 0, true));
    CHECK(host.window(QStringLiteral("out-main"))->snapshot().size() == full);
}

TEST_CASE("A live NDI destination changes its format without going down",
          "[render][ndi][destinations]")
{
    seventhslide::test::ensure_qt_application();
    if (!seventhslide::render::ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }
    OutputWindowHost host;
    const QString ndi = host.add_destination(QStringLiteral("out-main"), QStringLiteral("ndi"));
    REQUIRE_FALSE(ndi.isEmpty());
    REQUIRE(host.set_destination_ndi_name(ndi, QStringLiteral("SeventhSlide HostFormatTest")));
    REQUIRE(host.set_destination_enabled(ndi, true));
    const auto live = [&] {
        const auto* d = host.registry().find(ndi);
        REQUIRE(d != nullptr);
        return host.is_live(*d);
    };
    REQUIRE(live());
    REQUIRE(host.set_destination_format(ndi, 360, true));
    CHECK(live());
    REQUIRE(host.set_destination_format(ndi, 360, false));
    CHECK(live());
    REQUIRE(host.set_destination_format(ndi, 0, true));
    CHECK(live());
    host.shutdown();
}
