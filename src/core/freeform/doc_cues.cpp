// Freeform cues, line control, and the spans that tie cues to slides.

#include "seventhslide/freeform/doc_edit.hpp"

#include "doc_internal.hpp"

#include "seventhslide/parse/lyrics_blocks.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::freeform {
namespace {

[[nodiscard]] CueFlow* find_flow(Deck& deck, const std::string& id)
{
    const auto it = std::ranges::find(deck.cue_flow, id, &CueFlow::id);
    return it != deck.cue_flow.end() ? &*it : nullptr;
}

/// ``gone`` leaves ``deck``, ``next`` the cue after it if any. A line ``gone``
/// started is started by ``next`` now, with the room above it; a line it was
/// inside goes on from the cue before.
void forget_cue_flow(Deck& deck, const Cue& gone, const Cue* next)
{
    CueFlow was;
    if (const auto* flow = deck.flow_of(gone)) {
        was = *flow;
        std::erase_if(deck.cue_flow, [&](const CueFlow& f) { return f.id == gone.id; });
    }
    if (next == nullptr) {
        return;
    }
    auto* after = find_flow(deck, next->id);
    if (was.runs_on) {
        if (after != nullptr) {
            after->in_word = after->in_word && was.in_word;
        }
        return;
    }
    if (after != nullptr) {
        after->runs_on = false;
        after->in_word = false;
        after->gap_above = std::max(after->gap_above, was.gap_above);
    } else if (was.gap_above > 0) {
        deck.cue_flow.push_back({.id = next->id, .gap_above = was.gap_above});
    }
}

/// The cue at ``index`` of a line-controlled document, or why there is none
/// to edit.
struct CueTarget {
    Cue* cue = nullptr;
    EditResult error;
};

[[nodiscard]] CueTarget cue_to_edit(Document& doc, int index)
{
    if (!doc.line_control) {
        return {.error = EditResult::fail("line control is off")};
    }
    if (index < 0 || index >= static_cast<int>(doc.cues.size())) {
        return {.error = EditResult::fail("cue index out of range")};
    }
    return {.cue = &doc.cues[static_cast<std::size_t>(index)]};
}

/// Slide ``slide_index`` of the deck on ``output_id``, in a line-controlled
/// document, its cue map sized to the cues; or why there is none to edit.
struct DeckSlideTarget {
    Deck* deck = nullptr;
    EditResult error;
};

[[nodiscard]] DeckSlideTarget deck_slide_to_edit(Document& doc, std::string_view output_id,
                                                 int slide_index)
{
    if (!doc.line_control) {
        return {.error = EditResult::fail("line control is off")};
    }
    auto* deck = find_deck(doc, output_id);
    if (!deck) {
        return {.error = EditResult::fail("unknown deck")};
    }
    if (slide_index < 0 || slide_index >= deck->slide_count()) {
        return {.error = EditResult::fail("slide index out of range")};
    }
    pad_map(*deck, static_cast<int>(doc.cues.size()));
    return {.deck = deck};
}

}  // namespace

EditResult insert_cue(Document& doc, int index, std::string text)
{
    if (!doc.line_control) {
        return EditResult::fail("line control is off");
    }
    const int n = static_cast<int>(doc.cues.size());
    if (index < 0 || index > n) {
        return EditResult::fail("cue index out of range");
    }
    Cue cue;
    cue.id = alloc_id(doc, 'c');
    cue.text = std::move(text);
    if (n > 0) {
        const auto inherit_from = static_cast<std::size_t>(index > 0 ? index - 1 : 0);
        cue.verse_type = doc.cues[inherit_from].verse_type;
        cue.verse_number = doc.cues[inherit_from].verse_number;
        // A line added inside a Bible verse carries it on, without showing it again.
        cue.chapter = doc.cues[inherit_from].chapter;
        cue.verse = doc.cues[inherit_from].verse;
    }
    doc.cues.insert(doc.cues.begin() + index, std::move(cue));
    for (auto& deck : doc.decks) {
        pad_map(deck, n);
        int slide = 0;
        if (!deck.line_to_slide.empty()) {
            if (index == 0) {
                slide = deck.line_to_slide.front();
            } else {
                slide = deck.line_to_slide[static_cast<std::size_t>(index - 1)];
            }
        } else if (deck.slide_count() > 0) {
            slide = 0;
        }
        const int last = std::max(0, deck.slide_count() - 1);
        slide = std::clamp(slide, 0, last);
        deck.line_to_slide.insert(deck.line_to_slide.begin() + index, slide);
        // Nothing runs on from a cue that just arrived.
        if (index + 1 <= n) {
            if (auto* after = find_flow(deck, doc.cues[static_cast<std::size_t>(index + 1)].id)) {
                after->runs_on = false;
                after->in_word = false;
            }
        }
    }
    return EditResult::success();
}

bool can_delete_cue(const Document& doc, int index) noexcept
{
    if (!doc.line_control) {
        return false;
    }
    const int cues = static_cast<int>(doc.cues.size());
    if (cues <= 1 || index < 0 || index >= cues) {
        return false;
    }
    for (const auto& deck : doc.decks) {
        const int n_map = static_cast<int>(deck.line_to_slide.size());
        if (index >= n_map) {
            continue;
        }
        const int slide = deck.line_to_slide[static_cast<std::size_t>(index)];
        int count = 0;
        for (int v : deck.line_to_slide) {
            if (v == slide) {
                ++count;
            }
        }
        if (count <= 1) {
            return false;
        }
    }
    return true;
}

EditResult delete_cue(Document& doc, int index)
{
    if (const auto target = cue_to_edit(doc, index); !target.cue) {
        return target.error;
    }
    if (static_cast<int>(doc.cues.size()) <= 1) {
        return EditResult::fail("at least one cue is required");
    }
    if (!can_delete_cue(doc, index)) {
        return EditResult::fail("would orphan a slide");
    }
    const Cue* next = index + 1 < static_cast<int>(doc.cues.size())
                          ? &doc.cues[static_cast<std::size_t>(index + 1)]
                          : nullptr;
    for (auto& deck : doc.decks) {
        forget_cue_flow(deck, doc.cues[static_cast<std::size_t>(index)], next);
    }
    doc.cues.erase(doc.cues.begin() + index);
    for (auto& deck : doc.decks) {
        if (index < static_cast<int>(deck.line_to_slide.size())) {
            deck.line_to_slide.erase(deck.line_to_slide.begin() + index);
        }
    }
    return EditResult::success();
}

EditResult rename_cue(Document& doc, int index, std::string text)
{
    if (index < 0 || index >= static_cast<int>(doc.cues.size())) {
        return EditResult::fail("cue index out of range");
    }
    auto& cue = doc.cues[static_cast<std::size_t>(index)];
    if (cue.text != text) {
        const Cue before = cue;
        cue.text = std::move(text);
        carry_cue_overlays(doc, before, cue);
    }
    return EditResult::success();
}

EditResult set_cue_verse(Document& doc, int index, std::string type, int number)
{
    auto target = cue_to_edit(doc, index);
    if (!target.cue) {
        return target.error;
    }
    auto& cue = *target.cue;
    const Cue before = cue;
    cue.verse_type = parse::canonicalize_verse_type(type);
    cue.verse_number = cue.verse_type.empty() ? parse::k_min_verse_number
                                              : parse::clamp_verse_number(number);
    cue.chapter = 0;
    cue.verse = 0;
    cue.show_ref = false;
    carry_cue_overlays(doc, before, cue);
    return EditResult::success();
}

EditResult set_cue_scripture(Document& doc, int index, int chapter, int verse)
{
    auto target = cue_to_edit(doc, index);
    if (!target.cue) {
        return target.error;
    }
    auto& cue = *target.cue;
    const Cue before = cue;
    if (before.chapter <= 0) {
        cue.show_ref = true;
    }
    cue.verse_type.clear();
    cue.verse_number = parse::k_min_verse_number;
    cue.chapter = std::clamp(chapter, 1, parse::k_max_bible_chapter);
    cue.verse = std::clamp(verse, 1, parse::k_max_bible_verse);
    carry_cue_overlays(doc, before, cue);
    return EditResult::success();
}

EditResult set_cue_ref_shown(Document& doc, int index, bool shown)
{
    auto target = cue_to_edit(doc, index);
    if (!target.cue) {
        return target.error;
    }
    auto& cue = *target.cue;
    if (cue.chapter <= 0) {
        return EditResult::fail("the cue has no Bible verse");
    }
    const Cue before = cue;
    cue.show_ref = shown;
    carry_cue_overlays(doc, before, cue);
    return EditResult::success();
}

EditResult move_span_end(Document& doc, std::string_view output_id, int slide_index,
                         int last_cue)
{
    const auto target = deck_slide_to_edit(doc, output_id, slide_index);
    if (!target.deck) {
        return target.error;
    }
    auto* deck = target.deck;
    const int cues = static_cast<int>(doc.cues.size());
    if (cues == 0) {
        return EditResult::success();
    }
    if (last_cue < -1 || last_cue >= cues) {
        return EditResult::fail("cue index out of range");
    }
    int start = 0;
    while (start < cues && deck->line_to_slide[static_cast<std::size_t>(start)] < slide_index) {
        ++start;
    }
    const int new_end = last_cue + 1; // exclusive
    if (new_end < start) {
        return EditResult::fail("span end is before span start");
    }
    for (int i = start; i < cues; ++i) {
        int& v = deck->line_to_slide[static_cast<std::size_t>(i)];
        if (i < new_end) {
            v = slide_index;
        } else if (v <= slide_index) {
            v = std::min(slide_index + 1, std::max(0, deck->slide_count() - 1));
        }
    }
    make_non_decreasing(deck->line_to_slide, deck->slide_count());
    return EditResult::success();
}

EditResult place_slide_thumb(Document& doc, std::string_view output_id, int slide_index,
                             int last_cue)
{
    const auto target = deck_slide_to_edit(doc, output_id, slide_index);
    if (!target.deck) {
        return target.error;
    }
    auto* deck = target.deck;
    const int cues = static_cast<int>(doc.cues.size());
    if (cues == 0) {
        return EditResult::success();
    }
    if (last_cue < 0 || last_cue >= cues) {
        return EditResult::fail("cue index out of range");
    }
    if (last_cue < slide_index) {
        return EditResult::fail("would leave a slide without lines");
    }
    auto previous = deck->line_to_slide;
    std::vector<int> before(static_cast<std::size_t>(std::max(0, deck->slide_count())), 0);
    for (int v : previous) {
        if (v >= 0 && v < deck->slide_count()) {
            ++before[static_cast<std::size_t>(v)];
        }
    }
    int start = 0;
    while (start < cues && deck->line_to_slide[static_cast<std::size_t>(start)] < slide_index) {
        ++start;
    }
    if (last_cue < start && slide_index > 0) {
        const auto shrunk = place_slide_thumb(doc, output_id, slide_index - 1, last_cue - 1);
        if (!shrunk) {
            return shrunk;
        }
    }
    const auto moved = move_span_end(doc, output_id, slide_index, last_cue);
    if (!moved) {
        deck->line_to_slide = std::move(previous);
        return moved;
    }
    std::vector<int> after(static_cast<std::size_t>(std::max(0, deck->slide_count())), 0);
    for (int v : deck->line_to_slide) {
        if (v >= 0 && v < deck->slide_count()) {
            ++after[static_cast<std::size_t>(v)];
        }
    }
    for (int s = 0; s < deck->slide_count(); ++s) {
        if (before[static_cast<std::size_t>(s)] > 0 && after[static_cast<std::size_t>(s)] == 0) {
            deck->line_to_slide = std::move(previous);
            return EditResult::fail("would leave a slide without lines");
        }
    }
    return EditResult::success();
}

EditResult set_line_control(Document& doc, bool on, bool flatten, std::string_view flatten_from)
{
    if (on == doc.line_control) {
        return EditResult::success();
    }
    if (on) {
        doc.line_control = true;
        for (auto& deck : doc.decks) {
            if (deck.slides.empty()) {
                deck.slides.push_back(empty_slide(doc));
            }
        }
        if (doc.cues.empty()) {
            // A line for each slide to hold, with no words: anything here
            // would show on a box that shows cues.
            const int slides = doc.decks.empty() ? 1 : doc.decks.front().slide_count();
            for (int i = 0; i < slides; ++i) {
                Cue cue;
                cue.id = alloc_id(doc, 'c');
                doc.cues.push_back(std::move(cue));
            }
        }
        for (auto& deck : doc.decks) {
            if (deck.slide_count() == static_cast<int>(doc.cues.size())) {
                deck.line_to_slide = identity_map(deck.slide_count());
            } else {
                pad_map(deck, static_cast<int>(doc.cues.size()));
            }
        }
        return EditResult::success();
    }

    if (!can_disable_line_control(doc) && !flatten) {
        return EditResult::fail("flatten required: slide counts differ");
    }
    if (!can_disable_line_control(doc) && flatten) {
        const Deck* src = flatten_from.empty() ? nullptr : find_deck(doc, flatten_from);
        if (!src && !doc.decks.empty()) {
            src = &doc.decks.front();
        }
        if (!src) {
            return EditResult::fail("no decks");
        }
        const int n = std::max(1, src->slide_count());
        for (auto& deck : doc.decks) {
            while (deck.slide_count() < n) {
                deck.slides.push_back(empty_slide(doc));
            }
            if (deck.slide_count() > n) {
                deck.slides.resize(static_cast<std::size_t>(n));
            }
        }
    }
    doc.line_control = false;
    doc.cues.clear();
    for (auto& deck : doc.decks) {
        if (deck.slides.empty()) {
            deck.slides.push_back(empty_slide(doc));
        }
        deck.line_to_slide = identity_map(deck.slide_count());
        deck.cue_flow.clear();
    }
    return EditResult::success();
}

std::vector<ListRow> deck_list_rows(const Document& doc, const Deck& deck)
{
    std::vector<ListRow> rows;
    if (!doc.line_control) {
        for (int s = 0; s < deck.slide_count(); ++s) {
            rows.push_back(ListRow{ListRowKind::Slide, -1, s});
        }
        return rows;
    }
    const int cues = static_cast<int>(doc.cues.size());
    std::vector<int> map = deck.line_to_slide;
    if (static_cast<int>(map.size()) != cues) {
        map.resize(static_cast<std::size_t>(std::max(0, cues)),
                   std::max(0, deck.slide_count() - 1));
    }
    for (int s = 0; s < deck.slide_count(); ++s) {
        for (int c = 0; c < cues; ++c) {
            if (map[static_cast<std::size_t>(c)] == s) {
                rows.push_back(ListRow{ListRowKind::Cue, c, s});
            }
        }
        rows.push_back(ListRow{ListRowKind::Slide, -1, s});
    }
    return rows;
}

std::vector<CueSpan> deck_spans(const Document& doc, const Deck& deck)
{
    std::vector<CueSpan> spans;
    const int slides = deck.slide_count();
    const int cues = static_cast<int>(doc.cues.size());
    std::vector<int> map = deck.line_to_slide;
    if (doc.line_control && static_cast<int>(map.size()) != cues) {
        map.resize(static_cast<std::size_t>(std::max(0, cues)),
                   std::max(0, slides - 1));
    }
    for (int s = 0; s < slides; ++s) {
        CueSpan span;
        span.slide_index = s;
        span.first_cue = cues;
        span.last_cue = -1;
        if (doc.line_control) {
            for (int c = 0; c < cues; ++c) {
                if (map[static_cast<std::size_t>(c)] == s) {
                    span.first_cue = std::min(span.first_cue, c);
                    span.last_cue = std::max(span.last_cue, c);
                }
            }
        } else if (s < cues) {
            span.first_cue = s;
            span.last_cue = s;
        }
        spans.push_back(span);
    }
    return spans;
}

} // namespace seventhslide::freeform
