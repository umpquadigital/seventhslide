// SyncEngine: applying what the others sent.
//
// One slice of SyncEngine (declared in sync_engine.hpp): reading a change
// file, applying each computer's files in order once every file each was
// made on top of is applied (apply_incoming, apply_file), and trying held
// changes again (retry_held). How one change merges is
// sync_engine_merge.cpp's.
// Seam: db and the store (SyncStore).

#include "seventhslide/sync/sync_engine.hpp"

#include "sync_engine_internal.hpp"

#include "seventhslide/db/schema.hpp"
#include "seventhslide/sync/envelope.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace seventhslide::sync {

std::optional<ChangeFile> SyncEngine::read_change_file(const std::string& machine, std::int64_t n)
{
    ChangeFile file;
    try {
        file = change_file_from_json(unseal(store_.read(Numbered::Changes, machine, n), "A sync change file").body);
    } catch (const NewerFormat&) {
        throw;
    } catch (const std::exception&) {
        store_.set_aside(Numbered::Changes, machine, n);
        return std::nullopt;
    }
    if (file.library != db::library_uid(db_)) {
        throw std::runtime_error("A change file in this library's sync folder belongs to another library");
    }
    if (file.machine != machine || file.sequence != n) {
        throw std::runtime_error("A sync change file is not where it belongs (computer " + machine + ", file "
                                 + std::to_string(n) + ")");
    }
    return file;
}

bool SyncEngine::Origin::saw(const std::string& machine, std::int64_t number) const
{
    if (machine == version.machine) {
        return number < sequence;
    }
    const auto it = seen.find(machine);
    return it != seen.end() && it->second >= number;
}

void SyncEngine::apply_incoming(SyncResult& result)
{
    // What is marked is read afresh in each sync, and kept only through it.
    marked_.forget();
    {
        // What arrived since may let a held change through.
        unowned_.clear();
        db::Transaction tx(db_);
        find_deleted_here();
        retry_held();
        tx.commit();
        remove_unowned();
    }
    auto applied = book_.applied();
    // This computer's own files are here, but for those to replay after
    // joining again (after the last applied, up to the last sent). A file
    // sent and never recorded (after the last sent) is here too: what it
    // holds is still unsent here.
    const auto last_sent = book_.last_sent();
    std::map<std::string, std::vector<std::int64_t>> files;
    last_in_store_.clear();
    for (const auto& m : store_.machines(Numbered::Changes)) {
        auto sequences = store_.sequences(Numbered::Changes, m);
        if (!sequences.empty()) {
            last_in_store_[m] = sequences.back();
        }
        if (m == machine_) {
            std::erase_if(sequences, [&](std::int64_t n) { return n > last_sent; });
        }
        files.emplace(m, std::move(sequences));
    }

    // What the next file of each computer that waits was made on top of.
    std::map<std::string, std::map<std::string, std::int64_t>> waits_on;
    for (bool progress = true; progress;) {
        progress = false;
        for (auto& [m, sequences] : files) {
            for (;;) {
                const auto next = applied[m] + 1;
                if (std::ranges::find(sequences, next) == sequences.end()) {
                    break;
                }
                const auto read = read_change_file(m, next);
                if (!read) {
                    std::erase(sequences, next);  // damaged, set aside: fetched again
                    break;
                }
                const ChangeFile& file = *read;
                const bool ready = std::ranges::all_of(file.seen, [&](const auto& seen) {
                    return applied[seen.first] >= seen.second
                           || (seen.first == machine_ && applied[machine_] >= last_sent);
                });
                if (!ready) {
                    waits_on[m] = file.seen;
                    break;
                }
                waits_on.erase(m);
                unowned_.clear();
                db::Transaction tx(db_);
                for (auto digest : file.media) {
                    digest.version = file.version();  // the file's, whatever it says
                    book_.set_media_digest(digest);
                }
                try {
                    apply_file(file);
                    for (const auto& change : file.changes) {
                        result.changed.add(change.table, change.key);
                    }
                } catch (const std::invalid_argument& ex) {
                    throw std::runtime_error(std::string("A sync change file could not be "
                                                         "applied: ")
                                             + ex.what());
                }
                book_.set_applied(m, next);
                book_.set_counter(std::max(book_.counter(), file.counter));
                tx.commit();
                remove_unowned();
                applied[m] = next;
                ++result.files_applied;
                progress = true;
            }
        }
    }
    for (const auto& [m, sequences] : files) {
        result.files_waiting += static_cast<int>(
            std::ranges::count_if(sequences, [&](std::int64_t n) { return n > applied[m]; }));
    }
    // Waiting on a computer whose next file is not here.
    for (const auto& [m, seen] : waits_on) {
        for (const auto& [other, n] : seen) {
            if (other == machine_ || applied[other] >= n) {
                continue;
            }
            const auto theirs = files.find(other);
            if (theirs == files.end()
                || std::ranges::find(theirs->second, applied[other] + 1) == theirs->second.end()) {
                result.waiting_on.insert(other);
            }
        }
    }
    for (const auto& held : book_.held()) {
        result.held.push_back(record_name(held.table, held.key));
    }
    // Held changes let through (one held in an earlier sync, too), and the
    // copies conflicts kept.
    result.changed.add(std::exchange(changed_, {}));
    marked_.forget();
}

void SyncEngine::apply_file(const ChangeFile& file)
{
    arrived_.clear();
    find_deleted_here();
    Origin origin{file.version(), file.sequence, file.seen, {}, file.computer};
    const auto kept = deletes_kept(file);
    for (const auto& [table, key] : kept) {
        if (db::syncs_by_uid(table)) {
            origin.kept.insert(key);
        }
    }
    for (const auto& change : file.changes) {
        if (change.deleted && kept.contains({change.table, change.key})) {
            keep(change, origin);
            continue;
        }
        if (!apply_change(change, origin)) {
            json waiting = change_to_json(change);
            waiting["counter"] = file.counter;
            waiting["computer"] = file.computer;
            book_.hold({change.table, change.key, file.machine, file.sequence, std::move(waiting)});
        }
    }
    // A record pointing at one later in the same file (a new folder's new
    // parent) gets through now.
    if (!arrived_.empty()) {
        retry_held(std::exchange(arrived_, {}));
    }
}

void SyncEngine::retry_held(std::optional<std::set<std::string>> arrived)
{
    for (bool progress = true; progress;) {
        progress = false;
        auto held = book_.held();
        if (arrived) {
            std::erase_if(held, [&](const db::HeldChange& h) {
                const auto names = db::names_in(h.table, h.key, h.change.value("document", json()));
                return std::ranges::none_of(names, [&](const std::string& name) { return arrived->contains(name); });
            });
        }
        // Oldest first, as they were made.
        std::ranges::sort(held, [](const db::HeldChange& a, const db::HeldChange& b) {
            return db::SyncVersion{a.change.value("counter", std::int64_t{0}), a.machine}
                   < db::SyncVersion{b.change.value("counter", std::int64_t{0}), b.machine};
        });
        for (const auto& h : held) {
            const Origin origin{{h.change.value("counter", std::int64_t{0}), h.machine},
                                h.sequence,
                                {},
                                {},
                                h.change.value("computer", std::string())};
            if (apply_change(change_from_json(h.change), origin)) {
                changed_.add(h.table, h.key);
                book_.release(h);
                progress = true;
            }
        }
        if (arrived) {
            arrived->merge(arrived_);
        }
        arrived_.clear();
    }
}

}  // namespace seventhslide::sync
