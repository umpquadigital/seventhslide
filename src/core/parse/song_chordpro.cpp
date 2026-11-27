// ChordPro song files; see song_readers.hpp.
//
// The directives people use (chordpro.org, ChordPro 6): metadata such as
// {title: } and {key: }, sections as {start_of_verse: Verse 2} ... {end_of_verse}
// or, as SongSelect writes them, a {comment: Chorus} heading (met again with
// nothing under it, a repeat), {chorus} to repeat the chorus, and
// {new_page} for a slide break. [G] chords are the app's own chord markup.
// SongSelect ends a file with its CCLI footer, read as in its text files.

#include "song_readers.hpp"

#include <seventhslide/model/author.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/song_sections.hpp>
#include <seventhslide/util/strings.hpp>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <utility>

namespace seventhslide::parse {
namespace {

struct Directive {
    std::string name;
    std::string value;
};

/// ``{name: value}``, ``{name value}`` or ``{name}``; nullopt for a line
/// that is not one.
[[nodiscard]] std::optional<Directive> directive_of(std::string_view line)
{
    const auto t = util::trim_view(line);
    if (t.size() < 3 || t.front() != '{' || t.back() != '}') {
        return std::nullopt;
    }
    const auto body = t.substr(1, t.size() - 2);
    std::size_t end = 0;
    while (end < body.size() && (util::is_alnum(body[end]) || body[end] == '_' || body[end] == '-')) {
        ++end;
    }
    if (end == 0 || (end < body.size() && body[end] != ':' && !util::is_space(body[end]))) {
        return std::nullopt;
    }
    Directive d{util::to_lower(body.substr(0, end)), {}};
    auto value = body.substr(end);
    if (!value.empty() && value.front() == ':') {
        value.remove_prefix(1);
    }
    d.value = util::trim(value);
    return d;
}

[[nodiscard]] bool is_any(std::string_view name, std::initializer_list<std::string_view> names)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

/// The section a ``start_of_X`` (or its short form) begins: X's type, or
/// what its label says ("{sov: Chorus}" is a chorus).
[[nodiscard]] std::optional<SectionHeading> section_start(const Directive& d)
{
    std::string kind;
    if (d.name.starts_with("start_of_")) {
        kind = d.name.substr(9);
    } else if (d.name == "sov") {
        kind = "verse";
    } else if (d.name == "soc") {
        kind = "chorus";
    } else if (d.name == "sob") {
        kind = "bridge";
    } else {
        return std::nullopt;
    }
    if (auto label = heading_label(d.value)) {
        return label;
    }
    if (auto heading = heading_label(kind)) {
        return heading;
    }
    return SectionHeading{"Other", 0};
}

/// Sections whose content is not words: tabs, grids, notation.
[[nodiscard]] bool is_non_lyric_block(std::string_view name)
{
    return is_any(name, {"start_of_tab", "sot", "start_of_grid", "sog", "start_of_abc",
                         "start_of_ly", "start_of_svg", "start_of_textblock"});
}

[[nodiscard]] bool is_block_end(std::string_view name)
{
    return name.starts_with("end_of_") || is_any(name, {"eov", "eoc", "eob", "eot", "eog"});
}

/// SongSelect sets chords on held syllables apart: "word - [G]" for
/// "word[G]". The dashes go.
[[nodiscard]] std::string tighten_chords(std::string line)
{
    for (std::size_t at = line.find(" - ["); at != std::string::npos; at = line.find(" - [")) {
        auto start = at;
        while (start > 0 && line[start - 1] == ' ') {
            --start;
        }
        line.erase(start, at + 3 - start);
    }
    for (std::size_t at = line.find("] - "); at != std::string::npos; at = line.find("] - ")) {
        auto end = at + 4;
        while (end < line.size() && line[end] == ' ') {
            ++end;
        }
        line.erase(at + 1, end - at - 1);
    }
    return line;
}

/// ``line`` without its chords, as a title from the first line of words.
[[nodiscard]] std::string without_chords(std::string_view line)
{
    std::string out;
    bool in_chord = false;
    for (const char ch : line) {
        if (ch == '[') {
            in_chord = true;
        } else if (ch == ']') {
            in_chord = false;
        } else if (!in_chord) {
            out.push_back(ch);
        }
    }
    out = util::trim(out);
    while (!out.empty() && std::string_view(".,;:!?").find(out.back()) != std::string_view::npos) {
        out.pop_back();
    }
    return out;
}

class SongReader {
public:
    explicit SongReader(std::string_view fallback_title)
        : fallback_title_(fallback_title)
    {
        imported_.format = SongFormat::ChordPro;
    }

    void directive(const Directive& d)
    {
        if (d.name.find('-') != std::string::npos) {
            return;  // a selector's variant ({title-guitar: }): the plain one wins
        }
        if (in_skipped_block_) {
            if (is_block_end(d.name)) {
                in_skipped_block_ = false;
            }
            return;
        }
        if (is_non_lyric_block(d.name)) {
            builder_.end();
            in_skipped_block_ = true;
            if (!warned_block_) {
                imported_.warnings.emplace_back("Dropped tabs and chord grids");
                warned_block_ = true;
            }
            return;
        }
        if (auto start = section_start(d)) {
            builder_.begin(start->type, start->number);
            in_environment_ = true;
            return;
        }
        if (is_block_end(d.name)) {
            builder_.end();
            in_environment_ = false;
            return;
        }
        if (d.name == "chorus") {
            const auto label = heading_label(d.value);
            builder_.begin("Chorus", label ? label->number : 0);
            builder_.end();
            return;
        }
        if (is_any(d.name, {"comment", "c", "comment_italic", "ci", "comment_box", "cb", "highlight"})) {
            if (auto heading = heading_label(d.value)) {
                builder_.begin(heading->type, heading->number);
                in_environment_ = false;
            } else if (!d.value.empty()) {
                notes_.push_back(d.value);
            }
            return;
        }
        if (is_any(d.name, {"new_page", "np", "new_physical_page", "npp", "column_break", "colb"})) {
            builder_.slide_break();
            return;
        }
        metadata(d.name, d.value);
    }

    void line(std::string_view raw)
    {
        if (in_skipped_block_) {
            return;
        }
        const auto trimmed = util::trim_view(raw);
        if (trimmed.empty()) {
            // Outside a {start_of_...} block a blank line ends a verse.
            if (!in_environment_ && builder_.open_has_lines()) {
                builder_.end();
            }
            return;
        }
        if (trimmed.front() == '#') {
            return;
        }
        const auto tightened = tighten_chords(std::string(trimmed));
        if (first_line_.empty()) {
            first_line_ = without_chords(tightened);
        }
        builder_.line(model::escape_markup_text(tightened));
    }

    void footer(const std::vector<std::string_view>& lines)
    {
        auto footer = read_ccli_footer(lines);
        model::Song& song = imported_.song;
        if (song.ccli_song_number.empty()) {
            song.ccli_song_number = footer.ccli_number;
        }
        if (song.copyright.empty()) {
            song.copyright = footer.copyright;
        }
        if (song.authors.empty()) {
            for (auto& name : footer.authors) {
                song.authors.emplace_back(std::move(name));
            }
        }
    }

    [[nodiscard]] bool empty() const { return builder_.empty() && imported_.song.title.empty(); }

    [[nodiscard]] bool has_words() const { return !builder_.empty(); }

    [[nodiscard]] ImportedSong finish()
    {
        model::Song& song = imported_.song;
        auto built = builder_.finish();
        song.lyrics = std::move(built.lyrics);
        song.verse_order = std::move(built.verse_order);
        if (song.title.empty()) {
            song.title = first_line_.empty() ? fallback_title_ : first_line_;
        }
        song.topics = model::normalize_song_topics(topics_);
        notes_.insert(notes_.begin(), info_.begin(), info_.end());
        for (std::size_t i = 0; i < notes_.size(); ++i) {
            song.comments += (i > 0 ? "\n" : "") + notes_[i];
        }
        return std::move(imported_);
    }

private:
    void add_authors(std::string_view value, std::string_view role)
    {
        model::Song& song = imported_.song;
        for (auto& name : split_names(value, true)) {
            const auto match = std::find_if(song.authors.begin(), song.authors.end(),
                                            [&](const model::Author& a) { return a.name == name; });
            model::Author& author = match != song.authors.end() ? *match : song.authors.emplace_back(std::move(name));
            if (!role.empty()) {
                author.add_role(model::AuthorRole{std::string(role), {}, {}});
            }
        }
    }

    void metadata(const std::string& name, const std::string& value)
    {
        model::Song& song = imported_.song;
        if (value.empty()) {
            return;
        }
        if (name == "meta") {
            const auto space = value.find_first_of(" \t");
            if (space != std::string::npos) {
                metadata(util::to_lower(value.substr(0, space)), util::trim(value.substr(space + 1)));
            }
            return;
        }
        if (is_any(name, {"title", "t"})) {
            if (song.title.empty()) {
                song.title = value;
            }
        } else if (is_any(name, {"subtitle", "st", "su"})) {
            info_.push_back("Subtitle: " + value);
        } else if (name == "artist") {
            add_authors(value, {});
        } else if (name == "composer") {
            add_authors(value, model::k_author_role_music);
        } else if (is_any(name, {"lyricist", "author"})) {
            add_authors(value, model::k_author_role_words);
        } else if (name == "arranger") {
            add_authors(value, model::k_author_role_arrangement);
        } else if (name == "copyright") {
            song.copyright += (song.copyright.empty() ? "" : "; ") + value;
        } else if (name == "ccli" || name == "ccli_song_number") {
            song.ccli_song_number = value;
        } else if (name == "key") {
            if (song.key.empty()) {
                song.key = value;
            }
        } else if (is_any(name, {"topic", "topics", "keywords", "theme", "themes"})) {
            for (auto& topic : split_names(value, true)) {
                topics_.push_back(std::move(topic));
            }
        } else if (is_any(name, {"tempo", "time", "capo", "album", "year", "duration"})) {
            std::string label = name;
            label.front() = util::to_upper(label.front());
            info_.push_back(label + ": " + value);
        }
        // Everything else (chord definitions, fonts, the licence) is not the song's.
    }

    std::string fallback_title_;
    ImportedSong imported_;
    SongBuilder builder_;
    std::vector<std::string> topics_;
    /// Facts with no field (tempo, capo), then notes from {comment}s.
    std::vector<std::string> info_;
    std::vector<std::string> notes_;
    std::string first_line_;
    bool in_environment_ = false;
    bool in_skipped_block_ = false;
    bool warned_block_ = false;
};

[[nodiscard]] bool is_footer_line(std::string_view line)
{
    const auto t = util::trim_view(line);
    return util::starts_with_ci(t, "CCLI") || t.starts_with("©");
}

}  // namespace

std::vector<ImportedSong> read_chordpro(std::string_view text, std::string_view fallback_title)
{
    const auto lines = split_lines(text);
    std::vector<ImportedSong> out;
    SongReader reader(fallback_title);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto line = lines[i];
        if (const auto d = directive_of(line)) {
            if (is_any(d->name, {"new_song", "ns"})) {
                if (!reader.empty()) {
                    out.push_back(reader.finish());
                }
                reader = SongReader(fallback_title);
                continue;
            }
            reader.directive(*d);
            continue;
        }
        // Since 2023 SongSelect puts the writers on a line of their own just
        // before "CCLI Song #".
        const bool writers_line = i + 1 < lines.size()
            && util::starts_with_ci(util::trim_view(lines[i + 1]), "CCLI Song")
            && !util::trim_view(line).empty() && (i == 0 || util::trim_view(lines[i - 1]).empty())
            && !heading_label(line);
        if (writers_line || is_footer_line(line)) {
            // Above the words it is one fact, and the song goes on.
            if (!reader.has_words()) {
                reader.footer({line});
                continue;
            }
            // The footer runs to the next song or the end.
            std::size_t end = i;
            while (end < lines.size()) {
                const auto d = directive_of(lines[end]);
                if (d && is_any(d->name, {"new_song", "ns"})) {
                    break;
                }
                ++end;
            }
            reader.footer({lines.begin() + static_cast<std::ptrdiff_t>(i),
                           lines.begin() + static_cast<std::ptrdiff_t>(end)});
            i = end - 1;
            continue;
        }
        reader.line(line);
    }
    if (!reader.empty()) {
        out.push_back(reader.finish());
    }
    return out;
}

}  // namespace seventhslide::parse
