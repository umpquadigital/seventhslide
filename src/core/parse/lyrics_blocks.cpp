#include <seventhslide/parse/lyrics_blocks.hpp>

#include "seventhslide/util/find.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace seventhslide::parse {
namespace {

/// Split on ``---[Label]---\n`` capturing labels.
/// Returns empty when no headers found (caller treats as plain).
[[nodiscard]] std::vector<std::string> split_verse_headers(std::string_view lyrics)
{
    static constexpr std::string_view k_open = "---[";
    static constexpr std::string_view k_close = "]---\n";

    std::vector<std::string> parts;
    std::size_t pos = 0;
    std::size_t search = 0;
    bool found = false;
    while (search < lyrics.size()) {
        const auto open = lyrics.find(k_open, search);
        if (open == std::string_view::npos) {
            break;
        }
        const auto label_start = open + k_open.size();
        const auto close = lyrics.find(k_close, label_start);
        if (close == std::string_view::npos) {
            break;
        }
        // Reject labels containing ']'.
        const auto label = lyrics.substr(label_start, close - label_start);
        if (label.find(']') != std::string_view::npos) {
            search = open + 1;
            continue;
        }
        if (!found) {
            parts.emplace_back(lyrics.substr(0, open));
            found = true;
        } else {
            // Body since previous header end.
            parts.emplace_back(lyrics.substr(pos, open - pos));
        }
        parts.emplace_back(label);
        pos = close + k_close.size();
        search = pos;
    }
    if (!found) {
        return {};
    }
    parts.emplace_back(lyrics.substr(pos));
    return parts;
}

[[nodiscard]] LyricsBlocks verses_from_plain(std::string_view lyrics_text,
                                             std::string_view verse_order,
                                             bool suppress_titles)
{
    LyricsBlocks out;
    std::vector<std::string> body_verses;
    std::size_t start = 0;
    while (start <= lyrics_text.size()) {
        const auto sep = lyrics_text.find("\n\n", start);
        const auto end = sep == std::string_view::npos ? lyrics_text.size() : sep;
        auto chunk = std::string(lyrics_text.substr(start, end - start));
        if (!chunk.empty()) {
            body_verses.push_back(std::move(chunk));
        }
        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 2;
    }

    if (verse_order.empty()) {
        out.verses = std::move(body_verses);
        out.verse_codes.assign(out.verses.size(), "");
        return out;
    }

    bool emitted_body = false;
    bool saw_theme = false;
    bool saw_other = false;
    for (auto token_sv : util::split_ws(verse_order)) {
        const auto token = util::to_lower(token_sv);
        if (is_theme_code(token)) {
            saw_theme = true;
            continue;
        }
        saw_other = true;
        if (is_title_code(token)) {
            if (suppress_titles) {
                out.verses.emplace_back();
                out.verse_codes.emplace_back("t1");
            }
        } else if (!emitted_body) {
            for (auto& v : body_verses) {
                out.verses.push_back(v);
                out.verse_codes.emplace_back();
            }
            emitted_body = true;
        }
    }
    if (out.verses.empty() && !(saw_theme && !saw_other)) {
        out.verses = std::move(body_verses);
        out.verse_codes.assign(out.verses.size(), "");
    }
    return out;
}

[[nodiscard]] bool label_has_section_letter(std::string_view label)
{
    const auto lud = util::to_lower(label);
    std::size_t i = lud.size();
    while (i > 0 && util::is_space(lud[i - 1])) {
        --i;
    }
    if (i < 2) {
        return false;
    }
    return util::is_lower(lud[i - 1])
        && util::is_digit(lud[i - 2]);
}

/// ``Verse:1a`` -> ``Verse:1``; a label without a section letter is unchanged.
[[nodiscard]] std::string strip_section_letter(std::string_view label)
{
    // ``^(.+:\d+)[a-z]$``, ignoring case.
    const auto n = label.size();
    if (n < 4 || !util::is_alpha(label[n - 1])) {
        return std::string(label);
    }
    std::size_t i = n - 1;
    while (i > 0 && util::is_digit(label[i - 1])) {
        --i;
    }
    if (i == n - 1 || i < 2 || label[i - 1] != ':') {
        return std::string(label);
    }
    return std::string(label.substr(0, n - 1));
}

/// The type whose label equals ``name`` ignoring ASCII case, or null.
[[nodiscard]] const VerseType* verse_type_named(std::string_view name) noexcept
{
    for (const auto& type : k_verse_types) {
        if (util::iequals(type.name, name)) {
            return &type;
        }
    }
    return nullptr;
}

/// Verse-order token match: ``v1`` / ``v1a`` / ``v1b`` all select verse ``v1``.
[[nodiscard]] bool matches_token(std::string_view code, std::string_view token)
{
    return base_verse_code(code) == base_verse_code(token);
}

[[nodiscard]] std::vector<std::string> normalize_order_tokens(std::string_view verse_order)
{
    std::vector<std::string> out;
    for (auto token_sv : util::split_ws(verse_order)) {
        auto token = util::to_lower(token_sv);
        auto base = base_verse_code(token);
        const bool lettered = base != token;
        if (lettered && !out.empty() && out.back() == base) {
            continue;
        }
        out.push_back(std::move(base));
    }
    return out;
}

struct Block {
    std::string label;  ///< the first section's, without its letter
    std::string code;
    std::string content;
    bool lettered = false;
};

/// ``next`` after ``body`` in one verse, a forced break between them.
void join_section(std::string& body, std::string_view next)
{
    body.push_back('\n');
    body.append(model::k_forced_split);
    body.push_back('\n');
    body.append(next);
}

/// The sections of ``raw_parts`` (from split_verse_headers) as verses: lettered
/// sections of one verse that follow each other become one block, their bodies
/// split by forced breaks. Titles are never joined, and with ``skip_titles``
/// they are dropped first.
[[nodiscard]] std::vector<Block> verse_blocks(const std::vector<std::string>& raw_parts,
                                              bool skip_titles)
{
    std::vector<Block> blocks;
    for (std::size_t i = 1; i + 1 < raw_parts.size(); i += 2) {
        const auto label = util::trim(raw_parts[i]);
        auto content = util::trim(raw_parts[i + 1]);
        auto code = label_to_code(label);
        if (skip_titles && is_title_code(code)) {
            continue;
        }
        const bool lettered = label_has_section_letter(label);
        if (!blocks.empty() && blocks.back().code == code && !is_title_code(code)
            && (lettered || blocks.back().lettered)) {
            Block& joined = blocks.back();
            join_section(joined.content, content);
            joined.lettered = true;
            continue;
        }
        blocks.push_back(
            Block{strip_section_letter(label), std::move(code), std::move(content), lettered});
    }
    return blocks;
}

[[nodiscard]] LyricsBlocks verses_from_blocks(const std::vector<std::string>& raw_parts,
                                              std::string_view verse_order,
                                              bool suppress_titles)
{
    std::vector<Block> blocks = verse_blocks(raw_parts, suppress_titles);

    LyricsBlocks out;
    if (verse_order.empty()) {
        for (auto& b : blocks) {
            out.verse_codes.push_back(b.code);
            out.verses.push_back(std::move(b.content));
        }
        return out;
    }

    bool saw_theme = false;
    bool saw_content = false;
    for (const auto& token : normalize_order_tokens(verse_order)) {
        if (is_theme_code(token)) {
            saw_theme = true;
            continue;
        }
        saw_content = true;
        if (suppress_titles && is_title_code(token)) {
            out.verses.emplace_back();
            out.verse_codes.emplace_back("t1");
            continue;
        }
        // One token is one section, even where its verse's lettered sections
        // are apart in the lyrics: they play joined, as adjacent ones do. A
        // title in parts is one title slide too; it shows the song's title,
        // not its body.
        const Block* first = nullptr;
        std::string section;
        for (const auto& b : blocks) {
            if (!matches_token(b.code, token)) {
                continue;
            }
            if (first == nullptr) {
                first = &b;
                section = b.content;
            } else {
                join_section(section, b.content);
            }
        }
        if (first != nullptr) {
            out.verses.push_back(std::move(section));
            out.verse_codes.push_back(first->code);
        }
    }
    if (!out.verses.empty()) {
        return out;
    }
    if (saw_theme && !saw_content) {
        return out;
    }
    // Fallback: appearance order.
    for (auto& b : blocks) {
        out.verse_codes.push_back(b.code);
        out.verses.push_back(std::move(b.content));
    }
    return out;
}

}  // namespace

const VerseType* verse_type_for_code(char code) noexcept
{
    return util::find_by(k_verse_types, code, &VerseType::code);
}

const std::vector<std::string_view>& verse_type_names()
{
    static const std::vector<std::string_view> names = [] {
        std::vector<std::string_view> out;
        for (const auto& type : k_verse_types) {
            out.push_back(type.name);
        }
        return out;
    }();
    return names;
}

std::string canonicalize_verse_type(std::string_view type)
{
    const auto* found = verse_type_named(util::trim_view(type));
    return found != nullptr ? std::string(found->name) : std::string();
}

int clamp_verse_number(int number) noexcept
{
    if (number < k_min_verse_number) {
        return k_min_verse_number;
    }
    if (number > k_max_verse_number) {
        return k_max_verse_number;
    }
    return number;
}

std::string verse_code_from_label(std::string_view type, int number)
{
    const auto* found = verse_type_named(util::trim_view(type));
    if (found == nullptr) {
        return {};
    }
    return std::string(1, found->code) + std::to_string(clamp_verse_number(number));
}

VerseLabel verse_label_from_code(std::string_view code)
{
    VerseLabel out;
    const auto base = util::to_lower(base_verse_code(code));
    if (base.empty() || base == "misc") {
        return out;
    }
    std::size_t i = 0;
    while (i < base.size() && util::is_alpha(base[i])) {
        ++i;
    }
    if (i != 1) {
        return out;
    }
    std::string type;
    if (const auto* found = verse_type_for_code(base[0])) {
        type = std::string(found->name);
    }
    if (type.empty()) {
        return out;
    }
    int number = k_min_verse_number;
    if (i < base.size()) {
        int parsed = 0;
        bool any = false;
        while (i < base.size() && util::is_digit(base[i])) {
            any = true;
            parsed = std::min(parsed * 10 + (base[i] - '0'), k_max_verse_number + 1);
            ++i;
        }
        if (any) {
            number = parsed;
        }
    }
    out.type = std::move(type);
    out.number = clamp_verse_number(number);
    return out;
}

std::string verse_indicator_text(std::string_view type, int number)
{
    return util::to_upper(verse_code_from_label(type, number));
}

std::string label_to_code(std::string_view label)
{
    const auto lud = util::to_lower(label);
    std::string digits;

    // Trailing ``(\d+)([a-z])?``.
    std::size_t i = lud.size();
    while (i > 0 && util::is_space(lud[i - 1])) {
        --i;
    }
    if (i > 0 && util::is_lower(lud[i - 1])) {
        // A section letter (``1a``) follows a digit; skip it.
        if (i > 1 && util::is_digit(lud[i - 2])) {
            --i;
        }
    }
    const std::size_t digit_end = i;
    while (i > 0 && util::is_digit(lud[i - 1])) {
        --i;
    }
    if (i < digit_end) {
        digits = lud.substr(i, digit_end - i);
    } else {
        // No trailing number match - collect any digits.
        digits.clear();
        for (char c : lud) {
            if (util::is_digit(c)) {
                digits.push_back(c);
            }
        }
    }

    const auto colon = lud.find(':');
    const std::string_view label_type =
        colon == std::string::npos ? std::string_view{lud}
                                   : std::string_view{lud}.substr(0, colon);
    const auto* found = verse_type_named(label_type);
    if (found == nullptr) {
        return "misc";
    }
    std::string code(1, found->code);
    code += digits.empty() ? "1" : digits;
    return code;
}

bool is_title_code(std::string_view code)
{
    // ``^t\d+[a-z]?$``
    if (code.size() < 2 || code[0] != 't') {
        return false;
    }
    std::size_t i = 1;
    if (!util::is_digit(code[i])) {
        return false;
    }
    while (i < code.size() && util::is_digit(code[i])) {
        ++i;
    }
    if (i == code.size()) {
        return true;
    }
    return i + 1 == code.size()
        && util::is_lower(code[i]);
}

bool is_theme_code(std::string_view code)
{
    // ``^th\d+$`` - distinct from title ``t1`` (second character is not a digit).
    if (code.size() < 3 || code[0] != 't' || code[1] != 'h') {
        return false;
    }
    for (std::size_t i = 2; i < code.size(); ++i) {
        if (!util::is_digit(code[i])) {
            return false;
        }
    }
    return true;
}

std::string base_verse_code(std::string_view code)
{
    // ``^([a-z]+\d+)([a-z])$``
    if (code.size() < 3) {
        return std::string(code);
    }
    const char last = code.back();
    if (!util::is_lower(last)) {
        return std::string(code);
    }
    const char prev = code[code.size() - 2];
    if (!util::is_digit(prev)) {
        return std::string(code);
    }
    // Prefix must be [a-z]+ digits - already have trailing digit before letter.
    std::size_t i = 0;
    while (i < code.size() - 1
           && util::is_lower(code[i])) {
        ++i;
    }
    if (i == 0) {
        return std::string(code);
    }
    std::size_t j = i;
    while (j < code.size() - 1
           && util::is_digit(code[j])) {
        ++j;
    }
    if (j != code.size() - 1 || j == i) {
        return std::string(code);
    }
    return std::string(code.substr(0, code.size() - 1));
}

bool same_verse_sections(std::string_view a, std::string_view b)
{
    const auto ba = base_verse_code(a);
    const auto bb = base_verse_code(b);
    // Both must be section codes (base dropped a trailing letter).
    if (ba.size() == a.size() || bb.size() == b.size()) {
        return false;
    }
    return ba == bb;
}

LyricsBlocks parse_verses(std::string_view lyrics,
                          std::string_view verse_order,
                          bool suppress_titles)
{
    auto raw_parts = split_verse_headers(lyrics);
    if (raw_parts.empty()) {
        return verses_from_plain(lyrics, verse_order, suppress_titles);
    }
    return verses_from_blocks(raw_parts, verse_order, suppress_titles);
}

std::vector<LabelledVerse> labelled_verses(std::string_view lyrics)
{
    std::vector<LabelledVerse> out;
    const auto raw_parts = split_verse_headers(lyrics);
    if (raw_parts.empty()) {
        // Blank-line blocks; any run of blank lines is one break.
        std::size_t start = 0;
        while (start < lyrics.size()) {
            auto sep = lyrics.find("\n\n", start);
            const auto end = sep == std::string_view::npos ? lyrics.size() : sep;
            auto chunk = util::trim(lyrics.substr(start, end - start));
            if (!chunk.empty()) {
                out.push_back(LabelledVerse{"Verse:" + std::to_string(out.size() + 1),
                                            std::move(chunk)});
            }
            if (sep == std::string_view::npos) {
                break;
            }
            while (sep < lyrics.size() && lyrics[sep] == '\n') {
                ++sep;
            }
            start = sep;
        }
        return out;
    }
    for (auto& b : verse_blocks(raw_parts, false)) {
        out.push_back(LabelledVerse{std::move(b.label), std::move(b.content)});
    }
    return out;
}

std::string join_labelled_verses(const std::vector<LabelledVerse>& verses)
{
    std::string out;
    for (std::size_t i = 0; i < verses.size(); ++i) {
        if (i > 0) {
            out.push_back('\n');
        }
        out += "---[";
        out += verses[i].label;
        out += "]---\n";
        out += verses[i].content;
        out.push_back('\n');
    }
    return out;
}

std::optional<std::string> validate_verse_order(std::string_view verse_order,
                                                std::string_view lyrics)
{
    if (verse_order.empty() || lyrics.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> keys;
    auto parts = split_verse_headers(lyrics);
    if (parts.empty()) {
        // Plain / OpenSong lyrics have no ---[Label]--- headers to check against.
        return std::nullopt;
    }
    for (std::size_t i = 1; i + 1 < parts.size(); i += 2) {
        keys.push_back(label_to_code(util::trim(parts[i])));
    }
    std::vector<std::string> missing;
    for (auto token_sv : util::split_ws(verse_order)) {
        const auto token = util::to_lower(token_sv);
        if (is_title_code(token) || is_theme_code(token)) {
            continue;
        }
        bool ok = false;
        for (const auto& c : keys) {
            if (matches_token(c, token)) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            missing.push_back(token);
        }
    }
    if (missing.empty()) {
        return std::nullopt;
    }
    std::ostringstream msg;
    msg << "Invalid codes: ";
    for (std::size_t i = 0; i < missing.size(); ++i) {
        if (i) {
            msg << ", ";
        }
        msg << missing[i];
    }
    msg << "\nValid codes: ";
    std::vector<std::string> sorted = keys;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i) {
            msg << ", ";
        }
        msg << sorted[i];
    }
    return msg.str();
}

std::optional<std::string> validate_no_blank_lines_in_verse(std::string_view lyrics)
{
    auto raw_parts = split_verse_headers(lyrics);
    if (raw_parts.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 1; i + 1 < raw_parts.size(); i += 2) {
        const auto& label = raw_parts[i];
        std::string_view body = raw_parts[i + 1];
        // rstrip newlines then split
        while (!body.empty()
               && (body.back() == '\n' || body.back() == '\r')) {
            body.remove_suffix(1);
        }
        bool found_content = false;
        bool found_blank_after_content = false;
        std::size_t line_start = 0;
        while (line_start <= body.size()) {
            auto line_end = body.find('\n', line_start);
            if (line_end == std::string_view::npos) {
                line_end = body.size();
            }
            auto line = body.substr(line_start, line_end - line_start);
            bool blank = true;
            for (char c : line) {
                if (!util::is_space(c)) {
                    blank = false;
                    break;
                }
            }
            if (!blank) {
                if (found_blank_after_content) {
                    return "\"" + label
                        + "\" contains a blank line in the middle of the verse. "
                          "Blank lines are only allowed between verses, not within "
                          "them.";
                }
                found_content = true;
            } else if (found_content) {
                found_blank_after_content = true;
            }
            if (line_end >= body.size()) {
                break;
            }
            line_start = line_end + 1;
        }
    }
    return std::nullopt;
}

namespace {

/// The digits after ``label``'s first ``:`` that has any, as a number (large
/// runs saturate); nullopt when no ``:`` is followed by a digit.
[[nodiscard]] std::optional<long long> first_colon_number(std::string_view label)
{
    for (std::size_t i = 0; i + 1 < label.size(); ++i) {
        if (label[i] != ':' || !util::is_digit(label[i + 1])) {
            continue;
        }
        long long n = 0;
        for (std::size_t j = i + 1; j < label.size() && util::is_digit(label[j]); ++j) {
            n = std::min<long long>(n * 10 + (label[j] - '0'), 1'000'000);
        }
        return n;
    }
    return std::nullopt;
}

/// Where the last ``:<digits>`` that has something before it starts, and
/// where its digits end; nullopt when there is none.
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> last_colon_digits(
    std::string_view label)
{
    for (std::size_t i = label.size(); i-- > 1;) {
        if (label[i] == ':' && i + 1 < label.size() && util::is_digit(label[i + 1])) {
            std::size_t end = i + 1;
            while (end < label.size() && util::is_digit(label[end])) {
                ++end;
            }
            return std::pair{i, end};
        }
    }
    return std::nullopt;
}

/// ``label`` lowered and cut after its verse number: ``Verse:2a`` -> ``verse:2``.
[[nodiscard]] std::string verse_identity(std::string_view label)
{
    const auto lower = util::to_lower(label);
    const auto at = last_colon_digits(lower);
    return at ? lower.substr(0, at->second) : lower;
}

} // namespace

std::string verse_display_name(std::string_view label)
{
    const auto at = last_colon_digits(label);
    if (!at) {
        return std::string(label);
    }
    std::string out(label.substr(0, at->first));
    out += ' ';
    out += label.substr(at->first + 1, at->second - at->first - 1);
    return out;
}

std::string_view editor_verse_type(std::string_view label)
{
    static_assert(k_verse_types.front().code == 'v', "an unnamed type reads as Verse");
    const auto name = label.substr(0, label.find(':'));
    for (const auto& type : k_verse_types) {
        if (type.code != 't' && util::iequals(type.name, name)) {
            return type.name;
        }
    }
    return k_verse_types.front().name;
}

int verse_number(std::string_view label)
{
    const auto n = first_colon_number(label);
    return n && *n >= 1 ? clamp_verse_number(static_cast<int>(*n)) : 1;
}

std::string make_verse_label(std::string_view type, int number)
{
    std::string out = type.empty() ? std::string(k_verse_types.front().name) : std::string(type);
    out += ':';
    out += std::to_string(clamp_verse_number(number));
    return out;
}

int find_verse_label(const std::vector<std::string>& labels,
                     std::string_view type,
                     int number,
                     int except)
{
    const auto want = util::to_lower(type) + ':' + std::to_string(number);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (static_cast<int>(i) != except && verse_identity(labels[i]) == want) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int next_free_verse_number(const std::vector<std::string>& labels,
                           std::string_view type,
                           int preferred,
                           int except)
{
    const auto prefix = util::to_lower(type) + ':';
    std::array<bool, k_max_verse_number + 1> taken{};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto lower = util::to_lower(labels[i]);
        if (static_cast<int>(i) == except || !lower.starts_with(prefix)) {
            continue;
        }
        if (const auto n = first_colon_number(lower); n && *n <= k_max_verse_number) {
            taken[static_cast<std::size_t>(*n)] = true;
        }
    }
    const auto first_free = [&](int from) {
        int n = from;
        while (n <= k_max_verse_number && taken[static_cast<std::size_t>(n)]) {
            ++n;
        }
        return n;
    };
    const int up = first_free(clamp_verse_number(preferred));
    if (up <= k_max_verse_number) {
        return up;
    }
    const int wrapped = first_free(k_min_verse_number);
    return wrapped <= k_max_verse_number ? wrapped : -1;
}

std::vector<std::string> verse_order_tokens(std::string_view order)
{
    std::vector<std::string> out;
    for (const auto token : util::split_ws(order)) {
        out.push_back(util::to_upper(token));
    }
    return out;
}

}  // namespace seventhslide::parse
