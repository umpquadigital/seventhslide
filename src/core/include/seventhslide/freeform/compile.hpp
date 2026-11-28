#pragma once

// Compile a Freeform document into a playback snapshot.
// ``build_slides(BuildKind::Freeform)`` maps this onto ``SlideBuildResult``.
// Seam: plain C++20.

#include "seventhslide/canvas/object.hpp"
#include "seventhslide/freeform/builds.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/model/transition.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::freeform {

/// An indicator's box (``scene::indicator_box``) and what it shows when there
/// is nothing to count. Its line depends on the cursor, so it is resolved at
/// scene-frame time.
struct CompiledIndicator {
    layout::OverlayBox box;
    IndicatorFallback fallback = IndicatorFallback::Slide;
    /// ``Object::indicator_gap``.
    double gap = k_default_indicator_gap;
};

struct CompiledObject {
    int z = 0;
    std::string id;
    model::Transition transition;
    /// Substituted text box; unset when this object is not text, or dropped.
    std::optional<layout::OverlayBox> text;
    /// A picture, video, shape or clock, as it paints (painted_object).
    std::optional<canvas::Object> object;
    std::optional<CompiledIndicator> indicator;
};

struct CompiledSlide {
    std::vector<CompiledObject> objects;
    SlideBackground background;
    /// How the slide comes in: its own, else a fade as long as its longest
    /// object fade, else a cut.
    model::Transition transition;
    /// How its background comes in: its own, else the deck's; unset is a cut.
    std::optional<model::Transition> background_transition;
    /// Its builds as steps (freeform/builds.hpp): always at least one.
    std::vector<StepPlan> steps{StepPlan{}};
};

struct CompiledDeck {
    /// Output id, as ``Deck::output_id``.
    std::string output_id;
    std::vector<int> line_to_slide;
    std::vector<CompiledSlide> slides;
};

struct CompiledPlayback {
    bool line_control = false;
    std::vector<std::string> lines;
    std::vector<std::string> line_labels;
    /// With line control, the sections the cues play in (``cue_sections``):
    /// compact codes, empty for an unlabeled run.
    std::vector<std::string> verse_codes;
    /// Parallel to ``lines`` with line control: each line's entry in ``verse_codes``.
    std::vector<int> verse_indices;
    std::vector<CompiledDeck> decks;
};

/// Uses ``doc.fields`` (plus an empty value for every discovered token).
/// ``measurer`` packs ``show_upcoming`` leftover lines; null -> approx widths.
/// ``only_outputs``: compile only the decks of these output ids (the outputs a
/// build lays out); null compiles every deck.
[[nodiscard]] CompiledPlayback compile(
    const Document& doc,
    layout::ITextMeasurer* measurer = nullptr,
    const std::set<std::string, std::less<>>* only_outputs = nullptr);

/**
 * Paint box for a text object that owns its own text: geometry, typography and
 * the object's inline markup with ``{token}``s filled from ``values``.
 *
 * Shared with the theme designer - a themed text element is the same object with
 * the live song / passage tokens in ``values``.
 */
[[nodiscard]] layout::OverlayBox text_box_from_object(
    const Object& obj, const std::map<std::string, std::string>& values);

/// Paint text for one object (linked boxes read cues; others use ``obj.text``).
[[nodiscard]] layout::OverlayBox compile_text_object(const Document& doc, const Deck& deck,
                                                     int slide_index, const Object& obj,
                                                     const std::map<std::string, std::string>& values,
                                                     layout::ITextMeasurer& measurer);

/// What editing a cue-linked box shows, and what of it may change.
struct LinkedEdit {
    /// The slide's cues as the box styles them, then the follow cues it shows
    /// in the upcoming style, on the lines it paints them on; no chords, fields
    /// as typed.
    std::string markup;
    /// The slide's first cue, and how many cues of its own and follow cues
    /// ``markup`` holds.
    int first_cue = 0;
    int owned = 0;
    int upcoming = 0;
    /// Per cue shown, from ``first_cue``: the plain length of its text as
    /// shown (``cue_display_markup``, UTF-8 bytes), the spaces ``markup`` has
    /// between it and the cue before when it runs on from that, and its chords
    /// placed in it.
    std::vector<std::size_t> lengths;
    std::vector<std::size_t> spaces_before;
    std::vector<std::vector<layout::ChordMark>> chords;

    [[nodiscard]] bool operator==(const LinkedEdit&) const noexcept = default;
};

/// ``obj`` (a cue-linked text box) on ``slide_index`` as its editor shows it:
/// what the box paints, the follow cues included, with the chords apart so
/// they can be edited whether or not the box shows them. Empty for anything
/// else.
[[nodiscard]] LinkedEdit linked_edit(const Document& doc, const Deck& deck, int slide_index,
                                     const Object& obj, layout::ITextMeasurer& measurer);

/// What ``compile_text_object`` paints, for the editor canvas: its inline
/// markup, and the room above each of its lines, which markup does not hold.
struct ResolvedText {
    std::string markup;
    std::vector<int> gaps_above;
};
[[nodiscard]] ResolvedText resolved_object_text(const Document& doc, const Deck& deck,
                                                int slide_index, const Object& obj,
                                                layout::ITextMeasurer& measurer);

/// Opaque cache key for a slide's painted pixels: objects, field substitution,
/// deck background theme, and live indicator content (cue verses). Ignores
/// document name and selection.
[[nodiscard]] std::string slide_preview_key(const Document& doc, const Deck& deck,
                                            int slide_index);

/// ``slide_preview_key`` for every slide of ``deck``, reading the document's
/// field values once rather than once per slide.
[[nodiscard]] std::vector<std::string> slide_preview_keys(const Document& doc, const Deck& deck);

/// ``slide_preview_key``, remembered: a slide's key is worked out again only
/// when the slide or its place changes, the field values change, or, for a
/// slide that shows the cues (linked text, an indicator), the cues or how its
/// deck lays them out change. An edit then hashes the slides it touched, not
/// the deck.
class SlidePreviewKeys {
public:
    /// Every slide of ``deck``'s key, as ``slide_preview_keys``.
    [[nodiscard]] std::vector<std::string> keys(const Document& doc, const Deck& deck);
    /// One slide's, as ``slide_preview_key``.
    [[nodiscard]] std::string key(const Document& doc, const Deck& deck, int slide_index);

private:
    /// What a slide's key reads beyond the slide.
    struct Context {
        std::map<std::string, std::string> values;
        bool line_control = false;
        std::vector<Cue> cues;
        std::string output_id;
        geom::CanvasSize canvas;
        std::vector<int> line_to_slide;
        std::vector<CueFlow> cue_flow;
        bool hide_verse_refs = false;

        [[nodiscard]] bool operator==(const Context&) const = default;
    };
    struct Entry {
        Slide slide;
        int index = 0;
        bool uses_cues = false;
        std::string key;
    };
    /// Up to date with ``doc`` and ``deck``: entries that read what changed go.
    void sync(const Document& doc, const Deck& deck);
    [[nodiscard]] const std::string& key_of(const Document& doc, const Deck& deck, int slide_index);

    Context context_;
    /// By slide id, for the deck last asked about.
    std::map<std::string, Entry> entries_;
};

/// Which deck a library thumbnail uses, and the content key of its first slide.
/// Empty when the document has no slide to draw. Canvas size is not included:
/// the caller appends it, so a canvas change does not require a re-save.
struct LibraryThumbRef {
    std::string output_id;
    std::string preview_key;
};

[[nodiscard]] std::optional<LibraryThumbRef> library_thumb_ref(const Document& doc);

} // namespace seventhslide::freeform
