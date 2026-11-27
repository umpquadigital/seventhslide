#include <catch2/catch_test_macros.hpp>

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/scene_frame.hpp>
#include <seventhslide/session/stack_runtime.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

using seventhslide::model::Author;
using seventhslide::model::BiblePassage;
using seventhslide::model::PassageVerse;
using seventhslide::model::ServiceItem;
using seventhslide::model::Song;
using seventhslide::model::StyleProfile;
using seventhslide::model::k_item_type_bible;
using seventhslide::model::k_item_type_song;
using seventhslide::model::k_item_type_video;

namespace {

[[nodiscard]] Song sample_song()
{
    Song s;
    s.title = "Holy Holy";
    s.lyrics = "---[Verse:1]---\nLine\n";
    s.verse_order = "v1";
    s.authors = {"A", "B"};
    s.songbook_name = "SDA";
    s.songbook_entry = "1";
    s.theme_map = {{"Main", {{"song", {{"theme", "tid"}}}}}};
    s.copyright = "©";
    s.ccli_song_number = "123";
    s.show_copyright = true;
    s.key = "G";
    s.topics = {"Christmas", "Second Coming"};
    s.comments = "Slow the last chorus.";
    return s;
}

} // namespace

TEST_CASE("Song JSON holds the song's content, not its row id", "[model][content]")
{
    auto s = sample_song();
    s.id = 42;
    const auto j = s.to_json();
    REQUIRE_FALSE(j.contains("id"));

    auto again = Song::from_json(j);
    REQUIRE_FALSE(again.id.has_value());
    again.id = s.id;
    REQUIRE(again == s);
}

TEST_CASE("Song topics keep their spelling, without repeats", "[model][content]")
{
    auto j = sample_song().to_json();
    j["topics"] = nlohmann::json::array({"  Second   Coming ", "christmas", "", "Christmas", 7});
    REQUIRE(Song::from_json(j).topics == std::vector<std::string>{"Second Coming", "christmas"});

    std::vector<std::string> many;
    for (int i = 0; i < 30; ++i) {
        many.push_back("Topic " + std::to_string(i));
    }
    REQUIRE(seventhslide::model::normalize_song_topics(many).size()
            == seventhslide::model::k_max_song_topics);

    // A song stored before topics existed reads as having none.
    j.erase("topics");
    j.erase("comments");
    const auto old = Song::from_json(j);
    REQUIRE(old.topics.empty());
    REQUIRE(old.comments.empty());
}

TEST_CASE("Song authors read name and role objects", "[model][content]")
{
    const nlohmann::json j{
        {"title", "T"},
        {"authors", nlohmann::json::array({
            {{"name", "Ada"}, {"roles", {{{"type", "words"}}}}},
            {{"name", "Charles"}, {"roles", {{{"type", "music"}}}}},
        })},
    };
    const auto s = Song::from_json(j);
    REQUIRE(s.authors.size() == 2);
    REQUIRE(s.authors[0].name == "Ada");
    REQUIRE(s.authors[0].roles.at(0).type == seventhslide::model::k_author_role_words);
    REQUIRE(s.authors[1].name == "Charles");
    REQUIRE(s.authors[1].roles.at(0).type == seventhslide::model::k_author_role_music);
    REQUIRE(Song::from_json(s.to_json()).authors == s.authors);
}

TEST_CASE("A song row plays exactly the song it was snapshotted from", "[model][content]")
{
    auto song = sample_song();
    song.id = 5;

    ServiceItem it;
    it.item_id = 10;
    it.song_id = 5;
    it.item_type = std::string{k_item_type_song};
    it.data = seventhslide::db::song_to_service_snapshot(song, false);
    REQUIRE(it.song() == song);
    REQUIRE(it.title() == "Holy Holy");
    REQUIRE(it.theme_map() == song.theme_map);
    REQUIRE_FALSE(it.has_overrides());  // user_modified false

    it.data["user_modified"] = true;
    REQUIRE(it.has_overrides());
    // A snapshot from before the flag existed counts as edited.
    it.data.erase("user_modified");
    REQUIRE(it.has_overrides());

    // What the row shows follows the stored data, with nothing to refresh.
    it.data["title"] = "Renamed";
    REQUIRE(it.title() == "Renamed");
    REQUIRE(it.song().title == "Renamed");
}

TEST_CASE("ServiceItem titles per kind", "[model][content]")
{
    ServiceItem bible;
    bible.item_type = std::string{k_item_type_bible};
    bible.data = {{"ref", "John 3:16"}, {"bible_id", 1}, {"book", "John"},
                  {"chapter", 3}, {"verse_start", 16}, {"verse_end", 16}};
    REQUIRE(bible.title() == "John 3:16");
    REQUIRE_FALSE(bible.has_overrides());
    bible.data["theme_map"] = {{"Main", {{"bible", {{"theme", "t"}}}}}};
    REQUIRE(bible.has_overrides());

    ServiceItem video;
    video.item_type = std::string{k_item_type_video};
    video.data = {{"filename", "clip.mp4"}};
    REQUIRE(video.title() == "clip.mp4");

    ServiceItem divider;
    divider.item_type = std::string{seventhslide::model::k_item_type_divider};
    REQUIRE(divider.title() == "Section");

    // A song row with no title falls back to its type, never to nothing.
    ServiceItem untitled;
    untitled.item_type = std::string{k_item_type_song};
    REQUIRE(untitled.title() == "song");
}

TEST_CASE("ServiceItem freeform title strips markup", "[model][content]")
{
    const auto freeform = [](nlohmann::json data) {
        ServiceItem item;
        item.item_type = std::string{seventhslide::model::k_item_type_freeform};
        item.data = std::move(data);
        return item;
    };
    REQUIRE(freeform({{"name", "<b>Welcome</b>  "}}).title() == "Welcome");
    REQUIRE(freeform({{"name", "<b></b>"}}).title() == "Freeform");
    // Angle brackets that are not a tag are part of the name.
    REQUIRE(freeform({{"name", "Kids <3 VBS"}}).title() == "Kids <3 VBS");
    REQUIRE(freeform({{"name", "Cue card"}}).title() == "Cue card");

    // A Freeform row's look lives in its document, not in a theme map.
    const auto themed = freeform({{"name", "X"}, {"theme_map", {{"Main", {{"song", {{"bg", "b"}}}}}}}});
    REQUIRE(themed.theme_map().empty());
    REQUIRE_FALSE(themed.has_overrides());
}

TEST_CASE("BiblePassage JSON round-trips its verses", "[model][content]")
{
    BiblePassage p;
    p.bible_id = 2;
    p.book = "Romans";
    p.chapter = 8;
    p.verse_start = 28;
    p.verse_end = 29;
    p.ref = "Romans 8:28-29";
    p.version = "ESV";
    p.verses = {{28, "And we know"}, {29, "For whom"}};
    const auto j = p.to_json();
    REQUIRE(j.at("verses").at(1) == nlohmann::json{{"verse_num", 29}, {"text", "For whom"}});

    const auto again = BiblePassage::from_json(j);
    REQUIRE(again.ref == "Romans 8:28-29");
    REQUIRE(again.version == "ESV");
    REQUIRE(again.verses == p.verses);
    REQUIRE(*again.bible_id == 2);
}

// Stored passages are read tolerantly: a wrong type reads as absent instead of
// throwing out of whatever loaded it.
TEST_CASE("BiblePassage reads malformed verse rows without throwing", "[model][json]")
{
    const nlohmann::json j = {
        {"chapter", "three"},
        {"verses", {{{"verse_num", "16"}, {"text", 5}}, "loose", {{"verse_num", 17}}}},
    };
    BiblePassage p;
    REQUIRE_NOTHROW(p = BiblePassage::from_json(j));
    REQUIRE(p.chapter == 0);
    REQUIRE(p.verses == std::vector<PassageVerse>{{0, ""}, {0, ""}, {17, ""}});
}

TEST_CASE("StyleProfile starts with outputs and songs", "[model][content]")
{
    const auto data = StyleProfile::empty_data();
    REQUIRE(data.at("outputs").empty());
    REQUIRE(data.at("songs").empty());
    REQUIRE_FALSE(data.contains("ann_items"));
}

TEST_CASE("service item also_play helpers round-trip ids", "[model][content]")
{
    using seventhslide::model::service_item_also_play;
    using seventhslide::model::set_service_item_also_play;

    ServiceItem item;
    item.item_type = "song";
    item.data = {{"title", "Hymn"}};
    REQUIRE(service_item_also_play(item).empty());
    REQUIRE(item.title() == "Hymn");

    set_service_item_also_play(item, {12, 12, -1, 15});
    REQUIRE(service_item_also_play(item) == std::vector<std::int64_t>{12, 15});

    set_service_item_also_play(item, {});
    REQUIRE(service_item_also_play(item).empty());
    REQUIRE_FALSE(item.data.contains("also_play"));

    item.data["also_play"] = 7;
    REQUIRE(service_item_also_play(item) == std::vector<std::int64_t>{7});
}

TEST_CASE("ContentKind is the one content vocabulary", "[model][content]")
{
    using seventhslide::model::ContentKind;
    using seventhslide::model::content_kind_from_string;
    using seventhslide::model::to_string;
    using seventhslide::scene::SceneMode;
    using seventhslide::scene::scene_mode_for;
    using seventhslide::build::BuildKind;
    using seventhslide::session::SessionMode;
    using seventhslide::build::build_kind_for;
    using seventhslide::session::session_mode_for;

    for (const auto kind : {ContentKind::Song, ContentKind::Bible, ContentKind::Freeform,
                            ContentKind::Image, ContentKind::ImageFolder, ContentKind::Video,
                            ContentKind::Divider}) {
        REQUIRE(content_kind_from_string(to_string(kind)) == kind);
    }
    REQUIRE(to_string(ContentKind::Empty).empty());
    REQUIRE(content_kind_from_string("nope") == ContentKind::Empty);

    REQUIRE(scene_mode_for(ContentKind::ImageFolder) == SceneMode::Image);
    REQUIRE(scene_mode_for(ContentKind::Divider) == SceneMode::Empty);
    REQUIRE(scene_mode_for(ContentKind::Video) == SceneMode::Video);
    REQUIRE(build_kind_for(ContentKind::Freeform) == BuildKind::Freeform);
    REQUIRE(build_kind_for(ContentKind::Image) == BuildKind::Empty);
    REQUIRE(build_kind_for(ContentKind::Video) == BuildKind::Empty);

    REQUIRE(session_mode_for(ContentKind::Song, false) == SessionMode::Song);
    REQUIRE(session_mode_for(ContentKind::ImageFolder, false) == SessionMode::Image);
    REQUIRE(session_mode_for(ContentKind::Divider, false) == SessionMode::Empty);
    REQUIRE(session_mode_for(ContentKind::Bible, true) == SessionMode::Service);

    ServiceItem item;
    item.item_type = "image_folder";
    REQUIRE(item.kind() == ContentKind::ImageFolder);
}

