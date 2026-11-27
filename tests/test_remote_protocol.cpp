#include <catch2/catch_test_macros.hpp>

#include <seventhslide/remote/remote_protocol.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace seventhslide::remote;

namespace {

/// Records what the protocol layer asked for, and answers whatever the test
/// wants. No sockets, no facade, no Qt Quick.
class FakeBridge final : public IRemoteBridge {
public:
    struct Call {
        std::string name;
        nlohmann::json args;
    };

    std::vector<Call> calls;
    bool succeed = true;
    bool throws = false;

    /// The view the tests' commands run for.
    View view;

    void settle(View&) const override {}
    [[nodiscard]] nlohmann::json section(Section s, const View&) const override
    {
        return {{"name", std::string(section_name(s))}};
    }

    [[nodiscard]] CommandResult invoke(seventhslide::remote::Command command,
                                       const nlohmann::json& args,
                                       View&) override
    {
        calls.push_back({std::string(protocol::command_spec(command).name), args});
        if (throws) {
            throw std::runtime_error("database is locked");
        }
        return succeed ? CommandResult::good() : CommandResult::fail("refused");
    }

    [[nodiscard]] nlohmann::json library_page(const LibraryQuery&) const override
    {
        return nlohmann::json::object();
    }
    [[nodiscard]] nlohmann::json service_list(std::string_view, std::string_view) const override
    {
        return nlohmann::json::object();
    }
    [[nodiscard]] std::string glyph_sprite() const override { return {}; }
    [[nodiscard]] PendingThumb thumb(const ThumbRequest&) const override { return {}; }
};

bool table_has(std::string_view name)
{
    return protocol::find_command(name) != nullptr;
}

} // namespace

TEST_CASE("Protocol: the table has one entry per command, in order", "[remote][R3]")
{
    const auto& table = protocol::command_table();
    REQUIRE(table.size() == k_command_count);
    for (std::size_t i = 0; i < table.size(); ++i) {
        INFO("entry " << i << " " << table[i].name);
        REQUIRE(static_cast<std::size_t>(table[i].command) == i);
        REQUIRE(&protocol::command_spec(table[i].command) == &table[i]);
        REQUIRE(protocol::find_command(table[i].name) == &table[i]);
    }
}

TEST_CASE("Protocol: the whitelist admits the remote's scope", "[remote][R3]")
{
    REQUIRE(table_has("nextSlide"));
    REQUIRE(table_has("prevSlide"));
    REQUIRE(table_has("jumpToLine"));
    REQUIRE(table_has("toggleGlobalBlank"));
    REQUIRE(table_has("toggleStackBlank"));
    REQUIRE(table_has("clearBlankFreeze"));
    // Blank and Freeze have one target each; the moving one is gone.
    REQUIRE_FALSE(table_has("toggleBlank"));
    REQUIRE_FALSE(table_has("toggleFreeze"));
    REQUIRE(table_has("setFocusedStackId"));
    REQUIRE(table_has("selectServiceItem"));
    REQUIRE(table_has("reorderServiceItems"));
    REQUIRE(table_has("playSong"));
    REQUIRE(table_has("playBibleRef"));
    REQUIRE(table_has("playAudio"));
    REQUIRE(table_has("playWebPage"));
    REQUIRE(table_has("playLiveSource"));
    REQUIRE(table_has("webBack"));
    REQUIRE(table_has("webReload"));
    REQUIRE(table_has("resolveBibleRef"));
    REQUIRE(table_has("suggestBibleBooks"));
    REQUIRE(table_has("setAutoAdvance"));
    REQUIRE(table_has("setServiceItemStack"));
    REQUIRE(table_has("setServiceItemAlsoPlay"));
    REQUIRE(table_has("linkServiceItems"));
    REQUIRE(table_has("unlinkServiceItem"));
    REQUIRE(table_has("renameService"));
    REQUIRE(table_has("deleteService"));
}

TEST_CASE("Protocol: editing and configuration are unreachable", "[remote][R3]")
{
    // The scope boundary is the security boundary. These all exist on
    // SessionFacade; none may be reachable from a phone on the LAN.
    for (const char* forbidden : {
             "deleteLibrarySong", "updateLibrarySong", "createLibrarySong",
             "updateAppSettings", "createOutput", "deleteOutput",
             "updateOutputSettings", "createTheme", "updateTheme", "deleteTheme",
             "beginFreeformEdit", "saveFreeformEdit", "deleteItem",
             "switchLibrary", "removeLibrary", "importSongs", "importBible",
             "readSongFiles", "importReadSongs",
             "createService", "setServiceTags", "renameServiceGroup", "deleteServiceGroup",
             "deleteMediaFolder", "renameMediaFolder", "sendOutput",
             // Every library tab's folder calls (FolderLibrary).
             "createFolder", "renameFolder", "deleteFolder", "moveFolder", "moveItems",
             "reorderItems", "reorderFolders", "deleteItem", "duplicateItem",
             // The Browser and Live tabs are played, not edited; an address
             // is typed on the console.
             "savePage", "saveStream", "deleteStream", "addSourceToService", "webGoTo",
         }) {
        INFO("must not be whitelisted: " << forbidden);
        REQUIRE_FALSE(table_has(forbidden));
    }
}

TEST_CASE("Protocol: unknown commands are rejected before the bridge", "[remote][R3]")
{
    FakeBridge bridge;
    const auto out = protocol::dispatch(bridge, "deleteEverything", {}, bridge.view);
    REQUIRE_FALSE(out.result.ok);
    REQUIRE(out.dirty.empty());
    // Crucially, the bridge was never consulted.
    REQUIRE(bridge.calls.empty());
}

TEST_CASE("Protocol: arguments are validated before the bridge", "[remote][R3]")
{
    FakeBridge bridge;

    SECTION("missing required argument")
    {
        const auto out = protocol::dispatch(bridge, "jumpToLine", nlohmann::json::object(), bridge.view);
        REQUIRE_FALSE(out.result.ok);
        REQUIRE(bridge.calls.empty());
    }

    SECTION("wrong type")
    {
        const auto out =
            protocol::dispatch(bridge, "jumpToLine", {{"lineIndex", "seven"}}, bridge.view);
        REQUIRE_FALSE(out.result.ok);
        REQUIRE(bridge.calls.empty());
    }

    SECTION("unknown argument fails loudly rather than being ignored")
    {
        const auto out = protocol::dispatch(
            bridge, "nextSlide", {{"stackId", "main"}}, bridge.view);
        REQUIRE_FALSE(out.result.ok);
        REQUIRE(bridge.calls.empty());
    }

    SECTION("optional argument may be omitted")
    {
        const auto out = protocol::dispatch(bridge, "addSongToService",
                                            {{"songId", 12}}, bridge.view);
        REQUIRE(out.result.ok);
        REQUIRE(bridge.calls.size() == 1);
        REQUIRE(bridge.calls[0].name == "addSongToService");
    }

    SECTION("resolveBibleRef accepts an optional bibleId so the remote "
            "does not depend on the operator's browse selection")
    {
        const auto out = protocol::dispatch(
            bridge, "resolveBibleRef",
            {{"reference", "John 3:16"}, {"bibleId", 2}}, bridge.view);
        REQUIRE(out.result.ok);
        REQUIRE(bridge.calls.size() == 1);
        REQUIRE(bridge.calls[0].args.at("bibleId") == 2);
    }

    SECTION("suggestBibleBooks is a read-only lookup with an optional bibleId")
    {
        const auto out = protocol::dispatch(
            bridge, "suggestBibleBooks",
            {{"prefix", "Reve"}, {"bibleId", 2}}, bridge.view);
        REQUIRE(out.result.ok);
        REQUIRE(bridge.calls.size() == 1);
        REQUIRE(bridge.calls[0].name == "suggestBibleBooks");
        REQUIRE(out.dirty.empty());
    }

    SECTION("renameService requires both serviceId and name")
    {
        const auto missing = protocol::dispatch(bridge, "renameService",
                                                {{"serviceId", 1}}, bridge.view);
        REQUIRE_FALSE(missing.result.ok);
        REQUIRE(bridge.calls.empty());
        const auto ok = protocol::dispatch(
            bridge, "renameService", {{"serviceId", 1}, {"name", "Sunday"}}, bridge.view);
        REQUIRE(ok.result.ok);
        REQUIRE(bridge.calls.size() == 1);
        REQUIRE(bridge.calls[0].name == "renameService");
    }
}

TEST_CASE("Protocol: a valid command reaches the bridge and marks sections", "[remote][R3]")
{
    FakeBridge bridge;
    const auto out = protocol::dispatch(bridge, "jumpToLine", {{"lineIndex", 4}}, bridge.view);

    REQUIRE(out.result.ok);
    REQUIRE(bridge.calls.size() == 1);
    REQUIRE(bridge.calls[0].name == "jumpToLine");
    REQUIRE(bridge.calls[0].args.at("lineIndex") == 4);
    REQUIRE_FALSE(out.dirty.empty());
    REQUIRE(std::find(out.dirty.begin(), out.dirty.end(), Section::Lines)
            != out.dirty.end());
}

TEST_CASE("Protocol: a refused command marks nothing dirty", "[remote][R3]")
{
    FakeBridge bridge;
    bridge.succeed = false;
    const auto out = protocol::dispatch(bridge, "nextSlide", {}, bridge.view);

    REQUIRE_FALSE(out.result.ok);
    REQUIRE(out.result.error == "refused");
    REQUIRE(bridge.calls.size() == 1);
    // Nothing changed, so nothing should be broadcast.
    REQUIRE(out.dirty.empty());
}

TEST_CASE("Protocol: integer arrays are type-checked element by element", "[remote][R3]")
{
    FakeBridge bridge;

    const auto bad = protocol::dispatch(bridge, "reorderServiceItems",
                                        {{"orderedItemIds", {1, "two", 3}}}, bridge.view);
    REQUIRE_FALSE(bad.result.ok);
    REQUIRE(bridge.calls.empty());

    const auto good = protocol::dispatch(bridge, "reorderServiceItems",
                                         {{"orderedItemIds", {3, 1, 2}}}, bridge.view);
    REQUIRE(good.result.ok);
    REQUIRE(bridge.calls.size() == 1);
}

TEST_CASE("Protocol: frames carry the shapes the client expects", "[remote][R2]")
{
    FakeBridge bridge;

    const auto hello = protocol::hello_frame("1a2b");
    REQUIRE(hello.at("type") == "hello");
    REQUIRE(hello.at("protocol") == protocol::k_protocol_version);
    REQUIRE(hello.at("boot") == "1a2b");

    const auto snapshot = protocol::snapshot_frame(bridge, bridge.view, 42);
    REQUIRE(snapshot.at("type") == "snapshot");
    REQUIRE(snapshot.at("rev") == 42);
    // Every section is present in a snapshot, so a fresh client is complete.
    for (const Section s : all_sections()) {
        REQUIRE(snapshot.at("state").contains(std::string(section_name(s))));
    }

    const auto ok = protocol::ack_frame("c-1", CommandResult::good());
    REQUIRE(ok.at("ok") == true);
    REQUIRE(ok.at("id") == "c-1");

    const auto bad = protocol::ack_frame("c-2", CommandResult::fail("nope"));
    REQUIRE(bad.at("ok") == false);
    REQUIRE(bad.at("error") == "nope");
}

TEST_CASE("Protocol: the library is not pushed in snapshots", "[remote][R2]")
{
    FakeBridge bridge;
    const auto snapshot = protocol::snapshot_frame(bridge, bridge.view, 1);
    // The library is paged over REST; a snapshot carrying it would grow with
    // every song in it.
    REQUIRE_FALSE(snapshot.at("state").contains("library"));
    REQUIRE_FALSE(snapshot.at("state").contains("songs"));
}

TEST_CASE("Protocol: a command that throws fails instead of escaping", "[remote][R3]")
{
    FakeBridge bridge;
    bridge.throws = true;
    const auto out = protocol::dispatch(bridge, "nextSlide", {}, bridge.view);

    REQUIRE_FALSE(out.result.ok);
    REQUIRE(out.result.error.find("database is locked") != std::string::npos);
    REQUIRE(out.dirty.empty());
}

TEST_CASE("Protocol: client frames decode tolerantly", "[remote][R3]")
{
    SECTION("a well-formed command")
    {
        const auto d = protocol::decode_client_message(
            R"({"type":"cmd","id":"c1","name":"jumpToLine","args":{"lineIndex":2}})");
        REQUIRE(d.message);
        REQUIRE(d.message->type == "cmd");
        REQUIRE(d.message->id == "c1");
        REQUIRE(d.message->name == "jumpToLine");
        REQUIRE(d.message->args.at("lineIndex") == 2);
    }
    SECTION("not JSON")
    {
        const auto d = protocol::decode_client_message("{nope");
        REQUIRE_FALSE(d.message);
        REQUIRE(d.error == "malformed message");
    }
    SECTION("JSON that is not an object")
    {
        for (const char* text : {"[]", "5", "\"cmd\"", "null"}) {
            const auto d = protocol::decode_client_message(text);
            REQUIRE_FALSE(d.message);
            REQUIRE(d.error == "message must be an object");
        }
    }
    SECTION("fields of the wrong type read as absent")
    {
        const auto d = protocol::decode_client_message(
            R"({"type":7,"id":5,"name":["nextSlide"],"args":null})");
        REQUIRE(d.message);
        REQUIRE(d.message->type.empty());
        REQUIRE(d.message->id.empty());
        REQUIRE(d.message->name.empty());
        REQUIRE(d.message->args == nlohmann::json::object());
    }
    SECTION("non-object args are kept for validate_args to refuse")
    {
        const auto d =
            protocol::decode_client_message(R"({"type":"cmd","name":"nextSlide","args":3})");
        REQUIRE(d.message);
        FakeBridge bridge;
        const auto out = protocol::dispatch(bridge, d.message->name, d.message->args, bridge.view);
        REQUIRE_FALSE(out.result.ok);
        REQUIRE(bridge.calls.empty());
    }
}
