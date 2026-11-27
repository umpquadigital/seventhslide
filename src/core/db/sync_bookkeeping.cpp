#include "seventhslide/db/sync_bookkeeping.hpp"

#include "seventhslide/db/json_codec.hpp"
#include "seventhslide/db/query.hpp"
#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_records.hpp"
#include "seventhslide/db/sync_tables.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace seventhslide::db {
namespace {

constexpr std::string_view k_counter_key = "counter";
constexpr std::string_view k_last_sent_key = "last_sent";

/// A shared copy from its row: document, versions, id_here.
SharedCopy shared_copy_of(const QSqlQuery& q, int first)
{
    SharedCopy copy;
    if (!q.value(first).isNull()) {
        copy.document = nlohmann::json::parse(q.value(first).toString().toStdString());
    }
    if (!q.value(first + 2).isNull()) {
        copy.id_here = q.value(first + 2).toLongLong();
    }
    copy.versions =
        versions_from_json(nlohmann::json::parse(q.value(first + 1).toString().toStdString()));
    return copy;
}

/// A held change from its row: table, key, machine, sequence, change.
HeldChange held_row(const QSqlQuery& q)
{
    return {q.value(0).toString().toStdString(), q.value(1).toString().toStdString(),
            q.value(2).toString().toStdString(), q.value(3).toLongLong(),
            nlohmann::json::parse(q.value(4).toString().toStdString())};
}

/// The held changes ``q`` reads, a row each as ``held_row``.
std::vector<HeldChange> held_rows(QSqlQuery& q)
{
    std::vector<HeldChange> out;
    while (q.next()) {
        out.push_back(held_row(q));
    }
    return out;
}

}  // namespace

nlohmann::json version_to_json(const SyncVersion& v)
{
    return nlohmann::json::array({v.counter, v.machine});
}

SyncVersion version_from_json(const nlohmann::json& j)
{
    if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_string()) {
        throw std::invalid_argument("Not a sync version: " + j.dump());
    }
    return {j[0].get<std::int64_t>(), j[1].get<std::string>()};
}

nlohmann::json versions_to_json(const std::map<std::string, SyncVersion>& versions)
{
    auto out = nlohmann::json::object();
    for (const auto& [field, version] : versions) {
        out[field] = version_to_json(version);
    }
    return out;
}

std::map<std::string, SyncVersion> versions_from_json(const nlohmann::json& j)
{
    if (!j.is_object()) {
        throw std::invalid_argument("Not the versions of fields: " + j.dump());
    }
    std::map<std::string, SyncVersion> out;
    for (const auto& [field, version] : j.items()) {
        out.emplace(field, version_from_json(version));
    }
    return out;
}

nlohmann::json media_digest_to_json(const MediaDigest& m)
{
    nlohmann::json out{{"path", m.path}, {"size", m.size}, {"root", m.root}};
    if (m.version) {
        out["version"] = version_to_json(*m.version);
    }
    return out;
}

MediaDigest media_digest_from_json(const nlohmann::json& j)
{
    if (!j.is_object() || !j.contains("path") || !j["path"].is_string()
        || !j.contains("size") || !j["size"].is_number_integer() || !j.contains("root")
        || !j["root"].is_string()) {
        throw std::invalid_argument("Not a media file's digest: " + j.dump());
    }
    MediaDigest m{j["path"].get<std::string>(), j["size"].get<std::int64_t>(),
                  j["root"].get<std::string>()};
    if (!is_sync_media_path(m.path) || m.size < 0) {
        throw std::invalid_argument("Not a media file of the library: " + m.path);
    }
    const bool hex = m.root.size() == 64
                     && std::ranges::all_of(m.root, [](char c) {
                            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                        });
    if (!hex) {
        throw std::invalid_argument("Not a media file's root, for " + m.path);
    }
    if (const auto version = j.find("version"); version != j.end()) {
        m.version = version_from_json(*version);
    }
    return m;
}

std::optional<SharedCopy> SyncBookkeeping::shared(std::string_view table, std::string_view key)
{
    auto q = exec(db_,
                  QStringLiteral("SELECT document, versions, id_here FROM sync_shared "
                                 "WHERE record_table = ? AND record_key = ?"),
                  {sql_text(table), sql_text(key)}, "read a shared copy");
    if (!q.next()) {
        return std::nullopt;
    }
    return shared_copy_of(q, 0);
}

void SyncBookkeeping::set_shared(std::string_view table, std::string_view key,
                                 const SharedCopy& copy)
{
    exec(db_,
         QStringLiteral("INSERT INTO sync_shared (record_table, record_key, document, versions, "
                        "id_here) VALUES (?, ?, ?, ?, ?) ON CONFLICT (record_table, record_key) "
                        "DO UPDATE SET document = excluded.document, "
                        "versions = excluded.versions, id_here = excluded.id_here"),
         {sql_text(table), sql_text(key),
          copy.document ? QVariant(json_to_qstring(*copy.document)) : QVariant(),
          json_to_qstring(versions_to_json(copy.versions)), sql_id(copy.id_here)},
         "write a shared copy");
}

std::vector<std::string> SyncBookkeeping::shared_keys(std::string_view table)
{
    auto q = exec(db_,
                  QStringLiteral("SELECT record_key FROM sync_shared WHERE record_table = ? "
                                 "AND document IS NOT NULL ORDER BY record_key"),
                  {sql_text(table)}, "list shared copies");
    std::vector<std::string> out;
    while (q.next()) {
        out.push_back(q.value(0).toString().toStdString());
    }
    return out;
}

void SyncBookkeeping::for_each_shared(const std::function<void(SharedRecord)>& take)
{
    auto q = prepare_forward_only(db_,
                                  QStringLiteral("SELECT record_table, record_key, document, versions, id_here "
                                                 "FROM sync_shared WHERE record_table = ? ORDER BY record_key"),
                                  "list shared copies");
    for (const auto table : record_tables_in_order()) {
        exec(q, {sql_text(table)}, "list shared copies");
        while (q.next()) {
            take({std::string(table), q.value(1).toString().toStdString(), shared_copy_of(q, 2)});
        }
        q.finish();
    }
}

std::map<std::string, std::vector<std::pair<std::string, std::string>>> SyncBookkeeping::mentioning(
    const std::set<std::string>& names)
{
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> out;
    // Each shared copy that names one of ``names`` (``names_in``), but its own.
    const auto take = [&](std::string table, std::string key, const QString& text,
                          const std::set<std::string>& among) {
        const auto document = nlohmann::json::parse(text.toStdString(), nullptr, false);
        for (const auto& name : names_in(table, key, document.is_discarded() ? nlohmann::json() : document)) {
            if (name != key && among.contains(name)) {
                out[name].emplace_back(table, key);
            }
        }
    };
    // A few: each found by its text first. Many, or one the text cannot be
    // searched for (a character JSON escapes, so it is written otherwise in
    // a document held as text in a document): every shared copy read once.
    constexpr std::size_t k_one_by_one = 8;
    const auto searchable = [](const std::string& name) {
        return std::ranges::none_of(name, [](char c) {
            return c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20;
        });
    };
    if (names.size() > k_one_by_one || !std::ranges::all_of(names, searchable)) {
        auto q = exec(db_,
                      QStringLiteral("SELECT record_table, record_key, document FROM sync_shared "
                                     "WHERE document IS NOT NULL ORDER BY record_table, record_key"),
                      {}, "find what points at records");
        while (q.next()) {
            take(q.value(0).toString().toStdString(), q.value(1).toString().toStdString(),
                 q.value(2).toString(), names);
        }
        return out;
    }
    for (const auto& name : names) {
        // A uid, or a file's name, which may hold a LIKE wildcard: escaped.
        QString escaped;
        for (const QChar c : QString::fromStdString(name)) {
            if (c == u'\\' || c == u'%' || c == u'_') {
                escaped += u'\\';
            }
            escaped += c;
        }
        auto q = exec(db_,
                      QStringLiteral("SELECT record_table, record_key, document FROM sync_shared "
                                     "WHERE document IS NOT NULL AND record_key != ? "
                                     "AND (document LIKE ? ESCAPE '\\' OR record_key LIKE ? ESCAPE '\\' "
                                     "OR record_key LIKE ? ESCAPE '\\') "
                                     "ORDER BY record_table, record_key"),
                      {sql_text(name), QStringLiteral("%") + escaped + QStringLiteral("%"),
                       escaped + QStringLiteral("/%"), QStringLiteral("%/") + escaped},
                      "find what points at a record");
        const std::set<std::string> one{name};
        while (q.next()) {
            take(q.value(0).toString().toStdString(), q.value(1).toString().toStdString(),
                 q.value(2).toString(), one);
        }
    }
    return out;
}

std::optional<std::string> SyncBookkeeping::state(std::string_view key)
{
    auto q = exec(db_, QStringLiteral("SELECT value FROM sync_state WHERE key = ?"),
                  {sql_text(key)}, "read sync state");
    if (!q.next()) {
        return std::nullopt;
    }
    return q.value(0).toString().toStdString();
}

void SyncBookkeeping::set_state(std::string_view key, const std::string& value)
{
    exec(db_,
         QStringLiteral("INSERT INTO sync_state (key, value) VALUES (?, ?) "
                        "ON CONFLICT (key) DO UPDATE SET value = excluded.value"),
         {sql_text(key), sql_text(value)}, "write sync state");
}

std::int64_t SyncBookkeeping::counter()
{
    return std::stoll(state(k_counter_key).value_or("0"));
}

void SyncBookkeeping::set_counter(std::int64_t counter)
{
    set_state(k_counter_key, std::to_string(counter));
}

std::int64_t SyncBookkeeping::last_sent()
{
    return std::stoll(state(k_last_sent_key).value_or("0"));
}

void SyncBookkeeping::set_last_sent(std::int64_t sequence)
{
    set_state(k_last_sent_key, std::to_string(sequence));
}

std::map<std::string, std::int64_t> SyncBookkeeping::applied()
{
    auto q = exec(db_, QStringLiteral("SELECT machine_id, sequence FROM sync_applied"), {},
                  "read applied change files");
    std::map<std::string, std::int64_t> out;
    while (q.next()) {
        out.emplace(q.value(0).toString().toStdString(), q.value(1).toLongLong());
    }
    return out;
}

void SyncBookkeeping::set_applied(std::string_view machine, std::int64_t sequence)
{
    exec(db_,
         QStringLiteral("INSERT INTO sync_applied (machine_id, sequence) VALUES (?, ?) "
                        "ON CONFLICT (machine_id) DO UPDATE SET sequence = excluded.sequence"),
         {sql_text(machine), static_cast<qint64>(sequence)}, "record an applied change file");
}

void SyncBookkeeping::hold(const HeldChange& change)
{
    exec(db_,
         QStringLiteral("INSERT OR REPLACE INTO sync_held (record_table, record_key, machine_id, "
                        "sequence, change) VALUES (?, ?, ?, ?, ?)"),
         {sql_text(change.table), sql_text(change.key), sql_text(change.machine),
          static_cast<qint64>(change.sequence), json_to_qstring(change.change)},
         "hold a change");
}

std::vector<HeldChange> SyncBookkeeping::held()
{
    auto q = exec(db_,
                  QStringLiteral("SELECT record_table, record_key, machine_id, sequence, change "
                                 "FROM sync_held ORDER BY machine_id, sequence"),
                  {}, "read held changes");
    return held_rows(q);
}

std::vector<HeldChange> SyncBookkeeping::held(std::string_view table, std::string_view key)
{
    auto q = exec(db_,
                  QStringLiteral("SELECT record_table, record_key, machine_id, sequence, change "
                                 "FROM sync_held WHERE record_table = ? AND record_key = ? "
                                 "ORDER BY machine_id, sequence"),
                  {sql_text(table), sql_text(key)}, "read held changes");
    return held_rows(q);
}

void SyncBookkeeping::for_each_held(const std::function<void(HeldChange)>& take)
{
    auto q = prepare_forward_only(
        db_,
        QStringLiteral("SELECT record_table, record_key, machine_id, sequence, change "
                       "FROM sync_held ORDER BY machine_id, sequence, record_table, record_key"),
        "read held changes");
    exec(q, {}, "read held changes");
    while (q.next()) {
        take(held_row(q));
    }
}

void SyncBookkeeping::release(const HeldChange& change)
{
    exec(db_,
         QStringLiteral("DELETE FROM sync_held WHERE record_table = ? AND record_key = ? "
                        "AND machine_id = ? AND sequence = ?"),
         {sql_text(change.table), sql_text(change.key), sql_text(change.machine),
          static_cast<qint64>(change.sequence)},
         "release a held change");
}

namespace {

/// A ``sync_media`` row from ``SELECT path, size, sha256, counter, machine``
/// (``sha256`` holds the root: a SHA-256, of the pieces' hashes).
MediaDigest media_row(const QSqlQuery& q)
{
    return MediaDigest{q.value(0).toString().toStdString(), q.value(1).toLongLong(),
                       q.value(2).toString().toStdString(),
                       SyncVersion{q.value(3).toLongLong(), q.value(4).toString().toStdString()}};
}

}  // namespace

void SyncBookkeeping::release_all()
{
    db_.exec("DELETE FROM sync_held");
}

std::optional<MediaDigest> SyncBookkeeping::media_digest(std::string_view path)
{
    auto q = exec(db_,
                  QStringLiteral("SELECT path, size, sha256, counter, machine FROM sync_media "
                                 "WHERE path = ?"),
                  {sql_text(path)}, "read a media file's digest");
    if (!q.next()) {
        return std::nullopt;
    }
    return media_row(q);
}

void SyncBookkeeping::set_media_digest(const MediaDigest& digest)
{
    // The first declaration's bytes stand. Its version is the earliest any
    // declaration gives, so every computer comes to the same one, whatever
    // order the declarations reached it in.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): value() throws, as documented.
    const SyncVersion& version = digest.version.value();
    exec(db_,
         QStringLiteral("INSERT INTO sync_media (path, size, sha256, counter, machine) "
                        "VALUES (?, ?, ?, ?, ?) "
                        "ON CONFLICT (path) DO UPDATE SET counter = excluded.counter, "
                        "machine = excluded.machine "
                        "WHERE excluded.counter < sync_media.counter "
                        "OR (excluded.counter = sync_media.counter "
                        "AND excluded.machine < sync_media.machine)"),
         {sql_text(digest.path), static_cast<qint64>(digest.size), sql_text(digest.root),
          static_cast<qint64>(version.counter), sql_text(version.machine)},
         "write a media file's digest");
}

std::vector<MediaDigest> SyncBookkeeping::media_digests()
{
    auto q = exec(db_,
                  QStringLiteral("SELECT path, size, sha256, counter, machine FROM sync_media "
                                 "ORDER BY path"),
                  {}, "list media digests");
    std::vector<MediaDigest> out;
    while (q.next()) {
        out.push_back(media_row(q));
    }
    return out;
}

void SyncBookkeeping::for_each_media(const std::function<void(MediaDigest)>& take)
{
    auto q = prepare_forward_only(db_,
                                  QStringLiteral("SELECT path, size, sha256, counter, machine FROM sync_media "
                                                 "ORDER BY path"),
                                  "list media digests");
    exec(q, {}, "list media digests");
    while (q.next()) {
        take(media_row(q));
    }
}

bool SyncBookkeeping::is_marked(std::string_view table, std::string_view key)
{
    // A table marked as a whole is marked under "" too.
    auto q = exec(db_,
                  QStringLiteral("SELECT 1 FROM sync_outbox WHERE record_table = ? "
                                 "AND (record_key = ? OR record_key = ?)"),
                  {sql_text(table), sql_text(key), sql_text(marked_whole(table) ? "" : key)},
                  "read the sync outbox");
    return q.next();
}

void SyncBookkeeping::mark(const MarkedRecord& mark)
{
    exec(db_,
         QStringLiteral("INSERT OR IGNORE INTO sync_outbox (record_table, record_key) "
                        "VALUES (?, ?)"),
         {sql_text(mark.table), sql_text(mark.key)}, "mark a record");
}

void SyncBookkeeping::unmark(const MarkedRecord& mark)
{
    exec(db_,
         QStringLiteral("DELETE FROM sync_outbox WHERE record_table = ? AND record_key = ?"),
         {sql_text(mark.table), sql_text(mark.key)}, "unmark a record");
}

void SyncBookkeeping::mark_everything()
{
    Transaction tx(db_);
    auto mark = prepare(db_,
                        QStringLiteral("INSERT OR IGNORE INTO sync_outbox (record_table, "
                                       "record_key) VALUES (?, ?)"),
                        "mark every record");
    for (const auto table : record_tables_in_order()) {
        for (const auto& key : record_keys(db_, table)) {
            exec(mark, {sql_text(table), sql_text(key)}, "mark every record");
        }
    }
    tx.commit();
}

void SyncBookkeeping::clear_marks()
{
    db_.exec("DELETE FROM sync_outbox");
}

void SyncBookkeeping::leave_synced_library()
{
    Transaction tx(db_);
    exec(db_, QStringLiteral("UPDATE library_meta SET value = lower(hex(randomblob(16))) WHERE key = ?"),
         {sql_text(k_library_uid_key)}, "give the library a uid of its own");
    for (const char* table : {"sync_shared", "sync_applied", "sync_state", "sync_held", "sync_outbox"}) {
        db_.exec(std::string("DELETE FROM ") + table);
    }
    tx.commit();
}

bool SyncBookkeeping::synced_before()
{
    for (const char* table : {"sync_shared", "sync_applied", "sync_held", "sync_media"}) {
        if (db_.query_int64(std::string("SELECT EXISTS (SELECT 1 FROM ") + table + ")") != 0) {
            return true;
        }
    }
    return false;
}

void SyncBookkeeping::start_afresh()
{
    Transaction tx(db_);
    leave_synced_library();
    db_.exec("DELETE FROM sync_media");
    tx.commit();
}

}  // namespace seventhslide::db
