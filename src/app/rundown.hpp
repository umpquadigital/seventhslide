#pragma once

// The open service's rundown, as the library tabs add to it: pictures,
// Freeform items, web pages and live sources sent there or dropped on it.
// LibraryController implements it; the tabs know this of it and nothing more.
//
// Seam: plain C++ (nlohmann/json). Call on the GUI thread.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace seventhslide::app {

class IRundown {
public:
    IRundown() = default;
    virtual ~IRundown() = default;
    IRundown(const IRundown&) = delete;
    IRundown& operator=(const IRundown&) = delete;
    IRundown(IRundown&&) = delete;
    IRundown& operator=(IRundown&&) = delete;

    /// Add one item of ``item_type`` (``data`` as the type stores it) to the
    /// open service, at ``at_index`` or else at the end. False when there is
    /// no open service, or it would not take it.
    [[nodiscard]] virtual bool append_service_item(std::string_view item_type,
                                                   const nlohmann::json& data,
                                                   std::optional<std::int64_t> song_id = std::nullopt,
                                                   std::optional<int> at_index = std::nullopt) = 0;

    /// Add several items of one type in a single transaction, and reload the
    /// rundown once, at ``at_index`` or else at the end.
    virtual bool append_service_items(std::string_view item_type,
                                      const std::vector<nlohmann::json>& items,
                                      std::optional<int> at_index = std::nullopt) = 0;

    /// Re-read the open service's rows after a change to what they show.
    /// Nothing goes live and live stacks stay where they are;
    /// ``edited_item_id``, if it is live, is shown again in place.
    virtual bool sync_open_service_items(std::optional<std::int64_t> edited_item_id = std::nullopt) = 0;
};

}  // namespace seventhslide::app
