#include "seventhslide/db/style_profile_repository.hpp"

#include "seventhslide/db/query.hpp"

#include "seventhslide/db/json_codec.hpp"

#include <QSqlQuery>
#include <QVariant>

namespace seventhslide::db {

std::int64_t StyleProfileRepository::create(std::string_view name, const nlohmann::json& data)
{
    const int sort_order =
        db_.query_int("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM style_profiles", 0);
    const auto sql = QStringLiteral(
        "INSERT INTO style_profiles (name, sort_order, data) VALUES (?, ?, ?)");
    exec(db_, sql,
         {sql_text(name),
          sort_order,
          json_to_qstring(data.is_object() ? data : model::StyleProfile::empty_data())},
         "create style profile");
    return db_.last_insert_id();
}

void StyleProfileRepository::rename(std::int64_t id, std::string_view name)
{
    exec(db_, QStringLiteral("UPDATE style_profiles SET name = ? WHERE id = ?"),
         {sql_text(name), static_cast<qint64>(id)}, "rename style profile");
}

void StyleProfileRepository::save_data(std::int64_t id, const nlohmann::json& data)
{
    exec(db_, QStringLiteral("UPDATE style_profiles SET data = ? WHERE id = ?"),
         {json_to_qstring(data.is_object() ? data : nlohmann::json::object()),
          static_cast<qint64>(id)},
         "save style profile data");
}

std::optional<model::StyleProfile> StyleProfileRepository::get(std::int64_t id)
{
    const auto sql = QStringLiteral(
        "SELECT id, name, sort_order, data FROM style_profiles WHERE id = ?");
    auto q = exec(db_, sql, {static_cast<qint64>(id)}, "get style profile");
    if (!q.next()) {
        return std::nullopt;
    }
    model::StyleProfile p;
    p.id = q.value(0).toLongLong();
    p.name = q.value(1).toString().toStdString();
    p.sort_order = q.value(2).toLongLong();
    p.data = parse_json_text(q.value(3).toString(), model::StyleProfile::empty_data());
    if (!p.data.is_object()) {
        p.data = model::StyleProfile::empty_data();
    }
    return p;
}

std::vector<model::StyleProfile> StyleProfileRepository::list_summaries()
{
    const auto sql = QStringLiteral(
        "SELECT id, name, sort_order FROM style_profiles "
        "ORDER BY sort_order ASC, id ASC");
    auto q = exec(db_, sql, {}, "list style profiles");
    std::vector<model::StyleProfile> out;
    while (q.next()) {
        model::StyleProfile p;
        p.id = q.value(0).toLongLong();
        p.name = q.value(1).toString().toStdString();
        p.sort_order = q.value(2).toLongLong();
        out.push_back(std::move(p));
    }
    return out;
}

bool StyleProfileRepository::remove(std::int64_t id)
{
    auto q = exec(db_, QStringLiteral("DELETE FROM style_profiles WHERE id = ?"),
                  {static_cast<qint64>(id)}, "remove style profile");
    return q.numRowsAffected() > 0;
}

}  // namespace seventhslide::db
