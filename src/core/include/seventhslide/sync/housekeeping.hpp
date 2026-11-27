#pragma once

// Old files go: a computer's change files, once a snapshot holds them and
// every member has applied them; snapshots older than the newest; and
// snapshots' parts no snapshot that stays names, once no one has wanted them
// for ``k_part_grace_days`` (a part is touched as it is written, or wanted
// again, ``SyncStore::keep_part``). A part shared by two snapshots stays while
// either does; one a writer stopped part way left goes after the grace. A
// store would otherwise grow by a file for every save, for good.
//
// What each member has applied is known from what it said over a connection
// (``Progress``, kept by the caller), and from what it wrote: its newest change
// file (made on top of what it had applied) and its snapshots. A member not
// heard from for ``k_stale_days`` holds nothing up; if it comes back, it
// catches up from the newest snapshot (``SyncEngine::sync``). A member never
// heard from holds everything up until then, from when it was first noticed.
//
// The snapshot kept is the newest this SeventhSlide can read with all its
// parts here, and the newest of all when that is another: a computer that
// cannot read the newest still has one to catch up from. Files are let go of
// only up to what the one kept holds. While any snapshot here cannot be read
// (damaged, or a newer SeventhSlide's), no part goes: it may name them.
//
// Seam: the store, the member list, the file formats; Qt Core for dates.

#include "seventhslide/sync/member_list.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::sync {

/// How long a member not heard from holds up letting old files go.
inline constexpr int k_stale_days = 90;
/// How long a part no snapshot names stays: a snapshot being written or
/// fetched may be about to name it.
inline constexpr int k_part_grace_days = 7;

/// What a computer has applied of each computer's change files (its
/// ``sync_applied``, its own sent among them), as heard, and when: ISO 8601
/// UTC. One first noticed and not heard yet has none, and the time it was
/// noticed.
struct Progress {
    std::map<std::string, std::int64_t> applied;
    std::string at;
};

/// What a store may let go of.
struct OldFiles {
    /// Each computer's change files, up to these numbers.
    std::map<std::string, std::int64_t> changes_through;
    std::vector<SnapshotName> snapshots;
    /// Snapshots' parts, by name.
    std::vector<std::string> parts;
    /// Each part goes only if not touched since this (``k_part_grace_days``
    /// before now), as seen when it goes: one wanted meanwhile stays.
    std::filesystem::file_time_type parts_touched_before{};

    [[nodiscard]] bool empty() const { return changes_through.empty() && snapshots.empty() && parts.empty(); }
};

/// What ``store`` may let go of now (``now``, ISO 8601 UTC): ``me`` is this
/// computer, ``applied_here`` what it has applied (its own sent among them),
/// ``heard`` what each other member said over a connection, by machine id.
[[nodiscard]] OldFiles old_files(SyncStore& store, const Members& members, std::string_view me,
                                 const std::map<std::string, std::int64_t>& applied_here,
                                 const std::map<std::string, Progress>& heard, std::string_view now);

/// Let go of ``old``: the snapshots before their parts, and none of the parts
/// of a snapshot that would not go. Throws ``std::runtime_error`` when the
/// store cannot let go of change files.
void let_go(SyncStore& store, const OldFiles& old);

}  // namespace seventhslide::sync
