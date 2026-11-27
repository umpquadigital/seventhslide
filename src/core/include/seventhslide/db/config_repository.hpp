#pragma once

// Outputs + app_settings persistence.
// Seam: Qt SQL via Database; OutputConfig via model::to_persist_dict / from_dict.

#include "seventhslide/db/database.hpp"
#include "seventhslide/model/output_config.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

class ConfigRepository {
public:
    explicit ConfigRepository(Database& db) : db_(db) {}

    // --- app_settings (values stored as JSON TEXT) ---
    [[nodiscard]] nlohmann::json load_settings();
    void save_settings(const nlohmann::json& settings_object);
    [[nodiscard]] std::optional<nlohmann::json> get_setting(std::string_view key);
    void set_setting(std::string_view key, const nlohmann::json& value);

    // --- outputs (full replace) ---
    [[nodiscard]] std::vector<model::OutputConfig> load_outputs();
    void save_outputs(const std::vector<model::OutputConfig>& outputs);

private:
    Database& db_;
};

}  // namespace seventhslide::db
