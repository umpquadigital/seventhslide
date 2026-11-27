#pragma once

// This computer's sync settings, whatever runs it: the Sync page and the
// headless settings change the same file, ``sync_settings.json`` in the node's
// data folder, beside what it remembers (``sync_addresses.json``,
// ``sync_heard.json``). A setting left unset goes by the host's default. A flag
// given to ``seventhslide --server`` overrides one for that run and is never
// written back.
//
// Seam: nlohmann/json, the file system.

#include "seventhslide/sync/member_list.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::link {

/// The upload speed cap's range, in kilobits a second: 0.1 to 10,000 megabits.
inline constexpr std::int64_t k_least_upload_cap_kbit = 100;
inline constexpr std::int64_t k_most_upload_cap_kbit = 10'000'000;

/// An upload speed cap as typed, in megabits a second ("5", "0.5"), to a
/// thousandth: in kilobits a second, 0 for none (empty, "none", or nought);
/// nullopt when it is not one in range.
[[nodiscard]] std::optional<std::int64_t> read_upload_cap(std::string_view text);
/// A cap in megabits a second, as it would be typed ("5", "0.5"): empty for none.
[[nodiscard]] std::string upload_cap_text(std::int64_t kbit);

struct SyncSettings {
    /// What the others call this computer; empty for the host's default.
    std::string computer_name;
    /// What the others show beside it; none for its initials.
    sync::Picture picture;
    /// How the others reach it, "host" or "host:port", as typed. Published
    /// on each library's member list; none says it takes none.
    std::vector<std::string> addresses;
    /// Where it takes connections, when one was chosen.
    std::optional<std::uint16_t> port;
    /// Announce itself on the church network and hear the others.
    bool discovery = true;
    /// Off, it syncs only when asked (``SyncNode::sync_now``).
    bool sync_automatically = true;
    /// The others of its libraries may be told where it is, and it where they
    /// are (``Link::set_share_address``). Off, it asks for no introductions: it
    /// is seen only by the computers it connects to, or that reach it.
    bool share_address = true;
    /// What it sends of media to computers not on this network is held to this,
    /// in kilobits a second (``Link::set_upload_cap``); 0 for no cap.
    std::int64_t upload_cap_kbit = 0;

    friend bool operator==(const SyncSettings&, const SyncSettings&) = default;

    /// What ``data`` holds; the defaults for what it does not, a file that is
    /// not there or cannot be read among it.
    [[nodiscard]] static SyncSettings load(const std::filesystem::path& data);
    /// False when it could not be written.
    [[nodiscard]] bool save(const std::filesystem::path& data) const;
};

}  // namespace seventhslide::link
