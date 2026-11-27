#include "seventhslide/link/reach.hpp"

#include "seventhslide/link/peer.hpp"
#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/member_list.hpp"
#include "seventhslide/sync/now_utc.hpp"
#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/read_file.hpp"

#include <QHostInfo>
#include <QLoggingCategory>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <optional>

namespace seventhslide::link {
namespace {

Q_LOGGING_CATEGORY(lc_reach, "seventhslide.reach", QtWarningMsg)

using nlohmann::json;

constexpr int k_file_format = 2;
/// Addresses kept for each computer.
constexpr std::size_t k_most_remembered = 4;
/// The addresses kept that one computer was heard at lately (``Heard::lately``).
constexpr std::size_t k_most_heard_at = 8;
/// Where a computer was introduced at: so many kept, and so many new ones taken
/// a minute, whatever the computers introducing it say.
constexpr std::size_t k_most_introduced = 4;
constexpr std::size_t k_introduced_a_minute = 2;
constexpr std::size_t k_most_introduced_computers = 64;
constexpr qint64 k_minute_ms = 60000;
/// How long what a name was found to have stands.
constexpr qint64 k_looked_up_ms = 60000;

std::string address_of(const QHostAddress& host, quint16 port)
{
    const auto text = host.toString().toStdString();
    return (host.protocol() == QAbstractSocket::IPv6Protocol ? "[" + text + "]" : text) + ":" + std::to_string(port);
}

}  // namespace

bool is_local_address(const QHostAddress& address)
{
    // A connection taken on both families shows an IPv4 peer as ``::ffff:...``.
    bool v4 = false;
    const QHostAddress plain(address.toIPv4Address(&v4));
    const auto& a = v4 ? plain : address;
    if (a.isNull() || a.isLoopback() || a.isLinkLocal()) {
        return !a.isNull();
    }
    if (a.protocol() == QAbstractSocket::IPv4Protocol) {
        return a.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
               || a.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
               || a.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16);
    }
    return a.isUniqueLocalUnicast() || a.isSiteLocal();
}

Reach::Reach(Link& link, std::filesystem::path remembered, Options options, QObject* parent)
    : QObject(parent), link_(link), file_(std::move(remembered)), options_(options),
      fingerprint_(link.identity().key.public_key().fingerprint())
{
    load();
    link_.shared().joining_with = [this](const crypto::PublicKey& key) { return joining_through(key); };
    link_.shared().close_by = [this](const Peer& peer) { return close_by(peer); };
    connect(&link_, &Link::introduced, this, &Reach::introduced);
    timer_.setInterval(options_.check_ms);
    connect(&timer_, &QTimer::timeout, this, &Reach::check);
    soon_.setSingleShot(true);
    soon_.setInterval(200);
    connect(&soon_, &QTimer::timeout, this, &Reach::check);
    connect(&link_, &Link::peer_ready, this, &Reach::reached);
    connect(&link_, &Link::peer_closed, this, [this](Peer* peer, const QString& why) {
        const auto it = attempts_.find(peer);
        if (it != attempts_.end()) {
            // Ready and then closed as one too many still reached it there.
            tried(it->second.at, got_through(it->second, *peer));
            if (!peer->ready()) {
                qCDebug(lc_reach).noquote() << "could not reach"
                                            << QString::fromStdString(it->second.at) << ":" << why;
            }
            attempts_.erase(it);
        }
        if (peer->ready()) {
            soon_.start();  // to be reached again
        }
    });
}

Reach::~Reach()
{
    link_.shared().joining_with = {};
    link_.shared().close_by = {};
}

void Reach::introduced(const std::string& library, const std::string& by, const std::string& fingerprint,
                       const std::string& address)
{
    // Kept whether or not the list here names it yet (a computer joining hears
    // of the others before their entries come): tried only for a member.
    (void)library;
    // At an address on the internet: not a name, which would be looked up
    // wherever it pointed.
    const auto at = sync::parse_address(address);
    if (fingerprint == fingerprint_ || !at || at->port == 0) {
        return;
    }
    const QHostAddress host(QString::fromStdString(at->host));
    if (host.isNull() || local(host)) {
        return;
    }
    if (const auto known = introduced_.find(fingerprint); known != introduced_.end()) {
        std::erase_if(known->second,
                      [&](const Introduced& i) { return i.since.elapsed() >= options_.introduced_for_ms; });
        const auto again = std::ranges::find(known->second, address, &Introduced::address);
        if (again != known->second.end()) {
            again->since.restart();
            return;
        }
    }
    auto& lately = introduced_lately_[fingerprint];
    std::erase_if(lately, [](const QElapsedTimer& t) { return t.elapsed() >= k_minute_ms; });
    if (lately.size() >= k_introduced_a_minute) {
        return;
    }
    lately.emplace_back().start();
    auto& known = introduced_[fingerprint];
    Introduced fresh{address, {}};
    fresh.since.start();
    known.insert(known.begin(), std::move(fresh));
    if (known.size() > k_most_introduced) {
        known.resize(k_most_introduced);
    }
    // So many computers kept at most: the one introduced longest ago goes.
    while (introduced_.size() > k_most_introduced_computers) {
        auto oldest = introduced_.end();
        qint64 oldest_ms = -1;
        for (auto it = introduced_.begin(); it != introduced_.end(); ++it) {
            const auto ms = it->second.empty() ? std::numeric_limits<qint64>::max() : it->second.front().since.elapsed();
            if (it->first != fingerprint && ms > oldest_ms) {
                oldest = it;
                oldest_ms = ms;
            }
        }
        introduced_lately_.erase(oldest->first);
        introduced_.erase(oldest);
    }
    qCDebug(lc_reach).noquote() << QString::fromStdString(by.substr(0, 8)) << "says"
                                << QString::fromStdString(fingerprint.substr(0, 8)) << "is at"
                                << QString::fromStdString(address);
    soon_.start();
}

std::vector<std::string> Reach::introduced_at(const std::string& fingerprint) const
{
    std::vector<std::string> out;
    if (const auto known = introduced_.find(fingerprint); known != introduced_.end()) {
        for (const auto& i : known->second) {
            if (i.since.elapsed() < options_.introduced_for_ms) {
                out.push_back(i.address);
            }
        }
    }
    return out;
}

bool Reach::close_by(const Peer& peer) const
{
    if (!peer.key()) {
        return false;
    }
    const auto heard = heard_.find(peer.key()->fingerprint());
    return heard != heard_.end() && heard_at(heard->second, peer.address());
}

void Reach::start()
{
    started_.start();
    timer_.start();
    check();
    if (options_.startup_wait_ms > 0) {
        // Not early: a coarse timer may be, and then nothing goes until the next look.
        QTimer::singleShot(options_.startup_wait_ms, Qt::PreciseTimer, this, &Reach::check);
    }
}

bool Reach::local(const QHostAddress& address) const
{
    return options_.is_local ? options_.is_local(address) : is_local_address(address);
}

bool Reach::local(const std::string& address) const
{
    const auto at = sync::parse_address(address);
    const QHostAddress host(at ? QString::fromStdString(at->host) : QString());
    return !host.isNull() && local(host);
}

void Reach::set_typed_addresses(const std::string& fingerprint, std::vector<std::string> addresses)
{
    std::vector<std::string> fresh;
    for (auto& a : addresses) {
        if (!a.empty() && std::ranges::find(fresh, a) == fresh.end()) {
            fresh.push_back(std::move(a));
        }
    }
    // What was typed before and is not now is out of date, not to be gone back to.
    if (const auto it = typed_.find(fingerprint); it != typed_.end()) {
        if (const auto r = remembered_.find(fingerprint); r != remembered_.end()) {
            std::erase_if(r->second, [&](const std::string& a) {
                return std::ranges::find(it->second, a) != it->second.end()
                       && std::ranges::find(fresh, a) == fresh.end();
            });
        }
    }
    for (const auto& a : fresh) {
        retries_.erase(a);
    }
    if (fresh.empty()) {
        typed_.erase(fingerprint);
    } else {
        typed_[fingerprint] = std::move(fresh);
    }
    // Nothing already on its way holds this up, nor brings back what was
    // cleared, by being remembered where it got through.
    for (auto it = attempts_.begin(); it != attempts_.end();) {
        auto* peer = it->first;
        if (it->second.fingerprint == fingerprint && !peer->ready()) {
            it = attempts_.erase(it);
            peer->close(QStringLiteral("An address was typed for it"));
        } else {
            ++it;
        }
    }
    save();
    soon_.start();
}

std::vector<std::string> Reach::typed_addresses(const std::string& fingerprint) const
{
    const auto it = typed_.find(fingerprint);
    return it == typed_.end() ? std::vector<std::string>{} : it->second;
}

void Reach::reach_closer(const std::string& fingerprint, const crypto::PublicKey& key, const Peer& peer, Heard& heard)
{
    if (peer.replaces() || heard_at(heard, peer.address())) {
        heard.away.invalidate();
        return;
    }
    if (!heard.away.isValid()) {
        heard.away.start();
    }
    // As for a first connection: the lower key reaches out, the other only if
    // that does not come.
    const auto address = address_of(heard.address, heard.port);
    if (being_tried(address) || !may_try(address) || (fingerprint_ > fingerprint && heard.away.elapsed() < options_.lan_grace_ms)) {
        return;
    }
    qCDebug(lc_reach).noquote() << "a closer way to" << QString::fromStdString(fingerprint.substr(0, 8))
                                << "at" << QString::fromStdString(address);
    attempting(link_.connect_instead(heard.address.toString(), heard.port, key), fingerprint, address, address);
}

bool Reach::being_tried(const std::string& address) const
{
    return std::ranges::any_of(attempts_, [&](const auto& attempt) { return attempt.second.at == address; });
}

bool Reach::heard_at(const Heard& heard, const QHostAddress& address) const
{
    return std::ranges::any_of(heard.lately, [&](const HeardAt& at) {
        return at.last.elapsed() < options_.heard_fresh_ms && at.address.isEqual(address);
    });
}

void Reach::attempting(Peer* peer, const std::string& fingerprint, const std::string& address, const std::string& at)
{
    // Refused at once (no route: the network down, a Mac just awake), it
    // closed inside connect_to, before it could be noted here, so peer_closed
    // found nothing to take out. Noted, it would stay after it is deleted, and
    // the next check would use it. Tried, and not reached.
    if (peer == nullptr || peer->is_closed()) {
        tried(at, false);
        return;
    }
    auto& attempt = attempts_[peer];
    attempt = {fingerprint, address, at, {}};
    attempt.since.start();
}

std::optional<std::vector<std::string>> Reach::addresses_at(const std::string& address)
{
    const auto at = sync::parse_address(address);
    if (!at) {
        return std::vector<std::string>{};
    }
    const auto host = QString::fromStdString(at->host);
    if (!QHostAddress(host).isNull()) {
        return std::vector<std::string>{address};
    }
    auto& looked = looked_up_[at->host];
    // One not found is asked again sooner: the network may have been down.
    const auto stands_ms = looked.addresses.empty() ? options_.first_retry_ms : k_looked_up_ms;
    if ((!looked.since.isValid() || looked.since.elapsed() >= stands_ms) && !looked.asking) {
        looked.asking = true;
        const auto found = [this, name = at->host](const std::vector<QHostAddress>& addresses) {
            auto& entry = looked_up_[name];
            entry.addresses = addresses;
            entry.since.start();
            entry.asking = false;
            qCDebug(lc_reach).noquote() << "looked up" << QString::fromStdString(name) << ":" << addresses.size()
                                        << "address(es)";
            soon_.start();
        };
        if (options_.look_up) {
            found(options_.look_up(host));
        } else {
            QHostInfo::lookupHost(host, this, [found](const QHostInfo& info) {
                const auto addresses = info.addresses();
                found(std::vector<QHostAddress>(addresses.begin(), addresses.end()));
            });
        }
    }
    if (!looked.since.isValid()) {
        return std::nullopt;  // what it had stands meanwhile, once there is that
    }
    std::vector<std::string> out;
    for (const auto& a : looked.addresses) {
        out.push_back(address_of(a, at->port));
    }
    return out;
}

std::optional<std::string> Reach::to_try(const std::string& address, bool local_only)
{
    const auto addresses = addresses_at(address);
    if (!addresses) {
        return std::nullopt;
    }
    const auto it = std::ranges::find_if(*addresses, [&](const std::string& a) {
        return may_try(a) && (!local_only || local(a));
    });
    return it == addresses->end() ? std::string() : *it;
}

std::vector<Joining> Reach::joining_through(const crypto::PublicKey& key) const
{
    std::vector<Joining> out;
    for (const auto& lib : link_.libraries()) {
        if (sync::read_members(*lib.store, lib.uid).founded()) {
            continue;
        }
        const auto invite = sync::own_invite(*lib.store, lib.uid, link_.identity().machine);
        if (!invite || sync::invite_expired(invite->invite)) {
            continue;
        }
        const auto names = [&](const sync::ComputerAddress& c) { return c.key == key; };
        if (std::ranges::any_of(invite->invite.through, names) || (invite->invite.from && names(*invite->invite.from))) {
            out.push_back({lib.uid, invite->entry});
        }
    }
    return out;
}

void Reach::set_invite_addresses(const std::string& library, const std::string& invite, const std::string& expires,
                                 std::vector<std::string> addresses)
{
    std::vector<std::string> fresh;
    for (auto& a : addresses) {
        if (!a.empty() && std::ranges::find(fresh, a) == fresh.end()) {
            retries_.erase(a);
            fresh.push_back(std::move(a));
        }
    }
    const auto it = std::ranges::find_if(expected_, [&](const Expected& e) {
        return e.library == library && e.invite == invite;
    });
    if (fresh.empty()) {
        if (it != expected_.end()) {
            expected_.erase(it);
        }
    } else if (it != expected_.end()) {
        it->expires = expires;
        it->addresses = std::move(fresh);
    } else {
        expected_.push_back({library, invite, expires, std::move(fresh)});
    }
    save();
    soon_.start();
}

std::vector<std::string> Reach::invite_addresses(const std::string& library, const std::string& invite) const
{
    const auto it = std::ranges::find_if(expected_, [&](const Expected& e) {
        return e.library == library && e.invite == invite;
    });
    return it == expected_.end() ? std::vector<std::string>{} : it->addresses;
}

bool Reach::came_by_invite(const Peer& peer) const
{
    return std::ranges::any_of(expected_, [&](const Expected& e) { return peer.invited_by(e.library) == e.invite; });
}

bool Reach::got_through(const Attempt& attempt, const Peer& peer)
{
    if (!peer.ready()) {
        return false;
    }
    // Reached out to for whoever was invited there: something else answering
    // (a computer of the library, say) is no reason to try again at once.
    return !attempt.fingerprint.empty() || came_by_invite(peer);
}

bool Reach::all_reached() const
{
    return std::ranges::all_of(wanted(), [&](const auto& entry) {
        const auto* peer = link_.peer_with(entry.second.key);
        return peer != nullptr && peer->ready();
    });
}

void Reach::reach_expected()
{
    // Run out, used (the computer came, by this way or another) or withdrawn.
    const auto over = [&](const Expected& e) {
        if (sync::expired(e.expires)) {
            return true;
        }
        const auto lib = std::ranges::find(link_.libraries(), e.library, &SyncedLibrary::uid);
        if (lib == link_.libraries().end()) {
            return false;
        }
        const auto list = sync::read_members(*lib->store, lib->uid);
        return list.invites_used.contains(e.invite) || list.invites_withdrawn.contains(e.invite);
    };
    if (std::erase_if(expected_, over) > 0) {
        save();
    }
    for (const auto& e : expected_) {
        // Not held just now (it is taken up after this starts, or was let go
        // of): it waits, and goes at the latest when the invite runs out.
        if (!std::ranges::any_of(link_.libraries(), [&](const SyncedLibrary& lib) { return lib.uid == e.library; })) {
            continue;
        }
        // Here already, by this way or its own, until its join comes.
        if (std::ranges::any_of(link_.peers(), [&](const Peer* peer) {
                return peer->ready() && peer->invited_by(e.library) == e.invite;
            })) {
            continue;
        }
        // One address at a time, as for a computer of the list. Another
        // computer's attempt at an address holds nothing up: one replaced
        // here is still on the list, and would take every turn.
        const auto on_its_way = std::ranges::any_of(attempts_, [&](const auto& attempt) {
            return attempt.second.fingerprint.empty()
                   && std::ranges::find(e.addresses, attempt.second.address) != e.addresses.end();
        });
        if (on_its_way) {
            continue;
        }
        for (const auto& address : e.addresses) {
            const auto tried_at = to_try(address, false);
            if (!tried_at) {
                break;  // being looked up: it holds the turn
            }
            const auto at = sync::parse_address(*tried_at);
            if (!at) {
                continue;
            }
            qCDebug(lc_reach).noquote() << "reaching for a computer invited at" << QString::fromStdString(address)
                                        << "(" << QString::fromStdString(*tried_at) << ")";
            attempting(link_.connect_to(QString::fromStdString(at->host), at->port, std::nullopt), {}, address,
                       *tried_at);
            break;
        }
    }
}

std::map<std::string, Reach::Wanted> Reach::wanted() const
{
    std::map<std::string, Wanted> out;
    const auto want = [&](const crypto::PublicKey& key, const std::vector<std::string>& addresses) -> Wanted& {
        auto& w = out.try_emplace(key.fingerprint(), Wanted{key, {}, {}}).first->second;
        for (const auto& a : addresses) {
            if (std::ranges::find(w.addresses, a) == w.addresses.end()) {
                w.addresses.push_back(a);
            }
        }
        return w;
    };
    for (const auto& lib : link_.libraries()) {
        const auto list = sync::read_members(*lib.store, lib.uid);
        for (const auto& [machine, m] : list.members) {
            if (!m.removed && m.key.fingerprint() != fingerprint_) {
                (void)want(m.key, m.addresses);
            }
        }
        // Joining, while the library's own list has not come: through
        // whoever the invite names, with the invite.
        const auto invite = list.founded()
                                ? std::nullopt
                                : sync::own_invite(*lib.store, lib.uid, link_.identity().machine);
        if (invite && !sync::invite_expired(invite->invite)) {
            const Joining joining{lib.uid, invite->entry};
            auto through = invite->invite.through;
            if (invite->invite.from) {
                through.push_back(*invite->invite.from);
            }
            for (const auto& s : through) {
                if (s.key.fingerprint() != fingerprint_) {
                    want(s.key, s.addresses).joining.push_back(joining);
                }
            }
        }
    }
    return out;
}

void Reach::check()
{
    const auto wants = wanted();
    wanted_.clear();
    for (const auto& [fp, want] : wants) {
        wanted_.insert(fp);
    }
    forget_unheard();
    // An address that does not answer gives way to the others.
    for (auto& [peer, attempt] : std::map(attempts_)) {
        if (!peer->ready() && attempt.since.elapsed() >= options_.attempt_ms) {
            peer->close(QStringLiteral("No answer in time"));
        }
    }
    for (const auto& [fp, want] : wants) {
        const auto heard = heard_.find(fp);
        const bool on_network = heard != heard_.end() && heard->second.last.isValid()
                                && heard->second.last.elapsed() < options_.heard_fresh_ms;
        const bool close_by = on_network && local(heard->second.address);
        auto* peer = link_.peer_with(want.key);
        if (peer == nullptr || !peer->ready()) {
            if (heard != heard_.end()) {
                heard->second.away.invalidate();  // no connection some other way
            }
        }
        if (peer != nullptr) {
            for (const auto& j : want.joining) {
                peer->join(j);  // over the connection there is (once each)
            }
            if (peer->ready()) {
                if (heard != heard_.end()) {
                    heard->second.waiting.invalidate();
                }
                if (close_by) {
                    reach_closer(fp, want.key, *peer, heard->second);
                }
                continue;
            }
            if (!close_by || !give_way(fp, want, *peer, heard->second)) {
                continue;
            }
        }
        reach_for(fp, want, on_network ? &heard->second : nullptr);
    }
    reach_expected();
}

bool Reach::give_way(const std::string& fingerprint, const Wanted& want, Peer& peer, Heard& heard)
{
    const auto attempt = attempts_.find(&peer);
    if (attempt == attempts_.end() || local(attempt->second.at)) {
        return false;
    }
    if (!heard.waiting.isValid()) {
        heard.waiting.start();
    }
    // Nothing is held against the address it was trying. (Heard does not mean it
    // can be reached there: a firewall may let announcements through and not
    // connections.)
    const bool its_turn = fingerprint_ < fingerprint || !want.joining.empty() || heard.waiting.elapsed() >= options_.lan_grace_ms;
    if (!its_turn || !may_try(address_of(heard.address, heard.port))) {
        return false;
    }
    attempts_.erase(attempt);
    peer.close(QStringLiteral("Heard on the network"));
    // One from the other end may be on its way too: left to get here.
    return link_.peer_with(want.key) == nullptr;
}

void Reach::reach_for(const std::string& fingerprint, const Wanted& want, Heard* heard)
{
    std::vector<std::string> candidates;
    if (heard != nullptr) {
        if (!heard->waiting.isValid()) {
            heard->waiting.start();
        }
        // Of two that hear each other, the lower key reaches out; the
        // other waits for it, unless it does not come (it may not know
        // this computer yet). One joined through is never
        // waited for: it cannot know this computer yet.
        if (fingerprint_ > fingerprint && want.joining.empty() && heard->waiting.elapsed() < options_.lan_grace_ms) {
            return;
        }
        candidates.push_back(address_of(heard->address, heard->port));
    }
    if (const auto typed = typed_.find(fingerprint); typed != typed_.end()) {
        candidates.insert(candidates.end(), typed->second.begin(), typed->second.end());
    }
    for (auto& address : introduced_at(fingerprint)) {
        candidates.push_back(std::move(address));
    }
    if (const auto r = remembered_.find(fingerprint); r != remembered_.end()) {
        candidates.insert(candidates.end(), r->second.begin(), r->second.end());
    }
    candidates.insert(candidates.end(), want.addresses.begin(), want.addresses.end());
    const bool starting = started_.isValid() && started_.elapsed() < options_.startup_wait_ms;
    for (const auto& address : candidates) {
        const auto tried_at = to_try(address, starting);
        if (!tried_at) {
            return;  // being looked up: one address at a time, and it comes first
        }
        const auto at = sync::parse_address(*tried_at);
        if (!at) {
            continue;
        }
        qCDebug(lc_reach).noquote() << "reaching" << QString::fromStdString(fingerprint.substr(0, 8)) << "at"
                                    << QString::fromStdString(address) << "(" << QString::fromStdString(*tried_at) << ")";
        attempting(link_.connect_to(QString::fromStdString(at->host), at->port, want.key, want.joining), fingerprint,
                   address, *tried_at);
        break;  // one address at a time
    }
}

void Reach::heard(const std::string& fingerprint, const QHostAddress& address, quint16 port)
{
    if (fingerprint == fingerprint_) {
        return;
    }
    if (!heard_.contains(fingerprint)) {
        forget_unheard(1);
    }
    auto& h = heard_[fingerprint];
    const bool fresh = !h.last.isValid() || h.last.elapsed() >= options_.heard_fresh_ms
                       || h.address != address || h.port != port;
    h.address = address;
    h.port = port;
    h.last.start();
    h.when = QDateTime::currentDateTimeUtc();
    std::erase_if(h.lately, [&](const HeardAt& at) {
        return at.address.isEqual(address) || at.last.elapsed() >= options_.heard_fresh_ms;
    });
    h.lately.push_back({address, {}});
    h.lately.back().last.start();
    if (h.lately.size() > k_most_heard_at) {
        h.lately.erase(h.lately.begin());
    }
    if (fresh) {
        soon_.start();
    }
}

void Reach::forget_unheard(std::size_t room)
{
    // One not heard for this long is not on the network, as one never heard.
    std::erase_if(heard_, [&](const auto& entry) {
        return !entry.second.last.isValid() || entry.second.last.elapsed() >= options_.heard_fresh_ms;
    });
    // Anyone on the network may announce any number of keys: only those of
    // strangers are let go for one more.
    auto kept = wanted_;
    for (const auto* peer : link_.peers()) {
        if (peer->key()) {
            kept.insert(peer->key()->fingerprint());
        }
    }
    std::vector<std::map<std::string, Heard>::iterator> strangers;
    for (auto it = heard_.begin(); it != heard_.end(); ++it) {
        if (!kept.contains(it->first)) {
            strangers.push_back(it);
        }
    }
    while (!strangers.empty() && strangers.size() + room > k_most_heard) {
        const auto oldest = std::ranges::min_element(
            strangers, [](const auto& a, const auto& b) { return a->second.last < b->second.last; });
        heard_.erase(*oldest);
        strangers.erase(oldest);
    }
}

std::vector<Reach::Neighbour> Reach::neighbours() const
{
    std::vector<Neighbour> out;
    for (const auto& [fp, h] : heard_) {
        if (h.last.isValid() && h.last.elapsed() < options_.heard_fresh_ms) {
            out.push_back({fp, h.address, h.port, h.when});
        }
    }
    return out;
}

std::vector<std::string> Reach::remembered(const std::string& fingerprint) const
{
    const auto it = remembered_.find(fingerprint);
    return it == remembered_.end() ? std::vector<std::string>{} : it->second;
}

bool Reach::may_try(const std::string& address) const
{
    const auto it = retries_.find(address);
    return it == retries_.end() || it->second.since.elapsed() >= it->second.wait_ms;
}

void Reach::tried(const std::string& address, bool reached)
{
    if (reached) {
        retries_.erase(address);
        return;
    }
    auto& r = retries_[address];
    r.wait_ms = r.wait_ms == 0 ? options_.first_retry_ms
                               : std::min<qint64>(r.wait_ms * 2, options_.longest_retry_ms);
    r.since.start();
}

void Reach::reached(Peer* peer)
{
    if (!peer->key()) {
        return;
    }
    const auto fp = peer->key()->fingerprint();
    if (const auto heard = heard_.find(fp); heard != heard_.end()) {
        heard->second.waiting.invalidate();
    }
    const auto it = attempts_.find(peer);
    if (it == attempts_.end()) {
        return;
    }
    tried(it->second.at, got_through(it->second, *peer));
    // Where it was reached is where to look first next time.
    auto& addresses = remembered_[fp];
    std::erase(addresses, it->second.address);
    addresses.insert(addresses.begin(), it->second.address);
    if (addresses.size() > k_most_remembered) {
        addresses.resize(k_most_remembered);
    }
    attempts_.erase(it);
    save();
}

void Reach::load()
{
    if (file_.empty()) {
        return;
    }
    const auto text = util::read_file(file_);
    if (!text) {
        return;
    }
    const auto j = json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("computers") || !j["computers"].is_object()) {
        return;
    }
    // The addresses of each, "host:port" each; one alone as an earlier
    // SeventhSlide kept it.
    const auto addresses_in = [](const json& kept) {
        std::vector<std::string> out;
        const auto take = [&](const json& a) {
            if (a.is_string() && sync::parse_address(a.get<std::string>()) && out.size() < sync::k_most_addresses) {
                out.push_back(a.get<std::string>());
            }
        };
        if (kept.is_array()) {
            std::ranges::for_each(kept, take);
        } else {
            take(kept);
        }
        return out;
    };
    if (const auto typed = j.find("typed"); typed != j.end() && typed->is_object()) {
        for (const auto& [fp, addresses] : typed->items()) {
            if (auto kept = addresses_in(addresses); sync::is_fingerprint(fp) && !kept.empty()) {
                typed_[fp] = std::move(kept);
            }
        }
    }
    if (const auto invites = j.find("invites"); invites != j.end() && invites->is_array()) {
        for (const auto& e : *invites) {
            if (!e.is_object() || !e.value("library", json()).is_string() || !e.value("invite", json()).is_string()
                || !e.value("expires", json()).is_string()) {
                continue;
            }
            if (auto kept = addresses_in(e.value("addresses", json())); !kept.empty()) {
                expected_.push_back({e["library"].get<std::string>(), e["invite"].get<std::string>(),
                                     e["expires"].get<std::string>(), std::move(kept)});
            }
        }
    }
    // As an earlier SeventhSlide kept them: an address each, an invite's in
    // turn. Those that name no invite cannot be told used, and are let go.
    if (const auto expected = j.find("expected"); expected != j.end() && expected->is_array()) {
        for (const auto& e : *expected) {
            if (!e.is_object() || !e.value("library", json()).is_string() || !e.value("invite", json()).is_string()
                || !e.value("expires", json()).is_string() || e["invite"].get<std::string>().empty()) {
                continue;
            }
            auto kept = addresses_in(e.value("address", json()));
            if (kept.empty()) {
                continue;
            }
            const auto library = e["library"].get<std::string>();
            const auto invite = e["invite"].get<std::string>();
            const auto same = std::ranges::find_if(
                expected_, [&](const Expected& x) { return x.library == library && x.invite == invite; });
            if (same == expected_.end()) {
                expected_.push_back({library, invite, e["expires"].get<std::string>(), std::move(kept)});
            } else if (std::ranges::find(same->addresses, kept.front()) == same->addresses.end()
                       && same->addresses.size() < sync::k_most_addresses) {
                same->addresses.push_back(kept.front());
            }
        }
    }
    for (const auto& [fp, addresses] : j["computers"].items()) {
        if (!sync::is_fingerprint(fp) || !addresses.is_array()) {
            continue;
        }
        auto& kept = remembered_[fp];
        for (const auto& a : addresses) {
            if (a.is_string() && sync::parse_address(a.get<std::string>())
                && kept.size() < k_most_remembered) {
                kept.push_back(a.get<std::string>());
            }
        }
    }
}

void Reach::save() const
{
    if (file_.empty()) {
        return;
    }
    json invites = json::array();
    for (const auto& e : expected_) {
        invites.push_back(
            {{"library", e.library}, {"invite", e.invite}, {"expires", e.expires}, {"addresses", e.addresses}});
    }
    if (!util::write_file_atomically(file_, json{{"format", k_file_format},
                                                 {"computers", remembered_},
                                                 {"typed", typed_},
                                                 {"invites", std::move(invites)}}
                                                .dump(2))) {
        qCWarning(lc_reach).noquote() << "Could not write" << QString::fromStdString(file_.string());
    }
}

}  // namespace seventhslide::link
