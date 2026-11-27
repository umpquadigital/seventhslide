// SyncEngine: writing a snapshot.
//
// One slice of SyncEngine (declared in sync_engine.hpp): when one is due
// (snapshot_due, snapshot_refused), and writing one from the shared copies
// a record at a time, a part at a time (write_snapshot). Reading and
// taking one is sync_engine_catch_up.cpp's.
// Seam: db (the shared copies and the outbox) and the store's snapshots.

#include "seventhslide/sync/sync_engine.hpp"

#include "sync_engine_internal.hpp"

#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_outbox.hpp"
#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/now_utc.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace seventhslide::sync {
namespace {

/// ``table``'s record ``key``, holding ``doc``, for people: what it is called,
/// and which it is.
std::string called(const std::string& table, const std::string& key, const json& doc)
{
    const auto title = sent_record({table, key, false, {}, doc, {}}, json()).title;
    return title.empty() ? record_name(table, key) : "“" + title + "” (" + record_name(table, key) + ")";
}

}  // namespace

std::int64_t SyncEngine::files_in_store() const
{
    std::int64_t files = 0;
    for (const auto& [machine, last] : last_in_store_) {
        files += last;
    }
    return files;
}

bool SyncEngine::snapshot_due(const std::optional<SnapshotName>& newest)
{
    if (snapshot_after_ <= 0 || holds_only_ || !writes_snapshots_) {
        return false;
    }
    const auto files = files_in_store();
    if (snapshot_failed_at_ && files - *snapshot_failed_at_ < snapshot_after_) {
        return false;
    }
    if (!newest) {
        // None to start from while files were let go of: one joining could
        // not take the library whole.
        const auto machines = store_.machines(Numbered::Changes);
        if (std::ranges::any_of(machines, [&](const std::string& m) { return store_.dropped_through(m) > 0; })) {
            return true;
        }
    }
    return files - (newest ? newest->files : 0) >= snapshot_after_;
}

bool SyncEngine::snapshot_refused()
{
    if (refused_by_others_.empty() || holds_only_ || !writes_snapshots_
        || newest_snapshot_holds(refused_by_others_)) {
        return false;
    }
    // Not again straight after one could not be written (``sync``).
    if (snapshot_failed_at_ && files_in_store() - *snapshot_failed_at_ < std::max<std::int64_t>(snapshot_after_, 1)) {
        return false;
    }
    // Not while behind the newest itself: it would hold less of another's
    // files than the one it takes the place of.
    if (behind()) {
        return false;
    }
    const auto newest = newest_snapshot();
    std::string waits;
    const bool written = write_snapshot(newest ? newest->files : 0, waits);
    if (!written && !waits.empty()) {
        snapshot_problem_ = std::move(waits);
    }
    return written;
}

bool SyncEngine::newest_snapshot_holds(const std::map<std::string, std::int64_t>& files)
{
    const auto newest = newest_snapshot();
    const auto* label = newest ? newest_label(*newest) : nullptr;
    return label != nullptr && std::ranges::all_of(files, [&](const auto& file) {
               const auto held = label->files.find(file.first);
               return held != label->files.end() && held->second >= file.second;
           });
}

bool SyncEngine::snapshot()
{
    const auto newest = newest_snapshot();
    std::string waits;
    return write_snapshot(newest ? newest->files : 0, waits);
}

std::optional<json> SyncEngine::as_shared(const std::string& table, const std::string& key, const json& shared)
{
    auto here = db::read_record(db_, table, key);
    if (!here) {
        return std::nullopt;
    }
    const auto here_form = shared_form(table, *here);
    json out = shared;
    for (const auto& part : db::parts_of(table)) {
        const std::string name(part.table);
        if (here_form.contains(name) != shared.contains(name) || field_of(here_form, name) != field_of(shared, name)) {
            return std::nullopt;
        }
        if (here->contains(name)) {
            out[name] = std::move((*here)[name]);
        }
    }
    return out;
}

bool SyncEngine::write_snapshot(std::int64_t newest, std::string& waits)
{
    if (holds_only_ || !writes_snapshots_) {
        return false;
    }
    SnapshotName name{machine_, 0};
    SnapshotIndex index;
    {
        // Read as one moment, which no edit in the window waits for, a record
        // at a time; each part is written as it is made.
        db::ReadTransaction read(db_);
        auto applied = book_.applied();
        if (applied_from(applied, machine_) < book_.last_sent()) {
            return false;
        }
        std::erase_if(applied, [](const auto& entry) { return entry.second == 0; });
        index.files = std::move(applied);
        name.files = index.file_count();
        if (name.files <= newest) {
            return false;
        }
        // What is marked here goes in as it was shared: unsent, it is in no
        // change file the snapshot holds. A record with parts (a Bible) is
        // read here for them, so its parts must be the ones shared.
        std::set<RecordName> marked;
        for (const auto& mark : db::marked_records(db_)) {
            if (!db::has_parts(mark.table)) {
                continue;
            }
            const auto shared = book_.shared(mark.table, mark.key);
            if (!shared || !shared->document) {
                continue;  // not in it as a record
            }
            if (!as_shared(mark.table, mark.key, *shared->document)) {
                // Sent at the next sync, unless too large to be.
                const auto here = db::read_record(db_, mark.table, mark.key);
                if (here && here->dump().size() > db::k_largest_record) {
                    waits = "No snapshot is written while " + called(mark.table, mark.key, *here)
                            + ", changed here, is too large to sync";
                }
                return false;
            }
            marked.emplace(mark.table, mark.key);
        }
        PartWriter parts([this](const SnapshotPartRef& part, std::string_view bytes) {
            store_.write_checked_part(part.sha256, bytes);  // hashed as it was made
        }, part_bytes_);
        // Unmarked, a record here is just as its shared copy has it (the
        // outbox misses no write), but for the parts the shared copy keeps
        // only a fingerprint of (a Bible's verses).
        bool whole = true;
        book_.for_each_shared([&](db::SharedRecord shared) {
            if (!whole || (!shared.copy.document && shared.copy.versions.empty())) {
                return;  // only an id kept (``catch_up``): not a record yet
            }
            SnapshotRecord record{std::move(shared.table), std::move(shared.key), std::move(shared.copy.document),
                                  std::move(shared.copy.versions)};
            if (record.document && db::has_parts(record.table)) {
                record.document = marked.contains({record.table, record.key})
                                      ? as_shared(record.table, record.key, *record.document)
                                      : db::read_record(db_, record.table, record.key);
                if (!record.document) {
                    whole = false;
                    return;
                }
            }
            try {
                parts.add(record);
            } catch (const EntryTooLarge& ex) {
                // Merged from changes each small enough (a field from each
                // computer): only made smaller can it go in.
                constexpr std::int64_t mb = 1000 * 1000;
                throw std::runtime_error("No snapshot is written while "
                                         + called(record.table, record.key, record.document.value_or(json()))
                                         + " is too large to sync (" + std::to_string((ex.bytes + mb - 1) / mb)
                                         + " MB; the most is "
                                         + std::to_string(static_cast<std::int64_t>(db::k_largest_record) / mb)
                                         + " MB)");
            }
        });
        if (!whole) {
            return false;
        }
        book_.for_each_held([&](db::HeldChange held) { parts.add(held); });
        // Every declaration, not only the files records own now: a held
        // change's, or a deleted record's that an edit may bring back.
        book_.for_each_media([&](db::MediaDigest media) { parts.add(media); });
        auto files = std::move(index.files);
        index = parts.finish();
        index.files = std::move(files);
        index.counter = book_.counter();
        index.library = db::library_uid(db_);
        index.machine = machine_;
        index.computer = computer_;
        index.time = now_utc();
    }
    // Its parts are in: a snapshot in the store is always whole.
    store_.write_snapshot(name, seal(snapshot_index_to_json(index), {{k_signed_as_computer, &key_}}));
    return true;
}

}  // namespace seventhslide::sync
