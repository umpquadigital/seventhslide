// Peer: member list entries, shared log entries and admin keys as they come,
// and change files and snapshots' indexes checked at the gate off this thread,
// held for their media, and stored.

#include "seventhslide/link/peer.hpp"

#include "peer_internal.hpp"

#include "seventhslide/link/off_thread.hpp"
#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/member_list.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

namespace seventhslide::link {
namespace {

/// The bytes free on the disk at ``path``: ``free_space`` when given, else
/// as the system says; -1 when it cannot be told.
std::int64_t free_on(const std::function<std::int64_t(const std::filesystem::path&)>& free_space,
                     const std::filesystem::path& path)
{
    if (free_space) {
        return free_space(path);
    }
    std::error_code ec;
    const auto space = std::filesystem::space(path, ec);
    return ec ? -1 : static_cast<std::int64_t>(space.available);
}

}  // namespace

void Peer::take_store_file(const SyncedLibrary& lib, const json& m)
{
    files_heard_.restart();
    const bool snapshot = store_file_kind(text_of(m, "type")) == StoreFile::Snapshot;
    take_file(lib, text_of(m, "machine"), number_of(m, snapshot ? "files" : "sequence"), text_of(m, "text"),
              snapshot);
}

void Peer::take_member(const SyncedLibrary& lib, const json& m)
{
    const auto machine = text_of(m, "machine");
    const auto n = number_of(m, "sequence");
    const auto text = text_of(m, "text");
    const auto name = item("member", machine, n);
    auto how = Got::NotHere;
    try {
        gate(lib).member_entry(machine, n, text);
        lib.store->write(sync::Numbered::Members, machine, n, text);
        store_changed(lib.uid);
        how = Got::Stored;
    } catch (const sync::NotYet&) {
        state_[lib.uid].not_yet.insert(name);
    } catch (const std::exception& ex) {
        state_[lib.uid].refused.insert(name);
        emit refused(QString::fromUtf8(ex.what()));
    }
    got(lib.uid, name, how);
}

void Peer::take_log(const SyncedLibrary& lib, const json& m)
{
    const auto machine = text_of(m, "machine");
    const auto n = number_of(m, "sequence");
    const auto text = text_of(m, "text");
    const auto name = item("log", machine, n);
    if (!sync::is_machine_id(machine) || n < 1) {
        malformed("a shared log entry that is not one");
    }
    auto how = Got::NotHere;
    try {
        gate(lib).log_entry(machine, n, text);
        lib.store->write(sync::Numbered::Log, machine, n, text);
        logged_.insert(lib.uid);
        how = Got::Stored;
    } catch (const sync::NotYet&) {
        state_[lib.uid].not_yet.insert(name);
    } catch (const std::exception& ex) {
        state_[lib.uid].refused.insert(name);
        emit refused(QString::fromUtf8(ex.what()));
    }
    got(lib.uid, name, how);
}

void Peer::take_admin_key(const SyncedLibrary& lib, const json& m)
{
    const auto fingerprint = text_of(m, "fingerprint");
    const auto text = text_of(m, "text");
    const auto name = item("admin_key", fingerprint);
    auto how = Got::NotHere;
    try {
        gate(lib).admin_key(fingerprint, text);
        lib.store->write_admin_key(fingerprint, text);
        store_changed(lib.uid);
        how = Got::Stored;
    } catch (const std::exception& ex) {
        state_[lib.uid].refused.insert(name);
        emit refused(QString::fromUtf8(ex.what()));
    }
    got(lib.uid, name, how);
}

void Peer::take_file(const SyncedLibrary& lib, std::string machine, std::int64_t number,
                     std::string text, bool snapshot)
{
    if (halted()) {
        return;  // on its way when the pause came: let go of, and fetched again later
    }
    arrived_.push_back({lib.uid, snapshot, std::move(machine), number,
                        std::make_shared<const std::string>(std::move(text))});
    check_next();
}

void Peer::check_next()
{
    while (!checking_file_ && !arrived_.empty()) {
        auto file = std::move(arrived_.front());
        arrived_.pop_front();
        const auto* lib = library(file.library);
        const auto st = state_.find(file.library);
        // Only a file asked for, and not let go of since (a pause).
        if (lib == nullptr || st == state_.end() || !st->second.requested.contains(file.name())) {
            continue;
        }
        checking_file_ = true;
        // Of the list as it is now: the gate's copy goes with the file.
        auto list = members(*lib);
        run_off_thread(
            this, shared_.files,
            [file, list = std::move(list), store = lib->store, media = lib->media, free_space = shared_.free_space] {
                FileCheck out;
                try {
                    const sync::Gate gate(list);
                    if (file.snapshot) {
                        // Each part here is kept a while longer: this one
                        // is about to name it.
                        std::int64_t bytes = 0;
                        const auto parts = gate.snapshot({file.machine, file.number}, *file.text).parts;
                        if (parts.size() > k_most_parts) {
                            out.refused = "A sync snapshot names more parts than any library has ("
                                          + std::to_string(parts.size()) + ")";
                            return out;
                        }
                        for (const auto& part : parts) {
                            if (!store->keep_part(part.sha256)) {
                                out.parts_absent.emplace(part.sha256, part.size);
                                bytes += part.size;
                            }
                        }
                        // Room for them, and for as much again as one file
                        // more: what else comes meanwhile.
                        const auto free = bytes > 0 ? free_on(free_space, media) : -1;
                        if (free >= 0 && free < bytes + sync::k_largest_store_file) {
                            constexpr std::int64_t mb = 1000 * 1000;
                            out.refused = "A sync snapshot was not taken for now: there is not room for it on this "
                                          "computer's disk ("
                                          + std::to_string((bytes + mb - 1) / mb) + " MB, and "
                                          + std::to_string(free / mb) + " MB free)";
                            out.not_yet = true;
                            out.no_room = true;
                        }
                        return out;
                    }
                    for (const auto& digest : gate.read_change_file(file.machine, file.number, *file.text).media) {
                        std::error_code ec;
                        if (!std::filesystem::exists(media / digest.path, ec)) {
                            out.absent.push_back(digest);
                        }
                    }
                } catch (const sync::NotYet& ex) {
                    out.refused = ex.what();
                    out.not_yet = true;
                } catch (const std::exception& ex) {
                    out.refused = ex.what();
                }
                return out;
            },
            [this, file](FileCheck checked) { file_checked(file, std::move(checked)); });
    }
}

void Peer::file_checked(const Arrived& file, FileCheck checked)
{
    checking_file_ = false;
    const auto name = file.name();
    const auto* lib = library(file.library);
    const auto it = state_.find(file.library);
    // Let go of meanwhile (a pause, the library gone, the connection closed):
    // fetched again, from here or elsewhere.
    if (closed_ || halted() || lib == nullptr || it == state_.end() || !it->second.requested.contains(name)) {
        check_next();
        return;
    }
    auto& st = it->second;
    // Refused for now as its writer was not on the list it was checked
    // against: when the writer is on it by now (an entry came, through any
    // connection), it is asked for again rather than waiting for the next.
    bool ask_again = checked.not_yet && members(*lib).find(file.machine) != nullptr;
    if (checked.refused.empty() && !file.snapshot) {
        try {
            gate(*lib).admit_change_file(file.machine, file.number);
        } catch (const sync::NotYet& ex) {
            checked.refused = ex.what();
            checked.not_yet = true;
            ask_again = false;  // asked of the list as it is now
        }
    }
    auto how = Got::NotHere;
    if (checked.refused.empty() && file.snapshot) {
        // Kept once its parts are; it stays this connection's to fetch
        // meanwhile.
        st.waiting.push_back({file.machine, file.number, file.text, std::move(checked.parts_absent)});
        how = Got::Held;
        settle_waiting(file.library);
    } else if (checked.refused.empty()) {
        Pending pending{file.machine, file.number, file.text, {}};
        // What does not fit the size limit is not waited for: the file is
        // kept without it, and fetched later only if it fits.
        auto room = lib->media_room ? lib->media_room() : std::int64_t{-1};
        for (const auto& digest : checked.absent) {
            std::error_code ec;
            if (std::filesystem::exists(lib->media / digest.path, ec)) {
                continue;  // come meanwhile
            }
            if (room >= 0) {
                if (digest.size > room) {
                    continue;
                }
                room -= digest.size;
            }
            pending.needs.insert(digest.path);
            st.media_expected[digest.path] = digest;
        }
        st.pending.push_back(std::move(pending));
        how = Got::Held;  // kept by settle_pending, now or once its media is here
    } else if (checked.no_room) {
        // Asked for again once they next say what they have.
        st.missing_there.insert(name);
        emit refused(QString::fromStdString(checked.refused));
    } else if (checked.not_yet) {
        if (!ask_again) {
            st.not_yet.insert(name);
        }
        emit refused(QString::fromStdString(checked.refused));
    } else {
        st.refused.insert(name);
        emit refused(QString::fromStdString(checked.refused));
    }
    settle_pending(file.library);
    got(file.library, name, how);
    check_next();
}

void Peer::store_file(const SyncedLibrary& lib, bool snapshot, const std::string& machine, std::int64_t number,
                      std::shared_ptr<const std::string> text)
{
    const auto uid = lib.uid;
    const auto name = item(snapshot ? StoreFile::Snapshot : StoreFile::Change, machine, number);
    state_[uid].storing.insert(name);
    run_off_thread(
        this, shared_.files,
        [store = lib.store, vouches = lib.vouches, snapshot, machine, number, text] {
            StoreOutcome out;
            try {
                if (snapshot) {
                    // Each part once more, kept a while longer: housekeeping
                    // may have let one go since it was found here.
                    for (const auto& part : sync::snapshot_index_label(sync::unseal(*text, "A sync snapshot").body).parts) {
                        if (!store->keep_part(part.sha256)) {
                            out.gone.emplace(part.sha256, part.size);
                        }
                    }
                    if (!out.gone.empty()) {
                        return out;
                    }
                    store->write_snapshot({machine, number}, *text);
                } else {
                    store->write(sync::Numbered::Changes, machine, number, *text);
                }
            } catch (const std::exception& ex) {
                out.problem = ex.what();  // there already, or the disk
                return out;
            }
            // Before the window hears it is stored, so what it vouches for is
            // let in when asked for next.
            if (vouches) {
                if (snapshot) {
                    vouches->stored(sync::SnapshotName{machine, number});
                } else {
                    vouches->stored(machine, number);
                }
            }
            return out;
        },
        [this, uid, name, machine, number, text](const StoreOutcome& out) {
            if (const auto it = state_.find(uid); it != state_.end()) {
                it->second.storing.erase(name);
            }
            // A part gone: fetched again, and the index waits for it as before.
            if (!out.gone.empty() && !closed_ && !halted() && library(uid) != nullptr && allowed_.contains(uid)) {
                state_[uid].waiting.push_back({machine, number, text, out.gone});
                want_media(uid);
                return;
            }
            fetches_.release(uid, name, this);
            if (!out.gone.empty()) {
                emit let_go(uid);
            } else if (!out.problem.empty()) {
                emit refused(QString::fromStdString(out.problem));
            } else if (library(uid) != nullptr) {
                store_changed(uid);
            }
            tell_stored();
            // What waited for it (its writer, a file it vouches for) may be
            // asked for now.
            if (!closed_ && allowed_.contains(uid)) {
                want_more(uid);
            }
        });
}

void Peer::settle_pending(const std::string& uid)
{
    const auto* lib = library(uid);
    if (lib == nullptr || closed_) {
        return;
    }
    auto& st = state_[uid];
    const auto now = std::chrono::steady_clock::now();
    const auto longest = std::chrono::milliseconds(shared_.hold_for_media_ms);
    for (auto it = st.pending.begin(); it != st.pending.end();) {
        if (!it->needs.empty() && now - it->held_since < longest) {
            ++it;
            continue;
        }
        // What has not come is owed: fetched on, over any connection.
        owe(uid, *it);
        store_file(*lib, false, it->machine, it->number, it->text);
        it = st.pending.erase(it);
    }
    hold_until_next();
}

void Peer::owe(const std::string& uid, const Pending& file)
{
    const auto& st = state_[uid];
    for (const auto& path : file.needs) {
        if (const auto digest = st.media_expected.find(path); digest != st.media_expected.end()) {
            shared_.owed_media[uid][path] = digest->second;
        }
    }
}

void Peer::hold_until_next()
{
    std::optional<std::chrono::steady_clock::time_point> first;
    for (const auto& [uid, st] : state_) {
        for (const auto& p : st.pending) {
            if (!first || p.held_since < *first) {
                first = p.held_since;
            }
        }
    }
    if (!first) {
        held_long_enough_.stop();
        return;
    }
    const auto due = *first + std::chrono::milliseconds(shared_.hold_for_media_ms);
    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(due - std::chrono::steady_clock::now());
    held_long_enough_.start(static_cast<int>(std::max<std::int64_t>(0, wait.count())));
}

void Peer::store_held()
{
    for (auto& [uid, st] : state_) {
        const auto* lib = library(uid);
        if (lib == nullptr || st.pending.empty()) {
            continue;
        }
        for (const auto& file : st.pending) {
            owe(uid, file);
        }
        // Still fetched, until written: no other connection is to ask for it
        // meanwhile. Claimed by what outlasts this connection.
        std::vector<std::string> names;
        for (const auto& file : st.pending) {
            auto name = item(StoreFile::Change, file.machine, file.number);
            fetches_.release(uid, name, this);
            if (fetches_.claim(uid, name, &shared_.context)) {
                names.push_back(std::move(name));
            }
        }
        // Written as ``store_file`` writes, but reported through what lasts
        // longer than this connection: it is gone before the write is done.
        ++shared_.storing_held;
        run_off_thread(
            &shared_.context, shared_.files,
            [store = lib->store, vouches = lib->vouches, held = std::exchange(st.pending, {})] {
                bool any = false;
                for (const auto& file : held) {
                    try {
                        store->write(sync::Numbered::Changes, file.machine, file.number, *file.text);
                        if (vouches) {
                            vouches->stored(file.machine, file.number);
                        }
                        any = true;
                    } catch (const std::exception&) {
                        // There already (come through another connection), or
                        // the disk: fetched again as if never come.
                    }
                }
                return any;
            },
            [shared = &shared_, library = uid, names = std::move(names)](bool any) {
                --shared->storing_held;
                for (const auto& name : names) {
                    shared->fetches.release(library, name, &shared->context);
                }
                if (any && shared->stored) {
                    shared->stored(library);
                }
                // One that could not be written is for another to fetch.
                if (shared->part_free) {
                    shared->part_free(library);
                }
            });
    }
}

}  // namespace seventhslide::link
