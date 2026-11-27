#include <catch2/catch_test_macros.hpp>

#include <seventhslide/model/stack.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

using seventhslide::model::StackDef;
using seventhslide::model::StackTypeDefaults;
using seventhslide::model::add_stack;
using seventhslide::model::k_default_stack_id;
using seventhslide::model::make_default_stack;
using seventhslide::model::normalize_stacks;
using seventhslide::model::normalize_type_defaults;
using seventhslide::model::output_enables_stack;
using seventhslide::model::output_enables_type;
using seventhslide::model::remove_stack;
using seventhslide::model::reorder_stacks;
using seventhslide::model::resolve_stack_id;
using seventhslide::model::is_known_stack_icon;
using seventhslide::model::k_default_stack_icon;
using seventhslide::model::k_stack_icons;
using seventhslide::model::new_stack_id;
using seventhslide::model::normalize_stack_icon;
using seventhslide::model::rename_stack;
using seventhslide::model::set_stack_icon;
using seventhslide::model::unique_stack_name;
using seventhslide::model::fallback_stack_id;
using seventhslide::model::release_type_default;

TEST_CASE("normalize_stacks seeds Main when empty", "[model][stacks]")
{
    const auto stacks = normalize_stacks({});
    REQUIRE(stacks.size() == 1);
    REQUIRE(stacks[0].id == k_default_stack_id);
    REQUIRE(stacks[0].name == "Main");
    REQUIRE(stacks[0].icon == "display");
}

TEST_CASE("cannot delete the last stack", "[model][stacks]")
{
    auto stacks = normalize_stacks({});
    StackTypeDefaults defaults;
    auto r = remove_stack(stacks, k_default_stack_id, defaults);
    REQUIRE_FALSE(r.ok);
    REQUIRE(stacks.size() == 1);
}

TEST_CASE("add/reorder/remove stacks remaps type defaults", "[model][stacks]")
{
    auto stacks = normalize_stacks({});
    auto added = add_stack(stacks, "Announcements", "info");
    REQUIRE(added.ok);
    REQUIRE(stacks.size() == 2);

    StackTypeDefaults defaults;
    defaults.for_type("freeform") = added.id;
    defaults = normalize_type_defaults(defaults, stacks);
    REQUIRE(defaults.for_type("freeform") == added.id);

    REQUIRE(reorder_stacks(stacks, {added.id, std::string(k_default_stack_id)}).ok);
    REQUIRE(stacks[0].id == added.id);
    REQUIRE(stacks.back().id == k_default_stack_id);

    auto gone = remove_stack(stacks, added.id, defaults);
    REQUIRE(gone.ok);
    REQUIRE(stacks.size() == 1);
    REQUIRE(defaults.for_type("freeform") == k_default_stack_id);
}

TEST_CASE("resolve_stack_id uses armed then item then type default", "[model][stacks]")
{
    auto stacks = normalize_stacks({});
    auto extra = add_stack(stacks, "Lobby", "info");
    StackTypeDefaults defaults;
    defaults.for_type("image") = extra.id;

    REQUIRE(resolve_stack_id(stacks, defaults, extra.id, "", "song") == extra.id);
    REQUIRE(resolve_stack_id(stacks, defaults, "", extra.id, "song") == extra.id);
    REQUIRE(resolve_stack_id(stacks, defaults, "", "", "image") == extra.id);
    REQUIRE(resolve_stack_id(stacks, defaults, "", "", "song") == k_default_stack_id);
    REQUIRE(resolve_stack_id(stacks, defaults, "missing", "also-missing", "video")
            == k_default_stack_id);
}

TEST_CASE("output_enables_type leaves out what is listed, by the type it plays as",
          "[model][stacks]")
{
    REQUIRE(output_enables_type({}, "video"));
    REQUIRE(output_enables_type({"song"}, "video"));
    REQUIRE_FALSE(output_enables_type({"video"}, "video"));
    // An image folder plays as an image.
    REQUIRE_FALSE(output_enables_type({"image"}, "image_folder"));
    REQUIRE(output_enables_type({"video"}, "image_folder"));
    // Nothing is left out for having no content.
    REQUIRE(output_enables_type({"song"}, "empty"));
    REQUIRE(output_enables_type({"song"}, ""));
}

TEST_CASE("output_enables_stack treats empty list as all-on", "[model][stacks]")
{
    REQUIRE(output_enables_stack({}, "main"));
    REQUIRE(output_enables_stack({"main"}, "main"));
    REQUIRE_FALSE(output_enables_stack({"main"}, "announcements"));
}

TEST_CASE("StackDef json round-trip", "[model][stacks]")
{
    StackDef s;
    s.id = "abc";
    s.name = "Lobby";
    s.icon = "info";
    const auto back = StackDef::from_json(s.to_json());
    REQUIRE(back.id == "abc");
    REQUIRE(back.name == "Lobby");
    REQUIRE(back.icon == "info");
    REQUIRE(StackDef::from_json({{"icon", "nope"}}).icon == "display");
}

// --- naming, icons, and ids ------------------------------------------------
//
// normalize_stacks and resolve_stack_id are covered above. These are the
// helpers the settings UI calls directly when the operator adds or edits a
// stack.

TEST_CASE("is_known_stack_icon accepts exactly the built-in set", "[model][stacks]")
{
    for (const auto icon : k_stack_icons) {
        INFO("icon " << icon);
        REQUIRE(is_known_stack_icon(icon));
    }
    REQUIRE(is_known_stack_icon(k_default_stack_icon));

    REQUIRE_FALSE(is_known_stack_icon(""));
    REQUIRE_FALSE(is_known_stack_icon("not-an-icon"));
    // Ids are stable and case-sensitive; a mis-cased value is not a match.
    REQUIRE_FALSE(is_known_stack_icon("Display"));
    REQUIRE_FALSE(is_known_stack_icon("slides"));
}

TEST_CASE("normalize_stack_icon falls back to the default", "[model][stacks]")
{
    // A stack rendered with an unknown icon id would draw nothing at all in the
    // tab strip, so anything unrecognised has to become the default here.
    for (const auto icon : k_stack_icons) {
        REQUIRE(normalize_stack_icon(icon) == std::string(icon));
    }
    REQUIRE(normalize_stack_icon("") == std::string(k_default_stack_icon));
    REQUIRE(normalize_stack_icon("garbage") == std::string(k_default_stack_icon));
    REQUIRE(normalize_stack_icon("Display") == std::string(k_default_stack_icon));
    REQUIRE(normalize_stack_icon("slides") == std::string(k_default_stack_icon));
    CHECK(normalize_stack_icon("image") == "image");
}

TEST_CASE("new_stack_id yields distinct 16-hex-digit ids", "[model][stacks]")
{
    std::set<std::string> seen;
    for (int i = 0; i < 200; ++i) {
        const std::string id = new_stack_id();
        INFO("id " << id);
        REQUIRE(id.size() == 16);
        REQUIRE(std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }));
        // A collision would silently merge two stacks' content.
        REQUIRE(seen.insert(id).second);
    }
}

TEST_CASE("unique_stack_name de-duplicates case-insensitively", "[model][stacks]")
{
    std::vector<StackDef> stacks;
    stacks.push_back(make_default_stack());  // "Main"

    // A free name is kept as typed.
    REQUIRE(unique_stack_name("Lyrics", stacks, std::nullopt) == "Lyrics");

    // A taken name gains a numeric suffix, and the comparison ignores case -
    // "main" and "Main" would be indistinguishable to the operator.
    REQUIRE(unique_stack_name("Main", stacks, std::nullopt) == "Main 2");
    REQUIRE(unique_stack_name("main", stacks, std::nullopt) == "main 2");
    REQUIRE(unique_stack_name("MAIN", stacks, std::nullopt) == "MAIN 2");

    // The suffix keeps counting past an existing "Main 2".
    StackDef second;
    second.id = "s2";
    second.name = "Main 2";
    stacks.push_back(second);
    REQUIRE(unique_stack_name("Main", stacks, std::nullopt) == "Main 3");
}

TEST_CASE("unique_stack_name trims, caps, and never returns empty", "[model][stacks]")
{
    const std::vector<StackDef> stacks{make_default_stack()};

    REQUIRE(unique_stack_name("  Padded  ", stacks, std::nullopt) == "Padded");

    // A blank name would render as an unclickable tab.
    REQUIRE(unique_stack_name("", stacks, std::nullopt) == "Stack");
    REQUIRE(unique_stack_name("   ", stacks, std::nullopt) == "Stack");
    REQUIRE(unique_stack_name("\t\n ", stacks, std::nullopt) == "Stack");

    // Long names are capped so the tab strip cannot be blown out.
    const std::string long_name(80, 'x');
    const std::string capped = unique_stack_name(long_name, stacks, std::nullopt);
    REQUIRE(capped.size() == 40);

    // Trailing whitespace exposed by the cap is trimmed too.
    const std::string spacey = std::string(39, 'y') + "    tail";
    const std::string capped_spacey = unique_stack_name(spacey, stacks, std::nullopt);
    REQUIRE(capped_spacey == std::string(39, 'y'));
}

TEST_CASE("unique_stack_name excludes the stack being renamed", "[model][stacks]")
{
    // Without the exclusion, re-saving a stack's own name would bump it to
    // "Main 2" every time the operator opened and closed the rename field.
    std::vector<StackDef> stacks;
    stacks.push_back(make_default_stack());

    REQUIRE(unique_stack_name("Main", stacks, std::size_t{0}) == "Main");
    REQUIRE(unique_stack_name("Main", stacks, std::nullopt) == "Main 2");
}

TEST_CASE("rename_stack renames in place and rejects unknown ids", "[model][stacks]")
{
    std::vector<StackDef> stacks;
    stacks.push_back(make_default_stack());
    StackDef second;
    second.id = "s2";
    second.name = "Lower Third";
    second.icon = "text";
    stacks.push_back(second);

    const auto ok = rename_stack(stacks, "s2", "Captions");
    REQUIRE(ok.ok);
    REQUIRE(ok.id == "s2");
    REQUIRE(stacks[1].name == "Captions");

    // Renaming onto a taken name is de-duplicated rather than refused.
    const auto dup = rename_stack(stacks, "s2", "Main");
    REQUIRE(dup.ok);
    REQUIRE(stacks[1].name == "Main 2");
    REQUIRE(stacks[0].name == "Main");

    // Re-applying a stack's own name is a no-op, not a bump.
    const auto same = rename_stack(stacks, "s2", "Main 2");
    REQUIRE(same.ok);
    REQUIRE(stacks[1].name == "Main 2");

    const auto missing = rename_stack(stacks, "nope", "Whatever");
    REQUIRE_FALSE(missing.ok);
    REQUIRE_FALSE(missing.message.empty());
    REQUIRE(stacks.size() == 2);
}

TEST_CASE("set_stack_icon normalizes and rejects unknown ids", "[model][stacks]")
{
    std::vector<StackDef> stacks;
    stacks.push_back(make_default_stack());

    const auto ok = set_stack_icon(stacks, std::string(k_default_stack_id), "video");
    REQUIRE(ok.ok);
    REQUIRE(stacks[0].icon == "video");

    // An icon id the tab strip cannot draw is coerced, not stored.
    const auto coerced = set_stack_icon(stacks, std::string(k_default_stack_id), "bogus");
    REQUIRE(coerced.ok);
    REQUIRE(stacks[0].icon == std::string(k_default_stack_icon));

    const auto missing = set_stack_icon(stacks, "nope", "video");
    REQUIRE_FALSE(missing.ok);
    REQUIRE_FALSE(missing.message.empty());
}

TEST_CASE("StackTypeDefaults::for_type reaches every slot", "[model][stacks]")
{
    StackTypeDefaults d;
    d.ids() = {"a", "b", "c", "d", "e", "f", "g", "h"};

    const StackTypeDefaults& c = d;
    REQUIRE(c.for_type("song") == "a");
    REQUIRE(c.for_type("bible") == "b");
    REQUIRE(c.for_type("freeform") == "c");
    REQUIRE(c.for_type("image") == "d");
    REQUIRE(c.for_type("video") == "e");
    REQUIRE(c.for_type("audio") == "f");
    REQUIRE(c.for_type("web") == "g");
    REQUIRE(c.for_type("live") == "h");

    // Synonyms route to the same slot as their canonical type.
    REQUIRE(c.for_type("image_folder") == "d");

    // Anything unrecognised lands on the song slot rather than dangling.
    REQUIRE(c.for_type("") == "a");
    REQUIRE(c.for_type("divider") == "a");

    // The non-const overload is a real reference into the struct.
    d.for_type("bible") = "changed";
    REQUIRE(d.ids()[1] == "changed");
    REQUIRE(d.to_json() == nlohmann::json{{"song", "a"},
                                          {"bible", "changed"},
                                          {"freeform", "c"},
                                          {"image", "d"},
                                          {"video", "e"},
                                          {"audio", "f"},
                                          {"web", "g"},
                                          {"live", "h"}});
    REQUIRE(StackTypeDefaults::from_json(d.to_json()) == d);

    // Saved before web pages and live sources had slots: they play where
    // videos do.
    auto older = d.to_json();
    older.erase("web");
    older.erase("live");
    REQUIRE(StackTypeDefaults::from_json(older).for_type("web") == "e");
    REQUIRE(StackTypeDefaults::from_json(older).for_type("live") == "e");

    // Settings saved before audio was a type have no entry for it: audio
    // plays on the main stack until a default is chosen.
    const auto saved = StackTypeDefaults::from_json(
        {{"song", "a"}, {"bible", "b"}, {"freeform", "c"}, {"image", "d"}, {"video", "e"}});
    REQUIRE(saved.for_type("video") == "e");
    REQUIRE(saved.for_type("audio") == seventhslide::model::k_default_stack_id);
}

TEST_CASE("a rejected stack reorder leaves the list untouched", "[model][stacks]")
{
    auto stacks = normalize_stacks({});
    const auto lobby = add_stack(stacks, "Lobby", "info");
    const auto lower = add_stack(stacks, "Lower thirds", "textBox");
    const auto before = stacks;
    const std::string main_id(k_default_stack_id);

    SECTION("an unknown id after a known one")
    {
        const auto r = reorder_stacks(stacks, {lobby.id, "nope", main_id});
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.message == "Unknown stack");
    }
    SECTION("a repeated id")
    {
        const auto r = reorder_stacks(stacks, {lobby.id, lobby.id, main_id});
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.message == "Duplicate stack id");
    }
    SECTION("too few ids")
    {
        const auto r = reorder_stacks(stacks, {lower.id, main_id});
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.message == "Id list must include every stack");
    }

    REQUIRE(stacks == before);
}

TEST_CASE("fallback_stack_id is Main while it exists, else the front-most stack", "[model][stacks]")
{
    auto stacks = normalize_stacks({});
    auto added = add_stack(stacks, "Lower Thirds", "text");
    REQUIRE(added.ok);
    // Main sits behind the new stack, but is still the fallback.
    REQUIRE(stacks.back().id == added.id);
    REQUIRE(fallback_stack_id(stacks) == k_default_stack_id);

    StackTypeDefaults defaults;
    REQUIRE(remove_stack(stacks, k_default_stack_id, defaults).ok);
    REQUIRE(fallback_stack_id(stacks) == added.id);
    // Every type that was on Main went to the new fallback.
    REQUIRE(defaults.for_type("song") == added.id);
}

TEST_CASE("release_type_default sends a type back to the fallback stack", "[model][stacks]")
{
    auto stacks = normalize_stacks({});
    auto added = add_stack(stacks, "Lower Thirds", "text");
    REQUIRE(added.ok);
    StackTypeDefaults defaults;
    defaults.for_type("bible") = added.id;

    auto r = release_type_default(defaults, stacks, "bible", added.id);
    REQUIRE(r.ok);
    REQUIRE(defaults.for_type("bible") == k_default_stack_id);

    // Main has nowhere to send it, and a type not on the stack is refused.
    REQUIRE_FALSE(release_type_default(defaults, stacks, "bible", k_default_stack_id).ok);
    REQUIRE_FALSE(release_type_default(defaults, stacks, "song", added.id).ok);
    REQUIRE_FALSE(release_type_default(defaults, stacks, "bogus", added.id).ok);
    REQUIRE(defaults.for_type("song") == k_default_stack_id);
}
