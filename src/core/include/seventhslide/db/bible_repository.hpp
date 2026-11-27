#pragma once

// Bible library + verses. XML parsing lives in parse/bible.hpp.
// Seam: Qt SQL via Database.

#include "seventhslide/db/database.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/library.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace seventhslide::db {

class BibleRepository {
public:
    explicit BibleRepository(Database& db) : db_(db) {}

    /// Create bible + verses atomically. Returns bible id. Throws
    /// ``std::runtime_error``, adding nothing, for one too large to sync
    /// (``k_largest_record``).
    [[nodiscard]] std::int64_t import_bible(std::string_view name,
                                            std::string_view copyright,
                                            const std::vector<model::BibleVerse>& verses);

    [[nodiscard]] std::optional<model::Bible> get(std::int64_t id);
    [[nodiscard]] std::vector<model::Bible> list_all();
    bool remove(std::int64_t id); // CASCADE deletes verses
    /// Rename a bible translation. Returns false if id is missing.
    bool rename(std::int64_t id, std::string_view name);
    /// Set a translation's short name (normalize_bible_abbreviation). Returns
    /// false if id is missing.
    bool set_abbreviation(std::int64_t id, std::string_view abbreviation);
    /// Offer a translation for lookup, or not. Returns false if id is missing.
    bool set_enabled(std::int64_t id, bool enabled);

    /// Book names in import order (``MIN(id)``). Read once per bible and kept:
    /// the book box asks on every keystroke, and the query reads every verse.
    [[nodiscard]] std::vector<std::string> list_books(std::int64_t bible_id);
    [[nodiscard]] std::vector<int> list_chapters(std::int64_t bible_id, std::string_view book);
    [[nodiscard]] std::vector<model::BibleVerse> get_chapter(std::int64_t bible_id,
                                                             std::string_view book,
                                                             int chapter);
    [[nodiscard]] std::vector<model::BibleVerse> get_verse_range(std::int64_t bible_id,
                                                                 std::string_view book,
                                                                 int chapter,
                                                                 int verse_start,
                                                                 int verse_end);

    /// Verses ``verse_start``..``verse_end`` of a chapter as a passage: its
    /// reference, the translation's name and the verse text. Null when the
    /// range is malformed or holds no verses.
    [[nodiscard]] std::optional<model::BiblePassage> passage(std::int64_t bible_id,
                                                             std::string_view book,
                                                             int chapter,
                                                             int verse_start,
                                                             int verse_end);

    /// The library has had its copy of the shipped Bibles.
    [[nodiscard]] bool bundled_taken();
    void note_bundled_taken();

private:
    Database& db_;
    /// ``list_books`` by bible id, as the database was at ``books_version_``
    /// (SQLite's ``data_version``). This connection writes verses only
    /// through ``import_bible`` and ``remove``, which drop what they change;
    /// another connection's commit (the sync's) changes the version, and then
    /// all are read again.
    std::unordered_map<std::int64_t, std::vector<std::string>> books_;
    int books_version_ = -1;
};

}  // namespace seventhslide::db
