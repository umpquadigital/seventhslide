#include <catch2/catch_test_macros.hpp>

#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/theme/theme_elements.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using seventhslide::freeform::ObjectType;
using seventhslide::model::OutputConfig;
using seventhslide::theme::Element;
using seventhslide::theme::ElementBinding;
using seventhslide::theme::ElementContext;
using seventhslide::theme::ElementSet;
using seventhslide::theme::ElementConditions;
using seventhslide::theme::ElementVisibility;
using seventhslide::theme::ShowWhen;
using seventhslide::theme::SlideRule;
using seventhslide::theme::resolve_styles;
using seventhslide::theme::default_elements;
using seventhslide::theme::element_model;
using seventhslide::devsupport::elements_from_flat_style;
using seventhslide::theme::fit_elements_to_canvas;
using seventhslide::theme::has_elements;
using seventhslide::theme::has_clock;
using seventhslide::theme::make_element;
using seventhslide::theme::rule_covers_slide;
using seventhslide::theme::visible_elements;

namespace {

[[nodiscard]] const Element* find_id(const std::vector<Element>& list, std::string_view id)
{
    for (const auto& el : list) {
        if (el.object.id == id) {
            return &el;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("default_elements is the stock design, written out", "[theme][elements][defaults]")
{
    const auto set = default_elements();
    REQUIRE(set.canvas == seventhslide::geom::k_reference_canvas);

    REQUIRE(set.song.size() == 2);
    const auto* lyrics = find_id(set.song, "te_lyrics");
    REQUIRE(lyrics != nullptr);
    REQUIRE(lyrics->binding == ElementBinding::Lyrics);
    REQUIRE(lyrics->object.x == 320);
    REQUIRE(lyrics->object.y == 340);
    REQUIRE(lyrics->object.w == 1280);
    REQUIRE(lyrics->object.h == 400);
    REQUIRE(lyrics->object.font_family == "Liberation Sans");
    REQUIRE(lyrics->object.font_size == 48);
    REQUIRE(lyrics->object.font_color == "#ffffff");
    REQUIRE(lyrics->object.text_align == "center");

    const auto* notice = find_id(set.song, "te_copyright");
    REQUIRE(notice != nullptr);
    REQUIRE(notice->object.type == ObjectType::Text);
    REQUIRE(notice->binding == ElementBinding::Static);
    REQUIRE(notice->object.text == std::string(seventhslide::theme::k_default_copyright_template));
    REQUIRE(notice->show_when == ShowWhen::SongCopyrightOn);
    REQUIRE(notice->object.z > lyrics->object.z);

    REQUIRE(set.bible.size() == 2);
    REQUIRE(find_id(set.bible, "te_bible_text")->binding == ElementBinding::BibleText);
    const auto* ref = find_id(set.bible, "te_bible_ref");
    REQUIRE(ref != nullptr);
    REQUIRE(ref->object.text == std::string(seventhslide::theme::k_default_bible_ref_template));

    // Each media type's area fills the canvas, in its own context.
    REQUIRE(set.video.size() == 1);
    const auto* video = find_id(set.video, "te_video_area");
    REQUIRE(video != nullptr);
    REQUIRE(video->object.w == 1920);
    REQUIRE(video->object.h == 1080);
    REQUIRE(find_id(set.image, "te_image_area")->object.object_fit == "contain");
    REQUIRE(find_id(set.web, "te_web_area")->binding == ElementBinding::WebArea);
    REQUIRE(find_id(set.web, "te_web_area")->object.w == 1920);
    REQUIRE(find_id(set.live, "te_live_area")->binding == ElementBinding::VideoArea);

    // The notice paints under the title boxes too, on every slide.
    REQUIRE(set.titles.size() == 1);
    REQUIRE(set.titles[0].object.id == "te_title_copyright");
    REQUIRE(set.titles[0].visibility.rule == SlideRule::All);
    REQUIRE(set.titles[0].show_when == ShowWhen::SongCopyrightOn);
}

TEST_CASE("a style with no design takes its body boxes from its flat keys", "[theme][elements][flat]")
{
    const nlohmann::json style = {
        {"box_x", 100},
        {"width_px", 800},
        {"font_family", "Liberation Sans"},
        {"font_size", 44},
        {"align", "left"},
        {"highlight_color", "#ffcc00"},
        {"other_lines", {{"color", "#333333"}}},
        {"bible_text_box_x", 60},
        {"bible_main_font_family", ""},
        {"video_area_width", 0},
        {"image_enabled", false},
    };
    const auto set = elements_from_flat_style(style);

    const auto* lyrics = find_id(set.song, "te_lyrics");
    REQUIRE(lyrics != nullptr);
    REQUIRE(lyrics->object.x == 100);
    REQUIRE(lyrics->object.w == 800);
    REQUIRE(lyrics->object.font_family == "Liberation Sans");
    REQUIRE(lyrics->object.font_size == 44);
    REQUIRE(lyrics->object.text_align == "left");
    // The song template paints the body in highlight_color.
    REQUIRE(lyrics->object.font_color == "#ffcc00");
    REQUIRE(lyrics->body.other_lines.color == "#333333");

    // An unset passage font follows the song font.
    const auto* passage = find_id(set.bible, "te_bible_text");
    REQUIRE(passage != nullptr);
    REQUIRE(passage->object.x == 60);
    REQUIRE(passage->object.font_family == "Liberation Sans");

    // 0 means the (reference) canvas; a switched-off area is simply absent.
    const auto* video = find_id(set.video, "te_video_area");
    REQUIRE(video != nullptr);
    REQUIRE(video->object.w == 1920);
    REQUIRE(video->object.h == 1080);
    REQUIRE(find_id(set.image, "te_image_area") == nullptr);

    // Chrome has no flat keys: it is always the stock notice and reference.
    REQUIRE(find_id(set.song, "te_copyright") != nullptr);
    REQUIRE(find_id(set.song, "te_copyright")->object.font_family == "Liberation Sans");
    REQUIRE(find_id(set.bible, "te_bible_ref") != nullptr);
}

TEST_CASE("projection writes the bound elements onto the body fields", "[theme][elements][fold]")
{
    nlohmann::json style = {
        {"canvas_width", 1920},
        {"canvas_height", 1080},
        {"box_x", 100},
        {"width_px", 800},
        {"font_size", 44},
        {"show_lyrics", true},
    };
    fit_elements_to_canvas(style, {1920, 1080});
    REQUIRE(has_elements(style));

    // Move the body box in the element model; the resolved song box follows.
    auto blob = style.at("elements");
    for (auto& el : blob.at("song")) {
        if (el.at("id") == "te_lyrics") {
            el["x"] = 400;
            el["w"] = 1000;
            el["font_size"] = 60;
        }
    }
    style["elements"] = blob;

    const auto theme = resolve_styles(style, {}, {1920, 1080});
    REQUIRE(theme.song.x == 400);
    REQUIRE(theme.song.width == 1000);
    REQUIRE(theme.song.font_size == 60);
    REQUIRE(theme.song.shown);
}

TEST_CASE("a deleted body element leaves the body switched off", "[theme][elements][fold]")
{
    nlohmann::json style = {{"show_lyrics", true}, {"box_x", 100}};
    fit_elements_to_canvas(style, {1920, 1080});
    auto blob = style.at("elements");
    blob["song"] = nlohmann::json::array();
    style["elements"] = blob;

    const auto theme = resolve_styles(style, {}, {1920, 1080});
    REQUIRE_FALSE(theme.song.shown);
}

TEST_CASE("slide rules select which slides an element paints on", "[theme][elements][visibility]")
{
    ElementVisibility vis;
    REQUIRE(rule_covers_slide(vis, 0, 5));
    REQUIRE(rule_covers_slide(vis, 4, 5));

    vis.rule = SlideRule::First;
    vis.count = 2;
    REQUIRE(rule_covers_slide(vis, 0, 5));
    REQUIRE(rule_covers_slide(vis, 1, 5));
    REQUIRE_FALSE(rule_covers_slide(vis, 2, 5));

    vis.rule = SlideRule::Last;
    REQUIRE(rule_covers_slide(vis, 4, 5));
    REQUIRE(rule_covers_slide(vis, 3, 5));
    REQUIRE_FALSE(rule_covers_slide(vis, 2, 5));

    vis.rule = SlideRule::ExceptFirst;
    REQUIRE_FALSE(rule_covers_slide(vis, 0, 5));
    REQUIRE(rule_covers_slide(vis, 2, 5));

    vis.rule = SlideRule::ExceptLast;
    REQUIRE(rule_covers_slide(vis, 0, 5));
    REQUIRE_FALSE(rule_covers_slide(vis, 4, 5));
}

TEST_CASE("visible_elements applies the slide rule and z order", "[theme][elements][visibility]")
{
    ElementSet set;
    auto top = make_element(ObjectType::Text, ElementBinding::Static, "te10", {1920, 1080});
    top.object.z = 50;
    auto bottom = make_element(ObjectType::Shape, ElementBinding::Static, "te11", {1920, 1080});
    bottom.object.z = 5;
    auto first_only = make_element(ObjectType::Text, ElementBinding::Static, "te13", {1920, 1080});
    first_only.object.z = 90;
    first_only.visibility.rule = SlideRule::First;
    first_only.visibility.count = 1;
    set.song = {top, bottom, first_only};

    nlohmann::json style = nlohmann::json::object();
    style["elements"] = set.to_json();

    const auto on_first = visible_elements(ElementSet::from_json(element_model(style)).at(ElementContext::Song), 0, 4);
    REQUIRE(on_first.size() == 3);
    REQUIRE(on_first.front().object.id == "te11");  // z 5 paints first
    REQUIRE(on_first.back().object.id == "te13");   // z 90 paints last

    const auto later = visible_elements(ElementSet::from_json(element_model(style)).at(ElementContext::Song), 2, 4);
    REQUIRE(later.size() == 2);
}

TEST_CASE("element sets round-trip through JSON", "[theme][elements][json]")
{
    ElementSet set;
    auto text = make_element(ObjectType::Text, ElementBinding::Static, "te1", {1920, 1080});
    text.object.text = "CCLI License #{-ccli-license}";
    text.label = "Licence";
    text.visibility.rule = SlideRule::Last;
    text.visibility.count = 3;
    auto body = make_element(ObjectType::Text, ElementBinding::Lyrics, "te2", {1920, 1080});
    body.body.other_lines.color = "#101010";
    body.body.other_lines.italic = true;
    body.body.active_line = {.family = "Georgia", .size = 64, .bold = true, .color = "#ffcc00"};
    body.body.show_chords = true;
    body.body.chords = {.size_pct = 90, .gap_pct = 20, .color = "#00ccff"};
    // A clock has no markup to carry weight and slant, so its object flags have
    // to survive the round trip.
    auto clock = make_element(ObjectType::Clock, ElementBinding::Static, "te3", {1920, 1080});
    clock.object.bold = true;
    clock.object.italic = true;
    clock.object.clock_24h = true;
    set.song = {text, body, clock};
    set.next_id = 4;

    const auto j = set.to_json();
    const auto back = ElementSet::from_json(j);
    REQUIRE(back.song.size() == 3);
    REQUIRE(back.song[0].object.text == "CCLI License #{-ccli-license}");
    REQUIRE(back.song[0].label == "Licence");
    REQUIRE(back.song[0].visibility.rule == SlideRule::Last);
    REQUIRE(back.song[0].visibility.count == 3);
    REQUIRE(back.song[1].binding == ElementBinding::Lyrics);
    REQUIRE(back.song[1].body.other_lines.color == "#101010");
    REQUIRE(back.song[1].body.other_lines.italic == true);
    REQUIRE_FALSE(back.song[1].body.other_lines.bold.has_value());
    REQUIRE(back.song[1].body.active_line == body.body.active_line);
    REQUIRE(back.song[1].body.show_chords);
    REQUIRE(back.song[1].body.chords
            == seventhslide::layout::ChordStyle{.size_pct = 90, .gap_pct = 20, .color = "#00ccff"});
    REQUIRE(back.song[2].object.bold);
    REQUIRE(back.song[2].object.italic);
    REQUIRE(back.song[2].object.clock_24h);
    REQUIRE(back.next_id >= 4);
}

TEST_CASE("has_clock arms the repaint tick only when a theme places one", "[theme][elements][clock]")
{
    nlohmann::json off = nlohmann::json::object();
    fit_elements_to_canvas(off, {1920, 1080});
    // The stock design places no clock anywhere.
    REQUIRE_FALSE(has_clock(ElementSet::from_json(element_model(off))));

    auto set = default_elements();
    set.bible.push_back(make_element(ObjectType::Clock, ElementBinding::Static, "te1", {1920, 1080}));
    nlohmann::json on = nlohmann::json::object();
    on["elements"] = set.to_json();
    REQUIRE(has_clock(ElementSet::from_json(element_model(on))));
}

TEST_CASE("default_elements gives a fresh theme a body per content type", "[theme][elements][defaults]")
{
    const auto set = default_elements();
    nlohmann::json style = nlohmann::json::object();
    style["elements"] = set.to_json();

    const auto has_binding = [&style](ElementContext ctx, ElementBinding binding) {
        const auto list = ElementSet::from_json(element_model(style)).at(ctx);
        return std::any_of(list.begin(), list.end(),
                           [binding](const auto& el) { return el.binding == binding; });
    };
    REQUIRE(has_binding(ElementContext::Song, ElementBinding::Lyrics));
    REQUIRE(has_binding(ElementContext::Bible, ElementBinding::BibleText));
    REQUIRE(has_binding(ElementContext::Video, ElementBinding::VideoArea));
    REQUIRE(has_binding(ElementContext::Live, ElementBinding::VideoArea));
    REQUIRE(has_binding(ElementContext::Web, ElementBinding::WebArea));
    REQUIRE(has_binding(ElementContext::Image, ElementBinding::ImageArea));
    // A fresh theme is what the defaults describe: the body and a copyright
    // line on songs, the passage and its reference on Bible. Everything else the
    // designer inserts on purpose.
    const auto song = ElementSet::from_json(element_model(style)).at(ElementContext::Song);
    REQUIRE(song.size() == 2);
    REQUIRE(song[1].object.type == ObjectType::Text);
    REQUIRE(song[1].binding == ElementBinding::Static);
    REQUIRE_FALSE(song[1].object.text.empty());
}


namespace {

[[nodiscard]] bool has_element(const std::vector<Element>& list, std::string_view id)
{
    for (const auto& el : list) {
        if (el.id() == id) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("the copyright notice paints only when the song opts in", "[theme][elements][show_when]")
{
    nlohmann::json style = nlohmann::json::object();
    fit_elements_to_canvas(style, {1920, 1080});

    // The stock notice declares itself part of the copyright; the body box
    // does not, so a song that opts out never blanks its own lyrics.
    const auto song = ElementSet::from_json(element_model(style)).at(ElementContext::Song);
    const Element* notice = nullptr;
    const Element* body = nullptr;
    for (const auto& el : song) {
        if (el.id() == "te_copyright") {
            notice = &el;
        }
        if (el.binding == ElementBinding::Lyrics) {
            body = &el;
        }
    }
    REQUIRE(notice != nullptr);
    REQUIRE(body != nullptr);
    REQUIRE(notice->show_when == ShowWhen::SongCopyrightOn);
    REQUIRE(body->show_when == ShowWhen::Always);

    ElementConditions off;
    ElementConditions on;
    on.song_copyright = true;

    const auto hidden = visible_elements(ElementSet::from_json(element_model(style)).at(ElementContext::Song), 0, 1, off);
    const auto shown = visible_elements(ElementSet::from_json(element_model(style)).at(ElementContext::Song), 0, 1, on);
    REQUIRE_FALSE(has_element(hidden, "te_copyright"));
    REQUIRE(has_element(shown, "te_copyright"));
    // Unconditioned elements are untouched by the toggle either way.
    REQUIRE(has_element(hidden, "te_lyrics"));
    REQUIRE(has_element(shown, "te_lyrics"));
}

TEST_CASE("a caller that forgets the conditions hides the notice rather than showing it", "[theme][elements][show_when]")
{
    nlohmann::json style = nlohmann::json::object();
    fit_elements_to_canvas(style, {1920, 1080});
    // Default-constructed conditions grant nothing: printing a copyright for a
    // song that opted out is the worse of the two failures.
    REQUIRE_FALSE(has_element(visible_elements(ElementSet::from_json(element_model(style)).at(ElementContext::Song), 0, 1),
                              "te_copyright"));
}

TEST_CASE("show_when round-trips through the element blob", "[theme][elements][show_when]")
{
    Element marked = make_element(ObjectType::Text, ElementBinding::Static, "te9", {1920, 1080});
    marked.show_when = ShowWhen::SongCopyrightOn;
    const nlohmann::json j = marked;
    REQUIRE(j.at("show_when") == "song_copyright");
    REQUIRE(j.get<Element>().show_when == ShowWhen::SongCopyrightOn);

    // ``Always`` is the default and stays out of the blob.
    const Element plain =
        make_element(ObjectType::Text, ElementBinding::Static, "te10", {1920, 1080});
    const nlohmann::json pj = plain;
    REQUIRE_FALSE(pj.contains("show_when"));
    REQUIRE(pj.get<Element>().show_when == ShowWhen::Always);
}

TEST_CASE("Vertical alignment reads as top, middle or bottom", "[theme][elements]")
{
    // One set of spellings, so the designer's Vertical menu (top / middle /
    // bottom) can always show what a box is set to.
    const auto defaults = default_elements();
    REQUIRE(find_id(defaults.song, "te_lyrics") != nullptr);
    CHECK(find_id(defaults.song, "te_lyrics")->object.vertical_align == "middle");
    REQUIRE(find_id(defaults.bible, "te_bible_text") != nullptr);
    CHECK(find_id(defaults.bible, "te_bible_text")->object.vertical_align == "middle");

    const auto flat = elements_from_flat_style({{"valign", "center"}, {"bible_text_valign", "center"}});
    CHECK(find_id(flat.song, "te_lyrics")->object.vertical_align == "middle");
    CHECK(find_id(flat.bible, "te_bible_text")->object.vertical_align == "middle");
    const auto bottom = elements_from_flat_style({{"valign", "bottom"}});
    CHECK(find_id(bottom.song, "te_lyrics")->object.vertical_align == "bottom");

    const auto stored = [](const char* align) {
        Element el;
        seventhslide::theme::from_json(
            nlohmann::json{{"id", "te_x"}, {"type", "text"}, {"vertical_align", align}}, el);
        return el.object.vertical_align;
    };
    CHECK(stored("bottom") == "bottom");
    CHECK(stored("center") == "middle");
    CHECK(stored("baseline") == "middle");
    // Empty keeps the object type's own default (a clock's is top).
    CHECK(stored("").empty());
}

TEST_CASE("an element's lines read their own types", "[theme][elements][json]")
{
    ElementSet set;
    set.song = {make_element(ObjectType::Text, ElementBinding::Lyrics, "te2", {1920, 1080})};
    set.next_id = 3;
    auto j = set.to_json();
    auto& body = j["song"][0]["body"];
    REQUIRE(body.is_object());

    // Neither type: the defaults (the other lines dimmed).
    body.erase("active_line");
    body.erase("other_lines");
    auto back = ElementSet::from_json(j);
    REQUIRE(back.song[0].body.active_line.empty());
    REQUIRE(back.song[0].body.other_lines.color == seventhslide::theme::k_default_dim_color);

    // Each type as stored; an empty colour there is the box's own, not the
    // dim default.
    body["active_line"] = {{"size", 72}, {"underline", true}};
    body["other_lines"] = {{"color", ""}, {"italic", false}};
    back = ElementSet::from_json(j);
    REQUIRE(back.song[0].body.active_line.size == 72);
    REQUIRE(back.song[0].body.active_line.underline == true);
    REQUIRE(back.song[0].body.other_lines.color.empty());
    REQUIRE(back.song[0].body.other_lines.italic == false);
}

TEST_CASE("a resize carries the lines' own sizes, leaving the box's alone", "[theme][elements][canvas]")
{
    auto el = make_element(ObjectType::Text, ElementBinding::Lyrics, "te2", {1920, 1080});
    el.object.font_size = 48;
    el.body.active_line.size = 60;
    el.body.other_lines.size = 0;
    seventhslide::theme::resize_element(el, {1920, 1080}, {3840, 2160});
    REQUIRE(el.object.font_size == 96);
    REQUIRE(el.body.active_line.size == 120);
    REQUIRE(el.body.other_lines.size == 0);
}

TEST_CASE("each content type has its own transition", "[theme][transitions][contexts]")
{
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    // Every content type fades.
    nlohmann::json all = nlohmann::json::object();
    for (const auto ctx : seventhslide::theme::k_content_contexts) {
        all[std::string(seventhslide::theme::to_string(ctx))] =
            Transition{TransitionType::Fade, 300};
    }
    nlohmann::json style = {{"transitions", all}};
    auto b = seventhslide::theme::read_theme_behavior(style);
    for (const auto ctx : seventhslide::theme::k_content_contexts) {
        CHECK(b.transition(ctx) == Transition{TransitionType::Fade, 300});
    }

    // Set on one, it is that one's alone.
    seventhslide::theme::set_context_transition(style, ElementContext::Video,
                                                Transition{TransitionType::Cut, 300});
    b = seventhslide::theme::read_theme_behavior(style);
    CHECK(b.transition(ElementContext::Video).type == TransitionType::Cut);
    CHECK(b.transition(ElementContext::Song) == Transition{TransitionType::Fade, 300});
    CHECK(b.transition(ElementContext::Live) == Transition{TransitionType::Fade, 300});
    // A title slide is a song slide.
    CHECK(b.transition(ElementContext::Titles) == b.transition(ElementContext::Song));
    seventhslide::theme::set_context_transition(style, ElementContext::Titles,
                                                Transition{TransitionType::Fade, 800});
    b = seventhslide::theme::read_theme_behavior(style);
    CHECK(b.transition(ElementContext::Song).duration_ms == 800);
    CHECK(b.transition(ElementContext::Bible).duration_ms == 300);

    // Nothing set anywhere: a cut. The first one set writes the rest out as
    // what they read as.
    nlohmann::json bare = nlohmann::json::object();
    CHECK_FALSE(seventhslide::theme::read_theme_behavior(bare)
                    .transition(ElementContext::Image)
                    .plays());
    seventhslide::theme::set_context_transition(bare, ElementContext::Web,
                                                Transition{TransitionType::Fade, 200});
    CHECK(bare.at("transitions").size() == std::size(seventhslide::theme::k_content_contexts));
    b = seventhslide::theme::read_theme_behavior(bare);
    CHECK(b.transition(ElementContext::Web) == Transition{TransitionType::Fade, 200});
    CHECK_FALSE(b.transition(ElementContext::Image).plays());
}
