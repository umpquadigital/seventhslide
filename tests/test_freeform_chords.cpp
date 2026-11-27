#include <catch2/catch_test_macros.hpp>

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/scene/freeform_convert.hpp>

#include "theme_test_helpers.hpp"

#include <map>
#include <string>
#include <vector>

using seventhslide::freeform::Cue;
using seventhslide::freeform::Document;
using seventhslide::freeform::Editor;
using seventhslide::freeform::Object;
using seventhslide::freeform::ObjectType;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::layout::ChordMark;

namespace {

/// Two slides, a cue each; slide one's box is linked and shows the follow cue.
Document chord_doc()
{
    auto doc = seventhslide::freeform::make_document("Song", {"out-main"});
    doc.line_control = true;
    doc.cues = {Cue{.id = "c1", .text = "Amazing [G]grace"},
                Cue{.id = "c2", .text = "How [D]sweet the [G]sound"}};
    auto& deck = doc.decks.front();
    deck.slides.resize(2);
    deck.slides[0].id = "s1";
    deck.slides[1].id = "s2";
    deck.line_to_slide = {0, 1};
    Object box;
    box.id = "t1";
    box.type = ObjectType::Text;
    box.w = 1600;
    box.h = 900;
    box.font_size = 48;
    box.cue_linked = true;
    box.show_upcoming = true;
    deck.slides[0].objects.push_back(box);
    seventhslide::freeform::normalize(doc);
    return doc;
}

[[nodiscard]] std::vector<std::string> plain_lines(const seventhslide::layout::OverlayBox& box)
{
    std::vector<std::string> out;
    for (const auto& line : box.lines) {
        out.push_back(seventhslide::model::plain_text(line));
    }
    return out;
}

} // namespace

TEST_CASE("A linked box shows its cues' chords only when it shows chords", "[freeform][chords]")
{
    ApproxTextMeasurer measurer;
    auto doc = chord_doc();
    const auto& deck = doc.decks.front();
    const auto values = std::map<std::string, std::string>{};

    auto box = seventhslide::freeform::compile_text_object(doc, deck, 0, deck.slides[0].objects[0],
                                                           values, measurer);
    CHECK_FALSE(box.show_chords);
    CHECK(plain_lines(box) == std::vector<std::string>{"Amazing grace", "How sweet the sound"});

    doc.decks.front().slides[0].objects[0].show_chords = true;
    box = seventhslide::freeform::compile_text_object(doc, deck, 0, deck.slides[0].objects[0],
                                                      values, measurer);
    CHECK(box.show_chords);
    CHECK(plain_lines(box)
          == std::vector<std::string>{"Amazing [G]grace", "How [D]sweet the [G]sound"});
    // Laid out, the chords sit over their words, the follow cue's too.
    const auto laid = seventhslide::layout::lay_out_overlay(box, measurer);
    REQUIRE(laid.rows.size() == 2);
    REQUIRE(laid.rows[0].chords.size() == 1);
    CHECK(laid.rows[0].chords[0].text == "G");
    REQUIRE(laid.rows[1].chords.size() == 2);
    CHECK(laid.rows[1].chords[0].text == "D");

    // The line list reads words only.
    const auto compiled = seventhslide::freeform::compile(doc, &measurer);
    CHECK(compiled.lines == std::vector<std::string>{"Amazing grace", "How sweet the sound"});
}

TEST_CASE("A box's styling still fits a cue whose chords change", "[freeform][chords]")
{
    auto doc = chord_doc();
    auto& obj = doc.decks.front().slides[0].objects[0];
    obj.cue_overlays["c1"] = "<b>Amazing</b> grace";
    const auto& deck = doc.decks.front();
    CHECK(seventhslide::freeform::effective_cue_markup(deck, obj, doc.cues[0])
          == "<b>Amazing</b> grace");

    const Cue before = doc.cues[0];
    doc.cues[0].text = "[C]Amazing [G]grace";
    seventhslide::freeform::carry_cue_overlays(doc, before, doc.cues[0]);
    CHECK(obj.cue_overlays.at("c1") == "<b>Amazing</b> grace");
    CHECK(seventhslide::freeform::with_cue_chords(
              deck, seventhslide::freeform::effective_cue_markup(deck, obj, doc.cues[0]),
              doc.cues[0])
          == "<b>[C]Amazing</b> [G]grace");
}

TEST_CASE("Editing a linked box shows the follow cues and their chords", "[freeform][chords]")
{
    ApproxTextMeasurer measurer;
    const auto doc = chord_doc();
    const auto& deck = doc.decks.front();
    const auto edit = seventhslide::freeform::linked_edit(doc, deck, 0, deck.slides[0].objects[0],
                                                          measurer);
    CHECK(edit.first_cue == 0);
    CHECK(edit.owned == 1);
    CHECK(edit.upcoming == 1);
    CHECK(edit.lengths == std::vector<std::size_t>{13, 19});
    CHECK(edit.chords
          == std::vector<std::vector<ChordMark>>{{{8, "G"}}, {{4, "D"}, {14, "G"}}});
    const auto lines = seventhslide::model::parse_markup_lines(edit.markup);
    REQUIRE(lines.size() == 2);
    CHECK(seventhslide::model::plain_text(lines[0]) == "Amazing grace");
    CHECK(seventhslide::model::plain_text(lines[1]) == "How sweet the sound");
    // The follow cue is in the upcoming style.
    CHECK(lines[1].front().style.color == seventhslide::freeform::k_default_upcoming_color);
}

TEST_CASE("Editing a linked box changes its cues' chords in one step", "[freeform][chords]")
{
    ApproxTextMeasurer measurer;
    Editor ed;
    ed.load(chord_doc(), "out-main");
    ed.select("t1");
    REQUIRE(ed.selected_item() != nullptr);
    const auto edit = seventhslide::freeform::linked_edit(
        ed.document(), *ed.current_deck(), ed.slide_index(), *ed.selected_item(), measurer);

    CHECK(ed.linked_edit_allowed("Amazing grace\nHow sweet the sound", 1));
    CHECK(ed.linked_edit_allowed("Amazing \ngrace\n\nHow sweet the sound", 1));
    CHECK_FALSE(ed.linked_edit_allowed("Amazing grace\nHow sweet the soun", 1));
    CHECK_FALSE(ed.linked_edit_allowed("Amazing gracf\nHow sweet the sound", 1));

    // Bold on the slide's own cue; chords on it and on the follow cue.
    const std::string markup = "<b>Amazing</b> grace\n" + std::string("How sweet the sound");
    REQUIRE(ed.set_linked_edit(markup, edit.upcoming, {{{0, "C"}, {8, "G"}}, {{0, "F"}}}));
    CHECK(ed.document().cues[0].text == "[C]Amazing [G]grace");
    CHECK(ed.document().cues[1].text == "[F]How sweet the sound");
    CHECK(ed.selected_item()->cue_overlays.at("c1") == "<b>Amazing</b> grace");

    // The same again changes nothing and records nothing.
    REQUIRE(ed.set_linked_edit(markup, edit.upcoming, {{{0, "C"}, {8, "G"}}, {{0, "F"}}}));
    REQUIRE(ed.undo());
    CHECK(ed.document().cues[0].text == "Amazing [G]grace");
    CHECK(ed.document().cues[1].text == "How [D]sweet the [G]sound");
    CHECK(ed.selected_item()->cue_overlays.empty());
}

TEST_CASE("Save as Freeform keeps a song's chords", "[freeform][chords][convert]")
{
    ApproxTextMeasurer measurer;
    seventhslide::build::SlideBuildInput in;
    in.kind = seventhslide::build::BuildKind::Song;
    in.selection.kind = seventhslide::model::ContentKind::Song;
    in.song.title = "How Deep";
    in.song.lyrics = "---[Verse:1]---\nHow[E2] deep the Father's[F#m7] love\nNo chords here";
    seventhslide::model::OutputConfig stage;
    stage.id = "out-stage";
    stage.name = "Stage";
    stage.canvas_width = 1920;
    stage.canvas_height = 1080;
    seventhslide::test::set_look(stage, {{"show_chords", true}, {"chord_size_pct", 90}});
    in.outputs.push_back(stage);
    const auto built = seventhslide::build::build_slides(in, measurer);

    const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                              measurer, {.name = "How Deep"});
    std::vector<std::string> texts;
    for (const auto& cue : doc.cues) {
        texts.push_back(cue.text);
    }
    CHECK(std::ranges::find(texts, "How[E2] deep the Father's[F#m7] love") != texts.end());
    CHECK(std::ranges::find(texts, "No chords here") != texts.end());

    // The stage's lyric box shows them, in its theme's style.
    bool shows = false;
    for (const auto& slide : doc.decks.front().slides) {
        for (const auto& obj : slide.objects) {
            if (obj.cue_linked) {
                shows = shows || (obj.show_chords && obj.chords.size_pct == 90);
            }
        }
    }
    CHECK(shows);
}

TEST_CASE("Save as Freeform keeps the chords of a line split to fit", "[freeform][chords][convert]")
{
    // Four rows of chords over words in a box two rows high: the line is
    // played in two parts, and each part's cue keeps its own chords.
    ApproxTextMeasurer measurer;
    seventhslide::build::SlideBuildInput in;
    in.kind = seventhslide::build::BuildKind::Song;
    in.selection.kind = seventhslide::model::ContentKind::Song;
    in.song.title = "Amazing Grace";
    in.song.lyrics = "---[Verse:1]---\n[G]Amazing [D]grace how [C]sweet the [G]sound that "
                     "[D]saved a wretch like [G]me";
    seventhslide::model::OutputConfig stage;
    stage.id = "out-stage";
    stage.name = "Stage";
    stage.canvas_width = 1920;
    stage.canvas_height = 1080;
    seventhslide::test::set_look(stage, {{"show_chords", true},
                                         {"font_family", "X"},
                                         {"font_size", 40},
                                         {"width_px", 400},
                                         {"height_px", 160}});
    in.outputs.push_back(stage);
    const auto built = seventhslide::build::build_slides(in, measurer);

    const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                              measurer, {.name = "Amazing Grace"});
    std::vector<std::string> texts;
    for (const auto& cue : doc.cues) {
        texts.push_back(cue.text);
    }
    CHECK(std::ranges::find(texts, "[G]Amazing [D]grace how [C]sweet the [G]sound that")
          != texts.end());
    CHECK(std::ranges::find(texts, "[D]saved a wretch like [G]me") != texts.end());
}

TEST_CASE("Save as Freeform reads a word cut across rows back whole",
          "[freeform][chords][convert]")
{
    // 40 px characters are 20 px wide: "Hallelujah" takes two rows of a 100 px box.
    ApproxTextMeasurer measurer;
    for (const bool chords : {false, true}) {
        INFO("chords " << chords);
        seventhslide::build::SlideBuildInput in;
        in.kind = seventhslide::build::BuildKind::Song;
        in.selection.kind = seventhslide::model::ContentKind::Song;
        in.song.title = "Song";
        in.song.lyrics = "---[Verse:1]---\n[G]Hallelujah amen";
        seventhslide::model::OutputConfig out;
        out.id = "out";
        out.name = "Out";
        out.canvas_width = 1920;
        out.canvas_height = 1080;
        seventhslide::test::set_look(out, {{"show_chords", chords},
                                           {"font_family", "X"},
                                           {"font_size", 40},
                                           {"width_px", 100},
                                           {"height_px", 1000}});
        in.outputs.push_back(out);
        const auto built = seventhslide::build::build_slides(in, measurer);
        const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                                  measurer, {.name = "Song"});
        bool found = false;
        for (const auto& slide : doc.decks.front().slides) {
            for (const auto& obj : slide.objects) {
                found = found || (obj.cue_linked && obj.text == "Hallelujah amen");
            }
        }
        CHECK(found);
    }
}

namespace {

[[nodiscard]] seventhslide::model::OutputConfig output_of(const char* id, nlohmann::json look)
{
    seventhslide::model::OutputConfig out;
    out.id = id;
    out.name = id;
    out.canvas_width = 1920;
    out.canvas_height = 1080;
    look["font_family"] = "X";
    seventhslide::test::set_look(out, look);
    return out;
}

[[nodiscard]] seventhslide::build::SlideBuildInput song_on(
    std::string lyrics, std::vector<seventhslide::model::OutputConfig> outputs)
{
    seventhslide::build::SlideBuildInput in;
    in.kind = seventhslide::build::BuildKind::Song;
    in.selection.kind = seventhslide::model::ContentKind::Song;
    in.song.title = "Song";
    in.song.lyrics = std::move(lyrics);
    in.outputs = std::move(outputs);
    return in;
}

[[nodiscard]] const Object& linked_box(const Document& doc, std::size_t deck, std::size_t slide)
{
    for (const auto& obj : doc.decks[deck].slides[slide].objects) {
        if (obj.cue_linked) {
            return obj;
        }
    }
    FAIL("slide has no linked box");
    return doc.decks[deck].slides[slide].objects.front();
}

}  // namespace

TEST_CASE("Save as Freeform keeps the active line's size with no line following it",
          "[freeform][convert][follow]")
{
    ApproxTextMeasurer measurer;
    const auto in = song_on("---[Verse:1]---\nOne line alone",
                            {output_of("out", {{"font_size", 54},
                                               {"follow_lines", 1},
                                               {"active_line", {{"size", 80}}},
                                               {"highlight_color", "#ff0000"}})});
    const auto built = seventhslide::build::build_slides(in, measurer);
    const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                              measurer, {.name = "Song"});
    const auto& box = linked_box(doc, 0, doc.decks[0].slides.size() - 1);
    CHECK(box.font_size == 80);
    CHECK(box.font_color == "#ff0000");
    CHECK(box.upcoming_font_size == 54);
}

TEST_CASE("A following line keeps its exact size in a linked box", "[freeform][follow]")
{
    // 36 px under a 54 px box is 66.67%: not a whole percent.
    ApproxTextMeasurer measurer;
    auto doc = chord_doc();
    auto& deck = doc.decks.front();
    auto& box = deck.slides[0].objects[0];
    box.font_size = 54;
    box.upcoming_font_size = 36;
    box.line_height = 1.0;
    const auto compiled = seventhslide::freeform::compile_text_object(
        doc, deck, 0, box, std::map<std::string, std::string>{}, measurer);
    const auto laid = seventhslide::layout::lay_out_overlay(compiled, measurer);
    REQUIRE(laid.rows.size() == 2);
    CHECK(laid.rows[0].height == 54.0);
    CHECK(laid.rows[1].height == 36.0);
    CHECK(laid.rows[1].runs.front().style.size_px == 36.0);
}

TEST_CASE("Cues keep their slides when a screen hides a row", "[freeform][convert]")
{
    // The main screen hides the line of chords, so it has one line fewer than
    // the song has rows: cues are counted in rows, not in its lines.
    ApproxTextMeasurer measurer;
    const auto in = song_on("---[Verse:1]---\nFirst\n[G] [D]\nSecond\nThird",
                            {output_of("stage", {{"show_chords", true}, {"max_lines", 1}}),
                             output_of("main", {{"height_px", 60}, {"font_size", 40}})});
    const auto built = seventhslide::build::build_slides(in, measurer);
    const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                              measurer, {.name = "Song"});
    const auto& deck = doc.decks[1];
    const auto& rows = built.player.all_lines;
    REQUIRE(deck.line_to_slide.size() == rows.size());
    const std::map<std::string, std::string> values;
    for (std::size_t c = 0; c < rows.size(); ++c) {
        if (rows[c].empty()) {
            continue;  // the chords' own row
        }
        const auto slide = static_cast<std::size_t>(deck.line_to_slide[c]);
        const auto box = seventhslide::freeform::compile_text_object(
            doc, deck, static_cast<int>(slide), linked_box(doc, 1, slide), values, measurer);
        INFO("row " << c << ": " << rows[c]);
        const auto shown = plain_lines(box);
        CHECK(std::ranges::find(shown, rows[c]) != shown.end());
    }
}

TEST_CASE("A line one screen splits and another shows whole wraps as one on the other",
          "[freeform][convert]")
{
    // 40 px characters are 20 px wide. The narrow box splits the line in two;
    // the wide one shows it whole, on rows the split does not decide.
    ApproxTextMeasurer measurer;
    const auto in = song_on("---[Verse:1]---\naaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee",
                            {output_of("narrow", {{"font_size", 40},
                                                  {"width_px", 400},
                                                  {"height_px", 100}}),
                             output_of("wide", {{"font_size", 40},
                                                {"width_px", 800},
                                                {"height_px", 900}})});
    const auto built = seventhslide::build::build_slides(in, measurer);
    // The title's line, and the song's: in parts on one, whole on the other.
    REQUIRE(built.outputs[0].layout.lines.size() > 2);
    REQUIRE(built.outputs[1].layout.lines.size() == 2);
    const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                              measurer, {.name = "Song"});
    CHECK(doc.decks[0].cue_flow.empty());
    CHECK_FALSE(doc.decks[1].cue_flow.empty());

    const auto& deck = doc.decks[1];
    const auto slide = static_cast<std::size_t>(deck.line_to_slide.back());
    const auto box = seventhslide::freeform::compile_text_object(
        doc, deck, static_cast<int>(slide), linked_box(doc, 1, slide),
        std::map<std::string, std::string>{}, measurer);
    REQUIRE(box.lines.size() == 1);
    CHECK(seventhslide::model::plain_text(box.lines[0])
          == "aaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee");

    // It lasts: a saved document reads back the same.
    CHECK(Document::from_json(doc.to_json()).decks[1].cue_flow == deck.cue_flow);
}

TEST_CASE("Chords with no words are nothing to a linked box that hides chords",
          "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    const auto in = song_on("---[Verse:1]---\nFirst\n[G] [D]\nSecond",
                            {output_of("stage", {{"show_chords", true}}),
                             output_of("main", {{"font_size", 40}})});
    const auto built = seventhslide::build::build_slides(in, measurer);
    const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                              measurer, {.name = "Song"});
    const auto& deck = doc.decks[1];
    const auto slide = static_cast<std::size_t>(deck.line_to_slide.back());
    const auto box = seventhslide::freeform::compile_text_object(
        doc, deck, static_cast<int>(slide), linked_box(doc, 1, slide),
        std::map<std::string, std::string>{}, measurer);
    CHECK(plain_lines(box) == std::vector<std::string>{"First", "Second"});
}

TEST_CASE("Save as Freeform keeps a song's verse gap and balanced breaks", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    const std::map<std::string, std::string> values;

    SECTION("the room between verses, when both are shown")
    {
        const auto in = song_on("---[Verse:1]---\nOne\n---[Verse:2]---\nTwo",
                                {output_of("out", {{"font_size", 40},
                                                   {"fluid_slides", true},
                                                   {"verse_gap", 30}})});
        const auto built = seventhslide::build::build_slides(in, measurer);
        const auto doc = seventhslide::scene::document_from_build(
            built, in.outputs, measurer, {.name = "Song"});
        const auto& deck = doc.decks[0];
        const auto slide = static_cast<std::size_t>(deck.line_to_slide.back());
        const auto box = seventhslide::freeform::compile_text_object(
            doc, deck, static_cast<int>(slide), linked_box(doc, 0, slide), values, measurer);
        const auto laid = seventhslide::layout::lay_out_overlay(box, measurer);
        REQUIRE(laid.rows.size() == 2);
        CHECK(laid.rows[1].y == 48.0 + 30.0);
        CHECK(laid.height == 48.0 + 30.0 + 48.0);
    }

    SECTION("the breaks balancing chose")
    {
        // 20 px characters: plain wrapping in 500 px puts five words on the
        // first row and one on the second; balancing makes it three and three.
        const auto in = song_on("---[Verse:1]---\naaaa bbbb cccc dddd eeee ffff",
                                {output_of("out", {{"font_size", 40},
                                                   {"width_px", 500},
                                                   {"height_px", 900},
                                                   {"balance_wrapped_lines", true},
                                                   {"balance_wrapped_strength", 100}})});
        const auto built = seventhslide::build::build_slides(in, measurer);
        const auto doc = seventhslide::scene::document_from_build(
            built, in.outputs, measurer, {.name = "Song"});
        const auto& deck = doc.decks[0];
        const auto slide = static_cast<std::size_t>(deck.line_to_slide.back());
        const auto box = seventhslide::freeform::compile_text_object(
            doc, deck, static_cast<int>(slide), linked_box(doc, 0, slide), values, measurer);
        const auto laid = seventhslide::layout::lay_out_overlay(box, measurer);
        REQUIRE(laid.rows.size() == 2);
        CHECK(laid.rows[0].width == laid.rows[1].width);
        CHECK(doc.cues.back().text == "aaaa bbbb cccc dddd eeee ffff");
    }
}

TEST_CASE("A row of chords alone inside a line leaves the words' space alone",
          "[freeform][convert]")
{
    // The stage cuts the word's run of chords into rows, the first with no
    // syllable under it. The main screen hides chords and shows the line whole.
    ApproxTextMeasurer measurer;
    const auto in = song_on("---[Verse:1]---\nfoo [A][B][C][D][E][F][G][A][B][C][D][E][F][G]bar",
                            {output_of("stage", {{"show_chords", true},
                                                 {"font_size", 40},
                                                 {"width_px", 400},
                                                 {"height_px", 100}}),
                             output_of("main", {{"font_size", 40},
                                                {"width_px", 1600},
                                                {"height_px", 900}})});
    const auto built = seventhslide::build::build_slides(in, measurer);
    REQUIRE(std::ranges::find(built.player.all_lines, "") != built.player.all_lines.end());
    const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                              measurer, {.name = "Song"});
    const auto& deck = doc.decks[1];
    const auto slide = static_cast<std::size_t>(deck.line_to_slide.back());
    const auto box = seventhslide::freeform::compile_text_object(
        doc, deck, static_cast<int>(slide), linked_box(doc, 1, slide),
        std::map<std::string, std::string>{}, measurer);
    CHECK(plain_lines(box) == std::vector<std::string>{"foo bar"});
    CHECK(seventhslide::freeform::linked_snapshot_markup(doc, deck, static_cast<int>(slide),
                                                         linked_box(doc, 1, slide))
          == "foo bar");
}

TEST_CASE("A line cut for one screen reads as the song's on another that shows its chords",
          "[freeform][convert]")
{
    // A line of chords alone, a run of them over a word, and one after the last.
    ApproxTextMeasurer measurer;
    const auto in = song_on("---[Verse:1]---\n"
                            "[Gmaj7] [Dsus2/F#] [Em7] [Cadd9] [Gmaj7] [Dsus2/F#] [Em7] [Cadd9]\n"
                            "foo [Gmaj7][Dsus2/F#][Em7][Cadd9][Gmaj7][Dsus2/F#]bar\n"
                            "aaaaaaaa bbbbbbbb cccccccc[Dsus2/F#]",
                            {output_of("stage", {{"show_chords", true},
                                                 {"font_size", 40},
                                                 {"width_px", 200},
                                                 {"height_px", 100}}),
                             output_of("main", {{"show_chords", true},
                                                {"font_size", 40},
                                                {"width_px", 1800},
                                                {"height_px", 900}})});
    const auto built = seventhslide::build::build_slides(in, measurer);
    REQUIRE(std::ranges::find(built.player.all_lines, "") != built.player.all_lines.end());
    const auto doc = seventhslide::scene::document_from_build(built, in.outputs,
                                                              measurer, {.name = "Song"});
    const auto& deck = doc.decks[1];
    std::vector<std::string> merged;
    for (std::size_t s = 0; s < deck.slides.size(); ++s) {
        for (const auto& obj : deck.slides[s].objects) {
            if (obj.cue_linked) {
                const auto box = seventhslide::freeform::compile_text_object(
                    doc, deck, static_cast<int>(s), obj, std::map<std::string, std::string>{},
                    measurer);
                const auto lines = plain_lines(box);
                merged.insert(merged.end(), lines.begin(), lines.end());
            }
        }
    }
    int lines_with_chords = 0;
    for (const auto& line : built.outputs[1].layout.lines) {
        if (!line.chord_text.empty()) {
            ++lines_with_chords;
            INFO(line.chord_text);
            CHECK(std::ranges::find(merged, line.chord_text) != merged.end());
        }
    }
    CHECK(lines_with_chords == 3);
}

TEST_CASE("Editing a box whose cues run on keeps them one line", "[freeform][convert]")
{
    ApproxTextMeasurer measurer;
    const auto in = song_on("---[Verse:1]---\naaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee",
                            {output_of("narrow", {{"font_size", 40},
                                                  {"width_px", 400},
                                                  {"height_px", 100}}),
                             output_of("wide", {{"font_size", 40},
                                                {"width_px", 800},
                                                {"height_px", 900}})});
    const auto built = seventhslide::build::build_slides(in, measurer);
    const auto saved = seventhslide::scene::document_from_build(built, in.outputs,
                                                                measurer, {.name = "Song"});
    const auto slide = saved.decks[1].line_to_slide.back();
    Editor ed;
    ed.load(saved, saved.decks[1].output_id);
    ed.select_slide(slide);
    ed.select(linked_box(saved, 1, static_cast<std::size_t>(slide)).id);
    REQUIRE(ed.selected_item() != nullptr);

    // The editor shows the one line the box paints; saving that changes nothing.
    const auto edit = seventhslide::freeform::linked_edit(ed.document(), *ed.current_deck(), slide,
                                                          *ed.selected_item(), measurer);
    CHECK(edit.markup == "aaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee");
    REQUIRE(ed.set_linked_edit(edit.markup, edit.upcoming, edit.chords));
    CHECK(ed.selected_item()->cue_overlays.empty());
    const auto box = seventhslide::freeform::compile_text_object(
        ed.document(), *ed.current_deck(), slide, *ed.selected_item(),
        std::map<std::string, std::string>{}, measurer);
    CHECK(plain_lines(box)
          == std::vector<std::string>{"aaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee"});
}

TEST_CASE("A verse gap is not counted against a follow cue it is not above",
          "[freeform][follow]")
{
    ApproxTextMeasurer measurer;
    auto doc = chord_doc();
    doc.cues = {Cue{.id = "c1", .text = "One"}, Cue{.id = "c2", .text = "Two"},
                Cue{.id = "c3", .text = "Three\nFour"}, Cue{.id = "c4", .text = "Five"}};
    auto& deck = doc.decks.front();
    deck.line_to_slide = {0, 0, 1, 1};
    deck.cue_flow = {{.id = "c2", .gap_above = 100}};
    auto& box = deck.slides[0].objects[0];
    box.font_size = 48;
    box.upcoming_font_size = 48;
    box.line_height = 1.0;
    box.line_gap = 0;
    // Its own lines take 196 px, and the follow cue's two lines 96.
    box.h = 300;
    const auto compiled = seventhslide::freeform::compile_text_object(
        doc, deck, 0, box, std::map<std::string, std::string>{}, measurer);
    CHECK(plain_lines(compiled) == std::vector<std::string>{"One", "Two", "Three", "Four"});
}

TEST_CASE("Cues keep their flow as cues come and go", "[freeform][edit]")
{
    using seventhslide::freeform::CueFlow;
    auto doc = chord_doc();
    doc.cues = {Cue{.id = "c1", .text = "Zero"}, Cue{.id = "c2", .text = "One"},
                Cue{.id = "c3", .text = "Two"}, Cue{.id = "c4", .text = "Three"},
                Cue{.id = "c5", .text = "Four"}};
    auto& deck = doc.decks.front();
    deck.line_to_slide = {0, 0, 0, 0, 1};
    const std::vector<CueFlow> flow{{.id = "c2", .gap_above = 30},
                                    {.id = "c3", .runs_on = true},
                                    {.id = "c4", .runs_on = true, .in_word = true}};
    deck.cue_flow = flow;

    SECTION("the first of a line going, the next starts it, with its room")
    {
        REQUIRE(seventhslide::freeform::delete_cue(doc, 1));
        const auto* next = deck.flow_of(doc.cues[1]);
        REQUIRE(next != nullptr);
        CHECK_FALSE(next->runs_on);
        CHECK(next->gap_above == 30);
        CHECK(deck.flow_of(doc.cues[2])->runs_on);
        CHECK(deck.cue_flow.size() == 2);
    }

    SECTION("one inside a line going, the word it cut is no longer cut")
    {
        REQUIRE(seventhslide::freeform::delete_cue(doc, 2));
        const auto* next = deck.flow_of(doc.cues[2]);
        REQUIRE(next != nullptr);
        CHECK(next->runs_on);
        CHECK_FALSE(next->in_word);
    }

    SECTION("a cue added inside a line ends what ran on")
    {
        REQUIRE(seventhslide::freeform::insert_cue(doc, 2, "New"));
        const auto* after = deck.flow_of(doc.cues[3]);
        CHECK((after == nullptr || !after->runs_on));
    }

    SECTION("no cues, no flow")
    {
        REQUIRE(seventhslide::freeform::set_line_control(doc, false, false, ""));
        CHECK(deck.cue_flow.empty());
    }
}

TEST_CASE("A verse gap moves with its deck's canvas", "[freeform][canvas]")
{
    auto doc = chord_doc();
    auto& deck = doc.decks.front();
    deck.canvas = {1920, 1080};
    deck.cue_flow = {{.id = "c2", .gap_above = 30}};
    seventhslide::freeform::fit_deck_to_canvas(deck, {3840, 2160});
    CHECK(deck.cue_flow.front().gap_above == 60);
}

TEST_CASE("Unlinking a box keeps its cues' lines as it showed them", "[freeform][edit]")
{
    auto doc = chord_doc();
    doc.cues = {Cue{.id = "c1", .text = "aaa"}, Cue{.id = "c2", .text = "bbb"},
                Cue{.id = "c3", .text = "ccc"}, Cue{.id = "c4", .text = "[G]"},
                Cue{.id = "c5", .text = "ddd"}};
    auto& deck = doc.decks.front();
    deck.line_to_slide = {0, 0, 0, 0, 0};
    deck.cue_flow = {{.id = "c2", .runs_on = true}, {.id = "c3", .runs_on = true, .in_word = true}};
    seventhslide::freeform::normalize(doc);
    CHECK(seventhslide::freeform::linked_snapshot_markup(doc, deck, 0, deck.slides[0].objects[0])
          == "aaa bbbccc\nddd");
}

TEST_CASE("Words typed where only chords follow are refused", "[freeform][chords]")
{
    auto doc = chord_doc();
    doc.cues = {Cue{.id = "c1", .text = "Words"}, Cue{.id = "c2", .text = "[G]"}};
    auto& deck = doc.decks.front();
    deck.line_to_slide = {0, 0};
    seventhslide::freeform::normalize(doc);
    std::map<std::string, std::string> overlays;
    CHECK(seventhslide::freeform::split_linked_overlays(doc, deck, 0, deck.slides[0].objects[0], "Words\n", overlays));
    CHECK_FALSE(seventhslide::freeform::split_linked_overlays(doc, deck, 0, deck.slides[0].objects[0], "Words\nExtra", overlays));
}

TEST_CASE("A slide's preview key knows the room between its lines", "[freeform][follow]")
{
    auto doc = chord_doc();
    doc.cues = {Cue{.id = "c1", .text = "A"}, Cue{.id = "c2", .text = "B"},
                Cue{.id = "c3", .text = "A"}, Cue{.id = "c4", .text = "B"}};
    auto& deck = doc.decks.front();
    deck.line_to_slide = {0, 0, 1, 1};
    deck.slides[0].objects[0].show_upcoming = false;
    deck.slides[1].objects = deck.slides[0].objects;
    CHECK(seventhslide::freeform::slide_preview_key(doc, deck, 0)
          == seventhslide::freeform::slide_preview_key(doc, deck, 1));
    deck.cue_flow = {{.id = "c4", .gap_above = 30}};
    CHECK(seventhslide::freeform::slide_preview_key(doc, deck, 0)
          != seventhslide::freeform::slide_preview_key(doc, deck, 1));
}

TEST_CASE("A cue moved away from the line it ran on from starts a line of its own",
          "[freeform][edit]")
{
    using seventhslide::freeform::CueFlow;
    auto doc = chord_doc();
    doc.cues = {Cue{.id = "c1", .text = "First"}, Cue{.id = "c2", .text = "of a line"},
                Cue{.id = "c3", .text = "Next"}};
    auto& deck = doc.decks.front();
    deck.slides.resize(3);
    deck.slides[2].id = "s3";
    deck.line_to_slide = {0, 1, 2};
    deck.cue_flow = {CueFlow{.id = "c2", .runs_on = true}, CueFlow{.id = "c3", .gap_above = 30}};

    REQUIRE(seventhslide::freeform::reorder_slides(doc, 1, 2, deck.output_id));
    REQUIRE(doc.cues[1].id == "c3");
    REQUIRE(doc.cues[2].id == "c2");
    const auto* moved = deck.flow_of(doc.cues[2]);
    CHECK((moved == nullptr || !moved->runs_on));
    // Room above a line is no matter of what came before it.
    REQUIRE(deck.flow_of(doc.cues[1]) != nullptr);
    CHECK(deck.flow_of(doc.cues[1])->gap_above == 30);
}

TEST_CASE("Cues that stay together through a move keep running on", "[freeform][edit]")
{
    using seventhslide::freeform::CueFlow;
    auto doc = chord_doc();
    doc.cues = {Cue{.id = "c1", .text = "Before"}, Cue{.id = "c2", .text = "First"},
                Cue{.id = "c3", .text = "of a line"}};
    auto& deck = doc.decks.front();
    deck.line_to_slide = {0, 1, 1};
    deck.cue_flow = {CueFlow{.id = "c3", .runs_on = true, .in_word = true}};

    REQUIRE(seventhslide::freeform::reorder_slides(doc, 1, 0, deck.output_id));
    REQUIRE(doc.cues[1].id == "c3");
    const auto* kept = deck.flow_of(doc.cues[1]);
    REQUIRE(kept != nullptr);
    CHECK(kept->runs_on);
    CHECK(kept->in_word);
}

TEST_CASE("A blank follow line is as tall as a following line's size", "[freeform][follow]")
{
    ApproxTextMeasurer measurer;
    auto doc = chord_doc();
    doc.cues = {Cue{.id = "c1", .text = "Words"}, Cue{.id = "c2", .text = ""}};
    auto& box = doc.decks.front().slides[0].objects.front();
    box.font_size = 300;
    box.upcoming_font_size = 100;  // 33.33% of the box, which no whole percent is
    const auto compiled = seventhslide::freeform::compile_text_object(
        doc, doc.decks.front(), 0, box, std::map<std::string, std::string>{}, measurer);
    const auto laid = seventhslide::layout::lay_out_overlay(compiled, measurer);
    REQUIRE(laid.rows.size() == 2);
    CHECK(laid.rows[1].height
          == seventhslide::layout::line_box_height_px(100.0, compiled.line_height));
}
