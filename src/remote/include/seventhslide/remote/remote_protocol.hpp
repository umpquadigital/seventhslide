#pragma once

// Wire protocol - snapshot and patch builders, and the command
// whitelist.
//
// The table below is the security boundary as well as the scope boundary. The
// facade exposes roughly 380 Q_INVOKABLEs; a remote may reach the few listed
// here and nothing else. Dispatch is by explicit table lookup - never by
// reflection over the metaobject.
//
// Everything in this file is a pure function of a bridge, so it tests against a
// fake with no sockets.

#include <seventhslide/remote/remote_bridge.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::remote::protocol {

/// Wire format version. Bumped when a client would misread an older server;
/// docs/REMOTE_PROTOCOL.md describes the current one.
inline constexpr int k_protocol_version = 6;

enum class ArgType { Int, Number, Bool, String, IntArray };

struct ArgSpec {
    std::string_view key;
    ArgType type;
    bool required;
};

struct CommandSpec {
    Command command;
    /// What a client sends.
    std::string_view name;
    std::vector<ArgSpec> args;
    /// Sections a successful call invalidates.
    std::vector<Section> dirty;
};

/// The whitelist: one entry per Command, in Command order.
[[nodiscard]] const std::vector<CommandSpec>& command_table();
[[nodiscard]] const CommandSpec& command_spec(Command command);
/// Null when ``name`` is not whitelisted.
[[nodiscard]] const CommandSpec* find_command(std::string_view name);

/// Empty when ``args`` satisfies the spec, otherwise a client-facing reason.
[[nodiscard]] std::string validate_args(const CommandSpec& spec, const nlohmann::json& args);

struct Dispatch {
    CommandResult result;
    /// Sections to mark dirty; empty when the command failed.
    std::vector<Section> dirty;
};

/// Look up, validate, and run one command. Rejects unknown names. Never
/// throws: a bridge that throws is reported as a failed command.
[[nodiscard]] Dispatch dispatch(IRemoteBridge& bridge,
                                std::string_view name,
                                const nlohmann::json& args,
                                View& view);

// --- client -> server frames ---

/// One decoded client frame. Fields of the wrong JSON type read as absent, so
/// a malformed frame is refused by the checks that follow rather than thrown.
struct ClientMessage {
    std::string type;
    std::string id;
    std::string name;
    nlohmann::json args = nlohmann::json::object();
};

struct DecodedMessage {
    /// Empty when the text is not a JSON object.
    std::optional<ClientMessage> message;
    /// Client-facing reason when ``message`` is empty.
    std::string error;
};

/// Decode one WebSocket text frame. Never throws.
[[nodiscard]] DecodedMessage decode_client_message(std::string_view text);

// --- server -> client frames ---

/// ``boot`` names this run of the server. Thumbnail URLs carry it beside
/// their revision: a revision counts from 1 again after a restart, and a
/// browser keeps a thumbnail it fetched under the same URL before.
[[nodiscard]] nlohmann::json hello_frame(std::string_view boot);
/// Every section as ``view`` (settled) sees it.
[[nodiscard]] nlohmann::json snapshot_frame(const IRemoteBridge& bridge,
                                            const View& view,
                                            std::uint64_t rev);
[[nodiscard]] nlohmann::json ack_frame(const std::string& id, const CommandResult& result);
[[nodiscard]] nlohmann::json error_frame(std::string_view text);

} // namespace seventhslide::remote::protocol
