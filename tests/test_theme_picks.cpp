#include <catch2/catch_test_macros.hpp>

#include <seventhslide/theme/theme_category.hpp>
#include <seventhslide/theme/theme_kind.hpp>
#include <seventhslide/theme/theme_picks.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>

using nlohmann::json;
using seventhslide::model::ContentKind;
using seventhslide::theme::CategorySet;
using seventhslide::theme::OutputThemeMap;
using seventhslide::theme::PickSlot;
using seventhslide::theme::ThemeCategory;
using seventhslide::theme::ThemeField;
using seventhslide::theme::ThemeNeeds;
using seventhslide::theme::ThemePick;
using seventhslide::theme::ThemePicks;

TEST_CASE("Theme fields and categories are spelled one way", "[theme][picks]")
{
    for (const auto field : {ThemeField::Theme, ThemeField::Bg}) {
        REQUIRE(seventhslide::theme::theme_field_from_string(
                    seventhslide::theme::to_string(field))
                == field);
    }
    REQUIRE(seventhslide::theme::to_string(ThemeField::Theme) == "theme");
    REQUIRE(seventhslide::theme::to_string(ThemeField::Bg) == "bg");
    REQUIRE_FALSE(seventhslide::theme::theme_field_from_string("text"));

    for (const auto category : seventhslide::theme::k_theme_categories) {
        REQUIRE(seventhslide::theme::theme_category_from_string(
                    seventhslide::theme::to_string(category))
                == category);
    }
    REQUIRE(seventhslide::theme::to_string(ThemeCategory::Media) == "media");
    REQUIRE_FALSE(seventhslide::theme::theme_category_from_string("freeform"));
}

TEST_CASE("Each kind of content takes its looks from one category", "[theme][picks]")
{
    using seventhslide::theme::theme_needs;
    REQUIRE(theme_needs(ContentKind::Song) == ThemeNeeds{ThemeCategory::Song, ThemeCategory::Song});
    REQUIRE(theme_needs(ContentKind::Bible)
            == ThemeNeeds{ThemeCategory::Bible, ThemeCategory::Bible});
    for (const auto media : {ContentKind::Image, ContentKind::ImageFolder, ContentKind::Video,
                             ContentKind::Audio, ContentKind::Web, ContentKind::Live}) {
        REQUIRE(theme_needs(media) == ThemeNeeds{ThemeCategory::Media, std::nullopt});
    }
    // A divider shows the songs' background between them; Freeform brings its own.
    REQUIRE(theme_needs(ContentKind::Divider) == ThemeNeeds{std::nullopt, ThemeCategory::Song});
    REQUIRE(theme_needs(ContentKind::Freeform) == ThemeNeeds{});
    REQUIRE(seventhslide::theme::categories_for(ContentKind::Freeform).empty());
    REQUIRE_FALSE(seventhslide::theme::takes_background(ThemeCategory::Media));
}

TEST_CASE("Theme picks read only the categories and ids they may hold", "[theme][picks]")
{
    const json raw = {{"song", {{"theme", " plain "}, {"bg", "   "}, {"x", "y"}}},
                      {"bible", {{"theme", 9}, {"bg", nullptr}}},
                      {"media", {{"theme", "m1"}, {"bg", "never"}}},
                      {"freeform", {{"theme", "f1"}}}};
    const auto picks = ThemePicks::from_json(raw, CategorySet::all());
    REQUIRE(picks.of(ThemeCategory::Song) == ThemePick{.theme = "plain", .bg = ""});
    REQUIRE(picks.of(ThemeCategory::Bible).empty());
    // Media takes no background, whatever is stored.
    REQUIRE(picks.of(ThemeCategory::Media) == ThemePick{.theme = "m1", .bg = ""});
    REQUIRE(picks.to_json()
            == json{{"song", {{"theme", "plain"}}}, {"media", {{"theme", "m1"}}}});
    REQUIRE(ThemePicks::from_json(picks.to_json(), CategorySet::all()) == picks);

    const auto songs_only = ThemePicks::from_json(raw, CategorySet{}.add(ThemeCategory::Song));
    REQUIRE(songs_only.to_json() == json{{"song", {{"theme", "plain"}}}});
    REQUIRE(ThemePicks::from_json("t1", CategorySet::all()).empty());
}

TEST_CASE("A category's background can be set only where it takes one", "[theme][picks]")
{
    ThemePicks picks;
    REQUIRE(picks.set(ThemeCategory::Song, ThemeField::Bg, "b1"));
    REQUIRE_FALSE(picks.set(ThemeCategory::Media, ThemeField::Bg, "b1"));
    REQUIRE(picks.of(ThemeCategory::Media).empty());
    REQUIRE(picks.set(ThemeCategory::Song, ThemeField::Bg, ""));
    REQUIRE(picks.empty());
}

TEST_CASE("An output theme map keeps only the themes it names", "[theme][picks]")
{
    const json raw = {{"Main", {{"song", {{"theme", " plain "}, {"bg", "   "}}}}},
                      {"Lower", {{"bible", {{"bg", "dark"}}}}},
                      {"Empty", json::object()},
                      {"Junk", "not an object"}};
    const auto map = seventhslide::theme::service_themes_from_json(raw);
    REQUIRE(map.to_json()
            == json{{"Main", {{"song", {{"theme", "plain"}}}}},
                    {"Lower", {{"bible", {{"bg", "dark"}}}}}});
    REQUIRE(OutputThemeMap::from_json(json::array(), CategorySet::all()).empty());
    // An output whose picks are all for categories the map may not hold has none.
    REQUIRE(OutputThemeMap::from_json(raw, CategorySet{}.add(ThemeCategory::Media)).empty());

    // What is stored reads back as itself.
    REQUIRE(seventhslide::theme::service_themes_from_json(map.to_json()) == map);
}

TEST_CASE("An output theme map picks per output", "[theme][picks]")
{
    const auto map = seventhslide::theme::service_themes_from_json(
        {{"out-main", {{"song", {{"theme", "t1"}, {"bg", "b1"}}}}},
         {"out-hall", {{"song", {{"bg", "b2"}}}}}});
    REQUIRE(map.picks("out-main").of(ThemeCategory::Song) == ThemePick{.theme = "t1", .bg = "b1"});
    REQUIRE(map.picks("out-hall").of(ThemeCategory::Song) == ThemePick{.theme = "", .bg = "b2"});
    REQUIRE(map.picks("out-lobby").empty());
    // An output with no id is never anyone's.
    REQUIRE(map.picks("").empty());
}

TEST_CASE("A tier's choices are each category's theme and background", "[theme][picks]")
{
    using seventhslide::theme::pick_slots;
    REQUIRE(pick_slots(CategorySet::all())
            == std::vector<PickSlot>{{ThemeCategory::Song, ThemeField::Theme},
                                     {ThemeCategory::Song, ThemeField::Bg},
                                     {ThemeCategory::Bible, ThemeField::Theme},
                                     {ThemeCategory::Bible, ThemeField::Bg},
                                     {ThemeCategory::Media, ThemeField::Theme}});
    REQUIRE(pick_slots(ContentKind::Song)
            == std::vector<PickSlot>{{ThemeCategory::Song, ThemeField::Theme},
                                     {ThemeCategory::Song, ThemeField::Bg}});
    REQUIRE(pick_slots(ContentKind::Video)
            == std::vector<PickSlot>{{ThemeCategory::Media, ThemeField::Theme}});
    REQUIRE(pick_slots(ContentKind::Divider)
            == std::vector<PickSlot>{{ThemeCategory::Song, ThemeField::Bg}});
    REQUIRE(pick_slots(ContentKind::Freeform).empty());
}

TEST_CASE("Every design context is one kind of theme's", "[theme][kinds]")
{
    using seventhslide::theme::ElementContext;
    using seventhslide::theme::ThemeKind;
    for (const auto ctx : seventhslide::theme::k_element_contexts) {
        const auto kind = seventhslide::theme::kind_of(ctx);
        const auto contexts = seventhslide::theme::contexts_of(kind);
        CHECK(std::ranges::find(contexts, ctx) != contexts.end());
    }
    CHECK(seventhslide::theme::contexts_of(ThemeKind::Bg).empty());
    CHECK(seventhslide::theme::primary_context(ThemeKind::Song) == ElementContext::Song);
    CHECK(seventhslide::theme::primary_context(ThemeKind::Bible) == ElementContext::Bible);
    CHECK(seventhslide::theme::primary_context(ThemeKind::Media) == ElementContext::Image);

    for (const auto category : seventhslide::theme::k_theme_categories) {
        CHECK(seventhslide::theme::category_of(seventhslide::theme::text_kind_of(category))
              == category);
    }
    CHECK_FALSE(seventhslide::theme::category_of(ThemeKind::Bg));
    for (const auto kind : seventhslide::theme::k_theme_kinds) {
        CHECK(seventhslide::theme::parse_theme_kind(seventhslide::theme::to_string(kind)) == kind);
    }
    CHECK_FALSE(seventhslide::theme::parse_theme_kind("text"));
}
