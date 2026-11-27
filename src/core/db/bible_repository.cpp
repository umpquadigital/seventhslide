#include "seventhslide/db/bible_repository.hpp"

#include "seventhslide/db/bible_lookup.hpp"
#include "seventhslide/db/query.hpp"
#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_records.hpp"

#include <QSqlQuery>
#include <QVariant>

#include <stdexcept>
#include <string>

namespace seventhslide::db {
namespace {

/// Sizes are said in decimal megabytes, as the snapshot writer says them.
constexpr std::size_t k_megabyte = 1000 * 1000;

// The columns bible_from_row reads, in its order.
constexpr QLatin1StringView k_bible_columns{
    "id, name, copyright, created_at, abbreviation, enabled"};

model::Bible bible_from_row(QSqlQuery& q)
{
    model::Bible b;
    b.id = q.value(0).toLongLong();
    b.name = q.value(1).toString().toStdString();
    b.copyright = q.value(2).toString().toStdString();
    b.created_at = q.value(3).toString().toStdString();
    b.abbreviation = q.value(4).toString().toStdString();
    b.enabled = q.value(5).toInt() != 0;
    return b;
}

}  // namespace

std::int64_t BibleRepository::import_bible(std::string_view name,
                                           std::string_view copyright,
                                           const std::vector<model::BibleVerse>& verses)
{
    // One transaction and one prepared insert: a bible is ~31k verses, and a
    // statement per verse would be most of an import.
    Transaction tx(db_);
    exec(db_, QStringLiteral("INSERT INTO bibles (name, copyright) VALUES (?, ?)"),
         {sql_text(name), sql_text(copyright)}, "import bible");
    const auto id = db_.last_insert_id();
    // Asked about before it existed (a remote holding an unknown id): forget that.
    books_.erase(id);

    const auto sql = QStringLiteral(
        "INSERT INTO verses (bible_id, book, chapter, verse_num, text) VALUES (?, ?, ?, ?, ?)");
    auto iq = prepare(db_, sql, "import verse");
    for (const auto& v : verses) {
        exec(iq,
             {static_cast<qint64>(id), QString::fromStdString(v.book), v.chapter, v.verse_num,
              QString::fromStdString(v.text)},
             "import verse");
    }
    // Measured as sync sends it: a Bible too large to sync would stay on this
    // computer alone.
    auto uq = exec(db_, QStringLiteral("SELECT uid FROM bibles WHERE id = ?"), {static_cast<qint64>(id)},
                   "read the bible's uid");
    if (uq.next()) {
        const auto record = read_record(db_, "bibles", uq.value(0).toString().toStdString());
        const auto bytes = record ? record->dump().size() : 0;
        if (bytes > k_largest_record) {
            // Its size rounded up and the most rounded down: never the same.
            throw std::runtime_error("This Bible is too large to sync ("
                                     + std::to_string((bytes + k_megabyte - 1) / k_megabyte) + " MB; the most is "
                                     + std::to_string(k_largest_record / k_megabyte) + " MB).");
        }
    }
    tx.commit();
    return id;
}

std::optional<model::Bible> BibleRepository::get(std::int64_t id)
{
    const QString sql = QStringLiteral("SELECT ") + k_bible_columns
                        + QStringLiteral(" FROM bibles WHERE id = ?");
    auto q = exec(db_, sql, {static_cast<qint64>(id)}, "get bible");
    if (!q.next()) {
        return std::nullopt;
    }
    return bible_from_row(q);
}

std::vector<model::Bible> BibleRepository::list_all()
{
    const QString sql = QStringLiteral("SELECT ") + k_bible_columns
                        + QStringLiteral(" FROM bibles ORDER BY name COLLATE NOCASE ASC");
    auto q = exec(db_, sql, {}, "list bibles");
    std::vector<model::Bible> out;
    while (q.next()) {
        out.push_back(bible_from_row(q));
    }
    return out;
}

bool BibleRepository::remove(std::int64_t id)
{
    auto q = exec(db_, QStringLiteral("DELETE FROM bibles WHERE id = ?"),
                  {static_cast<qint64>(id)}, "delete bible");
    books_.erase(id);
    return q.numRowsAffected() > 0;
}

bool BibleRepository::rename(std::int64_t id, std::string_view name)
{
    auto q = exec(db_, QStringLiteral("UPDATE bibles SET name = ? WHERE id = ?"),
                  {sql_text(name), static_cast<qint64>(id)}, "rename bible");
    return q.numRowsAffected() > 0;
}

bool BibleRepository::set_abbreviation(std::int64_t id, std::string_view abbreviation)
{
    auto q = exec(db_, QStringLiteral("UPDATE bibles SET abbreviation = ? WHERE id = ?"),
                  {sql_text(model::normalize_bible_abbreviation(abbreviation)),
                   static_cast<qint64>(id)},
                  "set bible abbreviation");
    return q.numRowsAffected() > 0;
}

bool BibleRepository::set_enabled(std::int64_t id, bool enabled)
{
    auto q = exec(db_, QStringLiteral("UPDATE bibles SET enabled = ? WHERE id = ?"),
                  {enabled ? 1 : 0, static_cast<qint64>(id)}, "set bible enabled");
    return q.numRowsAffected() > 0;
}

std::vector<std::string> BibleRepository::list_books(std::int64_t bible_id)
{
    if (const int version = db_.query_int("PRAGMA data_version", -1); version < 0 || version != books_version_) {
        books_.clear();
        books_version_ = version;
    }
    if (const auto it = books_.find(bible_id); it != books_.end()) {
        return it->second;
    }
    const auto sql = QStringLiteral(
        "SELECT book FROM verses WHERE bible_id = ? "
        "GROUP BY book ORDER BY MIN(id) ASC");
    auto q = exec(db_, sql, {static_cast<qint64>(bible_id)}, "list books");
    std::vector<std::string> out;
    while (q.next()) {
        out.push_back(q.value(0).toString().toStdString());
    }
    books_.insert_or_assign(bible_id, out);
    return out;
}

std::vector<int> BibleRepository::list_chapters(std::int64_t bible_id, std::string_view book)
{
    const auto sql = QStringLiteral(
        "SELECT DISTINCT chapter FROM verses WHERE bible_id = ? AND book = ? "
        "ORDER BY chapter ASC");
    auto q = exec(db_, sql, {static_cast<qint64>(bible_id), sql_text(book)}, "list chapters");
    std::vector<int> out;
    while (q.next()) {
        out.push_back(q.value(0).toInt());
    }
    return out;
}

std::vector<model::BibleVerse> BibleRepository::get_chapter(std::int64_t bible_id,
                                                            std::string_view book,
                                                            int chapter)
{
    return get_verse_range(bible_id, book, chapter, 1, 100000);
}

std::vector<model::BibleVerse> BibleRepository::get_verse_range(std::int64_t bible_id,
                                                                std::string_view book,
                                                                int chapter,
                                                                int verse_start,
                                                                int verse_end)
{
    const auto sql = QStringLiteral(
        "SELECT id, bible_id, book, chapter, verse_num, text FROM verses "
        "WHERE bible_id = ? AND book = ? AND chapter = ? "
        "AND verse_num >= ? AND verse_num <= ? "
        "ORDER BY verse_num ASC");
    auto q = exec(db_, sql,
                  {static_cast<qint64>(bible_id),
                   sql_text(book),
                   chapter,
                   verse_start,
                   verse_end},
                  "verse range");
    std::vector<model::BibleVerse> out;
    while (q.next()) {
        model::BibleVerse v;
        v.id = q.value(0).toLongLong();
        v.bible_id = q.value(1).toLongLong();
        v.book = q.value(2).toString().toStdString();
        v.chapter = q.value(3).toInt();
        v.verse_num = q.value(4).toInt();
        v.text = q.value(5).toString().toStdString();
        out.push_back(std::move(v));
    }
    return out;
}

std::optional<model::BiblePassage> BibleRepository::passage(std::int64_t bible_id,
                                                            std::string_view book,
                                                            int chapter,
                                                            int verse_start,
                                                            int verse_end)
{
    if (book.empty() || chapter <= 0 || verse_start <= 0 || verse_end < verse_start) {
        return std::nullopt;
    }
    const auto verses = get_verse_range(bible_id, book, chapter, verse_start, verse_end);
    if (verses.empty()) {
        return std::nullopt;
    }
    model::BiblePassage passage;
    passage.bible_id = bible_id;
    passage.book = std::string(book);
    passage.chapter = chapter;
    passage.verse_start = verse_start;
    passage.verse_end = verse_end;
    passage.ref = format_reference(book, chapter, verse_start, verse_end);
    if (const auto bible = get(bible_id)) {
        passage.version = bible->name;
    }
    passage.verses.reserve(verses.size());
    for (const auto& v : verses) {
        passage.verses.push_back({v.verse_num, v.text});
    }
    return passage;
}

bool BibleRepository::bundled_taken()
{
    return exec(db_, QStringLiteral("SELECT 1 FROM library_meta WHERE key = ?"),
                {sql_text(k_bundled_bibles_key)}, "read whether the shipped Bibles were copied in")
        .next();
}

void BibleRepository::note_bundled_taken()
{
    exec(db_,
         QStringLiteral("INSERT INTO library_meta (key, value) VALUES (?, '1') "
                        "ON CONFLICT(key) DO NOTHING"),
         {sql_text(k_bundled_bibles_key)}, "note that the shipped Bibles were copied in");
}

}  // namespace seventhslide::db
