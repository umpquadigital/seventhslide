#pragma once

// What ``seventhslide --server ...`` was asked to do (docs/SERVER.md):
//
//   seventhslide --server [--data DIR] [--port N] [--address HOST[:PORT]]...
//                         [--name NAME] [--no-discovery]
//                                                run the server
//   seventhslide --server join [CODE]            join a library by an invite
//   seventhslide --server status [--json]        libraries, computers, disk
//   seventhslide --server sync                   sync every library now
//   seventhslide --server admin LIB [ACTION ...] admin work on a library
//   seventhslide --server address LIB COMPUTER|INVITE [HOST[:PORT]...|none]
//                                                where to reach a computer
//   seventhslide --server leave LIB [--yes]      stop holding a library
//   seventhslide --server settings [NAME VALUE...]
//                                                its sync settings
//   seventhslide --server config                 all of it on one screen
//
// ``--data`` goes with any of them: the commands find the running server by
// its data folder. Without it, by what the setup left (``data_folder.hpp``).
//
// Seam: plain C++20.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::server {

enum class Command { Run, Join, Status, Sync, Admin, Address, Leave, Settings, Config, Help };

struct CommandLine {
    Command command = Command::Run;
    /// The server's data folder: where the setup says (``data_folder_from``), or
    /// ``default_data_dir()``, unless given.
    std::filesystem::path data;
    /// None given: as its settings say, or ``link::k_usual_port``.
    std::optional<std::uint16_t> port;
    /// How computers outside reach the server: "host" or "host:port" (the
    /// port it listens on when none is given). None given: as its settings say.
    std::vector<std::string> addresses;
    /// What the server is called. None given: as its settings say, or the
    /// computer's own name.
    std::string name;
    /// ``--no-discovery``: neither announce on the network nor listen.
    bool no_discovery = false;
    /// ``status --json``, ``settings --json``: the reply as it is, for a script.
    bool json = false;
    /// ``leave --yes``, ``admin LIB remove ... --yes``: no question first.
    bool yes = false;
    /// ``admin``, ``address`` and ``leave``: the library, as its uid or the
    /// start of it.
    std::string library;
    /// ``admin``: the action and what follows it ("invite", "viewer").
    /// ``address``: the computer, and where it is when that is being said.
    /// ``settings``: which setting, and what to.
    std::vector<std::string> action;
    /// ``join``: the invite, when given here; asked for when not, which keeps
    /// it out of the process list and the shell's history.
    std::string code;
    /// ``admin LIB invite --days N``.
    int days = 7;
    /// ``admin LIB invite --at HOST[:PORT]``, once for each: where the computer
    /// joining may be, for this one to reach out to it.
    std::vector<std::string> at;
    /// ``admin LIB password --recovery``: with the recovery code, not the
    /// current password.
    bool recovery = false;
};

/// Whether the program was asked for the server: ``--server`` first, before
/// anything else is set up.
[[nodiscard]] bool asks_for_server(int argc, const char* const* argv);

/// The arguments after ``--server``. Throws ``std::invalid_argument``, with
/// what was wrong for the person who typed it, and ``std::runtime_error`` when the
/// setup's word on the data folder cannot be read.
[[nodiscard]] CommandLine parse_command_line(const std::vector<std::string>& args);

/// What the commands are, for ``help`` and after a mistake.
[[nodiscard]] std::string usage();

/// ``<data dir>/server`` (``platform::Paths``): a folder of its own, so the
/// server never mixes with an operator's libraries on the same computer.
[[nodiscard]] std::filesystem::path default_data_dir();

/// "50GB", "1.5 TB", "500MB", or a number of bytes; "none" and 0 for no
/// limit. Decimal units, as disks are sold. Throws ``std::invalid_argument``.
[[nodiscard]] std::int64_t parse_size(std::string_view text);

}  // namespace seventhslide::server
