#include "seventhslide/sync/gate.hpp"

#include "seventhslide/crypto/sha256.hpp"
#include "seventhslide/sync/admin_key.hpp"
#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/shared_log.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace seventhslide::sync {
namespace {

[[noreturn]] void refuse(std::string_view what, const std::string& why)
{
    throw std::runtime_error(std::string(what) + " was refused: " + why + ".");
}

[[noreturn]] void not_yet(std::string_view what, const std::string& why)
{
    throw NotYet(std::string(what) + " was refused: " + why + ".");
}

/// ``check()``, with anything the JSON library throws about a value of the
/// wrong type reported as ``what`` being refused.
template <typename Check>
auto guarded(std::string_view what, Check check)
{
    try {
        return check();
    } catch (const nlohmann::json::exception& ex) {
        refuse(what, std::string("it could not be read (") + ex.what() + ")");
    }
}

/// The whole file when this SeventhSlide reads its format, so one that
/// would stop the engine is never let in; only its label when a newer
/// SeventhSlide wrote it, to keep and pass on until this one is updated
/// (the computers that wrote and took it read it whole).
ChangeFile readable_change_file(const nlohmann::json& body)
{
    auto label = change_file_label(body);
    if (label.format > k_change_format || label.schema > db::k_schema_version) {
        return label;
    }
    return change_file_from_json(body);
}

/// Likewise a snapshot's index.
SnapshotIndex readable_index(const nlohmann::json& body)
{
    auto label = snapshot_index_label(body);
    if (label.format > k_snapshot_format || label.schema > db::k_schema_version) {
        return label;
    }
    return snapshot_index_from_json(body);
}

}  // namespace

void Vouches::read_change(const std::string& machine, std::int64_t sequence, Found& found)
{
    if (!read_.emplace(machine, sequence).second) {
        return;
    }
    try {
        const auto file = change_file_label(
            unseal(store_.read(Numbered::Changes, machine, sequence), "A sync change file").body);
        for (const auto& [other, last] : file.seen) {
            if (other != machine) {
                auto& up = found[other];
                up = std::max(up, last);
            }
        }
    } catch (const std::exception&) {
        // Not one: it vouches for nothing.
    }
}

void Vouches::read_snapshot(const SnapshotName& name, Found& found)
{
    if (!read_snapshots_.emplace(name.machine, name.files).second) {
        return;
    }
    try {
        const auto snapshot =
            snapshot_index_label(unseal(store_.read_snapshot(name), "A sync snapshot").body);
        for (const auto& [other, count] : snapshot.files) {
            if (other != name.machine) {
                auto& up = found[other];
                up = std::max(up, count);
            }
        }
    } catch (const std::exception&) {
    }
}

void Vouches::read_new(Found& found)
{
    for (const auto& machine : store_.machines(Numbered::Changes)) {
        for (const auto n : store_.sequences(Numbered::Changes, machine)) {
            read_change(machine, n, found);
        }
    }
    for (const auto& name : store_.snapshots()) {
        read_snapshot(name, found);
    }
}

void Vouches::take(const Found& found)
{
    std::lock_guard lock(mu_);
    for (const auto& [machine, last] : found) {
        auto& up = up_to_[machine];
        up = std::max(up, last);
    }
}

void Vouches::refresh()
{
    std::lock_guard refreshing(refreshing_);
    // Gathered first, and taken in under the lock at the end, so a gate
    // asking meanwhile never waits on a read.
    Found found;
    try {
        read_new(found);
    } catch (const std::exception&) {
        // A store that cannot be listed now: what was read is kept, and the
        // rest is read on the next refresh.
    }
    take(found);
}

void Vouches::stored(std::string_view machine, std::int64_t sequence)
{
    std::lock_guard refreshing(refreshing_);
    Found found;
    read_change(std::string(machine), sequence, found);
    take(found);
}

void Vouches::stored(const SnapshotName& name)
{
    std::lock_guard refreshing(refreshing_);
    Found found;
    read_snapshot(name, found);
    take(found);
}

std::int64_t Vouches::up_to(std::string_view machine) const
{
    std::lock_guard lock(mu_);
    const auto it = up_to_.find(std::string(machine));
    return it == up_to_.end() ? 0 : it->second;
}

bool Gate::may_count(std::string_view machine, std::int64_t sequence) const
{
    if (members_.may_have_written(machine, sequence)) {
        return true;
    }
    return members_.find(machine) != nullptr && vouches_ != nullptr
           && vouches_->up_to(machine) >= sequence;
}

void Gate::check_writer(const Envelope& envelope, std::string_view machine,
                        std::string_view what) const
{
    const auto* member = members_.find(machine);
    if (member == nullptr) {
        not_yet(what, "it is from a computer this one does not know as a member");
    }
    const auto* key = envelope.signed_as(k_signed_as_computer);
    if (key == nullptr || *key != member->key) {
        refuse(what, "it is signed with a key that is not that computer's");
    }
}

namespace {
constexpr std::string_view k_change_file = "A sync change file";
}  // namespace

ChangeFile Gate::change_file(std::string_view machine, std::int64_t sequence,
                             std::string_view text) const
{
    auto file = read_change_file(machine, sequence, text);
    admit_change_file(machine, sequence);
    return file;
}

ChangeFile Gate::read_change_file(std::string_view machine, std::int64_t sequence,
                                  std::string_view text) const
{
    constexpr std::string_view what = k_change_file;
    return guarded(what, [&] {
        const auto envelope = unseal(text, what);
        check_writer(envelope, machine, what);
        auto file = readable_change_file(envelope.body);
        if (file.library != members_.library) {
            refuse(what, "it belongs to another library");
        }
        if (file.machine != machine || file.sequence != sequence) {
            refuse(what, "it is not the file its name says");
        }
        return file;
    });
}

void Gate::admit_change_file(std::string_view machine, std::int64_t sequence) const
{
    if (!may_count(machine, sequence)) {
        not_yet(k_change_file, "it is from a computer that may not change this library");
    }
}

SnapshotIndex Gate::snapshot(const SnapshotName& name, std::string_view text) const
{
    constexpr std::string_view what = "A sync snapshot";
    return guarded(what, [&] {
        const auto envelope = unseal(text, what);
        check_writer(envelope, name.machine, what);
        if (!members_.may_write(name.machine)) {
            refuse(what, "it is from a computer that may not write snapshots of this library");
        }
        auto snapshot = readable_index(envelope.body);
        if (snapshot.library != members_.library) {
            refuse(what, "it belongs to another library");
        }
        if (snapshot.machine != name.machine || snapshot.file_count() != name.files) {
            refuse(what, "it is not the snapshot its name says");
        }
        return snapshot;
    });
}

void Gate::snapshot_part(std::string_view sha256, std::string_view text)
{
    constexpr std::string_view what = "A snapshot's part";
    if (crypto::sha256_hex(text) != sha256) {
        refuse(what, "it is not the part its snapshot names");
    }
    try {
        (void)read_checked_snapshot_part(text);
    } catch (const NewerFormat&) {
        // A newer SeventhSlide's: its hash says it is the part named.
    } catch (const std::exception& ex) {
        refuse(what, std::string("it could not be read (") + ex.what() + ")");
    }
}

void Gate::log_entry(std::string_view machine, std::int64_t sequence, std::string_view text) const
{
    constexpr std::string_view what = "A shared log entry";
    ReadLogEntry read;
    try {
        read = read_log_entry(text);
    } catch (const NewerFormat&) {
        throw;
    } catch (const std::exception& ex) {
        refuse(what, std::string("it could not be read (") + ex.what() + ")");
    }
    const auto* member = members_.find(machine);
    if (member == nullptr) {
        not_yet(what, "it is from a computer this one does not know as a member");
    }
    if (read.signer != member->key) {
        refuse(what, "it is signed with a key that is not that computer's");
    }
    if (read.entry.machine != machine || read.entry.sequence != sequence) {
        refuse(what, "it is not the entry its name says");
    }
    if (read.library != members_.library) {
        refuse(what, "it belongs to another library");
    }
    if (!members_.may_write(machine)) {
        refuse(what, "it is from a computer that may not write to this library");
    }
}

void Gate::member_entry(std::string_view machine, std::int64_t sequence,
                        std::string_view text) const
{
    constexpr std::string_view what = "A sync member list entry";
    guarded(what, [&] {
        const auto envelope = unseal(text, what);
        const auto& b = envelope.body;
        const auto text_at = [&](const char* name) {
            const auto it = b.find(name);
            return it != b.end() && it->is_string() ? it->get<std::string>() : std::string();
        };
        const auto seq = b.find("sequence");
        if (text_at("machine") != machine || seq == b.end() || !seq->is_number_integer()
            || seq->get<std::int64_t>() != sequence) {
            refuse(what, "it is not the entry its name says");
        }
        if (text_at("library") != members_.library) {
            refuse(what, "it belongs to another library");
        }
        const auto kind = text_at("kind");
        if (kind == "found" && members_.founded()) {
            refuse(what, "this library was founded already");
        }
        const auto* signer = envelope.signed_as(k_signed_as_computer);
        if (signer == nullptr) {
            refuse(what, "it is not signed by a computer");
        }
        if (const auto* member = members_.find(machine)) {
            if (*signer != member->key) {
                refuse(what, "it is signed with a key that is not that computer's");
            }
            return;
        }
        // A computer not on the list yet: one joining or founding gives its
        // key in the entry.
        if (kind == "join" || kind == "found") {
            const auto computer = b.find("computer");
            const auto key = computer != b.end() && computer->is_object()
                                 && computer->contains("key") && (*computer)["key"].is_string()
                                 ? crypto::PublicKey::from_base64(
                                       (*computer)["key"].get<std::string>())
                                 : std::nullopt;
            if (!key || *key != *signer) {
                refuse(what, "it is not signed by the computer it adds");
            }
            return;
        }
        not_yet(what, "it is from a computer this one does not know as a member");
    });
}

void Gate::admin_key(std::string_view fingerprint, std::string_view text) const
{
    constexpr std::string_view what = "A sync locked admin key";
    const auto locked = LockedAdminKey::from_json(
        nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false));
    if (!locked) {
        refuse(what, "it is not one");
    }
    if (locked->admin.fingerprint() != fingerprint) {
        refuse(what, "it is not the key its name says");
    }
    if (locked->admin != members_.admin) {
        refuse(what, "it is not this library's admin key");
    }
}

}  // namespace seventhslide::sync
