#include "seventhslide/server/config_model.hpp"

#include "commands.hpp"
#include "seventhslide/link/sync_node.hpp"
#include "seventhslide/platform/computer_name.hpp"
#include "seventhslide/server/command_line.hpp"
#include "seventhslide/server/control.hpp"
#include "seventhslide/sync/admin_key.hpp"
#include "seventhslide/util/strings.hpp"


#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace seventhslide::server {
namespace {

using nlohmann::json;

/// Long enough for the admin key to be unlocked, and for work on a slow computer.
constexpr int k_work_ms = 120000;
constexpr int k_look_ms = 10000;

const json* library_in(const Snapshot& now, const std::string& uid)
{
    if (!now.status.is_object() || !now.status.contains("libraries")) {
        return nullptr;
    }
    for (const auto& lib : now.status["libraries"]) {
        if (lib.value("uid", std::string()) == uid) {
            return &lib;
        }
    }
    return nullptr;
}

const json* computer_in(const json& lib, const std::string& machine)
{
    for (const auto& c : lib["computers"]) {
        if (c.value("machine", std::string()) == machine) {
            return &c;
        }
    }
    return nullptr;
}

/// Whether this computer can do admin work on ``lib``.
bool holds_admin_key(const json& lib)
{
    if (lib.value("standing", std::string()) != "member") {
        return false;
    }
    return std::ranges::any_of(lib["computers"], [](const json& c) {
        return c.value("this_computer", false) && c.value("holds_admin_key", false);
    });
}

FormField password_field()
{
    return {FormField::Kind::Secret, "Admin password", "Asked each time, and never kept", {}};
}

std::vector<FormField> new_password_fields()
{
    return {{FormField::Kind::Secret, "New admin password", {}, {}},
            {FormField::Kind::Secret, "The new password again", {}, {}}};
}

/// Why the two answers are no new password; empty when they are one.
std::string new_password_problem(const std::string& first, const std::string& again)
{
    if (const auto problem = sync::passphrase_problem(first)) {
        return *problem;
    }
    return first == again ? std::string() : "Those two were not the same.";
}

Done told(std::string message)
{
    return {true, std::move(message), {}};
}

Done refused(std::string why)
{
    return {false, std::move(why), {}};
}

std::string recovery_message(const std::string& first_line)
{
    return first_line + " Write the recovery code down and keep it somewhere safe: it is the way back "
                        "if the admin password is forgotten. The old one no longer works.";
}

const json* invite_in(const json& lib, const std::string& id)
{
    const auto invites = lib.find("invites");
    if (invites == lib.end()) {
        return nullptr;
    }
    for (const auto& i : *invites) {
        if (i.value("id", std::string()) == id) {
            return &i;
        }
    }
    return nullptr;
}

std::vector<std::string> strings_in(const json& j)
{
    std::vector<std::string> out;
    for (const auto& a : j) {
        if (a.is_string()) {
            out.push_back(a.get<std::string>());
        }
    }
    return out;
}

std::string joined(const std::vector<std::string>& addresses)
{
    std::string out;
    for (const auto& a : addresses) {
        out += (out.empty() ? "" : ", ") + a;
    }
    return out;
}

/// A form's addresses, apart by commas or spaces, as the server takes them.
std::vector<std::string> addresses_typed(const std::string& text)
{
    auto read = link::read_typed_addresses(text);
    if (!read.not_an_address.empty()) {
        throw std::invalid_argument("\"" + read.not_an_address + "\" is not an address.");
    }
    return std::move(read.addresses);
}

/// The form for where this server reaches a computer, or the one an invite is for;
/// ``set`` the addresses typed.
Form addresses_form(std::string title, std::string intro, const std::vector<std::string>& typed,
                    std::function<Done(std::vector<std::string>)> set)
{
    Form form;
    form.title = std::move(title);
    form.intro = std::move(intro);
    form.fields = {{FormField::Kind::Text, "Reach it at", "HOST or HOST:PORT, apart by commas; empty for none",
                    joined(typed)}};
    form.go = "Set Them";
    form.submit = [set = std::move(set)](const std::vector<std::string>& a) {
        try {
            return set(addresses_typed(a.at(0)));
        } catch (const std::invalid_argument& ex) {
            return refused(ex.what());
        }
    };
    return form;
}

}  // namespace

std::vector<LibraryRow> library_rows(const Snapshot& now)
{
    std::vector<LibraryRow> out;
    if (!now.status.is_object() || !now.status.contains("libraries")) {
        return out;
    }
    for (const auto& lib : now.status["libraries"]) {
        const auto uid = lib.value("uid", std::string());
        out.push_back({uid, uid.substr(0, 8), describe_library_state(lib)});
    }
    return out;
}

std::vector<ComputerRow> computer_rows(const Snapshot& now, const std::string& uid)
{
    std::vector<ComputerRow> out;
    const auto* lib = library_in(now, uid);
    if (lib == nullptr) {
        return out;
    }
    for (const auto& c : (*lib)["computers"]) {
        if (c.value("removed", false)) {
            continue;
        }
        const bool viewer = c.value("role", std::string()) == "viewer";
        out.push_back({c.value("machine", std::string()), {}, c.value("name", std::string()),
                       !viewer ? "Editor" : c.value("holds_admin_key", false) ? "Viewer with the admin key" : "Viewer",
                       describe_computer_state(c), strings_in(c.value("typed_addresses", json::array()))});
    }
    for (const auto& i : lib->value("invites", json::array())) {
        const bool viewer = i.value("role", std::string()) == "viewer";
        const auto id = i.value("id", std::string());
        out.push_back({{}, id, std::string(viewer ? "Invited viewer " : "Invited editor ") + id.substr(0, 6),
                       viewer ? "Viewer" : "Editor",
                       "Waiting to join, until " + describe_when(i.value("expires", std::string())) + ". Made by "
                           + i.value("made_by", std::string()),
                       strings_in(i.value("addresses", json::array()))});
    }
    return out;
}

SettingsFields settings_fields(const Snapshot& now)
{
    SettingsFields out;
    if (!now.settings.is_object() || !now.settings.contains("settings")) {
        return out;
    }
    const auto& saved = now.settings["settings"];
    out.name = saved.value("computer_name", std::string());
    if (out.name.empty()) {
        // What it goes by unless told otherwise.
        const auto it = now.settings.find("now");
        out.name = it != now.settings.end() ? it->value("computer_name", std::string())
                                            : platform::computer_name();
    }
    for (const auto& a : saved["addresses"]) {
        out.addresses += (out.addresses.empty() ? "" : ", ") + a.get<std::string>();
    }
    if (saved["port"].is_number_integer()) {
        out.port = std::to_string(saved["port"].get<std::int64_t>());
    }
    out.discovery = saved.value("discovery", true);
    out.sync_automatically = saved.value("sync_automatically", true);
    out.upload_cap = link::upload_cap_text(saved.value("upload_cap_kbit", std::int64_t{0}));
    out.share_address = saved.value("share_address", true);
    return out;
}

std::string setting_note(const Snapshot& now, const std::string& name)
{
    if (!now.settings.is_object()) {
        return {};
    }
    const auto lists = [&](const char* list) {
        const auto it = now.settings.find(list);
        return it != now.settings.end() && std::ranges::find(*it, json(name)) != it->end();
    };
    if (lists("flags")) {
        const auto& value = now.settings["now"][name];
        return "A flag sets this for this run: " + describe_setting(value);
    }
    return lists("after_restart") ? "Takes effect when the server next starts" : "";
}

ConfigModel::ConfigModel(std::filesystem::path data, AskServer ask)
    : data_(std::move(data)), ask_(std::move(ask))
{
    if (!ask_) {
        ask_ = [data = data_](const json& request, int timeout_ms) { return ask_server(data, request, timeout_ms); };
    }
}

Snapshot ConfigModel::snapshot() const
{
    Snapshot out;
    try {
        out.status = ask_({{"command", "status"}}, k_look_ms);
        out.running = out.status.value("ok", false);
        if (!out.running) {
            out.problem = out.status.value("error", std::string("The server would not say how it is."));
            out.status = nullptr;
        }
    } catch (const NoServer&) {
    } catch (const std::runtime_error& ex) {
        out.problem = ex.what();
    }
    try {
        auto settings = ask_settings(data_, ask_);
        if (settings.value("ok", false)) {
            out.settings = std::move(settings);
        } else if (out.problem.empty()) {
            out.problem = settings.value("error", std::string());
        }
    } catch (const std::runtime_error& ex) {
        if (out.problem.empty()) {
            out.problem = ex.what();
        }
    }
    return out;
}

Done ConfigModel::done_by(const json& request, const std::function<Done(const json&)>& said) const
{
    try {
        const auto reply = ask_(request, k_work_ms);
        if (!reply.value("ok", false)) {
            return refused(reply.value("error", std::string("The server could not do that.")));
        }
        return said(reply);
    } catch (const std::runtime_error& ex) {
        return refused(ex.what());
    }
}

Done ConfigModel::sync_now() const
{
    return done_by({{"command", "sync"}}, [](const json&) { return told("Syncing every library now."); });
}

Done ConfigModel::save_settings(const Snapshot& now, const SettingsFields& fields) const
{
    const auto before = settings_fields(now);
    json changes = json::object();
    if (util::trim(fields.name) != before.name) {
        changes["computer_name"] = fields.name;
    }
    if (fields.addresses != before.addresses) {
        const auto read = link::read_typed_addresses(fields.addresses);
        if (!read.not_an_address.empty()) {
            return refused("\"" + read.not_an_address + "\" is not an address.");
        }
        changes["addresses"] = read.addresses;
    }
    if (const auto port = util::trim(fields.port); port != before.port) {
        try {
            changes["port"] = port.empty() ? json(nullptr) : json(port_setting(port));
        } catch (const std::invalid_argument& ex) {
            return refused(ex.what());
        }
    }
    if (fields.discovery != before.discovery) {
        changes["discovery"] = fields.discovery;
    }
    if (fields.sync_automatically != before.sync_automatically) {
        changes["sync_automatically"] = fields.sync_automatically;
    }
    if (const auto cap = util::trim(fields.upload_cap); cap != before.upload_cap) {
        const auto kbit = link::read_upload_cap(cap);
        if (!kbit) {
            return refused(upload_cap_problem());
        }
        changes["upload_cap_kbit"] = *kbit;
    }
    if (fields.share_address != before.share_address) {
        changes["share_address"] = fields.share_address;
    }
    if (changes.empty()) {
        return told("Nothing to save: these are the settings already.");
    }
    try {
        const auto reply = ask_settings(data_, ask_, changes);
        if (!reply.value("ok", false)) {
            return refused(reply.value("error", std::string("The settings could not be saved.")));
        }
        if (!reply.value("running", false)) {
            return told("Saved. The server starts with these.");
        }
        return told(reply.value("after_restart", json::array()).empty()
                        ? "Saved."
                        : "Saved. Some take effect when the server next starts.");
    } catch (const std::runtime_error& ex) {
        return refused(ex.what());
    }
}

Form ConfigModel::join_form() const
{
    Form form;
    form.title = "Join a Synced Library";
    form.intro = "Paste the invite made on a computer of the library. It comes once a computer of the "
                 "library is reached.";
    form.fields = {{FormField::Kind::Secret, "Invite", "Kept out of the screen, as it lets a computer in", {}}};
    form.go = "Join";
    form.submit = [this](const std::vector<std::string>& answers) {
        const auto code = util::trim(answers.at(0));
        if (code.empty()) {
            return refused("Paste the invite.");
        }
        return done_by({{"command", "join"}, {"code", code}}, [](const json& reply) {
            return told("Joining library " + reply.value("library", std::string())
                        + ". It comes once a computer of the library is reached.");
        });
    };
    return form;
}

Form ConfigModel::admin_form(std::string title, std::string intro, std::string go, std::vector<FormField> fields,
                             std::function<json(const std::vector<std::string>&)> request,
                             std::function<Done(const json&)> said) const
{
    Form form;
    form.title = std::move(title);
    form.intro = std::move(intro);
    form.fields = std::move(fields);
    form.go = std::move(go);
    form.submit = [this, request = std::move(request), said = std::move(said)](const std::vector<std::string>& answers) {
        try {
            return done_by(request(answers), said);
        } catch (const std::invalid_argument& ex) {
            return refused(ex.what());
        }
    };
    return form;
}

std::vector<Action> ConfigModel::library_actions(const Snapshot& now, const std::string& uid) const
{
    std::vector<Action> out;
    const auto* lib = library_in(now, uid);
    if (lib == nullptr) {
        return out;
    }
    const auto admin_request = [uid](const char* action) {
        return json{{"command", "admin"}, {"library", uid}, {"action", action}};
    };
    if (holds_admin_key(*lib)) {
        const auto invite = [&](std::string label, std::string detail, std::string title, std::string role) {
            auto form = admin_form(
                title, detail, "Make the Invite",
                {{FormField::Kind::Text, "Good for (days)", "1 to 30", "7"}, password_field()},
                [admin_request, role](const std::vector<std::string>& a) {
                    int days = 0;
                    const auto text = util::trim(a.at(0));
                    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), days);
                    if (ec != std::errc{} || end != text.data() + text.size() || days < 1 || days > 30) {
                        throw std::invalid_argument("An invite is good for 1 to 30 days.");
                    }
                    auto request = admin_request("invite");
                    request["role"] = role;
                    request["days"] = days;
                    request["password"] = a.at(1);
                    return request;
                },
                [](const json& reply) {
                    return Done{true,
                                "Good once, until " + describe_when(reply.value("expires", std::string()))
                                    + ". On the computer joining: Settings > Libraries & Profiles > New... > Join a "
                                      "Synced Library, and paste it. Without a window: seventhslide --server join",
                                reply.value("code", std::string())};
                });
            out.push_back({std::move(label), form.intro, std::move(form)});
        };
        invite("Invite an Editor…", "Changes anything in the library", "Invite an editor", "editor");
        invite("Invite a Viewer…", "Only receives and presents: a stage display", "Invite a viewer", "viewer");

        const auto limit = lib->value("size_limit", std::int64_t{0});
        auto size = admin_form(
            "Size limit", "How much media the library may hold; none for no limit.", "Set It",
            {{FormField::Kind::Text, "Size limit", "50GB, 500MB or none", limit > 0 ? describe_size(limit) : "none"},
             password_field()},
            [admin_request](const std::vector<std::string>& a) {
                auto request = admin_request("limit");
                request["bytes"] = parse_size(a.at(0));
                request["password"] = a.at(1);
                return request;
            },
            [](const json&) { return told("The library's size limit is set."); });
        out.push_back({"Size Limit…", size.intro, std::move(size)});

        auto fields = new_password_fields();
        fields.insert(fields.begin(), {FormField::Kind::Secret, "Current admin password", {}, {}});
        const auto password_said = [](const json& reply) {
            const auto code = reply.value("recovery_code", std::string());
            return code.empty() ? told("The admin password is changed on every computer of the library.")
                                : Done{true, recovery_message("The admin password is changed."), code};
        };
        auto change = admin_form(
            "Change Password", "The admin password of the library, on every computer of it.", "Change It", std::move(fields),
            [admin_request](const std::vector<std::string>& a) {
                if (const auto problem = new_password_problem(a.at(1), a.at(2)); !problem.empty()) {
                    throw std::invalid_argument(problem);
                }
                auto request = admin_request("password");
                request["password"] = a.at(0);
                request["new_password"] = a.at(1);
                return request;
            },
            password_said);
        out.push_back({"Change Password…", change.intro, std::move(change)});

        fields = new_password_fields();
        fields.insert(fields.begin(), {FormField::Kind::Secret, "Recovery code", {}, {}});
        auto forgotten = admin_form(
            "Forgotten Password", "Set a new one with the recovery code.", "Change It", std::move(fields),
            [admin_request](const std::vector<std::string>& a) {
                if (const auto problem = new_password_problem(a.at(1), a.at(2)); !problem.empty()) {
                    throw std::invalid_argument(problem);
                }
                auto request = admin_request("password");
                request["recovery_code"] = a.at(0);
                request["new_password"] = a.at(1);
                return request;
            },
            password_said);
        out.push_back({"Forgotten Password…", forgotten.intro, std::move(forgotten)});

        auto recovery = admin_form(
            "New Recovery Code", "The old code stops working.", "Make It", {password_field()},
            [admin_request](const std::vector<std::string>& a) {
                auto request = admin_request("recovery");
                request["password"] = a.at(0);
                return request;
            },
            [](const json& reply) {
                return Done{true, recovery_message("A new recovery code."), reply.value("recovery_code", std::string())};
            });
        out.push_back({"New Recovery Code…", recovery.intro, std::move(recovery)});
    }

    Form leave;
    leave.title = "Stop syncing";
    leave.intro = "This server will stop holding library " + uid + ". Its copy is moved to the data folder's "
                  "\"left\" folder, to delete when no longer wanted.";
    leave.go = "Stop Syncing";
    leave.submit = [this, uid](const std::vector<std::string>&) {
        return done_by({{"command", "leave"}, {"library", uid}}, [](const json& reply) {
            std::string message = "The server no longer holds the library. Its copy is in "
                                  + reply.value("kept_at", std::string()) + ".";
            if (reply.value("still_listed", false)) {
                message += " The library still lists this server, so its computers will keep looking for it: "
                           "remove it there, on an editor computer.";
            }
            return told(std::move(message));
        });
    };
    out.push_back({"Stop Syncing…", "This server stops holding the library", std::move(leave)});
    return out;
}

std::vector<Action> ConfigModel::computer_actions(const Snapshot& now, const std::string& uid,
                                                  const std::string& machine) const
{
    std::vector<Action> out;
    const auto* lib = library_in(now, uid);
    const auto* computer = lib != nullptr ? computer_in(*lib, machine) : nullptr;
    if (computer == nullptr || computer->value("removed", false) || computer->value("this_computer", false)) {
        return out;
    }
    const auto name = computer->value("name", std::string());
    const auto request_for = [uid, machine](const char* action) {
        return json{{"command", "admin"}, {"library", uid}, {"action", action}, {"computer", machine}};
    };

    const auto own = strings_in(computer->value("addresses", json::array()));
    auto address = addresses_form(
        "Addresses",
        "Where this server reaches " + name + " before the addresses it gave"
            + (own.empty() ? std::string(" (it gave none)") : " (" + joined(own) + ")")
            + ", when those changed and this server did not hear. On this server alone. Its key is still checked there.",
        strings_in(computer->value("typed_addresses", json::array())),
        [this, uid, machine, name](std::vector<std::string> addresses) {
            return done_by({{"command", "address"}, {"library", uid}, {"computer", machine}, {"addresses", addresses}},
                           [name](const json& reply) {
                               const auto where = joined(strings_in(reply.value("addresses", json::array())));
                               return told(where.empty() ? name + " is reached at the addresses it gave."
                                                         : name + " is reached at " + where + " before any it gave.");
                           });
        });
    out.push_back({"Addresses…", "Where this server reaches it", std::move(address)});

    if (!holds_admin_key(*lib)) {
        return out;
    }
    const bool viewer = computer->value("role", std::string()) == "viewer";
    const std::string to = viewer ? "editor" : "viewer";
    auto role = admin_form(
        viewer ? "Make an editor" : "Make a viewer",
        viewer ? name + " will change anything in the library." : name + " will only receive the library and present from it.",
        viewer ? "Make an Editor" : "Make a Viewer", {password_field()},
        [request_for, to](const std::vector<std::string>& a) {
            auto request = request_for("role");
            request["role"] = to;
            request["password"] = a.at(0);
            return request;
        },
        [name, viewer](const json&) { return told(name + " is now " + (viewer ? "an editor." : "a viewer.")); });
    out.push_back({viewer ? "Make an Editor…" : "Make a Viewer…", role.intro, std::move(role)});

    if (viewer) {
        // An editor holds it whatever is said, as ``holds_admin_key`` then reads.
        const bool holds = computer->value("holds_admin_key", false);
        auto key = admin_form(
            holds ? "Stop sending the admin key" : "Give the admin key",
            holds ? "It is sent no more. A copy it has stays until the password changes."
                  : "For admin work there, with the password.",
            holds ? "Stop Sending It" : "Give It", {password_field()},
            [request_for, holds](const std::vector<std::string>& a) {
                auto request = request_for("key");
                request["holds"] = !holds;
                request["password"] = a.at(0);
                return request;
            },
            [name, holds](const json&) {
                return told(holds ? name + " is sent the admin key no more. A copy it has stays until the password changes."
                                  : name + " gets a copy of the locked admin key.");
            });
        out.push_back({holds ? "Stop Sending the Admin Key…" : "Give the Admin Key…", key.intro, std::move(key)});
    }

    auto remove = admin_form(
        "Remove computer",
        "Remove " + name + " from the library for good? What it already sent stays. It can come back only with a "
        "new invite after SeventhSlide is installed on it again.",
        "Remove It", {password_field()},
        [request_for](const std::vector<std::string>& a) {
            auto request = request_for("remove");
            request["password"] = a.at(0);
            return request;
        },
        [name](const json&) { return told(name + " was removed from the library."); });
    out.push_back({"Remove…", "For good", std::move(remove)});
    return out;
}

std::vector<Action> ConfigModel::invite_actions(const Snapshot& now, const std::string& uid,
                                                const std::string& invite) const
{
    std::vector<Action> out;
    const auto* lib = library_in(now, uid);
    const auto* waiting = lib != nullptr ? invite_in(*lib, invite) : nullptr;
    if (waiting == nullptr) {
        return out;
    }
    auto address = addresses_form(
        "Where it is",
        "Where the computer this invite is for can be found, when it can reach no computer of the library: this "
        "server reaches out to it there with the invite until it comes. It gets in only by proving it holds the "
        "invite, and anything else there gets nothing.",
        strings_in(waiting->value("addresses", json::array())),
        [this, uid, invite](std::vector<std::string> addresses) {
            return done_by({{"command", "address"}, {"library", uid}, {"computer", invite}, {"addresses", addresses}},
                           [](const json& reply) {
                               const auto where = joined(strings_in(reply.value("addresses", json::array())));
                               return told(where.empty() ? "Its computer is waited for, not reached out to."
                                                         : "Its computer is reached out to at " + where + ".");
                           });
        });
    out.push_back({"Where It Is…", "For a computer that can reach no other", std::move(address)});

    if (!holds_admin_key(*lib)) {
        return out;
    }
    auto withdraw = admin_form(
        "Withdraw the invite", "No computer can join by this invite from now on, wherever its code was given.",
        "Withdraw It", {password_field()},
        [uid, invite](const std::vector<std::string>& a) {
            return json{{"command", "admin"},
                        {"library", uid},
                        {"action", "withdraw"},
                        {"invite", invite},
                        {"password", a.at(0)}};
        },
        [](const json&) { return told("The invite lets no computer in now."); });
    out.push_back({"Withdraw…", "No computer joins by it", std::move(withdraw)});
    return out;
}

}  // namespace seventhslide::server
