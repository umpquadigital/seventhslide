#pragma once

// Changing a server's sync settings (``link::SyncSettings``), from the settings
// command and the settings screen alike. With a server running it is asked over
// its commands' socket, so what can change at once does. With none, the file is
// edited directly, holding the lock a server would (``server.lock``), so one cannot
// start halfway through.
//
// A setting is named as in the file: computer_name, addresses, port (null for
// none), discovery, sync_automatically, upload_cap_kbit (0 for none),
// share_address. Every reply is
//
//   {"ok": true, "running": bool,
//    "settings": what the file says,
//    "now": what it is going by this run (running only),
//    "flags": the settings a flag sets for this run, and the file does not,
//    "after_restart": the settings saved that take effect when it next starts}
//
// or {"ok": false, "error": why}.
//
// Seam: the file system; the commands' socket through ``AskServer``.

#include "seventhslide/link/sync_settings.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace seventhslide::server {

/// A request to the server running with the data folder (``ask_server``), with how
/// long to wait for it. Throws ``NoServer`` when none is.
using AskServer = std::function<nlohmann::json(const nlohmann::json& request, int timeout_ms)>;

[[nodiscard]] nlohmann::json settings_json(const link::SyncSettings& settings);

/// Why an upload speed cap given is not one (``link::read_upload_cap``).
[[nodiscard]] std::string upload_cap_problem();

/// ``changes`` made to ``settings``: why not, in words, or empty. Nothing is
/// changed then.
[[nodiscard]] std::string apply_settings_changes(link::SyncSettings& settings, const nlohmann::json& changes);

/// The settings reply; ``changes`` made first, when given. Throws
/// ``std::runtime_error`` when a server there does not answer.
[[nodiscard]] nlohmann::json ask_settings(const std::filesystem::path& data, const AskServer& ask,
                                          const std::optional<nlohmann::json>& changes = std::nullopt);

}  // namespace seventhslide::server
