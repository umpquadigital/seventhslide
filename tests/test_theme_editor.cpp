#include <catch2/catch_test_macros.hpp>

#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/canvas/object_list.hpp>
#include <seventhslide/geom/shape_path.hpp>
#include <seventhslide/theme/element_editor.hpp>
#include <seventhslide/theme/theme_elements.hpp>

#include <nlohmann/json.hpp>

#include <utility>

using seventhslide::freeform::ObjectType;
using seventhslide::freeform::ZOrderOp;
using seventhslide::geom::k_reference_canvas;
using seventhslide::theme::Editor;
using seventhslide::theme::ElementBinding;
using seventhslide::theme::ElementContext;
using seventhslide::theme::ElementSet;
using seventhslide::theme::default_elements;

TEST_CASE("theme editor insert refuses a second exclusive binding", "[theme][editor]")
{
    Editor ed;
    ed.load(default_elements());
    REQUIRE(ed.current().size() == 2);
    REQUIRE_FALSE(ed.insert(ObjectType::Text, ElementBinding::Lyrics));
    REQUIRE(ed.current().size() == 2);

    ed.set_context(ElementContext::Bible);
    REQUIRE_FALSE(ed.insert(ObjectType::Text, ElementBinding::Lyrics));
    REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Static));
    REQUIRE(ed.selected_item() != nullptr);
    REQUIRE(ed.selected_item()->object.text == "New text");
    REQUIRE(ed.dirty());
}

TEST_CASE("a theme without a Web area gets one, once, filling the canvas", "[theme][editor][web]")
{
    // Every theme saved before web pages had an area lacks one.
    auto set = default_elements();
    std::erase_if(set.web, [](const auto& el) { return el.binding == ElementBinding::WebArea; });
    Editor ed;
    ed.load(set);
    ed.set_context(ElementContext::Web);
    REQUIRE(ed.insert(ObjectType::Image, ElementBinding::WebArea));
    const auto* area = ed.selected_item();
    REQUIRE(area != nullptr);
    CHECK(area->binding == ElementBinding::WebArea);
    CHECK(area->object.x == 0);
    CHECK(area->object.w == k_reference_canvas.width);
    CHECK(area->object.h == k_reference_canvas.height);
    CHECK(area->object.object_fit == "contain");
    REQUIRE_FALSE(ed.insert(ObjectType::Image, ElementBinding::WebArea));
}

TEST_CASE("theme editor insert gives the element the type and binding asked for", "[theme][editor]")
{
    Editor ed;
    ed.load(default_elements());
    REQUIRE(ed.insert(ObjectType::Shape, ElementBinding::Static, "star"));
    const auto* shape = ed.selected_item();
    REQUIRE(shape != nullptr);
    CHECK(shape->object.type == ObjectType::Shape);
    CHECK(shape->binding == ElementBinding::Static);
    CHECK(shape->object.shape_kind == seventhslide::geom::ShapeKind::Star);

    ed.set_context(ElementContext::Bible);
    REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Static));
    const auto* text = ed.selected_item();
    REQUIRE(text != nullptr);
    CHECK(text->object.type == ObjectType::Text);
    CHECK(text->binding == ElementBinding::Static);
}

TEST_CASE("theme editor duplicate strips exclusive binding", "[theme][editor]")
{
    Editor ed;
    ed.load(default_elements());
    ed.select("te_lyrics");
    REQUIRE(ed.duplicate_selected());
    const auto* copy = ed.selected_item();
    REQUIRE(copy != nullptr);
    REQUIRE(copy->object.id != "te_lyrics");
    REQUIRE(copy->binding == ElementBinding::Static);
    REQUIRE(copy->object.x == ed.find("te_lyrics")->object.x + 24);
    REQUIRE(ed.current().size() == 3);
}

TEST_CASE("theme editor load puts every context in paint order", "[theme][editor]")
{
    // Each context gets three elements stored out of z order, two of them tied.
    ElementSet set;
    for (const auto ctx : seventhslide::theme::k_element_contexts) {
        for (const auto& [id, z] : {std::pair{"c", 20}, std::pair{"a", 0}, std::pair{"b", 0}}) {
            seventhslide::theme::Element el;
            el.object.id = id;
            el.object.z = z;
            set.at(ctx).push_back(el);
        }
    }

    Editor ed;
    ed.set_context(ElementContext::Titles);
    ed.load(set);
    REQUIRE(ed.context() == ElementContext::Song);
    for (const auto ctx : seventhslide::theme::k_element_contexts) {
        ed.set_context(ctx);
        const auto& list = ed.current();
        REQUIRE(list.size() == 3);
        // Sorted by z; the tie keeps its stored order.
        CHECK(list[0].object.id == "a");
        CHECK(list[1].object.id == "b");
        CHECK(list[2].object.id == "c");
    }
}

TEST_CASE("theme editor reorder stamps z in tens", "[theme][editor]")
{
    Editor ed;
    ed.load(default_elements());
    ed.select("te_lyrics");
    REQUIRE(ed.can_reorder_selected(ZOrderOp::Forward));
    REQUIRE(ed.reorder_selected(ZOrderOp::Forward));
    REQUIRE(ed.current()[0].object.id == "te_copyright");
    REQUIRE(ed.current()[0].object.z == 0);
    REQUIRE(ed.current()[1].object.id == "te_lyrics");
    REQUIRE(ed.current()[1].object.z == 10);
    REQUIRE_FALSE(ed.can_reorder_selected(ZOrderOp::Forward));
}

TEST_CASE("theme editor geometry and JSON round-trip", "[theme][editor]")
{
    Editor ed;
    ed.load(default_elements());
    ed.select("te_lyrics");
    REQUIRE(ed.set_geometry("te_lyrics", 10.4, 20.6, 300, 200, 0));
    REQUIRE(ed.selected_item()->object.x == 10);
    REQUIRE(ed.selected_item()->object.y == 21);
    REQUIRE(ed.selected_item()->object.w == 300);

    const auto blob = ed.set().to_json();
    Editor again;
    nlohmann::json style = {{"elements", blob}};
    again.load_from_style(style, k_reference_canvas);
    again.select("te_lyrics");
    REQUIRE(again.selected_item() != nullptr);
    REQUIRE(again.selected_item()->object.x == 10);
    REQUIRE(again.selected_item()->object.w == 300);
    REQUIRE_FALSE(again.dirty());
}

TEST_CASE("theme editor field clamps and text persist", "[theme][editor]")
{
    Editor ed;
    ed.load(ElementSet{});
    REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Static));
    REQUIRE(ed.set_property("font_size", 4));
    REQUIRE(ed.selected_item()->object.font_size == 8);
    REQUIRE(ed.set_property("width", 3));
    REQUIRE(ed.selected_item()->object.w == 8);
    REQUIRE(ed.set_property("opacity", 2.5));
    REQUIRE(ed.selected_item()->object.opacity == 1.0);
    REQUIRE(ed.set_text(ed.selected_id(), "Hello", true, false, true));
    REQUIRE(ed.selected_item()->object.text == "Hello");
    REQUIRE(ed.selected_item()->object.bold);
    REQUIRE(ed.selected_item()->object.underline);
}

TEST_CASE("theme editor gesture coalesces geometry into one undo", "[theme][editor]")
{
    Editor ed;
    ed.load(default_elements());
    ed.select("te_lyrics");
    const double x0 = ed.selected_item()->object.x;
    ed.begin_gesture();
    REQUIRE(ed.set_geometry("te_lyrics", x0 + 1, 0, 100, 100, 0));
    REQUIRE(ed.set_geometry("te_lyrics", x0 + 2, 0, 100, 100, 0));
    ed.end_gesture();
    REQUIRE(ed.undo());
    REQUIRE(ed.find("te_lyrics")->object.x == x0);
    REQUIRE_FALSE(ed.can_undo());
}

TEST_CASE("theme editor: a gesture that moves to another context is one undo step",
          "[theme][editor]")
{
    // The gesture's step starts as the song context alone, and takes the
    // whole set once the gesture reaches the Bible context.
    Editor ed;
    ed.load(default_elements());
    const ElementSet start = ed.set();
    const auto song = ed.current().front().object;
    ed.set_context(ElementContext::Bible);
    REQUIRE_FALSE(ed.current().empty());
    const auto bible = ed.current().front().object;
    ed.set_context(ElementContext::Song);

    ed.begin_gesture();
    REQUIRE(ed.set_geometry(song.id, song.x + 10, song.y, song.w, song.h, 0));
    ed.set_context(ElementContext::Bible);
    REQUIRE(ed.set_geometry(bible.id, bible.x + 20, bible.y, bible.w, bible.h, 0));
    ed.set_context(ElementContext::Song);
    REQUIRE(ed.set_geometry(song.id, song.x + 30, song.y, song.w, song.h, 0));
    ed.end_gesture();
    const ElementSet moved = ed.set();
    REQUIRE(moved.at(ElementContext::Song) != start.at(ElementContext::Song));
    REQUIRE(moved.at(ElementContext::Bible) != start.at(ElementContext::Bible));

    REQUIRE(ed.undo());
    CHECK(ed.set() == start);
    CHECK_FALSE(ed.can_undo());
    CHECK_FALSE(ed.dirty());
    REQUIRE(ed.redo());
    CHECK(ed.set() == moved);
}

TEST_CASE("binding helpers match the insert menu", "[theme][elements][binding]")
{
    using seventhslide::theme::binding_fits_context;
    using seventhslide::theme::binding_is_exclusive;
    using seventhslide::theme::element_context_from_string;

    REQUIRE(element_context_from_string("behavior") == ElementContext::Song);
    REQUIRE(element_context_from_string("bible") == ElementContext::Bible);
    REQUIRE(binding_is_exclusive(ElementBinding::Lyrics));
    REQUIRE(binding_is_exclusive(ElementBinding::VideoArea));
    REQUIRE_FALSE(binding_is_exclusive(ElementBinding::Indicator));
    REQUIRE(binding_fits_context(ElementContext::Song, ElementBinding::Lyrics));
    REQUIRE_FALSE(binding_fits_context(ElementContext::Bible, ElementBinding::Lyrics));
    REQUIRE(binding_fits_context(ElementContext::Video, ElementBinding::Countdown));
    REQUIRE_FALSE(binding_fits_context(ElementContext::Image, ElementBinding::Countdown));
    REQUIRE(binding_fits_context(ElementContext::Live, ElementBinding::VideoArea));
    REQUIRE_FALSE(binding_fits_context(ElementContext::Image, ElementBinding::VideoArea));
    // Audio is no context: its name is no context's.
    REQUIRE(element_context_from_string("audio") == ElementContext::Song);
    REQUIRE(element_context_from_string("live") == ElementContext::Live);
    REQUIRE_FALSE(binding_fits_context(ElementContext::Song, ElementBinding::VideoArea));
    REQUIRE(binding_is_exclusive(ElementBinding::WebArea));
    REQUIRE(binding_fits_context(ElementContext::Web, ElementBinding::WebArea));
    REQUIRE_FALSE(binding_fits_context(ElementContext::Video, ElementBinding::WebArea));
    REQUIRE_FALSE(binding_fits_context(ElementContext::Bible, ElementBinding::WebArea));
    REQUIRE(seventhslide::theme::element_binding_from_string("web_area") == ElementBinding::WebArea);
    REQUIRE(seventhslide::theme::to_string(ElementBinding::WebArea) == "web_area");
}

// The slide editor offers a control only where it changes what paints.
TEST_CASE("An object is offered the controls that change how it paints", "[theme][elements][binding]")
{
    using seventhslide::theme::object_controls;
    using seventhslide::theme::ObjectControls;

    const auto text = object_controls(ObjectType::Text, ElementBinding::Static);
    CHECK(text == ObjectControls{.runs = true, .weight = true, .font = true, .color = true,
                                 .align = true, .effects = true, .fill = true});
    // A lyrics body keeps its frame; a clock has no runs; a table no frame.
    CHECK(object_controls(ObjectType::Text, ElementBinding::Lyrics) == text);
    CHECK(object_controls(ObjectType::Clock, ElementBinding::Static)
          == ObjectControls{.weight = true, .font = true, .color = true, .align = true,
                            .clock = true, .effects = true, .fill = true});
    CHECK(object_controls(ObjectType::Table, ElementBinding::Static)
          == ObjectControls{.font = true, .color = true, .align = true});
    CHECK(object_controls(ObjectType::Shape, ElementBinding::Static)
          == ObjectControls{.shape = true, .effects = true, .fill = true});
    CHECK(object_controls(ObjectType::Indicator, ElementBinding::Indicator)
          == ObjectControls{.font = true, .color = true, .align = true, .indicator = true,
                            .effects = true, .fill = true});
    // A countdown's time is the video's: no 24-hour or seconds.
    CHECK_FALSE(object_controls(ObjectType::Clock, ElementBinding::Countdown).clock);
    CHECK(object_controls(ObjectType::Clock, ElementBinding::Countdown).fill);
    // A picture fits; the areas paint what the service brings, with no frame.
    CHECK(object_controls(ObjectType::Image, ElementBinding::Static)
          == ObjectControls{.fit = true, .effects = true});
    CHECK(object_controls(ObjectType::Video, ElementBinding::Static)
          == ObjectControls{.fit = true, .effects = true});
    CHECK(object_controls(ObjectType::Image, ElementBinding::ImageArea) == ObjectControls{.fit = true});
    CHECK(object_controls(ObjectType::Image, ElementBinding::VideoArea) == ObjectControls{});
    CHECK(object_controls(ObjectType::Image, ElementBinding::WebArea) == ObjectControls{.fit = true});
}

TEST_CASE("theme editor groups, aligns, and locks the selection", "[theme][editor][layout]")
{
    using seventhslide::canvas::Align;
    using seventhslide::canvas::FlipAxis;
    Editor ed;
    ed.load(default_elements());
    REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Static));
    const auto a = ed.selected_id();
    REQUIRE(ed.set_geometry(a, 40, 40, 80, 40, 0));
    REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Static));
    const auto b = ed.selected_id();
    REQUIRE(ed.set_geometry(b, 200, 80, 80, 40, 0));

    ed.select_ids({a, b});
    REQUIRE(ed.can_group_selected());
    REQUIRE(ed.group_selected());
    ed.clear_selection();
    ed.select(a);
    REQUIRE(ed.selected_ids().size() == 2);

    REQUIRE(ed.align_selected(Align::Top));
    REQUIRE(ed.find(a)->object.y == ed.find(b)->object.y);
    REQUIRE(ed.undo());

    ed.select_ids({a, b});
    REQUIRE(ed.flip_selected(FlipAxis::Horizontal));
    REQUIRE(ed.find(a)->object.flip_h);

    REQUIRE(ed.set_selected_locked(true));
    REQUIRE(ed.selection_locked());
    REQUIRE_FALSE(ed.delete_selected());
    REQUIRE(ed.set_selected_locked(false));
    REQUIRE(ed.ungroup_selected());
    REQUIRE_FALSE(ed.can_ungroup_selected());
}

TEST_CASE("a failed group leaves every object as it was", "[freeform][edit][layout]")
{
    std::vector<seventhslide::freeform::Object> list(2);
    list[0].id = "a";
    list[1].id = "b";
    // Two ids asked for, one present: nothing may be tagged.
    REQUIRE_FALSE(seventhslide::canvas::list_group(list, {"a", "missing"}, "g1"));
    REQUIRE(list[0].group_id.empty());
    REQUIRE(list[1].group_id.empty());
}

TEST_CASE("theme editor takes only the vertical alignments the ribbon offers", "[theme][editor]")
{
    Editor ed;
    ed.load(ElementSet{});
    REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Static));
    // Stored themes may say "center" (read as middle); the editor takes only
    // the names the ribbon offers.
    REQUIRE_FALSE(ed.set_property("valign", "center"));
    CHECK(ed.selected_item()->object.vertical_align == "middle");
    REQUIRE(ed.set_property("valign", "top"));
    CHECK(ed.selected_item()->object.vertical_align == "top");
}
