#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"

#include "freeform_test_helpers.hpp"
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/media_refs.hpp>
#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/freeform/tokens.hpp>
#include <seventhslide/model/content.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using seventhslide::freeform::Document;
using seventhslide::freeform::IndicatorFallback;
using seventhslide::freeform::ListRowKind;
using seventhslide::freeform::ObjectType;
using seventhslide::freeform::SlideBackgroundKind;
using seventhslide::test::add_object;
using seventhslide::freeform::can_disable_line_control;
using seventhslide::freeform::can_delete_cue;
using seventhslide::freeform::collect_field_names;
using seventhslide::freeform::collect_document_tokens;
using seventhslide::freeform::count_field_usage;
using seventhslide::freeform::make_shape_object;
using seventhslide::freeform::deck_list_rows;
using seventhslide::freeform::delete_cue;
using seventhslide::freeform::delete_slide;
using seventhslide::freeform::document_is_native;
using seventhslide::freeform::document_refusal;
using seventhslide::freeform::duplicate_slide;
using seventhslide::freeform::insert_cue;
using seventhslide::freeform::insert_slide;
using seventhslide::freeform::is_valid_field_name;
using seventhslide::freeform::make_document;
using seventhslide::freeform::make_clock_object;
using seventhslide::freeform::make_indicator_object;
using seventhslide::freeform::make_table_object;
using seventhslide::freeform::make_text_object;
using seventhslide::freeform::move_span_end;
using seventhslide::freeform::normalize;
using seventhslide::freeform::place_slide_thumb;
using seventhslide::freeform::prune_invalid_fields;
using seventhslide::freeform::substitute_fields;
using seventhslide::freeform::rewrite_field_token;
using seventhslide::freeform::reorder_slides;
using seventhslide::freeform::load_document;
using seventhslide::freeform::sanitize_field_name;
using seventhslide::freeform::set_cue_verse;
using seventhslide::freeform::set_line_control;
using seventhslide::test::validate;

namespace {

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("freeform");
}

using seventhslide::test::load_json;

Document load_doc(const char* file)
{
    auto doc = Document::from_json(load_json(fixtures_dir() / file));
    normalize(doc);
    return doc;
}

} // namespace

TEST_CASE("field names: lowercase, no spaces, max 28", "[freeform][tokens]")
{
    REQUIRE(is_valid_field_name("title"));
    REQUIRE(is_valid_field_name("a"));
    REQUIRE(is_valid_field_name("song-title_1"));
    REQUIRE(is_valid_field_name(std::string(28, 'a')));
    REQUIRE_FALSE(is_valid_field_name(""));
    REQUIRE_FALSE(is_valid_field_name("Title"));
    REQUIRE_FALSE(is_valid_field_name("has space"));
    REQUIRE_FALSE(is_valid_field_name("1abc"));
    REQUIRE_FALSE(is_valid_field_name(std::string(29, 'a')));
}

TEST_CASE("Document tokens skip illegal braces", "[freeform][tokens]")
{
    auto doc = make_document("x", {"out-main"});
    seventhslide::freeform::Object text;
    text.text = "{title} {Title} {has space} {-subtitle} {ok}";
    doc.decks[0].slides[0].objects.push_back(text);
    const auto hits = collect_document_tokens(doc);
    REQUIRE(hits.size() == 3);
    REQUIRE(hits[0].name == "title");
    REQUIRE_FALSE(hits[0].hide_line_if_empty);
    REQUIRE(hits[1].name == "subtitle");
    REQUIRE(hits[1].hide_line_if_empty);
    REQUIRE(hits[2].name == "ok");
}

TEST_CASE("JSON round-trip is semantically equal", "[freeform][document]")
{
    const auto original = load_doc("simple_two_outputs.json");
    const auto again = Document::from_json(original.to_json());
    REQUIRE(again.name == original.name);
    REQUIRE(again.line_control == original.line_control);
    REQUIRE(again.fields.at("title") == "Sunday Worship");
    REQUIRE(again.decks.size() == 2);
    REQUIRE(again.decks[0].slides.size() == 3);
    REQUIRE(again.decks[0].slides[0].objects[0].text == "{title}");
    REQUIRE(again.decks[1].output_id == "out-lt");
    REQUIRE(again.decks[1].slides[2].objects[0].text == "LT slot 3");
}

TEST_CASE("visit_document_media names every file, in the model and in the stored JSON",
          "[freeform][document][media]")
{
    using seventhslide::canvas::MediaRefKind;
    auto doc = seventhslide::freeform::make_document("Promo", {"out-a", "out-b"});
    seventhslide::freeform::Object picture;
    picture.type = ObjectType::Image;
    picture.filename = "one.png";
    REQUIRE(add_object(doc, "out-a", 0, picture));
    // A text box's filename names nothing: only an image object places a file.
    seventhslide::freeform::Object text;
    text.type = ObjectType::Text;
    text.filename = "stray.png";
    REQUIRE(add_object(doc, "out-a", 0, text));
    doc.decks[1].slides[0].background.kind = SlideBackgroundKind::Image;
    doc.decks[1].slides[0].background.image = "/static/images/two.png";
    // A colour background's leftover image name is not a file it shows.
    doc.decks[0].slides[0].background.kind = SlideBackgroundKind::Color;
    doc.decks[0].slides[0].background.image = "old.png";

    std::vector<std::string> typed;
    seventhslide::freeform::visit_document_media(std::as_const(doc), [&](const std::string& path,
                                                                        MediaRefKind kind) {
        REQUIRE(kind == MediaRefKind::Picture);
        typed.push_back(path);
    });
    REQUIRE(typed == std::vector<std::string>{"one.png", "/static/images/two.png"});

    const nlohmann::json stored = doc.to_json();
    std::vector<std::string> json;
    seventhslide::freeform::visit_document_media(stored, [&](const nlohmann::json& path,
                                                             MediaRefKind) {
        json.push_back(path.get<std::string>());
    });
    REQUIRE(json == typed);

    // The stored walk can repoint what it finds.
    nlohmann::json rewritten = stored;
    seventhslide::freeform::visit_document_media(rewritten, [](nlohmann::json& path, MediaRefKind) {
        path = "moved/" + path.get<std::string>();
    });
    const auto again = Document::from_json(rewritten);
    REQUIRE(again.decks[0].slides[0].objects[0].filename == "moved/one.png");
    REQUIRE(again.decks[1].slides[0].background.image == "moved//static/images/two.png");
}

TEST_CASE("a slide's speaker notes are stored only when there are any", "[freeform][document][notes]")
{
    auto doc = seventhslide::freeform::make_document("x", {"out-main"});
    nlohmann::json bare = doc.to_json();
    CHECK_FALSE(bare["decks"][0]["slides"][0].contains("notes"));
    doc.decks[0].slides[0].notes = "Pause here for the offering.";
    const nlohmann::json j = doc.to_json();
    CHECK(j["decks"][0]["slides"][0]["notes"] == "Pause here for the offering.");
    const auto again = Document::from_json(j);
    CHECK(again.decks[0].slides[0].notes == "Pause here for the offering.");
    CHECK(again == doc);
}

TEST_CASE("every file field in the object table is one to_json writes", "[freeform][document][media]")
{
    // The table in canvas/media_refs.hpp is what the database walks; an
    // object part that holds a file and is not in it would be missed when a
    // file is deleted or renamed. Each row: a file set through the model is
    // found again in the object's JSON.
    for (const auto& field : seventhslide::canvas::k_object_media_fields) {
        seventhslide::freeform::Object obj;
        obj.type = seventhslide::freeform::object_type_from_string(field.type);
        if (field.key == "fill_picture") {
            obj.fill.kind = seventhslide::canvas::FillKind::Picture;
        }
        int set = 0;
        seventhslide::freeform::visit_media(obj, [&](std::string& path, seventhslide::canvas::MediaRefKind) {
            path = "file-" + std::to_string(set++);
        });
        REQUIRE(set > 0);
        nlohmann::json j;
        seventhslide::freeform::to_json(j, obj);
        std::vector<std::string> found;
        seventhslide::canvas::visit_object_media(std::as_const(j), [&](const nlohmann::json& path,
                                                                       seventhslide::canvas::MediaRefKind) {
            found.push_back(path.get<std::string>());
        });
        REQUIRE(found.size() == static_cast<std::size_t>(set));
        REQUIRE(found.front() == "file-0");
    }
}

TEST_CASE("an object's parts round-trip and are stored only when set", "[freeform][document][parts]")
{
    namespace cv = seventhslide::canvas;
    seventhslide::freeform::Object text;
    text.type = ObjectType::Text;
    text.id = "o1";
    nlohmann::json plain;
    seventhslide::freeform::to_json(plain, text);
    // A text box with nothing set stores the keys it always has and no part.
    for (const char* key : {"fill", "stroke", "stroke_width", "fill_kind", "gradient",
                            "fill_picture", "line_dash", "effects", "paragraph", "picture",
                            "video", "table", "link", "name"}) {
        INFO(key);
        CHECK_FALSE(plain.contains(key));
    }
    seventhslide::freeform::Object read;
    seventhslide::freeform::from_json(plain, read);
    CHECK(read.fill.color.empty());
    CHECK(read.line.color.empty());

    // A stored shape without a fill kind is solid, as every shape was.
    nlohmann::json shape_json = {{"id", "s"}, {"type", "shape"}, {"fill", "#ff0000"}};
    seventhslide::freeform::from_json(shape_json, read);
    CHECK(read.fill.kind == cv::FillKind::Solid);
    CHECK(read.fill.color == "#ff0000");

    // A name an older document gave a box is let go.
    nlohmann::json named = {{"id", "n"}, {"type", "text"}, {"name", "Title line"}};
    seventhslide::freeform::from_json(named, read);
    nlohmann::json resaved;
    seventhslide::freeform::to_json(resaved, read);
    CHECK_FALSE(resaved.contains("name"));

    text.fill.kind = cv::FillKind::Gradient;
    text.fill.gradient = {.kind = cv::GradientKind::Radial,
                          .angle = 30,
                          .stops = {{0, "#000000"}, {1, "#ffffff"}}};
    text.fill.picture = {.file = "paper.png", .fit = "fill", .crop = {.left = 0.1, .bottom = 0.2}};
    text.line = {.color = "#00ff00", .width = 3, .dash = cv::LineDash::DashDot,
                 .start = cv::LineEnd::Arrow, .end = cv::LineEnd::Dot};
    text.effects.shadow = {.on = true, .color = "#ff000000", .blur = 4, .distance = 2, .angle = 90};
    text.effects.glow = {.on = true, .color = "#ffff00ff", .size = 12};
    text.effects.soft_edge = 5;
    text.effects.text_outline = {.color = "#123456", .width = 2};
    text.effects.text_shadow.on = true;
    text.paragraph = {.bullets = cv::Bullets::Number,
                      .space_before = 4,
                      .space_after = 6,
                      .indent = 20,
                      .autofit = cv::Autofit::Shrink,
                      .margins = {.left = 1, .top = 2, .right = 3, .bottom = 4},
                      .columns = 2,
                      .column_gap = 30};
    nlohmann::json j;
    seventhslide::freeform::to_json(j, text);
    seventhslide::freeform::from_json(j, read);
    CHECK(read == text);
    CHECK(j.at("fill_kind") == "gradient");
    CHECK(j.at("line_dash") == "dash_dot");
    // A deck saved while objects could be links reads as if they never were.
    j["link"] = {{"kind", "slide"}, {"slide_id", "s7"}};
    seventhslide::freeform::from_json(j, read);
    CHECK(read == text);

    seventhslide::freeform::Object picture;
    picture.type = ObjectType::Image;
    picture.filename = "a.png";
    picture.picture = {.crop = {.top = 0.25}, .recolor = cv::Recolor::Tint, .tint = "#8040c0",
                       .transparency = 0.5};
    seventhslide::freeform::to_json(j, picture);
    seventhslide::freeform::from_json(j, read);
    CHECK(read == picture);

    seventhslide::freeform::Object video;
    video.type = ObjectType::Video;
    video.filename = "clip.mp4";
    video.video = {.start = cv::VideoStart::OnClick, .loop = true, .mute = true, .poster = "p.png"};
    seventhslide::freeform::to_json(j, video);
    CHECK(j.at("type") == "video");
    seventhslide::freeform::from_json(j, read);
    CHECK(read == video);

    seventhslide::freeform::Object table;
    table.type = ObjectType::Table;
    table.table = {.rows = 2, .columns = 2, .cells = {"a", "b", "c", "d"}, .border_color = "#888888",
                   .border_width = 2, .header = true, .banding = true};
    seventhslide::freeform::to_json(j, table);
    seventhslide::freeform::from_json(j, read);
    CHECK(read == table);

    seventhslide::freeform::Object clock;
    clock.type = ObjectType::Clock;
    clock.clock_mode = cv::ClockMode::Countdown;
    seventhslide::freeform::to_json(j, clock);
    CHECK(j.at("clock_mode") == "countdown");
    seventhslide::freeform::from_json(j, read);
    CHECK(read == clock);
}

TEST_CASE("mapping an object carries every pixel size its parts hold", "[freeform][document][parts]")
{
    namespace cv = seventhslide::canvas;
    // A table: it holds every part that has a size, the border included.
    seventhslide::freeform::Object obj;
    obj.type = ObjectType::Table;
    obj.x = 100;
    obj.w = 200;
    obj.line.color = "#ffffff";
    obj.line.width = 4;
    obj.effects.shadow.on = true;
    obj.effects.shadow.blur = 8;
    obj.effects.shadow.distance = 6;
    obj.effects.glow.on = true;
    obj.effects.glow.size = 10;
    obj.effects.soft_edge = 2;
    obj.effects.text_outline.width = 3;
    obj.paragraph.indent = 20;
    obj.paragraph.margins.right = 12;
    obj.paragraph.column_gap = 30;
    obj.table.border_width = 2;
    nlohmann::json stored;
    seventhslide::freeform::to_json(stored, obj);
    // Twice the size each way: every length doubles.
    const auto mapping = seventhslide::geom::CanvasMapping::stretch({960, 540}, {1920, 1080});
    seventhslide::freeform::map_object(obj, mapping);
    CHECK(obj.x == 200);
    CHECK(obj.line.width == 8);
    CHECK(obj.effects.shadow.blur == 16);
    CHECK(obj.effects.shadow.distance == 12);
    CHECK(obj.effects.glow.size == 20);
    CHECK(obj.effects.soft_edge == 4);
    CHECK(obj.effects.text_outline.width == 6);
    CHECK(obj.paragraph.indent == 40);
    CHECK(obj.paragraph.margins.right == 24);
    CHECK(obj.paragraph.column_gap == 60);
    CHECK(obj.table.border_width == 4);
    // The stored form maps to the same object.
    seventhslide::freeform::map_object_json(stored, mapping);
    seventhslide::freeform::Object from_stored;
    seventhslide::freeform::from_json(stored, from_stored);
    CHECK(from_stored == obj);

    // A part that paints nothing keeps its sizes, as the stored form, which
    // leaves it out, cannot map them.
    seventhslide::freeform::Object quiet;
    quiet.type = ObjectType::Text;
    nlohmann::json quiet_stored;
    seventhslide::freeform::to_json(quiet_stored, quiet);
    seventhslide::freeform::map_object(quiet, mapping);
    seventhslide::freeform::map_object_json(quiet_stored, mapping);
    seventhslide::freeform::Object quiet_read;
    seventhslide::freeform::from_json(quiet_stored, quiet_read);
    CHECK(quiet_read == quiet);
    CHECK(quiet.line.width == 2.0);
    CHECK(quiet.effects.shadow.blur == 8);
}

TEST_CASE("cue verse type round-trips and insert inherits", "[freeform][document]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(doc.cues.size() == 1);
    REQUIRE(set_cue_verse(doc, 0, "verse", 2));
    REQUIRE(doc.cues[0].verse_type == "Verse");
    REQUIRE(doc.cues[0].verse_number == 2);
    const auto json = doc.to_json();
    REQUIRE(json.at("cues").at(0).at("verse_type") == "Verse");
    REQUIRE(json.at("cues").at(0).at("verse_number") == 2);
    const auto again = Document::from_json(json);
    REQUIRE(again.cues[0].verse_type == "Verse");
    REQUIRE(again.cues[0].verse_number == 2);

    REQUIRE(insert_cue(doc, 1, "next"));
    REQUIRE(doc.cues[1].verse_type == "Verse");
    REQUIRE(doc.cues[1].verse_number == 2);
    REQUIRE(set_cue_verse(doc, 1, "", 9));
    REQUIRE(doc.cues[1].verse_type.empty());
    REQUIRE_FALSE(doc.to_json().at("cues").at(1).contains("verse_type"));
}

namespace {

/// Verse 3 twice running, two lines a slide: c3 starts the repeat.
[[nodiscard]] Document repeated_verse()
{
    const auto cue = [](const char* id, const char* text, bool starts = false) {
        nlohmann::json j = {{"id", id}, {"text", text}, {"verse_type", "Verse"}, {"verse_number", 3}};
        if (starts) {
            j["section_start"] = true;
        }
        return j;
    };
    auto doc = Document::from_json({
        {"schema", "seventhslide.freeform"},
        {"schema_version", seventhslide::freeform::k_schema_version},
        {"name", "Repeat"},
        {"line_control", true},
        {"next_id", 100},
        {"cues", {cue("c1", "one"), cue("c2", "two"), cue("c3", "one", true), cue("c4", "two")}},
        {"decks",
         {{{"output", "out-main"},
           {"canvas", {{"width", 1920}, {"height", 1080}}},
           {"line_to_slide", {0, 0, 1, 1}},
           {"slides",
            {{{"id", "s1"}, {"objects", nlohmann::json::array()}},
             {{"id", "s2"}, {"objects", nlohmann::json::array()}}}}}}},
    });
    normalize(doc);
    return doc;
}

[[nodiscard]] std::vector<int> sections_of(const Document& doc)
{
    return seventhslide::freeform::cue_sections(doc.cues).cue_section;
}

/// A line-control document of one cue per ``labels`` entry (verse type and
/// number), cue ``i`` with id ``c<i+1>`` on slide ``line_to_slide[i]``.
[[nodiscard]] Document labelled(const std::vector<std::pair<std::string, int>>& labels,
                                const std::vector<int>& line_to_slide)
{
    nlohmann::json cues = nlohmann::json::array();
    for (std::size_t i = 0; i < labels.size(); ++i) {
        cues.push_back({{"id", "c" + std::to_string(i + 1)},
                        {"text", "line " + std::to_string(i + 1)},
                        {"verse_type", labels[i].first},
                        {"verse_number", labels[i].second}});
    }
    nlohmann::json slides = nlohmann::json::array();
    const int count = line_to_slide.empty() ? 0 : *std::ranges::max_element(line_to_slide) + 1;
    for (int i = 0; i < count; ++i) {
        slides.push_back({{"id", "s" + std::to_string(i + 1)}, {"objects", nlohmann::json::array()}});
    }
    auto doc = Document::from_json({
        {"schema", "seventhslide.freeform"},
        {"schema_version", seventhslide::freeform::k_schema_version},
        {"name", "Sections"},
        {"line_control", true},
        {"next_id", 100},
        {"cues", cues},
        {"decks",
         {{{"output", "out-main"},
           {"canvas", {{"width", 1920}, {"height", 1080}}},
           {"line_to_slide", line_to_slide},
           {"slides", slides}}}},
    });
    normalize(doc);
    return doc;
}

}  // namespace

TEST_CASE("A cue marked section_start parts a verse played twice running", "[freeform][document][sections]")
{
    const auto doc = repeated_verse();
    const auto sections = seventhslide::freeform::cue_sections(doc.cues);
    CHECK(sections.codes == std::vector<std::string>{"v3", "v3"});
    CHECK(sections.cue_section == std::vector<int>{0, 0, 1, 1});

    // Stored only where it is set.
    const auto json = doc.to_json();
    CHECK(json.at("cues").at(2).at("section_start") == true);
    CHECK_FALSE(json.at("cues").at(1).contains("section_start"));
    CHECK(Document::from_json(json).cues == doc.cues);

    // Without the mark the run is one section, as before.
    auto merged = doc;
    REQUIRE(seventhslide::freeform::set_cue_section_start(merged, 2, false));
    CHECK(sections_of(merged) == std::vector<int>{0, 0, 0, 0});
}

TEST_CASE("Only a cue with the label of the cue before can start a section", "[freeform][document][sections]")
{
    using seventhslide::freeform::can_start_section;
    using seventhslide::freeform::set_cue_section_start;
    auto doc = repeated_verse();
    CHECK_FALSE(can_start_section(doc.cues, 0));
    CHECK(can_start_section(doc.cues, 1));
    CHECK_FALSE(set_cue_section_start(doc, 0, true));
    REQUIRE(set_cue_section_start(doc, 1, true));
    CHECK(sections_of(doc) == std::vector<int>{0, 1, 2, 2});

    // A change of label parts the cues anyway: the mark goes.
    REQUIRE(set_cue_verse(doc, 2, "Verse", 4));
    CHECK_FALSE(doc.cues[2].section_start);
    CHECK_FALSE(set_cue_section_start(doc, 2, true));
    CHECK(sections_of(doc) == std::vector<int>{0, 1, 2, 3});

    // Unlabeled lines are never parted: the indicator shows none of them.
    REQUIRE(set_cue_verse(doc, 0, "", 1));
    REQUIRE(set_cue_verse(doc, 1, "", 1));
    CHECK_FALSE(doc.cues[1].section_start);
    CHECK_FALSE(can_start_section(doc.cues, 1));

    // A stored mark that parts nothing is dropped on load.
    auto json = repeated_verse().to_json();
    json["cues"][0]["section_start"] = true;
    json["cues"][1]["verse_number"] = 5;
    json["cues"][1]["section_start"] = true;
    auto loaded = Document::from_json(json);
    normalize(loaded);
    CHECK_FALSE(loaded.cues[0].section_start);
    CHECK_FALSE(loaded.cues[1].section_start);
    CHECK(loaded.cues[2].section_start == false);  // after Verse 5, not Verse 3
}

TEST_CASE("Deleting the first line of a section leaves the next starting it", "[freeform][document][sections]")
{
    auto doc = repeated_verse();
    REQUIRE(insert_cue(doc, 4, "three"));
    REQUIRE(delete_cue(doc, 2));
    CHECK(doc.cues[2].id == "c4");
    CHECK(doc.cues[2].section_start);
    CHECK(sections_of(doc) == std::vector<int>{0, 0, 1, 1});
}

TEST_CASE("Deleting the section between a verse and its repeat keeps them apart", "[freeform][document][sections]")
{
    // V3 C1 | V3: the chorus goes, and V3 V3 stay two.
    auto doc = labelled({{"Verse", 3}, {"Chorus", 1}, {"Verse", 3}}, {0, 0, 1});
    REQUIRE(sections_of(doc) == std::vector<int>{0, 1, 2});
    REQUIRE(delete_cue(doc, 1));
    CHECK(doc.cues[1].id == "c3");
    CHECK(doc.cues[1].section_start);
    CHECK(sections_of(doc) == std::vector<int>{0, 1});

    // V1 C1 | V2: no repeat either side, so no mark.
    auto other = labelled({{"Verse", 1}, {"Chorus", 1}, {"Verse", 2}}, {0, 0, 1});
    REQUIRE(delete_cue(other, 1));
    CHECK_FALSE(other.cues[1].section_start);
    CHECK(sections_of(other) == std::vector<int>{0, 1});
}

TEST_CASE("Reordering a verse's own slides keeps it one section", "[freeform][document][sections]")
{
    // V1 | V2 V2 over two slides: swapping V2's slides is still V1 V2.
    auto doc = labelled({{"Verse", 1}, {"Verse", 1}, {"Verse", 2}, {"Verse", 2}, {"Verse", 2}, {"Verse", 2}},
                        {0, 0, 1, 1, 2, 2});
    REQUIRE(reorder_slides(doc, 2, 1, "out-main"));
    REQUIRE(doc.cues[2].id == "c5");
    REQUIRE(doc.cues[4].id == "c3");
    CHECK(sections_of(doc) == std::vector<int>{0, 0, 1, 1, 1, 1});
    for (const auto& cue : doc.cues) {
        CHECK_FALSE(cue.section_start);
    }

    // The first verse's two slides swapped: still one V1.
    auto first = labelled({{"Verse", 1}, {"Verse", 1}, {"Chorus", 1}}, {0, 1, 2});
    REQUIRE(reorder_slides(first, 1, 0, "out-main"));
    REQUIRE(first.cues[0].id == "c2");
    CHECK(sections_of(first) == std::vector<int>{0, 0, 1});
    CHECK_FALSE(first.cues[1].section_start);
}

TEST_CASE("A verse reordered next to its repeat stays a section of its own", "[freeform][document][sections]")
{
    auto doc = repeated_verse();
    REQUIRE(reorder_slides(doc, 1, 0, "out-main"));
    REQUIRE(doc.cues[0].id == "c3");
    CHECK_FALSE(doc.cues[0].section_start);
    CHECK(doc.cues[2].id == "c1");
    CHECK(doc.cues[2].section_start);
    CHECK(sections_of(doc) == std::vector<int>{0, 0, 1, 1});
}

TEST_CASE("A new table of many rows fits on the canvas", "[freeform]")
{
    auto doc = make_document("x", {"out-main"});
    for (const int rows : {1, 8, 20}) {
        INFO(rows << " rows");
        const auto table = make_table_object(doc, rows, 3, {1920, 1080});
        CHECK(table.table.rows == rows);
        CHECK(table.h <= 1080 * 0.9 + 1e-6);
        CHECK(table.y >= 0);
        CHECK(table.y + table.h <= 1080);
    }
    // Rows still take their share up to there.
    CHECK(make_table_object(doc, 2, 3, {1920, 1080}).h
          < make_table_object(doc, 4, 3, {1920, 1080}).h);
}

TEST_CASE("New indicators and clocks start on their kind's type", "[freeform]")
{
    auto doc = make_document("x", {"out-main"});
    const auto indicator = make_indicator_object(doc, {1920, 1080});
    CHECK(indicator.font_size == 30);
    CHECK(indicator.text_align == "left");
    CHECK(indicator.vertical_align == "top");
    CHECK(indicator.font_color == "#ffffff");
    CHECK(indicator.h == Catch::Approx(30 * 1.4));
    const auto clock = make_clock_object(doc, {1920, 1080});
    CHECK(clock.font_size == 48);
    CHECK(clock.text_align == "left");
    CHECK(clock.vertical_align == "top");
    CHECK(clock.font_color == "#ffffff");
    CHECK(clock.h == Catch::Approx(48 * 1.4));

    // Both span the canvas from x = 10, one line high: the indicator along
    // the bottom, the clock along the top.
    CHECK(indicator.type == ObjectType::Indicator);
    CHECK(clock.type == ObjectType::Clock);
    CHECK(indicator.id != clock.id);
    for (const auto* o : {&indicator, &clock}) {
        CHECK(o->x == Catch::Approx(10));
        CHECK(o->w == Catch::Approx(1910));
    }
    CHECK(indicator.y == Catch::Approx(1000));
    CHECK(clock.y == Catch::Approx(10));
    CHECK(indicator.indicator_fallback == IndicatorFallback::Slide);
    CHECK_FALSE(clock.clock_24h);
    CHECK_FALSE(clock.clock_seconds);
}

TEST_CASE("Text indicator and clock objects store their typeface and only their own flags",
          "[freeform][document]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(add_object(doc, "out-main", 0, make_text_object(doc, {1920, 1080})));
    REQUIRE(add_object(doc, "out-main", 0, make_indicator_object(doc, {1920, 1080})));
    REQUIRE(add_object(doc, "out-main", 0, make_clock_object(doc, {1920, 1080})));
    const auto json = doc.to_json();
    const auto& objects = json.at("decks").at(0).at("slides").at(0).at("objects");
    REQUIRE(objects.size() == 3);
    for (const auto& o : objects) {
        INFO(o.at("type").get<std::string>());
        for (const char* key : {"font_family", "font_size", "font_color", "text_align",
                                "vertical_align"}) {
            CHECK(o.contains(key));
        }
    }
    const auto& text = objects.at(0);
    const auto& indicator = objects.at(1);
    const auto& clock = objects.at(2);
    for (const char* key : {"bold", "italic", "underline", "line_height", "line_gap", "letter_spacing"}) {
        CHECK(text.contains(key));
    }
    // An indicator has no weight or slant of its own; a clock paints both.
    CHECK_FALSE(indicator.contains("bold"));
    CHECK_FALSE(indicator.contains("italic"));
    CHECK(clock.contains("bold"));
    CHECK(clock.contains("italic"));
    CHECK_FALSE(clock.contains("underline"));
    CHECK(indicator.contains("indicator_fallback"));
    CHECK_FALSE(clock.contains("indicator_fallback"));
}

TEST_CASE("indicator object json round-trips without baked text", "[freeform][document]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = make_indicator_object(doc, {1920, 1080});
    obj.font_size = 36;
    obj.indicator_fallback = IndicatorFallback::Blank;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    const auto json = doc.to_json();
    const auto& jobj = json.at("decks").at(0).at("slides").at(0).at("objects").at(0);
    REQUIRE(jobj.at("type") == "indicator");
    REQUIRE(jobj.at("indicator_fallback") == "blank");
    REQUIRE_FALSE(jobj.contains("text"));
    const auto again = Document::from_json(json);
    REQUIRE(again.decks[0].slides[0].objects[0].type == ObjectType::Indicator);
    REQUIRE(again.decks[0].slides[0].objects[0].font_size == 36);
    REQUIRE(again.decks[0].slides[0].objects[0].indicator_fallback == IndicatorFallback::Blank);
    REQUIRE(again.decks[0].slides[0].objects[0].text.empty());
}

TEST_CASE("indicator gap is stored only when set and read back in range", "[freeform][document]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(add_object(doc, "out-main", 0, make_indicator_object(doc, {1920, 1080})));
    auto json = doc.to_json();
    const auto stored_object = [](nlohmann::json& j, std::size_t i) -> nlohmann::json& {
        return j.at("decks").at(0).at("slides").at(0).at("objects").at(i);
    };
    const auto read_gap = [](const nlohmann::json& j) {
        return Document::from_json(j).decks[0].slides[0].objects[0].indicator_gap;
    };
    // The default is how every indicator already looks; nothing to write.
    CHECK_FALSE(stored_object(json, 0).contains("indicator_gap"));
    CHECK(read_gap(json) == 1.0);

    doc.decks[0].slides[0].objects[0].indicator_gap = 0.6;
    json = doc.to_json();
    CHECK(stored_object(json, 0).at("indicator_gap") == 0.6);
    CHECK(read_gap(json) == 0.6);

    stored_object(json, 0)["indicator_gap"] = 50;
    CHECK(read_gap(json) == 4.0);
    stored_object(json, 0)["indicator_gap"] = -1;
    CHECK(read_gap(json) == 0.1);
    stored_object(json, 0)["indicator_gap"] = "wide";
    CHECK(read_gap(json) == 1.0);

    // Only an indicator carries one.
    auto text = make_text_object(doc, {1920, 1080});
    text.indicator_gap = 2.0;
    REQUIRE(add_object(doc, "out-main", 0, std::move(text)));
    json = doc.to_json();
    CHECK_FALSE(stored_object(json, 1).contains("indicator_gap"));
}

TEST_CASE("clock object json round-trips without baked time", "[freeform][document]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = make_clock_object(doc, {1920, 1080});
    obj.font_size = 36;
    obj.clock_24h = true;
    obj.clock_seconds = true;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    const auto json = doc.to_json();
    const auto& jobj = json.at("decks").at(0).at("slides").at(0).at("objects").at(0);
    REQUIRE(jobj.at("type") == "clock");
    REQUIRE(jobj.at("clock_24h") == true);
    REQUIRE(jobj.at("clock_seconds") == true);
    REQUIRE_FALSE(jobj.contains("text"));
    const auto again = Document::from_json(json);
    REQUIRE(again.decks[0].slides[0].objects[0].type == ObjectType::Clock);
    REQUIRE(again.decks[0].slides[0].objects[0].font_size == 36);
    REQUIRE(again.decks[0].slides[0].objects[0].clock_24h);
    REQUIRE(again.decks[0].slides[0].objects[0].clock_seconds);
    REQUIRE(again.decks[0].slides[0].objects[0].text.empty());
}

TEST_CASE("shape object json round-trips kind, border and adjustments", "[freeform][document]")
{
    using seventhslide::geom::ShapeKind;

    auto doc = make_document("x", {"out-main"});
    auto obj = make_shape_object(doc, ShapeKind::Star, {1920, 1080});
    obj.fill.color = "#ff0000";
    obj.line.color = "#0000ff";
    obj.line.width = 7.5;
    obj.shape_adjust.points = 9;
    obj.shape_adjust.inner = 0.3;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));

    const auto json = doc.to_json();
    const auto& jobj = json.at("decks").at(0).at("slides").at(0).at("objects").at(0);
    REQUIRE(jobj.at("type") == "shape");
    REQUIRE(jobj.at("shape_kind") == "star");
    REQUIRE(jobj.at("stroke_width") == 7.5);
    REQUIRE(jobj.at("shape_points") == 9);
    // Knobs this kind ignores are not written.
    REQUIRE_FALSE(jobj.contains("shape_head"));
    REQUIRE_FALSE(jobj.contains("shape_corner"));

    const auto loaded = Document::from_json(json);
    const auto& back = loaded.decks[0].slides[0].objects[0];
    REQUIRE(back.type == ObjectType::Shape);
    REQUIRE(back.shape_kind == ShapeKind::Star);
    REQUIRE(back.fill.color == "#ff0000");
    REQUIRE(back.line.color == "#0000ff");
    REQUIRE(back.line.width == 7.5);
    REQUIRE(back.shape_adjust.points == 9);
    REQUIRE(back.shape_adjust.inner == 0.3);
}

TEST_CASE("shapes saved before the kind existed load as rectangles", "[freeform][document]")
{
    using seventhslide::geom::ShapeKind;

    auto doc = make_document("x", {"out-main"});
    auto obj = make_shape_object(doc, ShapeKind::Rectangle, {1920, 1080});
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    auto json = doc.to_json();
    auto& jobj = json.at("decks").at(0).at("slides").at(0).at("objects").at(0);
    jobj.erase("shape_kind");
    jobj.erase("stroke_width");

    const auto loaded = Document::from_json(json);
    const auto& back = loaded.decks[0].slides[0].objects[0];
    REQUIRE(back.shape_kind == ShapeKind::Rectangle);
    // The pre-existing 2px hard-coded border becomes the documented default.
    REQUIRE(back.line.width == 2.0);
}

namespace {

/// One text object carrying ``text`` on the first deck's first slide.
Document doc_with_text(std::string text)
{
    auto doc = make_document("x", {"out-main", "LT"});
    doc.decks[0].slides[0].objects.push_back({});
    doc.decks[0].slides[0].objects.back().type = ObjectType::Text;
    doc.decks[0].slides[0].objects.back().text = std::move(text);
    return doc;
}

} // namespace

TEST_CASE("substitute_fields keeps a leading blank line", "[freeform][tokens]")
{
    const std::map<std::string, std::string> values{{"title", "Sunday"}};
    REQUIRE(substitute_fields("\nHello", values) == "\nHello");
    REQUIRE(substitute_fields("\n\n{title}", values) == "\n\nSunday");
    REQUIRE(substitute_fields("a\n\nb", values) == "a\n\nb");
    REQUIRE(substitute_fields("", values).empty());
}

TEST_CASE("substitute_fields leaves illegal braces literal", "[freeform][tokens]")
{
    const std::map<std::string, std::string> values{{"title", "Sunday"}};
    REQUIRE(substitute_fields("{Title} {title} { title }", values) == "{Title} Sunday { title }");
    // A ``{-token}`` with a declared but blank value takes its line away.
    const std::map<std::string, std::string> blank{{"note", ""}};
    REQUIRE(substitute_fields("keep\n{-note}\ntail", blank) == "keep\ntail");
}

TEST_CASE("count_field_usage counts every occurrence", "[freeform][tokens]")
{
    auto doc = doc_with_text("{name} and {name}\n{-name}\n{other}");
    REQUIRE(count_field_usage(doc, "name") == 3);
    REQUIRE(count_field_usage(doc, "other") == 1);
    REQUIRE(count_field_usage(doc, "missing") == 0);
    REQUIRE(count_field_usage(doc, "Name") == 0);

    // Cue text counts only while line control owns the lines.
    doc.cues.push_back({});
    doc.cues.back().text = "{name}";
    REQUIRE(count_field_usage(doc, "name") == 3);
    doc.line_control = true;
    REQUIRE(count_field_usage(doc, "name") == 4);
}

TEST_CASE("collect_document_tokens reports the hide form", "[freeform][tokens]")
{
    const auto doc = doc_with_text("{title}\n{-subtitle}\n{-title}");
    const auto hits = collect_document_tokens(doc);
    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].name == "title");
    // Any occurrence spelling ``{-title}`` marks the field.
    REQUIRE(hits[0].hide_line_if_empty);
    REQUIRE(hits[1].name == "subtitle");
    REQUIRE(hits[1].hide_line_if_empty);
}

TEST_CASE("rewrite_field_token renames or strips every occurrence", "[freeform][tokens]")
{
    auto doc = doc_with_text("Hi {name}, {name} and {-name}. Not {Name} or {other}.");
    REQUIRE(rewrite_field_token(doc, "name", "guest") == 3);
    REQUIRE(doc.decks[0].slides[0].objects[0].text
            == "Hi {guest}, {guest} and {-guest}. Not {Name} or {other}.");

    // Stripping removes the token text only - the surrounding punctuation and
    // spacing is the operator's, so it stays exactly as they typed it.
    REQUIRE(rewrite_field_token(doc, "guest", {}) == 3);
    REQUIRE(doc.decks[0].slides[0].objects[0].text == "Hi ,  and . Not {Name} or {other}.");

    // Illegal names on either side are a no-op, not a partial rewrite.
    const auto before = doc;
    REQUIRE(rewrite_field_token(doc, "other", "Bad") == 0);
    REQUIRE(rewrite_field_token(doc, "Other", "fine") == 0);
    REQUIRE(doc.decks[0].slides[0].objects[0].text
            == before.decks[0].slides[0].objects[0].text);
}

TEST_CASE("prune_invalid_fields keeps unused but legal names", "[freeform][tokens]")
{
    auto doc = doc_with_text("Hello {title}");
    doc.fields["title"] = "Hi";
    doc.fields["speaker"] = "Jane";
    doc.fields["Title"] = "nope";
    prune_invalid_fields(doc);
    REQUIRE(doc.fields.size() == 2);
    REQUIRE(doc.fields.at("title") == "Hi");
    REQUIRE(doc.fields.at("speaker") == "Jane");
    REQUIRE(doc.fields.count("Title") == 0);
}

TEST_CASE("simple insert/delete/reorder is global", "[freeform][edit]")
{
    auto doc = make_document("Welcome", {"out-main", "out-lt"});
    REQUIRE(insert_slide(doc, 1));
    REQUIRE(doc.decks[0].slide_count() == 2);
    REQUIRE(doc.decks[1].slide_count() == 2);
    REQUIRE(doc.decks[0].slides[1].objects.empty());
    REQUIRE(validate(doc).empty());

    REQUIRE(insert_slide(doc, 2));
    REQUIRE(reorder_slides(doc, 2, 0));
    REQUIRE(doc.decks[0].slide_count() == 3);
    REQUIRE(doc.decks[1].slide_count() == 3);

    REQUIRE(delete_slide(doc, 0));
    REQUIRE(doc.decks[0].slide_count() == 2);
    REQUIRE(doc.decks[1].slide_count() == 2);

    REQUIRE_FALSE(insert_slide(doc, 1, "out-main"));
}

TEST_CASE("normalize counts ids below the top of an int and skips the rest", "[freeform][edit]")
{
    auto doc = make_document("Welcome", {"out-main"});
    doc.next_id = 10;
    auto& objects = doc.decks[0].slides[0].objects;
    objects.emplace_back().id = "o99999999999999999999";
    objects.emplace_back().id = "o2147483647";
    normalize(doc);
    REQUIRE(doc.next_id == 10);

    objects.emplace_back().id = "o2147483646";
    normalize(doc);
    REQUIRE(doc.next_id == 2147483647);

    // No number is left, so the next ids are no numbers and all differ.
    const auto first = alloc_id(doc, 'o');
    const auto second = alloc_id(doc, 'o');
    CHECK(first != "o2147483647");
    CHECK(first.starts_with("ox"));
    CHECK(second.starts_with("ox"));
    CHECK(first != second);
    CHECK(doc.next_id == 2147483647);
    objects.emplace_back().id = first;
    normalize(doc);
    CHECK(doc.next_id == 2147483647);
}

TEST_CASE("alloc_id counts up to the last number an int holds", "[freeform][edit]")
{
    auto doc = make_document("Welcome", {"out-main"});
    doc.next_id = 2147483646;
    CHECK(alloc_id(doc, 'o') == "o2147483646");
    CHECK(doc.next_id == 2147483647);
    CHECK(alloc_id(doc, 'o').starts_with("ox"));
    CHECK(doc.next_id == 2147483647);
}

TEST_CASE("slide background json omits inherit and round-trips fills", "[freeform][document][bg]")
{
    auto doc = make_document("Welcome", {"out-main"});
    REQUIRE_FALSE(doc.to_json()["decks"][0]["slides"][0].contains("background"));

    auto& slide = doc.decks[0].slides[0];
    slide.background.kind = SlideBackgroundKind::Color;
    slide.background.color = "#112233";
    const auto color_json = doc.to_json()["decks"][0]["slides"][0]["background"];
    REQUIRE(color_json.at("kind") == "color");
    REQUIRE(color_json.at("color") == "#112233");
    REQUIRE_FALSE(color_json.contains("image"));

    const auto again = Document::from_json(doc.to_json());
    REQUIRE(again.decks[0].slides[0].background.kind == SlideBackgroundKind::Color);
    REQUIRE(again.decks[0].slides[0].background.color == "#112233");

    slide.background = {};
    slide.background.kind = SlideBackgroundKind::Video;
    slide.background.video = "dusk.mp4";
    REQUIRE(duplicate_slide(doc, 0));
    REQUIRE(doc.decks[0].slides[1].background.kind == SlideBackgroundKind::Video);
    REQUIRE(doc.decks[0].slides[1].background.video == "dusk.mp4");
    REQUIRE(doc.decks[0].slides[1].id != doc.decks[0].slides[0].id);
}

TEST_CASE("line control insert is per deck", "[freeform][edit]")
{
    auto doc = load_doc("line_control_fortress.json");
    REQUIRE(doc.line_control);
    REQUIRE(validate(doc).empty());
    REQUIRE(insert_slide(doc, 2, "out-main"));
    REQUIRE(doc.decks[0].slide_count() == 3);
    REQUIRE(doc.decks[1].slide_count() == 4);
    REQUIRE(validate(doc).empty());
    REQUIRE_FALSE(can_disable_line_control(doc));
}

TEST_CASE("line-control reorder moves a slide with its lines", "[freeform][edit]")
{
    auto doc = load_doc("line_control_fortress.json");
    const std::string first = doc.cues.front().text;
    const std::string later = doc.cues[4].text;
    REQUIRE(reorder_slides(doc, 0, 1, "out-main"));
    REQUIRE(validate(doc).empty());
    REQUIRE(doc.decks[0].slides[0].id == "s2");
    REQUIRE(doc.decks[0].slides[1].id == "s1");
    REQUIRE(doc.cues.front().text == later);
    REQUIRE(doc.cues[4].text == first);
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 0, 0, 1, 1, 1, 1}));
    REQUIRE(doc.decks[1].line_to_slide == std::vector<int>({0, 0, 1, 1, 2, 2, 3, 3}));
    const auto main = deck_list_rows(doc, doc.decks[0]);
    REQUIRE(main.front().kind == ListRowKind::Cue);
    REQUIRE(main.back().kind == ListRowKind::Slide);
    for (int s = 0; s < doc.decks[0].slide_count(); ++s) {
        int cues = 0;
        for (int v : doc.decks[0].line_to_slide) {
            if (v == s) {
                ++cues;
            }
        }
        REQUIRE(cues >= 1);
    }
    REQUIRE(reorder_slides(doc, 0, 0, "out-main"));
}

TEST_CASE("line-control reorder refuses a slide with no lines", "[freeform][edit]")
{
    auto doc = load_doc("line_control_fortress.json");
    REQUIRE(insert_slide(doc, 2, "out-main"));
    REQUIRE_FALSE(reorder_slides(doc, 2, 0, "out-main"));
    REQUIRE(validate(doc).empty());
}

TEST_CASE("move_span_end grows and shrinks a slide's cue span", "[freeform][edit]")
{
    auto doc = load_doc("line_control_fortress.json");
    REQUIRE(move_span_end(doc, "out-main", 0, 5));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 0, 0, 0, 0, 1, 1}));
    REQUIRE(move_span_end(doc, "out-main", 0, 1));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 1, 1, 1, 1, 1, 1}));
    REQUIRE(validate(doc).empty());
}

TEST_CASE("place_slide_thumb remaps cues like the cue-map handle", "[freeform][edit]")
{
    auto doc = load_doc("line_control_fortress.json");
    REQUIRE(place_slide_thumb(doc, "out-main", 0, 5));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 0, 0, 0, 0, 1, 1}));
    REQUIRE(validate(doc).empty());

    doc = load_doc("line_control_fortress.json");
    REQUIRE(place_slide_thumb(doc, "out-main", 1, 2));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 1, 1, 1, 1, 1, 1}));
    REQUIRE(validate(doc).empty());

    doc = load_doc("line_control_fortress.json");
    REQUIRE_FALSE(place_slide_thumb(doc, "out-main", 0, 7));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 0, 0, 1, 1, 1, 1}));
}

TEST_CASE("place_slide_thumb packs earlier slides when a later thumb moves up", "[freeform][edit]")
{
    // Main's second slide split before cue 6: three slides over the eight cues.
    const auto three_slides = [] {
        auto doc = load_doc("line_control_fortress.json");
        auto& main = doc.decks[0];
        main.slides.emplace_back();
        main.line_to_slide = {0, 0, 0, 0, 1, 1, 2, 2};
        normalize(doc);
        REQUIRE(validate(doc).empty());
        return doc;
    };
    auto doc = three_slides();
    REQUIRE(doc.decks[0].slide_count() == 3);
    REQUIRE(place_slide_thumb(doc, "out-main", 1, 6));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 0, 0, 1, 1, 1, 2}));

    doc = three_slides();
    REQUIRE(place_slide_thumb(doc, "out-main", 1, 2));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 1, 2, 2, 2, 2, 2}));
    REQUIRE(validate(doc).empty());
}

TEST_CASE("insert_cue inherits the previous cue's slide", "[freeform][edit]")
{
    auto doc = load_doc("line_control_fortress.json");
    REQUIRE(insert_cue(doc, 4, "extra"));
    REQUIRE(doc.cues[4].text == "extra");
    REQUIRE(doc.decks[0].line_to_slide[4] == 0); // previous cue is still slide 0
    REQUIRE(doc.decks[1].line_to_slide[4] == 1);
    REQUIRE(validate(doc).empty());
    REQUIRE(delete_cue(doc, 4));
    REQUIRE(doc.cues.size() == 8);
}

TEST_CASE("insert_cue fills out a deck's short cue map first", "[freeform][edit]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(insert_slide(doc, 1));
    REQUIRE(set_line_control(doc, true));
    REQUIRE(insert_cue(doc, 2, "third"));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 1, 1}));

    // A map shorter than the cues: those past its end are on the last slide.
    doc.decks[0].line_to_slide = {0};
    REQUIRE(insert_cue(doc, 3, "fourth"));
    CHECK(doc.decks[0].line_to_slide == std::vector<int>({0, 1, 1, 1}));
    CHECK(validate(doc).empty());

    doc.decks[0].line_to_slide.clear();
    REQUIRE(insert_cue(doc, 1, "between"));
    CHECK(doc.decks[0].line_to_slide == std::vector<int>({1, 1, 1, 1, 1}));
}

TEST_CASE("delete_cue refuses to orphan a slide", "[freeform][edit]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(insert_slide(doc, 1));
    REQUIRE(set_line_control(doc, true));
    REQUIRE(doc.cues.size() == 2);
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 1}));
    REQUIRE_FALSE(can_delete_cue(doc, 0));
    REQUIRE_FALSE(can_delete_cue(doc, 1));
    REQUIRE_FALSE(delete_cue(doc, 0));
    REQUIRE(doc.cues.size() == 2);

    REQUIRE(insert_cue(doc, 1, "extra"));
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 0, 1}));
    REQUIRE(can_delete_cue(doc, 0));
    REQUIRE(can_delete_cue(doc, 1));
    REQUIRE_FALSE(can_delete_cue(doc, 2));
    REQUIRE(delete_cue(doc, 1));
    REQUIRE(doc.cues.size() == 2);
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 1}));
    REQUIRE_FALSE(can_delete_cue(doc, 0));
}

TEST_CASE("delete_cue refuses if any output would lose a slide's last line", "[freeform][edit]")
{
    auto doc = load_doc("line_control_fortress.json");
    REQUIRE(can_delete_cue(doc, 2));
    REQUIRE(delete_cue(doc, 2));
    REQUIRE_FALSE(can_delete_cue(doc, 2));
    REQUIRE_FALSE(delete_cue(doc, 2));
}

TEST_CASE("delete_cue refuses the last remaining cue", "[freeform][edit]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(doc.cues.size() == 1);
    REQUIRE_FALSE(can_delete_cue(doc, 0));
    REQUIRE_FALSE(delete_cue(doc, 0));
    REQUIRE(doc.cues.size() == 1);
}

TEST_CASE("disabling line control requires flatten when counts differ", "[freeform][edit]")
{
    auto doc = load_doc("line_control_fortress.json");
    REQUIRE_FALSE(set_line_control(doc, false));
    REQUIRE(doc.line_control);
    REQUIRE(set_line_control(doc, false, true, "out-main"));
    REQUIRE_FALSE(doc.line_control);
    REQUIRE(doc.decks[0].slide_count() == 2);
    REQUIRE(doc.decks[1].slide_count() == 2);
    REQUIRE(doc.cues.empty());
    REQUIRE(validate(doc).empty());
}

TEST_CASE("enabling line control synthesizes identity cues", "[freeform][edit]")
{
    auto doc = make_document("x", {"out-main", "LT"});
    REQUIRE(insert_slide(doc, 1));
    REQUIRE(set_line_control(doc, true));
    REQUIRE(doc.line_control);
    REQUIRE(doc.cues.size() == 2);
    REQUIRE(doc.decks[0].line_to_slide == std::vector<int>({0, 1}));
    REQUIRE(doc.decks[1].line_to_slide == std::vector<int>({0, 1}));
}

TEST_CASE("left list is cues then the slide they belong to", "[freeform][edit]")
{
    const auto doc = load_doc("line_control_fortress.json");
    const auto main = deck_list_rows(doc, doc.decks[0]);
    REQUIRE(main.size() == 10); // 8 cues + 2 thumbs
    REQUIRE(main[0].kind == ListRowKind::Cue);
    REQUIRE(main[0].cue_index == 0);
    REQUIRE(main[3].kind == ListRowKind::Cue);
    REQUIRE(main[4].kind == ListRowKind::Slide);
    REQUIRE(main[4].slide_index == 0);
    REQUIRE(main[5].kind == ListRowKind::Cue);
    REQUIRE(main[5].cue_index == 4);
    REQUIRE(main.back().kind == ListRowKind::Slide);
    REQUIRE(main.back().slide_index == 1);

    const auto lt = deck_list_rows(doc, doc.decks[1]);
    REQUIRE(lt.size() == 12); // 8 cues + 4 thumbs
    REQUIRE(lt[0].kind == ListRowKind::Cue);
    REQUIRE(lt[2].kind == ListRowKind::Slide);
    REQUIRE(lt[2].slide_index == 0);
    REQUIRE(lt[3].kind == ListRowKind::Cue);
    REQUIRE(lt[3].cue_index == 2);
    REQUIRE(lt[5].kind == ListRowKind::Slide);
    REQUIRE(lt[5].slide_index == 1);
}

TEST_CASE("sanitize_field_name lowercases and strips illegal chars", "[freeform][tokens]")
{
    REQUIRE(sanitize_field_name("Title") == "title");
    REQUIRE(sanitize_field_name(" Song Title ") == "song_title");
    REQUIRE(sanitize_field_name("CCLI Song Number") == "ccli_song_number");
    REQUIRE(sanitize_field_name("1abc") == "abc");
    REQUIRE(sanitize_field_name("!!!") == "");
    REQUIRE_FALSE(document_is_native(nlohmann::json::object()));
    REQUIRE_FALSE(document_is_native(nlohmann::json{{"decks", nlohmann::json::array()}}));
}

TEST_CASE("load_document refuses a document at another schema version", "[freeform][document]")
{
    // The database rule, for documents: this build reads the versions it
    // knows and refuses any other, rather than misreading it.
    const auto current = make_document("Welcome", {"out-main"}).to_json();
    REQUIRE(current.value("schema_version", 0) == seventhslide::freeform::k_schema_version);
    REQUIRE(document_refusal(current).empty());

    auto newer = current;
    newer["schema_version"] = seventhslide::freeform::k_schema_version + 1;
    CHECK_FALSE(document_is_native(newer));
    CHECK_FALSE(load_document(newer, "Welcome"));
    CHECK(document_refusal(newer).find("different version") != std::string::npos);

    auto older = current;
    older["schema_version"] = seventhslide::freeform::k_schema_version - 1;
    CHECK_FALSE(load_document(older, "Welcome"));

    auto unversioned = current;
    unversioned.erase("schema_version");
    CHECK_FALSE(load_document(unversioned, "Welcome"));
    CHECK(document_refusal(unversioned).find("different version") != std::string::npos);

    // An item with no document yet is not a refusal.
    CHECK(document_refusal(nlohmann::json()).empty());
    CHECK(document_refusal(nlohmann::json::object()).empty());
    CHECK(document_refusal(nlohmann::json{{"decks", nlohmann::json::array()}})
              .find("not a Freeform document") != std::string::npos);
}

TEST_CASE("load_document parses native blobs and refuses foreign ones", "[freeform][document]")
{
    auto stored = make_document("", {"out-main", "out-lt"});
    const auto doc = load_document(stored.to_json(), "Welcome");
    REQUIRE(doc);
    REQUIRE(doc->name == "Welcome");
    REQUIRE(doc->decks.size() == 2);
    REQUIRE(document_is_native(doc->to_json()));
    REQUIRE(validate(*doc).empty());

    for (const auto& empty : {nlohmann::json::object(), nlohmann::json()}) {
        const auto blank = load_document(empty, "Empty");
        REQUIRE(blank);
        REQUIRE(blank->name == "Empty");
        // No document yet: no decks, rather than a deck for an output that
        // may not exist. The editor gives it one when it is opened.
        REQUIRE(blank->decks.empty());
        REQUIRE(validate(*blank).empty());
    }

    // Stored data that is not a Freeform document is refused, never read as
    // empty (the next save would overwrite it).
    for (const auto& foreign : {nlohmann::json{{"slides", {1, 2}}}, nlohmann::json::array(),
                                nlohmann::json("text"),
                                nlohmann::json{{"schema", "other"}, {"decks", nlohmann::json::array()}}}) {
        REQUIRE_FALSE(load_document(foreign, "Foreign"));
    }
}

TEST_CASE("document transition json round-trips", "[freeform][document][transition]")
{
    using seventhslide::model::TransitionType;
    auto doc = make_document("x", {"out-main"});
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());
    seventhslide::freeform::Object obj;
    obj.id = "o1";
    obj.transition = seventhslide::model::Transition{TransitionType::Fade, 180};
    doc.decks[0].slides[0].objects.push_back(std::move(obj));

    const auto again = Document::from_json(doc.to_json());
    REQUIRE(again.decks[0].slides[0].objects[0].transition.plays());
    REQUIRE(again.decks[0].slides[0].objects[0].transition.duration_ms == 180);
}

TEST_CASE("An object's cut is not written, and an old explicit one reads the same",
          "[freeform][document]")
{
    using seventhslide::freeform::Object;
    using seventhslide::model::Transition;
    Object cut;
    cut.id = "o1";
    REQUIRE_FALSE(nlohmann::json(cut).contains("transition"));

    // Files from before kept a chosen cut explicitly.
    auto old = nlohmann::json(cut);
    old["transition"] = {{"type", "cut"}, {"duration_ms", 500}};
    CHECK(old.get<Object>().transition == Transition{});

    // A cut that remembers another length keeps it for when it dissolves again.
    Object shorter = cut;
    shorter.transition.duration_ms = 300;
    CHECK(nlohmann::json(shorter).get<Object>().transition.duration_ms == 300);
}

TEST_CASE("cue-linked flags round-trip", "[freeform][document]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    seventhslide::freeform::Object obj;
    obj.id = "o1";
    obj.type = ObjectType::Text;
    obj.text = "stale";
    obj.cue_linked = true;
    obj.show_upcoming = true;
    obj.upcoming_color = "#aabbcc";
    obj.upcoming_font_size = 32;
    doc.decks[0].slides[0].objects.push_back(obj);
    const auto again = Document::from_json(doc.to_json());
    REQUIRE(again.decks[0].slides[0].objects[0].cue_linked);
    REQUIRE(again.decks[0].slides[0].objects[0].show_upcoming);
    REQUIRE(again.decks[0].slides[0].objects[0].upcoming_color == "#aabbcc");
    REQUIRE(again.decks[0].slides[0].objects[0].upcoming_font_size == 32);
    const auto compact = doc.to_json().at("decks").at(0).at("slides").at(0).at("objects").at(0);
    REQUIRE(compact.at("cue_linked") == true);
    REQUIRE(compact.at("text") == "stale");
}

TEST_CASE("cue overlays round-trip", "[freeform][document][link]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    seventhslide::freeform::Object obj;
    obj.id = "o1";
    obj.type = ObjectType::Text;
    obj.cue_linked = true;
    obj.cue_overlays["c1"] = "<color=#ff0000>Hello</color>";
    doc.decks[0].slides[0].objects.push_back(obj);
    const auto again = Document::from_json(doc.to_json());
    REQUIRE(again.decks[0].slides[0].objects[0].cue_overlays.at("c1")
            == "<color=#ff0000>Hello</color>");
    const auto compact = doc.to_json().at("decks").at(0).at("slides").at(0).at("objects").at(0);
    REQUIRE(compact.at("cue_overlays").at("c1") == "<color=#ff0000>Hello</color>");
}

// A multi-deck edit in simple mode stopped at the first deck that
// refused it, leaving the decks before it changed.
TEST_CASE("A simple-mode edit changes every deck or none", "[freeform][edit]")
{
    auto doc = seventhslide::freeform::make_document("x", {"out-main", "out-lt"});
    REQUIRE_FALSE(doc.line_control);
    REQUIRE(seventhslide::freeform::insert_slide(doc, 1, ""));
    // Knock the decks out of step so the second one refuses the delete.
    doc.decks[1].slides.pop_back();
    const auto main_before = doc.decks[0].slides.size();

    REQUIRE_FALSE(seventhslide::freeform::delete_slide(doc, 1, ""));
    REQUIRE(doc.decks[0].slides.size() == main_before);
}


TEST_CASE("a cue's Bible verse survives JSON, and one out of range is dropped", "[freeform][document][bible]")
{
    seventhslide::freeform::Cue cue;
    cue.id = "c1";
    cue.text = "For God so loved";
    cue.chapter = 119;
    cue.verse = 176;
    cue.show_ref = true;
    const nlohmann::json j = cue;
    REQUIRE(j.get<seventhslide::freeform::Cue>() == cue);

    auto wide = j;
    wide["chapter"] = 151;
    const auto dropped = wide.get<seventhslide::freeform::Cue>();
    REQUIRE(dropped.chapter == 0);
    REQUIRE_FALSE(dropped.show_ref);
    REQUIRE(seventhslide::freeform::cue_label_code(dropped).empty());

    // A song cue writes no verse.
    seventhslide::freeform::Cue song;
    song.verse_type = "Chorus";
    REQUIRE_FALSE(nlohmann::json(song).contains("chapter"));
    REQUIRE(seventhslide::freeform::cue_label_code(song) == "c1");
    REQUIRE(seventhslide::freeform::cue_label_code(cue) == "119:176");
}
