// Builds: a slide's animations as steps, the session's
// cursor through them, the frames each step shows, and how a build paints.
#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/freeform/builds.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/render/image_painter.hpp>
#include <seventhslide/render/scene_presenter.hpp>
#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/session/player_controller.hpp>
#include <seventhslide/session/session.hpp>

#include <nlohmann/json.hpp>

#include <QColor>
#include <QImage>
#include <QPainter>

#include <set>
#include <string>
#include <vector>

using seventhslide::canvas::Animation;
using seventhslide::canvas::AnimationEffect;
using seventhslide::canvas::AnimationTrigger;
using seventhslide::freeform::ObjectAnimations;
using seventhslide::freeform::plan_steps;

namespace {

Animation anim(int order, AnimationEffect effect, AnimationTrigger trigger, int duration = 400,
               int delay = 0)
{
    Animation a;
    a.order = order;
    a.effect = effect;
    a.trigger = trigger;
    a.duration_ms = duration;
    a.delay_ms = delay;
    return a;
}

using Ids = std::set<std::string>;

/// A one-slide deck on ``out-main``: a title there from the start, a box
/// that flies in on the first click with a line fading in after it, and the
/// title fading out on the second click.
seventhslide::freeform::Document deck()
{
    auto doc = seventhslide::freeform::make_document("Builds", {"out-main"});
    auto& slide = doc.decks[0].slides[0];
    const auto text = [](const char* id, const char* words, int z, std::vector<Animation> anims) {
        seventhslide::freeform::Object obj;
        obj.id = id;
        obj.type = seventhslide::canvas::ObjectType::Text;
        obj.text = words;
        obj.x = 100;
        obj.y = 100 * z;
        obj.w = 800;
        obj.h = 90;
        obj.z = z;
        obj.font_color = "#ffffff";
        obj.animations = std::move(anims);
        return obj;
    };
    slide.objects = {
        text("title", "Welcome", 1, {anim(3, AnimationEffect::FadeOut, AnimationTrigger::OnClick)}),
        text("box", "Potluck", 2, {anim(1, AnimationEffect::FlyIn, AnimationTrigger::OnClick)}),
        text("line", "Prayer", 3, {anim(2, AnimationEffect::Fade, AnimationTrigger::AfterPrevious, 300, 100)}),
    };
    return doc;
}

}  // namespace

TEST_CASE("After Previous waits for the longest animation before it", "[builds]")
{
    const std::vector<ObjectAnimations> objects{
        {"a", {anim(1, AnimationEffect::Fade, AnimationTrigger::OnClick, 3000)}},
        {"b", {anim(2, AnimationEffect::Fade, AnimationTrigger::WithPrevious, 500)}},
        {"c", {anim(3, AnimationEffect::Fade, AnimationTrigger::AfterPrevious, 400)}},
    };
    const auto steps = plan_steps(objects);
    REQUIRE(steps.size() == 2);
    REQUIRE(steps[1].plays.size() == 3);
    CHECK(steps[1].plays[2].start_ms == 3000);
}

TEST_CASE("A slide's animations are steps", "[builds]")
{
    const auto doc = deck();
    std::vector<ObjectAnimations> objects;
    for (const auto& obj : doc.decks[0].slides[0].objects) {
        objects.push_back({obj.id, obj.animations});
    }
    const auto steps = plan_steps(objects);
    REQUIRE(steps.size() == 3);
    // Step 0: the title alone; the box and line wait for their entrances.
    CHECK(steps[0].shown_before == Ids{"title"});
    CHECK(steps[0].plays.empty());
    CHECK(steps[0].shown_after == Ids{"title"});
    // Step 1: the box flies in, then the line fades in 100 ms after it ends.
    CHECK(steps[1].shown_after == Ids{"title", "box", "line"});
    REQUIRE(steps[1].plays.size() == 2);
    CHECK(steps[1].plays[0].object_id == "box");
    CHECK(steps[1].plays[0].start_ms == 0);
    CHECK(steps[1].plays[1].object_id == "line");
    CHECK(steps[1].plays[1].start_ms == 500);
    CHECK(steps[1].end_ms == 800);
    // Step 2: the title goes.
    CHECK(steps[2].shown_after == Ids{"box", "line"});
    CHECK(steps[2].shown_before.contains("title"));

    // No animations: one step, everything shown.
    const auto plain = plan_steps({{"a", {}}, {"b", {}}});
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].shown_after == Ids{"a", "b"});

    // With the slide: before any click, step 0 plays it.
    const auto with_slide = plan_steps({{"a", {anim(1, AnimationEffect::Zoom, AnimationTrigger::WithPrevious)}}});
    REQUIRE(with_slide.size() == 1);
    CHECK(with_slide[0].shown_before.empty());
    CHECK(with_slide[0].shown_after == Ids{"a"});
    CHECK(with_slide[0].plays.size() == 1);
}

TEST_CASE("Animations are stored with their object", "[builds][json]")
{
    const auto doc = deck();
    const auto back = seventhslide::freeform::Document::from_json(doc.to_json());
    const auto& objects = back.decks[0].slides[0].objects;
    REQUIRE(objects[2].animations.size() == 1);
    CHECK(objects[2].animations[0] == doc.decks[0].slides[0].objects[2].animations[0]);
    CHECK(objects[1].animations[0].effect == AnimationEffect::FlyIn);
    // An object with none stores none.
    seventhslide::freeform::Object plain;
    nlohmann::json j = plain;
    CHECK_FALSE(j.contains("animations"));
}

TEST_CASE("Next walks a slide's steps; previous walks them back", "[builds][session]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::model::OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    session.set_outputs({main});
    auto doc = deck();
    REQUIRE(seventhslide::freeform::insert_slide(doc, 1));
    seventhslide::session::LiveFreeformContent live;
    live.name = "Builds";
    live.document = doc;
    session.play_freeform(std::move(live));
    auto& stack = session.focused_stack().program;
    const auto& player = stack.player();
    REQUIRE(player.total_lines() == 2);
    REQUIRE(player.steps_at(0) == 3);
    REQUIRE(player.steps_at(1) == 1);

    const auto ids = [&] {
        const auto frames = session.scene_frames();
        Ids out;
        for (const auto& so : frames.front().objects) {
            out.insert(so.id);
        }
        return out;
    };
    CHECK(ids() == Ids{"title"});
    // A slide's picture (a thumbnail) is the slide fully built.
    {
        const auto picture = session.scene_frame_at({session.focused_stack_id()}, "out-main", 0);
        REQUIRE(picture);
        Ids shown;
        for (const auto& so : picture->objects) {
            shown.insert(so.id);
        }
        CHECK(shown == Ids{"box", "line"});
        CHECK(picture->builds_end_ms == 0);
    }
    // The first click: a step, on the same line; the box and line build in.
    REQUIRE(stack.next_slide());
    CHECK(player.line_cursor() == 0);
    CHECK(player.step() == 1);
    {
        const auto frame = session.scene_frames().front();
        CHECK(frame.builds_end_ms == 800);
        CHECK_FALSE(frame.content_transition.plays());
        int built = 0;
        for (const auto& so : frame.objects) {
            built += so.builds.empty() ? 0 : 1;
        }
        CHECK(built == 2);
    }
    // The second: the title goes, its exit on the frame until it has played.
    REQUIRE(stack.next_slide());
    CHECK(ids() == Ids{"title", "box", "line"});
    // Then the next slide.
    REQUIRE(stack.next_slide());
    CHECK(player.line_cursor() == 1);
    CHECK(player.step() == 0);
    // Back: onto the slide before, fully built, nothing playing.
    REQUIRE(stack.prev_slide());
    CHECK(player.line_cursor() == 0);
    CHECK(player.step() == 2);
    CHECK_FALSE(player.outputs().front().animate);
    CHECK(ids() == Ids{"box", "line"});
    CHECK(session.scene_frames().front().builds_end_ms == 0);
    // A step back shows that step as built.
    REQUIRE(stack.prev_slide());
    CHECK(player.step() == 1);
    CHECK(ids() == Ids{"title", "box", "line"});
    // A jump to a step, as built.
    REQUIRE(stack.jump_to_step(0, 0));
    CHECK(ids() == Ids{"title"});
    CHECK_FALSE(stack.jump_to_step(0, 5));
    // A rebuild keeps the place, as built.
    REQUIRE(stack.next_slide());
    session.rebuild();
    CHECK(session.focused_stack().program.player().step() == 1);
    CHECK_FALSE(session.focused_stack().program.player().outputs().front().animate);
    // At step 0 too: a rebuild plays nothing again.
    REQUIRE(stack.jump_to_step(0, 0));
    session.rebuild();
    CHECK(session.focused_stack().program.player().step() == 0);
    CHECK(session.scene_frames().front().builds_end_ms == 0);
    // Auto-advance waits for the step's builds.
    REQUIRE(stack.next_slide());
    CHECK(session.transition_ms_on(stack.id) >= 800);
}

TEST_CASE("A blank goes and comes by the slide's transition, its builds as built", "[builds][session]")
{
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::model::OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    seventhslide::model::OutputConfig stage;
    stage.name = "Stage";
    stage.id = "out-stage";
    session.set_outputs({main, stage});
    auto doc = seventhslide::freeform::make_document("Builds", {"out-main", "out-stage"});
    const Transition fade{.type = TransitionType::Fade, .duration_ms = 500};
    for (auto& each : doc.decks) {
        each.slides = deck().decks[0].slides;
        each.slides[0].transition = fade;
    }
    seventhslide::session::LiveFreeformContent live;
    live.document = doc;
    session.play_freeform(std::move(live));
    auto& stack = session.focused_stack().program;
    const auto frame_on = [&](const char* output_id) {
        const auto frame = session.scene_frame(output_id);
        REQUIRE(frame.has_value());
        return *frame;
    };

    // A click: the box and line build in, the slide staying put.
    REQUIRE(stack.next_slide());
    REQUIRE(frame_on("out-main").builds_end_ms > 0);
    REQUIRE_FALSE(frame_on("out-main").content_transition.plays());

    // Blanked, the slide goes by its own transition, not the click's cut.
    REQUIRE(session.toggle_output_blank("out-main"));
    const auto blanked = frame_on("out-main");
    CHECK(blanked.is_blank);
    CHECK(blanked.content_transition == fade);
    CHECK(blanked.builds_end_ms == 0);
    // Another output's builds play on.
    CHECK(frame_on("out-stage").builds_end_ms > 0);

    // Back, it comes in by that transition, as built: nothing plays again.
    REQUIRE(session.toggle_output_blank("out-main"));
    const auto back = frame_on("out-main");
    CHECK_FALSE(back.is_blank);
    CHECK(back.content_transition == fade);
    CHECK(back.builds_end_ms == 0);
    for (const auto& so : back.objects) {
        CHECK(so.builds.empty());
    }

    // The stack's blank and the global one settle their outputs the same way.
    REQUIRE(stack.next_slide());
    REQUIRE(frame_on("out-main").builds_end_ms > 0);
    REQUIRE(session.toggle_stack_blank(session.focused_stack_id()));
    CHECK(frame_on("out-main").content_transition == fade);
    CHECK(frame_on("out-stage").content_transition == fade);
    REQUIRE(session.toggle_stack_blank(session.focused_stack_id()));
    REQUIRE(stack.jump_to_step(0, 0));
    REQUIRE(stack.next_slide());
    session.set_global_blank(true);
    session.set_global_blank(false);
    CHECK(frame_on("out-main").builds_end_ms == 0);
    CHECK(frame_on("out-stage").builds_end_ms == 0);
}

TEST_CASE("Each output keeps the step on its own slide", "[builds][session]")
{
    using seventhslide::session::OutputNavState;
    seventhslide::session::PlayerController player;
    // A: one slide of three steps over both lines. B: a slide per line.
    player.reset_maps({{.output_id = "a", .line_to_slide = {0, 0}, .index = 0, .slide_steps = {3}},
                       {.output_id = "b", .line_to_slide = {0, 1}, .index = 0, .slide_steps = {1, 1}}},
                      2);
    const auto& a = player.outputs()[0];
    const auto& b = player.outputs()[1];
    REQUIRE(player.next_slide());
    REQUIRE(player.next_slide());
    CHECK(a.step == 2);
    CHECK(player.line_cursor() == 0);
    // B's slide changes; A's stays, as built, and plays nothing again.
    REQUIRE(player.next_slide());
    CHECK(player.line_cursor() == 1);
    CHECK(a.step == 2);
    CHECK_FALSE(a.animate);
    CHECK(b.index == 1);
    CHECK(b.step == 0);
    CHECK(b.animate);
    CHECK(player.step() == 2);
    CHECK(player.steps_at(1) == 3);
    CHECK_FALSE(player.next_slide());
    // A rebuild gives each its own step back.
    const auto kept = player.steps();
    player.reset_maps(player.outputs(), 2);
    player.restore(1, kept);
    CHECK(player.outputs()[0].step == 2);
    CHECK(player.outputs()[1].step == 0);

    // One output, a slide over two cue lines: a jump between them keeps the
    // step; a jump to the next slide starts it.
    seventhslide::session::PlayerController cues;
    cues.reset_maps({{.output_id = "a", .line_to_slide = {0, 0, 1}, .index = 0, .slide_steps = {2, 2}}},
                    3);
    REQUIRE(cues.next_slide());
    REQUIRE(cues.jump_to_line(1));
    CHECK(cues.step() == 1);
    CHECK_FALSE(cues.outputs().front().animate);
    REQUIRE(cues.jump_to_line(2));
    CHECK(cues.step() == 0);
    CHECK(cues.outputs().front().animate);
}

TEST_CASE("A build plays by its own clock within the change", "[builds][render]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::model::OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    session.set_outputs({main});
    // A red box that fades in on a click, over 400 ms after 100 ms.
    auto doc = seventhslide::freeform::make_document("Builds", {"out-main"});
    seventhslide::freeform::Object box;
    box.id = "box";
    box.type = seventhslide::canvas::ObjectType::Shape;
    box.x = 100;
    box.y = 100;
    box.w = 200;
    box.h = 200;
    box.fill.kind = seventhslide::canvas::FillKind::Solid;
    box.fill.color = "#ff0000";
    box.animations = {anim(1, AnimationEffect::Fade, AnimationTrigger::OnClick, 400, 100)};
    doc.decks[0].slides[0].objects = {box};
    seventhslide::session::LiveFreeformContent live;
    live.document = doc;
    session.play_freeform(std::move(live));

    seventhslide::render::ScenePresenter presenter;
    presenter.present(seventhslide::scene::share_frame(session.scene_frames().front()));
    const auto red = [&] {
        const QImage img = presenter.surface(QSize(960, 540), QColor(Qt::black));
        return qRed(img.pixel(100, 100));
    };
    CHECK(red() < 30);  // not there before the click
    REQUIRE(session.focused_stack().program.next_slide());
    REQUIRE(presenter.present(seventhslide::scene::share_frame(session.scene_frames().front())));
    REQUIRE(presenter.is_fading());
    presenter.set_fade_progress_for_test(0.1);  // 50 ms in: still waiting
    CHECK(red() < 30);
    presenter.set_fade_progress_for_test(0.6);  // half way through its fade
    const int mid = red();
    CHECK(mid > 40);
    CHECK(mid < 220);
    presenter.set_fade_progress_for_test(1.0);
    CHECK(red() > 230);
}

TEST_CASE("An object plays each of its builds in a step, and an exit stays gone", "[builds][render]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::model::OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    session.set_outputs({main});
    // A red box that fades in on a click and out after it; then a slide
    // that fades in.
    auto doc = seventhslide::freeform::make_document("Builds", {"out-main"});
    seventhslide::freeform::Object box;
    box.id = "box";
    box.type = seventhslide::canvas::ObjectType::Shape;
    box.x = 100;
    box.y = 100;
    box.w = 200;
    box.h = 200;
    box.fill.kind = seventhslide::canvas::FillKind::Solid;
    box.fill.color = "#ff0000";
    box.animations = {anim(1, AnimationEffect::Fade, AnimationTrigger::OnClick, 400),
                      anim(2, AnimationEffect::FadeOut, AnimationTrigger::AfterPrevious, 400)};
    doc.decks[0].slides[0].objects = {box};
    REQUIRE(seventhslide::freeform::insert_slide(doc, 1));
    doc.decks[0].slides[1].transition =
        seventhslide::model::Transition{.type = seventhslide::model::TransitionType::Fade, .duration_ms = 500};
    seventhslide::session::LiveFreeformContent live;
    live.document = doc;
    session.play_freeform(std::move(live));
    auto& stack = session.focused_stack().program;
    seventhslide::render::ScenePresenter presenter;
    presenter.present(seventhslide::scene::share_frame(session.scene_frames().front()));
    const auto red = [&] {
        const QImage img = presenter.surface(QSize(960, 540), QColor(Qt::black));
        return qRed(img.pixel(100, 100));
    };

    REQUIRE(stack.next_slide());
    const auto frame = session.scene_frames().front();
    REQUIRE(frame.objects.size() == 1);
    CHECK(frame.objects.front().builds.size() == 2);
    CHECK(frame.objects.front().exits());
    CHECK(frame.builds_end_ms == 800);
    REQUIRE(presenter.present(seventhslide::scene::share_frame(frame)));
    presenter.set_fade_progress_for_test(0.5);  // in, before it goes
    CHECK(red() > 230);
    presenter.set_fade_progress_for_test(1.0);
    CHECK(red() < 30);
    presenter.settle();
    CHECK(red() < 30);
    // The next slide fades in: the box, gone, does not come back to go again.
    REQUIRE(stack.next_slide());
    REQUIRE(presenter.present(seventhslide::scene::share_frame(session.scene_frames().front())));
    presenter.set_fade_progress_for_test(0.2);
    CHECK(red() < 30);
}

TEST_CASE("The editor adds, changes, orders and removes a slide's animations", "[builds][editor]")
{
    auto doc = deck();
    seventhslide::freeform::Editor ed;
    ed.load(doc, "out-main");
    REQUIRE(ed.slide_animations().size() == 3);

    // Two selected: the first on a click, the second with it, after the last.
    ed.select("title");
    ed.select("box", true);
    REQUIRE(ed.add_animation(AnimationEffect::Pulse));
    auto all = ed.slide_animations();
    REQUIRE(all.size() == 5);
    CHECK(all[3].object_id == "title");
    CHECK(all[3].animation.trigger == AnimationTrigger::OnClick);
    CHECK(all[4].object_id == "box");
    CHECK(all[4].animation.trigger == AnimationTrigger::WithPrevious);
    CHECK(all[4].animation.order == 5);

    // Changed in place.
    auto changed = all[1].animation;
    changed.effect = AnimationEffect::Zoom;
    changed.delay_ms = 250;
    REQUIRE(ed.set_animation(all[1].object_id, all[1].animation.order, changed));
    CHECK(ed.slide_animations()[1].animation.effect == AnimationEffect::Zoom);
    CHECK(ed.slide_animations()[1].animation.delay_ms == 250);

    // Moved: the last to the first, the order renumbered.
    REQUIRE(ed.move_animation(4, 0));
    all = ed.slide_animations();
    CHECK(all[0].object_id == "box");
    CHECK(all[0].animation.effect == AnimationEffect::Pulse);
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK(all[i].animation.order == static_cast<int>(i) + 1);
    }
    CHECK_FALSE(ed.move_animation(9, 0));

    // Removed, and the rest closed up.
    REQUIRE(ed.remove_animation(all[0].object_id, all[0].animation.order));
    all = ed.slide_animations();
    REQUIRE(all.size() == 4);
    CHECK(all[0].animation.order == 1);
    CHECK_FALSE(ed.remove_animation("box", 99));

    // Each is one undo step.
    REQUIRE(ed.undo());
    CHECK(ed.slide_animations().size() == 5);
}

TEST_CASE("A video that starts on a click takes a click of its own", "[builds][video]")
{
    // A box flying in on the first click, the video's click after it.
    const auto steps = plan_steps({{"box", {anim(1, AnimationEffect::FlyIn, AnimationTrigger::OnClick)}},
                                   {"clip", {}, true}});
    REQUIRE(steps.size() == 3);
    CHECK(steps[1].playing_after.empty());
    CHECK(steps[2].playing_after == Ids{"clip"});
    CHECK(steps[2].plays.empty());
    CHECK(steps[2].shown_after.contains("clip"));

    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::model::OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    session.set_outputs({main});
    auto doc = seventhslide::freeform::make_document("Video", {"out-main"});
    seventhslide::freeform::Object clip;
    clip.id = "clip";
    clip.type = seventhslide::canvas::ObjectType::Video;
    clip.filename = "clip.mp4";
    clip.w = 640;
    clip.h = 360;
    clip.video.start = seventhslide::canvas::VideoStart::OnClick;
    clip.video.loop = true;
    doc.decks[0].slides[0].objects = {clip};
    seventhslide::session::LiveFreeformContent live;
    live.document = doc;
    session.play_freeform(std::move(live));
    auto& stack = session.focused_stack().program;
    REQUIRE(stack.player().steps_at(0) == 2);

    const auto videos = [&] { return session.object_videos(); };
    REQUIRE(videos().size() == 1);
    CHECK(videos().front().stack_id == stack.id);
    CHECK(videos().front().video.filename == "clip.mp4");
    CHECK(videos().front().video.loop);
    CHECK_FALSE(videos().front().video.plays);
    CHECK_FALSE(session.scene_frames().front().objects.front().plays);
    REQUIRE(stack.next_slide());
    CHECK(videos().front().video.plays);
    CHECK(session.scene_frames().front().objects.front().plays);
    // A blank hides the video; it does not stop it.
    REQUIRE(session.toggle_output_blank("out-main"));
    CHECK(session.scene_frames().front().objects.empty());
    REQUIRE(videos().size() == 1);
    CHECK(videos().front().video.plays);
}

TEST_CASE("A video object paints its player's picture in its box", "[builds][video][render]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::scene::SceneFrame frame;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    seventhslide::scene::SceneObject so;
    so.id = "clip";
    seventhslide::canvas::Object clip;
    clip.type = seventhslide::canvas::ObjectType::Video;
    clip.filename = "clip.mp4";
    clip.object_fit = "fill";
    clip.x = 100;
    clip.w = 100;
    clip.h = 100;
    so.object = clip;
    so.plays = true;
    frame.objects = {so};
    const auto paint = [&](const seventhslide::render::PaintContext& ctx) {
        QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter p(&img);
        seventhslide::render::paint_scene_objects(p, frame, ctx);
        p.end();
        return img;
    };
    // Before its first picture: a black plate in its box, nothing beside it.
    QImage none = paint({});
    CHECK(none.pixelColor(150, 50) == QColor(Qt::black));
    CHECK(none.pixelColor(50, 50) == QColor(Qt::white));
    // Its picture, by its key.
    QImage red(16, 9, QImage::Format_RGB32);
    red.fill(Qt::red);
    seventhslide::render::SourceFrames sources{
        {seventhslide::scene::video_object_key("/static/videos/clip.mp4"), {red, {}}}};
    seventhslide::render::PaintContext ctx;
    ctx.source_frames = &sources;
    const QImage shown = paint(ctx);
    CHECK(shown.pixelColor(150, 50) == QColor(Qt::red));
    CHECK(seventhslide::render::frame_sources_of(frame).size() == 1);
}

TEST_CASE("A video's click comes before its exit, and a clip has one player", "[builds][video]")
{
    // On click, then going by a click of its own: the play comes first.
    const auto steps = plan_steps({{"clip", {anim(1, AnimationEffect::FadeOut, AnimationTrigger::OnClick)}, true}});
    REQUIRE(steps.size() == 3);
    CHECK(steps[1].plays.empty());
    CHECK(steps[1].playing_after == Ids{"clip"});
    CHECK(steps[1].shown_after.contains("clip"));
    CHECK_FALSE(steps[2].shown_after.contains("clip"));
    CHECK(steps[2].playing_after == Ids{"clip"});
    // In and out in one step: never left there to click.
    const auto gone = plan_steps({{"clip",
                                   {anim(1, AnimationEffect::Fade, AnimationTrigger::OnClick),
                                    anim(2, AnimationEffect::FadeOut, AnimationTrigger::AfterPrevious)},
                                   true}});
    CHECK(gone.size() == 2);

    // A deck on two outputs, each with its own object for one clip, the
    // first muted: one player, heard.
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::model::OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    auto stage = main;
    stage.name = "Stage";
    stage.id = "out-stage";
    session.set_outputs({main, stage});
    auto doc = seventhslide::freeform::make_document("Video", {"out-main", "out-stage"});
    REQUIRE(seventhslide::freeform::insert_slide(doc, 1));
    seventhslide::freeform::Object clip;
    clip.id = "one";
    clip.type = seventhslide::canvas::ObjectType::Video;
    clip.filename = "clip.mp4";
    clip.w = 640;
    clip.h = 360;
    clip.video.mute = true;
    doc.decks[0].slides[0].objects = {clip};
    clip.id = "two";
    clip.video.mute = false;
    doc.decks[1].slides[0].objects = {clip};
    seventhslide::session::LiveFreeformContent live;
    live.document = doc;
    session.play_freeform(live);
    auto videos = session.object_videos();
    REQUIRE(videos.size() == 1);
    CHECK_FALSE(videos.front().video.mute);
    const auto first_run = videos.front().video.run;
    // The same item again is another showing; a refresh is not.
    session.rebuild();
    CHECK(session.object_videos().front().video.run == first_run);
    session.play_freeform(live);
    CHECK(session.object_videos().front().video.run != first_run);
}

TEST_CASE("A Wipe holds for a text box's words", "[builds][render]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::scene::SceneFrame frame;
    frame.canvas_width = 400;
    frame.canvas_height = 100;
    seventhslide::scene::SceneObject so;
    so.id = "words";
    seventhslide::layout::OverlayBox box;
    box.w = 400;
    box.h = 100;
    box.font_size = 48;
    box.font_color = "#ffffff";
    box.text_align = "left";
    box.lines.push_back({{"MMMMMMMMMMMMMMMM"}});
    so.text = box;
    so.builds = {{.effect = AnimationEffect::Wipe,
                  .direction = seventhslide::model::TransitionDirection::Left,
                  .start_ms = 0,
                  .duration_ms = 1000}};
    frame.objects = {so};
    QImage img(400, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter p(&img);
        seventhslide::render::paint_scene_objects(p, frame, {}, 500.0);
    }
    const auto lit = [&](int from, int to) {
        int n = 0;
        for (int x = from; x < to; ++x) {
            for (int y = 0; y < 100; ++y) {
                n += qRed(img.pixel(x, y)) > 128 ? 1 : 0;
            }
        }
        return n;
    };
    CHECK(lit(0, 150) > 0);
    CHECK(lit(250, 400) == 0);
}
