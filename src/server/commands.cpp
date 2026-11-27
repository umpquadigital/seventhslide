#include "commands.hpp"

#include "seventhslide/link/sync_node.hpp"
#include "seventhslide/server/control.hpp"
#include "seventhslide/server/server.hpp"
#include "seventhslide/server/settings_edit.hpp"
#include "seventhslide/sync/admin_key.hpp"
#include "seventhslide/util/strings.hpp"
#include "terminal.hpp"

#include <QDateTime>
#include <QString>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace seventhslide::server {
namespace {

using nlohmann::json;

/// What the server said could not be done.
struct NotDone : std::runtime_error {
    using std::runtime_error::runtime_error;
};

json ask(const CommandLine& cl, const json& request)
{
    auto reply = ask_server(cl.data, request);
    if (!reply.value("ok", false)) {
        throw NotDone(reply.value("error", std::string("The server could not do that.")));
    }
    return reply;
}

std::string text(const json& j, const char* name)
{
    const auto it = j.find(name);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::int64_t number(const json& j, const char* name)
{
    const auto it = j.find(name);
    return it != j.end() && it->is_number_integer() ? it->get<std::int64_t>() : 0;
}

std::string padded(const std::string& s, std::size_t width)
{
    return s.size() >= width ? s + " " : s + std::string(width - s.size(), ' ');
}

/// The library ``cl`` names, from what the server holds.
std::string library_of(const CommandLine& cl, const json& status)
{
    std::vector<std::string> uids;
    for (const auto& lib : status["libraries"]) {
        uids.push_back(text(lib, "uid"));
    }
    const auto choice = choose_library(uids, cl.library);
    if (!choice.error.empty()) {
        throw NotDone(choice.error);
    }
    return choice.uid;
}

const json& library_in(const json& status, const std::string& uid)
{
    for (const auto& lib : status["libraries"]) {
        if (text(lib, "uid") == uid) {
            return lib;
        }
    }
    throw NotDone("This server holds no library " + uid + ".");
}

std::string computer_names(const json& lib)
{
    std::string out;
    for (const auto& c : lib["computers"]) {
        if (!c.value("removed", false) && !c.value("this_computer", false)) {
            out += (out.empty() ? "" : ", ") + text(c, "name");
        }
    }
    return out.empty() ? "none yet" : out;
}

std::string new_password()
{
    for (;;) {
        const auto first = read_secret("New admin password: ");
        if (const auto problem = sync::passphrase_problem(first)) {
            std::cerr << *problem << '\n';
            continue;
        }
        if (read_secret("The new password again: ") == first) {
            return first;
        }
        std::cerr << "Those were not the same. Once more.\n";
    }
}

void say_recovery_code(const std::string& code)
{
    std::cout << "\nThe library's new recovery code:\n\n    " << code
              << "\n\nWrite it down and keep it somewhere safe: it is the way back if the admin\n"
                 "password is forgotten. The old one no longer works.\n";
}

int join(const CommandLine& cl)
{
    // Asked for, the code is seen by no other user of this computer, and stays
    // in no history; given on the command line, it is for a script's sake.
    const auto code = cl.code.empty() ? read_secret("The invite: ") : cl.code;
    const auto reply = ask(cl, {{"command", "join"}, {"code", util::trim(code)}});
    std::cout << "Joining library " << text(reply, "library")
              << ". It comes once a computer of the library is reached;\n"
                 "status shows how it is getting on.\n";
    return 0;
}

int sync_now(const CommandLine& cl)
{
    (void)ask(cl, {{"command", "sync"}});
    std::cout << "Syncing every library now.\n";
    return 0;
}

int status(const CommandLine& cl)
{
    const auto reply = ask(cl, {{"command", "status"}});
    if (cl.json) {
        std::cout << reply.dump(2) << '\n';
    } else {
        std::cout << describe_status(reply);
    }
    return 0;
}

int admin(const CommandLine& cl)
{
    const auto state = ask(cl, {{"command", "status"}});
    const auto uid = library_of(cl, state);
    const auto& lib = library_in(state, uid);
    const auto& action = cl.action;
    if (action.empty()) {
        std::ostringstream out;
        out << "Library " << uid << "\n";
        for (const auto& c : lib["computers"]) {
            out << "  " << padded(text(c, "name"), 20) << padded(text(c, "role"), 8)
                << (c.value("removed", false) ? "removed" : text(c, "machine")) << '\n';
        }
        if (const auto it = lib.find("invites"); it != lib.end() && !it->empty()) {
            out << "\nInvites waiting:\n";
            for (const auto& i : *it) {
                out << "  " << padded(text(i, "id"), 20) << padded(text(i, "role"), 8) << "until "
                    << describe_when(text(i, "expires")) << '\n';
            }
        }
        out << "\nAdmin work: seventhslide --server admin " << cl.library
            << " invite|withdraw|role|key|remove|limit|password|recovery ... (see --server help)\n";
        std::cout << out.str();
        return 0;
    }
    const auto takes = [&](std::size_t n, const char* form) {
        if (action.size() != n) {
            throw std::invalid_argument(std::string("Give it as: admin LIB ") + form);
        }
    };
    json request{{"command", "admin"}, {"library", uid}, {"action", action[0]}};
    const auto& verb = action[0];
    if (verb == "invite") {
        takes(2, "invite editor|viewer [--days N] [--at HOST[:PORT]]...");
        request["role"] = action[1];
        request["days"] = cl.days;
        if (!cl.at.empty()) {
            request["joining_at"] = cl.at;
        }
    } else if (verb == "withdraw") {
        takes(2, "withdraw INVITE");
        request["invite"] = action[1];
    } else if (verb == "role") {
        takes(3, "role COMPUTER editor|viewer");
        request["computer"] = action[1];
        request["role"] = action[2];
    } else if (verb == "key") {
        takes(3, "key COMPUTER on|off");
        if (action[2] != "on" && action[2] != "off") {
            throw std::invalid_argument("Give it as: admin LIB key COMPUTER on|off");
        }
        request["action"] = "key";
        request["computer"] = action[1];
        request["holds"] = action[2] == "on";
    } else if (verb == "remove") {
        takes(2, "remove COMPUTER");
        request["computer"] = action[1];
        if (!cl.yes && !confirm("Remove \"" + action[1]
                                + "\" from the library for good? It can be invited again only "
                                  "after a reinstall. Type yes to go on: ")) {
            std::cout << "Nothing was changed.\n";
            return 1;
        }
    } else if (verb == "limit") {
        takes(2, "limit SIZE|none");
        request["bytes"] = parse_size(action[1]);
    } else if (verb == "password") {
        takes(1, "password [--recovery]");
        if (cl.recovery) {
            request["recovery_code"] = read_secret("The library's recovery code: ");
        } else {
            request["password"] = read_secret("The current admin password: ");
        }
        request["new_password"] = new_password();
    } else if (verb == "recovery") {
        takes(1, "recovery");
    } else {
        throw std::invalid_argument("There is no admin action \"" + verb + "\".");
    }
    if (!request.contains("password") && !request.contains("recovery_code")) {
        request["password"] = read_secret("The library's admin password: ");
    }
    std::cerr << "Working...\n";
    const auto reply = ask(cl, request);
    if (verb == "invite") {
        std::cout << "Invite for " << (action[1] == "viewer" ? "a viewer" : "an editor")
                  << ", good once until " << describe_when(text(reply, "expires")) << ":\n\n"
                  << text(reply, "code")
                  << "\n\nOn the computer joining: Settings > Libraries & Profiles > New... > Join a "
                     "Synced Library, and paste it. Without a window: seventhslide --server join\n";
    } else if (verb == "withdraw") {
        std::cout << "Invite " << text(reply, "invite") << " lets no computer in now.\n";
    } else if (verb == "role") {
        std::cout << action[1] << " is now " << (action[2] == "viewer" ? "a viewer" : "an editor")
                  << ".\n";
    } else if (verb == "key") {
        std::cout << (action[2] == "on"
                          ? action[1] + " gets a copy of the locked admin key.\n"
                          : action[1] + " is sent the admin key no more. A copy it has stays until the "
                                        "password changes.\n");
    } else if (verb == "remove") {
        std::cout << text(reply, "removed") << " was removed from the library.\n";
    } else if (verb == "limit") {
        const auto bytes = request["bytes"].get<std::int64_t>();
        std::cout << (bytes == 0 ? "The library has no size limit now.\n"
                                 : "The library's size limit is now " + describe_size(bytes) + ".\n");
    } else if (verb == "password") {
        std::cout << "The admin password is changed.\n";
        if (reply.contains("recovery_code")) {
            say_recovery_code(text(reply, "recovery_code"));
        }
    } else if (verb == "recovery") {
        say_recovery_code(text(reply, "recovery_code"));
    }
    return 0;
}

int address(const CommandLine& cl)
{
    const auto state = ask(cl, {{"command", "status"}});
    json request{{"command", "address"}, {"library", library_of(cl, state)}, {"computer", cl.action[0]}};
    if (cl.action.size() > 1) {
        const std::vector<std::string> given(cl.action.begin() + 1, cl.action.end());
        const bool none = std::ranges::find(given, "none") != given.end();
        if (none && given.size() > 1) {
            throw std::invalid_argument("\"none\" takes the addresses away: give it alone.");
        }
        request["addresses"] = none ? std::vector<std::string>{} : given;
    }
    const auto reply = ask(cl, request);
    std::string where;
    for (const auto& a : reply.value("addresses", json::array())) {
        where += (where.empty() ? "" : ", ") + a.get<std::string>();
    }
    if (reply.contains("invite")) {
        std::cout << (where.empty() ? "The computer invite " + text(reply, "invite")
                                          + " is for is waited for, not reached out to.\n"
                                    : "The computer invite " + text(reply, "invite") + " is for is reached out to at "
                                          + where + " until it comes. It gets in only with the invite.\n");
    } else if (where.empty()) {
        std::cout << text(reply, "name") << " is reached at the addresses it gave.\n";
    } else {
        std::cout << text(reply, "name") << " is reached at " << where
                  << " before any it gave, by this server. Its key is still checked there.\n";
    }
    return 0;
}

/// What ``cl`` asks to change, as the settings reply names it.
json settings_changes(const std::vector<std::string>& action)
{
    const auto& name = action[0];
    const auto one = [&]() -> const std::string& {
        if (action.size() != 2) {
            throw std::invalid_argument("\"settings " + name + "\" takes one value.");
        }
        return action[1];
    };
    const auto on_or_off = [&] {
        if (one() != "on" && one() != "off") {
            throw std::invalid_argument("\"settings " + name + "\" takes on or off.");
        }
        return one() == "on";
    };
    if (name == "name") {
        return {{"computer_name", one()}};
    }
    if (name == "addresses") {
        if (action.size() == 2 && action[1] == "none") {
            return {{"addresses", json::array()}};
        }
        std::string typed;
        for (std::size_t i = 1; i < action.size(); ++i) {
            typed += action[i] + " ";
        }
        const auto read = link::read_typed_addresses(typed);
        if (!read.not_an_address.empty()) {
            throw std::invalid_argument("\"" + read.not_an_address + "\" is not an address.");
        }
        return {{"addresses", read.addresses}};
    }
    if (name == "port") {
        return {{"port", one() == "none" ? json(nullptr) : json(port_setting(one()))}};
    }
    if (name == "discovery") {
        return {{"discovery", on_or_off()}};
    }
    if (name == "sync") {
        return {{"sync_automatically", on_or_off()}};
    }
    if (name == "sharing") {
        return {{"share_address", on_or_off()}};
    }
    if (name == "upload-cap") {
        const auto kbit = link::read_upload_cap(one());
        if (!kbit) {
            throw std::invalid_argument(upload_cap_problem());
        }
        return {{"upload_cap_kbit", *kbit}};
    }
    throw std::invalid_argument("There is no setting \"" + name + "\".");
}

int settings(const CommandLine& cl)
{
    const AskServer asker = [&](const json& request, int timeout_ms) { return ask_server(cl.data, request, timeout_ms); };
    const auto reply = ask_settings(cl.data, asker, cl.action.empty() ? std::nullopt : std::optional(settings_changes(cl.action)));
    if (!reply.value("ok", false)) {
        throw NotDone(text(reply, "error"));
    }
    if (cl.json) {
        std::cout << reply.dump(2) << '\n';
    } else {
        std::cout << describe_settings(reply);
    }
    return 0;
}

int leave(const CommandLine& cl)
{
    const auto state = ask(cl, {{"command", "status"}});
    const auto uid = library_of(cl, state);
    const auto& lib = library_in(state, uid);
    if (!cl.yes) {
        std::cout << "This server will stop holding library " << uid << " (computers: "
                  << computer_names(lib) << ").\nIts copy is moved to the data folder's \"left\" "
                                            "folder, to delete when no longer wanted.\n";
        if (!confirm("Type yes to go on: ")) {
            std::cout << "Nothing was changed.\n";
            return 1;
        }
    }
    const auto reply = ask(cl, {{"command", "leave"}, {"library", uid}});
    std::cout << "The server no longer holds the library. Its copy is in " << text(reply, "kept_at")
              << ".\n";
    if (reply.value("still_listed", false)) {
        std::cout << "\nThe library still lists this server, so its computers will keep looking for\n"
                     "it. Remove it there: on an editor computer (Settings > Sync), or here before\n"
                     "leaving next time (admin LIB remove \""
                  << text(state["server"], "name") << "\").\n";
    }
    return 0;
}

}  // namespace

std::string describe_when(const std::string& iso)
{
    const auto at = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
    return at.isValid() ? at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")).toStdString()
                        : iso;
}

std::string describe_size(std::int64_t bytes)
{
    const char* units[] = {"bytes", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int unit = 0;
    while (v >= 1000 && unit < 4) {
        v /= 1000;
        ++unit;
    }
    char out[32];
    std::snprintf(out, sizeof out, unit == 0 ? "%.0f %s" : "%.1f %s", v, units[unit]);
    return out;
}

std::string describe_library_state(const json& lib)
{
    const auto uid = text(lib, "uid");
    const auto standing = text(lib, "standing");
    const auto problem = text(lib, "problem");
    if (standing == "waiting") {
        return "Waiting for a computer of the library to be reached.";
    }
    if (standing == "absent") {
        return "Problem: this server is not on the library's member list, so computers do not "
               "take what it writes. Make it a new invite on an editor computer.";
    }
    if (standing == "removed") {
        return "Problem: this server was removed from the library. To let it go: "
               "seventhslide --server leave " + uid.substr(0, 8);
    }
    if (!problem.empty()) {
        return "Problem: " + problem;
    }
    if (!lib.value("joined", false)) {
        return "Getting the library.";
    }
    if (const auto waiting = lib.find("waiting_on");
        waiting != lib.end() && waiting->is_array() && !waiting->empty()) {
        std::string names;
        int unnamed = 0;
        for (const auto& computer : *waiting) {
            const auto name = computer.is_object() ? text(computer, "name") : std::string();
            if (name.empty()) {
                ++unnamed;
                continue;
            }
            names += (names.empty() ? "\"" : ", \"") + name + "\"";
        }
        if (unnamed > 0) {
            names += names.empty() ? "" : ", ";
            names += unnamed == 1 ? std::string("another computer")
                                  : std::to_string(unnamed) + " other computers";
        }
        return "Problem: waiting on changes from " + names
               + ", which this server can't accept. They come once another computer with this version "
                 "of SeventhSlide or later sends a copy of the whole library.";
    }
    if (number(lib, "over_limit") > 0) {
        return "Over its size limit: " + std::to_string(number(lib, "over_limit"))
               + " media file(s) are not kept here until there is room.";
    }
    if (number(lib, "media_coming") > 0) {
        return "Getting " + std::to_string(number(lib, "media_coming")) + " media file(s).";
    }
    if (number(lib, "missing_media") > 0) {
        return "Waiting for " + std::to_string(number(lib, "missing_media")) + " media file(s).";
    }
    if (number(lib, "media_going") > 0) {
        return "Sending " + std::to_string(number(lib, "media_going")) + " media file(s).";
    }
    if (auto why = text(lib, "snapshot_problem"); !why.empty()) {
        if (why.ends_with('.')) {
            why.pop_back();
        }
        return why + ". Old change files are kept until a snapshot can be written. Sync goes on.";
    }
    return "Up to date.";
}

std::string describe_computer_state(const json& computer)
{
    if (computer.value("removed", false)) {
        return "Removed";
    }
    if (computer.value("this_computer", false)) {
        return "This computer";
    }
    if (computer.value("connected", false)) {
        return "Connected";
    }
    if (!text(computer, "last_heard").empty()) {
        return "Last heard " + describe_when(text(computer, "last_heard"));
    }
    return "Not heard from here";
}

std::int64_t port_setting(const std::string& value)
{
    std::int64_t port = 0;
    const auto [at, ec] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (ec != std::errc{} || at != value.data() + value.size() || port < 1 || port > 65535) {
        throw std::invalid_argument("A port is a whole number from 1 to 65535.");
    }
    return port;
}

std::string describe_setting(const json& value)
{
    if (value.is_boolean()) {
        return value.get<bool>() ? "on" : "off";
    }
    if (value.is_array()) {
        std::string out;
        for (const auto& a : value) {
            out += (out.empty() ? "" : ", ") + describe_setting(a);
        }
        return out;
    }
    return value.is_string() ? value.get<std::string>() : value.dump();
}

std::string describe_upload_cap(std::int64_t kbit)
{
    return kbit > 0 ? link::upload_cap_text(kbit) + " Mbit/s, for computers not on this network" : "none";
}

std::string describe_settings(const json& reply)
{
    const auto& saved = reply["settings"];
    const auto marked = [&](const char* name) {
        const auto named = [&](const char* list) {
            const auto it = reply.find(list);
            return it != reply.end() && std::ranges::find(*it, json(name)) != it->end();
        };
        std::string out;
        if (named("flags")) {
            out = " (a flag sets this for this run: " + describe_setting(reply["now"][name]) + ")";
        } else if (named("after_restart")) {
            out = " (takes effect when the server next starts)";
        }
        return out;
    };
    std::ostringstream out;
    const auto name = text(saved, "computer_name");
    out << "Name                     " << (name.empty() ? "this computer's own name" : name) << marked("computer_name") << '\n'
        << "Routable address         "
        << (saved["addresses"].empty() ? "its addresses on its networks" : describe_setting(saved["addresses"]))
        << marked("addresses") << '\n'
        << "Port                     " << (saved["port"].is_null() ? "the usual one, " + std::to_string(link::k_usual_port) : saved["port"].dump())
        << marked("port") << '\n'
        << "Announce on the network  " << describe_setting(saved["discovery"]) << marked("discovery") << '\n'
        << "Sync automatically       " << describe_setting(saved["sync_automatically"]) << '\n'
        << "Upload speed cap         " << describe_upload_cap(saved.value("upload_cap_kbit", std::int64_t{0})) << '\n'
        << "Share its address        " << describe_setting(saved.value("share_address", true)) << '\n';
    if (!reply.value("running", false)) {
        out << "\nThe server is not running: these are what it will start with.\n";
    }
    return out.str();
}

std::string describe_status(const json& status)
{
    std::ostringstream out;
    const auto& s = status["server"];
    out << text(s, "name") << " (SeventhSlide " << text(s, "version") << "), up since "
        << describe_when(text(s, "started")) << "\n"
        << "  Key " << text(s, "fingerprint") << "; listening on port " << number(s, "port") << "\n"
        << "  Computers reach it at:";
    for (const auto& a : s["addresses"]) {
        out << ' ' << a.get<std::string>();
    }
    out << "\n  Data: " << text(s, "data");
    const auto free = number(s, "disk_free");
    const auto total = number(s, "disk_total");
    if (free >= 0 && total > 0) {
        out << ", " << describe_size(free) << " free of " << describe_size(total);
        if (disk_nearly_full(free, total)) {
            out << "\n  Problem: the disk is nearly full. Libraries stop syncing when it is.";
        }
    }
    out << "\n";
    if (!s.value("sync_automatically", true)) {
        out << "  Syncs only when asked: seventhslide --server sync\n";
    }

    const auto& libraries = status["libraries"];
    if (libraries.empty()) {
        out << "\nNo libraries yet. To join one: seventhslide --server join, with an invite "
               "from an editor computer.\n";
    }
    for (const auto& lib : libraries) {
        const auto uid = text(lib, "uid");
        out << "\nLibrary " << uid << "\n  " << describe_library_state(lib);
        if (!text(lib, "last_sync").empty()) {
            out << " Last synced " << describe_when(text(lib, "last_sync")) << ".";
        }
        out << "\n  " << describe_size(number(lib, "media_bytes")) << " of media";
        if (number(lib, "size_limit") > 0) {
            out << " (" << describe_size(number(lib, "size_used")) << " of its " << describe_size(number(lib, "size_limit"))
                << " limit)";
        }
        out << ", " << number(lib, "change_files") << " change file(s).\n  Computers:\n";
        for (const auto& c : lib["computers"]) {
            out << "    " << padded(text(c, "name"), 20);
            if (c.value("removed", false)) {
                out << "removed\n";
                continue;
            }
            out << padded(text(c, "role") + (c.value("holds_admin_key", false) && text(c, "role") == "viewer" ? "+key" : ""), 12);
            if (c.value("this_computer", false)) {
                out << "this server";
            } else if (c.value("connected", false)) {
                out << "connected now";
            } else if (!text(c, "last_heard").empty()) {
                out << "last heard " << describe_when(text(c, "last_heard"));
            } else {
                out << "not heard from here";
            }
            std::string typed;
            for (const auto& a : c.value("typed_addresses", json::array())) {
                typed += (typed.empty() ? "" : ", ") + a.get<std::string>();
            }
            if (!typed.empty()) {
                out << ", set to be reached at " << typed;
            }
            out << '\n';
        }
        if (const auto it = lib.find("invites"); it != lib.end() && !it->empty()) {
            out << "  Invites waiting:\n";
            for (const auto& i : *it) {
                out << "    " << padded(text(i, "id"), 20) << padded(text(i, "role"), 12) << "until "
                    << describe_when(text(i, "expires")) << ", made by " << text(i, "made_by");
                std::string at;
                for (const auto& a : i.value("addresses", json::array())) {
                    at += (at.empty() ? "" : ", ") + a.get<std::string>();
                }
                if (!at.empty()) {
                    out << ", reached out to at " << at;
                }
                out << '\n';
            }
        }
    }

    if (const auto it = status.find("neighbours"); it != status.end() && !it->empty()) {
        out << "\nHeard on the network:\n";
        for (const auto& n : *it) {
            const auto name = text(n, "name");
            out << "  " << padded(name.empty() ? "a SeventhSlide (key " + text(n, "fingerprint").substr(0, 8) + ")" : name, 30)
                << text(n, "address") << ", " << describe_when(text(n, "last_heard")) << '\n';
        }
    }

    const auto& connections = status["connections"];
    out << '\n';
    if (connections.empty()) {
        out << "No computers connected.\n";
    } else {
        out << "Connected now:";
        bool first = true;
        for (const auto& c : connections) {
            out << (first ? " " : ", ") << text(c, "name");
            first = false;
        }
        out << ".\n";
    }
    return out.str();
}

int run_command(const CommandLine& command)
{
    try {
        switch (command.command) {
        case Command::Join:
            return join(command);
        case Command::Status:
            return status(command);
        case Command::Sync:
            return sync_now(command);
        case Command::Admin:
            return admin(command);
        case Command::Address:
            return address(command);
        case Command::Leave:
            return leave(command);
        case Command::Settings:
            return settings(command);
        case Command::Config:
        case Command::Run:
        case Command::Help:
            break;
        }
        return 1;
    } catch (const std::invalid_argument& ex) {
        std::cerr << ex.what() << '\n';
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}

}  // namespace seventhslide::server
