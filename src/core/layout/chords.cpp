#include "seventhslide/layout/chords.hpp"

#include "seventhslide/layout/word_wrap.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace seventhslide::layout {
namespace {

[[nodiscard]] ChordCell make_cell(
    const std::optional<std::string>& chord,
    std::string syllable,
    const model::TextStyle& style)
{
    ChordCell cell;
    cell.syllable = std::move(syllable);
    cell.style = style;
    if (chord) {
        cell.chord = format_chord_accidentals(*chord);
    }
    return cell;
}

}  // namespace

ChordStyle clamped(ChordStyle style) noexcept
{
    style.size_pct = std::clamp(style.size_pct, k_min_chord_size_pct, k_max_chord_size_pct);
    style.gap_pct = std::clamp(style.gap_pct, 0, k_max_chord_gap_pct);
    return style;
}

FontStyle chord_font(const FontStyle& lyric, const ChordStyle& style)
{
    FontStyle font = lyric;
    font.size_px = lyric.size_px * clamped(style).size_pct / 100.0;
    font.bold = true;
    return font;
}

int chord_row_px(ITextMeasurer& measurer, const FontStyle& lyric, const ChordStyle& style)
{
    const double gap = lyric.size_px * clamped(style).gap_pct / 100.0;
    const double cap = measurer.font_metrics(chord_font(lyric, style)).cap_height;
    return static_cast<int>(std::ceil(cap + gap));
}

ChordPlacement place_chord_items(std::span<const ChordLineItem> items,
                                 const FontStyle& lyric,
                                 const FontStyle& chord,
                                 ITextMeasurer& measurer)
{
    ChordPlacement out;
    const double chord_gap = measurer.measure_width(" ", chord);
    const double space_w = measurer.measure_width(" ", lyric);
    out.x.reserve(items.size());
    double x = 0.0;
    // Where the chord row is free again for the next chord.
    double chords_free_at = 0.0;
    double chord_row_end = 0.0;
    // Whether the lyric just before ``x`` is part of a word.
    bool in_word = false;
    for (const auto& item : items) {
        if (item.kind == ChordItemKind::Space) {
            out.x.push_back(x);
            x += space_w;
            in_word = false;
            continue;
        }
        const auto& syllable = item.cell.syllable;
        const auto& name = item.cell.chord;
        if (name && !name->empty()) {
            const double word_end = x;
            x = std::max(x, chords_free_at);
            if (in_word && !syllable.empty() && x - word_end >= space_w) {
                const double hyphen_w = measurer.measure_width("-", lyric);
                out.hyphens.push_back((word_end + x - hyphen_w) * 0.5);
            }
            chord_row_end = x + measurer.measure_width(*name, chord);
            chords_free_at = chord_row_end + chord_gap;
        }
        out.x.push_back(x);
        auto cell_style = item.cell.style;
        cell_style.size_pct = 100;
        x += measurer.measure_width(syllable, span_font_style(lyric, cell_style));
        in_word = in_word || !syllable.empty();
    }
    out.width = std::max(x, chord_row_end);
    return out;
}

std::vector<WrapRun> wrap_chord_items(std::span<const ChordLineItem> items,
                                      const FontStyle& lyric,
                                      const FontStyle& chord,
                                      ITextMeasurer& measurer,
                                      double max_width)
{
    auto skip_spaces = [&](std::size_t k) {
        while (k < items.size() && items[k].kind == ChordItemKind::Space) {
            ++k;
        }
        return k;
    };
    // The cells from ``k`` to the next space, and whether one has a syllable.
    auto word_from = [&](std::size_t k) {
        bool syllable = false;
        while (k < items.size() && items[k].kind == ChordItemKind::Cell) {
            syllable = syllable || !items[k].cell.syllable.empty();
            ++k;
        }
        return std::pair{k, syllable};
    };

    auto width = [&](std::size_t begin, std::size_t end) {
        return place_chord_items(items.subspan(begin, end - begin), lyric, chord, measurer).width;
    };

    std::vector<WrapRun> rows;
    std::size_t i = skip_spaces(0);
    while (i < items.size()) {
        auto [word_end, syllable] = word_from(i);
        if (word_end > i + 1 && width(i, word_end) > max_width) {
            // Too wide for any row: it breaks where its cells do.
            word_end = i + 1;
        } else if (!syllable) {
            // A chord alone before a word is played going into it: it starts
            // the word's row rather than ending the row before over nothing,
            // where the two fit a row.
            const std::size_t next = skip_spaces(word_end);
            if (const auto [next_end, next_syllable] = word_from(next);
                next_syllable && width(i, next_end) <= max_width) {
                word_end = next_end;
            }
        }
        if (rows.empty() || width(rows.back().start, word_end) > max_width) {
            rows.push_back({i, word_end});
        } else {
            rows.back().end = word_end;
        }
        i = skip_spaces(word_end);
    }
    return rows;
}

void cut_wide_syllables(std::vector<ChordLineItem>& items,
                        const FontStyle& lyric,
                        ITextMeasurer& measurer,
                        double max_width)
{
    std::vector<ChordLineItem> out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        std::vector<std::string> pieces;
        if (item.kind == ChordItemKind::Cell && !item.cell.syllable.empty()) {
            auto style = item.cell.style;
            style.size_pct = 100;
            const auto font = span_font_style(lyric, style);
            if (measurer.measure_width(item.cell.syllable, font) > max_width) {
                pieces = cut_text_to_width(
                    item.cell.syllable,
                    [&](std::string_view text) { return measurer.measure_width(text, font); },
                    max_width);
            }
        }
        if (pieces.size() <= 1) {
            if (!out.empty()) {
                out.push_back(item);
            }
            continue;
        }
        if (out.empty()) {
            out.assign(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(i));
        }
        for (std::size_t k = 0; k < pieces.size(); ++k) {
            ChordCell cell = item.cell;
            cell.syllable = std::move(pieces[k]);
            if (k > 0) {
                cell.chord.reset();
            }
            out.push_back(ChordLineItem::make_cell(std::move(cell)));
        }
    }
    // Nothing was cut unless something is in ``out``: the first cut copies
    // everything before it, the rest follows.
    if (!out.empty()) {
        items = std::move(out);
    }
}

ChordRows wrap_chord_line(ChordLine line,
                          const FontStyle& lyric,
                          const ChordStyle& style,
                          ITextMeasurer& measurer,
                          double max_width)
{
    ChordRows out;
    out.line = std::move(line);
    cut_wide_syllables(out.line.items, lyric, measurer, max_width);
    out.rows = wrap_chord_items(out.line.items, lyric, chord_font(lyric, style), measurer, max_width);
    return out;
}

ChordLine chord_line_part(const ChordLine& line, WrapRun run)
{
    run.end = std::min(run.end, line.items.size());
    ChordLine out;
    if (run.start < run.end) {
        const auto first = line.items.begin() + static_cast<std::ptrdiff_t>(run.start);
        out.items.assign(first, first + static_cast<std::ptrdiff_t>(run.end - run.start));
    }
    return out;
}

model::RichLine chord_items_lyric(std::span<const ChordLineItem> items)
{
    static const model::TextStyle k_plain;
    model::RichLine out;
    const ChordCell* last = nullptr;
    bool gap = false;
    for (const auto& item : items) {
        if (item.kind == ChordItemKind::Space) {
            gap = true;
            continue;
        }
        if (item.cell.syllable.empty()) {
            continue;
        }
        if (gap && last != nullptr) {
            // Between two cells of one style a space is in that style
            // (``<b>one two</b>``); anywhere else it is plain.
            model::append_styled(out, " ",
                                 last->style == item.cell.style ? item.cell.style : k_plain);
        }
        model::append_styled(out, item.cell.syllable, item.cell.style);
        last = &item.cell;
        gap = false;
    }
    return out;
}

bool has_chords(std::string_view text) noexcept
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '[') {
            const auto end = text.find(']', i + 1);
            if (end != std::string_view::npos) {
                return true;
            }
        }
    }
    return false;
}

std::string strip_chords(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '[') {
            const auto end = text.find(']', i + 1);
            if (end != std::string_view::npos) {
                i = end + 1;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

std::string lyric_without_chords(std::string_view raw_line)
{
    std::string s = strip_chords(raw_line);
    // Collapse runs of ASCII spaces to one.
    std::string collapsed;
    collapsed.reserve(s.size());
    bool prev_space = false;
    for (char c : s) {
        if (c == ' ') {
            if (!prev_space) {
                collapsed.push_back(' ');
            }
            prev_space = true;
        } else {
            collapsed.push_back(c);
            prev_space = false;
        }
    }
    // Strip ends.
    std::size_t b = 0;
    while (b < collapsed.size() && util::is_space(collapsed[b])) {
        ++b;
    }
    std::size_t e = collapsed.size();
    while (e > b && util::is_space(collapsed[e - 1])) {
        --e;
    }
    return collapsed.substr(b, e - b);
}

std::string lyric_with_chords(std::string_view raw_line)
{
    const auto taken = take_chord_marks(raw_line);
    std::string lyric = lyric_without_chords(raw_line);
    if (taken.marks.empty()) {
        return lyric;
    }
    // Collapsing and trimming only drop spaces, so walking the two plain texts
    // side by side says where each character of the first ended up.
    const std::string before = model::plain_text_lines(model::parse_markup_lines(taken.markup));
    const std::string after = model::plain_text_lines(model::parse_markup_lines(lyric));
    std::vector<std::size_t> moved(before.size() + 1, after.size());
    std::size_t j = 0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        moved[i] = std::min(j, after.size());
        if (j < after.size() && before[i] == after[j]) {
            ++j;
        }
    }
    auto marks = taken.marks;
    for (auto& mark : marks) {
        mark.at = moved[std::min(mark.at, before.size())];
    }
    return put_chord_marks(lyric, marks);
}

ChordMarks take_chord_marks(std::string_view markup)
{
    ChordMarks out;
    out.markup = std::string(markup);
    if (!has_chords(markup)) {
        return out;
    }
    auto lines = model::parse_markup_lines(markup);
    std::size_t plain = 0;
    for (auto& line : lines) {
        for (auto& span : line) {
            const std::string& text = span.text;
            std::string kept;
            kept.reserve(text.size());
            for (std::size_t i = 0; i < text.size();) {
                if (text[i] == '[') {
                    const auto end = text.find(']', i + 1);
                    if (end != std::string::npos) {
                        out.marks.push_back({plain + kept.size(), text.substr(i + 1, end - i - 1)});
                        i = end + 1;
                        continue;
                    }
                }
                kept.push_back(text[i]);
                ++i;
            }
            plain += kept.size();
            span.text = std::move(kept);
        }
        std::erase_if(line, [](const model::TextSpan& span) { return span.text.empty(); });
    }
    if (!out.marks.empty()) {
        out.markup = model::format_markup_lines(lines);
    }
    return out;
}

std::string put_chord_marks(std::string_view markup, const std::vector<ChordMark>& marks)
{
    if (marks.empty()) {
        return std::string(markup);
    }
    auto sorted = marks;
    std::ranges::stable_sort(sorted, {}, &ChordMark::at);
    auto lines = model::parse_markup_lines(markup);
    if (lines.empty()) {
        lines.emplace_back();
    }
    auto marker = [](const ChordMark& mark) { return "[" + mark.chord + "]"; };
    std::size_t next = 0;
    std::size_t plain = 0;
    for (auto& line : lines) {
        for (auto& span : line) {
            std::string text;
            text.reserve(span.text.size() + 8);
            for (std::size_t i = 0; i < span.text.size(); ++i) {
                while (next < sorted.size() && sorted[next].at <= plain + i) {
                    text += marker(sorted[next++]);
                }
                text.push_back(span.text[i]);
            }
            plain += span.text.size();
            span.text = std::move(text);
        }
    }
    // What is left goes after the text, in the last run's style.
    auto& last = lines.back();
    if (last.empty()) {
        last.push_back({});
    }
    while (next < sorted.size()) {
        last.back().text += marker(sorted[next++]);
    }
    return model::format_markup_lines(lines);
}

std::string format_chord_accidentals(std::string_view chord)
{
    std::string out;
    out.reserve(chord.size() * 3);
    for (char c : chord) {
        if (c == 'b') {
            out += "\u266D";  // ♭
        } else if (c == '#') {
            out += "\u266F";  // ♯
        } else {
            out.push_back(c);
        }
    }
    return out;
}

ChordLine parse_chord_line(std::string_view line)
{
    // The one markup parser decides what is a tag: every tag styles the
    // syllables it covers and none is shown as text. ``span_of`` gives each
    // byte of the plain text the span (and so the style) it came from.
    const auto spans = model::parse_inline_markup(line);
    std::string text;
    std::vector<std::size_t> span_of;
    for (std::size_t k = 0; k < spans.size(); ++k) {
        text += spans[k].text;
        span_of.insert(span_of.end(), spans[k].text.size(), k);
    }
    static const model::TextStyle k_plain;
    auto style_at = [&](std::size_t i) -> const model::TextStyle& {
        return i < span_of.size() ? spans[span_of[i]].style : k_plain;
    };

    ChordLine result;
    std::optional<std::string> pending_chord;
    auto emit = [&](std::string syllable, const model::TextStyle& style) {
        result.items.push_back(
            ChordLineItem::make_cell(make_cell(pending_chord, std::move(syllable), style)));
        pending_chord.reset();
    };

    std::size_t i = 0;
    while (i < text.size()) {
        // Chord: [ ... ]
        if (text[i] == '[') {
            const auto end = text.find(']', i + 1);
            if (end != std::string::npos) {
                if (pending_chord) {
                    emit("", style_at(i));
                }
                pending_chord = text.substr(i + 1, end - (i + 1));
                i = end + 1;
                continue;
            }
            // An unclosed '[' is a character of its own.
            emit("[", style_at(i));
            ++i;
            continue;
        }

        // Space run
        if (util::is_space(text[i])) {
            std::size_t j = i + 1;
            while (j < text.size() && util::is_space(text[j])) {
                ++j;
            }
            if (pending_chord) {
                emit("", style_at(i));
            }
            result.items.push_back(ChordLineItem::space());
            i = j;
            continue;
        }

        // Word: up to a chord, a space or a change of style (each cell has one).
        std::size_t j = i + 1;
        while (j < text.size() && text[j] != '[' && !util::is_space(text[j])
               && span_of[j] == span_of[i]) {
            ++j;
        }
        emit(text.substr(i, j - i), style_at(i));
        i = j;
    }

    if (pending_chord) {
        emit("", text.empty() ? k_plain : style_at(text.size() - 1));
    }

    while (!result.items.empty() && result.items.front().kind == ChordItemKind::Space) {
        result.items.erase(result.items.begin());
    }
    while (!result.items.empty() && result.items.back().kind == ChordItemKind::Space) {
        result.items.pop_back();
    }
    return result;
}

}  // namespace seventhslide::layout
