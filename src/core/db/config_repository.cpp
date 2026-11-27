#include "seventhslide/db/config_repository.hpp"

#include "seventhslide/db/query.hpp"

#include "seventhslide/db/json_codec.hpp"

#include <stdexcept>
#include <utility>

#include <QSqlQuery>
#include <QVariant>

namespace seventhslide::db {

nlohmann::json ConfigRepository::load_settings()
{
    auto q = exec(db_, QStringLiteral("SELECT key, value FROM app_settings"), {}, "load settings");
    nlohmann::json out = nlohmann::json::object();
    while (q.next()) {
        const auto key = q.value(0).toString().toStdString();
        out[key] = parse_json_text(q.value(1).toString(), nullptr);
    }
    return out;
}

void ConfigRepository::save_settings(const nlohmann::json& settings_object)
{
    if (!settings_object.is_object()) {
        throw std::invalid_argument("save_settings expects a JSON object");
    }
    Transaction tx(db_);
    for (auto it = settings_object.begin(); it != settings_object.end(); ++it) {
        set_setting(it.key(), it.value());
    }
    tx.commit();

}

std::optional<nlohmann::json> ConfigRepository::get_setting(std::string_view key)
{
    auto q = exec(db_, QStringLiteral("SELECT value FROM app_settings WHERE key = ?"),
                  {sql_text(key)}, "get setting");
    if (!q.next()) {
        return std::nullopt;
    }
    return parse_json_text(q.value(0).toString(), nullptr);
}

void ConfigRepository::set_setting(std::string_view key, const nlohmann::json& value)
{
    const auto sql = QStringLiteral(
        "INSERT INTO app_settings(key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    exec(db_, sql, {sql_text(key), json_to_qstring(value)}, "set setting");
}

std::vector<model::OutputConfig> ConfigRepository::load_outputs()
{
    auto q = exec(db_, QStringLiteral("SELECT data FROM outputs ORDER BY order_num ASC, id ASC"),
                  {}, "load outputs");
    std::vector<model::OutputConfig> out;
    while (q.next()) {
        out.push_back(model::OutputConfig::from_dict(
            parse_json_text(q.value(0).toString(), nlohmann::json::object())));
    }
    return out;
}

void ConfigRepository::save_outputs(const std::vector<model::OutputConfig>& outputs)
{
    Transaction tx(db_);
    db_.exec("DELETE FROM outputs");
    auto q = prepare(db_, QStringLiteral("INSERT INTO outputs(order_num, data) VALUES (?, ?)"),
                     "save output");
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        exec(q, {static_cast<int>(i), json_to_qstring(outputs[i].to_persist_dict())},
             "save output");
    }
    tx.commit();
}

}  // namespace seventhslide::db
