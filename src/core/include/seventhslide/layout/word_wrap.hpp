#pragma once

// Word wrap + split-to-fit.
// Seam: plain C++20 - no Qt. Measure via WidthMeasurer (bind ITextMeasurer).

#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/model/rich_text.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::layout {

/// Width of plain text in one fixed font.
using WidthMeasurer = std::function<double(std::string_view)>;

/// Bind ``ITextMeasurer`` + ``FontStyle`` into a ``WidthMeasurer`` for plain text.
[[nodiscard]] WidthMeasurer bind_measurer(ITextMeasurer& measurer, FontStyle style);

/// Width of one run of text in the box font with ``style`` on top: how marked
/// text (song lyrics) is measured, run by run, the way paint draws it. Tags are
/// never measured as glyphs because a marked line is measured from its spans.
using RunMeasurer = std::function<double(std::string_view text, const model::TextStyle& style)>;

/// The font a span paints in: ``box`` with the span's family, size scale, bold
/// and italic on top. Layout and paint both use this, so what is measured is
/// what is drawn.
[[nodiscard]] FontStyle span_font_style(const FontStyle& box, const model::TextStyle& span);

/// Extra advance after a span in ``span_font`` (its ``margin_right_pct``).
[[nodiscard]] double span_margin_px(const FontStyle& span_font,
                                    const model::TextStyle& span) noexcept;

/// ``RunMeasurer`` for ``box``: each run in ``span_font_style``, margin included.
[[nodiscard]] RunMeasurer bind_run_measurer(ITextMeasurer& measurer, FontStyle box);

/// Words ``[start, end)`` of a line.
struct WrapRun {
    std::size_t start = 0;
    std::size_t end = 0;

    [[nodiscard]] bool operator==(const WrapRun&) const noexcept = default;
};

/**
 * A line broken into words once, for wrapping, splitting and measuring: what
 * ``PlainWords`` and ``StyledWords`` share, each word as plain text. Breaks
 * only ever fall between words. Made only as one of the two.
 */
class LineWords {
public:
    [[nodiscard]] std::size_t size() const noexcept { return plains_.size(); }
    [[nodiscard]] bool empty() const noexcept { return plains_.empty(); }
    /// Word ``i`` without markup.
    [[nodiscard]] const std::string& plain(std::size_t i) const { return plains_[i]; }

protected:
    LineWords() = default;
    LineWords(const LineWords&) = default;
    LineWords(LineWords&&) noexcept = default;
    LineWords& operator=(const LineWords&) = default;
    LineWords& operator=(LineWords&&) noexcept = default;
    ~LineWords() = default;

    std::vector<std::string> plains_;
};

/// A line of plain text in one font, its words joined by single spaces.
/// Measured with a ``WidthMeasurer``.
class PlainWords : public LineWords {
public:
    explicit PlainWords(std::string_view text);

    /// Cut every word of ``run`` wider than ``max_width_px`` into pieces that
    /// fit, between characters, the pieces joined by nothing, and return where
    /// ``run`` now is. A single character wider than the box stays whole.
    /// Wrapping then fills a cut word's rows and lets its last piece share a
    /// row with the next word.
    WrapRun break_wide_words(const WidthMeasurer& measure, double max_width_px, WrapRun run);
    /// Whether word ``i`` is a piece joined to the word before.
    [[nodiscard]] bool joined_to_previous(std::size_t i) const
    {
        return !joined_.empty() && joined_[i] != 0;
    }
    /// Words ``[begin, end)`` as the line shows them.
    [[nodiscard]] std::string text(std::size_t begin, std::size_t end) const;
    [[nodiscard]] std::string text(WrapRun run) const { return text(run.start, run.end); }

private:
    /// Lines with cut words: 1 for a piece joined to the word before.
    std::vector<char> joined_;
};

/// A line of styled runs: marked-up lyrics, joined by single spaces, or an
/// overlay line (``spans``), which keeps the whitespace the author typed
/// between words. Measured run by run with a ``RunMeasurer``.
///
/// A tag is never cut in half, and the text of any run of words re-opens
/// every style it continues: a marked line split into
/// ``<color=#f00>one</color>`` and ``<color=#f00>two</color>`` paints both
/// halves red.
class StyledWords : public LineWords {
public:
    [[nodiscard]] static StyledWords marked(std::string_view markup);
    /// Words of already-styled runs (an overlay line). Whitespace between
    /// words is kept as typed, each piece in its own style; whitespace before
    /// the first word and after the last is dropped.
    [[nodiscard]] static StyledWords spans(const model::RichLine& line);

    /// Word ``i``'s styled pieces.
    [[nodiscard]] const model::RichLine& pieces(std::size_t i) const { return pieces_[i]; }
    /// The whitespace before word ``i``, in its own style (empty for a piece
    /// of a cut word).
    [[nodiscard]] const model::RichLine& gap_before(std::size_t i) const { return gaps_[i]; }

    /// Cut every word of ``run`` wider than ``max_width_px`` into pieces that
    /// fit, as ``PlainWords::break_wide_words`` does, and return where ``run``
    /// now is.
    WrapRun break_wide_words(const RunMeasurer& measure, double max_width_px, WrapRun run);
    void break_wide_words(const RunMeasurer& measure, double max_width_px)
    {
        (void)break_wide_words(measure, max_width_px, WrapRun{0, size()});
    }
    /// Words ``[begin, end)`` as the line shows them, as markup.
    [[nodiscard]] std::string text(std::size_t begin, std::size_t end) const;
    [[nodiscard]] std::string text(WrapRun run) const { return text(run.start, run.end); }

    /// Width of words ``[begin, end)`` as one row, measured run by run with
    /// adjacent same-style pieces merged (as paint draws them). No markup is
    /// built or parsed.
    [[nodiscard]] double width(std::size_t begin, std::size_t end,
                               const RunMeasurer& measure) const;

private:
    /// Each word's styled pieces, and the whitespace before it (a space
    /// inside ``<b>...</b>`` is bold).
    std::vector<model::RichLine> pieces_;
    std::vector<model::RichLine> gaps_;
};

/// Greedy wrap of words ``[begin, end)`` to ``max_width_px``: each row as full
/// as it can be.
[[nodiscard]] std::vector<WrapRun> wrap_word_runs(const PlainWords& words,
                                                  std::size_t begin,
                                                  std::size_t end,
                                                  const WidthMeasurer& measure,
                                                  double max_width_px);
[[nodiscard]] std::vector<WrapRun> wrap_word_runs(const StyledWords& words,
                                                  std::size_t begin,
                                                  std::size_t end,
                                                  const RunMeasurer& measure,
                                                  double max_width_px);

/**
 * Words ``run`` as the rows a box draws: greedy to ``max_width_px``, a word
 * wider than the box cut to fit (``break_wide_words``), each row as runs of one
 * style. No words, no rows. Every text box wraps through this, so what is
 * counted is what is drawn.
 */
[[nodiscard]] std::vector<model::RichLine> wrap_styled_rows(const StyledWords& words,
                                                            WrapRun run,
                                                            const RunMeasurer& measure,
                                                            double max_width_px);

/// The same with the first row ``first_width_px`` wide (an indent, a bullet):
/// the rows after it are ``max_width_px``.
[[nodiscard]] std::vector<model::RichLine> wrap_styled_rows(const StyledWords& words,
                                                            WrapRun run,
                                                            const RunMeasurer& measure,
                                                            double max_width_px,
                                                            double first_width_px);

/// Whether a word of ``run`` is wider than ``max_width_px`` and so gets cut.
[[nodiscard]] bool has_word_wider_than(const StyledWords& words,
                                       WrapRun run,
                                       const RunMeasurer& measure,
                                       double max_width_px);

/// ``text`` cut between characters into pieces no wider than ``max_width_px``.
/// A single character wider than that stays whole. Empty text, no pieces.
[[nodiscard]] std::vector<std::string> cut_text_to_width(std::string_view text,
                                                         const WidthMeasurer& measure,
                                                         double max_width_px);

/// The size a row of ``runs`` is laid out at, as a percent of its box's: its
/// largest run. Every box opens a row up, or closes it down, to this.
[[nodiscard]] int row_size_pct(const model::RichLine& runs) noexcept;

/// Rows ``wrap_word_runs`` would give, without keeping them (at least 1).
[[nodiscard]] int wrapped_row_count(const StyledWords& words,
                                    WrapRun run,
                                    const RunMeasurer& measure,
                                    double max_width_px);

/// Words of ``range`` cut after every word ending in one of ``delimiters``
/// (closing quotes and brackets may follow it): sentences with ``".!?"``,
/// clauses with ``";,"``. The tiers ``split_word_runs_to_fit`` prefers.
[[nodiscard]] std::vector<WrapRun> chunk_word_runs(const LineWords& words,
                                                   WrapRun range,
                                                   std::string_view delimiters);

/**
 * Split words ``[begin, end)`` into runs that each fit ``max_width_px`` x
 * ``max_height_px``, preferring sentence ends, then clause ends (``;,``), then
 * any word. A range that fits is one run.
 */
[[nodiscard]] std::vector<WrapRun> split_word_runs_to_fit(const PlainWords& words,
                                                          WrapRun range,
                                                          const WidthMeasurer& measure,
                                                          int line_height_px,
                                                          double max_width_px,
                                                          double max_height_px);

/// Whether words ``run`` of a line fit where it is being split to fit.
using RunFits = std::function<bool(WrapRun run)>;

/// ``split_word_runs_to_fit`` for a line whose rows are not all alike (chords
/// over its words, sizes of their own, more than one size to fit at): ``fits``
/// says what fits. A word that does not fit alone still stands alone.
[[nodiscard]] std::vector<WrapRun> split_word_runs_to_fit(const LineWords& words,
                                                          WrapRun range,
                                                          const RunFits& fits);

/// Greedy word-wrap. Empty / whitespace-only -> single empty string.
[[nodiscard]] std::vector<std::string> wrap_plain_text_to_width(
    std::string_view plain_text,
    const WidthMeasurer& measure,
    double max_width_px);

/**
 * Greedy-wrap one marked-up lyric line to ``max_width_px``. Returns
 * markup-balanced visual rows; a line that fits is returned unchanged as the
 * only row.
 */
[[nodiscard]] std::vector<std::string> wrap_marked_text_to_width(
    std::string_view text,
    const RunMeasurer& measure,
    double max_width_px);

/// ``split_word_runs_to_fit`` for plain text. A line that fits comes back unchanged.
[[nodiscard]] std::vector<std::string> split_line_to_fit(
    std::string_view text,
    const WidthMeasurer& measure,
    int line_height_px,
    double max_width_px,
    double max_height_px);

}  // namespace seventhslide::layout
