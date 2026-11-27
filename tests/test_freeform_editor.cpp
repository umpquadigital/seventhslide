#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/freeform/background_copy.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/freeform/object_properties.hpp>
#include <seventhslide/freeform/tokens.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using seventhslide::freeform::Document;
using seventhslide::freeform::Editor;
using seventhslide::freeform::IndicatorFallback;
using seventhslide::freeform::ObjectType;
using seventhslide::freeform::collect_field_names;
using seventhslide::freeform::make_document;
using seventhslide::freeform::normalize;

namespace {

Document two_outputs()
{
    return make_document("Welcome", {"out-main", "LT"});
}

Document load_fixture(const char* file)
{
    std::ifstream in(std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "freeform" / file);
    REQUIRE(in);
    nlohmann::json j;
    in >> j;
    auto doc = Document::from_json(j);
    normalize(doc);
    return doc;
}

} // namespace

TEST_CASE("Editor add text is per-output and undoable", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.slide_count() == 1);
    REQUIRE(ed.current_slide()->objects.size() == 1);
    REQUIRE(ed.current_slide()->objects[0].type == ObjectType::Text);
    REQUIRE(ed.selected_item() != nullptr);

    ed.set_output("LT");
    REQUIRE(ed.current_slide()->objects.empty());
    REQUIRE(ed.add_text());
    REQUIRE(ed.current_slide()->objects.size() == 1);

    ed.set_output("out-main");
    REQUIRE(ed.current_slide()->objects.size() == 1);
    REQUIRE(ed.undo());
    REQUIRE(ed.undo());
    REQUIRE(ed.current_slide()->objects.empty());
    REQUIRE(ed.redo());
    REQUIRE(ed.current_slide()->objects.size() == 1);
}

TEST_CASE("Editor add indicator is a live object", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_indicator());
    REQUIRE(ed.current_slide()->objects.size() == 1);
    REQUIRE(ed.current_slide()->objects[0].type == ObjectType::Indicator);
    REQUIRE(ed.current_slide()->objects[0].text.empty());
    REQUIRE(ed.current_slide()->objects[0].indicator_fallback == IndicatorFallback::Slide);
    REQUIRE(ed.set_property("indicator_fallback", "blank"));
    REQUIRE(ed.selected_item()->indicator_fallback == IndicatorFallback::Blank);
    REQUIRE(ed.undo());
    REQUIRE(ed.current_slide()->objects[0].indicator_fallback == IndicatorFallback::Slide);
}

TEST_CASE("Editor add clock is a live object", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_clock());
    REQUIRE(ed.current_slide()->objects.size() == 1);
    REQUIRE(ed.current_slide()->objects[0].type == ObjectType::Clock);
    REQUIRE(ed.current_slide()->objects[0].text.empty());
    REQUIRE_FALSE(ed.current_slide()->objects[0].clock_24h);
    REQUIRE(ed.set_property("clock_24h", true));
    REQUIRE(ed.selected_item()->clock_24h);
    REQUIRE(ed.set_property("clock_seconds", true));
    REQUIRE(ed.selected_item()->clock_seconds);
    REQUIRE(ed.undo());
    REQUIRE_FALSE(ed.current_slide()->objects[0].clock_seconds);
}

TEST_CASE("Editor adds a shape of the requested kind", "[freeform][editor]")
{
    using seventhslide::geom::ShapeKind;

    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_shape(ShapeKind::Star));
    REQUIRE(ed.current_slide()->objects.size() == 1);
    const auto& obj = ed.current_slide()->objects[0];
    REQUIRE(obj.type == ObjectType::Shape);
    REQUIRE(obj.shape_kind == ShapeKind::Star);
    // Radial shapes start roughly square rather than squashed into a wide box.
    REQUIRE(obj.w == Catch::Approx(obj.h).margin(4.0));

    REQUIRE(ed.set_property("shape_kind", "arrow"));
    REQUIRE(ed.selected_item()->shape_kind == ShapeKind::Arrow);
    REQUIRE(ed.undo());
    REQUIRE(ed.current_slide()->objects[0].shape_kind == ShapeKind::Star);
}

TEST_CASE("Editor clamps shape adjustments to the kind's window", "[freeform][editor]")
{
    using seventhslide::geom::ShapeAdjust;
    using seventhslide::geom::ShapeKind;

    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_shape(ShapeKind::DoubleArrow));

    REQUIRE(ed.set_property("shape_head", 4.0));
    REQUIRE(ed.set_property("shape_thickness", -1.0));
    REQUIRE(ed.selected_item()->shape_adjust.head == Catch::Approx(0.5));
    REQUIRE(ed.selected_item()->shape_adjust.thickness == Catch::Approx(0.05));

    // Switching kind re-clamps: a single arrow may spend the full width on its head.
    REQUIRE(ed.set_property("shape_kind", "arrow"));
    REQUIRE(ed.selected_item()->shape_adjust.head == Catch::Approx(0.5));
}

TEST_CASE("Editor keeps a border width in range", "[freeform][editor]")
{
    using seventhslide::geom::ShapeKind;

    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_shape(ShapeKind::Rectangle));
    REQUIRE(ed.set_property("stroke_width", 6.5));
    REQUIRE(ed.selected_item()->line.width == Catch::Approx(6.5));
    REQUIRE(ed.set_property("stroke_width", -1.0));
    REQUIRE(ed.selected_item()->line.width == Catch::Approx(0.0));
    REQUIRE(ed.set_property("stroke_width", 500.0));
    REQUIRE(ed.selected_item()->line.width
            == Catch::Approx(seventhslide::freeform::k_max_stroke_width));
    // Not a number: refused, and the width stays.
    REQUIRE_FALSE(ed.set_property("stroke_width", "wide"));
    REQUIRE(ed.selected_item()->line.width
            == Catch::Approx(seventhslide::freeform::k_max_stroke_width));
}

TEST_CASE("Editor simple insert slide is global", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.slide_index() == 1);
    REQUIRE(ed.document().decks[0].slide_count() == 2);
    REQUIRE(ed.document().decks[1].slide_count() == 2);
    REQUIRE(ed.document().decks[0].slides[0].objects.size() == 1);
    REQUIRE(ed.document().decks[0].slides[1].objects.empty());
    REQUIRE(ed.document().decks[1].slides[1].objects.empty());

    ed.select_slide(0);
    REQUIRE(ed.duplicate_slide());
    REQUIRE(ed.slide_index() == 1);
    REQUIRE(ed.document().decks[0].slide_count() == 3);
    REQUIRE(ed.document().decks[0].slides[1].objects.size() == 1);
    REQUIRE(ed.document().decks[1].slides[1].objects.empty());
}

TEST_CASE("Editor fields appear from tokens and survive save", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.set_text("Hello {title}\n{-subtitle}"));
    REQUIRE(ed.set_field("title", "Sunday"));
    REQUIRE(ed.set_field("speaker", "Jane"));
    auto rows = ed.field_rows();
    REQUIRE(rows.size() >= 2);
    bool saw_title = false;
    bool saw_speaker = false;
    bool saw_subtitle = false;
    for (const auto& row : rows) {
        if (row.name == "title") {
            saw_title = true;
            REQUIRE_FALSE(row.unused);
            REQUIRE(row.value == "Sunday");
        }
        if (row.name == "speaker") {
            saw_speaker = true;
            REQUIRE(row.unused);
        }
        if (row.name == "subtitle") {
            saw_subtitle = true;
            REQUIRE_FALSE(row.unused);
            REQUIRE(row.hide);
        }
    }
    REQUIRE(saw_title);
    REQUIRE(saw_speaker);
    REQUIRE(saw_subtitle);

    // Save keeps an unused-but-legal field - deleting one is the operator's
    // call now that the dialog has a Delete - and drops only illegal names,
    // which only a loaded document can carry (set_field refuses them).
    auto with_illegal = ed.document();
    with_illegal.fields["Nope"] = "bad";
    ed.load(std::move(with_illegal), ed.output_id());
    REQUIRE(ed.document().fields.count("Nope") == 1);
    ed.prune_on_save();
    const auto names = collect_field_names(ed.document());
    REQUIRE(std::find(names.begin(), names.end(), "title") != names.end());
    REQUIRE(ed.document().fields.at("speaker") == "Jane");
    REQUIRE(ed.document().fields.count("Nope") == 0);
}

TEST_CASE("Editor add_field declares a field before any token uses it", "[freeform][editor][fields]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE_FALSE(ed.add_field("Speaker"));
    REQUIRE_FALSE(ed.add_field("speaker name"));
    REQUIRE(ed.add_field("speaker"));
    REQUIRE(ed.document().fields.at("speaker").empty());
    REQUIRE_FALSE(ed.add_field("speaker"));

    const auto rows = ed.field_rows();
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].name == "speaker");
    REQUIRE(rows[0].unused);

    REQUIRE(ed.undo());
    REQUIRE(ed.document().fields.empty());
}

TEST_CASE("Editor rename_field rewrites every token and carries the value", "[freeform][editor][fields]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.set_text("{title} — {title}\n{-title}"));
    REQUIRE(ed.set_field("title", "Sunday"));

    REQUIRE(ed.rename_field("title", "header"));
    const auto& text = ed.document().decks[0].slides[0].objects[0].text;
    REQUIRE(text == "{header} — {header}\n{-header}");
    REQUIRE(ed.document().fields.count("title") == 0);
    REQUIRE(ed.document().fields.at("header") == "Sunday");

    // One undo step puts the text and the value back together.
    REQUIRE(ed.undo());
    REQUIRE(ed.document().decks[0].slides[0].objects[0].text == "{title} — {title}\n{-title}");
    REQUIRE(ed.document().fields.at("title") == "Sunday");
}

TEST_CASE("Editor delete_field strips the tokens it owned", "[freeform][editor][fields]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.set_text("Hi {name}, welcome"));
    REQUIRE(ed.set_field("name", "Jane"));

    REQUIRE(ed.delete_field("name"));
    REQUIRE(ed.document().decks[0].slides[0].objects[0].text == "Hi , welcome");
    REQUIRE(ed.document().fields.count("name") == 0);
    REQUIRE(ed.field_rows().empty());

    REQUIRE(ed.undo());
    REQUIRE(ed.document().decks[0].slides[0].objects[0].text == "Hi {name}, welcome");
    REQUIRE(ed.document().fields.at("name") == "Jane");
}

TEST_CASE("Editor gesture records a single undo for geometry", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_text());
    const auto id = ed.selected_item()->id;
    const double before_x = ed.selected_item()->x;
    const double before_y = ed.selected_item()->y;
    ed.begin_gesture();
    REQUIRE(ed.set_geometry(id, 10, 20, 400, 200, 0));
    REQUIRE(ed.set_geometry(id, 30, 40, 400, 200, 0));
    ed.end_gesture();
    REQUIRE(ed.selected_item()->x == 30);
    // One undo lands before the whole gesture, not on the first move inside
    // it. Asserting only "x != 30" would pass with one snapshot per move,
    // which is the failure this test is named for.
    REQUIRE(ed.undo());
    REQUIRE(ed.current_slide()->objects[0].x == before_x);
    REQUIRE(ed.current_slide()->objects[0].y == before_y);
    REQUIRE(ed.can_undo());
}

TEST_CASE("Editor: a value refused inside a gesture leaves the box as it was", "[freeform][editor]")
{
    // Typing holds a gesture open. The paragraph writers take the lines'
    // own spacing off before they read the value, so a refused one has
    // already written.
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.set_text("<p=before:50,after:25,indent:100>One\nTwo"));
    const auto typed = ed.selected_item()->text;
    REQUIRE(typed.find("<p=") != std::string::npos);
    ed.begin_gesture();
    REQUIRE(ed.set_property("font_size", 50));
    const auto kept = ed.document();
    for (const char* name : {"space_before", "space_after", "indent"}) {
        INFO(name);
        CHECK_FALSE(ed.set_property(name, nlohmann::json::object()));
        CHECK(ed.document() == kept);
    }
    ed.end_gesture();
    CHECK(ed.selected_item()->text == typed);
}

TEST_CASE("Editor: a gesture over two slides is one undo step back to before both",
          "[freeform][editor]")
{
    // Each slide's edit keeps that slide alone; once the gesture reaches a
    // second slide, its step has to be the whole document.
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.insert_slide());
    ed.select_slide(0);
    REQUIRE(ed.add_text());
    const auto first = ed.selected_item()->id;
    ed.select_slide(1);
    REQUIRE(ed.add_text());
    const auto second = ed.selected_item()->id;
    ed.load(ed.document(), "out-main");
    const auto start = ed.document();

    ed.select_slide(0);
    ed.begin_gesture();
    REQUIRE(ed.set_geometry(first, 10, 20, 400, 200, 0));
    REQUIRE(ed.set_slide_notes("first"));
    ed.select_slide(1);
    ed.select(second);
    REQUIRE(ed.set_geometry(second, 30, 40, 400, 200, 0));
    CHECK_FALSE(ed.set_property("space_before", nlohmann::json::object()));
    REQUIRE(ed.set_name("Renamed"));
    ed.end_gesture();
    const auto moved = ed.document();

    REQUIRE(ed.undo());
    CHECK(ed.document() == start);
    CHECK_FALSE(ed.dirty());
    REQUIRE(ed.redo());
    CHECK(ed.document() == moved);
}

TEST_CASE("Editor: edits of one slide and of the slide list undo and redo as one chain",
          "[freeform][editor]")
{
    // A step of one slide keeps where the slide was; inserts, deletes and
    // reorders between them move the slides about.
    Editor ed;
    ed.load(two_outputs(), "out-main");
    std::vector<Document> states{ed.document()};
    const auto made = [&](bool ok) {
        REQUIRE(ok);
        states.push_back(ed.document());
    };

    made(static_cast<bool>(ed.add_text()));
    const auto first = ed.selected_item()->id;
    made(static_cast<bool>(ed.insert_slide()));
    ed.select_slide(1);
    made(static_cast<bool>(ed.add_text()));
    const auto second = ed.selected_item()->id;
    made(static_cast<bool>(ed.set_geometry(second, 10, 20, 300, 100, 0)));
    made(static_cast<bool>(ed.reorder_slides(1, 0)));
    ed.select_slide(1);
    ed.select(first);
    made(static_cast<bool>(ed.set_geometry(first, 50, 60, 300, 100, 0)));
    made(static_cast<bool>(ed.set_slide_notes("was first")));
    made(static_cast<bool>(ed.insert_slide()));
    ed.select_slide(2);
    made(static_cast<bool>(ed.set_slide_notes("third")));
    ed.select_slide(0);
    made(static_cast<bool>(ed.delete_slide()));
    ed.select_slide(0);
    ed.select(first);
    made(static_cast<bool>(ed.set_property("font_size", 60)));
    made(static_cast<bool>(ed.reorder_slides(0, 1)));
    ed.select_slide(0);
    made(static_cast<bool>(ed.add_text()));
    REQUIRE(ed.document().decks.front().slides.size() == 2);

    for (int round = 0; round < 2; ++round) {
        for (std::size_t i = states.size() - 1; i > 0; --i) {
            REQUIRE(ed.undo());
            CHECK(ed.document() == states[i - 1]);
        }
        CHECK_FALSE(ed.can_undo());
        CHECK_FALSE(ed.dirty());
        for (std::size_t i = 1; i < states.size(); ++i) {
            REQUIRE(ed.redo());
            CHECK(ed.document() == states[i]);
        }
        CHECK_FALSE(ed.can_redo());
    }
}

TEST_CASE("Editor: dirty is whether the document differs, edit by edit of single slides",
          "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.insert_slide());
    ed.select_slide(1);
    REQUIRE(ed.add_text());
    const auto id = ed.selected_item()->id;
    const auto box = *ed.selected_item();
    ed.load(ed.document(), "out-main");
    const auto start = ed.document();
    REQUIRE_FALSE(ed.dirty());

    // Moved and moved back.
    ed.select_slide(1);
    ed.select(id);
    ed.begin_gesture();
    REQUIRE(ed.set_geometry(id, 10, 20, 400, 200, 0));
    CHECK(ed.dirty());
    REQUIRE(ed.set_geometry(id, box.x, box.y, box.w, box.h, box.rotation));
    ed.end_gesture();
    CHECK_FALSE(ed.dirty());

    // Two slides changed: one put back leaves the other.
    REQUIRE(ed.set_slide_notes("one"));
    ed.select_slide(0);
    REQUIRE(ed.set_slide_notes("zero"));
    REQUIRE(ed.set_slide_notes(""));
    CHECK(ed.dirty());
    ed.select_slide(1);
    REQUIRE(ed.set_slide_notes(""));
    CHECK_FALSE(ed.dirty());

    // A paste mints ids; undone, the document is as it was, counter and all.
    ed.select(id);
    REQUIRE(ed.copy_selected());
    REQUIRE(ed.paste());
    CHECK(ed.document().next_id > start.next_id);
    CHECK(ed.dirty());
    REQUIRE(ed.undo());
    CHECK(ed.document() == start);
    CHECK_FALSE(ed.dirty());

    // A change past the slides is still a change after a slide's edit.
    REQUIRE(ed.set_name("Renamed"));
    REQUIRE(ed.set_slide_notes("one"));
    REQUIRE(ed.set_slide_notes(""));
    CHECK(ed.dirty());
}

TEST_CASE("Editor enabling line control synthesizes identity cues", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.set_line_control(true));
    REQUIRE(ed.line_control());
    REQUIRE(ed.document().cues.size() == 2);
    // A line for each slide, with no words of its own to show.
    REQUIRE(ed.document().cues[0].text.empty());
    REQUIRE(ed.document().cues[1].text.empty());
    REQUIRE(ed.document().decks[0].line_to_slide == std::vector<int>({0, 1}));
    REQUIRE(ed.cue_index() == 0);
}

TEST_CASE("Editor set_cue_verse and insert inherit", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.set_line_control(true));
    REQUIRE(ed.set_cue_verse("Bridge", 2));
    REQUIRE(ed.document().cues[0].verse_type == "Bridge");
    REQUIRE(ed.document().cues[0].verse_number == 2);
    REQUIRE(ed.insert_cue("more"));
    REQUIRE(ed.document().cues[1].verse_type == "Bridge");
    REQUIRE(ed.document().cues[1].verse_number == 2);
    ed.select_cue(1);
    REQUIRE(ed.set_cue_verse("", 1));
    REQUIRE(ed.document().cues[1].verse_type.empty());
}

TEST_CASE("Editor delete_cue is blocked when it would orphan a slide", "[freeform][editor]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.set_line_control(true));
    ed.select_cue(0);
    REQUIRE_FALSE(ed.can_delete_cue());
    REQUIRE_FALSE(ed.delete_cue());
    REQUIRE(ed.document().cues.size() == 2);

    REQUIRE(ed.insert_cue("extra"));
    ed.select_cue(1);
    REQUIRE(ed.can_delete_cue());
    REQUIRE(ed.delete_cue());
    REQUIRE(ed.document().cues.size() == 2);
    REQUIRE_FALSE(ed.can_delete_cue());
}

TEST_CASE("Editor turns line control off only with a flatten", "[freeform][editor]")
{
    Editor ed;
    ed.load(load_fixture("line_control_fortress.json"), "out-main");
    REQUIRE(ed.line_control());
    REQUIRE_FALSE(ed.can_disable_line_control());
    ed.select_cue(0);

    REQUIRE_FALSE(ed.set_line_control(false));
    REQUIRE(ed.line_control());
    REQUIRE(ed.set_line_control(false, true));
    REQUIRE_FALSE(ed.line_control());
    REQUIRE(ed.document().decks[0].slide_count() == ed.document().decks[1].slide_count());
    REQUIRE(ed.cue_index() == -1);
}

// A line is the document's, not a deck's: another output may put it on
// another slide. Switching outputs keeps the line and shows the slide that
// holds it there; the slide number from the last output picked a slide that
// did not hold the line still marked.
TEST_CASE("Editor keeps the line when the output changes", "[freeform][editor]")
{
    const auto canvas = nlohmann::json{{"width", 1920}, {"height", 1080}};
    const auto slides = [](std::initializer_list<const char*> ids) {
        auto out = nlohmann::json::array();
        for (const char* id : ids) {
            out.push_back({{"id", id}, {"objects", nlohmann::json::array()}});
        }
        return out;
    };
    auto doc = Document::from_json({
        {"schema", "seventhslide.freeform"},
        {"schema_version", 3},
        {"name", "Lines"},
        {"line_control", true},
        {"next_id", 100},
        {"cues", {{{"id", "c1"}, {"text", "one"}},
                  {{"id", "c2"}, {"text", "two"}},
                  {{"id", "c3"}, {"text", "three"}},
                  {{"id", "c4"}, {"text", "four"}}}},
        {"decks",
         {{{"output", "out-main"}, {"canvas", canvas}, {"line_to_slide", {0, 0, 1, 2}},
           {"slides", slides({"s1", "s2", "s3"})}},
          {{"output", "LT"}, {"canvas", canvas}, {"line_to_slide", {0, 1, 2, 3}},
           {"slides", slides({"s4", "s5", "s6", "s7"})}}}},
    });
    normalize(doc);
    Editor ed;
    ed.load(doc, "LT");

    ed.select_cue(2);
    REQUIRE(ed.slide_index() == 2);
    ed.set_output("out-main");
    CHECK(ed.cue_index() == 2);
    CHECK(ed.slide_index() == 1);

    ed.select_slide(2);
    REQUIRE(ed.cue_index() == 3);
    ed.set_output("LT");
    CHECK(ed.cue_index() == 3);
    CHECK(ed.slide_index() == 3);
}

TEST_CASE("Editor set_slide_background is per-slide and undoable", "[freeform][editor][bg]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.insert_slide());
    ed.select_slide(0);

    seventhslide::freeform::SlideBackground color;
    color.kind = seventhslide::freeform::SlideBackgroundKind::Color;
    color.color = "#aabbcc";
    REQUIRE(ed.set_slide_background(color));
    REQUIRE(ed.current_slide()->background.kind == seventhslide::freeform::SlideBackgroundKind::Color);
    REQUIRE(ed.current_slide()->background.color == "#aabbcc");

    ed.select_slide(1);
    REQUIRE(ed.current_slide()->background.is_none());

    ed.set_output("LT");
    ed.select_slide(0);
    REQUIRE(ed.current_slide()->background.is_none());

    ed.set_output("out-main");
    ed.select_slide(0);
    REQUIRE(ed.undo());
    REQUIRE(ed.current_slide()->background.is_none());
}

TEST_CASE("Editor set_slide_notes is per-slide, undoable, and no step for the same words",
          "[freeform][editor][notes]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.insert_slide());
    ed.select_slide(0);
    REQUIRE(ed.set_slide_notes("Welcome everyone."));
    REQUIRE(ed.current_slide()->notes == "Welcome everyone.");
    REQUIRE(ed.set_slide_notes("Welcome everyone."));
    ed.select_slide(1);
    REQUIRE(ed.current_slide()->notes.empty());
    ed.select_slide(0);
    REQUIRE(ed.undo());
    REQUIRE(ed.current_slide()->notes.empty());
    REQUIRE(ed.redo());
    REQUIRE(ed.current_slide()->notes == "Welcome everyone.");
    // The same words again added no step: one more undo empties them, and
    // the step before that is the slide's insertion.
    REQUIRE(ed.undo());
    REQUIRE(ed.current_slide()->notes.empty());
    REQUIRE(ed.undo());
    REQUIRE(ed.current_deck()->slides.size() == 1);
}

TEST_CASE("Editor cue-linked boxes refuse typing and snapshot on unlink", "[freeform][editor][link]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.set_line_control(true));
    REQUIRE(ed.add_text());
    REQUIRE(ed.rename_cue("Owned"));
    REQUIRE(ed.set_cue_linked(true));
    REQUIRE(ed.selected_item()->cue_linked);
    REQUIRE_FALSE(ed.set_text("typed"));
    REQUIRE(ed.selected_item()->text != "typed");
    REQUIRE(ed.set_show_upcoming(true));
    REQUIRE(ed.selected_item()->show_upcoming);
    REQUIRE(ed.set_property("upcoming_color", "#123456"));
    REQUIRE(ed.selected_item()->upcoming_color == "#123456");
    REQUIRE(ed.set_property("upcoming_font_size", 30));
    REQUIRE(ed.selected_item()->upcoming_font_size == 30);
    REQUIRE(ed.set_cue_linked(false));
    REQUIRE_FALSE(ed.selected_item()->cue_linked);
    REQUIRE(ed.selected_item()->text == "Owned");
}

TEST_CASE("Editor cue-linked overlays style without changing cue text", "[freeform][editor][link]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    REQUIRE(ed.set_line_control(true));
    REQUIRE(ed.add_text());
    const auto obj_id = ed.selected_item()->id;
    REQUIRE(ed.rename_cue("Hello"));
    REQUIRE(ed.insert_cue("World"));
    ed.select(obj_id);
    REQUIRE(ed.set_cue_linked(true));
    const auto id0 = ed.document().cues[0].id;
    const auto id1 = ed.document().cues[1].id;

    REQUIRE_FALSE(ed.linked_edit_allowed("Hellx\nWorld", 0));
    REQUIRE(ed.linked_edit_allowed("Hel\nlo\nWorld", 0));
    REQUIRE(ed.set_linked_style("<color=#ff0000>Hel</color>\n<color=#ff0000>lo</color>\n<size=80>World</size>"));
    REQUIRE(ed.selected_item()->cue_overlays.at(id0).find("Hel") != std::string::npos);
    REQUIRE(ed.selected_item()->cue_overlays.at(id1).find("World") != std::string::npos);
    REQUIRE_FALSE(ed.set_text("typed"));
    REQUIRE(ed.document().cues[0].text == "Hello");
    REQUIRE(ed.document().cues[1].text == "World");

    REQUIRE(ed.set_cue_linked(false));
    REQUIRE_FALSE(ed.selected_item()->cue_linked);
    REQUIRE(ed.selected_item()->cue_overlays.empty());
    REQUIRE(ed.selected_item()->text.find("Hel") != std::string::npos);
    REQUIRE(ed.selected_item()->text.find("World") != std::string::npos);
}

TEST_CASE("Editor object transitions default Cut", "[freeform][editor][transition]")
{
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    Editor ed;
    ed.load(make_document("t", {"out-main"}), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.selected_item());
    // A new object cuts.
    REQUIRE(ed.selected_item()->transition == Transition{});

    Transition dissolve{TransitionType::Fade, 400};
    REQUIRE(ed.set_object_transition(dissolve));
    REQUIRE(ed.selected_item()->transition.plays());
    REQUIRE(ed.selected_item()->transition.duration_ms == 400);

    REQUIRE(ed.set_object_transition(Transition{}));
    REQUIRE_FALSE(ed.selected_item()->transition.plays());
}

namespace {

/// Ids of ``ed``'s current slide, back-to-front.
std::vector<std::string> stack_ids(const Editor& ed)
{
    std::vector<std::string> out;
    for (const auto& obj : ed.current_slide()->objects) {
        out.push_back(obj.id);
    }
    return out;
}

} // namespace

TEST_CASE("Editor stacking moves the selection through the slide", "[freeform][editor][zorder]")
{
    using seventhslide::freeform::ZOrderOp;
    Editor ed;
    ed.load(make_document("z", {"out-main"}), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.add_text());
    REQUIRE(ed.add_text());
    const auto ids = stack_ids(ed);
    REQUIRE(ids.size() == 3);
    // Newly added objects land on top, so z tracks the insertion order.
    REQUIRE(ed.current_slide()->objects[0].z == 0);
    REQUIRE(ed.current_slide()->objects[2].z == 2);

    ed.select(ids[2]);
    REQUIRE(ed.can_reorder_selected(ZOrderOp::Backward));
    REQUIRE_FALSE(ed.can_reorder_selected(ZOrderOp::Forward));
    REQUIRE(ed.reorder_selected(ZOrderOp::Backward));
    REQUIRE(stack_ids(ed) == std::vector<std::string>{ids[0], ids[2], ids[1]});

    REQUIRE(ed.reorder_selected(ZOrderOp::ToBack));
    REQUIRE(stack_ids(ed) == std::vector<std::string>{ids[2], ids[0], ids[1]});
    REQUIRE_FALSE(ed.can_reorder_selected(ZOrderOp::Backward));
    REQUIRE_FALSE(ed.can_reorder_selected(ZOrderOp::ToBack));

    REQUIRE(ed.reorder_selected(ZOrderOp::ToFront));
    REQUIRE(stack_ids(ed) == std::vector<std::string>{ids[0], ids[1], ids[2]});

    // z stays contiguous from the back so the next move is unambiguous.
    for (int i = 0; i < 3; ++i) {
        REQUIRE(ed.current_slide()->objects[static_cast<std::size_t>(i)].z == i);
    }

    REQUIRE(ed.undo());
    REQUIRE(stack_ids(ed) == std::vector<std::string>{ids[2], ids[0], ids[1]});
}

TEST_CASE("Editor stacking keeps a multi-selection together", "[freeform][editor][zorder]")
{
    using seventhslide::freeform::ZOrderOp;
    Editor ed;
    ed.load(make_document("z", {"out-main"}), "out-main");
    for (int i = 0; i < 4; ++i) {
        REQUIRE(ed.add_text());
    }
    const auto ids = stack_ids(ed);
    ed.select(ids[0]);
    ed.select(ids[1], true);

    REQUIRE(ed.reorder_selected(ZOrderOp::Forward));
    REQUIRE(stack_ids(ed) == std::vector<std::string>{ids[2], ids[0], ids[1], ids[3]});
    REQUIRE(ed.reorder_selected(ZOrderOp::ToFront));
    REQUIRE(stack_ids(ed) == std::vector<std::string>{ids[2], ids[3], ids[0], ids[1]});

    // Selecting the whole slide leaves the stack alone rather than failing.
    ed.select(ids[2], true);
    ed.select(ids[3], true);
    REQUIRE_FALSE(ed.can_reorder_selected(ZOrderOp::ToFront));
    REQUIRE(ed.reorder_selected(ZOrderOp::ToFront));
    REQUIRE(stack_ids(ed) == std::vector<std::string>{ids[2], ids[3], ids[0], ids[1]});
}

TEST_CASE("normalize repairs duplicate object z", "[freeform][edit][zorder]")
{
    using seventhslide::freeform::ZOrderOp;
    auto doc = make_document("z", {"out-main"});
    auto& slide = doc.decks[0].slides[0];
    for (int i = 0; i < 3; ++i) {
        seventhslide::freeform::Object obj;
        obj.id = "o" + std::to_string(i);
        obj.z = 7; // a document may give every object the same z
        slide.objects.push_back(obj);
    }
    normalize(doc);
    REQUIRE(doc.decks[0].slides[0].objects[0].z == 0);
    REQUIRE(doc.decks[0].slides[0].objects[1].z == 1);
    REQUIRE(doc.decks[0].slides[0].objects[2].z == 2);

    Editor ed;
    ed.load(doc, "out-main");
    REQUIRE_FALSE(ed.dirty());
    ed.select("o0");
    REQUIRE(ed.reorder_selected(ZOrderOp::ToFront));
    REQUIRE(stack_ids(ed) == std::vector<std::string>{"o1", "o2", "o0"});
}

TEST_CASE("Editor groups expand selection and move as one undo", "[freeform][editor][layout]")
{
    using seventhslide::canvas::Align;
    using seventhslide::canvas::FlipAxis;
    Editor ed;
    ed.load(make_document("g", {"out-main"}), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.add_text());
    REQUIRE(ed.add_text());
    const auto ids = stack_ids(ed);
    REQUIRE(ed.set_geometry(ids[0], 40, 40, 80, 40, 0));
    REQUIRE(ed.set_geometry(ids[1], 200, 60, 80, 40, 0));
    REQUIRE(ed.set_geometry(ids[2], 360, 80, 80, 40, 0));
    ed.select(ids[0]);
    REQUIRE(ed.set_property("align", "left"));

    const auto obj = [&](const std::string& id) -> const seventhslide::freeform::Object* {
        for (const auto& o : ed.current_slide()->objects) {
            if (o.id == id) {
                return &o;
            }
        }
        return nullptr;
    };

    ed.select_ids({ids[0], ids[1]});
    REQUIRE(ed.can_group_selected());
    REQUIRE(ed.group_selected());
    ed.clear_selection();
    ed.select(ids[0]);
    REQUIRE(ed.selected_ids().size() == 2);

    REQUIRE(ed.align_selected(Align::Left));
    REQUIRE(obj(ids[0]) != nullptr);
    REQUIRE(obj(ids[1]) != nullptr);
    REQUIRE(obj(ids[0])->x == Catch::Approx(obj(ids[1])->x));
    REQUIRE(ed.undo());
    REQUIRE(obj(ids[1])->x == Catch::Approx(200));

    ed.select_ids({ids[0], ids[1], ids[2]});
    REQUIRE(ed.flip_selected(FlipAxis::Horizontal));
    REQUIRE(obj(ids[0])->flip_h);
    REQUIRE(obj(ids[0])->text_align == "right");

    ed.select_ids({ids[0], ids[1]});
    REQUIRE(ed.set_selected_locked(true));
    REQUIRE(ed.selection_locked());
    REQUIRE_FALSE(ed.delete_selected());
    REQUIRE(ed.set_selected_locked(false));
    REQUIRE(ed.ungroup_selected());
    REQUIRE_FALSE(ed.can_ungroup_selected());
}

TEST_CASE("Editor copies a background in and sets slide transitions", "[freeform][editor][bg]")
{
    using seventhslide::freeform::SlideBackground;
    using seventhslide::freeform::SlideBackgroundKind;
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    auto doc = make_document("Deck", {"out-main"});
    REQUIRE(seventhslide::freeform::insert_slide(doc, 1));
    Editor ed;
    ed.load(doc, "out-main");
    const auto second = ed.document().decks[0].slides[1].id;

    // A copy lands on the slide it was asked for, whichever is current.
    SlideBackground video;
    video.kind = SlideBackgroundKind::Video;
    video.video = "clip.mp4";
    REQUIRE(ed.set_background_of(second, video, Transition{TransitionType::Fade, 700}));
    CHECK(ed.current_slide()->background.is_none());
    const auto& slide = ed.document().decks[0].slides[1];
    CHECK(slide.background.video == "clip.mp4");
    CHECK(slide.background_transition == Transition{TransitionType::Fade, 700});
    CHECK_FALSE(ed.set_background_of("no-such-slide", video, std::nullopt));
    SlideBackground no_file;
    no_file.kind = SlideBackgroundKind::Video;
    CHECK_FALSE(ed.set_slide_background(no_file));

    // A slide's transition, and then every slide's; undo takes each back.
    REQUIRE(ed.set_slide_transition(Transition{TransitionType::FadeThroughBlack, 400}));
    CHECK(ed.current_slide()->transition->type == TransitionType::FadeThroughBlack);
    CHECK_FALSE(ed.document().decks[0].slides[1].transition.has_value());
    REQUIRE(ed.set_slide_transition(Transition{TransitionType::Fade, 300}, true));
    for (const auto& s : ed.document().decks[0].slides) {
        CHECK(s.transition == Transition{TransitionType::Fade, 300});
    }
    REQUIRE(ed.undo());
    CHECK(ed.current_slide()->transition->type == TransitionType::FadeThroughBlack);

    // Stored and read back.
    const auto back = Document::from_json(ed.document().to_json());
    CHECK(back.decks[0].slides[0].transition->type == TransitionType::FadeThroughBlack);
    CHECK(back.decks[0].slides[1].background.video == "clip.mp4");
}

TEST_CASE("A background theme gives a slide its colour, file and fade", "[freeform][bg]")
{
    using seventhslide::freeform::SlideBackgroundKind;
    using seventhslide::freeform::background_copy_of;
    using seventhslide::model::TransitionType;

    const auto colour = background_copy_of(
        {{"background_type", "color"}, {"background_color", "#112233"},
         {"bg_enable_fade", true}, {"bg_fade_duration", 800}});
    CHECK(colour.background.kind == SlideBackgroundKind::Color);
    CHECK(colour.background.color == "#112233");
    CHECK(colour.transition.type == TransitionType::Fade);
    CHECK(colour.transition.duration_ms == 800);
    CHECK_FALSE(colour.waits_for_copy());

    // A theme with no fade still says so: the slide cuts.
    const auto none = background_copy_of({{"background_type", "transparent"}});
    CHECK(none.background.is_none());
    CHECK(none.transition.type == TransitionType::Cut);

    // A file the theme owns has to be copied first; a library one need not be.
    auto owned = background_copy_of(
        {{"background_type", "image"}, {"background_image", "/static/theme_backgrounds/t1-bg.png"}});
    CHECK(owned.background.kind == SlideBackgroundKind::Image);
    CHECK(owned.background.image.empty());
    CHECK(owned.copy_from_url == "/static/theme_backgrounds/t1-bg.png");
    seventhslide::freeform::place_copied_file(owned, "ab12.png");
    CHECK(owned.background.image == "ab12.png");
    CHECK_FALSE(owned.waits_for_copy());

    const auto library = background_copy_of(
        {{"background_type", "video"}, {"background_video", "/static/videos/cd34.mp4"}});
    CHECK(library.background.kind == SlideBackgroundKind::Video);
    CHECK(library.background.video == "cd34.mp4");
    CHECK_FALSE(library.waits_for_copy());
}
