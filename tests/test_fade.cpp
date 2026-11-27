#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scene_object_helpers.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/render/fade.hpp>
#include <seventhslide/render/output_window.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <nlohmann/json.hpp>

#include <QImage>
#include <QPainter>
#include <QRgb>

#include <functional>
#include <vector>

using seventhslide::model::RichLine;
using seventhslide::model::TextSpan;
using seventhslide::render::BackgroundFadePresenter;
using seventhslide::render::FadeConfig;
using seventhslide::render::FadePresentAction;
using seventhslide::render::FadePresenter;
using seventhslide::render::background_payload_key;
using seventhslide::render::effective_transition_key;
using seventhslide::render::fade_config_from_spec;
using seventhslide::render::should_crossfade;
using seventhslide::render::should_crossfade_background;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneTextContent;
using seventhslide::scene::SceneTextLine;

namespace {

[[nodiscard]] SceneFrame make_frame(std::string key,
                                    SceneMode mode,
                                    bool enable_fade,
                                    int duration_ms,
                                    std::string line_text)
{
    SceneFrame frame;
    frame.mode = mode;
    frame.transition_key = std::move(key);
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    frame.content_transition = seventhslide::model::transition_from_fade(enable_fade, duration_ms);

    SceneTextContent text;
    text.box.x = 0;
    text.box.y = 0;
    text.box.width = 200;
    text.box.height = 100;
    text.box.font_family = "DejaVu Sans";
    text.box.font_size = 24;
    text.box.color = "#ffffff";
    SceneTextLine line;
    line.runs = RichLine{TextSpan{std::move(line_text), {}}};
    text.lines.push_back(std::move(line));
    frame.text = std::move(text);
    return frame;
}

}  // namespace

TEST_CASE("background payload key names what a background paints", "[render]")
{
    using seventhslide::layout::BackgroundType;
    seventhslide::scene::SceneBackground black;
    black.type = BackgroundType::Color;
    black.color = "#000000";
    REQUIRE(background_payload_key(black) == background_payload_key(black));
    REQUIRE(background_payload_key(black) == "color|#000000||");

    seventhslide::scene::SceneBackground a;
    a.type = BackgroundType::Image;
    a.image = "/static/a.jpg";
    auto b = a;
    b.image = "/static/b.jpg";
    REQUIRE(background_payload_key(a) != background_payload_key(b));
    auto clear = black;
    clear.type = BackgroundType::Transparent;
    REQUIRE(background_payload_key(clear) != background_payload_key(black));
}

TEST_CASE("fade_config_from_spec + should_crossfade", "[render]")
{
    SECTION("a cut does not fade")
    {
        const FadeConfig cfg = fade_config_from_spec(seventhslide::model::Transition{});
        REQUIRE_FALSE(cfg.enable);
        REQUIRE(cfg.duration_ms == 500);
    }

    SECTION("a dissolve fades for its duration")
    {
        const FadeConfig cfg = fade_config_from_spec(
            {seventhslide::model::TransitionType::Fade, 250});
        REQUIRE(cfg.enable);
        REQUIRE(cfg.duration_ms == 250);
    }

    SECTION("first paint cuts; another item plays the incoming transition")
    {
        FadeConfig on{.enable = true, .duration_ms = 400};
        REQUIRE_FALSE(should_crossfade(on, ""));
        REQUIRE(should_crossfade(on, "song"));
        // The incoming side decides, whatever changed.
        REQUIRE(should_crossfade(on, "song"));
        REQUIRE_FALSE(should_crossfade(FadeConfig{}, "song"));
        REQUIRE_FALSE(should_crossfade(FadeConfig{.enable = true, .duration_ms = 0}, "song"));
    }

    SECTION("effective key falls back to mode")
    {
        SceneFrame frame;
        frame.mode = SceneMode::Bible;
        frame.transition_key = "";
        REQUIRE(effective_transition_key(frame) == "bible");
        frame.transition_key = "item:3";
        REQUIRE(effective_transition_key(frame) == "item:3");
    }
}

TEST_CASE("FadePresenter snap vs crossfade", "[render]")
{
    FadePresenter fp;

    SECTION("first present snaps")
    {
        const auto a = fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, true, 300, "A")));
        REQUIRE(a == FadePresentAction::Snap);
        REQUIRE_FALSE(fp.is_fading());
        REQUIRE(fp.last_key() == "song");
    }

    SECTION("same key with fade starts crossfade")
    {
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, true, 300, "A")))
                == FadePresentAction::Snap);
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, true, 300, "B")))
                == FadePresentAction::StartFade);
        REQUIRE(fp.is_fading());
        REQUIRE(fp.progress() == Catch::Approx(0.0));
        REQUIRE(fp.outgoing() != nullptr);
        REQUIRE(fp.current()->text->lines[0].runs[0].text == "B");
        REQUIRE(fp.outgoing()->text->lines[0].runs[0].text == "A");

        fp.set_progress(0.5);
        REQUIRE(fp.is_fading());
        REQUIRE(fp.progress() == Catch::Approx(0.5));

        fp.set_progress(1.0);
        REQUIRE_FALSE(fp.is_fading());
        REQUIRE(fp.outgoing() == nullptr);
    }

    SECTION("another item plays its transition: nothing snaps for the change of kind")
    {
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, true, 300, "A")))
                == FadePresentAction::Snap);
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("bible", SceneMode::Bible, true, 300, "Jn 1")))
                == FadePresentAction::StartFade);
        REQUIRE(fp.is_fading());
        REQUIRE(fp.last_key() == "bible");
    }

    SECTION("a blank comes and goes by the slide's transition")
    {
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, true, 300, "A")))
                == FadePresentAction::Snap);
        auto blank = make_frame("song", SceneMode::Song, true, 300, "A");
        seventhslide::scene::apply_scene_blank(blank);
        REQUIRE(fp.present(seventhslide::scene::share_frame(blank)) == FadePresentAction::StartFade);
        REQUIRE(fp.is_fading());
        REQUIRE(fp.current()->is_blank);
        REQUIRE(fp.outgoing()->text->lines[0].runs[0].text == "A");
        fp.set_progress(1.0);
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, true, 300, "B")))
                == FadePresentAction::StartFade);
        REQUIRE(fp.is_fading());
        REQUIRE(fp.outgoing()->is_blank);
    }

    SECTION("a blank of a slide set to cut cuts")
    {
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, false, 300, "A")))
                == FadePresentAction::Snap);
        auto blank = make_frame("song", SceneMode::Song, false, 300, "A");
        blank.builds_end_ms = 800;
        seventhslide::scene::apply_scene_blank(blank);
        REQUIRE(fp.present(seventhslide::scene::share_frame(blank)) == FadePresentAction::Snap);
        REQUIRE_FALSE(fp.is_fading());
    }

    SECTION("fade disabled always snaps")
    {
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, false, 300, "A")))
                == FadePresentAction::Snap);
        REQUIRE(fp.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, false, 300, "B")))
                == FadePresentAction::Snap);
        REQUIRE_FALSE(fp.is_fading());
    }

    SECTION("identical frame is noop")
    {
        auto frame = make_frame("song", SceneMode::Song, true, 300, "A");
        REQUIRE(fp.present(seventhslide::scene::share_frame(frame)) == FadePresentAction::Snap);
        REQUIRE(fp.present(seventhslide::scene::share_frame(frame)) == FadePresentAction::Noop);
    }

    SECTION("freeze flag-only change is noop")
    {
        auto frame = make_frame("song", SceneMode::Song, true, 300, "A");
        REQUIRE(fp.present(seventhslide::scene::share_frame(frame)) == FadePresentAction::Snap);
        frame.is_frozen = true;
        REQUIRE(fp.present(seventhslide::scene::share_frame(frame)) == FadePresentAction::Noop);
        REQUIRE(fp.current()->is_frozen);
        REQUIRE_FALSE(fp.is_fading());
        frame.is_frozen = false;
        REQUIRE(fp.present(seventhslide::scene::share_frame(frame)) == FadePresentAction::Noop);
        REQUIRE_FALSE(fp.current()->is_frozen);
    }
}

TEST_CASE("BackgroundFadePresenter fades only on payload change", "[render][transition]")
{
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    using seventhslide::scene::SceneBackground;

    Transition dissolve{TransitionType::Fade, 400};
    BackgroundFadePresenter bp;

    SceneBackground a;
    a.type = seventhslide::layout::BackgroundType::Color;
    a.color = "#000000";
    SceneBackground b = a;
    b.color = "#ff0000";
    SceneBackground b2 = b;

    SECTION("first present snaps")
    {
        REQUIRE(bp.present(a, dissolve) == FadePresentAction::Snap);
        REQUIRE_FALSE(bp.is_fading());
    }

    SECTION("same payload is noop")
    {
        REQUIRE(bp.present(a, dissolve) == FadePresentAction::Snap);
        REQUIRE(bp.present(a, dissolve) == FadePresentAction::Noop);
        REQUIRE_FALSE(bp.is_fading());
    }

    SECTION("payload change with dissolve starts fade")
    {
        REQUIRE(bp.present(a, dissolve) == FadePresentAction::Snap);
        REQUIRE(bp.present(b, dissolve) == FadePresentAction::StartFade);
        REQUIRE(bp.is_fading());
        bp.set_progress(1.0);
        REQUIRE_FALSE(bp.is_fading());
        REQUIRE(bp.present(b2, dissolve) == FadePresentAction::Noop);
    }

    SECTION("cut never fades")
    {
        REQUIRE(bp.present(a, Transition{}) == FadePresentAction::Snap);
        REQUIRE(bp.present(b, Transition{}) == FadePresentAction::Snap);
        REQUIRE_FALSE(bp.is_fading());
    }

    SECTION("should_crossfade_background")
    {
        REQUIRE_FALSE(should_crossfade_background(dissolve, "", "color|#000||"));
        REQUIRE(should_crossfade_background(dissolve, "color|#000||", "color|#fff||"));
        REQUIRE_FALSE(should_crossfade_background(dissolve, "color|#000||", "color|#000||"));
        REQUIRE(background_payload_key(a) == "color|#000000||");
    }
}

TEST_CASE("OutputWindow shows its presenter's frame", "[render]")
{
    seventhslide::test::ensure_qt_application();

    seventhslide::render::OutputWindow win(QStringLiteral("out-main"), QStringLiteral("Main"), nullptr,
                                           QStringLiteral("dest-main"));
    win.setGeometry(0, 0, 320, 180);
    seventhslide::render::ScenePresenter presenter;
    win.set_presenter(&presenter);

    // The window keeps no frame of its own: its title (and letterbox) follow
    // whatever the output's presenter is showing.
    presenter.present(seventhslide::scene::share_frame(make_frame("song", SceneMode::Song, true, 200, "One")));
    win.presentation_changed();
    REQUIRE(win.presenter()->current_frame() != nullptr);
    REQUIRE(win.presenter()->current_frame()->transition_key == "song");
    REQUIRE(win.title().endsWith(QStringLiteral("[song]")));
    win.set_presenter(nullptr);
}

TEST_CASE("paint_text_content multiplies outer painter opacity for fade", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SceneTextContent text;
    text.box.x = 0;
    text.box.y = 0;
    text.box.width = 200;
    text.box.height = 80;
    text.box.font_family = "DejaVu Sans";
    text.box.font_size = 48;
    text.box.color = "#ffffff";
    text.box.opacity = 1.0;
    text.box.align = "center";
    text.box.valign = "middle";
    SceneTextLine line;
    line.runs = RichLine{TextSpan{"Hi", {}}};
    text.lines.push_back(std::move(line));

    auto paint_at = [&](qreal outer_opacity) {
        QImage img(200, 80, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setOpacity(outer_opacity);
        seventhslide::render::paint_text_content(p, text);
        p.end();
        return img;
    };

    const QImage full = paint_at(1.0);
    const QImage half = paint_at(0.5);

    // Find a lit pixel in the full-opacity render and compare to the half-fade.
    int fx = -1;
    int fy = -1;
    int full_lum = 0;
    for (int y = 0; y < full.height(); ++y) {
        for (int x = 0; x < full.width(); ++x) {
            const QRgb px = full.pixel(x, y);
            const int lum = qRed(px) + qGreen(px) + qBlue(px);
            if (lum > full_lum) {
                full_lum = lum;
                fx = x;
                fy = y;
            }
        }
    }
    REQUIRE(fx >= 0);
    REQUIRE(full_lum > 120);

    const QRgb half_px = half.pixel(fx, fy);
    const int half_lum = qRed(half_px) + qGreen(half_px) + qBlue(half_px);
    // Must be clearly dimmed vs full (opacity compose), not equally bright.
    REQUIRE(half_lum < full_lum * 0.75);
    REQUIRE(half_lum > full_lum * 0.20);
}

TEST_CASE("FadePresenter does not restart a fade as a video plays", "[render][video]")
{
    // Each video tick re-presents the frame with the new playback position. The
    // position is not painted (the frame image is), so it is not new content.
    FadePresenter fp;
    auto playing = make_frame("video", SceneMode::Video, true, 300, "");
    playing.video = seventhslide::scene::SceneVideoMedia{};
    playing.video->url = "/static/videos/clip.mp4";
    playing.video->position_sec = 1.0;
    REQUIRE(fp.present(seventhslide::scene::share_frame(playing)) == FadePresentAction::Snap);

    auto later = playing;
    later.video->position_sec = 1.033;
    REQUIRE(fp.present(seventhslide::scene::share_frame(later)) == FadePresentAction::Noop);
    REQUIRE_FALSE(fp.is_fading());

    auto paused = later;
    paused.video->autoplay = false;
    REQUIRE(fp.present(seventhslide::scene::share_frame(paused)) == FadePresentAction::Noop);
    REQUIRE_FALSE(fp.is_fading());

    // A different clip is new content and still cross-fades.
    auto other = later;
    other.video->url = "/static/videos/other.mp4";
    REQUIRE(fp.present(seventhslide::scene::share_frame(other)) == FadePresentAction::StartFade);
}

namespace {

/// The reference for what is new content: the whole frame compared with
/// ``==``, after a copy of one side takes the other's freeze and video clock.
[[nodiscard]] bool paints_same_as_whole(const SceneFrame& a, const SceneFrame& b)
{
    const bool same_clock =
        a.is_frozen == b.is_frozen
        && (!a.video || !b.video
            || (a.video->position_sec == b.video->position_sec
                && a.video->autoplay == b.video->autoplay));
    if (same_clock) {
        return a == b;
    }
    SceneFrame probe = b;
    probe.is_frozen = a.is_frozen;
    if (probe.video && a.video) {
        probe.video->position_sec = a.video->position_sec;
        probe.video->autoplay = a.video->autoplay;
    }
    return a == probe;
}

/// ``frame`` without the stacks below: the top stack's layer alone.
[[nodiscard]] SceneFrame without_stacks_below(SceneFrame frame)
{
    frame.stacked_below.clear();
    return frame;
}

}  // namespace

TEST_CASE("FadePresenter counts a change of anything but freeze, video clock and stacks below as new content", "[render][video]")
{
    // One change of each field a frame has, and of each a video has (none
    // once there is no video or text to change).
    using Change = std::function<void(SceneFrame&)>;
    const std::vector<Change> changes = {
        [](SceneFrame&) {},
        [](SceneFrame& f) { f.output_id = "out-2"; },
        [](SceneFrame& f) { f.output_name = "Stage"; },
        [](SceneFrame& f) { f.canvas_width = 400; },
        [](SceneFrame& f) { f.canvas_height = 300; },
        [](SceneFrame& f) { f.mode = SceneMode::Song; },
        [](SceneFrame& f) { f.slide_index = 2; },
        [](SceneFrame& f) { f.stack_id = "titles"; },
        [](SceneFrame& f) { f.transition_key = "item:2"; },
        [](SceneFrame& f) { f.is_blank = true; },
        [](SceneFrame& f) { f.is_frozen = true; },
        [](SceneFrame& f) { f.background.color = "#ff0000"; },
        [](SceneFrame& f) {
            if (f.text) {
                f.text->lines.front().runs.front().text = "Other";
            }
        },
        [](SceneFrame& f) { f.text.reset(); },
        [](SceneFrame& f) { f.overlay.boxes.emplace_back(); },
        [](SceneFrame& f) {
            seventhslide::scene::SceneObject clock;
            clock.object = seventhslide::test::scene_clock();
            f.objects.push_back(clock);
        },
        [](SceneFrame& f) { f.image = seventhslide::scene::SceneImageMedia{.url = "/static/a.jpg"}; },
        [](SceneFrame& f) { f.video.reset(); },
        [](SceneFrame& f) { if (f.video) { f.video->source = "content 2"; } },
        [](SceneFrame& f) { if (f.video) { f.video->url = "/static/videos/other.mp4"; } },
        [](SceneFrame& f) { if (f.video) { f.video->loop = true; } },
        [](SceneFrame& f) { if (f.video) { f.video->autoplay = false; } },
        [](SceneFrame& f) { if (f.video) { f.video->position_sec = 2.5; } },
        [](SceneFrame& f) { if (f.video) { f.video->area.width = 50; } },
        [](SceneFrame& f) { f.web = seventhslide::scene::SceneWebMedia{.url = "https://a"}; },
        [](SceneFrame& f) { f.live = seventhslide::scene::SceneLiveMedia{.source = "ndi:a"}; },
        [](SceneFrame& f) { f.media_stand_in = seventhslide::scene::SceneMediaStandIn{}; },
        [](SceneFrame& f) {
            f.media_stand_in = seventhslide::scene::SceneMediaStandIn{.bars = true};
        },
        [](SceneFrame& f) {
            f.stacked_below.push_back(seventhslide::scene::share_frame(
                make_frame("song", SceneMode::Song, true, 300, "Below")));
        },
        [](SceneFrame& f) { f.content_transition.duration_ms = 900; },
        [](SceneFrame& f) { f.builds_end_ms = 700; },
        [](SceneFrame& f) { f.background_transition.duration_ms = 900; },
    };
    auto base = make_frame("video", SceneMode::Video, true, 300, "Words");
    base.video = seventhslide::scene::SceneVideoMedia{.source = "content 1",
                                                      .url = "/static/videos/clip.mp4"};
    base.video->position_sec = 1.0;
    // One side changed in one way and the other in up to two: a freeze or a
    // clock beside another field, or two fields at once.
    int same = 0;
    int differ = 0;
    for (const auto& first : changes) {
        auto a = base;
        first(a);
        for (const auto& second : changes) {
            for (const auto& third : changes) {
                auto b = base;
                second(b);
                third(b);
                FadePresenter fp;
                (void)fp.present(seventhslide::scene::share_frame(a));
                const bool noop =
                    fp.present(seventhslide::scene::share_frame(b)) == FadePresentAction::Noop;
                const bool expected =
                    paints_same_as_whole(without_stacks_below(a), without_stacks_below(b));
                if (noop != expected) {
                    FAIL_CHECK("FadePresenter tells new content otherwise than the whole frame's ==");
                }
                (expected ? same : differ) += 1;
            }
        }
    }
    // Both answers came up, many times.
    CHECK(same > 100);
    CHECK(differ > 100);
}

TEST_CASE("scene_frame_has_clock finds clocks on the frame and stacks below", "[render][clock]")
{
    auto frame = make_frame("song", SceneMode::Song, false, 0, "Line");
    REQUIRE_FALSE(seventhslide::render::scene_frame_has_clock(frame));

    auto lower = make_frame("song", SceneMode::Song, false, 0, "Below");
    seventhslide::scene::SceneObject clock;
    clock.object = seventhslide::test::scene_clock();
    lower.objects.push_back(clock);
    frame.stacked_below.push_back(seventhslide::scene::share_frame(lower));
    REQUIRE(seventhslide::render::scene_frame_has_clock(frame));

    frame.stacked_below.clear();
    frame.objects.push_back(clock);
    REQUIRE(seventhslide::render::scene_frame_has_clock(frame));
}

TEST_CASE("Fade through black shows neither slide at its middle", "[render][transition]")
{
    seventhslide::test::ensure_qt_application();
    const auto ink_at = [](seventhslide::model::TransitionType type, double t) {
        seventhslide::render::ScenePresenter presenter;
        auto first = make_frame("song", SceneMode::Song, true, 400, "One");
        auto second = make_frame("song", SceneMode::Song, true, 400, "Two");
        first.content_transition.type = type;
        second.content_transition.type = type;
        presenter.present(seventhslide::scene::share_frame(first));
        presenter.present(seventhslide::scene::share_frame(second));
        REQUIRE(presenter.is_fading());
        presenter.set_fade_progress_for_test(t);
        const QImage& img = presenter.surface(QSize(200, 100), QColor(Qt::black));
        int lit = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                lit += qGray(img.pixel(x, y)) > 40 ? 1 : 0;
            }
        }
        return lit;
    };
    using seventhslide::model::TransitionType;
    // Through black, the middle is the black between them; a fade's middle
    // has both half there.
    CHECK(ink_at(TransitionType::FadeThroughBlack, 0.5) == 0);
    CHECK(ink_at(TransitionType::Fade, 0.5) > 0);
    CHECK(ink_at(TransitionType::FadeThroughBlack, 0.9) > 0);
}

TEST_CASE("Each stack changes by its own transition", "[render][transition][stacks]")
{
    seventhslide::test::ensure_qt_application();
    // Lyrics on the back stack, a title on the front one. The lyrics fade;
    // the title cuts, and it does not change.
    const auto output = [](const char* words) {
        auto lyrics = make_frame("song", SceneMode::Song, true, 400, words);
        lyrics.stack_id = "main";
        auto title = make_frame("song", SceneMode::Song, false, 400, "Title");
        title.stack_id = "titles";
        title.background.type = seventhslide::layout::BackgroundType::Transparent;
        title.stacked_below = {seventhslide::scene::share_frame(lyrics)};
        return seventhslide::scene::share_frame(title);
    };
    seventhslide::render::ScenePresenter presenter;
    REQUIRE(presenter.present(output("One")));
    REQUIRE_FALSE(presenter.is_fading());
    // Before stacks had their own, a change under a top stack that cuts
    // was a cut.
    REQUIRE(presenter.present(output("Two")));
    CHECK(presenter.is_fading());
    presenter.set_fade_progress_for_test(1.0);
    CHECK_FALSE(presenter.is_fading());

    // A stack that comes or goes cuts.
    auto alone = make_frame("song", SceneMode::Song, true, 400, "Three");
    alone.stack_id = "main";
    REQUIRE(presenter.present(seventhslide::scene::share_frame(alone)));
    CHECK(presenter.is_fading());  // the lyrics fade from Two to Three
    presenter.set_fade_progress_for_test(1.0);
    auto fresh = make_frame("song", SceneMode::Song, true, 400, "Four");
    fresh.stack_id = "other";
    REQUIRE(presenter.present(seventhslide::scene::share_frame(fresh)));
    CHECK_FALSE(presenter.is_fading());
    CHECK(presenter.current_frame()->stack_id == "other");
}

TEST_CASE("A layered output's stacks are shown as shared, not copied", "[render][stacks]")
{
    seventhslide::test::ensure_qt_application();
    auto lyrics = make_frame("song", SceneMode::Song, true, 400, "Words");
    lyrics.stack_id = "main";
    const auto below = seventhslide::scene::share_frame(lyrics);
    auto title = make_frame("song", SceneMode::Song, false, 400, "Title");
    title.stack_id = "titles";
    title.stacked_below = {below};
    const auto whole = seventhslide::scene::share_frame(std::move(title));

    seventhslide::render::ScenePresenter presenter;
    REQUIRE(presenter.present(whole));
    // The stack below: held here, in the frame, and as its layer. The frame:
    // held here, as what is shown, and as the top stack's layer. A layer of
    // either copied would leave it held twice.
    CHECK(below.use_count() == 3);
    CHECK(whole.use_count() == 3);
    CHECK(presenter.current_frame() == whole.get());
}

TEST_CASE("Another item's video fades out showing its own pictures", "[render][transition][video]")
{
    seventhslide::test::ensure_qt_application();
    auto video = make_frame("video", SceneMode::Video, true, 400, "");
    video.text.reset();
    video.video = seventhslide::scene::SceneVideoMedia{.source = "content 1",
                                                       .url = "/static/videos/clip.mp4"};
    video.video->area = {.enabled = true, .x = 0, .y = 0, .width = 200, .height = 100};
    auto song = make_frame("song", SceneMode::Song, true, 400, "");
    song.text.reset();

    seventhslide::render::ScenePresenter presenter;
    QImage red(200, 100, QImage::Format_RGB32);
    red.fill(QColor(Qt::red));
    REQUIRE(presenter.present(seventhslide::scene::share_frame(video)));
    // The video's player hands in its pictures under its own source, and goes
    // on doing so while it plays out over the change.
    presenter.set_source_frames({{"content 1", {red, {}}}});
    // The song comes in.
    REQUIRE(presenter.present(seventhslide::scene::share_frame(song)));
    REQUIRE(presenter.is_fading());

    const auto red_at = [&](double t) {
        presenter.set_fade_progress_for_test(t);
        const QImage& img = presenter.surface(QSize(200, 100), QColor(Qt::black));
        return qRed(img.pixel(100, 50));
    };
    const int early = red_at(0.25);
    const int late = red_at(0.75);
    CHECK(early > 120);
    CHECK(late < early);
    CHECK(late > 0);
}

namespace {

/// A frame of stack ``stack`` showing video item ``source`` in ``area``.
SceneFrame video_frame(const std::string& stack, const std::string& source,
                       seventhslide::scene::SceneMediaArea area)
{
    auto frame = make_frame("video", SceneMode::Video, true, 400, "");
    frame.text.reset();
    frame.stack_id = stack;
    frame.video = seventhslide::scene::SceneVideoMedia{.source = source,
                                                       .url = "/static/videos/" + source + ".mp4"};
    frame.video->area = area;
    return frame;
}

QImage solid_picture(Qt::GlobalColor colour)
{
    QImage img(200, 100, QImage::Format_RGB32);
    img.fill(QColor(colour));
    return img;
}

}  // namespace

TEST_CASE("Each stack shows its own video, the one below as much as the top",
          "[render][video][stacks]")
{
    seventhslide::test::ensure_qt_application();
    const auto half = [](int x) {
        return seventhslide::scene::SceneMediaArea{.enabled = true, .x = x, .y = 0,
                                                   .width = 100, .height = 100};
    };
    auto below = video_frame("back", "content 1", half(0));
    auto top = video_frame("front", "content 2", half(100));
    // The session clears an upper layer's wipe so the stacks behind show.
    top.background.type = seventhslide::layout::BackgroundType::Transparent;
    top.stacked_below.push_back(seventhslide::scene::share_frame(below));

    seventhslide::render::ScenePresenter presenter;
    REQUIRE(presenter.present(seventhslide::scene::share_frame(top)));
    presenter.set_source_frames({{"content 1", {solid_picture(Qt::red), {}}},
                                 {"content 2", {solid_picture(Qt::blue), {}}}});
    const QImage& img = presenter.surface(QSize(200, 100), QColor(Qt::black));
    CHECK(qRed(img.pixel(50, 50)) > 200);
    CHECK(qBlue(img.pixel(50, 50)) < 50);
    CHECK(qBlue(img.pixel(150, 50)) > 200);
    CHECK(qRed(img.pixel(150, 50)) < 50);
}

TEST_CASE("One video gives way to another on a stack, both moving", "[render][transition][video]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::scene::SceneMediaArea whole{.enabled = true, .x = 0, .y = 0,
                                                    .width = 200, .height = 100};
    seventhslide::render::ScenePresenter presenter;
    REQUIRE(presenter.present(
        seventhslide::scene::share_frame(video_frame("main", "content 1", whole))));
    // Both items' players hand in their pictures: the one going plays out.
    presenter.set_source_frames({{"content 1", {solid_picture(Qt::red), {}}},
                                 {"content 2", {solid_picture(Qt::blue), {}}}});
    REQUIRE(presenter.present(
        seventhslide::scene::share_frame(video_frame("main", "content 2", whole))));
    REQUIRE(presenter.is_fading());

    presenter.set_fade_progress_for_test(0.5);
    const QImage& mid = presenter.surface(QSize(200, 100), QColor(Qt::black));
    // Half way: both show, the one going under the one coming (each side
    // brings its own black plate, so the going one is dimmed twice).
    CHECK(qRed(mid.pixel(100, 50)) > 16);
    CHECK(qBlue(mid.pixel(100, 50)) > 100);

    presenter.set_fade_progress_for_test(1.0);
    const QImage& done = presenter.surface(QSize(200, 100), QColor(Qt::black));
    CHECK(qBlue(done.pixel(100, 50)) > 200);
    CHECK(qRed(done.pixel(100, 50)) < 50);
}

TEST_CASE("A change during a change fades from the picture as it stands", "[render][transition]")
{
    seventhslide::test::ensure_qt_application();
    const auto slide = [](const char* words, const char* colour) {
        auto frame = make_frame("song", SceneMode::Song, true, 400, words);
        frame.text->box.color = colour;
        return seventhslide::scene::share_frame(frame);
    };
    const auto count = [](const QImage& img, auto&& is) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                n += is(QColor(img.pixel(x, y))) ? 1 : 0;
            }
        }
        return n;
    };
    const auto reddish = [](const QColor& c) { return c.red() > 60 && c.red() > c.blue() + 20; };

    seventhslide::render::ScenePresenter presenter;
    REQUIRE(presenter.present(slide("AAAA", "#ff0000")));
    REQUIRE(presenter.present(slide("BBBB", "#0000ff")));
    presenter.set_fade_progress_for_test(0.5);
    const int red_mid = count(presenter.surface(QSize(200, 100), QColor(Qt::black)), reddish);
    REQUIRE(red_mid > 0);

    // The next slide comes before that change is done: it starts from the
    // half-faded picture, red words and all, not from the blue slide alone.
    REQUIRE(presenter.present(slide("CCCC", "#00ff00")));
    REQUIRE(presenter.is_fading());
    presenter.set_fade_progress_for_test(0.0);
    CHECK(count(presenter.surface(QSize(200, 100), QColor(Qt::black)), reddish) > red_mid / 2);
    presenter.set_fade_progress_for_test(1.0);
    CHECK(count(presenter.surface(QSize(200, 100), QColor(Qt::black)), reddish) == 0);
}

TEST_CASE("Moving transitions put each slide where the motion has it", "[render][transition]")
{
    seventhslide::test::ensure_qt_application();
    using seventhslide::model::TransitionDirection;
    using seventhslide::model::TransitionType;
    const auto slide = [](const char* colour, TransitionType type) {
        auto frame = make_frame("song", SceneMode::Song, true, 400, "WWWWWWWWWWWW");
        frame.text->box.color = colour;
        frame.content_transition.type = type;
        frame.content_transition.direction = TransitionDirection::Right;
        return seventhslide::scene::share_frame(frame);
    };
    // Where the red and the blue words are, as columns of the 200 px canvas.
    const auto columns = [&](TransitionType type) {
        seventhslide::render::ScenePresenter presenter;
        REQUIRE(presenter.present(slide("#ff0000", type)));
        REQUIRE(presenter.present(slide("#0000ff", type)));
        REQUIRE(presenter.is_fading());
        presenter.set_fade_progress_for_test(0.5);
        const QImage img = presenter.surface(QSize(200, 100), QColor(Qt::black));
        std::pair<int, int> red{1000, -1};
        std::pair<int, int> blue{1000, -1};
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QColor c(img.pixel(x, y));
                auto* range = c.red() > 120 && c.blue() < 60   ? &red
                              : c.blue() > 120 && c.red() < 60 ? &blue
                                                               : nullptr;
                if (range != nullptr) {
                    range->first = std::min(range->first, x);
                    range->second = std::max(range->second, x);
                }
            }
        }
        return std::pair{red, blue};
    };

    // Push from the right, half way: the red words half gone off the left,
    // the blue half in from the right.
    const auto [push_red, push_blue] = columns(TransitionType::Push);
    CHECK(push_red.second < 100);
    CHECK(push_blue.first >= 100);
    // Wipe from the right: the red stays put, the blue only behind the edge.
    const auto [wipe_red, wipe_blue] = columns(TransitionType::Wipe);
    CHECK(wipe_red.first < 100);
    CHECK(wipe_blue.first >= 100);
}
