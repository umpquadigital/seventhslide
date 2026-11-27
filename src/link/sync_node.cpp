#include "seventhslide/link/sync_node.hpp"

#include "seventhslide/link/off_thread.hpp"
#include "seventhslide/link/peer.hpp"
#include "seventhslide/library/library.hpp"
#include "seventhslide/sync/member_list.hpp"
#include "seventhslide/sync/now_utc.hpp"
#include "seventhslide/sync/shared_log.hpp"
#include "seventhslide/sync/sync_store.hpp"
#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/read_file.hpp"

#include <QLoggingCategory>
#include <QNetworkInterface>
#include <QRegularExpression>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace seventhslide::link {
namespace {

Q_LOGGING_CATEGORY(lc_node, "seventhslide.sync", QtInfoMsg)

using nlohmann::json;

constexpr std::string_view k_addresses_filename = "sync_addresses.json";
constexpr std::string_view k_heard_filename = "sync_heard.json";

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

/// How often the disk is looked at.
constexpr int k_disk_check_ms = 10 * 60 * 1000;

/// What the library's media takes on the disk: its files, not the
/// thumbnails or a transfer under way.
std::int64_t media_bytes(const std::filesystem::path& media)
{
    namespace fs = std::filesystem;
    std::int64_t total = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(media, ec); !ec && it != fs::end(it);
         it.increment(ec)) {
        if (it->is_directory(ec) && it->path().filename() == library::k_thumbs_dirname) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && it->path().extension() != ".part") {
            total += static_cast<std::int64_t>(it->file_size(ec));
        }
    }
    return total;
}

}  // namespace

bool disk_nearly_full(std::int64_t free, std::int64_t total)
{
    return free >= 0 && total > 0 && (free < total / 20 || free < 2'000'000'000);
}

std::string address_with_port(const std::string& address, std::uint16_t port)
{
    std::string out;
    if (sync::parse_address(address)) {
        out = address;
    } else if (address.starts_with('[')) {
        out = address + ":" + std::to_string(port);  // an IPv6 address in brackets
    } else if (address.find(':') != std::string::npos) {
        out = "[" + address + "]:" + std::to_string(port);  // an IPv6 address
    } else {
        out = address + ":" + std::to_string(port);
    }
    const auto at = sync::parse_address(out);
    if (!at || std::ranges::any_of(at->host, [](unsigned char c) { return std::isspace(c) != 0; })
        || (out.starts_with('[') && QHostAddress(QString::fromStdString(at->host)).protocol() != QAbstractSocket::IPv6Protocol)) {
        return {};
    }
    return out;
}

bool ipv6_to_give(const QNetworkAddressEntry& entry)
{
    const auto& ip = entry.ip();
    return ip.protocol() == QAbstractSocket::IPv6Protocol && ip.isGlobal() && !ip.isUniqueLocalUnicast()
           && entry.dnsEligibility() != QNetworkAddressEntry::DnsIneligible
           && !(entry.isLifetimeKnown() && entry.preferredLifetime().hasExpired());
}

SyncNode::SyncNode(sync::Identity me, Options options, QObject* parent)
    : QObject(parent), options_(std::move(options)),
      link_(std::make_unique<Link>(std::move(me))),
      turns_(std::make_shared<SyncTurns>(options_.syncs_at_once))
{
    log_pool_.setMaxThreadCount(1);
    link_->set_upload_cap(upload_cap_bytes(options_.upload_cap_kbit));
    link_->set_share_address(options_.share_address);
    // Without a port it may use, a host that cannot go on says so itself
    // (``listening``); one that may, goes on reaching out.
    if (options_.network && !link_->listen(options_.listen, options_.port) && options_.any_port_when_taken
        && !link_->listen(options_.listen, 0)) {
        qCWarning(lc_node).noquote() << "Cannot take connections on port" << options_.port
                                     << ": the others reach this computer only when it reaches them";
    }

    connect(link_.get(), &Link::store_changed, this, [this](const std::string& uid) {
        if (auto* lib = library(uid)) {
            lib->schedule_sync();
        }
        emit changed();
    });
    connect(link_.get(), &Link::files_coming, this, [this](const std::string& uid) {
        if (auto* lib = library(uid)) {
            lib->files_coming();
        }
    });
    connect(link_.get(), &Link::log_stored, this, [this](const std::string& uid) { emit log_changed(uid); });
    connect(link_.get(), &Link::media_stored, this, [this](const std::string& uid, const std::string& path) {
        if (auto* lib = library(uid)) {
            lib->media_arrived(path);
        }
        emit changed();
    });
    connect(link_.get(), &Link::progress, this,
            [this](const std::string& uid, const std::string& member,
                   const std::map<std::string, std::int64_t>& applied) {
                if (auto* lib = library(uid)) {
                    lib->heard_progress(member, applied);
                }
            });
    connect(link_.get(), &Link::peer_ready, this, [this](Peer* peer) {
        qCInfo(lc_node).noquote()
            << QStringLiteral("%1 connected, for %2 library(ies)").arg(q(peer->name())).arg(peer->libraries().size());
        heard(*peer);
        emit changed();
    });
    connect(link_.get(), &Link::peer_closed, this, [this](Peer* peer, const QString& why) {
        if (peer->ready()) {
            heard(*peer);
            qCInfo(lc_node).noquote() << QStringLiteral("%1 disconnected: %2").arg(q(peer->name()), why);
        } else {
            qCInfo(lc_node).noquote() << "A connection closed before it was let in:" << why;
        }
        emit changed();
    });
    connect(link_.get(), &Link::refused, this,
            [](const QString& why) { qCInfo(lc_node).noquote() << "Refused:" << why; });
    connect(link_.get(), &Link::not_there, this, [this](const std::string&) { emit changed(); });

    load_heard();
    // Nothing is heard without announcing, so nothing is waited for.
    auto reach_options = options_.reach_options;
    if (!options_.discovery) {
        reach_options.startup_wait_ms = 0;
    }
    reach_ = std::make_unique<Reach>(*link_, options_.data / k_addresses_filename, reach_options);
    // Announced only where it can be connected to.
    if (options_.network && options_.discovery && listening()) {
        discovery_ = std::make_unique<Discovery>(identity().key.public_key().fingerprint(), port(),
                                                 options_.discovery_options);
        connect(discovery_.get(), &Discovery::heard, reach_.get(), &Reach::heard);
        if (!discovery_->start()) {
            qCWarning(lc_node).noquote() << "This computer will not hear the other SeventhSlides on "
                                            "the network; they reach it by its address.";
        }
    }
    if (options_.network) {
        reach_->start();
    }
    connect(&sync_now_watch_, &QTimer::timeout, this, &SyncNode::watch_sync_now);
    sync_now_watch_.setInterval(250);
    follow_pause();
    for (const auto& bad : unusable_addresses()) {
        qCWarning(lc_node).noquote() << QStringLiteral("\"%1\" is not an address, so it is not said.").arg(q(bad));
    }
    disk_check_.setInterval(k_disk_check_ms);
    connect(&disk_check_, &QTimer::timeout, this, &SyncNode::watch_disk);
    disk_check_.start();
    watch_disk();
}

SyncNode::~SyncNode()
{
    discovery_.reset();
    reach_.reset();
    // Whoever is connected was heard from until now.
    for (const auto* peer : link_->peers()) {
        if (peer->ready()) {
            heard(*peer);
        }
    }
    // Off the link before the libraries whose stores it reads go.
    for (const auto& [uid, held] : held_) {
        link_->remove_library(uid);
    }
    held_.clear();
}

TypedAddresses read_typed_addresses(std::string_view text)
{
    TypedAddresses out;
    const auto s = QString::fromUtf8(text);
    for (const auto& part : s.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
        const auto address = part.toStdString();
        if (address_with_port(address, k_usual_port).empty()) {
            out.not_an_address = address;
            return out;
        }
        out.addresses.push_back(address);
    }
    out.too_many = out.addresses.size() > sync::k_most_addresses;
    return out;
}

AddressesFor read_addresses_for(const std::vector<std::string>& typed, const std::vector<std::string>& its_own)
{
    AddressesFor out;
    for (const auto& t : typed) {
        const auto trimmed = QString::fromStdString(t).trimmed().toStdString();
        if (trimmed.empty()) {
            continue;
        }
        auto address = address_with_port(trimmed, k_usual_port);
        if (address.empty()) {
            out.not_an_address = trimmed;
            return out;
        }
        if (std::ranges::find(its_own, address) != its_own.end()) {
            out.its_own = trimmed;
            return out;
        }
        if (std::ranges::find(out.addresses, address) == out.addresses.end()) {
            out.addresses.push_back(std::move(address));
        }
    }
    out.too_many = out.addresses.size() > sync::k_most_addresses;
    return out;
}

std::vector<std::string> SyncNode::addresses() const
{
    const auto port = std::to_string(this->port());
    const auto set_to = options_.port != 0 ? options_.port : this->port();
    std::vector<std::string> out;
    for (const auto& a : options_.addresses) {
        // Without a port of its own, only one that gives its own.
        auto address = listening() || sync::parse_address(a) ? address_with_port(a, set_to) : std::string();
        if (!address.empty()) {
            out.push_back(std::move(address));
        }
    }
    // Found only when none were given: never in place of what was typed.
    if (options_.addresses.empty() && listening()) {
        // On the church network, at least.
        for (const auto& address : QNetworkInterface::allAddresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()
                && !address.isLinkLocal()) {
                out.push_back(address.toString().toStdString() + ":" + port);
            }
        }
        // Reached from anywhere a firewall lets it, with no port forwarded.
        for (const auto& iface : QNetworkInterface::allInterfaces()) {
            if (!iface.flags().testFlag(QNetworkInterface::IsUp)) {
                continue;
            }
            for (const auto& entry : iface.addressEntries()) {
                if (ipv6_to_give(entry)) {
                    out.push_back("[" + entry.ip().toString().toStdString() + "]:" + port);
                }
            }
        }
    }
    // As many as the member list takes (a computer with many virtual
    // networks has more).
    if (out.size() > sync::k_most_addresses) {
        out.resize(sync::k_most_addresses);
    }
    return out;
}

std::vector<std::string> SyncNode::typed_addresses(const sync::Member& computer) const
{
    auto typed = reach_->typed_addresses(computer.key.fingerprint());
    std::erase_if(typed, [&](const std::string& a) {
        return std::ranges::find(computer.addresses, a) != computer.addresses.end();
    });
    return typed;
}

std::vector<std::string> SyncNode::unusable_addresses() const
{
    std::vector<std::string> out;
    for (const auto& a : options_.addresses) {
        if (address_with_port(a, k_usual_port).empty()) {
            out.push_back(a);
        }
    }
    return out;
}

std::vector<std::string> SyncNode::published_addresses() const
{
    if (options_.addresses.empty() && !options_.publish_found_addresses) {
        return {};
    }
    return addresses();
}

void SyncNode::set_addresses(std::vector<std::string> addresses)
{
    options_.addresses = std::move(addresses);
    for (const auto& uid : library_uids()) {
        follow_self(uid);
    }
}

void SyncNode::set_computer_name(std::string name)
{
    link_->set_computer_name(name);
    for (auto& [uid, held] : held_) {
        held.sync->set_computer_name(name);
    }
    for (const auto& uid : library_uids()) {
        follow_self(uid);
    }
}

void SyncNode::set_computer_picture(sync::Picture picture)
{
    options_.picture = std::move(picture);
    for (const auto& uid : library_uids()) {
        follow_self(uid);
    }
}

void SyncNode::post_message(const std::string& uid, std::string text, std::function<void(std::string)> done)
{
    const auto* lib = library(uid);
    if (lib == nullptr) {
        done("This library is not synced on this computer.");
        return;
    }
    run_off_thread(
        this, log_pool_,
        [store = lib->shared_store(), me = identity(), uid, text = std::move(text)]() -> std::string {
            try {
                sync::LogBook(*store, me, uid).post_message(text);
                return {};
            } catch (const std::invalid_argument& ex) {
                return ex.what();
            } catch (const std::exception& ex) {
                qCWarning(lc_node).noquote() << "A message could not be written:" << ex.what();
                return "The message could not be saved on this computer.";
            }
        },
        [this, uid, done = std::move(done)](std::string error) {
            if (error.empty()) {
                link_->library_changed(uid);
                emit log_changed(uid);
                if (!options_.sync_automatically) {
                    sync_now();
                }
            }
            done(std::move(error));
        });
}

LibrarySync& SyncNode::hold(const std::string& uid, const std::filesystem::path& root)
{
    // Held again: the one before goes first, so one library is never open on
    // two threads.
    let_go(uid);
    LibrarySync::Options options;
    options.box_params = options_.box_params;
    options.watch_edits = options_.watch_edits;
    options.join_wait_ms = options_.join_wait_ms;
    options.turns = turns_;
    auto sync = std::make_unique<LibrarySync>(root, uid, identity(), [this] { return link_->busy(); },
                                              options);
    auto* lib = sync.get();
    lib->set_paused(paused());  // taken on while paused: it waits too
    connect(lib, &LibrarySync::wrote, this, [this, uid] { link_->library_changed(uid); });
    connect(lib, &LibrarySync::logged, this, [this, uid] { emit log_changed(uid); });
    connect(lib, &LibrarySync::reportChanged, this, [this, uid, lib] {
        // Missing after a sync (kept without it, or lost here): asked for
        // now, so it comes, or the others say they have not got it.
        if (!lib->missing_media().empty()) {
            link_->fetch_more(uid);
        }
        // The list may name this computer now, or by another name.
        follow_self(uid);
        emit changed();
    });
    connect(lib, &LibrarySync::synced, this, [uid](int applied, bool snapshot, const QString& problem) {
        if (!problem.isEmpty()) {
            qCWarning(lc_node).noquote()
                << QStringLiteral("Library %1 could not sync: %2").arg(q(uid.substr(0, 8)), problem);
        } else if (applied > 0 || snapshot) {
            qCInfo(lc_node).noquote()
                << QStringLiteral("Library %1: applied %2 change file(s)%3")
                       .arg(q(uid.substr(0, 8)))
                       .arg(applied)
                       .arg(snapshot ? QStringLiteral(" and wrote a snapshot") : QString());
        }
    });
    auto& held = held_[uid];
    held.sync = std::move(sync);
    share(uid);
    follow_self(uid);
    return *lib;
}

void SyncNode::share(const std::string& uid)
{
    auto& held = held_.at(uid);
    auto* lib = held.sync.get();
    link_->add_library({uid, lib->shared_store(), lib->media(), lib->pieces(), [lib] { return lib->missing_media(); },
                        [lib] { return lib->media_room(); },
                        [lib] { return lib->applied_files(); }});
}

void SyncNode::let_go(const std::string& uid)
{
    const auto it = held_.find(uid);
    if (it == held_.end()) {
        return;
    }
    link_->remove_library(uid);
    held_.erase(it);  // waits for its thread
}

LibrarySync* SyncNode::library(const std::string& uid) const
{
    const auto it = held_.find(uid);
    return it == held_.end() ? nullptr : it->second.sync.get();
}

std::vector<std::string> SyncNode::library_uids() const
{
    std::vector<std::string> out;
    out.reserve(held_.size());
    for (const auto& [uid, held] : held_) {
        out.push_back(uid);
    }
    return out;
}

void SyncNode::set_paused(bool paused)
{
    host_paused_ = paused;
    follow_pause();
}

void SyncNode::set_sync_automatically(bool automatically)
{
    options_.sync_automatically = automatically;
    sync_now_ = false;
    sync_now_watch_.stop();
    follow_pause();
    emit changed();
}

void SyncNode::set_upload_cap(std::int64_t kbit)
{
    options_.upload_cap_kbit = kbit;
    link_->set_upload_cap(upload_cap_bytes(kbit));
}

void SyncNode::set_share_address(bool share)
{
    options_.share_address = share;
    link_->set_share_address(share);
}

void SyncNode::follow_pause()
{
    const bool paused = host_paused_ || (!options_.sync_automatically && !sync_now_);
    link_->set_paused(paused);
    for (const auto& [uid, held] : held_) {
        held.sync->set_paused(paused);
    }
}

void SyncNode::watch_sync_now()
{
    // Held back by its host, nothing moves: it has not synced yet.
    if (paused()) {
        sync_now_looking_.restart();
    }
    const bool looking = options_.network && sync_now_looking_.elapsed() < options_.sync_now_reach_ms
                         && !reach_->all_reached();
    if (paused() || busy() || looking || joining()) {
        sync_now_quiet_.restart();
        return;
    }
    if (sync_now_quiet_.elapsed() >= options_.sync_now_quiet_ms) {
        sync_now_ = false;
        sync_now_watch_.stop();
        follow_pause();
        emit changed();
    }
}

bool SyncNode::joining() const
{
    return std::ranges::any_of(held_, [this](const auto& entry) {
        const auto& [uid, held] = entry;
        auto& store = held.sync->store();
        if (sync::read_members(store, uid).founded()) {
            return false;
        }
        const auto invite = sync::own_invite(store, uid, identity().machine);
        return invite && !sync::invite_expired(invite->invite);
    });
}

void SyncNode::sync_now()
{
    if (!options_.sync_automatically) {
        sync_now_ = true;
        sync_now_quiet_.restart();
        sync_now_looking_.restart();
        sync_now_watch_.start();
        follow_pause();
        emit changed();
    }
    for (const auto& [uid, held] : held_) {
        held.sync->sync_now();
    }
    if (options_.network) {
        reach_->try_again_now();
        reach_->check();
    }
}

bool SyncNode::busy() const
{
    return link_->busy()
           || std::ranges::any_of(held_, [](const auto& entry) { return entry.second.sync->busy(); });
}

std::string SyncNode::last_heard(const crypto::PublicKey& key) const
{
    const bool connected = std::ranges::any_of(link_->peers(), [&](const Peer* p) {
        return p->ready() && !p->is_closed() && p->key() == key;
    });
    if (connected) {
        return sync::now_utc();
    }
    const auto it = heard_.find(key.fingerprint());
    return it != heard_.end() ? it->second : std::string();
}

void SyncNode::follow_self(const std::string& uid)
{
    const auto it = held_.find(uid);
    if (it == held_.end()) {
        return;
    }
    auto& held = it->second;
    const auto& me = identity();
    const auto list = sync::read_members(held.sync->store(), uid);
    const auto* self = list.find(me.machine);
    if (self == nullptr || self->removed) {
        return;
    }
    const auto wrote = [this](const QString& what) {
        return [this, what](const json& reply) {
            if (!reply.value("ok", false)) {
                qCWarning(lc_node).noquote() << what << q(reply.value("error", std::string()));
            }
            emit changed();
        };
    };
    // Every computer shows this one by its name on the list: put the name it
    // goes by here there, after a rename, and for a rename made while the
    // library was not here or by a SeventhSlide that did not.
    const bool differs = self->name != me.computer || self->picture != options_.picture;
    const bool tried = held.named_as == me.computer && held.pictured_as == options_.picture;
    if (!me.computer.empty() && differs && !tried) {
        held.named_as = me.computer;
        held.pictured_as = options_.picture;
        held.sync->run(
            [uid, name = me.computer, picture = options_.picture](sync::SyncStore& store, const sync::Identity& who) {
                sync::MemberLog(store, who, uid).rename(name, picture);
                return json{{"ok", true}, {"wrote", true}};
            },
            wrote(QStringLiteral("This computer's new name is not on the list:")));
    }
    // Where it takes connections, once they are not what the list says. None
    // found just now (the network not up yet), or none of those typed being an
    // address, says nothing: those on the list stand. None typed says it takes
    // none.
    if (listening()) {
        auto addresses = published_addresses();
        const bool nothing_to_say = addresses.empty() && (options_.publish_found_addresses || !options_.addresses.empty());
        if (!nothing_to_say && self->addresses != addresses && held.addressed_as != addresses) {
            held.addressed_as = addresses;
            held.sync->run(
                [uid, addresses = std::move(addresses)](sync::SyncStore& store, const sync::Identity& who) {
                    sync::MemberLog(store, who, uid).set_addresses(addresses);
                    return json{{"ok", true}, {"wrote", true}};
                },
                wrote(QStringLiteral("Where this computer takes connections is not on the list:")));
        }
    }
}

void SyncNode::watch_disk()
{
    std::error_code ec;
    const auto space = std::filesystem::space(options_.data, ec);
    if (ec) {
        return;
    }
    const bool full = disk_nearly_full(static_cast<std::int64_t>(space.available),
                                       static_cast<std::int64_t>(space.capacity));
    if (full && !disk_full_) {
        qCWarning(lc_node).noquote()
            << QStringLiteral("The disk %1 is on is nearly full (%2 MB free). Libraries stop "
                              "syncing when it is: free some space, or move the data folder.")
                   .arg(q(options_.data.string()))
                   .arg(space.available / 1'000'000);
    } else if (!full && disk_full_) {
        qCInfo(lc_node).noquote() << "The disk has room again.";
    }
    disk_full_ = full;
}

json SyncNode::status() const
{
    // Who is connected now, by library.
    std::map<std::string, std::vector<crypto::PublicKey>> connected;
    json connections = json::array();
    for (const auto* peer : link_->peers()) {
        if (!peer->ready() || !peer->key()) {
            continue;
        }
        json libs = json::array();
        for (const auto& uid : peer->libraries()) {
            connected[uid].push_back(*peer->key());
            libs.push_back(uid);
        }
        connections.push_back({{"name", peer->name()}, {"machine", peer->machine()}, {"libraries", libs}});
    }

    const auto now = sync::now_utc();
    json libraries = json::array();
    for (const auto& uid : library_uids()) {
        auto* lib = library(uid);
        const auto list = sync::read_members(lib->store(), uid);
        const auto report = lib->report();
        const auto here = connected.find(uid);
        json computers = json::array();
        for (const auto& [machine, m] : list.members) {
            const bool on = here != connected.end()
                            && std::ranges::find(here->second, m.key) != here->second.end();
            // A computer connected is heard from now.
            const auto last = on ? now : last_heard(m.key);
            computers.push_back({{"machine", machine},
                                 {"name", m.name},
                                 {"role", std::string(sync::role_name(m.role))},
                                 {"holds_admin_key", m.role == sync::Role::Editor || m.holds_admin_key},
                                 {"removed", m.removed},
                                 {"this_computer", machine == identity().machine},
                                 {"addresses", m.addresses},
                                 {"typed_addresses", typed_addresses(m)},
                                 {"connected", on},
                                 {"last_heard", last}});
        }
        json invites = json::array();
        for (const auto& made : sync::waiting_invites(list)) {
            const auto* by = list.find(made.made_by);
            invites.push_back({{"id", made.id},
                               {"role", std::string(sync::role_name(made.role))},
                               {"expires", made.expires},
                               {"made_by", by != nullptr ? by->name : made.made_by},
                               {"addresses", reach_->invite_addresses(uid, made.id)}});
        }
        const auto* self = list.find(identity().machine);
        std::int64_t change_files = 0;
        for (const auto& machine : lib->store().machines(sync::Numbered::Changes)) {
            change_files += static_cast<std::int64_t>(
                lib->store().sequences(sync::Numbered::Changes, machine).size());
        }
        json waiting_on = json::array();
        for (const auto& computer : report.waiting_on) {
            waiting_on.push_back({{"machine", computer.machine}, {"name", computer.name}});
        }
        libraries.push_back(
            {{"uid", uid},
             {"founded", list.founded()},
             {"standing", !list.founded()             ? "waiting"
                          : self == nullptr           ? "absent"
                          : self->removed             ? "removed"
                                                      : "member"},
             {"opened", report.opened},
             {"joined", report.joined},
             {"problem", report.problem},
             {"snapshot_problem", report.snapshot_problem},
             {"waiting_on", std::move(waiting_on)},
             {"last_sync", report.last_sync.isValid() ? report.last_sync.toString(Qt::ISODate).toStdString()
                                                      : std::string()},
             {"files_applied", report.files_applied},
             {"snapshots_written", report.snapshots_written},
             {"change_files", change_files},
             {"missing_media", lib->missing_media().size()},
             {"media_coming", link_->media_coming(uid).size()},
             {"media_going", link_->media_going(uid).size()},
             {"media_bytes", media_bytes(lib->media())},
             {"size_limit", list.size_limit},
             {"size_used", report.media.used},
             {"over_limit", report.media.over.size()},
             {"computers", std::move(computers)},
             {"invites", std::move(invites)}});
    }

    // SeventhSlides heard on the network, by name where a library knows them.
    json neighbours = json::array();
    for (const auto& n : reach_->neighbours()) {
        std::string name;
        for (const auto& uid : library_uids()) {
            for (const auto& [machine, m] : sync::read_members(library(uid)->store(), uid).members) {
                if (m.key.fingerprint() == n.fingerprint) {
                    name = m.name;
                }
            }
        }
        neighbours.push_back({{"fingerprint", n.fingerprint.substr(0, 16)},
                              {"name", name},
                              {"address", n.address.toString().toStdString() + ":" + std::to_string(n.port)},
                              {"last_heard", n.last_heard.toString(Qt::ISODate).toStdString()}});
    }
    return {{"libraries", std::move(libraries)},
            {"connections", std::move(connections)},
            {"neighbours", std::move(neighbours)}};
}

void SyncNode::heard(const Peer& peer)
{
    if (!peer.key()) {
        return;
    }
    heard_[peer.key()->fingerprint()] = sync::now_utc();
    save_heard();
}

void SyncNode::load_heard()
{
    const auto text = util::read_file(options_.data / k_heard_filename);
    if (!text) {
        return;
    }
    const auto j = json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        return;
    }
    for (const auto& [fp, when] : j.items()) {
        if (sync::is_fingerprint(fp) && when.is_string()) {
            heard_[fp] = when.get<std::string>();
        }
    }
}

void SyncNode::save_heard() const
{
    if (!util::write_file_atomically(options_.data / k_heard_filename, json(heard_).dump())) {
        qCWarning(lc_node).noquote() << "Could not write" << q((options_.data / k_heard_filename).string());
    }
}

}  // namespace seventhslide::link
