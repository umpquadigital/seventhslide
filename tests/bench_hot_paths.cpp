// Hot-path timings for a scripted show. Hidden from the default run:
//   ./seventhslide_tests_dev "[.bench]"
// Prints milliseconds per operation; asserts nothing about speed.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "canvas_test_helpers.hpp"
#include "library_controller.hpp"
#include "qvariant_json.hpp"
#include "facade_all.hpp"
#include "session_remote_bridge.hpp"
#include "scene_object_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "theme_test_helpers.hpp"
#include "test_qml.hpp"
#include "icon_image_provider.hpp"

#include <seventhslide/audio/audio_source.hpp>
#include <seventhslide/audio/mixer.hpp>
#include <seventhslide/build/bible_layout.hpp>
#include <seventhslide/crypto/password_box.hpp>
#include <seventhslide/crypto/signing.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/parse/bible.hpp>
#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/element_raster_cache.hpp>
#include <seventhslide/render/mpv_media_player.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/scene/text_metrics.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/stream/encoder_catalog.hpp>
#include <seventhslide/sync/change_file.hpp>
#include <seventhslide/sync/changed.hpp>
#include <seventhslide/sync/envelope.hpp>
#include <seventhslide/sync/identity.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/sync/sync_engine.hpp>
#include <seventhslide/sync/sync_store.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTime>
#include <QPainter>
#include <QImage>
#include <QMetaObject>
#include <QProcess>
#include <QSvgRenderer>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace CanvasAlignQml = seventhslide::app::CanvasAlignQml;
namespace CanvasDistributeQml = seventhslide::app::CanvasDistributeQml;
namespace CanvasFlipQml = seventhslide::app::CanvasFlipQml;
namespace CanvasOrderQml = seventhslide::app::CanvasOrderQml;

using seventhslide::app::SessionFacade;
using seventhslide::model::OutputConfig;
using seventhslide::model::Song;
using seventhslide::session::Session;

namespace {

template <typename Fn>
double ms_per_call(int iterations, Fn&& fn)
{
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        fn();
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / iterations;
}

void report(const char* what, double ms)
{
    std::printf("  %-44s %9.3f ms\n", what, ms);
}

/// For calls far under a millisecond.
void report_us(const char* what, double ms)
{
    std::printf("  %-44s %9.3f us\n", what, ms * 1000.0);
}

/// An output with a seeded theme library padded to ``themes`` text themes.
OutputConfig make_output(std::string name, int width, int height, int follow_lines, int themes)
{
    OutputConfig oc;
    oc.id = "out-" + name;
    oc.name = std::move(name);
    oc.canvas_width = width;
    oc.canvas_height = height;
    seventhslide::test::set_look(oc, {{"follow_lines", follow_lines}, {"font_family", "Liberation Sans"}, {"font_size", 64}, {"show_bible_text", true}});
    (void)seventhslide::theme::seed_default_themes(oc);
    const auto base = oc.song_themes.front();
    for (int i = 1; i < themes; ++i) {
        auto copy = base;
        copy.id = base.id + std::to_string(i);
        copy.name = "Theme " + std::to_string(i);
        oc.song_themes.push_back(std::move(copy));
    }
    return oc;
}

/// ``tag`` makes every line unique, so a run measures cold text (nothing the
/// width cache has seen). The same tag twice is the warm case.
Song long_song(int tag = 0)
{
    Song song;
    song.title = "A Mighty Fortress Is Our God";
    std::string lyrics;
    for (int v = 1; v <= 8; ++v) {
        lyrics += "---[Verse:" + std::to_string(v) + "]---\n";
        for (int l = 0; l < 6; ++l) {
            lyrics += "A mighty fortress is our God, a bulwark never failing line "
                      + std::to_string(l) + " " + std::to_string(tag) + "\n";
        }
    }
    song.lyrics = lyrics;
    return song;
}

seventhslide::model::BiblePassage long_passage()
{
    seventhslide::model::BiblePassage bible;
    bible.chapter = 3;
    for (int v = 1; v <= 20; ++v) {
        bible.verses.push_back({v,
                                "For God so loved the world, that he gave his only begotten "
                                "Son, that whosoever believeth in him should not perish."});
    }
    bible.book = "John";
    bible.verse_start = 1;
    bible.verse_end = 20;
    bible.ref = "John 3:1-20";
    return bible;
}

} // namespace

TEST_CASE("bench: live session hot paths", "[.bench]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    Session session(measurer);
    session.set_outputs({make_output("Main", 1920, 1080, 0, 20),
                         make_output("Stage", 1920, 1080, 3, 20),
                         make_output("Lower Third", 1920, 240, 0, 20)});

    auto stacks = session.stack_defs();
    const auto lobby = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(lobby.ok);
    session.set_stacks(stacks, session.type_defaults());
    const std::string main_stack(seventhslide::model::k_default_stack_id);

    std::printf("\nlive session (3 outputs, 2 stacks, 20 text themes per output)\n");
    int tag = 0;
    report("play_song, new song each time (cold text)",
           ms_per_call(10, [&] { session.play_song(long_song(++tag), main_stack); }));
    report("play_song, same song again (warm text)",
           ms_per_call(10, [&] { session.play_song(long_song(), main_stack); }));
    report("play_bible (20 verses, build)",
           ms_per_call(10, [&] { session.play_bible(long_passage(), lobby.id); }));
    session.set_focused_stack_id(main_stack);
    report("scene_frames() (all outputs)",
           ms_per_call(50, [&] { REQUIRE(session.scene_frames().size() == 3); }));
    // The first frames resolve each theme element's font,
    // once per process; this is what every later rebuild pays.
    report("scene_frames(), fonts already resolved",
           ms_per_call(50, [&] { REQUIRE(session.scene_frames().size() == 3); }));
    report("scene_frame(\"Main\")",
           ms_per_call(50, [&] { REQUIRE(session.scene_frame("out-Main").has_value()); }));
    report("next_slide + scene_frames()", ms_per_call(40, [&] {
               if (!session.focused_stack().program.next_slide()) {
                   (void)session.focused_stack().program.jump_to_line(0);
               }
               REQUIRE(session.scene_frames().size() == 3);
           }));
    report("switch Lower Third off and on (rebuild x2)", ms_per_call(5, [&] {
               REQUIRE(session.set_output_on("out-Lower Third", false));
               REQUIRE(session.set_output_on("out-Lower Third", true));
           }));

    // What every offscreen sink (NDI, virtual camera, Blackmagic) does for each
    // new frame of a background video: repaint the whole frame.
    auto lyrics = session.scene_frame("out-Main");
    REQUIRE(lyrics.has_value());
    lyrics->background.type = seventhslide::layout::BackgroundType::Video;
    lyrics->background.video = "/static/videos/motion.mp4";
    QImage motion(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    motion.fill(QColor(20, 40, 90));
    seventhslide::render::BackgroundImageCache images;
    seventhslide::render::SceneResources over_video;
    over_video.cache = &images;
    over_video.faces = &measurer.faces();
    over_video.background_video_frame = &motion;
    auto plain = over_video;
    plain.background_video_frame = nullptr;
    report("render lyrics frame over bg video (1080p)", ms_per_call(20, [&] {
               const QImage out = seventhslide::render::render_scene_frame_image(
                   *lyrics, 1920, 1080, QColor(Qt::black), over_video);
               REQUIRE(!out.isNull());
           }));
    auto still = *lyrics;
    still.background.type = seventhslide::layout::BackgroundType::Color;
    still.background.color = "#102040";
    report("render lyrics frame over color bg (1080p)", ms_per_call(20, [&] {
               const QImage out = seventhslide::render::render_scene_frame_image(
                   still, 1920, 1080, QColor(Qt::black), plain);
               REQUIRE(!out.isNull());
           }));
    auto motion_only = *lyrics;
    motion_only.text.reset();
    motion_only.overlay = {};
    motion_only.objects.clear();
    report("render bg video only (1080p)", ms_per_call(20, [&] {
               const QImage out = seventhslide::render::render_scene_frame_image(
                   motion_only, 1920, 1080, QColor(Qt::black), over_video);
               REQUIRE(!out.isNull());
           }));

    // The same frame mid cross-fade: what an offscreen sink renders for each of
    // the ~15 frames a 500 ms transition lasts. Two content passes instead of
    // one, so this is the ceiling on what the fade costs a sink - and it only
    // applies while a transition is actually running.
    const auto previous = still;
    seventhslide::render::ScenePresentation fading =
        seventhslide::render::ScenePresentation::still(still);
    fading.outgoing = &previous;
    fading.content_progress = 0.5;
    seventhslide::render::SceneResources fade_resources;
    fade_resources.cache = &images;
    fade_resources.faces = &measurer.faces();
    report("render lyrics frame mid cross-fade, no scratch (1080p)", ms_per_call(20, [&] {
               const QImage out = seventhslide::render::render_scene_presentation_image(
                   fading, 1920, 1080, QColor(Qt::black), fade_resources);
               REQUIRE(!out.isNull());
           }));

    // What a destination actually runs: the text and overlay are painted once
    // per transition and blended thereafter, so only the objects that carry
    // their own transitions are repainted per frame.
    seventhslide::render::SceneFadeLayers fade_layers;
    seventhslide::render::SceneResources with_scratch = fade_resources;
    with_scratch.fade_layers = &fade_layers;
    report("render lyrics frame mid cross-fade, cached (1080p)", ms_per_call(20, [&] {
               const QImage out = seventhslide::render::render_scene_presentation_image(
                   fading, 1920, 1080, QColor(Qt::black), with_scratch);
               REQUIRE(!out.isNull());
           }));
}

TEST_CASE("bench: text paint", "[.bench]")
{
    // What one output pays to draw text each frame: the lyrics box, a title
    // overlay that wraps, and a clock (whose text only exists at paint time).
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    Session session(measurer);
    session.set_outputs({make_output("Main", 1920, 1080, 0, 5)});
    session.play_song(long_song(), std::string(seventhslide::model::k_default_stack_id));
    // Past the title slide, onto a full verse.
    auto frame = session.scene_frame("out-Main");
    for (int i = 0; i < 10 && !(frame && frame->text && frame->text->lines.size() >= 4); ++i) {
        REQUIRE(session.focused_stack().program.next_slide());
        frame = session.scene_frame("out-Main");
    }
    REQUIRE(frame.has_value());
    REQUIRE(frame->text.has_value());
    REQUIRE(frame->text->lines.size() >= 4);

    seventhslide::layout::OverlayBox title;
    title.x = 160;
    title.y = 300;
    title.w = 1600;
    title.h = 480;
    title.font_family = "Liberation Sans";
    title.font_size = 96;
    title.font_color = "#ffffff";
    title.text_align = "center";
    title.vertical_align = "middle";
    title.lines.push_back(seventhslide::layout::styled_line(
        seventhslide::model::parse_inline_markup(
            "A Mighty Fortress Is Our God, a <b>Bulwark</b> Never Failing"),
        false, false, false));
    title.lines.push_back(seventhslide::layout::styled_line(
        seventhslide::model::parse_inline_markup("Martin Luther, 1529"), false, true, false));
    seventhslide::layout::OverlayScene overlay;
    overlay.boxes.push_back(title);

    seventhslide::canvas::Object clock = seventhslide::test::scene_clock();
    clock.x = 1500;
    clock.y = 40;
    clock.w = 380;
    clock.h = 90;
    clock.font_family = "Liberation Sans";
    clock.font_size = 64;
    const QDateTime noon(QDate(2026, 10, 4), QTime(12, 34, 56));

    QImage target(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    const auto* faces = &measurer.faces();
    std::printf("\ntext paint (1080p target)\n");
    report("lyrics box: paint_text_content", ms_per_call(200, [&] {
               QPainter p(&target);
               seventhslide::render::paint_text_content(p, *frame->text, {.faces = faces});
           }));
    report("title overlay: paint_overlay_box", ms_per_call(200, [&] {
               QPainter p(&target);
               for (const auto& box : overlay.boxes) {
                   seventhslide::render::paint_overlay_box(p, box, {.faces = faces});
               }
           }));
    report("clock: paint_clock_object", ms_per_call(200, [&] {
               QPainter p(&target);
               seventhslide::render::paint_clock_object(p, clock, {.faces = faces, .now = noon});
           }));
    report("lyrics box at preview scale 0.2", ms_per_call(200, [&] {
               QPainter p(&target);
               p.scale(0.2, 0.2);
               seventhslide::render::paint_text_content(p, *frame->text, {.faces = faces});
           }));
}

namespace {

/// A full slide the way a live service shows one: a scaled background picture,
/// 84 px lyrics, a 96 px title, a rotated outlined star, a photo object and a
/// clock. Every element is static between slide changes.
seventhslide::scene::SceneFrame rich_frame(seventhslide::layout::ITextMeasurer& measurer)
{
    namespace sc = seventhslide::scene;
    sc::SceneFrame frame;
    frame.output_name = "Main";
    frame.mode = sc::SceneMode::Song;
    frame.background.type = seventhslide::layout::BackgroundType::Image;
    frame.background.image = "/static/images/bg.png";

    sc::SceneTextContent text;
    text.box.x = 100;
    text.box.y = 160;
    text.box.width = 1720;
    text.box.height = 760;
    text.box.font_family = "Liberation Sans";
    text.box.font_size = 84;
    for (const char* line : {"A mighty fortress is our God,", "a bulwark never failing;",
                             "our helper he amid the flood", "of mortal ills prevailing."}) {
        sc::SceneTextLine l;
        l.runs = seventhslide::model::parse_inline_markup(line);
        text.lines.push_back(l);
    }
    sc::measure_text_content(text, measurer);
    frame.text = text;

    seventhslide::layout::OverlayBox title;
    title.x = 160;
    title.y = 20;
    title.w = 1600;
    title.h = 130;
    title.font_family = "Liberation Sans";
    title.font_size = 96;
    title.text_align = "center";
    title.lines.push_back(seventhslide::model::parse_inline_markup("<b>A Mighty Fortress</b>"));
    title.layout = seventhslide::layout::lay_out_overlay(title, measurer);
    frame.overlay.boxes.push_back(title);

    sc::SceneObject star;
    star.z = 1;
    star.object.emplace();
    star.object->type = seventhslide::canvas::ObjectType::Shape;
    star.object->shape_kind = seventhslide::geom::ShapeKind::Star;
    star.object->x = 1560;
    star.object->y = 760;
    star.object->w = 300;
    star.object->h = 300;
    star.object->rotation = 12;
    star.object->fill.color = "#c81e3c";
    star.object->line.color = "#ffffff";
    star.object->line.width = 8;
    frame.objects.push_back(star);

    sc::SceneObject photo;
    photo.z = 2;
    photo.object.emplace();
    photo.object->type = seventhslide::canvas::ObjectType::Image;
    photo.object->filename = "photo.png";
    photo.object->x = 60;
    photo.object->y = 760;
    photo.object->w = 450;
    photo.object->h = 300;
    frame.objects.push_back(photo);

    sc::SceneObject clock;
    clock.z = 3;
    clock.object.emplace();
    clock.object->type = seventhslide::canvas::ObjectType::Clock;
    clock.object->x = 1600;
    clock.object->y = 960;
    clock.object->w = 300;
    clock.object->h = 100;
    clock.object->font_family = "Liberation Sans";
    clock.object->font_size = 72;
    frame.objects.push_back(clock);
    return frame;
}

QImage test_pattern(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.setPixel(x, y, qRgb((x * 7) & 255, (y * 3) & 255, (x ^ y) & 255));
        }
    }
    return img;
}

}  // namespace

TEST_CASE("bench: effects paint", "[.bench]")
{
    // What a shadow, a glow and a text shadow cost at 1080p: once when the
    // element is drawn into its raster, and per composite while it is copied.
    seventhslide::test::ensure_qt_application();
    namespace sc = seventhslide::scene;
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::layout::QtTextMeasurer measurer(fonts);

    sc::SceneFrame frame;
    frame.mode = sc::SceneMode::Freeform;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#102030";
    sc::SceneObject panel;
    panel.z = 0;
    panel.id = "panel";
    panel.object.emplace();
    panel.object->type = seventhslide::canvas::ObjectType::Shape;
    panel.object->shape_kind = seventhslide::geom::ShapeKind::RoundedRect;
    panel.object->x = 160;
    panel.object->y = 120;
    panel.object->w = 1600;
    panel.object->h = 840;
    panel.object->fill.color = "#c0ffffff";
    panel.object->line.color = "#ffffff";
    panel.object->line.width = 6;
    frame.objects.push_back(panel);

    seventhslide::layout::OverlayBox words;
    words.x = 200;
    words.y = 200;
    words.w = 1520;
    words.h = 680;
    words.font_family = "Liberation Sans";
    words.font_size = 96;
    words.font_color = "#102030";
    words.lines.push_back(seventhslide::model::parse_inline_markup("A mighty fortress is our God,"));
    words.lines.push_back(seventhslide::model::parse_inline_markup("a bulwark never failing"));
    sc::SceneObject text;
    text.z = 1;
    text.id = "words";
    text.text = words;
    frame.objects.push_back(text);

    seventhslide::render::SceneResources res;
    res.faces = &measurer.faces();
    QImage target;
    const auto draw_once = [&] {
        seventhslide::render::ElementRasterCache fresh;
        res.rasters = &fresh;
        seventhslide::render::render_scene_presentation_into(
            target, seventhslide::render::ScenePresentation::still(frame), 1920, 1080,
            QColor(Qt::black), res);
    };
    std::printf("\neffects paint (1080p, a panel and two lines of words)\n");
    report("no effects, drawn once", ms_per_call(10, draw_once));
    frame.objects[0].object->effects.shadow.on = true;
    frame.objects[0].object->effects.shadow.blur = 16;
    frame.objects[0].object->effects.shadow.distance = 12;
    report("panel shadow (blur 16), drawn once", ms_per_call(10, draw_once));
    frame.objects[0].object->effects.glow.on = true;
    frame.objects[0].object->effects.glow.size = 20;
    report("panel shadow and glow, drawn once", ms_per_call(10, draw_once));
    frame.objects[1].text->effects.text_shadow.on = true;
    frame.objects[1].text->effects.text_shadow.blur = 8;
    frame.objects[1].text->effects.text_outline.width = 3;
    report("plus words outlined with a text shadow, drawn once", ms_per_call(10, draw_once));
    seventhslide::render::ElementRasterCache warm;
    res.rasters = &warm;
    const auto copy = [&] {
        seventhslide::render::render_scene_presentation_into(
            target, seventhslide::render::ScenePresentation::still(frame), 1920, 1080,
            QColor(Qt::black), res);
    };
    copy();
    report("the same, retained (per composite)", ms_per_call(40, copy));
    res.rasters = nullptr;
    report("the same, every element live (a fade without layers)", ms_per_call(5, copy));
}

TEST_CASE("bench: rich frame composite", "[.bench]")
{
    // What one output pays per tick while a background video plays (or a
    // clock runs): the whole slide composited again.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    const seventhslide::test::TempDir root;
    std::filesystem::create_directories(root.path / "images");
    REQUIRE(test_pattern(1280, 720).save(QString::fromStdString((root.path / "images" / "bg.png").string())));
    REQUIRE(test_pattern(1600, 1067).save(QString::fromStdString((root.path / "images" / "photo.png").string())));
    seventhslide::render::BackgroundImageCache images;
    images.set_static_root(root.path);

    const auto frame = rich_frame(measurer);
    auto video = frame;
    video.background.type = seventhslide::layout::BackgroundType::Video;
    video.background.video = "/static/videos/motion.mp4";
    const QImage motion = test_pattern(1920, 1080);

    seventhslide::render::SceneResources res;
    res.cache = &images;
    res.faces = &measurer.faces();
    res.now = QDateTime(QDate(2026, 10, 5), QTime(10, 30, 0));
    QImage target;
    const auto still = seventhslide::render::ScenePresentation::still(frame);
    const auto over_video = seventhslide::render::ScenePresentation::still(video);

    std::printf("\nrich frame composite (1080p)\n");
    report("picture background, every element live", ms_per_call(40, [&] {
               seventhslide::render::render_scene_presentation_into(target, still, 1920, 1080,
                                                                    QColor(Qt::black), res);
           }));
    res.background_video_frame = &motion;
    report("video background, every element live", ms_per_call(40, [&] {
               seventhslide::render::render_scene_presentation_into(target, over_video, 1920, 1080,
                                                                    QColor(Qt::black), res);
           }));
    res.background_video_frame = nullptr;
    report("picture background at 1366x768 (a window)", ms_per_call(40, [&] {
               seventhslide::render::render_scene_presentation_into(target, still, 1366, 768,
                                                                    QColor(Qt::black), res);
           }));

    // The same composites with retained rasters, as every live destination
    // runs them: the slide was drawn once already, so each tick copies it.
    seventhslide::render::ElementRasterCache rasters;
    res.rasters = &rasters;
    report("picture background, retained", ms_per_call(40, [&] {
               seventhslide::render::render_scene_presentation_into(target, still, 1920, 1080,
                                                                    QColor(Qt::black), res);
           }));
    res.background_video_frame = &motion;
    report("video background, retained", ms_per_call(40, [&] {
               seventhslide::render::render_scene_presentation_into(target, over_video, 1920, 1080,
                                                                    QColor(Qt::black), res);
           }));
    res.background_video_frame = nullptr;
    report("picture background at 1366x768, retained", ms_per_call(40, [&] {
               seventhslide::render::render_scene_presentation_into(target, still, 1366, 768,
                                                                    QColor(Qt::black), res);
           }));
    std::printf("  retained rasters: %zu, %.1f MB (1080p + 1366x768 shapes)\n", rasters.size(),
                static_cast<double>(rasters.bytes()) / (1024.0 * 1024.0));
    // A new slide: everything drawn once, then copied.
    report("first composite of a new slide (draws all)", ms_per_call(10, [&] {
               seventhslide::render::ElementRasterCache fresh;
               res.rasters = &fresh;
               seventhslide::render::render_scene_presentation_into(target, still, 1920, 1080,
                                                                    QColor(Qt::black), res);
           }));
    res.rasters = nullptr;
}

TEST_CASE("bench: output preview paint", "[.bench]")
{
    // What an output card's preview costs per frame while a background video
    // plays and a clock runs. It draws from the output's ScenePresenter at the
    // card's size, beside the audience's 1080p shape, sharing the presenter's
    // decoded images and rasters; scaling the 1080p composite down is what
    // it could do instead. A card is about 320x180; 640x360 is the same card
    // on a 2x screen.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    const seventhslide::test::TempDir root;
    std::filesystem::create_directories(root.path / "images");
    REQUIRE(test_pattern(1280, 720).save(QString::fromStdString((root.path / "images" / "bg.png").string())));
    REQUIRE(test_pattern(1600, 1067).save(QString::fromStdString((root.path / "images" / "photo.png").string())));

    auto video = rich_frame(measurer);
    video.background.type = seventhslide::layout::BackgroundType::Video;
    video.background.video = "/static/videos/motion.mp4";
    // Two video frames taken in turn, so every frame is a new picture.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    const QImage motion[2] = {test_pattern(1920, 1080), test_pattern(1920, 1080).flipped(Qt::Vertical)};
#else
    const QImage motion[2] = {test_pattern(1920, 1080), test_pattern(1920, 1080).mirrored()};
#endif
    int tick = 0;

    seventhslide::render::ScenePresenter presenter;
    presenter.set_static_root(root.path);
    presenter.set_font_faces(&measurer.faces());
    presenter.set_background_video_frame(motion[0]);
    presenter.present(seventhslide::scene::share_frame(video));
    const QColor black(Qt::black);
    const QColor clear(Qt::transparent);
    (void)presenter.surface(QSize(1920, 1080), black);

    std::printf("\noutput preview paint, video background and a clock (per frame)\n");
    report("the audience's 1080p composite", ms_per_call(40, [&] {
               presenter.set_background_video_frame(motion[++tick % 2]);
               (void)presenter.surface(QSize(1920, 1080), black);
           }));
    for (const QSize card : {QSize(320, 180), QSize(640, 360)}) {
        const std::string size = std::to_string(card.width()) + "x" + std::to_string(card.height());
        (void)presenter.surface(card, clear);
        report(("the preview at " + size + ", from the presenter").c_str(), ms_per_call(200, [&] {
                   presenter.set_background_video_frame(motion[++tick % 2]);
                   (void)presenter.surface(card, clear);
               }));
        const QImage& composite = presenter.surface(QSize(1920, 1080), black);
        QImage target(card, QImage::Format_ARGB32_Premultiplied);
        const QRectF viewport(QPointF(0, 0), QSizeF(card));
        report(("1080p composite scaled to " + size + ", smooth").c_str(), ms_per_call(200, [&] {
                   QPainter p(&target);
                   p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                   p.drawImage(viewport, composite);
               }));
    }
}

TEST_CASE("bench: video playback frame", "[.bench]")
{
    // What the window's thread does for each new frame while a
    // theme's 1080p background video plays behind lyrics on two outputs (the
    // audience's and a stage monitor), each with its card's preview in the
    // operator window. The playback tick reads the frame from mpv into a
    // QImage (mpv decodes on its own threads), then each output window, as it
    // polishes, and each card composites the slide again. Uploading the
    // textures is the scene graph's render thread's, and is not counted.
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir root;
    const auto clip = root.path / "motion.mp4";
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
                 {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
                  QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("testsrc2=size=1920x1080:rate=30"), QStringLiteral("-t"),
                  QStringLiteral("6"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
                  QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-pix_fmt"),
                  QStringLiteral("yuv420p"), QString::fromStdString(clip.string())});
    if (!ffmpeg.waitForFinished(120000) || ffmpeg.exitCode() != 0) {
        SKIP("needs the ffmpeg command to make a 1080p clip");
    }

    // The frame read, as the tick polls: what the window's thread pays for
    // it. mpv renders on a thread of the player's own, so this is the taking
    // of a finished frame. A video starting is timed by every call until its
    // fifth new frame (the tick calls whether or not one came); after that,
    // by the calls that brought a new frame.
    seventhslide::render::MpvMediaPlayer player;
    std::vector<QImage> frames;
    QElapsedTimer running;
    struct Played {
        double slowest_starting = 0;
        double starting = 0;  // every call's time, added up
        std::vector<double> reads;  // after starting
    };
    const auto play = [&](std::size_t count) {
        REQUIRE(player.load(clip.string(), true));
        player.play();
        Played played;
        std::size_t new_frames = 0;
        qint64 last = 0;
        running.start();
        while (running.elapsed() < 5000 && new_frames < count) {
            QCoreApplication::processEvents();
            const auto start = std::chrono::steady_clock::now();
            QImage frame = player.current_frame();
            const auto ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - start)
                                .count();
            const bool fresh = !frame.isNull() && frame.cacheKey() != last;
            if (new_frames < 5) {
                played.slowest_starting = std::max(played.slowest_starting, ms);
                played.starting += ms;
            } else if (fresh) {
                played.reads.push_back(ms);
            }
            if (fresh) {
                last = frame.cacheKey();
                ++new_frames;
                if (frames.size() < 8) {
                    frames.push_back(frame);
                }
            }
            QThread::msleep(10);
        }
        return played;
    };
    const auto first = play(90);
    const double seconds = static_cast<double>(running.elapsed()) / 1000.0;
    REQUIRE(first.reads.size() >= 10);
    REQUIRE(frames.front().size() == QSize(1920, 1080));
    // Loaded again into the same player, as the next theme's video is.
    player.unload();
    const auto again = play(5);
    // Going round: the clip is 6 s and loops; every call for 7 s, as the
    // tick makes them.
    double slowest_round = 0.0;
    running.restart();
    while (running.elapsed() < 7000) {
        QCoreApplication::processEvents();
        const auto start = std::chrono::steady_clock::now();
        (void)player.current_frame();
        (void)player.position_sec();
        (void)player.clock_running();
        slowest_round = std::max(slowest_round, std::chrono::duration<double, std::milli>(
                                                    std::chrono::steady_clock::now() - start)
                                                    .count());
        QThread::msleep(10);
    }

    auto reads = first.reads;
    std::ranges::sort(reads);
    const auto slower_than = [&](double ms) {
        return std::ranges::count_if(reads, [&](double r) { return r > ms; });
    };
    const double read_mean = std::accumulate(reads.begin(), reads.end(), 0.0)
                             / static_cast<double>(reads.size());

    // The slide each output shows, over the video.
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    Session session(measurer);
    session.set_outputs({make_output("Main", 1920, 1080, 0, 1), make_output("Stage", 1920, 1080, 2, 1)});
    session.play_song(long_song(), std::string(seventhslide::model::k_default_stack_id));
    const QColor black(Qt::black);
    const QColor clear(Qt::transparent);
    std::vector<std::unique_ptr<seventhslide::render::ScenePresenter>> outputs;
    for (const char* id : {"out-Main", "out-Stage"}) {
        auto frame = session.scene_frame(id);
        REQUIRE(frame.has_value());
        frame->background.type = seventhslide::layout::BackgroundType::Video;
        frame->background.video = "/static/videos/motion.mp4";
        auto presenter = std::make_unique<seventhslide::render::ScenePresenter>();
        presenter->set_static_root(root.path);
        presenter->set_font_faces(&measurer.faces());
        presenter->set_background_video_frame(frames[0]);
        presenter->present(seventhslide::scene::share_frame(std::move(*frame)));
        (void)presenter->surface(QSize(1920, 1080), black);
        outputs.push_back(std::move(presenter));
    }
    std::size_t tick = 0;
    // One new frame on every output, composited at each size shown.
    const auto one_frame = [&](std::initializer_list<QSize> sizes) {
        const QImage& frame = frames[++tick % frames.size()];
        for (auto& presenter : outputs) {
            presenter->set_background_video_frame(frame);
            for (const QSize size : sizes) {
                (void)presenter->surface(size, size == QSize(1920, 1080) ? black : clear);
            }
        }
    };

    std::printf("\nvideo playback frame (1080p30 video behind lyrics, 2 outputs)\n");
    std::printf("  (%zu new frames read in %.1f s: %.1f a second)\n", reads.size() + 5, seconds,
                static_cast<double>(reads.size() + 5) / seconds);
    report("a video starting: its slowest call", first.slowest_starting);
    report("a video starting: all its calls", first.starting);
    report("loaded again: its slowest call", again.slowest_starting);
    report("loaded again: all its calls", again.starting);
    report("going round a loop: the slowest tick", slowest_round);
    report("read the frame from mpv after, mean", read_mean);
    report("read the frame from mpv after, median", reads[reads.size() / 2]);
    report("read the frame from mpv after, slowest", reads.back());
    std::printf("  reads after over 16.7 ms: %td, over 33.3 ms: %td\n", slower_than(1000.0 / 60.0),
                slower_than(1000.0 / 30.0));
    const double outputs_only = ms_per_call(40, [&] { one_frame({QSize(1920, 1080)}); });
    report("composite both outputs at 1080p", outputs_only);
    const double with_cards =
        ms_per_call(40, [&] { one_frame({QSize(1920, 1080), QSize(320, 180)}); });
    report("both outputs and their cards (320x180)", with_cards);
    const double with_2x_cards =
        ms_per_call(40, [&] { one_frame({QSize(1920, 1080), QSize(640, 360)}); });
    report("both outputs and their cards (640x360, 2x)", with_2x_cards);
    const double total = read_mean + with_cards;
    report("composited on the window's thread (read + all, waiting)", total);
    std::printf("  that is %.0f%% of a 30 fps frame (33.3 ms), %.0f%% of a 60 fps one\n",
                total / (1000.0 / 30.0) * 100.0, total / (1000.0 / 60.0) * 100.0);

    // As the windows and cards ask now (RENDER-1): each presenter's worker
    // composites, and the window's thread only snapshots and asks. Timed:
    // what asking costs that thread, and how long until every picture is in.
    QObject receiver;
    double asking = 0.0;
    double arriving = 0.0;
    constexpr int k_frames = 40;
    for (int i = 0; i < k_frames; ++i) {
        const QImage& frame = frames[++tick % frames.size()];
        int waiting = 0;
        const auto start = std::chrono::steady_clock::now();
        for (auto& presenter : outputs) {
            presenter->set_background_video_frame(frame);
            for (const QSize size : {QSize(1920, 1080), QSize(320, 180)}) {
                ++waiting;
                presenter->request(size, size == QSize(1920, 1080) ? black : clear, &receiver,
                                   [&waiting](QImage) { --waiting; });
            }
        }
        asking += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                      .count();
        while (waiting > 0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        arriving += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
    }
    report("asked of the workers: the window's thread per frame", read_mean + asking / k_frames);
    report("asked of the workers: until every picture is in", arriving / k_frames);
}

TEST_CASE("bench: background image cache", "[.bench]")
{
    // Every picture element and background asks this cache on each draw it
    // is not retained for, from the GUI and render threads.
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir root;
    std::filesystem::create_directories(root.path / "images");
    REQUIRE(test_pattern(1600, 1067).save(QString::fromStdString((root.path / "images" / "photo.png").string())));
    seventhslide::render::BackgroundImageCache images;
    images.set_static_root(root.path);
    REQUIRE(!images.get("/static/images/photo.png").isNull());

    std::printf("\nbackground image cache\n");
    report_us("hit", ms_per_call(20000, [&] {
                  (void)images.get("/static/images/photo.png");
              }));
    report_us("hit on a larger decode of the same file", ms_per_call(20000, [&] {
                  (void)images.get("/static/images/photo.png", 800);
              }));
    report("cold decode (1600x1067 PNG)", ms_per_call(10, [&] {
               images.clear();
               (void)images.get("/static/images/photo.png");
           }));
}

TEST_CASE("bench: remote", "[.bench]")
{
    // What the web remote costs the GUI thread while a client is connected:
    // each broad sessionChanged marks every section, and each is built and
    // serialized to find out whether it changed. A new frame's preview
    // thumbnail is rendered and PNG-encoded inside the HTTP request.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main", 1920, 1080, 0, 4), make_output("Stage", 1920, 1080, 2, 4),
                        make_output("Stream", 1280, 720, 0, 4)});
    for (int i = 0; i < 20; ++i) {
        Song song = long_song(i);
        (void)facade.song_repo()->insert(song);
    }
    facade.library()->refresh_songs();
    const auto service = facade.library()->createService(QStringLiteral("Sunday"));
    REQUIRE(facade.library()->openService(service));
    for (qint64 id = 1; id <= 20; ++id) {
        REQUIRE(facade.library()->addSongToService(id));
    }
    REQUIRE(facade.live()->selectServiceItem(3));

    seventhslide::app::SessionRemoteBridge bridge(facade);
    seventhslide::remote::View view;
    bridge.settle(view);
    std::printf("\nremote (20-item service, 3 outputs, a live song)\n");
    for (const auto s : seventhslide::remote::all_sections()) {
        const std::string label = "section " + std::string(seventhslide::remote::section_name(s))
                                  + ": build + dump";
        report_us(label.c_str(), ms_per_call(200, [&] { (void)bridge.section(s, view).dump(); }));
    }
    report("every section (one sessionChanged)", ms_per_call(200, [&] {
               for (const auto s : seventhslide::remote::all_sections()) {
                   (void)bridge.section(s, view).dump();
               }
           }));
    for (const int width : {320, 640}) {
        seventhslide::remote::ThumbRequest r;
        r.kind = "preview";
        r.name = "out-Main";
        r.width = width;
        // The server runs the render on the GUI thread and the encode on a
        // worker; only the first is GUI-thread time.
        const std::string render = "preview thumb " + std::to_string(width) + ": render (GUI)";
        report(render.c_str(), ms_per_call(20, [&] { REQUIRE(bridge.thumb(r).encode); }));
        const auto pending = bridge.thumb(r);
        const std::string encode = "preview thumb " + std::to_string(width) + ": encode (worker)";
        report(encode.c_str(), ms_per_call(20, [&] { REQUIRE(pending.encode().ok); }));
    }
}

TEST_CASE("bench: preview text across a service", "[.bench]")
{
    // Below half scale, text is filled from glyph outlines cached per run
    // (text_painter.cpp glyph_outline), and the cache empties itself past 512
    // runs. Walk a long service at preview scale the way an operator does and
    // compare against a cache that is always warm, to see whether the
    // emptying costs anything worth an LRU.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    Session session(measurer);
    session.set_outputs({make_output("Main", 1920, 1080, 0, 5)});

    std::vector<seventhslide::scene::SceneTextContent> slides;
    for (int tag = 0; tag < 60; ++tag) {
        Song song;
        song.title = "Song " + std::to_string(tag);
        for (int v = 1; v <= 6; ++v) {
            song.lyrics += "---[Verse:" + std::to_string(v) + "]---\n";
            for (int l = 0; l < 4; ++l) {
                song.lyrics += "Song " + std::to_string(tag) + " verse " + std::to_string(v)
                               + " line " + std::to_string(l) + " of a long service\n";
            }
        }
        session.play_song(song, std::string(seventhslide::model::k_default_stack_id));
        for (int guard = 0; guard < 40; ++guard) {
            const auto frame = session.scene_frame("out-Main");
            if (frame && frame->text && !frame->text->lines.empty()) {
                slides.push_back(*frame->text);
            }
            if (!session.focused_stack().program.next_slide()) {
                break;
            }
        }
    }
    std::size_t runs = 0;
    for (const auto& slide : slides) {
        runs += slide.lines.size();
    }
    REQUIRE(runs > 1024);  // well past the cache's 512

    QImage target(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    const auto* faces = &measurer.faces();
    const auto paint = [&](const seventhslide::scene::SceneTextContent& text) {
        QPainter p(&target);
        p.scale(0.2, 0.2);
        seventhslide::render::paint_text_content(p, text, {.faces = faces});
    };
    std::printf("\npreview text across a service (%zu slides, %zu line runs, scale 0.2)\n",
                slides.size(), runs);
    // A full verse from mid-service (title slides carry one line).
    std::size_t mid = slides.size() / 2;
    while (mid < slides.size() && slides[mid].lines.size() < 4) {
        ++mid;
    }
    REQUIRE(mid < slides.size());
    const auto& verse = slides[mid];
    paint(verse);
    report("a verse, warm (repainted)", ms_per_call(200, [&] { paint(verse); }));
    std::size_t i = 0;
    report("first sight of each slide (cold)", ms_per_call(static_cast<int>(slides.size()), [&] {
               paint(slides[i++]);
           }));
    // "Next" then "previous": the previous slide was painted a moment ago, so
    // any cost above warm is the cache having emptied in between.
    double back_ms = 0;
    for (std::size_t k = 1; k < slides.size(); ++k) {
        paint(slides[k]);
        const auto t0 = std::chrono::steady_clock::now();
        paint(slides[k - 1]);
        back_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                       .count();
    }
    report("step back to the previous slide", back_ms / static_cast<double>(slides.size() - 1));
}

TEST_CASE("bench: bible layout", "[.bench]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);

    OutputConfig oc;
    oc.name = "Main";
    seventhslide::test::set_look(oc, {{"bible_main_font_family", "Liberation Sans"}, {"bible_main_font_size", 56}, {"show_bible_verse_numbers", true}});

    // ``tag`` makes every verse unique, so a run measures text the width cache
    // has not seen. The same tag twice is the warm case.
    const auto passage = [](int tag) {
        std::vector<seventhslide::build::BibleVerseInput> verses;
        for (int v = 1; v <= 176; ++v) {
            verses.push_back({v,
                              "Blessed are the undefiled in the way, who walk in the law of "
                              "the LORD, and seek him with the whole heart "
                                  + std::to_string(tag) + "-" + std::to_string(v) + "."});
        }
        return verses;
    };
    const auto layout = [&](const std::vector<seventhslide::build::BibleVerseInput>& verses) {
        seventhslide::theme::ThemeSelection sel;
        sel.kind = seventhslide::model::ContentKind::Bible;
        const auto theme = seventhslide::theme::resolve_theme(oc, sel);
        const std::vector<const seventhslide::theme::ResolvedTheme*> themes{&theme};
        const auto plan = seventhslide::build::plan_bible_chunks(119, verses, themes, measurer);
        REQUIRE_FALSE(
            seventhslide::build::build_bible_slides_for_output(theme, plan, measurer).slides.empty());
    };

    std::printf("\nbible layout (176 verses, one output, chunk + pack)\n");
    int tag = 0;
    const auto warm = passage(tag);
    for (const bool fluid : {false, true}) {
        seventhslide::test::set_look(oc, {{"bible_fluid_verses", fluid}});
        layout(warm);
        report(fluid ? "run-on verses, new text each time" : "verse per slide, new text each time",
               ms_per_call(5, [&] { layout(passage(++tag)); }));
        report(fluid ? "run-on verses, same text again" : "verse per slide, same text again",
               ms_per_call(20, [&] { layout(warm); }));
    }
}

TEST_CASE("bench: library refresh", "[.bench]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    for (int i = 0; i < 300; ++i) {
        seventhslide::model::Song song;
        song.title = "Song " + std::to_string(i);
        song.lyrics = long_song().lyrics;
        (void)facade.song_repo()->insert(song);
    }
    using seventhslide::db::MediaKind;
    std::vector<std::int64_t> folders;
    for (int f = 0; f < 20; ++f) {
        folders.push_back(
            facade.media_repo()->folders(MediaKind::Image).create("Folder " + std::to_string(f)));
    }
    for (int i = 0; i < 300; ++i) {
        const auto name = "img" + std::to_string(i) + ".png";
        facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, name, name);
        (void)facade.media_repo()->add_folder_item(MediaKind::Image,
                                                   folders[static_cast<std::size_t>(i % 20)], name);
    }
    for (int i = 0; i < 60; ++i) {
        seventhslide::model::FreeformItem item;
        item.name = "Card " + std::to_string(i);
        item.document = seventhslide::freeform::make_document(item.name, {"Main"}).to_json();
        (void)facade.freeform_repo()->create_item(item);
    }

    std::printf("\nlibrary (300 songs, 300 images in 20 folders, 60 Freeform items)\n");
    report("refreshLibrary()", ms_per_call(10, [&] { facade.refresh_library(); }));
    report("refreshSongs()", ms_per_call(10, [&] { facade.library()->refresh_songs(); }));
    const QStringList queries{QStringLiteral("s"), QStringLiteral("so"), QStringLiteral("son"),
                              QStringLiteral("song 1"), QString()};
    int keystroke = 0;
    report("song search keystroke", ms_per_call(50, [&] {
               facade.library()->songs()->setFilter(queries[keystroke++ % queries.size()]);
           }));
}

TEST_CASE("bench: window thread load", "[.bench]")
{
    // What used to run on the window's thread (or hold the library's write
    // lock), beside what does now. Run alone, so the encoder probe is the
    // process's first. The composition a change no longer repeats is
    // "scene_frames() (all outputs)" in the live session bench.
    seventhslide::test::ensure_qt_application();
    std::printf("\nwindow thread load\n");

    // Encoders: the first look opens every candidate.
    {
        const auto start = std::chrono::steady_clock::now();
        seventhslide::stream::probe_encoders();
        const auto end = std::chrono::steady_clock::now();
        report("first encoder probe (was on the window's)",
               std::chrono::duration<double, std::milli>(end - start).count());
    }

    // Songs: a large church library's worth.
    {
        SessionFacade facade;
        REQUIRE(facade.open_memory_database());
        for (int i = 0; i < 3000; ++i) {
            Song song;
            song.title = "Song " + std::to_string(i);
            song.lyrics = long_song().lyrics;
            (void)facade.song_repo()->insert(song);
        }
        facade.library()->refresh_songs();
        auto* songs = facade.library()->songs();
        const auto first = songs->data(songs->index(0), seventhslide::app::SongListModel::SongIdRole)
                               .toLongLong();
        const auto lyrics = QString::fromStdString(long_song().lyrics);
        int edit = 0;
        const auto save = [&] {
            REQUIRE(facade.library()
                        ->updateSong(first, QStringLiteral("Edited %1").arg(edit++), lyrics, {}, {})
                        .isEmpty());
        };
        std::printf("  (3000 songs)\n");
        report("save a song, then read every song (before)", ms_per_call(5, [&] {
                   save();
                   facade.library()->refresh_songs();
               }));
        report("save a song (now)", ms_per_call(5, save));
        report("a sync of one setting: every list (before)",
               ms_per_call(5, [&] { facade.refresh_library(); }));
        seventhslide::sync::Changed one_setting;
        one_setting.add("app_settings", "ccli_licence_number");
        report("a sync of one setting (now)",
               ms_per_call(5, [&] { facade.reload_after_sync(one_setting); }));
    }

    const seventhslide::sync::Identity me{"church", "Church PC",
                                          seventhslide::crypto::SigningKey::generate()};

    // Sync: a large change file arriving, verified and parsed.
    {
        seventhslide::sync::ChangeFile file;
        file.library = "0123456789abcdef0123456789abcdef";
        file.machine = me.machine;
        file.computer = me.computer;
        file.sequence = 1;
        file.counter = 1;
        for (int i = 0; i < 3000; ++i) {
            const std::string n = std::to_string(i);
            seventhslide::sync::Change change;
            change.table = "songs";
            change.key = std::string(32 - n.size(), '0') + n;
            change.document = {{"title", "Song " + n}, {"lyrics", long_song().lyrics}};
            file.changes.push_back(std::move(change));
        }
        const auto text = seventhslide::sync::seal(
            seventhslide::sync::change_file_to_json(file),
            {{seventhslide::sync::k_signed_as_computer, &me.key}});
        std::printf("  (a change file of %.1f MB)\n", static_cast<double>(text.size()) / 1e6);
        report("verify and parse it (was on the window's)", ms_per_call(3, [&] {
                   (void)seventhslide::sync::change_file_from_json(
                       seventhslide::sync::unseal(text, "A sync change file").body);
               }));
    }

    // Sync: putting a library online while the window saves through a
    // connection of its own.
    {
        seventhslide::test::TempDir root;
        seventhslide::sync::FolderStore store(root.path / "sync");
        const auto file = root.path / "seventhslide.db";
        auto db = seventhslide::db::Database::open_file(file);
        seventhslide::db::ensure_schema(db);
        {
            seventhslide::db::SongRepository songs(db);
            seventhslide::db::Transaction tx(db);
            for (int i = 0; i < 2000; ++i) {
                Song song;
                song.title = "Song " + std::to_string(i);
                song.lyrics = long_song().lyrics + std::string(8000, 'x');
                (void)songs.insert(song);
            }
            tx.commit();
        }
        const auto library = seventhslide::db::library_uid(db);
        seventhslide::sync::SyncEngine engine(db, store, root.path / "media", me);
        (void)seventhslide::sync::MemberLog(store, me, library)
            .found("lilies of the field", seventhslide::crypto::k_box_params_quick);
        engine.share_everything();

        std::atomic<bool> sending{true};
        std::atomic<double> longest{0};
        std::atomic<int> saves{0};
        std::atomic<int> failed{0};
        std::thread window([&] {
            auto other = seventhslide::db::Database::open_file(file);
            while (sending) {
                const auto start = std::chrono::steady_clock::now();
                try {
                    other.exec("INSERT OR REPLACE INTO app_settings (key, value) VALUES ('bench', 'x')");
                    ++saves;
                } catch (const std::exception&) {
                    ++failed;
                }
                const auto ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
                if (ms > longest) {
                    longest = ms;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
        const auto start = std::chrono::steady_clock::now();
        const auto result = engine.sync();
        const auto total = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - start)
                               .count();
        sending = false;
        window.join();
        std::printf("  (2000 songs of 12 KB: %d changes sent; %d saves, %d failed)\n",
                    result.changes_sent, saves.load(), failed.load());
        report("the send (the write lock was held throughout)", total);
        report("longest wait of a save during it (now)", longest);
    }

    // Sync: a file of 1,000 deletes arriving where 5,000 records are changed
    // and not sent yet: what points at each deleted record is looked for
    // among them.
    {
        seventhslide::test::TempDir root;
        seventhslide::sync::FolderStore store(root.path / "sync");
        auto church_db = seventhslide::db::Database::open_memory();
        seventhslide::db::ensure_schema(church_db);
        church_db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 6000) "
                       "INSERT INTO songs (title, lyrics) SELECT 'Song ' || i, '[V1]\nline ' || i FROM n");
        const auto library = seventhslide::db::library_uid(church_db);
        seventhslide::sync::SyncEngine church(church_db, store, root.path / "church", me);
        church.share_everything();
        (void)church.sync();
        const seventhslide::sync::Identity laptop_me{"laptop", "Laptop",
                                                     seventhslide::crypto::SigningKey::generate()};
        auto laptop_db = seventhslide::db::Database::open_memory();
        seventhslide::db::ensure_schema(laptop_db);
        seventhslide::sync::SyncEngine laptop(laptop_db, store, root.path / "laptop", laptop_me);
        laptop.join(library);
        (void)laptop.sync();
        // The first 1,000 go there; the other 5,000 change here.
        const std::string number = "CAST(substr(title, 6) AS INTEGER)";
        laptop_db.exec("DELETE FROM songs WHERE " + number + " <= 1000");
        (void)laptop.sync();
        church_db.exec("UPDATE songs SET key = 'E' WHERE " + number + " > 1000");
        std::printf("  (6,000 songs: 1,000 deleted there, 5,000 changed here and then sent)\n");
        report("a sync taking 1,000 deletes", ms_per_call(1, [&] { (void)church.sync(); }));
    }
}

TEST_CASE("bench: theme designer drag", "[.bench]")
{
    // What dragging an element on the designer stage costs per mouse-move, and
    // what the 33 ms live-preview timer costs beside it.
    //
    // Everything here is measured WARM. The first preview of a session pays a
    // one-off font-resolution cost (reported separately); averaging that over a
    // timed loop makes a cheap steady-state tick look like an expensive one.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main", 1920, 1080, 0, 20)});

    const auto theme = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song"))
                           .front().toMap();
    const auto style = seventhslide::app::to_json(theme.value(QStringLiteral("style")));
    REQUIRE(facade.slideEdit()->openTheme(QStringLiteral("Main"), QStringLiteral("song"),
                                          theme.value(QStringLiteral("id")).toString()));
    auto* edit = facade.slideEdit()->objects();
    const auto baseline = edit->canvasObjects().size();
    for (int i = 0; i < 6; ++i) {
        REQUIRE(edit->insert(QStringLiteral("text")).isEmpty());
    }
    const auto first = edit->selection().value(QStringLiteral("id")).toString();

    std::printf("\ntheme designer, 6 elements on the stage\n");

    // Cold: what the designer pays once, on the first preview it paints.
    {
        SessionFacade cold;
        REQUIRE(cold.open_memory_database());
        cold.set_outputs({make_output("Main", 1920, 1080, 0, 20)});
        report("first preview after opening the designer (one-off)", ms_per_call(1, [&] {
                   cold.themes()->update_draft_preview(QStringLiteral("Main"), QStringLiteral("song"),
                                                QStringLiteral("song"), style, QString());
                   REQUIRE(!cold.themes()->render_draft_preview(QSize(960, 540)).isNull());
               }));
    }

    // Warm the measurer the way an open designer already is.
    facade.themes()->update_draft_preview(QStringLiteral("Main"), QStringLiteral("song"),
                                   QStringLiteral("song"), style, QString());
    (void)facade.themes()->render_draft_preview(QSize(960, 540));

    edit->beginGesture();
    double x = 100;
    report("drag move: setGeometry", ms_per_call(200, [&] {
               edit->setGeometries(seventhslide::test::geometry(first, x += 1, 200, 400, 200));
           }));
    report("  + reading objects (what the stage binding does)", ms_per_call(200, [&] {
               edit->setGeometries(seventhslide::test::geometry(first, x += 1, 200, 400, 200));
               const auto objs = edit->canvasObjects();
               REQUIRE(objs.size() == baseline + 6);
           }));
    edit->endGesture();

    report("live-preview tick: updateThemeDraftPreview", ms_per_call(50, [&] {
               facade.themes()->update_draft_preview(QStringLiteral("Main"), QStringLiteral("song"),
                                              QStringLiteral("song"), style, QString());
           }));
    report("  + render_theme_draft_preview (the raster QML asks for)", ms_per_call(50, [&] {
               facade.themes()->update_draft_preview(QStringLiteral("Main"), QStringLiteral("song"),
                                              QStringLiteral("song"), style, QString());
               REQUIRE(!facade.themes()->render_draft_preview(QSize(960, 540)).isNull());
           }));
}

TEST_CASE("bench: freeform canvas refresh", "[.bench]")
{
    // A Freeform slide with a clock on it calls refreshCanvas() once a second,
    // which replaces the whole object list. This is what that replace
    // costs, and what the per-object rich-text parse behind the stage bindings
    // costs beside it.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main", 1920, 1080, 0, 5)});

    seventhslide::model::FreeformItem item;
    item.name = "Bench card";
    item.document = seventhslide::freeform::make_document(item.name, {"Main"}).to_json();
    const auto freeform_id = facade.freeform_repo()->create_item(item);

    REQUIRE(facade.slideEdit()->openFreeform(freeform_id));
    auto* edit = facade.slideEdit()->objects();

    // A realistic slide: a clock plus a dozen text boxes with markup in them.
    REQUIRE(edit->insert(QStringLiteral("clock")).isEmpty());  // these return an error string, "" on success
    const QString body = QStringLiteral(
        "**Welcome** to the _service_ this morning\n"
        "Please silence your **phones**\n"
        "Offering follows the _second_ hymn");
    for (int i = 0; i < 12; ++i) {
        REQUIRE(edit->insert(QStringLiteral("text")).isEmpty());  // a new object arrives selected
        REQUIRE(edit->setText(edit->selection().value(QStringLiteral("id")).toString(), body, false,
                              false, false)
                    .isEmpty());
    }
    const auto objects = edit->canvasObjects();
    REQUIRE(objects.size() >= 13);

    std::printf("\nfreeform slide, 1 clock + 12 text boxes\n");
    report("clock tick: canvasObjects() (the full replace)", ms_per_call(200, [&] {
               const auto objs = edit->canvasObjects();
               REQUIRE(objs.size() >= 13);
           }));
    report("  parseRichLines() for one box", ms_per_call(200, [&] {
               const auto lines = edit->parseRichLines(body, false, false, false);
               REQUIRE(!lines.isEmpty());
           }));
    report("  x12, what one stage rebuild re-parses", ms_per_call(50, [&] {
               for (int i = 0; i < 12; ++i) {
                   (void)edit->parseRichLines(body, false, false, false);
               }
           }));
}

TEST_CASE("bench: freeform geometry edits", "[.bench]")
{
    // Moving an object changes only its own slide's thumbnail. This is what a
    // nudge, a drag release and a ribbon geometry edit cost on a full deck.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main", 1920, 1080, 0, 5)});

    seventhslide::model::FreeformItem item;
    item.name = "Bench deck";
    item.document = seventhslide::freeform::make_document(item.name, {"Main"}).to_json();
    const auto freeform_id = facade.freeform_repo()->create_item(item);
    auto* session = facade.slideEdit();
    REQUIRE(session->openFreeform(freeform_id));
    auto* edit = session->objects();

    // A service deck: 40 slides of six boxes, with field tokens in them.
    constexpr int k_slides = 40;
    for (int s = 0; s < k_slides; ++s) {
        if (s > 0) {
            REQUIRE(session->slides()->insert().isEmpty());
            session->slides()->select(s);
        }
        for (int b = 0; b < 6; ++b) {
            REQUIRE(edit->insert(QStringLiteral("text")).isEmpty());
            REQUIRE(edit->setText(edit->selection().value(QStringLiteral("id")).toString(),
                                  QStringLiteral("{speaker} - point %1 of slide %2\nSecond line")
                                      .arg(b)
                                      .arg(s),
                                  false, false, false)
                        .isEmpty());
        }
    }
    session->slides()->select(k_slides / 2);
    const auto objects = edit->canvasObjects();
    REQUIRE(objects.size() == 6);
    const auto id = objects.front().toMap().value(QStringLiteral("id")).toString();
    edit->selectObject(id);

    std::printf("\nfreeform deck, %d slides x 6 text boxes\n", k_slides);
    int n = 0;
    report("keyboard nudge", ms_per_call(100, [&] {
               REQUIRE(edit->nudge((++n % 2) ? 1.0 : -1.0, 0.0).isEmpty());
           }));
    report("ribbon setGeometry", ms_per_call(100, [&] {
               REQUIRE(edit->setGeometries(seventhslide::test::geometry(id, 100 + (++n % 7), 100, 600, 200))
                           .isEmpty());
           }));
    report("drag release (begin + setGeometries + end)", ms_per_call(100, [&] {
               QVariantMap g;
               g.insert(QStringLiteral("id"), id);
               g.insert(QStringLiteral("x"), 100.0 + (++n % 9));
               g.insert(QStringLiteral("y"), 120.0);
               g.insert(QStringLiteral("w"), 600.0);
               g.insert(QStringLiteral("h"), 200.0);
               g.insert(QStringLiteral("rotation"), 0.0);
               edit->beginGesture();
               REQUIRE(edit->setGeometries({g}).isEmpty());
               edit->endGesture();
           }));
    report("align left (for scale)", ms_per_call(100, [&] {
               REQUIRE(edit->setGeometries(seventhslide::test::geometry(id, 300, 100, 600, 200))
                           .isEmpty());
               REQUIRE(edit->align(CanvasAlignQml::Left).isEmpty());
           }));
}

namespace {

/// A 200-slide service on three outputs: each slide a background picture, a
/// picture and four text boxes with markup and a field in them.
seventhslide::freeform::Document large_document()
{
    namespace ff = seventhslide::freeform;
    auto doc = ff::make_document("Big service", {"out-Main", "out-Stage", "out-Stream"});
    constexpr int k_slides = 200;
    for (int s = 1; s < k_slides; ++s) {
        REQUIRE(ff::insert_slide(doc, s));
    }
    for (auto& deck : doc.decks) {
        for (std::size_t s = 0; s < deck.slides.size(); ++s) {
            auto& slide = deck.slides[s];
            slide.background.kind = ff::SlideBackgroundKind::Image;
            slide.background.image = "photo" + std::to_string(s % 17) + ".jpg";
            auto picture = ff::make_image_object(doc, "picture" + std::to_string(s % 5) + ".png",
                                                 deck.canvas);
            slide.objects.push_back(std::move(picture));
            for (int b = 0; b < 4; ++b) {
                auto box = ff::make_text_object(doc, deck.canvas);
                box.text = "**{speaker}** - point " + std::to_string(b) + " of slide "
                    + std::to_string(s) + "\n_A second line_ of words to wrap across the box";
                slide.objects.push_back(std::move(box));
            }
            slide.notes = "Notes for slide " + std::to_string(s);
        }
    }
    ff::normalize(doc);
    return doc;
}

}  // namespace

TEST_CASE("bench: freeform edits on a large document", "[.bench]")
{
    // What one committed edit and one drag step cost, in the editor and
    // through the slide editor's session, on a document far bigger than the
    // slide being edited.
    namespace ff = seventhslide::freeform;
    const auto doc = large_document();
    std::printf("\nfreeform document, 200 slides x 3 outputs, a picture and 4 text boxes each\n");

    ff::Editor ed;
    ed.load(doc, "out-Main");
    ed.select_slide(100);
    const auto id = ed.current_slide()->objects.back().id;
    ed.select(id);
    int n = 0;
    report_us("editor: nudge (one committed edit)", ms_per_call(200, [&] {
                  REQUIRE(ed.nudge_selected((++n % 2) ? 1.0 : -1.0, 0.0));
              }));
    report_us("editor: set_property font_size", ms_per_call(200, [&] {
                  REQUIRE(ed.set_property("font_size", 40 + (++n % 9)));
              }));
    report_us("editor: set_slide_notes", ms_per_call(200, [&] {
                  REQUIRE(ed.set_slide_notes("Notes " + std::to_string(++n)));
              }));
    ed.begin_gesture();
    report_us("editor: drag step (set_geometry in a gesture)", ms_per_call(200, [&] {
                  REQUIRE(ed.set_geometry(id, 100 + (++n % 50), 120, 600, 200, 0));
              }));
    report_us("editor: typing step (set_text in a gesture)", ms_per_call(200, [&] {
                  REQUIRE(ed.set_text("Typed " + std::to_string(++n)));
              }));
    ed.end_gesture();
    report_us("editor: undo + redo", ms_per_call(50, [&] {
                  REQUIRE(ed.undo());
                  REQUIRE(ed.redo());
              }));

    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main", 1920, 1080, 0, 5), make_output("Stage", 1920, 1080, 0, 5),
                        make_output("Stream", 1920, 1080, 0, 5)});
    seventhslide::model::FreeformItem item;
    item.name = "Big service";
    item.document = doc.to_json();
    const auto freeform_id = facade.freeform_repo()->create_item(item);
    auto* session = facade.slideEdit();
    REQUIRE(session->openFreeform(freeform_id));
    session->slides()->select(100);
    auto* edit = session->objects();
    const auto qid = QString::fromUtf8(id);
    edit->selectObject(qid);
    report_us("session: nudge", ms_per_call(50, [&] {
                  REQUIRE(edit->nudge((++n % 2) ? 1.0 : -1.0, 0.0).isEmpty());
              }));
    edit->beginGesture();
    report_us("session: drag step (setGeometries in a gesture)", ms_per_call(50, [&] {
                  REQUIRE(edit->setGeometries(
                                  seventhslide::test::geometry(qid, 100 + (++n % 50), 120, 600, 200))
                              .isEmpty());
              }));
    edit->endGesture();
}

TEST_CASE("bench: theme edits", "[.bench]")
{
    // Editing one theme rewrites every output row. Whether that matters depends
    // on how big an output's serialized theme library actually gets.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main", 1920, 1080, 0, 60),
                       make_output("Stage", 1920, 1080, 3, 60),
                       make_output("Lower Third", 1920, 240, 0, 60)});

    const auto themes = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song"));
    REQUIRE(!themes.isEmpty());
    const auto id = themes.front().toMap().value(QStringLiteral("id")).toString();

    std::printf("\ntheme edits (3 outputs, 60 text themes each)\n");
    int n = 0;
    report("updateTheme() (persist + rebuild)", ms_per_call(20, [&] {
               const auto err = facade.themes()->update_theme(QStringLiteral("Main"), QStringLiteral("song"),
                                                   id, QStringLiteral("Renamed %1").arg(++n),
                                                   nlohmann::json::object(), QString(), false);
               REQUIRE(err.isEmpty());
           }));
    report("listThemes()",
           ms_per_call(50, [&] { (void)facade.themes()->list(QStringLiteral("Main"),
                                                         QStringLiteral("song")); }));
}

TEST_CASE("bench: previews", "[.bench]")
{
    // What one thumbnail costs the thread that renders it. Image providers run
    // this for every visible gallery tile and preview strip.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main", 1920, 1080, 0, 30)});
    facade.live()->play_song(long_song());

    const auto themes = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song"));
    REQUIRE(!themes.isEmpty());
    const QSize tile(320, 180);
    std::printf("\npreviews (320x180)\n");

    int i = 0;
    report("theme preview, cold (distinct theme)", ms_per_call(themes.size(), [&] {
               const auto id = themes.at(i++).toMap().value(QStringLiteral("id")).toString();
               (void)facade.themes()->render_preview(
                   QStringLiteral("theme/Main/song/") + id + QStringLiteral("?c=song"), tile);
           }));
    const auto first = themes.front().toMap().value(QStringLiteral("id")).toString();
    report("theme preview, cached", ms_per_call(200, [&] {
               (void)facade.themes()->render_preview(
                   QStringLiteral("theme/Main/song/") + first + QStringLiteral("?c=song"), tile);
           }));
    REQUIRE_FALSE(facade.outputPreviews()->render_output_preview(QStringLiteral("out-Main"), tile).isNull());
    report("output preview (live slide)", ms_per_call(200, [&] {
               (void)facade.outputPreviews()->render_output_preview(QStringLiteral("out-Main"), tile);
           }));
}

TEST_CASE("bench: freeform rebuild", "[.bench]")
{
    // A Freeform deck live on three outputs is rebuilt on every output, theme
    // or settings change. This is that rebuild, and the document copies and
    // normalize pass that come with it beside the compile itself.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    Session session(measurer);
    session.set_outputs({make_output("Main", 1920, 1080, 0, 4),
                         make_output("Stage", 1920, 1080, 0, 4),
                         make_output("Stream", 1280, 720, 0, 4)});

    auto doc = seventhslide::freeform::make_document("Announcements",
                                                     {"out-Main", "out-Stage", "out-Stream"});
    for (auto& deck : doc.decks) {
        deck.slides.clear();
        for (int s = 0; s < 30; ++s) {
            seventhslide::freeform::Slide slide;
            for (int o = 0; o < 6; ++o) {
                seventhslide::freeform::Object obj;
                obj.x = 100 + 40 * o;
                obj.y = 80 + 120 * o;
                obj.w = 900;
                obj.h = 100;
                obj.text = "**Slide " + std::to_string(s) + "** line " + std::to_string(o)
                           + " of the _morning_ announcements";
                slide.objects.push_back(std::move(obj));
            }
            deck.slides.push_back(std::move(slide));
        }
    }
    seventhslide::freeform::normalize(doc);
    seventhslide::session::LiveFreeformContent content;
    content.name = "Announcements";
    content.document = doc;
    session.play_freeform(content, std::string(seventhslide::model::k_default_stack_id));

    std::printf("\nfreeform, 3 outputs x 30 slides x 6 text boxes\n");
    report("Session::rebuild() (refresh live stack)", ms_per_call(20, [&] { session.rebuild(); }));
    report("  freeform::compile(doc)", ms_per_call(20, [&] {
               const auto compiled = seventhslide::freeform::compile(doc, &measurer);
               REQUIRE_FALSE(compiled.decks.empty());
           }));
    report_us("  Document copy", ms_per_call(200, [&] {
                  const auto copy = doc;
                  REQUIRE(copy.decks.size() == 3);
              }));
    report_us("  Document copy + normalize", ms_per_call(200, [&] {
                  auto copy = doc;
                  seventhslide::freeform::normalize(copy);
                  REQUIRE(copy.decks.size() == 3);
              }));
}

TEST_CASE("bench: bible book lookup", "[.bench]")
{
    // The book box suggests on every keystroke (and the remote asks the same),
    // and every typed reference resolves its book against the open bible.
    const std::vector<std::string> books{
        "Genesis", "Exodus", "Leviticus", "Numbers", "Deuteronomy", "Joshua", "Judges", "Ruth",
        "1 Samuel", "2 Samuel", "1 Kings", "2 Kings", "1 Chronicles", "2 Chronicles", "Ezra",
        "Nehemiah", "Esther", "Job", "Psalms", "Proverbs", "Ecclesiastes", "Song of Solomon",
        "Isaiah", "Jeremiah", "Lamentations", "Ezekiel", "Daniel", "Hosea", "Joel", "Amos",
        "Obadiah", "Jonah", "Micah", "Nahum", "Habakkuk", "Zephaniah", "Haggai", "Zechariah",
        "Malachi", "Matthew", "Mark", "Luke", "John", "Acts", "Romans", "1 Corinthians",
        "2 Corinthians", "Galatians", "Ephesians", "Philippians", "Colossians",
        "1 Thessalonians", "2 Thessalonians", "1 Timothy", "2 Timothy", "Titus", "Philemon",
        "Hebrews", "James", "1 Peter", "2 Peter", "1 John", "2 John", "3 John", "Jude",
        "Revelation"};
    REQUIRE(books.size() == 66);

    std::printf("\nbible book lookup, 66 books\n");
    report_us("suggest_bible_books, one keystroke", ms_per_call(200, [&] {
                  for (const char* typed : {"J", "Jo", "Joh", "1 Co", "Ps"}) {
                      (void)seventhslide::parse::suggest_bible_books(books, typed);
                  }
              }) / 5.0);
    report_us("parse_bible_reference(\"1 Cor 13:4-7\")", ms_per_call(200, [&] {
                  REQUIRE(seventhslide::parse::parse_bible_reference("1 Cor 13:4-7", books));
              }));

    // Both callers read the book list from the database first, each keystroke.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    std::vector<seventhslide::model::BibleVerse> verses;
    for (const auto& book : books) {
        for (int chapter = 1; chapter <= 20; ++chapter) {
            for (int verse = 1; verse <= 24; ++verse) {
                verses.push_back({.book = book, .chapter = chapter, .verse_num = verse,
                                  .text = "In the beginning was the Word"});
            }
        }
    }
    const auto bible_id = facade.bible_repo()->import_bible("Bench", "", verses);
    std::printf("  (%zu verses)\n", verses.size());
    report_us("BibleRepository::list_books, first read", ms_per_call(1, [&] {
                  REQUIRE(facade.bible_repo()->list_books(bible_id).size() == 66);
              }));
    report_us("BibleRepository::list_books, each keystroke after", ms_per_call(200, [&] {
                  REQUIRE(facade.bible_repo()->list_books(bible_id).size() == 66);
              }));
}

TEST_CASE("bench: icons", "[.bench]")
{
    // The glyph pipeline (glyphs.cpp, ``Icons`` in QML -> image://ssicon): what
    // building one URL costs called from QML, what the provider pays to decode and draw
    // it, and how that splits between parsing the SVG and drawing it - the
    // part a provider keyed by glyph id (with parsed glyphs kept) would keep.
    seventhslide::test::QmlScene scene;
    QObject* probe = scene.create(R"(
import QtQuick
import SeventhSlide
QtObject {
    property string last
    function run(n) {
        const inks = ["#cccccc", "#ffffff", "#4da3ff", "#8a8a8a"]
        for (let i = 0; i < n; ++i)
            last = Icons.url(i % 2 ? "song" : "settings", inks[i % 4], 2.5)
    }
}
)",
                                  QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR)
                                                      + QStringLiteral("/js/IconsProbe.qml")));
    const int n = 20000;
    std::printf("\nicons\n");
    report_us("Icons.url() from QML", ms_per_call(1, [&] {
                  QMetaObject::invokeMethod(probe, "run", Q_ARG(QVariant, n));
              }) / n);

    const QString settings = probe->property("last").toString();  // the last one built
    REQUIRE(settings.startsWith(QStringLiteral("image://ssicon/")));
    const QString id = settings.mid(QStringLiteral("image://ssicon/").size());
    std::printf("  provider id: %lld characters\n", static_cast<long long>(id.size()));

    seventhslide::app::IconImageProvider provider;
    for (const int edge : {16, 24}) {
        QSize size;
        const std::string what = "provider request at " + std::to_string(edge) + " px (now)";
        report_us(what.c_str(), ms_per_call(2000, [&] {
                      (void)provider.requestImage(id, &size, QSize(edge, edge));
                  }));
    }
    const QByteArray svg = QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding);
    report_us("  of which: base64 decode", ms_per_call(2000, [&] {
                  (void)QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding);
              }));
    report_us("  of which: SVG parse", ms_per_call(2000, [&] {
                  QSvgRenderer renderer(svg);
              }));
    QSvgRenderer parsed(svg);
    report_us("  of which: draw at 24 px (a kept glyph)", ms_per_call(2000, [&] {
                  QImage img(24, 24, QImage::Format_ARGB32_Premultiplied);
                  img.fill(Qt::transparent);
                  QPainter painter(&img);
                  painter.setRenderHint(QPainter::Antialiasing, true);
                  parsed.render(&painter);
              }));
}

TEST_CASE("bench: compositing per video frame", "[.bench]")
{
    // RENDER-1's measure without a real video: a 1080p background video
    // behind lyrics on two outputs, each with its card, a new picture every
    // frame. Composited on the window's thread and waited for (as every
    // destination did before the presenter had a worker), then asked of the
    // workers (as windows and cards do now).
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir root;
    std::vector<QImage> frames;
    for (int i = 0; i < 8; ++i) {
        QImage frame(1920, 1080, QImage::Format_RGB32);
        frame.fill(QColor::fromHsv(i * 40, 160, 200));
        frames.push_back(frame);
    }
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    Session session(measurer);
    session.set_outputs({make_output("Main", 1920, 1080, 0, 1), make_output("Stage", 1920, 1080, 2, 1)});
    session.play_song(long_song(), std::string(seventhslide::model::k_default_stack_id));
    const QColor black(Qt::black);
    const QColor clear(Qt::transparent);
    std::vector<std::unique_ptr<seventhslide::render::ScenePresenter>> outputs;
    for (const char* id : {"out-Main", "out-Stage"}) {
        auto frame = session.scene_frame(id);
        REQUIRE(frame.has_value());
        frame->background.type = seventhslide::layout::BackgroundType::Video;
        frame->background.video = "/static/videos/motion.mp4";
        auto presenter = std::make_unique<seventhslide::render::ScenePresenter>();
        presenter->set_static_root(root.path);
        presenter->set_font_faces(&measurer.faces());
        presenter->set_background_video_frame(frames[0]);
        presenter->present(seventhslide::scene::share_frame(std::move(*frame)));
        (void)presenter->surface(QSize(1920, 1080), black);
        (void)presenter->surface(QSize(320, 180), clear);
        outputs.push_back(std::move(presenter));
    }
    std::size_t tick = 0;
    const std::initializer_list<QSize> sizes = {QSize(1920, 1080), QSize(320, 180)};
    const double waiting = ms_per_call(60, [&] {
        const QImage& frame = frames[++tick % frames.size()];
        for (auto& presenter : outputs) {
            presenter->set_background_video_frame(frame);
            for (const QSize size : sizes) {
                (void)presenter->surface(size, size == QSize(1920, 1080) ? black : clear);
            }
        }
    });

    QObject receiver;
    double asking = 0.0;
    double arriving = 0.0;
    constexpr int k_frames = 60;
    for (int i = 0; i < k_frames; ++i) {
        const QImage& frame = frames[++tick % frames.size()];
        int left = 0;
        const auto start = std::chrono::steady_clock::now();
        for (auto& presenter : outputs) {
            presenter->set_background_video_frame(frame);
            for (const QSize size : sizes) {
                ++left;
                presenter->request(size, size == QSize(1920, 1080) ? black : clear, &receiver,
                                   [&left](QImage) { --left; });
            }
        }
        asking += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                      .count();
        while (left > 0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        arriving += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                        .count();
    }
    std::printf("\ncompositing per video frame (1080p background video behind lyrics, 2 outputs and their cards)\n");
    report("the window's thread, compositing and waiting", waiting);
    report("the window's thread, asking the workers", asking / k_frames);
    report("until every picture is in, from the workers", arriving / k_frames);
}

namespace {

/// A source that costs what a player's read does: a copy out of a ring.
class BenchSource final : public seventhslide::audio::AudioSource {
public:
    void read(float* out, std::size_t frames) override
    {
        std::fill_n(out, frames * seventhslide::audio::k_channels, 0.25f);
    }
};

class BenchSink final : public seventhslide::audio::OutputAudioSink {
public:
    void push(const float* /*frames*/, std::size_t /*count*/) override {}
};

/// The ``q``th quantile of ``ns``, in microseconds.
double quantile_us(std::vector<std::int64_t> ns, double q)
{
    if (ns.empty()) {
        return 0.0;
    }
    const auto at = static_cast<std::size_t>(q * static_cast<double>(ns.size() - 1));
    std::nth_element(ns.begin(), ns.begin() + static_cast<std::ptrdiff_t>(at), ns.end());
    return static_cast<double>(ns[at]) / 1000.0;
}

} // namespace

TEST_CASE("bench: mixer under routing changes", "[.bench]")
{
    // How long the audio callback's block takes (its wait on the mixer's
    // lock included) while the window's thread changes the routing, sources
    // and sinks, a take every 2 ms and then nonstop, or fades every source
    // nonstop, against the same blocks with nothing changing. A church's
    // mixer: four stacks with their cue strips, six outputs, eight sources
    // of two types, six sinks. The callback runs a block a millisecond (a
    // device asks every ten).
    namespace audio = seventhslide::audio;
    const std::vector<std::string> stacks = {"s0", "s1", "s2", "s3"};
    const auto routing_of = [&](bool flipped) {
        audio::Routing routing;
        routing.stacks = stacks;
        for (const auto& id : stacks) {
            routing.cues.push_back(audio::cue_strip_id(id));
        }
        for (int o = 0; o < 6; ++o) {
            audio::Routing::Output output{"o" + std::to_string(o), {}};
            for (std::size_t s = 0; s < stacks.size(); ++s) {
                if (((s + static_cast<std::size_t>(o)) % 2 == 0) != flipped) {
                    output.stacks.push_back(stacks[s]);
                }
            }
            if (o == 5) {
                output.left_out = {"video"};
            }
            routing.outputs.push_back(std::move(output));
        }
        return routing;
    };

    audio::Mixer mixer;
    std::vector<BenchSource> sources(9);
    std::vector<BenchSink> sinks(6);
    mixer.set_routing(routing_of(false));
    for (std::size_t i = 0; i < 8; ++i) {
        mixer.add_source(stacks[i % stacks.size()], &sources[i], i % 2 == 0 ? "audio" : "video");
    }
    for (std::size_t i = 0; i < sinks.size(); ++i) {
        mixer.add_sink("o" + std::to_string(i), &sinks[i]);
    }

    // A run stops early after a minute, as one whose blocks wait on every
    // change can take far longer: how many it mixed then says so.
    constexpr int k_blocks = 3000;
    constexpr auto k_longest_run = std::chrono::seconds(60);
    const auto run_blocks = [&] {
        std::vector<std::int64_t> ns;
        ns.reserve(k_blocks);
        std::vector<float> listen(audio::k_block_frames * audio::k_channels);
        const auto deadline = std::chrono::steady_clock::now() + k_longest_run;
        for (int b = 0; b < k_blocks && std::chrono::steady_clock::now() < deadline; ++b) {
            const auto start = std::chrono::steady_clock::now();
            mixer.process(listen.data(), audio::k_block_frames);
            ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return ns;
    };
    const auto print = [](const char* what, const std::vector<std::int64_t>& ns) {
        std::printf("  %-36s p50 %7.2f  p99 %7.2f  p99.9 %7.2f  max %8.2f us", what,
                    quantile_us(ns, 0.5), quantile_us(ns, 0.99), quantile_us(ns, 0.999),
                    quantile_us(ns, 1.0));
        if (ns.size() < static_cast<std::size_t>(k_blocks)) {
            std::printf("  (%zu blocks in a minute)", ns.size());
        }
        std::printf("\n");
    };

    const auto quiet = run_blocks();

    // The window's thread: a set of changes (a take: the routing, a source
    // placed, moved and removed, a sink joining and leaving), again and
    // again, ``paced`` a set every 2 ms or else as fast as it can.
    long changes = 0;
    std::vector<std::int64_t> change_ns;
    const auto changing = [&](bool paced) {
        std::atomic<bool> stop{false};
        changes = 0;
        change_ns.clear();
        change_ns.reserve(1 << 20);
        std::thread window([&] {
            bool flipped = false;
            BenchSink extra_sink;
            while (!stop.load()) {
                const auto start = std::chrono::steady_clock::now();
                flipped = !flipped;
                mixer.set_routing(routing_of(flipped));
                mixer.add_source(stacks[0], &sources[8], "audio");
                mixer.move_source(&sources[8], audio::cue_strip_id(stacks[1]));
                mixer.remove_source(&sources[8]);
                mixer.add_sink("o0", &extra_sink);
                mixer.remove_sink(&extra_sink);
                if (change_ns.size() < change_ns.capacity()) {
                    change_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - start)
                                            .count()
                                        / 6);
                }
                changes += 6;
                if (paced) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
        });
        auto ns = run_blocks();
        stop = true;
        window.join();
        return ns;
    };

    // The window's thread fading: every source on every output toward a
    // new level, what each strip hears read back, a source set at once now
    // and then, and the routing flipped every 64 sets, nonstop.
    const auto fading = [&] {
        std::atomic<bool> stop{false};
        changes = 0;
        change_ns.clear();
        change_ns.reserve(1 << 20);
        std::thread window([&] {
            bool flipped = false;
            float target = 0.0f;
            long sets = 0;
            while (!stop.load()) {
                const auto start = std::chrono::steady_clock::now();
                int made = 0;
                target = target > 0.5f ? 0.0f : 1.0f;
                for (std::size_t i = 0; i < 8; ++i) {
                    for (int o = 0; o < 6; ++o) {
                        mixer.fade_source(&sources[i], "o" + std::to_string(o), target,
                                          std::chrono::milliseconds(200 + 10 * o));
                        ++made;
                    }
                    (void)mixer.source_level(&sources[i], {});
                    ++made;
                }
                mixer.set_source_level(&sources[sets % 8], 1.0f);
                ++made;
                if (++sets % 64 == 0) {
                    flipped = !flipped;
                    mixer.set_routing(routing_of(flipped));
                    ++made;
                }
                if (change_ns.size() < change_ns.capacity()) {
                    change_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - start)
                                            .count()
                                        / made);
                }
                changes += made;
            }
        });
        auto ns = run_blocks();
        stop = true;
        window.join();
        return ns;
    };

    std::printf("\nmixer under routing changes (4 stacks + cues, 6 outputs, 8 sources, 6 sinks)\n");
    print("a block, nothing changing", quiet);
    const auto report_changes = [&] {
        std::printf("  %-36s %ld, p50 %.2f us each, max %.2f us\n", "  changes made meanwhile", changes,
                    quantile_us(change_ns, 0.5), quantile_us(change_ns, 1.0));
    };
    for (const bool paced : {true, false}) {
        const auto busy = changing(paced);
        print(paced ? "a block, a take every 2 ms" : "a block, changes nonstop", busy);
        report_changes();
    }
    print("a block, fades nonstop", fading());
    report_changes();
}

namespace {

/// The slide a transition goes from or to, by ``tag`` (0 or 1): lyrics and a
/// title over a background picture, and as asked a picture in the media area,
/// four objects with ids that change by their own fade (a star, a photo, a
/// clock and a text box; the incoming photo also wipes in as a build), or a
/// background picture of its own.
seventhslide::scene::SceneFrame transition_slide(seventhslide::layout::ITextMeasurer& measurer,
                                                 int tag,
                                                 bool picture,
                                                 bool objects,
                                                 bool background)
{
    namespace sc = seventhslide::scene;
    using seventhslide::model::TransitionType;
    sc::SceneFrame frame = rich_frame(measurer);
    const std::string n = std::to_string(tag);
    frame.objects.clear();
    frame.text->lines.front().runs =
        seventhslide::model::parse_inline_markup("Verse " + n + ": a mighty fortress");
    sc::measure_text_content(*frame.text, measurer);
    if (background) {
        frame.background.image = "/static/images/bg" + n + ".png";
    }
    if (picture) {
        frame.mode = sc::SceneMode::Image;
        frame.image.emplace();
        frame.image->url = "/static/images/photo" + n + ".png";
        frame.image->fit = "cover";
        frame.image->area = {true, 160, 180, 1600, 720};
    }
    if (objects) {
        auto base = rich_frame(measurer).objects;
        for (auto& obj : base) {
            obj.id = obj.object->type == seventhslide::canvas::ObjectType::Shape ? "star"
                     : obj.object->type == seventhslide::canvas::ObjectType::Image ? "photo"
                                                                                   : "clock";
            obj.transition.type = TransitionType::Fade;
            obj.object->x += 40.0 * tag;
            if (tag == 1 && obj.id == "photo") {
                obj.builds.push_back({seventhslide::canvas::AnimationEffect::Wipe,
                                      seventhslide::model::TransitionDirection::Left, 0, 300});
            }
            frame.objects.push_back(obj);
        }
        sc::SceneObject words;
        words.z = 4;
        words.id = "words";
        words.transition.type = TransitionType::Fade;
        words.text = frame.overlay.boxes.front();
        words.text->y = 900 + 20.0 * tag;
        words.text->lines.front() = seventhslide::model::parse_inline_markup("Psalm 46, verse " + n);
        words.text->layout = seventhslide::layout::lay_out_overlay(*words.text, measurer);
        frame.objects.push_back(words);
    }
    return frame;
}

}  // namespace

TEST_CASE("bench: transition frames", "[.bench]")
{
    // What each frame of a slide change costs, and what it does to the
    // retained rasters. A slide of lyrics, a background picture, and in turn
    // a picture in the media area, objects with ids (their own fades and a
    // build), and a new background picture, changed into another by each
    // transition over 30 frames at 1080p; the background change also in a
    // 1366x768 window.
    using seventhslide::model::TransitionType;
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    const seventhslide::test::TempDir root;
    std::filesystem::create_directories(root.path / "images");
    for (const char* name : {"bg.png", "bg0.png", "bg1.png"}) {
        REQUIRE(test_pattern(1280, 720).save(
            QString::fromStdString((root.path / "images" / name).string())));
    }
    for (const char* name : {"photo.png", "photo0.png", "photo1.png"}) {
        REQUIRE(test_pattern(1600, 1067).save(
            QString::fromStdString((root.path / "images" / name).string())));
    }
    seventhslide::render::BackgroundImageCache images;
    images.set_static_root(root.path);

    struct Scenario {
        const char* name;
        bool picture;
        bool objects;
        bool background;
        int width = 1920;
        int height = 1080;
    };
    // A window at 1366x768 leaves the canvas on part pixels.
    const Scenario scenarios[] = {{"picture", true, false, false},
                                  {"objects", false, true, false},
                                  {"background", false, false, true},
                                  {"background 1366", false, false, true, 1366, 768}};
    const std::pair<TransitionType, const char*> types[] = {
        {TransitionType::Fade, "fade"},   {TransitionType::FadeThroughBlack, "through black"},
        {TransitionType::Push, "push"},   {TransitionType::Cover, "cover"},
        {TransitionType::Reveal, "reveal"}, {TransitionType::Wipe, "wipe"},
        {TransitionType::Split, "split"}, {TransitionType::Zoom, "zoom"}};
    constexpr int k_frames = 30;

    std::printf("\ntransition frames (1080p unless named, %d frames; misses and MB are the raster cache's)\n",
                k_frames);
    std::printf("  %-28s %9s %9s %9s %11s\n", "", "ms/frame", "misses", "MB after",
                "warm kept");
    for (const auto& scenario : scenarios) {
        for (const auto& [type, type_name] : types) {
            auto from = transition_slide(measurer, 0, scenario.picture, scenario.objects,
                                         scenario.background);
            auto to = transition_slide(measurer, 1, scenario.picture, scenario.objects,
                                       scenario.background);
            to.content_transition.type = type;
            to.content_transition.direction = seventhslide::model::TransitionDirection::Right;
            seventhslide::render::ElementRasterCache rasters;
            seventhslide::render::SceneResources res;
            res.cache = &images;
            res.faces = &measurer.faces();
            res.now = QDateTime(QDate(2026, 10, 5), QTime(10, 30, 0));
            res.rasters = &rasters;
            QImage target;
            // The outgoing slide as it stood: its rasters warm.
            const auto still = seventhslide::render::ScenePresentation::still(from);
            for (int i = 0; i < 3; ++i) {
                seventhslide::render::render_scene_presentation_into(target, still, scenario.width, scenario.height,
                                                                     QColor(Qt::black), res);
            }
            const auto warm = rasters.size();
            const auto misses_before = rasters.misses();
            seventhslide::render::SceneFadeLayers scratch;
            int frame = 0;
            const double ms = ms_per_call(k_frames, [&] {
                seventhslide::render::ScenePresentation pres;
                pres.current = &to;
                pres.outgoing = &from;
                pres.content_type = type;
                pres.content_progress = static_cast<double>(frame) / (k_frames - 1);
                pres.elapsed_ms = 500.0 * pres.content_progress;
                pres.fade_layers = &scratch;
                if (scenario.background) {
                    pres.background = &to.background;
                    pres.background_outgoing = &from.background;
                    pres.background_progress = pres.content_progress;
                    pres.background_type = type;
                }
                ++frame;
                seventhslide::render::render_scene_presentation_into(target, pres, scenario.width, scenario.height,
                                                                     QColor(Qt::black), res);
            });
            const auto misses = rasters.misses() - misses_before;
            const double mb = static_cast<double>(rasters.bytes()) / (1024.0 * 1024.0);
            // A cut back: how many of the outgoing slide's rasters are still there.
            const auto before_back = rasters.misses();
            seventhslide::render::render_scene_presentation_into(target, still, scenario.width, scenario.height,
                                                                 QColor(Qt::black), res);
            const auto redrawn = rasters.misses() - before_back;
            char label[64];
            std::snprintf(label, sizeof label, "%s, %s", scenario.name, type_name);
            std::printf("  %-28s %9.3f %9llu %9.1f %5zu of %zu\n", label, ms,
                        static_cast<unsigned long long>(misses), mb,
                        warm - std::min<std::size_t>(warm, redrawn), warm);
        }
    }
}
