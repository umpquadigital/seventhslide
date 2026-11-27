// Peer: what each end says it has (``have``), what is asked for of it
// (``want``) and how each fetch ends, and what is sent when the other end
// asks.

#include "seventhslide/link/peer.hpp"

#include "peer_internal.hpp"

#include "seventhslide/sync/member_list.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

namespace seventhslide::link {
namespace {

constexpr int k_want_batch = 200;

/// Whether ``name`` names a store file of ``kind`` in a fetch.
bool is_item(StoreFile kind, const std::string& name)
{
    return name.starts_with(item(store_file_name(kind), ""));
}

/// Where a ``have`` says the last of each computer's numbered files.
const char* have_field(sync::Numbered kind)
{
    switch (kind) {
    case sync::Numbered::Changes:
        return "changes";
    case sync::Numbered::Members:
        return "members";
    case sync::Numbered::Log:
        return "log";
    }
    return "changes";
}

}  // namespace

std::int64_t Peer::their_last(const std::string& library, sync::Numbered kind,
                              const std::string& machine) const
{
    const auto st = state_.find(library);
    if (st == state_.end() || !allowed_.contains(library)) {
        return 0;
    }
    const auto field = st->second.their_have.find(have_field(kind));
    if (field == st->second.their_have.end() || !field->is_object()) {
        return 0;
    }
    const auto last = field->find(machine);
    return last != field->end() && last->is_number_integer() ? last->get<std::int64_t>() : 0;
}

bool Peer::told(const std::string& library) const
{
    const auto st = state_.find(library);
    return st != state_.end() && allowed_.contains(library) && st->second.their_have.is_object();
}

bool Peer::coming(const std::string& uid)
{
    const auto it = state_.find(uid);
    const auto* lib = library(uid);
    if (closed_ || it == state_.end() || lib == nullptr || !allowed_.contains(uid)) {
        return false;
    }
    const auto& st = it->second;
    if (!st.requested.empty() || !st.pending.empty() || !st.waiting.empty() || !st.storing.empty()) {
        return true;
    }
    // Owed by a file stored without it, unless they have said they have not got it.
    if (const auto owed = shared_.owed_media.find(uid); owed != shared_.owed_media.end()) {
        for (const auto& [path, digest] : owed->second) {
            if (!st.missing_there.contains(item("media", path))) {
                return true;
            }
        }
    }
    if (!st.their_have.is_object()) {
        return false;
    }
    // Media missing here and not asked for over this connection yet, as only a
    // few files are at a time: it may come from them, until they say they have
    // not got it. A file they do not have holds nothing back.
    if (!halted() && lib->missing_media) {
        for (const auto& digest : lib->missing_media()) {
            const auto name = item("media", digest.path);
            if (!st.missing_there.contains(name) && !st.refused.contains(name) && !st.not_yet.contains(name)) {
                return true;
            }
        }
    }
    if (halted()) {
        // Paused, either end: nothing can be asked for, nor whether it may be
        // (the member list may be what is missing), so any member list entry
        // or change file they have past the last here is still to come.
        for (const auto& [field, kind] : {std::pair{"members", sync::Numbered::Members},
                                          std::pair{"changes", sync::Numbered::Changes}}) {
            const auto lasts = st.their_have.find(field);
            if (lasts == st.their_have.end() || !lasts->is_object()) {
                continue;
            }
            for (const auto& [machine, last] : lasts->items()) {
                if (!sync::is_machine_id(machine) || !last.is_number_integer()) {
                    continue;
                }
                const auto mine = lib->store->sequences(kind, machine);
                auto here = mine.empty() ? std::int64_t{0} : mine.back();
                if (kind == sync::Numbered::Changes) {
                    here = std::max(here, lib->store->dropped_through(machine));
                }
                if (last.get<std::int64_t>() > here) {
                    return true;
                }
            }
        }
        return false;
    }
    // A change file they have and this computer has not, as ``want_more``
    // would ask for it.
    const auto may_ask = [&](const std::string& name) {
        return !st.refused.contains(name) && !st.not_yet.contains(name) && !st.missing_there.contains(name);
    };
    const auto changes = st.their_have.find("changes");
    if (changes == st.their_have.end() || !changes->is_object()) {
        return false;
    }
    std::optional<sync::Gate> files_gate;
    for (const auto& [machine, last] : changes->items()) {
        if (!sync::is_machine_id(machine) || !last.is_number_integer()) {
            continue;
        }
        const auto mine = lib->store->sequences(sync::Numbered::Changes, machine);
        const std::set<std::int64_t> here(mine.begin(), mine.end());
        for (auto n = lib->store->dropped_through(machine) + 1; n <= last.get<std::int64_t>(); ++n) {
            if (here.contains(n) || !may_ask(item(StoreFile::Change, machine, n))) {
                continue;
            }
            if (!files_gate) {
                files_gate.emplace(gate(*lib));
            }
            if (!files_gate->may_count(machine, n)) {
                break;  // nor any later one of this computer's, for now
            }
            return true;
        }
    }
    return false;
}

void Peer::fetch_more(const std::string& uid)
{
    if (ready_ && !closed_ && allowed_.contains(uid)) {
        want_more(uid);
    }
}

void Peer::take_have(const SyncedLibrary& lib, const json& message)
{
    auto& st = state_[lib.uid];
    st.their_have = message;
    st.missing_there.clear();
    if (const auto applied = message.find("applied"); applied != message.end() && applied->is_object()) {
        std::map<std::string, std::int64_t> said;
        for (const auto& [machine, n] : applied->items()) {
            if (sync::is_machine_id(machine) && n.is_number_integer() && n.get<std::int64_t>() >= 0) {
                said.emplace(machine, n.get<std::int64_t>());
            }
        }
        const auto list = members(lib);
        if (const auto* member = key_ ? list.with_key(*key_) : nullptr) {
            emit progress(lib.uid, member->machine, said);
        }
    }
    want_more(lib.uid);
}

json Peer::have(const SyncedLibrary& lib) const
{
    const auto last_of = [&](sync::Numbered kind) {
        json out = json::object();
        for (const auto& machine : lib.store->machines(kind)) {
            const auto sequences = lib.store->sequences(kind, machine);
            if (!sequences.empty()) {
                out[machine] = sequences.back();
            }
        }
        return out;
    };
    json snapshots = json::array();
    for (const auto& name : lib.store->snapshots()) {
        snapshots.push_back({name.machine, name.files});
    }
    json out{{"type", "have"},
             {"library", lib.uid},
             {"members", last_of(sync::Numbered::Members)},
             {"log", last_of(sync::Numbered::Log)},
             {"admin_keys", lib.store->admin_keys()},
             {"changes", last_of(sync::Numbered::Changes)},
             {"snapshots", std::move(snapshots)}};
    if (lib.applied) {
        out["applied"] = lib.applied();
    }
    return out;
}

bool Peer::wants_admin_keys(const SyncedLibrary& lib)
{
    // On the list, the list says, as it may say so after this computer joined.
    const auto list = members(lib);
    if (const auto* me = list.find(me_.machine)) {
        return me->role != sync::Role::Viewer || me->holds_admin_key;
    }
    for (const auto& j : joining_) {
        if (j.library == lib.uid) {
            try {
                return sync::join_entry_invite(j.entry).invite.role != sync::Role::Viewer;
            } catch (const std::exception&) {
                return false;
            }
        }
    }
    return true;
}

void Peer::want_more(const std::string& uid)
{
    auto& st = state_[uid];
    const auto* lib = library(uid);
    // Asked again once what was asked for has come. Media and snapshots'
    // parts aside: a video can take an hour, and an edit made after it is not
    // to wait that long.
    const bool asking = std::ranges::any_of(st.requested, [](const std::string& name) {
        return !name.starts_with("media/") && !is_item(StoreFile::Part, name);
    });
    if (lib == nullptr || st.their_have.is_null()) {
        return;
    }
    if (asking) {
        want_media(uid);
        return;
    }
    const auto asked_before = st.requested.size();
    // Halted, the shared log alone: it changes nothing in the library.
    const bool log_only = halted();
    std::set<std::string> held = st.storing;
    for (const auto& p : st.pending) {
        held.insert(item(StoreFile::Change, p.machine, p.number));
    }
    for (const auto& w : st.waiting) {
        held.insert(w.name());
    }
    json want{{"type", "want"},           {"library", uid},           {"members", json::array()},
              {"log", json::array()},        {"admin_keys", json::array()}, {"changes", json::array()},
              {"snapshots", json::array()}, {"parts", json::array()},     {"media", json::array()}};
    int count = 0;
    const auto skip = [&](const std::string& name) {
        return st.refused.contains(name) || st.not_yet.contains(name) || held.contains(name)
               || st.missing_there.contains(name);
    };
    const auto numbered = [&](sync::Numbered kind, const char* field, const char* key) {
        const auto it = st.their_have.find(field);
        if (it == st.their_have.end() || !it->is_object()) {
            return;
        }
        std::optional<sync::Gate> files_gate;
        for (const auto& [machine, last] : it->items()) {
            if (!sync::is_machine_id(machine) || !last.is_number_integer()) {
                continue;
            }
            const auto mine = lib->store->sequences(kind, machine);
            const std::set<std::int64_t> have_here(mine.begin(), mine.end());
            // Every one missing here, gaps too: a file can be stored out of
            // turn (one held for its media while a later one is not). But
            // for change files let go of here as old.
            const auto from = kind == sync::Numbered::Changes ? lib->store->dropped_through(machine) + 1 : 1;
            for (std::int64_t n = from; n <= last.get<std::int64_t>() && count < k_want_batch; ++n) {
                const auto name = item(key, machine, n);
                if (have_here.contains(n) || skip(name)) {
                    continue;
                }
                if (kind == sync::Numbered::Changes) {
                    if (!files_gate) {
                        files_gate.emplace(gate(*lib));
                    }
                    if (!files_gate->may_count(machine, n)) {
                        break;  // nor any later one of this computer's, for now
                    }
                }
                if (!fetches_.claim(uid, name, this)) {
                    continue;  // another connection is fetching it
                }
                want[field].push_back({machine, n});
                st.requested.insert(name);
                ++count;
            }
        }
    };
    numbered(sync::Numbered::Log, "log", "log");
    // Only what is new: what is on its way is not asked for again.
    const auto asked_more = [&] { return st.requested.size() > asked_before; };
    if (log_only) {
        if (asked_more()) {
            send(want);
        }
        return;
    }
    numbered(sync::Numbered::Members, "members", "member");
    if (wants_admin_keys(*lib)) {
        const auto mine = lib->store->admin_keys();
        for (const auto& fp : list_of(st.their_have, "admin_keys")) {
            if (!fp.is_string()) {
                continue;
            }
            const auto name = item("admin_key", fp.get<std::string>());
            if (sync::is_fingerprint(fp.get<std::string>())
                && std::ranges::find(mine, fp.get<std::string>()) == mine.end() && !skip(name)
                && fetches_.claim(uid, name, this)) {
                want["admin_keys"].push_back(fp);
                st.requested.insert(name);
            }
        }
    }
    numbered(sync::Numbered::Changes, "changes", store_file_name(StoreFile::Change));
    // Only the newest snapshot they have that may be asked for, newer than any
    // here, and one at a time: an older one holds nothing more, and each
    // waits here, index and all, for its parts.
    const auto is_snapshot = [](const std::string& name) { return is_item(StoreFile::Snapshot, name); };
    if (st.waiting.empty() && std::ranges::none_of(st.requested, is_snapshot)
        && std::ranges::none_of(st.storing, is_snapshot)) {
        std::optional<sync::SnapshotName> newest;
        for (const auto& s : list_of(st.their_have, "snapshots")) {
            if (!s.is_array() || s.size() != 2 || !s[0].is_string() || !s[1].is_number_integer()) {
                continue;
            }
            const sync::SnapshotName name{s[0].get<std::string>(), s[1].get<std::int64_t>()};
            if (sync::is_machine_id(name.machine) && (!newest || name.files > newest->files)
                && !skip(item(StoreFile::Snapshot, name.machine, name.files))) {
                newest = name;
            }
        }
        if (newest && newest->files > newest_snapshot(*lib)) {
            const auto key = item(StoreFile::Snapshot, newest->machine, newest->files);
            // Fetched by another connection, it is not; nor an older one.
            if (fetches_.claim(uid, key, this)) {
                want["snapshots"].push_back({newest->machine, newest->files});
                st.requested.insert(key);
            }
        }
    }
    ask_parts(uid, want);
    std::set<Peer*> better;
    ask_media(uid, want, better);
    if (asked_more()) {
        send(want);
    }
    leave_to(better, uid);
}

void Peer::ask_parts(const std::string& uid, json& want)
{
    auto& st = state_[uid];
    auto asked = static_cast<std::size_t>(
        std::ranges::count_if(st.requested, [](const std::string& name) { return is_item(StoreFile::Part, name); }));
    for (const auto& w : st.waiting) {
        for (const auto& [sha256, size] : w.needs) {
            if (asked >= k_parts_at_once) {
                return;
            }
            if (st.requested.insert(part_item(sha256)).second) {
                want["parts"].push_back(sha256);
                ++asked;
                files_heard_.restart();  // given its time to come
            }
        }
    }
}

std::int64_t Peer::newest_snapshot(const SyncedLibrary& lib)
{
    std::int64_t newest = 0;
    for (const auto& s : lib.store->snapshots()) {
        newest = std::max(newest, s.files);
    }
    return newest;
}

void Peer::want_media(const std::string& uid)
{
    const auto st = state_.find(uid);
    if (!ready_ || closed_ || halted() || !allowed_.contains(uid) || library(uid) == nullptr || st == state_.end()
        || st->second.their_have.is_null()) {
        return;
    }
    json want{{"type", "want"}, {"library", uid}, {"parts", json::array()}, {"media", json::array()}};
    ask_parts(uid, want);
    std::set<Peer*> better;
    ask_media(uid, want, better);
    if (!want["media"].empty() || !want["parts"].empty()) {
        send(want);
    }
    leave_to(better, uid);
}

void Peer::ask_media(const std::string& uid, json& want, std::set<Peer*>& better)
{
    auto& st = state_[uid];
    const auto* lib = library(uid);
    if (lib == nullptr) {
        return;
    }
    // What is left to each better connection so far: no more than it has room
    // for, so that this one fetches the rest meanwhile, rather than wait.
    std::map<const Peer*, MediaFree> leaving;
    // ``anyones``: missing here, or owed, so any connection may fetch it, and a
    // better one should. What a change file held here waits for, the others do
    // not know of.
    const auto ask = [&](const std::string& path, bool anyones) {
        const auto name = item("media", path);
        const auto expected = st.media_expected.find(path);
        if (expected == st.media_expected.end() || st.requested.contains(name) || st.refused.contains(name)
            || st.not_yet.contains(name) || st.missing_there.contains(name) || fetches_.claimed(uid, name)
            || !media_in_.free_to_begin(uid, path) || !media_room(expected->second.size)) {
            return;
        }
        if (anyones && shared_.better_for) {
            if (auto* other = shared_.better_for(*this, uid, expected->second)) {
                const bool large = expected->second.size >= k_large_media;
                const auto room = other->media_free();
                auto& left = leaving[other];
                if (left.files < room.files && (!large || left.large < room.large)) {
                    ++left.files;
                    left.large += large ? 1 : 0;
                    better.insert(other);
                    left_.insert(uid);
                    return;
                }
            }
        }
        if (!fetches_.claim(uid, name, this)) {
            return;
        }
        // Claimed: the part on the disk is this connection's to write.
        want["media"].push_back({{"path", path}, {"offset", media_in_.begin(*lib, expected->second)}});
        st.requested.insert(name);
    };
    for (const auto& p : st.pending) {
        for (const auto& path : p.needs) {
            ask(path, false);
        }
    }
    if (const auto owed = shared_.owed_media.find(uid); owed != shared_.owed_media.end()) {
        auto room = lib->media_room ? lib->media_room() : std::int64_t{-1};
        for (auto it = owed->second.begin(); it != owed->second.end();) {
            std::error_code ec;
            if (std::filesystem::exists(lib->media / it->first, ec)) {
                it = owed->second.erase(it);  // come meanwhile, by another way
                continue;
            }
            if (room >= 0) {
                // Past the size limit as it is now: owed no longer, and
                // fetched only if the library finds room for it.
                if (it->second.size > room) {
                    it = owed->second.erase(it);
                    continue;
                }
                room -= it->second.size;
            }
            st.media_expected[it->first] = it->second;
            ask(it->first, true);
            ++it;
        }
    }
    // Not looked through once there is no room: a file that comes makes room
    // for one, and a library may be missing thousands.
    if (lib->missing_media && media_free().files > 0) {
        for (const auto& digest : lib->missing_media()) {
            st.media_expected[digest.path] = digest;
            ask(digest.path, true);
            if (media_free().files == 0) {
                break;
            }
        }
    }
}

void Peer::leave_to(const std::set<Peer*>& better, const std::string& uid)
{
    // Not at once: that one may be asking, or this may be one of its own asks.
    for (auto* other : better) {
        QMetaObject::invokeMethod(other, [other, uid] { other->want_media(uid); }, Qt::QueuedConnection);
    }
}

void Peer::serve(const SyncedLibrary& lib, const json& want)
{
    // Each numbered file asked for, or ``missing`` when it is not here.
    const auto numbered = [&](const char* field, const char* type, const char* number_name,
                              auto read) {
        for (const auto& entry : list_of(want, field)) {
            if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string()
                || !entry[1].is_number_integer()) {
                malformed("a want that is not one");
            }
            const auto machine = entry[0].get<std::string>();
            const auto n = entry[1].get<std::int64_t>();
            try {
                send({{"type", type},
                      {"library", lib.uid},
                      {"machine", machine},
                      {number_name, n},
                      {"text", read(machine, n)}});
            } catch (const std::exception&) {
                send({{"type", "missing"},
                      {"library", lib.uid},
                      {"kind", type},
                      {"machine", machine},
                      {number_name, n}});
            }
        }
    };
    numbered("log", "log", "sequence", [&](const std::string& machine, std::int64_t n) {
        return lib.store->read(sync::Numbered::Log, machine, n);
    });
    if (halted()) {
        // The rest was asked before the pause was heard: let go of there,
        // once it is.
        return;
    }
    numbered("members", "member", "sequence", [&](const std::string& machine, std::int64_t n) {
        return lib.store->read(sync::Numbered::Members, machine, n);
    });
    const bool admin_keys = may_hold_admin_key(lib);
    for (const auto& fp : list_of(want, "admin_keys")) {
        if (!fp.is_string()) {
            malformed("a want that is not one");
        }
        if (!admin_keys) {
            // A viewer holds no copy of the locked admin key: one asked for
            // is not there, for it.
            send({{"type", "missing"}, {"library", lib.uid}, {"kind", "admin_key"}, {"fingerprint", fp}});
            continue;
        }
        try {
            send({{"type", "admin_key"},
                  {"library", lib.uid},
                  {"fingerprint", fp},
                  {"text", lib.store->read_admin_key(fp.get<std::string>())}});
        } catch (const std::exception&) {
            send({{"type", "missing"}, {"library", lib.uid}, {"kind", "admin_key"}, {"fingerprint", fp}});
        }
    }
    // Change files, snapshots' indexes and parts are read when their turn
    // comes, one at a time: two hundred asked for at once are not all held in memory.
    for (const auto& [field, kind] : {std::pair{"changes", StoreFile::Change}, {"snapshots", StoreFile::Snapshot}}) {
        for (const auto& entry : list_of(want, field)) {
            if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string()
                || !entry[1].is_number_integer()) {
                malformed("a want that is not one");
            }
            outbox_.file(lib.uid, kind, entry[0].get<std::string>(), entry[1].get<std::int64_t>());
        }
    }
    for (const auto& sha256 : list_of(want, "parts")) {
        if (!sha256.is_string() || !sync::is_part_name(sha256.get_ref<const std::string&>())) {
            malformed("a want that is not one");
        }
        outbox_.part(lib.uid, sha256.get<std::string>());
    }
    for (const auto& m : list_of(want, "media")) {
        if (!m.is_object()) {
            malformed("a want that is not one");
        }
        outbox_.media(lib, media_path_of(m), number_of(m, "offset"));
    }
    outbox_.pump();
}

void Peer::got(const std::string& uid, const std::string& name, Got how)
{
    auto& st = state_[uid];
    st.requested.erase(name);
    if (name.starts_with("media/")) {
        media_in_.forget(uid, name.substr(6));
        handing_to_.erase({uid, name.substr(6)});
    }
    // A file held for its media stays this connection's until it is kept.
    if (how != Got::Held) {
        fetches_.release(uid, name, this);
    }
    if (how == Got::NotHere) {
        emit let_go(uid);
    }
    // Held, what it waits for is asked for now: a snapshot's parts are not to
    // wait behind a video.
    if (st.requested.empty()) {
        want_more(uid);
    } else if (how == Got::Held || name.starts_with("media/") || is_item(StoreFile::Part, name)) {
        want_media(uid);  // room for another, or what it waits for
    }
}

void Peer::take_missing(const SyncedLibrary& lib, const json& m)
{
    const auto kind = text_of(m, "kind");
    if (store_file_kind(kind) == StoreFile::Part) {
        part_missing_there(lib, part_name_of(m));
        return;
    }
    auto& st = state_[lib.uid];
    std::string name;
    if (kind == "media") {
        const auto path = media_path_of(m);
        name = item("media", path);
        for (auto& p : st.pending) {
            p.needs.erase(path);  // kept without it; fetched once someone has it
        }
        settle_pending(lib.uid);
    } else if (kind == "admin_key") {
        name = item("admin_key", text_of(m, "fingerprint"));
    } else {
        name = item(kind.c_str(), text_of(m, "machine"),
                    number_of(m, store_file_kind(kind) == StoreFile::Snapshot ? "files" : "sequence"));
    }
    st.missing_there.insert(name);
    got(lib.uid, name, Got::NotHere);
}

}  // namespace seventhslide::link
