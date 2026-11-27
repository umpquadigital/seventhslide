#include "seventhslide/server/settings_edit.hpp"

#include "seventhslide/link/sync_node.hpp"
#include "seventhslide/server/control.hpp"
#include "seventhslide/server/data_folder.hpp"
#include "seventhslide/sync/member_list.hpp"
#include "seventhslide/util/strings.hpp"

#include <QLockFile>
#include <QString>

#include <stdexcept>
#include <vector>

namespace seventhslide::server {
namespace {

using nlohmann::json;

json failure(const std::string& why)
{
    return {{"ok", false}, {"error", why}};
}

json stopped_reply(const link::SyncSettings& settings)
{
    return {{"ok", true},
            {"running", false},
            {"settings", settings_json(settings)},
            {"flags", json::array()},
            {"after_restart", json::array()}};
}

/// None runs here: holding its lock, so none starts meanwhile.
json change_directly(const std::filesystem::path& data, const json& changes)
{
    prepare_data_folder(data);
    QLockFile lock(QString::fromStdString((data / "server.lock").string()));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        return failure("A SeventhSlide server has this data folder and is not answering, so its settings "
                       "were left alone. Its log may say why.");
    }
    auto settings = link::SyncSettings::load(data);
    if (auto why = apply_settings_changes(settings, changes); !why.empty()) {
        return failure(why);
    }
    if (!settings.save(data)) {
        return failure("This computer's settings could not be saved.");
    }
    return stopped_reply(settings);
}

}  // namespace

std::string upload_cap_problem()
{
    return "An upload speed cap is in megabits a second, from 0.1 to 10000, or none.";
}

json settings_json(const link::SyncSettings& settings)
{
    return {{"computer_name", settings.computer_name},
            {"addresses", settings.addresses},
            {"port", settings.port ? json(*settings.port) : json(nullptr)},
            {"discovery", settings.discovery},
            {"sync_automatically", settings.sync_automatically},
            {"upload_cap_kbit", settings.upload_cap_kbit},
            {"share_address", settings.share_address}};
}

std::string apply_settings_changes(link::SyncSettings& settings, const json& changes)
{
    if (!changes.is_object()) {
        return "Say which settings to change.";
    }
    auto out = settings;
    for (const auto& [name, value] : changes.items()) {
        if (name == "computer_name") {
            const auto trimmed = value.is_string() ? util::trim(value.get<std::string>()) : std::string();
            if (trimmed.empty()) {
                return "Give this computer a name the others will know it by.";
            }
            out.computer_name = trimmed;
        } else if (name == "addresses") {
            if (!value.is_array()) {
                return "Give the addresses as a list.";
            }
            std::vector<std::string> addresses;
            for (const auto& a : value) {
                if (!a.is_string() || link::address_with_port(a.get<std::string>(), link::k_usual_port).empty()) {
                    return "\"" + (a.is_string() ? a.get<std::string>() : a.dump()) + "\" is not an address.";
                }
                addresses.push_back(a.get<std::string>());
            }
            if (addresses.size() > sync::k_most_addresses) {
                return "A computer gives " + std::to_string(sync::k_most_addresses) + " addresses at most.";
            }
            out.addresses = std::move(addresses);
        } else if (name == "port") {
            if (value.is_null()) {
                out.port.reset();
            } else if (value.is_number_integer() && value.get<std::int64_t>() >= 1 && value.get<std::int64_t>() <= 65535) {
                out.port = static_cast<std::uint16_t>(value.get<std::int64_t>());
            } else {
                return "A port is a whole number from 1 to 65535.";
            }
        } else if (name == "discovery" || name == "sync_automatically" || name == "share_address") {
            if (!value.is_boolean()) {
                return "Say on or off for " + name + ".";
            }
            (name == "discovery"            ? out.discovery
             : name == "sync_automatically" ? out.sync_automatically
                                            : out.share_address) = value.get<bool>();
        } else if (name == "upload_cap_kbit") {
            if (!value.is_number_integer()
                || (value.get<std::int64_t>() != 0
                    && (value.get<std::int64_t>() < link::k_least_upload_cap_kbit
                        || value.get<std::int64_t>() > link::k_most_upload_cap_kbit))) {
                return upload_cap_problem();
            }
            out.upload_cap_kbit = value.get<std::int64_t>();
        } else {
            return "There is no setting \"" + name + "\".";
        }
    }
    settings = std::move(out);
    return {};
}

json ask_settings(const std::filesystem::path& data, const AskServer& ask, const std::optional<json>& changes)
{
    json request{{"command", "settings"}};
    if (changes) {
        request["set"] = *changes;
    }
    try {
        return ask(request, 10000);
    } catch (const NoServer&) {
        try {
            return changes ? change_directly(data, *changes) : stopped_reply(link::SyncSettings::load(data));
        } catch (const std::runtime_error& ex) {
            return failure(ex.what());
        }
    }
}

}  // namespace seventhslide::server
