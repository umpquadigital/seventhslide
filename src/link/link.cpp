#include "seventhslide/link/link.hpp"

#include "seventhslide/link/off_thread.hpp"
#include "seventhslide/link/reach.hpp"
#include "seventhslide/link/tls.hpp"
#include "seventhslide/sync/media_pieces.hpp"
#include "seventhslide/sync/member_list.hpp"

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace seventhslide::link {
namespace {

/// A file moved to another connection is not moved again for this long.
constexpr auto k_moved_lately = std::chrono::minutes(1);
/// A connection not measured yet is given a file to be measured by only from
/// one that would take this long over it.
constexpr double k_worth_measuring_seconds = 5;

/// ``address``, an IPv4 one seen as IPv6 (``::ffff:``) as IPv4, as a listener
/// taking both shows it.
QHostAddress plain_address(const QHostAddress& address)
{
    bool v4 = false;
    const auto ipv4 = address.toIPv4Address(&v4);
    return v4 ? QHostAddress(ipv4) : address;
}

/// ``address`` with ``port``, as the member list writes one ("host:port",
/// "[v6]:port").
std::string address_and_port(const QHostAddress& address, std::uint16_t port)
{
    auto plain = plain_address(address);
    plain.setScopeId({});
    const auto host = plain.toString().toStdString();
    return (plain.protocol() == QAbstractSocket::IPv4Protocol ? host : "[" + host + "]") + ":" + std::to_string(port);
}

}  // namespace

bool same_source(const QHostAddress& a, const QHostAddress& b)
{
    const auto plain = [](const QHostAddress& address) {
        bool v4 = false;
        const QHostAddress mapped(address.toIPv4Address(&v4));
        return v4 ? mapped : address;
    };
    const auto x = plain(a);
    const auto y = plain(b);
    if (x.protocol() != y.protocol()) {
        return false;
    }
    return x.protocol() == QAbstractSocket::IPv6Protocol ? x.isInSubnet(y, 64) : x == y;
}

Link::Link(sync::Identity me, QObject* parent)
    : QObject(parent), me_(std::move(me)), tls_(tls_configuration(me_))
{
    // A file at a time on each (``PeerShared``).
    shared_.checks.setMaxThreadCount(1);
    shared_.pieces.setMaxThreadCount(1);
    shared_.files.setMaxThreadCount(1);
    shared_.part_free = [this](const std::string& uid) {
        for (auto* peer : peers_) {
            peer->fetch_more(uid);
        }
    };
    shared_.stored = [this](const std::string& uid) { stored(uid); };
    // When accept() fails other than for a moment (out of open files:
    // EMFILE), QTcpServer pauses accepting and never starts again by itself,
    // so this computer would take no connection until it restarts. Try
    // again in a second.
    connect(&server_, &QTcpServer::acceptError, this, [this](QAbstractSocket::SocketError) {
        QTimer::singleShot(1000, &server_, [this] { server_.resumeAccepting(); });
    });
    tell_coming_.setInterval(1000);
    connect(&tell_coming_, &QTimer::timeout, this, [this] {
        for (const auto& lib : libraries_) {
            // Each is told what is on its way here from the others. What is
            // coming only from it is not: told so, it would read the file as
            // coming through this computer, when this one is asking it (and
            // it may have said it has not got it). Then nobody_has and
            // coming_through, there, would be wrong.
            std::vector<std::pair<const Peer*, std::set<std::string>>> from;
            for (const auto* peer : peers_) {
                if (peer->ready()) {
                    from.emplace_back(peer, peer->media_coming(lib.uid));
                }
            }
            for (auto* peer : peers_) {
                std::set<std::string> paths;
                for (const auto& [other, coming] : from) {
                    if (other != peer) {
                        paths.insert(coming.begin(), coming.end());
                    }
                }
                peer->tell_coming(lib.uid, paths);
            }
        }
    });
    tell_coming_.start();
    shared_.better_for = [this](const Peer& asking, const std::string& uid, const db::MediaDigest& file) {
        return better_for(asking, uid, file);
    };
    routes_.setInterval(1000);
    connect(&routes_, &QTimer::timeout, this, &Link::rebalance);
    measured_.start();
    routes_.start();
    server_.arrived = [this](qintptr descriptor) {
        auto* socket = new QSslSocket;
        if (!socket->setSocketDescriptor(descriptor)) {
            // Not the socket's, so not closed by it.
#ifdef Q_OS_WIN
            ::closesocket(static_cast<SOCKET>(descriptor));
#else
            ::close(static_cast<int>(descriptor));
#endif
            socket->deleteLater();
            return;
        }
        if (!make_room_for(socket->peerAddress())) {
            socket->abort();
            socket->deleteLater();
            return;
        }
        socket->setSslConfiguration(tls_);
        adopt(socket, std::nullopt, {}, false);
        socket->startServerEncryption();
    };
}

void Link::add_library(SyncedLibrary library)
{
    remove_library(library.uid);
    const auto uid = library.uid;
    if (!library.vouches) {
        library.vouches = std::make_shared<sync::Vouches>(*library.store);
    }
    libraries_.push_back(std::move(library));
    for (auto* peer : peers_) {
        peer->library_added(uid);
    }
    refresh_vouches(uid);
}

void Link::refresh_vouches(const std::string& uid)
{
    const auto lib = std::ranges::find(libraries_, uid, &SyncedLibrary::uid);
    if (lib == libraries_.end()) {
        return;
    }
    if (const auto running = refreshing_.find(uid); running != refreshing_.end()) {
        running->second = true;  // once more when this one is done
        return;
    }
    refreshing_[uid] = false;
    run_off_thread(
        this, shared_.files,
        // ``store`` is held for as long as ``vouches`` reads it.
        [store = lib->store, vouches = lib->vouches] {
            vouches->refresh();
            return true;
        },
        [this, uid](bool) {
            const bool again = refreshing_[uid];
            refreshing_.erase(uid);
            for (auto* peer : peers_) {
                peer->fetch_more(uid);
            }
            if (again) {
                refresh_vouches(uid);
            }
        });
}

void Link::remove_library(const std::string& uid)
{
    for (auto* peer : peers_) {
        peer->library_removed(uid);
    }
    std::erase_if(libraries_, [&](const SyncedLibrary& lib) { return lib.uid == uid; });
    shared_.owed_media.erase(uid);
    std::erase_if(shared_.said_too_large, [&](const auto& file) { return std::get<1>(file) == uid; });
}

bool Link::listen(const QHostAddress& address, quint16 port)
{
    const bool listening = server_.listen(address, port);
    shared_.listening_port = server_.serverPort();
    // Those connected already hear where it is now.
    if (shared_.share_address) {
        for (auto* peer : peers_) {
            peer->share_address_changed();
        }
    }
    return listening;
}

quint16 Link::port() const
{
    return server_.serverPort();
}

Peer* Link::connect_to(const QString& host, quint16 port, std::optional<crypto::PublicKey> expected,
                       std::vector<Joining> joining)
{
    if (expected) {
        if (auto* peer = peer_with(*expected)) {
            for (auto& j : joining) {
                peer->join(std::move(j));
            }
            return peer;
        }
    }
    return open_to(host, port, std::move(expected), std::move(joining), false);
}

Peer* Link::open_to(const QString& host, quint16 port, std::optional<crypto::PublicKey> expected,
                    std::vector<Joining> joining, bool replaces)
{
    auto* socket = new QSslSocket;
    socket->setSslConfiguration(tls_);
    Peer* peer = adopt(socket, std::move(expected), std::move(joining), true);
    if (replaces) {
        peer->set_replaces();
    }
    socket->connectToHostEncrypted(host, port);
    return peer;
}

Peer* Link::connect_instead(const QString& host, quint16 port, const crypto::PublicKey& key)
{
    return open_to(host, port, key, {}, true);
}

Peer* Link::peer_with(const crypto::PublicKey& key) const
{
    Peer* on_its_way = nullptr;
    for (auto* peer : peers_) {
        if (peer->is_closed() || !(peer->key() == key || (!peer->key() && peer->expected() == key))) {
            continue;
        }
        if (peer->ready()) {
            return peer;
        }
        if (on_its_way == nullptr) {
            on_its_way = peer;
        }
    }
    return on_its_way;
}

bool Link::make_room_for(const QHostAddress& address)
{
    Peer* longest = nullptr;  // peers_ is in the order they came
    int waiting = 0;
    int from_there = 0;
    for (auto* peer : peers_) {
        if (peer->is_closed() || peer->outgoing() || peer->admitted()) {
            continue;
        }
        if (longest == nullptr) {
            longest = peer;
        }
        ++waiting;
        if (same_source(peer->address(), address)) {
            ++from_there;
        }
    }
    if (from_there >= k_max_unadmitted_per_address) {
        return false;
    }
    if (waiting >= k_max_unadmitted) {
        longest->close(QStringLiteral("Too many connections waiting: the longest gave way"));
    }
    return true;
}

Peer* Link::adopt(QSslSocket* socket, std::optional<crypto::PublicKey> expected,
                  std::vector<Joining> joining, bool outgoing)
{
    auto* peer = new Peer(socket, me_, libraries_, shared_, std::move(expected),
                          std::move(joining), outgoing, this);
    peers_.push_back(peer);
    connect(peer, &Peer::stored, this, &Link::stored);
    connect(peer, &Peer::logged, this, [this](const std::string& uid) {
        // As for any file: every peer hears what we have now.
        for (auto* each : peers_) {
            each->library_changed(uid);
        }
        emit log_stored(uid);
    });
    connect(peer, &Peer::let_go, this, [this, peer](const std::string& uid) {
        for (auto* other : peers_) {
            if (other != peer) {
                other->fetch_more(uid);
            }
        }
        emit not_there(uid);
    });
    connect(peer, &Peer::media_done, this, [this, peer](const std::string& uid, const std::string& path) {
        // First, so that what the library says is missing no longer has it
        // when the others look.
        emit media_stored(uid, path);
        for (auto* other : peers_) {
            if (other != peer) {
                other->media_arrived(uid, path);
            }
        }
    });
    connect(peer, &Peer::became_ready, this, [this, peer] {
        keep_one(peer);
        if (!peer->is_closed()) {
            emit peer_ready(peer);
        }
    });
    connect(peer, &Peer::introductions_asked, this, &Link::introduce);
    connect(peer, &Peer::introduced, this,
            [this, peer](const std::string& uid, const std::string& fingerprint, const std::string& address) {
                if (peer->key()) {
                    emit introduced(uid, peer->key()->fingerprint(), fingerprint, address);
                }
            });
    connect(peer, &Peer::refused, this, &Link::refused);
    connect(peer, &Peer::progress, this, &Link::progress);
    connect(peer, &Peer::files_coming, this, &Link::files_coming);
    connect(peer, &Peer::part_stored, this, [this, peer](const std::string& uid, const std::string& sha256) {
        for (auto* other : peers_) {
            if (other != peer) {
                other->part_arrived(uid, sha256);
            }
        }
    });
    connect(peer, &Peer::closed, this, [this, peer](const QString& why) {
        std::erase(peers_, peer);
        // Back by another connection, it is introduced to the others again.
        if (peer->key()) {
            for (auto* other : peers_) {
                other->forget_introduced(*peer->key());
            }
        }
        // What it was fetching, another may fetch.
        for (const auto& uid : shared_.fetches.release_all(peer)) {
            for (auto* other : peers_) {
                other->fetch_more(uid);
            }
        }
        peer->deleteLater();
        emit peer_closed(peer, why);
    });
    return peer;
}

void Link::stored(const std::string& uid)
{
    // Every peer hears what we have now: the others, to fetch it from us;
    // the one it came from, to know it came through (``Peer::their_last``).
    for (auto* each : peers_) {
        each->library_changed(uid);
    }
    // The member list may name another of those connected now.
    introduce(uid);
    emit store_changed(uid);
}

void Link::keep_one(Peer* fresh)
{
    const auto& fresh_key = fresh->key();
    if (!fresh_key) {
        return;  // not ready: no key to be the same as another's
    }
    for (auto* other : peers_) {
        if (other == fresh || other->is_closed() || !other->ready() || other->key() != fresh_key) {
            continue;
        }
        Peer* keep = fresh;  // it replaces the other, or two made the same way: the newer
        const bool takes_over = fresh->replaces() && !other->replaces();
        if (!takes_over && fresh->outgoing() != other->outgoing()) {
            const bool lower_here = me_.key.public_key().fingerprint() < fresh_key->fingerprint();
            keep = fresh->outgoing() == lower_here ? fresh : other;
        }
        Peer* drop = keep == fresh ? other : fresh;
        for (const auto& j : drop->joining()) {
            keep->join(j);
        }
        drop->close(QStringLiteral("Connected to that computer already"));
        return;
    }
}

void Link::library_changed(const std::string& uid)
{
    for (auto* peer : peers_) {
        peer->library_changed(uid);
    }
    // The engine's own change files and snapshots vouch too.
    refresh_vouches(uid);
}

void Link::wait_for_writes()
{
    shared_.pieces.waitForDone();
    shared_.checks.waitForDone();
    // What a closing connection held is written there.
    shared_.files.waitForDone();
}

bool Link::busy() const
{
    return !shared_.parts_at_work.empty() || shared_.storing_held > 0
           || std::ranges::any_of(peers_, [](const Peer* p) { return p->busy(); });
}

bool Link::moving(const std::string& library) const
{
    return std::ranges::any_of(peers_, [&](const Peer* p) { return p->ready() && p->moving(library); });
}

std::set<std::string> Link::media_coming(const std::string& library) const
{
    std::set<std::string> out;
    for (const auto* peer : peers_) {
        if (peer->ready()) {
            out.merge(peer->media_coming(library));
        }
    }
    return out;
}

std::map<std::string, Arriving> Link::media_arriving(const std::string& library) const
{
    std::map<std::string, Arriving> out;
    for (const auto* peer : peers_) {
        if (peer->ready()) {
            out.merge(peer->media_arriving(library));
        }
    }
    return out;
}

std::set<std::string> Link::media_going(const std::string& library) const
{
    std::set<std::string> out;
    for (const auto* peer : peers_) {
        if (peer->ready()) {
            out.merge(peer->media_going(library));
        }
    }
    return out;
}

bool Link::nobody_has(const std::string& library, const std::string& path) const
{
    bool asked = false;
    for (const auto* peer : peers_) {
        if (!peer->ready() || peer->is_closed() || !peer->libraries().contains(library)) {
            continue;
        }
        // One still getting it has not got it, but it is not lost.
        if (!peer->said_missing(library, path) || peer->says_coming(library, path)) {
            return false;
        }
        asked = true;
    }
    return asked;
}

std::optional<std::string> Link::coming_through(const std::string& library, const std::string& path) const
{
    for (const auto* peer : peers_) {
        if (peer->ready() && !peer->is_closed() && peer->says_coming(library, path)) {
            return peer->name();
        }
    }
    return std::nullopt;
}

void Link::fetch_more(const std::string& library)
{
    for (auto* peer : peers_) {
        peer->fetch_more(library);
    }
}

void Link::set_share_address(bool share)
{
    if (share == shared_.share_address) {
        return;
    }
    shared_.share_address = share;
    for (auto* peer : peers_) {
        peer->share_address_changed();
    }
}

void Link::introduce(const std::string& library)
{
    const auto lib = std::ranges::find(libraries_, library, &SyncedLibrary::uid);
    const auto asks = [&](const Peer* p) { return p->ready() && p->asks_introductions(library).has_value(); };
    // The list is read only when there are two to introduce.
    if (lib == libraries_.end() || std::ranges::count_if(peers_, asks) < 2) {
        return;
    }
    // Members only, as this computer's list has them: a computer joining, or
    // one removed, learns where nobody is.
    const auto list = sync::read_members(*lib->store, library);
    const auto& is_local = shared_.is_local ? shared_.is_local : is_local_address;
    struct Asking {
        Peer* peer = nullptr;
        crypto::PublicKey key;
        std::string address;
    };
    std::vector<Asking> asking;
    for (auto* peer : peers_) {
        if (!peer->ready() || peer->is_closed() || !peer->key()) {
            continue;
        }
        const auto port = peer->asks_introductions(library);
        const auto* member = list.with_key(*peer->key());
        if (!port || member == nullptr || member->removed) {
            continue;
        }
        // Never an address of this place's own networks to another network.
        if (is_local(plain_address(peer->address()))) {
            continue;
        }
        asking.push_back({peer, *peer->key(), address_and_port(peer->address(), *port)});
    }
    for (const auto& a : asking) {
        for (const auto& b : asking) {
            if (a.peer != b.peer) {
                a.peer->introduce(library, b.key, b.address);
            }
        }
    }
}

void Link::set_upload_cap(std::int64_t bytes_per_second)
{
    if (bytes_per_second == shared_.upload.rate()) {
        return;
    }
    shared_.upload.set_rate(bytes_per_second);
    for (auto* peer : peers_) {
        peer->upload_cap_changed();
    }
}

Peer* Link::better_for(const Peer& asking, const std::string& library, const db::MediaDigest& file) const
{
    const auto here = asking.route();
    Peer* best = nullptr;
    Route best_route;
    for (auto* peer : peers_) {
        if (peer == &asking || !peer->could_take(library, file)) {
            continue;
        }
        const auto route = peer->route();
        if (clearly_better(route, here) && (best == nullptr || clearly_better(route, best_route))) {
            best = peer;
            best_route = route;
        }
    }
    return best;
}

Peer* Link::to_measure(const Peer& from, const std::string& library, const db::MediaDigest& file) const
{
    const auto there = from.route();
    for (auto* peer : peers_) {
        if (peer != &from && !peer->route().per_second && !peer->fetching_media() && peer->could_take(library, file)
            && !clearly_better(there, peer->route())) {
            return peer;
        }
    }
    return nullptr;
}

void Link::rebalance()
{
    const double seconds = static_cast<double>(measured_.restart()) / 1000.0;
    for (auto* peer : peers_) {
        if (peer->ready()) {
            peer->measure(seconds);
        }
    }
    // What was left to one that then had no room after all.
    for (auto* peer : peers_) {
        if (peer->ready()) {
            peer->ask_again_for_what_it_left();
        }
    }
    const auto now = Clock::now();
    std::erase_if(moved_, [&](const auto& entry) { return now - entry.second >= k_moved_lately; });

    struct Coming {
        Peer* over = nullptr;
        std::string library;
        db::MediaDigest file;
        Flowing flowing;
    };
    std::vector<Coming> coming;
    // Only what any connection may fetch is moved: what a change file held on
    // one waits for, the others do not know of. Worked out once a library.
    std::map<std::string, std::map<std::string, db::MediaDigest>> anyones_of;
    const auto anyones_in = [&](const std::string& uid) -> const std::map<std::string, db::MediaDigest>& {
        const auto [it, added] = anyones_of.try_emplace(uid);
        if (added) {
            if (const auto owed = shared_.owed_media.find(uid); owed != shared_.owed_media.end()) {
                it->second = owed->second;
            }
            const auto lib = std::ranges::find(libraries_, uid, &SyncedLibrary::uid);
            if (lib != libraries_.end() && lib->missing_media) {
                for (auto& digest : lib->missing_media()) {
                    it->second.try_emplace(digest.path, std::move(digest));
                }
            }
        }
        return it->second;
    };
    for (auto* peer : peers_) {
        if (!peer->ready() || peer->is_closed()) {
            continue;
        }
        for (const auto& uid : peer->libraries()) {
            const auto flowing = peer->media_flowing(uid);
            if (flowing.empty()) {
                continue;
            }
            const auto& anyones = anyones_in(uid);
            for (const auto& [path, f] : flowing) {
                // With less than a piece to come, what is on its way is most of it.
                const auto digest = anyones.find(path);
                if (f.left >= sync::k_media_piece_bytes && digest != anyones.end() && !moved_.contains({uid, path})) {
                    coming.push_back({peer, uid, digest->second, f});
                }
            }
        }
    }
    // Those at the end of a piece first, which go at once, then those with the
    // most still to come.
    std::ranges::sort(coming, [](const Coming& a, const Coming& b) {
        return std::pair{a.flowing.mid_piece, -a.flowing.left} < std::pair{b.flowing.mid_piece, -b.flowing.left};
    });
    // A file to each connection at a time: the room it has is not taken up
    // until it asks.
    std::set<const Peer*> given;
    for (const auto& c : coming) {
        auto* to = better_for(*c.over, c.library, c.file);
        if (to == nullptr) {
            // Worth finding out about another way only for what has a while to go.
            const auto there = c.over->route().per_second;
            if (there && static_cast<double>(c.flowing.left) / std::max(1.0, *there) >= k_worth_measuring_seconds) {
                to = to_measure(*c.over, c.library, c.file);
            }
        }
        if (to == nullptr || !given.insert(to).second) {
            continue;
        }
        c.over->hand_over(c.library, c.file.path, *to);
        moved_[{c.library, c.file.path}] = now;
        emit media_moved(c.library, c.file.path);
    }
}

void Link::set_paused(bool paused)
{
    if (shared_.paused == paused) {
        return;
    }
    shared_.paused = paused;
    for (auto* peer : peers_) {
        peer->pause_changed();
    }
}

bool Link::has_all_of(const std::string& library)
{
    bool told = false;
    for (auto* peer : peers_) {
        if (!peer->ready() || peer->is_closed()) {
            continue;
        }
        if (peer->coming(library)) {
            return false;
        }
        told = told || peer->told(library);
    }
    return told;
}

}  // namespace seventhslide::link
