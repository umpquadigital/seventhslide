#include "seventhslide/db/song_repository.hpp"

#include "seventhslide/db/query.hpp"

#include "seventhslide/db/json_codec.hpp"
#include "seventhslide/db/service_repository.hpp"
#include "seventhslide/json/read.hpp"

#include <stdexcept>
#include <utility>

#include <QSqlQuery>
#include <QVariant>

namespace seventhslide::db {
namespace {

// The song columns, in the order bind_song_columns binds them and (after id)
// song_from_query reads them.
constexpr QLatin1StringView k_song_columns{
    "title, lyrics, verse_order, authors, songbook_name, songbook_entry, theme_map, "
    "copyright, ccli_song_number, show_copyright, key, topics, comments"};
// What the library's search reads, as song_from_query reads it: every
// column but ``comments`` (``''`` keeps its place).
constexpr QLatin1StringView k_searchable_columns{
    "id, title, lyrics, verse_order, authors, songbook_name, songbook_entry, theme_map, "
    "copyright, ccli_song_number, show_copyright, key, topics, ''"};

model::Song song_from_query(QSqlQuery& q)
{
    // A NULL column reads as an empty string.
    model::Song song;
    song.id = q.value(0).toLongLong();
    song.title = q.value(1).toString().toStdString();
    song.lyrics = q.value(2).toString().toStdString();
    song.verse_order = q.value(3).toString().toStdString();
    song.authors =
        model::authors_from_json(parse_json_text(q.value(4).toString(), nlohmann::json::array()));
    song.songbook_name = q.value(5).toString().toStdString();
    song.songbook_entry = q.value(6).toString().toStdString();
    song.theme_map = parse_json_text(q.value(7).toString(), nlohmann::json::object());
    if (!song.theme_map.is_object()) {
        song.theme_map = nlohmann::json::object();
    }
    song.copyright = q.value(8).toString().toStdString();
    song.ccli_song_number = q.value(9).toString().toStdString();
    song.show_copyright = q.value(10).toInt() != 0;
    song.key = q.value(11).toString().toStdString();
    const auto topics = parse_json_text(q.value(12).toString(), nlohmann::json::array());
    song.topics = model::normalize_song_topics(json::strings_in(topics));
    song.comments = q.value(13).toString().toStdString();
    return song;
}

void bind_song_columns(QSqlQuery& q, const model::Song& song)
{
    q.addBindValue(QString::fromStdString(song.title));
    q.addBindValue(QString::fromStdString(song.lyrics));
    q.addBindValue(QString::fromStdString(song.verse_order));
    q.addBindValue(json_to_qstring(model::authors_to_json(song.authors)));
    q.addBindValue(QString::fromStdString(song.songbook_name));
    q.addBindValue(QString::fromStdString(song.songbook_entry));
    q.addBindValue(json_to_qstring(song.theme_map.is_object() ? song.theme_map
                                                              : nlohmann::json::object()));
    q.addBindValue(QString::fromStdString(song.copyright));
    q.addBindValue(QString::fromStdString(song.ccli_song_number));
    q.addBindValue(song.show_copyright ? 1 : 0);
    q.addBindValue(QString::fromStdString(song.key));
    q.addBindValue(json_to_qstring(nlohmann::json(model::normalize_song_topics(song.topics))));
    q.addBindValue(QString::fromStdString(song.comments));
}

}  // namespace

std::int64_t SongRepository::insert(model::Song& song)
{
    const QString sql = QStringLiteral("INSERT INTO songs (") + k_song_columns
                        + QStringLiteral(") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    auto q = prepare(db_, sql, "insert song");
    bind_song_columns(q, song);
    exec(q, {}, "insert song");
    const auto id = db_.last_insert_id();
    song.id = id;
    return id;
}

void SongRepository::update(const model::Song& song)
{
    if (!song.id) {
        throw std::invalid_argument("SongRepository::update requires song.id");
    }
    const auto sql = QStringLiteral(
        "UPDATE songs SET title=?, lyrics=?, verse_order=?, authors=?, "
        "songbook_name=?, songbook_entry=?, theme_map=?, copyright=?, "
        "ccli_song_number=?, show_copyright=?, key=?, topics=?, comments=? WHERE id=?");
    auto q = prepare(db_, sql, "update song");
    bind_song_columns(q, song);
    q.addBindValue(static_cast<qint64>(*song.id));
    exec(q, {}, "update song");
}

std::optional<model::Song> SongRepository::get(std::int64_t id)
{
    const QString sql = QStringLiteral("SELECT id, ") + k_song_columns
                        + QStringLiteral(" FROM songs WHERE id = ?");
    auto q = exec(db_, sql, {static_cast<qint64>(id)}, "get song");
    if (!q.next()) {
        return std::nullopt;
    }
    return song_from_query(q);
}

std::vector<model::Song> SongRepository::list_summaries()
{
    // ``lyrics`` and ``comments`` are the long columns and no summary reads
    // them; ``''`` keeps the positions ``song_from_query`` expects without
    // fetching the text.
    const auto sql = QStringLiteral(
        "SELECT id, title, '', verse_order, authors, songbook_name, songbook_entry, "
        "theme_map, copyright, ccli_song_number, show_copyright, key, topics, '' "
        "FROM songs ORDER BY title COLLATE NOCASE ASC, id ASC");
    auto q = exec(db_, sql, {}, "list song summaries");
    std::vector<model::Song> out;
    while (q.next()) {
        out.push_back(song_from_query(q));
    }
    return out;
}

std::vector<model::Song> SongRepository::list_searchable()
{
    const QString sql = QStringLiteral("SELECT ") + k_searchable_columns
                        + QStringLiteral(" FROM songs ORDER BY title COLLATE NOCASE ASC, id ASC");
    auto q = exec(db_, sql, {}, "list searchable songs");
    std::vector<model::Song> out;
    while (q.next()) {
        out.push_back(song_from_query(q));
    }
    return out;
}

std::vector<model::Song> SongRepository::list_searchable(const std::set<std::string>& uids)
{
    // One indexed lookup each, prepared once.
    auto q = prepare(db_,
                     QStringLiteral("SELECT ") + k_searchable_columns
                         + QStringLiteral(" FROM songs WHERE uid = ?"),
                     "list searchable songs by uid");
    std::vector<model::Song> out;
    for (const auto& uid : uids) {
        exec(q, {QString::fromStdString(uid)}, "list searchable songs by uid");
        if (q.next()) {
            out.push_back(song_from_query(q));
        }
    }
    return out;
}

std::vector<std::int64_t> SongRepository::list_ids()
{
    auto q = exec(db_, QStringLiteral("SELECT id FROM songs ORDER BY id"), {}, "list song ids");
    std::vector<std::int64_t> out;
    while (q.next()) {
        out.push_back(q.value(0).toLongLong());
    }
    return out;
}

bool SongRepository::remove(std::int64_t id)
{
    Transaction tx(db_);
    ServiceRepository services(db_);
    services.detach_songs({id});

    auto q = exec(db_, QStringLiteral("DELETE FROM songs WHERE id = ?"),
                  {static_cast<qint64>(id)}, "delete song");
    const bool removed = q.numRowsAffected() > 0;
    tx.commit();
    return removed;

}

void SongRepository::set_theme_maps(
    const std::vector<std::pair<std::int64_t, nlohmann::json>>& maps)
{
    if (maps.empty()) {
        return;
    }
    Transaction tx(db_);
    // Prepared once: the statement is the same for every row.
    auto q = prepare(db_, QStringLiteral("UPDATE songs SET theme_map = ? WHERE id = ?"),
                     "set theme map");
    for (const auto& [id, tm] : maps) {
        exec(q, {json_to_qstring(tm.is_object() ? tm : nlohmann::json::object()),
                 static_cast<qint64>(id)},
             "set theme map");
    }
    tx.commit();

}

void SongRepository::set_verse_orders(
    const std::vector<std::pair<std::int64_t, std::string>>& orders)
{
    if (orders.empty()) {
        return;
    }
    Transaction tx(db_);
    auto q = prepare(db_, QStringLiteral("UPDATE songs SET verse_order = ? WHERE id = ?"),
                     "set verse order");
    for (const auto& [id, order] : orders) {
        exec(q, {QString::fromStdString(order), static_cast<qint64>(id)}, "set verse order");
    }
    tx.commit();

}

}  // namespace seventhslide::db
