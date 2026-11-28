#pragma once

// Draft Freeform document + undo + selection. QML is chrome over these commands.
// Everything both canvas editors do lives in canvas::ObjectEditor; this adds outputs
// and slides, objects of each kind, cues and fields, the slide background, and
// a slide clipboard beside the object one.
// Seam: plain C++20 (no Qt).

#include "seventhslide/canvas/object_editor.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/freeform/doc_edit.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::freeform {

struct FieldRow {
    std::string name;
    std::string value;
    bool unused = false;
    /// Some occurrence spells the ``{-name}`` (hide the line when blank) form.
    bool hide = false;
};

/// Where an edit of one slide happens: its deck's place in the document and
/// its own in the deck.
struct SlidePlace {
    std::size_t deck = 0;
    std::size_t slide = 0;

    [[nodiscard]] bool operator==(const SlidePlace&) const noexcept = default;
};

/// The slide at ``place`` in ``doc``, or null.
[[nodiscard]] Slide* slide_at(Document& doc, const SlidePlace& place) noexcept;

/// What an edit of one slide may change: the slide and the id counter.
using SlidePart = canvas::DocPart<Document, SlidePlace, Slide, &slide_at>;

/// In-memory editor session for one library item.
class Editor : public canvas::ObjectEditor<Editor, Document, Object, SlidePart> {
public:
    void load(Document doc, std::string output_id = {});
    void clear();

    [[nodiscard]] const Document& document() const noexcept { return doc_; }

    [[nodiscard]] const std::string& output_id() const noexcept { return output_id_; }
    [[nodiscard]] int slide_index() const noexcept { return slide_index_; }
    /// The slides picked: ``slide_range_first()``..``slide_range_last()``,
    /// the current slide among them.
    [[nodiscard]] int slide_range_first() const noexcept { return range_first_; }
    [[nodiscard]] int slide_range_last() const noexcept { return range_last_; }

    [[nodiscard]] const Deck* current_deck() const;
    [[nodiscard]] const Slide* current_slide() const;

    [[nodiscard]] int slide_count() const noexcept;
    [[nodiscard]] int cue_index() const noexcept { return cue_index_; }
    [[nodiscard]] bool line_control() const noexcept { return doc_.line_control; }
    [[nodiscard]] bool can_disable_line_control() const noexcept
    {
        return freeform::can_disable_line_control(doc_);
    }
    [[nodiscard]] std::vector<FieldRow> field_rows() const;

    /// Edit ``output_id``'s deck. Under line control the line stays, and the
    /// slide shown is the one that holds it in that deck.
    void set_output(std::string_view output_id);
    /// Pick slide ``index`` alone.
    void select_slide(int index);
    /// Pick the slides from the one last picked alone to ``index``, which
    /// becomes the current slide.
    void extend_slide_selection(int index);
    /// Pick every slide; the current one stays.
    void select_all_slides();
    void select_cue(int index);

    [[nodiscard]] EditResult set_name(std::string name);
    [[nodiscard]] EditResult insert_slide();
    [[nodiscard]] EditResult duplicate_slide();
    [[nodiscard]] EditResult delete_slide();
    [[nodiscard]] EditResult reorder_slides(int from, int to);

    /// New objects on the current deck, laid out for the deck's canvas.
    [[nodiscard]] EditResult add_text();
    [[nodiscard]] EditResult add_image(std::string filename);
    [[nodiscard]] EditResult add_video(std::string filename);
    [[nodiscard]] EditResult add_table(int rows, int columns);
    /// A shape drawn through ``points`` (canvas px): make_drawn_object's.
    [[nodiscard]] EditResult add_drawn(std::vector<geom::PathPoint> points, bool closed);
    [[nodiscard]] EditResult add_shape(geom::ShapeKind kind);
    [[nodiscard]] EditResult add_indicator();
    [[nodiscard]] EditResult add_clock();

    [[nodiscard]] EditResult set_text(std::string text);
    [[nodiscard]] EditResult set_linked_style(std::string markup);
    /// The selected cue-linked box edited as ``linked_edit`` showed it, in one
    /// step: ``markup`` (no chords; the slide's cues, then ``upcoming`` follow
    /// cues) restyles the slide's cues as ``set_linked_style`` does, and
    /// ``chords`` (one list per cue shown, from the slide's first, placed as
    /// ``cue_display_chords`` places them) become those cues' chords. Follow
    /// cues keep the upcoming style; only their chords change.
    [[nodiscard]] EditResult set_linked_edit(std::string markup, int upcoming,
                                             std::vector<std::vector<layout::ChordMark>> chords);
    /// Whether ``plain`` keeps the selected cue-linked box's characters
    /// (only line breaks may move), for a box showing ``upcoming`` follow
    /// cues; true for any other box.
    [[nodiscard]] bool linked_edit_allowed(std::string_view plain, int upcoming) const;
    [[nodiscard]] EditResult set_cue_linked(bool on);
    [[nodiscard]] EditResult set_show_upcoming(bool on);

    /// The current slide's background: None, a colour, a picture or a video.
    /// A background theme is not linked; it is copied in with
    /// ``set_background_of``.
    [[nodiscard]] EditResult set_slide_background(SlideBackground background);
    /// Slide ``slide_id``'s background and its transition, as copied from a
    /// background theme (freeform::background_copy_of), whichever slide is
    /// current: a copy whose file was still coming in lands where it was
    /// asked for. Fails for a slide that is gone.
    [[nodiscard]] EditResult set_background_of(std::string_view slide_id,
                                               SlideBackground background,
                                               std::optional<model::Transition> transition);
    // --- builds ---
    /// One animation of the current slide, with its object.
    struct SlideAnimation {
        std::string object_id;
        canvas::Animation animation;
    };
    /// The current slide's animations, in its order.
    [[nodiscard]] std::vector<SlideAnimation> slide_animations() const;
    /// ``effect`` on each selected object, after the slide's last: the first
    /// on a click, the rest with it.
    [[nodiscard]] EditResult add_animation(canvas::AnimationEffect effect);
    /// Object ``object_id``'s animation ``order``, as ``value`` (its place
    /// kept).
    [[nodiscard]] EditResult set_animation(std::string_view object_id, int order,
                                           canvas::Animation value);
    [[nodiscard]] EditResult remove_animation(std::string_view object_id, int order);
    /// The animation at ``from`` in the slide's order, to ``to``.
    [[nodiscard]] EditResult move_animation(int from, int to);

    /// How the current slide comes in, or every slide with ``all_slides``.
    /// Unset hands it back to the slide's objects' fades.
    [[nodiscard]] EditResult set_slide_transition(std::optional<model::Transition> spec,
                                                  bool all_slides = false);
    /// The current slide's speaker notes. The same words again: no step.
    [[nodiscard]] EditResult set_slide_notes(std::string notes);
    [[nodiscard]] EditResult set_slide_background_transition(
        std::optional<model::Transition> spec);
    [[nodiscard]] EditResult set_object_transition(model::Transition spec);
    [[nodiscard]] EditResult set_field(std::string name, std::string value);
    /// Declare a field before any ``{token}`` uses it. Fails on an illegal or taken name.
    [[nodiscard]] EditResult add_field(std::string name);
    /// Move the value and rewrite every ``{from}`` / ``{-from}`` in the document.
    [[nodiscard]] EditResult rename_field(std::string from, std::string to);
    /// Drop the value and strip every ``{name}`` / ``{-name}`` from the document.
    [[nodiscard]] EditResult delete_field(std::string name);

    [[nodiscard]] EditResult set_line_control(bool on, bool flatten = false);
    [[nodiscard]] EditResult insert_cue(std::string text = {});
    [[nodiscard]] EditResult delete_cue();
    [[nodiscard]] bool can_delete_cue() const noexcept;
    [[nodiscard]] EditResult rename_cue(std::string text);
    [[nodiscard]] EditResult set_cue_verse(std::string type, int number);
    [[nodiscard]] EditResult set_cue_scripture(int chapter, int verse);
    [[nodiscard]] EditResult set_cue_ref_shown(bool shown);
    /// ``freeform::set_cue_section_start`` on the selected cue.
    [[nodiscard]] EditResult set_cue_section_start(bool on);
    [[nodiscard]] EditResult place_slide_thumb(int slide_index, int last_cue);

    /// Drop fields that could never be a token. Call immediately before persisting.
    /// Unused but legal fields are kept - they are the operator's to delete.
    void prune_on_save();

    /// The object clipboard is canvas::ObjectEditor's; the slide clipboard sits beside
    /// it, and whichever was filled last is what pastes.
    [[nodiscard]] EditResult copy_selected();
    /// The slides picked, with their lines (``copy_slides``).
    [[nodiscard]] EditResult copy_slides();
    /// Copied slides go after the last slide picked (``paste_slides``) and
    /// become the slides picked; copied objects land on the current slide.
    [[nodiscard]] EditResult paste();
    [[nodiscard]] bool can_paste() const noexcept;

private:
    friend class canvas::ObjectEditor<Editor, Document, Object, SlidePart>;
    using Base = canvas::ObjectEditor<Editor, Document, Object, SlidePart>;

    static constexpr auto k_units = GeometryUnits::Exact;
    static constexpr int k_z_step = 1;

    [[nodiscard]] std::vector<Object>* stack();
    [[nodiscard]] const std::vector<Object>* stack() const;
    /// Where the current slide is.
    [[nodiscard]] std::optional<SlidePlace> here() const;
    /// Canvas of the deck being edited.
    [[nodiscard]] geom::CanvasSize canvas() const { return current_canvas(doc_); }
    [[nodiscard]] static EditResult writable() noexcept { return EditResult::success(); }
    void after_edit() { clamp_view(); }
    void after_restore() { clamp_view(); }
    static void after_gesture() noexcept {}
    [[nodiscard]] std::string mint_id(char kind) { return alloc_id(doc_, kind); }
    static void prepare_copy(Object&) noexcept {}
    [[nodiscard]] static EditResult apply_property(Object& obj,
                                                   std::string_view name,
                                                   const nlohmann::json& value);

    /// Keep the view in the deck, and the slides picked to the current one.
    void clamp_view();
    /// Under line control, the current slide's first cue.
    void pick_current_cue();
    /// The selected cue-linked text box and the deck it is on, or why the
    /// linked-text commands have nothing to edit.
    struct LinkedTextTarget {
        const Object* box = nullptr;
        const Deck* deck = nullptr;
        EditResult error;
    };
    [[nodiscard]] LinkedTextTarget linked_text_target() const;
    /// Canvas of the deck being edited, in ``doc``.
    [[nodiscard]] geom::CanvasSize current_canvas(const Document& doc) const;
    [[nodiscard]] std::string_view slide_deck_arg() const noexcept;

    std::string output_id_;
    int slide_index_ = 0;
    int cue_index_ = -1;
    int range_anchor_ = 0;
    int range_first_ = 0;
    int range_last_ = 0;
    std::optional<SlideClip> clipboard_slides_;
};

} // namespace seventhslide::freeform
