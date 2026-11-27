#include "seventhslide/build/bible_layout.hpp"

#include "seventhslide/model/rich_text.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <span>
#include <utility>

namespace seventhslide::build {
namespace {

[[nodiscard]] std::string verse_ref(int chapter, int verse_num)
{
    return std::to_string(chapter) + ':' + std::to_string(verse_num);
}

[[nodiscard]] layout::FontStyle bible_font_style(const theme::ResolvedTheme& theme)
{
    const auto& bible = theme.bible;
    layout::FontStyle st;
    st.family = bible.font_family;
    st.size_px = static_cast<double>(std::max(1, bible.font_size));
    st.bold = bible.bold;
    st.italic = bible.italic;
    st.line_height = bible.line_height;
    st.letter_spacing = bible.letter_spacing;
    return st;
}

/**
 * Call ``on_word(offset, length, trailing_space)`` for every space-delimited word
 * of ``text``. This is the word boundary a citation marker must sit on.
 */
template <typename OnWord>
void for_each_word(std::string_view text, OnWord&& on_word)
{
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        const std::size_t end = std::min(text.find(' ', i), text.size());
        on_word(i, end - i, end < text.size());
        i = end;
    }
}

/// Width of ``ref`` painted as a citation span: its own size, family and weight
/// plus the right margin.
[[nodiscard]] double painted_citation_px(const layout::FontStyle& box_style,
                                         const BibleCitationStyle& cite,
                                         std::string_view ref,
                                         layout::ITextMeasurer& measurer)
{
    const int base = std::max(1, static_cast<int>(std::lround(box_style.size_px)));
    const double size_px = base * (bible_citation_size_pct(cite, base) / 100.0);

    layout::FontStyle st = box_style;
    st.size_px = size_px;
    if (!cite.font_family.empty()) {
        st.family = cite.font_family;
    }
    st.bold = st.bold || cite.bold;
    st.italic = st.italic || cite.italic;
    return measurer.measure_width(ref, st) + size_px * (std::max(0, cite.margin_right_pct) / 100.0);
}

/// A shown citation and the width it takes in a row: what it paints, or at most
/// a whole row, so that wrapping never takes it for a word to cut.
struct CitationWidth {
    std::string ref;
    double width_px = 0.0;
};

/**
 * ``text`` at ``style`` as paint draws it: each citation at its own width, with
 * no space after it, and the text between them in pieces of its own.
 */
[[nodiscard]] double measure_with_citations(layout::ITextMeasurer& measurer,
                                            const layout::FontStyle& style,
                                            const std::vector<CitationWidth>& citations,
                                            std::string_view text)
{
    if (citations.empty() || text.find(':') == std::string_view::npos) {
        return measurer.measure_width(text, style);
    }
    double w = 0.0;
    std::size_t from = 0;
    for_each_word(text, [&](std::size_t offset, std::size_t length, bool trailing_space) {
        const auto word = text.substr(offset, length);
        const auto it = std::find_if(citations.begin(), citations.end(),
                                     [word](const CitationWidth& c) { return c.ref == word; });
        if (it != citations.end()) {
            if (offset > from) {
                w += measurer.measure_width(text.substr(from, offset - from), style);
            }
            w += it->width_px;
            from = offset + length + (trailing_space ? 1 : 0);
        }
    });
    if (from < text.size()) {
        w += measurer.measure_width(text.substr(from), style);
    }
    return w;
}

/**
 * Fills one output's slides with verse text, a piece at a time.
 *
 * Greedy wrapping never revisits a finished row, so the open slide keeps only
 * how many rows are finished and the text from its last row on. A fit check
 * wraps that tail plus the new text instead of the whole slide. Citation widths
 * are measured only once a slide grows past its first piece.
 */
class SlidePacker {
public:
    SlidePacker(const BibleBoxMetrics& metrics,
                const BibleCitationStyle& cite,
                layout::ITextMeasurer& measurer)
        : metrics_(metrics)
        , cite_(cite)
        , measurer_(measurer)
    {
    }

    /// Append ``pieces`` to the open slide when the grown slide still fits.
    /// ``citation`` is the ``ch:v`` that ``pieces.front()`` starts with, or empty.
    bool try_append(std::span<const std::string_view> pieces, std::string_view citation)
    {
        if (chunk_count_ == 0) {
            return false;
        }
        std::string added;
        for (const auto piece : pieces) {
            added += ' ';
            added += piece;
        }
        if (!citation.empty()) {
            citations_.push_back(CitationWidth{.ref = std::string(citation)});
        }
        auto rows = wrap(tail_ + added);
        if (finished_px_ + rows_px(rows, rows.size()) > metrics_.avail_h) {
            if (!citation.empty()) {
                citations_.pop_back();
                measured_ = std::min(measured_, citations_.size());
            }
            return false;
        }
        text_ += added;
        finished_px_ += rows_px(rows, rows.size() - 1);
        tail_ = std::move(rows.back());
        chunk_count_ += pieces.size() + std::exchange(skipped_, 0);
        return true;
    }

    /// Close the open slide and start the next one with ``piece``.
    void start(std::string_view piece, std::string_view citation)
    {
        close();
        text_ = piece;
        tail_ = piece;
        if (!citation.empty()) {
            citations_.push_back(CitationWidth{.ref = std::string(citation)});
        }
        chunk_count_ = 1 + std::exchange(skipped_, 0);
    }

    /// A chunk this output shows nothing of: it goes with the piece after it.
    void skip_chunk() { ++skipped_; }

    [[nodiscard]] BibleOutputSlides finish()
    {
        close();
        if (out_.slides.empty()) {
            out_.slides.emplace_back();
        }
        return std::move(out_);
    }

private:
    /// The height of the first ``count`` of ``rows``, each as paint opens it.
    [[nodiscard]] int rows_px(const std::vector<std::string>& rows, std::size_t count) const
    {
        int px = 0;
        for (std::size_t r = 0; r < count; ++r) {
            px += has_citation(rows[r]) ? metrics_.citation_row_px : metrics_.line_height_px;
        }
        return px;
    }

    [[nodiscard]] bool has_citation(std::string_view row) const
    {
        if (metrics_.citation_row_px == metrics_.line_height_px
            || row.find(':') == std::string_view::npos) {
            return false;
        }
        bool found = false;
        for_each_word(row, [&](std::size_t offset, std::size_t length, bool /*trailing_space*/) {
            const auto word = row.substr(offset, length);
            found = found || std::ranges::any_of(citations_, [word](const CitationWidth& c) {
                return c.ref == word;
            });
        });
        return found;
    }

    [[nodiscard]] std::vector<std::string> wrap(std::string_view text)
    {
        for (; measured_ < citations_.size(); ++measured_) {
            auto& c = citations_[measured_];
            c.width_px = std::min(painted_citation_px(metrics_.style, cite_, c.ref, measurer_),
                                  static_cast<double>(metrics_.avail_w));
        }
        return layout::wrap_plain_text_to_width(
            text,
            [this](std::string_view row) {
                return measure_with_citations(measurer_, metrics_.style, citations_, row);
            },
            static_cast<double>(metrics_.avail_w));
    }

    void close()
    {
        if (chunk_count_ == 0) {
            return;
        }
        BibleSlide slide;
        slide.text = std::move(text_);
        slide.citations.reserve(citations_.size());
        for (auto& c : citations_) {
            slide.citations.push_back(std::move(c.ref));
        }
        const int slide_idx = static_cast<int>(out_.slides.size());
        out_.slides.push_back(std::move(slide));
        out_.line_to_slide.insert(out_.line_to_slide.end(), chunk_count_, slide_idx);

        text_.clear();
        tail_.clear();
        citations_.clear();
        measured_ = 0;
        finished_px_ = 0;
        chunk_count_ = 0;
    }

    const BibleBoxMetrics& metrics_;
    const BibleCitationStyle& cite_;
    layout::ITextMeasurer& measurer_;
    BibleOutputSlides out_;
    std::string text_;
    /// Text from the open slide's last finished row on (not yet wrapped).
    std::string tail_;
    std::vector<CitationWidth> citations_;
    /// Leading ``citations_`` whose widths are measured.
    std::size_t measured_ = 0;
    int finished_px_ = 0;
    std::size_t chunk_count_ = 0;
    std::size_t skipped_ = 0;
};

}  // namespace

BibleBoxMetrics bible_box_metrics(const theme::ResolvedTheme& theme, layout::ITextMeasurer& measurer)
{
    BibleBoxMetrics m;
    m.style = bible_font_style(theme);
    m.line_height_px = std::max(1, measurer.line_height_px(m.style));
    m.citation_row_px = m.line_height_px;
    if (theme.bible.show_verse_numbers) {
        const int pct = bible_citation_size_pct(bible_citation_style(theme), theme.bible.font_size);
        if (pct > 100) {
            m.citation_row_px = std::max(
                m.line_height_px,
                static_cast<int>(layout::line_box_height_px(
                    m.style.size_px * (pct / 100.0),
                    layout::clamp_css_line_height(m.style.line_height))));
        }
    }
    m.avail_w = std::max(1, theme.bible.width);
    m.avail_h = std::max(1, theme.bible.height);
    return m;
}

BibleCitationStyle bible_citation_style(const theme::ResolvedTheme& theme)
{
    const auto& bible = theme.bible;
    BibleCitationStyle cite;
    cite.shown = bible.show_verse_numbers;
    cite.font_size_px = bible.verse_font_size;
    cite.font_family = bible.verse_font_family;
    cite.bold = bible.verse_font_bold;
    cite.italic = bible.verse_font_italic;
    cite.margin_right_pct = std::clamp(bible.verse_margin_right_pct, 0, model::k_max_gap_pct);
    return cite;
}

int bible_citation_size_pct(const BibleCitationStyle& cite, int body_px)
{
    const int base = std::max(1, body_px);
    const int requested_px = cite.font_size_px > 0 ? cite.font_size_px : std::max(1, base / 2);
    return model::clamp_size_pct(static_cast<int>(std::lround(100.0 * requested_px / base)));
}

std::vector<BibleCitationMarker> find_bible_citation_markers(
    std::string_view text,
    const std::vector<std::string>& citations)
{
    std::vector<BibleCitationMarker> out;
    if (citations.empty() || text.find(':') == std::string_view::npos) {
        return out;
    }
    for_each_word(text, [&](std::size_t offset, std::size_t length, bool trailing_space) {
        const auto word = text.substr(offset, length);
        if (std::find(citations.begin(), citations.end(), word) != citations.end()) {
            out.push_back(BibleCitationMarker{
                .offset = offset, .length = length, .trailing_space = trailing_space});
        }
    });
    return out;
}

layout::WidthMeasurer bible_text_measurer(
    const layout::FontStyle& box_style,
    const BibleCitationStyle& cite,
    const std::vector<std::string>& citations,
    layout::ITextMeasurer& measurer,
    double max_width_px)
{
    if (!cite.shown || citations.empty()) {
        return layout::bind_measurer(measurer, box_style);
    }
    std::vector<CitationWidth> widths;
    widths.reserve(citations.size());
    for (const auto& ref : citations) {
        widths.push_back({ref, std::min(painted_citation_px(box_style, cite, ref, measurer),
                                        max_width_px)});
    }
    return [&measurer, style = box_style, widths = std::move(widths)](std::string_view text) {
        return measure_with_citations(measurer, style, widths, text);
    };
}

std::vector<std::string> chunk_bible_verse(
    int chapter,
    int verse_num,
    std::string_view verse_text,
    const std::vector<const theme::ResolvedTheme*>& bible_themes,
    layout::ITextMeasurer& measurer)
{
    const std::string ref = verse_ref(chapter, verse_num);

    // One space between words, however the source spaced them: rows are rebuilt
    // from words, and a cue's text has to read as they do.
    std::string clean;
    for (std::size_t from = 0; from <= verse_text.size();) {
        const auto end = std::min(verse_text.find('\n', from), verse_text.size());
        if (from > 0) {
            clean.push_back('\n');
        }
        clean += util::collapse_ws(verse_text.substr(from, end - from));
        from = end + 1;
    }

    // Chunks are shared by every output (the player's line indices are global),
    // so they have to satisfy every bible box at once. Ranking the outputs and
    // fitting only the "most restrictive" one is not sound - a box can be the
    // roomiest by line count and still the tightest by width - so apply each
    // box's constraint in turn to whatever the previous passes produced.
    // Subdividing a chunk never makes it stop fitting a box it already fit
    // (fewer words wrap to no more lines), so the result fits all of them.
    std::vector<std::string> chunks{ref + ' ' + clean};
    for (const auto* theme : bible_themes) {
        if (!theme) {
            continue;
        }
        const auto metrics = bible_box_metrics(*theme, measurer);
        const auto measure = bible_text_measurer(metrics.style, bible_citation_style(*theme),
                                                 {ref}, measurer,
                                                 static_cast<double>(metrics.avail_w));
        std::vector<std::string> refined;
        refined.reserve(chunks.size());
        for (const auto& chunk : chunks) {
            auto pieces = layout::split_line_to_fit(
                chunk,
                measure,
                metrics.line_height_px,
                static_cast<double>(metrics.avail_w),
                // Room for the one row that may hold the citation.
                static_cast<double>(metrics.avail_h
                                    - (metrics.citation_row_px - metrics.line_height_px)));
            refined.insert(refined.end(),
                           std::make_move_iterator(pieces.begin()),
                           std::make_move_iterator(pieces.end()));
        }
        chunks = std::move(refined);
    }
    return chunks;
}

BibleChunkPlan plan_bible_chunks(
    int chapter,
    const std::vector<BibleVerseInput>& verses,
    const std::vector<const theme::ResolvedTheme*>& bible_themes,
    layout::ITextMeasurer& measurer)
{
    BibleChunkPlan plan;
    plan.chapter = chapter;
    for (const auto& v : verses) {
        BibleVerseChunks vc;
        vc.verse_num = v.verse_num;
        vc.chunks = chunk_bible_verse(chapter, v.verse_num, v.text, bible_themes, measurer);
        plan.plain_lines.insert(plan.plain_lines.end(), vc.chunks.begin(), vc.chunks.end());
        plan.line_labels.insert(
            plan.line_labels.end(), vc.chunks.size(), verse_ref(chapter, v.verse_num));
        plan.verses.push_back(std::move(vc));
    }
    return plan;
}

BibleOutputSlides build_bible_slides_for_output(
    const theme::ResolvedTheme& theme,
    const BibleChunkPlan& plan,
    layout::ITextMeasurer& measurer)
{
    const auto metrics = bible_box_metrics(theme, measurer);
    const auto cite = bible_citation_style(theme);
    SlidePacker packer(metrics, cite, measurer);

    std::vector<std::string_view> pieces;
    for (const auto& vc : plan.verses) {
        if (vc.chunks.empty()) {
            continue;
        }
        const std::string ref = verse_ref(plan.chapter, vc.verse_num);
        pieces.assign(vc.chunks.begin(), vc.chunks.end());

        // Shown citations keep the ``"ch:v "`` marker so paint wraps where this
        // pass did (fused to ``"ch:vThe"``, the first row could not break and
        // would run past the box). Hidden citations are stripped before measuring.
        std::string_view citation;
        if (cite.shown) {
            citation = ref;
        } else if (const std::string prefix = ref + ' '; pieces.front().starts_with(prefix)) {
            pieces.front().remove_prefix(prefix.size());
        } else if (pieces.front() == ref && pieces.size() > 1) {
            // A box too small for the number and a word cut them apart.
            packer.skip_chunk();
            pieces.erase(pieces.begin());
        }

        if (theme.behavior.bible_fluid_verses && packer.try_append(pieces, citation)) {
            continue;
        }
        packer.start(pieces.front(), citation);
        for (std::size_t i = 1; i < pieces.size(); ++i) {
            if (!packer.try_append(std::span(&pieces[i], 1), {})) {
                packer.start(pieces[i], {});
            }
        }
    }
    return packer.finish();
}

}  // namespace seventhslide::build
