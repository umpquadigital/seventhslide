#include "seventhslide/sync/housekeeping.hpp"

#include "seventhslide/db/schema.hpp"
#include "seventhslide/sync/change_file.hpp"
#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/snapshot.hpp"

#include <QDateTime>
#include <QString>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <optional>
#include <set>

namespace seventhslide::sync {
namespace {

QDateTime when(std::string_view iso)
{
    return QDateTime::fromString(QString::fromUtf8(iso), Qt::ISODate);
}

/// ``into`` with each of ``more``'s the higher.
void take_highest(std::map<std::string, std::int64_t>& into, const std::map<std::string, std::int64_t>& more)
{
    for (const auto& [machine, n] : more) {
        auto& had = into[machine];
        had = std::max(had, n);
    }
}

struct Labelled {
    SnapshotName name;
    SnapshotIndex label;
};

/// ``now`` (ISO 8601 UTC) on the clock parts are touched by.
std::optional<std::filesystem::file_time_type> file_time(std::string_view now)
{
    const auto at = when(now);
    if (!at.isValid()) {
        return std::nullopt;
    }
    // By how far it is from now on either clock: every platform has both.
    const auto from_now = std::chrono::milliseconds(at.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch());
    return std::filesystem::file_time_type::clock::now()
           + std::chrono::duration_cast<std::filesystem::file_time_type::duration>(from_now);
}

/// Of ``here``, the parts no snapshot that stays names, untouched since
/// ``touched_before``.
std::vector<std::string> old_parts(const std::vector<PartFile>& here, const std::vector<Labelled>& snapshots,
                                   const std::vector<SnapshotName>& going,
                                   std::filesystem::file_time_type touched_before)
{
    std::set<std::string> named;
    for (const auto& s : snapshots) {
        if (std::ranges::find(going, s.name) == going.end()) {
            for (const auto& part : s.label.parts) {
                named.insert(part.sha256);
            }
        }
    }
    std::vector<std::string> out;
    for (const auto& part : here) {
        if (!named.contains(part.sha256) && part.touched < touched_before) {
            out.push_back(part.sha256);
        }
    }
    return out;
}

/// Whether every part ``label`` names is here, of its size (``sizes``, by
/// name).
bool whole(const SnapshotIndex& label, const std::map<std::string, std::int64_t>& sizes)
{
    return std::ranges::all_of(label.parts, [&](const SnapshotPartRef& ref) {
        const auto it = sizes.find(ref.sha256);
        return it != sizes.end() && it->second == ref.size;
    });
}

/// The label of snapshot ``name``'s index in ``store``.
SnapshotIndex label_of(SyncStore& store, const SnapshotName& name)
{
    return snapshot_index_label(unseal(store.read_snapshot(name), "A sync snapshot").body);
}

}  // namespace

OldFiles old_files(SyncStore& store, const Members& members, std::string_view me,
                   const std::map<std::string, std::int64_t>& applied_here,
                   const std::map<std::string, Progress>& heard, std::string_view now)
{
    OldFiles out;
    std::vector<Labelled> snapshots;
    bool all_read = true;
    for (const auto& name : store.snapshots()) {
        try {
            snapshots.push_back({name, label_of(store, name)});
        } catch (const std::exception&) {
            // One that cannot be read is no one's to go by, and may name any
            // part here.
            all_read = false;
        }
    }
    const auto parts = store.parts();
    std::map<std::string, std::int64_t> sizes;
    for (const auto& part : parts) {
        sizes.emplace(part.sha256, part.size);
    }
    const auto most = [](const Labelled& a, const Labelled& b) { return a.name.files < b.name.files; };
    const auto newest = std::ranges::max_element(snapshots, most);
    // One to go by, with its parts here: a computer joining from it takes it
    // whole.
    std::vector<Labelled> readable;
    std::ranges::copy_if(snapshots, std::back_inserter(readable), [&](const Labelled& s) {
        return s.label.format <= k_snapshot_format && s.label.schema <= db::k_schema_version
               && whole(s.label, sizes);
    });
    const auto basis = std::ranges::max_element(readable, most);
    if (basis != readable.end()) {
        for (const auto& s : snapshots) {
            if (s.name != basis->name && s.name != newest->name && s.name.files <= basis->name.files) {
                out.snapshots.push_back(s.name);
            }
        }
    }
    // Parts go even with no snapshot to go by (a writer stopped part way),
    // but none while a snapshot here cannot be read.
    if (const auto at = file_time(now); at && all_read) {
        out.parts_touched_before = *at - std::chrono::days(k_part_grace_days);
        out.parts = old_parts(parts, snapshots, out.snapshots, out.parts_touched_before);
    }
    if (basis == readable.end()) {
        return out;
    }

    // As far as the snapshot holds and this computer has applied...
    auto through = basis->label.files;
    for (auto& [machine, n] : through) {
        const auto here = applied_here.find(machine);
        n = std::min(n, here != applied_here.end() ? here->second : 0);
    }
    // ...and every member heard from lately has applied.
    const auto now_at = when(now);
    for (const auto& [machine, member] : members.members) {
        if (member.removed || machine == me) {
            continue;
        }
        std::map<std::string, std::int64_t> applied;
        QDateTime latest;
        const auto later = [&](const std::string& at) {
            if (const auto t = when(at); t.isValid() && (!latest.isValid() || t > latest)) {
                latest = t;
            }
        };
        if (const auto it = heard.find(machine); it != heard.end()) {
            take_highest(applied, it->second.applied);
            later(it->second.at);
        }
        // What it wrote was made on top of what it had applied.
        if (const auto own = store.sequences(Numbered::Changes, machine); !own.empty()) {
            try {
                const auto file = change_file_label(
                    unseal(store.read(Numbered::Changes, machine, own.back()), "A sync change file").body);
                take_highest(applied, file.seen);
                take_highest(applied, {{machine, file.sequence}});
                later(file.time);
            } catch (const std::exception&) {
                // its others say
            }
        }
        for (const auto& s : snapshots) {
            if (s.name.machine == machine) {
                take_highest(applied, s.label.files);
                later(s.label.time);
            }
        }
        if (latest.isValid() && now_at.isValid() && latest.daysTo(now_at) > k_stale_days) {
            continue;  // away for long: it catches up from a snapshot when it is back
        }
        for (auto& [m, n] : through) {
            const auto had = applied.find(m);
            n = std::min(n, had != applied.end() ? had->second : 0);
        }
    }
    for (const auto& [machine, n] : through) {
        if (n > store.dropped_through(machine)) {
            out.changes_through.emplace(machine, n);
        }
    }
    return out;
}

void let_go(SyncStore& store, const OldFiles& old)
{
    for (const auto& [machine, n] : old.changes_through) {
        store.drop_changes_through(machine, n);
    }
    // The snapshots first: one in the store is always whole. The parts of one
    // that stays, stay.
    std::set<std::string> kept;
    for (const auto& name : old.snapshots) {
        if (store.remove_snapshot(name)) {
            continue;
        }
        try {
            for (const auto& part : label_of(store, name).parts) {
                kept.insert(part.sha256);
            }
        } catch (const std::exception&) {
            return;  // it may name any of them
        }
    }
    for (const auto& sha256 : old.parts) {
        if (!kept.contains(sha256)) {
            store.remove_part(sha256, old.parts_touched_before);
        }
    }
}

}  // namespace seventhslide::sync
