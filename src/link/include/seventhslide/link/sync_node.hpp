#pragma once

// This computer's sync, whatever runs it: the app with its window on top,
// or ``seventhslide --server`` with none (docs/SERVER.md). Every SeventhSlide
// is the same kind of computer to the others; what differs is a setting
// here (``Options``), not another kind of host.
//
// The node owns the link (taking connections and reaching out), the
// announcing on the church network (``Discovery``), keeping connected to
// each library's other computers (``Reach``), and one ``LibrarySync`` per
// library it syncs, each on a thread of its own, taking turns
// (``SyncTurns``). It wires them as every computer needs: what a connection
// stores is synced soon, a media file that comes is no longer missing, what
// a member said it has applied is kept, what the engine wrote is told to
// every connection, media still missing after a sync is asked for, and this
// computer's name, its picture and the addresses typed for it are put on each
// library's member list. Paused, nothing syncs and nothing moves over any
// connection but each library's shared log, where a message posted here
// (``post_message``) goes at once.
//
// Whether it may sync now is asked here, once: not while its host holds it
// back (``set_paused``: something live on the window), and, when it does not
// sync automatically (``SyncSettings::sync_automatically``), only from Sync Now
// until everything has moved (``sync_now``).
//
// What its host adds: the app, which libraries (its registry) and the open
// one's window; the server, which libraries it keeps, and its commands.
//
// In its data folder it keeps where it last reached each computer
// (``sync_addresses.json``) and when it last heard from each
// (``sync_heard.json``, by key). It says in the log when the disk under it is
// nearly full (``disk_nearly_full``).
//
// Seam: Qt Network, the link.

#include "seventhslide/crypto/password_box.hpp"
#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/link/discovery.hpp"
#include "seventhslide/link/library_sync.hpp"
#include "seventhslide/link/link.hpp"
#include "seventhslide/link/reach.hpp"
#include "seventhslide/link/sync_settings.hpp"
#include "seventhslide/link/sync_turns.hpp"
#include "seventhslide/sync/identity.hpp"
#include "seventhslide/sync/member_list.hpp"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QObject>
#include <QThreadPool>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::link {

/// The port computers look for each other on unless told otherwise.
inline constexpr std::uint16_t k_usual_port = 8766;

/// Whether a disk with ``free`` of ``total`` bytes left is nearly full: under
/// 5%, or under 2 GB. Unknown (negative) is not.
[[nodiscard]] bool disk_nearly_full(std::int64_t free, std::int64_t total);

/// ``address`` as the member list keeps one, with ``port`` when it gives none:
/// "host:port" or "[v6 address]:port". Empty when it cannot be one.
[[nodiscard]] std::string address_with_port(const std::string& address, std::uint16_t port);

/// Whether ``entry``, one of this computer's addresses, is an IPv6 one to give the
/// others when none was typed: global (not unique local, a network's own as
/// 192.168 is, which Qt counts as global), not yet given up, and not one of those
/// a computer makes up for privacy and changes each day. Qt marks those as not for
/// DNS; its ``isTemporary`` is any address that ends, as most others do.
[[nodiscard]] bool ipv6_to_give(const QNetworkAddressEntry& entry);

/// Addresses as typed for "Routable address", by commas or spaces. When one
/// is no address ``not_an_address`` says which, and ``too_many`` when there are more
/// than a member list keeps.
struct TypedAddresses {
    std::vector<std::string> addresses;
    std::string not_an_address;
    bool too_many = false;
};
[[nodiscard]] TypedAddresses read_typed_addresses(std::string_view text);

/// Addresses typed for another computer, or for a computer invited, to reach it at
/// from here (``Reach::set_typed_addresses``, ``Reach::set_invite_addresses``):
/// each as "host:port", the usual port when none is given, once each. When one is
/// no address ``not_an_address`` says which; ``its_own`` names one the computer
/// gave itself, which it is reached at anyway; ``too_many`` when there are more
/// than ``sync::k_most_addresses``.
struct AddressesFor {
    std::vector<std::string> addresses;
    std::string not_an_address;
    std::string its_own;
    bool too_many = false;
};
[[nodiscard]] AddressesFor read_addresses_for(const std::vector<std::string>& typed,
                                              const std::vector<std::string>& its_own = {});

class SyncNode final : public QObject {
    Q_OBJECT
public:
    struct Options {
        /// Where the node keeps what is its own.
        std::filesystem::path data;
        /// Take connections, announce itself and reach out; off in tests
        /// that keep to one computer.
        bool network = true;
        QHostAddress listen = QHostAddress::Any;
        /// 0 for any free one.
        std::uint16_t port = k_usual_port;
        /// When ``port`` is taken: listen on another free one (a desktop,
        /// found on the church network wherever it listens) rather than
        /// refuse (a server, whose address the others were given).
        bool any_port_when_taken = false;
        /// How the others reach it, "host" or "host:port", as typed: said on
        /// each library's member list, for the others to reach it there and
        /// invites to name it.
        std::vector<std::string> addresses;
        /// With none typed, say this computer's addresses on its networks on
        /// the list instead: a computer always on, on a network it stays on.
        /// Never a laptop's, which would write an entry at every move.
        bool publish_found_addresses = false;
        /// Off, it syncs only when asked (``sync_now``).
        bool sync_automatically = true;
        /// The upload speed cap, in kilobits a second (``SyncSettings``); 0
        /// for none.
        std::int64_t upload_cap_kbit = 0;
        /// Ask to be introduced to the others of each library
        /// (``SyncSettings::share_address``).
        bool share_address = true;
        /// This computer's picture on each library's member list; none for
        /// none.
        sync::Picture picture;
        /// Asked to sync when it does not automatically: nothing having moved
        /// for this long, it has synced.
        int sync_now_quiet_ms = 3000;
        /// ... once it has looked this long for the computers not connected:
        /// long enough for one that answers to be reached.
        int sync_now_reach_ms = 20000;
        /// Announce itself on the church network and hear the others.
        bool discovery = true;
        Discovery::Options discovery_options;
        Reach::Options reach_options;
        /// How many libraries sync at once.
        int syncs_at_once = 2;
        /// Send what is edited in each library here (a window's); a
        /// computer nobody edits on makes no changes of its own.
        bool watch_edits = false;
        /// How a new admin key is locked (tests lock quicker).
        crypto::BoxParams box_params = crypto::k_box_params;
        /// How long a library joining waits with none of its files coming
        /// (``LibrarySync::Options::join_wait_ms``; tests wait less).
        qint64 join_wait_ms = 60LL * 1000;
    };

    /// Listens at once, when on the network; whether it could is
    /// ``listening``, for its host to say in its own words.
    SyncNode(sync::Identity me, Options options, QObject* parent = nullptr);
    /// Each library off the link first, then let go of, its thread waited
    /// for.
    ~SyncNode() override;
    SyncNode(const SyncNode&) = delete;
    SyncNode& operator=(const SyncNode&) = delete;

    [[nodiscard]] const sync::Identity& identity() const noexcept { return link_->identity(); }
    [[nodiscard]] Link& link() noexcept { return *link_; }
    [[nodiscard]] const Link& link() const noexcept { return *link_; }
    [[nodiscard]] Reach& reach() noexcept { return *reach_; }
    [[nodiscard]] const Reach& reach() const noexcept { return *reach_; }
    /// The addresses typed here for ``computer`` (``Reach::typed_addresses``)
    /// beyond those it gives itself now: one typed when its own changed, and
    /// since given by it too, is no longer one of this computer's alone.
    [[nodiscard]] std::vector<std::string> typed_addresses(const sync::Member& computer) const;
    /// Null when it does not announce itself.
    [[nodiscard]] Discovery* discovery() noexcept { return discovery_.get(); }
    [[nodiscard]] quint16 port() const { return link_->port(); }
    /// Whether it takes connections.
    [[nodiscard]] bool listening() const { return link_->port() != 0; }
    /// How the others reach it, "host:port" each: those given, at the port it
    /// is set to when they say none (where a router forwards, whatever port it
    /// had to take today), else this computer's IPv4 addresses on its networks
    /// and its global IPv6 ones (``ipv6_to_give``; none on a computer without).
    [[nodiscard]] std::vector<std::string> addresses() const;
    /// Say ``addresses`` from now on (``Options::addresses``): none to say it
    /// takes none, unless it publishes those it finds.
    void set_addresses(std::vector<std::string> addresses);
    /// Those given that cannot be addresses, for its host to say so. They are
    /// not said; what the list has stands if none of those given can be.
    [[nodiscard]] std::vector<std::string> unusable_addresses() const;
    /// What this computer is called, from now on: to every connection, in
    /// what each library writes, and on each library's member list.
    void set_computer_name(std::string name);
    /// This computer's picture, from now on, on each library's member list.
    void set_computer_picture(sync::Picture picture);
    [[nodiscard]] const sync::Picture& computer_picture() const noexcept { return options_.picture; }

    /// Post ``text`` to library ``uid``'s shared log, off this thread (not
    /// behind a sync), and tell every connection; ``done`` gets why it could
    /// not, or nothing. Not syncing automatically, it syncs now too.
    void post_message(const std::string& uid, std::string text, std::function<void(std::string)> done);

    /// Sync library ``uid``, whose folder is ``root``. The library is shared
    /// over the link at once, paused when the node is; one held already is let
    /// go of first.
    LibrarySync& hold(const std::string& uid, const std::filesystem::path& root);
    /// Stop syncing ``uid``: off the link, its thread waited for.
    void let_go(const std::string& uid);
    [[nodiscard]] LibrarySync* library(const std::string& uid) const;
    [[nodiscard]] std::vector<std::string> library_uids() const;

    /// Its host holds syncing back, or no longer does: no library syncs, and
    /// nothing moves over any connection (``Link::set_paused``).
    void set_paused(bool paused);
    /// Whether nothing syncs now, for whichever reason.
    [[nodiscard]] bool paused() const noexcept { return link_->paused(); }
    /// Whether it syncs on its own, rather than only when asked.
    [[nodiscard]] bool sync_automatically() const noexcept { return options_.sync_automatically; }
    void set_sync_automatically(bool automatically);
    /// Hold media sent to computers not on this network to ``kbit`` kilobits
    /// a second, from now on; 0 for no cap.
    void set_upload_cap(std::int64_t kbit);
    [[nodiscard]] std::int64_t upload_cap() const noexcept { return options_.upload_cap_kbit; }
    /// Ask to be introduced to the others of each library, from now on, or not.
    void set_share_address(bool share);
    [[nodiscard]] bool share_address() const noexcept { return options_.share_address; }
    /// Whether it is waiting to be asked to sync, and nothing else holds it.
    [[nodiscard]] bool waiting_for_sync_now() const noexcept
    {
        return !options_.sync_automatically && !sync_now_ && !host_paused_;
    }
    /// Sync every library now, and look for the computers not connected. Not
    /// syncing automatically, it goes on until everything has moved, with a
    /// while to reach those not connected and for as long as a library is being
    /// joined, then waits again; held back by its host, it does so once that
    /// ends.
    void sync_now();
    /// Whether anything is on its way: over a connection, a sync, admin work.
    [[nodiscard]] bool busy() const;
    /// When the computer with ``key`` was last connected, ISO 8601 UTC; now
    /// when it is; empty when never, as far as this one knows.
    [[nodiscard]] std::string last_heard(const crypto::PublicKey& key) const;
    /// The sync as ``seventhslide --server status`` shows it: each library's
    /// state, computers (with the addresses each gave, and those typed for it
    /// here) and invites waiting (with the addresses they are reached out
    /// with from here) (``libraries``), who is connected now
    /// (``connections``) and who was heard on the network (``neighbours``).
    [[nodiscard]] nlohmann::json status() const;

signals:
    /// Something a page showing the sync would show again changed: a
    /// connection came or went, a file came, a library synced.
    void changed();
    /// Library ``uid``'s shared log has entries it had not: one that came, a
    /// note of what a sync here sent, a message posted here.
    void log_changed(const std::string& uid);

private:
    struct Held {
        std::unique_ptr<LibrarySync> sync;
        /// The name last put on its member list for this computer: not
        /// written again, even when that failed, until the name changes.
        std::string named_as;
        /// Likewise the picture with it.
        std::optional<sync::Picture> pictured_as;
        /// Likewise the addresses.
        std::optional<std::vector<std::string>> addressed_as;
    };

    void share(const std::string& uid);
    /// Put the name this computer goes by on ``uid``'s member list, when
    /// the list has another (``MemberLog::rename``), and the addresses it
    /// says (``MemberLog::set_addresses``).
    void follow_self(const std::string& uid);
    /// What it says on the member list; empty for none.
    [[nodiscard]] std::vector<std::string> published_addresses() const;
    /// Whether it may sync now: held back, or waiting to be asked.
    void follow_pause();
    /// Asked to sync, not automatically: whether everything has moved.
    void watch_sync_now();
    /// Whether a library is being joined here, its invite still good.
    [[nodiscard]] bool joining() const;
    /// Say in the log when the disk becomes nearly full, and when it no
    /// longer is.
    void watch_disk();
    void heard(const Peer& peer);
    void load_heard();
    void save_heard() const;

    Options options_;
    std::unique_ptr<Link> link_;
    std::unique_ptr<Reach> reach_;
    std::unique_ptr<Discovery> discovery_;
    std::shared_ptr<SyncTurns> turns_;
    std::map<std::string, Held> held_;
    /// Last heard, by key fingerprint: ISO 8601 UTC.
    std::map<std::string, std::string> heard_;
    /// Its host holds syncing back (``set_paused``).
    bool host_paused_ = false;
    /// Asked to sync, not automatically, and not done yet.
    bool sync_now_ = false;
    QTimer sync_now_watch_;
    QElapsedTimer sync_now_quiet_;
    /// Since when it has been free to look for computers.
    QElapsedTimer sync_now_looking_;
    QTimer disk_check_;
    bool disk_full_ = false;
    /// Messages are written here, one at a time.
    QThreadPool log_pool_;
};

}  // namespace seventhslide::link
