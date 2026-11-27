// canvas::EditHistory - the undo machinery both canvas editors share.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/canvas/edit_history.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using seventhslide::canvas::EditHistory;

namespace {

/// Stand-in document: a value type that compares equal by content, with
/// pages a step may keep one of. It counts its comparisons.
struct Doc {
    std::string text;
    std::vector<std::string> pages;

    static inline int compares = 0;

    friend bool operator==(const Doc& a, const Doc& b)
    {
        ++compares;
        return a.text == b.text && a.pages == b.pages;
    }
};

/// One page of a Doc, for an edit of that page alone.
struct Page {
    std::size_t at = 0;
    std::string text;

    [[nodiscard]] bool swap_with(Doc& doc)
    {
        if (at >= doc.pages.size()) {
            return false;
        }
        std::swap(doc.pages[at], text);
        return true;
    }
    [[nodiscard]] bool same_place(const Page& other) const { return at == other.at; }
    [[nodiscard]] Page hollow() const { return Page{at, {}}; }

    friend bool operator==(const Page&, const Page&) = default;
};

/// A Page whose move throws once ``moves_until_throw`` more have gone by,
/// for a step that cannot be added to the stack.
struct BrittlePage {
    std::size_t at = 0;
    std::string text;

    static inline int moves_until_throw = -1;

    BrittlePage(std::size_t page, std::string t) : at(page), text(std::move(t)) {}
    BrittlePage(const BrittlePage&) = default;
    BrittlePage(BrittlePage&& other) : at(other.at)
    {
        if (moves_until_throw == 0) {
            moves_until_throw = -1;
            throw std::runtime_error("no room");
        }
        if (moves_until_throw > 0) {
            --moves_until_throw;
        }
        text = std::move(other.text);
    }
    BrittlePage& operator=(const BrittlePage&) = default;
    BrittlePage& operator=(BrittlePage&&) = default;
    ~BrittlePage() = default;

    [[nodiscard]] bool swap_with(Doc& doc)
    {
        if (at >= doc.pages.size()) {
            return false;
        }
        std::swap(doc.pages[at], text);
        return true;
    }
    [[nodiscard]] bool same_place(const BrittlePage& other) const { return at == other.at; }
    [[nodiscard]] BrittlePage hollow() const { return BrittlePage{at, {}}; }

    friend bool operator==(const BrittlePage&, const BrittlePage&) = default;
};

using History = EditHistory<Doc, Page>;

/// ``page`` of ``doc`` as it is, for ``push``.
[[nodiscard]] Page page_of(const Doc& doc, std::size_t page)
{
    return Page{page, doc.pages[page]};
}

/// Write ``text`` on ``page`` of ``doc`` as one step of ``h``.
void write_page(History& h, Doc& doc, std::size_t page, std::string text)
{
    auto part = page_of(doc, page);
    (void)h.push(part, doc);
    doc.pages[page] = std::move(text);
    h.keep();
    h.refresh_dirty(doc, Page{page, {}});
}

}  // namespace

TEST_CASE("EditHistory tracks dirty against the baseline it was reset to", "[canvas]")
{
    History h;
    Doc doc{"a"};
    h.reset(doc);
    REQUIRE_FALSE(h.dirty());
    REQUIRE_FALSE(h.can_undo());
    REQUIRE_FALSE(h.can_redo());

    REQUIRE(h.push(doc));
    doc.text = "ab";
    h.refresh_dirty(doc);
    REQUIRE(h.dirty());
    REQUIRE(h.can_undo());

    // Editing back to the baseline by hand is not dirty, whatever the stack holds.
    doc.text = "a";
    h.refresh_dirty(doc);
    REQUIRE_FALSE(h.dirty());
    REQUIRE(h.can_undo());
}

TEST_CASE("EditHistory undo and redo move the document", "[canvas]")
{
    History h;
    Doc doc{"one"};
    h.reset(doc);

    REQUIRE(h.push(doc));
    doc.text = "two";
    h.refresh_dirty(doc);

    REQUIRE(h.undo(doc));
    REQUIRE(doc.text == "one");
    REQUIRE_FALSE(h.dirty());
    REQUIRE_FALSE(h.can_undo());
    REQUIRE(h.can_redo());

    REQUIRE(h.redo(doc));
    REQUIRE(doc.text == "two");
    REQUIRE(h.dirty());

    // Nothing left to redo, and the document is left alone when there is not.
    REQUIRE_FALSE(h.redo(doc));
    REQUIRE(doc.text == "two");

    // A fresh step drops the redo branch, once its edit is kept.
    REQUIRE(h.undo(doc));
    REQUIRE(h.push(doc));
    REQUIRE(h.can_redo());
    h.keep();
    REQUIRE_FALSE(h.can_redo());
}

TEST_CASE("EditHistory collapses a gesture into one undo step", "[canvas]")
{
    History h;
    Doc doc{"start"};
    h.reset(doc);

    h.begin_gesture();
    for (int i = 0; i < 20; ++i) {  // a drag, one setGeometries per mouse move
        const bool pushed = h.push(doc);
        REQUIRE(pushed == (i == 0));
        doc.text = "move" + std::to_string(i);
    }
    h.end_gesture();
    h.refresh_dirty(doc);

    REQUIRE(h.undo(doc));
    REQUIRE(doc.text == "start");
    REQUIRE_FALSE(h.can_undo());
}

TEST_CASE("EditHistory begin_gesture is re-entrant", "[canvas]")
{
    // A nested begin must not clear the "already snapshotted" flag: that would
    // split one drag into two undo steps.
    History h;
    Doc doc{"start"};
    h.reset(doc);

    h.begin_gesture();
    REQUIRE(h.push(doc));
    h.begin_gesture();
    REQUIRE_FALSE(h.push(doc));
    h.end_gesture();
    REQUIRE_FALSE(h.in_gesture());
}

TEST_CASE("EditHistory rollback keeps a gesture's snapshot", "[canvas]")
{
    // An edit inside a gesture that fails must not take the gesture's
    // snapshot with it, or the edits already made inside the gesture stop
    // being undoable.
    History h;
    Doc doc{"start"};
    h.reset(doc);

    h.begin_gesture();
    const bool first = h.push(doc);
    REQUIRE(first);
    doc.text = "moved";

    const bool second = h.push(doc);  // same gesture: no snapshot taken
    REQUIRE_FALSE(second);
    h.rollback(second, doc);          // the failed edit cleans up
    CHECK(doc.text == "moved");

    REQUIRE(h.can_undo());
    h.end_gesture();
    h.refresh_dirty(doc);
    REQUIRE(h.undo(doc));
    REQUIRE(doc.text == "start");
}

TEST_CASE("EditHistory rollback restores the document too", "[canvas]")
{
    History h;
    Doc doc{"start"};
    h.reset(doc);

    const bool pushed = h.push(doc);
    doc.text = "half written";  // a mutation that wrote, then failed
    h.rollback(pushed, doc);
    REQUIRE(doc.text == "start");
    REQUIRE_FALSE(h.can_undo());
}

TEST_CASE("EditHistory caps the undo depth", "[canvas]")
{
    History h;
    Doc doc{"0"};
    h.reset(doc);
    for (std::size_t i = 1; i <= History::k_max_depth + 10; ++i) {
        REQUIRE(h.push(doc));
        doc.text = std::to_string(i);
        h.keep();
    }
    std::size_t depth = 0;
    while (h.undo(doc)) {
        ++depth;
    }
    REQUIRE(depth == History::k_max_depth);
    // The oldest states fell off the bottom, so the baseline is not reachable.
    REQUIRE(doc.text != "0");
}

TEST_CASE("EditHistory: a step taken back leaves redo and the oldest step", "[canvas]")
{
    History h;
    Doc doc{"0", {"page"}};
    h.reset(doc);
    for (std::size_t i = 1; i <= History::k_max_depth; ++i) {
        REQUIRE(h.push(doc));
        doc.text = std::to_string(i);
        h.keep();
    }
    const Doc full = doc;

    // Refused, of the whole document and of a page, with the stack full.
    const auto refuse_both = [&] {
        REQUIRE(h.push(doc));
        doc.text = "half written";
        h.rollback(true, doc);
        auto part = page_of(doc, 0);
        REQUIRE(h.push(part, doc));
        doc.pages[0] = "half written";
        h.rollback(true, doc);
    };
    refuse_both();
    CHECK(doc == full);

    // And after an undo, which leaves a step to redo.
    REQUIRE(h.undo(doc));
    refuse_both();
    REQUIRE(h.can_redo());
    REQUIRE(h.redo(doc));
    CHECK(doc == full);

    // Every step is still there, back to the start.
    std::size_t depth = 0;
    while (h.undo(doc)) {
        ++depth;
    }
    CHECK(depth == History::k_max_depth);
    CHECK(doc.text == "0");
}

TEST_CASE("EditHistory clear forgets the baseline", "[canvas]")
{
    History h;
    Doc doc{"a"};
    h.reset(doc);
    REQUIRE(h.push(doc));
    h.clear();
    REQUIRE_FALSE(h.can_undo());
    REQUIRE_FALSE(h.can_redo());
    REQUIRE_FALSE(h.dirty());
    h.refresh_dirty(Doc{});  // an empty document matches the cleared baseline
    REQUIRE_FALSE(h.dirty());
}

TEST_CASE("EditHistory undoes and redoes a step of one page", "[canvas]")
{
    History h;
    Doc doc{"title", {"one", "two"}};
    h.reset(doc);

    write_page(h, doc, 1, "TWO");
    REQUIRE(h.dirty());
    REQUIRE(h.undo(doc));
    CHECK(doc.pages == std::vector<std::string>{"one", "two"});
    CHECK_FALSE(h.dirty());
    REQUIRE(h.redo(doc));
    CHECK(doc.pages == std::vector<std::string>{"one", "TWO"});
    CHECK(h.dirty());

    // A failed edit of the page puts it back and leaves no step.
    auto part = page_of(doc, 0);
    const bool pushed = h.push(part, doc);
    REQUIRE(pushed);
    doc.pages[0] = "half written";
    h.rollback(pushed, doc);
    CHECK(doc.pages[0] == "one");
    REQUIRE(h.undo(doc));
    CHECK(doc.pages[1] == "two");
    CHECK_FALSE(h.can_undo());
}

TEST_CASE("EditHistory: a gesture's step takes the whole document for an edit past its page",
          "[canvas]")
{
    History h;
    Doc doc{"title", {"one", "two"}};
    const Doc start = doc;
    h.reset(doc);

    h.begin_gesture();
    write_page(h, doc, 0, "ONE");
    write_page(h, doc, 0, "ONE!");
    write_page(h, doc, 1, "TWO");
    REQUIRE_FALSE(h.push(doc));
    doc.text = "TITLE";
    h.refresh_dirty(doc);
    h.end_gesture();

    REQUIRE(h.undo(doc));
    CHECK(doc == start);
    CHECK_FALSE(h.can_undo());
    CHECK_FALSE(h.dirty());
    REQUIRE(h.redo(doc));
    CHECK(doc == Doc{"TITLE", {"ONE!", "TWO"}});
}

TEST_CASE("EditHistory: dirty after edits of pages is whether the document differs", "[canvas]")
{
    History h;
    Doc doc{"title", {"one", "two"}};
    h.reset(doc);

    write_page(h, doc, 0, "ONE");
    CHECK(h.dirty());
    write_page(h, doc, 0, "one");
    CHECK_FALSE(h.dirty());

    write_page(h, doc, 0, "ONE");
    write_page(h, doc, 1, "TWO");
    write_page(h, doc, 1, "two");
    CHECK(h.dirty());  // page one still differs
    write_page(h, doc, 0, "one");
    CHECK_FALSE(h.dirty());

    // A change past the pages is seen by a page's edit after it.
    doc.text = "TITLE";
    h.refresh_dirty(doc);
    write_page(h, doc, 0, "ONE");
    write_page(h, doc, 0, "one");
    CHECK(h.dirty());
    doc.text = "title";
    h.refresh_dirty(doc);
    CHECK_FALSE(h.dirty());

    // Undone and redone, too.
    while (h.undo(doc)) {
    }
    CHECK_FALSE(h.dirty());
    while (h.redo(doc)) {
    }
    CHECK_FALSE(h.dirty());
}

TEST_CASE("EditHistory compares the rest of the document once for each page", "[canvas]")
{
    History h;
    Doc doc{"title", {"one", "two"}};
    h.reset(doc);

    write_page(h, doc, 0, "a");
    const int first = Doc::compares;
    for (int i = 0; i < 10; ++i) {
        write_page(h, doc, 0, std::to_string(i));
    }
    CHECK(Doc::compares == first);
    write_page(h, doc, 1, "b");
    CHECK(Doc::compares == first + 1);
}

TEST_CASE("EditHistory: a page the baseline does not have is a change", "[canvas]")
{
    History h;
    Doc doc{"title", {"one"}};
    h.reset(doc);
    doc.pages.emplace_back("two");
    h.refresh_dirty(doc);
    write_page(h, doc, 1, "TWO");
    CHECK(h.dirty());
}

TEST_CASE("EditHistory: an edit after an undo inside a gesture is a step of its own", "[canvas]")
{
    History h;
    Doc doc{"title", {"one", "two"}};
    h.reset(doc);

    h.begin_gesture();
    write_page(h, doc, 0, "ONE");
    REQUIRE(h.undo(doc));
    write_page(h, doc, 1, "TWO");
    h.end_gesture();

    REQUIRE(h.undo(doc));
    CHECK(doc == Doc{"title", {"one", "two"}});
    CHECK_FALSE(h.can_undo());
}

TEST_CASE("EditHistory: a step that could not be added leaves the gesture without one", "[canvas]")
{
    // Else the gesture's next edit takes the step before it for its own.
    EditHistory<Doc, BrittlePage> h;
    Doc doc{"title", {"one", "two"}};
    h.reset(doc);
    BrittlePage first{0, doc.pages[0]};
    REQUIRE(h.push(first, doc));
    doc.pages[0] = "ONE";
    h.keep();

    h.begin_gesture();
    BrittlePage second{1, doc.pages[1]};
    BrittlePage::moves_until_throw = 1;  // into the step, but not onto the stack
    CHECK_THROWS(h.push(second, doc));
    REQUIRE(h.push(doc));
    doc.text = "TITLE";
    h.keep();
    h.end_gesture();

    REQUIRE(h.undo(doc));
    CHECK(doc == Doc{"title", {"ONE", "two"}});
    REQUIRE(h.undo(doc));
    CHECK(doc == Doc{"title", {"one", "two"}});
    CHECK_FALSE(h.can_undo());
}
