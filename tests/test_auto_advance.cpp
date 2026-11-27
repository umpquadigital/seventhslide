// Per-stack auto-advance. It lives in Session, which is what lets the web remote
// both see and drive it.

#include <catch2/catch_test_macros.hpp>

#include "theme_test_helpers.hpp"

#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/session/session.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::model::OutputConfig;
using seventhslide::model::ServiceItem;
using seventhslide::session::AutoAdvanceState;
using seventhslide::model::Song;
using seventhslide::session::Session;

namespace {

OutputConfig main_output()
{
    OutputConfig oc;
    oc.name = "Main";
    seventhslide::test::set_look(oc, {{"width_px", 800}, {"height_px", 600}});
    return oc;
}

ServiceItem song_item(std::int64_t id, const std::string& title)
{
    ServiceItem item;
    item.item_id = id;
    item.item_type = "song";
    item.data = json{{"title", title},
                     {"lyrics", "---[Verse:1]---\nA\n[---][---]\nB\n"},
                     {"verse_order", "v1"}};
    return item;
}

ServiceItem divider_item(std::int64_t id, const std::string& title)
{
    ServiceItem item;
    item.item_id = id;
    item.item_type = "divider";
    item.data = json{{"title", title}};
    return item;
}

Session make_session(ApproxTextMeasurer& measurer)
{
    Session session(measurer);
    session.set_outputs({main_output()});
    return session;
}

std::string focused(const Session& session) { return session.focused_stack_id(); }

} // namespace

TEST_CASE("AutoAdvance: an unconfigured stack reports the defaults", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);

    const auto state = session.auto_advance("nope");
    REQUIRE_FALSE(state.enabled);
    REQUIRE(state.interval_ms == 5000);
    REQUIRE_FALSE(state.loop);
    REQUIRE_FALSE(state.through);
    REQUIRE_FALSE(state.stop_at_dividers);
    REQUIRE(session.advancing_stack_ids().empty());
}

TEST_CASE("AutoAdvance: settings round-trip and list only while enabled", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);
    const std::string id = focused(session);

    AutoAdvanceState state;
    state.enabled = true;
    state.interval_ms = 3000;
    state.loop = true;
    state.through = true;
    state.stop_at_dividers = true;
    session.set_auto_advance(id, state);

    REQUIRE(session.auto_advance(id) == state);
    REQUIRE(session.advancing_stack_ids() == std::vector<std::string>{id});

    state.enabled = false;
    session.set_auto_advance(id, state);
    // Settings are remembered; only the timer stops.
    REQUIRE(session.auto_advance(id).interval_ms == 3000);
    REQUIRE(session.advancing_stack_ids().empty());
}

TEST_CASE("AutoAdvance: a disabled stack never ticks", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);

    Song song;
    song.lyrics = "---[Verse:1]---\nA\n[---][---]\nB\n";
    song.verse_order = "v1";
    session.play_song(song);

    const auto tick = session.tick_auto_advance(focused(session));
    REQUIRE_FALSE(tick.did_advance);
    REQUIRE_FALSE(tick.exhausted);
    REQUIRE_FALSE(tick.changed_item);
}

TEST_CASE("AutoAdvance: steps through lines then stops at the end", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);
    const std::string id = focused(session);

    Song song;
    song.lyrics = "---[Verse:1]---\nA\n[---][---]\nB\n[---][---]\nC\n";
    song.verse_order = "v1";
    session.play_song(song);
    const int total = session.focused_stack().program.player().total_lines();
    REQUIRE(total >= 3);

    AutoAdvanceState state;
    state.enabled = true;
    session.set_auto_advance(id, state);

    int advanced = 0;
    for (int i = 0; i < total + 4; ++i) {
        const auto tick = session.tick_auto_advance(id);
        if (tick.did_advance) {
            ++advanced;
            REQUIRE_FALSE(tick.changed_item);
        }
        if (tick.exhausted) {
            break;
        }
    }
    REQUIRE(advanced > 0);
    // Running out switches the stack off rather than spinning forever.
    REQUIRE(session.advancing_stack_ids().empty());
    REQUIRE_FALSE(session.auto_advance(id).enabled);
}

TEST_CASE("AutoAdvance: loop rewinds within the item instead of stopping", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);
    const std::string id = focused(session);

    Song song;
    song.lyrics = "---[Verse:1]---\nA\n[---][---]\nB\n";
    song.verse_order = "v1";
    session.play_song(song);

    AutoAdvanceState state;
    state.enabled = true;
    state.loop = true;
    session.set_auto_advance(id, state);

    bool wrapped = false;
    for (int i = 0; i < 12; ++i) {
        const auto tick = session.tick_auto_advance(id);
        REQUIRE_FALSE(tick.exhausted);
        if (session.focused_stack().program.player().line_cursor() == 0 && i > 0) {
            wrapped = true;
        }
    }
    REQUIRE(wrapped);
    // A looping stack stays armed.
    REQUIRE(session.advancing_stack_ids() == std::vector<std::string>{id});
}

TEST_CASE("AutoAdvance: run-through walks on into the rundown", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);
    const std::string id = focused(session);

    session.open_service(1, {}, {song_item(10, "First"), song_item(11, "Second")});
    REQUIRE(session.select_service_item(0));
    REQUIRE(session.service_item_index() == 0);

    AutoAdvanceState state;
    state.enabled = true;
    state.through = true;
    session.set_auto_advance(id, state);

    bool changed_item = false;
    for (int i = 0; i < 24; ++i) {
        const auto tick = session.tick_auto_advance(id);
        if (tick.changed_item) {
            changed_item = true;
            break;
        }
        if (tick.exhausted) {
            break;
        }
    }
    REQUIRE(changed_item);
    REQUIRE(session.service_item_index() == 1);
}

TEST_CASE("AutoAdvance: stop_at_dividers halts at a section break", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);
    const std::string id = focused(session);

    session.open_service(
        1, {},
        {song_item(10, "First"), divider_item(11, "Sermon"), song_item(12, "Second")});
    REQUIRE(session.select_service_item(0));

    AutoAdvanceState state;
    state.enabled = true;
    state.through = true;
    state.stop_at_dividers = true;
    session.set_auto_advance(id, state);

    for (int i = 0; i < 24; ++i) {
        const auto tick = session.tick_auto_advance(id);
        if (tick.exhausted) {
            break;
        }
    }
    // The divider is a wall: the second song must not have gone live on its own.
    REQUIRE(session.service_item_index() == 0);
    REQUIRE(session.advancing_stack_ids().empty());
}

TEST_CASE("AutoAdvance: pruning forgets stacks that no longer exist", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);
    const std::string id = focused(session);

    AutoAdvanceState state;
    state.enabled = true;
    session.set_auto_advance(id, state);
    session.set_auto_advance("ghost", state);
    REQUIRE(session.advancing_stack_ids().size() == 2);

    REQUIRE(session.prune_auto_advance());
    const auto ids = session.advancing_stack_ids();
    REQUIRE(ids.size() == 1);
    REQUIRE(ids[0] == id);
    // Idempotent: a second prune finds nothing left to drop.
    REQUIRE_FALSE(session.prune_auto_advance());
}

TEST_CASE("AutoAdvance: through skips items that belong to other stacks", "[session][R2.5]")
{
    ApproxTextMeasurer measurer;
    Session session = make_session(measurer);

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(added.ok);
    auto defaults = session.type_defaults();
    defaults.for_type("image") = added.id;
    session.set_stacks(stacks, defaults);

    ServiceItem first = song_item(10, "First");
    ServiceItem image;
    image.item_id = 11;
    image.item_type = "image";
    image.data = json{{"filename", "loop.jpg"}};
    ServiceItem second = song_item(12, "Second");

    session.open_service(1, {}, {first, image, second});
    REQUIRE(session.select_service_item(0));

    const std::string main = focused(session);
    AutoAdvanceState state;
    state.enabled = true;
    state.through = true;
    session.set_auto_advance(main, state);

    bool changed_item = false;
    for (int i = 0; i < 24; ++i) {
        const auto tick = session.tick_auto_advance(main);
        if (tick.changed_item) {
            changed_item = true;
            break;
        }
        if (tick.exhausted) {
            break;
        }
    }
    REQUIRE(changed_item);
    REQUIRE(session.service_item_index() == 2);

    session.arm_send_to(added.id);
    REQUIRE(session.select_service_item(0));
    session.set_auto_advance(main, state);
    changed_item = false;
    for (int i = 0; i < 24; ++i) {
        const auto tick = session.tick_auto_advance(main);
        if (tick.changed_item) {
            changed_item = true;
            break;
        }
        if (tick.exhausted) {
            break;
        }
    }
    REQUIRE(changed_item);
    REQUIRE(session.service_item_index() == 2);
}

TEST_CASE("A slide's time counts from the end of its transition", "[session][auto-advance][transition]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    auto oc = main_output();
    seventhslide::test::set_look(
        oc, {{"transitions",
              seventhslide::test::every_transition(
                  {seventhslide::model::TransitionType::Fade, 800})}});
    session.set_outputs({oc});
    Song song;
    song.title = "Steady";
    song.lyrics = "---[Verse:1]---\nA\n[---][---]\nB\n";
    song.verse_order = "v1";
    session.play_song(song);
    const auto stack = session.focused_stack_id();
    CHECK(session.transition_ms_on(stack) == 800);

    // A theme that cuts: nothing to wait for.
    seventhslide::test::set_look(oc, {{"transitions", seventhslide::test::every_transition({})}});
    session.set_outputs({oc});
    session.play_song(song);
    CHECK(session.transition_ms_on(stack) == 0);
    CHECK(session.transition_ms_on("no-such-stack") == 0);
}
