// The one property table both canvas editors write object fields through, and
// the theme element fields on top of it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/freeform/object_properties.hpp>
#include <seventhslide/freeform/paint.hpp>
#include <seventhslide/geom/shape_path.hpp>
#include <seventhslide/theme/element_editor.hpp>
#include <seventhslide/theme/element_properties.hpp>
#include <seventhslide/theme/resolved_theme.hpp>

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

using Catch::Approx;
using seventhslide::freeform::IndicatorFallback;
using seventhslide::freeform::Object;
using seventhslide::freeform::ObjectType;
using seventhslide::freeform::is_geometry_property;
using seventhslide::freeform::k_max_font_size;
using seventhslide::freeform::k_max_stroke_width;
using seventhslide::freeform::normalized_rotation;
using seventhslide::freeform::object_property_ranges;
using seventhslide::freeform::object_property_values;
using seventhslide::freeform::set_object_property;
using seventhslide::geom::ShapeKind;
using seventhslide::theme::Element;
using seventhslide::theme::ElementBinding;
using seventhslide::theme::element_property_ranges;
using seventhslide::theme::element_property_values;
using seventhslide::theme::set_element_property;
using json = nlohmann::json;

TEST_CASE("object properties take values of their own type only", "[freeform][properties]")
{
    Object o;
    REQUIRE(set_object_property(o, "font_size", 60));
    CHECK(o.font_size == 60);
    // QML sends whole numbers as doubles.
    REQUIRE(set_object_property(o, "font_size", 61.6));
    CHECK(o.font_size == 62);
    REQUIRE(set_object_property(o, "bold", true));
    CHECK(o.bold);

    // A wrong type is refused and changes nothing - no "true" strings, no
    // numbers for switches, no numbers for names.
    const Object before = o;
    CHECK_FALSE(set_object_property(o, "font_size", "72"));
    CHECK_FALSE(set_object_property(o, "bold", 0));
    CHECK_FALSE(set_object_property(o, "bold", "true"));
    CHECK_FALSE(set_object_property(o, "font_family", 12));
    CHECK_FALSE(set_object_property(o, "opacity", nullptr));
    CHECK_FALSE(set_object_property(o, "opacity", json(std::nan(""))));
    CHECK(o == before);
}

TEST_CASE("A property refused for its value's type leaves the object as it was", "[freeform][properties]")
{
    // A text box whose lines carry their own formats: some writers clear them.
    Object o;
    o.type = ObjectType::Text;
    o.text = "<p=align:center,before:50,after:25,indent:100,line:150>One\nTwo";
    const json values = object_property_values(o);
    const json wrongs = json::array({json::object(), json::array({1, 2})});
    int refused = 0;
    for (const auto& [name, value] : values.items()) {
        for (const json& wrong : wrongs) {
            INFO(name << " = " << wrong.dump());
            Object copy = o;
            if (!set_object_property(copy, name, wrong)) {
                ++refused;
                CHECK(copy == o);
            }
        }
    }
    CHECK(refused > 40);
}

TEST_CASE("object properties refuse unknown names and geometry", "[freeform][properties]")
{
    Object o;
    const Object before = o;
    const auto r = set_object_property(o, "colour", "#ff0000");
    REQUIRE_FALSE(r);
    CHECK(r.error.find("colour") != std::string::npos);
    // Geometry goes through the editors' geometry commands.
    for (const char* name : {"x", "y", "width", "height", "rotation"}) {
        INFO(name);
        CHECK(is_geometry_property(name));
        CHECK_FALSE(set_object_property(o, name, 10));
    }
    CHECK_FALSE(is_geometry_property("font_size"));
    CHECK(o == before);
}

TEST_CASE("object properties take the names the ribbon offers", "[freeform][properties]")
{
    Object o;
    for (const char* a : {"left", "center", "right"}) {
        REQUIRE(set_object_property(o, "align", a));
        CHECK(o.text_align == a);
    }
    CHECK_FALSE(set_object_property(o, "align", "justify"));
    for (const char* a : {"top", "middle", "bottom"}) {
        REQUIRE(set_object_property(o, "valign", a));
        CHECK(o.vertical_align == a);
    }
    // Stored data may say "center"; the editor takes only the ribbon's names.
    CHECK_FALSE(set_object_property(o, "valign", "center"));
    for (const char* f : {"contain", "cover", "fill"}) {
        REQUIRE(set_object_property(o, "object_fit", f));
    }
    CHECK_FALSE(set_object_property(o, "object_fit", "stretch"));

    REQUIRE(set_object_property(o, "shape_kind", "star"));
    CHECK(o.shape_kind == ShapeKind::Star);
    // The lenient reader would turn a typo into a rectangle; the editor refuses it.
    CHECK_FALSE(set_object_property(o, "shape_kind", "strar"));
    CHECK(o.shape_kind == ShapeKind::Star);

    REQUIRE(set_object_property(o, "indicator_fallback", "blank"));
    CHECK(o.indicator_fallback == IndicatorFallback::Blank);
    CHECK_FALSE(set_object_property(o, "indicator_fallback", "nothing"));

    REQUIRE(set_object_property(o, "indicator_gap", 0.5));
    CHECK(o.indicator_gap == 0.5);
    CHECK_FALSE(set_object_property(o, "indicator_gap", "wide"));
    CHECK(o.indicator_gap == 0.5);

    for (const char* d : {"solid", "dash", "dot", "dash_dot"}) {
        REQUIRE(set_object_property(o, "line_dash", d));
        CHECK(seventhslide::canvas::to_string(o.line.dash) == d);
    }
    CHECK_FALSE(set_object_property(o, "line_dash", "dashed"));
    for (const char* k : {"none", "solid"}) {
        REQUIRE(set_object_property(o, "fill_kind", k));
        CHECK(seventhslide::canvas::to_string(o.fill.kind) == k);
    }
    // A gradient starts from the box's colour; a picture fill is a kind, and
    // choosing its file makes it one.
    REQUIRE(set_object_property(o, "fill_kind", "gradient"));
    CHECK(o.fill.gradient.stops.size() == 2);
    CHECK(object_property_values(o).at("fill_kind") == "gradient");
    REQUIRE(set_object_property(o, "fill_kind", "picture"));
    CHECK(object_property_values(o).at("fill_kind") == "picture");
    CHECK_FALSE(seventhslide::canvas::fill_paints(o.fill));
    REQUIRE(set_object_property(o, "fill_kind", "none"));
    REQUIRE(set_object_property(o, "fill_picture_file", "photo.png"));
    CHECK(o.fill.kind == seventhslide::canvas::FillKind::Picture);
    CHECK(seventhslide::canvas::fill_paints(o.fill));
    CHECK_FALSE(set_object_property(o, "fill_picture_file", ""));
    CHECK_FALSE(set_object_property(o, "fill_picture_fit", "stretch"));
    for (const char* r : {"none", "grayscale", "tint"}) {
        REQUIRE(set_object_property(o, "recolor", r));
        CHECK(seventhslide::canvas::to_string(o.picture.recolor) == r);
    }
    CHECK_FALSE(set_object_property(o, "recolor", "sepia"));
    // A crop keeps a tenth of the picture; a transparency is a share.
    REQUIRE(set_object_property(o, "crop_left", 0.95));
    CHECK(o.picture.crop.left == seventhslide::canvas::k_max_crop);
    REQUIRE(set_object_property(o, "picture_transparency", 2));
    CHECK(o.picture.transparency == 1.0);

    for (const char* b : {"none", "disc", "number"}) {
        REQUIRE(set_object_property(o, "bullets", b));
        CHECK(seventhslide::canvas::to_string(o.paragraph.bullets) == b);
    }
    CHECK_FALSE(set_object_property(o, "bullets", "dots"));
    REQUIRE(set_object_property(o, "autofit", "shrink"));
    CHECK(o.paragraph.autofit == seventhslide::canvas::Autofit::Shrink);
    CHECK_FALSE(set_object_property(o, "autofit", "grow"));
    REQUIRE(set_object_property(o, "margin_left", -3));
    CHECK(o.paragraph.margins.left == 0);
}

TEST_CASE("Effects are set a field at a time and kept in range", "[freeform][properties][effects]")
{
    Object o;
    REQUIRE(set_object_property(o, "shadow_on", true));
    REQUIRE(set_object_property(o, "shadow_blur", 20));
    REQUIRE(set_object_property(o, "shadow_distance", -4));
    REQUIRE(set_object_property(o, "shadow_angle", 405));
    CHECK(o.effects.shadow.on);
    CHECK(o.effects.shadow.blur == 20);
    CHECK(o.effects.shadow.distance == 0);
    CHECK(o.effects.shadow.angle == 45);
    CHECK_FALSE(set_object_property(o, "shadow_blur", "soft"));
    CHECK_FALSE(set_object_property(o, "shadow_on", 1));
    CHECK_FALSE(set_object_property(o, "glow_color", ""));
    REQUIRE(set_object_property(o, "soft_edge", 1000));
    CHECK(o.effects.soft_edge == k_max_stroke_width);
    REQUIRE(set_object_property(o, "text_outline_width", 3));
    CHECK(object_property_values(o).at("text_outline_width") == 3.0);
    CHECK(object_property_values(o).at("shadow_angle") == 45.0);
}

TEST_CASE("A fill's kind says what paints; a line is its colour, width and dash",
          "[freeform][properties][fill][line]")
{
    using seventhslide::canvas::FillKind;
    using seventhslide::canvas::fill_paints;
    using seventhslide::canvas::line_paints;
    using seventhslide::canvas::paints_frame;

    // A new text box has no fill: solid, but no colour, so none paints and
    // the sidebar reads none.
    Object o;
    CHECK_FALSE(fill_paints(o.fill));
    CHECK(object_property_values(o).at("fill_kind") == "none");
    CHECK_FALSE(paints_frame(o));

    // Solid on an empty colour takes a translucent one, so something shows
    // and the box's text still does.
    REQUIRE(set_object_property(o, "fill_kind", "solid"));
    CHECK(o.fill.color == seventhslide::freeform::k_default_box_fill);
    CHECK(fill_paints(o.fill));
    CHECK(object_property_values(o).at("fill_kind") == "solid");
    CHECK(paints_frame(o));

    // A shape's takes a new shape's opaque colour instead.
    Object shape;
    shape.type = seventhslide::freeform::ObjectType::Shape;
    REQUIRE(set_object_property(shape, "fill_kind", "solid"));
    CHECK(shape.fill.color == seventhslide::freeform::k_default_shape_fill);

    // None keeps the colour for when the fill comes back.
    REQUIRE(set_object_property(o, "fill", "#123456"));
    REQUIRE(set_object_property(o, "fill_kind", "none"));
    CHECK(o.fill.color == "#123456");
    CHECK_FALSE(fill_paints(o.fill));
    CHECK(object_property_values(o).at("fill_kind") == "none");
    REQUIRE(set_object_property(o, "fill_kind", "solid"));
    CHECK(o.fill.color == "#123456");

    // A line paints with a colour and a width; a text box starts with neither.
    Object t;
    CHECK_FALSE(line_paints(t.line));
    REQUIRE(set_object_property(t, "stroke", "#ffffff"));
    CHECK(line_paints(t.line));
    CHECK(paints_frame(t));
    REQUIRE(set_object_property(t, "stroke_width", 0));
    CHECK_FALSE(line_paints(t.line));
    CHECK_FALSE(paints_frame(t));
    REQUIRE(set_object_property(t, "stroke_width", 2));
    REQUIRE(set_object_property(t, "line_dash", "dot"));
    CHECK(object_property_values(t).at("line_dash") == "dot");

    // A shape's fill and line are its outline, not a frame; a picture has none.
    Object sh;
    sh.type = ObjectType::Shape;
    sh.fill.color = "#ffffff";
    CHECK_FALSE(paints_frame(sh));
    Object pic;
    pic.type = ObjectType::Image;
    pic.fill.color = "#ffffff";
    CHECK_FALSE(paints_frame(pic));
}

TEST_CASE("object properties bring numbers into range", "[freeform][properties]")
{
    Object o;
    REQUIRE(set_object_property(o, "opacity", 2.5));
    CHECK(o.opacity == 1.0);
    REQUIRE(set_object_property(o, "opacity", -1));
    CHECK(o.opacity == 0.0);
    REQUIRE(set_object_property(o, "indicator_gap", 0));
    CHECK(o.indicator_gap == 0.1);
    REQUIRE(set_object_property(o, "indicator_gap", 12));
    CHECK(o.indicator_gap == 4.0);
    REQUIRE(set_object_property(o, "font_size", 4));
    CHECK(o.font_size == 8);
    REQUIRE(set_object_property(o, "font_size", 1e9));
    CHECK(o.font_size == k_max_font_size);
    REQUIRE(set_object_property(o, "font_size", 4320));
    CHECK(o.font_size == 4320);
    REQUIRE(set_object_property(o, "stroke_width", -3));
    CHECK(o.line.width == 0.0);
    REQUIRE(set_object_property(o, "stroke_width", 1000));
    CHECK(o.line.width == k_max_stroke_width);
    REQUIRE(set_object_property(o, "upcoming_font_size", 0));
    CHECK(o.upcoming_font_size == 0);
    REQUIRE(set_object_property(o, "upcoming_font_size", 1e9));
    CHECK(o.upcoming_font_size == k_max_font_size);
    REQUIRE(set_object_property(o, "upcoming_font_size", 3));
    CHECK(o.upcoming_font_size == 8);

    // Shape knobs stay inside the kind's window.
    o.shape_kind = ShapeKind::DoubleArrow;
    REQUIRE(set_object_property(o, "shape_head", 4.0));
    CHECK(o.shape_adjust.head == Approx(0.5));
}

TEST_CASE("object properties need a name where a name is the value", "[freeform][properties]")
{
    Object o;
    CHECK_FALSE(set_object_property(o, "font_family", ""));
    CHECK_FALSE(set_object_property(o, "font_color", ""));
    CHECK_FALSE(set_object_property(o, "filename", ""));
    // An empty fill or outline means none.
    REQUIRE(set_object_property(o, "fill", ""));
    REQUIRE(set_object_property(o, "stroke", ""));
}

TEST_CASE("rotation wraps into one turn", "[freeform][properties]")
{
    CHECK(normalized_rotation(370) == Approx(10));
    CHECK(normalized_rotation(-90) == Approx(270));
    CHECK(normalized_rotation(12.5) == Approx(12.5));
    CHECK(normalized_rotation(std::nan("")) == 0.0);
}

TEST_CASE("element properties add the theme's own fields", "[theme][properties]")
{
    Element el;
    REQUIRE(set_element_property(el, "rule", "first"));
    CHECK(seventhslide::theme::to_string(el.visibility.rule) == "first");
    CHECK_FALSE(set_element_property(el, "rule", "sometimes"));
    REQUIRE(set_element_property(el, "count", 250));
    CHECK(el.visibility.count == 99);
    REQUIRE(set_element_property(el, "verse_margin_pct", 900));
    CHECK(el.body.verse_margin_right_pct == 400);
    REQUIRE(set_element_property(el, "verse_valign", "baseline"));
    CHECK_FALSE(set_element_property(el, "verse_valign", "sub"));
    REQUIRE(set_element_property(el, "label", "Logo"));
    CHECK(el.label == "Logo");
    // Object fields go through the shared table, with its rules.
    REQUIRE(set_element_property(el, "font_size", 60));
    CHECK(el.object.font_size == 60);
    CHECK_FALSE(set_element_property(el, "valign", "center"));
    // One name per field: the old aliases are gone.
    for (const char* alias : {"w", "h", "text_align", "vertical_align", "slide_rule",
                              "slide_count", "verse_margin_right_pct"}) {
        INFO(alias);
        CHECK_FALSE(set_element_property(el, alias, 1));
    }
}

TEST_CASE("element properties read back by the names they are written by", "[theme][properties]")
{
    // Every name the designer's ribbon and panels write, with a value that is
    // not the default.
    const json written = {
        {"label", "Logo"},
        {"rule", "first"},
        {"count", 3},
        {"show_when", "song_copyright"},
        {"active_line_family", "Georgia"},
        {"active_line_size", 40},
        {"active_line_bold", true},
        {"active_line_italic", false},
        {"active_line_underline", true},
        {"active_line_color", "#f0f0f0"},
        {"other_lines_family", "Georgia"},
        {"other_lines_size", 36},
        {"other_lines_bold", false},
        {"other_lines_italic", true},
        {"other_lines_underline", false},
        {"other_lines_color", "#101010"},
        {"show_chords", true},
        {"chord_size_pct", 90},
        {"chord_gap_pct", 20},
        {"chord_color", "#00ccff"},
        {"verse_numbers", true},
        {"verse_font_family", "Georgia"},
        {"verse_font_size", 30},
        {"verse_font_bold", true},
        {"verse_font_italic", true},
        {"verse_color", "#202020"},
        {"verse_valign", "baseline"},
        {"verse_margin_pct", 120},
        {"font_family", "Georgia"},
        {"font_size", 60},
        {"font_color", "#303030"},
        {"bold", true},
        {"italic", true},
        {"underline", true},
        {"align", "right"},
        {"valign", "bottom"},
        {"line_height", 1.5},
        {"line_gap", 4},
        {"letter_spacing", 2.0},
        {"opacity", 0.5},
        {"object_fit", "cover"},
        {"filename", "logo.png"},
        {"fill", "#404040"},
        {"fill_kind", "picture"},
        {"fill_picture_file", "photo.png"},
        {"fill_picture_fit", "contain"},
        {"crop_left", 0.1},
        {"crop_top", 0.2},
        {"crop_right", 0.3},
        {"crop_bottom", 0.4},
        {"recolor", "tint"},
        {"tint", "#ff8800"},
        {"picture_transparency", 0.25},
        {"stroke", "#505050"},
        {"stroke_width", 3.0},
        {"line_dash", "dot"},
        {"shadow_on", true},
        {"shadow_color", "#40000000"},
        {"shadow_blur", 12.0},
        {"shadow_distance", 9.0},
        {"shadow_angle", 30.0},
        {"glow_on", true},
        {"glow_color", "#80ffff00"},
        {"glow_size", 14.0},
        {"soft_edge", 5.0},
        {"text_outline_color", "#101010"},
        {"text_outline_width", 2.5},
        {"text_shadow_on", true},
        {"text_shadow_color", "#60000000"},
        {"text_shadow_blur", 4.0},
        {"text_shadow_distance", 3.0},
        {"text_shadow_angle", 135.0},
        {"bullets", "disc"},
        {"space_before", 4.0},
        {"space_after", 6.0},
        {"indent", 12.0},
        {"autofit", "shrink"},
        {"margin_left", 1.0},
        {"margin_top", 2.0},
        {"margin_right", 3.0},
        {"margin_bottom", 4.0},
        {"indicator_fallback", "blank"},
        {"indicator_gap", 0.5},
        {"clock_24h", true},
        {"clock_seconds", true},
        {"upcoming_color", "#606060"},
        {"upcoming_font_size", 36},
    };
    Element el;
    for (const auto& [name, value] : written.items()) {
        INFO(name);
        REQUIRE(set_element_property(el, name, value));
    }
    REQUIRE(set_element_property(el, "shape_kind", "star"));
    REQUIRE(set_element_property(el, "shape_points", 7));

    const json read = element_property_values(el);
    for (const auto& [name, value] : written.items()) {
        INFO(name);
        CHECK(read.at(name) == value);
    }
    CHECK(read.at("shape_kind") == "star");
    CHECK(read.at("shape_points") == 7);

    // And everything read can be written back by its name, changing nothing:
    // one vocabulary both ways, geometry aside.
    for (const auto& [name, value] : read.items()) {
        INFO(name);
        if (is_geometry_property(name)) {
            continue;
        }
        Element again = el;
        REQUIRE(set_element_property(again, name, value));
        CHECK(again == el);
    }
}

TEST_CASE("properties read as they paint, geometry in whole pixels", "[freeform][properties]")
{
    Object o;
    o.x = 10.5;
    o.y = -2.5;
    o.w = 99.4;
    o.h = 0.6;
    o.rotation = 12.25;
    const json read = object_property_values(o);
    // Halves round up, as the ribbon showed them.
    CHECK(read.at("x") == 11);
    CHECK(read.at("y") == -2);
    CHECK(read.at("width") == 99);
    CHECK(read.at("height") == 1);
    CHECK(read.at("rotation") == 12.25);
    // Nothing stored: the kind's defaults, as on the slide.
    const auto painted = seventhslide::freeform::painted_type(o);
    CHECK(read.at("font_family") == painted.font_family);
    CHECK_FALSE(read.at("font_family").get<std::string>().empty());
    CHECK(read.at("upcoming_color") == seventhslide::freeform::k_default_upcoming_color);
    CHECK(read.at("upcoming_font_size") == painted.font_size);
}

TEST_CASE("a refused or unchanged property adds no undo step", "[theme][freeform][properties]")
{
    SECTION("theme designer")
    {
        seventhslide::theme::Editor ed;
        ed.load(seventhslide::theme::ElementSet{});
        REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Static));
        CHECK_FALSE(ed.set_property("valign", "sideways"));
        CHECK_FALSE(ed.set_property("no_such_field", 1));
        REQUIRE(ed.set_property("font_size", ed.selected_item()->object.font_size));
        // The only step is the insert.
        REQUIRE(ed.undo());
        CHECK(ed.current().empty());
        CHECK_FALSE(ed.can_undo());
    }
    SECTION("Freeform editor")
    {
        seventhslide::freeform::Editor ed;
        ed.load(seventhslide::freeform::make_document("Doc", {"out"}), "out");
        REQUIRE(ed.add_text());
        CHECK_FALSE(ed.set_property("valign", "sideways"));
        CHECK_FALSE(ed.set_property("no_such_field", 1));
        REQUIRE(ed.set_property("font_size", ed.selected_item()->font_size));
        REQUIRE(ed.undo());
        CHECK(ed.current_slide()->objects.empty());
        CHECK_FALSE(ed.can_undo());
    }
}

TEST_CASE("geometry by name keeps each editor's units", "[theme][freeform][properties]")
{
    SECTION("theme designer: whole pixels, rotation in one turn")
    {
        seventhslide::theme::Editor ed;
        ed.load(seventhslide::theme::ElementSet{});
        REQUIRE(ed.insert(ObjectType::Text, ElementBinding::Static));
        REQUIRE(ed.set_property("x", 10.4));
        REQUIRE(ed.set_property("width", 3));
        REQUIRE(ed.set_property("rotation", 370));
        CHECK(ed.selected_item()->object.x == 10);
        CHECK(ed.selected_item()->object.w == 8);
        CHECK(ed.selected_item()->object.rotation == Approx(10));
        CHECK_FALSE(ed.set_property("x", "left"));
        REQUIRE(ed.set_selected_locked(true));
        CHECK_FALSE(ed.set_property("x", 50));
        CHECK(ed.selected_item()->object.x == 10);
    }
    SECTION("Freeform editor: exact")
    {
        seventhslide::freeform::Editor ed;
        ed.load(seventhslide::freeform::make_document("Doc", {"out"}), "out");
        REQUIRE(ed.add_text());
        REQUIRE(ed.set_property("x", 10.4));
        CHECK(ed.selected_item()->x == Approx(10.4));
        REQUIRE(ed.set_property("rotation", -90));
        CHECK(ed.selected_item()->rotation == Approx(270));
    }
}

TEST_CASE("Both property tables give the ribbon the same reasons for a bad flag or colour",
          "[freeform][theme][properties]")
{
    Object o;
    CHECK(set_object_property(o, "bold", "yes").error == "expected on or off");
    CHECK(set_object_property(o, "font_color", "").error == "color required");
    CHECK(set_object_property(o, "font_color", 3).error == "color required");
    // A shape's fill may be empty (none), but not another type.
    CHECK(set_object_property(o, "fill", "").ok);
    CHECK(set_object_property(o, "fill", 3).error == "expected a color");

    Element el;
    CHECK(set_element_property(el, "show_chords", 1).error == "expected on or off");
    CHECK(set_element_property(el, "verse_color", "").error == "color required");
    CHECK(set_element_property(el, "verse_color", "#336699").ok);
    CHECK(el.body.verse_color == "#336699");
    CHECK(set_element_property(el, "label", 5).error == "expected a label");

    // Chord sizes are kept in range.
    CHECK(set_element_property(el, "chord_size_pct", 500).ok);
    CHECK(el.body.chords.size_pct == seventhslide::layout::k_max_chord_size_pct);
    CHECK(set_element_property(el, "chord_gap_pct", -5).ok);
    CHECK(el.body.chords.gap_pct == 0);
    CHECK(set_element_property(el, "chord_gap_pct", "wide").error == "expected a chord gap");
    CHECK(set_element_property(el, "chord_color", "").error == "color required");
}

TEST_CASE("Each geometry name moves only its own field", "[freeform][properties]")
{
    seventhslide::freeform::Editor ed;
    ed.load(seventhslide::freeform::make_document("Doc", {"out"}), "out");
    REQUIRE(ed.add_text());
    const Object start = *ed.selected_item();
    struct Case {
        const char* name;
        double value;
        double Object::* field;
        double expect;
    };
    const Case cases[] = {
        {"x", 50, &Object::x, 50},
        {"y", 60, &Object::y, 60},
        {"width", 300, &Object::w, 300},
        {"height", 120, &Object::h, 120},
        {"rotation", -90, &Object::rotation, 270},  // into one turn
    };
    for (const auto& c : cases) {
        INFO(c.name);
        ed.load(seventhslide::freeform::make_document("Doc", {"out"}), "out");
        REQUIRE(ed.add_text());
        REQUIRE(ed.set_property(c.name, c.value));
        Object want = start;
        want.id = ed.selected_item()->id;
        want.*(c.field) = c.expect;
        const Object& got = *ed.selected_item();
        CHECK(got.x == Approx(want.x));
        CHECK(got.y == Approx(want.y));
        CHECK(got.w == Approx(want.w));
        CHECK(got.h == Approx(want.h));
        CHECK(got.rotation == Approx(want.rotation));
    }
    CHECK(ed.set_property("width", "300").error == "expected a number");
    CHECK_FALSE(ed.set_property("depth", 3).ok);
}

TEST_CASE("a line's type hands a field back to the box", "[theme][properties]")
{
    Element el;
    REQUIRE(set_element_property(el, "active_line_bold", true));
    REQUIRE(el.body.active_line.bold == true);
    REQUIRE(element_property_values(el)["active_line_bold"] == json(true));
    REQUIRE(set_element_property(el, "active_line_bold", nullptr));
    REQUIRE_FALSE(el.body.active_line.bold.has_value());
    REQUIRE(element_property_values(el)["active_line_bold"].is_null());

    REQUIRE(set_element_property(el, "other_lines_size", 36));
    REQUIRE(set_element_property(el, "other_lines_size", 0));
    REQUIRE(el.body.other_lines.size == 0);
    REQUIRE(set_element_property(el, "other_lines_family", "Georgia"));
    REQUIRE(set_element_property(el, "other_lines_family", ""));
    REQUIRE(el.body.other_lines.family.empty());
    REQUIRE(set_element_property(el, "other_lines_color", ""));
    REQUIRE(el.body.other_lines.color.empty());

    CHECK_FALSE(set_element_property(el, "active_line_size", "big"));
    CHECK_FALSE(set_element_property(el, "active_line_bold", 3));
    CHECK_FALSE(set_element_property(el, "active_line_weight", true));
}

TEST_CASE("an upcoming flag the box has follows the box", "[freeform][properties]")
{
    Object o;
    o.bold = true;
    REQUIRE(set_object_property(o, "upcoming_bold", false));
    CHECK(o.upcoming_bold == false);
    CHECK(object_property_values(o).at("upcoming_bold") == false);
    REQUIRE(set_object_property(o, "upcoming_bold", true));
    CHECK_FALSE(o.upcoming_bold.has_value());
    CHECK(object_property_values(o).at("upcoming_bold") == true);
    REQUIRE(set_object_property(o, "upcoming_italic", true));
    CHECK(o.upcoming_italic == true);
    REQUIRE(set_object_property(o, "upcoming_italic", nullptr));
    CHECK_FALSE(o.upcoming_italic.has_value());
}

namespace {

/// One object of each kind the sidebar edits, the shapes with the two head
/// windows.
std::vector<Object> one_of_each_kind()
{
    std::vector<Object> out;
    for (const auto type : {ObjectType::Text, ObjectType::Image, ObjectType::Video, ObjectType::Table,
                            ObjectType::Indicator, ObjectType::Clock}) {
        Object o;
        o.type = type;
        out.push_back(o);
    }
    for (const auto kind : {ShapeKind::Star, ShapeKind::Arrow, ShapeKind::DoubleArrow}) {
        Object o;
        o.type = ObjectType::Shape;
        o.shape_kind = kind;
        out.push_back(o);
    }
    return out;
}

/// Names whose 0 hands the field back to the box rather than reading as the
/// least size.
const std::set<std::string> k_zero_hands_back{"upcoming_font_size", "active_line_size",
                                              "other_lines_size"};

/// Write each ranged property of ``item`` past both ends of its range
/// through ``set``, and check ``values`` then reads the end it was held to.
template <class Item, class Set, class Values>
void check_held_to_ranges(const Item& item, const json& ranges, Set set, Values values)
{
    REQUIRE_FALSE(ranges.empty());
    for (const auto& [name, range] : ranges.items()) {
        INFO("property " << name);
        const double lo = range.at("min").get<double>();
        const double hi = range.at("max").get<double>();
        REQUIRE(lo < hi);
        const double span = hi - lo;
        Item past = item;
        REQUIRE(set(past, name, hi + span + 1));
        CHECK(values(past).at(name).template get<double>() == Approx(hi));
        if (!k_zero_hands_back.contains(name)) {
            past = item;
            REQUIRE(set(past, name, lo - span - 1));
            CHECK(values(past).at(name).template get<double>() == Approx(lo));
        }
        past = item;
        REQUIRE(set(past, name, lo + span / 2));
        CHECK(values(past).at(name).template get<double>() == Approx(lo + span / 2).margin(0.5));
    }
}

}  // namespace

TEST_CASE("Each number property is held to the range it gives the editors", "[freeform][properties]")
{
    for (const auto& o : one_of_each_kind()) {
        INFO("object type " << static_cast<int>(o.type) << ", shape " << static_cast<int>(o.shape_kind));
        check_held_to_ranges(o, object_property_ranges(o), set_object_property, object_property_values);
    }
    Element el;
    el.binding = ElementBinding::Lyrics;
    el.object.type = ObjectType::Text;
    check_held_to_ranges(el, element_property_ranges(el), set_element_property, element_property_values);
}

TEST_CASE("A double arrow's head range is its own", "[freeform][properties]")
{
    Object o;
    o.type = ObjectType::Shape;
    o.shape_kind = ShapeKind::Arrow;
    CHECK(object_property_ranges(o).at("shape_head").at("max") == json(1.0));
    o.shape_kind = ShapeKind::DoubleArrow;
    CHECK(object_property_ranges(o).at("shape_head").at("max") == json(0.5));
}

TEST_CASE("The element table gives an object field the object table's range", "[theme][properties]")
{
    Element el;
    const json object = object_property_ranges(el.object);
    const json element = element_property_ranges(el);
    for (const auto& [name, range] : object.items()) {
        INFO("property " << name);
        CHECK(element.at(name) == range);
    }
    // The element's own fields, on top.
    CHECK(element.contains("count"));
    CHECK(element.contains("verse_font_size"));
    CHECK(element.contains("active_line_size"));
}

TEST_CASE("Every number a box reads has a range, or is a place or a turn", "[freeform][properties]")
{
    // Geometry is the editors' own (placed through their geometry commands);
    // an angle is brought into one turn rather than held.
    const std::set<std::string> unbounded{"x", "y", "width", "height", "rotation", "gradient_angle",
                                          "shadow_angle", "text_shadow_angle"};
    for (const auto& o : one_of_each_kind()) {
        const json ranges = object_property_ranges(o);
        const json values = object_property_values(o);
        for (const auto& [name, value] : values.items()) {
            if (value.is_number() && !unbounded.contains(name)) {
                INFO("property " << name);
                CHECK(ranges.contains(name));
            }
        }
    }
}

TEST_CASE("A theme's behaviour numbers read within the ranges the designer is given", "[theme][properties]")
{
    const json ranges = seventhslide::theme::theme_behavior_ranges();
    REQUIRE(ranges.size() == 3);
    json style = {{"follow_lines", 500}, {"verse_gap", -40}, {"balance_wrapped_strength", 900}};
    auto b = seventhslide::theme::read_theme_behavior(style);
    CHECK(b.follow_lines == ranges.at("follow_lines").at("max").get<int>());
    CHECK(b.verse_gap == ranges.at("verse_gap").at("min").get<int>());
    CHECK(b.balance_wrapped_strength == ranges.at("balance_wrapped_strength").at("max").get<int>());
    style = {{"follow_lines", 3}, {"verse_gap", 24}, {"balance_wrapped_strength", 40}};
    b = seventhslide::theme::read_theme_behavior(style);
    CHECK(b.follow_lines == 3);
    CHECK(b.verse_gap == 24);
    CHECK(b.balance_wrapped_strength == 40);
}
