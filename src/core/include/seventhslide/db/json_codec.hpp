#pragma once

// JSON <-> SQLite TEXT helpers for core/db repositories.
// Seam: nlohmann/json + Qt QString (db layer only).

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

#include <QString>

namespace seventhslide::db {

[[nodiscard]] inline nlohmann::json parse_json_text(std::string_view text,
                                                    nlohmann::json fallback)
{
    if (text.empty()) {
        return fallback;
    }
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception&) {
        return fallback;
    }
}

[[nodiscard]] inline nlohmann::json parse_json_text(const QString& text,
                                                    nlohmann::json fallback)
{
    return parse_json_text(text.toStdString(), std::move(fallback));
}

[[nodiscard]] inline std::string json_to_std_string(const nlohmann::json& j)
{
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

[[nodiscard]] inline QString json_to_qstring(const nlohmann::json& j)
{
    return QString::fromStdString(json_to_std_string(j));
}

}  // namespace seventhslide::db
