// What the operator does to an output's picture (blank and freeze, its own or
// global) belongs to the output, whatever any stack is doing; a stack's blank
// and freeze belong to the stack. Many of these play content on a second
// stack after the operator acted, where the two kinds could come apart.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/scene/scene_frame.hpp>
#include <seventhslide/session/session.hpp>

#include <string>
#include <vector>

using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::model::ContentKind;
using seventhslide::model::OutputConfig;
using seventhslide::model::Song;
using seventhslide::scene::SceneFrame;
using seventhslide::session::LiveMediaContent;
using seventhslide::session::Session;

namespace {

const std::string k_main(seventhslide::model::k_default_stack_id);
constexpr const char* k_projector = "out-projector";
constexpr const char* k_stage = "out-stage";

/// Two outputs and two stacks: a song live on Main, and a Top stack above it
/// that has never shown anything.
struct Show {
    ApproxTextMeasurer measurer;
    Session session{measurer};
    std::string top;

    Show()
    {
        OutputConfig projector;
        projector.id = k_projector;
        projector.name = "Projector";
        OutputConfig stage;
        stage.id = k_stage;
        stage.name = "Stage";
        session.set_outputs({projector, stage});

        auto stacks = session.stack_defs();
        const auto added = seventhslide::model::add_stack(stacks, "Top", "info");
        REQUIRE(added.ok);
        top = added.id;
        session.set_stacks(stacks, session.type_defaults());

        Song song;
        song.title = "Hymn";
        song.lyrics = "---[Verse:1]---\nPraise\n";
        song.verse_order = "v1";
        session.play_song(song, k_main);
    }

    void show_image_on(const std::string& stack_id)
    {
        LiveMediaContent media;
        media.kind = ContentKind::Image;
        media.data = {{"filename", "pic.jpg"}};
        session.play_media(media, stack_id);
    }

    [[nodiscard]] SceneFrame frame(const char* output_id) const
    {
        const auto f = session.scene_frame(output_id);
        REQUIRE(f.has_value());
        return *f;
    }
};

/// The frame and the stack layers under it, back to front.
[[nodiscard]] std::vector<const SceneFrame*> layers(const SceneFrame& frame)
{
    std::vector<const SceneFrame*> out;
    for (const auto& below : frame.stacked_below) {
        out.push_back(below.get());
    }
    out.push_back(&frame);
    return out;
}

[[nodiscard]] bool fully_blank(const SceneFrame& frame)
{
    for (const auto* layer : layers(frame)) {
        if (!layer->is_blank || layer->text || layer->image || !layer->objects.empty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool shows_image(const SceneFrame& frame)
{
    for (const auto* layer : layers(frame)) {
        if (layer->image && !layer->is_blank) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string lyrics(const SceneFrame& frame)
{
    for (const auto* layer : layers(frame)) {
        if (layer->text && !layer->text->lines.empty()) {
            return seventhslide::model::plain_text(layer->text->lines.front().runs);
        }
    }
    return {};
}

}  // namespace

TEST_CASE("A blanked output stays blank when a stack that never played goes live", "[session][display]")
{
    Show show;
    REQUIRE(show.session.toggle_output_blank(k_projector));
    show.show_image_on(show.top);

    const auto blanked = show.frame(k_projector);
    REQUIRE(blanked.stacked_below.size() == 1);
    CHECK(fully_blank(blanked));
    CHECK(shows_image(show.frame(k_stage)));

    // One press undoes it on every stack: nothing is left blanked, nothing inverted.
    REQUIRE(show.session.toggle_output_blank(k_projector));
    const auto projector = show.frame(k_projector);
    CHECK_FALSE(projector.is_blank);
    CHECK(shows_image(projector));
    CHECK(lyrics(projector) == "Praise");
}

TEST_CASE("A blanked output with nothing on its stacks is an empty frame, blank", "[session][display]")
{
    ApproxTextMeasurer measurer;
    Session session{measurer};
    OutputConfig projector;
    projector.id = k_projector;
    projector.name = "Projector";
    session.set_outputs({projector});
    REQUIRE(session.toggle_output_blank(k_projector));

    const auto frame = session.scene_frame(k_projector);
    REQUIRE(frame.has_value());
    CHECK(frame->mode == seventhslide::scene::SceneMode::Empty);
    CHECK(frame->stacked_below.empty());
    CHECK(frame->is_blank);
}

TEST_CASE("A frozen output keeps its picture when a stack that never played goes live", "[session][display]")
{
    Show show;
    REQUIRE(show.session.toggle_output_freeze(k_projector));
    show.show_image_on(show.top);

    auto projector = show.frame(k_projector);
    CHECK(projector.is_frozen);
    CHECK_FALSE(shows_image(projector));
    CHECK(lyrics(projector) == "Praise");
    CHECK(shows_image(show.frame(k_stage)));

    REQUIRE(show.session.toggle_output_freeze(k_projector));
    projector = show.frame(k_projector);
    CHECK_FALSE(projector.is_frozen);
    CHECK(shows_image(projector));
}

TEST_CASE("Global blank and freeze hold when a stack that never played goes live", "[session][display]")
{
    Show show;

    SECTION("blank")
    {
        show.session.set_global_blank(true);
        show.show_image_on(show.top);
        CHECK(fully_blank(show.frame(k_projector)));
        CHECK(fully_blank(show.frame(k_stage)));
        show.session.set_global_blank(false);
        CHECK(shows_image(show.frame(k_projector)));
    }
    SECTION("freeze")
    {
        show.session.set_global_freeze(true);
        show.show_image_on(show.top);
        for (const char* output : {k_projector, k_stage}) {
            INFO(output);
            const auto frame = show.frame(output);
            CHECK(frame.is_frozen);
            CHECK_FALSE(shows_image(frame));
            CHECK(lyrics(frame) == "Praise");
        }
        show.session.set_global_freeze(false);
        CHECK(shows_image(show.frame(k_projector)));
    }
}

TEST_CASE("Blank and freeze hold on a stack added during the service", "[session][display]")
{
    Show show;
    REQUIRE(show.session.toggle_output_blank(k_projector));
    REQUIRE(show.session.toggle_output_freeze(k_stage));

    auto stacks = show.session.stack_defs();
    const auto added = seventhslide::model::add_stack(stacks, "Lower thirds", "text");
    REQUIRE(added.ok);
    show.session.set_stacks(stacks, show.session.type_defaults());
    show.show_image_on(added.id);

    CHECK(fully_blank(show.frame(k_projector)));
    const auto stage = show.frame(k_stage);
    CHECK(stage.is_frozen);
    CHECK_FALSE(shows_image(stage));
}

TEST_CASE("A frozen output keeps a layer whose stack is cleared while frozen", "[session][display]")
{
    Show show;
    show.show_image_on(show.top);
    REQUIRE(show.session.toggle_output_freeze(k_projector));

    REQUIRE(show.session.clear_stack(show.top));
    CHECK(shows_image(show.frame(k_projector)));
    CHECK_FALSE(shows_image(show.frame(k_stage)));

    REQUIRE(show.session.toggle_output_freeze(k_projector));
    CHECK_FALSE(shows_image(show.frame(k_projector)));
}

TEST_CASE("Blank still beats freeze on a frozen output's layers", "[session][display]")
{
    Show show;
    show.show_image_on(show.top);
    REQUIRE(show.session.toggle_output_freeze(k_projector));

    // A stack blanked after the freeze still blanks its own layer...
    REQUIRE(show.session.toggle_stack_blank(show.top));
    auto projector = show.frame(k_projector);
    CHECK(projector.is_frozen);
    CHECK_FALSE(shows_image(projector));
    CHECK(lyrics(projector) == "Praise");

    // ...and blanking the output blanks all of it, until the blank is lifted.
    REQUIRE(show.session.toggle_stack_blank(show.top));
    REQUIRE(show.session.toggle_output_blank(k_projector));
    CHECK(fully_blank(show.frame(k_projector)));
    REQUIRE(show.session.toggle_output_blank(k_projector));
    projector = show.frame(k_projector);
    CHECK(projector.is_frozen);
    CHECK(shows_image(projector));
}

TEST_CASE("An empty stack cannot be blanked or frozen", "[session][display]")
{
    // Holding nothing would hide whatever is sent to the stack next, with no
    // tab to show why.
    Show show;
    CHECK_FALSE(show.session.toggle_stack_freeze(show.top));
    CHECK_FALSE(show.session.toggle_stack_blank(show.top));
    CHECK_FALSE(show.session.stack_is_frozen(show.top));
    CHECK_FALSE(show.session.stack_is_blank(show.top));
    show.show_image_on(show.top);
    CHECK(shows_image(show.frame(k_projector)));

    // Once something is live it can be, and turning it off works whatever
    // the stack holds by then.
    REQUIRE(show.session.toggle_stack_freeze(show.top));
    REQUIRE(show.session.toggle_stack_blank(show.top));
    REQUIRE(show.session.clear_stack(show.top));
    CHECK_FALSE(show.session.stack_is_frozen(show.top));
    CHECK_FALSE(show.session.stack_is_blank(show.top));
}

TEST_CASE("Global blank and freeze set the toggle of every output not exempt", "[session][display]")
{
    for (const bool own : {false, true}) {
        for (const bool global : {false, true}) {
            for (const bool exempt : {false, true}) {
                INFO("own " << own << ", global " << global << ", exempt " << exempt);
                ApproxTextMeasurer measurer;
                Session session(measurer);
                OutputConfig oc;
                oc.id = k_projector;
                oc.name = "Projector";
                oc.exempt_from_global_blank = exempt;
                oc.exempt_from_global_freeze = exempt;
                session.set_outputs({oc});
                if (own) {
                    REQUIRE(session.toggle_output_blank(k_projector));
                    REQUIRE(session.toggle_output_freeze(k_projector));
                }
                session.set_global_blank(global);
                session.set_global_freeze(global);

                const bool expected = exempt ? own : global;
                CHECK(session.output_is_blank(k_projector) == expected);
                CHECK(session.output_is_frozen(k_projector) == expected);
                CHECK(session.global_blank() == (!exempt && global));
            }
        }
    }
}

TEST_CASE("Global blank lifts only when every output not exempt has it", "[session][display]")
{
    Show show;
    REQUIRE(show.session.toggle_output_blank(k_projector));
    CHECK_FALSE(show.session.global_blank());

    // One output blank, one not: global blanks the other, not lifts the one.
    show.session.toggle_global_blank();
    CHECK(show.session.global_blank());
    CHECK(fully_blank(show.frame(k_projector)));
    CHECK(fully_blank(show.frame(k_stage)));

    show.session.toggle_global_blank();
    CHECK_FALSE(show.session.global_blank());
    CHECK(lyrics(show.frame(k_projector)) == "Praise");
    CHECK(lyrics(show.frame(k_stage)) == "Praise");
}

TEST_CASE("Global blank and freeze leave exempt outputs and outputs off alone", "[session][display]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig projector;
    projector.id = k_projector;
    projector.name = "Projector";
    OutputConfig stage;
    stage.id = k_stage;
    stage.name = "Stage";
    stage.exempt_from_global_blank = true;
    stage.exempt_from_global_freeze = true;
    OutputConfig lobby;
    lobby.id = "out-lobby";
    lobby.name = "Lobby";
    session.set_outputs({projector, stage, lobby});
    REQUIRE(session.set_output_on("out-lobby", false));

    session.toggle_global_blank();
    session.toggle_global_freeze();
    CHECK(session.global_blank());
    CHECK(session.global_freeze());
    CHECK(session.output_is_blank(k_projector));
    CHECK(session.output_is_frozen(k_projector));
    CHECK_FALSE(session.output_is_blank(k_stage));
    CHECK_FALSE(session.output_is_frozen(k_stage));

    // Switched back on, the lobby shows what it is sent: it had no card to
    // see the global blank on.
    REQUIRE(session.set_output_on("out-lobby", true));
    CHECK_FALSE(session.output_is_blank("out-lobby"));
    CHECK_FALSE(session.global_blank());
}

TEST_CASE("An output's button lifts a global blank or freeze on that output", "[session][display]")
{
    Show show;
    show.session.toggle_global_blank();
    show.session.toggle_global_freeze();

    REQUIRE(show.session.toggle_output_blank(k_projector));
    REQUIRE(show.session.toggle_output_freeze(k_projector));
    show.show_image_on(show.top);
    CHECK(shows_image(show.frame(k_projector)));
    CHECK_FALSE(show.frame(k_projector).is_frozen);
    CHECK(fully_blank(show.frame(k_stage)));
    CHECK(show.frame(k_stage).is_frozen);
    CHECK_FALSE(show.session.global_blank());
    CHECK_FALSE(show.session.global_freeze());
}

TEST_CASE("Global blank and freeze lift after a second stack goes live", "[session][display]")
{
    // The same key on and off, with content arriving on another stack in
    // between, must leave every output showing it.
    Show show;
    show.session.toggle_global_blank();
    show.session.toggle_global_freeze();
    show.show_image_on(show.top);
    CHECK(fully_blank(show.frame(k_projector)));

    show.session.toggle_global_blank();
    show.session.toggle_global_freeze();
    for (const char* output : {k_projector, k_stage}) {
        INFO(output);
        const auto frame = show.frame(output);
        CHECK_FALSE(frame.is_blank);
        CHECK_FALSE(frame.is_frozen);
        CHECK(shows_image(frame));
    }
}

TEST_CASE("Clearing blank and freeze lifts every output's and every stack's", "[session][display]")
{
    Show show;
    show.show_image_on(show.top);
    REQUIRE(show.session.toggle_output_blank(k_projector));
    REQUIRE(show.session.toggle_output_freeze(k_stage));
    REQUIRE(show.session.toggle_stack_blank(show.top));
    REQUIRE(show.session.toggle_stack_freeze(k_main));

    show.session.clear_blank_freeze();
    CHECK_FALSE(show.session.stack_is_blank(show.top));
    CHECK_FALSE(show.session.stack_is_frozen(k_main));
    for (const char* output : {k_projector, k_stage}) {
        INFO(output);
        CHECK_FALSE(show.session.output_is_blank(output));
        CHECK_FALSE(show.session.output_is_frozen(output));
        const auto frame = show.frame(output);
        CHECK_FALSE(frame.is_frozen);
        CHECK(shows_image(frame));
        CHECK(lyrics(frame) == "Praise");
    }
}

TEST_CASE("Output toggles address an output by id", "[session][display]")
{
    Show show;
    // The name is a label: renaming an output must not lose what is done to it.
    CHECK_FALSE(show.session.toggle_output_blank("Projector"));
    CHECK_FALSE(show.session.toggle_output_freeze("Projector"));
    CHECK_FALSE(show.session.set_output_on("Projector", false));
    CHECK_FALSE(show.session.toggle_output_blank("out-missing"));

    REQUIRE(show.session.toggle_output_blank(k_projector));
    show.session.outputs()[0].name = "Renamed";
    show.session.rebuild();
    CHECK(show.session.output_is_blank(k_projector));
    CHECK(show.frame(k_projector).output_name == "Renamed");
    CHECK(fully_blank(show.frame(k_projector)));
}

TEST_CASE("An output switched off is painted nothing until it is on", "[session][display]")
{
    Show show;
    REQUIRE(show.session.scene_frames().size() == 2);
    REQUIRE(show.session.set_output_on(k_stage, false));
    // No frame at all: nothing to paint, frozen or not.
    CHECK_FALSE(show.session.scene_frame(k_stage).has_value());
    const auto frames = show.session.scene_frames();
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].output_id == k_projector);
    REQUIRE(show.session.toggle_output_freeze(k_stage));
    CHECK(show.session.scene_frames().size() == 1);

    // On again, it shows what is live now.
    REQUIRE(show.session.toggle_output_freeze(k_stage));
    REQUIRE(show.session.set_output_on(k_stage, true));
    CHECK(lyrics(show.frame(k_stage)) == "Praise");
}

TEST_CASE("Removing an output forgets what was done to it", "[session][display]")
{
    Show show;
    REQUIRE(show.session.toggle_output_blank(k_stage));
    REQUIRE(show.session.toggle_output_freeze(k_stage));
    REQUIRE(show.session.set_output_on(k_stage, false));

    auto outputs = show.session.outputs();
    outputs.pop_back();
    show.session.set_outputs(outputs);
    CHECK_FALSE(show.session.output_is_blank(k_stage));
    CHECK_FALSE(show.session.output_is_frozen(k_stage));
    CHECK(show.session.outputs_off().empty());

    // An output added back under the same id starts clean.
    OutputConfig stage;
    stage.id = k_stage;
    stage.name = "Stage";
    outputs.push_back(stage);
    show.session.set_outputs(outputs);
    const auto frame = show.frame(k_stage);
    CHECK_FALSE(frame.is_blank);
    CHECK_FALSE(frame.is_frozen);
    CHECK(lyrics(frame) == "Praise");
}
