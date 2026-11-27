#include "seventhslide/server/server.hpp"

#include "seventhslide/library/computer_key.hpp"
#include "seventhslide/library/library.hpp"
#include "seventhslide/library/machine_store.hpp"
#include "seventhslide/link/admin.hpp"
#include "seventhslide/link/sync_settings.hpp"
#include "seventhslide/link/tls.hpp"
#include "seventhslide/platform/computer_name.hpp"
#include "seventhslide/server/control.hpp"
#include "seventhslide/server/data_folder.hpp"
#include "seventhslide/server/settings_edit.hpp"
#include "seventhslide/sync/member_list.hpp"
#include "seventhslide/sync/now_utc.hpp"
#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/read_file.hpp"
#include "seventhslide/version.hpp"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QSqlDatabase>
#include <QSslSocket>
#include <QTimer>

#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace seventhslide::server {
namespace {

Q_LOGGING_CATEGORY(lc_server, "seventhslide.server", QtInfoMsg)

using nlohmann::json;
namespace fs = std::filesystem;

constexpr int k_registry_format = 1;
/// The libraries it holds.
constexpr std::string_view k_held_filename = "held.json";

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

/// Why addresses typed for another computer will not do, in words; empty when they will.
std::string addresses_problem(const link::AddressesFor& read)
{
    if (!read.not_an_address.empty()) {
        return "\"" + read.not_an_address + "\" is not an address.";
    }
    if (!read.its_own.empty()) {
        return "\"" + read.its_own + "\" is one it gave itself, so it is reached there anyway.";
    }
    if (read.too_many) {
        return "A computer is given " + std::to_string(sync::k_most_addresses) + " addresses at most.";
    }
    return {};
}

/// The start of a key's fingerprint, in fours, for people to compare.
std::string short_fingerprint(const crypto::PublicKey& key)
{
    const auto fp = key.fingerprint();
    std::string out;
    for (std::size_t i = 0; i < 16; i += 4) {
        out += (out.empty() ? "" : " ") + fp.substr(i, 4);
    }
    return out;
}

json read_json_file(const fs::path& path)
{
    const auto text = util::read_file(path);
    return text ? json::parse(*text, nullptr, /*allow_exceptions=*/false) : json(nullptr);
}

void write_json_file(const fs::path& path, const json& j)
{
    if (!util::write_file_atomically(path, j.dump(2))) {
        qCWarning(lc_server).noquote() << "Could not write" << q(path.string());
    }
}

}  // namespace

LibraryChoice choose_library(const std::vector<std::string>& uids, std::string_view text)
{
    if (text.empty()) {
        return {{}, "Name the library."};
    }
    std::vector<std::string> matches;
    for (const auto& uid : uids) {
        if (uid == text) {
            return {uid, {}};
        }
        if (uid.starts_with(text)) {
            matches.push_back(uid);
        }
    }
    if (matches.size() == 1) {
        return {matches.front(), {}};
    }
    if (matches.empty()) {
        return {{}, "This server holds no library \"" + std::string(text) + "\". Status lists them."};
    }
    return {{}, "More than one library starts \"" + std::string(text) + "\". Give more of it."};
}

std::string lacking_for_server(bool sqlite, bool tls, const std::vector<std::string>& plugin_folders)
{
    if (sqlite && tls) {
        return {};
    }
    std::string folders;
    for (const auto& folder : plugin_folders) {
        folders += (folders.empty() ? "" : ", ") + folder;
    }
    return std::string("This SeventhSlide cannot run as a server: Qt has ")
           + (sqlite ? "no TLS (the tls plugin, and OpenSSL)"
              : tls  ? "no SQLite driver (the sqldrivers plugin)"
                     : "no SQLite driver (the sqldrivers plugin) and no TLS (the tls plugin, and OpenSSL)")
           + ". It looked for plugins in: " + (folders.empty() ? "nowhere" : folders) + ".";
}

Server::Server(ServerOptions options, QObject* parent)
    : QObject(parent), options_(std::move(options)),
      lock_(q((options_.data / "server.lock").string())),
      started_(QDateTime::currentDateTimeUtc())
{
    std::vector<std::string> plugin_folders;
    for (const auto& folder : QCoreApplication::libraryPaths()) {
        plugin_folders.push_back(folder.toStdString());
    }
    if (const auto lacking = lacking_for_server(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")),
                                                link::use_openssl_tls() && QSslSocket::supportsSsl(),
                                                plugin_folders);
        !lacking.empty()) {
        throw std::runtime_error(lacking);
    }
    const auto shown = options_.data.string();
    prepare_data_folder(options_.data);
    lock_.setStaleLockTime(0);
    if (!lock_.tryLock(0)) {
        qint64 pid = 0;
        QString host;
        QString app;
        lock_.getLockInfo(&pid, &host, &app);
        throw std::runtime_error("Another SeventhSlide server is running with the data folder "
                                 + shown
                                 + (pid > 0 ? " (process " + std::to_string(pid) + ")" : "")
                                 + ".");
    }

    library::MachineStore machine(options_.data);
    machine.load();
    if (machine.machine_id().empty()) {
        throw std::runtime_error("The server's id could not be written in " + shown);
    }
    // The same settings as a window's Sync page; a flag overrides one for this
    // run, and is not written back.
    const auto settings = link::SyncSettings::load(options_.data);
    auto name = !options_.name.empty() ? options_.name : settings.computer_name;
    if (name.empty()) {
        name = platform::computer_name();
    }
    if (name.empty()) {
        name = "SeventhSlide server";
    }
    const auto wanted_port = options_.port ? *options_.port : settings.port.value_or(link::k_usual_port);
    link::SyncNode::Options node;
    node.data = options_.data;
    node.listen = options_.listen;
    node.port = wanted_port;
    node.addresses = !options_.addresses.empty() ? options_.addresses : settings.addresses;
    // Its addresses stay: on the list, the others reach it there, and every
    // invite names it. With none given, those it has here.
    node.publish_found_addresses = true;
    node.sync_automatically = settings.sync_automatically;
    node.upload_cap_kbit = settings.upload_cap_kbit;
    node.share_address = settings.share_address;
    node.discovery = options_.discovery && settings.discovery;
    node.discovery_options = options_.discovery_options;
    node.reach_options = options_.reach_options;
    node.syncs_at_once = options_.syncs_at_once;
    node.box_params = options_.box_params;
    node_ = std::make_unique<link::SyncNode>(
        sync::Identity{machine.machine_id(), name, library::load_computer_key(options_.data)}, node);
    if (!node_->listening()) {
        throw std::runtime_error("The server cannot listen on port " + std::to_string(wanted_port)
                                 + ". Another program may be using it: choose another with "
                                   "--port.");
    }
    if (const auto bad = node_->unusable_addresses(); !bad.empty()) {
        throw std::runtime_error("\"" + bad.front() + "\" is not an address computers can reach: give "
                                 "--address as a name or number, with :port after it if "
                                 "the router forwards another port.");
    }
    load_registry();
    for (const auto& [uid, since] : held_) {
        hold(uid);
    }
    if (options_.control) {
        control_ = std::make_unique<Control>(*this);
        control_->listen(options_.data);
    }
    qCInfo(lc_server).noquote()
        << QStringLiteral("SeventhSlide %1 server \"%2\" (key %3) listening on port %4, holding %5 "
                          "library(ies), data in %6")
               .arg(QString::fromUtf8(seventhslide::version_string()), q(identity().computer),
                    q(short_fingerprint(identity().key.public_key())))
               .arg(port())
               .arg(held_.size())
               .arg(q(shown));
}

Server::~Server() = default;

std::vector<std::string> Server::addresses() const
{
    auto out = node_->addresses();
    if (out.empty()) {
        out.push_back("127.0.0.1:" + std::to_string(port()));
    }
    return out;
}

void Server::join(const std::string& code, std::function<void(json)> done)
{
    sync::SignedInvite invite;
    try {
        invite = sync::read_invite(code);
    } catch (const std::exception& ex) {
        done({{"ok", false}, {"error", ex.what()}});
        return;
    }
    const auto uid = invite.invite.library;
    if (!sync::is_library_uid(uid)) {
        done({{"ok", false}, {"error", "This invite is not for a library."}});
        return;
    }
    if (library(uid) != nullptr) {
        done({{"ok", false}, {"error", "This server holds that library already."}});
        return;
    }
    const auto root = options_.data / library::k_libraries_dirname / uid;
    std::error_code ec;
    const bool was_there = fs::exists(root, ec);
    held_[uid] = sync::now_utc();
    save_registry();
    hold(uid);
    library(uid)->run(
        [uid, code](sync::SyncStore& store, const sync::Identity& me) {
            sync::MemberLog(store, me, uid).join(sync::read_invite(code));
            return json{{"ok", true}, {"wrote", true}};
        },
        [this, uid, root, was_there, done = std::move(done)](json reply) {
            // On a turn of its own: on failure it lets go of the library whose
            // reply this is.
            QTimer::singleShot(0, this, [this, uid, root, was_there, done, reply = std::move(reply)]() mutable {
                if (!reply.value("ok", false)) {
                    node_->let_go(uid);
                    held_.erase(uid);
                    save_registry();
                    if (!was_there) {
                        std::error_code ignored;
                        fs::remove_all(root, ignored);
                    }
                    done(std::move(reply));
                    return;
                }
                qCInfo(lc_server).noquote() << "Joining library" << q(uid);
                reply["library"] = uid;
                if (!node_->sync_automatically()) {
                    node_->sync_now();
                }
                reach().check();
                done(std::move(reply));
            });
        });
}

void Server::sync_now()
{
    node_->sync_now();
}

void Server::hold(const std::string& uid)
{
    if (library(uid) == nullptr) {
        (void)node_->hold(uid, options_.data / library::k_libraries_dirname / uid);
    }
}

std::vector<std::string> Server::library_uids() const
{
    return node_->library_uids();
}

link::LibrarySync* Server::library(const std::string& uid) const
{
    return node_->library(uid);
}

void Server::admin(const std::string& uid, json request, std::function<void(json)> done)
{
    auto* lib = library(uid);
    if (lib == nullptr) {
        done({{"ok", false}, {"error", "This server holds no library " + uid + "."}});
        return;
    }
    const auto action = request.value("action", std::string());
    std::vector<std::string> joining_at;
    if (action == "invite") {
        // The computer joining may find no other, or may be reached out to
        // from here.
        const auto& me = identity();
        request["from"] = {{"machine", me.machine}, {"key", me.key.public_key().base64()}, {"addresses", addresses()}};
        if (const auto it = request.find("joining_at"); it != request.end()) {
            if (!it->is_array() || !std::ranges::all_of(*it, [](const json& a) { return a.is_string(); })) {
                done({{"ok", false}, {"error", "Where the computer joining is: a list of addresses."}});
                return;
            }
            const auto read = link::read_addresses_for(it->get<std::vector<std::string>>());
            if (const auto problem = addresses_problem(read); !problem.empty()) {
                done({{"ok", false}, {"error", problem}});
                return;
            }
            joining_at = read.addresses;
        }
    }
    lib->admin(std::move(request), [this, uid, action, joining_at, done = std::move(done)](json reply) {
        if (reply.value("ok", false) && !joining_at.empty()) {
            reach().set_invite_addresses(uid, reply.value("invite_id", std::string()),
                                         reply.value("expires", std::string()), joining_at);
        }
        if (reply.value("ok", false)) {
            qCInfo(lc_server).noquote()
                << QStringLiteral("Admin work on library %1: %2").arg(q(uid.substr(0, 8)), q(action));
        } else {
            qCInfo(lc_server).noquote()
                << QStringLiteral("Admin work on library %1: %2, not done: %3")
                       .arg(q(uid.substr(0, 8)), q(action), q(reply.value("error", std::string())));
        }
        done(std::move(reply));
    });
}

json Server::reach_at(const std::string& uid, const std::string& who,
                      const std::optional<std::vector<std::string>>& addresses)
{
    auto* lib = library(uid);
    if (lib == nullptr) {
        return {{"ok", false}, {"error", "This server holds no library " + uid + "."}};
    }
    const auto list = sync::read_members(lib->store(), uid);
    const auto found = link::find_computer(list, who);
    if (found.member == nullptr) {
        if (who.empty() || found.ambiguous) {
            return {{"ok", false}, {"error", found.error}};
        }
        // Not a computer: an invite waiting for one, then.
        const auto invite = link::find_invite(list, who);
        if (!invite.invite) {
            return {{"ok", false},
                    {"error", invite.ambiguous ? invite.error
                                               : "Neither a computer nor an invite waiting in this library goes by \""
                                                     + who + "\". Status lists them."}};
        }
        if (addresses) {
            if (sync::invite_expired(*invite.invite)) {
                return {{"ok", false}, {"error", "That invite has run out."}};
            }
            const auto read = link::read_addresses_for(*addresses);
            if (const auto problem = addresses_problem(read); !problem.empty()) {
                return {{"ok", false}, {"error", problem}};
            }
            reach().set_invite_addresses(uid, invite.invite->id, invite.invite->expires, read.addresses);
        }
        return {{"ok", true},
                {"invite", invite.invite->id},
                {"addresses", reach().invite_addresses(uid, invite.invite->id)}};
    }
    if (found.member->machine == identity().machine) {
        return {{"ok", false}, {"error", "That is this server. Give it where it is with --address."}};
    }
    const auto fingerprint = found.member->key.fingerprint();
    if (addresses) {
        const auto read = link::read_addresses_for(*addresses, found.member->addresses);
        if (const auto problem = addresses_problem(read); !problem.empty()) {
            return {{"ok", false}, {"error", problem}};
        }
        reach().set_typed_addresses(fingerprint, read.addresses);
    }
    return {{"ok", true}, {"name", found.member->name}, {"addresses", node_->typed_addresses(*found.member)}};
}

json Server::leave(const std::string& uid)
{
    auto* lib = library(uid);
    if (lib == nullptr) {
        return {{"ok", false}, {"error", "This server holds no library " + uid + "."}};
    }
    const auto list = sync::read_members(lib->store(), uid);
    const auto* self = list.find(identity().machine);
    const bool still_listed = self != nullptr && !self->removed;
    const auto root = lib->root();
    node_->let_go(uid);  // waits for its thread
    held_.erase(uid);
    save_registry();

    const auto left = options_.data / "left";
    auto kept = left / (uid + "-" + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")).toStdString());
    std::error_code ec;
    fs::create_directories(left, ec);
    fs::rename(root, kept, ec);
    if (ec) {
        kept = root;  // not held, whatever is in it: no longer in held.json
    }
    qCInfo(lc_server).noquote()
        << QStringLiteral("Left library %1; its copy is in %2").arg(q(uid), q(kept.string()));
    return {{"ok", true}, {"kept_at", kept.string()}, {"still_listed", still_listed}};
}

bool Server::busy() const
{
    return node_->busy();
}

json Server::status() const
{
    std::error_code ec;
    const auto space = fs::space(options_.data, ec);
    const auto& me = identity();
    json addresses = json::array();
    for (const auto& a : this->addresses()) {
        addresses.push_back(a);
    }
    auto out = node_->status();
    out["ok"] = true;
    out["server"] = {{"name", me.computer},
                     {"machine", me.machine},
                     {"fingerprint", short_fingerprint(me.key.public_key())},
                     {"version", seventhslide::version_string()},
                     {"port", port()},
                     {"addresses", std::move(addresses)},
                     {"data", options_.data.string()},
                     {"started", started_.toString(Qt::ISODate).toStdString()},
                     {"sync_automatically", node_->sync_automatically()},
                     {"disk_free", ec ? std::int64_t{-1} : static_cast<std::int64_t>(space.available)},
                     {"disk_total", ec ? std::int64_t{-1} : static_cast<std::int64_t>(space.capacity)}};
    return out;
}

json Server::settings() const
{
    const auto saved = link::SyncSettings::load(options_.data);
    const bool discovering = node_->discovery() != nullptr;
    json flags = json::array();
    json after_restart = json::array();
    if (!options_.name.empty()) {
        flags.push_back("computer_name");
    }
    if (!options_.addresses.empty()) {
        flags.push_back("addresses");
    }
    if (options_.port) {
        flags.push_back("port");
    } else if (saved.port.value_or(link::k_usual_port) != port()) {
        after_restart.push_back("port");
    }
    if (!options_.discovery) {
        flags.push_back("discovery");
    } else if (saved.discovery != discovering) {
        after_restart.push_back("discovery");
    }
    json now_addresses = json::array();
    for (const auto& a : addresses()) {
        now_addresses.push_back(a);
    }
    return {{"ok", true},
            {"running", true},
            {"settings", settings_json(saved)},
            {"now",
             {{"computer_name", identity().computer},
              {"addresses", std::move(now_addresses)},
              {"port", port()},
              {"discovery", discovering},
              {"sync_automatically", node_->sync_automatically()},
              {"upload_cap_kbit", node_->upload_cap()},
              {"share_address", node_->share_address()}}},
            {"flags", std::move(flags)},
            {"after_restart", std::move(after_restart)}};
}

json Server::set_settings(const json& changes)
{
    auto saved = link::SyncSettings::load(options_.data);
    if (auto why = apply_settings_changes(saved, changes); !why.empty()) {
        return {{"ok", false}, {"error", std::move(why)}};
    }
    if (!saved.save(options_.data)) {
        return {{"ok", false}, {"error", "This computer's settings could not be saved."}};
    }
    if (changes.contains("computer_name") && options_.name.empty()) {
        node_->set_computer_name(saved.computer_name);
    }
    if (changes.contains("addresses") && options_.addresses.empty()) {
        node_->set_addresses(saved.addresses);
    }
    if (changes.contains("sync_automatically")) {
        node_->set_sync_automatically(saved.sync_automatically);
    }
    if (changes.contains("upload_cap_kbit")) {
        node_->set_upload_cap(saved.upload_cap_kbit);
    }
    if (changes.contains("share_address")) {
        node_->set_share_address(saved.share_address);
    }
    qCInfo(lc_server).noquote() << "Settings changed:" << q(changes.dump());
    return settings();
}

void Server::load_registry()
{
    const auto j = read_json_file(options_.data / k_held_filename);
    if (!j.is_object() || !j.contains("libraries") || !j["libraries"].is_array()) {
        return;
    }
    for (const auto& entry : j["libraries"]) {
        if (!entry.is_object() || !entry.contains("uid") || !entry["uid"].is_string()
            || !sync::is_library_uid(entry["uid"].get<std::string>())) {
            continue;
        }
        auto& since = held_[entry["uid"].get<std::string>()];
        if (const auto when = entry.find("since"); when != entry.end() && when->is_string()) {
            since = when->get<std::string>();
        }
    }
}

void Server::save_registry() const
{
    json libraries = json::array();
    for (const auto& [uid, since] : held_) {
        libraries.push_back({{"uid", uid}, {"since", since}});
    }
    write_json_file(options_.data / k_held_filename,
                    {{"format", k_registry_format}, {"libraries", std::move(libraries)}});
}

}  // namespace seventhslide::server
