// SongSelect's older song file, .usr (later .bin, the same inside); see
// song_readers.hpp.
//
//   [File]
//   Type=SongSelect Import File
//   Version=3.0
//   [S A2672885]                       the CCLI song number
//   Title=Above All
//   Author=LeBlanc, Lenny | Baloche, Paul
//   Copyright=1999 Integrity's Hosanna! Music | LenSongs Publishing
//   Admin=Integrity Music
//   Themes=Cross/tKingship/tMajesty
//   Keys=A
//   Fields=Vers 1/tVers 2/tChorus 1/tAndere 1
//   Words=Above all powers.../nAbove all kings.../tCrucified.../t...
//
// Fields name the sections in the download's language; Words holds their
// lines in the same order, "/t" between sections and "/n" between lines.

#include "song_readers.hpp"

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/song_sections.hpp>
#include <seventhslide/util/strings.hpp>

#include <utility>

namespace seventhslide::parse {
namespace {

[[nodiscard]] std::vector<std::string> split_on(std::string_view text, std::string_view separator)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const auto at = text.find(separator, start);
        out.emplace_back(text.substr(start, at == std::string_view::npos ? text.npos : at - start));
        if (at == std::string_view::npos) {
            return out;
        }
        start = at + separator.size();
    }
}

[[nodiscard]] std::string replace_all(std::string text, std::string_view from, std::string_view to)
{
    for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

/// "LeBlanc, Lenny" -> "Lenny LeBlanc".
[[nodiscard]] std::string first_name_first(std::string_view name)
{
    const auto comma = name.find(',');
    if (comma == std::string_view::npos) {
        return util::trim(name);
    }
    return util::trim(util::trim(name.substr(comma + 1)) + " " + util::trim(name.substr(0, comma)));
}

}  // namespace

std::vector<ImportedSong> read_songselect_usr(std::string_view text, std::string_view fallback_title)
{
    ImportedSong imported;
    imported.format = SongFormat::SongSelectUsr;
    model::Song& song = imported.song;
    std::string authors;
    std::string admin;
    std::string themes;
    std::string fields;
    std::string words;
    for (const auto raw : split_lines(text)) {
        const auto line = util::trim_view(raw);
        if (line.starts_with("[S ")) {
            for (const char ch : line.substr(0, line.find(']'))) {
                if (util::is_digit(ch)) {
                    song.ccli_song_number.push_back(ch);
                }
            }
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const auto key = line.substr(0, eq);
        auto value = util::trim(line.substr(eq + 1));
        if (key == "Title") {
            song.title = std::move(value);
        } else if (key == "Author") {
            authors = std::move(value);
        } else if (key == "Copyright") {
            song.copyright = replace_all(std::move(value), " | ", "; ");
        } else if (key == "Admin") {
            admin = std::move(value);
        } else if (key == "Themes") {
            themes = replace_all(std::move(value), " | ", "/t");
        } else if (key == "Keys") {
            song.key = util::trim(split_on(value, "/t").front());
        } else if (key == "Fields") {
            fields = std::move(value);
        } else if (key == "Words") {
            words = std::move(value);
        }
    }
    if (song.title.empty()) {
        song.title = std::string(fallback_title);
    }
    if (!admin.empty()) {
        song.copyright += (song.copyright.empty() ? "" : " ") + ("(Admin. by " + admin + ")");
    }

    // Authors: "/"-separated, or "|"-separated when there is no "/".
    auto names = split_on(authors, "/");
    if (names.size() < 2) {
        names = split_on(authors, "|");
    }
    for (const auto& name : names) {
        if (auto first = first_name_first(name); !first.empty()) {
            song.authors.emplace_back(std::move(first));
        }
    }

    std::vector<std::string> topics;
    for (auto& topic : split_on(themes, "/t")) {
        topics.push_back(util::trim(topic));
    }
    song.topics = model::normalize_song_topics(topics);

    SongBuilder builder;
    const auto field_list = split_on(fields, "/t");
    const auto word_list = split_on(words, "/t");
    for (std::size_t i = 0; i < word_list.size(); ++i) {
        const std::string_view field = i < field_list.size() ? std::string_view(field_list[i]) : "";
        auto heading = heading_label(field).value_or(SectionHeading{"Other", 0});
        auto lines = split_on(replace_all(replace_all(word_list[i], "/n", "\n"), " | ", "\n"), "\n");
        // A section of another kind may say on its first line what it is:
        // "(PRE-CHORUS)", "(BRIDGE)". Any other "(...)" may be words, and stays.
        if (!lines.empty() && heading.type == "Other") {
            const auto first = util::trim(lines.front());
            if (auto named = heading_label(first); named && first.starts_with("(")) {
                heading = *named;
                lines.erase(lines.begin());
            }
        }
        builder.begin(heading.type, heading.number);
        for (const auto& line : lines) {
            builder.line(model::escape_markup_text(util::trim(line)));
        }
    }
    auto built = builder.finish();
    song.lyrics = std::move(built.lyrics);
    song.verse_order = std::move(built.verse_order);
    return {std::move(imported)};
}

}  // namespace seventhslide::parse
