// SyncEngine: taking a snapshot: joining, and catching up.
//
// One slice of SyncEngine (declared in sync_engine.hpp): reading the
// store's snapshots (newest_snapshot, their labels and indexes), joining
// from the newest whole one (join, restore), and catching up from one this
// computer is behind (behind, catch_up). Writing one is
// sync_engine_snapshot.cpp's.
// Seam: db and the store's snapshots (snapshot.hpp).

#include "seventhslide/sync/sync_engine.hpp"

#include "sync_engine_internal.hpp"

#include "seventhslide/crypto/sha256.hpp"
#include "seventhslide/db/query.hpp"
#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_outbox.hpp"
#include "seventhslide/sync/envelope.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>

namespace seventhslide::sync {
namespace {

/// Thrown once a snapshot whose parts are not as its index names them is set
/// aside (``PartsInOrder``).
struct SetAside : std::runtime_error {
    SetAside() : std::runtime_error("A snapshot in this library's sync folder was damaged, and set aside") {}
};

/// Reads snapshot ``name``'s parts in order, each checked (``PartReader``),
/// one at a time. One that is not there, or not what ``index`` names, sets
/// the index aside, with a part damaged here, and throws ``SetAside``: the
/// next-newest is tried, and this one fetched again.
class PartsInOrder {
public:
    PartsInOrder(SyncStore& store, SnapshotName name, const SnapshotIndex& index)
        : store_(store), name_(std::move(name)), index_(index), reader_(index)
    {
    }

    /// The next part; nullopt after the last, once every one is counted.
    std::optional<SnapshotPart> next()
    {
        if (read_ == index_.parts.size()) {
            try {
                reader_.finish();
            } catch (const std::exception&) {
                set_aside();
            }
            return std::nullopt;
        }
        const auto& ref = index_.parts[read_++];
        std::string text;
        try {
            text = store_.read_part(ref.sha256);
        } catch (const std::exception&) {
            set_aside();
        }
        try {
            return reader_.next(text);
        } catch (const std::exception&) {
            // Damaged here, or not as the index said.
            if (crypto::sha256_hex(text) != ref.sha256) {
                store_.set_aside_part(ref.sha256);
            }
            set_aside();
        }
    }
    /// Where the part ``next`` gave last is among the index's.
    [[nodiscard]] std::size_t at() const { return read_ - 1; }

private:
    [[noreturn]] void set_aside()
    {
        store_.set_aside_snapshot(name_);
        throw SetAside();
    }

    SyncStore& store_;
    SnapshotName name_;
    const SnapshotIndex& index_;
    PartReader reader_;
    std::size_t read_ = 0;
};

}  // namespace

void SyncEngine::join(std::string_view library_uid)
{
    db::Transaction tx(db_);
    for (const auto table : db::record_tables_in_order()) {
        if (table != "library_meta" && db::has_records(db_, table)) {
            throw std::logic_error("Only a new, empty library can join a synced library");
        }
    }
    db::exec(db_, QStringLiteral("UPDATE library_meta SET value = ? WHERE key = ?"),
             {db::sql_text(library_uid), db::sql_text(db::k_library_uid_key)},
             "take the library's uid");

    SnapshotIndex snapshot;
    // The newest that is whole. One with a part damaged or gone is set aside
    // as it is written, what it wrote undone, and the next-newest is tried.
    bool taken = false;
    bool files_gone = false;  // a snapshot set aside held change files not here
    for (auto newest = newest_snapshot(); newest; newest = newest_snapshot()) {
        auto index = read_snapshot_index(*newest);
        if (!index) {
            continue;
        }
        if (index->library != library_uid) {
            throw std::runtime_error("A snapshot in this library's sync folder belongs to "
                                     "another library");
        }
        try {
            db::Transaction attempt(db_);
            restore(*newest, *index);
            attempt.commit();
        } catch (const SetAside&) {
            files_gone = files_gone || !holds_files(*index);
            continue;
        }
        snapshot = std::move(*index);
        taken = true;
        break;
    }
    if (!taken && files_gone) {
        throw std::runtime_error("This library's snapshots in its sync folder were damaged, and set aside, and "
                                 "the change files they hold are not all here: it can join once one comes again");
    }
    // This computer's own files, when it had the library before: those the
    // snapshot does not hold are replayed, and its next file comes after all.
    const auto own = snapshot.files[machine_];
    for (const auto& [machine, files] : snapshot.files) {
        book_.set_applied(machine, files);
    }
    const auto mine = store_.sequences(Numbered::Changes, machine_);
    book_.set_last_sent(std::max(own, mine.empty() ? std::int64_t{0} : mine.back()));
    book_.set_counter(snapshot.counter);
    book_.clear_marks();
    tx.commit();
}

void SyncEngine::restore(const SnapshotName& name, const SnapshotIndex& index)
{
    RecordWriter records(db_, book_, "This library's snapshot could not be applied");
    PartsInOrder parts(store_, name, index);
    while (auto part = parts.next()) {
        for (auto& r : part->records) {
            if (r.document) {
                db::SharedCopy copy{shared_form(r.table, *r.document), std::move(r.versions), std::nullopt};
                records.write(r.table, r.key, std::move(*r.document), std::nullopt, std::move(copy));
            } else {
                book_.set_shared(r.table, r.key, {std::nullopt, std::move(r.versions), std::nullopt});
            }
        }
        for (const auto& held : part->held) {
            book_.hold(held);
        }
        for (const auto& digest : part->media) {
            book_.set_media_digest(digest);
        }
    }
    records.finish();
}

std::optional<SnapshotName> SyncEngine::behind()
{
    const auto newest = newest_snapshot();
    if (!newest) {
        return std::nullopt;
    }
    const auto* read = newest_label(*newest);
    if (read == nullptr) {
        return std::nullopt;  // damaged, set aside: fetched again
    }
    const auto& label = *read;
    if (label.format > k_snapshot_format || label.schema > db::k_schema_version) {
        return std::nullopt;  // from a newer SeventhSlide: this one cannot take it
    }
    const auto applied = book_.applied();
    for (const auto& [machine, files] : label.files) {
        const auto next = applied_from(applied, machine) + 1;
        if (next > files) {
            continue;
        }
        const auto here = store_.sequences(Numbered::Changes, machine);
        if (std::ranges::find(here, next) == here.end()) {
            return newest;
        }
    }
    return std::nullopt;
}

const SnapshotIndex* SyncEngine::newest_label(const SnapshotName& name)
{
    if (!newest_label_ || newest_label_->machine != name.machine
        || newest_label_->file_count() != name.files) {
        newest_label_ = read_snapshot_label(name);
    }
    return newest_label_ ? &*newest_label_ : nullptr;
}

std::optional<SnapshotIndex> SyncEngine::read_snapshot_label(const SnapshotName& name)
{
    try {
        return snapshot_index_label(unseal(store_.read_snapshot(name), "A sync snapshot").body);
    } catch (const NewerFormat&) {
        throw;
    } catch (const std::exception&) {
        store_.set_aside_snapshot(name);
        return std::nullopt;
    }
}

std::optional<SnapshotIndex> SyncEngine::read_snapshot_index(const SnapshotName& name)
{
    try {
        return snapshot_index_from_json(unseal(store_.read_snapshot(name), "A sync snapshot").body);
    } catch (const NewerFormat&) {
        throw;
    } catch (const std::exception&) {
        store_.set_aside_snapshot(name);
        return std::nullopt;
    }
}

bool SyncEngine::holds_files(const SnapshotIndex& index)
{
    return std::ranges::all_of(index.files, [&](const auto& entry) {
        const auto& [machine, files] = entry;
        const auto here = store_.sequences(Numbered::Changes, machine);
        return std::ranges::count_if(here, [&](std::int64_t n) { return n >= 1 && n <= files; }) == files;
    });
}

bool SyncEngine::catch_up(const SnapshotName& name, SyncResult& result)
{
    const auto index = read_snapshot_index(name);
    if (!index) {
        return false;
    }
    if (index->library != db::library_uid(db_)) {
        throw std::runtime_error("A snapshot in this library's sync folder belongs to another library");
    }

    // What catching up changes here: worked out from a read of the library,
    // which no edit in the window waits for, in one pass over the snapshot's
    // parts beside the shared copies, both in the one order the parts hold
    // records in. Only what differs is kept, by where it is in the parts and
    // by name, and only the parts holding it are read again to write it, in
    // one transaction.
    enum class What : std::uint8_t {
        Nothing,
        /// The snapshot's document.
        Theirs,
        /// The snapshot's, with what is unsent here on top (``Plan::merged``).
        Merged,
    };
    struct Write {
        /// Where the record is in its part's records.
        std::size_t entry = 0;
        What what = What::Nothing;
        /// Whether its shared copy changes.
        bool shared = false;
        std::optional<std::int64_t> id;
    };
    struct PartPlan {
        std::vector<Write> writes;
        /// It holds held changes, or media declared otherwise here.
        bool rest = false;
    };
    struct Plan {
        std::vector<db::MarkedRecord> marks;
        /// What is still unsent here, as read.
        std::map<RecordName, std::optional<json>> pending;
        /// By where the part is in the index.
        std::map<std::size_t, PartPlan> parts;
        std::map<RecordName, json> merged;
        std::vector<std::pair<RecordName, std::vector<std::string>>> deletes;  // with their files
        std::vector<RecordName> revive;  // kept here, and sent whole
        /// Here and not in the snapshot: its shared copy keeps only its id.
        std::vector<std::pair<RecordName, std::optional<std::int64_t>>> blank;
    };
    // Throws ``SetAside`` for a snapshot whose parts are not as its index
    // names them.
    const auto make_plan = [&](Plan& plan) {
        // What is still unsent here (sent first, but for what the size limit
        // keeps here): it stays, on top of the snapshot, and is sent later.
        plan.marks = db::marked_records(db_);
        std::set<std::string> pending_names;
        for (const auto& name_of : marked_now()) {
            auto current = db::read_record(db_, name_of.first, name_of.second);
            if (current) {
                pending_names.merge(db::names_in(name_of.first, name_of.second, *current));
            }
            plan.pending.emplace(name_of, std::move(current));
        }
        // Something unsent here names it: using counts as editing, and it stays.
        const auto used_here = [&](const std::string& table, const std::string& key) {
            return named_by_others(table) && pending_names.contains(key);
        };
        const auto go = [&](const std::string& table, const std::string& key) {
            if (used_here(table, key)) {
                plan.revive.emplace_back(table, key);
            } else {
                // Read only for the files it owns: a Bible owns none.
                const auto current = db::owns_files(table) ? db::read_record(db_, table, key) : std::nullopt;
                plan.deletes.push_back({{table, key}, current ? files_of(table, key, *current) : std::vector<std::string>{}});
            }
        };

        // A record of the snapshot's, at ``entry`` in part ``at``, with its
        // shared copy here, if any.
        const auto theirs = [&](const SnapshotRecord& r, std::size_t at, std::size_t entry,
                                const db::SharedCopy* shared) {
            const RecordName record{r.table, r.key};
            const auto id = shared && shared->id_here ? shared->id_here : db::record_id(db_, r.table, r.key);
            Write write{entry, What::Nothing, false, id};
            std::optional<json> shared_doc;
            if (const auto mine = plan.pending.find(record); mine != plan.pending.end()) {
                const auto& current = mine->second;
                if (current && r.document) {
                    // What this computer changed, on top of theirs.
                    json doc = *r.document;
                    const json changed = shared_form(r.table, *current);
                    const json before = shared && shared->document ? *shared->document : json();
                    if (!db::has_parts(r.table)) {
                        for (const auto& [field, value] : current->items()) {
                            if (field_of(changed, field) != field_of(before, field)) {
                                doc[field] = value;
                            }
                        }
                    } else {
                        doc = *current;  // a Bible changed here: as it is here
                    }
                    plan.merged.insert_or_assign(record, std::move(doc));
                    write.what = What::Merged;
                } else if (!current && r.document && !(shared && shared->versions == r.versions)) {
                    // Deleted here, changed there since: the edit wins.
                    write.what = What::Theirs;
                }
                if (r.document) {
                    shared_doc = shared_form(r.table, *r.document);
                }
            } else if (r.document) {
                shared_doc = shared_form(r.table, *r.document);
                const bool same = shared && shared->versions == r.versions && shared->document == shared_doc
                                  && db::record_exists(db_, r.table, r.key);
                if (!same) {
                    write.what = What::Theirs;
                }
            } else if (db::record_exists(db_, r.table, r.key)) {
                go(r.table, r.key);
            }
            write.shared = !(shared && shared->id_here == id && shared->versions == r.versions
                             && shared->document == shared_doc);
            if (write.what != What::Nothing || write.shared) {
                plan.parts[at].writes.push_back(write);
            }
        };
        // A shared copy here of a record the snapshot does not hold: from
        // files after it, which bring it again (under the id it has, which a
        // blank shared copy keeps), or unsent here.
        const auto ours = [&](const db::SharedRecord& s) {
            const RecordName record{s.table, s.key};
            if (!plan.pending.contains(record) && db::record_exists(db_, s.table, s.key)) {
                go(s.table, s.key);
            }
            if (s.copy != db::SharedCopy{std::nullopt, {}, s.copy.id_here}) {
                plan.blank.emplace_back(record, s.copy.id_here);
            }
        };
        // What a part holds after its records.
        const auto rest = [&](const SnapshotPart& part, std::size_t at) {
            const bool declared_otherwise = std::ranges::any_of(part.media, [&](const db::MediaDigest& m) {
                const auto here = book_.media_digest(m.path);
                return !here || here->version > m.version;
            });
            if (!part.held.empty() || declared_otherwise) {
                plan.parts[at].rest = true;
            }
        };

        PartsInOrder parts(store_, name, *index);
        std::optional<SnapshotPart> part;
        std::size_t at = 0;
        std::size_t entry = 0;
        // The snapshot's next record, reading the next part when one is done.
        const auto next = [&]() -> const SnapshotRecord* {
            while (!part || entry == part->records.size()) {
                if (part) {
                    rest(*part, at);
                }
                part = parts.next();
                entry = 0;
                if (!part) {
                    return nullptr;
                }
                at = parts.at();
            }
            return &part->records[entry];
        };
        std::optional<std::pair<std::ptrdiff_t, std::string>> last;
        book_.for_each_shared([&](db::SharedRecord s) {
            auto place = std::pair(table_rank(s.table), s.key);
            if (last && !(*last < place)) {
                throw std::logic_error("Shared copies are not read in the order a snapshot holds records");
            }
            for (const auto* r = next(); r != nullptr; r = next()) {
                const auto order = std::pair(table_rank(r->table), std::string_view(r->key))
                                   <=> std::pair(place.first, std::string_view(place.second));
                if (order > 0) {
                    break;
                }
                theirs(*r, at, entry++, order == 0 ? &s.copy : nullptr);
                if (order == 0) {
                    last = std::move(place);
                    return;
                }
            }
            ours(s);
            last = std::move(place);
        });
        for (const auto* r = next(); r != nullptr; r = next()) {
            theirs(*r, at, entry++, nullptr);
        }
    };

    // Edited in the window meanwhile: every edit marks what it changed, or
    // changes what was marked already. Then worked out again, as it is now.
    const auto still = [&](const Plan& made) {
        if (db::marked_records(db_) != made.marks) {
            return false;
        }
        return std::ranges::all_of(made.pending, [&](const auto& entry) {
            return db::read_record(db_, entry.first.first, entry.first.second) == entry.second;
        });
    };
    // Worked out without the lock while the window lets it be; under the lock
    // only after that many edits came meanwhile.
    constexpr int k_plans_unlocked = 3;
    Plan plan;
    std::optional<db::Transaction> tx;
    try {
        for (int plans = 0; !tx; ++plans) {
            plan = Plan{};
            if (plans == k_plans_unlocked) {
                qCInfo(lc_engine) << "The window kept editing the library while catching up was worked out:"
                                  << "worked out once more, under the lock";
                tx.emplace(db_);
                make_plan(plan);
                break;
            }
            {
                db::ReadTransaction reading(db_);
                make_plan(plan);
            }
            tx.emplace(db_);
            if (!still(plan)) {
                tx.reset();
            }
        }
    } catch (const SetAside&) {
        return false;
    }
    unowned_.clear();

    for (const auto& [record, id] : plan.blank) {
        book_.set_shared(record.first, record.second, {std::nullopt, {}, id});
    }
    // Those that go, what belongs to them first; then those that come.
    std::ranges::sort(plan.deletes, std::greater{}, [](const auto& d) { return table_rank(d.first.first); });
    for (const auto& [record, files] : plan.deletes) {
        db::delete_record(db_, record.first, record.second);
        unowned_.insert(unowned_.end(), files.begin(), files.end());
    }
    book_.release_all();
    RecordWriter records(db_, book_, "This library could not catch up from its snapshot");
    for (auto& [at, todo] : plan.parts) {
        // Read again: one not what it was when planned throws, and nothing
        // of this is kept.
        const auto& ref = index->parts[at];
        auto part = read_snapshot_part(store_.read_part(ref.sha256), ref.sha256);
        for (const auto& w : todo.writes) {
            auto& r = part.records.at(w.entry);
            std::optional<db::SharedCopy> copy;
            if (w.shared) {
                copy = db::SharedCopy{r.document ? std::optional(shared_form(r.table, *r.document)) : std::nullopt,
                                      r.versions, w.id};
            }
            if (w.what == What::Nothing) {
                book_.set_shared(r.table, r.key, *copy);
                continue;
            }
            auto doc = w.what == What::Merged ? std::move(plan.merged.at({r.table, r.key})) : std::move(*r.document);
            records.write(r.table, r.key, std::move(doc), w.id, std::move(copy));
        }
        if (todo.rest) {
            for (const auto& held : part.held) {
                book_.hold(held);
            }
            for (const auto& digest : part.media) {
                book_.set_media_digest(digest);
            }
        }
    }
    records.finish();
    // From the snapshot on, as it holds them: the files after it, this
    // computer's own just sent among them, are applied next.
    for (const auto& [machine, n] : book_.applied()) {
        book_.set_applied(machine, 0);
    }
    for (const auto& [machine, n] : index->files) {
        book_.set_applied(machine, n);
    }
    book_.set_counter(std::max(book_.counter(), index->counter));
    // What is marked is what was unsent; and what is kept because something
    // unsent uses it, to come back everywhere.
    book_.clear_marks();
    for (const auto& mark : plan.marks) {
        book_.mark(mark);
    }
    for (const auto& [table, key] : plan.revive) {
        book_.mark({table, key});
    }
    tx->commit();
    for (const auto table : db::record_tables_in_order()) {
        result.changed.add_all(table);
    }
    result.caught_up = true;
    return true;
}

std::optional<SnapshotName> SyncEngine::newest_snapshot()
{
    const auto all = store_.snapshots();
    const auto it = std::ranges::max_element(all, {}, &SnapshotName::files);
    return it != all.end() ? std::optional(*it) : std::nullopt;
}

}  // namespace seventhslide::sync
