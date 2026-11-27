// Rich text: the inline markup's grammar. Reading and writing the tags
// (``<b>``, ``<size=NN>``, ``<p=...>``...) and their entities, line formats,
// and the plain text of runs. Song-file import is rich_text_import.cpp, and
// the editor's operations on runs (re-fitting them after typing, styling a
// range, loading and saving an editor document) are rich_text_edit.cpp.

#include <seventhslide/model/rich_text.hpp>

#include "rich_text_internal.hpp"

#include <seventhslide/util/strings.hpp>

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <sstream>
#include <utility>

namespace seventhslide::model {
namespace {

[[nodiscard]] bool is_hex_digit(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/// ``<color=#RGB>`` / ``#RRGGBB`` / ``#RGBA`` / ``#RRGGBBAA``.
[[nodiscard]] std::optional<std::string> match_color_open(std::string_view raw_tag)
{
    if (raw_tag.size() < 11 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        return std::nullopt;
    }
    const std::string lower = util::to_lower(raw_tag);
    if (!util::starts_with_ci(lower, "<color=#")) {
        return std::nullopt;
    }
    const std::size_t hex_start = 8; // after "<color=#"
    if (hex_start >= lower.size() - 1) {
        return std::nullopt;
    }
    std::size_t i = hex_start;
    while (i < lower.size() - 1 && is_hex_digit(lower[i])) {
        ++i;
    }
    const std::size_t digits = i - hex_start;
    if ((digits != 3 && digits != 4 && digits != 6 && digits != 8)
        || i != lower.size() - 1) {
        return std::nullopt;
    }
    return "#" + lower.substr(hex_start, digits);
}

/// ``<name=NN>`` with NN = 1-3 digits; ``open`` is ``"<name="``.
[[nodiscard]] std::optional<int> match_number_open(std::string_view raw_tag,
                                                   std::string_view open)
{
    if (raw_tag.size() < open.size() + 2 || raw_tag.back() != '>'
        || !util::starts_with_ci(raw_tag, open)) {
        return std::nullopt;
    }
    const auto digits = raw_tag.substr(open.size(), raw_tag.size() - open.size() - 1);
    if (digits.size() > 3) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

/// ``<size=NN>`` with NN = 1-3 digits.
[[nodiscard]] std::optional<int> match_size_open(std::string_view raw_tag)
{
    return match_number_open(raw_tag, "<size=");
}

/// ``<rise=NN>`` with NN = 1-3 digits (percent of the box font size).
[[nodiscard]] std::optional<int> match_rise_open(std::string_view raw_tag)
{
    return match_number_open(raw_tag, "<rise=");
}

/// ``<gap=NN>`` with NN = 1-3 digits (percent of the run's own size).
[[nodiscard]] std::optional<int> match_gap_open(std::string_view raw_tag)
{
    return match_number_open(raw_tag, "<gap=");
}

/// ``<font=Name>`` - 1-64 chars, no ``<>&``. Quotes around the name are optional.
[[nodiscard]] std::optional<std::string> match_font_open(std::string_view raw_tag)
{
    if (raw_tag.size() < 8 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        return std::nullopt;
    }
    if (!util::starts_with_ci(raw_tag, "<font=")) {
        return std::nullopt;
    }
    std::string_view name = util::trim_view(raw_tag.substr(6, raw_tag.size() - 7));
    if (name.size() >= 2
        && ((name.front() == '"' && name.back() == '"')
            || (name.front() == '\'' && name.back() == '\''))) {
        name = util::trim_view(name.substr(1, name.size() - 2));
    }
    if (name.empty() || name.size() > 64) {
        return std::nullopt;
    }
    for (char c : name) {
        if (c == '<' || c == '>' || c == '&') {
            return std::nullopt;
        }
    }
    return std::string(name);
}

struct StyleState {
    int bold = 0;
    int italic = 0;
    int underline = 0;
    int super = 0;
    std::vector<int> size_stack{100};
    std::vector<int> rise_stack{0};
    std::vector<int> gap_stack{-1};  // -1: no gap asked for
    std::vector<std::string> color_stack{""};
    std::vector<std::string> font_stack{""};

    [[nodiscard]] TextStyle current() const noexcept
    {
        TextStyle s;
        s.bold = bold > 0;
        s.italic = italic > 0;
        s.underline = underline > 0;
        s.size_pct = size_stack.empty() ? 100 : size_stack.back();
        s.color = color_stack.empty() ? "" : color_stack.back();
        s.font_family = font_stack.empty() ? "" : font_stack.back();
        if (!gap_stack.empty() && gap_stack.back() >= 0) {
            s.margin_right_pct = gap_stack.back();
            s.margin_for_space = true;
        }
        const int rise = rise_stack.empty() ? 0 : rise_stack.back();
        if (rise > 0) {
            s.baseline_rise_pct = rise;
        } else if (super > 0) {
            s.baseline_rise_pct = k_super_rise_pct;
        }
        return s;
    }

    void apply_open(std::string_view base, std::optional<int> size)
    {
        if (size) {
            size_stack.push_back(clamp_size_pct(*size));
            return;
        }
        if (base == "b") {
            ++bold;
        } else if (base == "i") {
            ++italic;
        } else if (base == "u") {
            ++underline;
        } else if (base == "super") {
            ++super;
        }
    }

    void apply_rise(int pct)
    {
        rise_stack.push_back(clamp_rise_pct(pct));
    }

    void apply_gap(int pct)
    {
        gap_stack.push_back(std::clamp(pct, 0, k_max_gap_pct));
    }

    void apply_color(std::string hex)
    {
        color_stack.push_back(std::move(hex));
    }

    void apply_font(std::string family)
    {
        font_stack.push_back(std::move(family));
    }

    void apply_close(std::string_view base)
    {
        if (base == "size") {
            if (size_stack.size() > 1) {
                size_stack.pop_back();
            }
            return;
        }
        if (base == "color") {
            if (color_stack.size() > 1) {
                color_stack.pop_back();
            }
            return;
        }
        if (base == "font") {
            if (font_stack.size() > 1) {
                font_stack.pop_back();
            }
            return;
        }
        if (base == "rise") {
            if (rise_stack.size() > 1) {
                rise_stack.pop_back();
            }
            return;
        }
        if (base == "gap") {
            if (gap_stack.size() > 1) {
                gap_stack.pop_back();
            }
            return;
        }
        if (base == "b" && bold > 0) {
            --bold;
        } else if (base == "i" && italic > 0) {
            --italic;
        } else if (base == "u" && underline > 0) {
            --underline;
        } else if (base == "super" && super > 0) {
            --super;
        }
    }
};

/// Wrap one span in canonical tags (color -> font -> size -> super/rise -> gap -> b -> i -> u).
/// Verbose but stable for parse<->format span equality; storage may keep
/// editor-authored nesting.
void append_wrapped_span(std::string& out, const TextSpan& span)
{
    if (span.text.empty()) {
        return;
    }
    const std::string color = canonical_color_tag(span.style.color);
    if (!color.empty()) {
        out += "<color=";
        out += color;
        out += ">";
    }
    if (!span.style.font_family.empty()) {
        out += "<font=";
        out += span.style.font_family;
        out += ">";
    }
    if (span.style.size_pct != 100) {
        out += "<size=";
        out += std::to_string(span.style.size_pct);
        out += ">";
    }
    const bool super = span.style.baseline_rise_pct == k_super_rise_pct;
    const bool rise = span.style.baseline_rise_pct != 0 && !super;
    if (super) {
        out += "<super>";
    } else if (rise) {
        out += "<rise=";
        out += std::to_string(span.style.baseline_rise_pct);
        out += ">";
    }
    if (span.style.margin_for_space) {
        out += "<gap=";
        out += std::to_string(std::clamp(span.style.margin_right_pct, 0, k_max_gap_pct));
        out += ">";
    }
    if (span.style.bold) {
        out += "<b>";
    }
    if (span.style.italic) {
        out += "<i>";
    }
    if (span.style.underline) {
        out += "<u>";
    }
    out += escape_markup_text(span.text);
    if (span.style.underline) {
        out += "</u>";
    }
    if (span.style.italic) {
        out += "</i>";
    }
    if (span.style.bold) {
        out += "</b>";
    }
    if (span.style.margin_for_space) {
        out += "</gap>";
    }
    if (super) {
        out += "</super>";
    } else if (rise) {
        out += "</rise>";
    }
    if (span.style.size_pct != 100) {
        out += "</size>";
    }
    if (!span.style.font_family.empty()) {
        out += "</font>";
    }
    if (!color.empty()) {
        out += "</color>";
    }
}

} // namespace

int clamp_size_pct(int raw) noexcept
{
    if (raw < 10) {
        return 10;
    }
    if (raw > 400) {
        return 400;
    }
    return raw;
}

int clamp_rise_pct(int raw) noexcept
{
    if (raw < 0) {
        return 0;
    }
    if (raw > 100) {
        return 100;
    }
    return raw;
}

bool TextStyle::has_inline_style() const noexcept
{
    return bold || italic || underline || size_pct != 100 || size_px > 0.0
        || baseline_rise_pct != 0 || !color.empty() || !font_family.empty() || margin_right_pct != 0
        || margin_for_space;
}

std::string format_line_format(const LineFormat& format)
{
    std::string out;
    const auto add = [&out](std::string_view key, const std::string& value) {
        if (!out.empty()) {
            out.push_back(',');
        }
        out += key;
        out.push_back(':');
        out += value;
    };
    if (!format.align.empty()) {
        add("align", format.align);
    }
    if (format.before_pct >= 0) {
        add("before", std::to_string(format.before_pct));
    }
    if (format.after_pct >= 0) {
        add("after", std::to_string(format.after_pct));
    }
    if (format.indent_pct >= 0) {
        add("indent", std::to_string(format.indent_pct));
    }
    if (format.line_pct > 0) {
        add("line", std::to_string(format.line_pct));
    }
    return out;
}

std::optional<LineFormat> parse_line_format(std::string_view value)
{
    LineFormat out;
    while (!value.empty()) {
        const auto comma = value.find(',');
        const std::string_view item = value.substr(0, comma);
        value = comma == std::string_view::npos ? std::string_view{} : value.substr(comma + 1);
        const auto colon = item.find(':');
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view key = item.substr(0, colon);
        const std::string_view val = item.substr(colon + 1);
        if (key == "align") {
            if (val != "left" && val != "center" && val != "right") {
                return std::nullopt;
            }
            out.align = std::string(val);
            continue;
        }
        int number = 0;
        const auto [end, ec] = std::from_chars(val.data(), val.data() + val.size(), number);
        if (ec != std::errc{} || end != val.data() + val.size() || number < 0) {
            return std::nullopt;
        }
        number = std::min(number, k_max_line_format_pct);
        if (key == "before") {
            out.before_pct = number;
        } else if (key == "after") {
            out.after_pct = number;
        } else if (key == "indent") {
            out.indent_pct = number;
        } else if (key == "line") {
            out.line_pct = number;
        } else {
            return std::nullopt;
        }
    }
    return out;
}

std::optional<MarkupTag> match_markup_tag(std::string_view text)
{
    if (text.starts_with("<p=")) {
        const auto close = text.substr(0, 128).find('>');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view value = text.substr(3, close - 3);
        if (!parse_line_format(value)) {
            return std::nullopt;
        }
        MarkupTag tag;
        tag.name = "p";
        tag.value = std::string(value);
        tag.length = close + 1;
        return tag;
    }
    if (text.size() < 3 || text.front() != '<') {
        return std::nullopt;
    }
    // Every tag the grammar accepts is far shorter than this (the longest is
    // a quoted 64-character font name), so a '<' is only ever looked past this
    // far. Without the bound, text with many stray '<' was quadratic to scan.
    constexpr std::size_t k_max_tag_bytes = 128;
    const std::size_t gt = text.substr(0, k_max_tag_bytes).find('>');
    if (gt == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view raw = text.substr(0, gt + 1);
    MarkupTag tag;
    tag.length = raw.size();

    if (raw[1] == '/') {
        const std::string name = util::to_lower(raw.substr(2, raw.size() - 3));
        if (name == "size" || name == "rise" || name == "gap" || name == "color"
            || name == "font") {
            tag.name = name;
        } else {
            tag.name = canonical_style_name(name);
        }
        if (tag.name.empty()) {
            return std::nullopt;
        }
        tag.close = true;
        return tag;
    }
    if (const auto size = match_size_open(raw)) {
        tag.name = "size";
        tag.number = *size;
        return tag;
    }
    if (const auto rise = match_rise_open(raw)) {
        tag.name = "rise";
        tag.number = *rise;
        return tag;
    }
    if (const auto gap = match_gap_open(raw)) {
        tag.name = "gap";
        tag.number = *gap;
        return tag;
    }
    if (auto color = match_color_open(raw)) {
        tag.name = "color";
        tag.value = std::move(*color);
        return tag;
    }
    if (auto font = match_font_open(raw)) {
        tag.name = "font";
        tag.value = std::move(*font);
        return tag;
    }
    tag.name = canonical_style_name(raw.substr(1, raw.size() - 2));
    if (tag.name.empty()) {
        return std::nullopt;
    }
    return tag;
}

std::string escape_markup_text(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const std::string_view rest = text.substr(i);
        if (text[i] == '<' && match_markup_tag(rest)) {
            out += "&lt;";
        } else if (text[i] == '&' && markup_entity(rest)) {
            out += "&amp;";
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

RichLine parse_inline_markup(std::string_view text)
{
    RichLine line;
    StyleState state;
    std::string pending;
    LineFormat format;

    for (std::size_t i = 0; i < text.size();) {
        const std::string_view rest = text.substr(i);
        if (text[i] == '<') {
            if (const auto tag = match_markup_tag(rest)) {
                if (tag->name == "p") {
                    // A paragraph's own settings, at its start; anywhere
                    // else it means nothing.
                    if (i == 0) {
                        format = parse_line_format(tag->value).value_or(LineFormat{});
                    }
                    i += tag->length;
                    continue;
                }
                append_styled(line, pending, state.current());
                pending.clear();
                if (tag->close) {
                    state.apply_close(tag->name);
                } else if (tag->name == "size") {
                    state.apply_open("size", tag->number);
                } else if (tag->name == "rise") {
                    state.apply_rise(tag->number);
                } else if (tag->name == "gap") {
                    state.apply_gap(tag->number);
                } else if (tag->name == "color") {
                    state.apply_color(tag->value);
                } else if (tag->name == "font") {
                    state.apply_font(tag->value);
                } else {
                    state.apply_open(tag->name, std::nullopt);
                }
                i += tag->length;
                continue;
            }
        } else if (text[i] == '&') {
            if (const auto entity = markup_entity(rest)) {
                pending.push_back(entity->first);
                i += entity->second;
                continue;
            }
        }
        pending.push_back(text[i]);
        ++i;
    }
    append_styled(line, pending, state.current());
    if (format.any()) {
        if (line.empty()) {
            line.push_back(TextSpan{});
        }
        for (auto& span : line) {
            span.style.line = format;
        }
    }
    return line;
}

std::string format_inline_markup(const RichLine& line)
{
    std::string out;
    if (!line.empty() && line.front().style.line.any()) {
        out += "<p=" + format_line_format(line.front().style.line) + ">";
    }
    for (const TextSpan& span : line) {
        append_wrapped_span(out, span);
    }
    return out;
}

std::vector<RichLine> parse_markup_lines(std::string_view text)
{
    // Every newline ends a line, so a trailing one leaves an empty last line
    // and empty text is one empty line.
    std::vector<RichLine> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back(parse_inline_markup(text.substr(start)));
            return lines;
        }
        lines.push_back(parse_inline_markup(text.substr(start, nl - start)));
        start = nl + 1;
    }
}

std::string format_markup_lines(const std::vector<RichLine>& lines)
{
    std::ostringstream oss;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            oss << '\n';
        }
        oss << format_inline_markup(lines[i]);
    }
    return oss.str();
}

std::string plain_text(const RichLine& line)
{
    std::string out;
    for (const TextSpan& span : line) {
        out += span.text;
    }
    return out;
}

std::string strip_markup_tags(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const std::string_view rest = text.substr(i);
        if (text[i] == '<') {
            if (const auto tag = match_markup_tag(rest)) {
                i += tag->length;
                continue;
            }
        } else if (text[i] == '&') {
            if (const auto entity = markup_entity(rest)) {
                out.push_back(entity->first);
                i += entity->second;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

bool is_forced_break_line(const RichLine& line)
{
    const std::string plain = plain_text(line);
    return is_forced_split_marker(util::trim_view(plain));
}

std::string plain_text_lines(const std::vector<RichLine>& lines)
{
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out.push_back('\n');
        }
        out += plain_text(lines[i]);
    }
    return out;
}

} // namespace seventhslide::model
