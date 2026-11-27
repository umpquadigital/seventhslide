// Peer: who may share what. The hello, a library offered or taken away, a
// library joined through the connection, and introductions to the others.

#include "seventhslide/link/peer.hpp"

#include "peer_internal.hpp"

#include "seventhslide/link/tls.hpp"
#include "seventhslide/sync/member_list.hpp"

#include <algorithm>
#include <utility>

namespace seventhslide::link {

void Peer::on_encrypted()
{
    // On an accepted connection the other end's hello may be in before this
    // queued step runs; on_read greets first then, and this does nothing.
    if (closed_ || said_hello_ || !socket_->isEncrypted()) {
        return;
    }
    key_ = peer_key(socket_->peerCertificate());
    if (!key_) {
        close(QStringLiteral("The other computer showed no sync key"));
        return;
    }
    if (expected_ && *expected_ != *key_) {
        close(QStringLiteral("The other computer is not the one expected"));
        return;
    }
    // A computer this one joins through may be the one that reached out.
    if (shared_.joining_with) {
        for (auto& j : shared_.joining_with(*key_)) {
            join(std::move(j));
        }
    }
    // Only the libraries this key may share (it is a member, or this
    // computer joins through it): a stranger learns nothing of the rest. One
    // the other end joins by invite is offered once its hello lets it in.
    json offered = json::array();
    for (const auto& lib : libraries_) {
        if (!allowed(lib, json::object())) {
            continue;
        }
        json entry{{"library", lib.uid}};
        for (const auto& j : joining_) {
            if (j.library == lib.uid) {
                entry["join"] = j.entry;
            }
        }
        offered.push_back(std::move(entry));
        offered_.insert(lib.uid);
    }
    json hello{{"type", "hello"}, {"version", k_protocol_version}, {"machine", me_.machine}};
    if (!offered.empty()) {
        hello["name"] = me_.computer;
    }
    if (replaces_) {
        hello["replaces"] = true;
    }
    hello["libraries"] = std::move(offered);
    send(hello);
    said_hello_ = true;
}

sync::Members Peer::members(const SyncedLibrary& lib) const
{
    return sync::read_members(*lib.store, lib.uid);
}

sync::Gate Peer::gate(const SyncedLibrary& lib)
{
    return sync::Gate(members(lib), lib.vouches.get());
}

bool Peer::allowed(const SyncedLibrary& lib, const json& offered)
{
    if (!key_) {
        return false;
    }
    if (std::ranges::any_of(joining_, [&](const Joining& j) { return j.library == lib.uid; })) {
        return true;  // the invite named this computer's key, and TLS checked it
    }
    const auto& list = members(lib);
    if (!list.founded()) {
        return false;  // only the computers this one joins through, above
    }
    if (const auto* member = list.with_key(*key_)) {
        return !member->removed;
    }
    const auto entry = offered.find("join");
    if (entry == offered.end() || !entry->is_string()) {
        return false;
    }
    const auto invite = sync::proved_invite(entry->get<std::string>(), lib.uid, *key_);
    // The invite's expiry, by this computer's clock: the one letting the
    // joining computer in is the one that checks it.
    if (!invite || invite->admin != list.admin || sync::invite_expired(invite->invite)
        || list.invites_used.contains(invite->invite.id) || list.invites_withdrawn.contains(invite->invite.id)) {
        return false;
    }
    invited_[lib.uid] = {invite->invite.id, invite->invite.role};
    return true;
}

bool Peer::admit(const std::string& uid, const json& offered)
{
    if (allowed_.contains(uid)) {
        return true;
    }
    if (const auto* lib = library(uid); lib != nullptr && allowed(*lib, offered)) {
        allowed_.insert(uid);
        return true;
    }
    return false;
}

bool Peer::may_hold_admin_key(const SyncedLibrary& lib)
{
    if (!key_) {
        return false;
    }
    const auto& list = members(lib);
    if (const auto* member = list.with_key(*key_)) {
        return !member->removed && (member->role != sync::Role::Viewer || member->holds_admin_key);
    }
    const auto invited = invited_.find(lib.uid);
    return invited != invited_.end() && invited->second.role != sync::Role::Viewer;
}

void Peer::library_added(const std::string& uid)
{
    const auto* lib = library(uid);
    if (!ready_ || closed_ || lib == nullptr || !allowed(*lib, json::object())) {
        return;
    }
    allowed_.insert(uid);
    offered_.insert(uid);
    send({{"type", "offer"}, {"library", uid}, {"name", me_.computer}});
    send(have(*lib));
}

void Peer::library_removed(const std::string& uid)
{
    // What it was told is on its way here is not, any more.
    if (const auto st = state_.find(uid);
        ready_ && !closed_ && st != state_.end() && !st->second.told_coming.empty()) {
        send({{"type", "coming"}, {"library", uid}, {"paths", json::array()}});
    }
    fetches_.release_all(this, &uid);
    allowed_.erase(uid);
    offered_.erase(uid);
    invited_.erase(uid);
    state_.erase(uid);
    outbox_.drop_library(uid);
    media_in_.forget_library(uid);
    std::erase_if(arrived_, [&](const Arrived& file) { return file.library == uid; });
    if (incoming_ && incoming_->library == uid) {
        incoming_->accept = false;
    }
    changed_.erase(uid);
}

void Peer::join(Joining joining)
{
    if (std::ranges::any_of(joining_, [&](const Joining& j) {
            return j.library == joining.library && j.entry == joining.entry;
        })) {
        return;
    }
    joining_.push_back(std::move(joining));
    // Before the hello it goes in the hello; once ready, in an offer.
    if (ready_) {
        offer_joining();
    }
}

void Peer::offer_joining()
{
    for (const auto& j : joining_) {
        const auto* lib = library(j.library);
        if (lib == nullptr || closed_ || !offered_.insert(j.library).second) {
            continue;
        }
        const json offer{{"type", "offer"}, {"library", j.library}, {"name", me_.computer}, {"join", j.entry}};
        allowed_.insert(j.library);
        send(offer);
        send(have(*lib));
    }
}

void Peer::take_hello(const json& message)
{
    const auto version = number_of(message, "version");
    if (version > k_protocol_version) {
        close(QStringLiteral("The other computer runs a newer SeventhSlide. Update this one to "
                             "sync with it."));
        return;
    }
    if (version < k_protocol_version) {
        close(QStringLiteral("The other computer runs an earlier SeventhSlide. Update it to "
                             "sync with this one."));
        return;
    }
    machine_ = text_of(message, "machine");
    if (!sync::is_machine_id(machine_)) {
        malformed("a machine id that is not one");
    }
    if (const auto name = message.find("name"); name != message.end() && name->is_string()) {
        name_ = name->get<std::string>();
    }
    if (const auto replaces = message.find("replaces"); replaces != message.end() && replaces->is_boolean()) {
        replaces_ = replaces_ || replaces->get<bool>();
    }
    for (const auto& offered : list_of(message, "libraries")) {
        if (!offered.is_object() || !offered.contains("library") || !offered["library"].is_string()) {
            continue;
        }
        admit(offered["library"].get<std::string>(), offered);
    }
    if (allowed_.empty() && joining_.empty()) {
        close(QStringLiteral("The other computer shares no library with this one"));
        return;
    }
    ready_ = true;
    emit became_ready();
    if (shared_.paused) {
        send({{"type", "pause"}, {"paused", true}});  // before what is here: nothing is to be asked yet
    }
    for (const auto& uid : allowed_) {
        if (offered_.insert(uid).second) {
            // It let itself in by an invite: it has not heard this end has it,
            // nor who this is.
            send({{"type", "offer"}, {"library", uid}, {"name", me_.computer}});
        }
        send(have(*library(uid)));
        if (shared_.share_address) {
            ask_introductions(uid);
        }
    }
    // A library joined here after the hello went: offered now.
    offer_joining();
}

void Peer::take_offer(const std::string& uid, const json& message)
{
    // The other end syncs ``uid`` now (again): share it if it may, and say
    // what is here.
    if (!admit(uid, message)) {
        return;
    }
    if (const auto name = message.find("name"); name != message.end() && name->is_string()) {
        name_ = name->get<std::string>();
    }
    if (offered_.insert(uid).second) {
        send({{"type", "offer"}, {"library", uid}, {"name", me_.computer}});
    }
    send(have(*library(uid)));
    if (shared_.share_address) {
        ask_introductions(uid);
    }
    // It may have forgotten: told again at the next look.
    state_[uid].told_coming.clear();
    state_[uid].told_met.clear();
}

void Peer::take_introduce(const SyncedLibrary& lib, const json& m)
{
    const auto port = number_of(m, "port");
    if (port > 65535) {
        malformed("a port that is not one");
    }
    auto& st = state_[lib.uid];
    st.asks_port = port == 0 ? std::nullopt : std::optional(static_cast<std::uint16_t>(port));
    emit introductions_asked(lib.uid);
}

void Peer::take_met(const SyncedLibrary& lib, const json& m)
{
    const auto fingerprint = text_of(m, "fingerprint");
    const auto address = text_of(m, "address");
    if (!sync::is_fingerprint(fingerprint) || !sync::parse_address(address)) {
        malformed("an introduction that is not one");
    }
    emit introduced(lib.uid, fingerprint, address);
}

void Peer::ask_introductions(const std::string& uid)
{
    if (!ready_ || closed_ || !allowed_.contains(uid)) {
        return;
    }
    const auto port = shared_.share_address ? shared_.listening_port : std::uint16_t{0};
    send({{"type", "introduce"}, {"library", uid}, {"port", port}});
}

std::optional<std::uint16_t> Peer::asks_introductions(const std::string& library) const
{
    const auto st = state_.find(library);
    if (closed_ || st == state_.end() || !allowed_.contains(library)) {
        return std::nullopt;
    }
    return st->second.asks_port;
}

void Peer::introduce(const std::string& library, const crypto::PublicKey& key, const std::string& address)
{
    if (!ready_ || closed_ || !allowed_.contains(library)) {
        return;
    }
    const auto fingerprint = key.fingerprint();
    if (!state_[library].told_met.insert(fingerprint + " " + address).second) {
        return;
    }
    send({{"type", "met"}, {"library", library}, {"fingerprint", fingerprint}, {"address", address}});
}

void Peer::forget_introduced(const crypto::PublicKey& key)
{
    const auto prefix = key.fingerprint() + " ";
    for (auto& [uid, st] : state_) {
        std::erase_if(st.told_met, [&](const std::string& told) { return told.starts_with(prefix); });
    }
}

void Peer::share_address_changed()
{
    for (const auto& uid : allowed_) {
        ask_introductions(uid);
    }
}

}  // namespace seventhslide::link
