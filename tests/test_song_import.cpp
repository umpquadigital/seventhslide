#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/model/author.hpp>
#include <seventhslide/parse/song_import.hpp>
#include <seventhslide/parse/song_sections.hpp>
#include <seventhslide/parse/song_xml.hpp>
#include <seventhslide/parse/xml_safe.hpp>
#include <seventhslide/util/text_encoding.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <winioctl.h>
#endif

using seventhslide::model::Author;
using seventhslide::model::AuthorRole;
using seventhslide::model::Song;
using seventhslide::parse::ImportedSong;
using seventhslide::parse::SectionHeading;
using seventhslide::parse::SongFile;
using seventhslide::parse::SongFormat;
using seventhslide::parse::heading_label;
using seventhslide::parse::read_song_bytes;
using seventhslide::parse::read_song_file;

namespace {

std::filesystem::path import_fixture(std::string_view name)
{
    return seventhslide::test::fixtures_dir("songs") / "import" / std::string(name);
}

/// The one song in fixture ``name``, read as ``format``.
ImportedSong read_one(std::string_view name, SongFormat format)
{
    const SongFile file = read_song_file(import_fixture(name));
    INFO(file.error);
    REQUIRE(file.error.empty());
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].format == format);
    return file.songs[0];
}

std::vector<std::string> author_names(const Song& song)
{
    std::vector<std::string> out;
    for (const auto& author : song.authors) {
        out.push_back(author.name);
    }
    return out;
}

}  // namespace

// --- reading text ---------------------------------------------------------

TEST_CASE("decode_text reads byte order marks, UTF-8 and Windows-1252", "[util][text_encoding]")
{
    using seventhslide::util::decode_text;
    CHECK(decode_text("plain") == "plain");
    CHECK(decode_text("\xEF\xBB\xBFwith BOM") == "with BOM");
    CHECK(decode_text("caf\xC3\xA9") == "caf\xC3\xA9");
    CHECK(decode_text(std::string_view("\xFF\xFEH\0i\0", 6)) == "Hi");
    CHECK(decode_text(std::string_view("\xFE\xFF\0H\0i", 6)) == "Hi");
    // UTF-16 beyond the BMP: U+1F600 as a surrogate pair.
    CHECK(decode_text(std::string_view("\xFF\xFE\x3D\xD8\x00\xDE", 6)) == "\xF0\x9F\x98\x80");
    // Not UTF-8: the curly quotes and apostrophe of Windows-1252.
    CHECK(decode_text("\x93Hi\x94 it\x92s") == "\xE2\x80\x9CHi\xE2\x80\x9D it\xE2\x80\x99s");
    CHECK(decode_text("caf\xE9") == "caf\xC3\xA9");
    CHECK_FALSE(seventhslide::util::is_valid_utf8("\xC0\xAF"));  // overlong "/"
    CHECK_FALSE(seventhslide::util::is_valid_utf8("\xED\xA0\x80"));  // a surrogate
}

// --- what every text format shares ---------------------------------------

TEST_CASE("heading_label reads section headings and only those", "[parse][song_import]")
{
    CHECK(heading_label("Verse 1") == SectionHeading{"Verse", 1});
    CHECK(heading_label("  CHORUS  ") == SectionHeading{"Chorus", 0});
    CHECK(heading_label("Pre-Chorus 2:") == SectionHeading{"Pre-Chorus", 2});
    CHECK(heading_label("Pre Chorus") == SectionHeading{"Pre-Chorus", 0});
    CHECK(heading_label("[Bridge]") == SectionHeading{"Bridge", 0});
    CHECK(heading_label("(PRE-CHORUS)") == SectionHeading{"Pre-Chorus", 0});
    CHECK(heading_label("Refrain") == SectionHeading{"Chorus", 0});
    CHECK(heading_label("Vers 3") == SectionHeading{"Verse", 3});
    CHECK(heading_label("Coro") == SectionHeading{"Chorus", 0});
    CHECK(heading_label("Outro") == SectionHeading{"Ending", 0});
    CHECK(heading_label("Tag") == SectionHeading{"Other", 0});
    CHECK(heading_label("Misc 1") == SectionHeading{"Other", 1});
    CHECK(heading_label("Chorus x2") == SectionHeading{"Chorus", 0});
    CHECK(heading_label("Verse1") == SectionHeading{"Verse", 1});
    CHECK_FALSE(heading_label("Amazing grace how sweet the sound"));
    CHECK_FALSE(heading_label("Chorus of angels"));
    CHECK_FALSE(heading_label("G"));
    CHECK_FALSE(heading_label(""));
}

TEST_CASE("chord lines are told from words and set into them", "[parse][song_import]")
{
    using seventhslide::parse::chords_only;
    using seventhslide::parse::chords_over_words;
    using seventhslide::parse::is_chord_line;
    CHECK(is_chord_line("G   C   D7"));
    CHECK(is_chord_line("  Ebmaj7  C#m7/G#  Gsus4  N.C.  "));
    CHECK(is_chord_line("| G | Am |"));
    CHECK(is_chord_line("Bb  F/A  Gm7  E\xE2\x99\xAD"));
    CHECK_FALSE(is_chord_line("Amazing grace"));
    CHECK_FALSE(is_chord_line("A band of angels"));
    CHECK_FALSE(is_chord_line("Be"));
    CHECK_FALSE(is_chord_line("|  |"));

    CHECK(chords_over_words("D       G  D", "Be Thou my Vision") == "[D]Be Thou [G]my [D]Vision");
    // Columns count characters, not bytes.
    CHECK(chords_over_words("     C", "Caf\xC3\xA9 au lait") == "Caf\xC3\xA9 [C]au lait");
    CHECK(chords_over_words("G           D", "home") == "[G]home [D]");
    // Words that would read as formatting stay words.
    const auto escaped = chords_over_words("G", "a <b> c");
    CHECK(escaped.starts_with("[G]a "));
    CHECK(escaped.find("<b>") == std::string::npos);
    CHECK(chords_only("G    C  |  D") == "[G] [C] [D]");
}

TEST_CASE("SongBuilder makes a repeated section a verse order entry", "[parse][song_import]")
{
    seventhslide::parse::SongBuilder b;
    b.begin("Verse", 1);
    b.line("One");
    b.begin("Chorus");
    b.line("Sing");
    b.begin("Verse");
    b.line("Two");
    b.begin("Chorus");
    b.line("Sing");  // the same words: a repeat
    b.begin("Chorus");  // no words: a repeat too
    b.begin("Chorus");
    b.line("Different");  // other words: numbered on
    const auto out = b.finish();
    CHECK(out.lyrics
          == "---[Verse:1]---\nOne\n\n---[Chorus:1]---\nSing\n\n---[Verse:2]---\nTwo\n\n"
             "---[Chorus:2]---\nDifferent");
    CHECK(out.verse_order == "v1 c1 v2 c1 c1 c2");

    seventhslide::parse::SongBuilder plain;
    plain.line("Words");
    plain.slide_break();
    plain.line("More");
    plain.begin("Verse");
    plain.line("Again");
    const auto in_order = plain.finish();
    CHECK(in_order.lyrics == "---[Verse:1]---\nWords\n[---][---]\nMore\n\n---[Verse:2]---\nAgain");
    CHECK(in_order.verse_order.empty());
}

TEST_CASE("split_names splits writers' names", "[parse][song_import]")
{
    using seventhslide::parse::split_names;
    CHECK(split_names("A | B|C", false) == std::vector<std::string>{"A", "B", "C"});
    CHECK(split_names("Smith, John", false) == std::vector<std::string>{"Smith, John"});
    CHECK(split_names("A, B & C and D", true) == std::vector<std::string>{"A", "B", "C", "D"});
    CHECK(split_names("Alexander Brandon", true) == std::vector<std::string>{"Alexander Brandon"});
}

// --- telling formats apart -------------------------------------------------

TEST_CASE("detect_song_format goes by content first", "[parse][song_import]")
{
    using seventhslide::parse::detect_song_format;
    CHECK(detect_song_format("a.xml", R"(<song xmlns="http://openlyrics.info/namespace/2009/song">)")
          == SongFormat::OpenLyrics);
    CHECK(detect_song_format("a.xml", R"(<song xmlns="http://openlyrics.info/namespace/2009/song" createdIn="SeventhSlide 1.0">)")
          == SongFormat::SeventhSlide);
    CHECK(detect_song_format("a", "<song><title>T</title><lyrics>[V1]\n words</lyrics></song>")
          == SongFormat::OpenSong);
    CHECK(detect_song_format("a.txt", "{title: T}\n[G]Words") == SongFormat::ChordPro);
    CHECK(detect_song_format("a.pro", "{t:T}") == SongFormat::ChordPro);
    CHECK(detect_song_format("a.txt", "T\n\nVerse 1\nWords\n\nCCLI Song # 1\n") == SongFormat::SongSelectText);
    CHECK(detect_song_format("a.bin", "[File]\nType=SongSelect Import File") == SongFormat::SongSelectUsr);
    CHECK(detect_song_format("a.txt", "T\n\nWords {not a directive} here") == SongFormat::PlainText);
}

TEST_CASE("read_song_bytes names what it cannot read", "[parse][song_import]")
{
    const auto presenter = read_song_bytes("Sunday.pro", std::string_view("\x0a\x05hello\0\x12", 9));
    CHECK(presenter.format_name == "ProPresenter 7");
    CHECK(presenter.error == "Format not supported");
    const auto older = read_song_bytes("x.pro6", "<RVPresentationDocument/>");
    CHECK(older.format_name == "ProPresenter 6");
    CHECK(older.error == "Format not supported");
    const auto binary = read_song_bytes("x.dat", std::string_view("ab\0cd", 5));
    CHECK(binary.format_name.empty());
    CHECK(binary.error == "Format not recognized");
    CHECK(read_song_bytes("bible.xml", "<XMLBIBLE><BIBLEBOOK/></XMLBIBLE>").error
          == "Format not recognized");
    CHECK(read_song_bytes("empty.txt", "\n\n").error == "No lyrics found");
    CHECK(read_song_bytes("bad.xml", "<song><lyrics>").error == "Damaged file");
    CHECK(read_song_file(import_fixture("no_such_file.txt")).error == "Could not read file");
}

// --- each format -----------------------------------------------------------

TEST_CASE("SongSelect text with the 2023 footer", "[parse][song_import]")
{
    const auto imported = read_one("songselect_2023.txt", SongFormat::SongSelectText);
    const auto& song = imported.song;
    CHECK(song.title == "It Is Well With My Soul");
    CHECK(song.lyrics
          == "---[Verse:1]---\n"
             "When peace like a river attendeth my way\n"
             "When sorrows like sea billows roll\n\n"
             "---[Chorus:1]---\n"
             "It is well with my soul\n"
             "It is well it is well with my soul\n\n"
             "---[Verse:2]---\n"
             "Though Satan should buffet though trials should come\n"
             "Let this blest assurance control");
    CHECK(song.verse_order == "v1 c1 v2 c1");
    CHECK(author_names(song) == std::vector<std::string>{"Horatio Spafford", "Philip Bliss"});
    CHECK(song.ccli_song_number == "1234567");
    CHECK(song.copyright == "\xC2\xA9 Words: Public Domain; Music: Public Domain");
    CHECK(imported.warnings.empty());
}

TEST_CASE("SongSelect text with the footer before 2023", "[parse][song_import]")
{
    const auto imported = read_one("songselect_classic.txt", SongFormat::SongSelectText);
    const auto& song = imported.song;
    CHECK(song.title == "Amazing Grace");
    // "Misc 1" opening with "(PRE-CHORUS)" is a pre-chorus.
    CHECK(song.lyrics
          == "---[Verse:1]---\n"
             "Amazing grace how sweet the sound\n"
             "That saved a wretch like me\n\n"
             "---[Pre-Chorus:1]---\n"
             "Through many dangers toils and snares\n"
             "I have already come\n\n"
             "---[Verse:2]---\n"
             "'Twas grace that taught my heart to fear\n"
             "And grace my fears relieved");
    CHECK(song.verse_order.empty());
    CHECK(author_names(song) == std::vector<std::string>{"John Newton", "Edwin Othello Excell"});
    CHECK(song.ccli_song_number == "22025");
    CHECK(song.copyright == "\xC2\xA9 Words: Public Domain (Admin. by Nobody); Music: Public Domain");
}

TEST_CASE("SongSelect's ChordPro", "[parse][song_import]")
{
    const auto imported = read_one("songselect_chordpro.chordpro", SongFormat::ChordPro);
    const auto& song = imported.song;
    CHECK(song.title == "Amazing Grace");
    CHECK(song.key == "G");
    // " - " beside a chord on a held syllable goes; the chorus heading met
    // again with nothing under it repeats the chorus.
    CHECK(song.lyrics
          == "---[Verse:1]---\n"
             "A[G]mazing grace how [G7]sweet the [C]sound that [G]saved\n"
             "A wretch like [Em]me[D]\n\n"
             "---[Chorus:1]---\n"
             "My [C]chains are gone I've been set [G]free\n\n"
             "---[Verse:2]---\n"
             "'Twas [G]grace that taught my [C]heart to fear");
    CHECK(song.verse_order == "v1 c1 v2 c1");
    CHECK(song.ccli_song_number == "22025");
    CHECK(song.copyright == "\xC2\xA9 Words: Public Domain");
    CHECK(song.authors.empty());
}

TEST_CASE("ChordPro sections, repeats and metadata", "[parse][song_import]")
{
    const auto imported = read_one("environments.cho", SongFormat::ChordPro);
    const auto& song = imported.song;
    CHECK(song.title == "Swing Low Sweet Chariot");
    CHECK(song.lyrics
          == "---[Chorus:1]---\n"
             "Swing [D]low, sweet [G]chari[D]ot,\n"
             "Comin' for to carry me [A7]home.\n\n"
             "---[Verse:1]---\n"
             "I looked over Jordan, and what did I see,\n"
             "Comin' for to carry me home.\n"
             "[---][---]\n"
             "A band of angels comin' after me,\n"
             "Comin' for to carry me home.\n\n"
             "---[Verse:2]---\n"
             "If you get there before I do,\n"
             "Comin' for to carry me home.");
    CHECK(song.verse_order == "c1 v1 c1 v2 c1");
    REQUIRE(song.authors.size() == 2);
    CHECK(song.authors[0] == Author{"Traditional"});
    CHECK(song.authors[1].name == "Wallis Willis");
    REQUIRE(song.authors[1].roles.size() == 2);
    CHECK(song.authors[1].roles[0].type == seventhslide::model::k_author_role_music);
    CHECK(song.authors[1].roles[1].type == seventhslide::model::k_author_role_words);
    CHECK(song.copyright == "Public Domain");
    CHECK(song.ccli_song_number == "4242");
    CHECK(song.key == "D");
    CHECK(song.topics == std::vector<std::string>{"Heaven", "Hope"});
    CHECK(song.comments == "Tempo: 72\nSlowly");
    CHECK(imported.warnings == std::vector<std::string>{"Dropped tabs and chord grids"});
}

TEST_CASE("ChordPro without a title takes the first line of words", "[parse][song_import]")
{
    const auto file = read_song_bytes("x.cho", "{key: A}\n[A]Hallelu[E]jah!\nPraise him\n");
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].song.title == "Hallelujah");

    const auto two = read_song_bytes("x.cho", "{t: One}\nWords\n{new_song}\n{t: Two}\nMore\n");
    REQUIRE(two.songs.size() == 2);
    CHECK(two.songs[0].song.title == "One");
    CHECK(two.songs[1].song.title == "Two");
    CHECK(two.songs[1].song.lyrics == "---[Verse:1]---\nMore");
}

TEST_CASE("OpenSong lyrics, chords and properties", "[parse][song_import]")
{
    const auto imported = read_one("opensong_vision", SongFormat::OpenSong);
    const auto& song = imported.song;
    CHECK(song.title == "Be Thou My Vision");
    CHECK(song.lyrics
          == "---[Verse:1]---\n"
             "[D]Be Thou [G]my [D]Vision\n\n"
             "---[Verse:2]---\n"
             "[D]Be Thou [G]my [D]Wisdom\n\n"
             "---[Chorus:1]---\n"
             "Heart of my own heart\n"
             "whatever befall\n"
             "[---][---]\n"
             "Still be my vision");
    CHECK(song.verse_order == "v1 c1 v2 c1");
    CHECK(author_names(song) == std::vector<std::string>{"Mary Byrne", "Eleanor Hull"});
    CHECK(song.ccli_song_number == "30639");
    CHECK(song.key == "D");
    CHECK(song.topics == std::vector<std::string>{"Light", "Guidance", "Lord"});
    CHECK(song.songbook_name == "Hymnal");
    CHECK(song.songbook_entry == "452");
    CHECK(song.comments == "Also known as: Slane");
}

TEST_CASE("SongSelect .usr in Windows-1252 with sections named in German", "[parse][song_import]")
{
    const auto imported = read_one("holy_holy.usr", SongFormat::SongSelectUsr);
    const auto& song = imported.song;
    CHECK(song.title == "Holy, Holy, Holy");
    CHECK(song.ccli_song_number == "1234");
    CHECK(author_names(song) == std::vector<std::string>{"Reginald Heber", "John Bacchus Dykes"});
    CHECK(song.copyright == "Public Domain; Nobody\xE2\x80\x99s Music (Admin. by Nobody)");
    CHECK(song.topics == std::vector<std::string>{"Holiness", "Worship"});
    CHECK(song.key == "D");
    CHECK(song.lyrics
          == "---[Verse:1]---\n"
             "Holy holy holy\n"
             "Lord God Almighty\n\n"
             "---[Chorus:1]---\n"
             "Early in the morning\n"
             "Our song shall rise\n\n"
             "---[Pre-Chorus:1]---\n"
             "Holy holy holy\n\n"
             "---[Verse:2]---\n"
             "Though the darkness hide Thee\n"
             "Though the eye of sinful man");
    CHECK(song.verse_order == "v1 c1 p1 v2 c1");
}

TEST_CASE("plain text: a title apart, verses, chords over words", "[parse][song_import]")
{
    const auto imported = read_one("plain.txt", SongFormat::PlainText);
    const auto& song = imported.song;
    CHECK(song.title == "Simple Song");
    CHECK(song.lyrics
          == "---[Verse:1]---\n"
             "[G]Chords [C]over words\n"
             "and a second line\n\n"
             "---[Chorus:1]---\n"
             "[G]Sing it a[C]gain\n\n"
             "---[Verse:2]---\n"
             "Last verse here");
    CHECK(song.verse_order.empty());

    // A first line that runs on into the words is words; the file names it.
    const auto untitled = read_song_bytes("My Song.txt", "Line one\nLine two\n");
    REQUIRE(untitled.songs.size() == 1);
    CHECK(untitled.songs[0].song.title == "My Song");
    CHECK(untitled.songs[0].song.lyrics == "---[Verse:1]---\nLine one\nLine two");
}

TEST_CASE("OpenLyrics 0.9: chords, notes, languages, instruments", "[parse][song_import]")
{
    const auto imported = read_one("openlyrics_09.xml", SongFormat::OpenLyrics);
    const auto& song = imported.song;
    // The original title; the other is kept in the comments.
    CHECK(song.title == "Amazing Grace");
    // Chords by root, structure and bass; stage notes off the slide; the
    // file's indenting is not line breaks; a part and a repeat.
    CHECK(song.lyrics
          == "---[Verse:1]---\n"
             "[G]Amazing [Cmaj7]grace how\n"
             "sweet the [D/F#]sound\n\n"
             "---[Chorus:1]---\n"
             "(men)\n"
             "My chains are gone\n"
             "[Em]Set free\n"
             "[Em]Set free");
    // The instrumental intro has no slide, so it leaves the order.
    CHECK(song.verse_order == "v1 c1 v1");
    CHECK(song.comments == "Also known as: Erstaunliche Gnade\nTempo: 90 bpm\nVerse 1: Softly.");
    CHECK(imported.warnings
          == std::vector<std::string>{"Dropped languages: DE",
                                      "Dropped instrumentals from order"});
}

TEST_CASE("Import notes are short", "[parse][song_import]")
{
    constexpr std::string_view languages = R"(<?xml version="1.0"?>
<song xmlns="http://openlyrics.info/namespace/2009/song" version="1.0" xml:lang="en">
  <properties><titles><title>T</title></titles><verseOrder>v9</verseOrder></properties>
  <lyrics>
    <verse name="v1" lang="en"><lines>Words</lines></verse>
    <verse name="v1" lang="pt-BR"><lines>Palavras</lines></verse>
    <verse name="v1" lang="he" translit="en"><lines>Milim</lines></verse>
  </lyrics>
</song>)";
    const auto file = read_song_bytes("T.xml", languages);
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].warnings
          == std::vector<std::string>{"Newer OpenLyrics: 1.0, read as 0.9",
                                      "Dropped languages: PT-BR, HE/EN",
                                      "Dropped invalid verse order"});

    const auto opensong = read_song_bytes(
        "T", "<song><title>T</title><presentation>V9</presentation><lyrics>[V1]\n Words</lyrics></song>");
    REQUIRE(opensong.songs.size() == 1);
    CHECK(opensong.songs[0].warnings == std::vector<std::string>{"Dropped invalid verse order"});
}

TEST_CASE("OpenLyrics this app wrote reads back as SeventhSlide", "[parse][song_import]")
{
    Song song;
    song.title = "Mine";
    song.lyrics = "---[Verse:1]---\nWords";
    const auto file = read_song_bytes("Mine.xml", seventhslide::parse::write_song_xml(song, "SeventhSlide 2.0"));
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].format == SongFormat::SeventhSlide);
    CHECK(file.songs[0].song.lyrics == song.lyrics);
}

// --- OpenLyrics as its schema has it ----------------------------------------

TEST_CASE("write_song_xml writes what OpenLyrics 0.8's schema takes", "[parse][song_xml]")
{
    Song song;
    song.title = "Strict";
    song.lyrics = "---[Verse:1]---\nOne\n\n---[Zed!]---\nTwo";
    song.authors = {
        {"Ada", {AuthorRole{"words", {}, {}}, AuthorRole{"arrangement", {}, {}}}},
        {"Bo", {AuthorRole{"translation", {}, {}}}},  // no language: not a translation credit
        {"Cy", {AuthorRole{"other", "Arranger", {}}}},
        {"Di", {AuthorRole{"translation", {}, "pt-BR"}}},
    };
    song.ccli_song_number = "CCLI-22025";
    song.songbook_entry = "45";  // no songbook name

    const auto xml = seventhslide::parse::write_song_xml(song, "SeventhSlide test", "2026-11-01T00:00:00Z");
    INFO(xml);
    CHECK(xml.find("createdIn=\"SeventhSlide test\"") != std::string::npos);
    CHECK(xml.find("modifiedIn=\"SeventhSlide test\"") != std::string::npos);
    CHECK(xml.find("modifiedDate=\"2026-11-01T00:00:00Z\"") != std::string::npos);
    CHECK(xml.find("<author type=\"words\">Ada</author>") != std::string::npos);
    CHECK(xml.find("arrangement") == std::string::npos);
    CHECK(xml.find("Arranger") == std::string::npos);
    CHECK(xml.find("<author>Bo</author>") != std::string::npos);
    CHECK(xml.find("<author>Cy</author>") != std::string::npos);
    CHECK(xml.find("<author type=\"translation\" lang=\"pt-BR\">Di</author>") != std::string::npos);
    CHECK(xml.find("<ccliNo>") == std::string::npos);
    CHECK(xml.find("<comment>CCLI: CCLI-22025</comment>") != std::string::npos);
    CHECK(xml.find("<songbook") == std::string::npos);
    CHECK(xml.find("<verse name=\"Zed\">") != std::string::npos);

    // Without a date, now (UTC).
    Song plain;
    plain.title = "Now";
    plain.lyrics = "Words";
    const auto now = seventhslide::parse::write_song_xml(plain);
    const auto at = now.find("modifiedDate=\"");
    REQUIRE(at != std::string::npos);
    const auto date = now.substr(at + 14, 20);
    CHECK(date[4] == '-');
    CHECK(date[10] == 'T');
    CHECK(date[19] == 'Z');
    CHECK(now.find("createdIn=\"SeventhSlide\"") != std::string::npos);
}

// --- files that are not what they seem --------------------------------------

TEST_CASE("OpenSong words with no leading space keep their first letter", "[parse][song_import]")
{
    const auto file = read_song_bytes(
        "T", "<song><title>T</title><lyrics>[V1]\nAmazing grace\n how sweet</lyrics></song>");
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].song.lyrics == "---[Verse:1]---\nAmazing grace\nhow sweet");
}

TEST_CASE("A ChordPro footer line above the words does not swallow them", "[parse][song_import]")
{
    const auto file = read_song_bytes("T.cho", "{title: T}\n\xC2\xA9 2020 Someone\n\n[G]Words here\n");
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].song.copyright == "\xC2\xA9 2020 Someone");
    CHECK(file.songs[0].song.lyrics == "---[Verse:1]---\n[G]Words here");
}

TEST_CASE("A CCLI line of any spelling is the number, not a writer", "[parse][song_import]")
{
    const auto file = read_song_bytes("T.cho", "{title: T}\n[G]Words here\n\nCCLI: 12345\n");
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].song.ccli_song_number == "12345");
    CHECK(file.songs[0].song.authors.empty());
}

TEST_CASE("Documents and web pages are not read as lyrics", "[parse][song_import]")
{
    const auto pdf = read_song_bytes("Sheet.pdf", "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n");
    CHECK(pdf.songs.empty());
    CHECK(pdf.format_name == "PDF");
    CHECK(pdf.error == "Format not supported");

    const auto rtf = read_song_bytes("Words.rtf", "{\\rtf1\\ansi Amazing grace}\n");
    CHECK(rtf.songs.empty());
    CHECK(rtf.format_name == "Rich Text");
    CHECK(rtf.error == "Format not supported");

    for (const std::string_view page :
         {"<!DOCTYPE html><html><body>Hi</body></html>", "<html><body><p>Hi</body></html>"}) {
        const auto file = read_song_bytes("page.html", page);
        CHECK(file.songs.empty());
        CHECK(file.error == "Format not recognized");
    }
}

TEST_CASE("A damaged song file says so, and what is wrong goes to the log", "[parse][song_import]")
{
    const auto file = read_song_bytes(
        "T.xml", "<song xmlns=\"http://openlyrics.info/namespace/2009/song\"><properties>");
    CHECK(file.songs.empty());
    CHECK(file.error == "Damaged file");
    CHECK(file.detail.starts_with("Failed to parse XML"));
}

namespace {

/// ``count`` elements named ``name``, each inside the one before, around ``inner``.
std::string nested(std::string_view name, int count, std::string_view inner)
{
    std::string out;
    for (int i = 0; i < count; ++i) {
        out += '<';
        out += name;
        out += '>';
    }
    out += inner;
    for (int i = 0; i < count; ++i) {
        out += "</";
        out += name;
        out += '>';
    }
    return out;
}

/// An OpenLyrics song whose one line of words is ``depth`` elements deep.
std::string openlyrics_nested_to(int depth)
{
    // <song>, <lyrics>, <verse> and <lines> are the first four.
    return R"(<song xmlns="http://openlyrics.info/namespace/2009/song">)"
           R"(<properties><titles><title>T</title></titles></properties>)"
           R"(<lyrics><verse name="v1"><lines>)"
           + nested("b", depth - 4, "Deep") + "</lines></verse></lyrics></song>";
}

}  // namespace

TEST_CASE("A song nested far too deep is refused, not walked", "[parse][song_import]")
{
    // The OpenLyrics reader walks the tree recursively; a million levels
    // would overflow the stack.
    const auto openlyrics = read_song_bytes("T.xml", openlyrics_nested_to(1'000'000));
    CHECK(openlyrics.songs.empty());
    CHECK(openlyrics.error == "Damaged file");
    CHECK(openlyrics.detail.find("nested") != std::string::npos);

    // The OpenSong reader does not recurse, but holds to the same limit.
    const auto limit = seventhslide::parse::k_max_xml_depth;
    const auto opensong = read_song_bytes(
        "T", "<song><title>T</title><lyrics>[V]\n Words</lyrics>"
                 + nested("x", limit, "") + "</song>");
    CHECK(opensong.songs.empty());
    CHECK(opensong.error == "Damaged file");
    CHECK(opensong.detail.find("nested") != std::string::npos);

    // The limit itself still reads.
    const auto at_limit = read_song_bytes("T.xml", openlyrics_nested_to(limit));
    INFO(at_limit.detail);
    CHECK(at_limit.error.empty());
    REQUIRE(at_limit.songs.size() == 1);
    CHECK(at_limit.songs[0].song.lyrics.find("Deep") != std::string::npos);
    CHECK(read_song_bytes("T.xml", openlyrics_nested_to(limit + 1)).error == "Damaged file");
}

TEST_CASE("One OpenSong chord line serves one numbered line of each verse", "[parse][song_import]")
{
    const auto file = read_song_bytes("T", R"(<song><title>T</title><lyrics>[V]
.G
1One
2Two
1Three
 Four
.C
1Five
2Six
 Seven
[C]
 Refrain
</lyrics></song>)");
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].song.lyrics
          == "---[Verse:1]---\n[G]One\nThree\nFour\n[C]Five\nSeven\n\n"
             "---[Verse:2]---\n[G]Two\n[C]Six\n\n"
             "---[Chorus:1]---\nRefrain");
}

TEST_CASE("A SongSelect .usr keeps words in parentheses", "[parse][song_import]")
{
    const auto file = read_song_bytes(
        "T.usr", "[File]\nTitle=T\nFields=Misc 1\nWords=(Oh how I love you)/nLine two\n");
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].song.lyrics == "---[Other:1]---\n(Oh how I love you)\nLine two");
}

TEST_CASE("A file far too big for a song is not read whole", "[parse][song_import]")
{
    const seventhslide::test::TempDir tmp;
    const auto path = tmp.path / "Sunday.txt";
    {
        std::ofstream(path) << "Words";
    }
    constexpr std::uint64_t k_size = std::uint64_t{1} << 36;  // 64 GiB, never stored
#if defined(_WIN32)
    // 64 GiB of zeros would be written out on Windows: NTFS stores a file's
    // length unless it is sparse, and the C runtime's resize (under
    // std::filesystem::resize_file) writes the zeros itself. Marked sparse
    // and lengthened by its end, the file stores nothing. (Linux and macOS
    // file systems leave the hole unwritten as it is.)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(file != INVALID_HANDLE_VALUE);
        DWORD unused = 0;
        const bool sparse =
            DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &unused, nullptr) != 0;
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>(k_size);
        const bool sized =
            sparse && SetFilePointerEx(file, end, nullptr, FILE_BEGIN) != 0 && SetEndOfFile(file) != 0;
        CloseHandle(file);
        if (!sparse) {
            SKIP("this file system has no sparse files, and the test would write 64 GiB");
        }
        REQUIRE(sized);
    }
#else
    std::filesystem::resize_file(path, k_size);
#endif
    REQUIRE(std::filesystem::file_size(path) == k_size);
    const auto file = read_song_file(path);
    CHECK(file.songs.empty());
    CHECK(file.error == "Format not recognized");
}

TEST_CASE("OpenLyrics verse names spelled out are read as what they say", "[parse][song_import]")
{
    const auto file = read_song_bytes("T.xml", R"(<?xml version="1.0"?>
<song xmlns="http://openlyrics.info/namespace/2009/song" version="0.8">
  <properties><titles><title>T</title></titles></properties>
  <lyrics>
    <verse name="verse2"><lines>Two</lines></verse>
    <verse name="chorus"><lines>Refrain</lines></verse>
    <verse name="c2a"><lines>Part</lines></verse>
    <verse name="Tag"><lines>Tagged</lines></verse>
    <verse name="Zed"><lines>Own</lines></verse>
  </lyrics>
</song>)");
    REQUIRE(file.songs.size() == 1);
    CHECK(file.songs[0].song.lyrics
          == "---[Verse:2]---\nTwo\n\n---[Chorus:1]---\nRefrain\n\n---[Chorus:2]---\nPart\n\n"
             "---[Other:1]---\nTagged\n\n---[Zed]---\nOwn");
}
