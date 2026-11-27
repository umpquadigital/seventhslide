// Rich text: what an editor does to runs. Re-fitting them to the text after
// typing, styling a range and setting paragraph formats, sampling the style
// at the caret, and loading and saving an editor document.

#include <seventhslide/model/rich_text.hpp>

#include "rich_text_internal.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace seventhslide::model {
namespace {

/// ``prev`` index meaning "no line to inherit styling from".
constexpr std::size_t k_no_source = static_cast<std::size_t>(-1);

/// True when byte ``at`` of ``s`` continues a multi-byte UTF-8 sequence.
[[nodiscard]] bool is_utf8_continuation(std::string_view s, std::size_t at) noexcept
{
    return at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0U) == 0x80U;
}

/// Split on ``\n`` keeping empty parts. Empty input -> one empty line.
[[nodiscard]] std::vector<std::string> split_plain_lines(std::string_view text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return parts;
}

/// Append the byte range ``[from, to)`` of ``line``'s runs, styling preserved.
void append_run_slice(RichLine& out, const RichLine& line, std::size_t from, std::size_t to)
{
    std::size_t seen = 0;
    for (const TextSpan& sp : line) {
        const std::size_t start = seen;
        const std::size_t end = seen + sp.text.size();
        seen = end;
        const std::size_t a = std::max(from, start);
        const std::size_t b = std::min(to, end);
        if (b > a) {
            append_styled(out, std::string_view(sp.text).substr(a - start, b - a), sp.style);
        }
    }
}

/// Run holding the character before byte ``offset`` (the first run at offset 0).
[[nodiscard]] const TextSpan* run_at(const RichLine& line, std::size_t offset)
{
    if (line.empty()) {
        return nullptr;
    }
    const std::size_t at = offset > 0 ? offset - 1 : 0;
    std::size_t seen = 0;
    for (const TextSpan& sp : line) {
        if (at < seen + sp.text.size()) {
            return &sp;
        }
        seen += sp.text.size();
    }
    return &line.back();
}

} // namespace

RichLine reconcile_rich_line(const RichLine& prev, std::string_view next)
{
    const std::string old = plain_text(prev);
    if (old == next) {
        RichLine kept = prev;
        if (kept.empty()) {
            kept.push_back(TextSpan{});
        }
        return kept;
    }

    std::size_t head = 0;
    const std::size_t max_head = std::min(old.size(), next.size());
    while (head < max_head && old[head] == next[head]) {
        ++head;
    }
    // Never cut inside a UTF-8 sequence; both strings share these bytes.
    while (head > 0 && (is_utf8_continuation(next, head) || is_utf8_continuation(old, head))) {
        --head;
    }

    std::size_t tail = 0;
    const std::size_t max_tail = std::min(old.size() - head, next.size() - head);
    while (tail < max_tail && old[old.size() - 1 - tail] == next[next.size() - 1 - tail]) {
        ++tail;
    }
    while (tail > 0
           && (is_utf8_continuation(next, next.size() - tail)
               || is_utf8_continuation(old, old.size() - tail))) {
        --tail;
    }

    const std::size_t removed_end = old.size() - tail;
    RichLine out;
    append_run_slice(out, prev, 0, head);
    const std::string_view inserted = next.substr(head, next.size() - tail - head);
    if (!inserted.empty()) {
        // Typing over a selection takes the first replaced run's style; a plain
        // insertion takes the run to its left.
        const TextSpan* src = run_at(prev, removed_end > head ? head + 1 : head);
        append_styled(out, inserted, src != nullptr ? src->style : TextStyle{});
    }
    append_run_slice(out, prev, removed_end, old.size());
    if (out.empty()) {
        TextSpan sp;
        if (!prev.empty()) {
            sp.style = prev.front().style;
        }
        out.push_back(std::move(sp));
    }
    return out;
}

std::vector<RichLine> reconcile_rich_lines(const std::vector<RichLine>& prev,
                                            std::string_view text)
{
    const std::vector<std::string> parts = split_plain_lines(text);
    std::vector<std::string> prev_plain;
    prev_plain.reserve(prev.size());
    for (const RichLine& line : prev) {
        prev_plain.push_back(plain_text(line));
    }

    std::size_t head = 0;
    while (head < prev.size() && head < parts.size() && prev_plain[head] == parts[head]) {
        ++head;
    }
    std::size_t tail = 0;
    while (tail < prev.size() - head && tail < parts.size() - head
           && prev_plain[prev.size() - 1 - tail] == parts[parts.size() - 1 - tail]) {
        ++tail;
    }
    const std::size_t mid = prev.size() - head - tail;

    std::vector<RichLine> out;
    out.reserve(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        std::size_t src = k_no_source;
        if (i < head) {
            src = i;
        } else if (i >= parts.size() - tail) {
            src = prev.size() - (parts.size() - i);
        } else if (i - head < mid) {
            src = head + (i - head);
        } else if (mid > 0) {
            // More new lines than old in the changed run: they all grow out of
            // the last old line the edit touched.
            src = head + mid - 1;
        } else if (!prev.empty()) {
            src = head > 0 ? head - 1 : 0;
        }
        if (src == k_no_source) {
            TextSpan sp;
            sp.text = parts[i];
            out.push_back(RichLine{std::move(sp)});
        } else {
            out.push_back(reconcile_rich_line(prev[src], parts[i]));
        }
    }
    return out;
}

namespace {

/// Whether ``<name=value>`` is one whole tag the parser reads back with
/// ``value`` as it is, rather than as literal text or another value.
[[nodiscard]] bool tag_reads_back(std::string_view name, std::string_view value)
{
    std::string raw = "<";
    raw += name;
    raw += '=';
    raw += value;
    raw += '>';
    const auto tag = match_markup_tag(raw);
    return tag && !tag->close && tag->name == name && tag->length == raw.size()
           && tag->value == value;
}

void apply_span_attrs(TextStyle& style, const SpanAttrs& attrs)
{
    if (attrs.size_pct) {
        style.size_pct = clamp_size_pct(*attrs.size_pct);
    }
    if (attrs.bold) {
        style.bold = *attrs.bold;
    }
    if (attrs.italic) {
        style.italic = *attrs.italic;
    }
    if (attrs.underline) {
        style.underline = *attrs.underline;
    }
    // A colour or family the markup cannot carry is left out, as its tag
    // would read back as literal text.
    if (attrs.color
        && (attrs.color->empty() || tag_reads_back("color", canonical_color_tag(*attrs.color)))) {
        style.color = *attrs.color;
    }
    if (attrs.font_family
        && (attrs.font_family->empty() || tag_reads_back("font", *attrs.font_family))) {
        style.font_family = *attrs.font_family;
    }
    if (attrs.baseline_rise_pct) {
        style.baseline_rise_pct = clamp_rise_pct(*attrs.baseline_rise_pct);
    }
}

[[nodiscard]] RichLine ensure_spans(RichLine line)
{
    if (line.empty()) {
        line.push_back(TextSpan{});
    }
    return line;
}

/// Style ``[start, end)`` of one line (UTF-8 byte offsets into ``plain_text``).
[[nodiscard]] RichLine apply_attrs_to_range(RichLine line, std::size_t start, std::size_t end,
                                            const SpanAttrs& attrs)
{
    line = ensure_spans(std::move(line));
    const std::string plain = plain_text(line);
    const std::size_t a = std::min(start, plain.size());
    const std::size_t b = std::max(a, std::min(end, plain.size()));
    if (a >= b) {
        return line;
    }
    RichLine out;
    append_run_slice(out, line, 0, a);
    std::size_t seen = 0;
    for (const TextSpan& sp : line) {
        const std::size_t from = std::max(a, seen);
        const std::size_t to = std::min(b, seen + sp.text.size());
        if (to > from) {
            TextStyle styled = sp.style;
            apply_span_attrs(styled, attrs);
            append_styled(out, std::string_view(sp.text).substr(from - seen, to - from), styled);
        }
        seen += sp.text.size();
    }
    append_run_slice(out, line, b, plain.size());
    return out;
}

} // namespace

std::vector<RichLine> apply_attrs_to_all(std::vector<RichLine> lines, const SpanAttrs& attrs)
{
    if (lines.empty()) {
        lines.push_back(RichLine{TextSpan{}});
    }
    for (auto& line : lines) {
        line = ensure_spans(std::move(line));
        const std::string plain = plain_text(line);
        if (plain.empty()) {
            apply_span_attrs(line.front().style, attrs);
        } else {
            line = apply_attrs_to_range(std::move(line), 0, plain.size(), attrs);
        }
    }
    return lines;
}

std::vector<RichLine> apply_attrs_to_selection(std::vector<RichLine> lines, std::size_t start,
                                               std::size_t end, const SpanAttrs& attrs)
{
    if (end <= start) {
        return lines;
    }
    if (lines.empty()) {
        lines.push_back(RichLine{TextSpan{}});
    }
    std::size_t pos = 0;
    for (auto& line : lines) {
        const std::string plain = plain_text(line);
        const std::size_t line_start = pos;
        const std::size_t line_end = pos + plain.size();
        const std::size_t sel_a = std::max(start, line_start);
        const std::size_t sel_b = std::min(end, line_end);
        if (sel_a < sel_b) {
            line = apply_attrs_to_range(std::move(line), sel_a - line_start, sel_b - line_start,
                                        attrs);
        }
        pos = line_end + 1;
    }
    return lines;
}

namespace {

/// Calls ``fn(i, first, last)`` for each line: its UTF-8 range in
/// ``plain_text_lines``, newline left out.
template <class Lines, class Fn>
void for_each_line_range(Lines& lines, Fn&& fn)
{
    std::size_t at = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::size_t last = at + plain_text(lines[i]).size();
        fn(i, at, last);
        at = last + 1;
    }
}

}  // namespace

std::vector<RichLine> apply_line_format(std::vector<RichLine> lines,
                                        std::size_t start,
                                        std::size_t end,
                                        const LineFormatAttrs& attrs)
{
    for_each_line_range(lines, [&](std::size_t i, std::size_t first, std::size_t last) {
        // A selection ending just where a paragraph starts does not touch it.
        const bool touches = start == end ? first <= start && start <= last
                                          : first < end && start <= last;
        if (!touches) {
            return;
        }
        auto& line = lines[i];
        if (line.empty()) {
            line.push_back(TextSpan{});
        }
        for (auto& span : line) {
            auto& f = span.style.line;
            if (attrs.align) {
                f.align = *attrs.align;
            }
            if (attrs.before_pct) {
                f.before_pct = *attrs.before_pct;
            }
            if (attrs.after_pct) {
                f.after_pct = *attrs.after_pct;
            }
            if (attrs.indent_pct) {
                f.indent_pct = *attrs.indent_pct;
            }
            if (attrs.line_pct) {
                f.line_pct = *attrs.line_pct;
            }
        }
    });
    return lines;
}

LineFormat line_format_at_offset(const std::vector<RichLine>& lines, std::size_t offset)
{
    LineFormat out;
    for_each_line_range(lines, [&](std::size_t i, std::size_t first, std::size_t last) {
        if (first <= offset && offset <= last && !lines[i].empty()) {
            out = lines[i].front().style.line;
        }
    });
    return out;
}

RunStyle style_at_offset(const std::vector<RichLine>& lines, std::size_t offset)
{
    RunStyle out;
    if (lines.empty()) {
        return out;
    }
    std::size_t pos = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string plain = plain_text(lines[i]);
        const std::size_t line_end = pos + plain.size();
        if (offset <= line_end || i + 1 == lines.size()) {
            const std::size_t at =
                std::min(plain.size(), offset > pos ? offset - pos : std::size_t{0});
            std::size_t seen = 0;
            const RichLine empty{TextSpan{}};
            const RichLine& line = lines[i].empty() ? empty : lines[i];
            for (std::size_t j = 0; j < line.size(); ++j) {
                const auto& sp = line[j];
                const std::size_t next = seen + sp.text.size();
                if (at < next || j + 1 == line.size()) {
                    out.size_pct = sp.style.size_pct;
                    out.bold = sp.style.bold;
                    out.italic = sp.style.italic;
                    out.underline = sp.style.underline;
                    out.color = sp.style.color;
                    out.font_family = sp.style.font_family;
                    out.baseline_rise_pct = sp.style.baseline_rise_pct;
                    return out;
                }
                seen = next;
            }
            return out;
        }
        pos = line_end + 1;
    }
    return out;
}

std::vector<RichLine> parse_editor_lines(std::string_view markup, bool box_bold, bool box_italic,
                                         bool box_underline)
{
    auto lines = parse_markup_lines(markup);
    bool styled = false;
    for (const auto& line : lines) {
        for (const auto& sp : line) {
            if (sp.style.has_inline_style()) {
                styled = true;
                break;
            }
        }
        if (styled) {
            break;
        }
    }
    for (auto& line : lines) {
        if (line.empty()) {
            TextSpan empty;
            empty.style.bold = styled ? false : box_bold;
            empty.style.italic = styled ? false : box_italic;
            empty.style.underline = styled ? false : box_underline;
            line.push_back(std::move(empty));
        } else if (!styled) {
            for (auto& sp : line) {
                sp.style.bold = box_bold;
                sp.style.italic = box_italic;
                sp.style.underline = box_underline;
            }
        }
    }
    return lines;
}

EditorObjectText format_editor_object(const std::vector<RichLine>& lines)
{
    EditorObjectText out;
    bool first = true;
    bool uniform_bold = false;
    bool uniform_italic = false;
    bool uniform_underline = false;
    bool mixed = false;
    for (const auto& line : lines) {
        for (const auto& sp : line) {
            if (sp.style.line.any()) {
                mixed = true;
            }
            if (sp.text.empty()) {
                continue;
            }
            if (sp.style.size_pct != 100 || !sp.style.color.empty()
                || !sp.style.font_family.empty() || sp.style.baseline_rise_pct != 0
                || sp.style.margin_for_space) {
                mixed = true;
            }
            if (first) {
                uniform_bold = sp.style.bold;
                uniform_italic = sp.style.italic;
                uniform_underline = sp.style.underline;
                first = false;
            } else if (sp.style.bold != uniform_bold || sp.style.italic != uniform_italic
                       || sp.style.underline != uniform_underline) {
                mixed = true;
            }
        }
    }
    if (mixed) {
        out.text = format_markup_lines(lines);
        out.mixed = true;
        return out;
    }
    out.text = plain_text_lines(lines);
    out.bold = !first && uniform_bold;
    out.italic = !first && uniform_italic;
    out.underline = !first && uniform_underline;
    return out;
}

} // namespace seventhslide::model
