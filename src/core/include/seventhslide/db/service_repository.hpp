#pragma once

// Services (tagged) and service items with song snapshots.
// Seam: Qt SQL via Database; domain types from model::*.

#include "seventhslide/db/database.hpp"
#include "seventhslide/model/content.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

/// Build the durable service-item ``data`` blob from a library song.
[[nodiscard]] nlohmann::json song_to_service_snapshot(const model::Song& song,
                                                      bool user_modified = false);

class ServiceRepository {
public:
    explicit ServiceRepository(Database& db) : db_(db) {}

    // --- Services ---
    /// A new, empty service dated now.
    [[nodiscard]] std::int64_t create_service(std::string_view name);
    void rename_service(std::int64_t id, std::string_view name);
    void set_service_theme_map(std::int64_t id, const nlohmann::json& theme_map);
    /// The audio fades service ``id`` sets for its audio (only the fields
    /// ``fades`` sets, ``model::set_audio_fades``); an empty object sets
    /// none, leaving each output's.
    void set_service_audio_fades(std::int64_t id, const nlohmann::json& fades);
    /// ``iso`` is local ``yyyy-MM-ddTHH:mm:ss``; empty for no date. Throws if
    /// unparseable.
    void set_service_at(std::int64_t id, std::string_view iso);
    /// Replace the service's tags (normalize_service_tags).
    void set_service_tags(std::int64_t id, const std::vector<std::string>& tags);
    /// CASCADE deletes items via FK.
    bool delete_service(std::int64_t id);
    [[nodiscard]] std::optional<model::Service> get_service(std::int64_t id);
    /// Newest first by date, then the services with no date.
    [[nodiscard]] std::vector<model::Service> list_services();

    // --- Items ---
    /// Snapshot library song into the service. ``at_index`` nullopt appends.
    /// Returns new item id, or nullopt if the song does not exist.
    [[nodiscard]] std::optional<std::int64_t> add_song_to_service(
        std::int64_t service_id,
        std::int64_t song_id,
        std::optional<int> at_index = std::nullopt);

    /// Generic item insert (bible, divider, freeform, ...). ``data`` stored as-is.
    [[nodiscard]] std::int64_t add_item(std::int64_t service_id,
                                        std::string_view item_type,
                                        const nlohmann::json& data,
                                        std::optional<std::int64_t> song_id = std::nullopt,
                                        std::optional<int> at_index = std::nullopt);

    bool remove_item(std::int64_t item_id);
    /// Put the service's items in the order of ``ordered_item_ids``, which
    /// lists every one. Only the items that move get new numbers
    /// (``list_order.hpp``).
    void reorder_items(std::int64_t service_id, const std::vector<std::int64_t>& ordered_item_ids);

    /// Replace the entire ``data`` JSON (no merge). Caller owns merge/reset logic.
    void replace_item_data(std::int64_t item_id, const nlohmann::json& data);

    [[nodiscard]] std::optional<model::ServiceItem> get_item(std::int64_t item_id);
    /// In order; items with the same number by uid, so every computer that
    /// has the service lists them alike.
    [[nodiscard]] std::vector<model::ServiceItem> list_items(std::int64_t service_id);
    /// How many items the service holds, as the operator's list numbers
    /// them: its dividers are headings, not items, and do not count.
    [[nodiscard]] int item_count(std::int64_t service_id);

    /// Unlink song items from their library rows: null song_id, set user_modified.
    void detach_songs(const std::vector<std::int64_t>& song_ids);

private:
    Database& db_;

    /// Order numbers for ``count`` new items at ``at_index`` (nullopt: the
    /// end), between their neighbours' (``list_order.hpp``); the service is
    /// spaced out first when there is no room.
    [[nodiscard]] std::vector<std::int64_t> open_insert_orders(std::int64_t service_id,
                                                               std::optional<int> at_index,
                                                               int count);
    /// Renumber the service's items a gap apart, in their order.
    void space_out(std::int64_t service_id);
};

}  // namespace seventhslide::db
