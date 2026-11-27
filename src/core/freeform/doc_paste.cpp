// Copying slides, and pasting them with the lines they hold.

#include "seventhslide/freeform/doc_edit.hpp"

#include "doc_internal.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace seventhslide::freeform {
namespace {

/// Old cue id -> the id of the cue that copies it.
using Renamed = std::map<std::string, std::string, std::less<>>;

template <class Map>
void rename_keys(Map& map, const Renamed& renamed)
{
    for (const auto& [from, to] : renamed) {
        auto it = map.find(from);
        if (it == map.end()) {
            continue;
        }
        auto value = std::move(it->second);
        map.erase(it);
        map[to] = std::move(value);
    }
}

/// A copy of ``src`` (``clone_slide``), laid out from ``from`` onto ``to``.
/// A box's look for a cue in ``renamed`` becomes its look for the copy.
[[nodiscard]] Slide pasted_copy(Document& doc, const Slide& src, geom::CanvasSize from,
                                geom::CanvasSize to, const Renamed& renamed)
{
    Slide copy = clone_slide(doc, src);
    for (auto& obj : copy.objects) {
        if (from != to) {
            resize_object(obj, from, to);
        }
        rename_keys(obj.cue_overlays, renamed);
        rename_keys(obj.cue_ref_looks, renamed);
    }
    return copy;
}

[[nodiscard]] CueFlow* flow_by_id(Deck& deck, std::string_view id)
{
    const auto it = std::ranges::find(deck.cue_flow, id, &CueFlow::id);
    return it != deck.cue_flow.end() ? &*it : nullptr;
}

[[nodiscard]] const CueFlow* flow_by_id(const std::vector<CueFlow>& flows, std::string_view id)
{
    const auto it = std::ranges::find(flows, id, &CueFlow::id);
    return it != flows.end() ? &*it : nullptr;
}

/// ``flow`` for cue ``id`` on ``deck``, in place of any it had.
void put_flow(Deck& deck, CueFlow flow, std::string id)
{
    flow.id = std::move(id);
    if (auto* had = flow_by_id(deck, flow.id)) {
        *had = std::move(flow);
    } else {
        deck.cue_flow.push_back(std::move(flow));
    }
}

[[nodiscard]] EditResult paste_simple(Document& doc, std::string_view output_id, int after,
                                      const SlideClip& clip, PastedSlides& pasted)
{
    const int at = after + 1;
    for (const auto& deck : doc.decks) {
        if (at < 0 || at > deck.slide_count()) {
            return EditResult::fail("slide index out of range");
        }
    }
    for (auto& deck : doc.decks) {
        std::vector<Slide> slides;
        slides.reserve(clip.entries.size());
        for (const auto& entry : clip.entries) {
            const Slide* src = &entry.slide;
            geom::CanvasSize from = clip.canvas;
            if (deck.output_id != output_id) {
                const auto own = std::ranges::find(entry.own, deck.output_id,
                                                   &SlideClip::Own::output_id);
                if (own != entry.own.end()) {
                    src = &own->slide;
                    from = own->canvas;
                }
            }
            slides.push_back(pasted_copy(doc, *src, from, deck.canvas, {}));
        }
        deck.slides.insert(deck.slides.begin() + at, std::make_move_iterator(slides.begin()),
                           std::make_move_iterator(slides.end()));
        deck.line_to_slide = identity_map(deck.slide_count());
    }
    pasted = {at, at + static_cast<int>(clip.entries.size()) - 1};
    return EditResult::success();
}

/// Where a line's slide comes from as a deck is laid out again: one of the
/// deck's own (``Own``), a copy of one of them for a line pasted again
/// (``Again``), or a copied slide (``Copied``, by entry). Lines in a row with
/// the same source share a slide.
struct Source {
    enum class Kind { Own, Again, Copied };
    Kind kind = Kind::Own;
    int index = 0;

    [[nodiscard]] bool operator==(const Source&) const noexcept = default;
};

[[nodiscard]] EditResult paste_lines(Document& doc, std::string_view output_id, int after,
                                     const SlideClip& clip, PastedSlides& pasted)
{
    const int cues = static_cast<int>(doc.cues.size());
    for (auto& deck : doc.decks) {
        pad_map(deck, cues);
    }
    auto* target = find_deck(doc, output_id);
    if (after < -1 || after >= target->slide_count()) {
        return EditResult::fail("slide index out of range");
    }
    std::map<std::string, int, std::less<>> index_of;
    for (int c = 0; c < cues; ++c) {
        index_of[doc.cues[static_cast<std::size_t>(c)].id] = c;
    }

    // Pasted into another output, a slide lands on its lines; where it was
    // copied, or with its lines gone, they come again.
    const bool elsewhere = clip.output_id != output_id;
    std::map<int, int> landed;  // cue index -> entry, on the target
    std::vector<int> again;
    std::vector<int> lineless;
    for (int j = 0; j < static_cast<int>(clip.entries.size()); ++j) {
        const auto& entry = clip.entries[static_cast<std::size_t>(j)];
        if (entry.cues.empty()) {
            lineless.push_back(j);
            continue;
        }
        bool found = false;
        if (elsewhere) {
            for (const auto& cue : entry.cues) {
                if (const auto it = index_of.find(cue.id); it != index_of.end()) {
                    landed[it->second] = j;
                    found = true;
                }
            }
        }
        if (!found) {
            again.push_back(j);
        }
    }

    // The lines that come again follow the lines of slide ``after``.
    int at = 0;
    for (int c = 0; c < cues; ++c) {
        if (target->line_to_slide[static_cast<std::size_t>(c)] <= after) {
            at = c + 1;
        }
    }
    struct Added {
        int entry = 0;
        std::string from;
    };
    std::vector<Cue> new_cues;
    std::vector<Added> added;
    Renamed renamed;
    for (const int j : again) {
        for (const auto& cue : clip.entries[static_cast<std::size_t>(j)].cues) {
            Cue copy = cue;
            copy.id = alloc_id(doc, 'c');
            renamed[cue.id] = copy.id;
            added.push_back({j, cue.id});
            new_cues.push_back(std::move(copy));
        }
    }
    const int k = static_cast<int>(new_cues.size());
    doc.cues.insert(doc.cues.begin() + at, new_cues.begin(), new_cues.end());

    for (auto& deck : doc.decks) {
        const bool is_target = &deck == target;
        const auto old_map = deck.line_to_slide;
        const auto original = deck.slides;
        const int n_slides = static_cast<int>(original.size());

        std::vector<Source> sources(static_cast<std::size_t>(cues + k));
        for (int p = 0; p < cues + k; ++p) {
            auto& source = sources[static_cast<std::size_t>(p)];
            if (p >= at && p < at + k) {
                const auto& add = added[static_cast<std::size_t>(p - at)];
                const auto it = index_of.find(add.from);
                if (!is_target && it != index_of.end()) {
                    source = {Source::Kind::Again, old_map[static_cast<std::size_t>(it->second)]};
                } else {
                    source = {Source::Kind::Copied, add.entry};
                }
                continue;
            }
            const int c = p < at ? p : p - k;
            const auto hit = landed.find(c);
            source = is_target && hit != landed.end()
                       ? Source{Source::Kind::Copied, hit->second}
                       : Source{Source::Kind::Own, old_map[static_cast<std::size_t>(c)]};
        }

        std::vector<char> holds(static_cast<std::size_t>(n_slides), 0);
        for (const int v : old_map) {
            if (v >= 0 && v < n_slides) {
                holds[static_cast<std::size_t>(v)] = 1;
            }
        }
        std::vector<char> placed(static_cast<std::size_t>(n_slides), 0);
        std::vector<Slide> slides;
        std::vector<int> map(static_cast<std::size_t>(cues + k), 0);
        int after_at = -1;
        // A slide that holds no lines keeps its place among the deck's own.
        int next_lineless = 0;
        const auto place_own = [&](int v) {
            slides.push_back(original[static_cast<std::size_t>(v)]);
            placed[static_cast<std::size_t>(v)] = 1;
            if (is_target && v == after) {
                after_at = static_cast<int>(slides.size()) - 1;
            }
        };
        const auto lineless_before = [&](int limit) {
            for (; next_lineless < limit; ++next_lineless) {
                if (!holds[static_cast<std::size_t>(next_lineless)]) {
                    place_own(next_lineless);
                }
            }
        };
        for (int p = 0; p < cues + k; ++p) {
            const auto& source = sources[static_cast<std::size_t>(p)];
            if (p == 0 || !(source == sources[static_cast<std::size_t>(p - 1)])) {
                switch (source.kind) {
                case Source::Kind::Own:
                    lineless_before(source.index);
                    if (!placed[static_cast<std::size_t>(source.index)]) {
                        place_own(source.index);
                    } else {
                        // The lines that came in between split it: the rest
                        // carry on on a copy.
                        slides.push_back(pasted_copy(doc,
                                                     original[static_cast<std::size_t>(source.index)],
                                                     deck.canvas, deck.canvas, {}));
                    }
                    break;
                case Source::Kind::Again:
                    slides.push_back(pasted_copy(doc, original[static_cast<std::size_t>(source.index)],
                                                 deck.canvas, deck.canvas, renamed));
                    break;
                case Source::Kind::Copied: {
                    const auto& entry = clip.entries[static_cast<std::size_t>(source.index)];
                    const bool came_again = std::ranges::find(again, source.index) != again.end();
                    slides.push_back(pasted_copy(doc, entry.slide, clip.canvas, deck.canvas,
                                                 came_again ? renamed : Renamed{}));
                    if (is_target) {
                        const int here = static_cast<int>(slides.size()) - 1;
                        pasted.first = pasted.first < 0 ? here : pasted.first;
                        pasted.last = here;
                    }
                    break;
                }
                }
            }
            map[static_cast<std::size_t>(p)] = static_cast<int>(slides.size()) - 1;
        }
        lineless_before(n_slides);

        if (is_target && !lineless.empty()) {
            const int at_slide = pasted.last >= 0 ? pasted.last + 1
                               : after_at >= 0    ? after_at + 1
                                                  : static_cast<int>(slides.size());
            std::vector<Slide> alone;
            for (const int j : lineless) {
                alone.push_back(pasted_copy(doc, clip.entries[static_cast<std::size_t>(j)].slide,
                                            clip.canvas, deck.canvas, {}));
            }
            slides.insert(slides.begin() + at_slide, std::make_move_iterator(alone.begin()),
                          std::make_move_iterator(alone.end()));
            for (int& v : map) {
                if (v >= at_slide) {
                    v += static_cast<int>(lineless.size());
                }
            }
            pasted.first = pasted.first < 0 ? at_slide : std::min(pasted.first, at_slide);
            pasted.last = std::max(pasted.last, at_slide + static_cast<int>(lineless.size()) - 1);
        }
        deck.slides = std::move(slides);
        deck.line_to_slide = std::move(map);

        // How the lines flow: a copied slide's as they flowed where it was
        // copied, a line that came again as the line it copies.
        const auto gap = geom::CanvasMapping::resize(clip.canvas, deck.canvas);
        const auto copied_flow = [&](int j, std::string_view id) -> std::optional<CueFlow> {
            const auto* flow = flow_by_id(clip.entries[static_cast<std::size_t>(j)].flows, id);
            if (flow == nullptr) {
                return std::nullopt;
            }
            CueFlow out = *flow;
            out.gap_above = gap.pixels(out.gap_above);
            return out;
        };
        if (is_target) {
            for (const auto& [c, j] : landed) {
                const auto& id = doc.cues[static_cast<std::size_t>(c < at ? c : c + k)].id;
                std::erase_if(deck.cue_flow, [&](const CueFlow& f) { return f.id == id; });
                if (auto flow = copied_flow(j, id)) {
                    put_flow(deck, std::move(*flow), id);
                }
            }
        }
        for (int q = 0; q < k; ++q) {
            const auto& add = added[static_cast<std::size_t>(q)];
            const auto& id = doc.cues[static_cast<std::size_t>(at + q)].id;
            const auto& source = sources[static_cast<std::size_t>(at + q)];
            if (source.kind == Source::Kind::Again) {
                if (const auto* flow = flow_by_id(deck, add.from)) {
                    put_flow(deck, *flow, id);
                }
            } else if (auto flow = copied_flow(add.entry, add.from)) {
                put_flow(deck, std::move(*flow), id);
            }
        }
        // Nothing runs on across the lines that came in.
        for (const int p : {at, at + k}) {
            if (k > 0 && p < cues + k) {
                if (auto* flow = flow_by_id(deck, doc.cues[static_cast<std::size_t>(p)].id)) {
                    flow->runs_on = false;
                    flow->in_word = false;
                }
            }
        }
    }
    return EditResult::success();
}

} // namespace

SlideClip copy_slides(const Document& doc, std::string_view output_id, int first, int last)
{
    SlideClip clip;
    const auto* deck = find_deck(doc, output_id);
    if (deck == nullptr) {
        return clip;
    }
    first = std::max(0, first);
    last = std::min(last, deck->slide_count() - 1);
    clip.output_id = std::string(output_id);
    clip.canvas = deck->canvas;
    const int cues = std::min(static_cast<int>(doc.cues.size()),
                              static_cast<int>(deck->line_to_slide.size()));
    for (int s = first; s <= last; ++s) {
        SlideClip::Entry entry;
        entry.slide = deck->slides[static_cast<std::size_t>(s)];
        if (doc.line_control) {
            for (int c = 0; c < cues; ++c) {
                if (deck->line_to_slide[static_cast<std::size_t>(c)] != s) {
                    continue;
                }
                const auto& cue = doc.cues[static_cast<std::size_t>(c)];
                entry.cues.push_back(cue);
                if (const auto* flow = deck->flow_of(cue)) {
                    entry.flows.push_back(*flow);
                }
            }
        } else {
            for (const auto& other : doc.decks) {
                if (other.output_id != deck->output_id && s < other.slide_count()) {
                    entry.own.push_back(
                        {other.output_id, other.canvas, other.slides[static_cast<std::size_t>(s)]});
                }
            }
        }
        clip.entries.push_back(std::move(entry));
    }
    return clip;
}

EditResult paste_slides(Document& doc, std::string_view output_id, int after,
                        const SlideClip& clip, PastedSlides& pasted)
{
    pasted = {};
    if (clip.entries.empty()) {
        return EditResult::fail("nothing to paste");
    }
    if (find_deck(doc, output_id) == nullptr) {
        return EditResult::fail("unknown deck");
    }
    return doc.line_control ? paste_lines(doc, output_id, after, clip, pasted)
                            : paste_simple(doc, output_id, after, clip, pasted);
}

} // namespace seventhslide::freeform
