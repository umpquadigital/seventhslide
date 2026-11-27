#include "destination_rows.hpp"
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include "output_host_test_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/audio/device_sink.hpp>
#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/render/bmd_runtime.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/output_window.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/virtual_camera.hpp>
#include <seventhslide/scene/scene_frame.hpp>
#include <seventhslide/stream/encoder_catalog.hpp>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QSignalSpy>
#include <QPointer>
#include <QTest>
#include <QWindow>

#include <filesystem>
#include <set>
#include <string>

using seventhslide::render::OutputWindowHost;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;

TEST_CASE("OutputWindowHost lists screens and opens/closes windows", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    const QVariantList displays = seventhslide::app::display_rows(host);
    REQUIRE_FALSE(displays.isEmpty());
    REQUIRE_FALSE(displays.front().toMap().value(QStringLiteral("key")).toString().isEmpty());

    const QString dest = seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));

    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#112233";
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));
    REQUIRE(host.window(QStringLiteral("out-main")) != nullptr);
    REQUIRE(host.window(QStringLiteral("out-main"))->presenter()->current_frame() != nullptr);
    REQUIRE(host.window(QStringLiteral("out-main"))->presenter()->current_frame()->background.color == "#112233");

    REQUIRE(host.set_destination_enabled(dest, false));
    REQUIRE_FALSE(host.is_open(QStringLiteral("out-main")));
}

TEST_CASE("OutputWindowHost window snapshot paints the presented color", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    const QString dest = seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));

    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#e04040";
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));

    auto* win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);
    const QImage img = win->snapshot();
    REQUIRE_FALSE(img.isNull());
    REQUIRE(img.width() > 0);
    REQUIRE(img.height() > 0);
    const QRgb px = img.pixel(img.width() / 2, img.height() / 2);
    REQUIRE(qRed(px) > 160);
    QCoreApplication::processEvents();
    const QImage grabbed = win->grabWindow();
    REQUIRE_FALSE(grabbed.isNull());
    const QRgb gpx = grabbed.pixel(grabbed.width() / 2, grabbed.height() / 2);
    INFO("grabbed center " << qRed(gpx) << " " << qGreen(gpx) << " " << qBlue(gpx)
                           << " size " << grabbed.width() << "x" << grabbed.height());
    REQUIRE(qRed(gpx) > 160);
    REQUIRE(qGreen(px) < 90);
    REQUIRE(qBlue(px) < 90);

    REQUIRE(host.set_destination_enabled(dest, false));
}

TEST_CASE("present_all routes each frame by its output id", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    const QString dest = seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));

    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#e04040";
    host.present_all({seventhslide::scene::share_frame(frame)});

    auto* win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);
    REQUIRE(win->presenter()->current_frame() != nullptr);
    REQUIRE(win->presenter()->current_frame()->background.color == "#e04040");
    REQUIRE(win->title().contains(QStringLiteral("song")));

    REQUIRE(host.set_destination_enabled(dest, false));
}

TEST_CASE("OutputWindowHost replays the last frame when a window opens later", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#e04040";
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));

    const QString dest = seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));

    auto* win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);
    REQUIRE(win->presenter()->current_frame() != nullptr);
    REQUIRE(win->presenter()->current_frame()->background.color == "#e04040");
    const QImage img = win->snapshot();
    REQUIRE_FALSE(img.isNull());
    const QRgb px = img.pixel(img.width() / 2, img.height() / 2);
    REQUIRE(qRed(px) > 160);

    REQUIRE(host.set_destination_enabled(dest, false));
}

TEST_CASE("A preview's hold keeps its output's presenter until the last release", "[render][preview]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#e04040";
    // Before anything holds it, an output with no destination presents nowhere,
    // but the frame is kept for whatever opens next.
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));

    auto* held = host.hold_presenter(QStringLiteral("out-main"));
    REQUIRE(held != nullptr);
    REQUIRE(held->current_frame() != nullptr);
    REQUIRE(held->current_frame()->background.color == "#e04040");

    SECTION("a destination opening draws the same presenter, and closing leaves it held")
    {
        const QString dest =
            seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));
        auto* win = host.window(QStringLiteral("out-main"));
        REQUIRE(win != nullptr);
        REQUIRE(win->presenter() == held);
        REQUIRE(host.set_destination_enabled(dest, false));
        REQUIRE(held->current_frame() != nullptr);  // still there
    }
    SECTION("shutting the destinations down keeps a held presenter")
    {
        host.shutdown();
        REQUIRE(held->current_frame() != nullptr);
        frame.background.color = "#40e040";
        host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));
        REQUIRE(held->current_frame()->background.color == "#40e040");
    }
    SECTION("a deleted output's held presenter lasts until the release")
    {
        (void)seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));
        host.remove_output(QStringLiteral("out-main"));
        REQUIRE(host.window(QStringLiteral("out-main")) == nullptr);
        REQUIRE(held->current_frame() != nullptr);
    }
    host.release_presenter(QStringLiteral("out-main"));
}

TEST_CASE("An output draws a picture that arrives after it was drawn without it", "[render][preview]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir root;
    std::filesystem::create_directories(root.path / "images");

    OutputWindowHost host;
    host.set_static_root(root.path);
    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.canvas_width = 160;
    frame.canvas_height = 90;
    frame.background.type = seventhslide::layout::BackgroundType::Image;
    frame.background.image = "/static/images/late.png";
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));
    auto* held = host.hold_presenter(QStringLiteral("out-main"));
    REQUIRE(held != nullptr);
    const auto centre = [&] {
        return held->surface(QSize(160, 90), Qt::black).pixelColor(80, 45);
    };
    CHECK(centre() != QColor(Qt::red));

    // Another picture arriving: nothing drawn again.
    const auto before = held->revision();
    host.forget_missing_pictures({"/static/images/other.png"});
    CHECK(held->revision() == before);

    QImage red(16, 9, QImage::Format_RGB32);
    red.fill(Qt::red);
    REQUIRE(red.save(QString::fromStdString((root.path / "images" / "late.png").string())));
    host.forget_missing_pictures({"/static/images/late.png"});
    CHECK(held->revision() != before);
    CHECK(centre() == QColor(Qt::red));

    // Nothing missing: nothing drawn again.
    const auto after = held->revision();
    host.forget_missing_pictures({"/static/images/late.png"});
    CHECK(held->revision() == after);
    host.release_presenter(QStringLiteral("out-main"));
}

TEST_CASE("OutputWindowHost screen_key is stable for primary", "[render]")
{
    seventhslide::test::ensure_qt_application();
    QScreen* primary = QGuiApplication::primaryScreen();
    REQUIRE(primary != nullptr);
    const QString a = seventhslide::render::DeviceCatalog::screen_key(primary);
    const QString b = seventhslide::render::DeviceCatalog::screen_key(primary);
    REQUIRE(a == b);
    REQUIRE_FALSE(a.isEmpty());
}

TEST_CASE("OutputWindowHost shutdown keeps assignment intent", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    const QString dest = seventhslide::test::open_on_first_display(host, QStringLiteral("out-foldback"));

    host.shutdown();
    REQUIRE_FALSE(host.is_open(QStringLiteral("out-foldback")));
    REQUIRE(host.window(QStringLiteral("out-foldback")) == nullptr);
    // Windows are deleted on the next event loop pass, never inside the
    // window event handling a shutdown can run from.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (QWindow* w : QGuiApplication::allWindows()) {
        REQUIRE(qobject_cast<seventhslide::render::OutputWindow*>(w) == nullptr);
    }

    // Assignment retained -> restore reopens when the output name is still valid.
    host.restore();
    REQUIRE(host.is_open(QStringLiteral("out-foldback")));

    REQUIRE(host.set_destination_enabled(dest, false));
    REQUIRE_FALSE(host.is_open(QStringLiteral("out-foldback")));
}

TEST_CASE("A destination moves up and down its own output's list", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    const QString main = QStringLiteral("out-main");
    const QString a = host.add_destination(main, QStringLiteral("display"));
    // Another output's destination between them in the shared list.
    const QString other = host.add_destination(QStringLiteral("out-stage"), QStringLiteral("display"));
    const QString b = host.add_destination(main, QStringLiteral("display"));
    REQUIRE_FALSE(a.isEmpty());
    REQUIRE_FALSE(other.isEmpty());
    REQUIRE_FALSE(b.isEmpty());

    const auto order = [&](const QString& output) {
        QStringList ids;
        for (const auto& row : seventhslide::app::destination_rows(host, output)) {
            ids << row.toMap().value(QStringLiteral("id")).toString();
        }
        return ids;
    };
    QSignalSpy edited(&host, &OutputWindowHost::destinationsEdited);

    // B passes A, stepping over the other output's.
    REQUIRE(host.move_destination(b, -1));
    CHECK(order(main) == QStringList{b, a});
    CHECK(order(QStringLiteral("out-stage")) == QStringList{other});
    CHECK(edited.count() == 1);

    // Nothing above the first or below the last; an unknown id moves nothing.
    CHECK_FALSE(host.move_destination(b, -1));
    CHECK_FALSE(host.move_destination(a, 1));
    CHECK_FALSE(host.move_destination(other, 1));
    CHECK_FALSE(host.move_destination(QStringLiteral("nope"), 1));
    CHECK(edited.count() == 1);

    REQUIRE(host.move_destination(b, 1));
    CHECK(order(main) == QStringList{a, b});
}

TEST_CASE("OutputWindowHost supports multiple display destinations", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    const QString id_a = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    const QString id_b = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    REQUIRE_FALSE(id_a.isEmpty());
    REQUIRE_FALSE(id_b.isEmpty());
    REQUIRE(id_a != id_b);
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main")).size() == 2);

    REQUIRE(host.set_destination_enabled(id_a, true));
    REQUIRE(host.is_open(QStringLiteral("out-main")));

    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#112233";
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));
    REQUIRE(host.window(QStringLiteral("out-main")) != nullptr);
    REQUIRE(host.window(QStringLiteral("out-main"))->presenter()->current_frame() != nullptr);

    REQUIRE(host.remove_destination(id_b));
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main")).size() == 1);

    // A rename is only a new title: the destination, its window and the
    // output's presentation all stay with the output id.
    host.set_output_names({{QStringLiteral("out-main"), QStringLiteral("Auditorium")}});
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main")).size() == 1);
    REQUIRE(host.window(QStringLiteral("out-main"))->title().contains(QStringLiteral("Auditorium")));
    REQUIRE(host.window(QStringLiteral("out-main"))->presenter()->current_frame()->background.color
            == "#112233");

    host.remove_output(QStringLiteral("out-main"));
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main")).isEmpty());
    REQUIRE_FALSE(host.is_open(QStringLiteral("out-main")));
}

TEST_CASE("OutputWindowHost sends one output to two screens when available", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    const QVariantList displays = seventhslide::app::display_rows(host);
    if (displays.size() < 2) {
        SKIP("needs two screens");
    }
    const QString key_a = displays[0].toMap().value(QStringLiteral("key")).toString();
    const QString key_b = displays[1].toMap().value(QStringLiteral("key")).toString();
    const QString id_a = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    const QString id_b = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    REQUIRE(host.set_destination_screen(id_a, key_a));
    REQUIRE(host.set_destination_screen(id_b, key_b));
    REQUIRE(host.set_destination_enabled(id_a, true));
    REQUIRE(host.set_destination_enabled(id_b, true));

    const auto dests = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
    int live = 0;
    for (const auto& v : dests) {
        if (v.toMap().value(QStringLiteral("live")).toBool()) {
            ++live;
        }
    }
    REQUIRE(live == 2);
    REQUIRE(host.is_open(QStringLiteral("out-main")));

    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#445566";
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));
    REQUIRE(host.window(QStringLiteral("out-main"))->presenter()->current_frame() != nullptr);
    REQUIRE(host.window(QStringLiteral("out-main"))->presenter()->current_frame()->background.color == "#445566");
}

TEST_CASE("OutputWindowHost enabling a display steals it from another output", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    const QString main_id =
        host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    const QString fold_id =
        host.add_destination(QStringLiteral("out-foldback"), QStringLiteral("display"));
    const QString key =
        seventhslide::app::display_rows(host).front().toMap().value(QStringLiteral("key")).toString();
    REQUIRE(host.set_destination_screen(main_id, key));
    REQUIRE(host.set_destination_screen(fold_id, key));
    REQUIRE(host.set_destination_enabled(main_id, true));
    REQUIRE(host.is_open(QStringLiteral("out-main")));

    REQUIRE(host.set_destination_enabled(fold_id, true));
    REQUIRE(host.is_open(QStringLiteral("out-foldback")));
    REQUIRE_FALSE(host.is_open(QStringLiteral("out-main")));
}

TEST_CASE("OutputWindowHost destination names and type catalog", "[render]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    const auto types = seventhslide::app::destination_type_rows();
    REQUIRE(types.size() == 6);
    bool display_ok = false;
    bool window_ok = false;
    bool srt_ok = false;
    bool vcam_ok = false;
    bool ndi_ok = false;
    bool bmd_ok = true;
    for (const auto& v : types) {
        const auto m = v.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        const bool available = m.value(QStringLiteral("available")).toBool();
        if (type == QLatin1String("display")) {
            display_ok = available;
        } else if (type == QLatin1String("vcam")) {
            vcam_ok = available;
        } else if (type == QLatin1String("ndi")) {
            ndi_ok = available;
        } else if (type == QLatin1String("bmd")) {
            bmd_ok = available;
        } else if (type == QLatin1String("srt")) {
            srt_ok = available;
        } else if (type == QLatin1String("window")) {
            window_ok = available;
        }
    }
    REQUIRE(display_ok);
    REQUIRE(window_ok);
    REQUIRE(vcam_ok == seventhslide::render::virtual_camera_implemented());
    // NDI is greyed out unless the operator installed a runtime themselves -
    // SeventhSlide never ships one.
    REQUIRE(ndi_ok == seventhslide::render::ndi_runtime_available());
    // Likewise Blackmagic: greyed out unless the operator installed Desktop
    // Video. Availability tracks the driver, not whether a card is attached.
    REQUIRE(bmd_ok == seventhslide::render::bmd_runtime_available());
    // SRT wherever FFmpeg was built with it.
    REQUIRE(srt_ok == seventhslide::stream::srt_supported());

    // Every kind says whether this system can have it at all: all but a
    // virtual camera off Linux, which the add menu then leaves out. Its row
    // says, in this system's terms, what to do with no device.
    for (const auto& v : types) {
        const auto m = v.toMap();
        const bool vcam = m.value(QStringLiteral("type")).toString() == QLatin1String("vcam");
        REQUIRE(m.contains(QStringLiteral("built")));
        CHECK(m.value(QStringLiteral("built")).toBool()
              == (!vcam || seventhslide::render::virtual_camera_implemented()));
        if (vcam) {
            CHECK(m.value(QStringLiteral("message")).toString()
                  == seventhslide::render::virtual_camera_missing_message());
        }
    }

    if (!seventhslide::render::bmd_runtime_available()) {
        REQUIRE(host.add_destination(QStringLiteral("out-main"), QStringLiteral("bmd")).isEmpty());
    }
    if (seventhslide::render::ndi_runtime_available()) {
        host.set_output_names({{QStringLiteral("out-main"), QStringLiteral("Main")}});
        const QString ndi_id = host.add_destination(QStringLiteral("out-main"), QStringLiteral("ndi"));
        REQUIRE_FALSE(ndi_id.isEmpty());
        const auto rows = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
        REQUIRE(rows.size() == 1);
        const auto row = rows.front().toMap();
        // A fresh NDI destination is pre-named after its output.
        REQUIRE(row.value(QStringLiteral("targetKey")).toString()
                == QStringLiteral("SeventhSlide Main"));
        REQUIRE_FALSE(row.value(QStringLiteral("missing")).toBool());
        REQUIRE_FALSE(row.value(QStringLiteral("alpha")).toBool());
        // A second NDI destination must not collide on the network.
        const QString second = host.add_destination(QStringLiteral("out-main"), QStringLiteral("ndi"));
        REQUIRE_FALSE(second.isEmpty());
        REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main")).back().toMap()
                    .value(QStringLiteral("targetKey")).toString()
                != QStringLiteral("SeventhSlide Main"));
        // Duplicate names are rejected outright.
        REQUIRE_FALSE(host.set_destination_ndi_name(second, QStringLiteral("SeventhSlide Main")));
        REQUIRE_FALSE(host.last_error().isEmpty());
        REQUIRE(host.set_destination_ndi_name(second, QStringLiteral("Stage Left")));
        REQUIRE(host.set_destination_alpha(second, true));
        REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main")).back().toMap()
                    .value(QStringLiteral("alpha")).toBool());
        // Sound is on unless turned off.
        REQUIRE(row.value(QStringLiteral("audio")).toBool());
        REQUIRE(host.set_destination_audio(second, false));
        REQUIRE_FALSE(seventhslide::app::destination_rows(host, QStringLiteral("out-main"))
                          .back().toMap().value(QStringLiteral("audio")).toBool());
        host.remove_output(QStringLiteral("out-main"));
    } else {
        REQUIRE(host.add_destination(QStringLiteral("out-main"), QStringLiteral("ndi")).isEmpty());
    }
    if (seventhslide::render::virtual_camera_implemented()) {
        REQUIRE_FALSE(host.add_destination(QStringLiteral("out-main"), QStringLiteral("vcam")).isEmpty());
        host.remove_output(QStringLiteral("out-main"));
    } else {
        REQUIRE(host.add_destination(QStringLiteral("out-main"), QStringLiteral("vcam")).isEmpty());
    }

    const QString id = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    REQUIRE_FALSE(id.isEmpty());
    const auto listed = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
    REQUIRE(listed.size() == 1);
    REQUIRE_FALSE(listed.front().toMap().value(QStringLiteral("name")).toString().isEmpty());
    REQUIRE(listed.front().toMap().value(QStringLiteral("name")).toString()
            == QStringLiteral("Display 1"));

    const QString id_b = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    REQUIRE_FALSE(id_b.isEmpty());
    const auto two = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
    REQUIRE(two.size() == 2);
    REQUIRE(two[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Display 1"));
    REQUIRE(two[1].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Display 2"));
    REQUIRE_FALSE(two[0].toMap().value(QStringLiteral("name")).toString().startsWith(QLatin1String("0x")));
    REQUIRE_FALSE(two[1].toMap().value(QStringLiteral("name")).toString().startsWith(QLatin1String("0x")));

    host.add_destination(QStringLiteral("out-foldback"), QStringLiteral("display"));
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-foldback")).front().toMap().value(QStringLiteral("name")).toString()
            == QStringLiteral("Display 1"));

    REQUIRE(host.set_destination_name(id, QStringLiteral("  House Left  ")));
    const auto renamed = seventhslide::app::destination_rows(host, QStringLiteral("out-main")).front().toMap();
    REQUIRE(renamed.value(QStringLiteral("name")).toString() == QStringLiteral("House Left"));
    REQUIRE(host.set_destination_name(QStringLiteral("missing"), QStringLiteral("X")) == false);
}

TEST_CASE("OutputWindowHost virtual camera destinations", "[render][vcam]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    if (!seventhslide::render::virtual_camera_implemented()) {
        REQUIRE(host.add_destination(QStringLiteral("out-main"), QStringLiteral("vcam")).isEmpty());
        return;
    }

    const QString id = host.add_destination(QStringLiteral("out-main"), QStringLiteral("vcam"));
    REQUIRE_FALSE(id.isEmpty());
    const auto listed = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
    REQUIRE(listed.size() == 1);
    REQUIRE(listed.front().toMap().value(QStringLiteral("type")).toString()
            == QStringLiteral("vcam"));
    REQUIRE(listed.front().toMap().value(QStringLiteral("name")).toString()
            == QStringLiteral("Virtual Camera 1"));
    REQUIRE_FALSE(listed.front().toMap().value(QStringLiteral("enabled")).toBool());

    const auto devices = seventhslide::render::list_virtual_camera_devices();
    const bool enabled = host.set_destination_enabled(id, true);
    if (devices.empty()) {
        REQUIRE_FALSE(enabled);
        REQUIRE_FALSE(host.last_error().isEmpty());
        REQUIRE_FALSE(host.is_open(QStringLiteral("out-main")));
        REQUIRE_FALSE(listed.front().toMap().value(QStringLiteral("live")).toBool());
    } else {
        REQUIRE(enabled);
        REQUIRE(host.is_open(QStringLiteral("out-main")));
        SceneFrame frame;
        frame.output_id = "out-main";
        frame.mode = SceneMode::Song;
        frame.background.type = seventhslide::layout::BackgroundType::Color;
        frame.background.color = "#112233";
        frame.canvas_width = 1920;
        frame.canvas_height = 1080;
        host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));
        const auto live = seventhslide::app::destination_rows(host, QStringLiteral("out-main")).front().toMap();
        REQUIRE(live.value(QStringLiteral("live")).toBool());
        REQUIRE(host.set_destination_enabled(id, false));
        REQUIRE_FALSE(host.is_open(QStringLiteral("out-main")));
    }

    REQUIRE_FALSE(host.set_destination_screen(id, QStringLiteral("serial:nope")));
    REQUIRE(host.remove_destination(id));
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main")).isEmpty());
}

TEST_CASE("Device rows name the output using each device only with a host", "[render][vcam][bmd]")
{
    seventhslide::test::ensure_qt_application();
    OutputWindowHost host;
    const auto key = QStringLiteral("assignedOutput");

    const auto cameras = seventhslide::render::list_virtual_camera_devices();
    const auto with_host = seventhslide::app::virtual_camera_rows(&host);
    const auto without = seventhslide::app::virtual_camera_rows(nullptr);
    REQUIRE(with_host.size() == static_cast<qsizetype>(cameras.size()));
    REQUIRE(without.size() == with_host.size());
    for (qsizetype i = 0; i < without.size(); ++i) {
        CHECK(with_host[i].toMap().contains(key));
        CHECK_FALSE(without[i].toMap().contains(key));
        CHECK(without[i].toMap().value(QStringLiteral("key")).toString()
              == cameras[static_cast<std::size_t>(i)].path);
    }

    const auto cards = seventhslide::render::bmd_list_devices();
    const auto bmd = seventhslide::app::blackmagic_device_rows(nullptr);
    REQUIRE(bmd.size() == static_cast<qsizetype>(cards.size()));
    for (const auto& row : bmd) {
        CHECK_FALSE(row.toMap().contains(key));
    }
    for (const auto& row : seventhslide::app::blackmagic_device_rows(&host)) {
        CHECK(row.toMap().contains(key));
    }
    CHECK(seventhslide::app::blackmagic_mode_rows(QStringLiteral("no-such-device")).isEmpty());

    const auto ndi = seventhslide::app::ndi_runtime_row();
    CHECK(ndi.value(QStringLiteral("available")).toBool()
          == seventhslide::render::ndi_runtime_available());
    CHECK(ndi.value(QStringLiteral("message")).toString().isEmpty()
          == seventhslide::render::ndi_runtime_available());
}

TEST_CASE("OutputWindowHost Blackmagic destinations", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;

    const bool driver = seventhslide::render::bmd_runtime_available();

    // The catalog follows the DRIVER, not a card. That is the whole point of
    // the three-state model: with Desktop Video installed but nothing plugged
    // in, the row is live and the destination reports a missing device.
    bool listed = false;
    bool available = false;
    for (const QVariant& v : seventhslide::app::destination_type_rows()) {
        const auto m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("bmd")) {
            listed = true;
            available = m.value(QStringLiteral("available")).toBool();
        }
    }
    REQUIRE(listed);
    REQUIRE(available == driver);

    const QVariantMap info = seventhslide::app::bmd_runtime_row();
    REQUIRE(info.value(QStringLiteral("available")).toBool() == driver);
    if (!driver) {
        // No driver: adding must be refused outright.
        REQUIRE(host.add_destination(QStringLiteral("out-main"), QStringLiteral("bmd")).isEmpty());
        REQUIRE_FALSE(info.value(QStringLiteral("message")).toString().isEmpty());
        return;
    }

    const QString id = host.add_destination(QStringLiteral("out-main"), QStringLiteral("bmd"));
    REQUIRE_FALSE(id.isEmpty());

    const QVariantList dests = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
    REQUIRE(dests.size() == 1);
    const auto d = dests.front().toMap();
    REQUIRE(d.value(QStringLiteral("type")).toString() == QStringLiteral("bmd"));
    REQUIRE(d.value(QStringLiteral("name")).toString().startsWith(QStringLiteral("Blackmagic")));
    REQUIRE(d.contains(QStringLiteral("modeKey")));
    REQUIRE(d.value(QStringLiteral("driverPresent")).toBool());
    REQUIRE_FALSE(d.value(QStringLiteral("enabled")).toBool());

    const int devices = info.value(QStringLiteral("deviceCount")).toInt();
    if (devices == 0) {
        // Driver, no card. The destination exists but knows it cannot run, and
        // enabling it must fail with an explanation rather than silently
        // pretending to be live.
        REQUIRE(d.value(QStringLiteral("missing")).toBool());
        REQUIRE_FALSE(host.set_destination_enabled(id, true));
        REQUIRE_FALSE(host.last_error().isEmpty());
        REQUIRE_FALSE(seventhslide::app::destination_rows(host, QStringLiteral("out-main"))
                          .front()
                          .toMap()
                          .value(QStringLiteral("live"))
                          .toBool());
    }

    REQUIRE(host.remove_destination(id));
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main")).isEmpty());
}

TEST_CASE("OutputWindowHost keeps a Blackmagic video mode across a reload", "[render][bmd]")
{
    seventhslide::test::ensure_qt_application();

    // A DeckLink output is locked to a broadcast mode, so the mode is part of
    // the destination's identity and has to survive a restart alongside the
    // device: it has to come back out of the stored records.
    OutputWindowHost host;

    if (!seventhslide::render::bmd_runtime_available()) {
        REQUIRE(host.add_destination(QStringLiteral("out-main"), QStringLiteral("bmd")).isEmpty());
        return;
    }

    const QString id = host.add_destination(QStringLiteral("out-main"), QStringLiteral("bmd"));
    REQUIRE_FALSE(id.isEmpty());

    // An unattached device and an arbitrary mode are still recorded: the
    // operator may be setting a machine up before the card arrives.
    REQUIRE(host.set_destination_bmd_device(id, QStringLiteral("some-device")));
    REQUIRE(host.set_destination_bmd_mode(id, QStringLiteral("Hi59")));

    OutputWindowHost reloaded;
    reloaded.replace_destinations(host.destination_records());
    const auto d = seventhslide::app::destination_rows(reloaded, QStringLiteral("out-main")).front().toMap();
    REQUIRE(d.value(QStringLiteral("targetKey")).toString() == QStringLiteral("some-device"));
    REQUIRE(d.value(QStringLiteral("modeKey")).toString() == QStringLiteral("Hi59"));

    // Switching device clears the mode: a mode from one card is not
    // necessarily offered by another.
    REQUIRE(host.set_destination_bmd_device(id, QStringLiteral("other-device")));
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("out-main"))
                .front()
                .toMap()
                .value(QStringLiteral("modeKey"))
                .toString()
                .isEmpty());
}

TEST_CASE("OutputWindowHost's NDI destinations send their output's mix", "[render][ndi][audio]")
{
    seventhslide::test::ensure_qt_application();
    if (!seventhslide::render::ndi_runtime_available()) {
        SKIP("no NDI runtime installed");
    }
    seventhslide::audio::Mixer mixer;
    mixer.set_routing({{"main"}, {{"out-main", {"main"}}}});
    OutputWindowHost host;
    host.set_output_names({{QStringLiteral("out-main"), QStringLiteral("Main")}});
    host.set_audio_mixer(&mixer);
    const QString ndi = host.add_destination(QStringLiteral("out-main"), QStringLiteral("ndi"));
    REQUIRE_FALSE(ndi.isEmpty());
    REQUIRE(host.set_destination_ndi_name(ndi, QStringLiteral("SeventhSlide HostAudioTest")));
    REQUIRE(host.set_destination_enabled(ndi, true));
    CHECK(host.outputs_sending_audio() == QStringList{QStringLiteral("out-main")});

    // Off, then on, without the sender going down.
    REQUIRE(host.set_destination_audio(ndi, false));
    CHECK(host.outputs_sending_audio().isEmpty());
    REQUIRE(host.set_destination_audio(ndi, true));
    CHECK(host.outputs_sending_audio() == QStringList{QStringLiteral("out-main")});

    // A display has no say in sound (until it has a sound device).
    const QString display = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    CHECK_FALSE(host.set_destination_audio(display, false));

    // The mixer taken back, the sender stays up with pictures only; given
    // again, the sound returns.
    host.set_audio_mixer(nullptr);
    CHECK(host.outputs_sending_audio().isEmpty());
    host.set_audio_mixer(&mixer);
    CHECK(host.outputs_sending_audio() == QStringList{QStringLiteral("out-main")});
    host.shutdown();
    CHECK(host.outputs_sending_audio().isEmpty());
}

TEST_CASE("OutputWindowHost plays a display's sound on its device while its window is open",
          "[render][audio]")
{
    using Backend = seventhslide::audio::AudioEngine::Backend;
    seventhslide::test::ensure_qt_application();
    const auto devices = seventhslide::audio::playback_devices(Backend::Null);
    REQUIRE_FALSE(devices.empty());
    const QString device = QString::fromStdString(devices.front().id);

    seventhslide::audio::Mixer mixer;
    mixer.set_routing({{"main"}, {{"out-main", {"main"}}}});
    OutputWindowHost host;
    host.set_sound_backend(Backend::Null);
    host.set_audio_mixer(&mixer);
    const QString d = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    REQUIRE_FALSE(d.isEmpty());
    // No device until one is chosen.
    CHECK(seventhslide::app::destination_rows(host).front().toMap()
              .value(QStringLiteral("audioDevice")).toString().isEmpty());
    REQUIRE(host.set_destination_audio_device(d, device));
    // Chosen, but the window is not open: silent.
    CHECK_FALSE(host.display_sound_playing(d));

    REQUIRE(host.set_destination_enabled(d, true));
    CHECK(host.display_sound_playing(d));
    CHECK(host.outputs_sending_audio() == QStringList{QStringLiteral("out-main")});
    const auto row = seventhslide::app::destination_rows(host).front().toMap();
    CHECK(row.value(QStringLiteral("audioDevice")).toString() == device);
    CHECK(row.value(QStringLiteral("soundPlaying")).toBool());

    // A device that is not there stays silent; back to one that is, it plays.
    REQUIRE(host.set_destination_audio_device(d, QStringLiteral("unplugged-projector")));
    CHECK_FALSE(host.display_sound_playing(d));
    CHECK(host.outputs_sending_audio().isEmpty());
    REQUIRE(host.set_destination_audio_device(d, device));
    CHECK(host.display_sound_playing(d));
    // None.
    REQUIRE(host.set_destination_audio_device(d, {}));
    CHECK_FALSE(host.display_sound_playing(d));
    REQUIRE(host.set_destination_audio_device(d, device));

    // The window closed, and open again.
    REQUIRE(host.set_destination_enabled(d, false));
    CHECK_FALSE(host.display_sound_playing(d));
    REQUIRE(host.set_destination_enabled(d, true));
    CHECK(host.display_sound_playing(d));
    // The mixer taken back, and given again.
    host.set_audio_mixer(nullptr);
    CHECK_FALSE(host.display_sound_playing(d));
    host.set_audio_mixer(&mixer);
    CHECK(host.display_sound_playing(d));

    // Kept with the destination.
    OutputWindowHost reloaded;
    reloaded.replace_destinations(host.destination_records());
    CHECK(seventhslide::app::destination_rows(reloaded).front().toMap()
              .value(QStringLiteral("audioDevice")).toString() == device);
    // Only a display has a sound device: a record says what kind it is.
    const nlohmann::json ndi{{"id", "n"}, {"output", "out-main"}, {"type", "ndi"}};
    reloaded.replace_destinations(nlohmann::json::array({ndi}));
    CHECK_FALSE(reloaded.set_destination_audio_device(QStringLiteral("n"), device));
    CHECK_FALSE(host.set_destination_audio_device(QStringLiteral("nope"), device));

    host.shutdown();
    CHECK_FALSE(host.display_sound_playing(d));
}

TEST_CASE("A display's sound devices list the likeliest first", "[render][audio]")
{
    const std::vector<seventhslide::audio::DeviceInfo> devices{
        {"alsa_output.analog", "Built-in Audio Analog Stereo", true},
        {"alsa_output.hdmi", "HDA NVidia Digital Stereo (HDMI)", false},
        {"alsa_output.usb", "Scarlett 2i2 3rd Gen", false},
    };
    const auto rows = seventhslide::app::sound_device_rows(QStringLiteral("HDMI-1"), devices);
    REQUIRE(rows.size() == 4);
    // None first, then the device whose name shares a word with the screen.
    CHECK(rows[0].toMap().value(QStringLiteral("key")).toString().isEmpty());
    CHECK(rows[1].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("alsa_output.hdmi"));
    CHECK(rows[1].toMap().value(QStringLiteral("suggested")).toBool());
    // The rest keep the system's order.
    CHECK(rows[2].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("alsa_output.analog"));
    CHECK_FALSE(rows[2].toMap().value(QStringLiteral("suggested")).toBool());
    CHECK(rows[3].toMap().value(QStringLiteral("label")).toString()
          == QStringLiteral("Scarlett 2i2 3rd Gen"));
    // A screen with nothing in common leaves the order alone.
    const auto plain = seventhslide::app::sound_device_rows(QStringLiteral("DP-2"), devices);
    CHECK(plain[1].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("alsa_output.analog"));
}

TEST_CASE("OutputWindowHost destinations round-trip through their records", "[render][destinations]")
{
    seventhslide::test::ensure_qt_application();

    // What the owner stores is exactly what a host exports; loading it into
    // another host (the next launch, or switching back to a library) must give
    // the same destinations back.
    OutputWindowHost host;
    const QString a = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    const QString b = host.add_destination(QStringLiteral("Stage"), QStringLiteral("display"));
    REQUIRE_FALSE(a.isEmpty());
    REQUIRE_FALSE(b.isEmpty());
    REQUIRE(host.set_destination_name(b, QStringLiteral("Confidence")));
    REQUIRE(host.set_destination_ignore_live(b, true));

    OutputWindowHost reloaded;
    reloaded.replace_destinations(host.destination_records());
    REQUIRE(seventhslide::app::destination_rows(reloaded) == seventhslide::app::destination_rows(host));

    // Replacing drops what was there: one library's setup never lingers in
    // the next one's.
    reloaded.replace_destinations({});
    REQUIRE(seventhslide::app::destination_rows(reloaded).isEmpty());
}

TEST_CASE("Live off stops every destination but those that ignore it, and keeps them enabled",
          "[render][destinations][live]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    const QString out = QStringLiteral("out-main");
    const QString dest = seventhslide::test::open_on_first_display(host, out);
    const auto enabled = [&] { return host.registry().find(dest)->enabled; };
    REQUIRE(host.live());
    CHECK(host.live_off_stops_any());

    QSignalSpy switched(&host, &OutputWindowHost::liveChanged);
    host.set_live(false);
    CHECK(switched.count() == 1);
    host.set_live(false);
    CHECK(switched.count() == 1);
    CHECK_FALSE(host.is_open(out));
    CHECK(enabled());
    CHECK_FALSE(host.live_off_stops_any());
    // Nothing opens while it is off: not a restore, not an edit.
    host.restore();
    CHECK_FALSE(host.is_open(out));
    REQUIRE(host.set_destination_enabled(dest, true));
    CHECK_FALSE(host.is_open(out));

    // One that ignores Live starts at once, and stops when it follows it again.
    REQUIRE(host.set_destination_ignore_live(dest, true));
    CHECK(host.is_open(out));
    REQUIRE(host.set_destination_ignore_live(dest, false));
    CHECK_FALSE(host.is_open(out));
    CHECK(enabled());

    // On opens nothing by itself; the restore after it does.
    host.set_live(true);
    CHECK_FALSE(host.is_open(out));
    host.restore();
    CHECK(host.is_open(out));

    // Ignoring Live, it sends through Live off, and turning Live off would
    // stop nothing.
    REQUIRE(host.set_destination_ignore_live(dest, true));
    CHECK(host.is_open(out));
    CHECK_FALSE(host.live_off_stops_any());
    host.set_live(false);
    CHECK(host.is_open(out));
}

TEST_CASE("A display enabled while Live is off takes its screen from another output's",
          "[render][destinations][live]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    host.set_live(false);
    const QString screen = seventhslide::test::first_display_key(host);
    const QString main = host.add_destination(QStringLiteral("out-main"), QStringLiteral("display"));
    const QString stage = host.add_destination(QStringLiteral("Stage"), QStringLiteral("display"));
    for (const QString& id : {main, stage}) {
        REQUIRE(host.set_destination_screen(id, screen));
        REQUIRE(host.set_destination_enabled(id, true));
    }
    // The last one enabled has the screen; the other is switched off, as it
    // would have been with Live on.
    CHECK_FALSE(host.registry().find(main)->enabled);
    CHECK(host.registry().find(stage)->enabled);

    host.set_live(true);
    host.restore();
    CHECK(host.is_open(QStringLiteral("Stage")));
    CHECK_FALSE(host.is_open(QStringLiteral("out-main")));
}

TEST_CASE("OutputWindowHost lists every kind of destination from its record",
          "[render][destinations]")
{
    seventhslide::test::ensure_qt_application();

    // Records load whatever the hardware, so each kind's row can be checked
    // here without a card, an NDI runtime or a loopback device. None of these
    // targets exists.
    const auto record = [](const char* id, const char* output, const char* type, const char* key,
                           bool enabled) {
        return nlohmann::json{
            {"id", id}, {"output", output}, {"type", type}, {"key", key}, {"enabled", enabled}};
    };
    auto display = record("d", "main", "display", "serial:nope", false);
    display["label"] = "HDMI-1";
    display["bounds"] = "0,0,1920,1080";
    auto ndi = record("n", "main", "ndi", "Stage Left", false);
    ndi["alpha"] = true;
    ndi["audio"] = false;
    auto bmd = record("b", "main", "bmd", "dev-1", false);
    bmd["label"] = "DeckLink Mini";
    bmd["mode"] = "Hi59";
    auto vcam = record("v", "main", "vcam", "/dev/video99", false);
    vcam["label"] = "Cam";
    // Enabled on the screen "d" names, for another output: "d" reports it.
    auto rival = record("r", "stage", "display", "serial:nope", true);
    rival["name"] = "Foldback";
    // A desktop window, where it was left.
    auto window = record("w", "main", "window", "", false);
    window["bounds"] = "100,120,640,360";
    window["audio_device"] = "hdmi-2";

    // No such kind (a newer version's, or a hand edit): left out.
    const auto unknown = record("x", "main", "hologram", "k", true);

    OutputWindowHost host;
    host.set_output_names({{QStringLiteral("stage"), QStringLiteral("Stage")}});
    host.replace_destinations(nlohmann::json::array({display, ndi, bmd, vcam, rival, window, unknown}));

    const auto rows = seventhslide::app::destination_rows(host, QStringLiteral("main"));
    REQUIRE(rows.size() == 5);
    const auto d = rows[0].toMap();
    const auto n = rows[1].toMap();
    const auto b = rows[2].toMap();
    const auto v = rows[3].toMap();
    const auto w = rows[4].toMap();
    const auto keys = [](const QVariantMap& m) {
        auto list = m.keys();
        list.sort();
        return list;
    };
    // The fields every row has, plus ``extra``, sorted as ``keys`` sorts.
    const auto fields = [](const char* extra) {
        auto list = QStringLiteral("alpha,audio,audioCapable,closed,conflictOutput,enabled,id,ignoreLive,live,maxHeight,"
                                   "missing,name,output,screenKey,screenLabel,sizable,"
                                   "soundAloneCapable,targetKey,type,video")
                        .split(QLatin1Char(','));
        if (*extra != 0) {
            list += QString::fromUtf8(extra).split(QLatin1Char(','));
        }
        list.sort();
        return list;
    };

    REQUIRE(keys(d) == fields("audioDevice,soundPlaying"));
    REQUIRE(d.value(QStringLiteral("type")).toString() == QStringLiteral("display"));
    REQUIRE(d.value(QStringLiteral("name")).toString() == QStringLiteral("Display 1"));
    REQUIRE(d.value(QStringLiteral("screenLabel")).toString() == QStringLiteral("HDMI-1"));
    REQUIRE(d.value(QStringLiteral("missing")).toBool());
    REQUIRE(d.value(QStringLiteral("conflictOutput")).toString() == QStringLiteral("Stage"));
    // A record that does not say has its sound on.
    REQUIRE(d.value(QStringLiteral("audio")).toBool());

    REQUIRE(keys(n) == fields("onPreview,onProgram,receivers"));
    REQUIRE(n.value(QStringLiteral("name")).toString() == QStringLiteral("NDI 1"));
    REQUIRE(n.value(QStringLiteral("screenLabel")).toString() == QStringLiteral("Stage Left"));
    REQUIRE(n.value(QStringLiteral("alpha")).toBool());
    REQUIRE_FALSE(n.value(QStringLiteral("audio")).toBool());
    REQUIRE(n.value(QStringLiteral("missing")).toBool()
            == !seventhslide::render::ndi_runtime_available());
    REQUIRE(n.value(QStringLiteral("receivers")).toInt() == -1);
    REQUIRE_FALSE(n.value(QStringLiteral("onProgram")).toBool());
    REQUIRE(n.value(QStringLiteral("conflictOutput")).toString().isEmpty());

    REQUIRE(keys(b) == fields("driverPresent,keying,modeKey,modeName"));
    REQUIRE(b.value(QStringLiteral("name")).toString() == QStringLiteral("Blackmagic 1"));
    REQUIRE(b.value(QStringLiteral("screenLabel")).toString() == QStringLiteral("DeckLink Mini"));
    REQUIRE(b.value(QStringLiteral("missing")).toBool());
    REQUIRE(b.value(QStringLiteral("modeKey")).toString() == QStringLiteral("Hi59"));
    REQUIRE(b.value(QStringLiteral("modeName")).toString().isEmpty());
    REQUIRE_FALSE(b.value(QStringLiteral("keying")).toBool());
    REQUIRE(b.value(QStringLiteral("driverPresent")).toBool()
            == seventhslide::render::bmd_runtime_available());
    // Its sound is on, as a record that does not say has it; it can be turned
    // off with no card attached. A virtual camera's can be where PipeWire
    // gives it a microphone; a display's never (it has a sound device).
    REQUIRE(b.value(QStringLiteral("audio")).toBool());
    REQUIRE(host.set_destination_audio(QStringLiteral("b"), false));
    REQUIRE_FALSE(seventhslide::app::destination_rows(host, QStringLiteral("main"))[2]
                      .toMap().value(QStringLiteral("audio")).toBool());
    REQUIRE(host.set_destination_audio(QStringLiteral("b"), true));
    const bool vcam_sound =
        seventhslide::render::destination_kind_carries_audio(seventhslide::render::DestinationKind::Vcam);
    REQUIRE(host.set_destination_audio(QStringLiteral("v"), false) == vcam_sound);
    REQUIRE(seventhslide::app::destination_rows(host, QStringLiteral("main"))[3].toMap()
                .value(QStringLiteral("audioCapable")).toBool()
            == vcam_sound);
    REQUIRE(b.value(QStringLiteral("audioCapable")).toBool());
    REQUIRE_FALSE(d.value(QStringLiteral("audioCapable")).toBool());
    REQUIRE_FALSE(host.set_destination_audio(QStringLiteral("d"), false));

    REQUIRE(keys(v) == fields(""));
    REQUIRE(v.value(QStringLiteral("name")).toString() == QStringLiteral("Virtual Camera 1"));
    REQUIRE(v.value(QStringLiteral("screenLabel")).toString() == QStringLiteral("/dev/video99"));
    REQUIRE(v.value(QStringLiteral("missing")).toBool());

    // A window has its own sound device, as a display does, and nothing to
    // be missing.
    REQUIRE(keys(w) == fields("audioDevice,soundPlaying"));
    REQUIRE(w.value(QStringLiteral("name")).toString() == QStringLiteral("Window 1"));
    REQUIRE(w.value(QStringLiteral("type")).toString() == QStringLiteral("window"));
    REQUIRE(w.value(QStringLiteral("audioDevice")).toString() == QStringLiteral("hdmi-2"));
    REQUIRE_FALSE(w.value(QStringLiteral("missing")).toBool());
    REQUIRE(w.value(QStringLiteral("sizable")).toBool());
    REQUIRE_FALSE(w.value(QStringLiteral("audioCapable")).toBool());
    REQUIRE(host.registry().find(QStringLiteral("w"))->bounds == QStringLiteral("100,120,640,360"));

    // The rival names no conflict of its own: "d" is not enabled.
    const auto stage = seventhslide::app::destination_rows(host, QStringLiteral("stage"));
    REQUIRE(stage.size() == 1);
    REQUIRE(stage[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Foldback"));
    REQUIRE(stage[0].toMap().value(QStringLiteral("conflictOutput")).toString().isEmpty());

    // And every kind goes back out as it came in.
    OutputWindowHost reloaded;
    reloaded.set_output_names({{QStringLiteral("stage"), QStringLiteral("Stage")}});
    reloaded.replace_destinations(host.destination_records());
    REQUIRE(seventhslide::app::destination_rows(reloaded) == seventhslide::app::destination_rows(host));
}

// An audience window draws its output's shared presentation, as NDI, Blackmagic
// and virtual cameras do, so a window and an NDI feed of one output agree about
// whether a transition ran.
TEST_CASE("An audience window shows its output's presenter mid-transition", "[render][window][fade]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));
    auto* win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);

    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#ff0000";
    frame.background_transition = seventhslide::model::transition_from_fade(true, 500);
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));
    frame.background.color = "#0000ff";
    host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));

    seventhslide::render::ScenePresenter* presenter = win->presenter();
    REQUIRE(presenter != nullptr);
    REQUIRE(presenter->is_fading());
    presenter->set_fade_progress_for_test(0.5);
    win->presentation_changed();  // what the host does on every present

    // The presenter's worker paints it; the window shows it when it comes.
    const auto centre = [win] {
        const QImage shown = win->grabWindow();
        return shown.isNull() ? QRgb(0) : shown.pixel(shown.width() / 2, shown.height() / 2);
    };
    (void)QTest::qWaitFor([&] { return qRed(centre()) > 30 && qBlue(centre()) > 30; }, 3000);
    const QRgb mid = centre();
    INFO("centre " << qRed(mid) << " " << qGreen(mid) << " " << qBlue(mid));
    // Half way: both colours on screen. A window fading on its own clock
    // (still at the start) would show red only.
    CHECK(qRed(mid) > 30);
    CHECK(qBlue(mid) > 30);

    // The presenter finishing is the window finishing, whatever its own clock
    // would say (it would still be early in a 500 ms fade here).
    presenter->set_fade_progress_for_test(1.0);
    win->presentation_changed();
    (void)QTest::qWaitFor([&] { return qRed(centre()) < 30; }, 3000);
    const QRgb end = centre();
    INFO("settled centre " << qRed(end) << " " << qGreen(end) << " " << qBlue(end));
    CHECK(qBlue(end) > 150);
    CHECK(qRed(end) < 30);
}


// Closing an audience window from its own key handler used to delete the
// window while Qt was still delivering that key event to it; quitting the app
// the same way crashed once the operator window's close handler shut the host
// down mid-way through Qt closing every window.
TEST_CASE("Escape on an audience window closes it safely", "[render][window]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    const QString dest = seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));
    QPointer<seventhslide::render::OutputWindow> win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);

    QTest::keyClick(win.data(), Qt::Key_Escape);
    REQUIRE_FALSE(host.is_open(QStringLiteral("out-main")));
    REQUIRE(host.window(QStringLiteral("out-main")) == nullptr);
    // Deleted once the key event had been delivered, not during it.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    REQUIRE(win == nullptr);
    // The destination is only closed: still listed, and still on.
    const auto rows = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().toMap().value(QStringLiteral("enabled")).toBool());
    CHECK(rows.front().toMap().value(QStringLiteral("closed")).toBool());
    CHECK(host.is_closed(dest));
    // Switched on again (it already is), it opens.
    REQUIRE(host.set_destination_enabled(dest, true));
    REQUIRE(host.is_open(QStringLiteral("out-main")));
    CHECK_FALSE(host.is_closed(dest));
}

// Alt+F4 (or the system closing it) on an audience window: the window goes,
// and its destination stays on, as Escape leaves it.
TEST_CASE("Closing an audience window from outside leaves its destination on", "[render][window]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    const QString dest = seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));
    QPointer<seventhslide::render::OutputWindow> win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);

    // Refused there and then; the host takes it down on the next pass.
    CHECK_FALSE(win->close());
    REQUIRE(QTest::qWaitFor([&] { return !host.is_open(QStringLiteral("out-main")); }, 2000));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    REQUIRE(win == nullptr);
    const auto rows = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().toMap().value(QStringLiteral("enabled")).toBool());
    CHECK(host.is_closed(dest));
    // Switched off and on, it is back.
    REQUIRE(host.set_destination_enabled(dest, false));
    REQUIRE(host.set_destination_enabled(dest, true));
    REQUIRE(host.is_open(QStringLiteral("out-main")));
}

// A window closed stays closed through what reopens the others (a restore:
// a screen added, Live turned on for another reason), until Live goes off
// and on again; one that ignores Live waits for its own switch.
TEST_CASE("A window closed comes back with Live, unless it ignores Live", "[render][window][live]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    const QString out = QStringLiteral("out-main");
    const QString dest = seventhslide::test::open_on_first_display(host, out);
    QTest::keyClick(host.window(out), Qt::Key_Escape);
    REQUIRE_FALSE(host.is_open(out));

    host.restore();
    CHECK_FALSE(host.is_open(out));

    host.set_live(false);
    host.set_live(true);
    host.restore();
    CHECK(host.is_open(out));

    REQUIRE(host.set_destination_ignore_live(dest, true));
    QTest::keyClick(host.window(out), Qt::Key_Escape);
    REQUIRE_FALSE(host.is_open(out));
    host.set_live(false);
    host.set_live(true);
    host.restore();
    CHECK_FALSE(host.is_open(out));
    CHECK(host.is_closed(dest));

    // Its output switched off and on brings it back too.
    host.set_outputs_off({out});
    host.set_outputs_off({});
    host.restore();
    CHECK(host.is_open(out));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("A window destination opens on the desktop where it was left", "[render][window]")
{
    seventhslide::test::ensure_qt_application();
    REQUIRE_FALSE(QGuiApplication::screens().isEmpty());

    OutputWindowHost host;
    const QString out = QStringLiteral("out-main");
    const QString id = host.add_destination(out, QStringLiteral("window"));
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(host.registry().find(id)->name == QStringLiteral("Window 1"));
    REQUIRE(host.set_destination_enabled(id, true));
    QPointer<seventhslide::render::OutputWindow> win = host.window(out);
    REQUIRE(win != nullptr);
    CHECK(win->desktop());
    // A window claims no screen: a display destination can still take one.
    CHECK(host.open_screens().isEmpty());
    const auto rows = seventhslide::app::destination_rows(host, out);
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().toMap().value(QStringLiteral("live")).toBool());
    CHECK_FALSE(rows.front().toMap().value(QStringLiteral("missing")).toBool());

    // Moved and resized, its place is kept, and saved once it settles.
    QSignalSpy edited(&host, &OutputWindowHost::destinationsEdited);
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    const QRect place(avail.x() + 40, avail.y() + 50, 400, 225);
    win->setGeometry(place);
    REQUIRE(QTest::qWaitFor([&] { return edited.count() > 0; }, 3000));
    QString kept = host.registry().find(id)->bounds;
    CHECK_FALSE(kept.isEmpty());

    // Moved again just before its library closes: saved there and then.
    const int saves = edited.count();
    win->setGeometry(place.translated(30, 20));
    REQUIRE(QTest::qWaitFor([&] { return host.registry().find(id)->bounds != kept; }, 2000));
    host.flush_edits();
    CHECK(edited.count() == saves + 1);
    // Nothing left to announce.
    host.flush_edits();
    CHECK(edited.count() == saves + 1);
    kept = host.registry().find(id)->bounds;

    // Two windows of one output are fine: neither takes the other's place.
    const QString second = host.add_destination(out, QStringLiteral("window"));
    REQUIRE(host.set_destination_enabled(second, true));
    CHECK(host.registry().find(id)->enabled);

    // Closed, it stays on; reopened, it is where it was.
    win->close();
    REQUIRE(QTest::qWaitFor([&] { return host.is_closed(id); }, 2000));
    CHECK(host.registry().find(id)->enabled);
    REQUIRE(host.set_destination_enabled(id, true));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // Its place survives its record.
    OutputWindowHost again;
    again.replace_destinations(host.destination_records());
    CHECK(again.registry().find(id)->bounds == kept);
    CHECK(again.registry().find(id)->kind == seventhslide::render::DestinationKind::Window);
}

TEST_CASE("A desktop window opens where it was left while it can be reached", "[render][window]")
{
    using seventhslide::render::desktop_window_geometry;
    const QRect home(0, 0, 1920, 1080);
    const QList<QRect> screens{home, QRect(1920, 0, 1280, 1024)};

    // Kept, on either screen.
    CHECK(desktop_window_geometry(QStringLiteral("100,120,640,360"), {1920, 1080}, screens, home)
          == QRect(100, 120, 640, 360));
    CHECK(desktop_window_geometry(QStringLiteral("2000,40,640,360"), {1920, 1080}, screens, home)
          == QRect(2000, 40, 640, 360));
    // Off every screen (one unplugged), or nothing kept: a new one in the
    // middle of the operator's screen, in the canvas's shape.
    const QRect fresh(480, 270, 960, 540);
    CHECK(desktop_window_geometry(QStringLiteral("5000,40,640,360"), {1920, 1080}, screens, home)
          == fresh);
    CHECK(desktop_window_geometry(QString(), {1920, 1080}, screens, home) == fresh);
    CHECK(desktop_window_geometry(QStringLiteral("nonsense"), {1920, 1080}, screens, home) == fresh);
    // Only its bottom edge showing: its title bar is out of reach.
    CHECK(desktop_window_geometry(QStringLiteral("100,-340,640,360"), {1920, 1080}, screens, home)
          == fresh);
    // Left larger than the screen it is on now (sized on a 4K monitor): as
    // large as that screen, where it was.
    CHECK(desktop_window_geometry(QStringLiteral("2000,0,3840,2160"), {1920, 1080}, screens, home)
          == QRect(2000, 0, 1280, 1024));
    // A tall canvas stays within half the screen's height.
    const QRect tall = desktop_window_geometry(QString(), {1080, 1920}, screens, home);
    CHECK(tall.height() == 540);
    CHECK(tall.width() == 303);
}

// Quitting, Qt asks every window to close and stops quitting if one will
// not: the audience windows go first, and their destinations stay on, so
// they open again at the next start.
TEST_CASE("Quitting closes the audience windows and keeps their destinations on", "[render][window]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    (void)seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));
    REQUIRE(host.is_open(QStringLiteral("out-main")));

    QEvent quit(QEvent::Quit);
    // Only the filter's part: the test's application must not quit.
    REQUIRE_FALSE(host.eventFilter(QCoreApplication::instance(), &quit));
    CHECK_FALSE(host.is_open(QStringLiteral("out-main")));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto rows = seventhslide::app::destination_rows(host, QStringLiteral("out-main"));
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().toMap().value(QStringLiteral("enabled")).toBool());
}

// A window shows the presenter's surface as a texture, and the texture keeps
// its image until it is replaced. The presenter paints the next frame into a
// spare buffer rather than copying the held one: across many changed frames
// only two buffers are ever used.
TEST_CASE("A window's changed frames reuse the presenter's buffers", "[render][window]")
{
    seventhslide::test::ensure_qt_application();

    OutputWindowHost host;
    seventhslide::test::open_on_first_display(host, QStringLiteral("out-main"));
    auto* win = host.window(QStringLiteral("out-main"));
    REQUIRE(win != nullptr);

    SceneFrame frame;
    frame.output_id = "out-main";
    frame.mode = SceneMode::Song;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    frame.background.type = seventhslide::layout::BackgroundType::Image;  // bars stay black: one surface shape throughout

    std::set<const uchar*> buffers;
    for (int i = 0; i < 6; ++i) {
        frame.background.image = "/static/images/frame" + std::to_string(i) + ".png";
        host.present(QStringLiteral("out-main"), seventhslide::scene::share_frame(frame));
        (void)win->grabWindow();
        // Only the address is kept: holding the image would itself be a hold.
        buffers.insert(win->snapshot().constBits());
    }
    CHECK(buffers.size() <= 2);
}
