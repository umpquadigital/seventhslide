#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/model/transition.hpp>

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using seventhslide::freeform::Deck;
using seventhslide::freeform::Document;
using seventhslide::freeform::Editor;
using seventhslide::freeform::find_deck;
using seventhslide::freeform::normalize;

namespace {

const nlohmann::json k_canvas = {{"width", 1920}, {"height", 1080}};

/// A slide with one box of font size ``size`` (how a test tells looks apart).
nlohmann::json slide(const char* id, int size, nlohmann::json overlays = nlohmann::json::object())
{
    return {{"id", id},
            {"objects",
             {{{"id", std::string(id) + "-box"},
               {"type", "text"},
               {"x", 960},
               {"y", 100},
               {"w", 400},
               {"h", 200},
               {"font_size", size},
               {"cue_linked", true},
               {"cue_overlays", std::move(overlays)}}}}};
}

nlohmann::json deck(const char* output, std::vector<int> map, nlohmann::json slides,
                    nlohmann::json canvas = k_canvas)
{
    return {{"output", output},
            {"canvas", std::move(canvas)},
            {"line_to_slide", std::move(map)},
            {"slides", std::move(slides)}};
}

/// Four verse lines and two chorus lines. Main shows the verse on one slide
/// and the chorus on another, in size 60; ``lt`` is the second deck.
Document song(nlohmann::json lt)
{
    auto doc = Document::from_json({
        {"schema", "seventhslide.freeform"},
        {"schema_version", 3},
        {"name", "Song"},
        {"line_control", true},
        {"next_id", 100},
        {"cues",
         {{{"id", "c1"}, {"text", "verse one"}},
          {{"id", "c2"}, {"text", "verse two"}},
          {{"id", "c3"}, {"text", "verse three"}},
          {{"id", "c4"}, {"text", "verse four"}},
          {{"id", "c5"}, {"text", "chorus one"}},
          {{"id", "c6"}, {"text", "chorus two"}}}},
        {"decks",
         {deck("out-main", {0, 0, 0, 0, 1, 1},
               {slide("m1", 60), slide("m2", 60, {{"c5", "<b>chorus one</b>"}})}),
          std::move(lt)}},
    });
    normalize(doc);
    return doc;
}

/// Lower Thirds: two lines a slide, in size 30, the chorus a line a slide.
nlohmann::json lower_thirds(nlohmann::json canvas = k_canvas)
{
    return deck("LT", {0, 0, 1, 1, 2, 3},
                {slide("l1", 30), slide("l2", 30), slide("l3", 30), slide("l4", 30)},
                std::move(canvas));
}

const Deck& deck_of(const Editor& ed, const char* output)
{
    const auto* d = find_deck(ed.document(), output);
    REQUIRE(d != nullptr);
    return *d;
}

std::vector<int> sizes(const Deck& d)
{
    std::vector<int> out;
    for (const auto& s : d.slides) {
        out.push_back(s.objects.empty() ? 0 : s.objects.front().font_size);
    }
    return out;
}

std::vector<std::string> cue_texts(const Document& doc)
{
    std::vector<std::string> out;
    for (const auto& cue : doc.cues) {
        out.push_back(cue.text);
    }
    return out;
}

} // namespace

TEST_CASE("Slides pasted onto another output land on their lines", "[freeform][paste]")
{
    Editor ed;
    ed.load(song(lower_thirds()), "out-main");
    ed.select_slide(0);
    ed.extend_slide_selection(1);
    REQUIRE(ed.slide_range_first() == 0);
    REQUIRE(ed.slide_range_last() == 1);
    REQUIRE(ed.copy_slides());
    REQUIRE(ed.can_paste());

    ed.set_output("LT");
    ed.select_slide(3);
    REQUIRE(ed.paste());

    // Lower Thirds splits the lines as Main does, in Main's look.
    const auto& lt = deck_of(ed, "LT");
    CHECK(lt.line_to_slide == std::vector<int>{0, 0, 0, 0, 1, 1});
    CHECK(sizes(lt) == std::vector<int>{60, 60});
    CHECK(lt.slides[1].objects.front().cue_overlays.at("c5") == "<b>chorus one</b>");
    // The copies are the deck's own: ids of their own.
    const auto& main = deck_of(ed, "out-main");
    CHECK(lt.slides[0].id != main.slides[0].id);
    CHECK(lt.slides[0].objects.front().id != main.slides[0].objects.front().id);
    // No line came or went, and Main is as it was.
    CHECK(ed.document().cues.size() == 6);
    CHECK(sizes(main) == std::vector<int>{60, 60});
    // The pasted slides are picked.
    CHECK(ed.slide_range_first() == 0);
    CHECK(ed.slide_range_last() == 1);
    CHECK(ed.slide_index() == 0);

    // One undo takes it all back.
    REQUIRE(ed.undo());
    CHECK(deck_of(ed, "LT").line_to_slide == std::vector<int>{0, 0, 1, 1, 2, 3});
    CHECK(sizes(deck_of(ed, "LT")) == std::vector<int>{30, 30, 30, 30});
}

TEST_CASE("A slide pasted onto another output changes only its lines there", "[freeform][paste]")
{
    SECTION("its lines on slides of their own")
    {
        Editor ed;
        ed.load(song(lower_thirds()), "out-main");
        ed.select_slide(1);
        REQUIRE(ed.copy_slides());
        ed.set_output("LT");
        const auto before = deck_of(ed, "LT");
        REQUIRE(ed.paste());
        const auto& lt = deck_of(ed, "LT");
        CHECK(lt.line_to_slide == std::vector<int>{0, 0, 1, 1, 2, 2});
        CHECK(sizes(lt) == std::vector<int>{30, 30, 60});
        CHECK(lt.slides[0].id == before.slides[0].id);
        CHECK(lt.slides[1].id == before.slides[1].id);
        CHECK(ed.slide_index() == 2);
    }
    SECTION("its lines sharing a slide with others")
    {
        // Lower Thirds: one | two three | four chorus-one | chorus-two.
        Editor ed;
        ed.load(song(deck("LT", {0, 1, 1, 2, 2, 3},
                          {slide("l1", 30), slide("l2", 30), slide("l3", 30), slide("l4", 30)})),
                "out-main");
        ed.select_slide(1);
        REQUIRE(ed.copy_slides());
        ed.set_output("LT");
        REQUIRE(ed.paste());
        // "four" keeps its slide; the chorus is Main's.
        const auto& lt = deck_of(ed, "LT");
        CHECK(lt.line_to_slide == std::vector<int>{0, 1, 1, 2, 3, 3});
        CHECK(sizes(lt) == std::vector<int>{30, 30, 30, 60});
        CHECK(lt.slides[2].id == "l3");
    }
}

TEST_CASE("Slides pasted where they were copied repeat their lines", "[freeform][paste]")
{
    SECTION("after the last slide")
    {
        Editor ed;
        ed.load(song(lower_thirds()), "out-main");
        ed.select_slide(1);
        REQUIRE(ed.copy_slides());
        REQUIRE(ed.paste());

        const auto& doc = ed.document();
        CHECK(cue_texts(doc)
              == std::vector<std::string>{"verse one", "verse two", "verse three", "verse four",
                                          "chorus one", "chorus two", "chorus one", "chorus two"});
        // The copies are lines of their own.
        CHECK(doc.cues[6].id != "c5");
        CHECK(doc.cues[7].id != "c6");
        const auto& main = deck_of(ed, "out-main");
        CHECK(main.line_to_slide == std::vector<int>{0, 0, 0, 0, 1, 1, 2, 2});
        CHECK(ed.slide_index() == 2);
        // The pasted box styles the copy of its line as it styled the line.
        const auto& overlays = main.slides[2].objects.front().cue_overlays;
        CHECK(overlays.at(doc.cues[6].id) == "<b>chorus one</b>");
        CHECK_FALSE(overlays.contains("c5"));

        // Lower Thirds shows the lines again as it shows them: a slide each,
        // copies of its own.
        const auto& lt = deck_of(ed, "LT");
        CHECK(lt.line_to_slide == std::vector<int>{0, 0, 1, 1, 2, 3, 4, 5});
        CHECK(sizes(lt) == std::vector<int>{30, 30, 30, 30, 30, 30});
        CHECK(lt.slides[4].id != "l3");
    }
    SECTION("between two slides")
    {
        Editor ed;
        ed.load(song(lower_thirds()), "out-main");
        ed.select_slide(1);
        REQUIRE(ed.copy_slides());
        ed.select_slide(0);
        REQUIRE(ed.paste());

        CHECK(cue_texts(ed.document())
              == std::vector<std::string>{"verse one", "verse two", "verse three", "verse four",
                                          "chorus one", "chorus two", "chorus one", "chorus two"});
        CHECK(deck_of(ed, "out-main").line_to_slide == std::vector<int>{0, 0, 0, 0, 1, 1, 2, 2});
        CHECK(ed.slide_index() == 1);
        const auto& lt = deck_of(ed, "LT");
        CHECK(lt.line_to_slide == std::vector<int>{0, 0, 1, 1, 2, 3, 4, 5});
        CHECK(lt.slides[4].id == "l3");
        CHECK(lt.slides[5].id == "l4");
    }
    SECTION("into the middle of another output's slide")
    {
        // Lower Thirds: one two | three four chorus-one | chorus-two.
        Editor ed;
        ed.load(song(deck("LT", {0, 0, 1, 1, 1, 2},
                          {slide("l1", 30), slide("l2", 30), slide("l3", 30)})),
                "out-main");
        ed.select_slide(1);
        REQUIRE(ed.copy_slides());
        ed.select_slide(0);
        REQUIRE(ed.paste());
        // The new lines come after "four": Lower Thirds' middle slide splits
        // around them, its rest on a copy.
        const auto& lt = deck_of(ed, "LT");
        CHECK(lt.line_to_slide == std::vector<int>{0, 0, 1, 1, 2, 3, 4, 5});
        CHECK(lt.slides[1].id == "l2");
        CHECK(lt.slides[4].id != "l2");
        CHECK(lt.slides[5].id == "l3");
    }
}

TEST_CASE("Pasted slides keep their transitions and notes", "[freeform][paste]")
{
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    Transition fade;
    fade.type = TransitionType::Fade;
    fade.duration_ms = 800;
    Transition wipe;
    wipe.type = TransitionType::Wipe;
    wipe.duration_ms = 300;
    auto doc = song(lower_thirds());
    auto& first = find_deck(doc, "out-main")->slides[0];
    first.transition = fade;
    first.background_transition = wipe;
    first.notes = "Pause here";

    Editor ed;
    ed.load(doc, "out-main");
    ed.select_slide(0);
    REQUIRE(ed.copy_slides());

    const auto check = [&](const char* output, int index) {
        INFO(output << " slide " << index);
        const auto& pasted = deck_of(ed, output).slides.at(index);
        CHECK(pasted.id != deck_of(ed, "out-main").slides[0].id);
        CHECK(pasted.transition == fade);
        CHECK(pasted.background_transition == wipe);
        CHECK(pasted.notes == "Pause here");
    };
    SECTION("onto another output")
    {
        ed.set_output("LT");
        REQUIRE(ed.paste());
        check("LT", 0);
    }
    SECTION("where they were copied")
    {
        REQUIRE(ed.paste());
        check("out-main", 1);
    }
}

TEST_CASE("Pasted slides are laid out for the output's canvas", "[freeform][paste]")
{
    Editor ed;
    ed.load(song(lower_thirds({{"width", 960}, {"height", 540}})), "out-main");
    ed.select_slide(0);
    REQUIRE(ed.copy_slides());
    ed.set_output("LT");
    REQUIRE(ed.paste());
    const auto& box = deck_of(ed, "LT").slides[0].objects.front();
    CHECK(box.x == Catch::Approx(480));
    CHECK(box.w == Catch::Approx(200));
    CHECK(box.font_size == 30);
}

TEST_CASE("Without line control pasted slides come on every output", "[freeform][paste]")
{
    auto doc = song(lower_thirds());
    doc.line_control = false;
    doc.cues.clear();
    // Two slides on each output, in step.
    find_deck(doc, "LT")->slides.resize(2);
    normalize(doc);
    Editor ed;
    ed.load(doc, "out-main");
    REQUIRE(deck_of(ed, "LT").slide_count() == 2);
    REQUIRE(deck_of(ed, "out-main").slide_count() == 2);

    SECTION("on the output copied from, each output repeats its own")
    {
        ed.select_slide(0);
        REQUIRE(ed.copy_slides());
        ed.select_slide(1);
        REQUIRE(ed.paste());
        CHECK(sizes(deck_of(ed, "out-main")) == std::vector<int>{60, 60, 60});
        CHECK(sizes(deck_of(ed, "LT")) == std::vector<int>{30, 30, 30});
        CHECK(ed.slide_index() == 2);
    }
    SECTION("on another output, that output takes the copy")
    {
        ed.select_slide(0);
        REQUIRE(ed.copy_slides());
        ed.set_output("LT");
        ed.select_slide(0);
        REQUIRE(ed.paste());
        CHECK(sizes(deck_of(ed, "LT")) == std::vector<int>{30, 60, 30});
        CHECK(sizes(deck_of(ed, "out-main")) == std::vector<int>{60, 60, 60});
        CHECK(deck_of(ed, "LT").line_to_slide == std::vector<int>{0, 1, 2});
    }
}

TEST_CASE("A slide holding no lines pastes alone", "[freeform][paste]")
{
    Editor ed;
    ed.load(song(lower_thirds()), "out-main");
    ed.select_slide(1);
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.slide_index() == 2);
    REQUIRE(ed.copy_slides());
    ed.set_output("LT");
    ed.select_slide(0);
    REQUIRE(ed.paste());
    const auto& lt = deck_of(ed, "LT");
    CHECK(lt.slide_count() == 5);
    CHECK(lt.line_to_slide == std::vector<int>{0, 0, 2, 2, 3, 4});
    CHECK(ed.slide_index() == 1);
    CHECK(ed.document().cues.size() == 6);
}

TEST_CASE("The slides picked run from the first picked to the last", "[freeform][paste]")
{
    Editor ed;
    ed.load(song(lower_thirds()), "LT");
    ed.select_slide(2);
    ed.extend_slide_selection(0);
    CHECK(ed.slide_range_first() == 0);
    CHECK(ed.slide_range_last() == 2);
    CHECK(ed.slide_index() == 0);
    ed.extend_slide_selection(3);
    CHECK(ed.slide_range_first() == 2);
    CHECK(ed.slide_range_last() == 3);

    ed.select_all_slides();
    CHECK(ed.slide_range_first() == 0);
    CHECK(ed.slide_range_last() == 3);
    REQUIRE(ed.copy_slides());

    // Picking one slide, or another output, picks that alone.
    ed.select_slide(1);
    CHECK(ed.slide_range_first() == 1);
    CHECK(ed.slide_range_last() == 1);
    ed.select_all_slides();
    ed.set_output("out-main");
    CHECK(ed.slide_range_first() == ed.slide_range_last());
}
