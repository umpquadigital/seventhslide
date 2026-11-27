#pragma once

// The link: this computer's connections to the other computers of its synced
// libraries. It listens for them and reaches out to them, keeps one ``Peer``
// per connection, and passes on what arrives: a store that changes through one
// peer is announced to the others, so a change reaches every connected
// computer, through a server when the two are apart. The sync engine is not
// run here: the app runs it when a store changes (``store_changed``), and
// tells the link when the engine wrote (``library_changed``).
//
// One connection to each computer: reaching out to one connected already
// joins over that connection, and when two computers reach each other at
// once, both close the same one of the two (``keep_one``). Across all its
// connections, the link fetches each file from one at a time (``Fetches``),
// and each connection a few files at a time, so the work is shared. Media goes
// to the clearly better route (``routes.hpp``): a connection leaves a file to a
// better one with room for it (``better_for``), and every second a file coming
// over one is moved to a better one that has room (``rebalance``), going on from
// the end of its last piece. A connection not measured yet, with nothing to
// fetch, is given a file that has a while to go elsewhere, so that one that came
// later finds work, and is measured. A file moves once a minute at most.
//
// Two members of a library connected here that both ask (``Peer::introduce``)
// are told where the other is, as seen here, so that each may reach the other
// directly: forwarded, or with IPv6 and an open firewall. Only an address on the
// internet is told, never one of this place's own networks.
// Paused (``set_paused``), nothing moves over any of them but each library's
// shared log (``Peer``). A better way to a computer connected already is
// taken with ``connect_instead``.
//
// Seam: Qt Network.

#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/link/peer.hpp"
#include "seventhslide/link/synced_library.hpp"
#include "seventhslide/sync/identity.hpp"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QSslConfiguration>
#include <QString>
#include <QTcpServer>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <string>
#include <vector>

namespace seventhslide::link {

/// Connections from others not yet let in to a library (``Peer::admitted``)
/// this computer keeps at once, in all and from one address, counted from when
/// they arrive, before any TLS. One more from an address with its share is closed
/// as it comes; one more when all are taken closes the one waiting longest. A real
/// computer is let in within moments of connecting, so those that hang back,
/// from however many addresses, take each other's places and not its.
inline constexpr int k_max_unadmitted = 32;
inline constexpr int k_max_unadmitted_per_address = 4;

/// Whether connections from ``a`` and ``b`` count as from one address, for the
/// limit above: an IPv4 address alone (as ``::ffff:...`` too, as a listener taking
/// both shows it), an IPv6 one with all of its /64, which one home is given whole.
[[nodiscard]] bool same_source(const QHostAddress& a, const QHostAddress& b);

/// Hands over each connection as it arrives. ``QSslServer`` will not do: it stops
/// taking connections with 30 mid-handshake, and those that went away do not
/// start it again, so one port scanner shuts a computer's door until it restarts.
class Listener final : public QTcpServer {
public:
    std::function<void(qintptr)> arrived;

protected:
    void incomingConnection(qintptr descriptor) override { arrived(descriptor); }
};

class Link final : public QObject {
    Q_OBJECT
public:
    /// Throws ``std::runtime_error`` when TLS cannot be set up from the key.
    explicit Link(sync::Identity me, QObject* parent = nullptr);

    [[nodiscard]] const sync::Identity& identity() const noexcept { return me_; }
    /// What this computer is called, from the next hello on.
    void set_computer_name(std::string name) { me_.computer = std::move(name); }

    /// Sync ``library`` with the peers that have it. Its media folder must
    /// outlive the link; its store is the link's to share.
    void add_library(SyncedLibrary library);
    void remove_library(const std::string& uid);
    [[nodiscard]] const std::vector<SyncedLibrary>& libraries() const noexcept { return libraries_; }

    /// Accept connections at ``address:port`` (0 for any port). False when
    /// the port cannot be had.
    [[nodiscard]] bool listen(const QHostAddress& address, quint16 port = 0);
    [[nodiscard]] quint16 port() const;

    /// Reach out to a computer. ``expected`` is its key when known (a
    /// member's from the invite or the list); ``joining`` names the libraries
    /// this computer joins through it, with its join entries. When
    /// this computer is connected (or connecting) to that key already, the
    /// libraries are joined over that connection and it is the one
    /// returned. The peer is this link's until it closes. It may be closed
    /// already as this returns (is_closed()): a connection refused at once,
    /// with no route to the host (the network down, a Mac just awake), closes
    /// inside connectToHost. Then it is deleted on the next pass of the event
    /// loop, and peer_closed has been and gone, so it must not be kept.
    Peer* connect_to(const QString& host, quint16 port, std::optional<crypto::PublicKey> expected,
                     std::vector<Joining> joining = {});

    /// The engine wrote to the store of ``library``: tell every peer.
    void library_changed(const std::string& uid);

    [[nodiscard]] const std::vector<Peer*>& peers() const noexcept { return peers_; }
    /// Reach out to the computer with ``key``, connected already, at a better
    /// way to it. Once through, both ends keep this connection and close the
    /// other; if it does not get through, the other stands. Only the end that
    /// reaches out decides: behind a router's loopback the two can see one
    /// connection differently, and each closing another would lose both.
    Peer* connect_instead(const QString& host, quint16 port, const crypto::PublicKey& key);

    /// The connection to the computer with ``key``: one that is ready, or one
    /// on its way; null for none.
    [[nodiscard]] Peer* peer_with(const crypto::PublicKey& key) const;
    /// Whether any peer still has something on its way.
    [[nodiscard]] bool busy() const;
    /// Whether this computer has all of ``library`` the connected computers
    /// have: one of them at least has said what it has, and nothing of it is
    /// still to come from any (``Peer::coming``). A library joined here may
    /// be opened only then.
    [[nodiscard]] bool has_all_of(const std::string& library);
    /// Whether anything of ``library`` is going over any connection now,
    /// either way (``Peer::moving``).
    [[nodiscard]] bool moving(const std::string& library) const;
    /// The media files of ``library`` on their way in over any connection
    /// (``Peer::media_coming``).
    [[nodiscard]] std::set<std::string> media_coming(const std::string& library) const;
    /// Of those, the ones being written here now, and how far each is
    /// (``Peer::media_arriving``).
    [[nodiscard]] std::map<std::string, Arriving> media_arriving(const std::string& library) const;
    /// The media files of ``library`` going out over any connection.
    [[nodiscard]] std::set<std::string> media_going(const std::string& library) const;
    /// Whether no connected computer has media file ``path`` of ``library``:
    /// one at least shares the library, and each that does said it has not
    /// got it. False while one may still have it, or none is connected.
    [[nodiscard]] bool nobody_has(const std::string& library, const std::string& path) const;
    /// The name of a connected computer ``path`` of ``library`` is on its way
    /// to, if one has said so; nullopt if none.
    [[nodiscard]] std::optional<std::string> coming_through(const std::string& library,
                                                            const std::string& path) const;
    /// Ask every connection for what of ``library`` is missing here now
    /// (media the library found missing after a sync, say), rather than
    /// when each next says what it has.
    void fetch_more(const std::string& library);
    /// Ask the computers connected to introduce this one to the others of each
    /// library, and it them; or stop (``PeerShared::share_address``).
    void set_share_address(bool share);
    [[nodiscard]] bool share_address() const noexcept { return shared_.share_address; }
    /// Hold media sent to computers not on this network to ``bytes_per_second``
    /// over every connection: 0 for no cap (``PeerShared::upload``).
    void set_upload_cap(std::int64_t bytes_per_second);
    [[nodiscard]] std::int64_t upload_cap() const noexcept { return shared_.upload.rate(); }
    /// How often connections are measured, and files moved to better ones
    /// (tests change it).
    void set_rebalance_every(int ms) { routes_.setInterval(ms); }
    /// Pause syncing over every connection, or go on (``Peer::pause_changed``):
    /// while paused nothing is fetched or sent but the shared log, and the
    /// other computers fetch from elsewhere what they were fetching from this
    /// one. Connections stay up, so they go on at once.
    void set_paused(bool paused);
    [[nodiscard]] bool paused() const noexcept { return shared_.paused; }
    /// How often a quiet connection is asked whether it is there, and how
    /// long one may say nothing before it is closed (tests shorten them),
    /// for connections made from now on.
    void set_keep_alive(int ping_ms, int silent_ms)
    {
        shared_.ping_ms = ping_ms;
        shared_.silent_ms = silent_ms;
    }
    /// How long a connection may go without saying hello (tests shorten it).
    void set_hello_timeout(int ms) { shared_.hello_ms = ms; }
    /// How long one from elsewhere has to finish TLS (``PeerShared::handshake_ms``).
    void set_handshake_timeout(int ms) { shared_.handshake_ms = ms; }
    /// How long a change file waits for its media before it is stored without
    /// it (``PeerShared::hold_for_media_ms``; tests shorten it).
    void set_hold_for_media(int ms) { shared_.hold_for_media_ms = ms; }
    /// How often the others are told what is on its way here (tests change it).
    void set_tell_coming_every(int ms) { tell_coming_.setInterval(ms); }
    /// What its connections share, among it the threads files are read
    /// through on (tests hold them up, to see what waits for them).
    [[nodiscard]] PeerShared& shared() noexcept { return shared_; }
    /// Wait for what is being written of the media and the store, on those threads: a
    /// library off the link (``remove_library``) gets no more, and its folder
    /// may go once this returns.
    void wait_for_writes();

signals:
    /// A peer put something new in the store of ``library``: the engine has
    /// work.
    void store_changed(const std::string& library);
    /// A snapshot's part of ``library``, or a piece of a large file of its
    /// store, came through a peer (``Peer::files_coming``).
    void files_coming(const std::string& library);
    /// A peer put shared log entries in the store of ``library``: nothing for
    /// the engine, only for whoever shows the log.
    void log_stored(const std::string& library);
    /// A media file of ``library`` came through a peer, checked, and is on
    /// the disk at ``path`` (under the library's media folder).
    void media_stored(const std::string& library, const std::string& path);
    /// Member ``machine`` of ``library``, connected, said what it has applied
    /// (``Peer::progress``).
    void progress(const std::string& library, const std::string& machine,
                  const std::map<std::string, std::int64_t>& applied);
    void peer_ready(Peer* peer);
    /// ``peer`` closed (it is deleted later): why, in words.
    void peer_closed(seventhslide::link::Peer* peer, const QString& why);
    void refused(const QString& why);
    /// A connected computer said it has not got something of ``library``.
    void not_there(const std::string& library);
    /// Media file ``path`` of ``library``, coming over one connection, is
    /// handed to another (``rebalance``).
    void media_moved(const std::string& library, const std::string& path);
    /// The computer with key ``by``, connected, said the one with key
    /// ``fingerprint``, of ``library``, is at ``address`` ("host:port"): as yet
    /// unchecked (``Reach::introduced``).
    void introduced(const std::string& library, const std::string& by, const std::string& fingerprint,
                    const std::string& address);

private:
    /// Read what ``uid``'s store now vouches for, off this thread, then let
    /// every connection ask for what that lets in. Only one refresh runs per
    /// library; one asked for while it runs is done once, after it.
    void refresh_vouches(const std::string& uid);
    /// Whether a connection from ``address`` may be taken on now, closing the
    /// one waiting longest when that makes the room (``k_max_unadmitted``).
    [[nodiscard]] bool make_room_for(const QHostAddress& address);
    Peer* adopt(QSslSocket* socket, std::optional<crypto::PublicKey> expected,
                std::vector<Joining> joining, bool outgoing);
    /// A connection this computer makes.
    Peer* open_to(const QString& host, quint16 port, std::optional<crypto::PublicKey> expected,
                  std::vector<Joining> joining, bool replaces);
    /// ``fresh`` is ready: when another connection to the same computer is
    /// too, close one. ``fresh`` is kept when it replaces the other and the
    /// other is not one that did; else (both reached out at once) the connection
    /// the lower key made, or the newer of two made the same way. A connection
    /// long since kept, that once replaced another, then counts as any other, so
    /// a dead one gives way to a new one. That turns on which came last, so a
    /// plain connection crossing one that replaces can close both: the same one
    /// goes at both ends otherwise, and ``Reach`` connects again if not.
    void keep_one(Peer* fresh);
    /// Something went into the store of ``library`` from a connection, or as
    /// one closed.
    void stored(const std::string& library);
    /// The connection clearly better than ``asking`` to fetch ``file`` of
    /// ``library`` with, of those that would ask for it now: the best of them,
    /// or null.
    [[nodiscard]] Peer* better_for(const Peer& asking, const std::string& library, const db::MediaDigest& file) const;
    /// A connection to be measured by fetching ``file`` of ``library``, coming
    /// over ``from`` now: one not measured yet, with nothing to fetch, room for
    /// it, and not clearly worse. Null for none.
    [[nodiscard]] Peer* to_measure(const Peer& from, const std::string& library, const db::MediaDigest& file) const;
    /// Introduce to each other the members of ``library`` connected here that
    /// ask to be, at the addresses they are seen at, those on the internet.
    void introduce(const std::string& library);
    /// Measure every connection, ask again for what was left to others, and move
    /// files coming over one to a clearly better one with room for them, or to
    /// one to be measured.
    void rebalance();

    sync::Identity me_;
    QSslConfiguration tls_;
    Listener server_;
    std::vector<SyncedLibrary> libraries_;
    /// Libraries whose vouches are being refreshed, and whether they were
    /// asked for again meanwhile (``refresh_vouches``).
    std::map<std::string, bool> refreshing_;
    PeerShared shared_;
    std::vector<Peer*> peers_;
    /// Every second: the others hear what is on its way here, if it changed.
    QTimer tell_coming_;
    /// Every second: ``rebalance``, over the time since it last did.
    QTimer routes_;
    QElapsedTimer measured_;
    /// The media files moved to another connection lately, by library and
    /// path: when.
    std::map<std::pair<std::string, std::string>, Clock::time_point> moved_;
};

}  // namespace seventhslide::link
