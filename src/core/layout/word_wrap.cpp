#include "seventhslide/layout/word_wrap.hpp"

#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <ranges>
#include <optional>
#include <utility>

namespace seventhslide::layout {
namespace {

/// Words ``[begin, end)`` of ``plains`` joined by single spaces into ``out``
/// (a reused buffer: a warm call allocates nothing), the pieces of a cut word
/// by nothing.
void join_words(const std::vector<std::string>& plains, const std::vector<char>& joined,
                std::size_t begin, std::size_t end, std::string& out)
{
    out.clear();
    for (std::size_t i = begin; i < end; ++i) {
        if (i > begin && (joined.empty() || joined[i] == 0)) {
            out.push_back(' ');
        }
        out += plains[i];
    }
}

[[nodiscard]] bool is_closer(char c) noexcept
{
    switch (c) {
    case '"':
    case '\'':
    case ')':
    case ']':
    case '}':
        return true;
    default:
        return false;
    }
}

/// Whether ``word`` ends a chunk: its last character before any closing
/// quotes and brackets is one of ``delimiters``.
[[nodiscard]] bool ends_chunk(std::string_view word, std::string_view delimiters) noexcept
{
    while (!word.empty() && is_closer(word.back())) {
        word.remove_suffix(1);
    }
    return !word.empty() && delimiters.find(word.back()) != std::string_view::npos;
}


// The engine below is written once against a row builder: ``start(i)`` begins
// a row at word ``i``, ``extend(i)`` adds word ``i`` and returns the row's width.
// The greedy loops only ever grow a row by one word or start a new one, so a
// builder re-measures only what the new word changed.

/// Plain words joined by single spaces, measured whole.
class PlainRow {
public:
    PlainRow(const PlainWords& words, const WidthMeasurer& measure)
        : words_(words)
        , measure_(measure)
    {
    }
    void start(std::size_t i) { row_ = words_.plain(i); }
    [[nodiscard]] double extend(std::size_t i)
    {
        if (!words_.joined_to_previous(i)) {
            row_.push_back(' ');
        }
        row_ += words_.plain(i);
        return measure_(row_);
    }

private:
    const PlainWords& words_;
    const WidthMeasurer& measure_;
    std::string row_;
};

/// Styled words as runs of one style each (adjacent same-style pieces merged,
/// as paint draws them). A run is measured once when it closes; extending
/// re-measures only the run still open.
class StyledRow {
public:
    StyledRow(const StyledWords& words, const RunMeasurer& measure)
        : words_(words)
        , measure_(measure)
    {
    }
    void start(std::size_t i)
    {
        runs_ = 0;
        closed_px_ = 0.0;
        add_word(i);
    }
    [[nodiscard]] double extend(std::size_t i)
    {
        for (const auto& piece : words_.gap_before(i)) {
            add(piece.text, piece.style);
        }
        add_word(i);
        return width();
    }
    [[nodiscard]] double width() const
    {
        return runs_ == 0 ? closed_px_
                          : closed_px_ + measure_(open_.text, *open_.style);
    }

private:
    struct Run {
        std::string text;
        const model::TextStyle* style = nullptr;
    };

    void add_word(std::size_t i)
    {
        for (const auto& piece : words_.pieces(i)) {
            add(piece.text, piece.style);
        }
    }
    void add(std::string_view text, const model::TextStyle& style)
    {
        if (runs_ > 0 && *open_.style == style) {
            open_.text.append(text);
            return;
        }
        if (runs_ > 0) {
            closed_px_ += measure_(open_.text, *open_.style);
        }
        open_.text.assign(text);
        open_.style = &style;
        ++runs_;
    }

    const StyledWords& words_;
    const RunMeasurer& measure_;
    Run open_;
    std::size_t runs_ = 0;
    double closed_px_ = 0.0;
};

template <typename Row>
[[nodiscard]] std::vector<WrapRun> wrap_runs(WrapRun range, Row& row, double max_width_px)
{
    std::vector<WrapRun> runs;
    if (range.start >= range.end) {
        return runs;
    }
    std::size_t start = range.start;
    row.start(start);
    for (std::size_t i = range.start + 1; i < range.end; ++i) {
        if (row.extend(i) > max_width_px) {
            runs.push_back({start, i});
            start = i;
            row.start(i);
        }
    }
    runs.push_back({start, range.end});
    return runs;
}

template <typename Row>
[[nodiscard]] int count_rows(WrapRun range, Row& row, double max_width_px)
{
    int rows = 1;
    if (range.start >= range.end) {
        return rows;
    }
    row.start(range.start);
    for (std::size_t i = range.start + 1; i < range.end; ++i) {
        if (row.extend(i) > max_width_px) {
            ++rows;
            row.start(i);
        }
    }
    return rows;
}

/// Words of ``run`` packed into pieces of at most ``max_rows`` rows each.
template <typename Row>
[[nodiscard]] std::vector<WrapRun> split_run_by_words(WrapRun run,
                                                      Row& row,
                                                      double max_width_px,
                                                      int max_rows)
{
    std::vector<WrapRun> out;
    if (run.start >= run.end) {
        return out;
    }
    const int max_vl = std::max(1, max_rows);
    std::size_t piece_start = run.start;
    int rows = 1;
    row.start(run.start);
    for (std::size_t i = run.start + 1; i < run.end; ++i) {
        if (row.extend(i) <= max_width_px) {
            continue;
        }
        row.start(i);
        if (rows < max_vl) {
            ++rows;
            continue;
        }
        out.push_back({piece_start, i});
        piece_start = i;
        rows = 1;
    }
    out.push_back({piece_start, run.end});
    return out;
}

// What splitting asks of a line: whether a run of words fits (``fits``), and
// the run packed a word at a time into pieces that do (``by_words``).

/// Fits in so many rows, as a row builder wraps them at a width.
template <typename Row>
class BuiltRows {
public:
    BuiltRows(Row& row, double max_width_px, int max_rows)
        : row_(row)
        , max_width_px_(max_width_px)
        , max_rows_(max_rows)
    {
    }
    [[nodiscard]] bool fits(WrapRun run) { return count_rows(run, row_, max_width_px_) <= max_rows_; }
    [[nodiscard]] std::vector<WrapRun> by_words(WrapRun run)
    {
        return split_run_by_words(run, row_, max_width_px_, max_rows_);
    }

private:
    Row& row_;
    double max_width_px_;
    int max_rows_;
};

/// Fits as a ``RunFits`` says.
class AskedRuns {
public:
    explicit AskedRuns(const RunFits& asked)
        : asked_(asked)
    {
    }
    [[nodiscard]] bool fits(WrapRun run) const { return asked_(run); }
    [[nodiscard]] std::vector<WrapRun> by_words(WrapRun run) const
    {
        std::vector<WrapRun> out;
        if (run.start >= run.end) {
            return out;
        }
        std::size_t piece_start = run.start;
        for (std::size_t i = run.start + 1; i < run.end; ++i) {
            if (!asked_({piece_start, i + 1})) {
                out.push_back({piece_start, i});
                piece_start = i;
            }
        }
        out.push_back({piece_start, run.end});
        return out;
    }

private:
    const RunFits& asked_;
};

/// Greedily pack consecutive ``chunks`` into pieces that fit; a chunk too big
/// on its own goes through ``split_oversize``.
template <typename Runs, typename SplitOversize>
[[nodiscard]] std::vector<WrapRun> greedy_group(const std::vector<WrapRun>& chunks,
                                                Runs& runs,
                                                SplitOversize&& split_oversize)
{
    std::vector<WrapRun> out;
    std::optional<WrapRun> group;
    for (const auto& chunk : chunks) {
        if (group) {
            const WrapRun grown{group->start, chunk.end};
            if (runs.fits(grown)) {
                group = grown;
                continue;
            }
            out.push_back(*group);
            group.reset();
        }
        if (runs.fits(chunk)) {
            group = chunk;
            continue;
        }
        auto pieces = split_oversize(chunk);
        out.insert(out.end(), pieces.begin(), pieces.end());
    }
    if (group) {
        out.push_back(*group);
    }
    return out;
}

/// ``range`` in pieces that fit, preferring sentence ends, then clause ends,
/// then any word.
template <typename Runs>
[[nodiscard]] std::vector<WrapRun> split_runs_to_fit(const LineWords& words,
                                                     WrapRun range,
                                                     Runs& runs)
{
    auto by_words = [&](WrapRun run) { return runs.by_words(run); };
    auto by_clauses = [&](WrapRun sentence) {
        const auto clauses = chunk_word_runs(words, sentence, ";,");
        if (clauses.size() <= 1) {
            return by_words(sentence);
        }
        return greedy_group(clauses, runs, by_words);
    };
    return greedy_group(chunk_word_runs(words, range, ".!?"), runs, by_clauses);
}

/// ``split_runs_to_fit`` for a line of even rows ``line_height_px`` high.
template <typename Row>
[[nodiscard]] std::vector<WrapRun> split_even_rows_to_fit(const LineWords& words,
                                                          WrapRun range,
                                                          Row& row,
                                                          int line_height_px,
                                                          double max_width_px,
                                                          double max_height_px)
{
    range.end = std::min(range.end, words.size());
    const int lh = std::max(1, line_height_px);
    if (range.start >= range.end
        || static_cast<double>(count_rows(range, row, max_width_px)) * lh <= max_height_px) {
        return {range};
    }
    BuiltRows runs(row, max_width_px, std::max(1, static_cast<int>(max_height_px / lh)));
    return split_runs_to_fit(words, range, runs);
}

[[nodiscard]] std::vector<std::string> run_texts(const PlainWords& words,
                                                 const std::vector<WrapRun>& runs)
{
    std::vector<std::string> out;
    out.reserve(runs.size());
    for (const auto& run : runs) {
        out.push_back(words.text(run));
    }
    return out;
}

[[nodiscard]] WrapRun whole(const LineWords& words) noexcept
{
    return {0, words.size()};
}

[[nodiscard]] double pieces_width(const model::RichLine& pieces, const RunMeasurer& measure)
{
    double width = 0.0;
    for (const auto& piece : pieces) {
        width += measure(piece.text, piece.style);
    }
    return width;
}

/// Words ``run`` of ``words`` with the whitespace between them, adjacent
/// pieces of one style merged: the runs a row draws.
[[nodiscard]] model::RichLine row_pieces(const StyledWords& words, WrapRun run)
{
    model::RichLine out;
    const auto append = [&out](const model::TextSpan& piece) {
        model::append_styled(out, piece.text, piece.style);
    };
    for (std::size_t i = run.start; i < run.end; ++i) {
        if (i > run.start) {
            for (const auto& piece : words.gap_before(i)) {
                append(piece);
            }
        }
        for (const auto& piece : words.pieces(i)) {
            append(piece);
        }
    }
    return out;
}

/// Whether a row of ``rows`` is one word wider than ``max_width_px``. Such a
/// word always wraps onto a row of its own, so no other is worth measuring.
template <typename WordWidth>
[[nodiscard]] bool has_wide_row(const std::vector<WrapRun>& rows,
                                double max_width_px,
                                WordWidth&& word_width)
{
    return std::ranges::any_of(rows, [&](const WrapRun& row) {
        return row.end - row.start == 1 && word_width(row.start) > max_width_px;
    });
}

}  // namespace

WidthMeasurer bind_measurer(ITextMeasurer& measurer, FontStyle style)
{
    return [&measurer, style = std::move(style)](std::string_view text) {
        return measurer.measure_width(text, style);
    };
}

FontStyle span_font_style(const FontStyle& box, const model::TextStyle& span)
{
    FontStyle out = box;
    out.size_px = span.size_px > 0.0 ? span.size_px
                                     : box.size_px * (std::max(10, span.size_pct) / 100.0);
    if (!span.font_family.empty()) {
        out.family = span.font_family;
    }
    out.bold = box.bold || span.bold;
    out.italic = box.italic || span.italic;
    return out;
}

double span_margin_px(const FontStyle& span_font, const model::TextStyle& span) noexcept
{
    return span.margin_right_pct > 0 ? span_font.size_px * (span.margin_right_pct / 100.0) : 0.0;
}

RunMeasurer bind_run_measurer(ITextMeasurer& measurer, FontStyle box)
{
    return [&measurer, box = std::move(box)](std::string_view text,
                                              const model::TextStyle& style) {
        if (!style.has_inline_style()) {
            return measurer.measure_width(text, box);  // the common case: no copy of the font
        }
        const auto font = span_font_style(box, style);
        return measurer.measure_width(text, font) + span_margin_px(font, style);
    };
}

PlainWords::PlainWords(std::string_view text)
{
    plains_ = util::split_words(text);
}

StyledWords StyledWords::marked(std::string_view markup)
{
    StyledWords out;
    model::RichLine word;
    std::optional<model::TextStyle> pending_space;
    model::TextStyle space_style;
    auto finish_word = [&]() {
        if (word.empty()) {
            return;
        }
        out.plains_.push_back(model::plain_text(word));
        out.pieces_.push_back(std::move(word));
        // The first word's gap is never drawn.
        out.gaps_.push_back(model::RichLine{model::TextSpan{" ", space_style}});
        word.clear();
    };
    for (const auto& span : model::parse_inline_markup(markup)) {
        std::size_t i = 0;
        while (i < span.text.size()) {
            const bool space = util::is_space(span.text[i]);
            std::size_t j = i;
            while (j < span.text.size() && util::is_space(span.text[j]) == space) {
                ++j;
            }
            if (space) {
                finish_word();
                if (!pending_space) {
                    pending_space = span.style;
                }
            } else {
                if (word.empty() && pending_space) {
                    space_style = *pending_space;
                }
                pending_space.reset();
                word.push_back({span.text.substr(i, j - i), span.style});
            }
            i = j;
        }
    }
    finish_word();
    return out;
}

StyledWords StyledWords::spans(const model::RichLine& line)
{
    StyledWords out;
    model::RichLine word;
    model::RichLine gap;
    bool after_margin = false;  // ``margin_for_space``: no more space than that
    auto finish_word = [&]() {
        if (word.empty()) {
            return;
        }
        out.plains_.push_back(model::plain_text(word));
        out.pieces_.push_back(std::move(word));
        out.gaps_.push_back(std::move(gap));
        word.clear();
        gap.clear();
    };
    for (const auto& span : line) {
        std::size_t i = 0;
        while (i < span.text.size()) {
            const bool space = util::is_space(span.text[i]);
            std::size_t j = i;
            while (j < span.text.size() && util::is_space(span.text[j]) == space) {
                ++j;
            }
            const std::string_view piece(span.text.data() + i, j - i);
            if (space) {
                finish_word();
                // Whitespace before the first word is not kept.
                if (!out.pieces_.empty() && !after_margin) {
                    model::append_styled(gap, piece, span.style);
                }
            } else {
                after_margin = false;
                model::append_styled(word, piece, span.style);
            }
            i = j;
        }
        if (span.style.margin_for_space && !span.text.empty()) {
            finish_word();
            gap.clear();
            after_margin = true;
        }
    }
    finish_word();  // whitespace after the last word is dropped with ``gap``
    return out;
}

WrapRun StyledWords::break_wide_words(const RunMeasurer& measure, double max_width_px, WrapRun run)
{
    run.end = std::min(run.end, size());
    if (empty() || !(max_width_px > 0.0)) {
        return run;
    }
    WrapRun moved;
    std::vector<std::string> plains;
    std::vector<model::RichLine> pieces;
    std::vector<model::RichLine> gaps;
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        if (i == run.start) {
            moved.start = pieces.size();
        }
        if (i == run.end) {
            moved.end = pieces.size();
        }
        // A run whose margin is the space after it is a word of its own, a verse
        // number: cut, it is no longer one.
        const bool whole = std::ranges::any_of(
            pieces_[i], [](const model::TextSpan& sp) { return sp.style.margin_for_space; });
        if (i < run.start || i >= run.end || whole || width(i, i + 1, measure) <= max_width_px) {
            plains.push_back(std::move(plains_[i]));
            pieces.push_back(std::move(pieces_[i]));
            gaps.push_back(std::move(gaps_[i]));
            continue;
        }
        bool first = true;
        auto emit = [&](model::RichLine fragment) {
            plains.push_back(model::plain_text(fragment));
            pieces.push_back(std::move(fragment));
            // Only the first piece keeps the whitespace before the word.
            gaps.push_back(first ? std::move(gaps_[i]) : model::RichLine{});
            first = false;
        };
        model::RichLine current;
        for (const auto& piece : pieces_[i]) {
            std::size_t k = 0;
            while (k < piece.text.size()) {
                const std::size_t n = std::min(
                    util::utf8_sequence_length(static_cast<unsigned char>(piece.text[k])),
                    piece.text.size() - k);
                const std::string_view ch(piece.text.data() + k, n);
                model::RichLine trial = current;
                model::append_styled(trial, ch, piece.style);
                if (!current.empty() && pieces_width(trial, measure) > max_width_px) {
                    emit(std::move(current));
                    current.clear();
                    model::append_styled(current, ch, piece.style);
                } else {
                    current = std::move(trial);
                }
                k += n;
            }
        }
        if (!current.empty()) {
            emit(std::move(current));
        }
    }
    if (run.end >= pieces_.size()) {
        moved.end = pieces.size();
    }
    plains_ = std::move(plains);
    pieces_ = std::move(pieces);
    gaps_ = std::move(gaps);
    return moved;
}

WrapRun PlainWords::break_wide_words(const WidthMeasurer& measure, double max_width_px, WrapRun run)
{
    run.end = std::min(run.end, size());
    if (!(max_width_px > 0.0)) {
        return run;
    }
    WrapRun moved;
    std::vector<std::string> plains;
    std::vector<char> joined;
    for (std::size_t i = 0; i < plains_.size(); ++i) {
        if (i == run.start) {
            moved.start = plains.size();
        }
        if (i == run.end) {
            moved.end = plains.size();
        }
        const char was_joined = joined_.empty() ? 0 : joined_[i];
        if (i < run.start || i >= run.end || measure(plains_[i]) <= max_width_px) {
            plains.push_back(std::move(plains_[i]));
            joined.push_back(was_joined);
            continue;
        }
        bool first = true;
        for (auto& piece : cut_text_to_width(plains_[i], measure, max_width_px)) {
            plains.push_back(std::move(piece));
            joined.push_back(first ? was_joined : 1);
            first = false;
        }
    }
    if (run.end >= plains_.size()) {
        moved.end = plains.size();
    }
    plains_ = std::move(plains);
    joined_ = std::move(joined);
    return moved;
}

std::string PlainWords::text(std::size_t begin, std::size_t end) const
{
    std::string out;
    join_words(plains_, joined_, begin, std::min(end, plains_.size()), out);
    return out;
}

std::string StyledWords::text(std::size_t begin, std::size_t end) const
{
    end = std::min(end, plains_.size());
    model::RichLine line;
    const auto append = [&line](const model::RichLine& pieces) {
        for (const auto& piece : pieces) {
            model::append_styled(line, piece.text, piece.style);
        }
    };
    for (std::size_t i = begin; i < end; ++i) {
        if (i > begin) {
            append(gaps_[i]);
        }
        append(pieces_[i]);
    }
    return model::format_inline_markup(line);
}

double StyledWords::width(std::size_t begin, std::size_t end, const RunMeasurer& measure) const
{
    end = std::min(end, plains_.size());
    if (begin >= end) {
        return 0.0;
    }
    StyledRow row(*this, measure);
    row.start(begin);
    for (std::size_t i = begin + 1; i < end; ++i) {
        (void)row.extend(i);
    }
    return row.width();
}

std::vector<WrapRun> wrap_word_runs(const PlainWords& words,
                                    std::size_t begin,
                                    std::size_t end,
                                    const WidthMeasurer& measure,
                                    double max_width_px)
{
    PlainRow row(words, measure);
    return wrap_runs({begin, std::min(end, words.size())}, row, max_width_px);
}

std::vector<WrapRun> wrap_word_runs(const StyledWords& words,
                                    std::size_t begin,
                                    std::size_t end,
                                    const RunMeasurer& measure,
                                    double max_width_px)
{
    StyledRow row(words, measure);
    return wrap_runs({begin, std::min(end, words.size())}, row, max_width_px);
}

std::vector<model::RichLine> wrap_styled_rows(const StyledWords& words,
                                              WrapRun run,
                                              const RunMeasurer& measure,
                                              double max_width_px)
{
    run.end = std::min(run.end, words.size());
    const auto draw = [](const StyledWords& source, const std::vector<WrapRun>& rows) {
        std::vector<model::RichLine> out;
        out.reserve(rows.size());
        for (const auto& row : rows) {
            out.push_back(row_pieces(source, row));
        }
        return out;
    };
    const auto rows = wrap_word_runs(words, run.start, run.end, measure, max_width_px);
    const bool wide = has_wide_row(rows, max_width_px, [&](std::size_t i) {
        return words.width(i, i + 1, measure);
    });
    if (!wide) {
        return draw(words, rows);
    }
    StyledWords cut = words;
    const WrapRun moved = cut.break_wide_words(measure, max_width_px, run);
    return draw(cut, wrap_word_runs(cut, moved.start, moved.end, measure, max_width_px));
}

std::vector<model::RichLine> wrap_styled_rows(const StyledWords& words,
                                              WrapRun run,
                                              const RunMeasurer& measure,
                                              double max_width_px,
                                              double first_width_px)
{
    run.end = std::min(run.end, words.size());
    if (run.start >= run.end) {
        return {};
    }
    // Words too wide for the narrower row are cut to the full width first, so
    // a cut word's pieces fill rows as they would without the indent.
    StyledWords cut = words;
    const double narrow = std::min(first_width_px, max_width_px);
    const WrapRun moved = cut.break_wide_words(measure, narrow, run);
    const auto first = wrap_word_runs(cut, moved.start, moved.end, measure, first_width_px);
    std::vector<model::RichLine> out;
    if (first.empty()) {
        return out;
    }
    out.push_back(row_pieces(cut, first.front()));
    for (const auto& row :
         wrap_word_runs(cut, first.front().end, moved.end, measure, max_width_px)) {
        out.push_back(row_pieces(cut, row));
    }
    return out;
}

bool has_word_wider_than(const StyledWords& words,
                         WrapRun run,
                         const RunMeasurer& measure,
                         double max_width_px)
{
    run.end = std::min(run.end, words.size());
    return has_wide_row(wrap_word_runs(words, run.start, run.end, measure, max_width_px),
                        max_width_px,
                        [&](std::size_t i) { return words.width(i, i + 1, measure); });
}

std::vector<std::string> cut_text_to_width(std::string_view text,
                                           const WidthMeasurer& measure,
                                           double max_width_px)
{
    std::vector<std::string> out;
    std::string current;
    std::size_t k = 0;
    while (k < text.size()) {
        const std::size_t n =
            std::min(util::utf8_sequence_length(static_cast<unsigned char>(text[k])), text.size() - k);
        const std::string_view ch = text.substr(k, n);
        if (!current.empty() && measure(current + std::string(ch)) > max_width_px) {
            out.push_back(std::move(current));
            current.clear();
        }
        current += ch;
        k += n;
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

int row_size_pct(const model::RichLine& runs) noexcept
{
    int pct = 0;
    for (const auto& run : runs) {
        pct = std::max(pct, run.style.size_pct);
    }
    return pct > 0 ? pct : 100;
}

int wrapped_row_count(const StyledWords& words,
                      WrapRun run,
                      const RunMeasurer& measure,
                      double max_width_px)
{
    run.end = std::min(run.end, words.size());
    StyledRow row(words, measure);
    return count_rows(run, row, max_width_px);
}

std::vector<WrapRun> chunk_word_runs(const LineWords& words,
                                     WrapRun range,
                                     std::string_view delimiters)
{
    std::vector<WrapRun> out;
    range.end = std::min(range.end, words.size());
    std::size_t start = range.start;
    for (std::size_t i = range.start; i < range.end; ++i) {
        if (ends_chunk(words.plain(i), delimiters)) {
            out.push_back({start, i + 1});
            start = i + 1;
        }
    }
    if (start < range.end) {
        out.push_back({start, range.end});
    }
    return out;
}

std::vector<WrapRun> split_word_runs_to_fit(const PlainWords& words,
                                            WrapRun range,
                                            const WidthMeasurer& measure,
                                            int line_height_px,
                                            double max_width_px,
                                            double max_height_px)
{
    PlainRow row(words, measure);
    return split_even_rows_to_fit(words, range, row, line_height_px, max_width_px, max_height_px);
}

std::vector<WrapRun> split_word_runs_to_fit(const LineWords& words,
                                            WrapRun range,
                                            const RunFits& fits)
{
    range.end = std::min(range.end, words.size());
    if (range.start >= range.end || fits(range)) {
        return {range};
    }
    AskedRuns runs(fits);
    return split_runs_to_fit(words, range, runs);
}

std::vector<std::string> wrap_plain_text_to_width(
    std::string_view plain_text,
    const WidthMeasurer& measure,
    double max_width_px)
{
    PlainWords words(plain_text);
    if (words.empty()) {
        return {""};
    }
    auto rows = wrap_word_runs(words, 0, words.size(), measure, max_width_px);
    const bool wide = has_wide_row(rows, max_width_px, [&](std::size_t i) {
        return measure(words.plain(i));
    });
    if (wide) {
        const WrapRun moved = words.break_wide_words(measure, max_width_px, whole(words));
        rows = wrap_word_runs(words, moved.start, moved.end, measure, max_width_px);
    }
    return run_texts(words, rows);
}

std::vector<std::string> wrap_marked_text_to_width(
    std::string_view text,
    const RunMeasurer& measure,
    double max_width_px)
{
    const auto words = StyledWords::marked(text);
    const auto rows = wrap_styled_rows(words, whole(words), measure, max_width_px);
    if (rows.size() <= 1) {
        return {std::string(text)};
    }
    std::vector<std::string> out;
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back(model::format_inline_markup(row));
    }
    return out;
}

std::vector<std::string> split_line_to_fit(
    std::string_view text,
    const WidthMeasurer& measure,
    int line_height_px,
    double max_width_px,
    double max_height_px)
{
    const PlainWords words(text);
    const bool wide_word = std::ranges::any_of(
        std::views::iota(std::size_t{0}, words.size()),
        [&](std::size_t i) { return measure(words.plain(i)) > max_width_px; });
    if (!wide_word) {
        const auto runs = split_word_runs_to_fit(words, whole(words), measure, line_height_px,
                                                 max_width_px, max_height_px);
        return runs.size() <= 1 ? std::vector<std::string>{std::string(text)}
                                : run_texts(words, runs);
    }
    // As many rows as a word too wide for the box is cut into, as it paints.
    const int max_rows =
        std::max(1, static_cast<int>(max_height_px / std::max(1, line_height_px)));
    const RunFits fits = [&](WrapRun run) {
        return static_cast<int>(wrap_plain_text_to_width(words.text(run), measure, max_width_px).size())
            <= max_rows;
    };
    const auto runs = split_word_runs_to_fit(words, whole(words), fits);
    if (runs.size() <= 1) {
        return {std::string(text)};
    }
    return run_texts(words, runs);
}

}  // namespace seventhslide::layout
