// What presenting a frame costs the window's thread: a layered output's
// frame, and a playing video's, presented again as the session does (a clock's
// tick, the video's clock moving). Hidden from the default run:
//   ./seventhslide_tests_dev "[.bench]"
// Prints microseconds per present; asserts nothing about speed.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "test_qt_app.hpp"
#include "theme_test_helpers.hpp"

#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using seventhslide::scene::SceneFrame;
using seventhslide::scene::SharedSceneFrame;

namespace {

/// Microseconds per present of ``frames``, in turn, to a presenter already
/// showing the first. The frames are made beforehand, as the session hands in
/// a fresh one each time, so only the present is timed.
double us_per_present(const std::vector<SharedSceneFrame>& frames)
{
    seventhslide::render::ScenePresenter presenter;
    presenter.present(frames.front());
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 1; i < frames.size(); ++i) {
        presenter.present(frames[i]);
    }
    const auto end = std::chrono::steady_clock::now();
    REQUIRE_FALSE(presenter.is_fading());
    return std::chrono::duration<double, std::micro>(end - start).count()
           / static_cast<double>(frames.size() - 1);
}

template <typename Fn>
std::vector<SharedSceneFrame> make_frames(const SceneFrame& from, int count, Fn&& vary)
{
    std::vector<SharedSceneFrame> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        SceneFrame frame = from;
        // Each composed afresh, as the session's are.
        for (auto& below : frame.stacked_below) {
            below = seventhslide::scene::share_frame(*below);
        }
        vary(frame, i);
        out.push_back(seventhslide::scene::share_frame(std::move(frame)));
    }
    return out;
}

void report_us(const char* what, double us)
{
    std::printf("  %-52s %9.2f us\n", what, us);
}

/// The output's frame with a song on the back stack and a passage on the front,
/// as the session composes it.
SceneFrame layered_frame(seventhslide::session::Session& session)
{
    seventhslide::model::OutputConfig oc;
    oc.id = "out-Main";
    oc.name = "Main";
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    seventhslide::test::set_look(oc, {{"font_family", "Liberation Sans"}, {"font_size", 64}, {"show_bible_text", true}});
    (void)seventhslide::theme::seed_default_themes(oc);
    session.set_outputs({oc});
    auto stacks = session.stack_defs();
    const auto lobby = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(lobby.ok);
    session.set_stacks(stacks, session.type_defaults());

    seventhslide::model::Song song;
    song.title = "A Mighty Fortress Is Our God";
    for (int l = 0; l < 4; ++l) {
        song.lyrics += "A mighty fortress is our God, a bulwark never failing line "
                       + std::to_string(l) + "\n";
    }
    session.play_song(song, std::string(seventhslide::model::k_default_stack_id));
    seventhslide::model::BiblePassage bible;
    bible.book = "John";
    bible.chapter = 3;
    bible.verse_start = 16;
    bible.verse_end = 17;
    bible.ref = "John 3:16-17";
    bible.verses = {{16, "For God so loved the world, that he gave his only begotten Son."},
                    {17, "For God sent not his Son into the world to condemn the world."}};
    session.play_bible(bible, lobby.id);
    auto frame = session.scene_frame(oc.id);
    REQUIRE(frame.has_value());
    REQUIRE(frame->stacked_below.size() == 1);
    return *frame;
}

/// ``frame`` showing a video item instead of its words.
void show_video(SceneFrame& frame, const std::string& source)
{
    frame.mode = seventhslide::scene::SceneMode::Video;
    frame.text.reset();
    frame.video = seventhslide::scene::SceneVideoMedia{.source = source,
                                                       .url = "/static/videos/" + source + ".mp4"};
}

}  // namespace

TEST_CASE("bench: presenting a frame", "[.bench]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    seventhslide::session::Session session(measurer);
    const SceneFrame layered = layered_frame(session);
    SceneFrame single = layered;
    single.stacked_below.clear();
    constexpr int k_presents = 2000;
    const auto same = [](SceneFrame&, int) {};

    std::printf("\npresenting a frame (one 1080p output; what the window's thread spends)\n");
    report_us("one stack, the same frame again (a clock's tick)",
              us_per_present(make_frames(single, k_presents, same)));
    report_us("two stacks, the same frame again (a clock's tick)",
              us_per_present(make_frames(layered, k_presents, same)));
    // A busy slide on top: forty objects, each a laid-out text box.
    SceneFrame busy = layered;
    {
        seventhslide::scene::SceneObject box;
        seventhslide::layout::OverlayBox text;
        text.x = 100;
        text.y = 100;
        text.w = 800;
        text.h = 200;
        text.lines = seventhslide::model::parse_markup_lines(
            "A line of words on a busy slide\nlaid out as the frame is built");
        text.layout = seventhslide::layout::lay_out_overlay(text, measurer);
        box.text = text;
        for (int i = 0; i < 40; ++i) {
            box.id = "box " + std::to_string(i);
            box.z = i;
            busy.objects.push_back(box);
        }
    }
    const auto copy_us = [](const SceneFrame& frame) {
        const auto copies = make_frames(frame, 50, [](SceneFrame&, int) {});
        std::size_t sink = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i) {
            const SceneFrame copy = *copies[static_cast<std::size_t>(i) % copies.size()];
            sink += copy.objects.size();
        }
        const auto end = std::chrono::steady_clock::now();
        REQUIRE(sink > 0);
        return std::chrono::duration<double, std::micro>(end - start).count() / 1000.0;
    };
    report_us("copying the layered frame (for scale)", copy_us(layered));
    report_us("copying the busy layered frame (for scale)", copy_us(busy));
    std::printf("  (the passage's frame has %zu objects; the busy one %zu)\n",
                layered.objects.size(), busy.objects.size());
    report_us("two stacks, a busy slide on top, the same again",
              us_per_present(make_frames(busy, k_presents / 4, same)));
    // The top stack frozen and thawed in turn: only its freeze moves.
    report_us("two stacks, the busy slide frozen and thawed in turn",
              us_per_present(make_frames(busy, k_presents / 4, [](SceneFrame& frame, int i) {
                  frame.is_frozen = i % 2 == 1;
              })));

    // A playing video's clock moves with every frame the session composes.
    SceneFrame video = single;
    show_video(video, "content 1");
    report_us("one stack, a video whose clock moved",
              us_per_present(make_frames(video, k_presents, [](SceneFrame& frame, int i) {
                  frame.video->position_sec = i / 30.0;
              })));
    // A video on the back stack under the passage.
    SceneFrame over_video = layered;
    {
        SceneFrame below = *over_video.stacked_below.front();
        show_video(below, "content 1");
        over_video.stacked_below.front() = seventhslide::scene::share_frame(std::move(below));
    }
    report_us("two stacks, the video's clock moved below",
              us_per_present(make_frames(over_video, k_presents, [](SceneFrame& frame, int i) {
                  SceneFrame below = *frame.stacked_below.front();
                  below.video->position_sec = i / 30.0;
                  frame.stacked_below.front() = seventhslide::scene::share_frame(std::move(below));
              })));
}
