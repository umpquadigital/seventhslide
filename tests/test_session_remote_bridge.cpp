// SessionRemoteBridge - the seam where src/remote's contract meets the session.
//
// test_remote_protocol and test_remote_server both drive a *fake* bridge, so
// everything they prove stops at the interface. This file drives the real one
// over a live SessionFacade, and asserts the two things a fake can never check:
// that each section actually carries the keys the client reads, and that the
// deliberate omissions (per-row cursor flags, output configuration, the video
// preview revision) really are omitted.

#include "scene_test_helpers.hpp"
#include "test_qt_app.hpp"

#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "fake_live_receiver.hpp"
#include "fake_web_renderer.hpp"
#include "freeform_thumb_cache.hpp"
#include "glyphs.hpp"
#include "live_library_controller.hpp"
#include "media_thumb_cache.hpp"
#include "refusals.hpp"
#include "sample_library.hpp"
#include "line_map_model.hpp"
#include "session_remote_bridge.hpp"
#include "test_temp_dir.hpp"
#include "web_library_controller.hpp"

#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/live_source.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/remote/remote_bridge.hpp>
#include <seventhslide/remote/remote_protocol.hpp>
#include <seventhslide/session/session.hpp>

#include <nlohmann/json.hpp>

#include <QImage>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <algorithm>
#include <future>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using seventhslide::app::SessionFacade;
using seventhslide::app::SessionRemoteBridge;
using seventhslide::model::OutputConfig;
using seventhslide::remote::Command;
using seventhslide::remote::Section;
using seventhslide::model::Song;

namespace {

OutputConfig make_output(std::string name)
{
    OutputConfig oc;
    oc.id = "out-" + name;
    oc.name = std::move(name);
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    return oc;
}

Song demo_song()
{
    Song song;
    song.title = "Test Song";
    song.lyrics =
        "[V1]\nPraise to the Lord\nThe Almighty\n\n[V2]\nO my soul praise Him\nFor He is "
        "thy health";
    song.verse_order = "v1 v2";
    return song;
}

/// One remote device as the server serves it: the bridge, and the view the
/// server keeps for the device, settled before each use as the server does.
class Remote {
public:
    explicit Remote(SessionFacade& facade) : bridge_(facade) {}

    [[nodiscard]] nlohmann::json section(Section s)
    {
        bridge_.settle(view_);
        return bridge_.section(s, view_);
    }
    [[nodiscard]] seventhslide::remote::CommandResult invoke(Command command,
                                                           const nlohmann::json& args)
    {
        bridge_.settle(view_);
        return bridge_.invoke(command, args, view_);
    }
    [[nodiscard]] seventhslide::remote::PendingThumb thumb(
        const seventhslide::remote::ThumbRequest& r) const
    {
        return bridge_.thumb(r);
    }
    [[nodiscard]] nlohmann::json library_page(const seventhslide::remote::LibraryQuery& q) const
    {
        return bridge_.library_page(q);
    }
    [[nodiscard]] nlohmann::json service_list(std::string_view tag, std::string_view query) const
    {
        return bridge_.service_list(tag, query);
    }

private:
    SessionRemoteBridge bridge_;
    seventhslide::remote::View view_;
};

/// Runs before any member below it, so SessionFacade is never constructed
/// without the QGuiApplication its text measurer needs.
/// A facade with one output and a live song - the state most sections describe.
struct LiveFixture {
    seventhslide::test::QtBootstrap qt_;
    SessionFacade facade;
    Remote bridge{facade};

    LiveFixture()
    {
        facade.set_outputs({make_output("Main")});
        facade.live()->play_song(demo_song());
    }
};

bool has_key(const nlohmann::json& obj, const char* key)
{
    return obj.is_object() && obj.contains(key);
}

} // namespace

TEST_CASE("Bridge: every section is an object the client can patch", "[app][remote][bridge]")
{
    // The protocol layer merges each section into a keyed state tree, so a
    // section that came back null or as a bare array would corrupt the patch
    // rather than fail loudly.
    LiveFixture fx;

    for (const Section s : seventhslide::remote::all_sections()) {
        INFO("section " << seventhslide::remote::section_name(s));
        const nlohmann::json body = fx.bridge.section(s);
        REQUIRE(body.is_object());
    }
}

TEST_CASE("Bridge: the session section mirrors the live cursor", "[app][remote][bridge]")
{
    LiveFixture fx;

    const nlohmann::json body = fx.bridge.section(Section::Session);

    REQUIRE(body["mode"] == "song");
    REQUIRE(body["hasBuild"] == true);
    REQUIRE(body["currentTitle"] == "Test Song");
    REQUIRE(body["primaryOutputId"] == "out-Main");
    REQUIRE(body["primaryOutputName"] == "Main");
    REQUIRE(body["lineCursor"] == 0);
    REQUIRE(body["totalLines"].get<int>() > 1);
    REQUIRE(body["globalBlank"] == false);
    REQUIRE(body["globalFreeze"] == false);
    // Blank and Freeze have one target each: there is no moving one to show.
    REQUIRE_FALSE(has_key(body, "blankOn"));
    REQUIRE_FALSE(has_key(body, "freezeOn"));

    // Modes are mutually exclusive here; a stale true would make the remote
    // show the wrong transport controls.
    REQUIRE(body["imageMode"] == false);
    REQUIRE(body["videoMode"] == false);

    // One slide index per output on.
    REQUIRE(body["outputSlides"].is_array());
    REQUIRE(body["outputSlides"].size() == 1);

    REQUIRE(body["advancingStackIds"].is_array());
}

TEST_CASE("Bridge: the session section follows next/prev", "[app][remote][bridge]")
{
    LiveFixture fx;

    REQUIRE(fx.bridge.section(Section::Session)["lineCursor"] == 0);

    const auto result = fx.bridge.invoke(Command::NextSlide, nlohmann::json::object());
    REQUIRE(result.ok);

    const nlohmann::json after = fx.bridge.section(Section::Session);
    REQUIRE(after["lineCursor"].get<int>() > 0);
}

TEST_CASE("Bridge: the lines section carries where next and prev go from every line",
          "[app][remote][bridge]")
{
    // The client predicts a quick second tap from these, so they must be the
    // session's own answers.
    LiveFixture fx;
    const nlohmann::json body = fx.bridge.section(Section::Lines);
    const auto& next = body["nextChange"];
    const auto& prev = body["prevChange"];
    const auto total = fx.bridge.section(Section::Session)["totalLines"].get<std::size_t>();
    REQUIRE(next.size() == total);
    REQUIRE(prev.size() == total);
    REQUIRE(prev[0] == -1);  // nothing before the first line
    REQUIRE(next[0].get<int>() > 1);  // the first slide holds more than one line

    // From every line, next and prev land where the table says: a slide holds
    // several lines here, so this is more than "the line before / after".
    for (int line = 0; line < static_cast<int>(total); ++line) {
        INFO("line " << line);
        const auto at = static_cast<std::size_t>(line);
        nlohmann::json jump{{"lineIndex", line}};
        REQUIRE(fx.bridge.invoke(Command::JumpToLine, jump).ok);
        REQUIRE(fx.bridge.invoke(Command::NextSlide, nlohmann::json::object()).ok);
        const int after_next = fx.bridge.section(Section::Session)["lineCursor"];
        REQUIRE(after_next == (next[at] == -1 ? line : next[at].get<int>()));

        REQUIRE(fx.bridge.invoke(Command::JumpToLine, jump).ok);
        REQUIRE(fx.bridge.invoke(Command::PrevSlide, nlohmann::json::object()).ok);
        const int after_prev = fx.bridge.section(Section::Session)["lineCursor"];
        REQUIRE(after_prev == (prev[at] == -1 ? line : prev[at].get<int>()));
    }
}

TEST_CASE("Bridge: previewRevision rides only on the video plate", "[app][remote][bridge]")
{
    // Documented in the section builder: putting the revision on every frame
    // would change this section's hash on clock ticks and make the remote
    // refetch thumbnails that did not change.
    LiveFixture fx;

    const nlohmann::json song_body = fx.bridge.section(Section::Session);
    REQUIRE(song_body["videoMode"] == false);
    REQUIRE_FALSE(song_body.contains("previewRevision"));
}

TEST_CASE("Bridge: audio sends the transport without the video plate", "[app][remote][bridge][audio]")
{
    LiveFixture fx;
    REQUIRE(fx.facade.live()->playAudio(QStringLiteral("hymn.mp3")));

    const nlohmann::json body = fx.bridge.section(Section::Session);
    REQUIRE(body["mode"] == "audio");
    REQUIRE(body["mediaTransport"] == true);
    REQUIRE(body["videoMode"] == false);
    REQUIRE_FALSE(body.contains("previewRevision"));
}

TEST_CASE("Bridge: an image row names its file, not its title", "[app][remote][bridge]")
{
    // An imported image is stored under a name of its own and shown under
    // its title: the remote asks for its thumbnail by the file.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    Remote bridge{facade};
    seventhslide::model::ServiceItem image;
    image.item_id = 1;
    image.item_type = "image";
    image.data = {{"filename", "3f2a9c.png"}, {"title", "Sunrise"}};
    seventhslide::model::ServiceItem song;
    song.item_id = 2;
    song.item_type = "song";
    song.data = {{"title", "Hymn"}, {"lyrics", "[V1]\nOne"}, {"verse_order", "v1"}};
    facade.library()->open_rundown(1, nlohmann::json::object(), {image, song});

    const nlohmann::json items = bridge.section(Section::Service)["items"];
    REQUIRE(items.size() == 2);
    CHECK(items[0]["title"] == "Sunrise");
    CHECK(items[0]["imageFile"] == "3f2a9c.png");
    CHECK(items[1]["imageFile"] == "");
}

TEST_CASE("Bridge: a rundown row sends exactly its wire fields", "[app][remote][bridge]")
{
    // The fields are the remote's list, not the model's roles: a role added
    // for QML must not reach the phone, and none may silently go missing.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    Remote bridge{facade};
    Song song = demo_song();
    const auto song_id = facade.song_repo()->insert(song);
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
    REQUIRE(facade.library()->addSongToService(song_id));

    const nlohmann::json items = bridge.section(Section::Service)["items"];
    REQUIRE(items.size() == 1);
    std::set<std::string> keys;
    for (const auto& [key, value] : items[0].items()) {
        keys.insert(key);
    }
    const std::set<std::string> wire{"title",       "itemType",    "itemIndex",     "itemId",
                                     "selected",    "hasOverrides", "displayNumber", "folderImages",
                                     "imageFile",   "stackId",     "stackIcon",     "alsoPlayIds",
                                     "alsoPlayWith", "cued"};
    REQUIRE(keys == wire);
    REQUIRE(items[0]["title"] == "Test Song");
    REQUIRE(items[0]["itemType"] == "song");
}

TEST_CASE("Bridge: the lines section leaves out per-row cursor flags", "[app][remote][bridge]")
{
    // LineMapModel really does expose isCurrent / isNext / isVisible, for QML.
    // The wire leaves them out on purpose: the client derives them from the
    // cursor, and sending them would re-send every lyric line on each next/prev.
    LiveFixture fx;

    const nlohmann::json body = fx.bridge.section(Section::Lines);
    REQUIRE(body["rows"].is_array());
    REQUIRE_FALSE(body["rows"].empty());

    for (const nlohmann::json& row : body["rows"]) {
        REQUIRE(row.is_object());
        REQUIRE_FALSE(row.contains("isCurrent"));
        REQUIRE_FALSE(row.contains("isNext"));
        REQUIRE_FALSE(row.contains("isVisible"));
        // What the client does need is still there.
        REQUIRE(row.contains("text"));
        REQUIRE(row.contains("lineIndex"));
    }

    // The row payload must not change when the cursor moves - that is the whole
    // point of stripping the flags.
    const nlohmann::json rows_before = body["rows"];
    REQUIRE(fx.bridge.invoke(Command::NextSlide, nlohmann::json::object()).ok);
    REQUIRE(fx.bridge.section(Section::Lines)["rows"] == rows_before);

    // One slide map per output on, for the client's slide grouping.
    REQUIRE(body["slideMaps"].is_array());
    REQUIRE(body["slideMaps"].size() == 1);
}

TEST_CASE("Bridge: the outputs section publishes status and nothing else", "[app][remote][bridge]")
{
    // Screen assignment, geometry, themes and preview URLs are operator
    // concerns. The remote is explicitly not a configuration surface, so this
    // section is a hand-built projection rather than the raw model rows.
    LiveFixture fx;

    const nlohmann::json body = fx.bridge.section(Section::Outputs);
    REQUIRE(body["list"].is_array());
    REQUIRE(body["list"].size() == 1);

    const nlohmann::json& row = body["list"][0];
    REQUIRE(row["id"] == "out-Main");
    REQUIRE(row["name"] == "Main");
    REQUIRE(row["isBlank"] == false);
    REQUIRE(row["isFrozen"] == false);

    // Exactly these keys: anything else means configuration leaked to the LAN.
    REQUIRE(row.size() == 4);
}

TEST_CASE("Bridge: an output switched off is left out of every section", "[app][remote][bridge]")
{
    LiveFixture fx;
    fx.facade.set_outputs({make_output("Main"), make_output("Stage")});
    fx.facade.live()->play_song(demo_song());
    REQUIRE(fx.bridge.section(Section::Outputs)["list"].size() == 2);
    REQUIRE(fx.bridge.section(Section::Session)["outputSlides"].size() == 2);

    REQUIRE(fx.facade.outputSettings()->setOutputOn(QStringLiteral("Stage"), false).isEmpty());
    const nlohmann::json list = fx.bridge.section(Section::Outputs)["list"];
    REQUIRE(list.size() == 1);
    CHECK(list[0]["name"] == "Main");
    CHECK(fx.bridge.section(Section::Session)["outputSlides"].size() == 1);
    CHECK(fx.bridge.section(Section::Lines)["slideMaps"].size() == 1);
    // And there is no command to leave one out from the remote.
    REQUIRE(fx.facade.outputSettings()->setOutputOn(QStringLiteral("Stage"), true).isEmpty());
    CHECK(fx.bridge.section(Section::Outputs)["list"].size() == 2);
}

TEST_CASE("Bridge: output status follows blank and freeze commands", "[app][remote][bridge]")
{
    LiveFixture fx;

    // Outputs are named by id on the wire.
    nlohmann::json args;
    args["outputId"] = "out-Main";
    REQUIRE(fx.bridge.invoke(Command::ToggleOutputBlank, args).ok);
    REQUIRE(fx.bridge.section(Section::Outputs)["list"][0]["isBlank"] == true);

    REQUIRE(fx.bridge.invoke(Command::ToggleOutputFreeze, args).ok);
    REQUIRE(fx.bridge.section(Section::Outputs)["list"][0]["isFrozen"] == true);

    // An output the session does not have is refused, with a message.
    nlohmann::json missing;
    missing["outputId"] = "out-Nope";
    const auto refused = fx.bridge.invoke(Command::ToggleOutputBlank, missing);
    REQUIRE_FALSE(refused.ok);
    REQUIRE_FALSE(refused.error.empty());
}

TEST_CASE("Bridge: global blank round-trips through the session section", "[app][remote][bridge]")
{
    LiveFixture fx;

    REQUIRE(fx.bridge.invoke(Command::ToggleGlobalBlank, nlohmann::json::object()).ok);
    REQUIRE(fx.bridge.section(Section::Session)["globalBlank"] == true);

    REQUIRE(fx.bridge.invoke(Command::ToggleGlobalBlank, nlohmann::json::object()).ok);
    REQUIRE(fx.bridge.section(Section::Session)["globalBlank"] == false);
}

TEST_CASE("Bridge: stack blank and freeze name their stack; clearBlankFreeze lifts all", "[app][remote][bridge]")
{
    LiveFixture fx;
    const auto live = fx.facade.stacks()->focusedStackId().toStdString();
    const auto stack_state = [&](const char* key) {
        return fx.bridge.section(Section::Stacks)["occupied"][0][key] == true;
    };

    REQUIRE(fx.bridge.invoke(Command::ToggleStackBlank, {{"stackId", live}}).ok);
    REQUIRE(fx.bridge.invoke(Command::ToggleStackFreeze, {{"stackId", live}}).ok);
    CHECK(stack_state("blank"));
    CHECK(stack_state("frozen"));
    CHECK(fx.bridge.section(Section::Session)["globalBlank"] == false);

    REQUIRE(fx.bridge.invoke(Command::ToggleGlobalFreeze, nlohmann::json::object()).ok);
    CHECK(fx.bridge.section(Section::Session)["globalFreeze"] == true);

    REQUIRE(fx.bridge.invoke(Command::ClearBlankFreeze, nlohmann::json::object()).ok);
    CHECK_FALSE(stack_state("blank"));
    CHECK_FALSE(stack_state("frozen"));
    CHECK(fx.bridge.section(Section::Session)["globalFreeze"] == false);

    // A stack with nothing live on it is refused, with a message.
    REQUIRE(fx.facade.stacks()->clearStack(QString::fromStdString(live)).isEmpty());
    const auto refused = fx.bridge.invoke(Command::ToggleStackFreeze, {{"stackId", live}});
    CHECK_FALSE(refused.ok);
    CHECK_FALSE(refused.error.empty());
    CHECK_FALSE(fx.facade.session().stack_is_frozen(live));
}

TEST_CASE("Bridge: the stacks section resolves icons to inline URLs", "[app][remote][bridge]")
{
    // The client renders stack tabs without a second round trip, so every def
    // that names an icon must arrive with a usable iconUrl beside it.
    LiveFixture fx;

    const nlohmann::json body = fx.bridge.section(Section::Stacks);
    REQUIRE(body["defs"].is_array());
    REQUIRE_FALSE(body["defs"].empty());
    REQUIRE(has_key(body, "focusedStackId"));
    REQUIRE(has_key(body, "armedSendToId"));
    REQUIRE(has_key(body, "showStackTabs"));

    for (const nlohmann::json& def : body["defs"]) {
        REQUIRE(def.contains("id"));
        if (def.contains("icon") && def["icon"].is_string()) {
            REQUIRE(def.contains("iconUrl"));
            REQUIRE(def["iconUrl"].is_string());
            REQUIRE_FALSE(def["iconUrl"].get<std::string>().empty());
        }
    }

    // A song is live, so exactly one stack is occupied and it carries an icon
    // URL too (the occupied list is built separately from defs).
    REQUIRE(body["occupied"].is_array());
    for (const nlohmann::json& def : body["occupied"]) {
        if (def.contains("icon") && def["icon"].is_string()) {
            REQUIRE(def.contains("iconUrl"));
        }
    }
}

TEST_CASE("Bridge: stack icons ship one URL per strip state", "[app][remote][bridge]")
{
    // The web strip tints its glyph by focus the way the desktop one does, and
    // a data: URI cannot be recoloured once it is in an <img>. So each state
    // has to arrive as its own URL, and the two must actually differ - a bug
    // that collapsed them would leave the focused tab indistinguishable.
    LiveFixture fx;

    const nlohmann::json body = fx.bridge.section(Section::Stacks);
    int checked = 0;
    for (const nlohmann::json* list : {&body["defs"], &body["occupied"]}) {
        for (const nlohmann::json& def : *list) {
            if (!def.contains("icon") || !def["icon"].is_string()) {
                continue;
            }
            REQUIRE(def.contains("iconUrlIdle"));
            REQUIRE(def.contains("iconUrlActive"));
            const auto idle = def["iconUrlIdle"].get<std::string>();
            const auto active = def["iconUrlActive"].get<std::string>();
            REQUIRE_FALSE(idle.empty());
            REQUIRE_FALSE(active.empty());
            REQUIRE(idle != active);
            ++checked;
        }
    }
    REQUIRE(checked > 0);
}

TEST_CASE("Bridge: the remote and the console keep their own stack tabs", "[app][remote][bridge][stacks]")
{
    seventhslide::test::QtBootstrap qt;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto stacks = facade.stacks()->stacks();
    const auto main = stacks.front().toMap().value(QStringLiteral("id")).toString();
    const auto lobby = stacks.back().toMap().value(QStringLiteral("id")).toString();

    const auto pinned_song = [](std::int64_t id, const char* title, const QString& stack) {
        return seventhslide::test::song_item_on_stack(id, title, stack.toStdString());
    };
    facade.library()->open_rundown(1, nlohmann::json::object(),
                                   {pinned_song(1, "Hymn", main), pinned_song(2, "Notice", lobby)});
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.live()->selectServiceItem(1));
    REQUIRE(facade.stacks()->focusedStackId() == lobby);

    Remote bridge{facade};
    const auto remote_tab = [&] {
        return QString::fromStdString(bridge.section(Section::Stacks)["focusedStackId"]);
    };
    // The remote starts on the console's tab.
    REQUIRE(remote_tab() == lobby);

    // Picking a tab on the remote leaves the console where it is.
    REQUIRE(bridge.invoke(Command::SetFocusedStackId, {{"stackId", main.toStdString()}}).ok);
    CHECK(remote_tab() == main);
    CHECK(facade.stacks()->focusedStackId() == lobby);
    const auto session = bridge.section(Section::Session);
    CHECK(session["currentTitle"] == "Hymn");
    CHECK(session["serviceItemIndex"] == 0);
    CHECK(facade.live()->current_title() == QStringLiteral("Notice"));

    // Picking a tab on the console leaves the remote where it is, and the
    // title follows the console's tab.
    facade.stacks()->setFocusedStackId(main);
    CHECK(facade.live()->current_title() == QStringLiteral("Hymn"));
    facade.stacks()->setFocusedStackId(lobby);
    CHECK(remote_tab() == main);

    // Navigation from the remote moves the remote's stack, not the console's.
    REQUIRE(bridge.invoke(Command::NextSlide, nlohmann::json::object()).ok);
    CHECK(facade.session().runtime({main.toStdString()})->player().line_cursor() > 0);
    CHECK(facade.session().runtime({lobby.toStdString()})->player().line_cursor() == 0);
    CHECK(facade.stacks()->focusedStackId() == lobby);
    CHECK(facade.live()->current_title() == QStringLiteral("Notice"));

    // Global blank from the remote reaches the outputs whichever tab either
    // side shows, and leaves both tabs where they are.
    REQUIRE(bridge.invoke(Command::ToggleGlobalBlank, nlohmann::json::object()).ok);
    CHECK(facade.live()->global_blank());
    CHECK(bridge.section(Section::Session)["globalBlank"] == true);
    CHECK(remote_tab() == main);
    CHECK(facade.stacks()->focusedStackId() == lobby);
    REQUIRE(bridge.invoke(Command::ToggleGlobalBlank, nlohmann::json::object()).ok);
    CHECK_FALSE(facade.live()->global_blank());

    // Sending something live from the remote moves only the remote's tab.
    facade.stacks()->setFocusedStackId(main);
    REQUIRE(bridge.invoke(Command::SelectServiceItem, {{"index", 1}}).ok);
    CHECK(remote_tab() == lobby);
    CHECK(facade.stacks()->focusedStackId() == main);
}

TEST_CASE("Bridge: the images section is inert when no playlist is live", "[app][remote][bridge]")
{
    // The client keys off "active"; the empty array and index -1 keep it from
    // needing a null guard.
    LiveFixture fx;

    const nlohmann::json body = fx.bridge.section(Section::Images);
    REQUIRE(body["active"] == false);
    REQUIRE(body["images"].is_array());
    REQUIRE(body["images"].empty());
    REQUIRE(body["index"] == -1);
}

TEST_CASE("Bridge: the advance section reports every stack", "[app][remote][bridge]")
{
    LiveFixture fx;

    const nlohmann::json body = fx.bridge.section(Section::Advance);
    REQUIRE(body["byStack"].is_object());
    REQUIRE(body["byStack"].size() == fx.facade.session().stack_defs().size());

    for (const auto& def : fx.facade.session().stack_defs()) {
        INFO("stack " << def.id);
        REQUIRE(body["byStack"].contains(def.id));
        const nlohmann::json& state = body["byStack"][def.id];
        REQUIRE(state["enabled"] == false);
        REQUIRE(state.contains("intervalSec"));
        REQUIRE(state.contains("loop"));
        REQUIRE(state.contains("through"));
        REQUIRE(state.contains("stopAtDividers"));
    }
}

TEST_CASE("Bridge: setAutoAdvance round-trips and reports seconds", "[app][remote][bridge]")
{
    // The wire speaks seconds while the session stores milliseconds, so the
    // conversion is worth pinning in both directions.
    LiveFixture fx;
    const std::string stack = fx.facade.session().stack_defs().front().id;

    nlohmann::json args;
    args["stackId"] = stack;
    args["enabled"] = true;
    args["intervalSec"] = 12;
    args["loop"] = true;
    REQUIRE(fx.bridge.invoke(Command::SetAutoAdvance, args).ok);

    const nlohmann::json state = fx.bridge.section(Section::Advance)["byStack"][stack];
    REQUIRE(state["enabled"] == true);
    REQUIRE(state["intervalSec"] == 12);
    REQUIRE(state["loop"] == true);
}

TEST_CASE("Bridge: setAutoAdvance keeps what it is not sent", "[app][remote][bridge]")
{
    LiveFixture fx;
    const std::string stack = fx.facade.session().stack_defs().front().id;

    nlohmann::json args;
    args["stackId"] = stack;
    args["intervalSec"] = 12;
    args["loop"] = true;
    REQUIRE(fx.bridge.invoke(Command::SetAutoAdvance, args).ok);
    nlohmann::json enable;
    enable["stackId"] = stack;
    enable["enabled"] = true;
    REQUIRE(fx.bridge.invoke(Command::SetAutoAdvance, enable).ok);

    const nlohmann::json state = fx.bridge.section(Section::Advance)["byStack"][stack];
    CHECK(state["enabled"] == true);
    CHECK(state["intervalSec"] == 12);
    CHECK(state["loop"] == true);
}

TEST_CASE("Bridge: a refused command answers with a message", "[app][remote][bridge]")
{
    // The protocol layer validates argument *types*, but only the session knows
    // whether a line exists. A refusal must carry text the remote can show.
    LiveFixture fx;

    nlohmann::json args;
    args["lineIndex"] = 99999;
    const auto refused = fx.bridge.invoke(Command::JumpToLine, args);
    REQUIRE_FALSE(refused.ok);
    REQUIRE_FALSE(refused.error.empty());

    args["lineIndex"] = 1;
    const auto accepted = fx.bridge.invoke(Command::JumpToLine, args);
    REQUIRE(accepted.ok);
    REQUIRE(fx.bridge.section(Section::Session)["lineCursor"] == 1);
}

TEST_CASE("Bridge: end of the rundown is a silent no-op", "[app][remote][bridge]")
{
    // Explicitly not an error: the operator console stays quiet at the end of a
    // song, and the remote must not start toasting where the console does not.
    LiveFixture fx;

    for (int i = 0; i < 200; ++i) {
        REQUIRE(fx.bridge.invoke(Command::NextSlide, nlohmann::json::object()).ok);
    }
    REQUIRE(fx.bridge.invoke(Command::NextSlide, nlohmann::json::object()).ok);
    REQUIRE(fx.bridge.invoke(Command::PrevSlide, nlohmann::json::object()).ok);
}

// --- library paging ------------------------------------------------------

TEST_CASE("Bridge: library_page lists and filters songs", "[app][remote][bridge]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    Remote bridge{facade};

    Song a;
    a.title = "Amazing Grace";
    a.lyrics = "[V1]\nOne\nTwo";
    a.verse_order = "v1";
    a.authors = {"Newton", "Excell"};
    REQUIRE(facade.song_repo()->insert(a) > 0);

    Song b;
    b.title = "Be Thou My Vision";
    b.lyrics = "[V1]\nThree\nFour";
    b.verse_order = "v1";
    REQUIRE(facade.song_repo()->insert(b) > 0);
    facade.refresh_library();

    seventhslide::remote::LibraryQuery q;
    q.kind = "songs";
    const nlohmann::json all = bridge.library_page(q);
    REQUIRE(all.is_object());
    REQUIRE(all["rows"].is_array());
    REQUIRE(all["rows"].size() == 2);

    // Authors are pre-joined for the client rather than sent as an array.
    const auto amazing =
        std::find_if(all["rows"].begin(), all["rows"].end(), [](const nlohmann::json& r) {
            return r["title"] == "Amazing Grace";
        });
    REQUIRE(amazing != all["rows"].end());
    REQUIRE((*amazing)["authors"] == "Newton, Excell");
    REQUIRE((*amazing)["id"].is_number_integer());

    // Search is case-insensitive substring, so a phone keyboard's lowercase
    // works without the operator thinking about it.
    q.query = "amazing";
    REQUIRE(bridge.library_page(q)["rows"].size() == 1);
    q.query = "AMAZING";
    REQUIRE(bridge.library_page(q)["rows"].size() == 1);
    q.query = "grace";
    REQUIRE(bridge.library_page(q)["rows"].size() == 1);
    q.query = "no such song";
    REQUIRE(bridge.library_page(q)["rows"].empty());

    // As the console searches: more than one word, the authors, and the
    // lyrics, which name the line that matched.
    q.query = "thou my";
    REQUIRE(bridge.library_page(q)["rows"].size() == 1);
    q.query = "excell";
    REQUIRE(bridge.library_page(q)["rows"].size() == 1);
    q.query = "three";
    const nlohmann::json by_lyrics = bridge.library_page(q)["rows"];
    REQUIRE(by_lyrics.size() == 1);
    CHECK(by_lyrics[0]["title"] == "Be Thou My Vision");
    CHECK(by_lyrics[0]["matchedLine"] == "Three");
    q.query = "amazing";
    CHECK_FALSE(bridge.library_page(q)["rows"][0].contains("matchedLine"));
}

TEST_CASE("Bridge: library_page lists only the Bibles switched on", "[app][remote][bridge]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    Remote bridge{facade};
    const auto kjv = facade.bible_repo()->import_bible("King James", "", {});
    const auto web = facade.bible_repo()->import_bible("World English", "", {});
    REQUIRE(facade.library()->setBibleEnabled(web, false).isEmpty());

    seventhslide::remote::LibraryQuery q;
    q.kind = "bibles";
    const nlohmann::json page = bridge.library_page(q);
    REQUIRE(page["rows"].size() == 1);
    CHECK(page["rows"][0]["id"] == kjv);
}

TEST_CASE("Bridge: library_page answers an unknown kind with an empty page", "[app][remote][bridge]")
{
    // A client built against a newer protocol must get a well-formed empty page
    // rather than a malformed body.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    Remote bridge{facade};

    seventhslide::remote::LibraryQuery q;
    q.kind = "not-a-kind";
    const nlohmann::json page = bridge.library_page(q);
    REQUIRE(page.is_object());
    REQUIRE(page["rows"].is_array());
    REQUIRE(page["rows"].empty());
    // Its refusal is a sentence, as the bridge's others are.
    CHECK(page["error"]
          == SessionRemoteBridge::tr("That is not one of the library’s lists.").toStdString());
}

// --- service list --------------------------------------------------------

namespace {

std::vector<std::string> service_names(const nlohmann::json& page)
{
    std::vector<std::string> out;
    for (const auto& s : page["services"]) {
        out.push_back(s["name"].get<std::string>());
    }
    return out;
}

}  // namespace

TEST_CASE("Bridge: service_list lists tags and services, and narrows to a tag",
          "[app][remote][bridge]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    Remote bridge{facade};

    auto* library = facade.library();
    const qint64 easter = library->createService(QStringLiteral("Easter"));
    const qint64 vespers = library->createService(QStringLiteral("Vespers"));
    REQUIRE(library->setServiceDateTime(easter, QStringLiteral("2026-04-05T11:00:00")).isEmpty());
    REQUIRE(library->setServiceDateTime(vespers, QStringLiteral("2026-03-07T17:30:00")).isEmpty());
    REQUIRE(library->setServiceTags(easter, {QStringLiteral("Spring"), QStringLiteral("Holidays")})
                .isEmpty());
    REQUIRE(library->setServiceTags(vespers, {QStringLiteral("Youth")}).isEmpty());
    REQUIRE(facade.openServiceId() == vespers);

    // Every tag in use, by name; every service, newest first.
    const nlohmann::json all = bridge.service_list("", "");
    REQUIRE(all["tag"] == "");
    REQUIRE(all["tags"].size() == 3);
    CHECK(all["tags"][0]["name"] == "Holidays");
    CHECK(all["tags"][2] == nlohmann::json{{"name", "Youth"}, {"count", 1}});
    CHECK(service_names(all) == std::vector<std::string>{"Easter", "Vespers"});
    CHECK(all["services"][1]["isOpen"] == true);
    CHECK(all["services"][0]["tags"] == nlohmann::json::array({"Spring", "Holidays"}));

    // Inside a tag (any case): its services, and no tag rows.
    const nlohmann::json spring = bridge.service_list("spring", "");
    CHECK(spring["tags"].empty());
    CHECK(service_names(spring) == std::vector<std::string>{"Easter"});
}

TEST_CASE("Bridge: service_list search finds services by name or tag, anywhere",
          "[app][remote][bridge]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    Remote bridge{facade};

    auto* library = facade.library();
    const qint64 easter = library->createService(QStringLiteral("Easter"));
    REQUIRE(library->createService(QStringLiteral("Christmas")) > 0);
    REQUIRE(library->setServiceTags(easter, {QStringLiteral("Spring")}).isEmpty());

    const nlohmann::json by_name = bridge.service_list("", "easter");
    CHECK(service_names(by_name) == std::vector<std::string>{"Easter"});
    CHECK(by_name["tags"].empty());

    // A tag's name finds the tag and the services that carry it, even from
    // inside another tag.
    const nlohmann::json by_tag = bridge.service_list("Other", "Spr");
    REQUIRE(by_tag["tags"].empty());  // tag rows only at the top
    CHECK(service_names(by_tag) == std::vector<std::string>{"Easter"});
    const nlohmann::json top = bridge.service_list("", "Spr");
    REQUIRE(top["tags"].size() == 1);
    CHECK(top["tags"][0]["name"] == "Spring");

    const nlohmann::json miss = bridge.service_list("", "zzzz");
    CHECK(miss["tags"].empty());
    CHECK(miss["services"].empty());
}

// --- thumbnails ----------------------------------------------------------

TEST_CASE("Bridge: an unknown thumb kind is not found", "[app][remote][bridge]")
{
    // serve_thumb turns !ok into a 404, so this is the path that keeps a bad
    // request from becoming a broken image.
    LiveFixture fx;

    seventhslide::remote::ThumbRequest r;
    r.kind = "no-such-kind";
    REQUIRE_FALSE(fx.bridge.thumb(r).encode);
}

TEST_CASE("Bridge: two remotes keep their own stack tabs", "[app][remote][bridge][stacks]")
{
    seventhslide::test::QtBootstrap qt;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main")});
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    const auto main = facade.session().focused_stack_id();
    facade.live()->play_song(demo_song());  // on main
    facade.stacks()->armSendTo(lobby);
    Song notice = demo_song();
    notice.title = "Notice";
    facade.live()->play_song(notice);  // on the lobby, which the console now shows

    Remote phone{facade};
    Remote tablet{facade};
    const auto tab_of = [](Remote& r) {
        return r.section(Section::Stacks)["focusedStackId"].get<std::string>();
    };
    REQUIRE(tab_of(phone) == lobby.toStdString());
    REQUIRE(tab_of(tablet) == lobby.toStdString());

    // The phone picks main; the tablet stays where it was.
    REQUIRE(phone.invoke(Command::SetFocusedStackId, {{"stackId", main}}).ok);
    CHECK(tab_of(phone) == main);
    CHECK(tab_of(tablet) == lobby.toStdString());
    CHECK(phone.section(Section::Session)["currentTitle"] == "Test Song");
    CHECK(tablet.section(Section::Session)["currentTitle"] == "Notice");

    // Each one's Next moves its own tab's stack.
    const auto cursor_on = [&](const std::string& stack) {
        return facade.session().runtime({stack})->player().line_cursor();
    };
    REQUIRE(cursor_on(main) == 0);
    REQUIRE(cursor_on(lobby.toStdString()) == 0);
    REQUIRE(phone.invoke(Command::NextSlide, nlohmann::json::object()).ok);
    const int main_after = cursor_on(main);
    CHECK(main_after > 0);
    CHECK(cursor_on(lobby.toStdString()) == 0);
    REQUIRE(tablet.invoke(Command::NextSlide, nlohmann::json::object()).ok);
    CHECK(cursor_on(lobby.toStdString()) > 0);
    CHECK(cursor_on(main) == main_after);
    CHECK(facade.session().focused_stack_id() == lobby.toStdString());
}

TEST_CASE("Bridge: a preview thumb renders PNG at the output's aspect", "[app][remote][bridge]")
{
    // PNG rather than JPEG for rendered slides, so lyric text stays crisp on a
    // phone; the height follows the output canvas rather than an assumed 16:9.
    LiveFixture fx;

    seventhslide::remote::ThumbRequest r;
    r.kind = "preview";
    r.name = "out-Main";
    r.width = 320;
    r.slide_index = -1;

    const auto pending = fx.bridge.thumb(r);
    REQUIRE(pending.encode);
    const auto result = pending.encode();
    REQUIRE(result.ok);
    REQUIRE(result.mime == "image/png");
    REQUIRE_FALSE(result.bytes.empty());
    // PNG magic, so a silently-JPEG-encoded slide would fail here.
    REQUIRE(result.bytes.compare(1, 3, "PNG") == 0);
    // The output (named by id) was found: the picture is its 16:9 canvas.
    const QImage image = QImage::fromData(
        reinterpret_cast<const uchar*>(result.bytes.data()), static_cast<int>(result.bytes.size()));
    CHECK(image.size() == QSize(320, 180));
}

// The remote's Freeform slide grid asks for "slide N" of a deck. Its URL
// names the deck (stack) and its build, so the picture changes exactly when
// the slides do; before, it named neither and the server kept serving the
// first deck's slides for every later one.
TEST_CASE("Bridge: the lines section names the deck its rows are and its build",
          "[app][remote][bridge]")
{
    LiveFixture fx;
    const auto first = fx.bridge.section(Section::Lines);
    REQUIRE(first.contains("stackId"));
    REQUIRE(first.contains("build"));
    CHECK(first["stackId"] == fx.facade.session().focused_stack_id());

    Song other = demo_song();
    other.title = "Another";
    other.lyrics = "[V1]\nSomething else\n\n[V2]\nAltogether";
    fx.facade.live()->play_song(other);
    const auto second = fx.bridge.section(Section::Lines);
    CHECK(second["build"] != first["build"]);
}

TEST_CASE("Bridge: a slide thumb is of the stack it names", "[app][remote][bridge][stacks]")
{
    seventhslide::test::QtBootstrap qt;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main")});
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    const auto lobby = facade.stacks()->stacks().back().toMap().value(QStringLiteral("id")).toString();
    const auto main = facade.session().focused_stack_id();

    facade.live()->play_song(demo_song());  // on main
    facade.stacks()->armSendTo(lobby);
    Song notice = demo_song();
    notice.title = "Notice";
    notice.lyrics = "[V1]\nCoffee after the service\n\n[V2]\nIn the hall";
    facade.live()->play_song(notice);  // on the lobby
    REQUIRE(facade.session().focused_stack_id() == lobby.toStdString());
    Remote bridge{facade};

    const auto slide_of = [&](const std::string& stack) {
        seventhslide::remote::ThumbRequest r;
        r.kind = "preview";
        r.name = "out-Main";
        r.width = 320;
        r.slide_index = 0;
        r.stack = stack;
        const auto pending = bridge.thumb(r);
        REQUIRE(pending.encode);
        const auto result = pending.encode();
        REQUIRE(result.ok);
        return result.bytes;
    };
    const auto console = slide_of({});
    CHECK(slide_of(lobby.toStdString()) == console);
    CHECK(slide_of(main) != console);
    // A stack that does not exist is the console's, as one not named is.
    CHECK(slide_of("no-such-stack") == console);
}

TEST_CASE("Bridge: a preview thumb for an unknown output still answers", "[app][remote][bridge]")
{
    // An output id the session does not have falls back to a 16:9 guess
    // rather than dividing by a zero canvas width.
    LiveFixture fx;

    seventhslide::remote::ThumbRequest r;
    r.kind = "preview";
    r.name = "Nonexistent";
    r.width = 160;

    const auto pending = fx.bridge.thumb(r);
    const auto result = pending.encode ? pending.encode() : seventhslide::remote::ThumbResult{};
    // Either a rendered fallback or a clean miss - never a crash or a
    // half-written buffer.
    REQUIRE((result.ok || result.bytes.empty()));
}

TEST_CASE("Bridge: a thumb encodes the same off the GUI thread", "[app][remote][bridge]")
{
    // The server runs the encode on a worker, which paints the thumbnail too.
    // It must read only what was prepared on the GUI thread, never the session.
    LiveFixture fx;
    seventhslide::remote::ThumbRequest r;
    r.kind = "preview";
    r.name = "out-Main";
    r.width = 320;
    const auto pending = fx.bridge.thumb(r);
    REQUIRE(pending.encode);
    const auto here = pending.encode();
    const auto there = std::async(std::launch::async, pending.encode).get();
    REQUIRE(there.ok);
    CHECK(there.mime == here.mime);
    CHECK(there.bytes == here.bytes);
}

TEST_CASE("Bridge: a media thumb is decoded on the worker", "[app][remote][bridge]")
{
    // A phone scrolling the image library asks for a tile per image. The
    // decode, and the thumbnail write on a miss, belong to the encode, not
    // to the GUI thread that took the request.
    LiveFixture fx;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto root = std::filesystem::path(dir.path().toStdString());
    std::filesystem::create_directories(root / "images");
    QImage red(640, 480, QImage::Format_RGB32);
    red.fill(Qt::red);
    REQUIRE(red.save(QString::fromStdString((root / "images" / "red.png").string())));
    fx.facade.set_media_root(root);

    seventhslide::remote::ThumbRequest r;
    r.kind = "media";
    r.sub = "images";
    r.name = "red.png";
    r.width = 160;
    const auto pending = fx.bridge.thumb(r);
    REQUIRE(pending.encode);
    const auto thumbs = seventhslide::app::media_thumbs_dir(root);
    CHECK_FALSE(std::filesystem::exists(thumbs));  // nothing read yet
    const auto result = std::async(std::launch::async, pending.encode).get();
    REQUIRE(result.ok);
    CHECK(result.mime == "image/jpeg");
    CHECK(std::filesystem::exists(thumbs));
}

TEST_CASE("Bridge: a stale Freeform tile is painted and stored on the worker",
          "[app][remote][bridge]")
{
    seventhslide::test::QtBootstrap qt;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto root = std::filesystem::path(dir.path().toStdString());
    SessionFacade facade;
    REQUIRE(facade.open_database(root / "library.db"));
    Remote bridge{facade};

    auto doc = seventhslide::freeform::make_document(
        "Tile", {facade.session().primary_output()->id});
    seventhslide::freeform::Object box;
    box.type = seventhslide::freeform::ObjectType::Text;
    box.w = 800;
    box.h = 200;
    box.text = "Hello";
    doc.decks[0].slides[0].objects.push_back(box);
    seventhslide::model::FreeformItem item;
    item.name = "Tile";
    item.document = doc.to_json();
    const auto id = facade.freeform_repo()->create_item(item);  // no tile on disk yet

    seventhslide::remote::ThumbRequest r;
    r.kind = "freeform";
    r.id = id;
    r.width = 320;
    const auto pending = bridge.thumb(r);
    REQUIRE(pending.encode);
    const auto tile = seventhslide::app::freeform_thumb_path(facade.media_root(), id,
                                                              seventhslide::app::k_media_thumb_tier_lg);
    CHECK_FALSE(std::filesystem::exists(tile));  // not painted yet
    const auto result = std::async(std::launch::async, pending.encode).get();
    REQUIRE(result.ok);
    CHECK(result.mime == "image/png");
    CHECK(std::filesystem::exists(tile));
}

TEST_CASE("Bridge: every command answers arguments that name nothing", "[app][remote][bridge]")
{
    // The compiler holds the bridge to a handler for every Command. This runs
    // each one with ids, names and indexes that match nothing, as a remote
    // can send, and needs each to come back with an answer.
    namespace protocol = seventhslide::remote::protocol;
    LiveFixture fx;
    for (const auto& spec : protocol::command_table()) {
        nlohmann::json args = nlohmann::json::object();
        for (const auto& arg : spec.args) {
            const std::string key(arg.key);
            switch (arg.type) {
            case protocol::ArgType::Int:
                args[key] = 987654;
                break;
            case protocol::ArgType::Number:
                args[key] = 0.0;
                break;
            case protocol::ArgType::Bool:
                args[key] = false;
                break;
            case protocol::ArgType::String:
                args[key] = "no-such-thing";
                break;
            case protocol::ArgType::IntArray:
                args[key] = nlohmann::json::array();
                break;
            }
        }
        INFO("command " << spec.name);
        try {
            const auto result = fx.bridge.invoke(spec.command, args);
            REQUIRE((result.ok || !result.error.empty()));
        } catch (const std::exception&) {
            // A handler that refuses these arguments by throwing still exists.
        }
    }
}

TEST_CASE("Bridge: the glyph sprite holds every glyph, painted by the page",
          "[app][remote][bridge]")
{
    LiveFixture fx;
    SessionRemoteBridge bridge(fx.facade);
    const std::string sprite = bridge.glyph_sprite();
    REQUIRE(sprite.starts_with("<svg"));
    for (const auto& glyph : seventhslide::app::glyphs::all()) {
        INFO(glyph.name);
        const std::string symbol = "<symbol id=\"ic-" + std::string(glyph.name) + "\"";
        CHECK(sprite.find(symbol) != std::string::npos);
    }
    // In the page's colour and stroke width, never a fixed one.
    CHECK(sprite.find("{ink}") == std::string::npos);
    CHECK(sprite.find("stroke-width:2.5") == std::string::npos);
    CHECK(sprite.find("currentColor") != std::string::npos);
}

// --- commands for other clients -------------------------------------------
//
// The bundled client plays; it opens no service and edits none. These
// commands stay in the protocol for other clients (a Stream Deck, a script),
// so each is held to what it does here (tests/remote_web/lint.mjs fails for
// a command with neither a client caller nor a test).

namespace {

/// The sample library open on disk, with an output, and a remote on it.
struct SampleFixture {
    seventhslide::test::QtBootstrap qt_;
    seventhslide::test::TempDir dir;
    SessionFacade facade;
    seventhslide::test::SampleLibrary lib;
    Remote bridge{facade};

    SampleFixture()
    {
        REQUIRE(facade.open_database(dir.path / "library.db"));
        lib = seventhslide::test::fill_sample_library(facade);
        facade.set_outputs({make_output("Main")});
    }

    [[nodiscard]] nlohmann::json items() { return bridge.section(Section::Service)["items"]; }

    [[nodiscard]] std::vector<std::string> titles()
    {
        std::vector<std::string> out;
        for (const auto& item : items()) {
            out.push_back(item["title"].get<std::string>());
        }
        return out;
    }

    [[nodiscard]] std::int64_t item_id(std::string_view title)
    {
        for (const auto& item : items()) {
            if (item["title"] == title) {
                return item["itemId"].get<std::int64_t>();
            }
        }
        FAIL("no rundown item " << title);
        return -1;
    }

    /// A new stack called ``name``; its id.
    [[nodiscard]] std::string add_stack(const char* name)
    {
        REQUIRE(facade.stacks()->addStack(QString::fromUtf8(name)).isEmpty());
        const QVariantMap made = facade.stacks()->stacks().back().toMap();
        return made.value(QStringLiteral("id")).toString().toStdString();
    }

    [[nodiscard]] bool run(Command command, const nlohmann::json& args = nlohmann::json::object())
    {
        const auto result = bridge.invoke(command, args);
        INFO(result.error);
        return result.ok;
    }
};

}  // namespace

TEST_CASE("Bridge: openService opens a service, and refuses one there is not",
          "[app][remote][bridge][other-clients]")
{
    SampleFixture fx;
    REQUIRE(fx.run(Command::OpenService, {{"serviceId", fx.lib.other_service}}));
    CHECK(fx.bridge.section(Section::Session)["openServiceName"] == "Good Friday");
    CHECK(fx.items().empty());
    CHECK_FALSE(fx.run(Command::OpenService, {{"serviceId", 987654}}));
    CHECK(fx.facade.openServiceId() == fx.lib.other_service);
}

TEST_CASE("Bridge: closeService leaves no service open, and refuses when none is",
          "[app][remote][bridge][other-clients]")
{
    SampleFixture fx;
    const auto services_before = fx.facade.library()->services()->rowCount();
    REQUIRE(fx.facade.openServiceId() >= 0);
    REQUIRE_FALSE(fx.items().empty());
    // Its first item live.
    REQUIRE(fx.run(Command::SelectServiceItem, {{"index", 0}}));
    REQUIRE(fx.bridge.section(Section::Session)["serviceItemIndex"] == 0);
    REQUIRE_FALSE(fx.bridge.section(Section::Stacks)["occupied"].empty());

    REQUIRE(fx.run(Command::CloseService, {}));
    const auto session = fx.bridge.section(Section::Session);
    CHECK(session["openServiceId"] == -1);
    CHECK(session["openServiceName"] == "");
    CHECK(session["serviceItemIndex"] == -1);
    CHECK(fx.items().empty());
    // What was live is cleared, as opening another service clears it.
    CHECK(fx.bridge.section(Section::Stacks)["occupied"].empty());
    // Closed, not deleted: the service is still in the library, to open again.
    CHECK(fx.facade.library()->services()->rowCount() == services_before);

    const auto refused = fx.bridge.invoke(Command::CloseService, nlohmann::json::object());
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == seventhslide::app::refusal::no_service_open().toStdString());
    REQUIRE(fx.run(Command::OpenService, {{"serviceId", fx.lib.other_service}}));
    CHECK(fx.facade.openServiceId() == fx.lib.other_service);
}

TEST_CASE("Bridge: the add commands put library content in the open service",
          "[app][remote][bridge][other-clients]")
{
    SampleFixture fx;
    REQUIRE(fx.run(Command::OpenService, {{"serviceId", fx.lib.other_service}}));

    REQUIRE(fx.run(Command::AddSongToService, {{"songId", fx.lib.first_song}}));
    REQUIRE(fx.run(Command::AddFreeformToService, {{"freeformId", fx.lib.freeform}}));
    REQUIRE(fx.run(Command::AddBibleRefToService, {{"bibleId", fx.lib.bible},
                                                   {"book", "John"},
                                                   {"chapter", 3},
                                                   {"verseStart", 16},
                                                   {"verseEnd", 18}}));
    REQUIRE(fx.run(Command::AddImageToService, {{"filename", "city.png"}}));
    REQUIRE(fx.run(Command::AddVideoToService, {{"filename", "welcome loop.mp4"}}));
    REQUIRE(fx.run(Command::AddImageFolderToService, {{"folderId", fx.lib.image_folder}}));
    // atIndex puts it there rather than at the end.
    REQUIRE(fx.run(Command::AddDividerToService, {{"title", "Opening"}, {"atIndex", 0}}));

    const auto items = fx.items();
    REQUIRE(items.size() == 7);
    CHECK(items[0]["itemType"] == "divider");
    CHECK(items[0]["title"] == "Opening");
    CHECK(items[1]["itemType"] == "song");
    CHECK(items[1]["title"] == "Amazing Grace");
    CHECK(items[2]["itemType"] == "freeform");
    CHECK(items[3]["itemType"] == "bible");
    CHECK(items[3]["title"] == "John 3:16-18");
    CHECK(items[4]["itemType"] == "image");
    CHECK(items[4]["imageFile"] == "city.png");
    CHECK(items[5]["itemType"] == "video");
    CHECK(items[6]["itemType"] == "image_folder");
    CHECK(items[6]["folderImages"].size() == 2);

    // A song that is not there adds nothing.
    CHECK_FALSE(fx.run(Command::AddSongToService, {{"songId", 987654}}));
    CHECK(fx.items().size() == 7);
}

TEST_CASE("Bridge: a video or Freeform folder goes into the service whole",
          "[app][remote][bridge][other-clients]")
{
    SampleFixture fx;
    REQUIRE(fx.run(Command::OpenService, {{"serviceId", fx.lib.other_service}}));

    auto* repo = fx.facade.media_repo();
    const auto clips = repo->folders(seventhslide::db::MediaKind::Video).create("Clips");
    (void)repo->add_folder_item(seventhslide::db::MediaKind::Video, clips, "welcome loop.mp4");
    REQUIRE(fx.run(Command::AddVideoFolderToService, {{"folderId", clips}}));

    REQUIRE(fx.facade.freeformLibrary()
                ->moveItems(QVariantList{fx.lib.freeform}, fx.lib.freeform_folder)
                .isEmpty());
    REQUIRE(fx.run(Command::AddFreeformFolderToService, {{"folderId", fx.lib.freeform_folder}}));

    const auto items = fx.items();
    REQUIRE(items.size() == 2);
    CHECK(items[0]["itemType"] == "video");
    CHECK(items[1]["itemType"] == "freeform");
    CHECK(items[1]["title"] == "Welcome");
}

TEST_CASE("Bridge: the rundown's items move, reorder and go",
          "[app][remote][bridge][other-clients]")
{
    SampleFixture fx;
    const auto start = fx.titles();
    REQUIRE(start.front() == "Amazing Grace");
    REQUIRE(start.back() == "Welcome");

    REQUIRE(fx.run(Command::MoveServiceItemTo, {{"fromIndex", 0}, {"toIndex", 1}}));
    CHECK(fx.titles()[0] == "Holy Holy Holy");
    CHECK(fx.titles()[1] == "Amazing Grace");

    std::vector<std::int64_t> reversed;
    for (const auto& item : fx.items()) {
        reversed.insert(reversed.begin(), item["itemId"].get<std::int64_t>());
    }
    REQUIRE(fx.run(Command::ReorderServiceItems, {{"orderedItemIds", reversed}}));
    CHECK(fx.titles().front() == "Welcome");
    CHECK(fx.titles().back() == "Holy Holy Holy");

    const auto removed = fx.item_id("Welcome");
    REQUIRE(fx.run(Command::RemoveServiceItem, {{"itemId", removed}}));
    const auto left = fx.titles();
    CHECK(left.size() == start.size() - 1);
    CHECK(std::ranges::find(left, "Welcome") == left.end());
    CHECK_FALSE(fx.run(Command::RemoveServiceItem, {{"itemId", removed}}));
}

TEST_CASE("Bridge: renameService and deleteService change the service library",
          "[app][remote][bridge][other-clients]")
{
    SampleFixture fx;
    REQUIRE(fx.run(Command::RenameService,
                   {{"serviceId", fx.lib.service}, {"name", "Morning Worship"}}));
    CHECK(fx.bridge.section(Section::Session)["openServiceName"] == "Morning Worship");

    REQUIRE(fx.run(Command::DeleteService, {{"serviceId", fx.lib.other_service}}));
    CHECK(service_names(fx.bridge.service_list("", ""))
          == std::vector<std::string>{"Morning Worship"});
    CHECK_FALSE(fx.run(Command::DeleteService, {{"serviceId", fx.lib.other_service}}));
}

TEST_CASE("Bridge: items are pinned to a stack and linked to play together",
          "[app][remote][bridge][other-clients][stacks]")
{
    SampleFixture fx;
    const std::string lower = fx.add_stack("Lower thirds");
    const auto grace = fx.item_id("Amazing Grace");
    const auto reading = fx.item_id("John 3:16-17");
    const auto picture = fx.item_id("sunrise.png");

    REQUIRE(fx.run(Command::SetServiceItemStack, {{"itemId", reading}, {"stackId", lower}}));
    CHECK(fx.items()[3]["stackId"] == lower);

    // Together: only items on different stacks.
    CHECK_FALSE(fx.run(Command::LinkServiceItems, {{"itemIds", {grace, picture}}}));
    REQUIRE(fx.run(Command::LinkServiceItems, {{"itemIds", {grace, reading}}}));
    CHECK(fx.items()[0]["alsoPlayIds"] == nlohmann::json::array({reading}));
    REQUIRE(fx.run(Command::UnlinkServiceItem, {{"itemId", grace}}));
    CHECK(fx.items()[0]["alsoPlayIds"].empty());

    // The same, named from one side.
    REQUIRE(fx.run(Command::SetServiceItemAlsoPlay,
                   {{"itemId", grace}, {"companionIds", {reading}}}));
    CHECK(fx.items()[0]["alsoPlayIds"] == nlohmann::json::array({reading}));

    // Back to its type's stack.
    REQUIRE(fx.run(Command::SetServiceItemStack, {{"itemId", reading}, {"stackId", ""}}));
    CHECK(fx.items()[3]["stackId"] == "");
}

TEST_CASE("Bridge: armSendTo names the stack the next item goes to",
          "[app][remote][bridge][other-clients][stacks]")
{
    SampleFixture fx;
    const std::string lower = fx.add_stack("Lower thirds");
    REQUIRE(fx.run(Command::ArmSendTo, {{"stackId", lower}}));
    CHECK(fx.bridge.section(Section::Stacks)["armedSendToId"] == lower);
}

TEST_CASE("Bridge: another stack moves without moving the remote's tab",
          "[app][remote][bridge][other-clients][stacks]")
{
    SampleFixture fx;
    REQUIRE(fx.run(Command::SelectServiceItem, {{"index", 0}}));
    const std::string main = fx.bridge.section(Section::Stacks)["focusedStackId"];
    const auto* stack = fx.facade.session().runtime({main});
    REQUIRE(stack != nullptr);
    REQUIRE(stack->player().line_cursor() == 0);

    REQUIRE(fx.run(Command::NextSlideOnStack, {{"stackId", main}}));
    CHECK(stack->player().line_cursor() > 0);
    REQUIRE(fx.run(Command::JumpToLineOnStack, {{"stackId", main}, {"lineIndex", 0}}));
    CHECK(stack->player().line_cursor() == 0);
    CHECK_FALSE(fx.run(Command::NextSlideOnStack, {{"stackId", "no-such-stack"}}));

    // The service's next slide, wherever it is.
    REQUIRE(fx.run(Command::NextServiceSlide));
    CHECK(stack->player().line_cursor() > 0);
}

TEST_CASE("Bridge: playImageFolder plays a folder from the picture asked for",
          "[app][remote][bridge][other-clients]")
{
    SampleFixture fx;
    REQUIRE(fx.run(Command::PlayImageFolder,
                   {{"folderId", fx.lib.image_folder}, {"startIndex", 1}}));
    const auto images = fx.bridge.section(Section::Images);
    CHECK(images["active"] == true);
    CHECK(images["images"].size() == 2);
    CHECK(images["index"] == 1);
    CHECK_FALSE(fx.run(Command::PlayImageFolder, {{"folderId", 987654}}));
}

// --- audio, web pages, live sources ----------------------------------------

TEST_CASE("Bridge: library_page lists audio, and web pages with their folders",
          "[app][remote][bridge]")
{
    SampleFixture fx;
    seventhslide::remote::LibraryQuery q;
    q.kind = "audio";
    const auto audio = fx.bridge.library_page(q)["rows"];
    REQUIRE(audio.size() == 3);
    CHECK(audio[0]["name"] == "Preludes");
    CHECK(audio[1]["filename"] == "Gathering pad.wav");
    CHECK(audio[2]["title"] == "Postlude.flac");

    q.kind = "web";
    const auto web = fx.bridge.library_page(q)["rows"];
    REQUIRE(web.size() == 3);
    CHECK(web[0]["name"] == "Announcements");
    CHECK(web[0].contains("folderId"));
    CHECK(web[1]["name"] == "Countdown");
    CHECK(web[1]["url"] == "https://example.com/countdown");
    CHECK(web[1]["id"].is_number_integer());
    // The still is named by the page's look.
    CHECK_FALSE(web[1]["thumb"].get<std::string>().empty());

    q.query = "welcome";
    const auto found = fx.bridge.library_page(q)["rows"];
    REQUIRE(found.size() == 1);
    CHECK(found[0]["name"] == "Welcome slides");
}

TEST_CASE("Bridge: reading the Live tab looks for sources for a while",
          "[app][remote][bridge][live]")
{
    seventhslide::test::QtBootstrap qt;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    Remote bridge{facade};
    auto* live = facade.liveLibrary();
    live->set_finder([] {
        return std::vector<seventhslide::model::LiveSource>{{"ndi", "STAGE (Camera 1)", ""},
                                                            {"ndi", "STAGE (Camera 2)", ""}};
    });
    REQUIRE_FALSE(live->searching());

    seventhslide::remote::LibraryQuery q;
    q.kind = "live";
    const auto rows = bridge.library_page(q)["rows"];
    CHECK(live->searching());
    REQUIRE(rows.size() == 2);
    CHECK(rows[0]["name"] == "STAGE (Camera 1)");
    CHECK(rows[0]["kind"] == "ndi");
    CHECK(rows[0]["key"] == "ndi:STAGE (Camera 1)");
    CHECK(rows[0]["thumbRev"].is_number_integer());
    q.query = "camera 2";
    CHECK(bridge.library_page(q)["rows"].size() == 1);

    // The lease runs out unless read again; the console's tab keeps its own.
    // Its timer fires when the event loop next gets to it, which on a busy
    // computer can be well past its millisecond.
    live->keep_searching_for(std::chrono::milliseconds(1));
    CHECK(QTest::qWaitFor([&] { return !live->searching(); }, 5000));
    live->setSearching(true);
    live->keep_searching_for(std::chrono::milliseconds(1));
    QTest::qWait(20);
    CHECK(live->searching());
    live->setSearching(false);
    CHECK_FALSE(live->searching());
}

TEST_CASE("Bridge: audio, a web page and a live source play on the remote's tab",
          "[app][remote][bridge][live]")
{
    seventhslide::test::QtBootstrap qt;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("Main")});
    seventhslide::test::FakeWebRenderers renderers;
    facade.set_web_renderer_factory(renderers.factory());
    seventhslide::test::FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());
    facade.liveLibrary()->set_finder([] {
        return std::vector<seventhslide::model::LiveSource>{{"ndi", "STAGE (Camera 1)", ""}};
    });
    Remote bridge{facade};
    const auto run = [&bridge](Command c, const nlohmann::json& args = nlohmann::json::object()) {
        const auto result = bridge.invoke(c, args);
        INFO(result.error);
        return result.ok;
    };

    REQUIRE(run(Command::PlayAudio, {{"filename", "hymn.mp3"}}));
    CHECK(bridge.section(Section::Session)["mode"] == "audio");

    const auto saved = facade.webLibrary()->savePage(
        -1, {{QStringLiteral("name"), QStringLiteral("Countdown")},
             {QStringLiteral("url"), QStringLiteral("example.com/countdown")}});
    const auto page = saved.value(QStringLiteral("id")).toLongLong();
    REQUIRE(page > 0);
    REQUIRE(run(Command::PlayWebPage, {{"pageId", page}}));
    auto session = bridge.section(Section::Session);
    CHECK(session["webMode"] == true);
    CHECK(session["webUrl"] == "https://example.com/countdown");
    CHECK(session["webCanGoBack"] == false);
    CHECK(session.contains("previewRevision"));
    CHECK_FALSE(run(Command::WebBack));
    CHECK(run(Command::WebReload));
    CHECK_FALSE(run(Command::PlayWebPage, {{"pageId", 987654}}));

    // A source is named as the console names it: by kind and name.
    seventhslide::remote::LibraryQuery q;
    q.kind = "live";
    (void)bridge.library_page(q);
    REQUIRE(run(Command::PlayLiveSource, {{"kind", "ndi"}, {"name", "STAGE (Camera 1)"}}));
    session = bridge.section(Section::Session);
    CHECK(session["liveSourceMode"] == true);
    CHECK(session["webMode"] == false);
    CHECK(session.contains("liveSourceLost"));
    CHECK(session.contains("previewRevision"));
}

TEST_CASE("Bridge: a live source's still and a web page's are thumbnails",
          "[app][remote][bridge][live]")
{
    seventhslide::test::QtBootstrap qt;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    seventhslide::test::FakeLiveReceivers receivers;
    facade.set_live_receiver_factory(receivers.factory());
    auto* live = facade.liveLibrary();
    live->set_finder([] {
        return std::vector<seventhslide::model::LiveSource>{{"ndi", "STAGE (Camera 1)", ""}};
    });
    live->setSearching(true);
    Remote bridge{facade};

    seventhslide::remote::ThumbRequest r;
    r.kind = "live";
    r.name = "ndi:STAGE (Camera 1)";
    r.width = 160;
    const auto pending = bridge.thumb(r);
    REQUIRE(pending.encode);
    const auto still = pending.encode();
    CHECK(still.ok);
    CHECK(still.mime == "image/jpeg");
    r.name = "ndi:Nothing";
    CHECK_FALSE(bridge.thumb(r).encode);

    // A page that names nothing has no still.
    r = {};
    r.kind = "web";
    r.id = 987654;
    CHECK_FALSE(bridge.thumb(r).encode);
    live->setSearching(false);
}

TEST_CASE("Bridge: a slide's build steps reach the remote and the controller", "[app][remote][builds]")
{
    LiveFixture fx;
    // A slide whose box comes in on the first click and goes on the second.
    auto doc = seventhslide::freeform::make_document("Builds", {"out-Main"});
    seventhslide::freeform::Object box;
    box.id = "box";
    box.type = seventhslide::canvas::ObjectType::Text;
    box.text = "Potluck";
    box.w = 400;
    box.h = 100;
    seventhslide::canvas::Animation in;
    in.order = 1;
    in.effect = seventhslide::canvas::AnimationEffect::Fade;
    seventhslide::canvas::Animation out = in;
    out.order = 2;
    out.effect = seventhslide::canvas::AnimationEffect::FadeOut;
    box.animations = {in, out};
    doc.decks[0].slides[0].objects = {box};
    seventhslide::session::LiveFreeformContent live;
    live.name = "Builds";
    live.document = doc;
    fx.facade.session().play_freeform(std::move(live));

    auto* program = fx.facade.program();
    CHECK(program->step_count() == 3);
    CHECK(program->currentStep() == 0);
    nlohmann::json body = fx.bridge.section(Section::Session);
    CHECK(body["stepCursor"] == 0);
    CHECK(body["totalSteps"] == 3);

    // Next is a step, on the same slide.
    REQUIRE(fx.bridge.invoke(Command::NextSlide, nlohmann::json::object()).ok);
    body = fx.bridge.section(Section::Session);
    CHECK(body["stepCursor"] == 1);
    CHECK(body["lineCursor"] == 0);

    // The controller's line map: the slide's steps, and the live one.
    auto* map = fx.facade.program()->lines();
    map->reload();
    REQUIRE(map->rowCount() == 1);
    CHECK(map->data(map->index(0), seventhslide::app::LineMapModel::StepsRole).toInt() == 3);
    CHECK(map->data(map->index(0), seventhslide::app::LineMapModel::CurrentStepRole).toInt() == 1);

    // A mark on the thumbnail goes to its step.
    REQUIRE(program->jumpToStep(0, 2));
    CHECK(program->currentStep() == 2);
    CHECK_FALSE(program->jumpToStep(0, 3));
}

namespace {

/// A facade with one output and a library, whose Main stack has a Preview
/// and whose Lobby stack, in front of it, has none.
struct PreviewFixture {
    seventhslide::test::QtBootstrap qt_;
    SessionFacade facade;
    std::string main;
    std::string lobby;

    PreviewFixture()
    {
        REQUIRE(facade.open_memory_database());
        facade.set_outputs({make_output("Main")});
        REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
        const auto stacks = facade.stacks()->stacks();
        main = stacks.front().toMap().value(QStringLiteral("id")).toString().toStdString();
        lobby = stacks.back().toMap().value(QStringLiteral("id")).toString().toStdString();
        REQUIRE(facade.stacks()->setStackPreview(QString::fromStdString(main), true).isEmpty());
        facade.stacks()->setFocusedStackId(QString::fromStdString(main));
    }

    [[nodiscard]] const seventhslide::session::StackRuntime& program() const
    {
        return *facade.session().runtime({main});
    }
    [[nodiscard]] const seventhslide::session::StackRuntime& preview() const
    {
        return *facade.session().runtime({main, seventhslide::session::StackSide::Preview});
    }
};

}  // namespace

TEST_CASE("Bridge: the preview sections are the tab's Preview, as the live ones are its Program",
          "[app][remote][bridge][preview]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};

    // Nothing cued: there is a Preview, empty.
    auto preview = bridge.section(Section::Preview);
    CHECK(preview["available"] == true);
    CHECK(preview["occupied"] == false);

    fx.facade.live()->play_song(demo_song());
    preview = bridge.section(Section::Preview);
    const auto session = bridge.section(Section::Session);
    CHECK(preview["occupied"] == true);
    CHECK(preview["stackId"] == fx.main);
    CHECK(preview["currentTitle"] == "Test Song");
    CHECK(preview["totalLines"].get<int>() > 0);
    CHECK(preview["lineCursor"] == 0);
    CHECK(session["totalLines"] == 0);
    // Each field the session section has of its side, the preview has of
    // its own.
    for (const char* key : {"lineCursor", "totalLines", "stepCursor", "totalSteps",
                            "serviceItemIndex", "currentTitle", "imageMode", "videoMode",
                            "mediaTransport", "videoPlaying", "videoPositionSec",
                            "videoDurationSec", "currentImageFilename", "freeformGallery",
                            "webMode", "webUrl", "webLoading", "webProblem", "webCanGoBack",
                            "liveSourceMode", "liveSourceProblem", "liveSourceLost",
                            "outputSlides", "hasBuild"}) {
        INFO(key);
        CHECK(has_key(session, key));
        CHECK(has_key(preview, key));
    }
    // What is the session's alone stays there.
    CHECK_FALSE(has_key(preview, "globalBlank"));
    CHECK_FALSE(has_key(preview, "openServiceId"));

    const auto lines = bridge.section(Section::PreviewLines);
    CHECK(lines["stackId"] == fx.main);
    CHECK(lines["build"] == fx.preview().build_id());
    CHECK(lines["rows"].size() == static_cast<std::size_t>(preview["totalLines"].get<int>()));
    CHECK(lines["nextChange"].size() == lines["rows"].size());
    CHECK(bridge.section(Section::Lines)["rows"].empty());
    CHECK(bridge.section(Section::PreviewImages)["active"] == false);

    // A stack without a Preview has none to describe.
    fx.facade.stacks()->armSendTo(QString::fromStdString(fx.lobby));
    fx.facade.live()->play_song(demo_song());
    REQUIRE(bridge.invoke(Command::SetFocusedStackId, {{"stackId", fx.lobby}}).ok);
    preview = bridge.section(Section::Preview);
    CHECK(preview["available"] == false);
    CHECK(preview["occupied"] == false);
    CHECK(bridge.section(Section::PreviewLines)["rows"].empty());
}

TEST_CASE("Bridge: the stacks section lists what is cued, and this computer's Fade length",
          "[app][remote][bridge][preview]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};
    fx.facade.live()->play_song(demo_song());

    const auto stacks = bridge.section(Section::Stacks);
    REQUIRE(stacks["occupied"].size() == 1);
    const auto& entry = stacks["occupied"][0];
    CHECK(entry["id"] == fx.main);
    CHECK(entry["cued"] == true);
    CHECK(entry["live"] == false);
    CHECK(entry["preview"] == true);
    CHECK(has_key(entry, "iconUrlActive"));
    const auto main_def = std::ranges::find_if(stacks["defs"], [&](const nlohmann::json& d) {
        return d["id"] == fx.main;
    });
    REQUIRE(main_def != stacks["defs"].end());
    CHECK((*main_def)["preview"] == true);

    // The take's Fade, which setTakeFade sets and the section follows.
    CHECK(stacks["takeFadeMs"] == 500);
    REQUIRE(bridge.invoke(Command::SetTakeFade, {{"durationMs", 1500}}).ok);
    CHECK(bridge.section(Section::Stacks)["takeFadeMs"] == 1500);
    CHECK(fx.facade.stacks()->takeFadeMs() == 1500);
}

TEST_CASE("Bridge: a side names the Preview for moving through it and its media",
          "[app][remote][bridge][preview]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};
    fx.facade.live()->play_song(demo_song());
    fx.facade.live()->play_song(demo_song(), seventhslide::session::Land::Live);

    // Named, the Preview moves, as the Program does unnamed: the same song,
    // the same moves, the same lines.
    REQUIRE(bridge.invoke(Command::NextSlide, {{"side", "preview"}}).ok);
    const int next = fx.preview().player().line_cursor();
    CHECK(next > 0);
    CHECK(fx.program().player().line_cursor() == 0);
    REQUIRE(bridge.invoke(Command::PrevSlide, {{"side", "preview"}}).ok);
    const int back = fx.preview().player().line_cursor();
    CHECK(back < next);
    REQUIRE(bridge.invoke(Command::NextSlide, nlohmann::json::object()).ok);
    CHECK(fx.program().player().line_cursor() == next);
    REQUIRE(bridge.invoke(Command::PrevSlide, {{"side", "program"}}).ok);
    CHECK(fx.program().player().line_cursor() == back);
    CHECK(fx.preview().player().line_cursor() == back);
    // Back to the start of each, for what follows.
    REQUIRE(bridge.invoke(Command::JumpToLine, {{"lineIndex", 0}, {"side", "preview"}}).ok);
    REQUIRE(bridge.invoke(Command::JumpToLine, {{"lineIndex", 0}}).ok);
    REQUIRE(bridge.invoke(Command::JumpToLine, {{"lineIndex", 1}, {"side", "preview"}}).ok);
    CHECK(fx.preview().player().line_cursor() == 1);
    CHECK(bridge.section(Section::Preview)["lineCursor"] == 1);
    REQUIRE(bridge.invoke(Command::JumpToLine, {{"lineIndex", 1}, {"side", "program"}}).ok);
    CHECK(fx.program().player().line_cursor() == 1);
    CHECK_FALSE(bridge.invoke(Command::NextSlide, {{"side", "sideways"}}).ok);

    // Video cued: its transport drives the Preview's, and the Program has none.
    REQUIRE(fx.facade.library()->createService(QStringLiteral("Media")) > 0);
    REQUIRE(fx.facade.media()->addVideoToService(QStringLiteral("clip.mp4")));
    REQUIRE(fx.facade.live()->selectServiceItem(0));
    REQUIRE(fx.preview().has_transport());
    CHECK_FALSE(bridge.invoke(Command::VideoPlay, nlohmann::json::object()).ok);
    REQUIRE(bridge.invoke(Command::VideoPlay, {{"side", "preview"}}).ok);
    CHECK(bridge.section(Section::Preview)["videoPlaying"] == true);
    REQUIRE(bridge.invoke(Command::VideoPause, {{"side", "preview"}}).ok);
    CHECK(bridge.section(Section::Preview)["videoPlaying"] == false);
    REQUIRE(bridge.invoke(Command::VideoSeek, {{"positionSec", 4.0}, {"side", "preview"}}).ok);
    CHECK(bridge.section(Section::Preview)["videoPositionSec"] == 4.0);
    REQUIRE(bridge.invoke(Command::VideoRestart, {{"side", "preview"}}).ok);
    CHECK(bridge.section(Section::Preview)["videoPlaying"] == true);
    CHECK(bridge.section(Section::Session)["mediaTransport"] == false);

    // A stack with no Preview refuses one.
    fx.facade.stacks()->armSendTo(QString::fromStdString(fx.lobby));
    fx.facade.live()->play_song(demo_song());
    REQUIRE(bridge.invoke(Command::SetFocusedStackId, {{"stackId", fx.lobby}}).ok);
    const auto refused = bridge.invoke(Command::NextSlide, {{"side", "preview"}});
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == SessionRemoteBridge::tr("This stack has no Preview.").toStdString());
}

TEST_CASE("Bridge: transition takes what is cued, by the theme's way or the one named",
          "[app][remote][bridge][preview]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};
    fx.facade.live()->play_song(demo_song());
    REQUIRE(bridge.invoke(Command::Transition, {{"stackId", fx.main}}).ok);
    CHECK(fx.program().song().title == "Test Song");
    CHECK_FALSE(fx.preview().occupied());
    CHECK(bridge.section(Section::Preview)["occupied"] == false);
    // Nothing cued: nothing to take.
    CHECK_FALSE(bridge.invoke(Command::Transition, {{"stackId", fx.main}}).ok);

    // By one of its own, as Transition's menu names it.
    fx.facade.live()->play_song(demo_song());
    REQUIRE(bridge.invoke(Command::Transition, {{"stackId", fx.main},
                                                {"type", "push"},
                                                {"durationMs", 800},
                                                {"direction", "left"}})
                .ok);
    CHECK(fx.facade.session().transition_ms_on(fx.main) == 800);
    // What was live went back.
    CHECK(fx.preview().occupied());

    // What it is not told cannot be guessed: an unknown name, or a length
    // with no type.
    fx.facade.live()->play_song(demo_song());
    CHECK_FALSE(bridge.invoke(Command::Transition, {{"stackId", fx.main}, {"type", "swirl"}}).ok);
    CHECK_FALSE(bridge.invoke(Command::Transition,
                              {{"stackId", fx.main}, {"type", "push"}, {"direction", "inward"}})
                    .ok);
    CHECK_FALSE(bridge.invoke(Command::Transition, {{"stackId", fx.main}, {"durationMs", 300}}).ok);
    CHECK_FALSE(bridge.invoke(Command::Transition, {{"stackId", fx.main}, {"easing", "linear"}}).ok);
    CHECK(fx.preview().occupied());
    CHECK_FALSE(bridge.invoke(Command::Transition, {{"stackId", fx.lobby}}).ok);
}

TEST_CASE("Bridge: a take from the remote trades the Controller's sides only on the stack it shows",
          "[app][remote][bridge][preview][scroll]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};
    QSignalSpy swapping(&fx.facade, &SessionFacade::focusedSidesSwapping);
    fx.facade.live()->play_song(demo_song(), seventhslide::session::Land::Live);
    fx.facade.live()->play_song(demo_song());

    // The console shows Main, as the remote does: said once, the console's
    // focus back on Main by then.
    QString focused_then;
    QObject::connect(&fx.facade, &SessionFacade::focusedSidesSwapping,
                     [&] { focused_then = fx.facade.stacks()->focusedStackId(); });
    REQUIRE(bridge.invoke(Command::Transition, {{"stackId", fx.main}}).ok);
    CHECK(swapping.count() == 1);
    CHECK(focused_then.toStdString() == fx.main);

    // The console on the Lobby: the remote's take on Main trades nothing the
    // Controller shows.
    fx.facade.stacks()->armSendTo(QString::fromStdString(fx.lobby));
    fx.facade.live()->play_song(demo_song());
    fx.facade.stacks()->setFocusedStackId(QString::fromStdString(fx.lobby));
    REQUIRE(fx.facade.stacks()->focusedStackId().toStdString() == fx.lobby);
    REQUIRE(fx.preview().occupied());
    REQUIRE(bridge.invoke(Command::Transition, {{"stackId", fx.main}}).ok);
    CHECK(swapping.count() == 1);
    CHECK(fx.facade.stacks()->focusedStackId().toStdString() == fx.lobby);

    // The remote on the Lobby's tab, the console on Main: the remote's take
    // on Main is on the stack the Controller shows, so it trades there,
    // once the console's focus is back.
    fx.facade.stacks()->setFocusedStackId(QString::fromStdString(fx.main));
    REQUIRE(bridge.invoke(Command::SetFocusedStackId, {{"stackId", fx.lobby}}).ok);
    REQUIRE(fx.facade.stacks()->focusedStackId().toStdString() == fx.main);
    REQUIRE(fx.preview().occupied());
    focused_then.clear();
    REQUIRE(bridge.invoke(Command::Transition, {{"stackId", fx.main}}).ok);
    CHECK(swapping.count() == 2);
    CHECK(focused_then.toStdString() == fx.main);
}

TEST_CASE("Bridge: clearPreview drops what is cued and leaves what is live",
          "[app][remote][bridge][preview]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};
    fx.facade.live()->play_song(demo_song(), seventhslide::session::Land::Live);
    fx.facade.live()->play_song(demo_song());
    REQUIRE(fx.preview().occupied());

    REQUIRE(bridge.invoke(Command::ClearPreview, {{"stackId", fx.main}}).ok);
    CHECK_FALSE(fx.preview().occupied());
    CHECK(fx.program().occupied());
    CHECK(bridge.section(Section::Stacks)["occupied"][0]["cued"] == false);
    // A stack without one has none to clear.
    CHECK_FALSE(bridge.invoke(Command::ClearPreview, {{"stackId", fx.lobby}}).ok);
}

TEST_CASE("Bridge: toggleStackBlank with a side blanks what is cued, to go live blanked",
          "[app][remote][bridge][preview]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};
    const auto& session = fx.facade.session();
    fx.facade.live()->play_song(demo_song(), seventhslide::session::Land::Live);
    // Nothing cued has nothing to blank.
    const nlohmann::json cued_blank{{"stackId", fx.main}, {"side", "preview"}};
    CHECK_FALSE(bridge.invoke(Command::ToggleStackBlank, cued_blank).ok);
    fx.facade.live()->play_song(demo_song());

    REQUIRE(bridge.invoke(Command::ToggleStackBlank, cued_blank).ok);
    CHECK(session.preview_is_blank(fx.main));
    CHECK_FALSE(session.stack_is_blank(fx.main));
    CHECK(bridge.section(Section::Preview)["blank"] == true);
    CHECK(bridge.section(Section::Stacks)["occupied"][0]["previewBlank"] == true);
    CHECK(bridge.section(Section::Stacks)["occupied"][0]["blank"] == false);

    // The take: it goes live blanked.
    REQUIRE(bridge.invoke(Command::Transition, {{"stackId", fx.main}}).ok);
    CHECK(session.stack_is_blank(fx.main));
    CHECK_FALSE(session.preview_is_blank(fx.main));
    CHECK(bridge.section(Section::Preview)["blank"] == false);

    // A stack without a Preview has none to blank.
    REQUIRE(bridge.invoke(Command::SetFocusedStackId, {{"stackId", fx.lobby}}).ok);
    fx.facade.stacks()->armSendTo(QString::fromStdString(fx.lobby));
    fx.facade.live()->play_song(demo_song());
    CHECK_FALSE(bridge.invoke(Command::ToggleStackBlank,
                              {{"stackId", fx.lobby}, {"side", "preview"}}).ok);
}

TEST_CASE("Bridge: a tab with only something cued stays the remote's",
          "[app][remote][bridge][preview][stacks]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};
    fx.facade.stacks()->armSendTo(QString::fromStdString(fx.lobby));
    fx.facade.live()->play_song(demo_song());
    fx.facade.stacks()->armSendTo(QString::fromStdString(fx.main));
    fx.facade.live()->play_song(demo_song());
    REQUIRE(fx.preview().occupied());
    REQUIRE_FALSE(fx.program().occupied());

    // Main shows nothing live, but is being worked: the remote stays on it.
    REQUIRE(bridge.invoke(Command::SetFocusedStackId, {{"stackId", fx.main}}).ok);
    CHECK(bridge.section(Section::Stacks)["focusedStackId"] == fx.main);
    CHECK(bridge.section(Section::Preview)["occupied"] == true);
    // Cleared there too, it shows the stack in front that has something.
    REQUIRE(bridge.invoke(Command::ClearPreview, {{"stackId", fx.main}}).ok);
    CHECK(bridge.section(Section::Stacks)["focusedStackId"] == fx.lobby);
}

TEST_CASE("Bridge: a Preview's slide thumb is of the Preview", "[app][remote][bridge][preview]")
{
    PreviewFixture fx;
    Remote bridge{fx.facade};
    fx.facade.live()->play_song(demo_song(), seventhslide::session::Land::Live);
    // Two lines to a slide: a line is not a slide.
    Song cued = demo_song();
    cued.title = "Cued";
    cued.lyrics = "[V1]\nCoffee after the service\nIn the hall\n\n[V2]\nTea as well\nAnd cake";
    fx.facade.live()->play_song(cued);
    REQUIRE(fx.preview().player().outputs().front().line_to_slide.at(1) == 0);

    const auto slide_of = [&](const std::string& side, int slide) {
        seventhslide::remote::ThumbRequest r;
        r.kind = "preview";
        r.name = "out-Main";
        r.width = 320;
        r.slide_index = slide;
        r.stack = fx.main;
        r.side = side;
        const auto pending = bridge.thumb(r);
        REQUIRE(pending.encode);
        const auto result = pending.encode();
        REQUIRE(result.ok);
        return result.bytes;
    };
    CHECK(slide_of("preview", 0) != slide_of("", 0));
    // Without a slide, the one its current line is on.
    REQUIRE(bridge.invoke(Command::JumpToLine, {{"lineIndex", 1}, {"side", "preview"}}).ok);
    CHECK(slide_of("preview", -1) == slide_of("preview", 0));
    REQUIRE(bridge.invoke(Command::JumpToLine, {{"lineIndex", 2}, {"side", "preview"}}).ok);
    CHECK(slide_of("preview", -1) == slide_of("preview", 1));
    CHECK(slide_of("preview", -1) != slide_of("preview", 0));
}
