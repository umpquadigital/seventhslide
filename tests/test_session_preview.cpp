// A stack's Preview: what lands there, and the take (Session::transition).

#include "counting_player.hpp"
#include "theme_test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/model/transition.hpp>
#include <seventhslide/session/session.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::model::ContentKind;
using seventhslide::model::OutputConfig;
using seventhslide::model::ServiceItem;
using seventhslide::model::Song;
using seventhslide::model::Transition;
using seventhslide::model::TransitionType;
using seventhslide::session::Land;
using seventhslide::session::Session;
using seventhslide::session::StackRuntime;
using seventhslide::session::StackSide;
using seventhslide::test::Players;

namespace {

/// One output, Main with Preview on, and Lobby in front of it without.
struct PreviewSession {
    ApproxTextMeasurer measurer;
    Session session{measurer};
    std::string main_id{seventhslide::model::k_default_stack_id};
    std::string lobby_id;

    PreviewSession()
    {
        OutputConfig oc;
        oc.name = "Main";
        oc.id = "out-main";
        seventhslide::test::set_look(oc, {{"video_enabled", true}});
        session.set_outputs({oc});
        auto stacks = session.stack_defs();
        const auto lobby = seventhslide::model::add_stack(stacks, "Lobby", "info");
        REQUIRE(lobby.ok);
        lobby_id = lobby.id;
        auto defaults = session.type_defaults();
        defaults.for_type("image") = lobby_id;
        session.set_stacks(stacks, defaults);
        REQUIRE(session.set_preview_stacks({main_id}));
    }

    [[nodiscard]] StackRuntime& program() { return *session.runtime({main_id}); }
    [[nodiscard]] StackRuntime& preview()
    {
        return *session.runtime({main_id, StackSide::Preview});
    }
};

[[nodiscard]] Song song(const char* title, int lines)
{
    Song out;
    out.title = title;
    out.lyrics = "---[Verse:1]---\n";
    for (int i = 0; i < lines; ++i) {
        out.lyrics += std::string(title) + " line " + std::to_string(i + 1) + "\n[---][---]\n";
    }
    out.verse_order = "v1";
    return out;
}

[[nodiscard]] ServiceItem song_item(std::int64_t id, const char* title, json extra = json::object())
{
    ServiceItem item;
    item.item_id = id;
    item.item_type = "song";
    item.data = {{"title", title},
                 {"lyrics", std::string("---[Verse:1]---\n") + title + " one\n[---][---]\n" + title
                                + " two\n"},
                 {"verse_order", "v1"}};
    item.data.update(extra);
    return item;
}

[[nodiscard]] seventhslide::session::LiveMediaContent video(const char* file, bool autoplay = true)
{
    return {ContentKind::Video, {{"filename", file}, {"autoplay", autoplay}}};
}

}  // namespace

TEST_CASE("What is sent to a stack with Preview on is cued, and the take swaps the sides",
          "[session][preview]")
{
    PreviewSession t;
    auto& session = t.session;
    session.set_focused_stack_id(t.lobby_id);

    // Sent: cued, and the stack is focused. The outputs show nothing new.
    const auto empty = session.scene_frames();
    const auto landed = session.play_song(song("Be Thou My Vision", 3), t.main_id);
    CHECK(landed.stack_id == t.main_id);
    CHECK(landed.side == StackSide::Preview);
    CHECK(session.focused_stack_id() == t.main_id);
    CHECK(t.preview().song().title == "Be Thou My Vision");
    CHECK_FALSE(t.program().occupied());
    CHECK(session.occupied_stacks().empty());
    CHECK(session.scene_frames() == empty);

    // Straight live, Preview or not.
    CHECK(session.play_song(song("Opening", 2), t.main_id, Land::Live).side == StackSide::Program);
    CHECK(t.program().song().title == "Opening");
    CHECK(t.preview().song().title == "Be Thou My Vision");
    // A stack without a Preview takes what is sent at once.
    CHECK(session.play_song(song("Notices", 1), t.lobby_id).side == StackSide::Program);

    // Worked in Preview: its own cursor.
    REQUIRE(t.preview().jump_to_line(2));
    REQUIRE(t.program().next_slide());
    const auto live_before = session.scene_frames();
    // The take leaves the focus where it is.
    session.set_focused_stack_id(t.lobby_id);

    REQUIRE(session.transition(t.main_id));
    CHECK(t.program().song().title == "Be Thou My Vision");
    CHECK(t.program().player().line_cursor() == 2);
    CHECK(t.preview().song().title == "Opening");
    CHECK(t.preview().player().line_cursor() == 1);
    CHECK(session.scene_frames() != live_before);
    CHECK(session.focused_stack_id() == t.lobby_id);

    // And back, each where it was.
    REQUIRE(session.transition(t.main_id));
    CHECK(t.program().song().title == "Opening");
    CHECK(t.program().player().line_cursor() == 1);
    CHECK(t.preview().player().line_cursor() == 2);
    CHECK(session.scene_frames() == live_before);
}

TEST_CASE("A take is refused without something cued", "[session][preview]")
{
    PreviewSession t;
    auto& session = t.session;
    session.play_song(song("Opening", 2), t.main_id, Land::Live);
    // An empty Preview cannot take the stack off the air.
    CHECK_FALSE(session.transition(t.main_id));
    CHECK(t.program().occupied());
    // Nor a stack without one, nor one that is not there.
    session.play_song(song("Notices", 1), t.lobby_id);
    CHECK_FALSE(session.transition(t.lobby_id));
    CHECK_FALSE(session.transition("no such stack"));

    // Cleared, a Preview is empty again; what is live stays.
    session.play_song(song("Cued", 1), t.main_id);
    REQUIRE(session.toggle_stack_blank(t.main_id));
    session.set_focused_stack_id(t.lobby_id);
    REQUIRE(session.clear({t.main_id, StackSide::Preview}));
    CHECK_FALSE(t.preview().occupied());
    CHECK(t.program().occupied());
    CHECK(session.stack_is_blank(t.main_id));
    CHECK(session.focused_stack_id() == t.lobby_id);
    CHECK_FALSE(session.transition(t.main_id));
    CHECK_FALSE(session.clear({t.lobby_id, StackSide::Preview}));
}

TEST_CASE("A blank goes with its side at the take", "[session][preview]")
{
    PreviewSession t;
    auto& session = t.session;
    // The output shows words: it is not blanked.
    const auto on_air = [&session] {
        const auto frame = session.scene_frames().front();
        return !frame.is_blank && frame.text && !frame.text->empty();
    };
    session.play_song(song("Opening", 2), t.main_id, Land::Live);
    REQUIRE(session.toggle_stack_blank(t.main_id));
    session.play_song(song("Closing", 2), t.main_id);
    CHECK(session.scene_frames().front().is_blank);
    CHECK_FALSE(session.preview_is_blank(t.main_id));

    // What was cued goes live as it was, and what was live comes back blanked.
    REQUIRE(session.transition(t.main_id));
    CHECK(t.program().song().title == "Closing");
    CHECK_FALSE(session.stack_is_blank(t.main_id));
    CHECK_FALSE(session.scene_frames().front().is_blank);
    CHECK(on_air());
    CHECK(t.preview().song().title == "Opening");
    CHECK(session.preview_is_blank(t.main_id));

    // And back again.
    REQUIRE(session.transition(t.main_id));
    CHECK(t.program().song().title == "Opening");
    CHECK(session.stack_is_blank(t.main_id));
    CHECK(session.scene_frames().front().is_blank);
    CHECK_FALSE(session.preview_is_blank(t.main_id));

    // A Preview blanked of its own goes live blanked; the outputs show
    // nothing of it before.
    REQUIRE(session.toggle_stack_blank(t.main_id));
    REQUIRE(session.toggle_preview_blank(t.main_id));
    CHECK(on_air());
    REQUIRE(session.transition(t.main_id));
    CHECK(session.stack_is_blank(t.main_id));
    CHECK(session.scene_frames().front().is_blank);
    CHECK_FALSE(session.preview_is_blank(t.main_id));
}

TEST_CASE("A Preview's blank goes with what is cued", "[session][preview]")
{
    PreviewSession t;
    auto& session = t.session;
    // Nothing cued has nothing to blank, and a stack without a Preview has
    // no Preview to blank.
    CHECK_FALSE(session.toggle_preview_blank(t.main_id));
    session.play_song(song("Notices", 1), t.lobby_id);
    CHECK_FALSE(session.toggle_preview_blank(t.lobby_id));
    CHECK_FALSE(session.toggle_preview_blank("no such stack"));

    // Something new cued is not blanked, as what it replaced was.
    session.play_song(song("Opening", 2), t.main_id);
    REQUIRE(session.toggle_preview_blank(t.main_id));
    CHECK(session.preview_is_blank(t.main_id));
    CHECK_FALSE(session.stack_is_blank(t.main_id));
    session.play_song(song("Closing", 2), t.main_id);
    CHECK_FALSE(session.preview_is_blank(t.main_id));

    // A step through it keeps it; a Clear of the Preview lifts it.
    REQUIRE(session.toggle_preview_blank(t.main_id));
    REQUIRE(session.next_slide_on({t.main_id, StackSide::Preview}));
    CHECK(session.preview_is_blank(t.main_id));
    REQUIRE(session.clear({t.main_id, StackSide::Preview}));
    CHECK_FALSE(session.preview_is_blank(t.main_id));

    // Clearing what is live keeps what is cued blanked; the Preview taken
    // away takes its blank with it.
    session.play_song(song("Opening", 2), t.main_id, Land::Live);
    session.play_song(song("Closing", 2), t.main_id);
    REQUIRE(session.toggle_stack_blank(t.main_id));
    REQUIRE(session.toggle_preview_blank(t.main_id));
    REQUIRE(session.clear_stack(t.main_id));
    CHECK_FALSE(session.stack_is_blank(t.main_id));
    CHECK(session.preview_is_blank(t.main_id));
    REQUIRE(session.set_preview_stacks({}));
    CHECK_FALSE(session.preview_is_blank(t.main_id));
    REQUIRE(session.set_preview_stacks({t.main_id}));
    CHECK_FALSE(session.preview_is_blank(t.main_id));

    // Turning it off always works, and Clear blank and freeze lifts it.
    session.play_song(song("Closing", 2), t.main_id);
    REQUIRE(session.toggle_preview_blank(t.main_id));
    session.clear_blank_freeze();
    CHECK_FALSE(session.preview_is_blank(t.main_id));
}

TEST_CASE("A take comes in by the theme's transition or its own, until the next move",
          "[session][preview]")
{
    PreviewSession t;
    auto& session = t.session;
    session.play_song(song("Opening", 3), t.main_id, Land::Live);
    const auto theme_way = session.scene_frames().front().content_transition;

    session.play_song(song("Be Thou My Vision", 3), t.main_id);
    REQUIRE(session.transition(t.main_id));
    auto frames = session.scene_frames();
    CHECK(frames.front().content_transition == theme_way);

    const Transition own{.type = TransitionType::FadeThroughBlack, .duration_ms = 1234};
    REQUIRE(session.transition(t.main_id, own));
    frames = session.scene_frames();
    CHECK(frames.front().content_transition == own);
    CHECK(frames.front().background_transition == own);
    CHECK(session.transition_ms_on(t.main_id) == 1234);
    // A cut is a transition of its own too.
    REQUIRE(session.transition(t.main_id, Transition{.type = TransitionType::Cut}));
    CHECK(session.transition_ms_on(t.main_id) == 0);

    // The next move comes as moves do.
    REQUIRE(session.transition(t.main_id, own));
    REQUIRE(session.next_slide_on({t.main_id}));
    frames = session.scene_frames();
    CHECK(frames.front().content_transition == theme_way);
}

TEST_CASE("Nothing plays in Preview by being sent; a take plays it from where it stands",
          "[session][preview][video]")
{
    PreviewSession t;
    auto& session = t.session;
    Players players;
    session.set_players(players.source());

    // Cued: paused, on a player of its own, which the Preview drives.
    session.play_media(video("intro.mp4"), t.main_id);
    session.attach_players();
    CHECK(t.preview().has_transport());
    CHECK_FALSE(t.preview().playback().is_playing);
    REQUIRE(t.preview().media_player() != nullptr);
    auto* const cued_player = t.preview().media_player();
    REQUIRE(t.preview().media_seek(4.0));

    // Taken: it plays, from where it was left, on the same player.
    REQUIRE(session.transition(t.main_id));
    CHECK(t.program().playback().is_playing);
    CHECK(t.program().playback().position_sec == 4.0);
    REQUIRE(t.program().media_player() == cued_player);
    CHECK(t.preview().media_player() == nullptr);
    auto& intro = players.video_of(t.program());
    REQUIRE(intro.load("intro.mp4", false, t.program().playback().position_sec));
    intro.play();
    intro.position = 9.5;

    // Another cued over it: the video goes back where it was taken off,
    // meant to be paused, and keeps its player (the app holds it).
    session.play_media(video("walk-in.mp4"), t.main_id);
    REQUIRE(session.transition(t.main_id));
    CHECK(t.preview().playback().position_sec == 9.5);
    CHECK_FALSE(t.preview().playback().is_playing);
    CHECK(t.preview().media_player() == &intro);
    CHECK(t.program().playback().is_playing);
    CHECK(t.program().media_player() != &intro);

    // Taken back, it plays on from there.
    REQUIRE(session.transition(t.main_id));
    CHECK(t.program().playback().position_sec == 9.5);
    CHECK(t.program().playback().is_playing);
    CHECK(t.program().media_player() == &intro);
}

TEST_CASE("A take leaves an item that waits for Play waiting; one played in Preview plays on",
          "[session][preview][video]")
{
    PreviewSession t;
    auto& session = t.session;

    session.play_media(video("hold.mp4", /*autoplay=*/false), t.main_id);
    REQUIRE(session.transition(t.main_id));
    CHECK_FALSE(t.program().playback().is_playing);

    // Played while cued: it plays on into Program.
    session.play_media(video("intro.mp4", /*autoplay=*/false), t.main_id);
    REQUIRE(t.preview().media_play());
    REQUIRE(session.transition(t.main_id));
    CHECK(t.program().playback().is_playing);
    CHECK_FALSE(t.preview().playback().is_playing);
}

TEST_CASE("The service's selected row is what is live; its Also play set goes live at the take",
          "[session][preview][service]")
{
    PreviewSession t;
    auto& session = t.session;
    ServiceItem picture;
    picture.item_id = 3;
    picture.item_type = "image";
    picture.data = {{"filename", "loop.jpg"}};
    session.open_service(1, {},
                         {song_item(1, "Be Thou My Vision"),
                          song_item(2, "The Church's One Foundation", {{"also_play", {3}}}),
                          picture});

    // Cued: no row selected, and its companion waits.
    REQUIRE(session.select_service_item(1));
    CHECK(session.focused_stack_id() == t.main_id);
    CHECK(t.preview().service_item_id() == 2);
    CHECK(session.service_item_index() == -1);
    CHECK_FALSE(session.runtime({t.lobby_id})->occupied());

    // Something cued on the companion's stack is left alone: the companion
    // goes live past it.
    REQUIRE(session.set_preview_stacks({t.main_id, t.lobby_id}));
    session.play_song(song("Cued on Lobby", 1), t.lobby_id);
    session.set_focused_stack_id(t.main_id);

    REQUIRE(session.transition(t.main_id));
    CHECK(session.service_item_index() == 1);
    CHECK(session.current_service_item()->item_id == 2);
    CHECK(session.runtime({t.lobby_id})->content_kind() == ContentKind::Image);
    CHECK(session.runtime({t.lobby_id, StackSide::Preview})->song().title == "Cued on Lobby");

    // Library content taken: the row the stack played is no longer live.
    session.play_song(song("Opening", 1), t.main_id);
    REQUIRE(session.transition(t.main_id));
    CHECK(session.service_item_index() == -1);
    // The row taken back is selected again.
    REQUIRE(session.transition(t.main_id));
    CHECK(session.service_item_index() == 1);
}

TEST_CASE("Auto-advance runs on into the service live, past the Preview", "[session][preview][auto]")
{
    PreviewSession t;
    auto& session = t.session;
    session.open_service(1, {}, {song_item(1, "First"), song_item(2, "Second")});
    REQUIRE(session.select_service_item(0, true, Land::Live));
    session.play_song(song("Cued", 1), t.main_id);

    seventhslide::session::AutoAdvanceState state;
    state.enabled = true;
    state.through = true;
    session.set_auto_advance(t.main_id, state);
    bool changed_item = false;
    for (int i = 0; i < 8 && !changed_item; ++i) {
        changed_item = session.tick_auto_advance(t.main_id).changed_item;
    }
    REQUIRE(changed_item);
    CHECK(t.program().service_item_id() == 2);
    CHECK(session.service_item_index() == 1);
    CHECK(t.preview().song().title == "Cued");
}

TEST_CASE("A cued place survives a rebuild and an edit of its row", "[session][preview][service]")
{
    PreviewSession t;
    auto& session = t.session;
    session.open_service(1, {}, {song_item(1, "First")});
    REQUIRE(session.select_service_item(0));
    REQUIRE(t.preview().jump_to_line(1));

    session.rebuild();
    CHECK(t.preview().player().line_cursor() == 1);

    auto edited = song_item(1, "First");
    edited.data["lyrics"] = "---[Verse:1]---\nFirst one\n[---][---]\nFirst two, edited\n";
    session.replace_service_items({edited});
    REQUIRE(session.refresh_service_item(1));
    CHECK(t.preview().player().line_cursor() == 1);
    CHECK(t.preview().song().lyrics.find("edited") != std::string::npos);
}

TEST_CASE("Blank and freeze stay with the stack across a take", "[session][preview]")
{
    PreviewSession t;
    auto& session = t.session;
    session.play_song(song("Opening", 1), t.main_id, Land::Live);
    REQUIRE(session.toggle_stack_freeze(t.main_id));
    const auto held = session.scene_frames();
    session.play_song(song("Cued", 1), t.main_id);
    REQUIRE(session.transition(t.main_id));
    CHECK(session.stack_is_frozen(t.main_id));
    // Frozen, the outputs hold what they had.
    CHECK(session.scene_frames() == held);
    REQUIRE(session.toggle_stack_freeze(t.main_id));
    CHECK(session.scene_frames() != held);
}

TEST_CASE("A clip played to its end in Preview starts over at the take", "[session][preview][video]")
{
    PreviewSession t;
    auto& session = t.session;
    Players players;
    session.set_players(players.source());
    session.play_media(video("intro.mp4"), t.main_id);
    session.attach_players();
    auto& intro = players.video_of(t.preview());
    REQUIRE(intro.load("intro.mp4", false, 0.0));
    REQUIRE(t.preview().media_play());
    intro.position = 30.0;
    intro.end();

    // As Play would have it: over again, not held at its end.
    REQUIRE(session.transition(t.main_id));
    CHECK(intro.restarts == 1);
    CHECK(t.program().is_playing());
    CHECK(t.program().playback().position_sec == 0.0);
}

TEST_CASE("A cued row made another kind of item loads anew, still cued",
          "[session][preview][service]")
{
    PreviewSession t;
    auto& session = t.session;
    session.open_service(1, {}, {song_item(1, "Cued"), song_item(2, "Live")});
    REQUIRE(session.select_service_item(0));
    REQUIRE(session.select_service_item(1, true, Land::Live));
    std::vector<ServiceItem> clips;
    for (const std::int64_t id : {1, 2}) {
        ServiceItem clip;
        clip.item_id = id;
        clip.item_type = "video";
        clip.data = {{"filename", "intro.mp4"}, {"autoplay", true}};
        clips.push_back(clip);
    }
    session.replace_service_items(clips);

    // Cued, nothing starts by landing in a Preview; live, it plays as sent.
    REQUIRE(session.refresh_service_item(1));
    REQUIRE(session.refresh_service_item(2));
    CHECK(t.preview().content_kind() == ContentKind::Video);
    CHECK_FALSE(t.preview().playback().is_playing);
    CHECK(t.program().content_kind() == ContentKind::Video);
    CHECK(t.program().playback().is_playing);
}

TEST_CASE("Clearing either side of a stack leaves it focused while the other holds something",
          "[session][preview]")
{
    PreviewSession t;
    auto& session = t.session;
    session.play_song(song("Notices", 1), t.lobby_id);
    session.play_song(song("Opening", 1), t.main_id, Land::Live);
    session.play_song(song("Cued", 1), t.main_id);
    REQUIRE(session.focused_stack_id() == t.main_id);

    // What is live cleared, something still cued: the focus stays.
    REQUIRE(session.clear_stack(t.main_id));
    CHECK(session.focused_stack_id() == t.main_id);
    // Nothing on either side: the focus goes to what is in front.
    REQUIRE(session.clear({t.main_id, StackSide::Preview}));
    CHECK(session.focused_stack_id() == t.lobby_id);

    // What is cued cleared, something still live: the focus stays.
    session.play_song(song("Opening", 1), t.main_id, Land::Live);
    session.play_song(song("Cued", 1), t.main_id);
    REQUIRE(session.clear({t.main_id, StackSide::Preview}));
    CHECK(session.focused_stack_id() == t.main_id);
    REQUIRE(session.clear_stack(t.main_id));
    CHECK(session.focused_stack_id() == t.lobby_id);
}

TEST_CASE("A take leaves a member of its Also play set that is already showing as it is",
          "[session][preview][service]")
{
    PreviewSession t;
    auto& session = t.session;
    ServiceItem picture;
    picture.item_id = 3;
    picture.item_type = "image";
    picture.data = {{"filename", "loop.jpg"}};
    session.open_service(1, {},
                         {song_item(1, "Be Thou My Vision"),
                          song_item(2, "The Church's One Foundation", {{"also_play", {3}}}),
                          picture});
    REQUIRE(session.select_service_item(1));
    REQUIRE(session.transition(t.main_id));
    const auto& lobby = *session.runtime({t.lobby_id});
    REQUIRE(lobby.service_item_id() == 3);
    const auto shown = lobby.build_id();

    // Something else taken and the row taken back: the picture never left.
    session.play_song(song("Opening", 1), t.main_id);
    REQUIRE(session.transition(t.main_id));
    REQUIRE(session.transition(t.main_id));
    CHECK(session.service_item_index() == 1);
    CHECK(lobby.build_id() == shown);

    // Selected again, the row starts over, and its set with it.
    REQUIRE(session.select_service_item(1, true, Land::Live));
    CHECK(lobby.build_id() != shown);
}
