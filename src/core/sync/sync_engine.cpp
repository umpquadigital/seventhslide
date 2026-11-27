// SyncEngine: making one, and a sync from end to end.
//
// One slice of SyncEngine (declared in sync_engine.hpp): its constructor,
// shared_form, and sync, which catches up, applies, sends, sees to the
// media and writes a snapshot, in that order. The rest is split by what it
// does: sync_engine_apply.cpp (applying the others' files in order),
// sync_engine_merge.cpp (merging one change, and the deletes that stay),
// sync_engine_marks.cpp (what is marked here), sync_engine_send.cpp
// (sending what changed), sync_engine_catch_up.cpp (joining and catching
// up from a snapshot), sync_engine_snapshot.cpp (writing one) and
// sync_engine_media.cpp (the media files records own).
// Seam: db and the store (SyncStore).

#include "seventhslide/sync/sync_engine.hpp"

#include "sync_engine_internal.hpp"

#include "seventhslide/crypto/sha256.hpp"

#include <utility>

namespace seventhslide::sync {

Q_LOGGING_CATEGORY(lc_engine, "seventhslide.sync", QtInfoMsg)

json shared_form(std::string_view table, const json& document)
{
    if (!document.is_object()) {
        return document;
    }
    json out = document;
    for (const auto& part : db::parts_of(table)) {
        if (const auto it = out.find(part.table); it != out.end()) {
            *it = json{{"fingerprint", crypto::sha256_hex(it->dump())}};
        }
    }
    return out;
}

SyncEngine::SyncEngine(db::Database& db, SyncStore& store, std::filesystem::path media, Identity me)
    : db_(db), book_(db), store_(store), media_(std::move(media)), key_(std::move(me.key)),
      machine_(std::move(me.machine)), computer_(std::move(me.computer))
{
}

void SyncEngine::share_everything()
{
    book_.mark_everything();
}

SyncResult SyncEngine::sync(bool may_catch_up)
{
    SyncResult result;
    std::vector<std::string> let_go;
    if (may_catch_up) {
        if (const auto snapshot = behind()) {
            // What is unsent goes first, to be merged like anyone's.
            send_changes(result);
            if (catch_up(*snapshot, result)) {
                let_go = std::exchange(unowned_, {});
            }
        }
    }
    apply_incoming(result);
    if (!let_go.empty()) {
        // Only now: a record the catching up took away may have come back.
        unowned_ = std::move(let_go);
        remove_unowned();
    }
    send_changes(result);
    const auto here = media_here();
    result.media = media_use(here);
    result.media_missing = missing_media(result.media, here);
    // What came in and went out is done by now: a snapshot that cannot be
    // written is said, and the sync goes on.
    try {
        if (const auto newest = newest_snapshot(); snapshot_due(newest)) {
            snapshot_problem_.clear();
            result.snapshot_written = write_snapshot(newest ? newest->files : 0, snapshot_problem_);
            snapshot_failed_at_.reset();
        }
        if (!result.snapshot_written) {
            result.snapshot_written = snapshot_refused();
        }
    } catch (const std::exception& ex) {
        snapshot_problem_ = ex.what();
        snapshot_failed_at_ = files_in_store();
    }
    if (snapshot_after_ <= 0 || holds_only_ || !writes_snapshots_) {
        snapshot_problem_.clear();
    }
    result.snapshot_problem = snapshot_problem_;
    return result;
}

}  // namespace seventhslide::sync
