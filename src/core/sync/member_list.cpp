#include "seventhslide/sync/member_list.hpp"

#include "seventhslide/crypto/encoding.hpp"
#include "seventhslide/crypto/random.hpp"
#include "seventhslide/sync/admin_key.hpp"
#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/now_utc.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

namespace seventhslide::sync {
namespace {

using nlohmann::json;

constexpr const char* k_what = "A sync member list entry";

/// Why ``j`` is not what the entry says it is, as a reason to refuse it.
struct Refusal : std::runtime_error {
    using std::runtime_error::runtime_error;
};

const json& field(const json& j, const char* name)
{
    const auto it = j.find(name);
    if (it == j.end()) {
        throw Refusal(std::string("no ") + name);
    }
    return *it;
}

std::string text_of(const json& j, const char* name)
{
    const auto& v = field(j, name);
    if (!v.is_string()) {
        throw Refusal(std::string(name) + " is not text");
    }
    return v.get<std::string>();
}

std::int64_t number_of(const json& j, const char* name)
{
    const auto& v = field(j, name);
    if (!v.is_number_integer() || v.get<std::int64_t>() < 0) {
        throw Refusal(std::string(name) + " is not a whole number");
    }
    return v.get<std::int64_t>();
}

crypto::PublicKey key_of(const json& j, const char* name)
{
    auto key = crypto::PublicKey::from_base64(text_of(j, name));
    if (!key) {
        throw Refusal(std::string(name) + " is not a key");
    }
    return *key;
}

Role role_of(const json& j, const char* name)
{
    const auto role = role_from_name(text_of(j, name));
    if (!role) {
        throw Refusal(std::string(name) + " is not a role");
    }
    return *role;
}

/// Throws ``std::invalid_argument`` for what ``addresses_of`` would refuse.
void check_addresses(const std::vector<std::string>& addresses)
{
    if (addresses.size() > k_most_addresses
        || !std::ranges::all_of(addresses, [](const std::string& a) { return parse_address(a).has_value(); })) {
        throw std::invalid_argument("Not addresses a computer can be reached at");
    }
}

std::vector<std::string> addresses_of(const json& j)
{
    std::vector<std::string> out;
    const auto it = j.find("addresses");
    if (it == j.end()) {
        return out;
    }
    if (!it->is_array()) {
        throw Refusal("addresses is not a list");
    }
    if (it->size() > k_most_addresses) {
        throw Refusal("too many addresses");
    }
    for (const auto& a : *it) {
        if (!a.is_string() || !parse_address(a.get<std::string>())) {
            throw Refusal("an address is not one");
        }
        out.push_back(a.get<std::string>());
    }
    return out;
}

/// The computer an entry adds: ``{"machine", "key", "name"}``.
Member computer_of(const json& j)
{
    const auto& c = field(j, "computer");
    if (!c.is_object()) {
        throw Refusal("computer is not an object");
    }
    Member m;
    m.machine = text_of(c, "machine");
    if (!is_machine_id(m.machine)) {
        throw Refusal("computer's machine id is not one");
    }
    m.key = key_of(c, "key");
    if (const auto name = c.find("name"); name != c.end()) {
        if (!name->is_string()) {
            throw Refusal("computer's name is not text");
        }
        m.name = name->get<std::string>();
    }
    return m;
}

bool is_glyph_name(std::string_view name)
{
    return !name.empty() && name.size() <= 32 && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-';
    });
}

bool is_colour(std::string_view colour)
{
    return colour.size() == 7 && colour[0] == '#' && std::ranges::all_of(colour.substr(1), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

bool is_base64(std::string_view text)
{
    return !text.empty() && text.size() <= k_largest_picture && text.size() % 4 == 0
           && std::ranges::all_of(text, [](char c) {
                  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+'
                         || c == '/' || c == '=';
              });
}

json computer_json(const Member& m)
{
    return {{"machine", m.machine}, {"key", m.key.base64()}, {"name", m.name}};
}

/// A member list entry as the store holds it.
struct StoredEntry {
    std::string machine;
    std::int64_t sequence = 0;
    std::string text;
};

/// An entry read and checked for shape, waiting its turn.
struct Parsed {
    std::string machine;
    std::int64_t sequence = 0;
    std::string kind;
    /// The last entry of each other computer its writer had.
    std::map<std::string, std::int64_t> seen;
    Envelope envelope;

    // Placed among the others (``order``):
    std::int64_t depth = 0;
    /// The last entry of every computer before it, its own included.
    std::map<std::string, std::int64_t> clock;
};

Parsed parse(std::string_view library, const StoredEntry& entry)
{
    Parsed p;
    p.machine = entry.machine;
    p.sequence = entry.sequence;
    p.envelope = unseal(entry.text, k_what);
    const auto& b = p.envelope.body;
    if (number_of(b, "format") > k_member_format) {
        throw Refusal("written by a newer SeventhSlide");
    }
    if (text_of(b, "library") != library) {
        throw Refusal("for another library");
    }
    if (text_of(b, "machine") != entry.machine || number_of(b, "sequence") != entry.sequence
        || entry.sequence < 1 || !is_machine_id(entry.machine)) {
        throw Refusal("not the entry its name says");
    }
    p.kind = text_of(b, "kind");
    const auto& seen = field(b, "seen");
    if (!seen.is_object()) {
        throw Refusal("seen is not an object");
    }
    for (const auto& [machine, last] : seen.items()) {
        if (!is_machine_id(machine) || !last.is_number_integer() || last.get<std::int64_t>() < 0) {
            throw Refusal("seen holds something but computers' entry numbers");
        }
        if (machine != entry.machine && last.get<std::int64_t>() > 0) {
            p.seen.emplace(machine, last.get<std::int64_t>());
        }
    }
    return p;
}

/// Every computer's entries among ``placed``, numbered from 1 with none
/// missing: the last. A new entry has seen these; never one that waits (it
/// may wait for good, and would take the new one with it).
std::map<std::string, std::int64_t> heads_of(const std::vector<Parsed>& parsed,
                                             const std::vector<std::size_t>& placed)
{
    std::map<std::string, std::set<std::int64_t>> numbers;
    for (const auto i : placed) {
        numbers[parsed[i].machine].insert(parsed[i].sequence);
    }
    std::map<std::string, std::int64_t> out;
    for (const auto& [machine, set] : numbers) {
        std::int64_t last = 0;
        while (set.contains(last + 1)) {
            ++last;
        }
        if (last > 0) {
            out.emplace(machine, last);
        }
    }
    return out;
}

/// Place each entry after everything it saw: its depth (one more than the
/// deepest it saw) and its clock. Those that saw an entry not here (or, in
/// a forged list, one that saw them) stay unplaced: they wait. The order is
/// by depth, then machine id, then number.
std::vector<std::size_t> order(std::vector<Parsed>& parsed)
{
    std::map<std::pair<std::string, std::int64_t>, std::size_t> index;
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        index.emplace(std::pair(parsed[i].machine, parsed[i].sequence), i);
    }
    std::vector<bool> placed(parsed.size(), false);
    for (bool progress = true; progress;) {
        progress = false;
        for (std::size_t i = 0; i < parsed.size(); ++i) {
            if (placed[i]) {
                continue;
            }
            auto& p = parsed[i];
            std::vector<std::pair<std::string, std::int64_t>> before(p.seen.begin(), p.seen.end());
            if (p.sequence > 1) {
                before.emplace_back(p.machine, p.sequence - 1);
            }
            bool ready = true;
            for (const auto& b : before) {
                const auto it = index.find(b);
                if (it == index.end() || !placed[it->second]) {
                    ready = false;
                    break;
                }
            }
            if (!ready) {
                continue;
            }
            p.depth = 1;
            p.clock.clear();
            for (const auto& b : before) {
                const auto& earlier = parsed[index.at(b)];
                p.depth = std::max(p.depth, earlier.depth + 1);
                for (const auto& [machine, last] : earlier.clock) {
                    auto& mine = p.clock[machine];
                    mine = std::max(mine, last);
                }
            }
            p.clock[p.machine] = p.sequence;
            placed[i] = true;
            progress = true;
        }
    }
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        if (placed[i]) {
            out.push_back(i);
        }
    }
    std::ranges::sort(out, [&](std::size_t a, std::size_t b) {
        return std::tie(parsed[a].depth, parsed[a].machine, parsed[a].sequence)
               < std::tie(parsed[b].depth, parsed[b].machine, parsed[b].sequence);
    });
    return out;
}

/// Whether ``a`` came before ``b``: ``b``'s writer had it.
bool before(const Parsed& a, const Parsed& b)
{
    if (&a == &b) {
        return false;
    }
    const auto it = b.clock.find(a.machine);
    return it != b.clock.end() && it->second >= a.sequence;
}

/// What an entry that counted did to who may do what, and under which keys:
/// what it may cancel, and what may cancel it.
struct Effect {
    std::optional<crypto::PublicKey> admin_signer;
    std::optional<crypto::PublicKey> recovery_signer;
    /// The admin key that signed the invite a join used, and that invite's id.
    std::optional<crypto::PublicKey> invite_signer;
    std::optional<std::string> joined_by;
    /// It ends a computer's say (a removal), or a key's.
    std::optional<std::string> ends_machine;
    std::optional<crypto::PublicKey> ends_admin;
    std::optional<crypto::PublicKey> ends_recovery;
    /// It ends an invite (a withdrawal).
    std::optional<std::string> ends_invite;
    /// Which wins where two cancel each other: lower first.
    int rank = 3;
    bool founding = false;

    [[nodiscard]] bool ends_any() const
    {
        return ends_machine.has_value() || ends_admin.has_value() || ends_recovery.has_value()
               || ends_invite.has_value();
    }
};

constexpr int k_rank_recovery = 0;
constexpr int k_rank_removal = 1;
constexpr int k_rank_password = 2;
/// Cancelled by any of those, and cancelling none of them.
constexpr int k_rank_withdrawal = 3;

/// Whether ``e`` cancels ``v``, were ``v`` made apart from it.
bool cancels(const Effect& e, const Parsed& vp, const Effect& v)
{
    if (v.founding) {
        return false;
    }
    if (e.ends_machine && vp.machine == *e.ends_machine) {
        return true;
    }
    if (e.ends_admin
        && ((v.admin_signer && *v.admin_signer == *e.ends_admin)
            || (v.invite_signer && *v.invite_signer == *e.ends_admin))) {
        return true;
    }
    if (e.ends_invite && v.joined_by && *v.joined_by == *e.ends_invite) {
        return true;
    }
    return e.ends_recovery && v.recovery_signer && *v.recovery_signer == *e.ends_recovery;
}

/// Why ``v`` is refused, cancelled by ``e``.
std::string cancelled_why(const Effect& e)
{
    if (e.ends_machine) {
        return "written apart from its computer's removal";
    }
    if (e.ends_admin) {
        return "made with an admin key replaced meanwhile (the password was changed)";
    }
    if (e.ends_invite) {
        return "its invite was withdrawn";
    }
    return "made with a recovery key replaced meanwhile";
}

/// One pass over the entries in order, each checked against the list as it
/// stands when it is reached; those in ``cancelled`` are passed over.
///
/// An entry made with a key, or by a computer, that an entry it had not seen
/// ended is not refused here: by its writer's lights it was good. It counts
/// provisionally: its effect is noted, for the cancelling to weigh it
/// against what ended its authority, but it changes nothing. The cancelling
/// always settles it (what ended its authority cancels it, or it cancels
/// that), and the next pass takes it as it is then.
class Resolver {
public:
    Resolver(Members& out, const std::vector<Parsed>& parsed, const std::set<std::size_t>& cancelled)
        : out_(out), parsed_(parsed), cancelled_(cancelled), effects_(parsed.size())
    {
    }

    /// ``i``'s effect once it counted, provisionally or not; null while not.
    [[nodiscard]] const Effect* effect(std::size_t i) const
    {
        return counted_.contains(i) ? &effects_[i] : nullptr;
    }

    void run(const std::vector<std::size_t>& in_order)
    {
        for (const auto i : in_order) {
            if (cancelled_.contains(i)) {
                continue;
            }
            try {
                apply(i);
                counted_.insert(i);
            } catch (const Refusal& ex) {
                out_.refused.push_back({parsed_[i].machine, parsed_[i].sequence, ex.what()});
            } catch (const json::exception& ex) {
                out_.refused.push_back({parsed_[i].machine, parsed_[i].sequence, ex.what()});
            }
        }
    }

private:
    /// How an entry stands with a key or a computer: current, ended by an
    /// entry it had not seen (provisional), or not at all.
    enum class Standing { Current, Provisional, None };

    Standing standing(std::size_t i, const crypto::PublicKey& key,
                      const std::optional<crypto::PublicKey>& current,
                      const std::map<crypto::PublicKey, std::size_t>& ended) const
    {
        if (current && key == *current) {
            return Standing::Current;
        }
        const auto it = ended.find(key);
        if (it != ended.end() && !before(parsed_[it->second], parsed_[i])) {
            return Standing::Provisional;
        }
        return Standing::None;
    }

    /// How ``i`` stands signed as ``as``, if it is.
    Standing signed_standing(std::size_t i, std::string_view as) const
    {
        const auto* signer = parsed_[i].envelope.signed_as(as);
        if (signer == nullptr) {
            return Standing::None;
        }
        return as == k_signed_as_admin
                   ? standing(i, *signer, out_.admin, admin_ended_)
                   : standing(i, *signer, out_.recovery, recovery_ended_);
    }

    void apply(std::size_t i)
    {
        const auto& p = parsed_[i];
        auto& effect = effects_[i];
        const auto& b = p.envelope.body;
        const auto* computer_key = p.envelope.signed_as(k_signed_as_computer);
        if (computer_key == nullptr) {
            throw Refusal("not signed by a computer");
        }
        if (p.kind == "found") {
            if (out_.founded()) {
                throw Refusal("the library was founded already");
            }
            auto founder = computer_of(b);
            const auto admin = key_of(b, "admin");
            const auto recovery = key_of(b, "recovery");
            if (founder.machine != p.machine || founder.key != *computer_key) {
                throw Refusal("not signed by the founding computer");
            }
            const auto* signer = p.envelope.signed_as(k_signed_as_admin);
            if (signer == nullptr || *signer != admin) {
                throw Refusal("not signed by its admin key");
            }
            out_.admin = admin;
            out_.recovery = recovery;
            founder.has_edited = true;
            out_.members.emplace(founder.machine, std::move(founder));
            effect.founding = true;
            return;
        }
        if (!out_.founded()) {
            throw Refusal("before the library was founded");
        }
        if (p.kind == "join") {
            join(i, *computer_key);
            return;
        }

        // The writer: a member that may write, or one removed by an entry it
        // had not seen.
        const auto* writer = out_.find(p.machine);
        if (writer == nullptr) {
            throw Refusal("written by a computer that is not a member");
        }
        if (writer->key != *computer_key) {
            throw Refusal("signed with a key that is not that computer's");
        }
        bool provisional = false;
        if (writer->removed) {
            const auto it = removed_by_.find(p.machine);
            if (it == removed_by_.end() || before(parsed_[it->second], p)) {
                throw Refusal("written by a computer that had been removed");
            }
            provisional = true;
        } else if (writer->role == Role::Viewer && !writer->holds_admin_key && p.kind != "leave"
                   && p.kind != "name" && p.kind != "addresses") {
            // A viewer given the admin key does admin work there: the admin
            // signature below is what counts for that.
            throw Refusal("written by a computer that may not");
        }

        if (p.kind == "name") {
            // A computer names itself: no password, as it can only ever be
            // itself. Its later names come after, each with its picture.
            const auto name = text_of(b, "name");
            if (name.empty()) {
                throw Refusal("its name is empty");
            }
            if (!provisional) {
                auto& self = out_.members.at(p.machine);
                self.name = name;
                const auto picture = b.find("picture");
                self.picture = picture != b.end() ? picture_from_json(*picture) : Picture{};
            }
            return;
        }

        if (p.kind == "addresses") {
            // Where a computer takes connections: its say alone, as its
            // name is.
            auto addresses = addresses_of(b);
            if (!provisional) {
                out_.members.at(p.machine).addresses = std::move(addresses);
            }
            return;
        }

        if (p.kind == "leave") {
            // A computer takes itself off the list: no password, as it can
            // only ever be itself. What it wrote so far counts.
            auto& self = out_.members.at(p.machine);
            const auto kept = number_of(b, "files_kept");
            effect.ends_machine = self.machine;
            effect.rank = k_rank_removal;
            if (!provisional) {
                self.removed = true;
                self.files_kept = std::min(self.files_kept, kept);
                removed_by_.emplace(self.machine, i);
            }
            return;
        }

        if (p.kind == "admin") {
            // The recovery code's: a forgotten password. The code is spent.
            if (const auto by_code = signed_standing(i, k_signed_as_recovery);
                by_code != Standing::None) {
                effect.recovery_signer = *p.envelope.signed_as(k_signed_as_recovery);
                effect.ends_recovery = effect.recovery_signer;
                effect.ends_admin = out_.admin;
                effect.rank = k_rank_recovery;
                const auto admin = key_of(b, "admin");
                const auto recovery = key_of(b, "recovery");
                if (provisional || by_code == Standing::Provisional) {
                    return;
                }
                admin_ended_.emplace(*out_.admin, i);
                recovery_ended_.emplace(*out_.recovery, i);
                out_.admin = admin;
                out_.recovery = recovery;
                out_.invites.clear();  // signed with the key it ended, they admit no one
                return;
            }
            // The password's: a new password. The code stands.
            if (const auto by_password = signed_standing(i, k_signed_as_admin);
                by_password != Standing::None) {
                if (b.contains("recovery")) {
                    throw Refusal("a new password does not change the recovery code");
                }
                effect.admin_signer = *p.envelope.signed_as(k_signed_as_admin);
                effect.ends_admin = effect.admin_signer;
                effect.rank = k_rank_password;
                const auto admin = key_of(b, "admin");
                if (provisional || by_password == Standing::Provisional) {
                    return;
                }
                admin_ended_.emplace(*out_.admin, i);
                out_.admin = admin;
                out_.invites.clear();  // signed with the key it ended, they admit no one
                return;
            }
            throw Refusal("not signed by the admin key or the recovery key");
        }

        const auto by_password = signed_standing(i, k_signed_as_admin);
        if (by_password == Standing::None) {
            throw Refusal("not signed by the library's admin key");
        }
        effect.admin_signer = *p.envelope.signed_as(k_signed_as_admin);
        provisional = provisional || by_password == Standing::Provisional;

        if (p.kind == "recovery") {
            const auto recovery = key_of(b, "recovery");
            effect.ends_recovery = out_.recovery;
            effect.rank = k_rank_password;
            if (!provisional) {
                recovery_ended_.emplace(*out_.recovery, i);
                out_.recovery = recovery;
            }
        } else if (p.kind == "role") {
            auto& target = target_of(b);
            const auto role = role_of(b, "role");
            const auto kept = role == Role::Viewer ? number_of(b, "files_kept") : k_all_files;
            if (!provisional) {
                // Made an editor again: what it wrote meanwhile it could
                // write again now, so nothing is gained by refusing it.
                target.files_kept = role == Role::Viewer ? std::min(target.files_kept, kept)
                                                         : k_all_files;
                if (role == Role::Viewer && target.role == Role::Editor) {
                    target.holds_admin_key = false;  // given again, if wanted
                }
                target.has_edited = target.has_edited || role == Role::Editor;
                target.role = role;
            }
        } else if (p.kind == "holds_key") {
            auto& target = target_of(b);
            const auto holds = field(b, "holds");
            if (!holds.is_boolean()) {
                throw Refusal("holds is not yes or no");
            }
            if (!provisional) {
                target.holds_admin_key = holds.get<bool>();
            }
        } else if (p.kind == "remove") {
            auto& target = target_of(b);
            const auto kept = number_of(b, "files_kept");
            effect.ends_machine = target.machine;
            effect.rank = k_rank_removal;
            if (!provisional) {
                target.removed = true;
                target.files_kept = std::min(target.files_kept, kept);
                removed_by_.emplace(target.machine, i);
            }
        } else if (p.kind == "limit") {
            const auto bytes = number_of(b, "bytes");
            if (!provisional) {
                out_.size_limit = bytes;
            }
        } else if (p.kind == "invite") {
            InviteMade made{text_of(b, "invite"), role_of(b, "role"), text_of(b, "expires"), p.machine};
            if (!provisional && !out_.invites_used.contains(made.id) && !out_.invites_withdrawn.contains(made.id)) {
                out_.invites.insert_or_assign(made.id, std::move(made));
            }
        } else if (p.kind == "withdraw") {
            // As a removal ends a computer's say, this ends the invite's: a
            // join by it made apart from this is cancelled (``cancels``).
            auto id = text_of(b, "invite");
            effect.ends_invite = id;
            effect.rank = k_rank_withdrawal;
            if (!provisional) {
                out_.invites.erase(id);
                out_.invites_withdrawn.insert(std::move(id));
            }
        } else {
            throw Refusal("of a kind this SeventhSlide does not know: " + p.kind);
        }
    }

    Member& target_of(const json& b)
    {
        const auto it = out_.members.find(text_of(b, "target"));
        if (it == out_.members.end() || it->second.removed) {
            throw Refusal("about a computer that is not a member");
        }
        return it->second;
    }

    /// A machine id is never two computers (the files under it would mix),
    /// and a removed computer's key is never a member again.
    void add(Member member)
    {
        if (out_.members.contains(member.machine)) {
            throw Refusal("that machine id is a member already");
        }
        if (const auto* same = out_.with_key(member.key); same != nullptr) {
            throw Refusal(same->removed ? "that computer was removed, for good"
                                        : "that computer is a member already");
        }
        out_.members.emplace(member.machine, std::move(member));
    }

    void join(std::size_t i, const crypto::PublicKey& computer_key)
    {
        const auto& p = parsed_[i];
        auto& effect = effects_[i];
        auto joiner = computer_of(p.envelope.body);
        if (joiner.machine != p.machine || joiner.key != computer_key) {
            throw Refusal("not signed by the joining computer");
        }
        SignedInvite invite;
        try {
            invite = read_invite(text_of(p.envelope.body, "invite"));
        } catch (const std::runtime_error& ex) {
            throw Refusal(ex.what());
        }
        if (invite.invite.library != out_.library) {
            throw Refusal("its invite is for another library");
        }
        if (const auto* proof = p.envelope.signed_as(k_signed_as_invite);
            proof == nullptr || *proof != invite.invite.proof) {
            throw Refusal("it does not prove it holds its invite");
        }
        const auto by = standing(i, invite.admin, out_.admin, admin_ended_);
        if (by == Standing::None) {
            throw Refusal("its invite was not signed by the library's admin key");
        }
        effect.invite_signer = invite.admin;
        effect.joined_by = invite.invite.id;
        if (by == Standing::Provisional) {
            return;
        }
        if (const auto* same = out_.find(joiner.machine);
            same != nullptr && same->key == joiner.key && !same->removed) {
            return;  // the same computer, joining again: nothing new
        }
        auto& used = invites_used_[invite.invite.id];
        for (const auto earlier : used) {
            if (before(parsed_[earlier], p)) {
                throw Refusal("its invite had been used already");
            }
        }
        joiner.role = invite.invite.role;
        // A viewer from the start has no change files that count.
        joiner.files_kept = joiner.role == Role::Viewer ? 0 : k_all_files;
        joiner.has_edited = joiner.role == Role::Editor;
        add(std::move(joiner));
        used.push_back(i);
        out_.invites_used.insert(invite.invite.id);
        out_.invites.erase(invite.invite.id);
    }

    Members& out_;
    const std::vector<Parsed>& parsed_;
    const std::set<std::size_t>& cancelled_;
    std::vector<Effect> effects_;
    std::set<std::size_t> counted_;
    std::map<std::string, std::vector<std::size_t>> invites_used_;
    /// Each key ended, and each computer removed: by which entry.
    std::map<crypto::PublicKey, std::size_t> admin_ended_;
    std::map<crypto::PublicKey, std::size_t> recovery_ended_;
    std::map<std::string, std::size_t> removed_by_;
};

}  // namespace

json picture_to_json(const Picture& picture)
{
    if (!picture.image.empty()) {
        return {{"image", picture.image}};
    }
    return {{"glyph", picture.glyph}, {"colour", picture.colour}};
}

Picture picture_from_json(const json& j)
{
    if (!j.is_object()) {
        return {};
    }
    const auto text = [&](const char* name) {
        const auto it = j.find(name);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    if (auto image = text("image"); is_base64(image)) {
        return {{}, {}, std::move(image)};
    }
    auto glyph = text("glyph");
    auto colour = text("colour");
    if (is_glyph_name(glyph) && is_colour(colour)) {
        return {std::move(glyph), std::move(colour), {}};
    }
    return {};
}

std::optional<HostPort> parse_address(std::string_view address)
{
    std::string_view host;
    std::string_view port;
    if (address.starts_with('[')) {
        // [::1]:8766
        const auto close = address.find(']');
        if (close == std::string_view::npos || close + 1 >= address.size()
            || address[close + 1] != ':') {
            return std::nullopt;
        }
        host = address.substr(1, close - 1);
        port = address.substr(close + 2);
    } else {
        const auto colon = address.rfind(':');
        if (colon == std::string_view::npos || address.find(':') != colon) {
            return std::nullopt;
        }
        host = address.substr(0, colon);
        port = address.substr(colon + 1);
    }
    unsigned value = 0;
    const auto [at, ec] = std::from_chars(port.data(), port.data() + port.size(), value);
    if (host.empty() || ec != std::errc{} || at != port.data() + port.size() || value == 0
        || value > 65535) {
        return std::nullopt;
    }
    return HostPort{std::string(host), static_cast<std::uint16_t>(value)};
}

std::string_view role_name(Role role)
{
    switch (role) {
    case Role::Editor:
        return "editor";
    case Role::Viewer:
        return "viewer";
    }
    return "editor";
}

std::optional<Role> role_from_name(std::string_view name)
{
    if (name == "editor") {
        return Role::Editor;
    }
    if (name == "viewer") {
        return Role::Viewer;
    }
    return std::nullopt;
}

const Member* Members::find(std::string_view machine) const
{
    const auto it = members.find(std::string(machine));
    return it == members.end() ? nullptr : &it->second;
}

bool Members::may_have_written(std::string_view machine, std::int64_t sequence) const
{
    const auto* m = find(machine);
    if (m == nullptr) {
        return false;
    }
    return (!m->removed && m->role == Role::Editor) || sequence <= m->files_kept;
}

const Member* Members::with_key(const crypto::PublicKey& key) const
{
    for (const auto& [machine, member] : members) {
        if (member.key == key) {
            return &member;
        }
    }
    return nullptr;
}

bool Members::may_write(std::string_view machine) const
{
    const auto* m = find(machine);
    return m != nullptr && !m->removed && m->role == Role::Editor;
}

json Invite::to_json() const
{
    json list = json::array();
    for (const auto& s : through) {
        list.push_back({{"machine", s.machine}, {"key", s.key.base64()}, {"addresses", s.addresses}});
    }
    json out{{"library", library},
             {"role", std::string(role_name(role))},
             {"expires", expires},
             {"id", id},
             {"through", std::move(list)},
             {"seen", seen},
             {"name", name},
             {"proof", proof.base64()}};
    if (from) {
        out["from"] = {{"machine", from->machine}, {"key", from->key.base64()}, {"addresses", from->addresses}};
    }
    return out;
}

Invite Invite::from_json(const json& j)
{
    try {
        if (!j.is_object()) {
            throw Refusal("not an object");
        }
        Invite out;
        out.library = text_of(j, "library");
        out.role = role_of(j, "role");
        out.expires = text_of(j, "expires");
        out.id = text_of(j, "id");
        const auto& through = field(j, "through");
        if (!through.is_array()) {
            throw Refusal("through is not a list");
        }
        for (const auto& s : through) {
            if (!s.is_object()) {
                throw Refusal("a computer to join through is not an object");
            }
            out.through.push_back({text_of(s, "machine"), key_of(s, "key"), addresses_of(s)});
            if (!is_machine_id(out.through.back().machine)) {
                throw Refusal("a computer to join through is not a computer");
            }
        }
        const auto& seen = field(j, "seen");
        if (!seen.is_object()) {
            throw Refusal("seen is not an object");
        }
        for (const auto& [machine, last] : seen.items()) {
            if (!is_machine_id(machine) || !last.is_number_integer() || last.get<std::int64_t>() < 1) {
                throw Refusal("seen holds something but computers' entry numbers");
            }
            out.seen.emplace(machine, last.get<std::int64_t>());
        }
        if (const auto name = j.find("name"); name != j.end()) {
            if (!name->is_string()) {
                throw Refusal("name is not text");
            }
            out.name = name->get<std::string>();
        }
        if (const auto from = j.find("from"); from != j.end()) {
            if (!from->is_object()) {
                throw Refusal("from is not an object");
            }
            out.from = ComputerAddress{text_of(*from, "machine"), key_of(*from, "key"), addresses_of(*from)};
            if (!is_machine_id(out.from->machine)) {
                throw Refusal("from is not a computer");
            }
        }
        out.proof = key_of(j, "proof");
        return out;
    } catch (const Refusal& ex) {
        throw std::invalid_argument(std::string("An invite could not be read (") + ex.what() + ")");
    } catch (const json::exception& ex) {
        throw std::invalid_argument(std::string("An invite could not be read (") + ex.what() + ")");
    }
}

std::string invite_code(Invite invite, const crypto::SigningKey& admin)
{
    const auto secret = crypto::SigningKey::generate();
    invite.proof = secret.public_key();
    const json code{{"invite", seal(invite.to_json(), {{k_signed_as_admin, &admin}})},
                    {"secret", crypto::to_base64(secret.seed())}};
    return crypto::to_base64(code.dump());
}

SignedInvite read_invite(std::string_view code)
{
    // The join entry carries the signed invite as it is; a person pastes it
    // as base64.
    std::string text(code);
    if (!text.starts_with('{')) {
        // A terminal breaks a long code across lines, and a copy keeps the breaks.
        std::erase_if(text, [](unsigned char c) { return std::isspace(c) != 0; });
        const auto decoded = crypto::from_base64(text);
        // Words with their spaces gone may read as base64: what they decode to is no JSON.
        if (!decoded || !decoded->starts_with('{')) {
            throw std::runtime_error("This is not an invite code.");
        }
        text = *decoded;
    }
    // A code to paste holds the signed invite and its one-time key; a signed
    // invite is two lines, which do not read as one piece of JSON.
    std::optional<crypto::SigningKey> secret;
    if (const auto code_json = json::parse(text, nullptr, /*allow_exceptions=*/false); code_json.is_object()) {
        const auto invite = code_json.find("invite");
        const auto seed = code_json.find("secret");
        if (invite == code_json.end() || !invite->is_string() || seed == code_json.end() || !seed->is_string()) {
            throw std::runtime_error("This is not an invite code.");
        }
        if (const auto bytes = crypto::from_base64(seed->get<std::string>())) {
            secret = crypto::SigningKey::from_seed(*bytes);
        }
        if (!secret) {
            throw std::runtime_error("This is not an invite code.");
        }
        text = invite->get<std::string>();
    }
    const auto envelope = unseal(text, "An invite");
    const auto* admin = envelope.signed_as(k_signed_as_admin);
    if (admin == nullptr) {
        throw std::runtime_error("An invite could not be read (not signed by an admin key)");
    }
    try {
        auto invite = Invite::from_json(envelope.body);
        if (secret && secret->public_key() != invite.proof) {
            throw std::runtime_error("This is not an invite code.");
        }
        return {std::move(invite), *admin, std::move(text), std::move(secret), {}};
    } catch (const std::invalid_argument& ex) {
        throw std::runtime_error(ex.what());
    }
}

SignedInvite join_entry_invite(std::string_view entry)
{
    const auto envelope = unseal(entry, k_what);
    const auto kind = envelope.body.find("kind");
    const auto invite = envelope.body.find("invite");
    if (kind == envelope.body.end() || *kind != "join" || invite == envelope.body.end() || !invite->is_string()) {
        throw std::runtime_error("This is not a member list entry joining a library.");
    }
    auto out = read_invite(invite->get<std::string>());
    out.secret.reset();  // an entry never holds one; if it did, it is no proof
    out.entry = std::string(entry);
    return out;
}

std::optional<SignedInvite> proved_invite(std::string_view entry, std::string_view library,
                                          const crypto::PublicKey& computer)
{
    try {
        const auto envelope = unseal(entry, k_what);
        const auto* by = envelope.signed_as(k_signed_as_computer);
        const auto* proof = envelope.signed_as(k_signed_as_invite);
        if (by == nullptr || *by != computer || proof == nullptr
            || computer_of(envelope.body).key != computer || text_of(envelope.body, "library") != library) {
            return std::nullopt;
        }
        auto invite = join_entry_invite(entry);
        if (invite.invite.library != library || invite.invite.proof != *proof) {
            return std::nullopt;
        }
        return invite;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

namespace {

/// ``entry`` parsed and checked, or why it is refused.
std::variant<Parsed, std::string> parse_or_why(std::string_view library, const StoredEntry& entry)
{
    try {
        return parse(library, entry);
    } catch (const std::runtime_error& ex) {
        return std::string(ex.what());
    } catch (const json::exception& ex) {
        return std::string(ex.what());
    }
}

/// The list ``parsed`` resolves to, the entries in ``refused`` refused
/// already.
Members resolve_parsed(std::string_view library, std::vector<Parsed> parsed,
                       std::vector<Members::Refused> refused)
{
    Members base;
    base.library = std::string(library);
    base.refused = std::move(refused);
    const auto in_order = order(parsed);
    base.heads = heads_of(parsed, in_order);
    {
        std::set<std::size_t> placed(in_order.begin(), in_order.end());
        for (std::size_t i = 0; i < parsed.size(); ++i) {
            if (!placed.contains(i)) {
                base.waiting.push_back({parsed[i].machine, parsed[i].sequence});
            }
        }
    }

    // Resolve, find what changes of authority cancel (what was made apart
    // from them), pass over it, and resolve again, until nothing more is
    // cancelled. What is cancelled only grows, so this ends.
    std::map<std::size_t, std::string> reasons;
    std::set<std::size_t> cancelled;
    for (;;) {
        Members out = base;
        Resolver resolver(out, parsed, cancelled);
        resolver.run(in_order);

        // The changes of authority, strongest first: where two cancel each
        // other, the stronger is reached first, and one cancelled cancels
        // nothing.
        std::vector<std::size_t> enders;
        for (const auto i : in_order) {
            if (const auto* e = resolver.effect(i); e != nullptr && e->ends_any()) {
                enders.push_back(i);
            }
        }
        std::ranges::sort(enders, [&](std::size_t a, std::size_t b) {
            return std::tie(resolver.effect(a)->rank, parsed[a].machine, parsed[a].sequence)
                   < std::tie(resolver.effect(b)->rank, parsed[b].machine, parsed[b].sequence);
        });
        std::map<std::size_t, std::string> more;
        for (const auto ei : enders) {
            if (more.contains(ei)) {
                continue;
            }
            const auto& e = *resolver.effect(ei);
            for (const auto vi : in_order) {
                const auto* v = resolver.effect(vi);
                if (v == nullptr || vi == ei || more.contains(vi) || before(parsed[vi], parsed[ei])
                    || !cancels(e, parsed[vi], *v)) {
                    continue;
                }
                more.emplace(vi, cancelled_why(e));
            }
        }
        if (more.empty()) {
            for (const auto& [i, why] : reasons) {
                out.refused.push_back({parsed[i].machine, parsed[i].sequence, why});
            }
            return out;
        }
        for (auto& [i, why] : more) {
            cancelled.insert(i);
            reasons.try_emplace(i, std::move(why));
        }
    }
}

}  // namespace

/// The entries a store holds never change: each is read, parsed and its
/// signature checked once, and the list resolved again only when the
/// entries there are not those it was resolved from (one came, or one was
/// set aside). Every store on the folder lists the same files, so this
/// stays true whichever writes them.
struct SyncStore::MemberCache {
    std::mutex mutex;
    /// The library the entries were checked as entries of.
    std::string library;
    /// By computer and number: the entry parsed, or why it was refused.
    std::map<std::pair<std::string, std::int64_t>, std::variant<Parsed, std::string>> entries;
    /// What ``entries`` resolve to; none until they were.
    std::optional<Members> members;
};

SyncStore::MemberCache& SyncStore::member_cache() const
{
    std::call_once(member_cache_made_, [this] { member_cache_ = std::make_shared<MemberCache>(); });
    return *member_cache_;
}

Members read_members(SyncStore& store, std::string_view library)
{
    auto& cache = store.member_cache();
    const std::lock_guard lock(cache.mutex);
    std::vector<std::pair<std::string, std::int64_t>> names;
    for (const auto& machine : store.machines(Numbered::Members)) {
        for (const auto n : store.sequences(Numbered::Members, machine)) {
            names.emplace_back(machine, n);
        }
    }
    if (cache.library != library) {
        cache.entries.clear();
        cache.members.reset();
        cache.library = std::string(library);
    }
    const bool same = cache.members && names.size() == cache.entries.size()
                      && std::ranges::all_of(names, [&](const auto& n) { return cache.entries.contains(n); });
    if (same) {
        return *cache.members;
    }
    // One set aside is let go of; one new is read. The list the others
    // resolved to goes first: a read that throws leaves none to trust.
    cache.members.reset();
    std::erase_if(cache.entries, [&](const auto& entry) {
        return std::ranges::find(names, entry.first) == names.end();
    });
    std::vector<Parsed> parsed;
    std::vector<Members::Refused> refused;
    for (const auto& name : names) {
        auto it = cache.entries.find(name);
        if (it == cache.entries.end()) {
            const StoredEntry entry{name.first, name.second,
                                    store.read(Numbered::Members, name.first, name.second)};
            it = cache.entries.emplace(name, parse_or_why(library, entry)).first;
        }
        if (const auto* p = std::get_if<Parsed>(&it->second)) {
            parsed.push_back(*p);
        } else {
            refused.push_back({name.first, name.second, std::get<std::string>(it->second)});
        }
    }
    cache.members = resolve_parsed(library, std::move(parsed), std::move(refused));
    return *cache.members;
}

std::optional<SignedInvite> own_invite(SyncStore& store, std::string_view library,
                                       std::string_view machine)
{
    const std::string who(machine);
    for (const auto n : store.sequences(Numbered::Members, who)) {
        try {
            const auto body = unseal(store.read(Numbered::Members, who, n), "A member list entry").body;
            if (body.value("kind", std::string()) != "join" || body.value("library", std::string()) != library) {
                continue;
            }
            return join_entry_invite(store.read(Numbered::Members, who, n));
        } catch (const std::exception&) {
            // not one to go by
        }
    }
    return std::nullopt;
}

bool joined_here(SyncStore& store, std::string_view library, std::string_view machine)
{
    const std::string who(machine);
    for (const auto n : store.sequences(Numbered::Members, who)) {
        try {
            const auto body = unseal(store.read(Numbered::Members, who, n), "A member list entry").body;
            if (body.value("kind", std::string()) == "join"
                && body.value("library", std::string()) == library) {
                return true;
            }
        } catch (const std::exception&) {
            // not one to go by
        }
    }
    return false;
}

bool invite_expired(const Invite& invite)
{
    return expired(invite.expires);
}

bool invite_expired(const InviteMade& invite)
{
    return expired(invite.expires);
}

std::vector<InviteMade> waiting_invites(const Members& list)
{
    std::vector<InviteMade> out;
    for (const auto& [id, made] : list.invites) {
        if (!invite_expired(made)) {
            out.push_back(made);
        }
    }
    std::ranges::sort(out, [](const InviteMade& a, const InviteMade& b) {
        return std::tie(a.expires, a.id) < std::tie(b.expires, b.id);
    });
    return out;
}

MemberLog::MemberLog(SyncStore& store, Identity me, std::string library)
    : store_(store), me_(std::move(me)), library_(std::move(library))
{
}

Members MemberLog::members() const
{
    return read_members(store_, library_);
}

json MemberLog::body(std::string_view kind) const
{
    const auto mine = store_.sequences(Numbered::Members, me_.machine);
    json seen = json::object();
    for (const auto& [machine, last] : members().heads) {
        if (machine != me_.machine) {
            seen[machine] = last;
        }
    }
    return {{"format", k_member_format},
            {"library", library_},
            {"machine", me_.machine},
            {"sequence", (mine.empty() ? 0 : mine.back()) + 1},
            {"seen", std::move(seen)},
            {"time", now_utc()},
            {"kind", std::string(kind)}};
}
void MemberLog::write(const json& body,
                      const std::vector<std::pair<std::string_view, const crypto::SigningKey*>>& signers)
{
    store_.write(Numbered::Members, me_.machine, body["sequence"].get<std::int64_t>(),
                 seal(body, signers));
}

MemberLog::Founded MemberLog::found(std::string_view passphrase, crypto::BoxParams params)
{
    if (members().founded()) {
        throw std::logic_error("This library is a synced library already");
    }
    if (const auto problem = passphrase_problem(passphrase)) {
        throw std::invalid_argument(*problem);
    }
    // The founding entry is signed by the admin key it introduces.
    auto admin = crypto::SigningKey::generate();
    const auto recovery = crypto::SigningKey::generate();
    const auto locked = lock_admin_key(admin, passphrase, params);
    store_.write_admin_key(admin.public_key().fingerprint(), locked.to_json().dump());
    auto b = body("found");
    b["computer"] = computer_json({me_.machine, me_.key.public_key(), me_.computer});
    b["admin"] = admin.public_key().base64();
    b["recovery"] = recovery.public_key().base64();
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_admin, &admin}});
    return {std::move(admin), recovery_code(recovery)};
}

void MemberLog::join(const SignedInvite& invite)
{
    if (invite.invite.library != library_) {
        throw std::runtime_error("This invite is for another library.");
    }
    if (invite_expired(invite.invite)) {
        throw std::runtime_error("This invite has expired. Ask for a new one.");
    }
    if (!invite.secret) {
        throw std::runtime_error("This invite is not one this SeventhSlide can use. Ask for a new one.");
    }
    auto b = body("join");
    for (const auto& [machine, last] : invite.invite.seen) {
        if (machine != me_.machine && b["seen"].value(machine, std::int64_t{0}) < last) {
            b["seen"][machine] = last;
        }
    }
    b["computer"] = computer_json({me_.machine, me_.key.public_key(), me_.computer});
    b["invite"] = invite.sealed;
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_invite, &*invite.secret}});
}

std::int64_t MemberLog::files_here(std::string_view target) const
{
    // Those let go of here (old files) were here, every one.
    std::int64_t last = store_.dropped_through(target);
    for (const auto n : store_.sequences(Numbered::Changes, target)) {
        if (n <= last) {
            continue;
        }
        if (n != last + 1) {
            break;
        }
        last = n;
    }
    return last;
}

void MemberLog::set_role(std::string_view target, Role role, const crypto::SigningKey& admin)
{
    auto b = body("role");
    b["target"] = std::string(target);
    b["role"] = std::string(role_name(role));
    if (role == Role::Viewer) {
        b["files_kept"] = files_here(target);
    }
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_admin, &admin}});
}

void MemberLog::set_key_holder(std::string_view target, bool holds, const crypto::SigningKey& admin)
{
    auto b = body("holds_key");
    b["target"] = std::string(target);
    b["holds"] = holds;
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_admin, &admin}});
}

void MemberLog::leave()
{
    const auto list = members();
    const auto* self = list.find(me_.machine);
    if (self == nullptr || self->removed) {
        throw std::runtime_error("This computer is not a member of the library.");
    }
    auto b = body("leave");
    b["files_kept"] = files_here(me_.machine);
    write(b, {{k_signed_as_computer, &me_.key}});
}

void MemberLog::rename(std::string_view name, const Picture& picture)
{
    const auto list = members();
    const auto* self = list.find(me_.machine);
    if (self == nullptr || self->removed) {
        throw std::runtime_error("This computer is not a member of the library.");
    }
    auto b = body("name");
    b["name"] = std::string(name);
    if (!picture.empty()) {
        b["picture"] = picture_to_json(picture);
    }
    write(b, {{k_signed_as_computer, &me_.key}});
}

void MemberLog::set_addresses(const std::vector<std::string>& addresses)
{
    const auto list = members();
    const auto* self = list.find(me_.machine);
    if (self == nullptr || self->removed) {
        throw std::runtime_error("This computer is not a member of the library.");
    }
    check_addresses(addresses);
    auto b = body("addresses");
    b["addresses"] = addresses;
    write(b, {{k_signed_as_computer, &me_.key}});
}

void MemberLog::remove(std::string_view target, const crypto::SigningKey& admin)
{
    auto b = body("remove");
    b["target"] = std::string(target);
    b["files_kept"] = files_here(target);
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_admin, &admin}});
}

void MemberLog::set_size_limit(std::int64_t bytes, const crypto::SigningKey& admin)
{
    if (bytes < 0) {
        throw std::invalid_argument("A size limit is not less than nothing");
    }
    auto b = body("limit");
    b["bytes"] = bytes;
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_admin, &admin}});
}

MemberLog::Founded MemberLog::replace_admin(std::string_view passphrase,
                                            const crypto::SigningKey& signer,
                                            crypto::BoxParams params)
{
    const auto current = members();
    if (!current.founded()) {
        throw std::logic_error("This library is not a synced library yet");
    }
    if (const auto problem = passphrase_problem(passphrase)) {
        throw std::invalid_argument(*problem);
    }
    const bool recovering = current.recovery == signer.public_key();
    auto admin = crypto::SigningKey::generate();
    const auto locked = lock_admin_key(admin, passphrase, params);
    store_.write_admin_key(admin.public_key().fingerprint(), locked.to_json().dump());
    auto b = body("admin");
    b["admin"] = admin.public_key().base64();
    std::string code;
    if (recovering) {
        // The code was used: someone may have seen it. A new one goes on
        // paper.
        const auto recovery = crypto::SigningKey::generate();
        b["recovery"] = recovery.public_key().base64();
        code = recovery_code(recovery);
    }
    write(b, {{k_signed_as_computer, &me_.key},
              {recovering ? k_signed_as_recovery : k_signed_as_admin, &signer}});
    return {std::move(admin), std::move(code)};
}

void MemberLog::withdraw_invite(std::string_view id, const crypto::SigningKey& admin)
{
    auto b = body("withdraw");
    b["invite"] = std::string(id);
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_admin, &admin}});
}

std::string MemberLog::replace_recovery(const crypto::SigningKey& admin)
{
    const auto recovery = crypto::SigningKey::generate();
    auto b = body("recovery");
    b["recovery"] = recovery.public_key().base64();
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_admin, &admin}});
    return recovery_code(recovery);
}

std::optional<crypto::SigningKey> MemberLog::unlock_admin(std::string_view passphrase) const
{
    const auto current = members();
    if (!current.admin) {
        throw std::runtime_error("This library is not a synced library yet.");
    }
    const auto fingerprint = current.admin->fingerprint();
    const auto held = store_.admin_keys();
    if (std::ranges::find(held, fingerprint) == held.end()) {
        throw std::runtime_error("This computer holds no copy of the library's admin key: it "
                                 "views the library, or has not caught up with a password change. "
                                 "Admin work is done on an editor computer, or a viewer given the key.");
    }
    const auto locked = LockedAdminKey::from_json(
        json::parse(store_.read_admin_key(fingerprint), nullptr, /*allow_exceptions=*/false));
    if (!locked || locked->admin != *current.admin) {
        throw std::runtime_error("The library's locked admin key here is damaged. Another "
                                 "computer's copy of the library may still have a good one.");
    }
    return unlock_admin_key(*locked, passphrase);
}

std::string MemberLog::invite(Role role, std::string_view expires, const crypto::SigningKey& admin,
                              std::string_view name, const std::optional<ComputerAddress>& from)
{
    // What the invite would not read back as.
    if (from) {
        check_addresses(from->addresses);
    }
    Invite inv;
    inv.library = library_;
    inv.role = role;
    inv.expires = std::string(expires);
    inv.id = crypto::to_hex(crypto::random_bytes(8));
    inv.name = std::string(name);
    inv.from = from;
    // The invite has not seen what says it was made: its join would wait
    // for that entry wherever it came first, and the computer that made it
    // may be away from them all. Joined before that comes, it is not waiting.
    const auto list = members();
    auto b = body("invite");
    b["invite"] = inv.id;
    b["role"] = std::string(role_name(role));
    b["expires"] = inv.expires;
    write(b, {{k_signed_as_computer, &me_.key}, {k_signed_as_admin, &admin}});
    inv.seen = list.heads;
    for (const auto& [machine, m] : list.members) {
        if (!m.removed && !m.addresses.empty()) {
            inv.through.push_back({m.machine, m.key, m.addresses});
        }
    }
    return invite_code(inv, admin);
}

}  // namespace seventhslide::sync
