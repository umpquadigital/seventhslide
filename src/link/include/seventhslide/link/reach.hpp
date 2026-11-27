#pragma once

// Keeping this computer connected to the others of its libraries. For every
// member of every library the link syncs (not removed, not this computer) it
// keeps one connection up, reaching the computer where it can be found:
//
//   - where it was heard on the church network (``Discovery``): of two that
//     hear each other, the one with the lower key's fingerprint connects,
//     and the other only if that has not happened after a while (it may not
//     know the newer one yet);
//   - at the addresses typed for it here (``set_typed_addresses``), when its
//     own are out of date or not enough: kept in the same file, by key, for
//     every library it is in, and never on a list. The key is checked there as
//     anywhere;
//   - where this computer last reached it (kept in a file, so a laptop
//     reaches the church PC again after a restart);
//   - where a member connected to both said it is (``introduced``): only on
//     the internet, a few at a time, given up after a while, and never put on a
//     list;
//   - at the addresses it gave on the member list (a computer that says
//     where it takes connections);
//
// Connected some other way, a computer heard on the church network is reached
// there too, and that connection replaces the other (``Link::connect_instead``):
// two computers side by side do not talk through their router, nor through a VPN
// with addresses that look local. Connected at any address it is heard at (a
// computer on the network twice) it is left be, and the connection that replaced
// another is the last: this moves once. At startup, addresses not on a local
// network wait a moment for the network to be heard, and one being tried gives way
// the moment it is.
//
// A library this computer is joining has no list here yet: until it comes,
// the computers its invite names (the members that take connections, the
// computer that made it) are reached instead, at every address the invite
// gives, each offered the library with the invite, until the invite runs
// out. It is read from this
// computer's own join entry, so a restart goes on where it left off.
//
// A computer invited may be one nobody can reach yet from where it is: an
// always-on one on a cloud machine, whose editor is at home, can dial no one.
// So any member may be told where the computer an invite waiting on the list
// is for can be found (``set_invite_addresses``), and reaches out there, one
// address at a time, until it has come, or the invite has been used, withdrawn
// or has run out. No key is expected, as that computer's is not known: it gets
// in only by proving it holds the invite, and anything else there gets nothing.
// A computer of the library answering there is not the one invited, and the
// wait goes on: a server being replaced may still be up at the address its
// successor will have.
//
// An attempt not through in 15 seconds is given up. A connection that
// fails is not tried at that address again at once:
// 5 seconds, then twice as long each time, up to 10 minutes; one that
// closes is tried again soon. Members are connected to by the key the list
// gives them, so an address another computer claims gets it nothing.
//
// A name is looked up here, and each address it has is tried as one of its own,
// in the order given: Qt would wait 30 seconds on each that does not answer, so an
// IPv6 one a firewall drops would keep a name's IPv4 one from ever being tried. A
// name is looked up again after a minute, as a home's addresses change. It is the
// name that is kept where the computer was reached.
//
// Seam: Qt Core (timers), the link, the member list.

#include "seventhslide/link/link.hpp"
#include "seventhslide/link/most_heard.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTimer>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::link {

/// Whether ``address`` is on a network of this place: private, link-local,
/// unique local or this computer. A VPN's or a carrier's shared addresses are not.
[[nodiscard]] bool is_local_address(const QHostAddress& address);

class Reach final : public QObject {
    Q_OBJECT
public:
    struct Options {
        /// How often to look for a computer not connected.
        int check_ms = 5000;
        /// How long the higher key waits for the lower to connect.
        int lan_grace_ms = 15000;
        int first_retry_ms = 5000;
        int longest_retry_ms = 600000;
        /// A computer heard on the network counts as there for this long.
        int heard_fresh_ms = 100000;
        /// An attempt not through by then is given up, so an address that
        /// never answers does not keep the computer from being reached at
        /// another.
        int attempt_ms = 15000;
        /// How long addresses not on a local network wait at the start, for the
        /// computers on this one to be heard first.
        int startup_wait_ms = 2000;
        /// How long an address a computer was introduced at is tried for.
        int introduced_for_ms = 600000;
        /// What counts as a local address; ``is_local_address`` unless given.
        std::function<bool(const QHostAddress&)> is_local;
        /// The addresses a name stands for, in the order to try them; looked up
        /// without waiting (``QHostInfo``) unless given.
        std::function<std::vector<QHostAddress>(const QString&)> look_up;
    };

    /// ``remembered`` is the file where the addresses computers were last
    /// reached at are kept (in the data folder); empty for none.
    Reach(Link& link, std::filesystem::path remembered, Options options, QObject* parent = nullptr);
    Reach(Link& link, std::filesystem::path remembered, QObject* parent = nullptr)
        : Reach(link, std::move(remembered), Options{}, parent)
    {
    }

    ~Reach() override;

    /// The computer joining ``library`` by invite ``invite`` (its id) may be at
    /// ``addresses`` ("host:port" each): reach out there until it has come, or
    /// the invite has been used or withdrawn, or ``expires`` (ISO 8601 UTC) has
    /// passed; none to stop. Kept across a restart.
    void set_invite_addresses(const std::string& library, const std::string& invite, const std::string& expires,
                              std::vector<std::string> addresses);
    /// The addresses invite ``invite`` of ``library`` is reached out with.
    [[nodiscard]] std::vector<std::string> invite_addresses(const std::string& library,
                                                            const std::string& invite) const;
    /// Reach the computer with key ``fingerprint`` at ``addresses`` ("host:port"
    /// each, in turn) before any it gave; none to go by its own again.
    void set_typed_addresses(const std::string& fingerprint, std::vector<std::string> addresses);
    /// The addresses typed for it here, in the order they are tried.
    [[nodiscard]] std::vector<std::string> typed_addresses(const std::string& fingerprint) const;
    /// Asked to look now: what was to wait before it is tried again need not.
    void try_again_now()
    {
        retries_.clear();
        looked_up_.clear();
    }
    /// Whether every computer it would reach is connected.
    [[nodiscard]] bool all_reached() const;

    /// Start looking now and every ``check_ms``.
    void start();
    /// Look now: connect to whoever is not connected and may be tried.
    void check();
    /// A SeventhSlide with the key ``fingerprint`` takes connections at
    /// ``address:port`` (``Discovery::heard``). Those heard lately are kept:
    /// every member, and any computer connected, and ``k_most_heard`` others
    /// at most.
    void heard(const std::string& fingerprint, const QHostAddress& address, quint16 port);
    /// How many computers heard on the network are kept track of.
    [[nodiscard]] std::size_t heard_count() const { return heard_.size(); }

    /// A SeventhSlide heard on the network lately.
    struct Neighbour {
        std::string fingerprint;
        QHostAddress address;
        quint16 port = 0;
        QDateTime last_heard;
    };
    [[nodiscard]] std::vector<Neighbour> neighbours() const;
    /// Whether ``peer``'s computer is on this network: heard here lately, at
    /// the address it is connected at (``PeerShared::close_by``).
    [[nodiscard]] bool close_by(const Peer& peer) const;

    /// The computer with key ``by``, connected and sharing ``library``, said the
    /// one with key ``fingerprint`` is at ``address`` (``Link::introduced``):
    /// tried after the addresses typed for it, when it is an address on the
    /// internet, once the list here names it a member (it may not yet, for a
    /// computer joining). Only as honest as ``by``, so few are kept, and fewer
    /// taken a minute: one that lies makes this computer try an address that
    /// gets it nothing, now and then, the key checked there.
    void introduced(const std::string& library, const std::string& by, const std::string& fingerprint,
                    const std::string& address);
    /// The addresses the computer with key ``fingerprint`` was introduced at,
    /// still tried, the latest first.
    [[nodiscard]] std::vector<std::string> introduced_at(const std::string& fingerprint) const;

    /// The addresses ``fingerprint``'s computer was last reached at, most
    /// recent first.
    [[nodiscard]] std::vector<std::string> remembered(const std::string& fingerprint) const;

private:
    struct HeardAt {
        QHostAddress address;
        QElapsedTimer last;
    };
    struct Heard {
        /// Where it was heard last.
        QHostAddress address;
        quint16 port = 0;
        QElapsedTimer last;
        /// Each address it was heard at, longest ago first, and few.
        std::vector<HeardAt> lately;
        QDateTime when;
        /// Since when it has been heard without a connection to it; invalid
        /// while connected.
        QElapsedTimer waiting;
        /// Since when it has been heard with a connection to it some other
        /// way; invalid while there is none.
        QElapsedTimer away;
    };
    struct Retry {
        QElapsedTimer since;
        qint64 wait_ms = 0;
    };
    struct Attempt {
        std::string fingerprint;
        /// What it was reached for: an address, or a name with its port.
        std::string address;
        /// The address tried: ``address``, or one the name has.
        std::string at;
        QElapsedTimer since;
    };
    struct LookedUp {
        /// In the order to try them.
        std::vector<QHostAddress> addresses;
        /// Since it was found; invalid until it first is.
        QElapsedTimer since;
        bool asking = false;
    };
    struct Wanted {
        crypto::PublicKey key;
        std::vector<std::string> addresses;
        /// The libraries to offer it, joining: this computer has to reach
        /// it, as it does not know this computer yet.
        std::vector<Joining> joining;
    };
    struct Expected {
        std::string library;
        std::string invite;
        std::string expires;
        std::vector<std::string> addresses;
    };
    struct Introduced {
        std::string address;
        QElapsedTimer since;
    };

    [[nodiscard]] std::map<std::string, Wanted> wanted() const;
    /// What of the libraries being joined here goes through the computer with
    /// ``key``: asked of every connection, a stranger's too, so it looks no
    /// further than those libraries.
    [[nodiscard]] std::vector<Joining> joining_through(const crypto::PublicKey& key) const;
    /// The computer ``fingerprint`` is being reached for ``address``, at ``at`` (the
    /// address, or one its name has), by ``peer``.
    void attempting(Peer* peer, const std::string& fingerprint, const std::string& address, const std::string& at);
    /// What ``address`` ("host:port") stands for, "host:port" each: itself, when it
    /// is one; for a name, the addresses it was last found to have, the name looked
    /// up again when that was long ago. Nothing while it is being looked up.
    [[nodiscard]] std::optional<std::vector<std::string>> addresses_at(const std::string& address);
    /// Of those ``address`` stands for, the first that may be tried, and is local
    /// when ``local_only``: empty for none, nothing while it is being looked up.
    [[nodiscard]] std::optional<std::string> to_try(const std::string& address, bool local_only);
    /// Reach out for the computers invited (``set_invite_addresses``).
    void reach_expected();
    /// Whether ``peer`` got in by an invite reached out with.
    [[nodiscard]] bool came_by_invite(const Peer& peer) const;
    /// A connection this computer made to ``address`` got as far as it did:
    /// whether that counts as reaching what it was made for.
    [[nodiscard]] bool got_through(const Attempt& attempt, const Peer& peer);
    [[nodiscard]] bool may_try(const std::string& address) const;
    [[nodiscard]] bool local(const QHostAddress& address) const;
    /// Whether ``address`` ("host:port") is one: a name is not known to be.
    [[nodiscard]] bool local(const std::string& address) const;
    /// Whether something is being tried at ``address`` already.
    [[nodiscard]] bool being_tried(const std::string& address) const;
    /// Whether the computer heard at ``heard`` was heard lately at ``address``.
    [[nodiscard]] bool heard_at(const Heard& heard, const QHostAddress& address) const;
    /// ``peer`` reaches the computer heard at ``heard``: try for a better way,
    /// when this is the end to, and it may be tried.
    void reach_closer(const std::string& fingerprint, const crypto::PublicKey& key, const Peer& peer, Heard& heard);
    /// ``peer`` is on its way to the computer, which was just heard close by.
    /// Further off and made here, it gives way once it is this end's turn to reach
    /// out and the address heard may be tried: whether that leaves the computer to
    /// reach (nothing else on its way).
    [[nodiscard]] bool give_way(const std::string& fingerprint, const Wanted& want, Peer& peer, Heard& heard);
    /// Try the next address for a computer with no connection on its way;
    /// ``heard`` is where it was just heard on the network, if it was.
    void reach_for(const std::string& fingerprint, const Wanted& want, Heard* heard);
    void tried(const std::string& address, bool reached);
    /// Forget the computers not heard for ``heard_fresh_ms``; and, for room for
    /// one more, the strangers heard longest ago.
    void forget_unheard(std::size_t room = 0);
    void reached(Peer* peer);
    void load();
    void save() const;

    Link& link_;
    std::filesystem::path file_;
    Options options_;
    std::string fingerprint_;
    QTimer timer_;
    QTimer soon_;
    std::map<std::string, Heard> heard_;
    /// The fingerprints ``wanted`` gave at the last look, never forgotten for
    /// a stranger.
    std::set<std::string> wanted_;
    /// fingerprint -> "host:port", most recent first.
    std::map<std::string, std::vector<std::string>> remembered_;
    std::map<std::string, Retry> retries_;
    std::map<Peer*, Attempt> attempts_;
    /// name -> what it was found to have.
    std::map<std::string, LookedUp> looked_up_;
    std::vector<Expected> expected_;
    /// fingerprint -> "host:port" each, as typed.
    std::map<std::string, std::vector<std::string>> typed_;
    /// fingerprint -> where it was introduced at, the latest first.
    std::map<std::string, std::vector<Introduced>> introduced_;
    /// fingerprint -> when each address taken for it in the last minute was.
    std::map<std::string, std::vector<QElapsedTimer>> introduced_lately_;
    /// Since it started looking.
    QElapsedTimer started_;
};

}  // namespace seventhslide::link
