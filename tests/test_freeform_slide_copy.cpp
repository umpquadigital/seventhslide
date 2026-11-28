// Copying a slide: duplicating it, and copying and pasting it.

#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"

#include <seventhslide/freeform/document.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/model/transition.hpp>

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

using seventhslide::freeform::Deck;
using seventhslide::freeform::Document;
using seventhslide::freeform::Editor;
using seventhslide::freeform::Slide;
using seventhslide::freeform::SlideBackgroundKind;
using seventhslide::freeform::duplicate_slide;
using seventhslide::freeform::find_deck;
using seventhslide::freeform::normalize;

namespace {

/// Two outputs, two slides each, without line control. Each slide has a
/// background and a text box and a shape grouped together.
Document two_outputs()
{
    const auto box = [](const char* id, const char* text) {
        return nlohmann::json{{"id", id},       {"type", "text"}, {"x", 100},
                              {"y", 100},       {"w", 400},       {"h", 200},
                              {"text", text},   {"group_id", "g1"}};
    };
    const auto shape = [](const char* id) {
        return nlohmann::json{{"id", id}, {"type", "shape"}, {"x", 600}, {"y", 100},
                              {"w", 100}, {"h", 100},        {"z", 1},   {"group_id", "g1"}};
    };
    const auto slide = [&](const char* id, const char* text, const char* color) {
        return nlohmann::json{
            {"id", id},
            {"background", {{"kind", "color"}, {"color", color}}},
            {"objects", {box((std::string(id) + "-box").c_str(), text),
                         shape((std::string(id) + "-shape").c_str())}}};
    };
    auto doc = Document::from_json({
        {"schema", "seventhslide.freeform"},
        {"schema_version", 4},
        {"name", "Two"},
        {"next_id", 100},
        {"decks",
         {{{"output", "out-main"},
           {"slides", {slide("m1", "Main one", "#110000"), slide("m2", "Main two", "#220000")}}},
          {{"output", "LT"},
           {"slides", {slide("l1", "LT one", "#001100"), slide("l2", "LT two", "#002200")}}}}},
    });
    normalize(doc);
    return doc;
}

Document line_control_fortress()
{
    auto doc = Document::from_json(seventhslide::test::load_json(
        seventhslide::test::fixtures_dir("freeform") / "line_control_fortress.json"));
    normalize(doc);
    return doc;
}

/// ``copy`` with ``like``'s ids, its slide's and its objects': what is left
/// to tell them apart is everything else.
Slide with_ids_of(Slide copy, const Slide& like)
{
    copy.id = like.id;
    REQUIRE(copy.objects.size() == like.objects.size());
    for (std::size_t i = 0; i < copy.objects.size(); ++i) {
        copy.objects[i].id = like.objects[i].id;
    }
    return copy;
}

/// Every slide and object id in ``doc``, each once.
bool ids_unique(const Document& doc)
{
    std::set<std::string> seen;
    for (const auto& deck : doc.decks) {
        for (const auto& slide : deck.slides) {
            if (!seen.insert(slide.id).second) {
                return false;
            }
            for (const auto& obj : slide.objects) {
                if (!seen.insert(obj.id).second) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("Every output's slide is duplicated after it, with ids of its own",
          "[freeform][slide_copy]")
{
    auto doc = two_outputs();
    const auto before = doc;
    REQUIRE(duplicate_slide(doc, 0));
    for (std::size_t d = 0; d < doc.decks.size(); ++d) {
        INFO("deck " << doc.decks[d].output_id);
        const auto& slides = doc.decks[d].slides;
        const auto& was = before.decks[d].slides;
        REQUIRE(slides.size() == 3);
        CHECK(slides[0] == was[0]);
        CHECK(slides[2] == was[1]);
        CHECK(slides[1].id != was[0].id);
        CHECK(slides[1].background == was[0].background);
        CHECK(with_ids_of(slides[1], was[0]).objects == was[0].objects);
        // Grouped objects stay grouped on the copy.
        CHECK(slides[1].objects[0].group_id == slides[1].objects[1].group_id);
        CHECK(doc.decks[d].line_to_slide == std::vector<int>({0, 1, 2}));
    }
    CHECK(ids_unique(doc));
}

TEST_CASE("Under line control a slide is duplicated on its own output, with no lines",
          "[freeform][slide_copy]")
{
    auto doc = line_control_fortress();
    const auto before = doc;
    REQUIRE(duplicate_slide(doc, 0, "out-main"));
    const auto& main = *find_deck(doc, "out-main");
    const auto& was = *find_deck(before, "out-main");
    REQUIRE(main.slide_count() == was.slide_count() + 1);
    CHECK(with_ids_of(main.slides[1], was.slides[0]).objects == was.slides[0].objects);
    // The lines stay on the slide they were on; the copy holds none.
    CHECK(main.line_to_slide == std::vector<int>({0, 0, 0, 0, 2, 2, 2, 2}));
    CHECK(*find_deck(doc, "out-lt") == *find_deck(before, "out-lt"));
    CHECK(doc.cues == before.cues);
    CHECK(ids_unique(doc));
}

TEST_CASE("A slide out of range is not duplicated", "[freeform][slide_copy]")
{
    auto doc = two_outputs();
    const auto before = doc;
    CHECK_FALSE(duplicate_slide(doc, 2));
    CHECK(doc == before);
    CHECK_FALSE(duplicate_slide(doc, 0, "out-main"));
    CHECK(doc == before);
}

TEST_CASE("The editor duplicates the slide it is on, as one undo step", "[freeform][slide_copy]")
{
    Editor ed;
    ed.load(two_outputs(), "out-main");
    const auto before = ed.document();
    ed.select_slide(1);
    REQUIRE(ed.duplicate_slide());
    CHECK(ed.slide_index() == 2);
    CHECK(ed.slide_count() == 3);
    CHECK(ed.current_slide()->objects.front().text == "Main two");
    REQUIRE(ed.undo());
    CHECK(ed.document() == before);
    CHECK_FALSE(ed.can_undo());
}

TEST_CASE("A duplicated slide keeps its transitions and notes", "[freeform][slide_copy]")
{
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    Transition fade;
    fade.type = TransitionType::Fade;
    fade.duration_ms = 800;
    Transition wipe;
    wipe.type = TransitionType::Wipe;
    wipe.duration_ms = 300;
    const auto dressed = [&](Document doc) {
        for (auto& deck : doc.decks) {
            auto& first = deck.slides[0];
            first.transition = fade;
            first.background_transition = wipe;
            first.notes = "Pause here";
        }
        return doc;
    };
    SECTION("on every output")
    {
        auto doc = dressed(two_outputs());
        REQUIRE(duplicate_slide(doc, 0));
        for (const auto& deck : doc.decks) {
            INFO("deck " << deck.output_id);
            const auto& copy = deck.slides[1];
            CHECK(copy.transition == fade);
            CHECK(copy.background_transition == wipe);
            CHECK(copy.notes == "Pause here");
            CHECK(with_ids_of(copy, deck.slides[0]) == deck.slides[0]);
        }
    }
    SECTION("under line control")
    {
        auto doc = dressed(line_control_fortress());
        REQUIRE(duplicate_slide(doc, 0, "out-main"));
        const auto& main = *find_deck(doc, "out-main");
        CHECK(with_ids_of(main.slides[1], main.slides[0]) == main.slides[0]);
        CHECK(ids_unique(doc));
    }
}

TEST_CASE("A slide either side of the deck is not duplicated", "[freeform][slide_copy]")
{
    auto doc = two_outputs();
    const auto before = doc;
    CHECK_FALSE(duplicate_slide(doc, -1));
    CHECK(doc == before);
    auto lines = line_control_fortress();
    const auto had = lines;
    CHECK_FALSE(duplicate_slide(lines, -1, "out-main"));
    CHECK_FALSE(duplicate_slide(lines, 2, "out-main"));
    CHECK(lines == had);
}

TEST_CASE("Duplicating a slide copies what copying and pasting it does",
          "[freeform][slide_copy]")
{
    using seventhslide::model::Transition;
    using seventhslide::model::TransitionType;
    auto doc = two_outputs();
    for (auto& deck : doc.decks) {
        auto& first = deck.slides[0];
        first.transition = Transition{};
        first.transition->type = TransitionType::Push;
        first.transition->duration_ms = 500;
        first.notes = "Notes on " + deck.output_id;
    }
    Editor duplicated;
    duplicated.load(doc, "out-main");
    REQUIRE(duplicated.duplicate_slide());

    Editor pasted;
    pasted.load(doc, "out-main");
    pasted.select_slide(0);
    REQUIRE(pasted.copy_slides());
    REQUIRE(pasted.paste());

    for (const auto& deck : duplicated.document().decks) {
        INFO("deck " << deck.output_id);
        const auto* other = find_deck(pasted.document(), deck.output_id);
        REQUIRE(other != nullptr);
        REQUIRE(other->slide_count() == deck.slide_count());
        for (int s = 0; s < deck.slide_count(); ++s) {
            const auto i = static_cast<std::size_t>(s);
            CHECK(with_ids_of(other->slides[i], deck.slides[i]) == deck.slides[i]);
        }
    }
}
