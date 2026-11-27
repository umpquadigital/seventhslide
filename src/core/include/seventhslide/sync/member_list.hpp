#pragma once

// The member list: who belongs to a synced library, and what each may do. It
// is the library's own signed log, kept in the store (``Numbered::Members``):
// each computer writes numbered entries of its own, and every computer
// resolves the same list from the union of them. Nothing here is decided by
// "the later change wins" on a record.
//
// The first entry founds the library: the founding computer, the admin key
// and the recovery key. Every later entry is signed by the computer that
// wrote it, which alone may take itself off the list or give itself a new
// name, and, for admin work, by the admin key: a computer joining by an
// invite (signed by the admin key when the invite was made, so nobody need
// be online), an invite made or withdrawn, a role changed, a viewer given
// the locked admin key or not, a computer removed, the size limit set, the recovery key replaced, or the
// admin key replaced: by the old admin key (a new password; the recovery code
// stands) or by the recovery key, which may sign nothing else (a forgotten
// password; the code is spent, and a new one is made).
//
// **Before and after are facts, not claims.** Each entry names what its
// writer had of the list when it wrote it (``seen``: the last entry of each
// other computer; its own earlier ones it had too). An entry is resolved
// once everything it saw is here; until then it waits. Entries are taken in
// an order where each comes after everything it saw (two made apart: by
// machine id, then number), each checked against the list as it stands
// when reached. Neither a clock nor a counter a computer chose decides
// anything.
//
// **What was made apart from a change of authority does not count.** A
// removal cancels the removed computer's entries it had not seen; a new
// admin key cancels entries signed with the old one that it had not seen;
// likewise a new recovery key. So whoever kept an old password, or a
// removed laptop, cannot slip an entry in by claiming to have written it
// earlier. Where two such changes each cancel the other (made apart), the
// recovery code's wins, then a removal, then a new password; between two of
// a kind, the one from the lower machine id. A computer holding a current
// editor's key and the password can always do admin work; changing the
// password keeps someone out only together with removing their computers.
//
// **An invite admits one computer.** A join on an invite an earlier join
// used (one it had seen) is refused; two computers using one invite while
// apart are both admitted, for the password to remove one. An invite's
// expiry is checked when the joining computer is let in (by the computer
// letting it in, by its own clock), not here: the time an entry gives is
// its writer's say.
//
// **An invite made is said on the list**, without its secret: its id, role
// and expiry, for every member to show it waiting, to reach out with it where
// someone said the computer joining is, and for an admin to withdraw it. The
// invite has not seen that entry, so its join waits for nothing the computer
// that made it has not passed on: a join placed before it leaves it used, not
// waiting. A withdrawal lets no
// computer in by the invite from then on, and cancels a join by it that the
// withdrawal had not seen, as a removal would: whoever withdrew it had not
// seen that computer come. One made by an earlier SeventhSlide is not said,
// so it is not shown, and only changing the password stops it.
//
// **An invite's secret stays with whoever was given it.** An invite names a
// one-time key (``Invite::proof``) the admin key signed; the secret half is
// only in the code pasted (``invite_code``). A join entry is signed with it
// as well as by the computer joining, and that covers the machine and key the
// entry adds. So the entry can be passed around, and the signed invite in it
// read by every member, and neither lets anyone else join. An invite without
// a proof is refused, and so is any join on it.
//
// A computer's name entry may carry its picture (``Picture``), shown beside
// what it says and does in the shared log: a later name entry without one
// takes it away. A picture that is not one is no picture; it refuses nothing.
//
// A member may also say where it takes connections (``set_addresses``), no
// password needed, as for its name: an invite names every member that has, for
// the computer joining to join through, and every member reaches the others
// there. The key is checked wherever a computer connects, so what a member
// says gets it only a computer's attempts.
//
// A computer always on, which keeps a library for the others, is a viewer
// unless it is wanted to edit: its key, the one most exposed, then changes
// nothing.
//
// Refused entries are kept with the reason, for the operator.
//
// Seam: the crypto module, nlohmann/json; Qt Core for the clock.

#include "seventhslide/crypto/password_box.hpp"
#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/sync/identity.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::sync {

inline constexpr int k_member_format = 1;

enum class Role { Editor, Viewer };

[[nodiscard]] std::string_view role_name(Role role);
[[nodiscard]] std::optional<Role> role_from_name(std::string_view name);

/// Every change file of a computer counts.
inline constexpr std::int64_t k_all_files = std::numeric_limits<std::int64_t>::max();

/// An invite made, as the list says it (``MemberLog::invite``): not the
/// invite itself, which only whoever was given the code has.
struct InviteMade {
    std::string id;
    Role role = Role::Editor;
    /// ISO 8601, UTC: when it runs out.
    std::string expires;
    /// The computer that made it.
    std::string made_by;

    friend bool operator==(const InviteMade&, const InviteMade&) = default;
};

/// A computer's picture: a glyph on a colour, or an image of its own; none
/// (empty) for its initials.
struct Picture {
    /// A glyph's name, as the app names them ("song").
    std::string glyph;
    /// "#rrggbb".
    std::string colour;
    /// A PNG, in base64, no larger than ``k_largest_picture``.
    std::string image;

    [[nodiscard]] bool empty() const noexcept { return glyph.empty() && image.empty(); }
    friend bool operator==(const Picture&, const Picture&) = default;
};
/// The most base64 a picture's image takes.
inline constexpr std::size_t k_largest_picture = 48 * 1024;

[[nodiscard]] nlohmann::json picture_to_json(const Picture& picture);
/// ``j`` as a picture; none for anything that is not one.
[[nodiscard]] Picture picture_from_json(const nlohmann::json& j);

struct Member {
    std::string machine;
    crypto::PublicKey key;
    std::string name;
    Picture picture;
    Role role = Role::Editor;
    /// Where it takes connections, "host:port" each (``parse_address``).
    std::vector<std::string> addresses;
    bool removed = false;
    /// How many of its change files count, from the first: all for an
    /// editor; for a viewer or a removed computer, those the computer that
    /// made it one had (and any later ones another computer's files were made
    /// on top of: ``Vouches``).
    std::int64_t files_kept = k_all_files;
    /// A viewer given a copy of the locked admin key (``set_key_holder``),
    /// for admin work with the password on that computer. An editor always
    /// has one. Taking it away stops only what is sent from then on.
    bool holds_admin_key = false;
    /// Whether it has ever been an editor. A viewer that has may hold edits
    /// made then, which go in its change files and count if it is made an
    /// editor again; one that has not has nothing of its own.
    bool has_edited = false;

    friend bool operator==(const Member&, const Member&) = default;
};

/// The list as it stands: what the entries resolve to.
struct Members {
    std::string library;
    /// The admin key, once the library is founded.
    std::optional<crypto::PublicKey> admin;
    std::optional<crypto::PublicKey> recovery;
    /// In bytes; 0 for none.
    std::int64_t size_limit = 0;
    /// By machine id, removed ones too.
    std::map<std::string, Member> members;
    /// The last entry here of each computer, counting only those with all
    /// their earlier ones here: what a new entry has seen.
    std::map<std::string, std::int64_t> heads;

    struct Entry {
        std::string machine;
        std::int64_t sequence = 0;
    };
    struct Refused {
        std::string machine;
        std::int64_t sequence = 0;
        std::string why;
    };
    std::vector<Refused> refused;
    /// Entries waiting for an entry they saw, not here yet.
    std::vector<Entry> waiting;
    /// The ids of the invites a computer has joined by. Such an invite lets
    /// in no other computer: not a code seen since, in a chat or a photo.
    std::set<std::string> invites_used;
    /// The invites made and neither used nor withdrawn, by id, since the admin
    /// key last changed (one signed with an earlier key admits no one). Those
    /// run out too, as the list goes by no clock (``waiting_invites``).
    std::map<std::string, InviteMade> invites;
    /// The ids of the invites withdrawn. Such an invite lets in no computer.
    std::set<std::string> invites_withdrawn;

    [[nodiscard]] bool founded() const noexcept { return admin.has_value(); }
    [[nodiscard]] const Member* find(std::string_view machine) const;
    /// Whether ``machine``'s change file ``sequence`` counts by the list
    /// alone: an editor's always, a viewer's or a removed computer's up to
    /// its ``files_kept``.
    [[nodiscard]] bool may_have_written(std::string_view machine, std::int64_t sequence) const;
    /// The member whose key is ``key``, removed or not; null for none.
    [[nodiscard]] const Member* with_key(const crypto::PublicKey& key) const;
    /// Whether ``machine`` may write snapshots: an editor, not removed.
    [[nodiscard]] bool may_write(std::string_view machine) const;
};

/// An address as the member list keeps one: "host:port", or
/// "[v6 address]:port".
struct HostPort {
    std::string host;
    std::uint16_t port = 0;

    friend bool operator==(const HostPort&, const HostPort&) = default;
};
[[nodiscard]] std::optional<HostPort> parse_address(std::string_view address);
/// The most addresses one computer gives.
inline constexpr std::size_t k_most_addresses = 16;

/// A computer as an invite names it: enough to reach it and know it.
struct ComputerAddress {
    std::string machine;
    crypto::PublicKey key;
    std::vector<std::string> addresses;

    friend bool operator==(const ComputerAddress&, const ComputerAddress&) = default;
};

/// What an invite says. Signed by the admin key, it is the secret itself:
/// whoever has it may join once as ``role``, until it expires.
struct Invite {
    std::string library;
    Role role = Role::Editor;
    /// ISO 8601, UTC: "2026-11-01T00:00:00Z".
    std::string expires;
    /// Random, so two invites are told apart.
    std::string id;
    /// The members that take connections, to join through.
    std::vector<ComputerAddress> through;
    /// The member list as the computer that made it had it (each computer's
    /// last entry): the join entry has seen these, so it comes after all of
    /// them, wherever the joining computer's id falls among the others',
    /// and is judged by the admin key that signed the invite.
    std::map<std::string, std::int64_t> seen;
    /// What the library is called on the computer that made it, for the
    /// one joining to call it too.
    std::string name;
    /// The computer that made it, and where it takes connections: one to
    /// join through when no member publishes an address, on the church
    /// network.
    std::optional<ComputerAddress> from;
    /// The public half of the one-time key whose secret half is in the code
    /// pasted, and only there. Set by ``invite_code``.
    crypto::PublicKey proof;

    [[nodiscard]] nlohmann::json to_json() const;
    /// Throws ``std::invalid_argument`` for anything but an invite.
    [[nodiscard]] static Invite from_json(const nlohmann::json& j);

    friend bool operator==(const Invite&, const Invite&) = default;
};

/// An invite as it was signed: the invite, the admin key that signed it
/// (checked against the list when the join entry is resolved), and the
/// signed text itself, which the join entry carries as it is.
struct SignedInvite {
    Invite invite;
    crypto::PublicKey admin;
    std::string sealed;
    /// The one-time key (``Invite::proof``), when this came from the code
    /// pasted; none when it came from a join entry.
    std::optional<crypto::SigningKey> secret;
    /// The join entry it came from (``own_invite``, ``proved_invite``).
    std::string entry;
};

/// ``invite``, with a new one-time key, signed by ``admin``, as the code to
/// paste or scan.
[[nodiscard]] std::string invite_code(Invite invite, const crypto::SigningKey& admin);
/// A code to paste, or the signed text a join entry carries. Throws
/// ``std::runtime_error`` for anything but a signed invite.
[[nodiscard]] SignedInvite read_invite(std::string_view code);
/// The invite in the join entry ``entry``, its proof not looked at. Throws
/// ``std::runtime_error`` for anything but a join entry.
[[nodiscard]] SignedInvite join_entry_invite(std::string_view entry);
/// The invite to ``library`` that the join entry ``entry`` proves the computer
/// with key ``computer`` holds; none when it does not. The entry is how a
/// computer not on the list yet shows it was invited: it can be seen by anyone
/// and gets no other computer in.
[[nodiscard]] std::optional<SignedInvite> proved_invite(std::string_view entry, std::string_view library,
                                                        const crypto::PublicKey& computer);

/// Every entry in ``store``, resolved. Cheap to ask again: an entry is
/// read and checked once, and the list resolved again only when entries
/// came or went (``SyncStore::member_cache``); nothing need keep it.
[[nodiscard]] Members read_members(SyncStore& store, std::string_view library);

/// The invite computer ``machine`` joined ``library`` by, from its own join
/// entry in ``store``: what to reach the library through (the members it
/// names, the computer that made it) until the library's own list has come. None when
/// ``machine`` has written no join entry there.
[[nodiscard]] std::optional<SignedInvite> own_invite(SyncStore& store, std::string_view library,
                                                     std::string_view machine);
/// Whether computer ``machine`` joined ``library`` rather than founding it: it
/// wrote a join entry in ``store``, whether or not that counts by today's
/// rules (an invite from before invites had to prove themselves does not).
[[nodiscard]] bool joined_here(SyncStore& store, std::string_view library, std::string_view machine);
/// Whether ``invite`` has run out, by this computer's clock.
[[nodiscard]] bool invite_expired(const Invite& invite);
[[nodiscard]] bool invite_expired(const InviteMade& invite);
/// The invites of ``list`` still waiting for their computer: neither used nor
/// withdrawn, nor run out by this computer's clock. The soonest to run out first.
[[nodiscard]] std::vector<InviteMade> waiting_invites(const Members& list);

/// Writing entries to this computer's own place in the store. Each call
/// writes one entry, numbered after this computer's last, having seen every
/// entry here.
class MemberLog {
public:
    MemberLog(SyncStore& store, Identity me, std::string library);

    [[nodiscard]] Members members() const;

    /// A new admin key, unlocked, and the recovery code to write down
    /// (empty when the code stands: a password changed with the password).
    struct Founded {
        crypto::SigningKey admin;
        std::string recovery_code;
    };

    /// Found the library on this computer: a new admin key locked under
    /// ``passphrase``, a new recovery key, and this computer its first
    /// editor. Throws ``std::logic_error`` when the list here is founded
    /// already, and ``std::invalid_argument`` for a passphrase
    /// ``passphrase_problem`` refuses.
    Founded found(std::string_view passphrase, crypto::BoxParams params = crypto::k_box_params);

    /// Join by ``invite``: the entry adding this computer, for the admin
    /// signature on the invite to admit. It has seen what the invite was
    /// made from, so it waits here for those entries, which come from the
    /// computer letting this one in. Throws ``std::runtime_error`` when
    /// the invite is for another library, expired by this computer's clock
    /// (the computer letting this one in checks it again by its own), or
    /// has no one-time key with it (not a code as it was pasted).
    void join(const SignedInvite& invite);

    /// Make ``target`` an editor or a viewer. A viewer's change files from
    /// here on do not count. Made a viewer, it holds no admin key until given
    /// one (``set_key_holder``).
    void set_role(std::string_view target, Role role, const crypto::SigningKey& admin);
    /// Give viewer ``target`` a copy of the locked admin key from now on, or
    /// send it no more. A copy it has already stays until the password
    /// changes.
    void set_key_holder(std::string_view target, bool holds, const crypto::SigningKey& admin);
    /// Remove ``target`` for good; its change files written so far count.
    void remove(std::string_view target, const crypto::SigningKey& admin);
    /// Take this computer off the list, for good, as ``remove`` would: no
    /// password is needed to remove oneself. Throws ``std::runtime_error``
    /// when it is not a member.
    void leave();
    /// Give this computer ``name`` on the list, and ``picture`` (none for
    /// none), for every computer to show: no password, as a computer only
    /// ever names itself. Throws
    /// ``std::runtime_error`` when it is not a member.
    void rename(std::string_view name, const Picture& picture = {});
    /// Say where this computer takes connections (``addresses``, each
    /// "host:port"; none to say it takes none): no password, as for its
    /// name. Throws ``std::runtime_error`` when it is not a member, and
    /// ``std::invalid_argument`` for an address that is not one, or more
    /// than ``k_most_addresses``.
    void set_addresses(const std::vector<std::string>& addresses);
    void set_size_limit(std::int64_t bytes, const crypto::SigningKey& admin);
    /// A new admin key under a new passphrase: changing the password
    /// (``signer`` the admin key, unlocked by the old one; the recovery code
    /// stands) or recovering it (``signer`` the recovery key, from the code;
    /// the code is spent, and a new one made). The old admin key signs
    /// nothing after this.
    Founded replace_admin(std::string_view passphrase, const crypto::SigningKey& signer,
                          crypto::BoxParams params = crypto::k_box_params);
    /// A new recovery key (the paper was lost); its code to write down.
    std::string replace_recovery(const crypto::SigningKey& admin);

    /// The admin key, unlocked by ``passphrase``; ``std::nullopt`` for the
    /// wrong one. Throws ``std::runtime_error`` when this computer holds no
    /// locked copy (a viewer not given one, or a library not founded).
    [[nodiscard]] std::optional<crypto::SigningKey> unlock_admin(std::string_view passphrase) const;

    /// An invite for ``role``, good until ``expires``, naming the members
    /// on the list that take connections, the library's ``name`` and, when given, the computer making
    /// it (``from``); as the code to pass on. It is said on the list too
    /// (``Members::invites``), in an entry it has not seen.
    [[nodiscard]] std::string invite(Role role, std::string_view expires,
                                     const crypto::SigningKey& admin, std::string_view name = {},
                                     const std::optional<ComputerAddress>& from = std::nullopt);
    /// Withdraw invite ``id``: no computer joins by it from now on, and a join
    /// by it that this computer had not seen does not count.
    void withdraw_invite(std::string_view id, const crypto::SigningKey& admin);

private:
    nlohmann::json body(std::string_view kind) const;
    /// ``target``'s change files here, numbered from 1 with none missing
    /// (those let go of as old counting as here).
    std::int64_t files_here(std::string_view target) const;
    void write(const nlohmann::json& body,
               const std::vector<std::pair<std::string_view, const crypto::SigningKey*>>& signers);

    SyncStore& store_;
    Identity me_;
    std::string library_;
};

}  // namespace seventhslide::sync
