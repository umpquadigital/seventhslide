#include "seventhslide/link/admin.hpp"

#include "seventhslide/sync/admin_key.hpp"

#include <QDateTime>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <vector>

namespace seventhslide::link {
namespace {

using nlohmann::json;

/// A request that cannot be done, said in words.
struct Refused : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string text(const json& request, const char* name)
{
    const auto it = request.find(name);
    if (it == request.end() || !it->is_string()) {
        throw Refused(std::string("The request has no ") + name + ".");
    }
    return it->get<std::string>();
}

std::string lower(std::string_view s)
{
    std::string out(s);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

sync::Role role_of(const json& request)
{
    const auto role = sync::role_from_name(lower(text(request, "role")));
    if (!role) {
        throw Refused("A computer is an editor or a viewer.");
    }
    return *role;
}

crypto::SigningKey unlock(const sync::MemberLog& log, const json& request)
{
    auto admin = log.unlock_admin(text(request, "password"));
    if (!admin) {
        throw Refused("That is not the library's admin password.");
    }
    return std::move(*admin);
}

const sync::Member& computer(const sync::Members& list, const json& request)
{
    const auto found = find_computer(list, text(request, "computer"));
    if (found.member == nullptr) {
        throw Refused(found.error);
    }
    return *found.member;
}

}  // namespace

Found find_computer(const sync::Members& list, std::string_view who)
{
    if (who.empty()) {
        return {nullptr, "Name the computer."};
    }
    const auto wanted = lower(who);
    std::vector<const sync::Member*> by_name;
    std::vector<const sync::Member*> by_id;
    for (const auto& [machine, m] : list.members) {
        if (m.removed) {
            continue;
        }
        if (machine == wanted) {
            return {&m, {}};
        }
        if (lower(m.name) == wanted) {
            by_name.push_back(&m);
        }
        if (machine.starts_with(wanted)) {
            by_id.push_back(&m);
        }
    }
    const auto& matches = by_name.empty() ? by_id : by_name;
    if (matches.size() == 1) {
        return {matches.front(), {}};
    }
    if (matches.empty()) {
        return {nullptr, "No computer in this library is called \"" + std::string(who)
                             + "\". Status lists them."};
    }
    return {nullptr,
            "More than one computer in this library goes by \"" + std::string(who)
                + "\". Give its id, as status shows it.",
            true};
}

FoundInvite find_invite(const sync::Members& list, std::string_view id)
{
    if (id.empty()) {
        return {std::nullopt, "Name the invite, by its id."};
    }
    const auto wanted = lower(id);
    std::vector<sync::InviteMade> matches;
    for (const auto& [invite_id, made] : list.invites) {
        if (invite_id == wanted) {
            return {made, {}};
        }
        if (invite_id.starts_with(wanted)) {
            matches.push_back(made);
        }
    }
    if (matches.size() == 1) {
        return {std::move(matches.front()), {}};
    }
    if (matches.empty()) {
        return {std::nullopt, "No invite of this library has the id \"" + std::string(id)
                                  + "\" and is still to be used. Status lists those waiting."};
    }
    return {std::nullopt,
            "More than one invite of this library has an id starting \"" + std::string(id)
                + "\". Give more of it.",
            true};
}

json admin_work(sync::SyncStore& store, const sync::Identity& me, const std::string& library,
                const json& request, crypto::BoxParams params)
{
    try {
        sync::MemberLog log(store, me, library);
        const auto list = log.members();
        if (!list.founded()) {
            throw Refused("This library has not come to this computer yet.");
        }
        const auto* self = list.find(me.machine);
        if (self == nullptr || self->removed) {
            throw Refused("This computer is not a member of the library, so it cannot do admin "
                          "work on it. Do it on an editor computer.");
        }
        const auto action = text(request, "action");
        if (action == "invite") {
            const auto role = role_of(request);
            const auto days_it = request.find("days");
            const int days = days_it != request.end() && days_it->is_number_integer()
                                 ? std::clamp(days_it->get<int>(), 1, 30)
                                 : 7;
            const auto admin = unlock(log, request);
            const auto expires = QDateTime::currentDateTimeUtc()
                                     .addDays(days)
                                     .toString(Qt::ISODate)
                                     .toStdString();
            std::optional<sync::ComputerAddress> from;
            if (const auto it = request.find("from"); it != request.end() && it->is_object()) {
                const auto key = crypto::PublicKey::from_base64(it->value("key", std::string()));
                if (!key) {
                    throw Refused("The invite's computer has no key.");
                }
                from = sync::ComputerAddress{it->value("machine", std::string()), *key,
                                           it->value("addresses", std::vector<std::string>{})};
            }
            const auto name = request.value("name", std::string());
            const auto code = log.invite(role, expires, admin, name, from);
            return {{"ok", true},
                    {"code", code},
                    {"invite_id", sync::read_invite(code).invite.id},
                    {"expires", expires}};
        }
        if (action == "withdraw") {
            const auto found = find_invite(list, text(request, "invite"));
            if (!found.invite) {
                throw Refused(found.error);
            }
            log.withdraw_invite(found.invite->id, unlock(log, request));
            return {{"ok", true}, {"invite", found.invite->id}};
        }
        if (action == "role") {
            const auto& target = computer(list, request);
            const auto role = role_of(request);
            // An editor always holds the admin key: only a viewer's is asked.
            std::optional<bool> holds;
            if (const auto it = request.find("holds"); it != request.end() && role == sync::Role::Viewer) {
                if (!it->is_boolean()) {
                    throw Refused("Say whether the computer is to hold the admin key.");
                }
                holds = it->get<bool>();
            }
            // Made a viewer, it holds no admin key until given one.
            const bool holds_after_role = target.role == role && target.holds_admin_key;
            const bool new_role = target.role != role;
            const bool new_holds = holds && *holds != holds_after_role;
            if (!new_role && !new_holds) {
                auto already = target.name + " is " + (role == sync::Role::Viewer ? "a viewer" : "an editor");
                if (holds) {
                    already += *holds ? " holding the admin key" : " without the admin key";
                }
                throw Refused(already + " already.");
            }
            const auto admin = unlock(log, request);
            if (new_role) {
                log.set_role(target.machine, role, admin);
            }
            if (new_holds) {
                log.set_key_holder(target.machine, *holds, admin);
            }
            return {{"ok", true}};
        }
        if (action == "key") {
            const auto& target = computer(list, request);
            const auto holds = request.find("holds");
            if (holds == request.end() || !holds->is_boolean()) {
                throw Refused("Say whether the computer is to hold the admin key.");
            }
            if (target.role != sync::Role::Viewer) {
                throw Refused(target.name + " is an editor, which always holds the admin key.");
            }
            log.set_key_holder(target.machine, holds->get<bool>(), unlock(log, request));
            return {{"ok", true}};
        }
        if (action == "remove") {
            const auto& target = computer(list, request);
            log.remove(target.machine, unlock(log, request));
            return {{"ok", true}, {"removed", target.name}};
        }
        if (action == "limit") {
            const auto it = request.find("bytes");
            if (it == request.end() || !it->is_number_integer() || it->get<std::int64_t>() < 0) {
                throw Refused("A size limit is a size, or none.");
            }
            log.set_size_limit(it->get<std::int64_t>(), unlock(log, request));
            return {{"ok", true}};
        }
        if (action == "password") {
            const auto fresh = text(request, "new_password");
            if (const auto problem = sync::passphrase_problem(fresh)) {
                throw Refused(*problem);
            }
            std::optional<crypto::SigningKey> signer;
            if (request.contains("recovery_code")) {
                signer = sync::key_from_recovery_code(text(request, "recovery_code"));
                if (!signer || !list.recovery || signer->public_key() != *list.recovery) {
                    throw Refused("That is not the library's recovery code.");
                }
            } else {
                signer = unlock(log, request);
            }
            const auto founded = log.replace_admin(fresh, *signer, params);
            json reply{{"ok", true}};
            if (!founded.recovery_code.empty()) {
                reply["recovery_code"] = founded.recovery_code;
            }
            return reply;
        }
        if (action == "recovery") {
            return {{"ok", true}, {"recovery_code", log.replace_recovery(unlock(log, request))}};
        }
        throw Refused("There is no admin action \"" + action + "\".");
    } catch (const std::exception& ex) {
        return {{"ok", false}, {"error", ex.what()}};
    }
}

}  // namespace seventhslide::link
