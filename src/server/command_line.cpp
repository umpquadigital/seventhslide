#include "seventhslide/server/command_line.hpp"

#include "seventhslide/platform/paths.hpp"
#include "seventhslide/server/data_folder.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace seventhslide::server {
namespace {

[[noreturn]] void wrong(const std::string& why)
{
    throw std::invalid_argument(why);
}

int whole_number(const std::string& text, const std::string& option, int low, int high)
{
    int value = 0;
    const auto [at, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || at != text.data() + text.size() || value < low || value > high) {
        wrong(option + " takes a whole number from " + std::to_string(low) + " to "
              + std::to_string(high) + ", not \"" + text + "\".");
    }
    return value;
}


}  // namespace

bool asks_for_server(int argc, const char* const* argv)
{
    return argc >= 2 && std::string_view(argv[1]) == "--server";
}

CommandLine parse_command_line(const std::vector<std::string>& args)
{
    CommandLine out;
    bool run_options = false;
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        const auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size()) {
                wrong(arg + " needs a value after it.");
            }
            return args[++i];
        };
        if (arg == "--data") {
            out.data = value();
        } else if (arg == "--port") {
            out.port = static_cast<std::uint16_t>(whole_number(value(), arg, 0, 65535));
            run_options = true;
        } else if (arg == "--address") {
            out.addresses.push_back(value());
            run_options = true;
        } else if (arg == "--name") {
            out.name = value();
            run_options = true;
        } else if (arg == "--no-discovery") {
            out.no_discovery = true;
            run_options = true;
        } else if (arg == "--json") {
            out.json = true;
        } else if (arg == "--yes") {
            out.yes = true;
        } else if (arg == "--days") {
            out.days = whole_number(value(), arg, 1, 30);
        } else if (arg == "--at") {
            out.at.push_back(value());
        } else if (arg == "--recovery") {
            out.recovery = true;
        } else if (arg == "--help" || arg == "-h") {
            out.command = Command::Help;
            return out;
        } else if (arg.starts_with("--")) {
            wrong("There is no option " + arg + ".");
        } else {
            positional.push_back(arg);
        }
    }

    if (!positional.empty()) {
        const auto& word = positional.front();
        if (word == "join") {
            out.command = Command::Join;
        } else if (word == "status") {
            out.command = Command::Status;
        } else if (word == "sync") {
            out.command = Command::Sync;
        } else if (word == "admin") {
            out.command = Command::Admin;
        } else if (word == "address") {
            out.command = Command::Address;
        } else if (word == "leave") {
            out.command = Command::Leave;
        } else if (word == "settings") {
            out.command = Command::Settings;
        } else if (word == "config") {
            out.command = Command::Config;
        } else if (word == "help") {
            out.command = Command::Help;
            return out;
        } else {
            wrong("There is no command \"" + word + "\".");
        }
    }
    const auto takes = [&](std::size_t low, std::size_t high) {
        if (positional.size() < low || positional.size() > high) {
            wrong(positional.size() < low ? "\"" + positional.front() + "\" needs a library."
                                          : "\"" + positional.front()
                                                + "\" was given more than it takes.");
        }
    };
    switch (out.command) {
    case Command::Run:
        break;
    case Command::Join:
        if (positional.size() > 2) {
            wrong("\"join\" takes the invite, and nothing else.");
        }
        out.code = positional.size() == 2 ? positional[1] : std::string();
        break;
    case Command::Status:
    case Command::Sync:
    case Command::Config:
        takes(1, 1);
        break;
    case Command::Settings:
        if (positional.size() == 2) {
            wrong("Give it as: settings [name|addresses|port|discovery|sync VALUE...]");
        }
        out.action.assign(positional.begin() + 1, positional.end());
        break;
    case Command::Admin:
        takes(2, std::numeric_limits<std::size_t>::max());
        out.library = positional[1];
        out.action.assign(positional.begin() + 2, positional.end());
        break;
    case Command::Address:
        if (positional.size() < 3) {
            wrong("Give it as: address LIB COMPUTER|INVITE [HOST[:PORT]...|none]");
        }
        out.library = positional[1];
        out.action.assign(positional.begin() + 2, positional.end());
        break;
    case Command::Leave:
        takes(2, 2);
        out.library = positional[1];
        break;
    case Command::Help:
        break;
    }
    if (run_options && out.command != Command::Run) {
        wrong("--port, --address, --name and --no-discovery are for running the server, not for a "
              "command.");
    }
    if (out.json && out.command != Command::Status && out.command != Command::Settings) {
        wrong("--json goes with status and settings.");
    }
    if (!out.at.empty() && out.command != Command::Admin) {
        wrong("--at goes with admin LIB invite.");
    }
    if (out.yes && out.command != Command::Leave && out.command != Command::Admin) {
        wrong("--yes goes with leave and admin.");
    }
    if (out.data.empty()) {
        out.data = data_folder_from(data_folder_pointer(), default_data_dir());
    }
    return out;
}

std::string usage()
{
    return R"(SeventhSlide as a server: always on, holding synced libraries and passing them
between the computers that sync them.

  seventhslide --server [--data DIR] [--port N] [--address HOST[:PORT]]... [--name NAME]
                        [--no-discovery]
      Run the server. --address is how computers outside the local network
      reach it (a name for the organization's internet address); give it once
      for each. The port is 8766 unless given. On the local network it finds
      the computers, and they find it, by announcing over UDP port 8766;
      --no-discovery turns that off (a container without the host's network).
      Each of these overrides the data folder's sync_settings.json for this run,
      and is not written back: set them for good with config or settings.

  seventhslide --server config       all of this on one screen: status, libraries,
                                     admin work and settings
  seventhslide --server settings [--json]
                                     the sync settings, running or not
  seventhslide --server settings name NAME
  seventhslide --server settings addresses HOST[:PORT]...|none
  seventhslide --server settings port N|none
  seventhslide --server settings discovery on|off
  seventhslide --server settings sync on|off
  seventhslide --server settings upload-cap MBIT|none
  seventhslide --server settings sharing on|off
                                     change one: at once where it can be, or when
                                     the server next starts

  seventhslide --server join [CODE]  join a library by an invite an editor made;
                                     it asks for the code when none is given, which
                                     keeps it out of the process list and history
  seventhslide --server status [--json]
                                     the libraries held, their computers, the disk
  seventhslide --server sync         sync every library now (for a server set
                                     not to sync automatically)
  seventhslide --server admin LIB    the library's computers, and admin work on it:
      invite editor|viewer [--days N] [--at HOST[:PORT]]...
                                        an invite for a computer (7 days unless
                                        given); --at is where the computer joining
                                        is, when it cannot reach any computer here
      withdraw INVITE                   an invite waiting lets no computer in
      role COMPUTER editor|viewer       change a computer's role
      key COMPUTER on|off               whether a viewer holds the locked admin key,
                                        for admin work there with the password
      remove COMPUTER                   remove a computer for good
      limit SIZE|none                   the library's size limit ("50GB")
      password [--recovery]             a new admin password, with the current
                                        one or the recovery code
      recovery                          a new recovery code
  seventhslide --server address LIB COMPUTER|INVITE [HOST[:PORT]...|none]
                                     reach a computer of the library there, before
                                     the addresses it gave, on this server alone, when
                                     they changed and this server did not hear; none
                                     to go by its own again. For an invite waiting,
                                     where the computer joining is, to reach out to
                                     it there with the invite
  seventhslide --server leave LIB [--yes]
                                     stop holding a library (its copy is kept
                                     in the data folder's "left" folder)

Add --data DIR to any of them when the server's data folder is not the usual
one (/etc/seventhslide/server.json says where, once set up). LIB is a library's id as status shows it, or enough of its start to tell
it from the others; COMPUTER is a computer's name or id; INVITE an invite's id,
or its start, as status shows it.
)";
}

std::filesystem::path default_data_dir()
{
    return platform::Paths().data_dir() / "server";
}

std::int64_t parse_size(std::string_view text)
{
    std::string s = util::to_lower(text);
    std::erase(s, ' ');
    if (s == "none" || s == "0") {
        return 0;
    }
    double factor = 1;
    for (const auto& [suffix, f] : {std::pair<const char*, double>{"tb", 1e12},
                                    {"gb", 1e9},
                                    {"mb", 1e6},
                                    {"kb", 1e3},
                                    {"b", 1}}) {
        if (s.ends_with(suffix)) {
            factor = f;
            s.resize(s.size() - std::string_view(suffix).size());
            break;
        }
    }
    double number = 0;
    const auto [at, ec] = std::from_chars(s.data(), s.data() + s.size(), number);
    if (s.empty() || ec != std::errc{} || at != s.data() + s.size() || !(number >= 0)
        || number * factor > 9e18) {
        wrong("\"" + std::string(text) + "\" is not a size. Give it as 50GB, 500MB or none.");
    }
    return static_cast<std::int64_t>(std::llround(number * factor));
}

}  // namespace seventhslide::server
