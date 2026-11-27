#include "seventhslide/db/service_repository.hpp"

#include "seventhslide/db/query.hpp"

#include "seventhslide/db/json_codec.hpp"
#include "seventhslide/db/list_order.hpp"
#include "seventhslide/db/song_repository.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/model/audio_fades.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

namespace seventhslide::db {
namespace {

model::ServiceItem item_from_row(QSqlQuery& q)
{
    model::ServiceItem item;
    item.item_id = q.value(0).toLongLong();
    if (!q.value(1).isNull()) {
        item.song_id = q.value(1).toLongLong();
    }
    item.order_num = q.value(2).toLongLong();
    item.item_type = q.value(3).toString().toStdString();
    item.data = parse_json_text(q.value(4).toString(), nlohmann::json::object());
    if (!item.data.is_object()) {
        item.data = nlohmann::json::object();
    }
    return item;
}

QString now_iso()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

QString normalize_service_iso(std::string_view iso)
{
    const auto text = sql_text(iso).trimmed();
    QDateTime dt = QDateTime::fromString(text, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    if (!dt.isValid()) {
        dt = QDateTime::fromString(text, Qt::ISODate);
    }
    if (!dt.isValid()) {
        dt = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!dt.isValid()) {
        throw std::runtime_error("Invalid date/time");
    }
    return dt.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

// The service columns service_from_row reads, in its order.
constexpr QLatin1StringView k_service_columns{
    "id, name, theme_map, tags, created_at, service_at, audio_fades"};

model::Service service_from_row(QSqlQuery& q)
{
    model::Service s;
    s.id = q.value(0).toLongLong();
    s.name = q.value(1).toString().toStdString();
    s.theme_map = parse_json_text(q.value(2).toString(), nlohmann::json::object());
    if (!s.theme_map.is_object()) {
        s.theme_map = nlohmann::json::object();
    }
    const auto tags = parse_json_text(q.value(3).toString(), nlohmann::json::array());
    s.tags = model::normalize_service_tags(json::strings_in(tags));
    s.created_at = q.value(4).toString().toStdString();
    s.service_at = q.value(5).toString().toStdString();  // NULL: no date
    s.audio_fades = model::set_audio_fades(
        parse_json_text(q.value(6).toString(), nlohmann::json::object()));
    return s;
}

}  // namespace

nlohmann::json song_to_service_snapshot(const model::Song& song, bool user_modified)
{
    auto snapshot = song.to_json();
    snapshot["user_modified"] = user_modified;
    return snapshot;
}

std::int64_t ServiceRepository::create_service(std::string_view name)
{
    const auto stamp = now_iso();
    const auto sql = QStringLiteral(
        "INSERT INTO services (name, theme_map, created_at, service_at) VALUES (?, ?, ?, ?)");
    exec(db_, sql, {sql_text(name), QStringLiteral("{}"), stamp, stamp}, "create service");
    return db_.last_insert_id();
}

void ServiceRepository::rename_service(std::int64_t id, std::string_view name)
{
    exec(db_, QStringLiteral("UPDATE services SET name = ? WHERE id = ?"),
         {sql_text(name), static_cast<qint64>(id)}, "rename service");
}

void ServiceRepository::set_service_at(std::int64_t id, std::string_view iso)
{
    // Empty: the service has no date.
    const QVariant stamp = util::trim_view(iso).empty() ? QVariant(QMetaType(QMetaType::QString))
                                                        : QVariant(normalize_service_iso(iso));
    exec(db_, QStringLiteral("UPDATE services SET service_at = ? WHERE id = ?"),
         {stamp, static_cast<qint64>(id)}, "set service_at");
}

void ServiceRepository::set_service_tags(std::int64_t id, const std::vector<std::string>& tags)
{
    exec(db_, QStringLiteral("UPDATE services SET tags = ? WHERE id = ?"),
         {json_to_qstring(nlohmann::json(model::normalize_service_tags(tags))),
          static_cast<qint64>(id)},
         "set service tags");
}

void ServiceRepository::set_service_theme_map(std::int64_t id, const nlohmann::json& theme_map)
{
    exec(db_, QStringLiteral("UPDATE services SET theme_map = ? WHERE id = ?"),
         {json_to_qstring(theme_map.is_object() ? theme_map : nlohmann::json::object()),
          static_cast<qint64>(id)},
         "set service theme_map");
}

void ServiceRepository::set_service_audio_fades(std::int64_t id, const nlohmann::json& fades)
{
    exec(db_, QStringLiteral("UPDATE services SET audio_fades = ? WHERE id = ?"),
         {json_to_qstring(model::set_audio_fades(fades)), static_cast<qint64>(id)},
         "set service audio fades");
}

bool ServiceRepository::delete_service(std::int64_t id)
{
    auto q = exec(db_, QStringLiteral("DELETE FROM services WHERE id = ?"),
                  {static_cast<qint64>(id)}, "delete service");
    return q.numRowsAffected() > 0;
}

std::optional<model::Service> ServiceRepository::get_service(std::int64_t id)
{
    const QString sql = QStringLiteral("SELECT ") + k_service_columns
                        + QStringLiteral(" FROM services WHERE id = ?");
    auto q = exec(db_, sql, {static_cast<qint64>(id)}, "get service");
    if (!q.next()) {
        return std::nullopt;
    }
    return service_from_row(q);
}

std::vector<model::Service> ServiceRepository::list_services()
{
    // Newest first; a service with no date after every one with.
    const QString sql = QStringLiteral("SELECT ") + k_service_columns
                        + QStringLiteral(" FROM services ORDER BY (service_at IS NULL), "
                                         "service_at DESC, id DESC");
    auto q = exec(db_, sql, {}, "list services");
    std::vector<model::Service> out;
    while (q.next()) {
        out.push_back(service_from_row(q));
    }
    return out;
}

std::vector<std::int64_t> ServiceRepository::open_insert_orders(std::int64_t service_id,
                                                       std::optional<int> at_index,
                                                       int count)
{
    if (count <= 0) {
        return {};
    }
    for (int attempt = 0;; ++attempt) {
        auto list = exec(db_,
                         QStringLiteral("SELECT order_num FROM service_items WHERE service_id = ? "
                                        "ORDER BY order_num ASC, uid ASC"),
                         {static_cast<qint64>(service_id)}, "list orders");
        std::vector<std::int64_t> orders;
        while (list.next()) {
            orders.push_back(list.value(0).toLongLong());
        }
        std::optional<std::int64_t> low;
        std::optional<std::int64_t> high;
        if (!at_index || *at_index >= static_cast<int>(orders.size())) {
            if (!orders.empty()) {
                low = orders.back();
            }
        } else {
            const auto index = static_cast<std::size_t>(std::max(*at_index, 0));
            if (index > 0) {
                low = orders[index - 1];
            }
            high = orders[index];
        }
        if (auto out = orders_between(low, high, count)) {
            return std::move(*out);
        }
        if (attempt > 0) {
            throw std::runtime_error("No room in the service's order");
        }
        space_out(service_id);
    }
}

void ServiceRepository::space_out(std::int64_t service_id)
{
    std::vector<std::int64_t> ids;
    {
        auto q = exec(db_,
                      QStringLiteral("SELECT id FROM service_items WHERE service_id = ? "
                                     "ORDER BY order_num ASC, uid ASC"),
                      {static_cast<qint64>(service_id)}, "space out a service");
        while (q.next()) {
            ids.push_back(q.value(0).toLongLong());
        }
    }
    auto set = prepare(db_, QStringLiteral("UPDATE service_items SET order_num = ? WHERE id = ?"),
                       "space out a service");
    for (std::size_t i = 0; i < ids.size(); ++i) {
        exec(set, {static_cast<qint64>(static_cast<std::int64_t>(i) * k_order_gap),
                   static_cast<qint64>(ids[i])},
             "space out a service");
    }
}

std::optional<std::int64_t> ServiceRepository::add_song_to_service(
    std::int64_t service_id,
    std::int64_t song_id,
    std::optional<int> at_index)
{
    SongRepository songs(db_);
    auto song = songs.get(song_id);
    if (!song) {
        return std::nullopt;
    }

    Transaction tx(db_);
    const auto orders = open_insert_orders(service_id, at_index, 1);
    const auto snap = song_to_service_snapshot(*song, false);

    const auto sql = QStringLiteral(
        "INSERT INTO service_items (service_id, song_id, order_num, item_type, data) "
        "VALUES (?, ?, ?, 'song', ?)");
    exec(db_, sql,
         {static_cast<qint64>(service_id),
          static_cast<qint64>(song_id),
          static_cast<qint64>(orders.front()),
          json_to_qstring(snap)},
         "add song item");
    const auto id = db_.last_insert_id();
    tx.commit();
    return id;

}

std::int64_t ServiceRepository::add_item(std::int64_t service_id,
                                         std::string_view item_type,
                                         const nlohmann::json& data,
                                         std::optional<std::int64_t> song_id,
                                         std::optional<int> at_index)
{
    Transaction tx(db_);
    const auto orders = open_insert_orders(service_id, at_index, 1);
    const auto sql = QStringLiteral(
        "INSERT INTO service_items (service_id, song_id, order_num, item_type, data) "
        "VALUES (?, ?, ?, ?, ?)");
    exec(db_, sql,
         {static_cast<qint64>(service_id),
          sql_id(song_id),
          static_cast<qint64>(orders.front()),
          sql_text(item_type),
          json_to_qstring(data)},
         "add item");
    const auto id = db_.last_insert_id();
    tx.commit();
    return id;

}

bool ServiceRepository::remove_item(std::int64_t item_id)
{
    Transaction tx(db_);
    // Rows of the same service that list this one as a companion forget it,
    // so no also-play list keeps an id that no longer exists.
    std::vector<std::pair<std::int64_t, nlohmann::json>> updates;
    {
        const auto sql = QStringLiteral(
            "SELECT id, data FROM service_items WHERE id != ? AND service_id = "
            "(SELECT service_id FROM service_items WHERE id = ?)");
        auto q = exec(db_, sql,
                      {static_cast<qint64>(item_id),
                       static_cast<qint64>(item_id)},
                      "remove item companions");
        while (q.next()) {
            model::ServiceItem row;
            row.data = parse_json_text(q.value(1).toString(), nlohmann::json::object());
            auto ids = model::service_item_also_play(row);
            const auto end = std::remove(ids.begin(), ids.end(), item_id);
            if (end == ids.end()) {
                continue;
            }
            ids.erase(end, ids.end());
            model::set_service_item_also_play(row, std::move(ids));
            updates.emplace_back(q.value(0).toLongLong(), std::move(row.data));
        }
    }
    if (!updates.empty()) {
        auto upd = prepare(db_, QStringLiteral("UPDATE service_items SET data = ? WHERE id = ?"),
                           "forget companion");
        for (const auto& [id, data] : updates) {
            exec(upd, {json_to_qstring(data), static_cast<qint64>(id)}, "forget companion");
        }
    }

    auto q = exec(db_, QStringLiteral("DELETE FROM service_items WHERE id = ?"),
                  {static_cast<qint64>(item_id)}, "remove item");
    const bool removed = q.numRowsAffected() > 0;
    tx.commit();
    return removed;
}

void ServiceRepository::reorder_items(std::int64_t service_id,
                                      const std::vector<std::int64_t>& ordered_item_ids)
{
    reorder_rows(db_, {"service_items", "order_num", "service_id", service_id}, ordered_item_ids);
}

void ServiceRepository::replace_item_data(std::int64_t item_id, const nlohmann::json& data)
{
    exec(db_, QStringLiteral("UPDATE service_items SET data = ? WHERE id = ?"),
         {json_to_qstring(data), static_cast<qint64>(item_id)}, "replace item data");
}

std::optional<model::ServiceItem> ServiceRepository::get_item(std::int64_t item_id)
{
    const auto sql = QStringLiteral(
        "SELECT id, song_id, order_num, item_type, data FROM service_items WHERE id = ?");
    auto q = exec(db_, sql, {static_cast<qint64>(item_id)}, "get item");
    if (!q.next()) {
        return std::nullopt;
    }
    return item_from_row(q);
}

std::vector<model::ServiceItem> ServiceRepository::list_items(std::int64_t service_id)
{
    const auto sql = QStringLiteral(
        "SELECT id, song_id, order_num, item_type, data FROM service_items "
        "WHERE service_id = ? ORDER BY order_num ASC, uid ASC");
    auto q = exec(db_, sql, {static_cast<qint64>(service_id)}, "list items");
    std::vector<model::ServiceItem> out;
    while (q.next()) {
        out.push_back(item_from_row(q));
    }
    return out;
}

int ServiceRepository::item_count(std::int64_t service_id)
{
    auto q = exec(db_,
                  QStringLiteral("SELECT COUNT(*) FROM service_items "
                                 "WHERE service_id = ? AND item_type <> ?"),
                  {static_cast<qint64>(service_id),
                   QString::fromUtf8(model::k_item_type_divider)},
                  "count items");
    return q.next() ? q.value(0).toInt() : 0;
}

void ServiceRepository::detach_songs(const std::vector<std::int64_t>& song_ids)
{
    if (song_ids.empty()) {
        return;
    }
    Transaction tx(db_);
    std::vector<std::pair<std::int64_t, nlohmann::json>> updates;
    {
        auto q = exec(db_,
                      QStringLiteral("SELECT id, data FROM service_items "
                                     "WHERE item_type = 'song' AND song_id IN %1")
                          .arg(id_in_list(song_ids)),
                      {}, "detach select");
        while (q.next()) {
            auto snap = parse_json_text(q.value(1).toString(), nlohmann::json::object());
            if (!snap.is_object()) {
                snap = nlohmann::json::object();
            }
            // The row keeps its snapshot but is no longer tied to the library.
            snap["user_modified"] = true;
            updates.emplace_back(q.value(0).toLongLong(), std::move(snap));
        }
    }
    auto upd = prepare(
        db_, QStringLiteral("UPDATE service_items SET song_id = NULL, data = ? WHERE id = ?"),
        "detach update");
    for (const auto& [item_id, snap] : updates) {
        exec(upd, {json_to_qstring(snap), static_cast<qint64>(item_id)}, "detach update");
    }
    tx.commit();
}

}  // namespace seventhslide::db
