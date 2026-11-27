#pragma once

// Turning what an operator typed ("John 3:16-18", "Jude 4", "Psalm 23") into a
// passage from one bible.
//
// The console and the phone remote both take references and share this one
// parse, single-chapter-book rule, set of refusal messages and reference
// formatting, so the two cannot disagree about what a reference means.
//
// Seam: BibleRepository (Qt SQL).

#include "seventhslide/db/bible_repository.hpp"
#include "seventhslide/model/content.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

struct BibleLookup {
    /// False when the reference could not be resolved; ``message`` says why,
    /// in words meant for the operator.
    bool ok = false;
    std::string message;

    std::string book;
    int chapter = 0;
    int verse_start = 0;
    int verse_end = 0;
    /// Display form: "John 3", "John 3:16", or "John 3:16-18".
    std::string ref;
    std::vector<model::BibleVerse> verses;
};

/// "John 3:16", "John 3:16-18", or "John 3" when ``verse_start`` is 0.
[[nodiscard]] std::string format_reference(std::string_view book,
                                           int chapter,
                                           int verse_start,
                                           int verse_end);

/**
 * Resolve ``reference`` against ``bible_id``.
 *
 * Trailing punctuation an operator leaves while typing (": . -" and spaces) is
 * ignored. A reference with no verse means the whole chapter. In a
 * single-chapter book a bare trailing number is read as the verse, which is how
 * "Jude 4" is conventionally written. A verse range that runs past the end of
 * the chapter is clamped rather than refused.
 */
[[nodiscard]] BibleLookup resolve_reference(BibleRepository& repo,
                                            std::int64_t bible_id,
                                            std::string_view reference);

}  // namespace seventhslide::db
