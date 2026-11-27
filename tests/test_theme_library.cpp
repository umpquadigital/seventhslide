#include <seventhslide/theme/theme_library.hpp>
#include <seventhslide/theme/theme_elements.hpp>
#include <seventhslide/model/transition.hpp>

#include <catch2/catch_test_macros.hpp>

#include "test_temp_dir.hpp"

#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

using seventhslide::model::OutputConfig;
using seventhslide::model::ThemeEntry;
using seventhslide::theme::ThemeKind;
using seventhslide::theme::create_theme;
using seventhslide::theme::delete_theme;
using seventhslide::theme::filter_theme_style;
using seventhslide::theme::normalize_theme_tags;
using seventhslide::theme::seed_default_themes;
using seventhslide::theme::set_category_default;
using seventhslide::theme::update_theme;
using seventhslide::theme::set_owned_theme_asset;
using seventhslide::theme::new_owned_asset_name;
using seventhslide::theme::theme_assets_dir;
using seventhslide::theme::find_theme;

TEST_CASE("theme_library seeds a Base theme of each kind + category defaults", "[theme]")
{
    using seventhslide::theme::element_model;
    OutputConfig oc;
    oc.name = "Main";
    REQUIRE(seed_default_themes(oc));
    for (const auto* list : {&oc.song_themes, &oc.bible_themes, &oc.media_themes, &oc.bg_themes}) {
        REQUIRE(list->size() == 1);
        REQUIRE(list->front().name == "Base");
    }
    // Each kind's Base is its own contexts of the stock design. Behaviour and
    // background stay at their defaults, so they are not written as keys.
    const auto contexts = [](const ThemeEntry& t) {
        std::vector<std::string> out;
        for (const auto& [key, value] : element_model(t.style).items()) {
            if (value.is_array()) {
                out.push_back(key);
            }
        }
        return out;
    };
    CHECK(contexts(oc.song_themes[0]) == std::vector<std::string>{"song", "titles"});
    CHECK(contexts(oc.bible_themes[0]) == std::vector<std::string>{"bible"});
    CHECK(contexts(oc.media_themes[0])
          == std::vector<std::string>{"image", "live", "video", "web"});
    const auto song =
        seventhslide::theme::ElementSet::from_json(element_model(oc.song_themes[0].style))
            .at(seventhslide::theme::ElementContext::Song);
    bool sized_body = false;
    for (const auto& el : song) {
        if (el.binding == seventhslide::theme::ElementBinding::Lyrics) {
            sized_body = el.object.font_size == 48;
        }
    }
    REQUIRE(sized_body);
    REQUIRE_FALSE(oc.song_themes[0].style.contains("font_size"));
    REQUIRE_FALSE(oc.bg_themes[0].style.contains("background_color"));
    REQUIRE_FALSE(oc.bg_themes[0].style.contains("elements"));
    // Each category's default is its own kind's Base; media takes no background.
    REQUIRE(oc.category_defaults
            == nlohmann::json{
                {"song", {{"theme", oc.song_themes[0].id}, {"bg", oc.bg_themes[0].id}}},
                {"bible", {{"theme", oc.bible_themes[0].id}, {"bg", oc.bg_themes[0].id}}},
                {"media", {{"theme", oc.media_themes[0].id}}}});
    REQUIRE_FALSE(seed_default_themes(oc));
}

TEST_CASE("Each theme list an output stores is named for its kind", "[theme]")
{
    // What reads stored outputs without the theme module goes by this table.
    const auto& lists = seventhslide::model::k_output_theme_lists;
    REQUIRE(std::size(lists) == std::size(seventhslide::theme::k_theme_kinds));
    for (std::size_t i = 0; i < std::size(lists); ++i) {
        const auto kind = seventhslide::theme::k_theme_kinds[i];
        CHECK(lists[i].kind == seventhslide::theme::to_string(kind));
        OutputConfig oc;
        seventhslide::theme::theme_list(oc, kind).push_back(ThemeEntry{"x", "X"});
        const auto stored = oc.to_persist_dict();
        REQUIRE(stored.contains(std::string(lists[i].key)));
        CHECK(stored.at(std::string(lists[i].key)).size() == 1);
    }
}

TEST_CASE("An output given its Base themes twice gets the same ids", "[theme]")
{
    // As each time a library this computer only views is opened: the themes
    // are made in memory only, and must not change id each time.
    const auto seeded = [](const std::string& output_id) {
        OutputConfig oc;
        oc.id = output_id;
        REQUIRE(seed_default_themes(oc));
        std::vector<std::string> ids;
        for (const auto* list :
             {&oc.song_themes, &oc.bible_themes, &oc.media_themes, &oc.bg_themes}) {
            ids.push_back(list->at(0).id);
        }
        return ids;
    };
    const auto first = seeded("out-a");
    CHECK(seeded("out-a") == first);
    // Each kind's Base is its own.
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].size() == 32);
        for (std::size_t j = i + 1; j < first.size(); ++j) {
            CHECK(first[i] != first[j]);
        }
    }
    CHECK(seeded("out-b").front() != first.front());
}

TEST_CASE("theme_library keeps the title-slide switch on a song theme", "[theme][title]")
{
    // ``show_title_slides`` is a song-theme key: the style filter has to keep it
    // on a song theme (it is what the Slide tab saves) and drop it elsewhere.
    OutputConfig oc;
    oc.name = "Main";
    REQUIRE(seed_default_themes(oc));

    const nlohmann::json style = {{"show_title_slides", false}, {"follow_lines", 2}};
    REQUIRE(filter_theme_style(ThemeKind::Song, style).at("show_title_slides") == false);
    REQUIRE_FALSE(filter_theme_style(ThemeKind::Bible, style).contains("show_title_slides"));
    REQUIRE_FALSE(filter_theme_style(ThemeKind::Bg, style).contains("show_title_slides"));

    const auto created = create_theme(oc, ThemeKind::Song, "Titles", style, nullptr, "");
    REQUIRE(created.ok);
    REQUIRE(created.theme.style.at("show_title_slides") == false);

    nlohmann::json back_on = created.theme.style;
    back_on["show_title_slides"] = true;
    const auto updated = update_theme(oc, ThemeKind::Song, created.theme.id, "Titles",
                                      back_on, nullptr, false);
    REQUIRE(updated.ok);
    REQUIRE(updated.theme.style.at("show_title_slides") == true);
}

TEST_CASE("a stored theme holds only its kind's design and settings", "[theme]")
{
    using seventhslide::theme::ElementContext;
    // A whole design and every setting, as the designer's draft of a theme
    // used to carry them.
    nlohmann::json all = seventhslide::theme::default_elements().to_json();
    nlohmann::json transitions = nlohmann::json::object();
    for (const auto ctx : seventhslide::theme::k_content_contexts) {
        transitions[std::string(seventhslide::theme::to_string(ctx))] =
            seventhslide::model::Transition{};
    }
    // What a Media theme held for audio before its fades were each output's:
    // its settings, an elements list and a transition. The gate drops them.
    all["audio"] = nlohmann::json::array();
    transitions["audio"] = seventhslide::model::Transition{};
    const nlohmann::json style = {{"elements", all},
                                  {"transitions", transitions},
                                  {"follow_lines", 2},
                                  {"bible_fluid_verses", true},
                                  {"audio", {{"fade_in_ms", 500}}},
                                  {"background_color", "#000000"}};
    const auto keys = [](const nlohmann::json& obj) {
        std::vector<std::string> out;
        for (const auto& [key, value] : obj.items()) {
            out.push_back(key);
        }
        return out;
    };

    const auto song = filter_theme_style(ThemeKind::Song, style);
    CHECK(keys(song) == std::vector<std::string>{"elements", "follow_lines", "transitions"});
    CHECK(keys(song["elements"])
          == std::vector<std::string>{"canvas", "next_id", "song", "titles"});
    CHECK(keys(song["transitions"]) == std::vector<std::string>{"song"});

    const auto bible = filter_theme_style(ThemeKind::Bible, style);
    CHECK(keys(bible)
          == std::vector<std::string>{"bible_fluid_verses", "elements", "transitions"});
    CHECK(keys(bible["elements"]) == std::vector<std::string>{"bible", "canvas", "next_id"});
    CHECK(keys(bible["transitions"]) == std::vector<std::string>{"bible"});

    const auto media = filter_theme_style(ThemeKind::Media, style);
    CHECK(keys(media) == std::vector<std::string>{"elements", "transitions"});
    CHECK(keys(media["elements"])
          == std::vector<std::string>{"canvas", "image", "live", "next_id", "video", "web"});
    CHECK(keys(media["transitions"])
          == std::vector<std::string>{"image", "live", "video", "web"});

    // What is stored passes the gate unchanged.
    for (const auto kind : {ThemeKind::Song, ThemeKind::Bible, ThemeKind::Media}) {
        const auto once = filter_theme_style(kind, style);
        CHECK(filter_theme_style(kind, once) == once);
    }

    // Saving goes through it.
    OutputConfig oc;
    seed_default_themes(oc);
    const auto created = create_theme(oc, ThemeKind::Bible, "Scripture", style);
    REQUIRE(created.ok);
    CHECK(created.theme.style == filter_theme_style(ThemeKind::Bible, style));
    CHECK(find_theme(oc, ThemeKind::Bible, created.theme.id)->style == created.theme.style);
}

TEST_CASE("theme_library create/update/delete + last-theme guard", "[theme]")
{
    OutputConfig oc;
    seed_default_themes(oc);
    const auto base_song = oc.song_themes[0].id;

    auto created = create_theme(oc, ThemeKind::Song, "Alt",
                                {{"follow_lines", 2}, {"bogus", 1}});
    REQUIRE(created.ok);
    REQUIRE(created.theme.style.contains("follow_lines"));
    REQUIRE(created.theme.style.contains("elements"));
    REQUIRE_FALSE(created.theme.style.contains("bogus"));
    REQUIRE(oc.song_themes.size() == 2);
    REQUIRE(oc.bible_themes.size() == 1);
    REQUIRE_FALSE(created.theme.created_at.empty());

    auto updated = update_theme(oc,
                                ThemeKind::Song,
                                created.theme.id,
                                "Alt 2",
                                {{"follow_lines", 3}, {"align", "left"}},
                                "live, stage",
                                true);
    REQUIRE(updated.ok);
    REQUIRE(updated.theme.name == "Alt 2");
    REQUIRE(updated.theme.style.at("follow_lines") == 3);
    REQUIRE_FALSE(updated.theme.style.contains("align"));
    REQUIRE(updated.theme.tags.size() == 2);

    REQUIRE(set_category_default(oc, "song", "theme", created.theme.id).ok);
    REQUIRE(oc.category_defaults["song"]["theme"] == created.theme.id);
    REQUIRE_FALSE(set_category_default(oc, "song", "text", created.theme.id).ok);
    // A category's default is a theme of its own kind.
    const auto not_media = set_category_default(oc, "media", "theme", created.theme.id);
    REQUIRE_FALSE(not_media.ok);
    REQUIRE(oc.category_defaults["media"]["theme"] == oc.media_themes[0].id);

    // Freeform paints its own slides, so it has no category default; media
    // fills the screen over black, so it has no background.
    REQUIRE_FALSE(set_category_default(oc, "freeform", "bg", oc.bg_themes[0].id).ok);
    const auto media_bg = set_category_default(oc, "media", "bg", oc.bg_themes[0].id);
    REQUIRE_FALSE(media_bg.ok);
    REQUIRE_FALSE(media_bg.message.empty());
    REQUIRE_FALSE(oc.category_defaults["media"].contains("bg"));

    // The default that named it names the first song theme left; other
    // categories' defaults are another kind's and stay.
    const auto bible_default = oc.category_defaults["bible"]["theme"];
    auto del = delete_theme(oc, ThemeKind::Song, created.theme.id);
    REQUIRE(del.ok);
    REQUIRE(oc.song_themes.size() == 1);
    REQUIRE(oc.category_defaults["song"]["theme"] == base_song);
    REQUIRE(oc.category_defaults["bible"]["theme"] == bible_default);

    auto last = delete_theme(oc, ThemeKind::Song, base_song);
    REQUIRE_FALSE(last.ok);
    REQUIRE(last.message.find("last theme") != std::string::npos);
    // Each kind keeps its own last theme.
    REQUIRE_FALSE(delete_theme(oc, ThemeKind::Media, oc.media_themes[0].id).ok);
}

TEST_CASE("theme_library stamps created_at on create and duplicate", "[theme]")
{
    OutputConfig oc;
    seed_default_themes(oc);

    auto created = create_theme(oc, ThemeKind::Song, "Dated");
    REQUIRE(created.ok);
    const auto stamp = created.theme.created_at;
    REQUIRE(stamp.size() >= 19);
    REQUIRE(stamp[10] == 'T');

    auto dup = create_theme(oc, ThemeKind::Song, "", nlohmann::json::object(), nlohmann::json(),
                            created.theme.id);
    REQUIRE(dup.ok);
    REQUIRE_FALSE(dup.theme.created_at.empty());
}

TEST_CASE("theme_library filters keys and normalizes tags", "[theme]")
{
    // Geometry and typography belong to elements now, so a stray ``font_size``
    // is filtered out of a text theme the same way a background colour is.
    const auto text = filter_theme_style(
        ThemeKind::Song,
        {{"follow_lines", 2}, {"font_size", 1}, {"background_color", "#000"}});
    REQUIRE(text.contains("follow_lines"));
    REQUIRE_FALSE(text.contains("font_size"));
    REQUIRE_FALSE(text.contains("background_color"));

    const auto bg = filter_theme_style(
        ThemeKind::Bg,
        {{"follow_lines", 2}, {"background_color", "#000"}});
    REQUIRE(bg.contains("background_color"));
    REQUIRE_FALSE(bg.contains("follow_lines"));

    const auto tags = normalize_theme_tags("Live, live, STAGE, ");
    REQUIRE(tags.size() == 2);
    REQUIRE(tags[0] == "live");
    REQUIRE(tags[1] == "stage");
}

TEST_CASE("theme tags follow one rule for storage and display", "[theme]")
{
    // The same rule the tag chips show: collapsed spaces, lowercase.
    REQUIRE(normalize_theme_tags("  Lower   Third ,lower third")
            == std::vector<std::string>{"lower third"});
    REQUIRE(normalize_theme_tags(nlohmann::json::array({"A", "b", 3, "B"}))
            == std::vector<std::string>{"a", "b"});

    // A long tag is cut at a character boundary, never inside one.
    std::string long_tag(39, 'a');
    long_tag += "\xC3\xB3n";  // "o-acute" straddles byte 40
    const auto cut = normalize_theme_tags(nlohmann::json::array({long_tag}));
    REQUIRE(cut.size() == 1);
    REQUIRE(cut[0] == std::string(39, 'a'));

    // At most 24 tags are kept.
    nlohmann::json many = nlohmann::json::array();
    for (int i = 0; i < 30; ++i) {
        many.push_back("t" + std::to_string(i));
    }
    REQUIRE(normalize_theme_tags(many).size() == seventhslide::theme::k_max_theme_tags);
}

namespace {

/// Put bytes in ``theme``'s owned store for ``slot`` and point the slot at
/// them, the way the app finishes an upload once the copy is done.
std::filesystem::path give_owned_asset(seventhslide::model::ThemeEntry& theme,
                                       const char* slot,
                                       const std::filesystem::path& media_dir,
                                       const char* bytes)
{
    const auto name = new_owned_asset_name(theme.id, slot, ".png");
    std::filesystem::create_directories(theme_assets_dir(media_dir));
    {
        std::ofstream out(theme_assets_dir(media_dir) / name, std::ios::binary);
        out << bytes;
    }
    REQUIRE(set_owned_theme_asset(theme, slot, name).ok);
    return theme_assets_dir(media_dir) / name;
}

}  // namespace

TEST_CASE("theme_library points a slot at an owned asset, and pruning drops the old one",
          "[theme]")
{
    OutputConfig oc;
    seed_default_themes(oc);
    auto* theme = find_theme(oc, ThemeKind::Bg, oc.bg_themes[0].id);
    REQUIRE(theme != nullptr);

    const seventhslide::test::TempDir tmp_dir;
    const auto& tmp = tmp_dir.path;
    std::filesystem::create_directories(tmp);

    const auto owned = give_owned_asset(*theme, "bg", tmp, "PNG");
    const auto url = theme->style.at("background_image").get<std::string>();
    REQUIRE(url == "/static/theme_backgrounds/" + owned.filename().string());
    REQUIRE(theme->style.at("background_type") == "image");
    REQUIRE(std::filesystem::is_regular_file(owned));

    // The replacement leaves the old file, for a save that fails to go back to.
    const auto owned2 = give_owned_asset(*theme, "bg", tmp, "PNG2");
    REQUIRE(theme->style.at("background_image").get<std::string>() != url);
    REQUIRE(std::filesystem::is_regular_file(owned2));
    REQUIRE(std::filesystem::is_regular_file(owned));

    // Pruning another slot leaves it; pruning this one drops it.
    const auto video = give_owned_asset(*theme, "bg_video", tmp, "MP4");
    const auto video2 = give_owned_asset(*theme, "bg_video", tmp, "MP4-2");
    seventhslide::theme::prune_unused_theme_assets(*theme, tmp, "bg");
    REQUIRE(std::filesystem::is_regular_file(owned2));
    REQUIRE_FALSE(std::filesystem::exists(owned));
    REQUIRE(std::filesystem::is_regular_file(video));
    seventhslide::theme::prune_unused_theme_assets(*theme, tmp);
    REQUIRE_FALSE(std::filesystem::exists(video));
    REQUIRE(std::filesystem::is_regular_file(video2));
}

TEST_CASE("theme_library refuses a file the theme does not own", "[theme]")
{
    using seventhslide::theme::sync_theme_owned_assets;

    OutputConfig oc;
    seed_default_themes(oc);
    auto* theme = find_theme(oc, ThemeKind::Bg, oc.bg_themes[0].id);
    REQUIRE(theme != nullptr);

    const seventhslide::test::TempDir tmp_dir;
    const auto& tmp = tmp_dir.path;
    std::filesystem::create_directories(tmp);
    const auto owned = give_owned_asset(*theme, "bg", tmp, "PNG1");
    const auto url = theme->style.at("background_image").get<std::string>();

    // Not named for this theme and slot: refused; prior owned file + URL stay put.
    auto failed = set_owned_theme_asset(*theme, "bg", "stranger.png");
    REQUIRE_FALSE(failed.ok);
    REQUIRE(theme->style.at("background_image").get<std::string>() == url);
    REQUIRE(std::filesystem::is_regular_file(owned));
    REQUIRE_FALSE(set_owned_theme_asset(*theme, "nope", owned.filename().string()).ok);

    // Sync must not wipe an already-owned URL when nothing needs adopting.
    const auto synced = sync_theme_owned_assets(*theme, tmp);
    REQUIRE_FALSE(synced.changed);
    REQUIRE(synced.copies.empty());
    REQUIRE(std::filesystem::is_regular_file(owned));
}

TEST_CASE("theme_library sync gives a shared background its own file at once", "[theme]")
{
    using seventhslide::theme::sync_theme_owned_assets;

    OutputConfig oc;
    seed_default_themes(oc);
    auto created = create_theme(oc, ThemeKind::Bg, "Alt", {{"background_type", "color"}});
    REQUIRE(created.ok);
    auto* a = find_theme(oc, ThemeKind::Bg, oc.bg_themes[0].id);
    auto* b = find_theme(oc, ThemeKind::Bg, created.theme.id);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    const seventhslide::test::TempDir tmp_dir;
    const auto& tmp = tmp_dir.path;
    std::filesystem::create_directories(tmp);
    const auto a_file = give_owned_asset(*a, "bg", tmp, "POOL");
    const auto a_url = a->style.at("background_image").get<std::string>();

    // Theme B points at A's owned file (duplicate-style share).
    b->style["background_image"] = a_url;
    b->style["background_type"] = "image";
    const auto synced = sync_theme_owned_assets(*b, tmp);
    REQUIRE(synced.changed);
    // Linked, not queued for a copy.
    REQUIRE(synced.copies.empty());
    const auto b_url = b->style.at("background_image").get<std::string>();
    REQUIRE(b_url != a_url);
    const auto b_file = tmp / "theme_backgrounds" / std::filesystem::path(b_url).filename();
    REQUIRE(std::filesystem::is_regular_file(b_file));
    // One set of bytes under two names.
    REQUIRE(std::filesystem::hard_link_count(b_file) == 2);
    // A's source survives, and deleting A's files leaves B's intact.
    REQUIRE(std::filesystem::is_regular_file(a_file));
    seventhslide::theme::delete_theme_owned_files(a->id, tmp);
    REQUIRE_FALSE(std::filesystem::exists(a_file));
    std::ifstream in(b_file, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(bytes == "POOL");
}

#if !defined(_WIN32)
TEST_CASE("theme_library sync hands back a shared background it cannot link", "[theme]")
{
    using seventhslide::theme::sync_theme_owned_assets;
    if (::geteuid() == 0) {
        SKIP("root ignores directory permissions");
    }

    OutputConfig oc;
    seed_default_themes(oc);
    auto created = create_theme(oc, ThemeKind::Bg, "Alt", {{"background_type", "color"}});
    REQUIRE(created.ok);
    auto* a = find_theme(oc, ThemeKind::Bg, oc.bg_themes[0].id);
    auto* b = find_theme(oc, ThemeKind::Bg, created.theme.id);

    const seventhslide::test::TempDir tmp_dir;
    const auto& tmp = tmp_dir.path;
    std::filesystem::create_directories(tmp);
    const auto a_file = give_owned_asset(*a, "bg", tmp, "POOL");
    const auto a_url = a->style.at("background_image").get<std::string>();
    b->style["background_image"] = a_url;
    b->style["background_type"] = "image";

    // No new name can be made in the store: the link fails as it would on a
    // filesystem without hard links.
    const auto dir = theme_assets_dir(tmp);
    std::filesystem::permissions(dir, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::remove);
    const auto synced = sync_theme_owned_assets(*b, tmp);
    std::filesystem::permissions(dir, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    // The slot stays on the shared file until the caller's copy lands.
    REQUIRE(b->style.at("background_image").get<std::string>() == a_url);
    REQUIRE(synced.copies.size() == 1);
    REQUIRE(synced.copies.front().slot == "bg");
    REQUIRE(synced.copies.front().url == a_url);
    REQUIRE(synced.copies.front().source == a_file);
}
#endif
