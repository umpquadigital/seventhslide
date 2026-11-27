#include <catch2/catch_test_macros.hpp>

#include <seventhslide/model/audio_fades.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/theme_keys.hpp>

#include <algorithm>
#include <iterator>
#include <set>
#include <span>
#include <string_view>
#include <string>
#include <vector>

using seventhslide::model::OutputConfig;
using seventhslide::model::ThemeEntry;
using seventhslide::model::is_bg_theme_key;
using seventhslide::model::k_default_theme_priority;
using seventhslide::model::normalize_theme_priority;

TEST_CASE("theme key partitions match expected sizes", "[model][theme_keys]")
{
    using seventhslide::model::key_in;
    using seventhslide::model::k_bible_theme_keys;
    using seventhslide::model::k_media_theme_keys;
    using seventhslide::model::k_song_theme_keys;
    REQUIRE(std::size(k_song_theme_keys) == 10);
    REQUIRE(std::size(k_bible_theme_keys) == 3);
    REQUIRE(std::size(k_media_theme_keys) == 2);
    REQUIRE(std::size(seventhslide::model::k_bg_theme_keys) == 6);
    // A text theme's design is its element lists; the keys it still owns are
    // the packing policy the layout engine reads before any frame, each kind
    // its own.
    for (const auto keys : {std::span<const std::string_view>(k_song_theme_keys),
                            std::span<const std::string_view>(k_bible_theme_keys),
                            std::span<const std::string_view>(k_media_theme_keys)}) {
        // Each content type's transition, under its context's name.
        REQUIRE(key_in("elements", keys));
        REQUIRE(key_in("transitions", keys));
        REQUIRE_FALSE(key_in("show_lyrics", keys));
        REQUIRE_FALSE(key_in("video_enabled", keys));
        REQUIRE_FALSE(key_in("line_height", keys));
        REQUIRE_FALSE(key_in("background_type", keys));
        REQUIRE_FALSE(key_in("name", keys));
    }
    REQUIRE(key_in("follow_lines", k_song_theme_keys));
    REQUIRE(key_in("fluid_slides", k_song_theme_keys));
    REQUIRE(key_in("verse_gap", k_song_theme_keys));
    REQUIRE(key_in("show_title_slides", k_song_theme_keys));
    REQUIRE(key_in("bible_fluid_verses", k_bible_theme_keys));
    REQUIRE_FALSE(key_in("bible_fluid_verses", k_song_theme_keys));
    REQUIRE_FALSE(key_in("audio", k_media_theme_keys));
    REQUIRE_FALSE(key_in("audio", k_song_theme_keys));
    REQUIRE(is_bg_theme_key("background_type"));
    REQUIRE_FALSE(is_bg_theme_key("elements"));
    REQUIRE_FALSE(is_bg_theme_key("font_family"));
}

TEST_CASE("the title-slide switch lives on the text theme", "[model][output][title]")
{
    OutputConfig oc;
    ThemeEntry text;
    text.id = "t";
    text.name = "Base";
    text.style = {{"show_title_slides", false}, {"elements", nlohmann::json::object()}};
    oc.song_themes.push_back(text);
    const auto dumped = oc.to_persist_dict();
    const auto restored = OutputConfig::from_dict(dumped);
    REQUIRE(restored.song_themes.size() == 1);
    REQUIRE(restored.song_themes[0].style.at("show_title_slides") == false);
}

TEST_CASE("an output keeps a theme library of each kind", "[model][output]")
{
    OutputConfig oc;
    oc.song_themes.push_back(ThemeEntry{.id = "s", .name = "Song"});
    oc.bible_themes.push_back(ThemeEntry{.id = "b", .name = "Bible"});
    oc.media_themes.push_back(ThemeEntry{.id = "m", .name = "Media"});
    oc.bg_themes.push_back(ThemeEntry{.id = "g", .name = "Background"});
    const auto restored = OutputConfig::from_dict(oc.to_persist_dict());
    REQUIRE(restored.song_themes.size() == 1);
    REQUIRE(restored.song_themes[0].id == "s");
    REQUIRE(restored.bible_themes.size() == 1);
    REQUIRE(restored.bible_themes[0].id == "b");
    REQUIRE(restored.media_themes.size() == 1);
    REQUIRE(restored.media_themes[0].id == "m");
    REQUIRE(restored.bg_themes.size() == 1);
    REQUIRE(restored.bg_themes[0].id == "g");
}

TEST_CASE("the content types an output leaves out are saved with it", "[model][output]")
{
    OutputConfig oc;
    REQUIRE(oc.disabled_content_types.empty());
    oc.disabled_content_types = {"video", "image"};
    const auto dumped = oc.to_persist_dict();
    REQUIRE(OutputConfig::from_dict(dumped).disabled_content_types
            == std::vector<std::string>{"video", "image"});

    // An output saved before there were any shows everything.
    auto without = dumped;
    without.erase("disabled_content_types");
    REQUIRE(OutputConfig::from_dict(without).disabled_content_types.empty());
}

TEST_CASE("normalize_theme_priority accepts permutations and rejects junk", "[model][theme_keys]")
{
    const std::vector<std::string> custom{"global", "content", "service"};
    REQUIRE(normalize_theme_priority(custom) == custom);

    const auto fallback = normalize_theme_priority({"nope"});
    REQUIRE(fallback.size() == std::size(seventhslide::model::k_default_theme_priority));
    REQUIRE(fallback[0] == std::string{k_default_theme_priority[0]});
}

TEST_CASE("a fresh output is a 1920x1080 canvas", "[model][output]")
{
    const OutputConfig oc;
    REQUIRE(oc.canvas_width == 1920);
    REQUIRE(oc.canvas_height == 1080);
    REQUIRE(oc.song_themes.empty());
    REQUIRE(oc.bg_themes.empty());
}

TEST_CASE("to_persist_dict / from_dict round-trip preserves style and structural fields", "[model][output]")
{
    OutputConfig oc;
    oc.name = "House";
    oc.canvas_width = 1280;

    ThemeEntry text;
    text.id = "abc";
    text.name = "Base";
    text.style = {{"follow_lines", 2}, {"fluid_slides", true}, {"bible_fluid_verses", true}};
    text.tags = {"default"};
    text.created_at = "2026-01-02T03:04:05";
    oc.song_themes.push_back(text);

    ThemeEntry bg;
    bg.id = "def";
    bg.name = "Dark";
    bg.style = {{"background_type", "color"}, {"background_color", "#112233"}};
    oc.bg_themes.push_back(bg);

    oc.category_defaults = {
        {"song", {{"theme", "abc"}, {"bg", "def"}}},
        {"bible", {{"theme", "abc"}, {"bg", "def"}}},
    };

    const auto dumped = oc.to_persist_dict();
    REQUIRE(dumped["name"] == "House");
    REQUIRE(dumped["canvas_width"] == 1280);
    REQUIRE_FALSE(dumped.contains("font_size"));
    REQUIRE_FALSE(dumped.contains("follow_lines"));
    REQUIRE(dumped.contains("song_themes"));
    REQUIRE_FALSE(dumped.contains("index"));
    REQUIRE_FALSE(dumped.contains("is_blank"));
    REQUIRE_FALSE(dumped.contains("slides"));

    auto again = OutputConfig::from_dict(dumped);
    REQUIRE(again.name == "House");
    REQUIRE(again.canvas_width == 1280);
    REQUIRE(again.song_themes[0].style.at("follow_lines") == 2);
    REQUIRE(again.song_themes[0].style.at("fluid_slides") == true);
    REQUIRE(again.bg_themes[0].style.at("background_color") == "#112233");
    REQUIRE(again.song_themes.size() == 1);
    REQUIRE(again.song_themes[0].id == "abc");
    REQUIRE(again.song_themes[0].tags == std::vector<std::string>{"default"});
    REQUIRE(again.song_themes[0].created_at == "2026-01-02T03:04:05");
    REQUIRE(again.bg_themes[0].created_at.empty());
    REQUIRE(again.bg_themes[0].name == "Dark");
    REQUIRE(again.category_defaults["song"]["theme"] == "abc");
}

// Stored outputs still carry an ``index`` (their order is the row order) and
// may carry style keys from before themes owned them.
TEST_CASE("from_dict ignores the stored index and retired style keys", "[model][output]")
{
    nlohmann::json raw = {
        {"name", "Foldback"},
        {"font_size", 40},
        {"index", 7},
        {"canvas_width", 1920},
    };
    auto oc = OutputConfig::from_dict(raw);
    REQUIRE(oc.name == "Foldback");
    REQUIRE(oc.canvas_width == 1920);
    REQUIRE(oc.song_themes.empty());
    REQUIRE_FALSE(oc.to_persist_dict().contains("index"));
}

TEST_CASE("to_persist_dict keeps structural + intrinsic keys only", "[model][output]")
{
    OutputConfig oc;
    oc.name = "Main";
    oc.enabled_stack_ids = {"main"};
    oc.song_themes.push_back(ThemeEntry{.id = "t1", .name = "Base"});

    const auto p = oc.to_persist_dict();
    REQUIRE(p["name"] == "Main");
    REQUIRE_FALSE(p.contains("index"));
    // Media switches are text-theme keys now, not per-output state.
    REQUIRE_FALSE(p.contains("video_enabled"));
    REQUIRE_FALSE(p.contains("image_enabled"));
    REQUIRE(p["enabled_stack_ids"] == nlohmann::json::array({"main"}));
    REQUIRE(p.contains("song_themes"));
    REQUIRE_FALSE(p.contains("font_size"));
    REQUIRE_FALSE(p.contains("align"));
}

TEST_CASE("an ill-typed value costs that value, not the whole output", "[model][output_config]")
{
    const nlohmann::json stored = {
        {"id", 42},                      // wrong type
        {"name", "Stage"},
        {"canvas_width", "wide"},        // wrong type
        {"canvas_height", 720},
        {"category_defaults", {1, 2}},   // wrong type
        {"song_themes", {{{"id", 5}, {"name", "T"}, {"tags", {"ok", 3}}}, "junk"}},
        {"bg_themes", "not a list"},     // wrong type
        {"index", "first"},              // wrong type
    };

    OutputConfig oc;
    REQUIRE_NOTHROW(oc = OutputConfig::from_dict(stored));
    const OutputConfig defaults;

    REQUIRE(oc.name == "Stage");
    REQUIRE(oc.canvas_height == 720);
    REQUIRE(oc.id.empty());
    REQUIRE(oc.canvas_width == defaults.canvas_width);
    REQUIRE(oc.category_defaults == nlohmann::json::object());
    REQUIRE(oc.bg_themes.empty());
    REQUIRE(oc.song_themes.size() == 1);
    REQUIRE(oc.song_themes[0].id.empty());
    REQUIRE(oc.song_themes[0].name == "T");
    REQUIRE(oc.song_themes[0].tags == std::vector<std::string>{"ok"});
}

TEST_CASE("an output's stored form is every field, each under its own key", "[model][output]")
{
    // Sorted, as nlohmann::json keeps an object's keys.
    const std::string every_field =
        R"({"audio_fades":{"fade_in_ms":300,"fade_out_ms":900,"on_play_pause":true,"overlap_ms":400},)"
        R"("bg_themes":[{"id":"g1","name":"Dark","style":{"background_color":"#112233"}}],)"
        R"("bible_themes":[{"id":"b1","name":"Bible","style":{}}],)"
        R"("canvas_height":720,"canvas_width":1280,)"
        R"("category_defaults":{"song":{"bg":"g1","theme":"s1"}},)"
        R"("disabled_content_types":["video"],"enabled_stack_ids":["main","side"],)"
        R"("exempt_from_global_blank":true,"exempt_from_global_freeze":true,"icon":"tv",)"
        R"("id":"out-1","media_themes":[],"name":"Main",)"
        R"("song_themes":[{"created_at":"2026-01-02T03:04:05","id":"s1","name":"Song",)"
        R"("style":{"follow_lines":2},"tags":["a","b"]}]})";
    CHECK(OutputConfig::from_dict(nlohmann::json::parse(every_field)).to_persist_dict().dump()
          == every_field);

    const std::string fresh =
        R"({"audio_fades":{"fade_in_ms":0,"fade_out_ms":0,"on_play_pause":false,"overlap_ms":0},)"
        R"("bg_themes":[],"bible_themes":[],"canvas_height":1080,"canvas_width":1920,)"
        R"("category_defaults":{},"disabled_content_types":[],"enabled_stack_ids":[],)"
        R"("exempt_from_global_blank":false,"exempt_from_global_freeze":false,)"
        R"("icon":"display","id":"","media_themes":[],"name":"","song_themes":[]})";
    CHECK(OutputConfig{}.to_persist_dict().dump() == fresh);
    CHECK(OutputConfig::from_dict(nlohmann::json::object()).to_persist_dict().dump() == fresh);
}

TEST_CASE("a theme entry keeps its fields and no others", "[model][output]")
{
    const auto stored = nlohmann::json::parse(
        R"({"created_at":"2026-01-02T03:04:05","id":"s1","name":"Song",)"
        R"("style":{"follow_lines":2},"tags":["a"],"title_layout_id":"layout-1"})");
    const auto entry = stored.get<ThemeEntry>();
    CHECK(entry.id == "s1");
    CHECK(entry.name == "Song");
    CHECK(entry.style == nlohmann::json{{"follow_lines", 2}});
    CHECK(entry.tags == std::vector<std::string>{"a"});
    CHECK(entry.created_at == "2026-01-02T03:04:05");
    auto kept = stored;
    kept.erase("title_layout_id");
    CHECK(nlohmann::json(entry) == kept);

    // A created_at that is not a string, or none, is not written.
    const auto odd = nlohmann::json{{"id", "b1"}, {"created_at", 7}}.get<ThemeEntry>();
    CHECK(odd.created_at.empty());
    CHECK_FALSE(nlohmann::json(odd).contains("created_at"));
}

TEST_CASE("an output writes the keys the key tables name, and no others", "[model][output]")
{
    const auto stored = OutputConfig{}.to_persist_dict();
    std::set<std::string> written;
    for (const auto& [key, value] : stored.items()) {
        written.insert(key);
    }
    std::set<std::string> named{"icon", std::string(seventhslide::model::k_audio_fades_key)};
    for (const auto key : seventhslide::model::k_output_structural_keys) {
        named.emplace(key);
    }
    for (const auto key : seventhslide::model::k_output_intrinsic_keys) {
        named.emplace(key);
    }
    CHECK(written == named);
}

TEST_CASE("an output keeps its own audio fades, each in range", "[model][output][audio]")
{
    using seventhslide::model::AudioFades;
    seventhslide::model::OutputConfig oc;
    oc.id = "o1";
    oc.name = "Main";
    // None set is all cuts.
    CHECK(seventhslide::model::OutputConfig::from_dict(oc.to_persist_dict()).audio_fades
          == AudioFades{});
    oc.audio_fades = {300, 900, 400};
    const auto back = seventhslide::model::OutputConfig::from_dict(oc.to_persist_dict());
    CHECK(back.audio_fades == AudioFades{300, 900, 400});

    // Out of range or not a number: clamped, or a cut.
    const auto read = AudioFades::from_json(
        {{"fade_in_ms", -5}, {"fade_out_ms", 999999}, {"overlap_ms", "long"}});
    CHECK(read == AudioFades{0, seventhslide::model::k_audio_fade_max_ms, 0});
    CHECK(AudioFades::from_json(nullptr) == AudioFades{});
}

TEST_CASE("an audio file's fades hold over a service's, and both over an output's",
          "[model][audio]")
{
    using seventhslide::model::AudioFades;
    using seventhslide::model::resolve_audio_fades;
    using seventhslide::model::set_audio_fades;
    const AudioFades output{300, 900, 400};
    const auto none = nlohmann::json::object();
    // Neither sets any: the output's.
    CHECK(resolve_audio_fades(output, none, none) == output);
    CHECK(resolve_audio_fades(output, nullptr, nullptr) == output);
    // The service's over the output's, field by field.
    const nlohmann::json service = {{"fade_out_ms", 2000}, {"overlap_ms", 0}};
    CHECK(resolve_audio_fades(output, service, none) == AudioFades{300, 2000, 0});
    // The file's over both.
    const nlohmann::json file = {{"fade_out_ms", 5000}};
    CHECK(resolve_audio_fades(output, service, file) == AudioFades{300, 5000, 0});
    CHECK(resolve_audio_fades(output, none, file) == AudioFades{300, 5000, 400});

    // Only the three are kept, each a number, clamped; nothing else is set.
    const nlohmann::json typed = {
        {"fade_in_ms", 0}, {"fade_out_ms", 70000}, {"overlap_ms", "x"}, {"loud", 3}};
    CHECK(set_audio_fades(typed)
          == nlohmann::json{{"fade_in_ms", 0},
                            {"fade_out_ms", seventhslide::model::k_audio_fade_max_ms}});
    CHECK(set_audio_fades("slow").empty());
    CHECK(resolve_audio_fades(output, typed, none)
          == AudioFades{0, seventhslide::model::k_audio_fade_max_ms, 400});
}

TEST_CASE("audio fades follow the theme priority, tier by tier", "[model][audio]")
{
    using seventhslide::model::AudioFades;
    using seventhslide::model::resolve_audio_fades;
    const AudioFades output{300, 900, 400};
    const nlohmann::json service = {{"fade_out_ms", 2000}};
    const nlohmann::json file = {{"fade_out_ms", 5000}, {"on_play_pause", true}};
    const auto with = [&](std::vector<std::string> priority) {
        return resolve_audio_fades(output, service, file, priority);
    };
    // Song / Item first: the file's, then the service's, then the output's.
    CHECK(with({"content", "service", "global"}) == AudioFades{300, 5000, 400, true});
    // Service first: its fade out over the file's.
    CHECK(with({"service", "content", "global"}) == AudioFades{300, 2000, 400, true});
    // Global first: the output's whole, whatever the others set.
    CHECK(with({"global", "content", "service"}) == output);
    // The service over the output, the file under both.
    CHECK(with({"service", "global", "content"}) == AudioFades{300, 2000, 400});
    // None given is the default order.
    CHECK(with({}) == with({"content", "service", "global"}));

    // The play/pause switch is a field like the rest: kept only as true or
    // false, and kept by the output.
    CHECK(seventhslide::model::set_audio_fades({{"on_play_pause", 1}}).empty());
    CHECK(seventhslide::model::set_audio_fades({{"on_play_pause", false}})
          == nlohmann::json{{"on_play_pause", false}});
    seventhslide::model::OutputConfig oc;
    oc.id = "o1";
    oc.audio_fades = {0, 600, 0, true};
    CHECK(seventhslide::model::OutputConfig::from_dict(oc.to_persist_dict()).audio_fades
          == AudioFades{0, 600, 0, true});
}
