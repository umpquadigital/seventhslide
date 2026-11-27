// Transitions on offscreen destinations (NDI, virtual camera, Blackmagic).
//
// These live on the OffscreenSink base class rather than on any one transport,
// because the fade is shared: fixing it for NDI fixes it for all three, and a
// test pinned to NdiSink would need a runtime that CI does not have.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "pixel_compare.hpp"
#include "recording_sink.hpp"
#include "test_qt_app.hpp"
#include "test_wait.hpp"

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/render/offscreen_sink.hpp>
#include <seventhslide/render/image_painter.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <nlohmann/json.hpp>

#include <QColor>
#include <QFontDatabase>
#include <QImage>
#include <QRect>
#include <QRgb>
#include <QSize>

using seventhslide::model::RichLine;
using seventhslide::model::TextSpan;
using seventhslide::render::OffscreenSink;
using seventhslide::render::ScenePresenter;
using seventhslide::render::SourceFrames;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneTextContent;
using seventhslide::scene::SceneTextLine;
using seventhslide::test::RecordingSink;
using seventhslide::test::run_events_for;

namespace {

constexpr int k_canvas_w = 200;
constexpr int k_canvas_h = 100;

/// A sans family this machine has. Naming one it lacks (DejaVu Sans on a Mac)
/// makes Qt search every installed font for an alias the first time it is
/// asked: about 850 ms on macOS, spent on the compositor thread inside the
/// first composite, by which time a wall-clock fade is already over.
[[nodiscard]] const std::string& sans_family()
{
    static const std::string family = [] {
        const QStringList installed = QFontDatabase::families();
        for (const char* name : {"DejaVu Sans", "Helvetica", "Arial"}) {
            if (installed.contains(QLatin1StringView(name))) {
                return std::string(name);
            }
        }
        return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family().toStdString();
    }();
    return family;
}

/// A frame whose only content is one big word in ``color``. Two frames that
/// differ just in colour paint the same glyphs in the same place, so a single
/// pixel tells us cleanly what is on screen.
[[nodiscard]] SceneFrame make_frame(const std::string& key,
                                    const std::string& color,
                                    bool enable_fade,
                                    int duration_ms,
                                    const std::string& background = "#000000")
{
    SceneFrame frame;
    frame.mode = SceneMode::Song;
    frame.transition_key = key;
    frame.canvas_width = k_canvas_w;
    frame.canvas_height = k_canvas_h;
    if (background == "transparent") {
        frame.background.type = seventhslide::layout::BackgroundType::Transparent;
    } else {
        frame.background.type = seventhslide::layout::BackgroundType::Color;
        frame.background.color = background;
    }
    frame.content_transition = seventhslide::model::transition_from_fade(enable_fade, duration_ms);

    SceneTextContent text;
    text.box.x = 0;
    text.box.y = 0;
    text.box.width = k_canvas_w;
    text.box.height = k_canvas_h;
    text.box.font_family = sans_family();
    text.box.font_size = 64;
    text.box.color = color;
    text.box.align = "center";
    text.box.valign = "middle";
    SceneTextLine line;
    line.runs = RichLine{TextSpan{"W", {}}};
    text.lines.push_back(std::move(line));
    frame.text = std::move(text);
    return frame;
}

/// ``frame`` showing video item ``source``: its pictures come by that key.
void show_video(SceneFrame& frame, const std::string& source = "content 1")
{
    frame.mode = SceneMode::Video;
    frame.video = seventhslide::scene::SceneVideoMedia{.source = source,
                                                       .url = "/static/videos/clip.mp4"};
}

/// ``image`` as video item ``source``'s latest picture.
[[nodiscard]] SourceFrames video_picture(const QImage& image,
                                         const std::string& source = "content 1")
{
    return {{source, {image, {}}}};
}

/// Where the glyph is: the pixel carrying the most of ``channel``.
struct Probe {
    int x = -1;
    int y = -1;
};

[[nodiscard]] Probe brightest(const QImage& img, int (*channel)(QRgb), QRect area = QRect())
{
    if (area.isNull()) {
        area = img.rect();
    }
    area = area.intersected(img.rect());
    Probe best;
    int best_value = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const int value = channel(img.pixel(x, y));
            if (value > best_value) {
                best_value = value;
                best.x = x;
                best.y = y;
            }
        }
    }
    return best;
}

int red_of(QRgb px) { return qRed(px); }
int blue_of(QRgb px) { return qBlue(px); }

}  // namespace

TEST_CASE("an offscreen sink cross-fades content instead of snapping", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w, k_canvas_h)));

    sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));
    REQUIRE_FALSE(sink.is_fading());  // first present has nothing to fade from
    const QImage settled_red = sink.last();

    sink.present(seventhslide::scene::share_frame(make_frame("song", "#0000ff", true, 500)));
    REQUIRE(sink.is_fading());

    const Probe glyph = brightest(settled_red, red_of);
    REQUIRE(glyph.x >= 0);
    REQUIRE(qRed(settled_red.pixel(glyph.x, glyph.y)) > 120);

    sink.set_fade_progress_for_test(0.5);
    const QRgb mid = sink.last().pixel(glyph.x, glyph.y);

    // Half way through, the outgoing red and the incoming blue are both on
    // screen. Snapping would show one or the other, never both.
    REQUIRE(qRed(mid) > 30);
    REQUIRE(qBlue(mid) > 30);

    sink.set_fade_progress_for_test(1.0);
    REQUIRE_FALSE(sink.is_fading());
    const QRgb settled = sink.last().pixel(glyph.x, glyph.y);
    REQUIRE(qBlue(settled) > 120);
    REQUIRE(qRed(settled) < 30);
}

TEST_CASE("an offscreen sink fades a slide out to a blank and back", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w, k_canvas_h)));

    sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));
    const Probe glyph = brightest(sink.last(), red_of);
    REQUIRE(glyph.x >= 0);

    auto blank = make_frame("song", "#ff0000", true, 500);
    seventhslide::scene::apply_scene_blank(blank);
    sink.present(seventhslide::scene::share_frame(blank));
    REQUIRE(sink.is_fading());
    // Half way out, the words are still there, fainter.
    sink.set_fade_progress_for_test(0.5);
    REQUIRE(qRed(sink.last().pixel(glyph.x, glyph.y)) > 30);
    sink.set_fade_progress_for_test(1.0);
    REQUIRE(qRed(sink.last().pixel(glyph.x, glyph.y)) < 30);

    sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));
    REQUIRE(sink.is_fading());
}

TEST_CASE("an offscreen sink snaps when the theme asks for a cut", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w, k_canvas_h)));

    SECTION("fade disabled")
    {
        sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", false, 500)));
        sink.present(seventhslide::scene::share_frame(make_frame("song", "#0000ff", false, 500)));
        REQUIRE_FALSE(sink.is_fading());
    }

    SECTION("zero duration")
    {
        sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 0)));
        sink.present(seventhslide::scene::share_frame(make_frame("song", "#0000ff", true, 0)));
        REQUIRE_FALSE(sink.is_fading());
    }

    // Whatever the reason, the incoming frame is fully on screen right away.
    const Probe glyph = brightest(sink.last(), blue_of);
    REQUIRE(glyph.x >= 0);
    REQUIRE(qBlue(sink.last().pixel(glyph.x, glyph.y)) > 120);
}

TEST_CASE("the keepalive tick runs a fade to completion on the wall clock", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w, k_canvas_h)));

    sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 300)));
    sink.present(seventhslide::scene::share_frame(make_frame("song", "#0000ff", true, 300)));
    REQUIRE(sink.is_fading());

    const int mid_changed = sink.changed_deliveries();
    // Progress moves on a grid, so a window has to span several steps to be
    // sure of one - a single tick can land back inside the step it was already
    // in. The sink has no animation framework: this keepalive is what walks the
    // fade forward, so it must re-render while one is in flight.
    run_events_for(120);
    REQUIRE(sink.changed_deliveries() > mid_changed);
    REQUIRE(sink.is_fading());

    REQUIRE(seventhslide::test::wait_until([&] { return !sink.is_fading(); }, 2000));

    const Probe glyph = brightest(sink.last(), blue_of);
    REQUIRE(glyph.x >= 0);
    REQUIRE(qRed(sink.last().pixel(glyph.x, glyph.y)) < 30);
}

TEST_CASE("a settled offscreen sink keeps streaming without re-rendering", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w, k_canvas_h)));
    sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));

    const int settled_changed = sink.changed_deliveries();
    const int settled_deliveries = sink.deliveries();
    run_events_for(120);

    // Receivers still get frames, but an unchanged picture must not re-encode:
    // that is what keeps a static slide cheap on a YUYV or UYVY transport.
    REQUIRE(sink.deliveries() > settled_deliveries);
    REQUIRE(sink.changed_deliveries() == settled_changed);
}

TEST_CASE("re-presenting an identical frame is not a new picture", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w, k_canvas_h)));
    sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));

    const int settled_changed = sink.changed_deliveries();
    sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));
    REQUIRE_FALSE(sink.is_fading());
    REQUIRE(sink.changed_deliveries() == settled_changed);
}

TEST_CASE("republishing the same video frame is not a new picture", "[render][offscreen][video]")
{
    seventhslide::test::ensure_qt_application();

    // A player hands back the frame it is holding while the clip has not
    // advanced, and callers poll faster than most files change frames. If that
    // repeat counted as a new presentation, every destination would re-encode
    // pixels it has already sent.
    ScenePresenter presenter;
    SceneFrame showing = make_frame("video", "#ff0000", false, 0);
    show_video(showing);
    presenter.present(seventhslide::scene::share_frame(showing));
    QImage a(k_canvas_w, k_canvas_h, QImage::Format_ARGB32_Premultiplied);
    a.fill(Qt::red);

    const auto start = presenter.revision();
    presenter.set_source_frames(video_picture(a));
    const auto after_first = presenter.revision();
    REQUIRE(after_first > start);

    SECTION("the same image again does not move the revision")
    {
        presenter.set_source_frames(video_picture(a));
        presenter.set_source_frames(video_picture(a));
        REQUIRE(presenter.revision() == after_first);
    }

    SECTION("a shallow copy is the same pixels, so it does not move it either")
    {
        QImage shared = a;  // implicit sharing: the data mpv would hand back
        presenter.set_source_frames(video_picture(shared));
        REQUIRE(presenter.revision() == after_first);
    }

    SECTION("a genuinely new frame does move it")
    {
        QImage b(k_canvas_w, k_canvas_h, QImage::Format_ARGB32_Premultiplied);
        b.fill(Qt::blue);
        presenter.set_source_frames(video_picture(b));
        REQUIRE(presenter.revision() > after_first);
    }

    SECTION("the background video frame follows the same rule")
    {
        // On a moving background, which is what paints it.
        showing.background.type = seventhslide::layout::BackgroundType::Video;
        presenter.present(seventhslide::scene::share_frame(showing));
        const auto before = presenter.revision();
        presenter.set_background_video_frame(a);
        const auto bumped = presenter.revision();
        REQUIRE(bumped > before);
        presenter.set_background_video_frame(a);
        REQUIRE(presenter.revision() == bumped);
    }
}

TEST_CASE("a frozen output holds its video frame on every destination", "[render][offscreen][freeze]")
{
    seventhslide::test::ensure_qt_application();

    ScenePresenter presenter;
    SceneFrame frame = make_frame("song", "#ff0000", false, 0);
    show_video(frame);
    frame.is_frozen = true;
    presenter.present(seventhslide::scene::share_frame(frame));

    QImage video(4, 4, QImage::Format_ARGB32_Premultiplied);
    video.fill(Qt::green);
    const auto frozen_revision = presenter.revision();
    presenter.set_source_frames(video_picture(video));
    presenter.set_background_video_frame(video);
    REQUIRE(presenter.revision() == frozen_revision);

    frame.is_frozen = false;
    presenter.present(seventhslide::scene::share_frame(frame));
    const auto live_revision = presenter.revision();
    video = video.copy();
    presenter.set_source_frames(video_picture(video));
    REQUIRE(presenter.revision() != live_revision);
}

TEST_CASE("a frozen stack holds its own video, not the others'", "[render][offscreen][freeze]")
{
    seventhslide::test::ensure_qt_application();

    QImage green(4, 4, QImage::Format_ARGB32_Premultiplied);
    green.fill(Qt::green);
    QImage blue(4, 4, QImage::Format_ARGB32_Premultiplied);
    blue.fill(Qt::blue);
    const auto pictures = [](const QImage& left, const QImage& right) {
        return SourceFrames{{"content 1", {left, {}}}, {"content 2", {right, {}}}};
    };
    // Each video in its half of the canvas; a corner of each half is clear of
    // the word every test frame shows in the middle.
    const auto in_half = [](SceneFrame& frame, const char* source, int x) {
        show_video(frame, source);
        frame.video->area = {.enabled = true, .x = x, .y = 0, .width = 100, .height = 100};
    };
    const auto colour_at = [](ScenePresenter& presenter, int x) {
        return QColor(presenter.surface(QSize(k_canvas_w, k_canvas_h), QColor(Qt::black))
                          .pixel(x, 10));
    };

    // A video under a lower third that is frozen: the video plays on.
    SceneFrame video = make_frame("video", "#ff0000", false, 0);
    in_half(video, "content 1", 0);
    SceneFrame third = make_frame("third", "#ff0000", false, 0, "transparent");
    third.is_frozen = true;
    third.stacked_below.push_back(seventhslide::scene::share_frame(video));
    ScenePresenter presenter;
    presenter.present(seventhslide::scene::share_frame(third));
    presenter.set_source_frames(pictures(green, green));
    REQUIRE(colour_at(presenter, 10) == QColor(Qt::green));
    auto before = presenter.revision();
    presenter.set_source_frames(pictures(blue, green));
    CHECK(presenter.revision() > before);
    CHECK(colour_at(presenter, 10) == QColor(Qt::blue));

    // The video's own stack frozen, under one that is not: it holds the
    // picture it had when it froze, while the video over it plays on.
    SceneFrame held = make_frame("video", "#ff0000", false, 0);
    in_half(held, "content 1", 0);
    SceneFrame over = make_frame("over", "#ff0000", false, 0, "transparent");
    in_half(over, "content 2", 100);
    over.stacked_below.push_back(seventhslide::scene::share_frame(held));
    ScenePresenter second;
    second.present(seventhslide::scene::share_frame(over));
    second.set_source_frames(pictures(green, green));
    REQUIRE(colour_at(second, 10) == QColor(Qt::green));
    held.is_frozen = true;
    over.stacked_below.front() = seventhslide::scene::share_frame(held);
    second.present(seventhslide::scene::share_frame(over));
    before = second.revision();
    second.set_source_frames(pictures(blue, green));
    CHECK(second.revision() == before);
    CHECK(colour_at(second, 10) == QColor(Qt::green));
    second.set_source_frames(pictures(blue, blue));
    CHECK(second.revision() > before);
    CHECK(colour_at(second, 190) == QColor(Qt::blue));
    CHECK(colour_at(second, 10) == QColor(Qt::green));
}

TEST_CASE("an output repaints only for the video pictures it paints", "[render][offscreen][video]")
{
    seventhslide::test::ensure_qt_application();
    using seventhslide::layout::BackgroundType;

    ScenePresenter presenter;
    SceneFrame frame = make_frame("song", "#ff0000", false, 0);
    frame.background.type = BackgroundType::Color;
    presenter.present(seventhslide::scene::share_frame(frame));

    QImage clip(4, 4, QImage::Format_ARGB32_Premultiplied);
    clip.fill(Qt::green);
    QImage motion(4, 4, QImage::Format_ARGB32_Premultiplied);
    motion.fill(Qt::blue);

    // A song on a plain background paints neither, however they tick.
    const auto still = presenter.revision();
    presenter.set_source_frames(video_picture(clip));
    presenter.set_background_video_frame(motion);
    REQUIRE(presenter.revision() == still);

    // Showing the clip, its next picture moves it, and the background's does not.
    show_video(frame);
    presenter.present(seventhslide::scene::share_frame(frame));
    auto before = presenter.revision();
    clip = clip.copy();
    presenter.set_source_frames(video_picture(clip));
    REQUIRE(presenter.revision() > before);
    before = presenter.revision();
    motion = motion.copy();
    presenter.set_background_video_frame(motion);
    REQUIRE(presenter.revision() == before);

    // Another stack's video, which this output does not show, does not move it.
    before = presenter.revision();
    presenter.set_source_frames({{"content 1", {clip, {}}}, {"content 2", {motion, {}}}});
    REQUIRE(presenter.revision() == before);

    // A song on the moving background: the other way round.
    frame.mode = SceneMode::Song;
    frame.video.reset();
    frame.background.type = BackgroundType::Video;
    presenter.present(seventhslide::scene::share_frame(frame));
    before = presenter.revision();
    motion = motion.copy();
    presenter.set_background_video_frame(motion);
    REQUIRE(presenter.revision() > before);
    before = presenter.revision();
    clip = clip.copy();
    presenter.set_source_frames(video_picture(clip));
    REQUIRE(presenter.revision() == before);
}

TEST_CASE("a repeat video frame does not re-deliver to a destination", "[render][offscreen][video]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w, k_canvas_h)));
    SceneFrame showing = make_frame("video", "#ff0000", false, 0);
    show_video(showing);
    sink.present(seventhslide::scene::share_frame(showing));

    QImage frame(k_canvas_w, k_canvas_h, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::green);
    sink.set_source_frames(video_picture(frame));
    run_events_for(80);
    const int changed_after_first = sink.changed_deliveries();
    REQUIRE(changed_after_first > 0);

    // Several ticks holding the same frame: keepalives still go out (the
    // transport must not starve), but none of them counts as a changed picture.
    const int deliveries_before = sink.deliveries();
    for (int i = 0; i < 4; ++i) {
        sink.set_source_frames(video_picture(frame));
        run_events_for(40);
    }
    REQUIRE(sink.deliveries() > deliveries_before);
    REQUIRE(sink.changed_deliveries() == changed_after_first);

    // ...and the clip advancing is still seen.
    QImage next(k_canvas_w, k_canvas_h, QImage::Format_ARGB32_Premultiplied);
    next.fill(Qt::blue);
    sink.set_source_frames(video_picture(next));
    run_events_for(80);
    REQUIRE(sink.changed_deliveries() > changed_after_first);
}

TEST_CASE("destinations sharing an output are given a video frame once", "[render][offscreen][video]")
{
    seventhslide::test::ensure_qt_application();

    // OutputWindowHost pushes per presenter, not per sink. Even if it did not,
    // the presenter must not count the second push as a new picture.
    ScenePresenter shared;
    SceneFrame showing = make_frame("video", "#ff0000", false, 0);
    show_video(showing);
    shared.present(seventhslide::scene::share_frame(showing));
    RecordingSink a;
    RecordingSink b;
    a.set_presenter(&shared);
    b.set_presenter(&shared);
    REQUIRE(a.start(QStringLiteral("a"), QSize(k_canvas_w, k_canvas_h)));
    REQUIRE(b.start(QStringLiteral("b"), QSize(k_canvas_w, k_canvas_h)));

    QImage frame(k_canvas_w, k_canvas_h, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::green);

    const auto before = shared.revision();
    a.set_source_frames(video_picture(frame));
    b.set_source_frames(video_picture(frame));
    REQUIRE(shared.revision() == before + 1);
}

TEST_CASE("a keyed offscreen sink cross-fades over a transparent letterbox", "[render][offscreen][fade][ndi]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    sink.keyed = true;
    // A canvas narrower than the output leaves real letterbox bars to key.
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w * 2, k_canvas_h)));

    sink.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500, "transparent")));
    sink.present(seventhslide::scene::share_frame(make_frame("song", "#0000ff", true, 500, "transparent")));
    REQUIRE(sink.is_fading());
    sink.set_fade_progress_for_test(0.5);

    // The bars stay fully transparent mid-fade - a switcher keys them out.
    REQUIRE(qAlpha(sink.last().pixel(1, k_canvas_h / 2)) == 0);

    // And the glyph itself blends without picking up a dark fringe from the
    // letterbox: premultiplied compositing, not straight-alpha over black.
    const Probe glyph = brightest(sink.last(), red_of);
    REQUIRE(glyph.x >= 0);
    const QRgb mid = sink.last().pixel(glyph.x, glyph.y);
    REQUIRE(qAlpha(mid) > 0);
    REQUIRE(qRed(mid) > 30);
    REQUIRE(qBlue(mid) > 30);
}

TEST_CASE("an offscreen sink dissolves the background independently", "[render][offscreen][fade][transition]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink sink;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(k_canvas_w, k_canvas_h)));

    auto with_bg_fade = [](SceneFrame frame) {
        frame.background_transition = seventhslide::model::transition_from_fade(true, 500);
        return frame;
    };

    // Same content and key, different background: only the background moves.
    sink.present(seventhslide::scene::share_frame(with_bg_fade(make_frame("song", "#ffffff", false, 0, "#ff0000"))));
    sink.present(seventhslide::scene::share_frame(with_bg_fade(make_frame("song", "#ffffff", false, 0, "#0000ff"))));
    REQUIRE(sink.is_fading());

    sink.set_fade_progress_for_test(0.5);
    // Sample a corner, away from the glyph in the middle.
    const QRgb corner = sink.last().pixel(2, 2);
    REQUIRE(qRed(corner) > 30);
    REQUIRE(qBlue(corner) > 30);

    sink.set_fade_progress_for_test(1.0);
    REQUIRE_FALSE(sink.is_fading());
    const QRgb settled = sink.last().pixel(2, 2);
    REQUIRE(qBlue(settled) > 120);
    REQUIRE(qRed(settled) < 30);
}

TEST_CASE("an offscreen sink learns what a transition costs and cuts the next one", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    // A canvas big enough that compositing is genuinely expensive, against a
    // cadence nothing of that size can fit inside. 4K, not 1080p: an Apple
    // M3 blends a 1080p fade frame in about 0.7 ms, inside the 1 ms budget,
    // and 4K in about 2.6 ms.
    constexpr int w = 3840;
    constexpr int h = 2160;
    const auto big = [](SceneFrame frame) {
        frame.canvas_width = w;
        frame.canvas_height = h;
        frame.text->box.width = w;
        frame.text->box.height = h;
        frame.text->box.font_size = 800;
        return frame;
    };

    // A dissolve the clock cannot finish: its progress is set by hand below.
    // A short one could end in real time before a second frame of it is
    // painted (on a loaded machine, a 4K frame can take longer than 500 ms),
    // and then nothing would be learned.
    constexpr int k_long_fade_ms = 10 * 60 * 1000;

    RecordingSink sink;
    sink.tick_budget_ms = 1;
    REQUIRE(sink.start(QStringLiteral("test"), QSize(w, h)));

    sink.present(seventhslide::scene::share_frame(big(make_frame("song", "#ff0000", true, k_long_fade_ms))));
    sink.present(seventhslide::scene::share_frame(big(make_frame("song", "#00ff00", true, k_long_fade_ms))));

    // Nothing has been measured at this shape yet, so the first transition is
    // allowed to run and is what teaches the sink its cost.
    REQUIRE(sink.is_fading());
    // Each painted before the next is asked for, as the sink's timer paints a
    // transition's every step.
    sink.settle();
    sink.set_fade_progress_for_test(0.3);  // a second frame: the cadence to sustain
    sink.settle();
    sink.set_fade_progress_for_test(1.0);
    sink.settle();
    REQUIRE_FALSE(sink.is_fading());

    sink.present(seventhslide::scene::share_frame(big(make_frame("song", "#0000ff", true, k_long_fade_ms))));

    // The theme still asks for a dissolve and the frames are still compatible,
    // so the only reason not to fade is cost. A clean cut beats a transition
    // that cannot keep cadence.
    REQUIRE_FALSE(sink.is_fading());

    // And the incoming frame is fully on screen, not stuck mid-blend.
    const QRect middle((w - 1200) / 2, (h - 1200) / 2, 1200, 1200);
    const Probe glyph = brightest(sink.last(), blue_of, middle);
    REQUIRE(glyph.x >= 0);
    REQUIRE(qBlue(sink.last().pixel(glyph.x, glyph.y)) > 120);
    REQUIRE(qRed(sink.last().pixel(glyph.x, glyph.y)) < 30);
}

TEST_CASE("destinations sharing an output composite the scene once", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    // What OutputWindowHost does for every offscreen destination of one output.
    ScenePresenter shared;
    RecordingSink a;
    RecordingSink b;
    a.set_presenter(&shared);
    b.set_presenter(&shared);
    REQUIRE(a.start(QStringLiteral("a"), QSize(k_canvas_w, k_canvas_h)));
    REQUIRE(b.start(QStringLiteral("b"), QSize(k_canvas_w, k_canvas_h)));

    a.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));
    b.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));

    // Same size, same letterbox: one composite serves both, so they are handed
    // the identical buffer rather than two renders of the same scene.
    REQUIRE(a.last_bits() != nullptr);
    REQUIRE(a.last_bits() == b.last_bits());

    SECTION("and they stay shared through a transition")
    {
        a.present(seventhslide::scene::share_frame(make_frame("song", "#0000ff", true, 500)));
        b.present(seventhslide::scene::share_frame(make_frame("song", "#0000ff", true, 500)));
        REQUIRE(shared.is_fading());
        a.set_fade_progress_for_test(0.5);
        b.set_fade_progress_for_test(0.5);
        REQUIRE(a.last_bits() == b.last_bits());
    }

    SECTION("a destination wanting a different letterbox gets its own")
    {
        // Keying one feed and not the other is a real configuration, and the
        // two genuinely differ in the bars, so they cannot share.
        b.keyed = true;
        b.present(seventhslide::scene::share_frame(make_frame("song", "#00ff00", true, 500)));
        a.present(seventhslide::scene::share_frame(make_frame("song", "#00ff00", true, 500)));
        REQUIRE(a.last_bits() != b.last_bits());
    }
}

TEST_CASE("a standalone offscreen sink still composites for itself", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    RecordingSink a;
    RecordingSink b;
    REQUIRE(a.start(QStringLiteral("a"), QSize(k_canvas_w, k_canvas_h)));
    REQUIRE(b.start(QStringLiteral("b"), QSize(k_canvas_w, k_canvas_h)));

    a.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));
    b.present(seventhslide::scene::share_frame(make_frame("song", "#ff0000", true, 500)));

    // No shared presenter: each owns its own presentation and its own buffer.
    REQUIRE(a.last_bits() != nullptr);
    REQUIRE(b.last_bits() != nullptr);
    REQUIRE(a.last_bits() != b.last_bits());
}

TEST_CASE("the cached fade blend matches painting both sides live", "[render][offscreen][fade]")
{
    seventhslide::test::ensure_qt_application();

    // Real themed frames carry objects with stable ids, which transition one at
    // a time and so cannot be flattened into the cached layers. Cover both that
    // path and the flat one, since they cache different things.
    const auto with_object = [](SceneFrame frame, const std::string& colour) {
        seventhslide::layout::OverlayBox box;
        box.x = 10;
        box.y = 10;
        box.w = 80;
        box.h = 40;
        box.font_family = sans_family();
        box.font_size = 24;
        box.font_color = colour;
        box.id = "element";
        seventhslide::scene::SceneObject object;
        object.id = "element";
        object.z = 5;
        object.text = std::move(box);
        frame.objects.push_back(std::move(object));
        return frame;
    };

    const bool per_object = GENERATE(false, true);
    CAPTURE(per_object);

    const SceneFrame outgoing = per_object
        ? with_object(make_frame("song", "#ff0000", true, 500), "#ff0000")
        : make_frame("song", "#ff0000", true, 500);
    const SceneFrame incoming = per_object
        ? with_object(make_frame("song", "#0000ff", true, 500), "#0000ff")
        : make_frame("song", "#0000ff", true, 500);
    REQUIRE(seventhslide::render::scene_objects_have_ids(incoming) == per_object);

    seventhslide::render::ScenePresentation pres =
        seventhslide::render::ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;

    for (const double progress : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        pres.content_progress = progress;

        // Without scratch surfaces the compositor repaints both sides every
        // frame; with them it blends two cached bitmaps. The pictures must
        // agree, or the optimisation is changing what an audience sees.
        seventhslide::render::SceneResources live;
        const QImage painted = seventhslide::render::render_scene_presentation_image(
            pres, k_canvas_w, k_canvas_h, QColor(Qt::black), live);

        seventhslide::render::SceneFadeLayers layers;
        seventhslide::render::SceneResources cached;
        cached.fade_layers = &layers;
        const QImage blended = seventhslide::render::render_scene_presentation_image(
            pres, k_canvas_w, k_canvas_h, QColor(Qt::black), cached);

        INFO("progress " << progress);
        REQUIRE(seventhslide::test::mean_abs_diff(painted, blended) < 1.5);
    }
}
