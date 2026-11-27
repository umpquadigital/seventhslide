// SyncEngine: merging one change into the library.
//
// One slice of SyncEngine (declared in sync_engine.hpp): the merge rules
// for one change (apply_change), field by field, word lists combined, and
// the version that loses kept as a copy beside it (add_copy); and the
// deletes that do not go through: which of a file's deletes stay
// (deletes_kept, keep), and a record deleted here brought back when an
// arriving change uses it (find_deleted_here, bring_back_used).
// Seam: db (the records, their shared copies and the outbox).

#include "seventhslide/sync/sync_engine.hpp"

#include "sync_engine_internal.hpp"

#include "seventhslide/crypto/sha256.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <utility>

namespace seventhslide::sync {
namespace {

/// The files of every record that may be a row of ``table``'s by a column
/// (a service's items, a folder's Freeform items) and names its files by URL.
/// Deleting a record of ``table`` takes those rows with it (ON DELETE
/// CASCADE), and their own deletes then find nothing to read; what they named
/// is checked here instead, against what still names it (remove_unowned).
std::vector<std::string> files_of_rows_under(db::Database& db, std::string_view table)
{
    std::vector<std::string> out;
    for (const auto& f : db::k_sync_files) {
        if (f.in != db::FileIn::Url) {
            continue;
        }
        const bool under = std::ranges::any_of(db::k_sync_references, [&](const db::SyncReference& r) {
            return r.in == db::RefIn::Column && r.table == f.table && r.target == table;
        });
        if (!under) {
            continue;
        }
        for (const auto& key : db::record_keys(db, f.table)) {
            if (const auto record = db::read_record(db, f.table, key)) {
                std::ranges::copy(files_of(f.table, key, *record), std::back_inserter(out));
            }
        }
    }
    return out;
}

/// The words of a word list's text; nullopt when it is not one.
std::optional<std::vector<std::string>> words_of(const json& text)
{
    if (!text.is_string()) {
        return std::nullopt;
    }
    const auto list = json::parse(text.get<std::string>(), nullptr, false);
    if (!list.is_array()) {
        return std::nullopt;
    }
    std::vector<std::string> out;
    for (const auto& word : list) {
        if (!word.is_string()) {
            return std::nullopt;
        }
        out.push_back(word.get<std::string>());
    }
    return out;
}

/// ``list`` with the words ``after`` gained over ``before`` added and those
/// it lost taken out, in alphabetical order: two computers combining the same
/// changes in either order get the same list.
json combine_words(const std::vector<std::string>& list, const std::vector<std::string>& before,
                   const std::vector<std::string>& after)
{
    const auto has = [](const std::vector<std::string>& words, const std::string& w) {
        return std::ranges::find(words, w) != words.end();
    };
    std::vector<std::string> out;
    for (const auto& w : list) {
        if ((has(after, w) || !has(before, w)) && !has(out, w)) {
            out.push_back(w);
        }
    }
    for (const auto& w : after) {
        if (!has(before, w) && !has(out, w)) {
            out.push_back(w);
        }
    }
    std::ranges::sort(out);
    return json(json(out).dump());
}

/// A record going (deleted), with what it names (``db::names_in``).
struct Going {
    std::string table;
    std::string key;
    std::set<std::string> names;
};

/// ``staying[i]`` for each of ``going`` tied to one staying, and so on: what
/// one staying belongs to, and what belongs to it or points at it, stays with
/// it (a service with every item, a folder with its folders and files). Two
/// are tied when one names the other.
void keep_tied(const std::vector<Going>& going, std::vector<bool>& staying)
{
    std::map<std::string_view, std::vector<std::size_t>> by_key;
    for (std::size_t i = 0; i < going.size(); ++i) {
        if (named_by_others(going[i].table)) {
            by_key[going[i].key].push_back(i);
        }
    }
    std::vector<std::vector<std::size_t>> tied(going.size());
    for (std::size_t g = 0; g < going.size(); ++g) {
        for (const auto& name : going[g].names) {
            const auto it = by_key.find(name);
            if (it == by_key.end()) {
                continue;
            }
            for (const auto k : it->second) {
                if (k != g) {
                    tied[g].push_back(k);
                    tied[k].push_back(g);
                }
            }
        }
    }
    std::vector<std::size_t> reached;
    for (std::size_t i = 0; i < going.size(); ++i) {
        if (staying[i]) {
            reached.push_back(i);
        }
    }
    while (!reached.empty()) {
        const auto at = reached.back();
        reached.pop_back();
        for (const auto k : tied[at]) {
            if (!staying[k]) {
                staying[k] = true;
                reached.push_back(k);
            }
        }
    }
}

/// Whether the computer that deleted a record had seen every field of it as
/// it is here.
bool saw_every_field(const db::SharedCopy& shared, const Change& change)
{
    return shared.versions.size() == change.base.size()
           && std::ranges::all_of(shared.versions, [&](const auto& field) {
                  const auto it = change.base.find(field.first);
                  return it != change.base.end() && it->second == field.second;
              });
}

/// Add to ``shared``, the shared document of ``table``'s record ``key`` just
/// written here, each field the record has that it lacks: one the arriving
/// document did not carry, which the record took this library's default
/// for. It is agreed, not a change made here, and no one changed it, so it
/// has no version.
void add_defaults(db::Database& db, std::string_view table, std::string_view key, json& shared)
{
    const auto here = db::read_record(db, table, key);
    if (!here) {
        return;
    }
    const json here_form = shared_form(table, *here);
    for (const auto& [field, value] : here_form.items()) {
        if (!shared.contains(field)) {
            shared[field] = value;
        }
    }
}

}  // namespace

std::set<SyncEngine::RecordName> SyncEngine::deletes_kept(const ChangeFile& file)
{
    if (std::ranges::none_of(file.changes, &Change::deleted)) {
        return {};
    }
    // Changed here and not sent: marked (``marked_``, as ``find_deleted_here``
    // read it for this file; no output points at a record), and not as shared.
    std::map<RecordName, bool> changed;
    const auto changed_here = [&](const std::string& table, const std::string& key) {
        if (!marked_.marked({table, key})) {
            return false;
        }
        const auto [it, added] = changed.try_emplace({table, key}, false);
        if (added) {
            const auto current = db::read_record(db_, table, key);
            const auto shared = book_.shared(table, key);
            const json shared_doc = shared && shared->document ? *shared->document : json();
            it->second = (current ? shared_form(table, *current) : json()) != shared_doc;
        }
        return it->second;
    };
    // What here points at the record ``key`` or belongs to it: by its shared
    // copy (looked for once for all the file's deletes), or by what it holds
    // now (changed here and not sent).
    std::set<std::string> going_names;
    for (const auto& change : file.changes) {
        if (change.deleted && named_by_others(change.table)) {
            going_names.insert(change.key);
        }
    }
    auto named_by_shared = book_.mentioning(going_names);
    const auto referrers_here = [&](const std::string& key) {
        auto out = named_by_shared[key];
        std::ranges::copy(marked_.naming(key), std::back_inserter(out));
        std::ranges::sort(out);
        const auto [first, last] = std::ranges::unique(out);
        out.erase(first, last);
        return out;
    };

    std::vector<Going> gone;
    std::vector<bool> keeps;
    for (const auto& change : file.changes) {
        if (!change.deleted) {
            continue;
        }
        const auto shared = book_.shared(change.table, change.key);
        const bool live = shared && shared->document;
        bool stays = (live && !saw_every_field(*shared, change))
                     || changed_here(change.table, change.key);
        if (!stays && named_by_others(change.table)) {
            for (const auto& [table, key] : referrers_here(change.key)) {
                const auto theirs = std::ranges::find_if(change.referrers, [&](const Referrer& r) {
                    return r.table == table && r.key == key;
                });
                const auto here = book_.shared(table, key);
                if (theirs == change.referrers.end() || !here || theirs->versions != here->versions
                    || changed_here(table, key)) {
                    stays = true;
                    break;
                }
            }
        }
        gone.push_back({change.table, change.key,
                        db::names_in(change.table, change.key, live ? *shared->document : json())});
        keeps.push_back(stays);
    }
    keep_tied(gone, keeps);
    std::set<RecordName> kept;
    for (std::size_t i = 0; i < gone.size(); ++i) {
        if (keeps[i]) {
            kept.emplace(gone[i].table, gone[i].key);
        }
    }
    return kept;
}

void SyncEngine::find_deleted_here()
{
    marked_.refresh();
    deleted_here_.clear();
    for (const auto& record : marked_.records()) {
        const auto& [table, key] = record;
        // Not here, and shared as a record. One whose parts the shared copy
        // keeps only a fingerprint of (a Bible) cannot be rebuilt from it.
        if (!named_by_others(table) || db::has_parts(table) || marked_.here(record)
            || !marked_.shared_live(record)) {
            continue;
        }
        if (auto shared = book_.shared(table, key); shared && shared->document) {
            deleted_here_.push_back({table, key, std::move(*shared->document), shared->id_here});
        }
    }
    // Parents first, as ``marked_now`` has them.
    std::ranges::stable_sort(deleted_here_, {}, [](const DeletedHere& d) { return table_rank(d.table); });
}

void SyncEngine::bring_back_used(const Change& change)
{
    if (deleted_here_.empty()) {
        return;
    }
    const auto names = db::names_in(change.table, change.key, change.document);
    std::vector<Going> going;
    std::vector<bool> back;
    for (const auto& d : deleted_here_) {
        going.push_back({d.table, d.key, db::names_in(d.table, d.key, d.document)});
        back.push_back(names.contains(d.key));
    }
    if (std::ranges::none_of(back, std::identity{})) {
        return;
    }
    keep_tied(going, back);
    // Written as the others last had it, under the id it had, parents first:
    // nothing here differs from the shared copy, so nothing about it is sent.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < back.size(); ++i) {
        if (back[i]) {
            order.push_back(i);
        }
    }
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return table_rank(deleted_here_[i].table); });
    RecordWriter records(db_, book_, "A record deleted here could not be brought back");
    for (const auto i : order) {
        auto& d = deleted_here_[i];
        marked_.came_back(d.table, d.key);
        arrived_.insert(d.key);
        records.write(d.table, d.key, std::move(d.document), d.id_here, std::nullopt);
    }
    (void)records.write_waiting();  // one that cannot be whole stays deleted
    std::vector<DeletedHere> still;
    for (std::size_t i = 0; i < back.size(); ++i) {
        if (!back[i]) {
            still.push_back(std::move(deleted_here_[i]));
        }
    }
    deleted_here_ = std::move(still);
}

void SyncEngine::keep(const Change& change, const Origin& origin)
{
    const auto shared = book_.shared(change.table, change.key);
    const auto id_here = shared && shared->id_here ? shared->id_here
                                                   : db::record_id(db_, change.table, change.key);
    book_.set_shared(change.table, change.key,
                     db::SharedCopy{std::nullopt, {{"", origin.version}}, id_here});
    if (db::read_record(db_, change.table, change.key)) {
        book_.mark({change.table, change.key});
        marked_.changed(change.table, change.key);
    }
}

void SyncEngine::add_copy(const std::string& table, const std::string& key, Copy copy)
{
    const auto uid = crypto::sha256_hex("copy " + table + " " + key + " "
                                + std::to_string(copy.of.counter) + " " + copy.of.machine)
                         .substr(0, 32);
    if (db::read_record(db_, table, uid)) {
        return;
    }
    const std::string field(db::copy_name_field(table));
    const auto name = copy.document.value(field, std::string());
    copy.document[field] = name + " (from "
                           + (copy.computer.empty() ? "another computer" : copy.computer) + ")";
    db::write_record(db_, table, uid, copy.document);
    marked_.changed(table, uid);
    arrived_.insert(uid);
    changed_.add(table, uid);
}

bool SyncEngine::apply_change(const Change& change, const Origin& origin)
{
    if (!change.deleted) {
        // Using what was deleted here, and not yet sent, brings it back.
        bring_back_used(change);
    }
    const auto& version = origin.version;
    const auto& table = change.table;
    const auto& key = change.key;
    const auto shared = book_.shared(table, key);
    const auto current = db::read_record(db_, table, key);
    if (change.deleted) {
        marked_.went(table, key);
    } else if (!current && shared && shared->id_here) {
        marked_.came_back(table, key);  // under the id it had (below)
    } else {
        marked_.changed(table, key);
    }
    const json current_form = current ? shared_form(table, *current) : json();
    const bool live = shared && shared->document;
    const json shared_doc = live ? *shared->document : json();
    // Changed here and not sent yet.
    const bool pending = book_.is_marked(table, key) && current_form != shared_doc;

    if (change.deleted) {
        // A held change the deleting computer had seen is gone with the record.
        for (const auto& held : book_.held(table, key)) {
            if (origin.saw(held.machine, held.sequence)) {
                book_.release(held);
            }
        }
        if (live) {
            // A field the deleting computer had not seen changed is an edit
            // made meanwhile: it wins, and brings the record back there too.
            const bool saw_all = saw_every_field(*shared, change);
            if (!saw_all) {
                return true;
            }
        } else if (shared && shared->versions.contains("")
                   && shared->versions.at("") > version) {
            return true;  // deleted already, by a later delete
        }
        // A record changed here and not yet sent stays, and is sent whole.
        if (current && !pending) {
            std::ranges::copy(files_of_rows_under(db_, table), std::back_inserter(unowned_));
            db::delete_record(db_, table, key);
            std::ranges::copy(files_of(table, key, *current), std::back_inserter(unowned_));
        }
        book_.set_shared(table, key, db::SharedCopy{std::nullopt, {{"", version}},
                                                    shared ? shared->id_here : std::nullopt});
        return true;
    }

    db::SharedCopy next;
    next.id_here = shared ? shared->id_here : std::nullopt;
    bool resend = false;  // the record keeps what the change took away
    const bool keeps_both = !db::copy_name_field(table).empty();
    std::optional<Copy> copy;  // the version replaced
    json record;  // what the record holds after the change
    if (live) {
        next = *shared;
        if (current) {
            record = *current;
        } else {
            // Deleted here and not yet sent: the edit wins, and it comes back.
            record = shared_doc;
            for (const auto& part : db::parts_of(table)) {
                const std::string name(part.table);
                record.erase(name);
                if (const auto it = change.document.find(name); it != change.document.end()) {
                    record[name] = *it;
                }
            }
        }
        const json incoming_form = shared_form(table, change.document);
        for (const auto& [field, before] : change.base) {
            const auto set_by = shared->versions.find(field);
            const auto was = change.before.find(field);
            const auto before_words =
                was != change.before.end() ? words_of(was->second) : std::nullopt;
            const auto after_words = words_of(field_of(change.document, field));
            const auto shared_words = words_of(field_of(shared_doc, field));
            if (db::is_word_list(table, field) && before_words && after_words && shared_words) {
                // The words this change added and removed, on top of what is
                // here: taken whole when it was made on top of it, combined
                // with a change made meanwhile (here, or arrived) otherwise.
                const bool on_top =
                    set_by != shared->versions.end() && before == std::optional(set_by->second);
                const json merged =
                    on_top ? field_of(change.document, field)
                           : combine_words(*shared_words, *before_words, *after_words);
                (*next.document)[field] = merged;
                if (set_by == shared->versions.end() || version > set_by->second) {
                    next.versions[field] = version;
                }
                const bool mine_too =
                    pending && current && field_of(current_form, field) != field_of(shared_doc, field);
                const auto current_words =
                    current ? words_of(field_of(*current, field)) : std::nullopt;
                if (mine_too && current_words) {
                    record[field] = combine_words(*current_words, *before_words, *after_words);
                } else {
                    record[field] = merged;
                }
                continue;
            }
            if (set_by != shared->versions.end() && set_by->second > version) {
                continue;  // a later change to it is here already
            }
            (*next.document)[field] = field_of(incoming_form, field);
            next.versions[field] = version;
            const bool mine =
                pending && current && field_of(current_form, field) != field_of(shared_doc, field);
            if (keeps_both && !copy) {
                const auto& incoming = field_of(incoming_form, field);
                if (mine && field_of(current_form, field) != incoming) {
                    // This computer's change wins when it is sent: the
                    // arriving version becomes the copy.
                    copy = Copy{change.document, version, origin.computer};
                } else if (!mine && set_by != shared->versions.end()
                           && set_by->second.machine == machine_
                           && before != std::optional(set_by->second)
                           && field_of(shared_doc, field) != incoming && current) {
                    // This computer's change, sent, loses to one made without
                    // seeing it: this computer's version becomes the copy.
                    copy = Copy{*current, set_by->second, computer_};
                }
            }
            // Set to null as the record it named was deleted, which stays
            // here: it keeps naming it, and is sent so.
            const auto& named = field_of(shared_doc, field);
            const bool keeps_target = field_of(change.document, field).is_null() && named.is_string()
                                      && db::column_reference(table, field) != nullptr
                                      && origin.kept.contains(named.get<std::string>());
            if (keeps_target) {
                resend = true;
            } else if (!mine) {
                record[field] = field_of(change.document, field);
            }
        }
    } else {
        // New here, or back after a delete. Made here as well and not sent
        // (a setting set on both): this computer's is sent after, and wins.
        next.versions.clear();
        next.document = shared_form(table, change.document);
        for (const auto& [field, value] : change.document.items()) {
            next.versions[field] = version;
        }
        record = current && pending ? *current : change.document;
    }

    json write = record;
    if (current) {
        write = json::object();
        for (const auto& [field, value] : record.items()) {
            if (field_of(*current, field) != value) {
                write[field] = value;
            }
        }
    }
    if (resend) {
        book_.mark({table, key});
    }
    if (current && write.empty()) {
        next.id_here = db::record_id(db_, table, key);
        book_.set_shared(table, key, next);
        if (copy) {
            add_copy(table, key, std::move(*copy));
        }
        return true;
    }
    // Written whole or not at all: a record that needs one not here waits.
    db::Transaction attempt(db_);
    // Back after a delete, it takes the id it had, which JSON may still name.
    const auto written = db::write_record(db_, table, key, write, next.id_here);
    if (!written.written || !written.unresolved.empty()) {
        return false;
    }
    next.id_here = db::record_id(db_, table, key);
    if (!current) {
        add_defaults(db_, table, key, *next.document);
    }
    book_.set_shared(table, key, next);
    if (copy) {
        add_copy(table, key, std::move(*copy));
    }
    attempt.commit();
    if (current) {
        std::ranges::copy(without(files_of(table, key, *current), files_of(table, key, record)),
                          std::back_inserter(unowned_));
    } else {
        arrived_.insert(key);
    }
    return true;
}

}  // namespace seventhslide::sync
