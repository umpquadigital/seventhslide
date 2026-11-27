// Peer: snapshots' parts, checked and written off this thread, and the
// indexes waiting for them: stored once they are all here, or let go of.

#include "seventhslide/link/peer.hpp"

#include "peer_internal.hpp"

#include "seventhslide/link/off_thread.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::link {

void Peer::take_part(const SyncedLibrary& lib, const json& m)
{
    files_heard_.restart();
    const auto sha256 = part_name_of(m);
    auto text = text_of(m, "text");
    if (state_[lib.uid].requested.contains(part_item(sha256))
        && !size_as_named(lib.uid, sha256, static_cast<std::int64_t>(text.size()))) {
        return;
    }
    part_came(lib, sha256, std::move(text));
}

bool Peer::size_as_named(const std::string& uid, const std::string& sha256, std::int64_t size)
{
    auto& st = state_[uid];
    if (std::ranges::none_of(st.waiting, [&](const Waiting& w) {
            const auto need = w.needs.find(sha256);
            return need != w.needs.end() && need->second != size;
        })) {
        return true;
    }
    // The disk's room was counted by the size named.
    emit refused(QStringLiteral("A sync snapshot's part was not of the size its snapshot names, and was dropped: %1 "
                                "(%2 bytes)")
                     .arg(QString::fromStdString(sha256))
                     .arg(size));
    drop_waiting(uid, sha256, false);
    got(uid, part_item(sha256), Got::NotHere);
    return false;
}

void Peer::settle_waiting(const std::string& uid)
{
    const auto* lib = library(uid);
    const auto found = state_.find(uid);
    if (lib == nullptr || closed_ || found == state_.end() || found->second.waiting.empty()) {
        return;
    }
    auto& st = found->second;
    // One no newer than a snapshot stored meanwhile, through any connection,
    // holds nothing more.
    const auto newest = newest_snapshot(*lib);
    let_go_waiting(uid, [&](const Waiting& w) { return w.files <= newest; }, nullptr);
    for (auto it = st.waiting.begin(); it != st.waiting.end();) {
        if (!it->needs.empty()) {
            ++it;
            continue;
        }
        store_file(*lib, true, it->machine, it->files, it->text);
        it = st.waiting.erase(it);
    }
}

void Peer::part_arrived(const std::string& uid, const std::string& sha256)
{
    const auto it = state_.find(uid);
    if (it == state_.end()) {
        return;
    }
    for (auto& w : it->second.waiting) {
        w.needs.erase(sha256);
    }
    const auto asked = it->second.requested.size();
    forget_unneeded_parts(it->second);
    settle_waiting(uid);
    // Room to ask for others.
    if (it->second.requested.size() < asked) {
        want_media(uid);
    }
}

void Peer::forget_unneeded_parts(LibraryState& st)
{
    const auto prefix = part_item("");
    std::erase_if(st.requested, [&](const std::string& name) {
        return name.starts_with(prefix) && std::ranges::none_of(st.waiting, [&](const Waiting& w) {
                   return w.needs.contains(name.substr(prefix.size()));
               });
    });
}

void Peer::part_came(const SyncedLibrary& lib, const std::string& sha256, std::string text)
{
    const auto name = item("part", sha256);
    if (halted() || !state_[lib.uid].requested.contains(name)) {
        return;  // let go of meanwhile
    }
    emit files_coming(lib.uid);
    // Still asked for while it is checked and written: not asked again.
    run_off_thread(
        this, shared_.files,
        [store = lib.store, sha256, text = std::move(text)] {
            PartCheck out;
            try {
                sync::Gate::snapshot_part(sha256, text);
            } catch (const std::exception& ex) {
                out.problem = ex.what();
                return out;
            }
            try {
                store->write_checked_part(sha256, text);
            } catch (const std::exception& ex) {
                out.problem = ex.what();
                out.ours = true;
            }
            return out;
        },
        [this, uid = lib.uid, sha256](const PartCheck& checked) { part_checked(uid, sha256, checked); });
}

void Peer::part_checked(const std::string& uid, const std::string& sha256, const PartCheck& checked)
{
    const auto name = part_item(sha256);
    const auto it = state_.find(uid);
    if (closed_ || library(uid) == nullptr || it == state_.end() || !it->second.requested.contains(name)) {
        return;  // let go of meanwhile: written or not, nothing waits for it here
    }
    files_heard_.restart();
    if (checked.problem.empty()) {
        emit part_stored(uid, sha256);  // the other connections need it no more
        part_arrived(uid, sha256);
        got(uid, name, Got::Stored);
        return;
    }
    emit refused(QString::fromStdString(checked.problem));
    drop_waiting(uid, sha256, checked.ours);
    got(uid, name, Got::NotHere);
}

void Peer::drop_waiting(const std::string& uid, const std::string& sha256, bool ours)
{
    auto& st = state_[uid];
    if (let_go_waiting(uid, [&](const Waiting& w) { return w.needs.contains(sha256); },
                       ours ? &st.not_yet : &st.missing_there)) {
        emit let_go(uid);
    }
}

bool Peer::let_go_waiting(const std::string& uid, const std::function<bool(const Waiting&)>& which,
                          std::set<std::string>* mark)
{
    auto& st = state_[uid];
    bool any = false;
    for (auto it = st.waiting.begin(); it != st.waiting.end();) {
        if (!which(*it)) {
            ++it;
            continue;
        }
        const auto name = it->name();
        if (mark != nullptr) {
            mark->insert(name);
        }
        fetches_.release(uid, name, this);
        it = st.waiting.erase(it);
        any = true;
    }
    if (any) {
        forget_unneeded_parts(st);
    }
    return any;
}

void Peer::let_go_of_quiet_parts()
{
    if (halted() || files_heard_.elapsed() < shared_.parts_quiet_ms) {
        return;
    }
    std::vector<std::string> quiet;
    for (auto& [uid, st] : state_) {
        if (let_go_waiting(uid, [](const Waiting&) { return true; }, &st.missing_there)) {
            quiet.push_back(uid);
        }
    }
    for (const auto& uid : quiet) {
        qCDebug(lc_link).noquote() << QString::fromStdString(me_.machine) << "lets go of a snapshot of"
                                   << QString::fromStdString(uid) << ": no part came from"
                                   << QString::fromStdString(machine_);
        emit let_go(uid);
    }
}

void Peer::part_missing_there(const SyncedLibrary& lib, const std::string& sha256)
{
    const auto name = part_item(sha256);
    if (!state_[lib.uid].requested.contains(name)) {
        return;
    }
    // Here meanwhile (by another connection, or this computer's own
    // snapshot), it is not waited for. Else its snapshot cannot be had whole
    // from them.
    run_off_thread(
        this, shared_.files, [store = lib.store, sha256] { return store->keep_part(sha256); },
        [this, uid = lib.uid, sha256, name](bool here) {
            const auto it = state_.find(uid);
            if (closed_ || library(uid) == nullptr || it == state_.end() || !it->second.requested.contains(name)) {
                return;  // let go of meanwhile
            }
            if (here) {
                part_arrived(uid, sha256);
                got(uid, name, Got::Stored);
                return;
            }
            drop_waiting(uid, sha256, false);
            got(uid, name, Got::NotHere);
        });
}

}  // namespace seventhslide::link
