// Peer: media shared between connections. How good a way this one is, a
// file handed over to a better one, what each end says is on its way, and
// what came of a file coming in (``MediaIn``).

#include "seventhslide/link/peer.hpp"

#include "peer_internal.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::link {
namespace {

/// The most files one ``coming`` message names: enough to say why, and what a
/// computer keeps of what another tells it stays small.
constexpr std::size_t k_max_coming = 1000;

}  // namespace

std::set<std::string> Peer::media_coming(const std::string& library) const
{
    std::set<std::string> out;
    const auto st = state_.find(library);
    if (closed_ || st == state_.end()) {
        return out;
    }
    for (const auto& name : st->second.requested) {
        if (name.starts_with("media/")) {
            out.insert(name.substr(6));
        }
    }
    for (const auto& pending : st->second.pending) {
        out.insert(pending.needs.begin(), pending.needs.end());
    }
    if (const auto owed = shared_.owed_media.find(library); owed != shared_.owed_media.end()) {
        for (const auto& [path, digest] : owed->second) {
            if (!st->second.missing_there.contains(item("media", path))) {
                out.insert(path);
            }
        }
    }
    return out;
}

std::map<std::string, Arriving> Peer::media_arriving(const std::string& library) const
{
    return closed_ ? std::map<std::string, Arriving>{} : media_in_.arriving(library);
}

std::set<std::string> Peer::media_going(const std::string& library) const
{
    return closed_ ? std::set<std::string>{} : outbox_.media_going(library);
}

bool Peer::said_missing(const std::string& library, const std::string& path) const
{
    const auto st = state_.find(library);
    return st != state_.end() && st->second.missing_there.contains(item("media", path));
}

bool Peer::says_coming(const std::string& library, const std::string& path) const
{
    const auto st = state_.find(library);
    return st != state_.end() && st->second.coming_there.contains(path);
}

Route Peer::route() const
{
    return {shared_.close_by && shared_.close_by(*this), in_rate_.per_second()};
}

void Peer::measure(double seconds)
{
    // Bytes came, so they were due; none came while some were, and that counts
    // as slow. Nothing due counts for nothing.
    const bool expecting = media_bytes_in_ > 0 || (!closed_ && !halted() && media_in_.expecting());
    in_rate_.sample(std::exchange(media_bytes_in_, 0), seconds, expecting);
}

bool Peer::could_take(const std::string& library, const db::MediaDigest& file) const
{
    const auto st = state_.find(library);
    if (!ready_ || closed_ || halted() || !allowed_.contains(library) || st == state_.end()
        || !st->second.their_have.is_object()) {
        return false;
    }
    const auto name = item("media", file.path);
    const auto& s = st->second;
    return !s.requested.contains(name) && !s.refused.contains(name) && !s.not_yet.contains(name)
           && !s.missing_there.contains(name) && media_room(file.size);
}

bool Peer::fetching_media() const
{
    return std::ranges::any_of(state_, [](const auto& entry) {
        return std::ranges::any_of(entry.second.requested,
                                   [](const std::string& name) { return name.starts_with("media/"); });
    });
}

std::map<std::string, Flowing> Peer::media_flowing(const std::string& library) const
{
    return closed_ ? std::map<std::string, Flowing>{} : media_in_.flowing(library);
}

void Peer::hand_over(const std::string& library, const std::string& path, Peer& to)
{
    if (!closed_ && media_in_.hand_over(library, path)) {
        handing_to_[{library, path}] = &to;
    }
}

bool Peer::take_over(const std::string& library, const db::MediaDigest& file)
{
    const auto* lib = this->library(library);
    const auto name = item("media", file.path);
    if (lib == nullptr || !could_take(library, file) || !media_in_.free_to_begin(library, file.path)
        || !fetches_.claim(library, name, this)) {
        return false;
    }
    auto& st = state_[library];
    st.media_expected[file.path] = file;
    send({{"type", "want"},
          {"library", library},
          {"media", json::array({{{"path", file.path}, {"offset", media_in_.begin(*lib, file)}}})}});
    st.requested.insert(name);
    return true;
}

void Peer::handed_over(const std::string& uid, const std::string& path)
{
    // What else of it is on its way is let by.
    send({{"type", "cancel"}, {"library", uid}, {"path", path}});
    auto& st = state_[uid];
    const auto name = item("media", path);
    st.requested.erase(name);
    fetches_.release(uid, name, this);
    const auto to = handing_to_.extract({uid, path});
    const auto digest = st.media_expected.find(path);
    const bool taken = to && to.mapped() && digest != st.media_expected.end()
                       && to.mapped()->take_over(uid, digest->second);
    if (!taken) {
        emit let_go(uid);  // whoever may, this one too
    }
    if (st.requested.empty()) {
        want_more(uid);
    } else {
        want_media(uid);
    }
}

void Peer::ask_again_for_what_it_left()
{
    for (const auto& uid : std::exchange(left_, {})) {
        want_media(uid);
    }
}

Peer::MediaFree Peer::media_free() const
{
    std::size_t files = 0;
    std::size_t large = 0;
    for (const auto& [uid, st] : state_) {
        for (const auto& name : st.requested) {
            if (!name.starts_with("media/")) {
                continue;
            }
            ++files;
            const auto expected = st.media_expected.find(name.substr(6));
            if (expected != st.media_expected.end() && expected->second.size >= k_large_media) {
                ++large;
            }
        }
    }
    return {k_media_at_once - std::min(files, k_media_at_once), k_large_at_once - std::min(large, k_large_at_once)};
}

bool Peer::media_room(std::int64_t size) const
{
    const auto free = media_free();
    return free.files > 0 && (size < k_large_media || free.large > 0);
}

void Peer::tell_coming(const std::string& library, const std::set<std::string>& paths)
{
    if (!ready_ || closed_ || halted() || !allowed_.contains(library)) {
        return;
    }
    std::set<std::string> told(paths.begin(),
                               std::next(paths.begin(), static_cast<std::ptrdiff_t>(std::min(paths.size(), k_max_coming))));
    auto& st = state_[library];
    if (told == st.told_coming) {
        return;
    }
    send({{"type", "coming"}, {"library", library}, {"paths", told}});
    st.told_coming = std::move(told);
}

void Peer::media_arrived(const std::string& uid, const std::string& path)
{
    if (ready_ && !closed_ && allowed_.contains(uid)) {
        send({{"type", "have_media"}, {"library", uid}, {"path", path}});
    }
    const auto it = state_.find(uid);
    if (it == state_.end()) {
        return;
    }
    for (auto& p : it->second.pending) {
        p.needs.erase(path);
    }
    settle_pending(uid);
}

void Peer::take_have_media(const SyncedLibrary& lib, const json& message)
{
    // Come there since it said it had not got it: it may be asked again.
    if (state_[lib.uid].missing_there.erase(item("media", media_path_of(message))) > 0) {
        want_more(lib.uid);
    }
}

void Peer::take_coming(const SyncedLibrary& lib, const json& message)
{
    const auto& paths = list_of(message, "paths");
    if (paths.size() > k_max_coming) {
        malformed("more files said to be coming than are told of");
    }
    std::set<std::string> coming;
    for (const auto& path : paths) {
        if (!path.is_string() || !db::is_sync_media_path(path.get_ref<const std::string&>())) {
            malformed("a media path that is not one");
        }
        coming.insert(path.get<std::string>());
    }
    state_[lib.uid].coming_there = std::move(coming);
}

void Peer::take_cancel(const SyncedLibrary& lib, const json& m)
{
    // The other end has enough of it from here: no more is sent.
    outbox_.cancel_media(lib.uid, media_path_of(m));
}

void Peer::drop_media(const std::string& uid, const std::string& path, const QString& why)
{
    auto& st = state_[uid];
    st.missing_there.insert(item("media", path));
    // What else of it is on its way is let go by.
    send({{"type", "cancel"}, {"library", uid}, {"path", path}});
    emit refused(why.arg(QString::fromStdString(path)));
    for (auto& p : st.pending) {
        p.needs.erase(path);  // kept without it, for another to supply
    }
    settle_pending(uid);
    got(uid, item("media", path), Got::NotHere);
}

void Peer::media_failed(const std::string& uid, const std::string& path, const QString& why)
{
    // This computer's own trouble: the other end is not blamed, and what
    // waits for the file goes on waiting. Asked for again once more is known
    // here, not at once: a full disk would take the same piece for ever.
    auto& st = state_[uid];
    st.not_yet.insert(item("media", path));
    send({{"type", "cancel"}, {"library", uid}, {"path", path}});
    emit refused(why.arg(QString::fromStdString(path)));
    got(uid, item("media", path), Got::NotHere);
}

void Peer::media_placed(const std::string& uid, const std::string& path)
{
    if (library(uid) == nullptr) {
        got(uid, item("media", path), Got::NotHere);  // taken offline here meanwhile
        return;
    }
    if (const auto owed = shared_.owed_media.find(uid); owed != shared_.owed_media.end()) {
        owed->second.erase(path);
    }
    emit media_done(uid, path);
    auto& st = state_[uid];
    for (auto& p : st.pending) {
        p.needs.erase(path);
    }
    settle_pending(uid);
    got(uid, item("media", path), Got::Stored);
    // Outside reading: what went into the store is told now.
    tell_stored();
}

}  // namespace seventhslide::link
