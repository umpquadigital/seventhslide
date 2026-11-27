// What both canvas editors do the same way, because they share
// canvas::ObjectEditor: each case runs against the theme designer's editor
// and the Freeform editor.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/canvas/object_editor.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/theme/element_editor.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Approx;
using seventhslide::freeform::EditResult;
using seventhslide::freeform::Object;
using seventhslide::freeform::ObjectType;
using seventhslide::freeform::ZOrderOp;
using seventhslide::theme::ElementBinding;

namespace {

/// The theme designer's editor, on the song context.
struct ThemeEditor {
    seventhslide::theme::Editor ed;

    ThemeEditor() { ed.load(seventhslide::theme::ElementSet{}); }

    [[nodiscard]] EditResult add() { return ed.insert(ObjectType::Text, ElementBinding::Static); }

    [[nodiscard]] std::vector<std::string> ids() const
    {
        std::vector<std::string> out;
        for (const auto& el : ed.current()) {
            out.push_back(el.object.id);
        }
        return out;
    }

    [[nodiscard]] const Object& object(const std::string& id) const
    {
        return ed.find(id)->object;
    }
};

/// The Freeform editor, on its first slide.
struct FreeformEditor {
    seventhslide::freeform::Editor ed;

    FreeformEditor() { ed.load(seventhslide::freeform::make_document("Doc", {"out"}), "out"); }

    [[nodiscard]] EditResult add() { return ed.add_text(); }

    [[nodiscard]] std::vector<std::string> ids() const
    {
        std::vector<std::string> out;
        for (const auto& obj : ed.current_slide()->objects) {
            out.push_back(obj.id);
        }
        return out;
    }

    [[nodiscard]] const Object& object(const std::string& id) const { return *ed.find(id); }
};

}  // namespace

TEMPLATE_TEST_CASE("undo keeps the selected objects that still exist", "[canvas_editor]",
                   ThemeEditor, FreeformEditor)
{
    TestType t;
    REQUIRE(t.add());
    REQUIRE(t.add());
    const auto ids = t.ids();
    t.ed.select(ids[0]);
    REQUIRE(t.ed.nudge_selected(5, 0));
    REQUIRE(t.ed.undo());
    // The nudged object is still there, so it stays selected.
    CHECK(t.ed.selected_ids() == std::vector<std::string>{ids[0]});

    // Undoing the add of a selected object drops it from the selection.
    t.ed.select(ids[1]);
    REQUIRE(t.ed.undo());
    CHECK(t.ed.selected_ids().empty());
    REQUIRE(t.ed.redo());
    CHECK(t.ed.selected_ids().empty());
}

TEMPLATE_TEST_CASE("delete leaves nothing selected", "[canvas_editor]", ThemeEditor,
                   FreeformEditor)
{
    TestType t;
    REQUIRE(t.add());
    REQUIRE(t.add());
    REQUIRE(t.add());
    const auto ids = t.ids();
    t.ed.select(ids[1]);
    REQUIRE(t.ed.delete_selected());
    CHECK(t.ed.selected_ids().empty());
    CHECK(t.ids() == std::vector<std::string>{ids[0], ids[2]});
    // A second Delete has nothing to remove.
    CHECK_FALSE(t.ed.delete_selected());
    CHECK(t.ids().size() == 2);
}

TEMPLATE_TEST_CASE("a locked selection refuses delete and moves", "[canvas_editor]",
                   ThemeEditor, FreeformEditor)
{
    TestType t;
    REQUIRE(t.add());
    const auto id = t.ids().front();
    t.ed.select(id);
    REQUIRE(t.ed.set_selected_locked(true));
    const double x = t.object(id).x;
    CHECK_FALSE(t.ed.delete_selected());
    CHECK_FALSE(t.ed.nudge_selected(10, 0));
    CHECK(t.object(id).x == Approx(x));
    CHECK(t.ids().size() == 1);
}

TEMPLATE_TEST_CASE("copy and paste land on top, keep groups together, and undo as one",
                   "[canvas_editor]", ThemeEditor, FreeformEditor)
{
    TestType t;
    REQUIRE(t.add());
    REQUIRE(t.add());
    REQUIRE(t.add());
    const auto ids = t.ids();
    t.ed.select(ids[0]);
    t.ed.select(ids[1], true);
    REQUIRE(t.ed.group_selected());
    const auto group = t.object(ids[0]).group_id;
    REQUIRE_FALSE(group.empty());

    CHECK_FALSE(t.ed.can_paste());
    REQUIRE(t.ed.copy_selected());
    REQUIRE(t.ed.can_paste());
    REQUIRE(t.ed.paste());

    const auto after = t.ids();
    REQUIRE(after.size() == 5);
    // The copies are the selection, on top of the stack, in place.
    const std::vector<std::string> pasted{after[3], after[4]};
    CHECK(t.ed.selected_ids() == pasted);
    CHECK(t.object(after[3]).x == Approx(t.object(ids[0]).x));
    // Grouped together again, under a group of their own.
    CHECK(t.object(after[3]).group_id == t.object(after[4]).group_id);
    CHECK_FALSE(t.object(after[3]).group_id.empty());
    CHECK(t.object(after[3]).group_id != group);

    REQUIRE(t.ed.undo());
    CHECK(t.ids() == ids);
    CHECK(t.ed.selected_ids().empty());
}

TEMPLATE_TEST_CASE("duplicate offsets the copies", "[canvas_editor]", ThemeEditor,
                   FreeformEditor)
{
    TestType t;
    REQUIRE(t.add());
    const auto id = t.ids().front();
    t.ed.select(id);
    REQUIRE(t.ed.duplicate_selected());
    const auto after = t.ids();
    REQUIRE(after.size() == 2);
    CHECK(t.ed.selected_ids() == std::vector<std::string>{after[1]});
    CHECK(t.object(after[1]).x == Approx(t.object(id).x + 24));
    CHECK(t.object(after[1]).y == Approx(t.object(id).y + 24));
}

TEMPLATE_TEST_CASE("flip is one undo step, alignment included", "[canvas_editor]",
                   ThemeEditor, FreeformEditor)
{
    TestType t;
    REQUIRE(t.add());
    const auto id = t.ids().front();
    t.ed.select(id);
    REQUIRE(t.ed.set_property("align", "left"));
    REQUIRE(t.ed.flip_selected(seventhslide::canvas::FlipAxis::Horizontal));
    CHECK(t.object(id).flip_h);
    CHECK(t.object(id).text_align == "right");
    REQUIRE(t.ed.undo());
    CHECK_FALSE(t.object(id).flip_h);
    CHECK(t.object(id).text_align == "left");
}

TEMPLATE_TEST_CASE("a reorder with nowhere to go succeeds without a step", "[canvas_editor]",
                   ThemeEditor, FreeformEditor)
{
    TestType t;
    REQUIRE(t.add());
    REQUIRE(t.add());
    const auto ids = t.ids();
    t.ed.select(ids[1]);
    CHECK_FALSE(t.ed.can_reorder_selected(ZOrderOp::ToFront));
    REQUIRE(t.ed.reorder_selected(ZOrderOp::ToFront));
    REQUIRE(t.ed.reorder_selected(ZOrderOp::ToBack));
    CHECK(t.ids() == std::vector<std::string>{ids[1], ids[0]});
    // One undo takes back the one real move.
    REQUIRE(t.ed.undo());
    CHECK(t.ids() == ids);
}

TEMPLATE_TEST_CASE("a refused edit, or one that changes nothing, leaves redo as it was",
                   "[canvas_editor]", ThemeEditor, FreeformEditor)
{
    TestType t;
    REQUIRE(t.add());
    REQUIRE(t.add());
    const auto ids = t.ids();
    t.ed.select(ids[0]);
    REQUIRE(t.ed.set_selected_locked(true));
    t.ed.select(ids[1]);
    REQUIRE(t.ed.set_property("align", "left"));
    const double x = t.object(ids[1]).x;
    REQUIRE(t.ed.nudge_selected(5, 0));
    REQUIRE(t.ed.undo());

    // A ribbon value the box already has, then a drag of the locked box.
    REQUIRE(t.ed.set_property("align", "left"));
    CHECK(t.ed.can_redo());
    const auto& locked = t.object(ids[0]);
    t.ed.begin_gesture();
    CHECK_FALSE(t.ed.set_geometry(ids[0], locked.x + 10, locked.y, locked.w, locked.h, 0));
    t.ed.end_gesture();
    CHECK(t.ed.can_redo());
    REQUIRE(t.ed.redo());
    CHECK(t.object(ids[1]).x == Approx(x + 5));
}

TEST_CASE("a pasted theme binding that must stay unique becomes static", "[canvas_editor][theme]")
{
    seventhslide::theme::Editor ed;
    ed.load(seventhslide::theme::ElementSet{});
    REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Lyrics));
    const auto lyrics = ed.selected_id();
    REQUIRE(ed.copy_selected());
    REQUIRE(ed.paste());
    CHECK(ed.find(lyrics)->binding == ElementBinding::Lyrics);
    CHECK(ed.selected_item()->binding == ElementBinding::Static);

    // Pasted into a context the binding does not fit, it is static too.
    ed.set_context(seventhslide::theme::ElementContext::Bible);
    REQUIRE(ed.paste());
    CHECK(ed.selected_item()->binding == ElementBinding::Static);
}

namespace {

/// Counts the copies made of the document it is in.
struct CopyTally {
    static inline int copies = 0;

    CopyTally() = default;
    CopyTally(const CopyTally&) noexcept { ++copies; }
    CopyTally(CopyTally&&) noexcept = default;
    CopyTally& operator=(const CopyTally&) noexcept
    {
        ++copies;
        return *this;
    }
    CopyTally& operator=(CopyTally&&) noexcept = default;
    ~CopyTally() = default;
};

/// A document of plain objects and a note, for an editor whose edits can
/// write and then refuse, or throw. It counts its copies and comparisons.
struct ScratchDoc {
    std::vector<seventhslide::canvas::Object> objects;
    std::string note;
    int next_id = 1;
    CopyTally tally;

    static inline int compares = 0;

    [[nodiscard]] friend bool operator==(const ScratchDoc& a, const ScratchDoc& b) noexcept
    {
        ++compares;
        return a.objects == b.objects && a.note == b.note && a.next_id == b.next_id;
    }
};

[[nodiscard]] std::vector<seventhslide::canvas::Object>* scratch_objects(ScratchDoc& doc,
                                                                         const int& /*place*/) noexcept
{
    return &doc.objects;
}

using ScratchPart = seventhslide::canvas::DocPart<ScratchDoc, int,
                                                  std::vector<seventhslide::canvas::Object>,
                                                  &scratch_objects>;

/// How a scratch edit ends, once it has written.
enum class Ends { Done, Refused, Throws };

[[nodiscard]] EditResult ending(Ends ends)
{
    if (ends == Ends::Throws) {
        throw std::runtime_error("broke partway");
    }
    return ends == Ends::Done ? EditResult::success() : EditResult::fail("refused partway");
}

class ScratchEditor
    : public seventhslide::canvas::ObjectEditor<ScratchEditor, ScratchDoc,
                                                seventhslide::canvas::Object, ScratchPart> {
public:
    ScratchEditor()
    {
        seventhslide::canvas::Object box;
        box.id = "o1";
        box.text = "start";
        start(ScratchDoc{{box}, "start"});
        select("o1");
    }

    [[nodiscard]] const ScratchDoc& doc() const noexcept { return doc_; }

    /// Whether the stack has a place (``here()``); with none, an edit of it
    /// keeps the whole document.
    bool placed = true;

    /// An edit of the whole document: write ``note``, then end as told.
    [[nodiscard]] EditResult write_note(std::string note, Ends ends)
    {
        return commit([&](ScratchDoc& doc) {
            doc.note = std::move(note);
            return ending(ends);
        });
    }

    /// An edit of the selected box: its text, written by the property table.
    [[nodiscard]] EditResult write_text(const std::string& text, Ends ends)
    {
        return set_property(k_names[static_cast<int>(ends)], text);
    }

private:
    friend class seventhslide::canvas::ObjectEditor<ScratchEditor, ScratchDoc,
                                                    seventhslide::canvas::Object, ScratchPart>;
    static constexpr const char* k_names[] = {"text", "text_refused", "text_throws"};
    static constexpr auto k_units = seventhslide::canvas::GeometryUnits::Exact;
    // The editor's contract (object_editor.hpp); these tests never stack by
    // z, so nothing here reads it, and Apple clang calls it unused.
    [[maybe_unused]] static constexpr int k_z_step = 1;

    [[nodiscard]] std::vector<seventhslide::canvas::Object>* stack() { return &doc_.objects; }
    [[nodiscard]] const std::vector<seventhslide::canvas::Object>* stack() const
    {
        return &doc_.objects;
    }
    [[nodiscard]] std::optional<int> here() const noexcept
    {
        return placed ? std::optional<int>{0} : std::nullopt;
    }
    [[nodiscard]] static seventhslide::geom::CanvasSize canvas() noexcept { return {1920, 1080}; }
    [[nodiscard]] static EditResult writable() noexcept { return EditResult::success(); }
    static void after_edit() noexcept {}
    static void after_restore() noexcept {}
    static void after_gesture() noexcept {}
    [[nodiscard]] std::string mint_id(char kind) { return kind + std::to_string(doc_.next_id++); }
    static void prepare_copy(seventhslide::canvas::Object&) noexcept {}
    [[nodiscard]] static EditResult apply_property(seventhslide::canvas::Object& obj,
                                                   std::string_view name,
                                                   const nlohmann::json& value)
    {
        obj.text = value.get<std::string>();
        if (name == "text_throws") {
            return ending(Ends::Throws);
        }
        return ending(name == "text_refused" ? Ends::Refused : Ends::Done);
    }
};

}  // namespace

TEST_CASE("an edit that fails partway inside a gesture leaves the gesture's edits as they were",
          "[canvas_editor]")
{
    ScratchEditor ed;
    const auto start = ed.doc();
    ed.begin_gesture();
    REQUIRE(ed.write_note("kept", Ends::Done));
    REQUIRE(ed.write_text("kept", Ends::Done));
    const auto kept = ed.doc();

    CHECK_FALSE(ed.write_note("half", Ends::Refused));
    CHECK(ed.doc() == kept);
    CHECK_THROWS(ed.write_note("half", Ends::Throws));
    CHECK(ed.doc() == kept);
    CHECK_FALSE(ed.write_text("half", Ends::Refused));
    CHECK(ed.doc() == kept);
    CHECK_THROWS(ed.write_text("half", Ends::Throws));
    CHECK(ed.doc() == kept);
    ed.end_gesture();

    // The gesture is still one step, back to before it.
    REQUIRE(ed.undo());
    CHECK(ed.doc() == start);
    CHECK_FALSE(ed.can_undo());
}

TEST_CASE("an edit that throws changes nothing and leaves no undo step", "[canvas_editor]")
{
    ScratchEditor ed;
    const auto start = ed.doc();
    CHECK_THROWS(ed.write_note("half", Ends::Throws));
    CHECK(ed.doc() == start);
    CHECK_FALSE(ed.can_undo());
    CHECK_FALSE(ed.dirty());
    CHECK_THROWS(ed.write_text("half", Ends::Throws));
    CHECK(ed.doc() == start);
    CHECK_FALSE(ed.can_undo());

    // A gesture's first edit throwing leaves the next to make the step.
    ed.begin_gesture();
    CHECK_THROWS(ed.write_text("half", Ends::Throws));
    REQUIRE(ed.write_text("moved", Ends::Done));
    ed.end_gesture();
    REQUIRE(ed.undo());
    CHECK(ed.doc() == start);
    CHECK_FALSE(ed.can_undo());
}

TEST_CASE("an edit that fails partway after a gesture's edits reach past one place leaves them",
          "[canvas_editor]")
{
    // The gesture's step starts as the box's place alone; the note is
    // beyond it, so the step must take the whole document before it.
    ScratchEditor ed;
    const auto start = ed.doc();
    ed.begin_gesture();
    REQUIRE(ed.write_text("kept", Ends::Done));
    REQUIRE(ed.write_note("kept", Ends::Done));
    const auto kept = ed.doc();

    CHECK_FALSE(ed.write_text("half", Ends::Refused));
    CHECK(ed.doc() == kept);
    CHECK_THROWS(ed.write_text("half", Ends::Throws));
    CHECK(ed.doc() == kept);
    CHECK_FALSE(ed.write_note("half", Ends::Refused));
    CHECK(ed.doc() == kept);
    CHECK_THROWS(ed.write_note("half", Ends::Throws));
    CHECK(ed.doc() == kept);
    ed.end_gesture();

    REQUIRE(ed.undo());
    CHECK(ed.doc() == start);
    CHECK_FALSE(ed.can_undo());
    REQUIRE(ed.redo());
    CHECK(ed.doc() == kept);
}

TEST_CASE("an edit of one place copies none of the document", "[canvas_editor]")
{
    // A committed edit and each step of a drag keep the place they change,
    // not the whole document.
    ScratchEditor ed;
    CopyTally::copies = 0;
    REQUIRE(ed.write_text("typed", Ends::Done));
    REQUIRE(ed.set_geometry("o1", 10, 10, 100, 50, 0));
    CHECK_FALSE(ed.write_text("half", Ends::Refused));
    ed.begin_gesture();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(ed.set_geometry("o1", 20 + i, 10, 100, 50, 0));
    }
    CHECK_FALSE(ed.write_text("half", Ends::Refused));
    ed.end_gesture();
    REQUIRE(ed.undo());
    REQUIRE(ed.redo());
    CHECK(CopyTally::copies == 0);
}

TEST_CASE("each step of a drag compares only the place it changed", "[canvas_editor]")
{
    // Whether the document is changed is worked out from the place: the rest
    // of the document is compared once, at the drag's first step.
    ScratchEditor ed;
    const auto box = ed.doc().objects.front();
    ed.begin_gesture();
    REQUIRE(ed.set_geometry("o1", 20, 10, 100, 50, 0));
    const int after_first = ScratchDoc::compares;
    for (int i = 1; i < 10; ++i) {
        REQUIRE(ed.set_geometry("o1", 20 + i, 10, 100, 50, 0));
    }
    ed.end_gesture();
    CHECK(ScratchDoc::compares == after_first);
    CHECK(ed.dirty());
    // Back where it was, the document is as it started.
    REQUIRE(ed.set_geometry("o1", box.x, box.y, box.w, box.h, box.rotation));
    CHECK_FALSE(ed.dirty());
}

TEST_CASE("a refused edit with the undo stack full drops no step", "[canvas_editor]")
{
    ScratchEditor ed;
    const auto start = ed.doc();
    const auto depth = seventhslide::canvas::EditHistory<ScratchDoc, ScratchPart>::k_max_depth;
    for (std::size_t i = 0; i < depth; ++i) {
        REQUIRE(ed.write_text(std::to_string(i), Ends::Done));
    }
    CHECK_FALSE(ed.write_text("half", Ends::Refused));
    CHECK_FALSE(ed.write_note("half", Ends::Refused));
    for (std::size_t i = 0; i < depth; ++i) {
        REQUIRE(ed.undo());
    }
    CHECK(ed.doc() == start);
}

TEST_CASE("an edit that changes nothing leaves no step where the stack has no place",
          "[canvas_editor]")
{
    ScratchEditor ed;
    ed.placed = false;
    REQUIRE(ed.write_text("start", Ends::Done));
    CHECK_FALSE(ed.can_undo());
    REQUIRE(ed.write_text("typed", Ends::Done));
    REQUIRE(ed.undo());
    CHECK(ed.doc().objects.front().text == "start");
}
