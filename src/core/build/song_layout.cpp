#include "seventhslide/build/song_layout.hpp"

#include "seventhslide/build/even_wrap.hpp"
#include "seventhslide/layout/word_wrap.hpp"
#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/parse/lyrics_blocks.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>

namespace seventhslide::build {
namespace {

/// A lyric line as read once for every output.
struct LyricLine {
    /// The lyric as markup, chord markers removed.
    std::string text;
    /// ``text`` tokenised: every wrap and split of this line works on these.
    layout::StyledWords words;
    bool has_chord_markers = false;
    /// ``text`` with its chords (``lyric_with_chords``); empty when it has none.
    std::string chord_text;
    /// ``chord_text`` read as chords over words: what a box shows, and what
    /// the line reads as once saved as Freeform.
    layout::ChordLine chords;
    /// Per word of ``words``, the items of ``chords`` it carries
    /// (``chord_word_items``); empty when ``chords`` is.
    std::vector<layout::WrapRun> word_items;

    [[nodiscard]] layout::WrapRun whole() const noexcept { return {0, words.size()}; }
};

/**
 * A piece of a lyric line that plays as a row of its own: words of ``words``,
 * and the items of ``chords`` over them. Where a line has no word left to cut
 * at (chords alone, or one word with more chords than fit) a piece is cut at
 * the chord rows instead, and a piece can have chords and no words.
 */
struct LinePart {
    layout::WrapRun words;
    /// Nullopt when the line's words and chords could not be matched: such a
    /// piece of a line shows its words alone.
    std::optional<layout::WrapRun> items;
    /// What it reads as where it shows its chords, when it was cut at a chord
    /// row and so not at a word: the syllables under them.
    std::optional<std::string> chord_row_text;
};

/// What ``part`` reads as where it shows its chords.
[[nodiscard]] std::string chord_part_text(const LyricLine& line, const LinePart& part)
{
    return part.chord_row_text ? *part.chord_row_text : line.words.text(part.words);
}

/// The chord items a box showing chords draws ``part`` with, in a chord row:
/// its items, when it has chords where they can be placed. Nullopt for a part
/// with none, which is words like any other line's.
[[nodiscard]] std::optional<layout::WrapRun> shown_chords(const LyricLine& line,
                                                         const LinePart& part)
{
    if (!part.items) {
        return std::nullopt;
    }
    const auto items =
        std::span(line.chords.items).subspan(part.items->start, part.items->end - part.items->start);
    const bool any = std::ranges::any_of(items, [](const layout::ChordLineItem& item) {
        return item.kind == layout::ChordItemKind::Cell && item.cell.chord.has_value();
    });
    return any ? part.items : std::nullopt;
}

[[nodiscard]] LinePart whole_part(const LyricLine& line)
{
    return {.words = line.whole(), .items = layout::WrapRun{0, line.chords.items.size()}};
}

/// Words ``words`` of ``parent``, with the items that go with them.
[[nodiscard]] LinePart part_of(const LyricLine& line, const LinePart& parent, layout::WrapRun words)
{
    if (words == parent.words) {
        return parent;
    }
    LinePart out{.words = words};
    if (!parent.items || line.word_items.empty()) {
        return out;
    }
    out.items = layout::WrapRun{
        words.start == parent.words.start ? parent.items->start : line.word_items[words.start].start,
        words.end == parent.words.end ? parent.items->end : line.word_items[words.end - 1].end,
    };
    return out;
}

/**
 * Per lyric word, the items of ``chords`` over it, so a line split between
 * words splits its chords there too. ``chords`` is read from the lyric with its
 * chords (``lyric_with_chords``), which keeps every chord on a word, so its
 * runs of cells are the words. Should they not be, the result is empty and
 * parts of the line show no chords.
 */
[[nodiscard]] std::vector<layout::WrapRun> chord_word_items(const layout::ChordLine& chords,
                                                            std::size_t words)
{
    using layout::ChordItemKind;
    const auto& items = chords.items;
    std::vector<layout::WrapRun> out;
    std::size_t i = 0;
    while (i < items.size()) {
        if (items[i].kind == ChordItemKind::Space) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < items.size() && items[end].kind == ChordItemKind::Cell) {
            ++end;
        }
        out.push_back({i, end});
        i = end;
    }
    if (out.size() != words) {
        out.clear();
    }
    return out;
}

/// One type an output draws its lines in: ``type`` over the song box's.
struct LineFace {
    layout::FontStyle style;
    layout::RunMeasurer measure;
    int line_height = 0;
    /// What a chord line adds on top of ``line_height``.
    int chord_row = 0;

    LineFace(const theme::ResolvedTheme& theme,
             const canvas::LineType& type,
             layout::ITextMeasurer& measurer)
    {
        const auto& song = theme.song;
        const auto painted = canvas::painted_type(song, type);
        style = layout::FontStyle{
            .family = painted.family,
            .size_px = static_cast<double>(std::max(1, painted.size)),
            .bold = painted.bold,
            .italic = painted.italic,
            .line_height = song.line_height,
            .letter_spacing = song.letter_spacing,
        };
        measure = layout::bind_run_measurer(measurer, style);
        line_height = measurer.line_height_px(style);
        chord_row = layout::chord_row_px(measurer, style, song.chords);
    }

    /// A row of ``runs``: as paint sizes it for its largest text.
    [[nodiscard]] int row_px(const model::RichLine& runs) const
    {
        const int pct = layout::row_size_pct(runs);
        if (pct == 100) {
            return line_height;
        }
        return static_cast<int>(
            layout::line_box_height_px(style.size_px * (pct / 100.0),
                                       layout::clamp_css_line_height(style.line_height)));
    }
};

/// How much of the box some of a line takes: its rows and their height.
struct LineExtent {
    int rows = 0;
    int px = 0;
};

/// One output's box and fonts, and how lines are measured in it.
struct LineLayoutCtx {
    const theme::ResolvedTheme& theme;
    layout::ITextMeasurer& measurer;
    /// The type the lines are drawn in: under follow-lines, the other lines'.
    LineFace base;
    /// The active line's type, where follow-lines measures differently in it.
    std::optional<LineFace> highlight;
    int avail_w = 1;
    int avail_h = 1;

    /// The type lines are measured in other than while active.
    [[nodiscard]] static const canvas::LineType& base_type(const theme::ResolvedTheme& resolved)
    {
        static const canvas::LineType k_box_type;
        return resolved.behavior.follow_lines > 0 ? resolved.song.other_lines : k_box_type;
    }

    LineLayoutCtx(const theme::ResolvedTheme& resolved, layout::ITextMeasurer& text_measurer)
        : theme(resolved)
        , measurer(text_measurer)
        , base(resolved, base_type(resolved), text_measurer)
        , avail_w(std::max(1, resolved.song.width))
        , avail_h(std::max(1, resolved.song.height))
    {
        if (theme.behavior.follow_lines > 0) {
            LineFace active(theme, theme.song.active_line, measurer);
            if (active.style != base.style) {
                highlight.emplace(std::move(active));
            }
        }
    }

    [[nodiscard]] const LineFace& active_face() const { return highlight ? *highlight : base; }
    [[nodiscard]] double width() const noexcept { return static_cast<double>(avail_w); }

    /// The rows ``part`` of ``line`` is drawn in when it has no chord row over
    /// it. ``chords``: where chords show, a piece cut at a chord row is the
    /// syllables it was cut to, not the word they are of.
    [[nodiscard]] std::vector<model::RichLine> word_rows(const LineFace& face,
                                                         const LyricLine& line,
                                                         const LinePart& part,
                                                         bool chords) const
    {
        if (chords && part.chord_row_text) {
            const auto cut = layout::StyledWords::marked(*part.chord_row_text);
            return layout::wrap_styled_rows(cut, {0, cut.size()}, face.measure, width());
        }
        return layout::wrap_styled_rows(line.words, part.words, face.measure, width());
    }

    /// What ``part`` of ``line`` takes in ``face``: with its chords over it
    /// when ``chords`` and they can be placed, else its words alone (nothing
    /// for a part with no words).
    [[nodiscard]] LineExtent extent(const LineFace& face,
                                    const LyricLine& line,
                                    const LinePart& part,
                                    bool chords) const
    {
        if (const auto run = chords ? shown_chords(line, part) : std::nullopt) {
            const auto rows = layout::wrap_chord_line(layout::chord_line_part(line.chords, *run),
                                                      face.style, theme.song.chords, measurer,
                                                      width());
            const int count = std::max(1, static_cast<int>(rows.rows.size()));
            return {count, count * (face.line_height + face.chord_row)};
        }
        if (line.words.empty()) {
            return {1, face.line_height};  // text with no words paints as one row
        }
        LineExtent out;
        for (const auto& row : word_rows(face, line, part, chords)) {
            ++out.rows;
            out.px += face.row_px(row);
        }
        return out;
    }

    /// Whether ``part`` fits the box in ``face``, ``extra`` above it. A single
    /// row always does: there is no less of it to show.
    [[nodiscard]] bool fits(const LineFace& face,
                            const LyricLine& line,
                            const LinePart& part,
                            bool chords,
                            int extra) const
    {
        const auto taken = extent(face, line, part, chords);
        return taken.rows <= 1 || taken.px + extra <= avail_h;
    }
};

/// How a lyric line measures on one output.
struct MeasuredLine {
    bool shows_chords = false;
    /// The whole line's height at the box's size and while it is active.
    int base_px = 0;
    int active_px = 0;
    /// Verse gap above the line.
    int extra_px = 0;

    /// Whether the box shows the whole line, at every size it is drawn at.
    [[nodiscard]] bool fits(const LineLayoutCtx& lay) const noexcept
    {
        return std::max(base_px, active_px) + extra_px <= lay.avail_h;
    }
};

/// ``line`` measured on ``lay``'s output; nullopt when that output shows
/// nothing for it (a blank line, or chords with no words where chords are
/// hidden).
[[nodiscard]] std::optional<MeasuredLine> measure_line(const LyricLine& line,
                                                       int li_idx,
                                                       int v_idx,
                                                       const std::vector<std::string>& verse_codes,
                                                       const LineLayoutCtx& lay)
{
    MeasuredLine m;
    m.shows_chords = lay.theme.song.show_chords && line.has_chord_markers;
    if (m.shows_chords) {
        // Chord-only lines still "render" (non-empty cells).
        if (line.chords.items.empty() && line.text.empty()) {
            return std::nullopt;
        }
    } else if (line.text.empty()) {
        return std::nullopt;
    }

    const auto whole = whole_part(line);
    m.base_px = lay.extent(lay.base, line, whole, m.shows_chords).px;
    m.active_px = lay.highlight ? lay.extent(*lay.highlight, line, whole, m.shows_chords).px
                                : m.base_px;

    if (lay.theme.behavior.fluid_slides && li_idx == 0 && v_idx > 0
        && lay.theme.behavior.verse_gap > 0
        && !parse::same_verse_sections(
            verse_codes[static_cast<std::size_t>(v_idx - 1)],
            verse_codes[static_cast<std::size_t>(v_idx)])) {
        m.extra_px = lay.theme.behavior.verse_gap;
    }
    return m;
}

/// ``part`` cut between its chord rows into pieces that fit: the last way left
/// to make room, once it is down to chords alone or one word and its chords.
/// ``run`` is the chord items it shows (shown_chords). The piece with the word
/// has its words; the others have none.
[[nodiscard]] std::vector<LinePart> cut_at_chord_rows(const LineLayoutCtx& lay,
                                                      const LineFace& face,
                                                      const LyricLine& line,
                                                      const LinePart& part,
                                                      layout::WrapRun run,
                                                      int extra)
{
    const auto items = std::span(line.chords.items).subspan(run.start, run.end - run.start);
    const auto rows = layout::wrap_chord_items(items, face.style,
                                               layout::chord_font(face.style, lay.theme.song.chords),
                                               lay.measurer, lay.width());
    const int row_px = face.line_height + face.chord_row;
    const auto per_piece =
        static_cast<std::size_t>(std::max(1, (lay.avail_h - extra) / std::max(1, row_px)));
    std::vector<LinePart> out;
    bool word_placed = false;
    for (std::size_t r = 0; r < rows.size(); r += per_piece) {
        const auto& last = rows[std::min(r + per_piece, rows.size()) - 1];
        LinePart piece{.words = {part.words.start, part.words.start},
                       .items = layout::WrapRun{run.start + rows[r].start, run.start + last.end}};
        const auto cells = items.subspan(rows[r].start, last.end - rows[r].start);
        piece.chord_row_text = model::format_inline_markup(layout::chord_items_lyric(cells));
        if (!word_placed && std::ranges::any_of(cells, [](const layout::ChordLineItem& item) {
                return !item.cell.syllable.empty();
            })) {
            piece.words = part.words;
            word_placed = true;
        } else if (word_placed) {
            piece.words = {part.words.end, part.words.end};
        }
        out.push_back(piece);
    }
    return out;
}

/// ``part`` of ``line`` (its ``index``th) in pieces that fit in ``face``.
[[nodiscard]] std::vector<LinePart> split_part(const LineLayoutCtx& lay,
                                               const LineFace& face,
                                               const LyricLine& line,
                                               const MeasuredLine& m,
                                               const LinePart& part,
                                               std::size_t index)
{
    const int extra = index == 0 ? m.extra_px : 0;
    const layout::RunFits fits = [&](layout::WrapRun words) {
        return lay.fits(face, line, part_of(line, part, words), m.shows_chords, extra);
    };
    std::vector<LinePart> out;
    for (const auto& words : layout::split_word_runs_to_fit(line.words, part.words, fits)) {
        const auto piece = part_of(line, part, words);
        const auto run = m.shows_chords ? shown_chords(line, piece) : std::nullopt;
        if (run && !lay.fits(face, line, piece, true, extra)) {
            const auto cut = cut_at_chord_rows(lay, face, line, piece, *run, extra);
            out.insert(out.end(), cut.begin(), cut.end());
        } else {
            out.push_back(piece);
        }
    }
    return out;
}

/// One step through the song, in play order.
struct SongStep {
    enum class Kind { Title, SplitMarker, Lyric };
    Kind kind = Kind::Lyric;
    int v_idx = 0;
    int li_idx = 0;
    LyricLine lyric;
    /// Per output, in the order given; nullopt where it shows nothing.
    std::vector<std::optional<MeasuredLine>> measured;
    /// The parts the line is split into, the same for every output; empty =
    /// shown whole everywhere.
    std::vector<LinePart> parts;
    /// Some output shows it. A line none shows (blank) takes no row.
    bool shown = false;
};

/// Measure ``step``'s line on every output and settle its parts. An output
/// that cannot show the line whole, at every size it draws it at, refines the
/// parts settled on so far: subdividing a part never makes it stop fitting a
/// box it already fit (less wraps to no more rows), so the result fits all of
/// them.
void plan_parts(SongStep& step,
                const std::vector<std::string>& verse_codes,
                const std::vector<LineLayoutCtx>& lays)
{
    const LyricLine& line = step.lyric;
    step.measured.reserve(lays.size());
    for (const auto& lay : lays) {
        auto m = measure_line(line, step.li_idx, step.v_idx, verse_codes, lay);
        step.measured.push_back(m);
        if (!m) {
            continue;
        }
        step.shown = true;
        if (m->fits(lay)) {
            continue;
        }
        if (step.parts.empty()) {
            step.parts.push_back(whole_part(line));
        }
        auto refine = [&](const LineFace& face) {
            std::vector<LinePart> refined;
            refined.reserve(step.parts.size());
            for (std::size_t k = 0; k < step.parts.size(); ++k) {
                const LinePart& part = step.parts[k];
                if (lay.fits(face, line, part, m->shows_chords, k == 0 ? m->extra_px : 0)) {
                    refined.push_back(part);
                    continue;
                }
                const auto pieces = split_part(lay, face, line, *m, part, k);
                refined.insert(refined.end(), pieces.begin(), pieces.end());
            }
            step.parts = std::move(refined);
        };
        refine(lay.base);
        if (lay.highlight) {
            refine(*lay.highlight);
        }
    }
}

/**
 * Read the song once: every line tokenised once, measured once per output,
 * and - where some output cannot show a line whole - split into parts that fit
 * every output that uses them (as chunk_bible_verse does for verses).
 *
 * The player has one cursor for all outputs, so every output needs the same
 * rows; outputs still group them into slides their own way (four lines a
 * slide on one, two on another). An output with room shows a split line whole
 * and holds it across the parts' rows. A line only some outputs show (chords
 * with no words, where chords are hidden) keeps its row; the others stay on
 * their slide for it.
 */
[[nodiscard]] std::vector<SongStep> read_song(const parse::LyricsBlocks& blocks,
                                              const std::vector<LineLayoutCtx>& lays)
{
    std::vector<SongStep> steps;
    for (std::size_t v = 0; v < blocks.verses.size(); ++v) {
        const auto v_idx = static_cast<int>(v);
        const std::string_view code =
            v < blocks.verse_codes.size() ? std::string_view(blocks.verse_codes[v]) : "";
        if (parse::is_title_code(code)) {
            steps.push_back({.kind = SongStep::Kind::Title, .v_idx = v_idx});
            continue;
        }
        const std::string_view verse = blocks.verses[v];
        std::size_t start = 0;
        int li_idx = 0;
        while (start <= verse.size()) {
            const auto nl = verse.find('\n', start);
            const auto end = (nl == std::string_view::npos) ? verse.size() : nl;
            const std::string_view raw = verse.substr(start, end - start);
            SongStep step{.v_idx = v_idx, .li_idx = li_idx};
            if (model::is_forced_split_marker(util::trim(raw))) {
                step.kind = SongStep::Kind::SplitMarker;
            } else {
                auto& lyric = step.lyric;
                lyric.text = layout::lyric_without_chords(raw);
                lyric.words = layout::StyledWords::marked(lyric.text);
                lyric.has_chord_markers = layout::has_chords(raw);
                if (lyric.has_chord_markers) {
                    lyric.chord_text = layout::lyric_with_chords(raw);
                    lyric.chords = layout::parse_chord_line(lyric.chord_text);
                    lyric.word_items = chord_word_items(lyric.chords, lyric.words.size());
                }
                plan_parts(step, blocks.verse_codes, lays);
            }
            steps.push_back(std::move(step));
            ++li_idx;
            if (nl == std::string_view::npos) {
                break;
            }
            start = nl + 1;
        }
    }
    return steps;
}

/// ``line``'s chords cut at ``parts``: each part's words with the chords over
/// them, as their own markers. Empty for a part with none, and for every part
/// when the parts cannot be found in the line.
[[nodiscard]] std::vector<std::string> parts_chord_text(const LyricLine& line,
                                                        const std::vector<LinePart>& parts)
{
    std::vector<std::string> out(parts.size());
    if (line.chord_text.empty()) {
        return out;
    }
    auto plain_of = [](std::string_view markup) {
        return model::plain_text_lines(model::parse_markup_lines(markup));
    };
    auto has_marker = [](const layout::ChordLineItem& item) {
        return item.kind == layout::ChordItemKind::Cell && item.cell.chord.has_value();
    };
    const auto taken = layout::take_chord_marks(line.chord_text);
    if (taken.marks.size() != static_cast<std::size_t>(std::ranges::count_if(line.chords.items, has_marker))) {
        return out;
    }
    const std::string whole = plain_of(taken.markup);
    // The markers come in the order of the cells that hold them.
    std::size_t next_mark = 0;
    std::size_t from = 0;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        const auto& run = parts[k].items;
        if (!run) {
            return std::vector<std::string>(parts.size());
        }
        const std::string text = chord_part_text(line, parts[k]);
        const std::string plain = plain_of(text);
        const auto at = whole.find(plain, from);
        if (at == std::string::npos) {
            return std::vector<std::string>(parts.size());
        }
        from = at + plain.size();
        const auto items = std::span(line.chords.items).subspan(run->start, run->end - run->start);
        std::vector<layout::ChordMark> marks;
        for (const auto& item : items) {
            if (has_marker(item)) {
                const auto& mark = taken.marks[next_mark++];
                marks.push_back({mark.at - std::min(mark.at, at), mark.chord});
            }
        }
        if (!marks.empty()) {
            out[k] = layout::put_chord_marks(text, marks);
        }
    }
    return out;
}

/// ``part`` of ``line`` as a line of ``lay``'s output, ``extra`` above it.
[[nodiscard]] LayoutLine lay_out_part(const LineLayoutCtx& lay,
                                      const LyricLine& line,
                                      const MeasuredLine& m,
                                      const LinePart& part,
                                      int extra)
{
    LayoutLine ln;
    ln.extra_px = extra;
    const auto chords = m.shows_chords ? shown_chords(line, part) : std::nullopt;
    const int base = lay.extent(lay.base, line, part, m.shows_chords).px;
    ln.base_height_px = base + extra;
    ln.active_height_px =
        lay.extent(lay.active_face(), line, part, m.shows_chords).px + extra;
    if (chords) {
        ln.has_chord_layout = true;
        ln.chords = layout::chord_line_part(line.chords, *chords);  // paint wraps it
        ln.line_height_px = lay.base.line_height + lay.base.chord_row;
        return ln;
    }
    // Paint has no word-wrap of its own: a line needing more than one row at
    // full box width carries its rows (evening may replace them later). Evening
    // assumes even rows of whole words.
    const auto rows = lay.word_rows(lay.base, line, part, m.shows_chords);
    ln.line_height_px = lay.base.line_height;
    for (const auto& row : rows) {
        ln.line_height_px = std::max(ln.line_height_px, lay.base.row_px(row));
    }
    if (rows.size() > 1) {
        ln.wrap_fragments.reserve(rows.size());
        for (const auto& row : rows) {
            ln.wrap_fragments.push_back(model::format_inline_markup(row));
        }
    }
    ln.evenable = std::ranges::all_of(rows, [](const model::RichLine& row) {
        return layout::row_size_pct(row) == 100;
    }) && !part.chord_row_text
        && !layout::has_word_wider_than(line.words, part.words, lay.base.measure, lay.width());
    return ln;
}

/// Lay out one planned lyric line on output ``o``: whole when it fits there
/// (covering every row of the parts), else as the parts it shows something for.
void emit_lyric(const SongStep& step,
                std::size_t o,
                const LineLayoutCtx& lay,
                std::string_view verse_label,
                layout::SlideGrouper& grouper,
                std::vector<LayoutLine>& lines,
                std::vector<int>& row_lines)
{
    const std::size_t row_count = std::max<std::size_t>(1, step.parts.size());
    const auto& m = step.measured[o];
    if (!m) {
        row_lines.insert(row_lines.end(), row_count, -1);
        return;
    }
    const LyricLine& line = step.lyric;
    auto add = [&](LayoutLine ln) {
        const int idx = static_cast<int>(lines.size());
        const int base_px = ln.base_height_px;
        const int active = ln.active_height_px;
        grouper.add_line(idx, base_px, active, ln.verse_index);
        lines.push_back(std::move(ln));
        return idx;
    };

    if (step.parts.empty() || m->fits(lay)) {
        LayoutLine ln = lay_out_part(lay, line, *m, whole_part(line), m->extra_px);
        ln.text = line.text;
        ln.chord_text = line.chord_text;
        ln.label = std::string(verse_label);
        ln.verse_index = step.v_idx;
        // Held across rows other outputs split it into, it has no one place
        // to be evened from.
        ln.evenable = ln.evenable && step.parts.empty();
        row_lines.insert(row_lines.end(), row_count, add(std::move(ln)));
        return;
    }

    const auto& parts = step.parts;
    const auto chord_texts = parts_chord_text(line, parts);
    // A part with no words shows nothing where chords are hidden; its row stays
    // on the slide before.
    auto shown_here = [&](const LinePart& part) {
        return (m->shows_chords && (shown_chords(line, part) || part.chord_row_text))
            || part.words.start < part.words.end;
    };
    const auto shown_parts = static_cast<std::size_t>(std::ranges::count_if(parts, shown_here));
    std::size_t number = 0;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (!shown_here(parts[k])) {
            row_lines.push_back(-1);
            continue;
        }
        ++number;
        LayoutLine ln = lay_out_part(lay, line, *m, parts[k], k == 0 ? m->extra_px : 0);
        ln.text = m->shows_chords ? chord_part_text(line, parts[k])
                                  : line.words.text(parts[k].words);
        ln.chord_text = chord_texts[k];
        if (shown_parts > 1) {
            const std::string of = std::to_string(number) + "/" + std::to_string(shown_parts);
            ln.label = verse_label.empty() ? "Part " + of
                                           : std::string(verse_label) + " (" + of + ")";
        } else {
            ln.label = std::string(verse_label);
        }
        ln.verse_index = step.v_idx;
        row_lines.push_back(add(std::move(ln)));
    }
}

[[nodiscard]] LayoutResult layout_output(const std::vector<SongStep>& steps,
                                         std::size_t o,
                                         const LineLayoutCtx& lay,
                                         const parse::LyricsBlocks& blocks)
{
    const auto& theme = lay.theme;
    layout::SlideGrouper grouper(lay.avail_h, theme.behavior.follow_lines,
                                 theme.behavior.prevent_mixed_active);
    std::vector<LayoutLine> lines;
    std::vector<int> row_lines;
    int prev_verse = -1;

    for (const auto& step : steps) {
        if (step.kind == SongStep::Kind::Title) {
            prev_verse = step.v_idx;
            grouper.flush_buffer();
            LayoutLine title;
            title.label = "t1";
            title.verse_index = step.v_idx;
            title.base_height_px = lay.base.line_height;
            title.active_height_px = lay.base.line_height;
            title.line_height_px = lay.base.line_height;
            title.evenable = false;
            title.is_title = true;
            const int idx = static_cast<int>(lines.size());
            lines.push_back(std::move(title));
            row_lines.push_back(idx);
            layout::SlideGroup g;
            g.indices = {idx};
            g.active_count = 1;
            g.is_title = true;
            g.owner_line = idx;
            grouper.push_group(std::move(g));
            continue;
        }
        if (step.v_idx != prev_verse) {
            prev_verse = step.v_idx;
            if (!theme.behavior.fluid_slides) {
                grouper.flush_buffer();
            }
        }
        if (step.kind == SongStep::Kind::SplitMarker) {
            // Paging: author split starts a new slide. Fluid: ignore like verse
            // boundaries - the marker is never painted.
            if (!theme.behavior.fluid_slides) {
                grouper.flush_buffer();
            }
            continue;
        }
        if (!step.shown) {
            continue;
        }
        const auto v = static_cast<std::size_t>(step.v_idx);
        const std::string_view label =
            v < blocks.verse_codes.size() ? std::string_view(blocks.verse_codes[v]) : "";
        emit_lyric(step, o, lay, label, grouper, lines, row_lines);
    }
    grouper.flush_buffer();

    LayoutResult result;
    result.verse_codes = blocks.verse_codes;
    result.lines = std::move(lines);
    result.groups = grouper.take_groups();
    result.row_lines = std::move(row_lines);
    apply_even_wrap_if_enabled(
        result, theme, lay.base.measure, lay.width(), static_cast<double>(lay.avail_h));
    result.line_to_slide = layout::build_line_to_slide(result.groups, result.lines.size());
    return result;
}

}  // namespace

std::vector<LayoutResult> compute_song_layouts(
    const std::vector<const theme::ResolvedTheme*>& themes,
    layout::ITextMeasurer& measurer,
    std::string_view lyrics,
    std::string_view verse_order,
    bool suppress_titles)
{
    std::vector<LineLayoutCtx> lays;
    lays.reserve(themes.size());
    for (const auto* theme : themes) {
        lays.emplace_back(*theme, measurer);
    }
    const auto blocks = parse::parse_verses(lyrics, verse_order, suppress_titles);
    const auto steps = read_song(blocks, lays);

    std::vector<LayoutResult> out;
    out.reserve(lays.size());
    for (std::size_t o = 0; o < lays.size(); ++o) {
        out.push_back(layout_output(steps, o, lays[o], blocks));
    }
    return out;
}

LayoutResult compute_song_layout(
    const theme::ResolvedTheme& theme,
    layout::ITextMeasurer& measurer,
    std::string_view lyrics,
    std::string_view verse_order,
    bool suppress_titles)
{
    return std::move(compute_song_layouts({&theme}, measurer, lyrics, verse_order, suppress_titles)
                         .front());
}

std::vector<int> row_to_slide(const LayoutResult& layout)
{
    std::vector<int> out;
    out.reserve(layout.row_lines.size());
    int held = -1;
    for (const int line : layout.row_lines) {
        if (line >= 0 && line < static_cast<int>(layout.line_to_slide.size())) {
            held = layout.line_to_slide[static_cast<std::size_t>(line)];
        }
        out.push_back(held);
    }
    // Rows before this output's first line show its first slide.
    for (int& slide : out) {
        if (slide >= 0) {
            break;
        }
        slide = 0;
    }
    return out;
}

bool needs_verse_gap(const LayoutResult& layout,
                     bool fluid_slides,
                     int verse_gap,
                     int line_index,
                     int prev_line_index)
{
    if (!fluid_slides || verse_gap <= 0) {
        return false;
    }
    const int line_count = static_cast<int>(layout.lines.size());
    if (line_index < 0 || prev_line_index < 0 || line_index >= line_count
        || prev_line_index >= line_count) {
        return false;
    }
    const int v_idx = layout.lines[static_cast<std::size_t>(line_index)].verse_index;
    const int prev_v_idx = layout.lines[static_cast<std::size_t>(prev_line_index)].verse_index;
    if (v_idx == prev_v_idx) {
        return false;
    }
    const int code_count = static_cast<int>(layout.verse_codes.size());
    if (v_idx < 0 || prev_v_idx < 0 || v_idx >= code_count || prev_v_idx >= code_count) {
        return true;
    }
    return !parse::same_verse_sections(layout.verse_codes[static_cast<std::size_t>(prev_v_idx)],
                                       layout.verse_codes[static_cast<std::size_t>(v_idx)]);
}

int painted_line_height_px(const LayoutLine& line,
                           const theme::ResolvedTheme& theme,
                           layout::ITextMeasurer& measurer)
{
    if (line.is_title) {
        return 0;
    }
    // In the type lines are measured in other than while active.
    const LineFace face(theme, LineLayoutCtx::base_type(theme), measurer);
    const double width = static_cast<double>(std::max(1, theme.song.width));
    if (line.has_chord_layout) {
        const auto rows =
            layout::wrap_chord_line(line.chords, face.style, theme.song.chords, measurer, width);
        return std::max(1, static_cast<int>(rows.rows.size()))
            * (face.line_height + face.chord_row);
    }
    int px = 0;
    const auto words = layout::StyledWords::marked(line.text);
    for (const auto& row : layout::wrap_styled_rows(words, {0, words.size()}, face.measure, width)) {
        px += face.row_px(row);
    }
    return std::max(px, face.line_height);
}

}  // namespace seventhslide::build
