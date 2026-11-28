#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"

#include "freeform_test_helpers.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/chrome_text.hpp>
#include <seventhslide/scene/scene_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using seventhslide::freeform::Document;
using seventhslide::freeform::IndicatorFallback;
using seventhslide::freeform::Object;
using seventhslide::freeform::ObjectType;
using seventhslide::test::add_object;
using seventhslide::freeform::compile;
using seventhslide::freeform::insert_cue;
using seventhslide::freeform::insert_slide;
using seventhslide::freeform::make_document;
using seventhslide::freeform::make_clock_object;
using seventhslide::freeform::make_indicator_object;
using seventhslide::freeform::make_text_object;
using seventhslide::freeform::move_span_end;
using seventhslide::freeform::normalize;
using seventhslide::freeform::rename_cue;
using seventhslide::freeform::set_cue_verse;
using seventhslide::freeform::set_line_control;
using seventhslide::freeform::slide_preview_key;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::model::OutputConfig;
using seventhslide::model::ThemeEntry;
using seventhslide::build::BuildKind;
using seventhslide::build::SlideBuildInput;
using seventhslide::build::build_slides;
using seventhslide::model::ContentKind;

namespace {

std::filesystem::path fixtures_dir()
{
    return seventhslide::test::fixtures_dir("freeform");
}

Document load_doc(const char* file)
{
    std::ifstream in(fixtures_dir() / file);
    REQUIRE(in);
    nlohmann::json j;
    in >> j;
    auto doc = Document::from_json(j);
    normalize(doc);
    return doc;
}

Object text_object(std::string text)
{
    Object o;
    o.type = ObjectType::Text;
    o.text = std::move(text);
    return o;
}

} // namespace

TEST_CASE("simple compile uses identity maps and per-output art", "[freeform][compile]")
{
    const auto built = compile(load_doc("simple_two_outputs.json"));
    REQUIRE_FALSE(built.line_control);
    REQUIRE(built.lines == std::vector<std::string>{"1", "2", "3"});
    REQUIRE(built.decks.size() == 2);
    REQUIRE(built.decks[0].line_to_slide == std::vector<int>({0, 1, 2}));
    REQUIRE(built.decks[1].line_to_slide == std::vector<int>({0, 1, 2}));
    REQUIRE(built.decks[0].slides.size() == 3);
    REQUIRE(built.decks[1].slides.size() == 3);

    REQUIRE(built.decks[0].slides[0].objects.size() == 1);
    REQUIRE(seventhslide::model::plain_text(built.decks[0].slides[0].objects[0].text->lines[0]) == "Sunday Worship");
    REQUIRE(seventhslide::model::plain_text(built.decks[0].slides[2].objects[0].text->lines[0]) == "Main only — slot 3");
    REQUIRE(seventhslide::model::plain_text(built.decks[1].slides[2].objects[0].text->lines[0]) == "LT slot 3");
    REQUIRE(built.decks[1].slides[0].objects.empty());
    REQUIRE(built.decks[0].slides[0].background.kind
            == seventhslide::freeform::SlideBackgroundKind::Color);
    REQUIRE(built.decks[0].slides[0].background.color == "#010203");
    REQUIRE(built.decks[1].slides[0].background.is_none());
}

TEST_CASE("line-control compile keeps per-output spans", "[freeform][compile]")
{
    const auto built = compile(load_doc("line_control_fortress.json"));
    REQUIRE(built.line_control);
    REQUIRE(built.lines.size() == 8);
    REQUIRE(built.lines.front() == "A mighty fortress is our God,");
    REQUIRE(built.decks[0].output_id == "out-main");
    REQUIRE(built.decks[0].line_to_slide == std::vector<int>({0, 0, 0, 0, 1, 1, 1, 1}));
    REQUIRE(built.decks[1].output_id == "out-lt");
    REQUIRE(built.decks[1].line_to_slide == std::vector<int>({0, 0, 1, 1, 2, 2, 3, 3}));
    REQUIRE(built.decks[0].slides.size() == 2);
    REQUIRE(built.decks[1].slides.size() == 4);
    REQUIRE(built.decks[0].slides[0].objects[0].text->lines.size() == 4);
}

TEST_CASE("compile lays out only the decks it is asked for", "[freeform][compile]")
{
    const auto doc = load_doc("line_control_fortress.json");
    const std::set<std::string, std::less<>> only{"out-lt"};
    const auto one = compile(doc, nullptr, &only);
    const auto all = compile(doc);
    REQUIRE(one.decks.size() == 1);
    REQUIRE(one.decks[0].output_id == "out-lt");
    REQUIRE(one.decks[0].line_to_slide == all.decks[1].line_to_slide);
    REQUIRE(one.decks[0].slides.size() == all.decks[1].slides.size());
    // The lines belong to the document, not to a deck.
    REQUIRE(one.lines == all.lines);
}

TEST_CASE("compile emits compact verse codes from cues", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(seventhslide::freeform::set_line_control(doc, true));
    REQUIRE(seventhslide::freeform::set_cue_verse(doc, 0, "Verse", 1));
    REQUIRE(seventhslide::freeform::insert_cue(doc, 1, "line two"));
    REQUIRE(seventhslide::freeform::set_cue_verse(doc, 1, "Chorus", 1));
    const auto built = compile(doc);
    REQUIRE(built.verse_codes == std::vector<std::string>({"v1", "c1"}));
    REQUIRE(built.verse_indices == std::vector<int>({0, 1}));
    REQUIRE(built.line_labels == std::vector<std::string>({"v1", "c1"}));
    REQUIRE(built.lines == std::vector<std::string>({"", "line two"}));
}

TEST_CASE("compile groups a run of cues with one label into one section", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(seventhslide::freeform::set_line_control(doc, true));
    REQUIRE(seventhslide::freeform::set_cue_verse(doc, 0, "Verse", 1));
    REQUIRE(seventhslide::freeform::insert_cue(doc, 1, "verse line two"));
    REQUIRE(seventhslide::freeform::insert_cue(doc, 2, "unlabeled"));
    REQUIRE(seventhslide::freeform::set_cue_verse(doc, 2, "", 1));
    REQUIRE(seventhslide::freeform::insert_cue(doc, 3, "chorus"));
    REQUIRE(seventhslide::freeform::set_cue_verse(doc, 3, "Chorus", 1));
    const auto built = compile(doc);
    REQUIRE(built.verse_codes == std::vector<std::string>({"v1", "", "c1"}));
    REQUIRE(built.verse_indices == std::vector<int>({0, 0, 1, 2}));
    // Labels stay per line.
    REQUIRE(built.line_labels == std::vector<std::string>({"v1", "v1", "", "c1"}));
}

TEST_CASE("compile substitutes tokens and drops {-empty} lines", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    doc.decks[0].slides[0].objects.push_back(
        text_object("{title}\n{-speaker} hidden\nplain"));
    doc.fields["title"] = "  Sunday  ";
    doc.fields["speaker"] = "";
    const auto built = compile(doc);
    REQUIRE(built.decks[0].slides[0].objects.size() == 1);
    const auto& lines = built.decks[0].slides[0].objects[0].text->lines;
    REQUIRE(lines.size() == 2);
    REQUIRE(seventhslide::model::plain_text(lines[0]) == "Sunday");
    REQUIRE(seventhslide::model::plain_text(lines[1]) == "plain");
}

TEST_CASE("illegal braces stay literal", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    doc.decks[0].slides[0].objects.push_back(text_object("{Title} {has space} {ok}"));
    doc.fields["ok"] = "yes";
    doc.fields["title"] = "should-not-match";
    const auto built = compile(doc);
    const auto& text = seventhslide::model::plain_text(built.decks[0].slides[0].objects[0].text->lines[0]);
    REQUIRE(text.find("{Title}") != std::string::npos);
    REQUIRE(text.find("{has space}") != std::string::npos);
    REQUIRE(text.find("yes") != std::string::npos);
    REQUIRE(text.find("{ok}") == std::string::npos);
    REQUIRE(text.find("should-not-match") == std::string::npos);
}

TEST_CASE("build_slides Freeform maps compile onto runtimes", "[freeform][compile]")
{
    ApproxTextMeasurer measurer;
    OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    OutputConfig lt;
    lt.name = "Lower Third";
    lt.id = "out-lt";
    OutputConfig hidden;
    hidden.name = "Fold";
    hidden.id = "out-fold";

    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = load_doc("simple_two_outputs.json");
    in.outputs = {main, lt, hidden};

    const auto built = build_slides(in, measurer);
    REQUIRE(built.kind == BuildKind::Freeform);
    REQUIRE(built.player.total_lines == 3);
    REQUIRE(built.player.all_line_labels == std::vector<std::string>{"1", "2", "3"});
    REQUIRE(built.outputs.size() == 3);
    REQUIRE(built.outputs[0].line_to_slide == std::vector<int>({0, 1, 2}));
    REQUIRE(built.outputs[1].line_to_slide == std::vector<int>({0, 1, 2}));
    REQUIRE(seventhslide::model::plain_text(built.outputs[0].compiled_slides[0].objects[0].text->lines[0])
            == "Sunday Worship");
    REQUIRE(seventhslide::model::plain_text(built.outputs[0].compiled_slides[2].objects[0].text->lines[0])
            == "Main only — slot 3");
    REQUIRE(seventhslide::model::plain_text(built.outputs[1].compiled_slides[2].objects[0].text->lines[0]) == "LT slot 3");
    // Freeform text lives on the object stack, never on the theme overlay -
    // that split is what used to force every text box under every shape.
    REQUIRE(built.outputs[0].slide_overlays[0].empty());
    REQUIRE(built.outputs[2].slide_overlays[0].empty());
    REQUIRE(built.outputs[2].compiled_slides.empty());
}

TEST_CASE("build_slides Freeform line-control fortress spans", "[freeform][compile]")
{
    ApproxTextMeasurer measurer;
    OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    OutputConfig lt;
    lt.name = "Lower Third";
    lt.id = "out-lt";

    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = load_doc("line_control_fortress.json");
    in.outputs = {main, lt};

    const auto built = build_slides(in, measurer);
    REQUIRE(built.player.total_lines == 8);
    REQUIRE(built.player.all_lines.front() == "A mighty fortress is our God,");
    REQUIRE(built.outputs[0].line_to_slide == std::vector<int>({0, 0, 0, 0, 1, 1, 1, 1}));
    REQUIRE(built.outputs[1].line_to_slide == std::vector<int>({0, 0, 1, 1, 2, 2, 3, 3}));
    REQUIRE(built.outputs[0].slide_count() == 2);
    REQUIRE(built.outputs[1].slide_count() == 4);
    REQUIRE(built.outputs[0].compiled_slides[0].objects[0].text->lines.size() == 4);
}

TEST_CASE("compile maps inline markup onto overlay spans", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = text_object("Hello <b>bold</b> <size=50>small</size>");
    obj.italic = true;
    doc.decks[0].slides[0].objects.push_back(std::move(obj));
    const auto built = compile(doc);
    const auto& lines = built.decks[0].slides[0].objects[0].text->lines;
    REQUIRE(lines.size() == 1);
    REQUIRE(seventhslide::model::plain_text(lines[0]) == "Hello bold small");
    REQUIRE(lines[0].size() == 4);
    REQUIRE(lines[0][0].text == "Hello ");
    REQUIRE_FALSE(lines[0][0].style.bold);
    REQUIRE(lines[0][1].text == "bold");
    REQUIRE(lines[0][1].style.bold);
    REQUIRE_FALSE(lines[0][1].style.italic);
    REQUIRE(lines[0][3].text == "small");
    REQUIRE(lines[0][3].style.size_pct == 50);
    REQUIRE_FALSE(lines[0][3].style.italic);
}

TEST_CASE("compile keeps blank lines as overlay rows", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    doc.decks[0].slides[0].objects.push_back(text_object("A\n\nB"));
    const auto built = compile(doc);
    const auto& lines = built.decks[0].slides[0].objects[0].text->lines;
    REQUIRE(lines.size() == 3);
    REQUIRE(seventhslide::model::plain_text(lines[0]) == "A");
    REQUIRE(seventhslide::model::plain_text(lines[1]).empty());
    REQUIRE(seventhslide::model::plain_text(lines[2]) == "B");
}

TEST_CASE("compile maps inline color onto overlay spans", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = text_object("Hi <color=#888888>dim</color>");
    doc.decks[0].slides[0].objects.push_back(std::move(obj));
    const auto built = compile(doc);
    const auto& line = built.decks[0].slides[0].objects[0].text->lines[0];
    REQUIRE(line.size() == 2);
    REQUIRE(line[0].text == "Hi ");
    REQUIRE(line[0].style.color.empty());
    REQUIRE(line[1].text == "dim");
    REQUIRE(line[1].style.color == "#888888");
}

TEST_CASE("compile leaves unstyled text as whole-line flags", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = text_object("plain");
    obj.bold = true;
    obj.italic = true;
    doc.decks[0].slides[0].objects.push_back(std::move(obj));
    const auto built = compile(doc);
    const auto& line = built.decks[0].slides[0].objects[0].text->lines[0];
    REQUIRE(seventhslide::model::plain_text(line) == "plain");
    REQUIRE(line.size() == 1);
    REQUIRE(line[0].style.bold);
    REQUIRE(line[0].style.italic);
}

TEST_CASE("compile maps underline markup onto overlay spans", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    doc.decks[0].slides[0].objects.push_back(text_object("Hello <u>under</u>"));
    const auto built = compile(doc);
    const auto& line = built.decks[0].slides[0].objects[0].text->lines[0];
    REQUIRE(seventhslide::model::plain_text(line) == "Hello under");
    REQUIRE(line.size() == 2);
    REQUIRE_FALSE(line[0].style.underline);
    REQUIRE(line[1].text == "under");
    REQUIRE(line[1].style.underline);
}

TEST_CASE("compile leaves unstyled underline as a whole-line flag", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = text_object("plain");
    obj.underline = true;
    doc.decks[0].slides[0].objects.push_back(std::move(obj));
    const auto built = compile(doc);
    const auto& line = built.decks[0].slides[0].objects[0].text->lines[0];
    REQUIRE(line.size() == 1);
    REQUIRE(line[0].style.underline);
}

TEST_CASE("compile substitutes tokens before parsing markup", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    doc.decks[0].slides[0].objects.push_back(text_object("<b>{title}</b>"));
    doc.fields["title"] = "Sunday";
    const auto built = compile(doc);
    const auto& line = built.decks[0].slides[0].objects[0].text->lines[0];
    REQUIRE(seventhslide::model::plain_text(line) == "Sunday");
    REQUIRE(line.size() == 1);
    REQUIRE(line[0].text == "Sunday");
    REQUIRE(line[0].style.bold);
}

TEST_CASE("slide_preview_key tracks painted content only", "[freeform][compile][preview]")
{
    auto doc = make_document("Deck", {"out-main"});
    auto& deck = doc.decks[0];
    deck.slides[0].objects.push_back(text_object("Hello {title}"));
    REQUIRE(insert_slide(doc, 1));
    deck.slides[1].objects.push_back(text_object("Other"));

    const auto k0 = slide_preview_key(doc, deck, 0);
    const auto k1 = slide_preview_key(doc, deck, 1);
    REQUIRE_FALSE(k0.empty());
    REQUIRE_FALSE(k1.empty());
    REQUIRE(k0 != k1);

    doc.name = "Renamed";
    REQUIRE(slide_preview_key(doc, deck, 0) == k0);
    REQUIRE(slide_preview_key(doc, deck, 1) == k1);

    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "Cue name"));
    REQUIRE(set_cue_verse(doc, 0, "Verse", 2));
    REQUIRE(slide_preview_key(doc, deck, 0) == k0);
    REQUIRE(slide_preview_key(doc, deck, 1) == k1);

    doc.fields["unused"] = "nope";
    REQUIRE(slide_preview_key(doc, deck, 0) == k0);

    doc.fields["title"] = "Sunday";
    const auto k0_field = slide_preview_key(doc, deck, 0);
    REQUIRE(k0_field != k0);
    REQUIRE(slide_preview_key(doc, deck, 1) == k1);

    deck.slides[0].objects[0].x = 40;
    const auto k0_moved = slide_preview_key(doc, deck, 0);
    REQUIRE(k0_moved != k0_field);
    REQUIRE(slide_preview_key(doc, deck, 1) == k1);

    deck.slides[0].background.kind = seventhslide::freeform::SlideBackgroundKind::Color;
    deck.slides[0].background.color = "#ff0000";
    REQUIRE(slide_preview_key(doc, deck, 0) != k0_moved);
    REQUIRE(slide_preview_key(doc, deck, 1) == k1);
}

TEST_CASE("Remembered preview keys match the keys worked out afresh", "[freeform][compile][preview]")
{
    auto doc = make_document("Deck", {"out-main"});
    auto& deck = doc.decks[0];
    deck.slides[0].objects.push_back(text_object("Hello {title}"));
    REQUIRE(insert_slide(doc, 1));
    deck.slides[1].objects.push_back(make_indicator_object(doc, deck.canvas));
    REQUIRE(insert_slide(doc, 2));
    deck.slides[2].objects.push_back(text_object("Third"));

    seventhslide::freeform::SlidePreviewKeys remembered;
    const auto same = [&] {
        CHECK(remembered.keys(doc, deck) == seventhslide::freeform::slide_preview_keys(doc, deck));
        for (int s = 0; s < deck.slide_count(); ++s) {
            CHECK(remembered.key(doc, deck, s) == slide_preview_key(doc, deck, s));
        }
    };
    same();
    deck.slides[2].objects[0].x = 40;  // one slide's own change
    same();
    doc.fields["title"] = "Sunday";  // a field every slide may read
    same();
    REQUIRE(set_line_control(doc, true));  // the cues the indicator shows
    REQUIRE(set_cue_verse(doc, 0, "Verse", 2));
    same();
    std::swap(deck.slides[0], deck.slides[2]);  // slides that change places
    same();
    deck.slides.pop_back();  // a slide gone
    same();
}

TEST_CASE("compile and SlideBuilder honor per-slide backgrounds", "[freeform][compile][bg]")
{
    auto doc = make_document("Deck", {"out-main"});
    auto& slide0 = doc.decks[0].slides[0];
    slide0.background.kind = seventhslide::freeform::SlideBackgroundKind::Color;
    slide0.background.color = "#ff0000";
    REQUIRE(insert_slide(doc, 1));
    doc.decks[0].slides[1].background.kind = seventhslide::freeform::SlideBackgroundKind::Video;
    doc.decks[0].slides[1].background.video = "clip.mp4";
    REQUIRE(insert_slide(doc, 2));
    doc.decks[0].slides[2].background.kind = seventhslide::freeform::SlideBackgroundKind::Image;
    doc.decks[0].slides[2].background.image = "photo.png";
    REQUIRE(insert_slide(doc, 3));

    const auto compiled = compile(doc);
    REQUIRE(compiled.decks[0].slides[0].background.kind
            == seventhslide::freeform::SlideBackgroundKind::Color);
    REQUIRE(compiled.decks[0].slides[1].background.video == "clip.mp4");
    REQUIRE(compiled.decks[0].slides[2].background.kind
            == seventhslide::freeform::SlideBackgroundKind::Image);
    REQUIRE(compiled.decks[0].slides[3].background.is_none());

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.bg_themes = {
        ThemeEntry{"night", "Night",
                   nlohmann::json{{"background_type", "color"}, {"background_color", "#010203"}}},
    };

    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = doc;
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    REQUIRE(built.outputs[0].slide_bg_overrides.size() == 4);
    REQUIRE(built.outputs[0].slide_bg_overrides[0].has_value());
    REQUIRE(built.outputs[0].slide_bg_overrides[0]->type
            == seventhslide::layout::BackgroundType::Color);
    REQUIRE(built.outputs[0].slide_bg_overrides[0]->color == "#ff0000");
    REQUIRE(built.outputs[0].slide_bg_overrides[1].has_value());
    REQUIRE(built.outputs[0].slide_bg_overrides[1]->type
            == seventhslide::layout::BackgroundType::Video);
    REQUIRE(built.outputs[0].slide_bg_overrides[1]->video == "/static/videos/clip.mp4");
    REQUIRE(built.outputs[0].slide_bg_overrides[2].has_value());
    REQUIRE(built.outputs[0].slide_bg_overrides[2]->type
            == seventhslide::layout::BackgroundType::Image);
    REQUIRE(built.outputs[0].slide_bg_overrides[2]->image == "/static/images/photo.png");
    // A slide with no background of its own paints none: not the
    // background theme's.
    REQUIRE(built.outputs[0].slide_bg_overrides[3].has_value());
    REQUIRE(built.outputs[0].slide_bg_overrides[3]->type
            == seventhslide::layout::BackgroundType::Transparent);
    REQUIRE(built.outputs[0].theme.background.color == "#000000");
}

TEST_CASE("compile content clock is max dissolving object", "[freeform][compile][transition]")
{
    using seventhslide::model::TransitionType;
    auto doc = make_document("t", {"out-main"});
    REQUIRE_FALSE(doc.decks.empty());
    REQUIRE_FALSE(doc.decks[0].slides.empty());

    Object fade = text_object("Fade me");
    fade.transition = seventhslide::model::Transition{TransitionType::Fade, 400};
    Object hold = text_object("Hold me");
    doc.decks[0].slides[0].objects.push_back(std::move(fade));
    doc.decks[0].slides[0].objects.push_back(std::move(hold));

    const auto built = compile(doc);
    REQUIRE_FALSE(built.decks.empty());
    REQUIRE_FALSE(built.decks[0].slides.empty());
    const auto& slide = built.decks[0].slides[0];
    REQUIRE(slide.objects.size() == 2);
    REQUIRE(slide.objects[0].transition.plays());
    REQUIRE(slide.objects[0].transition.duration_ms == 400);
    REQUIRE_FALSE(slide.objects[1].transition.plays());
    REQUIRE(slide.transition.plays());
    REQUIRE(slide.transition.duration_ms == 400);
}

TEST_CASE("compile carries a text box's frame beside its words", "[freeform][compile][fill]")
{
    auto doc = make_document("x", {"out-main"});
    Object plain = text_object("Plain");
    Object framed = text_object("Framed");
    framed.fill.color = "#202020";
    Object lined = text_object("");
    lined.line.color = "#ffffff";
    lined.line.width = 3;
    Object off = text_object("Kind none");
    off.fill.color = "#202020";
    off.fill.kind = seventhslide::canvas::FillKind::None;
    for (auto* obj : {&plain, &framed, &lined, &off}) {
        doc.decks[0].slides[0].objects.push_back(*obj);
    }
    const auto built = compile(doc);
    const auto& objects = built.decks[0].slides[0].objects;
    REQUIRE(objects.size() == 4);
    // Plain words: no object. A fill or a line: the object too, as it paints.
    CHECK(objects[0].text);
    CHECK_FALSE(objects[0].object);
    CHECK(objects[1].text);
    REQUIRE(objects[1].object);
    CHECK(objects[1].object->type == ObjectType::Text);
    CHECK(objects[1].object->fill.color == "#202020");
    // A frame with no words still paints.
    CHECK_FALSE(objects[2].text);
    REQUIRE(objects[2].object);
    CHECK(objects[2].object->line.width == 3);
    // A fill of kind none is no frame.
    CHECK(objects[3].text);
    CHECK_FALSE(objects[3].object);
}

TEST_CASE("compile carries a text box's effects to its words and its frame",
          "[freeform][compile][effects]")
{
    auto doc = make_document("x", {"out-main"});
    Object obj = text_object("Shadowed");
    obj.effects.text_shadow.on = true;
    obj.effects.text_outline.width = 2;
    obj.effects.shadow.on = true;
    obj.fill.color = "#202020";
    doc.decks[0].slides[0].objects.push_back(obj);
    const auto built = compile(doc);
    const auto& compiled = built.decks[0].slides[0].objects.at(0);
    REQUIRE(compiled.text);
    CHECK(compiled.text->effects == obj.effects);
    REQUIRE(compiled.object);
    CHECK(compiled.object->effects == obj.effects);
}

TEST_CASE("compile carries a text box's paragraphs to its words", "[freeform][compile][paragraph]")
{
    auto doc = make_document("x", {"out-main"});
    Object obj = text_object("One\nTwo");
    obj.paragraph.bullets = seventhslide::canvas::Bullets::Number;
    obj.paragraph.margins.left = 24;
    obj.paragraph.autofit = seventhslide::canvas::Autofit::Shrink;
    doc.decks[0].slides[0].objects.push_back(obj);
    const auto built = compile(doc);
    const auto& compiled = built.decks[0].slides[0].objects.at(0);
    REQUIRE(compiled.text);
    CHECK(compiled.text->paragraph == obj.paragraph);
}

TEST_CASE("An indicator with a frame is one scene object, words and frame together",
          "[freeform][compile][scene][fill]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(set_cue_verse(doc, 0, "Verse", 1));
    auto obj = make_indicator_object(doc, {1920, 1080});
    obj.fill.color = "#303030";
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));

    ApproxTextMeasurer measurer;
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = doc;
    in.outputs = {oc};
    auto built = build_slides(in, measurer);
    REQUIRE_FALSE(built.outputs.empty());
    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], seventhslide::scene::SceneMode::Freeform, 1920, 1080, 0, {}, &measurer, 0,
    };
    const auto frame = seventhslide::scene::build_scene_frame(req);
    int indicators = 0;
    for (const auto& so : frame.objects) {
        if (so.object && so.object->type == seventhslide::canvas::ObjectType::Indicator) {
            ++indicators;
            CHECK(so.text);
            CHECK(so.object->fill.color == "#303030");
        }
    }
    CHECK(indicators == 1);
}

TEST_CASE("compile keeps indicator placeholders without baking verse text", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(set_cue_verse(doc, 0, "Verse", 1));
    auto obj = make_indicator_object(doc, {1920, 1080});
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    const auto built = compile(doc);
    REQUIRE(built.decks[0].slides[0].objects.size() == 1);
    REQUIRE(built.decks[0].slides[0].objects[0].indicator);
    REQUIRE_FALSE(built.decks[0].slides[0].objects[0].text);
    REQUIRE(built.decks[0].slides[0].objects[0].indicator->fallback == IndicatorFallback::Slide);
}

TEST_CASE("compile keeps clock placeholders without baking the time", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = make_clock_object(doc, {1920, 1080});
    obj.clock_24h = true;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    const auto built = compile(doc);
    REQUIRE(built.decks[0].slides[0].objects.size() == 1);
    REQUIRE(built.decks[0].slides[0].objects[0].object);
    REQUIRE_FALSE(built.decks[0].slides[0].objects[0].text);
    REQUIRE(built.decks[0].slides[0].objects[0].object->clock_24h);
    REQUIRE_FALSE(built.decks[0].slides[0].objects[0].object->clock_seconds);
}

TEST_CASE("scene frame carries live clock layout without overlay text", "[freeform][compile][scene]")
{
    auto doc = make_document("x", {"out-main"});
    auto clock = make_clock_object(doc, {1920, 1080});
    // Alignment used to be dropped between compile and the scene frame, so a
    // Freeform clock always painted top-left.
    clock.text_align = "right";
    clock.vertical_align = "bottom";
    REQUIRE(add_object(doc, "out-main", 0, std::move(clock)));

    ApproxTextMeasurer measurer;
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = doc;
    in.outputs = {oc};
    auto built = build_slides(in, measurer);
    REQUIRE_FALSE(built.outputs.empty());

    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0],
        seventhslide::scene::SceneMode::Freeform,
        1920,
        1080,
        0,
        {},
        &measurer,
        0,
    };
    auto frame = seventhslide::scene::build_scene_frame(req);
    bool saw_clock = false;
    for (const auto& obj : frame.objects) {
        if (obj.object && obj.object->type == seventhslide::canvas::ObjectType::Clock) {
            saw_clock = true;
            REQUIRE(obj.object->font_size == 48);
            REQUIRE(obj.object->text_align == "right");
            REQUIRE(obj.object->vertical_align == "bottom");
            REQUIRE_FALSE(obj.text);
        }
    }
    REQUIRE(saw_clock);
}

TEST_CASE("scene frame resolves live indicator from current cue", "[freeform][compile][scene]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(set_cue_verse(doc, 0, "Verse", 1));
    REQUIRE(insert_cue(doc, 1, "chorus line"));
    REQUIRE(set_cue_verse(doc, 1, "Chorus", 1));
    auto obj = make_indicator_object(doc, {1920, 1080});
    obj.line_height = 1.6;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));

    ApproxTextMeasurer measurer;
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = doc;
    in.outputs = {oc};
    auto built = build_slides(in, measurer);
    REQUIRE_FALSE(built.outputs.empty());

    seventhslide::scene::SceneFrameRequest req{
        built.outputs[0],
        seventhslide::scene::SceneMode::Freeform,
        1920,
        1080,
        0,
        {},
        &measurer,
        0,
    };
    auto frame = seventhslide::scene::build_scene_frame(req);
    // The resolved indicator joins the object stack (in its z slot), not the
    // theme overlay.
    REQUIRE(frame.overlay.boxes.empty());
    REQUIRE_FALSE(frame.objects.empty());
    REQUIRE(frame.objects.back().text.has_value());
    // Painted at the object's line height, as the editor canvas draws it.
    REQUIRE(frame.objects.back().text->line_height == 1.6);
    const auto& line0 = frame.objects.back().text->lines.front();
    REQUIRE(seventhslide::model::plain_text(line0).find("V1") != std::string::npos);
    REQUIRE(seventhslide::model::plain_text(line0).find("C1") != std::string::npos);
    REQUIRE_FALSE(line0.empty());
    REQUIRE(line0.front().style.bold);

    req.line_index = 1;
    frame = seventhslide::scene::build_scene_frame(req);
    const auto& line1 = frame.objects.back().text->lines.front();
    REQUIRE(line1.size() >= 2);
    REQUIRE_FALSE(line1.front().style.bold);
    REQUIRE(line1.back().style.bold);

    REQUIRE(set_cue_verse(doc, 0, "", 1));
    in.freeform.document = doc;
    built = build_slides(in, measurer);
    seventhslide::scene::SceneFrameRequest unlabeled{
        built.outputs[0],
        seventhslide::scene::SceneMode::Freeform,
        1920,
        1080,
        0,
        {},
        &measurer,
        0,
    };
    frame = seventhslide::scene::build_scene_frame(unlabeled);
    REQUIRE_FALSE(frame.objects.empty());
    REQUIRE(frame.objects.back().text.has_value());
    REQUIRE(seventhslide::model::plain_text(frame.objects.back().text->lines.front()).find("1/")
            != std::string::npos);
}

TEST_CASE("scene frame sets a Freeform indicator's labels its gap apart", "[freeform][compile][scene][indicator]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(set_cue_verse(doc, 0, "Verse", 1));
    REQUIRE(insert_cue(doc, 1, "chorus line"));
    REQUIRE(set_cue_verse(doc, 1, "Chorus", 1));
    auto obj = make_indicator_object(doc, {1920, 1080});
    obj.indicator_gap = 0.5;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));

    ApproxTextMeasurer measurer;
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = doc;
    in.outputs = {oc};
    const auto built = build_slides(in, measurer);
    REQUIRE_FALSE(built.outputs.empty());
    const seventhslide::scene::SceneFrameRequest req{
        built.outputs[0], seventhslide::scene::SceneMode::Freeform, 1920, 1080, 0, {}, &measurer, 0,
    };
    const auto frame = seventhslide::scene::build_scene_frame(req);
    REQUIRE_FALSE(frame.objects.empty());
    REQUIRE(frame.objects.back().text.has_value());
    CHECK(frame.objects.back().text->lines.front()
          == seventhslide::scene::indicator_line({{"V1", true}, {"C1", false}}, 0.5));
}

TEST_CASE("slide_preview_key tracks indicator cue verses", "[freeform][compile][preview]")
{
    auto doc = make_document("Deck", {"out-main"});
    auto& deck = doc.decks[0];
    REQUIRE(set_line_control(doc, true));
    REQUIRE(add_object(doc, "out-main", 0, make_indicator_object(doc, {1920, 1080})));
    const auto k0 = slide_preview_key(doc, deck, 0);
    REQUIRE(set_cue_verse(doc, 0, "Verse", 1));
    const auto k1 = slide_preview_key(doc, deck, 0);
    REQUIRE(k0 != k1);
    REQUIRE(set_cue_verse(doc, 0, "Chorus", 1));
    const auto k2 = slide_preview_key(doc, deck, 0);
    REQUIRE(k2 != k1);

    // Where a repeat starts is not in the labels.
    REQUIRE(insert_cue(doc, 1, "again"));
    const auto k3 = slide_preview_key(doc, deck, 0);
    REQUIRE(seventhslide::freeform::set_cue_section_start(doc, 1, true));
    REQUIRE(slide_preview_key(doc, deck, 0) != k3);
}

TEST_CASE("linked text paints cue span instead of object.text", "[freeform][compile][link]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "Hello"));
    auto obj = make_text_object(doc, {1920, 1080});
    obj.text = "stale";
    obj.cue_linked = true;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    const auto built = compile(doc);
    REQUIRE(built.decks[0].slides[0].objects[0].text);
    REQUIRE(seventhslide::model::plain_text(built.decks[0].slides[0].objects[0].text->lines[0]) == "Hello");
    REQUIRE(rename_cue(doc, 0, "Renamed"));
    const auto again = compile(doc);
    REQUIRE(seventhslide::model::plain_text(again.decks[0].slides[0].objects[0].text->lines[0]) == "Renamed");
}

TEST_CASE("show upcoming paints later cues without owning them", "[freeform][compile][link]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(insert_slide(doc, 1));
    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "Now"));
    REQUIRE(rename_cue(doc, 1, "Next"));
    REQUIRE(insert_cue(doc, 2, "Later"));
    auto obj = make_text_object(doc, {1920, 1080});
    obj.text = "stale";
    obj.cue_linked = true;
    obj.show_upcoming = true;
    obj.upcoming_color = "#888888";
    obj.font_size = 24;
    obj.w = 1600;
    obj.h = 800;
    REQUIRE(add_object(doc, "out-main", 0, obj));

    ApproxTextMeasurer measurer;
    const auto tall = compile(doc, &measurer);
    REQUIRE(tall.decks[0].line_to_slide[0] == 0);
    REQUIRE(tall.decks[0].line_to_slide[1] == 1);
    REQUIRE(tall.decks[0].line_to_slide[2] == 1);

    const auto& painted = tall.decks[0].slides[0].objects[0].text;
    REQUIRE(painted);
    REQUIRE(painted->lines.size() >= 2);
    REQUIRE(seventhslide::model::plain_text(painted->lines[0]) == "Now");
    REQUIRE(painted->lines[0].front().style.color.empty());
    bool saw_next = false;
    for (const auto& line : painted->lines) {
        const auto text = seventhslide::model::plain_text(line);
        if (text == "Next" || text == "Later") {
            saw_next = true;
            REQUIRE(line.front().style.color == "#888888");
        }
    }
    REQUIRE(saw_next);

    doc.decks[0].slides[0].objects[0].h = 20;
    const auto clipped = compile(doc, &measurer);
    const auto& short_painted = clipped.decks[0].slides[0].objects[0].text;
    REQUIRE(short_painted);
    REQUIRE(short_painted->lines.size() == 1);
    REQUIRE(seventhslide::model::plain_text(short_painted->lines[0]) == "Now");
}

// The fit check measured each upcoming cue as plain text in the box font,
// so a cue whose markup makes it bigger was taken and overflowed the box.
TEST_CASE("show upcoming measures a cue as paint lays it out", "[freeform][compile][link]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(insert_slide(doc, 1));
    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "Now"));
    REQUIRE(rename_cue(doc, 1, "Next"));
    auto obj = make_text_object(doc, {1920, 1080});
    obj.cue_linked = true;
    obj.show_upcoming = true;
    obj.font_size = 24;
    obj.line_height = 1.15;
    obj.w = 1600;
    // Room for two rows at 24 px (about 28 px each), not for one at 72 px.
    obj.h = 70;
    REQUIRE(add_object(doc, "out-main", 0, obj));

    ApproxTextMeasurer measurer;
    const auto plain_next = [&] {
        const auto built = compile(doc, &measurer);
        const auto& painted = built.decks[0].slides[0].objects[0].text;
        REQUIRE(painted);
        return painted->lines.size();
    };
    CHECK(plain_next() == 2);

    doc.cues[1].text = "<size=300>Next</size>";
    CHECK(plain_next() == 1);
}

TEST_CASE("growing a linked span promotes upcoming cues to active", "[freeform][compile][link]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(insert_slide(doc, 1));
    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "Fortress"));
    REQUIRE(rename_cue(doc, 1, "Bulwark"));
    REQUIRE(insert_cue(doc, 2, "Helper"));
    REQUIRE(insert_cue(doc, 3, "Prevailing"));
    REQUIRE(insert_cue(doc, 4, "Ancient foe"));
    REQUIRE(move_span_end(doc, "out-main", 0, 3));
    auto obj = make_text_object(doc, {1920, 1080});
    obj.text = "Fortress\nBulwark\nHelper\nPrevailing";
    obj.cue_linked = true;
    obj.show_upcoming = true;
    obj.upcoming_color = "#888888";
    obj.font_size = 24;
    obj.w = 1600;
    obj.h = 800;
    REQUIRE(add_object(doc, "out-main", 0, obj));

    ApproxTextMeasurer measurer;
    const auto before = compile(doc, &measurer);
    REQUIRE(before.decks[0].line_to_slide == std::vector<int>{0, 0, 0, 0, 1});
    const auto& painted = before.decks[0].slides[0].objects[0].text;
    REQUIRE(painted);
    REQUIRE(painted->lines.size() >= 5);
    REQUIRE(seventhslide::model::plain_text(painted->lines[0]) == "Fortress");
    REQUIRE(seventhslide::model::plain_text(painted->lines[3]) == "Prevailing");
    REQUIRE(painted->lines[3].front().style.color.empty());
    REQUIRE(seventhslide::model::plain_text(painted->lines[4]) == "Ancient foe");
    REQUIRE(painted->lines[4].front().style.color == "#888888");

    REQUIRE(move_span_end(doc, "out-main", 0, 4));
    const auto after = compile(doc, &measurer);
    REQUIRE(after.decks[0].line_to_slide == std::vector<int>{0, 0, 0, 0, 0});
    const auto& grown = after.decks[0].slides[0].objects[0].text;
    REQUIRE(grown);
    REQUIRE(grown->lines.size() >= 5);
    REQUIRE(seventhslide::model::plain_text(grown->lines[4]) == "Ancient foe");
    REQUIRE(grown->lines[4].front().style.color.empty());
}

TEST_CASE("slide_preview_key tracks linked cue text", "[freeform][compile][preview][link]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "Hello"));
    auto obj = make_text_object(doc, {1920, 1080});
    obj.cue_linked = true;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    const auto k0 = slide_preview_key(doc, doc.decks[0], 0);
    REQUIRE(rename_cue(doc, 0, "Changed"));
    REQUIRE(slide_preview_key(doc, doc.decks[0], 0) != k0);
}

TEST_CASE("linked overlay applies per-cue styling and extra breaks", "[freeform][compile][link]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "Hello"));
    REQUIRE(insert_cue(doc, 1, "World"));
    auto obj = make_text_object(doc, {1920, 1080});
    obj.cue_linked = true;
    const std::string id0 = doc.cues[0].id;
    const std::string id1 = doc.cues[1].id;
    obj.cue_overlays[id0] = "<color=#ff0000>Hel</color>\n<color=#ff0000>lo</color>";
    obj.cue_overlays[id1] = "<font=Courier><size=80>World</size></font>";
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));

    const auto built = compile(doc);
    REQUIRE(built.decks[0].slides[0].objects[0].text);
    const auto& lines = built.decks[0].slides[0].objects[0].text->lines;
    REQUIRE(lines.size() == 3);
    REQUIRE(seventhslide::model::plain_text(lines[0]) == "Hel");
    REQUIRE_FALSE(lines[0].empty());
    REQUIRE(lines[0][0].style.color == "#ff0000");
    REQUIRE(seventhslide::model::plain_text(lines[1]) == "lo");
    REQUIRE(lines[1][0].style.color == "#ff0000");
    REQUIRE(seventhslide::model::plain_text(lines[2]) == "World");
    REQUIRE_FALSE(lines[2].empty());
    REQUIRE(lines[2][0].style.font_family == "Courier");
    REQUIRE(lines[2][0].style.size_pct == 80);
}

TEST_CASE("compile raises <super> runs on overlay spans", "[freeform][compile][super]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = make_text_object(doc, {1920, 1080});
    obj.cue_linked = false;
    obj.text = "<size=50><super>3:16</super></size> For God";
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));

    const auto built = compile(doc);
    REQUIRE(built.decks[0].slides[0].objects[0].text);
    const auto& lines = built.decks[0].slides[0].objects[0].text->lines;
    REQUIRE_FALSE(lines.empty());
    REQUIRE_FALSE(lines[0].empty());
    REQUIRE(lines[0][0].text == "3:16");
    REQUIRE(lines[0][0].style.size_pct == 50);
    REQUIRE(lines[0][0].style.baseline_rise_pct == 40);
}

TEST_CASE("changing a cue keeps its overlay's look on its words, and no other's changes",
          "[freeform][compile][link]")
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "Hello"));
    REQUIRE(insert_cue(doc, 1, "World"));
    auto obj = make_text_object(doc, {1920, 1080});
    obj.cue_linked = true;
    obj.cue_overlays[doc.cues[0].id] = "<color=#ff0000>Hello</color>";
    obj.cue_overlays[doc.cues[1].id] = "<color=#00ff00>World</color>";
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));

    REQUIRE(rename_cue(doc, 0, "Hi"));
    const auto built = compile(doc);
    const auto& lines = built.decks[0].slides[0].objects[0].text->lines;
    REQUIRE(lines.size() == 2);
    REQUIRE(seventhslide::model::plain_text(lines[0]) == "Hi");
    REQUIRE(lines[0].size() == 1);
    REQUIRE(lines[0][0].style.color == "#ff0000");
    REQUIRE(seventhslide::model::plain_text(lines[1]) == "World");
    REQUIRE_FALSE(lines[1].empty());
    REQUIRE(lines[1][0].style.color == "#00ff00");
    REQUIRE(doc.decks[0].slides[0].objects[0].cue_overlays.count(doc.cues[0].id) == 1);
    REQUIRE(doc.decks[0].slides[0].objects[0].cue_overlays.count(doc.cues[1].id) == 1);
}


// The compiler split lines with getline, which drops a trailing empty
// line the editor shows, so the box laid out differently on the output.
TEST_CASE("compile keeps the lines the editor shows, trailing blank included", "[freeform][compile]")
{
    auto doc = make_document("x", {"out-main"});
    auto obj = make_text_object(doc, {1920, 1080});
    obj.text = "First\nSecond\n";
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    const auto built = compile(doc);
    const auto& text = built.decks[0].slides[0].objects[0].text;
    REQUIRE(text.has_value());
    REQUIRE(text->lines.size()
            == seventhslide::model::parse_markup_lines("First\nSecond\n").size());
    REQUIRE(text->lines.size() == 3);
    REQUIRE(seventhslide::model::plain_text(text->lines.back()).empty());
}

namespace {

/// One slide, line control on, and a box linked to its cues: "For God so
/// loved" from John 3:16, then "the world" carrying that verse on.
Document bible_verse_doc()
{
    auto doc = make_document("x", {"out-main"});
    REQUIRE(set_line_control(doc, true));
    REQUIRE(rename_cue(doc, 0, "For God so loved"));
    REQUIRE(seventhslide::freeform::set_cue_scripture(doc, 0, 3, 16));
    REQUIRE(insert_cue(doc, 1, "the world"));
    auto obj = make_text_object(doc, {1920, 1080});
    obj.cue_linked = true;
    REQUIRE(add_object(doc, "out-main", 0, std::move(obj)));
    return doc;
}

const std::vector<seventhslide::model::RichLine>& painted_lines(
    const seventhslide::freeform::CompiledPlayback& built)
{
    REQUIRE(built.decks[0].slides[0].objects[0].text);
    return built.decks[0].slides[0].objects[0].text->lines;
}

} // namespace

TEST_CASE("a Bible verse cue shows its chapter:verse in linked text", "[freeform][compile][link][bible]")
{
    const auto doc = bible_verse_doc();
    // A line added inside the verse carries it on without showing it again.
    REQUIRE(doc.cues[1].chapter == 3);
    REQUIRE(doc.cues[1].verse == 16);
    REQUIRE_FALSE(doc.cues[1].show_ref);

    const auto built = compile(doc);
    const auto& lines = painted_lines(built);
    REQUIRE(lines.size() == 2);
    REQUIRE(seventhslide::model::plain_text(lines[0]) == "3:16 For God so loved");
    REQUIRE(lines[0][0].text == "3:16");
    REQUIRE(lines[0][0].style.size_pct == seventhslide::freeform::k_cue_ref_size_pct);
    REQUIRE(lines[0][0].style.baseline_rise_pct == seventhslide::model::k_super_rise_pct);
    REQUIRE(seventhslide::model::plain_text(lines[1]) == "the world");
    // The controller labels both lines by the verse; the cue's own text has no number.
    REQUIRE(built.line_labels == std::vector<std::string>{"3:16", "3:16"});
    REQUIRE(built.lines == std::vector<std::string>{"For God so loved", "the world"});
}

TEST_CASE("a linked box restyles a verse number but cannot retype it", "[freeform][compile][link][bible]")
{
    auto doc = bible_verse_doc();
    const auto& deck = doc.decks[0];
    std::map<std::string, std::string> overlays;
    REQUIRE_FALSE(seventhslide::freeform::split_linked_overlays(
        doc, deck, 0, deck.slides[0].objects[0], "3:17 For God so loved\nthe world", overlays));
    REQUIRE_FALSE(seventhslide::freeform::split_linked_overlays(
        doc, deck, 0, deck.slides[0].objects[0], "For God so loved\nthe world", overlays));
    REQUIRE(seventhslide::freeform::split_linked_overlays(
        doc, deck, 0, deck.slides[0].objects[0], "<color=#ffcc00>3:16</color> For God so loved\nthe world", overlays));
    doc.decks[0].slides[0].objects[0].cue_overlays = overlays;

    const auto built = compile(doc);
    const auto& lines = painted_lines(built);
    REQUIRE(lines[0][0].text == "3:16");
    REQUIRE(lines[0][0].style.color == "#ffcc00");
    REQUIRE(lines[0][0].style.size_pct == 100);
}

TEST_CASE("a verse number keeps its style when the verse or the text changes", "[freeform][compile][link][bible]")
{
    auto doc = bible_verse_doc();
    doc.decks[0].slides[0].objects[0].cue_overlays[doc.cues[0].id] =
        "<color=#ffcc00>3:16</color> <i>For God so loved</i>";

    // A new verse number keeps the old one's style, and the text keeps its own.
    REQUIRE(seventhslide::freeform::set_cue_scripture(doc, 0, 3, 17));
    {
        const auto built = compile(doc);
        const auto& line = painted_lines(built)[0];
        REQUIRE(seventhslide::model::plain_text(line) == "3:17 For God so loved");
        REQUIRE(line[0].style.color == "#ffcc00");
        REQUIRE(line.back().style.italic);
    }

    // Changed text keeps its style on what did not change, the number its own.
    REQUIRE(rename_cue(doc, 0, "For God so"));
    {
        const auto built = compile(doc);
        const auto& line = painted_lines(built)[0];
        REQUIRE(seventhslide::model::plain_text(line) == "3:17 For God so");
        REQUIRE(line[0].style.color == "#ffcc00");
        REQUIRE(line.back().style.italic);
    }

    // Hidden, the number goes and the text stays styled; shown again, it is as it was.
    REQUIRE(seventhslide::freeform::set_cue_ref_shown(doc, 0, false));
    {
        const auto built = compile(doc);
        const auto& line = painted_lines(built)[0];
        REQUIRE(seventhslide::model::plain_text(line) == "For God so");
        REQUIRE(line.back().style.italic);
    }
    REQUIRE(seventhslide::freeform::set_cue_ref_shown(doc, 0, true));
    {
        const auto built = compile(doc);
        const auto& line = painted_lines(built)[0];
        REQUIRE(line[0].text == "3:17");
        REQUIRE(line[0].style.color == "#ffcc00");
        REQUIRE(line.back().style.italic);
    }

    // A song label replaces the verse.
    REQUIRE(set_cue_verse(doc, 0, "Verse", 2));
    REQUIRE(doc.cues[0].chapter == 0);
    REQUIRE_FALSE(doc.cues[0].show_ref);
    REQUIRE(seventhslide::model::plain_text(painted_lines(compile(doc))[0]) == "For God so");
    REQUIRE_FALSE(seventhslide::freeform::set_cue_ref_shown(doc, 0, true));
}

TEST_CASE("a cue's Bible verse stays in range", "[freeform][edit][bible]")
{
    auto doc = bible_verse_doc();
    REQUIRE(seventhslide::freeform::set_cue_scripture(doc, 0, 0, 999));
    REQUIRE(doc.cues[0].chapter == 1);
    REQUIRE(doc.cues[0].verse == seventhslide::parse::k_max_bible_verse);
    REQUIRE(seventhslide::freeform::set_cue_scripture(doc, 0, 151, 1));
    REQUIRE(doc.cues[0].chapter == seventhslide::parse::k_max_bible_chapter);
}

TEST_CASE("A Freeform takes nothing of a theme's: its own transitions, or cuts",
          "[freeform][compile][transition][bg]")
{
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    auto doc = make_document("Deck", {"out-main"});
    REQUIRE(insert_slide(doc, 1));
    REQUIRE(insert_slide(doc, 2));
    const auto text = [](const char* words) {
        Object obj;
        obj.type = ObjectType::Text;
        obj.text = words;
        obj.w = 400;
        obj.h = 100;
        return obj;
    };
    REQUIRE(add_object(doc, "out-main", 0, text("Hello")));
    // The second slide comes in through black; its object has none of its own.
    doc.decks[0].slides[1].transition = Transition{TransitionType::FadeThroughBlack, 600};
    REQUIRE(add_object(doc, "out-main", 1, text("Again")));
    doc.decks[0].slides[2].background.kind = seventhslide::freeform::SlideBackgroundKind::Video;
    doc.decks[0].slides[2].background.video = "clip.mp4";

    const auto compiled = compile(doc);
    const auto& second = compiled.decks[0].slides[1];
    CHECK(second.transition == Transition{TransitionType::FadeThroughBlack, 600});
    REQUIRE_FALSE(second.objects.empty());
    CHECK(second.objects[0].transition == Transition{TransitionType::FadeThroughBlack, 600});
    CHECK_FALSE(compiled.decks[0].slides[0].transition.plays());

    // A background theme that fades does not reach it: an unset background
    // transition is a cut; a video background plays from the library.
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.bg_themes = {ThemeEntry{"night", "Night",
                               nlohmann::json{{"background_type", "color"},
                                              {"background_color", "#010203"},
                                              {"bg_enable_fade", true},
                                              {"bg_fade_duration", 900}}}};
    oc.category_defaults = {{"song", {{"bg", "night"}}}, {"bible", {{"bg", "night"}}}};
    SlideBuildInput in;
    in.kind = BuildKind::Freeform;
    in.selection.kind = ContentKind::Freeform;
    in.freeform.document = doc;
    in.outputs = {oc};
    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 1);
    REQUIRE(built.outputs[0].slide_bg_overrides[2]->type
            == seventhslide::layout::BackgroundType::Video);
    CHECK(built.outputs[0].slide_bg_overrides[2]->video == "/static/videos/clip.mp4");
    for (int s = 0; s < 3; ++s) {
        const auto frame = seventhslide::scene::build_scene_frame(
            {built.outputs[0], seventhslide::scene::SceneMode::Freeform, 1920, 1080, s, {}});
        CAPTURE(s);
        CHECK_FALSE(frame.background_transition.plays());
        CHECK(frame.background.color != "#010203");
    }

    // A Freeform slide takes nothing from any theme: built for an output with
    // none at all, every slide paints the same.
    OutputConfig bare;
    bare.name = oc.name;
    bare.id = oc.id;
    in.outputs = {bare};
    const auto unthemed = build_slides(in, measurer);
    REQUIRE(unthemed.outputs.size() == 1);
    for (int s = 0; s < 3; ++s) {
        CAPTURE(s);
        CHECK(seventhslide::scene::build_scene_frame(
                  {unthemed.outputs[0], seventhslide::scene::SceneMode::Freeform, 1920, 1080, s, {}})
              == seventhslide::scene::build_scene_frame(
                  {built.outputs[0], seventhslide::scene::SceneMode::Freeform, 1920, 1080, s, {}}));
    }
}
