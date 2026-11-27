#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/scene/chrome_text.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/theme/theme_elements.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

using Catch::Approx;
using seventhslide::geom::CanvasMapping;
using seventhslide::geom::CanvasSize;
using seventhslide::geom::k_reference_canvas;
namespace freeform = seventhslide::freeform;
namespace theme = seventhslide::theme;

namespace {

[[nodiscard]] nlohmann::json* element_json(nlohmann::json& style,
                                           std::string_view ctx,
                                           std::string_view id)
{
    for (auto& el : style.at("elements").at(std::string(ctx))) {
        if (el.value("id", "") == id) {
            return &el;
        }
    }
    return nullptr;
}

[[nodiscard]] const seventhslide::scene::SceneObject* scene_object(
    const seventhslide::scene::SceneFrame& frame, std::string_view id)
{
    for (const auto& so : frame.objects) {
        if (so.id == id) {
            return &so;
        }
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------- the mapping

TEST_CASE("CanvasMapping::stretch between canvases of one aspect is a uniform scale", "[geom][canvas]")
{
    const auto m = CanvasMapping::stretch(k_reference_canvas, {1280, 720});
    REQUIRE(m.x(300) == Approx(200));
    REQUIRE(m.y(540) == Approx(360));
    REQUIRE(m.width(1920) == Approx(1280));
    REQUIRE(m.height(1080) == Approx(720));
    REQUIRE(m.font_size(48) == 32);
    REQUIRE(m.length(3.0) == Approx(2.0));
    // "Same as the box" stays unset, and a positive gap never vanishes.
    REQUIRE(m.font_size(0) == 0);
    REQUIRE(m.pixels(1) == 1);
    REQUIRE(CanvasMapping::stretch({1280, 720}, {1280, 720}).is_identity());

    const auto up = CanvasMapping::stretch(k_reference_canvas, {3840, 2160});
    REQUIRE(up.font_size(48) == 96);
    REQUIRE(up.x(320) == Approx(640));
}

TEST_CASE("CanvasMapping::stretch keeps edges on another aspect and sizes type to fit", "[geom][canvas]")
{
    const auto m = CanvasMapping::stretch(k_reference_canvas, {1024, 768});
    // A full-canvas box still fills the canvas.
    REQUIRE(m.width(1920) == Approx(1024));
    REQUIRE(m.height(1080) == Approx(768));
    // Type follows the tighter axis (1024 / 1920), so it fits the narrower box.
    REQUIRE(m.length(1.0) == Approx(1024.0 / 1920.0));
    REQUIRE(m.font_size(48) == 26);
}

TEST_CASE("CanvasMapping::fit and fill centre a uniform scale", "[geom][canvas]")
{
    const CanvasSize portrait{1080, 1920};
    const auto fit = CanvasMapping::fit(k_reference_canvas, portrait);
    REQUIRE(fit.width(1920) == Approx(1080));
    REQUIRE(fit.y(0) == Approx((1920 - 1080 * 0.5625) / 2));
    REQUIRE(fit.x(0) == Approx(0));

    const auto fill = CanvasMapping::fill(k_reference_canvas, portrait);
    REQUIRE(fill.height(1080) == Approx(1920));
    REQUIRE(fill.x(0) < 0);
    REQUIRE(fill.y(0) == Approx(0));
}

TEST_CASE("canvas sizes fall back to the reference canvas", "[geom][canvas]")
{
    REQUIRE(seventhslide::geom::canvas_or_reference(0, 720) == k_reference_canvas);
    REQUIRE(seventhslide::geom::canvas_or_reference(1280, 720) == CanvasSize{1280, 720});
    REQUIRE(seventhslide::geom::same_aspect({3840, 2160}, {1280, 720}));
    REQUIRE_FALSE(seventhslide::geom::same_aspect({1024, 768}, {1280, 720}));
}

// ---------------------------------------------------------------- Freeform

TEST_CASE("map_object and map_object_json agree field for field", "[freeform][canvas]")
{
    freeform::Object obj;
    obj.id = "o1";
    obj.type = freeform::ObjectType::Shape;
    obj.x = 100;
    obj.y = 50;
    obj.w = 400;
    obj.h = 300;
    obj.font_size = 48;
    obj.upcoming_font_size = 30;
    obj.line_gap = 6;
    obj.letter_spacing = 2.5;
    obj.line.color = "#ffffff";
    obj.line.width = 3;

    for (const CanvasSize to : {CanvasSize{1280, 720}, CanvasSize{3840, 2160}, CanvasSize{1024, 768}}) {
        const auto mapping = CanvasMapping::stretch(k_reference_canvas, to);
        nlohmann::json stored;
        freeform::to_json(stored, obj);
        freeform::map_object_json(stored, mapping);

        freeform::Object mapped = obj;
        freeform::map_object(mapped, mapping);
        nlohmann::json expected;
        freeform::to_json(expected, mapped);
        REQUIRE(stored == expected);
    }
}

TEST_CASE("a deck records its canvas and fits onto another", "[freeform][canvas]")
{
    freeform::Deck deck;
    deck.output_id = "out-main";
    REQUIRE(deck.canvas == k_reference_canvas);
    freeform::Slide slide;
    freeform::Object obj;
    obj.x = 960;
    obj.w = 480;
    obj.font_size = 48;
    slide.objects.push_back(obj);
    deck.slides.push_back(slide);

    freeform::fit_deck_to_canvas(deck, {3840, 2160});
    REQUIRE(deck.canvas == CanvasSize{3840, 2160});
    REQUIRE(deck.slides[0].objects[0].x == Approx(1920));
    REQUIRE(deck.slides[0].objects[0].w == Approx(960));
    REQUIRE(deck.slides[0].objects[0].font_size == 96);
    const auto fitted = deck;
    freeform::fit_deck_to_canvas(deck, {3840, 2160});
    REQUIRE(deck == fitted);

    nlohmann::json j;
    freeform::to_json(j, deck);
    freeform::Deck back;
    freeform::from_json(j, back);
    REQUIRE(back.canvas == CanvasSize{3840, 2160});

    j.erase("canvas");
    freeform::from_json(j, back);
    REQUIRE(back.canvas == k_reference_canvas);
}

TEST_CASE("only a deck whose output names another valid canvas needs a fit", "[freeform][canvas]")
{
    freeform::Document doc;
    for (const char* id : {"out-main", "out-stage", "out-gone"}) {
        freeform::Deck deck;
        deck.output_id = id;
        doc.decks.push_back(deck);
    }

    SECTION("every deck already matches, is invalid, or has no output")
    {
        // out-main matches, out-stage is not a usable canvas, out-gone is not listed.
        const freeform::DeckCanvases canvases{{"out-main", k_reference_canvas},
                                              {"out-stage", CanvasSize{0, 720}}};
        REQUIRE(freeform::decks_fit_canvases(doc, canvases));
        const auto before = doc;
        REQUIRE_FALSE(freeform::fit_decks_to_canvases(doc, canvases));
        REQUIRE(doc == before);
    }

    SECTION("one deck on another canvas is the only one that moves")
    {
        const freeform::DeckCanvases canvases{{"out-main", k_reference_canvas},
                                              {"out-stage", CanvasSize{1280, 720}}};
        REQUIRE_FALSE(freeform::decks_fit_canvases(doc, canvases));
        REQUIRE(freeform::fit_decks_to_canvases(doc, canvases));
        REQUIRE(doc.decks[0].canvas == k_reference_canvas);
        REQUIRE(doc.decks[1].canvas == CanvasSize{1280, 720});
        REQUIRE(doc.decks[2].canvas == k_reference_canvas);
        REQUIRE(freeform::decks_fit_canvases(doc, canvases));
    }
}

TEST_CASE("new Freeform objects are proportioned for the canvas they land on", "[freeform][canvas]")
{
    auto doc = freeform::make_document("x", {"out-main"});
    const auto hd = freeform::make_text_object(doc, k_reference_canvas);
    const auto uhd = freeform::make_text_object(doc, {3840, 2160});
    REQUIRE(uhd.font_size == hd.font_size * 2);
    REQUIRE(uhd.x == Approx(hd.x * 2));
    REQUIRE(uhd.w == Approx(hd.w * 2));

    const auto clock = freeform::make_clock_object(doc, {1280, 720});
    REQUIRE(clock.font_size == 32);
    REQUIRE(clock.x + clock.w <= Approx(1280));
    const auto indicator = freeform::make_indicator_object(doc, {1280, 720});
    REQUIRE(indicator.y + indicator.h <= Approx(720));
}

TEST_CASE("Freeform playback maps each deck onto its output's canvas", "[freeform][canvas]")
{
    auto doc = freeform::make_document("x", {"out-main", "out-stage"});
    auto* main = freeform::find_deck(doc, "out-main");
    REQUIRE(main != nullptr);
    freeform::Object shape;
    shape.id = "o_shape";
    shape.type = freeform::ObjectType::Shape;
    shape.x = 960;
    shape.y = 540;
    shape.w = 400;
    shape.h = 200;
    shape.line.color = "#ffffff";
    shape.line.width = 4;
    main->slides[0].objects.push_back(shape);
    // The stage display is 720p; its deck still holds the 1080p design.
    freeform::find_deck(doc, "out-stage")->slides[0].objects.push_back(shape);

    seventhslide::model::OutputConfig main_out;
    main_out.name = "Main";
    main_out.id = "out-main";
    seventhslide::model::OutputConfig stage_out;
    stage_out.name = "Stage";
    stage_out.id = "out-stage";
    stage_out.canvas_width = 1280;
    stage_out.canvas_height = 720;

    seventhslide::build::SlideBuildInput in;
    in.kind = seventhslide::build::BuildKind::Freeform;
    in.selection.kind = seventhslide::model::ContentKind::Freeform;
    in.freeform.document = doc;
    in.outputs = {main_out, stage_out};
    seventhslide::layout::ApproxTextMeasurer measurer;
    const auto built = seventhslide::build::build_slides(in, measurer);
    REQUIRE(built.outputs.size() == 2);

    const auto& on_main = *built.outputs[0].compiled_slides.at(0).objects.at(0).object;
    REQUIRE(on_main.x == Approx(960));
    REQUIRE(on_main.line.width == Approx(4));

    const auto& on_stage = *built.outputs[1].compiled_slides.at(0).objects.at(0).object;
    REQUIRE(on_stage.x == Approx(640));
    REQUIRE(on_stage.y == Approx(360));
    REQUIRE(on_stage.w == Approx(400.0 * 2 / 3));
    REQUIRE(on_stage.line.width == Approx(4.0 * 2 / 3));
}

// ---------------------------------------------------------------- themes

TEST_CASE("fit_elements_to_canvas lays a design out on the output's canvas", "[theme][canvas]")
{
    nlohmann::json style = nlohmann::json::object();
    style["elements"] = theme::default_elements().to_json();
    style["verse_gap"] = 30;
    // A designer-only flag the C++ model does not carry must survive the fit.
    element_json(style, "song", "te_copyright")->emplace("visible", false);

    theme::fit_elements_to_canvas(style, {1280, 720});
    REQUIRE(style.at("elements").at("canvas") == nlohmann::json{{"width", 1280}, {"height", 720}});
    REQUIRE(style.at("verse_gap") == 20);

    const auto* lyrics = element_json(style, "song", "te_lyrics");
    REQUIRE(lyrics->at("x").get<double>() == Approx(320.0 * 2 / 3));
    REQUIRE(lyrics->at("w").get<double>() == Approx(1280.0 * 2 / 3));
    REQUIRE(lyrics->at("font_size") == 32);

    // The copyright notice sits at the bottom of the canvas - on the canvas.
    const auto* notice = element_json(style, "song", "te_copyright");
    REQUIRE(notice->at("y").get<double>() + notice->at("h").get<double>() <= Approx(720));
    REQUIRE(notice->at("visible") == false);

    // Idempotent for one canvas, lossless for the stock sizes on the way back.
    const auto once = style;
    theme::fit_elements_to_canvas(style, {1280, 720});
    REQUIRE(style == once);
    theme::fit_elements_to_canvas(style, k_reference_canvas);
    REQUIRE(element_json(style, "song", "te_lyrics")->at("x").get<double>() == Approx(320));
    REQUIRE(element_json(style, "song", "te_lyrics")->at("font_size") == 48);
    REQUIRE(style.at("verse_gap") == 30);
}

TEST_CASE("set_elements_canvas declares a canvas without moving anything", "[theme][canvas]")
{
    nlohmann::json style = nlohmann::json::object();
    style["elements"] = theme::default_elements().to_json();
    theme::set_elements_canvas(style, {1280, 720});
    REQUIRE(element_json(style, "song", "te_lyrics")->at("x").get<double>() == Approx(320));
    REQUIRE(theme::ElementSet::from_json(style.at("elements")).canvas == CanvasSize{1280, 720});

    // A bg theme has no design to stamp.
    nlohmann::json bg = {{"background_type", "color"}};
    theme::set_elements_canvas(bg, {1280, 720});
    REQUIRE_FALSE(bg.contains("elements"));
}

TEST_CASE("make_element sizes an inserted element for the canvas", "[theme][canvas]")
{
    const auto lyrics = theme::make_element(freeform::ObjectType::Text,
                                            theme::ElementBinding::Lyrics, "te9", {3840, 2160});
    REQUIRE(lyrics.object.font_size == 96);
    REQUIRE(lyrics.object.w == Approx(2560));
    REQUIRE(lyrics.object.x == Approx(640));

    const auto video = theme::make_element(freeform::ObjectType::Image,
                                           theme::ElementBinding::VideoArea, "te10", {1024, 768});
    REQUIRE(video.object.w == Approx(1024));
    REQUIRE(video.object.h == Approx(768));
}

TEST_CASE("the stock song design paints inside every output canvas", "[theme][canvas][scene]")
{
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::model::Song song;
    song.title = "Canvas";
    song.copyright = "Public domain";
    song.show_copyright = true;

    for (const CanvasSize canvas :
         {CanvasSize{1280, 720}, CanvasSize{1920, 1080}, CanvasSize{3840, 2160}, CanvasSize{1024, 768}}) {
        CAPTURE(canvas.width, canvas.height);
        seventhslide::model::OutputConfig oc;
        oc.name = "Main";
        oc.id = "out-main";
        oc.canvas_width = canvas.width;
        oc.canvas_height = canvas.height;
        theme::seed_default_themes(oc);

        seventhslide::build::SlideBuildInput in;
        in.kind = seventhslide::build::BuildKind::Song;
        in.selection.kind = seventhslide::model::ContentKind::Song;
        in.song.lyrics = "---[Verse:1]---\nAmazing grace\nHow sweet the sound\n";
        in.song.verse_order = "v1";
        in.outputs = {oc};
        const auto built = seventhslide::build::build_slides(in, measurer);
        REQUIRE(built.outputs.size() == 1);

        seventhslide::scene::SceneFrameRequest req{
            built.outputs[0], seventhslide::scene::SceneMode::Song, canvas.width, canvas.height,
            0, {}, &measurer,
        };
        req.extras.copyright_vars = seventhslide::scene::copyright_token_values(song, "");
        req.extras.song_copyright = true;
        const auto frame = seventhslide::scene::build_scene_frame(req);

        // The stock design is laid out for the canvas as a resize lays it out.
        const auto mapping = CanvasMapping::resize(k_reference_canvas, canvas);
        REQUIRE(frame.text.has_value());
        const auto& box = frame.text->box;
        REQUIRE(box.font_size == mapping.font_size(48));
        REQUIRE(box.x == Approx(mapping.x(320)).margin(1));
        REQUIRE(box.y == Approx(mapping.y(340)).margin(1));
        REQUIRE(box.x + box.width <= canvas.width);
        REQUIRE(box.y + box.height <= canvas.height);

        const auto* notice = scene_object(frame, "te_copyright");
        REQUIRE(notice != nullptr);
        REQUIRE(notice->text.has_value());
        REQUIRE(notice->text->font_size == mapping.font_size(20));
        REQUIRE(notice->text->y >= 0);
        REQUIRE(notice->text->y + notice->text->h <= Approx(canvas.height).margin(1));
        REQUIRE(notice->text->x + notice->text->w <= Approx(canvas.width).margin(1));
    }
}

// ---------------------------------------------------------------- a resize
//
// An output that changes canvas carries what is laid out on it over once, by
// CanvasMapping::resize and the proportion rule (canvas::resized_frame).

TEST_CASE("CanvasMapping::resize scales type by the mean of the two factors", "[geom][canvas][resize]")
{
    // The shape held: the one uniform scale, as stretch gives.
    const auto up = CanvasMapping::resize(k_reference_canvas, {3840, 2160});
    REQUIRE(up.x(320) == Approx(640));
    REQUIRE(up.width(1280) == Approx(2560));
    REQUIRE(up.font_size(48) == 96);
    REQUIRE(up.length(3.0) == Approx(6.0));

    // A turn: positions per axis, type by exactly 1.
    const auto turned = CanvasMapping::resize(k_reference_canvas, {1080, 1920});
    REQUIRE(turned.x(320) == Approx(180));
    REQUIRE(turned.y(340) == Approx(340.0 * 1920 / 1080));
    REQUIRE(turned.font_size(48) == 48);
    REQUIRE(turned.length(4.0) == Approx(4.0));

    // 16:9 to 4:3: the mean, between the two factors.
    const auto standard = CanvasMapping::resize(k_reference_canvas, {1440, 1080});
    REQUIRE(standard.length(1.0) == Approx(std::sqrt(0.75)));
    REQUIRE(standard.font_size(48) == 42);
    // Where stretch would take the smaller.
    REQUIRE(CanvasMapping::stretch(k_reference_canvas, {1440, 1080}).font_size(48) == 36);
}

TEST_CASE("a resized picture or shape keeps its proportions; a text box's frame stretches",
          "[canvas][resize]")
{
    using seventhslide::canvas::BoxRect;
    using seventhslide::canvas::resized_frame;
    const CanvasSize portrait{1080, 1920};

    SECTION("a square stays square, centred where its centre moved")
    {
        const BoxRect square{760, 340, 400, 400};
        const auto on_portrait = resized_frame(square, k_reference_canvas, portrait);
        REQUIRE(on_portrait.w == Approx(400));
        REQUIRE(on_portrait.h == Approx(400));
        REQUIRE(on_portrait.x == Approx(340));
        REQUIRE(on_portrait.y == Approx(760));
        // And comes back.
        const auto back = resized_frame(on_portrait, portrait, k_reference_canvas);
        REQUIRE(back.x == Approx(760));
        REQUIRE(back.y == Approx(340));
        REQUIRE(back.w == Approx(400));
    }
    SECTION("one that covered the canvas still does")
    {
        const auto cover = resized_frame({0, 0, 1920, 1080}, k_reference_canvas, portrait);
        REQUIRE(cover == BoxRect{0, 0, 1080, 1920});
    }
    SECTION("one too wide for the new canvas is pulled in")
    {
        const auto wide = resized_frame({160, 90, 1600, 900}, k_reference_canvas, portrait);
        REQUIRE(wide.w == Approx(1080));
        REQUIRE(wide.h == Approx(900.0 * 1080 / 1600));
        REQUIRE(wide.x == Approx(0));
        REQUIRE(wide.y == Approx(960 - wide.h / 2));
    }
    SECTION("one that was inside stays inside")
    {
        const auto corner = resized_frame({1500, 660, 400, 400}, k_reference_canvas, portrait);
        REQUIRE(corner.w == Approx(400));
        REQUIRE(corner.x == Approx(680));
        REQUIRE(corner.y + corner.h <= Approx(1920));
    }
    SECTION("the shape held: per axis is uniform")
    {
        REQUIRE(resized_frame({100, 100, 300, 200}, k_reference_canvas, {3840, 2160})
                == BoxRect{200, 200, 600, 400});
    }

    // The objects: a text box stretches per axis and keeps its type; a shape
    // keeps its proportions and its line.
    freeform::Object text;
    text.type = freeform::ObjectType::Text;
    text.x = 320;
    text.y = 340;
    text.w = 1280;
    text.h = 400;
    text.font_size = 48;
    freeform::resize_object(text, k_reference_canvas, portrait);
    REQUIRE(text.x == Approx(180));
    REQUIRE(text.w == Approx(720));
    REQUIRE(text.h == Approx(400.0 * 1920 / 1080));
    REQUIRE(text.font_size == 48);

    freeform::Object circle;
    circle.type = freeform::ObjectType::Shape;
    circle.shape_kind = seventhslide::geom::ShapeKind::Ellipse;
    circle.x = 760;
    circle.y = 340;
    circle.w = 400;
    circle.h = 400;
    circle.line.color = "#ffffff";
    circle.line.width = 4;
    nlohmann::json stored;
    freeform::to_json(stored, circle);
    freeform::resize_object(circle, k_reference_canvas, portrait);
    REQUIRE(circle.w == Approx(400));
    REQUIRE(circle.h == Approx(400));
    REQUIRE(circle.line.width == Approx(4));
    // The stored form agrees.
    freeform::resize_object_json(stored, k_reference_canvas, portrait);
    nlohmann::json expected;
    freeform::to_json(expected, circle);
    REQUIRE(stored == expected);
}

TEST_CASE("a resized deck comes back as it was, and doubles exactly for 4K",
          "[freeform][canvas][resize]")
{
    freeform::Deck deck;
    deck.output_id = "out-main";
    freeform::Slide slide;
    freeform::Object text;
    text.id = "o_text";
    text.type = freeform::ObjectType::Text;
    text.x = 320;
    text.y = 340;
    text.w = 1280;
    text.h = 400;
    text.font_size = 47;
    text.letter_spacing = 2.5;
    freeform::Object square;
    square.id = "o_square";
    square.type = freeform::ObjectType::Shape;
    square.x = 760;
    square.y = 340;
    square.w = 400;
    square.h = 400;
    square.line.color = "#ffffff";
    square.line.width = 4;
    slide.objects = {text, square};
    deck.slides.push_back(slide);
    deck.cue_flow = {{.id = "c2", .gap_above = 30}};
    const auto original = deck;

    SECTION("a turn and back")
    {
        REQUIRE(freeform::resize_deck(deck, {1080, 1920}));
        REQUIRE(deck.canvas == CanvasSize{1080, 1920});
        REQUIRE(deck.slides[0].objects[0].font_size == 47);
        REQUIRE(deck.slides[0].objects[1].w == Approx(400));
        REQUIRE(deck.cue_flow[0].gap_above == 30);
        REQUIRE_FALSE(freeform::resize_deck(deck, {1080, 1920}));

        REQUIRE(freeform::resize_deck(deck, k_reference_canvas));
        const auto& t = deck.slides[0].objects[0];
        REQUIRE(t.x == Approx(320).margin(1));
        REQUIRE(t.w == Approx(1280).margin(1));
        REQUIRE(t.font_size == 47);
        REQUIRE(t.letter_spacing == Approx(2.5));
        const auto& s = deck.slides[0].objects[1];
        REQUIRE(s.x == Approx(760).margin(1));
        REQUIRE(s.y == Approx(340).margin(1));
        REQUIRE(s.w == Approx(400).margin(1));
        REQUIRE(s.line.width == Approx(4));
        REQUIRE(deck.cue_flow[0].gap_above == 30);
    }
    SECTION("the same shape, twice the size")
    {
        REQUIRE(freeform::resize_deck(deck, {3840, 2160}));
        const auto& t = deck.slides[0].objects[0];
        REQUIRE(t.x == Approx(640));
        REQUIRE(t.w == Approx(2560));
        REQUIRE(t.font_size == 94);
        REQUIRE(t.letter_spacing == Approx(5.0));
        const auto& s = deck.slides[0].objects[1];
        REQUIRE(s.x == Approx(1520));
        REQUIRE(s.w == Approx(800));
        REQUIRE(s.line.width == Approx(8));
        REQUIRE(deck.cue_flow[0].gap_above == 60);
    }
    SECTION("not a usable canvas: nothing moves")
    {
        REQUIRE_FALSE(freeform::resize_deck(deck, {0, 1080}));
        REQUIRE(deck == original);
    }
}

TEST_CASE("resizing an output's deck moves that deck alone", "[freeform][canvas][resize]")
{
    auto doc = freeform::make_document("x", {"out-main", "out-stage"});
    freeform::Object text;
    text.id = "o1";
    text.type = freeform::ObjectType::Text;
    text.x = 320;
    text.w = 1280;
    for (auto& deck : doc.decks) {
        deck.slides[0].objects.push_back(text);
    }

    REQUIRE(freeform::resize_output_deck(doc, "out-main", {1080, 1920}));
    const auto* main = freeform::find_deck(doc, "out-main");
    REQUIRE(main->canvas == CanvasSize{1080, 1920});
    REQUIRE(main->slides[0].objects[0].x == Approx(180));
    const auto* stage = freeform::find_deck(doc, "out-stage");
    REQUIRE(stage->canvas == k_reference_canvas);
    REQUIRE(stage->slides[0].objects[0].x == Approx(320));

    // Already there, or no deck for the output: nothing.
    REQUIRE_FALSE(freeform::resize_output_deck(doc, "out-main", {1080, 1920}));
    REQUIRE_FALSE(freeform::resize_output_deck(doc, "out-gone", {1080, 1920}));
}

TEST_CASE("resize_elements carries a design over once, and back", "[theme][canvas][resize]")
{
    nlohmann::json style = nlohmann::json::object();
    style["elements"] = theme::default_elements().to_json();
    style["verse_gap"] = 30;
    element_json(style, "song", "te_copyright")->emplace("visible", false);
    const auto original = style;

    // Nothing to do on its own canvas, or without a model.
    REQUIRE_FALSE(theme::resize_elements(style, k_reference_canvas));
    nlohmann::json bare = {{"font_size", 44}};
    REQUIRE_FALSE(theme::resize_elements(bare, {1080, 1920}));
    REQUIRE(bare == nlohmann::json{{"font_size", 44}});

    REQUIRE(theme::resize_elements(style, {1080, 1920}));
    REQUIRE(style.at("elements").at("canvas") == nlohmann::json{{"width", 1080}, {"height", 1920}});
    const auto* lyrics = element_json(style, "song", "te_lyrics");
    REQUIRE(lyrics->at("x").get<double>() == Approx(180));
    REQUIRE(lyrics->at("w").get<double>() == Approx(720));
    REQUIRE(lyrics->at("font_size") == 48);
    REQUIRE(style.at("verse_gap") == 30);
    REQUIRE(element_json(style, "song", "te_copyright")->at("visible") == false);
    // The media areas still cover the canvas.
    const auto* video = element_json(style, "video", "te_video_area");
    REQUIRE(video->at("w").get<double>() == Approx(1080));
    REQUIRE(video->at("h").get<double>() == Approx(1920));

    REQUIRE(theme::resize_elements(style, k_reference_canvas));
    for (const char* ctx : {"song", "bible", "image", "video", "web", "live", "titles"}) {
        const auto& before = original.at("elements").at(ctx);
        const auto& after = style.at("elements").at(ctx);
        REQUIRE(after.size() == before.size());
        for (std::size_t i = 0; i < before.size(); ++i) {
            CAPTURE(ctx, i);
            for (const char* key : {"x", "y", "w", "h"}) {
                REQUIRE(after[i].at(key).get<double>()
                        == Approx(before[i].at(key).get<double>()).margin(1));
            }
            // A media area has no type.
            REQUIRE(after[i].value("font_size", 0) == before[i].value("font_size", 0));
        }
    }
    REQUIRE(style.at("verse_gap") == 30);

    // Twice the size: every length doubled.
    REQUIRE(theme::resize_elements(style, {3840, 2160}));
    REQUIRE(element_json(style, "song", "te_lyrics")->at("font_size") == 96);
    REQUIRE(element_json(style, "song", "te_lyrics")->at("w").get<double>() == Approx(2560));
    REQUIRE(style.at("verse_gap") == 60);
}

TEST_CASE("the stock design has a portrait variant", "[theme][canvas][resize]")
{
    const auto landscape = theme::default_elements();
    REQUIRE(landscape == theme::default_elements(k_reference_canvas));
    REQUIRE(landscape.canvas == k_reference_canvas);

    const auto portrait = theme::default_elements({1080, 1920});
    REQUIRE(portrait.canvas == CanvasSize{1080, 1920});
    REQUIRE(portrait.song.size() == landscape.song.size());
    const auto& lyrics = portrait.song.at(0);
    REQUIRE(lyrics.binding == theme::ElementBinding::Lyrics);
    // Spans the width, at the landscape's type.
    REQUIRE(lyrics.object.w == Approx(900));
    REQUIRE(lyrics.object.x == Approx(90));
    REQUIRE(lyrics.object.font_size == 48);
    REQUIRE(lyrics.object.y + lyrics.object.h <= Approx(1920));
    const auto& notice = portrait.song.at(1);
    REQUIRE(notice.object.y + notice.object.h <= Approx(1920));
    REQUIRE(notice.object.x + notice.object.w <= Approx(1080));
    REQUIRE(notice.object.font_size == 20);
    for (const auto* list : {&portrait.image, &portrait.video, &portrait.web, &portrait.live}) {
        REQUIRE(list->size() == 1);
        for (const auto& area : *list) {
            REQUIRE(area.object.w == Approx(1080));
            REQUIRE(area.object.h == Approx(1920));
        }
    }
    REQUIRE(portrait.bible.at(0).object.w == Approx(900));

    // A taller portrait canvas gets it at scale.
    const auto tall_4k = theme::default_elements({2160, 3840});
    REQUIRE(tall_4k.song.at(0).object.font_size == 96);
    REQUIRE(tall_4k.song.at(0).object.w == Approx(1800));

    // A square canvas gets the landscape design, carried over.
    const auto square = theme::default_elements({1080, 1080});
    REQUIRE(square.canvas == CanvasSize{1080, 1080});
    REQUIRE(square.song.at(0).object.w == Approx(720));
    REQUIRE(square.song.at(0).object.font_size == 36);

    // A new theme on a portrait output starts from it.
    seventhslide::model::OutputConfig oc;
    oc.name = "Sign";
    oc.id = "out-sign";
    oc.canvas_width = 1080;
    oc.canvas_height = 1920;
    theme::seed_default_themes(oc);
    const auto seeded = theme::ElementSet::from_json(oc.song_themes.at(0).style.at("elements"));
    REQUIRE(seeded.canvas == CanvasSize{1080, 1920});
    REQUIRE(seeded.song.at(0).object.w == Approx(900));
    REQUIRE(seeded.song.at(0).object.font_size == 48);
}

TEST_CASE("an inserted element is laid out for a portrait canvas too", "[theme][canvas][resize]")
{
    const auto lyrics = theme::make_element(freeform::ObjectType::Text,
                                            theme::ElementBinding::Lyrics, "te9", {1080, 1920});
    REQUIRE(lyrics.object.font_size == 48);
    REQUIRE(lyrics.object.w == Approx(720));
    REQUIRE(lyrics.object.x == Approx(180));

    // A shape keeps its proportions, centred.
    const auto shape = theme::make_element(freeform::ObjectType::Shape,
                                           theme::ElementBinding::Static, "te10", {1080, 1920});
    REQUIRE(shape.object.w == Approx(480));
    REQUIRE(shape.object.h == Approx(270));
    REQUIRE(shape.object.x + shape.object.w / 2 == Approx(540));
    REQUIRE(shape.object.y + shape.object.h / 2 == Approx(960).margin(1));

    // And a new Freeform picture.
    auto doc = freeform::make_document("x", {"out-main"});
    const auto hd = freeform::make_image_object(doc, "a.png", k_reference_canvas);
    const auto tall = freeform::make_image_object(doc, "a.png", {1080, 1920});
    REQUIRE(tall.w / tall.h == Approx(hd.w / hd.h));
    REQUIRE(tall.x >= 0);
    REQUIRE(tall.x + tall.w <= Approx(1080));
}

TEST_CASE("document_has_deck reads the stored decks", "[freeform][canvas]")
{
    const auto doc = freeform::make_document("x", {"out-main", "out-stage"}).to_json();
    REQUIRE(freeform::document_has_deck(doc, "out-main"));
    REQUIRE(freeform::document_has_deck(doc, "out-stage"));
    REQUIRE_FALSE(freeform::document_has_deck(doc, "out-gone"));
    REQUIRE_FALSE(freeform::document_has_deck(nlohmann::json::object(), "out-main"));
    REQUIRE_FALSE(freeform::document_has_deck(nlohmann::json{{"decks", 3}}, "out-main"));
}
