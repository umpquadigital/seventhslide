#pragma once

// Style profile persistence. Default-profile seeding is app/session.
// Seam: Qt SQL via Database.

#include "seventhslide/db/database.hpp"
#include "seventhslide/model/content.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

class StyleProfileRepository {
public:
    explicit StyleProfileRepository(Database& db) : db_(db) {}

    [[nodiscard]] std::int64_t create(std::string_view name,
                                      const nlohmann::json& data = model::StyleProfile::empty_data());
    void rename(std::int64_t id, std::string_view name);
    void save_data(std::int64_t id, const nlohmann::json& data);
    [[nodiscard]] std::optional<model::StyleProfile> get(std::int64_t id);
    /// id / name / sort_order only (``data`` left as empty object).
    [[nodiscard]] std::vector<model::StyleProfile> list_summaries();
    bool remove(std::int64_t id);

    /// The connection these profiles live on (for a transaction spanning a
    /// profile switch).
    [[nodiscard]] Database& database() noexcept { return db_; }

private:
    Database& db_;
};

}  // namespace seventhslide::db
