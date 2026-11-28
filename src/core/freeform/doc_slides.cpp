// Freeform slides: inserting, deleting, reordering and duplicating them, and
// keeping each deck's cue-to-slide map valid as they move.

#include "seventhslide/freeform/doc_edit.hpp"

#include "doc_internal.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::freeform {
namespace {

void shift_map_insert_slide(Deck& deck, int index)
{
    for (int& v : deck.line_to_slide) {
        if (v >= index) {
            ++v;
        }
    }
}

void shift_map_delete_slide(Deck& deck, int index)
{
    const int last = std::max(0, deck.slide_count() - 1);
    for (int& v : deck.line_to_slide) {
        if (v == index) {
            v = std::max(0, index - 1);
        } else if (v > index) {
            --v;
        }
        if (v > last) {
            v = last;
        }
    }
}

struct CueGroup {
    int slide_index = 0;
    std::vector<int> cue_indices;
};

[[nodiscard]] std::vector<CueGroup> cue_groups(const Deck& deck, int cue_count)
{
    std::vector<CueGroup> groups;
    groups.reserve(static_cast<std::size_t>(std::max(0, deck.slide_count())));
    for (int s = 0; s < deck.slide_count(); ++s) {
        CueGroup g;
        g.slide_index = s;
        for (int c = 0; c < cue_count; ++c) {
            if (c < static_cast<int>(deck.line_to_slide.size())
                && deck.line_to_slide[static_cast<std::size_t>(c)] == s) {
                g.cue_indices.push_back(c);
            }
        }
        if (!g.cue_indices.empty()) {
            groups.push_back(std::move(g));
        }
    }
    return groups;
}

void permute_cues(Document& doc, const std::vector<int>& new_order)
{
    const int n = static_cast<int>(doc.cues.size());
    if (static_cast<int>(new_order.size()) != n) {
        return;
    }
    // Each cue's section where it was.
    const auto sections = cue_sections(doc.cues).cue_section;
    std::vector<Cue> next(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const int old = new_order[static_cast<std::size_t>(i)];
        if (old < 0 || old >= n) {
            return;
        }
        next[static_cast<std::size_t>(i)] = std::move(doc.cues[static_cast<std::size_t>(old)]);
    }
    doc.cues = std::move(next);
    // A cue with another before it now starts a line of its own, and a
    // section where the cue now before it was in another: a verse moved next
    // to its repeat stays apart from it, and a verse's own slides swapped
    // stay one section.
    for (int i = 0; i < n; ++i) {
        const int old = new_order[static_cast<std::size_t>(i)];
        if (i > 0 && new_order[static_cast<std::size_t>(i - 1)] == old - 1) {
            continue;
        }
        const auto before = i > 0 ? new_order[static_cast<std::size_t>(i - 1)] : -1;
        doc.cues[static_cast<std::size_t>(i)].section_start =
            before >= 0
            && sections[static_cast<std::size_t>(old)] != sections[static_cast<std::size_t>(before)];
        const auto& id = doc.cues[static_cast<std::size_t>(i)].id;
        for (auto& deck : doc.decks) {
            for (auto& flow : deck.cue_flow) {
                if (flow.id == id) {
                    flow.runs_on = false;
                    flow.in_word = false;
                }
            }
        }
    }
    for (auto& deck : doc.decks) {
        pad_map(deck, n);
        std::vector<int> mapped(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            const int old = new_order[static_cast<std::size_t>(i)];
            mapped[static_cast<std::size_t>(i)] =
                deck.line_to_slide[static_cast<std::size_t>(old)];
        }
        deck.line_to_slide = std::move(mapped);
    }
    tidy_section_starts(doc.cues);
}

void compact_deck_slides(Deck& deck)
{
    const int n = deck.slide_count();
    if (n <= 0) {
        return;
    }
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(n));
    std::vector<char> seen(static_cast<std::size_t>(n), 0);
    for (int v : deck.line_to_slide) {
        if (v >= 0 && v < n && !seen[static_cast<std::size_t>(v)]) {
            seen[static_cast<std::size_t>(v)] = 1;
            order.push_back(v);
        }
    }
    for (int s = 0; s < n; ++s) {
        if (!seen[static_cast<std::size_t>(s)]) {
            order.push_back(s);
        }
    }
    std::vector<int> old_to_new(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        old_to_new[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])] = i;
    }
    std::vector<Slide> slides(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        slides[static_cast<std::size_t>(i)] =
            std::move(deck.slides[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])]);
    }
    deck.slides = std::move(slides);
    for (int& v : deck.line_to_slide) {
        if (v >= 0 && v < n) {
            v = old_to_new[static_cast<std::size_t>(v)];
        }
    }
}

[[nodiscard]] EditResult reorder_line_control_groups(Document& doc, Deck& deck, int from, int to)
{
    const int cues = static_cast<int>(doc.cues.size());
    pad_map(deck, cues);
    auto groups = cue_groups(deck, cues);
    int from_g = -1;
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
        if (groups[static_cast<std::size_t>(i)].slide_index == from) {
            from_g = i;
            break;
        }
    }
    if (from_g < 0) {
        return EditResult::fail("slide has no lines");
    }
    int to_g = static_cast<int>(groups.size()) - 1;
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
        if (groups[static_cast<std::size_t>(i)].slide_index == to) {
            to_g = i;
            break;
        }
    }
    if (from_g == to_g) {
        return EditResult::success();
    }
    CueGroup moved = std::move(groups[static_cast<std::size_t>(from_g)]);
    groups.erase(groups.begin() + from_g);
    const int insert_at = std::clamp(to_g, 0, static_cast<int>(groups.size()));
    groups.insert(groups.begin() + insert_at, std::move(moved));

    std::vector<int> new_order;
    new_order.reserve(static_cast<std::size_t>(cues));
    for (const auto& g : groups) {
        new_order.insert(new_order.end(), g.cue_indices.begin(), g.cue_indices.end());
    }
    if (static_cast<int>(new_order.size()) != cues) {
        return EditResult::fail("would orphan line rows");
    }
    permute_cues(doc, new_order);
    for (auto& d : doc.decks) {
        compact_deck_slides(d);
    }
    return EditResult::success();
}

[[nodiscard]] EditResult apply_to_decks(Document& doc, std::string_view output_id, auto&& fn)
{
    if (doc.line_control) {
        auto* deck = find_deck(doc, output_id);
        if (!deck) {
            return EditResult::fail("unknown deck");
        }
        return fn(*deck);
    }
    if (!output_id.empty()) {
        return EditResult::fail("simple mode applies to every output");
    }
    if (doc.decks.empty()) {
        return EditResult::fail("no decks");
    }
    // All or nothing: a deck that refuses must not leave the ones before it
    // changed (simple mode keeps every deck in step). The edits land deck by
    // deck, so each Deck stays where it was for anyone holding it.
    auto edited = doc.decks;
    for (auto& deck : edited) {
        const auto r = fn(deck);
        if (!r) {
            return r;
        }
    }
    for (std::size_t i = 0; i < edited.size(); ++i) {
        doc.decks[i] = std::move(edited[i]);
    }
    return EditResult::success();
}

/// ``slide`` into ``deck`` at ``index``. Under line control the lines stay
/// on the slides they were on, so it holds none.
void put_slide(const Document& doc, Deck& deck, int index, Slide slide)
{
    deck.slides.insert(deck.slides.begin() + index, std::move(slide));
    if (doc.line_control) {
        shift_map_insert_slide(deck, index);
    } else {
        deck.line_to_slide = identity_map(deck.slide_count());
    }
}

} // namespace

EditResult insert_slide(Document& doc, int index, std::string_view output_id)
{
    return apply_to_decks(doc, output_id, [&](Deck& deck) -> EditResult {
        const int n = deck.slide_count();
        if (index < 0 || index > n) {
            return EditResult::fail("slide index out of range");
        }
        put_slide(doc, deck, index, empty_slide(doc));
        return EditResult::success();
    });
}

EditResult delete_slide(Document& doc, int index, std::string_view output_id)
{
    return apply_to_decks(doc, output_id, [&](Deck& deck) -> EditResult {
        if (index < 0 || index >= deck.slide_count()) {
            return EditResult::fail("slide index out of range");
        }
        if (deck.slide_count() <= 1 && !doc.line_control) {
            deck.slides[0] = empty_slide(doc);
            deck.line_to_slide = identity_map(1);
            return EditResult::success();
        }
        deck.slides.erase(deck.slides.begin() + index);
        if (deck.slides.empty()) {
            deck.slides.push_back(empty_slide(doc));
        }
        if (doc.line_control) {
            shift_map_delete_slide(deck, index);
            pad_map(deck, static_cast<int>(doc.cues.size()));
        } else {
            deck.line_to_slide = identity_map(deck.slide_count());
        }
        return EditResult::success();
    });
}

EditResult reorder_slides(Document& doc, int from, int to, std::string_view output_id)
{
    if (doc.line_control) {
        auto* deck = find_deck(doc, output_id);
        if (!deck) {
            return EditResult::fail("unknown deck");
        }
        const int n = deck->slide_count();
        if (from < 0 || from >= n || to < 0 || to >= n) {
            return EditResult::fail("slide index out of range");
        }
        if (from == to) {
            return EditResult::success();
        }
        return reorder_line_control_groups(doc, *deck, from, to);
    }
    return apply_to_decks(doc, output_id, [&](Deck& deck) -> EditResult {
        const int n = deck.slide_count();
        if (from < 0 || from >= n || to < 0 || to >= n) {
            return EditResult::fail("slide index out of range");
        }
        if (from == to) {
            return EditResult::success();
        }
        Slide moved = std::move(deck.slides[static_cast<std::size_t>(from)]);
        deck.slides.erase(deck.slides.begin() + from);
        deck.slides.insert(deck.slides.begin() + to, std::move(moved));
        deck.line_to_slide = identity_map(n);
        return EditResult::success();
    });
}

EditResult duplicate_slide(Document& doc, int index, std::string_view output_id)
{
    return apply_to_decks(doc, output_id, [&](Deck& deck) -> EditResult {
        if (index < 0 || index >= deck.slide_count()) {
            return EditResult::fail("slide index out of range");
        }
        put_slide(doc, deck, index + 1,
                  clone_slide(doc, deck.slides[static_cast<std::size_t>(index)]));
        return EditResult::success();
    });
}

} // namespace seventhslide::freeform
