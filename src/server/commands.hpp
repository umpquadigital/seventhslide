#pragma once

// The commands an operator types (``join``, ``status``, ``admin``, ``leave`` and
// the rest): each asks the running server (``ask_server``) and says what came of it
// in words, as the settings screens do too. Not part of the module's API.
//
// Seam: the commands' local socket; the terminal.

#include "seventhslide/server/command_line.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace seventhslide::server {

/// The exit status: 0 done, 1 not done (and why is printed).
[[nodiscard]] int run_command(const CommandLine& command);

/// ``status``'s reply, as the operator reads it.
[[nodiscard]] std::string describe_status(const nlohmann::json& status);
/// How a library of ``status``'s reply stands, in a sentence.
[[nodiscard]] std::string describe_library_state(const nlohmann::json& library);
/// How a computer of a library stands, in the Sync page's words.
[[nodiscard]] std::string describe_computer_state(const nlohmann::json& computer);
/// One setting's value, as the operator reads it: on or off, a list by commas.
[[nodiscard]] std::string describe_setting(const nlohmann::json& value);
/// An upload speed cap of ``kbit`` kilobits a second, as the operator reads it.
[[nodiscard]] std::string describe_upload_cap(std::int64_t kbit);
/// The settings reply, as the operator reads it.
[[nodiscard]] std::string describe_settings(const nlohmann::json& reply);
/// "2026-10-24 21:13", this computer's time, from ISO 8601 UTC.
[[nodiscard]] std::string describe_when(const std::string& iso);
/// "48.2 GB".
[[nodiscard]] std::string describe_size(std::int64_t bytes);
/// A port as typed. Throws ``std::invalid_argument``.
[[nodiscard]] std::int64_t port_setting(const std::string& value);

}  // namespace seventhslide::server
