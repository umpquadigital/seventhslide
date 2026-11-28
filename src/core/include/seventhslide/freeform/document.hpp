#pragma once

// Freeform document - source of truth for a library item.
// Playback compiles this; the editor mutates it. Seam: plain C++20 + nlohmann/json.

#include "seventhslide/canvas/object.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/layout/chords.hpp"
#include "seventhslide/model/transition.hpp"
#include "seventhslide/parse/bible.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::freeform {

inline constexpr std::string_view k_schema = "seventhslide.freeform";
/// Written into every document; this build reads exactly this version and
/// refuses any other (see document_refusal). Bump it with any change to the
/// stored shape.
inline constexpr int k_schema_version = 4;

// An object is canvas::Object (its geometry, type, parts and stored form)
// with the cue fields below; these names are its, said once.
using canvas::IndicatorFallback;
using canvas::ObjectType;
using canvas::clamp_indicator_gap;
using canvas::indicator_fallback_from_string;
using canvas::k_default_indicator_gap;
using canvas::k_default_box_fill;
using canvas::k_default_shape_fill;
using canvas::k_default_shape_stroke;
using canvas::k_max_indicator_gap;
using canvas::k_min_indicator_gap;
using canvas::object_type_from_string;
using canvas::read_vertical_align;
using canvas::to_string;

/// A slide's own background. ``None`` paints nothing: the
/// stacks behind show, and on the back stack that is black. A background
/// theme is copied into a slide's own fields, never linked.
enum class SlideBackgroundKind {
    None,
    Color,
    Image,
    Video,
};

[[nodiscard]] std::string_view to_string(SlideBackgroundKind kind) noexcept;
[[nodiscard]] SlideBackgroundKind slide_background_kind_from_string(std::string_view s) noexcept;

struct SlideBackground {
    SlideBackgroundKind kind = SlideBackgroundKind::None;
    /// ``kind == Color``: CSS hex (``#rrggbb``).
    std::string color;
    /// ``kind == Image``: library image filename (or a ``/static/...`` URL).
    std::string image;
    /// ``kind == Video``: library video filename (or a ``/static/...`` URL).
    std::string video;

    [[nodiscard]] bool is_none() const noexcept
    {
        return kind == SlideBackgroundKind::None;
    }

    [[nodiscard]] bool operator==(const SlideBackground&) const noexcept = default;
};

struct Cue {
    std::string id;
    std::string text;
    /// Canonical song label (``Verse``, ``Chorus``, ...). Empty = no indicator.
    std::string verse_type;
    /// Used when ``verse_type`` is set. Clamped to 1-99 on write.
    int verse_number = 1;
    /// The Bible verse the cue is from; ``chapter`` 0 = none. A cue has this
    /// or a song ``verse_type``, never both. Every line of a verse split over
    /// several cues names that verse.
    int chapter = 0;
    int verse = 0;
    /// Text linked to cues shows ``chapter:verse`` before the cue's text. A box
    /// may restyle it like the rest of the cue, but not retype it.
    bool show_ref = false;
    /// The cue starts a section of its own though the cue before has the same
    /// label: a verse played twice running. Only set where it does that
    /// (``tidy_section_starts``); a change of label starts a section anyway.
    bool section_start = false;

    [[nodiscard]] bool operator==(const Cue&) const noexcept = default;
};

/// ``cue``'s Bible verse, or nullopt when it has none.
[[nodiscard]] std::optional<parse::VerseRef> cue_verse_ref(const Cue& cue);

/// What ``cue`` is labelled by on the controller and grouped by in
/// indicators: ``3:16`` for a Bible verse, a song code (``v1``) for a song
/// section, empty for neither.
[[nodiscard]] std::string cue_label_code(const Cue& cue);

/// The sections ``cues`` play in, as indicators show them: a section starts
/// at the first cue, at a change of label (``cue_label_code``), and at a cue
/// marked ``section_start``.
struct CueSections {
    /// Each section's label code, in order; empty for a run with no label.
    std::vector<std::string> codes;
    /// Parallel to ``cues``: the index in ``codes`` of each cue's section.
    std::vector<int> cue_section;

    /// True when cue ``cue`` is the first of its section.
    [[nodiscard]] bool starts_section(int cue) const noexcept
    {
        const auto i = static_cast<std::size_t>(cue);
        return cue >= 0 && i < cue_section.size() && (i == 0 || cue_section[i] != cue_section[i - 1]);
    }
};
[[nodiscard]] CueSections cue_sections(const std::vector<Cue>& cues);

/// True when ``cues[index]`` could be marked ``section_start``: it has a label,
/// and the cue before has the same one, so only the mark parts them.
[[nodiscard]] bool can_start_section(const std::vector<Cue>& cues, int index);

/// Clear ``section_start`` wherever ``can_start_section`` is false, so a mark
/// is only ever where it parts two sections. Every edit that changes a label or
/// which cue follows which calls this.
void tidy_section_starts(std::vector<Cue>& cues);

/// The colour of "show upcoming" cue lines that name none.
inline constexpr std::string_view k_default_upcoming_color = "#888888";

/// A canvas object placed on a slide, and what a text box linked to the
/// document's cues adds.
struct Object : canvas::Object {
    /// How it comes on, is shown off, or goes, step by step, in
    /// its slide's order (``Animation::order``). Empty: it is there from the
    /// slide's start to its end.
    std::vector<canvas::Animation> animations;
    /// When set with document line control, paint this slide's cue texts instead
    /// of ``text``. ``text`` is kept as an unlink snapshot.
    bool cue_linked = false;
    /// Fill leftover box height with later cues (paint only; does not own them).
    bool show_upcoming = false;
    /// Empty -> ``#888888`` at compile when ``show_upcoming``.
    std::string upcoming_color;
    /// 0 -> same as ``font_size``.
    int upcoming_font_size = 0;
    /// Unset -> the box's own ``bold`` / ``italic`` / ``underline``.
    std::optional<bool> upcoming_bold;
    std::optional<bool> upcoming_italic;
    std::optional<bool> upcoming_underline;
    /// A cue-linked box shows its cues' chords over their words (follow
    /// cues' too), in ``chords``' style. Off, it leaves them out.
    bool show_chords = false;
    layout::ChordStyle chords;

    /// Per-cue cosmetic overlay (inline markup), keyed by ``Cue::id``. Extra
    /// newlines are allowed; non-newline characters must match the cue. A
    /// mismatch (or a missing entry) paints the cue with box defaults so a cue
    /// edit resets that line without touching the rest of the slide.
    std::map<std::string, std::string> cue_overlays;
    /// How this box draws a verse number that has no overlay, as markup of a
    /// sample: the theme's look, or the last a number had here. It lasts while
    /// numbers are off, for when they come back. Empty: the default look.
    std::string verse_ref_look;
    /// A cue's number styled unlike that, while the number is off, for when it
    /// comes back. Keyed by cue id; the look is a sample, as above.
    std::map<std::string, std::string> cue_ref_looks;

    [[nodiscard]] bool operator==(const Object&) const noexcept = default;
};

struct Slide {
    std::string id;
    std::vector<Object> objects;
    SlideBackground background;
    /// How the slide comes in: the deck's first slide's is the item's
    /// entrance. Unset: the slide's objects' fades.
    std::optional<model::Transition> transition;
    /// How its background comes in. Unset: the deck's, and with that unset a
    /// cut: nothing of a background theme's reaches a Freeform.
    std::optional<model::Transition> background_transition;
    /// Speaker notes: the operator's own words about the slide, never shown
    /// on an output. Stored as ``notes`` when there are any.
    std::string notes;

    [[nodiscard]] bool operator==(const Slide&) const noexcept = default;
};

/// How a cue follows the one before it in a deck's linked text, where it does
/// not simply start a line of its own. Keyed by the cue's id, so it holds
/// through edits.
struct CueFlow {
    std::string id;
    /// It runs on from the cue before as one line that wraps as one: a song's
    /// line split into cues for another output, shown whole on this one.
    bool runs_on = false;
    /// Running on, nothing comes between it and the cue before: it continues a
    /// word the line was cut inside, or chords that sit against each other.
    bool in_word = false;
    /// Extra room above it, in px, when a line is above it in the box.
    int gap_above = 0;

    [[nodiscard]] bool operator==(const CueFlow&) const noexcept = default;
};

/// One output's slides. A document holds one deck per output it plays on.
struct Deck {
    /// ``OutputConfig::id`` of the output this deck paints. Decks are keyed by
    /// the output's stable id, not its display name, so renaming an output
    /// keeps its slides.
    std::string output_id;
    /// The canvas the slides' objects are laid out on: the output's own. When
    /// the output changes canvas the deck is carried over once
    /// (``resize_deck``); playback maps a deck that is on another canvas all
    /// the same (``geom::CanvasMapping::stretch``).
    geom::CanvasSize canvas = geom::k_reference_canvas;
    std::vector<Slide> slides;
    /// Parallel to ``Document::cues`` when ``line_control``; identity / unused when simple.
    std::vector<int> line_to_slide;
    /// See ``CueFlow``; cues not here just start a line of their own.
    std::vector<CueFlow> cue_flow;
    /// This output shows no verse numbers, whichever cues show theirs elsewhere.
    bool hide_verse_refs = false;

    [[nodiscard]] const CueFlow* flow_of(const Cue& cue) const
    {
        const auto it = std::ranges::find(cue_flow, cue.id, &CueFlow::id);
        return it != cue_flow.end() ? &*it : nullptr;
    }
    [[nodiscard]] int slide_count() const noexcept
    {
        return static_cast<int>(slides.size());
    }

    [[nodiscard]] bool operator==(const Deck&) const noexcept = default;
};

struct Document {
    std::string name;
    bool line_control = false;
    int next_id = 1;
    std::vector<Cue> cues;
    /// Token name -> fill value. Not slot-order.
    std::map<std::string, std::string> fields;
    std::vector<Deck> decks;
    /// Every slide's background transition where the slide sets none; unset,
    /// a cut.
    std::optional<model::Transition> background_transition;

    [[nodiscard]] nlohmann::json to_json() const;
    static Document from_json(const nlohmann::json& j);

    [[nodiscard]] bool operator==(const Document&) const noexcept = default;
};

void to_json(nlohmann::json& j, const Cue& v);
void from_json(const nlohmann::json& j, Cue& v);
void to_json(nlohmann::json& j, const Object& v);
void from_json(const nlohmann::json& j, Object& v);
void to_json(nlohmann::json& j, const SlideBackground& v);
void from_json(const nlohmann::json& j, SlideBackground& v);
void to_json(nlohmann::json& j, const Slide& v);
void from_json(const nlohmann::json& j, Slide& v);
void to_json(nlohmann::json& j, const Deck& v);
void from_json(const nlohmann::json& j, Deck& v);
void to_json(nlohmann::json& j, const Document& v);
void from_json(const nlohmann::json& j, Document& v);

} // namespace seventhslide::freeform
