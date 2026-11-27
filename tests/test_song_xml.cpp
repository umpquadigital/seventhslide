#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"

#include <seventhslide/parse/song_xml.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using seventhslide::model::Song;
using seventhslide::parse::SongXmlError;
using seventhslide::parse::parse_song_file;
using seventhslide::parse::parse_song_xml;

namespace {

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("songs");
}

using seventhslide::test::load_json;

nlohmann::json song_to_cmp(const Song& s)
{
    return nlohmann::json{
        {"title", s.title},
        {"lyrics", s.lyrics},
        {"verse_order", s.verse_order.empty() ? nlohmann::json(nullptr)
                                              : nlohmann::json(s.verse_order)},
        {"authors", seventhslide::model::authors_to_json(s.authors)},
        {"songbook_name", s.songbook_name},
        {"songbook_entry", s.songbook_entry},
        {"key", s.key},
        {"copyright", s.copyright},
        {"ccli_song_number", s.ccli_song_number},
    };
}

void require_matches_golden(const std::string& stem)
{
    const auto dir = fixtures_dir();
    const auto xml = dir / (stem + ".xml");
    const auto golden = dir / (stem + ".expected.json");
    REQUIRE(std::filesystem::exists(xml));
    REQUIRE(std::filesystem::exists(golden));

    const auto songs = parse_song_file(xml);
    const auto expected = load_json(golden);
    REQUIRE(expected.is_array());
    REQUIRE(songs.size() == expected.size());
    for (std::size_t i = 0; i < songs.size(); ++i) {
        REQUIRE(song_to_cmp(songs[i]) == expected[i]);
    }
}

} // namespace

TEST_CASE("parse_song_file OpenLyrics basic matches golden", "[parse][song_xml]")
{
    require_matches_golden("openlyrics_basic");
}

TEST_CASE("parse_song_file OpenSong-shaped matches golden", "[parse][song_xml]")
{
    require_matches_golden("opensong_shaped");
}

TEST_CASE("parse_song_file multi-song container matches golden", "[parse][song_xml]")
{
    require_matches_golden("openlyrics_multi");
}

TEST_CASE("parse_song_file flat lines matches golden", "[parse][song_xml]")
{
    require_matches_golden("openlyrics_flat_lines");
}

TEST_CASE("parse_song_file non-song fallback matches golden", "[parse][song_xml]")
{
    require_matches_golden("fallback_lines_only");
}

TEST_CASE("parse_song_file chords and markup (local-name strip)", "[parse][song_xml]")
{
    const auto xml = fixtures_dir() / "openlyrics_chords_markup.xml";
    REQUIRE(std::filesystem::exists(xml));
    const auto songs = parse_song_file(xml);
    REQUIRE(songs.size() == 1);
    REQUIRE(songs[0].title == "Chorded Song");
    const std::string expect =
        "---[Verse:1]---\n"
        "[G]Amazing <b>grace</b> [C]how <size=80>sweet</size>\n"
        "the <i>sound</i>\n"
        "\n"
        "---[Other:1]---\n"
        "Other block\n"
        "\n"
        "---[Misc]---\n"
        "Misc block";
    REQUIRE(songs[0].lyrics == expect);
}

TEST_CASE("parse_song_xml merges OpenLyrics verse parts", "[parse][song_xml]")
{
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<song>
  <properties><titles><title>Parts</title></titles></properties>
  <lyrics>
    <verse name="v1a"><lines>A</lines></verse>
    <verse name="v1b"><lines>B</lines></verse>
    <verse name="c1"><lines>C</lines></verse>
  </lyrics>
</song>
)";
    const auto songs = parse_song_xml(xml);
    REQUIRE(songs.size() == 1);
    REQUIRE(songs[0].title == "Parts");
    REQUIRE(songs[0].lyrics.find("---[Verse:1]---") != std::string::npos);
    REQUIRE(songs[0].lyrics.find("---[Verse:1a]---") == std::string::npos);
    REQUIRE(songs[0].lyrics.find("[---][---]") != std::string::npos);
    REQUIRE(songs[0].lyrics.find("---[Chorus:1]---") != std::string::npos);
}

TEST_CASE("parse_song_xml keeps OpenLyrics author types", "[parse][song_xml]")
{
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<song>
  <properties>
    <titles><title>Credited</title></titles>
    <authors>
      <author type="words">Ada</author>
      <author type="music">Ada</author>
      <author type="translation" lang="es">Bo</author>
      <author type="arrangement">Cy</author>
      <author type="paraphrase">Di</author>
      <author>Ed</author>
    </authors>
  </properties>
  <lyrics><verse name="v1"><lines>L</lines></verse></lyrics>
</song>
)";
    const auto songs = parse_song_xml(xml);
    REQUIRE(songs.size() == 1);
    const auto& authors = songs[0].authors;
    // Ada is credited twice in the file - one author, both roles.
    REQUIRE(authors.size() == 5);
    REQUIRE(authors[0].name == "Ada");
    REQUIRE(authors[0].roles.size() == 2);
    REQUIRE(authors[0].roles[0].type == seventhslide::model::k_author_role_words);
    REQUIRE(authors[0].roles[1].type == seventhslide::model::k_author_role_music);
    REQUIRE(authors[1].name == "Bo");
    REQUIRE(authors[1].roles[0].type == seventhslide::model::k_author_role_translation);
    REQUIRE(authors[1].roles[0].lang == "es");
    REQUIRE(authors[2].roles.at(0).type == seventhslide::model::k_author_role_arrangement);
    // A type this app has no slug for survives as its own wording.
    REQUIRE(authors[3].roles[0].type == seventhslide::model::k_author_role_other);
    REQUIRE(authors[3].roles[0].label == "paraphrase");
    REQUIRE(authors[4].name == "Ed");
    REQUIRE(authors[4].roles.empty());
}

TEST_CASE("parse_song_file rejects DOCTYPE", "[parse][song_xml]")
{
    const auto xml = fixtures_dir() / "doctype_rejected.xml";
    REQUIRE(std::filesystem::exists(xml));
    REQUIRE_THROWS_AS(parse_song_file(xml), SongXmlError);
    try {
        (void)parse_song_file(xml);
        FAIL("expected throw");
    } catch (const SongXmlError& e) {
        REQUIRE(std::string(e.what()).find("DOCTYPE") != std::string::npos);
    }
}

TEST_CASE("parse_song_file missing file throws", "[parse][song_xml]")
{
    REQUIRE_THROWS_AS(
        parse_song_file(fixtures_dir() / "no_such_file.xml"), SongXmlError);
}

TEST_CASE("parse_song_xml turns chord elements into inline markers", "[parse][song_xml]")
{
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<song xmlns="http://openlyrics.info/namespace/2009/song">
  <properties><titles><title>Chords</title></titles></properties>
  <lyrics>
    <verse name="v1"><lines><chord name="G"/>Amazing <CHORD name="C">gr</CHORD>ace<br/><x:chord xmlns:x="urn:x" name="D7"/>how <b><chord name="Em"/>sweet</b></lines></verse>
  </lyrics>
</song>)";
    const auto songs = seventhslide::parse::parse_song_xml(xml, "fallback");
    REQUIRE(songs.size() == 1);
    const auto& lyrics = songs[0].lyrics;
    CHECK(lyrics.find("[G]Amazing [C]grace") != std::string::npos);
    CHECK(lyrics.find("[D7]how <b>[Em]sweet</b>") != std::string::npos);
    CHECK(lyrics.find("chord") == std::string::npos);
    CHECK(lyrics.find("CHORD") == std::string::npos);
}

TEST_CASE("write_song_xml round trips a song through parse_song_xml", "[parse][song_xml]")
{
    using seventhslide::model::AuthorRole;
    Song song;
    song.title = "Grace & Peace";
    song.lyrics =
        "---[Verse:1]---\n"
        "[G]Amazing <b>grace</b>, how <size=80>sweet</size> the [C]sound\n"
        "That saved a <i><u>wretch</u></i> like me\n"
        "[---][---]\n"
        "Lost & found <color=#ff0000>red</color> <font=Georgia>set</font> x<super>2</super>\n"
        "\n"
        "---[Chorus:1]---\n"
        "Chorus line\n"
        "\n"
        "---[Bridge:2]---\n"
        "<b>[D]All bold</b>";
    song.verse_order = "v1 c1 th1 b2 c1";
    song.authors = {
        {"Ada", {AuthorRole{"words", {}, {}}, AuthorRole{"music", {}, {}}}},
        {"Bo", {AuthorRole{"translation", {}, "es"}}},
        {"Cy", {AuthorRole{"other", "paraphrase", {}}}},
        {"Ed"},
    };
    song.copyright = "Public Domain";
    song.ccli_song_number = "22025";
    song.songbook_name = "Hymnal";
    song.songbook_entry = "45";
    song.key = "G";
    song.topics = {"Grace", "Hope"};
    song.comments = "Line one\nLine two";
    song.show_copyright = true;

    const auto xml = seventhslide::parse::write_song_xml(song, "SeventhSlide test");
    INFO(xml);
    REQUIRE(xml.find("xmlns=\"http://openlyrics.info/namespace/2009/song\"") != std::string::npos);
    REQUIRE(xml.find("createdIn=\"SeventhSlide test\"") != std::string::npos);
    // Other programs see verses, chords and formatting tags the file defines.
    REQUIRE(xml.find("<verse name=\"v1a\">") != std::string::npos);
    REQUIRE(xml.find("<verse name=\"v1b\">") != std::string::npos);
    REQUIRE(xml.find("<chord name=\"G\" />") != std::string::npos);
    REQUIRE(xml.find("<tag name=\"b\">grace</tag>") != std::string::npos);
    REQUIRE(xml.find("<open>&lt;b&gt;</open>") != std::string::npos);

    const auto songs = parse_song_xml(xml);
    REQUIRE(songs.size() == 1);
    const auto& back = songs[0];
    CHECK(back.title == song.title);
    CHECK(back.lyrics == song.lyrics);
    // A theme chip names this library's themes, which stay here.
    CHECK(back.verse_order == "v1 c1 b2 c1");
    // OpenLyrics 0.8 has no wording of its own for a role: Cy is credited
    // plainly.
    auto authors = song.authors;
    authors[2].roles.clear();
    CHECK(back.authors == authors);
    CHECK(back.copyright == song.copyright);
    CHECK(back.ccli_song_number == song.ccli_song_number);
    CHECK(back.songbook_name == song.songbook_name);
    CHECK(back.songbook_entry == song.songbook_entry);
    CHECK(back.key == song.key);
    CHECK(back.topics == song.topics);
    CHECK(back.comments == song.comments);
}

TEST_CASE("write_song_xml writes a song without headers as verses", "[parse][song_xml]")
{
    Song song;
    song.title = "Plain";
    song.lyrics = "One\nTwo\n\nThree";
    const auto songs = parse_song_xml(seventhslide::parse::write_song_xml(song));
    REQUIRE(songs.size() == 1);
    CHECK(songs[0].lyrics == "---[Verse:1]---\nOne\nTwo\n\n---[Verse:2]---\nThree");
}

TEST_CASE("parse_song_xml reads formatting a file's <format> defines", "[parse][song_xml]")
{
    constexpr std::string_view xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<song xmlns="http://openlyrics.info/namespace/2009/song" version="0.8">
  <properties>
    <titles><title>Formatted</title></titles>
    <verseOrder>v1 x9</verseOrder>
  </properties>
  <format>
    <tags application="OpenLP 2">
      <tag name="st"><open>&lt;strong&gt;</open><close>&lt;/strong&gt;</close></tag>
      <tag name="r"><open>&lt;span style="-webkit-text-fill-color:red"&gt;</open><close>&lt;/span&gt;</close></tag>
    </tags>
  </format>
  <lyrics>
    <verse name="v1"><lines><tag name="st">Bold</tag> and <tag name="r">red</tag> and <tag name="zz">plain</tag></lines></verse>
  </lyrics>
</song>
)";
    const auto songs = parse_song_xml(xml);
    REQUIRE(songs.size() == 1);
    // Formatting this app can show is kept; markup it cannot (or a tag the
    // file never defines) leaves the words plain.
    CHECK(songs[0].lyrics == "---[Verse:1]---\n<b>Bold</b> and red and plain");
    // An order naming a verse the song does not have is dropped.
    CHECK(songs[0].verse_order.empty());
}
