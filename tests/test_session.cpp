#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scene_test_helpers.hpp"
#include "counting_player.hpp"
#include "theme_test_helpers.hpp"
#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/devsupport/scene_dump.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/session/session.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <fstream>
#include <string>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::devsupport::serialize_output_layout;
using seventhslide::model::ContentKind;
using seventhslide::model::OutputConfig;
using seventhslide::model::ServiceItem;
using seventhslide::model::ThemeEntry;
using seventhslide::session::LiveFreeformContent;
using seventhslide::session::LiveMediaContent;
using seventhslide::model::Song;
using seventhslide::session::Session;
using seventhslide::session::SessionMode;
using seventhslide::devsupport::bg_override_present_flags;
using seventhslide::devsupport::overlay_nonempty_flags;
using seventhslide::test::element_text;
using seventhslide::test::load_json;
using seventhslide::test::oc_from_oracle;

namespace {

}  // namespace


namespace {

using seventhslide::test::oracle_dir;

[[nodiscard]] std::string first_line_text(const seventhslide::scene::SceneFrame& frame)
{
    if (!frame.text || frame.text->lines.empty()) {
        return {};
    }
    return seventhslide::model::plain_text(frame.text->lines[0].runs);
}

} // namespace

TEST_CASE("Session play_song matches the song fixtures end to end", "[session][oracle]")
{
    const auto manifest = load_json(oracle_dir() / "manifest.json");
    ApproxTextMeasurer measurer;

    for (const auto& id_j : manifest.at("fixtures")) {
        const auto id = id_j.get<std::string>();
        INFO(id);
        const auto oracle = load_json(oracle_dir() / (id + ".json"));

        Session session(measurer);
        session.set_outputs({oc_from_oracle(oracle.at("outputs").at(0))});

        Song song;
        song.title = oracle.at("input").value("title", "");
        song.lyrics = oracle.at("input").at("lyrics").get<std::string>();
        song.verse_order = oracle.at("input").value("verse_order", "");
        session.play_song(song);
        REQUIRE(session.mode() == SessionMode::Song);
        REQUIRE(session.has_build());

        REQUIRE(session.focused_stack().program.player().total_lines()
                == oracle.at("player").at("total_lines").get<int>());
        REQUIRE(session.focused_stack().program.player().all_lines()
                == oracle.at("player").at("all_lines").get<std::vector<std::string>>());

        const auto& rt = session.focused_stack().program.last_build().outputs.at(0);
        const auto& out = session.outputs()[0];
        const auto serialized = serialize_output_layout(
            out.name,
            out.canvas_width,
            out.canvas_height,
            rt.theme,
            rt.layout,
            overlay_nonempty_flags(rt),
            bg_override_present_flags(rt),
            session.focused_stack().program.player().total_lines());
        REQUIRE(serialized.at("groups") == oracle.at("outputs").at(0).at("groups"));
        REQUIRE(serialized.at("line_to_slide")
                == oracle.at("outputs").at(0).at("line_to_slide"));
    }
}

// A rundown song carries its theme chips in its snapshot; each run of its play
// order takes its chip's picks over the service's, and the service's where a
// chip leaves one to it.
TEST_CASE("Session plays a rundown song's theme chips over the service's themes",
          "[session][theme_chips]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.song_themes = {ThemeEntry{"t1", "T", seventhslide::test::flat_style({{"font_size", 56}})},
                      ThemeEntry{"t2", "T2", seventhslide::test::flat_style({{"font_size", 72}})}};
    oc.bg_themes = {ThemeEntry{"base", "Base", {{"background_color", "#101010"}}},
                    ThemeEntry{"svc", "Service", {{"background_color", "#202020"}}},
                    ThemeEntry{"dusk", "Dusk", {{"background_color", "#303030"}}}};
    oc.category_defaults = {{"song", {{"theme", "t1"}, {"bg", "base"}}}};
    session.set_outputs({oc});

    ServiceItem song_item;
    song_item.item_type = "song";
    song_item.item_id = 1;
    song_item.data = {
        {"title", "Chips"},
        {"lyrics", "---[Verse:1]---\nOne\n---[Verse:2]---\nTwo\n"},
        {"verse_order", "TH1 v1 TH2 v2"},
        {"theme_map",
         {{"active", "a1"},
          {"arrangements",
           json::array({{{"id", "a1"},
                         {"name", "Default"},
                         {"order", "TH1 v1 TH2 v2"},
                         {"chips",
                          {{"TH1", {{"out-main", {{"song", {{"bg", "dusk"}}}}}}},
                           {"TH2", {{"out-main", {{"song", {{"theme", "t2"}}}}}}}}}}})}}},
    };
    session.open_service(1,
                         seventhslide::theme::service_themes_from_json(
                             json{{"out-main", {{"song", {{"bg", "svc"}}}}}}),
                         {song_item});
    REQUIRE(session.select_service_item(0));

    const auto& rt = session.focused_stack().program.last_build().outputs.front();
    const int last = rt.slide_count() - 1;
    REQUIRE(last >= 1);
    REQUIRE(rt.segment_themes.size() == 2);
    CHECK(rt.theme_for_slide(0).background.color == "#303030");
    CHECK(rt.theme_for_slide(0).song.font_size == 56);
    CHECK(rt.theme_for_slide(last).background.color == "#202020");
    CHECK(rt.theme_for_slide(last).song.font_size == 72);
}

TEST_CASE("Session service select + nav + blank commands", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"width_px", 1000}, {"height_px", 800}});
    oc.song_themes = {ThemeEntry{"t1", "T", seventhslide::test::flat_style({{"font_size", 56}})}};
    oc.bg_themes = {ThemeEntry{"b1", "B", {{"background_color", "#000000"}}}};
    oc.category_defaults = {{"song", {{"theme", "t1"}, {"bg", "b1"}}}};
    session.set_outputs({oc});

    ServiceItem song_item;
    song_item.item_type = "song";
    song_item.item_id = 1;
    song_item.data = {
        {"title", "Hi"},
        {"lyrics", "---[Verse:1]---\nOne\n[---][---]\nTwo\n[---][---]\nThree\n"},
        {"verse_order", "v1"},
        {"theme_map", json::object()},
    };

    auto doc = seventhslide::freeform::make_document("Welcome", {"out-main"});
    seventhslide::freeform::Object text;
    text.type = seventhslide::freeform::ObjectType::Text;
    text.text = "{title}";
    doc.decks[0].slides[0].objects.push_back(text);
    doc.fields["title"] = "Hello";
    seventhslide::freeform::normalize(doc);

    ServiceItem freeform_item;
    freeform_item.item_type = std::string{seventhslide::model::k_item_type_freeform};
    freeform_item.item_id = 2;
    freeform_item.data = {{"name", "Welcome"}, {"document", doc.to_json()}};

    session.open_service(10,
                         seventhslide::theme::service_themes_from_json(
                             json{{"out-main", {{"song", {{"bg", "b1"}}}}}}),
                         {song_item, freeform_item});
    REQUIRE(session.mode() == SessionMode::Service);
    REQUIRE(session.service_items().size() == 2);
    REQUIRE(session.current_service_item() == nullptr);

    REQUIRE(session.select_service_item(0));
    REQUIRE(session.current_service_item()->title() == "Hi");
    REQUIRE(session.focused_stack().program.player().total_lines() >= 3);
    REQUIRE(session.focused_stack().program.last_build().outputs.front().theme.song.font_size == 56);

    REQUIRE(session.focused_stack().program.next_slide());
    const int mid = session.focused_stack().program.player().line_cursor();
    REQUIRE(mid > 0);
    REQUIRE(session.focused_stack().program.jump_to_line(0));
    REQUIRE(session.focused_stack().program.player().line_cursor() == 0);

    // Global blank sets the output's own toggle, so its button lifts it.
    session.toggle_global_blank();
    REQUIRE(session.output_is_blank("out-main"));
    REQUIRE(session.global_blank());
    REQUIRE(session.toggle_output_blank("out-main"));
    REQUIRE_FALSE(session.output_is_blank("out-main"));
    REQUIRE_FALSE(session.global_blank());

    REQUIRE(session.select_service_item(1));
    REQUIRE(session.focused_stack().program.last_build().kind == seventhslide::build::BuildKind::Freeform);
    REQUIRE(session.focused_stack().program.player().all_line_labels() == std::vector<std::string>{"1"});
    REQUIRE_FALSE(session.focused_stack().program.last_build().outputs[0].compiled_slides[0].objects.empty());

    REQUIRE_FALSE(session.select_service_item(99));
}

TEST_CASE("An output switched off is left out of the build; media clears slides", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);

    OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    seventhslide::test::set_look(main, {{"width_px", 800}, {"height_px", 600}});
    OutputConfig fold;
    fold.name = "Fold";
    fold.id = "out-fold";
    seventhslide::test::set_look(fold, {{"width_px", 800}, {"height_px", 600}});
    session.set_outputs({main, fold});

    Song song;
    song.lyrics = "---[Verse:1]---\nA\n[---][---]\nB\n";
    song.verse_order = "v1";
    session.play_song(song);
    const int lines = session.focused_stack().program.player().total_lines();
    REQUIRE(lines >= 2);
    REQUIRE(session.focused_stack().program.player().outputs().size() == 2);

    // Off, it is not built or played, and the live content stays where it is.
    REQUIRE(session.focused_stack().program.jump_to_line(1));
    REQUIRE(session.set_output_on("out-fold", false));
    REQUIRE_FALSE(session.output_is_on("out-fold"));
    REQUIRE(session.focused_stack().program.last_build().outputs.size() == 1);
    REQUIRE(session.focused_stack().program.player().outputs().size() == 1);
    REQUIRE(session.focused_stack().program.player().outputs()[0].output_id == "out-main");
    REQUIRE(session.focused_stack().program.player().line_cursor() == 1);
    REQUIRE(session.focused_stack().program.player().total_lines() == lines);
    // Its settings are untouched.
    REQUIRE(session.outputs().size() == 2);
    REQUIRE(session.outputs()[1].name == "Fold");

    // The last one on stays on.
    REQUIRE_FALSE(session.set_output_on("out-main", false));
    REQUIRE(session.output_is_on("out-main"));

    REQUIRE(session.set_output_on("out-fold", true));
    REQUIRE(session.output_is_on("out-fold"));
    REQUIRE(session.focused_stack().program.player().outputs().size() == 2);
    REQUIRE(static_cast<int>(session.focused_stack().program.player().outputs()[1].line_to_slide.size()) == lines);

    // Every output off at once (a library opening) leaves the first on.
    session.set_outputs_off({"out-main", "out-fold"});
    REQUIRE(session.output_is_on("out-main"));
    REQUIRE_FALSE(session.output_is_on("out-fold"));
    REQUIRE(session.primary_output()->id == "out-main");
    session.set_outputs_off({});

    session.play_media({ContentKind::Video, {{"filename", "a.mp4"}}});
    REQUIRE(session.mode() == SessionMode::Video);
    REQUIRE(session.focused_stack().program.player().total_lines() == 0);
    REQUIRE(session.focused_stack().program.last_build().outputs[0].slide_count() == 0);
}

TEST_CASE("Session play_bible and clear_all_stacks", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"show_bible_text", true}, {"bible_text_box_width", 800}, {"bible_text_box_height", 400}, {"bible_main_font_size", 40}});
    session.set_outputs({oc});

    seventhslide::model::BiblePassage bible;
    bible.chapter = 1;
    bible.verses = {{1, "In the beginning."}};
    session.play_bible(bible);
    REQUIRE(session.mode() == SessionMode::Bible);
    REQUIRE(session.focused_stack().program.player().total_lines() >= 1);
    REQUIRE(session.focused_stack().program.player().all_line_labels()[0] == "1:1");

    session.clear_all_stacks();
    REQUIRE(session.mode() == SessionMode::Empty);
    REQUIRE(session.focused_stack().program.player().total_lines() == 0);
}

TEST_CASE("Session bible chrome and scene extras", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"show_bible_text", true}, {"bible_text_box_width", 800}, {"bible_text_box_height", 400}, {"bible_main_font_size", 40}});
    session.set_outputs({oc});

    seventhslide::model::BiblePassage bible;
    bible.chapter = 3;
    bible.verses = {{16, "For God so loved the world."}};
    bible.ref = "John 3:16";
    bible.version = "KJV";
    session.play_bible(bible);
    REQUIRE(session.focused_stack().program.bible_mode_active());
    REQUIRE(session.focused_stack().program.scene_extras().bible_ref_vars.at("reference") == "John 3:16");
    REQUIRE(session.focused_stack().program.scene_extras().bible_ref_vars.at("version") == "KJV");

    const auto frames = session.scene_frames();
    REQUIRE_FALSE(frames.empty());
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Bible);
    REQUIRE(element_text(frames[0], "te_bible_ref") == "John 3:16\nKJV");
    REQUIRE(frames[0].text.has_value());

    session.clear_all_stacks();
    REQUIRE(session.focused_stack().program.scene_extras().bible_ref_vars.empty());
}

TEST_CASE("Session image playlist + scene extras + nav", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.canvas_width = 800;
    oc.canvas_height = 600;
    seventhslide::test::set_look(oc, {{"image_enabled", true}, {"image_fit", "contain"}});
    session.set_outputs({oc});

    session.play_media(
        {ContentKind::ImageFolder,
         {{"folder_id", 1},
          {"folder_name", "Slides"},
          {"images", nlohmann::json::array({"a.png", "b.png", "c.png"})}}});
    REQUIRE(session.mode() == SessionMode::Image);
    REQUIRE(session.focused_stack().program.image_mode_active());
    REQUIRE(session.focused_stack().program.image_playlist().count() == 3);
    REQUIRE(session.focused_stack().program.image_playlist().index == 0);
    REQUIRE(session.focused_stack().program.scene_extras().image.has_value());
    REQUIRE(session.focused_stack().program.scene_extras().image->url == "/static/images/a.png");
    REQUIRE(session.focused_stack().program.scene_extras().image->count == 3);

    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Image);
    REQUIRE(frames[0].image.has_value());
    REQUIRE(frames[0].image->url == "/static/images/a.png");
    REQUIRE_FALSE(frames[0].text.has_value());

    REQUIRE(session.focused_stack().program.next_slide());
    REQUIRE(session.focused_stack().program.image_playlist().index == 1);
    REQUIRE(session.focused_stack().program.scene_extras().image->url == "/static/images/b.png");
    REQUIRE(session.focused_stack().program.prev_slide());
    REQUIRE(session.focused_stack().program.image_playlist().index == 0);
    REQUIRE_FALSE(session.focused_stack().program.prev_slide());
    REQUIRE(session.focused_stack().program.jump_to_line(2));
    REQUIRE(session.focused_stack().program.image_playlist().index == 2);
    REQUIRE_FALSE(session.focused_stack().program.next_slide());

    session.set_global_blank(true);
    frames = session.scene_frames();
    REQUIRE(frames[0].is_blank);
    REQUIRE_FALSE(frames[0].image.has_value());
}

TEST_CASE("Session service image item loads playlist", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    ServiceItem img;
    img.item_type = "image";
    img.data = {{"filename", "solo.jpg"}};

    ServiceItem folder;
    folder.item_type = "image_folder";
    folder.data = {{"folder_name", "Pack"},
                   {"images", nlohmann::json::array({"x.png", "y.png"})}};

    session.open_service(1, {}, {img, folder});
    REQUIRE(session.select_service_item(0));
    REQUIRE(session.focused_stack().program.image_mode_active());
    REQUIRE(session.focused_stack().program.image_playlist().count() == 1);
    REQUIRE(session.focused_stack().program.scene_extras().image->url == "/static/images/solo.jpg");

    REQUIRE(session.select_service_item(1));
    REQUIRE(session.focused_stack().program.image_playlist().count() == 2);
    REQUIRE(session.focused_stack().program.image_playlist().folder_name == "Pack");
    REQUIRE(session.focused_stack().program.next_slide());
    REQUIRE(session.focused_stack().program.image_playlist().index == 1);
}

TEST_CASE("A rundown item's own picks are the content tier, for its own category only",
          "[session][theme]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    // Each media theme is told apart by how a picture comes in.
    const auto media_theme = [](std::string id, int ms) {
        return ThemeEntry{id, id,
                          {{"transitions", {{"image", {{"type", "fade"}, {"duration_ms", ms}}}}}}};
    };
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    oc.media_themes = {media_theme("m-default", 100), media_theme("m-service", 200),
                       media_theme("m-item", 300)};
    oc.category_defaults = {{"media", {{"theme", "m-default"}}}};
    session.set_outputs({oc});

    ServiceItem own;
    own.item_type = "image";
    own.data = {{"filename", "a.jpg"},
                {"theme_map", {{"out-main", {{"media", {{"theme", "m-item"}}}}}}}};
    // A picture takes no song theme: its map's song pick is not what it plays under.
    ServiceItem other;
    other.item_type = "image";
    other.data = {{"filename", "b.jpg"},
                  {"theme_map", {{"out-main", {{"song", {{"theme", "m-item"}}}}}}}};
    session.open_service(1,
                         seventhslide::theme::service_themes_from_json(
                             json{{"out-main", {{"media", {{"theme", "m-service"}}}}}}),
                         {own, other});

    const auto image_ms = [&] {
        return session.focused_stack()
            .program.last_build()
            .outputs.front()
            .theme.behavior.transition(seventhslide::theme::ElementContext::Image)
            .duration_ms;
    };
    REQUIRE(session.select_service_item(0));
    CHECK(image_ms() == 300);
    REQUIRE(session.select_service_item(1));
    CHECK(image_ms() == 200);
}

TEST_CASE("Session video extras + play/pause", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);

    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"video_enabled", true}});
    auto design = seventhslide::test::design_of(oc);
    seventhslide::test::add_element(design.video, seventhslide::test::countdown_element());
    seventhslide::test::use_design(oc, design);
    session.set_outputs({oc});

    session.play_media(
        {ContentKind::Video,
         {{"filename", "clip.mp4"}, {"title", "Clip"}, {"loop", false}, {"autoplay", true}}});
    REQUIRE(session.mode() == SessionMode::Video);
    REQUIRE(session.focused_stack().program.video_mode_active());
    REQUIRE(session.focused_stack().program.playback().filename == "clip.mp4");
    REQUIRE(session.focused_stack().program.playback().is_playing);
    REQUIRE(session.focused_stack().program.scene_extras().video.has_value());
    REQUIRE(session.focused_stack().program.scene_extras().video->url == "/static/videos/clip.mp4");
    REQUIRE(session.focused_stack().program.scene_extras().video->autoplay);

    // No decode backend injected - Session still tracks play flags.
    REQUIRE(session.focused_stack().program.media_play());
    REQUIRE(session.focused_stack().program.playback().is_playing);
    REQUIRE(session.focused_stack().program.scene_extras().video->autoplay);

    REQUIRE(session.focused_stack().program.media_pause());
    REQUIRE_FALSE(session.focused_stack().program.playback().is_playing);

    REQUIRE(session.focused_stack().program.media_seek(5.0));
    REQUIRE(session.focused_stack().program.playback().position_sec == Catch::Approx(5.0));

    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Video);
    REQUIRE(frames[0].video.has_value());
    REQUIRE_FALSE(frames[0].text.has_value());
}

TEST_CASE("Session copyright + indicator chrome", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"width_px", 1000}, {"height_px", 800}});
    auto design = seventhslide::test::design_of(oc);
    seventhslide::test::add_element(design.song, seventhslide::test::indicator_element());
    nlohmann::json t1_style = {{"font_size", 48}};
    t1_style["elements"] = design.to_json();
    oc.song_themes = {ThemeEntry{"t1", "T", t1_style}};
    oc.bg_themes = {ThemeEntry{"b1", "B", {{"background_color", "#000000"}}}};
    oc.category_defaults = {{"song", {{"theme", "t1"}, {"bg", "b1"}}}};
    session.set_outputs({oc});
    session.set_ccli_licence_number("999");

    Song song;
    song.title = "Hi";
    song.lyrics = "---[Verse:1]---\nOne\n[---][---]\nTwo\n---[Chorus:1]---\nSing\n";
    song.verse_order = "v1 c1";
    song.show_copyright = true;
    song.authors = {"Writer"};
    song.copyright = "© Test";
    session.play_song(std::move(song));

    REQUIRE(session.focused_stack().program.scene_extras().copyright_vars.at("authors") == "Writer");
    REQUIRE(session.focused_stack().program.scene_extras().copyright_vars.at("copyright") == "© Test");
    REQUIRE(session.focused_stack().program.scene_extras().copyright_vars.at("ccli-license") == "999");

    auto frames = session.scene_frames();
    REQUIRE_FALSE(frames.empty());
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Song);
    REQUIRE(element_text(frames[0], "te_copyright").find("Writer") != std::string::npos);
    REQUIRE_FALSE(element_text(frames[0], "te_indicator").empty());
    REQUIRE(element_text(frames[0], "te_indicator").find("V1") != std::string::npos);

    session.clear_all_stacks();
    REQUIRE(session.focused_stack().program.scene_extras().copyright_vars.empty());
}

TEST_CASE("Session fills the licence on a song's title slide", "[session][copyright][title]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"width_px", 1000}, {"height_px", 800}});
    auto design = seventhslide::test::design_of(oc);
    seventhslide::test::set_copyright_text(design, "Licence {ccli-license}");
    seventhslide::test::use_design(oc, design);
    session.set_outputs({oc});
    session.set_ccli_licence_number("111");

    Song song;
    song.title = "Hi";
    song.lyrics = "---[Verse:1]---\nOne\n";
    song.verse_order = "";  // the song asks for a title slide
    song.show_copyright = true;
    session.play_song(std::move(song));

    const auto title_text = [&] {
        const auto frames = session.scene_frames();
        REQUIRE_FALSE(frames.empty());
        std::string out;
        for (const auto& box : frames[0].overlay.boxes) {
            for (const auto& line : box.lines) {
                for (const auto& span : line) {
                    out += span.text;
                }
            }
        }
        return out;
    };
    REQUIRE(title_text() == "Licence 111");

    // Changing the setting under a live song reaches its title slide too.
    session.set_ccli_licence_number("222");
    REQUIRE(title_text() == "Licence 222");
}

TEST_CASE("Session play_freeform simple two outputs", "[session][freeform]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    OutputConfig lt;
    lt.name = "Lower Third";
    lt.id = "out-lt";
    OutputConfig hidden;
    hidden.name = "Fold";
    hidden.id = "out-fold";
    session.set_outputs({main, lt, hidden});

    std::ifstream in(std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "freeform"
                     / "simple_two_outputs.json");
    REQUIRE(in);
    json j;
    in >> j;

    LiveFreeformContent live;
    live.name = "Welcome";
    live.document = seventhslide::freeform::Document::from_json(j);
    session.play_freeform(std::move(live));
    REQUIRE(session.mode() == SessionMode::Freeform);
    REQUIRE(session.focused_stack().program.freeform_mode_active());
    REQUIRE(session.focused_stack().program.last_build().kind == seventhslide::build::BuildKind::Freeform);
    REQUIRE(session.focused_stack().program.player().total_lines() == 3);
    REQUIRE(session.focused_stack().program.player().all_line_labels() == std::vector<std::string>{"1", "2", "3"});

    REQUIRE(session.focused_stack().program.last_build().outputs[0].line_to_slide == std::vector<int>({0, 1, 2}));
    REQUIRE(session.focused_stack().program.last_build().outputs[1].line_to_slide == std::vector<int>({0, 1, 2}));
    REQUIRE(session.focused_stack().program.last_build().outputs[2].slide_overlays[0].empty());

    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 3);
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Freeform);
    REQUIRE(seventhslide::model::plain_text(frames[0].objects[0].text->lines[0]) == "Sunday Worship");
    REQUIRE(frames[1].objects.empty());
    REQUIRE(frames[2].objects.empty());

    REQUIRE(session.focused_stack().program.jump_to_line(2));
    frames = session.scene_frames();
    REQUIRE(seventhslide::model::plain_text(frames[0].objects[0].text->lines[0]) == "Main only — slot 3");
    REQUIRE(seventhslide::model::plain_text(frames[1].objects[0].text->lines[0]) == "LT slot 3");
    const auto at = session.scene_frame_at({session.focused_stack_id()}, "out-main", 0);
    REQUIRE(at.has_value());
    REQUIRE(seventhslide::model::plain_text(at->objects[0].text->lines[0]) == "Sunday Worship");
}

TEST_CASE("Session play_freeform line-control fortress spans", "[session][freeform]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    OutputConfig lt;
    lt.name = "Lower Third";
    lt.id = "out-lt";
    session.set_outputs({main, lt});

    std::ifstream in(std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "freeform"
                     / "line_control_fortress.json");
    REQUIRE(in);
    json j;
    in >> j;

    LiveFreeformContent live;
    live.document = seventhslide::freeform::Document::from_json(j);
    session.play_freeform(std::move(live));
    REQUIRE(session.focused_stack().program.player().total_lines() == 8);
    REQUIRE(session.focused_stack().program.last_build().outputs[0].line_to_slide
            == std::vector<int>({0, 0, 0, 0, 1, 1, 1, 1}));
    REQUIRE(session.focused_stack().program.last_build().outputs[1].line_to_slide
            == std::vector<int>({0, 0, 1, 1, 2, 2, 3, 3}));
    REQUIRE(session.focused_stack().program.last_build().outputs[0].slide_count() == 2);
    REQUIRE(session.focused_stack().program.last_build().outputs[1].slide_count() == 4);

    auto frames = session.scene_frames();
    REQUIRE(frames[0].objects[0].text->lines.size() == 4);
    REQUIRE(frames[1].objects[0].text->lines.size() == 2);

    REQUIRE(session.focused_stack().program.jump_to_line(4));
    frames = session.scene_frames();
    REQUIRE(session.focused_stack().program.player().outputs()[0].index == 1);
    REQUIRE(session.focused_stack().program.player().outputs()[1].index == 2);
    REQUIRE(frames[0].objects.empty());
    REQUIRE(frames[1].objects.empty());
}

TEST_CASE("Session freeze holds the paint packet while the cursor moves", "[session]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);

    OutputConfig main;
    main.name = "Main";
    main.id = "out-main";
    seventhslide::test::set_look(main, {{"width_px", 1000}, {"height_px", 800}});
    OutputConfig fold;
    fold.name = "Fold";
    fold.id = "out-fold";
    seventhslide::test::set_look(fold, {{"width_px", 800}, {"height_px", 600}});
    fold.exempt_from_global_freeze = true;
    session.set_outputs({main, fold});

    Song song;
    song.lyrics = "---[Verse:1]---\nOne\n[---][---]\nTwo\n[---][---]\nThree\n";
    song.verse_order = "v1";
    session.play_song(song);
    REQUIRE(session.focused_stack().program.player().total_lines() >= 3);

    const auto first = session.scene_frames();
    REQUIRE(first.size() == 2);
    REQUIRE_FALSE(first[0].is_frozen);
    const int frozen_slide = first[0].slide_index;
    const auto frozen_text = first_line_text(first[0]);
    REQUIRE_FALSE(frozen_text.empty());

    session.toggle_global_freeze();
    REQUIRE(session.global_freeze());
    REQUIRE(session.output_is_frozen("out-main"));
    REQUIRE_FALSE(session.output_is_frozen("out-fold"));

    REQUIRE(session.focused_stack().program.next_slide());
    REQUIRE(session.focused_stack().program.player().line_cursor() > 0);

    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0].is_frozen);
    REQUIRE(frames[0].slide_index == frozen_slide);
    REQUIRE(first_line_text(frames[0]) == frozen_text);
    REQUIRE_FALSE(frames[1].is_frozen);
    REQUIRE(frames[1].slide_index != frozen_slide);

    const int live_index = session.focused_stack().program.player().outputs()[0].index;
    REQUIRE(live_index != frozen_slide);
    const auto thumb = session.scene_frame_at({session.focused_stack_id()}, "out-main", live_index);
    REQUIRE(thumb.has_value());
    REQUIRE_FALSE(thumb->is_frozen);
    REQUIRE(thumb->slide_index == live_index);
    REQUIRE(first_line_text(*thumb) != frozen_text);

    session.set_global_blank(true);
    frames = session.scene_frames();
    REQUIRE(frames[0].is_frozen);
    REQUIRE(frames[0].is_blank);
    REQUIRE_FALSE(frames[0].text.has_value());
    REQUIRE(frames[0].background == first[0].background);

    session.set_global_blank(false);
    frames = session.scene_frames();
    REQUIRE(frames[0].is_frozen);
    REQUIRE_FALSE(frames[0].is_blank);
    REQUIRE(first_line_text(frames[0]) == frozen_text);

    session.set_global_freeze(false);
    frames = session.scene_frames();
    REQUIRE_FALSE(frames[0].is_frozen);
    REQUIRE(frames[0].slide_index == live_index);
    REQUIRE(first_line_text(frames[0]) == first_line_text(*thumb));

    REQUIRE(session.focused_stack().program.jump_to_line(0));
    REQUIRE(session.toggle_output_freeze("out-main"));
    REQUIRE(session.focused_stack().program.next_slide());
    frames = session.scene_frames();
    REQUIRE(frames[0].is_frozen);
    REQUIRE(first_line_text(frames[0]) == frozen_text);
    REQUIRE_FALSE(frames[1].is_frozen);

    Song other;
    other.lyrics = "---[Verse:1]---\nAlpha\n[---][---]\nBeta\n";
    other.verse_order = "v1";
    session.play_song(other);
    frames = session.scene_frames();
    REQUIRE(frames[0].is_frozen);
    REQUIRE(first_line_text(frames[0]) == frozen_text);

    // Global freeze off lifts the output's own freeze too: they are one toggle.
    session.set_global_freeze(false);
    frames = session.scene_frames();
    REQUIRE_FALSE(frames[0].is_frozen);
    REQUIRE(first_line_text(frames[0]) == "Alpha");
}

TEST_CASE("Session composites two occupied stacks back to front", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Atmosphere", "video");
    REQUIRE(added.ok);
    REQUIRE(seventhslide::model::reorder_stacks(
                stacks, {added.id, std::string(seventhslide::model::k_default_stack_id)})
                .ok);
    session.set_stacks(stacks, session.type_defaults());

    LiveMediaContent media;
    media.kind = seventhslide::model::ContentKind::Image;
    media.data = {{"filename", "loop.jpg"}};
    session.play_media(std::move(media), added.id);

    Song song;
    song.title = "Hymn";
    song.lyrics = "---[Verse:1]---\nPraise\n";
    song.verse_order = "v1";
    session.play_song(std::move(song), std::string(seventhslide::model::k_default_stack_id));

    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Song);
    REQUIRE(frames[0].stacked_below.size() == 1);
    REQUIRE(frames[0].stacked_below[0]->mode == seventhslide::scene::SceneMode::Image);
    REQUIRE(frames[0].stacked_below[0]->background.type != seventhslide::layout::BackgroundType::Transparent);

    REQUIRE(session.occupied_stacks().size() == 2);
    REQUIRE(session.clear_stack(added.id));
    REQUIRE(session.occupied_stacks().size() == 1);
    frames = session.scene_frames();
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].stacked_below.empty());
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Song);
}

TEST_CASE("Session reaches one side of a stack by its slot", "[session][stacks]")
{
    using seventhslide::session::Slot;
    using seventhslide::session::StackSide;

    ApproxTextMeasurer measurer;
    Session session(measurer);
    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Lower thirds", "titles");
    REQUIRE(added.ok);
    session.set_stacks(stacks, session.type_defaults());

    const auto* stack = session.find_stack(added.id);
    REQUIRE(stack != nullptr);
    CHECK(stack->id() == added.id);
    CHECK(session.runtime({added.id, StackSide::Program}) == &stack->program);
    CHECK(session.runtime(Slot{added.id}) == &stack->program);
    // Preview is not on for it, so there is no such side.
    CHECK(stack->side(StackSide::Preview) == nullptr);
    CHECK(session.runtime({added.id, StackSide::Preview}) == nullptr);
    CHECK(session.runtime({"no such stack", StackSide::Program}) == nullptr);
}

TEST_CASE("Session gives the stacks named a Preview, and takes it from the rest",
          "[session][stacks][preview]")
{
    using seventhslide::session::StackSide;
    const std::string main_id(seventhslide::model::k_default_stack_id);

    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});
    auto stacks = session.stack_defs();
    auto lower = seventhslide::model::add_stack(stacks, "Lower thirds", "titles");
    REQUIRE(lower.ok);
    session.set_stacks(stacks, session.type_defaults());
    REQUIRE(session.preview_stack_ids().empty());

    // An id naming no stack is passed over.
    CHECK(session.set_preview_stacks({main_id, "no such stack"}));
    CHECK(session.preview_stack_ids() == std::set<std::string, std::less<>>{main_id});
    const auto* preview = session.runtime({main_id, StackSide::Preview});
    REQUIRE(preview != nullptr);
    CHECK(preview->id == main_id);
    CHECK_FALSE(preview->occupied());
    CHECK(session.runtime({lower.id, StackSide::Preview}) == nullptr);
    // The same again changes nothing.
    CHECK_FALSE(session.set_preview_stacks({main_id}));

    // Something live and something cued. The outputs show only the live one.
    Song song;
    song.title = "Be Thou My Vision";
    song.lyrics = "---[Verse:1]---\nBe thou my vision\n";
    song.verse_order = "v1";
    session.play_song(song, main_id, seventhslide::session::Land::Live);
    auto* stack = session.find_stack(main_id);
    song.title = "The Church's One Foundation";
    song.lyrics = "---[Verse:1]---\nThe church's one foundation\n";
    session.play_song(song, main_id);
    REQUIRE(stack->preview->occupied());
    REQUIRE(session.occupied_stacks().size() == 1);

    // The stack list edited: the stack kept keeps its Preview and what is
    // cued in it; one added has none.
    auto more = session.stack_defs();
    auto added = seventhslide::model::add_stack(more, "Atmosphere", "video");
    REQUIRE(added.ok);
    session.set_stacks(more, session.type_defaults());
    stack = session.find_stack(main_id);
    REQUIRE(stack->preview);
    CHECK(stack->preview->occupied());
    CHECK(session.runtime({added.id, StackSide::Preview}) == nullptr);

    // Taken away, what was cued goes with it; what is live stays.
    CHECK(session.set_preview_stacks({lower.id}));
    CHECK(session.preview_stack_ids() == std::set<std::string, std::less<>>{lower.id});
    CHECK(session.runtime({main_id, StackSide::Preview}) == nullptr);
    CHECK(session.find_stack(main_id)->program.occupied());
    const auto frames = session.scene_frames();
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].mode == seventhslide::scene::SceneMode::Song);

    // A stack deleted takes its Preview with it.
    auto fewer = session.stack_defs();
    auto defaults = session.type_defaults();
    REQUIRE(seventhslide::model::remove_stack(fewer, lower.id, defaults).ok);
    session.set_stacks(fewer, defaults);
    CHECK(session.preview_stack_ids().empty());
}

TEST_CASE("Session output enabled_stack_ids selects layers", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(added.ok);
    REQUIRE(seventhslide::model::reorder_stacks(
                stacks, {added.id, std::string(seventhslide::model::k_default_stack_id)})
                .ok);
    session.set_stacks(stacks, session.type_defaults());

    OutputConfig sanctuary;
    sanctuary.name = "Sanctuary";
    sanctuary.enabled_stack_ids = {std::string(seventhslide::model::k_default_stack_id)};
    OutputConfig foyer;
    foyer.name = "Foyer";
    foyer.enabled_stack_ids = {added.id};
    session.set_outputs({sanctuary, foyer});

    Song song;
    song.lyrics = "---[Verse:1]---\nPraise\n";
    song.verse_order = "v1";
    session.play_song(std::move(song), std::string(seventhslide::model::k_default_stack_id));

    LiveMediaContent media;
    media.kind = seventhslide::model::ContentKind::Image;
    media.data = {{"filename", "loop.jpg"}};
    session.play_media(std::move(media), added.id);

    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0].output_name == "Sanctuary");
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Song);
    REQUIRE(frames[0].stacked_below.empty());
    REQUIRE(frames[1].output_name == "Foyer");
    REQUIRE(frames[1].mode == seventhslide::scene::SceneMode::Image);
    REQUIRE(frames[1].stacked_below.empty());
}

TEST_CASE("Session output disabled_content_types leaves those layers out", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Titles", "titles");
    REQUIRE(added.ok);
    session.set_stacks(stacks, session.type_defaults());
    const std::string main_id(seventhslide::model::k_default_stack_id);

    OutputConfig sanctuary;
    sanctuary.name = "Sanctuary";
    OutputConfig thirds;
    thirds.name = "Lower Thirds";
    thirds.disabled_content_types = {"video", "image"};
    session.set_outputs({sanctuary, thirds});

    LiveMediaContent media;
    media.kind = seventhslide::model::ContentKind::Video;
    media.data = {{"filename", "clip.mp4"}};
    session.play_media(std::move(media), main_id);

    // Only what it leaves out is on air: an empty frame there, the video elsewhere.
    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].mode == seventhslide::scene::SceneMode::Video);
    CHECK(frames[1].mode == seventhslide::scene::SceneMode::Empty);

    // A song above it shows on both; only one has the video under it.
    Song song;
    song.lyrics = "---[Verse:1]---\nPraise\n";
    song.verse_order = "v1";
    session.play_song(std::move(song), added.id);
    frames = session.scene_frames();
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].mode == seventhslide::scene::SceneMode::Song);
    REQUIRE(frames[0].stacked_below.size() == 1);
    CHECK(frames[0].stacked_below[0]->mode == seventhslide::scene::SceneMode::Video);
    CHECK(frames[1].mode == seventhslide::scene::SceneMode::Song);
    CHECK(frames[1].stacked_below.empty());

    // An image folder is an image.
    LiveMediaContent folder;
    folder.kind = seventhslide::model::ContentKind::ImageFolder;
    folder.data = {{"filename", "loop.jpg"}};
    session.play_media(std::move(folder), main_id);
    frames = session.scene_frames();
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].stacked_below.size() == 1);
    CHECK(frames[1].stacked_below.empty());
}

TEST_CASE("Session output disabled_content_types holds for a frozen stack", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    session.set_outputs({oc});
    const std::string main_id(seventhslide::model::k_default_stack_id);

    LiveMediaContent media;
    media.kind = seventhslide::model::ContentKind::Image;
    media.data = {{"filename", "loop.jpg"}};
    session.play_media(std::move(media), main_id);
    REQUIRE(session.toggle_stack_freeze(main_id));
    REQUIRE(session.scene_frames().at(0).mode == seventhslide::scene::SceneMode::Image);

    // What the stack holds is what is judged, whatever it plays meanwhile.
    Song song;
    song.lyrics = "---[Verse:1]---\nPraise\n";
    song.verse_order = "v1";
    session.play_song(std::move(song), main_id);
    session.outputs().front().disabled_content_types = {"song"};
    CHECK(session.scene_frames().at(0).mode == seventhslide::scene::SceneMode::Image);
    session.outputs().front().disabled_content_types = {"image"};
    CHECK(session.scene_frames().at(0).mode == seventhslide::scene::SceneMode::Empty);
}

TEST_CASE("Session frozen output leaves a type out at once, and holds it for coming back",
          "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    LiveMediaContent media;
    media.kind = seventhslide::model::ContentKind::Image;
    media.data = {{"filename", "loop.jpg"}};
    session.play_media(std::move(media), std::string(seventhslide::model::k_default_stack_id));
    REQUIRE(session.toggle_output_freeze("out-main"));
    REQUIRE(session.scene_frames().at(0).mode == seventhslide::scene::SceneMode::Image);

    session.outputs().front().disabled_content_types = {"image"};
    auto frame = session.scene_frames().at(0);
    CHECK(frame.mode == seventhslide::scene::SceneMode::Empty);
    CHECK(frame.is_frozen);
    session.outputs().front().disabled_content_types = {};
    CHECK(session.scene_frames().at(0).mode == seventhslide::scene::SceneMode::Image);
}

TEST_CASE("Session video background plays unless a content video is on top of that output",
          "[session][stacks]")
{
    using seventhslide::layout::BackgroundType;
    using seventhslide::scene::SceneMode;
    ApproxTextMeasurer measurer;
    Session session(measurer);

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Top", "titles");
    REQUIRE(added.ok);
    session.set_stacks(stacks, session.type_defaults());
    const std::string back_id(seventhslide::model::k_default_stack_id);

    OutputConfig sanctuary;
    sanctuary.name = "Sanctuary";
    sanctuary.bg_themes = {ThemeEntry{"b1",
                                      "Motion",
                                      {{"background_type", "video"},
                                       {"background_video", "/static/loop.mp4"},
                                       {"background_color", ""}}}};
    sanctuary.category_defaults = {{"song", {{"bg", "b1"}}}};
    OutputConfig thirds = sanctuary;
    thirds.name = "Lower Thirds";
    thirds.disabled_content_types = {"video"};
    session.set_outputs({sanctuary, thirds});

    const auto song = [] {
        Song s;
        s.lyrics = "---[Verse:1]---\nPraise\n";
        s.verse_order = "v1";
        return s;
    };
    const auto video = [] {
        LiveMediaContent media;
        media.kind = seventhslide::model::ContentKind::Video;
        media.data = {{"filename", "clip.mp4"}};
        return media;
    };

    // The video on top has the output's picture to itself. The output leaving
    // it out keeps the moving background behind its song.
    session.play_song(song(), back_id);
    session.play_media(video(), added.id);
    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].mode == SceneMode::Video);
    CHECK(frames[0].background.type != BackgroundType::Video);
    CHECK(frames[1].mode == SceneMode::Song);
    CHECK(frames[1].background.type == BackgroundType::Video);
    CHECK(frames[1].background.video == "/static/loop.mp4");

    // Alone on the output, the same.
    REQUIRE(session.clear_stack(back_id));
    frames = session.scene_frames();
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0].stacked_below.empty());
    CHECK(frames[0].background.type != BackgroundType::Video);
    CHECK(frames[0].background.video.empty());

    REQUIRE(session.clear_stack(added.id));

    // Under a song, the song's background is on top of it, and still plays.
    session.play_media(video(), back_id);
    session.play_song(song(), added.id);
    frames = session.scene_frames();
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].mode == SceneMode::Song);
    REQUIRE(frames[0].stacked_below.size() == 1);
    CHECK(frames[0].background.type == BackgroundType::Video);
}

TEST_CASE("Session select_service_item leaves other stacks sticky", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(added.ok);
    REQUIRE(seventhslide::model::reorder_stacks(
                stacks, {added.id, std::string(seventhslide::model::k_default_stack_id)})
                .ok);
    session.set_stacks(stacks, session.type_defaults());

    LiveMediaContent media;
    media.kind = seventhslide::model::ContentKind::Image;
    media.data = {{"filename", "loop.jpg"}};
    session.play_media(std::move(media), added.id);

    ServiceItem song;
    song.item_type = "song";
    song.data = {{"title", "Hymn"},
                 {"lyrics", "---[Verse:1]---\nOne\n"},
                 {"verse_order", "v1"}};
    session.open_service(1, {}, {song});
    // open_service clears stacks - re-send atmosphere after the rundown is loaded.
    LiveMediaContent again;
    again.kind = seventhslide::model::ContentKind::Image;
    again.data = {{"filename", "loop.jpg"}};
    session.play_media(std::move(again), added.id);
    REQUIRE(session.select_service_item(0));

    REQUIRE(session.occupied_stacks().size() == 2);
    auto frames = session.scene_frames();
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Song);
    REQUIRE(frames[0].stacked_below.size() == 1);
    REQUIRE(frames[0].stacked_below[0]->mode == seventhslide::scene::SceneMode::Image);
}

TEST_CASE("Session next_slide_on and select keep focus", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(added.ok);
    REQUIRE(seventhslide::model::reorder_stacks(
                stacks, {added.id, std::string(seventhslide::model::k_default_stack_id)})
                .ok);
    session.set_stacks(stacks, session.type_defaults());

    Song lobby_song;
    lobby_song.lyrics = "---[Verse:1]---\nOne\n[---][---]\nTwo\n[---][---]\nThree\n";
    lobby_song.verse_order = "v1";
    session.play_song(lobby_song, added.id);
    REQUIRE(session.focused_stack_id() == added.id);
    REQUIRE(session.focused_stack().program.player().line_cursor() == 0);

    Song main_song;
    main_song.lyrics = "---[Verse:1]---\nAlpha\n[---][---]\nBeta\n";
    main_song.verse_order = "v1";
    session.play_song(main_song, std::string(seventhslide::model::k_default_stack_id));
    REQUIRE(session.focused_stack_id() == seventhslide::model::k_default_stack_id);

    REQUIRE(session.next_slide_on({added.id}));
    REQUIRE(session.runtime({added.id})->player().line_cursor() > 0);
    REQUIRE(session.focused_stack_id() == seventhslide::model::k_default_stack_id);
    REQUIRE(session.jump_to_line_on({added.id}, 0));
    REQUIRE(session.runtime({added.id})->player().line_cursor() == 0);

    ServiceItem item;
    item.item_type = "song";
    item.data = {{"title", "LobbyLoop"},
                 {"lyrics", "---[Verse:1]---\nLoopA\n[---][---]\nLoopB\n"},
                 {"verse_order", "v1"},
                 {"stack_id", added.id}};
    session.open_service(1, {}, {item});
    session.play_song(main_song, std::string(seventhslide::model::k_default_stack_id));
    const auto focused_before = session.focused_stack_id();
    REQUIRE(session.select_service_item(0, false));
    REQUIRE(session.focused_stack_id() == focused_before);
    REQUIRE(session.runtime({added.id})->occupied());
}

TEST_CASE("Session library play honors armed send-to; rundown pins do not", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(added.ok);
    REQUIRE(seventhslide::model::reorder_stacks(
                stacks, {added.id, std::string(seventhslide::model::k_default_stack_id)})
                .ok);
    session.set_stacks(stacks, session.type_defaults());
    session.arm_send_to(added.id);

    Song library_song;
    library_song.lyrics = "---[Verse:1]---\nLib\n";
    library_song.verse_order = "v1";
    session.play_song(library_song);
    REQUIRE(session.focused_stack_id() == added.id);

    ServiceItem pinned;
    pinned.item_id = 1;
    pinned.item_type = "song";
    pinned.data = {{"title", "Pinned"},
                   {"lyrics", "---[Verse:1]---\nOne\n"},
                   {"verse_order", "v1"},
                   {"stack_id", std::string(seventhslide::model::k_default_stack_id)}};
    session.open_service(1, {}, {pinned});
    REQUIRE(session.select_service_item(0));
    REQUIRE(session.focused_stack_id() == seventhslide::model::k_default_stack_id);
    REQUIRE(session.resolve_planned_stack("song",
                                          seventhslide::model::service_item_stack_id(pinned))
            == seventhslide::model::k_default_stack_id);
    REQUIRE(session.resolve_target_stack("song") == added.id);
}

TEST_CASE("Session also-play fires a companion onto another stack", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(added.ok);
    REQUIRE(seventhslide::model::reorder_stacks(
                stacks, {added.id, std::string(seventhslide::model::k_default_stack_id)})
                .ok);
    auto defaults = session.type_defaults();
    defaults.for_type("image") = added.id;
    session.set_stacks(stacks, defaults);

    ServiceItem song;
    song.item_id = 1;
    song.item_type = "song";
    song.data = {{"title", "Hymn"},
                 {"lyrics", "---[Verse:1]---\nOne\n"},
                 {"verse_order", "v1"},
                 {"also_play", json::array({2})}};

    ServiceItem image;
    image.item_id = 2;
    image.item_type = "image";
    image.data = {{"filename", "loop.jpg"}};

    session.open_service(1, {}, {song, image});
    REQUIRE(session.also_play_list_error(1, {2}).empty());
    REQUIRE_FALSE(session.also_play_list_error(1, {1}).empty());
    REQUIRE(session.select_service_item(0));
    REQUIRE(session.focused_stack_id() == seventhslide::model::k_default_stack_id);
    REQUIRE(session.service_item_index() == 0);
    REQUIRE(session.runtime({std::string(seventhslide::model::k_default_stack_id)})->occupied());
    REQUIRE(session.runtime({added.id})->occupied());
}

TEST_CASE("Session also-play fires the whole set from any member", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    auto added = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(added.ok);
    auto defaults = session.type_defaults();
    defaults.for_type("image") = added.id;
    session.set_stacks(stacks, defaults);

    ServiceItem song;
    song.item_id = 1;
    song.item_type = "song";
    song.data = {{"title", "Hymn"},
                 {"lyrics", "---[Verse:1]---\nOne\n"},
                 {"verse_order", "v1"},
                 {"also_play", json::array({2})}};

    ServiceItem image;
    image.item_id = 2;
    image.item_type = "image";
    image.data = {{"filename", "loop.jpg"}};

    ServiceItem loose;
    loose.item_id = 3;
    loose.item_type = "image";
    loose.data = {{"filename", "other.jpg"}};

    session.open_service(1, {}, {song, image, loose});
    REQUIRE(session.also_play_set(1) == std::vector<std::int64_t>{1, 2});
    REQUIRE(session.also_play_set(2) == std::vector<std::int64_t>{1, 2});
    REQUIRE(session.also_play_set(3).empty());

    // The image holds no link of its own, yet it still takes the song live.
    REQUIRE(session.select_service_item(1));
    REQUIRE(session.focused_stack_id() == added.id);
    REQUIRE(session.runtime({added.id})->service_item_id() == 2);
    REQUIRE(session.runtime({std::string(seventhslide::model::k_default_stack_id)})->service_item_id() == 1);
}

TEST_CASE("Session also-play skips a companion that resolves to the same stack", "[session][stacks]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    ServiceItem first;
    first.item_id = 1;
    first.item_type = "song";
    first.data = {{"title", "First"},
                  {"lyrics", "---[Verse:1]---\nAlpha\n"},
                  {"verse_order", "v1"},
                  {"also_play", json::array({2})}};

    ServiceItem second;
    second.item_id = 2;
    second.item_type = "song";
    second.data = {{"title", "Second"},
                   {"lyrics", "---[Verse:1]---\nBeta\n"},
                   {"verse_order", "v1"}};

    session.open_service(1, {}, {first, second});
    REQUIRE_FALSE(session.also_play_list_error(1, {2}).empty());
    REQUIRE(session.select_service_item(0));
    REQUIRE(session.occupied_stacks().size() == 1);
    REQUIRE(session.current_service_item() != nullptr);
    REQUIRE(session.current_service_item()->title() == "First");
}



namespace {

using seventhslide::test::CountingPlayer;
using seventhslide::test::Players;

/// A session with one output and a second stack, "Audio", in front of Main
/// that audio plays on by default.
struct AudioSession {
    ApproxTextMeasurer measurer;
    Session session{measurer};
    std::string main_id{seventhslide::model::k_default_stack_id};
    std::string audio_id;

    AudioSession()
    {
        OutputConfig oc;
        oc.name = "Main";
        oc.id = "out-main";
        seventhslide::test::set_look(oc, {{"video_enabled", true}});
        session.set_outputs({oc});
        auto stacks = session.stack_defs();
        const auto added = seventhslide::model::add_stack(stacks, "Audio", "layers");
        REQUIRE(added.ok);
        audio_id = added.id;
        auto defaults = session.type_defaults();
        defaults.for_type("audio") = audio_id;
        session.set_stacks(stacks, defaults);
    }
};

Song one_line_song()
{
    Song song;
    song.title = "Hymn";
    song.lyrics = "---[Verse:1]---\nOne\n";
    song.verse_order = "v1";
    return song;
}

}  // namespace

TEST_CASE("Every stack's video plays on a player of its own, whatever the focus",
          "[session][stacks][video]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"video_enabled", true}});
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    const auto lobby = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(lobby.ok);
    session.set_stacks(stacks, session.type_defaults());
    const std::string main_id(seventhslide::model::k_default_stack_id);

    Players players;
    session.play_media({ContentKind::Video, {{"filename", "back.mp4"}}}, main_id);
    session.set_players(players.source());
    auto* back = session.runtime({main_id});
    auto& back_player = players.video_of(*back);
    REQUIRE(back->media_player() == &back_player);
    // Stands in for the facade loading the owner's clip.
    REQUIRE(back_player.load("back.mp4", false));

    Song song;
    song.lyrics = "---[Verse:1]---\nOne\n";
    song.verse_order = "v1";
    session.play_song(song, lobby.id);
    session.attach_players();
    REQUIRE(session.focused_stack_id() == lobby.id);
    REQUIRE(back->media_player() == &back_player);
    REQUIRE(session.runtime({lobby.id})->media_player() == nullptr);

    // Clearing another stack must not stop the video it does not show.
    REQUIRE(session.clear_stack(lobby.id));
    REQUIRE(back_player.unloads == 0);
    REQUIRE(back_player.has_media());

    // Video on a stack painted in front has a player of its own; the one
    // behind keeps its own, and both play.
    session.play_media({ContentKind::Video, {{"filename", "front.mp4"}}}, lobby.id);
    session.attach_players();
    auto* front = session.runtime({lobby.id});
    REQUIRE(front->media_player() == &players.video_of(*front));
    REQUIRE(front->media_player() != back->media_player());
    REQUIRE(back->media_player() == &back_player);
    REQUIRE(players.videos.size() == 2);
}

TEST_CASE("Playing a second video on a stack starts it fresh", "[session][stacks][video]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"video_enabled", true}});
    session.set_outputs({oc});
    const std::string main_id(seventhslide::model::k_default_stack_id);

    CountingPlayer player;
    session.play_media({ContentKind::Video, {{"filename", "first.mp4"}}}, main_id);
    Players players{&player};
    session.set_players(players.source());
    REQUIRE(player.load("first.mp4", false));
    player.position = 42.0;

    // The first clip's position must not leak into the second one's frame:
    // the stack lets go of the first clip's player as the second comes. The
    // session stops nothing; the app lets the first clip go at its next sync.
    session.play_media({ContentKind::Video, {{"filename", "second.mp4"}}}, main_id);
    REQUIRE(session.runtime({main_id})->media_player() == nullptr);
    REQUIRE(player.unloads == 0);
    const auto& video = session.runtime({main_id})->playback();
    REQUIRE(video.filename == "second.mp4");
    REQUIRE(video.position_sec == 0.0);
}

TEST_CASE("Play on media that played to its end starts it over", "[session][stacks][video]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"video_enabled", true}});
    session.set_outputs({oc});
    const std::string main_id(seventhslide::model::k_default_stack_id);

    CountingPlayer player;
    session.play_media({ContentKind::Video, {{"filename", "clip.mp4"}}}, main_id);
    Players players{&player};
    session.set_players(players.source());
    REQUIRE(player.load("clip.mp4", false));
    REQUIRE(session.focused_stack_id() == main_id);
    auto& stack = session.focused_stack().program;

    // Paused part way, Play goes on from there.
    REQUIRE(stack.media_pause());
    REQUIRE(stack.media_play());
    REQUIRE(player.restarts == 0);
    REQUIRE(stack.is_playing());

    // At its end, Play starts it over.
    player.end();
    REQUIRE_FALSE(stack.is_playing());
    REQUIRE(stack.media_play());
    REQUIRE(player.restarts == 1);
    REQUIRE(stack.is_playing());
}

TEST_CASE("Each stack's video transport drives its own player", "[session][stacks][video]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    seventhslide::test::set_look(oc, {{"video_enabled", true}});
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    const auto lobby = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(lobby.ok);
    session.set_stacks(stacks, session.type_defaults());
    const std::string main_id(seventhslide::model::k_default_stack_id);

    Players players;
    session.play_media({ContentKind::Video, {{"filename", "back.mp4"}}}, main_id);
    session.play_media({ContentKind::Video, {{"filename", "front.mp4"}}}, lobby.id);
    session.set_players(players.source());
    auto& back = players.video_of(*session.runtime({main_id}));
    auto& front = players.video_of(*session.runtime({lobby.id}));
    REQUIRE(back.load("back.mp4", false));
    REQUIRE(front.load("front.mp4", false));

    session.set_focused_stack_id(main_id);
    REQUIRE(session.focused_stack().program.media_pause());
    REQUIRE_FALSE(session.focused_stack().program.playback().is_playing);
    REQUIRE(back.pauses == 1);
    REQUIRE(front.pauses == 0);

    session.set_focused_stack_id(lobby.id);
    REQUIRE(session.focused_stack().program.media_pause());
    REQUIRE(front.pauses == 1);
    REQUIRE(back.pauses == 1);
}

TEST_CASE("Audio plays on its own default stack and adds no picture", "[session][stacks][audio]")
{
    AudioSession t;
    auto& session = t.session;
    session.play_song(one_line_song(), t.main_id);
    const auto song_only = session.scene_frames();
    REQUIRE(song_only.size() == 1);

    // No stack named: the audio type default picks the stack.
    session.play_media({ContentKind::Audio, {{"filename", "hymn.mp3"}, {"title", "Hymn"}}});
    auto* audio = session.runtime({t.audio_id});
    REQUIRE(audio->audio_mode_active());
    REQUIRE(audio->has_transport());
    REQUIRE_FALSE(audio->video_mode_active());
    REQUIRE(audio->playback().filename == "hymn.mp3");
    REQUIRE(audio->playback().is_playing);
    REQUIRE_FALSE(session.runtime({t.main_id})->audio_mode_active());
    // The song is still live behind it.
    REQUIRE(session.runtime({t.main_id})->content_kind() == ContentKind::Song);

    // The audio stack is in front, yet every output shows exactly what it
    // showed with the song alone, and reports the song's mode.
    const auto with_audio = session.scene_frames();
    REQUIRE(with_audio.size() == 1);
    REQUIRE(with_audio.front().mode == seventhslide::scene::SceneMode::Song);
    REQUIRE(with_audio.front().stacked_below.empty());
    REQUIRE(with_audio.front() == song_only.front());

    // The audio stack's own frame (its controller thumbnail) paints nothing.
    session.set_focused_stack_id(t.audio_id);
    const auto own = session.scene_frame_at({session.focused_stack_id()}, "out-main", 0);
    REQUIRE(own.has_value());
    REQUIRE(own->mode == seventhslide::scene::SceneMode::Audio);
    REQUIRE(own->background.type == seventhslide::layout::BackgroundType::Transparent);
    REQUIRE_FALSE(own->text.has_value());
    REQUIRE(own->objects.empty());
}

TEST_CASE("Audio and other content replace each other on one stack", "[session][stacks][audio]")
{
    AudioSession t;
    auto& session = t.session;
    Players players;
    session.set_players(players.source());

    // A song on the audio stack, then audio sent there: the song is gone.
    session.play_song(one_line_song(), t.audio_id);
    session.play_media({ContentKind::Audio, {{"filename", "a.mp3"}}}, t.audio_id);
    session.attach_players();
    auto* stack = session.runtime({t.audio_id});
    REQUIRE(stack->content_kind() == ContentKind::Audio);
    REQUIRE(stack->song().title.empty());
    auto& player = players.audio_of(*stack);
    REQUIRE(stack->media_player() == &player);
    REQUIRE(player.load("a.mp3", false));  // stands in for the app loading it

    // A song sent to it now takes the stack, which lets go of the audio's
    // player at once. The session stops nothing: the app lets the audio go,
    // as its theme says, at its next sync.
    session.play_song(one_line_song(), t.audio_id);
    REQUIRE(stack->media_player() == nullptr);
    REQUIRE(player.unloads == 0);
    session.attach_players();
    REQUIRE(stack->content_kind() == ContentKind::Song);
    REQUIRE(stack->media_player() == nullptr);
    REQUIRE_FALSE(stack->has_transport());
    REQUIRE_FALSE(stack->audio_mode_active());

    // Video replaces audio the same way. Another audio item has a player of
    // its own.
    session.play_media({ContentKind::Audio, {{"filename", "b.mp3"}}}, t.audio_id);
    session.attach_players();
    auto& second = players.audio_of(*stack);
    REQUIRE(&second != &player);
    REQUIRE(stack->media_player() == &second);
    REQUIRE(second.load("b.mp3", false));
    session.play_media({ContentKind::Video, {{"filename", "clip.mp4"}}}, t.audio_id);
    REQUIRE(stack->media_player() == nullptr);
    REQUIRE(second.unloads == 0);
    REQUIRE(stack->video_mode_active());
    REQUIRE_FALSE(stack->audio_mode_active());
}

TEST_CASE("Each video or audio item is one instance while it stays", "[session][stacks][audio]")
{
    AudioSession t;
    auto& session = t.session;
    session.play_media({ContentKind::Audio, {{"filename", "bed.mp3"}}}, t.audio_id);
    session.play_media({ContentKind::Video, {{"filename", "clip.mp4"}}}, t.main_id);
    const auto* bed = session.runtime({t.audio_id});
    const auto* clip = session.runtime({t.main_id});
    const auto first = bed->media_instance();
    REQUIRE(first != 0);
    REQUIRE(clip->media_instance() != 0);
    REQUIRE(clip->media_instance() != first);

    // Built again (outputs, themes, settings): the same item.
    session.rebuild();
    REQUIRE(bed->media_instance() == first);

    // The same file sent again is another item.
    session.play_media({ContentKind::Audio, {{"filename", "bed.mp3"}}}, t.audio_id);
    REQUIRE(bed->media_instance() != 0);
    REQUIRE(bed->media_instance() != first);

    // An edit of the service row it plays from keeps it playing, as itself.
    ServiceItem item;
    item.item_id = 7;
    item.item_type = "audio";
    item.data = {{"filename", "hymn.mp3"}};
    session.open_service(1, {}, {item});
    REQUIRE(session.select_service_item(0));
    const auto served = bed->media_instance();
    REQUIRE(served != 0);
    item.data["title"] = "Hymn";
    session.replace_service_items({item});
    REQUIRE(session.refresh_service_item(7));
    REQUIRE(bed->media_instance() == served);

    // Anything else has none.
    session.play_song(one_line_song(), t.audio_id);
    REQUIRE(bed->media_instance() == 0);
}

TEST_CASE("A stack frozen before audio arrives keeps its picture", "[session][stacks][audio]")
{
    AudioSession t;
    auto& session = t.session;
    session.play_song(one_line_song(), t.main_id);
    const auto showing = session.scene_frames();
    REQUIRE(session.toggle_stack_freeze(t.main_id));

    // Audio replaces the song on the frozen stack: the sound plays, and the
    // output keeps what it held until the stack is unfrozen.
    session.play_media({ContentKind::Audio, {{"filename", "a.mp3"}}}, t.main_id);
    REQUIRE(session.runtime({t.main_id})->audio_mode_active());
    auto held = session.scene_frames();
    REQUIRE(held.size() == 1);
    REQUIRE(held.front().mode == seventhslide::scene::SceneMode::Song);
    REQUIRE(held.front().text == showing.front().text);

    REQUIRE(session.toggle_stack_freeze(t.main_id));
    REQUIRE(session.scene_frames().front().mode == seventhslide::scene::SceneMode::Empty);
}

TEST_CASE("Each audio stack has its own player, alongside the video", "[session][stacks][audio]")
{
    AudioSession t;
    auto& session = t.session;
    auto stacks = session.stack_defs();
    const auto lobby = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(lobby.ok);
    session.set_stacks(stacks, session.type_defaults());

    CountingPlayer video;
    Players players{&video};
    session.set_players(players.source());
    session.play_media({ContentKind::Video, {{"filename", "clip.mp4"}}}, t.main_id);
    session.play_media({ContentKind::Audio, {{"filename", "bed.mp3"}}}, t.audio_id);
    session.play_media({ContentKind::Audio, {{"filename", "lobby.mp3"}}}, lobby.id);
    session.attach_players();

    REQUIRE(session.runtime({t.main_id})->media_player() == &video);
    REQUIRE(players.audio.size() == 2);
    auto* bed = session.runtime({t.audio_id});
    auto* lobby_stack = session.runtime({lobby.id});
    REQUIRE(bed->audio_mode_active());
    REQUIRE(lobby_stack->audio_mode_active());
    REQUIRE(bed->media_player() == &players.audio_of(*bed));
    REQUIRE(lobby_stack->media_player() == &players.audio_of(*lobby_stack));
    REQUIRE(bed->media_player() != lobby_stack->media_player());

    // Transport on one audio stack drives its own player only.
    for (auto& [instance, p] : players.audio) {
        REQUIRE(p.load("file.mp3", false));
        p.play();
    }
    session.set_focused_stack_id(t.audio_id);
    REQUIRE(session.focused_stack().program.media_pause());
    REQUIRE(players.audio_of(*bed).pauses == 1);
    REQUIRE(players.audio_of(*lobby_stack).pauses == 0);
    REQUIRE(video.pauses == 0);
    REQUIRE_FALSE(session.focused_stack().program.is_playing());
    REQUIRE(lobby_stack->is_playing());
    REQUIRE(session.focused_stack().program.media_play());
    REQUIRE(session.focused_stack().program.is_playing());

    // The player reports the clip has ended: the stack reads as stopped.
    players.audio_of(*bed).pause();
    REQUIRE_FALSE(session.focused_stack().program.is_playing());
}

TEST_CASE("Audio links with other service items through Also play", "[session][stacks][audio]")
{
    AudioSession t;
    auto& session = t.session;

    ServiceItem song;
    song.item_id = 1;
    song.item_type = "song";
    song.data = {{"title", "Hymn"},
                 {"lyrics", "---[Verse:1]---\nOne\n"},
                 {"verse_order", "v1"},
                 {"also_play", json::array({2})}};
    ServiceItem bed;
    bed.item_id = 2;
    bed.item_type = "audio";
    bed.data = {{"filename", "bed.mp3"}};
    ServiceItem prelude;
    prelude.item_id = 3;
    prelude.item_type = "audio";
    prelude.data = {{"filename", "prelude.mp3"}, {"also_play", json::array({1})}};
    ServiceItem other;
    other.item_id = 4;
    other.item_type = "audio";
    other.data = {{"filename", "other.mp3"}};

    session.open_service(1, {}, {song, bed, prelude, other});
    REQUIRE(session.also_play_list_error(1, {2}).empty());
    REQUIRE(session.also_play_list_error(3, {1}).empty());
    // Two audio items share the audio stack, so one cannot also play the other.
    REQUIRE_FALSE(session.also_play_list_error(2, {4}).empty());

    // A song that also plays audio: both go live, each on its own stack.
    REQUIRE(session.select_service_item(0));
    REQUIRE(session.runtime({t.main_id})->content_kind() == ContentKind::Song);
    REQUIRE(session.runtime({t.audio_id})->audio_mode_active());
    REQUIRE(session.runtime({t.audio_id})->playback().filename == "bed.mp3");

    // Audio that also plays a song.
    REQUIRE(session.select_service_item(2));
    REQUIRE(session.runtime({t.audio_id})->playback().filename == "prelude.mp3");
    REQUIRE(session.runtime({t.main_id})->content_kind() == ContentKind::Song);
}

TEST_CASE("Session refresh_freeform updates only stacks playing that library item", "[session][freeform]")
{
    ApproxTextMeasurer measurer;
    Session session(measurer);
    OutputConfig oc;
    oc.name = "Main";
    oc.id = "out-main";
    session.set_outputs({oc});

    auto stacks = session.stack_defs();
    const auto lobby = seventhslide::model::add_stack(stacks, "Lobby", "info");
    REQUIRE(lobby.ok);
    session.set_stacks(stacks, session.type_defaults());
    const std::string main_id(seventhslide::model::k_default_stack_id);

    const auto library_freeform = [](std::int64_t id, const std::string& name) {
        LiveFreeformContent live;
        live.name = name;
        live.document = seventhslide::freeform::make_document(name, {"out-main"});
        live.library_id = id;
        return live;
    };

    // Item 7 on the back stack, item 8 on the (focused) front one.
    session.play_freeform(library_freeform(7, "Seven"), main_id);
    session.play_freeform(library_freeform(8, "Eight"), lobby.id);
    REQUIRE(session.focused_stack_id() == lobby.id);

    SECTION("an item nothing is playing refreshes nothing")
    {
        REQUIRE_FALSE(session.refresh_freeform(library_freeform(9, "Nine")));
        REQUIRE(session.runtime({main_id})->freeform().name == "Seven");
        REQUIRE(session.runtime({lobby.id})->freeform().name == "Eight");
    }
    SECTION("the matching stack updates even when it is not focused")
    {
        REQUIRE(session.refresh_freeform(library_freeform(7, "Seven v2")));
        REQUIRE(session.runtime({main_id})->freeform().name == "Seven v2");
        REQUIRE(session.runtime({lobby.id})->freeform().name == "Eight");
        REQUIRE(session.focused_stack_id() == lobby.id);
    }
    SECTION("content without a library id is not a refresh")
    {
        auto adhoc = library_freeform(7, "Adhoc");
        adhoc.library_id.reset();
        REQUIRE_FALSE(session.refresh_freeform(adhoc));
    }
}
