// OpenSong song files; see song_readers.hpp.
//
// An OpenSong song is XML whose <lyrics> is text, a line at a time by its
// first character: "[V1]" a heading, "." chords for the line below, " " words,
// ";" a comment, "---" / "-!!" a page or column break. Under one heading,
// lines may start with a verse number ("1Amazing grace", "2'Twas grace").
// Within words, "|" ends a line, "||" a slide, and "_" holds a syllable.

#include "song_readers.hpp"

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/parse/song_sections.hpp>
#include <seventhslide/parse/song_xml.hpp>
#include <seventhslide/parse/xml_safe.hpp>
#include <seventhslide/util/strings.hpp>

#include <pugixml.hpp>

#include <algorithm>
#include <utility>

namespace seventhslide::parse {
namespace {

/// OpenSong's heading letters.
[[nodiscard]] std::string_view type_for_letter(char letter)
{
    switch (util::to_lower(letter)) {
    case 'v':
        return "Verse";
    case 'c':
        return "Chorus";
    case 'p':
        return "Pre-Chorus";
    case 'b':
        return "Bridge";
    case 'i':
        return "Intro";
    case 'e':
        return "Ending";
    case 't':  // Tag
    case 'o':
        return "Other";
    default:
        return {};
    }
}

/// A heading's type and number: "V1", "C", "1" (a verse), "Chorus 2".
[[nodiscard]] SectionHeading opensong_heading(std::string_view text)
{
    const auto t = util::trim_view(text);
    std::size_t letters = 0;
    while (letters < t.size() && util::is_alpha(t[letters])) {
        ++letters;
    }
    const auto number = util::parse_leading_int(t.substr(letters)).value_or(0);
    if (letters <= 1) {
        const auto type = letters == 1 ? type_for_letter(t.front()) : std::string_view("Verse");
        if (!type.empty()) {
            return {std::string(type), number};
        }
    }
    if (auto heading = heading_label(t)) {
        return *heading;
    }
    return {"Other", number};
}

struct Section {
    SectionHeading heading;
    std::vector<std::string> lines;
    /// The heading was met again: the next words start a new slide.
    bool break_before_next = false;
};

/// Words of one OpenSong line as lyric lines: "|" ends a line, "||" a slide
/// (``model::k_forced_split``), "_" goes.
void add_words(Section& section, std::string markup)
{
    std::erase(markup, '_');
    if (section.break_before_next && !section.lines.empty()) {
        section.lines.emplace_back(model::k_forced_split);
    }
    section.break_before_next = false;
    std::size_t start = 0;
    while (start <= markup.size()) {
        const auto bar = markup.find('|', start);
        const auto end = bar == std::string::npos ? markup.size() : bar;
        if (auto line = util::trim(std::string_view(markup).substr(start, end - start)); !line.empty()) {
            section.lines.push_back(std::move(line));
        }
        if (bar == std::string::npos) {
            break;
        }
        if (bar + 1 < markup.size() && markup[bar + 1] == '|') {
            section.lines.emplace_back(model::k_forced_split);
            start = bar + 2;
        } else {
            start = bar + 1;
        }
    }
}

/// A child element's text as written: a lyric line's first character is
/// what kind of line it is, so nothing is trimmed.
[[nodiscard]] std::string_view raw_child_text(pugi::xml_node song, const char* name)
{
    for (auto child = song.first_child(); child; child = child.next_sibling()) {
        if (xml_local_name(child.name()) == name) {
            return child.child_value();
        }
    }
    return {};
}

[[nodiscard]] std::string child_text(pugi::xml_node song, const char* name)
{
    return util::trim(raw_child_text(song, name));
}

/// "God: Awe/Wonder; Church: Unity" -> Awe, Wonder, Unity.
void add_themes(std::string_view text, std::vector<std::string>& topics)
{
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto semi = text.find(';', start);
        auto part = text.substr(start, semi == std::string_view::npos ? text.npos : semi - start);
        if (const auto colon = part.find(':'); colon != std::string_view::npos) {
            part = part.substr(colon + 1);
        }
        std::size_t s = 0;
        while (s <= part.size()) {
            const auto slash = part.find('/', s);
            auto topic = util::trim(part.substr(s, slash == std::string_view::npos ? part.npos : slash - s));
            if (!topic.empty()) {
                topics.push_back(std::move(topic));
            }
            if (slash == std::string_view::npos) {
                break;
            }
            s = slash + 1;
        }
        if (semi == std::string_view::npos) {
            break;
        }
        start = semi + 1;
    }
}

/// "V1 C V2 C B" as the app's codes, against the sections the song has.
[[nodiscard]] std::string presentation_order(std::string_view presentation)
{
    std::string out;
    for (const auto token : util::split_ws(presentation)) {
        const auto heading = opensong_heading(token);
        const auto code = verse_code_from_label(heading.type, heading.number > 0 ? heading.number : 1);
        if (!code.empty()) {
            out += (out.empty() ? "" : " ") + code;
        }
    }
    return out;
}

[[nodiscard]] ImportedSong read_song(pugi::xml_node root, std::string_view fallback_title)
{
    ImportedSong imported;
    imported.format = SongFormat::OpenSong;
    model::Song& song = imported.song;
    song.title = child_text(root, "title");
    if (song.title.empty()) {
        song.title = std::string(fallback_title);
    }
    for (auto& name : split_names(child_text(root, "author"), true)) {
        song.authors.emplace_back(std::move(name));
    }
    song.copyright = child_text(root, "copyright");
    for (const char ch : child_text(root, "ccli")) {
        if (util::is_digit(ch)) {
            song.ccli_song_number.push_back(ch);
        }
    }
    song.key = child_text(root, "key");
    std::vector<std::string> topics;
    add_themes(child_text(root, "theme"), topics);
    add_themes(child_text(root, "alttheme"), topics);
    song.topics = model::normalize_song_topics(topics);
    // "Hymnal 123": the book, then its number.
    if (const auto hymn = child_text(root, "hymn_number"); !hymn.empty()) {
        const auto space = hymn.find_last_of(' ');
        const auto last = space == std::string::npos ? hymn : hymn.substr(space + 1);
        if (!last.empty() && std::all_of(last.begin(), last.end(), util::is_digit)) {
            song.songbook_entry = last;
            song.songbook_name = space == std::string::npos ? std::string() : util::trim(hymn.substr(0, space));
        } else {
            song.songbook_name = hymn;
        }
    }
    std::vector<std::string> comments;
    if (const auto aka = child_text(root, "aka"); !aka.empty()) {
        comments.push_back("Also known as: " + aka);
    }
    for (const char* user : {"user1", "user2", "user3"}) {
        if (auto text = child_text(root, user); !text.empty()) {
            comments.push_back(std::move(text));
        }
    }
    for (std::size_t i = 0; i < comments.size(); ++i) {
        song.comments += (i > 0 ? "\n" : "") + comments[i];
    }

    // Sections in the order first met; a heading met again adds a slide.
    std::vector<Section> sections;
    SectionHeading heading{"Verse", 0};
    std::string chords;
    // The last numbered line ``chords`` went on: one chord line serves one
    // numbered line of each verse ("1Amazing grace" / "2'Twas grace").
    int chords_verse = 0;
    const auto section_for = [&](const SectionHeading& h) -> Section& {
        const int number = h.number > 0 ? h.number : 1;
        const auto found = std::find_if(sections.begin(), sections.end(), [&](const Section& s) {
            return s.heading.type == h.type && s.heading.number == number;
        });
        if (found != sections.end()) {
            return *found;
        }
        return sections.emplace_back(Section{{h.type, number}, {}, false});
    };
    for (const auto raw : split_lines(raw_child_text(root, "lyrics"))) {
        if (util::trim_view(raw).empty() || raw.front() == ';' || raw.substr(0, 3) == "---"
            || raw.substr(0, 3) == "-!!") {
            continue;
        }
        if (raw.front() == '.') {
            chords = std::string(raw.substr(1));
            chords_verse = 0;
            continue;
        }
        if (raw.front() == '[') {
            const auto close = raw.find(']');
            heading = opensong_heading(raw.substr(1, close == std::string_view::npos ? raw.npos : close - 1));
            chords.clear();
            chords_verse = 0;
            const SectionHeading named{heading.type, heading.number > 0 ? heading.number : 1};
            for (auto& s : sections) {
                if (s.heading == named && !s.lines.empty()) {
                    s.break_before_next = true;
                }
            }
            continue;
        }
        auto line = raw;
        auto h = heading;
        const int verse = util::is_digit(line.front()) ? line.front() - '0' : 0;
        if (verse > 0) {
            h.number = verse;
        }
        // Past the numbered lines it served, a chord line is spent.
        if (chords_verse > 0 && verse <= chords_verse) {
            chords.clear();
        }
        // The first character is the line's kind (or its verse number); the
        // chord line lost its "." alike, so their columns line up. Files
        // edited by hand may start the words there instead.
        if (line.front() == ' ' || util::is_digit(line.front())) {
            line.remove_prefix(1);
        }
        auto markup = chords.empty() ? model::escape_markup_text(line) : chords_over_words(chords, line);
        chords_verse = chords.empty() ? 0 : verse;
        if (verse == 0) {
            chords.clear();
        }
        add_words(section_for(h), std::move(markup));
    }

    SongBuilder builder;
    for (const auto& section : sections) {
        builder.begin(section.heading.type, section.heading.number);
        for (const auto& line : section.lines) {
            if (line == model::k_forced_split) {
                builder.slide_break();
            } else {
                builder.line(line);
            }
        }
    }
    song.lyrics = builder.finish().lyrics;
    song.verse_order = presentation_order(child_text(root, "presentation"));
    if (validate_verse_order(song.verse_order, song.lyrics)) {
        song.verse_order.clear();
        imported.warnings.emplace_back("Dropped invalid verse order");
    }
    return imported;
}

}  // namespace

std::vector<ImportedSong> read_opensong(std::string_view xml, std::string_view fallback_title)
{
    return with_xml_errors<SongXmlError>([&] {
        reject_doctype<SongXmlError>(xml);
        pugi::xml_document doc;
        const auto result = doc.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_utf8);
        if (!result) {
            throw SongXmlError(std::string("Failed to parse XML: ") + result.description());
        }
        reject_deep_nesting<SongXmlError>(doc);
        const auto root = doc.document_element();
        if (!root || xml_local_name(root.name()) != "song") {
            throw SongXmlError("Not an OpenSong song: its root is not <song>");
        }
        return std::vector<ImportedSong>{read_song(root, fallback_title)};
    });
}

}  // namespace seventhslide::parse
