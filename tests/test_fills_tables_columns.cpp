// Plan 4.7's 9: gradient fills, tables and text in columns.
#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/canvas/object.hpp>
#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/object_properties.hpp>
#include <seventhslide/freeform/tokens.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/overlays.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/render/shape_painter.hpp>
#include <seventhslide/render/table_painter.hpp>

#include <nlohmann/json.hpp>

#include <QColor>
#include <QImage>
#include <QPainter>

#include <string>
#include <vector>

using seventhslide::freeform::Object;
using seventhslide::freeform::set_object_property;

TEST_CASE("A gradient fill runs from its first colour to its last", "[render][fill][gradient]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::canvas::Fill fill;
    fill.kind = seventhslide::canvas::FillKind::Gradient;
    CHECK_FALSE(seventhslide::canvas::fill_paints(fill));
    fill.gradient.angle = 0;  // left to right
    fill.gradient.stops = {{0.0, "#ff0000"}, {1.0, "#0000ff"}};
    REQUIRE(seventhslide::canvas::fill_paints(fill));
    QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter painter(&img);
        seventhslide::render::paint_shape_outline(painter, seventhslide::geom::ShapeKind::Rectangle, 200,
                                                  100, {}, fill, seventhslide::canvas::Line{});
    }
    CHECK(qRed(img.pixel(3, 50)) > 220);
    CHECK(qBlue(img.pixel(3, 50)) < 40);
    CHECK(qBlue(img.pixel(196, 50)) > 220);
    CHECK(qRed(img.pixel(196, 50)) < 40);
    // Radial: the middle the first colour, the corners toward the last.
    fill.gradient.kind = seventhslide::canvas::GradientKind::Radial;
    img.fill(Qt::black);
    {
        QPainter painter(&img);
        seventhslide::render::paint_shape_outline(painter, seventhslide::geom::ShapeKind::Rectangle, 200,
                                                  100, {}, fill, seventhslide::canvas::Line{});
    }
    CHECK(qRed(img.pixel(100, 50)) > 220);
    CHECK(qBlue(img.pixel(2, 2)) > qRed(img.pixel(2, 2)));
}

TEST_CASE("A gradient is chosen and set by its properties", "[freeform][properties][gradient]")
{
    Object obj;
    obj.type = seventhslide::freeform::ObjectType::Shape;
    obj.fill.color = "#336699";
    REQUIRE(set_object_property(obj, "fill_kind", "gradient"));
    REQUIRE(obj.fill.gradient.stops.size() == 2);
    CHECK(obj.fill.gradient.stops.front().color == "#336699");
    REQUIRE(set_object_property(obj, "gradient_to", "#ffcc00"));
    REQUIRE(set_object_property(obj, "gradient_kind", "radial"));
    REQUIRE(set_object_property(obj, "gradient_angle", 45));
    CHECK(obj.fill.gradient.stops.back().color == "#ffcc00");
    CHECK(obj.fill.gradient.kind == seventhslide::canvas::GradientKind::Radial);
    CHECK(obj.fill.gradient.angle == 45);
    const auto read = seventhslide::freeform::object_property_values(obj);
    CHECK(read.at("fill_kind") == "gradient");
    CHECK(read.at("gradient_from") == "#336699");
}

TEST_CASE("A table's size keeps its cells where they were", "[freeform][properties][table]")
{
    auto doc = seventhslide::freeform::make_document("Table", {"Main"});
    auto obj = seventhslide::freeform::make_table_object(doc, 2, 2, {1920, 1080});
    REQUIRE(obj.table.cells.size() == 4);
    CHECK(obj.table.header);
    REQUIRE(set_object_property(obj, "table_cells", nlohmann::json::array({"a", "b", "c", "d"})));
    REQUIRE(set_object_property(obj, "table_columns", 3));
    CHECK(obj.table.cells == std::vector<std::string>{"a", "b", "", "c", "d", ""});
    REQUIRE(set_object_property(obj, "table_rows", 1));
    CHECK(obj.table.cells == std::vector<std::string>{"a", "b", ""});
    CHECK_FALSE(set_object_property(obj, "table_rows", "many"));
    // Stored and read back.
    const nlohmann::json j = static_cast<const seventhslide::canvas::Object&>(obj);
    const auto back = j.get<seventhslide::canvas::Object>();
    CHECK(back.table == obj.table);
}

TEST_CASE("A table paints its cells, header and borders", "[render][table]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::canvas::Object table;
    table.type = seventhslide::canvas::ObjectType::Table;
    table.w = 200;
    table.h = 100;
    table.font_size = 20;
    table.font_color = "#ffffff";
    table.table.rows = 2;
    table.table.columns = 2;
    table.table.cells = {"", "", "", ""};
    table.table.header = true;
    table.table.border_color = "#00ff00";
    table.table.border_width = 2;
    QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter painter(&img);
        seventhslide::render::paint_table_object(painter, table);
    }
    // The line between the columns, the header tinted, the body plain.
    CHECK(qGreen(img.pixel(100, 75)) > 200);
    CHECK(qGreen(img.pixel(50, 25)) > 40);
    CHECK(qGreen(img.pixel(50, 75)) < 20);
}

TEST_CASE("Words in columns fill one column, then the next", "[layout][columns]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::layout::OverlayBox box;
    box.w = 400;
    box.h = 120;
    box.font_size = 40;
    for (int i = 0; i < 4; ++i) {
        box.lines.push_back({{"Line"}});
    }
    const auto single = seventhslide::layout::lay_out_overlay(box, measurer);
    REQUIRE(single.rows.size() == 4);
    for (const auto& row : single.rows) {
        CHECK(row.x == 0);
    }
    box.paragraph.columns = 2;
    box.paragraph.column_gap = 20;
    CHECK(seventhslide::layout::overlay_column_width(box) == 190);
    const auto two = seventhslide::layout::lay_out_overlay(box, measurer);
    REQUIRE(two.rows.size() == 4);
    CHECK(two.rows.front().x == 0);
    CHECK(two.rows.back().x == 210);
    CHECK(two.rows.back().y < two.rows.front().y + box.h);
    CHECK(two.height <= box.h);
    CHECK(two.height < single.height);
}

TEST_CASE("A table's cells show fields", "[table][tokens]")
{
    auto doc = seventhslide::freeform::make_document("Table", {"out-main"});
    auto table = seventhslide::freeform::make_table_object(doc, 1, 1, {1920, 1080});
    table.table.header = false;
    table.table.cells = {"{speaker}"};
    doc.decks[0].slides[0].objects = {table};
    doc.fields["speaker"] = "Ana";
    seventhslide::layout::ApproxTextMeasurer measurer;
    const auto compiled = seventhslide::freeform::compile(doc, &measurer);
    REQUIRE(compiled.decks.front().slides.front().objects.front().object);
    CHECK(compiled.decks.front().slides.front().objects.front().object->table.cells.front() == "Ana");
    // Used, as the Fields window counts it.
    CHECK(seventhslide::freeform::count_field_usage(doc, "speaker") == 1);
}

TEST_CASE("A radial gradient starts at its centre", "[render][fill][gradient]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::canvas::Fill fill;
    fill.kind = seventhslide::canvas::FillKind::Gradient;
    fill.gradient.kind = seventhslide::canvas::GradientKind::Radial;
    fill.gradient.center_x = 0.0;
    fill.gradient.center_y = 0.0;
    fill.gradient.stops = {{0.0, "#ff0000"}, {1.0, "#0000ff"}};
    QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter painter(&img);
        seventhslide::render::paint_shape_outline(painter, seventhslide::geom::ShapeKind::Rectangle, 200, 100, {},
                                                  fill, seventhslide::canvas::Line{});
    }
    // Red at its centre in the top left, blue at the corner farthest from it.
    CHECK(qRed(img.pixel(2, 2)) > 220);
    CHECK(qBlue(img.pixel(197, 97)) > 220);
    // Stored only when it is not the middle.
    nlohmann::json j = fill.gradient;
    CHECK(j.at("center_x") == 0.0);
    CHECK_FALSE(nlohmann::json(seventhslide::canvas::Gradient{}).contains("center_x"));
    CHECK(j.get<seventhslide::canvas::Gradient>() == fill.gradient);
    // And set by its property.
    Object obj;
    obj.type = seventhslide::freeform::ObjectType::Shape;
    REQUIRE(set_object_property(obj, "gradient_center_x", 0.25));
    CHECK(obj.fill.gradient.center_x == 0.25);
    CHECK(seventhslide::freeform::object_property_values(obj).at("gradient_center_x") == 0.25);
}

TEST_CASE("A paragraph sets its own alignment and spacing", "[layout][paragraph]")
{
    using seventhslide::model::LineFormat;
    using seventhslide::model::parse_markup_lines;
    using seventhslide::model::format_markup_lines;
    // In the markup, round and round; a tag elsewhere or malformed is not one.
    const std::string text = "<p=align:center,before:50,after:25,indent:100,line:150>One\nTwo";
    auto lines = parse_markup_lines(text);
    REQUIRE(lines.size() == 2);
    CHECK(seventhslide::model::plain_text(lines[0]) == "One");
    const LineFormat& format = lines[0].front().style.line;
    CHECK(format.align == "center");
    CHECK(format.before_pct == 50);
    CHECK(format.after_pct == 25);
    CHECK(format.indent_pct == 100);
    CHECK(format.line_pct == 150);
    CHECK_FALSE(lines[1].front().style.line.any());
    CHECK(format_markup_lines(lines) == text);
    CHECK(seventhslide::model::plain_text(parse_markup_lines("<p=nonsense>x")[0]) == "<p=nonsense>x");
    CHECK(seventhslide::model::strip_markup_tags("<p=align:right>x") == "x");
    // A paragraph of nothing but a format keeps it.
    CHECK(format_markup_lines(parse_markup_lines("<p=align:right>")) == "<p=align:right>");

    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::layout::OverlayBox box;
    box.w = 800;
    box.h = 400;
    box.font_size = 40;
    box.text_align = "left";
    box.line_height = 1.0;
    const auto plain = [&](const char* t) {
        std::vector<seventhslide::model::RichLine> out;
        for (const auto& line : parse_markup_lines(t)) {
            out.push_back(seventhslide::layout::styled_line(line, false, false, false));
        }
        return out;
    };
    box.lines = plain("One\nTwo\nThree");
    const auto base = seventhslide::layout::lay_out_overlay(box, measurer);
    box.lines = plain("One\n<p=align:right,before:100,after:50,line:200>Two\nThree");
    const auto own = seventhslide::layout::lay_out_overlay(box, measurer);
    REQUIRE(own.rows.size() == 3);
    CHECK(own.rows[0].align.empty());
    CHECK(own.rows[1].align == "right");
    CHECK(own.rows[2].align.empty());
    // 40 px before it, twice as tall, 20 px after it.
    CHECK(own.rows[1].y == base.rows[1].y + 40);
    CHECK(own.rows[1].height == 2 * base.rows[1].height);
    CHECK(own.rows[2].y == own.rows[1].y + own.rows[1].height + 20);
}

TEST_CASE("Setting the whole box takes that setting off its paragraphs", "[freeform][properties][paragraph]")
{
    Object obj;
    obj.type = seventhslide::freeform::ObjectType::Text;
    obj.text = "<p=align:right,before:50>One\nTwo";
    REQUIRE(set_object_property(obj, "align", "center"));
    CHECK(obj.text == "<p=before:50>One\nTwo");
    REQUIRE(set_object_property(obj, "space_before", 12));
    CHECK(obj.text == "One\nTwo");
}
