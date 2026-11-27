#include <seventhslide/parse/song_sections.hpp>

#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/util/strings.hpp>

#include <algorithm>
#include <utility>

namespace seventhslide::parse {
namespace {

/// Heading words, lower case without spaces or hyphens, by verse type.
struct HeadingWord {
    std::string_view word;
    std::string_view type;
};

constexpr HeadingWord k_heading_words[] = {
    // Verse
    {"verse", "Verse"}, {"vers", "Verse"}, {"verso", "Verse"}, {"couplet", "Verse"},
    {"strophe", "Verse"}, {"stanza", "Verse"}, {"estrofa", "Verse"},
    // Chorus
    {"chorus", "Chorus"}, {"refrain", "Chorus"}, {"coro", "Chorus"},
    {"estribillo", "Chorus"}, {"refrão", "Chorus"}, {"refrao", "Chorus"},
    {"refrein", "Chorus"}, {"ritornello", "Chorus"},
    // Pre-Chorus
    {"prechorus", "Pre-Chorus"}, {"prerefrain", "Pre-Chorus"}, {"precoro", "Pre-Chorus"},
    {"lift", "Pre-Chorus"}, {"channel", "Pre-Chorus"},
    // Bridge
    {"bridge", "Bridge"}, {"puente", "Bridge"}, {"ponte", "Bridge"}, {"pont", "Bridge"},
    {"brücke", "Bridge"}, {"brug", "Bridge"},
    // Intro
    {"intro", "Intro"}, {"introduction", "Intro"}, {"introducción", "Intro"},
    {"introduccion", "Intro"}, {"introdução", "Intro"}, {"introducao", "Intro"},
    {"vorspiel", "Intro"},
    // Ending
    {"ending", "Ending"}, {"end", "Ending"}, {"outro", "Ending"}, {"coda", "Ending"},
    {"final", "Ending"}, {"fin", "Ending"}, {"schluss", "Ending"}, {"nachspiel", "Ending"},
    // Other
    {"other", "Other"}, {"misc", "Other"}, {"tag", "Other"}, {"vamp", "Other"},
    {"interlude", "Other"}, {"interludio", "Other"}, {"instrumental", "Other"},
    {"turnaround", "Other"}, {"andere", "Other"}, {"otro", "Other"}, {"autre", "Other"},
    {"zwischenspiel", "Other"},
};

/// ``s`` without a repeat marker at its end ("x2", "(x2)", "2x", "(Repeat)").
[[nodiscard]] std::string_view without_repeat_marker(std::string_view s)
{
    const auto space = s.find_last_of(" \t");
    if (space == std::string_view::npos) {
        return s;
    }
    auto tail = s.substr(space + 1);
    if (tail.size() >= 2 && tail.front() == '(' && tail.back() == ')') {
        tail = tail.substr(1, tail.size() - 2);
    }
    const auto all_digits = [](std::string_view d) {
        return !d.empty() && std::all_of(d.begin(), d.end(), util::is_digit);
    };
    const bool marker =
        util::iequals(tail, "repeat")
        || (tail.size() >= 2 && util::to_lower(tail.front()) == 'x' && all_digits(tail.substr(1)))
        || (tail.size() >= 2 && util::to_lower(tail.back()) == 'x'
            && all_digits(tail.substr(0, tail.size() - 1)));
    return marker ? util::trim_view(s.substr(0, space)) : s;
}

[[nodiscard]] bool starts_with(std::string_view s, std::string_view prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

/// A note at ``s``'s start (``C``, ``F#``, ``Bb``); its length, or 0.
[[nodiscard]] std::size_t note_length(std::string_view s)
{
    if (s.empty() || s.front() < 'A' || s.front() > 'G') {
        return 0;
    }
    std::size_t n = 1;
    for (const std::string_view accidental : {"#", "b", "♯", "♭"}) {
        if (starts_with(s.substr(n), accidental)) {
            n += accidental.size();
            break;
        }
    }
    return n;
}

[[nodiscard]] bool is_chord_quality(std::string_view s)
{
    // Longest first, so "maj" is not read as "m" + "aj".
    static constexpr std::string_view k_parts[] = {
        "maj", "min", "dim", "aug", "sus", "add", "alt", "no", "M", "m", "+", "-",
        "°",   "ø",   "Δ",   "(",   ")",   "#",   "b",   "♯", "♭",
    };
    while (!s.empty()) {
        if (util::is_digit(s.front())) {
            s.remove_prefix(1);
            continue;
        }
        bool matched = false;
        for (const auto part : k_parts) {
            if (starts_with(s, part)) {
                s.remove_prefix(part.size());
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool is_chord_token(std::string_view t)
{
    if (util::iequals(t, "N.C.") || util::iequals(t, "N.C") || util::iequals(t, "NC")) {
        return true;
    }
    if (t.size() >= 2 && t.front() == '(' && t.back() == ')') {
        t = t.substr(1, t.size() - 2);
    }
    const auto slash = t.find('/');
    const auto chord = t.substr(0, slash);
    const auto root = note_length(chord);
    if (root == 0 || !is_chord_quality(chord.substr(root))) {
        return false;
    }
    if (slash == std::string_view::npos) {
        return true;
    }
    const auto bass = t.substr(slash + 1);
    return note_length(bass) == bass.size() && !bass.empty();
}

[[nodiscard]] bool is_bar_token(std::string_view t)
{
    return !t.empty() && t.find_first_not_of("|/-%:.") == std::string_view::npos;
}

/// Each chord of ``chord_line`` and the column (in characters) it starts at.
[[nodiscard]] std::vector<std::pair<std::size_t, std::string>> chords_with_columns(
    std::string_view chord_line)
{
    std::vector<std::pair<std::size_t, std::string>> out;
    std::size_t column = 0;
    for (std::size_t i = 0; i < chord_line.size();) {
        if (util::is_space(chord_line[i])) {
            ++column;
            ++i;
            continue;
        }
        const auto start_column = column;
        const auto start = i;
        while (i < chord_line.size() && !util::is_space(chord_line[i])) {
            if ((static_cast<unsigned char>(chord_line[i]) & 0xC0) != 0x80) {
                ++column;
            }
            ++i;
        }
        const auto token = chord_line.substr(start, i - start);
        if (!is_bar_token(token)) {
            out.emplace_back(start_column, std::string(token));
        }
    }
    return out;
}

}  // namespace

std::optional<SectionHeading> heading_label(std::string_view line)
{
    auto s = util::trim_view(line);
    while (!s.empty() && (s.back() == ':' || s.back() == '.')) {
        s = util::trim_view(s.substr(0, s.size() - 1));
    }
    if (s.size() >= 2
        && ((s.front() == '[' && s.back() == ']') || (s.front() == '(' && s.back() == ')')
            || (s.front() == '{' && s.back() == '}'))) {
        s = util::trim_view(s.substr(1, s.size() - 2));
    }
    s = without_repeat_marker(s);
    if (s.empty() || s.size() > 32) {
        return std::nullopt;
    }
    std::size_t number_start = s.size();
    while (number_start > 0 && util::is_digit(s[number_start - 1])) {
        --number_start;
    }
    std::string word;
    for (const char ch : s.substr(0, number_start)) {
        if (ch != ' ' && ch != '-' && ch != '_' && ch != '\t') {
            word.push_back(util::to_lower(ch));
        }
    }
    for (const auto& known : k_heading_words) {
        if (known.word == word) {
            SectionHeading heading{std::string(known.type), 0};
            if (number_start < s.size()) {
                heading.number = clamp_verse_number(
                    util::parse_leading_int(s.substr(number_start)).value_or(k_min_verse_number));
            }
            return heading;
        }
    }
    return std::nullopt;
}

void SongBuilder::begin(std::string_view type, int number)
{
    end();
    open_ = Section{std::string(type), number > 0 ? clamp_verse_number(number) : 0, {}};
    open_explicit_number_ = number > 0;
}

void SongBuilder::line(std::string markup)
{
    if (util::trim_view(markup).empty()) {
        return;
    }
    if (!open_) {
        open_ = Section{"Verse", 0, {}};
        open_explicit_number_ = false;
    }
    open_->lines.push_back(std::move(markup));
}

void SongBuilder::slide_break()
{
    if (open_ && !open_->lines.empty() && open_->lines.back() != model::k_forced_split) {
        open_->lines.emplace_back(model::k_forced_split);
    }
}

void SongBuilder::end()
{
    if (!open_) {
        return;
    }
    Section section = std::move(*open_);
    open_.reset();
    while (!section.lines.empty() && section.lines.back() == model::k_forced_split) {
        section.lines.pop_back();
    }
    const auto same_type = [&](const Section& s) { return s.type == section.type; };
    if (section.lines.empty()) {
        // A heading alone repeats the section it names.
        const auto named = std::find_if(sections_.begin(), sections_.end(), [&](const Section& s) {
            return same_type(s) && (!open_explicit_number_ || s.number == section.number);
        });
        if (named != sections_.end()) {
            order_.push_back(code_of(*named));
        }
        return;
    }
    const auto same = std::find_if(sections_.begin(), sections_.end(), [&](const Section& s) {
        return same_type(s) && s.lines == section.lines
            && (!open_explicit_number_ || s.number == section.number);
    });
    if (same != sections_.end()) {
        order_.push_back(code_of(*same));
        return;
    }
    const auto taken = [&](int number) {
        return std::any_of(sections_.begin(), sections_.end(),
                           [&](const Section& s) { return same_type(s) && s.number == number; });
    };
    if (section.number == 0 || taken(section.number)) {
        section.number = k_min_verse_number;
        while (section.number < k_max_verse_number && taken(section.number)) {
            ++section.number;
        }
    }
    order_.push_back(code_of(section));
    sections_.push_back(std::move(section));
}

bool SongBuilder::empty() const noexcept
{
    return sections_.empty() && (!open_ || open_->lines.empty());
}

bool SongBuilder::open_has_lines() const noexcept
{
    return open_ && !open_->lines.empty();
}

SongBuilder::Result SongBuilder::finish()
{
    end();
    Result out;
    std::vector<std::string> natural;
    for (const auto& section : sections_) {
        if (!out.lyrics.empty()) {
            out.lyrics += "\n\n";
        }
        out.lyrics += "---[" + section.type + ":" + std::to_string(section.number) + "]---";
        for (const auto& line : section.lines) {
            out.lyrics += "\n" + line;
        }
        natural.push_back(code_of(section));
    }
    if (order_ != natural) {
        for (const auto& code : order_) {
            out.verse_order += (out.verse_order.empty() ? "" : " ") + code;
        }
    }
    return out;
}

std::string SongBuilder::code_of(const Section& section) const
{
    return verse_code_from_label(section.type, section.number);
}

bool is_chord_line(std::string_view line)
{
    bool chord = false;
    for (const auto token : util::split_ws(line)) {
        if (is_chord_token(token)) {
            chord = true;
        } else if (!is_bar_token(token)) {
            return false;
        }
    }
    return chord;
}

std::string chords_over_words(std::string_view chord_line, std::string_view lyric)
{
    const auto chords = chords_with_columns(chord_line);
    std::string out;
    std::size_t next = 0;
    std::size_t column = 0;
    std::size_t segment = 0;
    const auto flush = [&](std::size_t end) {
        out += model::escape_markup_text(lyric.substr(segment, end - segment));
        segment = end;
    };
    for (std::size_t i = 0; i <= lyric.size(); ++i) {
        const bool boundary =
            i == lyric.size() || (static_cast<unsigned char>(lyric[i]) & 0xC0) != 0x80;
        if (!boundary) {
            continue;
        }
        while (next < chords.size() && chords[next].first <= column && i < lyric.size()) {
            flush(i);
            out += "[" + chords[next].second + "]";
            ++next;
        }
        if (i < lyric.size()) {
            ++column;
        }
    }
    flush(lyric.size());
    for (; next < chords.size(); ++next) {
        if (!out.empty() && out.back() != ' ') {
            out.push_back(' ');
        }
        out += "[" + chords[next].second + "]";
    }
    return out;
}

std::string chords_only(std::string_view chord_line)
{
    std::string out;
    for (const auto& [column, chord] : chords_with_columns(chord_line)) {
        out += (out.empty() ? "[" : " [") + chord + "]";
    }
    return out;
}

std::vector<std::string> split_names(std::string_view text, bool commas)
{
    std::vector<std::string> out;
    std::string current;
    const auto take = [&] {
        if (auto name = util::trim(current); !name.empty()) {
            out.push_back(std::move(name));
        }
        current.clear();
    };
    for (std::size_t i = 0; i < text.size();) {
        const char ch = text[i];
        if (ch == '|' || ch == ';' || (commas && ch == ',')) {
            take();
            ++i;
            continue;
        }
        const auto rest = text.substr(i);
        if (starts_with(rest, " & ")) {
            take();
            i += 3;
            continue;
        }
        if (rest.size() > 5 && util::starts_with_ci(rest, " and ")) {
            take();
            i += 5;
            continue;
        }
        current.push_back(ch);
        ++i;
    }
    take();
    return out;
}

namespace {

[[nodiscard]] bool is_ccli_song_line(std::string_view line)
{
    return util::starts_with_ci(util::trim_view(line), "CCLI Song");
}

[[nodiscard]] bool is_copyright_line(std::string_view line)
{
    const auto t = util::trim_view(line);
    return starts_with(t, "©") || util::starts_with_ci(t, "(c)") || util::starts_with_ci(t, "copyright");
}

/// The terms of use, a licence number or a link: not the song's.
[[nodiscard]] bool is_terms_line(std::string_view line)
{
    const auto t = util::trim_view(line);
    return util::starts_with_ci(t, "For use solely") || util::starts_with_ci(t, "CCLI Licen")
        || util::starts_with_ci(t, "All rights reserved") || util::contains_ci(t, "www.ccli.com")
        || util::contains_ci(t, "songselect.com");
}

}  // namespace

std::size_t ccli_footer_start(const std::vector<std::string_view>& lines, std::size_t from)
{
    std::size_t k = from;
    while (k < lines.size() && !is_ccli_song_line(lines[k])) {
        ++k;
    }
    if (k == lines.size()) {
        return k;
    }
    // Since 2023 the writers stand alone on the line before the number.
    if (k > from && !util::trim_view(lines[k - 1]).empty() && !heading_label(lines[k - 1])
        && (k - 1 == from || util::trim_view(lines[k - 2]).empty())) {
        return k - 1;
    }
    return k;
}

CcliFooter read_ccli_footer(const std::vector<std::string_view>& lines)
{
    CcliFooter out;
    bool in_copyright = false;
    bool authors_seen = false;
    std::vector<std::string> copyright;
    for (const auto raw : lines) {
        const auto line = util::trim_view(raw);
        if (line.empty()) {
            continue;
        }
        if (is_ccli_song_line(line) || (util::starts_with_ci(line, "CCLI") && !is_terms_line(line))) {
            in_copyright = false;
            std::string digits;
            for (const char ch : line) {
                if (util::is_digit(ch)) {
                    digits.push_back(ch);
                }
            }
            out.ccli_number = digits;
            continue;
        }
        if (is_terms_line(line)) {
            in_copyright = false;
            continue;
        }
        if (is_copyright_line(line)) {
            in_copyright = true;
            copyright.emplace_back(line);
            continue;
        }
        if (in_copyright) {
            copyright.emplace_back(line);
        } else if (!authors_seen) {
            out.authors = split_names(line, line.find('|') == std::string_view::npos);
            authors_seen = true;
        } else {
            copyright.emplace_back(line);
        }
    }
    for (const auto& part : copyright) {
        out.copyright += (out.copyright.empty() ? "" : "; ") + part;
    }
    return out;
}

std::vector<std::string_view> split_lines(std::string_view text)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' || text[i] == '\r') {
            out.push_back(text.substr(start, i - start));
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            start = i + 1;
        }
    }
    if (start < text.size()) {
        out.push_back(text.substr(start));
    }
    return out;
}

}  // namespace seventhslide::parse
