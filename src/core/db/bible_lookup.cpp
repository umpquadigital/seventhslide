#include "seventhslide/db/bible_lookup.hpp"

#include "seventhslide/parse/bible.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace seventhslide::db {
namespace {

[[nodiscard]] BibleLookup refuse(std::string message)
{
    BibleLookup out;
    out.message = std::move(message);
    return out;
}

/// Drop the punctuation an operator leaves behind mid-type: "John 3:" is still
/// John 3.
[[nodiscard]] std::string_view trim_reference(std::string_view s)
{
    const auto droppable = [](char c) {
        return c == ':' || c == '.' || c == '-'
               || util::is_space(c);
    };
    while (!s.empty() && droppable(s.back())) {
        s.remove_suffix(1);
    }
    while (!s.empty() && util::is_space(s.front())) {
        s.remove_prefix(1);
    }
    return s;
}

}  // namespace

std::string format_reference(std::string_view book, int chapter, int verse_start, int verse_end)
{
    std::string ref = std::string(book) + ' ' + std::to_string(chapter);
    if (verse_start <= 0) {
        return ref;
    }
    ref += ':' + std::to_string(verse_start);
    if (verse_end > verse_start) {
        ref += '-' + std::to_string(verse_end);
    }
    return ref;
}

BibleLookup resolve_reference(BibleRepository& repo,
                              std::int64_t bible_id,
                              std::string_view reference)
{
    if (!repo.get(bible_id)) {
        return refuse("Select a bible first.");
    }

    const auto trimmed = trim_reference(reference);
    if (trimmed.empty()) {
        return refuse("Enter a scripture reference such as John 3:16.");
    }

    const auto books = repo.list_books(bible_id);
    const auto parsed = parse::parse_bible_reference(std::string(trimmed), books);
    if (!parsed) {
        return refuse("Could not find \"" + std::string(trimmed) + "\" in this bible.");
    }

    std::string book = parsed->book;
    int chapter = parsed->chapter;
    std::optional<int> v_start = parsed->verse_start;
    std::optional<int> v_end = parsed->verse_end;

    const auto chapters = repo.list_chapters(bible_id, book);
    const auto has_chapter = [&chapters](int c) {
        return std::find(chapters.begin(), chapters.end(), c) != chapters.end();
    };
    // Single-chapter books: a bare trailing number is conventionally the verse.
    if (!v_start && chapters.size() == 1 && !has_chapter(chapter)) {
        v_start = v_end = chapter;
        chapter = chapters.front();
    }
    if (!has_chapter(chapter)) {
        return refuse(book + " has no chapter " + std::to_string(chapter) + ".");
    }

    auto chapter_verses = repo.get_chapter(bible_id, book, chapter);
    if (chapter_verses.empty()) {
        return refuse("No verses found for " + book + " " + std::to_string(chapter) + ".");
    }

    BibleLookup out;
    if (!v_start) {
        out.verse_start = chapter_verses.front().verse_num;
        out.verse_end = chapter_verses.back().verse_num;
        out.ref = format_reference(book, chapter, 0, 0);
    } else {
        const auto has_start = std::any_of(
            chapter_verses.begin(), chapter_verses.end(),
            [&](const model::BibleVerse& v) { return v.verse_num == *v_start; });
        if (!has_start) {
            return refuse(book + " " + std::to_string(chapter) + " has no verse "
                          + std::to_string(*v_start) + ".");
        }
        out.verse_start = *v_start;
        out.verse_end = std::min(v_end.value_or(*v_start), chapter_verses.back().verse_num);
        out.ref = format_reference(book, chapter, out.verse_start, out.verse_end);
    }

    for (auto& v : chapter_verses) {
        if (v.verse_num >= out.verse_start && v.verse_num <= out.verse_end) {
            out.verses.push_back(std::move(v));
        }
    }

    out.ok = true;
    out.book = std::move(book);
    out.chapter = chapter;
    return out;
}

}  // namespace seventhslide::db
