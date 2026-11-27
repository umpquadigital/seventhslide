#pragma once

// Freeform document mutations. Invariants live here, not in QML.
// Seam: plain C++20 (no Qt).

#include "seventhslide/canvas/edit_result.hpp"
#include "seventhslide/freeform/document.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <map>

namespace seventhslide::freeform {

// The result of an edit, the z-order operations and the geometry an editor
// writes are canvas/edit_result.hpp's; these are their names here.
using canvas::EditResult;
using canvas::GeometryUnits;
using canvas::ObjectGeometry;
using canvas::ZOrderOp;

enum class ListRowKind {
    Cue,
    Slide,
};

/// One row of the left slide list (cues of a span, then that span's thumb).
struct ListRow {
    ListRowKind kind = ListRowKind::Slide;
    int cue_index = -1;
    int slide_index = -1;
};

[[nodiscard]] std::string alloc_id(Document& doc, char prefix);

/// The deck painting output ``output_id``, or null.
[[nodiscard]] Deck* find_deck(Document& doc, std::string_view output_id);
[[nodiscard]] const Deck* find_deck(const Document& doc, std::string_view output_id);

/// Empty simple-mode item: one blank slide on each output in ``output_ids``.
[[nodiscard]] Document make_document(std::string name,
                                     const std::vector<std::string>& output_ids);

/// Repair maps / ids / simple-mode alignment after JSON load.
void normalize(Document& doc);

/// True when ``blob`` is a Freeform document (schema + decks) at the
/// k_schema_version this build writes. Not for ``{}``.
[[nodiscard]] bool document_is_native(const nlohmann::json& blob);

/// Why ``blob`` cannot be loaded, as a sentence for the operator; empty when
/// it can. A null or ``{}`` blob is an item with no document yet, not a
/// refusal. A document at any other schema_version is refused, as the
/// database is (db/schema.hpp): there is no upgrade step.
[[nodiscard]] std::string document_refusal(const nlohmann::json& blob);

/// Parse and normalize a stored document. ``name`` fills a missing name.
///
/// A null or empty ``{}`` blob is an item with no document yet and loads as an
/// empty document with no decks. Anything else that is not a Freeform document
/// is refused (nullopt): loading it as empty would let the next save overwrite
/// whatever is actually stored.
[[nodiscard]] std::optional<Document> load_document(const nlohmann::json& blob,
                                                    std::string_view name);

/// True when the stored document ``blob`` has a deck for ``output_id``: read
/// off the JSON, so a walk over a library skips the items an output's change
/// does not touch without loading them.
[[nodiscard]] bool document_has_deck(const nlohmann::json& blob, std::string_view output_id);

[[nodiscard]] bool slide_counts_equal(const Document& doc) noexcept;
[[nodiscard]] bool can_disable_line_control(const Document& doc) noexcept;

/// Simple: all decks. Line control: ``output_id`` required (that deck only).
[[nodiscard]] EditResult insert_slide(Document& doc, int index,
                                      std::string_view output_id = {});
[[nodiscard]] EditResult delete_slide(Document& doc, int index,
                                      std::string_view output_id = {});
/// Simple: all decks, identity maps. Line control: move this deck's slide
/// together with its line rows; cue order stays contiguous and every slide
/// keeps at least one line.
[[nodiscard]] EditResult reorder_slides(Document& doc, int from, int to,
                                        std::string_view output_id = {});

[[nodiscard]] EditResult insert_cue(Document& doc, int index, std::string text);
[[nodiscard]] EditResult delete_cue(Document& doc, int index);
/// True when ``delete_cue`` would succeed: line control on, not the last cue,
/// and every slide on every deck still has at least one remaining line.
[[nodiscard]] bool can_delete_cue(const Document& doc, int index) noexcept;
[[nodiscard]] EditResult rename_cue(Document& doc, int index, std::string text);
/// Give the cue a song label (``type`` empty = none), dropping any Bible verse.
[[nodiscard]] EditResult set_cue_verse(Document& doc, int index, std::string type, int number);
/// Give the cue a Bible verse (clamped into range), dropping any song label.
/// A cue that had no verse starts out showing it.
[[nodiscard]] EditResult set_cue_scripture(Document& doc, int index, int chapter, int verse);
/// Show or hide the cue's ``chapter:verse`` in linked text. Fails for a cue
/// with no Bible verse.
[[nodiscard]] EditResult set_cue_ref_shown(Document& doc, int index, bool shown);

/// Grow/shrink slide ``slide_index`` so it ends at ``last_cue`` (inclusive).
[[nodiscard]] EditResult move_span_end(Document& doc, std::string_view output_id,
                                       int slide_index, int last_cue);

/// Place a slide thumb after ``last_cue`` on one deck (left-list drag). Shrinks
/// earlier slides when the drop is above the current span; refuses empties.
[[nodiscard]] EditResult place_slide_thumb(Document& doc, std::string_view output_id,
                                           int slide_index, int last_cue);

/// ``flatten`` is required when counts differ. Flatten keeps ``flatten_from``'s
/// slide count (default: first deck); others are padded or truncated.
[[nodiscard]] EditResult set_line_control(Document& doc, bool on, bool flatten = false,
                                          std::string_view flatten_from = {});

[[nodiscard]] std::vector<ListRow> deck_list_rows(const Document& doc, const Deck& deck);

struct CueSpan {
    int slide_index = 0;
    int first_cue = 0;
    int last_cue = -1; // inclusive; empty span when last < first
};

[[nodiscard]] std::vector<CueSpan> deck_spans(const Document& doc, const Deck& deck);

/// How a shown ``chapter:verse`` looks until a box restyles it: smaller, and
/// raised like a Bible theme's verse numbers.
inline constexpr int k_cue_ref_size_pct = 60;

/// ``cue``'s text without its ``[chord]`` markers. Chords live in the cue's
/// text, as in a song's lyrics; everything that matches or restyles a cue's
/// characters reads them without.
[[nodiscard]] std::string cue_text_markup(const Cue& cue);

/// What text linked to cues shows for ``cue`` on ``deck``: its ``chapter:verse``
/// and a space before its text when it shows its verse there, else its text
/// alone, with no chords. A box's overlay for the cue restyles exactly these
/// characters.
[[nodiscard]] std::string cue_display_markup(const Deck& deck, const Cue& cue);

/// ``cue``'s chords, placed in ``cue_display_markup``'s plain text.
[[nodiscard]] std::vector<layout::ChordMark> cue_display_chords(const Deck& deck, const Cue& cue);

/// ``markup`` (the cue's display, an overlay of it, or that in the upcoming
/// style) with ``cue``'s chords put back in.
[[nodiscard]] std::string with_cue_chords(const Deck& deck, std::string_view markup,
                                          const Cue& cue);

/// ``cue``'s text with ``display_chords`` (placed as ``cue_display_chords``
/// places them) as its chords instead of the ones it has.
[[nodiscard]] std::string cue_text_with_chords(const Deck& deck, const Cue& cue,
                                               std::vector<layout::ChordMark> display_chords);

/// True when ``overlay`` is ``cue_text`` plus optional extra newlines (and/or
/// inline markup). Character content must match after tags and newlines.
[[nodiscard]] bool linked_overlay_matches(std::string_view cue_text,
                                          std::string_view overlay_markup);

/// ``cue`` as ``obj`` draws it with no overlay of it: the number in the box's look
/// (``Object::verse_ref_look``), and where that or the cue's own text gives the
/// line a style, the box's bold, italic and underline on its runs, which a styled
/// line does not otherwise take (``layout::styled_line``).
[[nodiscard]] std::string box_cue_markup(const Deck& deck, const Object& obj, const Cue& cue);

/// Overlay markup for this cue, or ``box_cue_markup`` when missing / mismatched.
[[nodiscard]] std::string effective_cue_markup(const Deck& deck, const Object& obj,
                                               const Cue& cue);

/// ``deck`` shows verse numbers again, where cues show theirs. What boxes have
/// styled of those cues' text stays styled.
void show_deck_verse_refs(Document& doc, Deck& deck);

/// Owned-span overlays joined with newlines (unlink snapshot / editor seed).
[[nodiscard]] std::string linked_snapshot_markup(const Document& doc, const Deck& deck,
                                                 int slide_index, const Object& obj);

/// Split ``obj``'s styled display back into per-cue overlays, none for a cue that
/// reads as the box draws it anyway. Fails when the non-newline characters no
/// longer match the slide's cues.
[[nodiscard]] EditResult split_linked_overlays(const Document& doc, const Deck& deck,
                                               int slide_index, const Object& obj,
                                               std::string_view display_markup,
                                               std::map<std::string, std::string>& out);

/// ``markup`` (a linked box's text as edited, the slide's cues then
/// ``upcoming`` follow cues) cut to the slide's own cues, as
/// ``split_linked_overlays`` reads them. Fails when the follow cues'
/// characters no longer match.
[[nodiscard]] EditResult owned_linked_markup(const Document& doc, const Deck& deck,
                                             int slide_index, int upcoming,
                                             std::string_view markup, std::string& owned);

/// Fit every linked box's overlay for a cue to the cue's new text or verse
/// (``before`` and ``after`` are the cue either side of the edit). The shown
/// verse keeps its style through any change, its number included; the text
/// keeps its style while it is unchanged and resets to the box's when it is
/// edited, as a retyped cue always has.
void carry_cue_overlays(Document& doc, const Cue& before, const Cue& after);

/// Drop overlay keys that do not name a current cue.
void prune_cue_overlays(Document& doc);

[[nodiscard]] Slide* find_slide(Document& doc, std::string_view output_id, int slide_index);
[[nodiscard]] const Slide* find_slide(const Document& doc, std::string_view output_id,
                                      int slide_index);
[[nodiscard]] Object* find_object(Document& doc, std::string_view output_id, int slide_index,
                                  std::string_view object_id);
[[nodiscard]] const Object* find_object(const Document& doc, std::string_view output_id,
                                        int slide_index, std::string_view object_id);

/// Sort ``slide.objects`` back-to-front and renumber ``z`` to ``0..n-1``, so a
/// stacking move is well defined even when objects share a ``z``.
void normalize_object_order(Slide& slide);

/// New objects for ``canvas``. Each is designed once on
/// ``geom::k_reference_canvas`` and carried over as a resize is
/// (``resize_object``), so type and margins keep their proportions on any
/// output.
[[nodiscard]] Object make_text_object(Document& doc, geom::CanvasSize canvas);
[[nodiscard]] Object make_image_object(Document& doc, std::string filename,
                                       geom::CanvasSize canvas);
/// A video object playing ``filename`` (a library video), on show, in a 16:9
/// box.
[[nodiscard]] Object make_video_object(Document& doc, std::string filename,
                                       geom::CanvasSize canvas);
[[nodiscard]] Object make_shape_object(Document& doc, geom::ShapeKind kind,
                                      geom::CanvasSize canvas);
/// A table of ``rows`` x ``columns`` empty cells (each at least 1, at most
/// 20), a header row on, white borders.
[[nodiscard]] Object make_table_object(Document& doc, int rows, int columns, geom::CanvasSize canvas);
/// ``points`` without those on top of the one before (a double click puts
/// its point twice).
[[nodiscard]] std::vector<geom::PathPoint> distinct_points(const std::vector<geom::PathPoint>& points);
/// A drawn shape through ``points`` (canvas px; each smooth or a corner; at
/// least two distinct), its box fitted to its outline, its points as shares
/// of the box. Open, it is a line (white, 4 px, no fill); closed, it fills
/// as a new shape does.
[[nodiscard]] Object make_drawn_object(Document& doc, std::vector<geom::PathPoint> points, bool closed);
/// A drawn shape's box brought to its outline, curves and all (at least 8 px
/// each way, so a straight line has one), staying where it is on the canvas.
void fit_drawn_box(canvas::Object& obj);
[[nodiscard]] Object make_indicator_object(Document& doc, geom::CanvasSize canvas);
[[nodiscard]] Object make_clock_object(Document& doc, geom::CanvasSize canvas);

/// Carry one object's geometry, type sizes, gaps, tracking and stroke through
/// ``mapping``.
void map_object(Object& obj, const geom::CanvasMapping& mapping);

/// ``map_object`` on a stored object, in place: only the mapped fields change,
/// so keys the struct does not model (designer-only flags) survive.
void map_object_json(nlohmann::json& obj, const geom::CanvasMapping& mapping);

/// Carry ``obj`` from ``from`` onto ``to`` as an output resize does
/// (``canvas::resize_object``, with the cue-linked box's sizes).
void resize_object(Object& obj, geom::CanvasSize from, geom::CanvasSize to);
void resize_object_json(nlohmann::json& obj, geom::CanvasSize from, geom::CanvasSize to);

/// Carry ``deck`` onto ``canvas`` the way an output resize does: every object
/// by ``resize_object`` from the deck's own canvas, the cues' gaps with them,
/// and the deck restamped. False, and nothing moved, when the canvases already
/// match or ``canvas`` is not a usable size.
bool resize_deck(Deck& deck, geom::CanvasSize canvas);

/// Lay ``deck`` out on ``canvas`` as playback does: every object is mapped from
/// the deck's own canvas (``CanvasMapping::stretch``) and the deck is
/// restamped. No-op when the canvases already match, or when ``canvas`` is not
/// a usable size. A deck is kept in its output's canvas (``resize_deck`` when
/// the output changes), so this is the net under one that is not.
void fit_deck_to_canvas(Deck& deck, geom::CanvasSize canvas);

/// Output id -> the canvas that output paints.
using DeckCanvases = std::map<std::string, geom::CanvasSize, std::less<>>;

/// The output ``output_id`` now paints ``canvas``: carry its deck over
/// (``resize_deck``). True when the document changed.
bool resize_output_deck(Document& doc, std::string_view output_id, geom::CanvasSize canvas);

/// ``fit_deck_to_canvas`` for every deck whose output is in ``canvases``.
/// Returns true when any deck moved.
bool fit_decks_to_canvases(Document& doc, const DeckCanvases& canvases);

/// True when ``fit_decks_to_canvases`` would move nothing: every deck whose
/// output is in ``canvases`` is already on that canvas, or the canvas is not
/// usable.
[[nodiscard]] bool decks_fit_canvases(const Document& doc, const DeckCanvases& canvases);


/// Copy slide ``index``, all it has, to ``index + 1``. Simple: every deck.
/// Line control: one deck, and the copy holds no lines.
[[nodiscard]] EditResult duplicate_slide(Document& doc, int index,
                                         std::string_view output_id = {});

/// Slides copied from one output's deck, for ``paste_slides``.
struct SlideClip {
    /// Another output's slide in the same place (without line control).
    struct Own {
        std::string output_id;
        geom::CanvasSize canvas = geom::k_reference_canvas;
        Slide slide;
    };
    struct Entry {
        Slide slide;
        /// The lines the slide held (line control), as they were.
        std::vector<Cue> cues;
        /// How those lines flowed on the output copied from.
        std::vector<CueFlow> flows;
        /// Without line control: every other output's slide in its place.
        std::vector<Own> own;
    };
    /// The output copied from, and its canvas.
    std::string output_id;
    geom::CanvasSize canvas = geom::k_reference_canvas;
    std::vector<Entry> entries;
};

/// Slides ``first``..``last`` of ``output_id``'s deck, with their lines.
/// Empty for a deck that is not there.
[[nodiscard]] SlideClip copy_slides(const Document& doc, std::string_view output_id, int first,
                                    int last);

/// Where ``paste_slides`` put the slides, on the deck pasted into.
struct PastedSlides {
    int first = -1;
    int last = -1;
};

/**
 * Paste ``clip`` into ``output_id``'s deck after its slide ``after`` (-1:
 * first), each slide laid out for the deck's canvas.
 *
 * Without line control every deck gets the slides there: the deck pasted
 * into the copies, every other deck its own slides from those places.
 *
 * Under line control a slide comes with its lines. Pasted into another
 * output, it lands on those same lines: the slides that held them there give
 * way, so the lines split as they did where they were copied, and the other
 * lines keep their slides. Pasted where it was copied (or when its lines are
 * gone), its lines come again after the lines of slide ``after``, and every
 * other deck shows them as it shows the lines they copy. A slide that held
 * no lines comes alone, after the rest.
 */
[[nodiscard]] EditResult paste_slides(Document& doc, std::string_view output_id, int after,
                                      const SlideClip& clip, PastedSlides& pasted);

/// ``{0, 1, ..., n-1}``: each line on the slide of the same index.
[[nodiscard]] std::vector<int> identity_map(int n);

} // namespace seventhslide::freeform
