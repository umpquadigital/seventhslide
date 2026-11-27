// SyncEngine: what is marked here.
//
// One slice of SyncEngine (declared in sync_engine.hpp): MarkedHere, the
// outbox's marks read once in a sync and kept up to date as changes are
// applied, with what each marked record names; and marked_now, every
// record marked, parents first.
// Seam: db (the outbox).

#include "seventhslide/sync/sync_engine.hpp"

#include "sync_engine_internal.hpp"

#include "seventhslide/db/sync_outbox.hpp"

#include <algorithm>

namespace seventhslide::sync {

void SyncEngine::MarkedHere::forget()
{
    read_ = false;
    names_read_ = false;
    marks_.clear();
    named_.clear();
    changed_.clear();
    went_.clear();
    came_back_.clear();
}

void SyncEngine::MarkedHere::changed(std::string_view table, std::string_view key)
{
    if (read_) {
        changed_.emplace(table, key);
    }
}

void SyncEngine::MarkedHere::went(std::string_view table, std::string_view key)
{
    if (read_) {
        changed_.emplace(table, key);
        went_.emplace(table, key);
    }
}

void SyncEngine::MarkedHere::came_back(std::string_view table, std::string_view key)
{
    if (read_) {
        changed_.emplace(table, key);
        came_back_.emplace(table);
    }
}

void SyncEngine::MarkedHere::refresh()
{
    // Changed when another connection (the window) has written since.
    const auto version = db_.query_int64("PRAGMA data_version");
    if (!read_ || version != data_version_) {
        forget();
        read_ = true;
        data_version_ = version;
        read_marks({});
        return;
    }
    const auto read_whole = pointing_at(came_back_);
    for (const auto& table : read_whole) {
        read_marks(table);
    }
    // Marks read again, or new.
    std::vector<RecordName> read;
    std::set<std::string> went_from;
    for (const auto& record : went_) {
        went_from.insert(record.first);
    }
    for (const auto& table : pointing_at(went_from)) {
        if (!read_whole.contains(table)) {
            list_marks(table, read);
        }
    }
    for (const auto& record : changed_) {
        if (!read_whole.contains(record.first) && !db::marked_whole(record.first)) {
            read_again(record);
            read.push_back(record);
        }
    }
    if (!went_.empty()) {
        // What named one that went, or one that went with it.
        read_all_names();
        std::vector<std::string> gone;
        for (const auto& record : went_) {
            gone.push_back(record.second);
        }
        for (const auto& record : read) {
            if (marks_.contains(record) && !here(record)) {
                gone.push_back(record.second);
            }
        }
        for (std::set<std::string> done; !gone.empty();) {
            const auto name = std::move(gone.back());
            gone.pop_back();
            const auto it = named_.find(name);
            if (!done.insert(name).second || it == named_.end()) {
                continue;
            }
            for (const auto& record : std::vector<RecordName>(it->second.begin(), it->second.end())) {
                read_again(record);
                if (marks_.contains(record) && !here(record)) {
                    gone.push_back(record.second);
                }
            }
        }
    }
    changed_.clear();
    went_.clear();
    came_back_.clear();
}

std::set<std::string> SyncEngine::MarkedHere::pointing_at(const std::set<std::string>& tables)
{
    std::set<std::string> out;
    std::vector<std::string> next(tables.begin(), tables.end());
    while (!next.empty()) {
        const auto target = std::move(next.back());
        next.pop_back();
        for (const auto& t : db::k_sync_tables) {
            if (db::points_at(t.table, target)) {
                if (const std::string from(db::owner_of(t.table)); out.insert(from).second) {
                    next.push_back(from);
                }
            }
        }
    }
    return out;
}

void SyncEngine::MarkedHere::read_marks(std::string_view table)
{
    if (!table.empty()) {
        for (auto it = marks_.lower_bound({std::string(table), {}}); it != marks_.end() && it->first.first == table;) {
            drop(it++);
        }
    }
    for (auto& mark : table.empty() ? db::marked_records(db_) : db::marked_records(db_, table)) {
        if (!db::marked_whole(mark.table)) {
            add({std::move(mark.table), std::move(mark.key)});
        }
    }
}

void SyncEngine::MarkedHere::list_marks(std::string_view table, std::vector<RecordName>& added)
{
    for (auto& mark : db::marked_records(db_, table)) {
        RecordName record{std::move(mark.table), std::move(mark.key)};
        if (!db::marked_whole(record.first) && !marks_.contains(record)) {
            add(record);
            added.push_back(std::move(record));
        }
    }
}

void SyncEngine::MarkedHere::read_again(RecordName record)
{
    if (const auto it = marks_.find(record); it != marks_.end()) {
        drop(it);
    }
    if (book_.is_marked(record.first, record.second)) {
        add(std::move(record));
    }
}

void SyncEngine::MarkedHere::add(RecordName record)
{
    const auto it = marks_.try_emplace(std::move(record)).first;
    if (names_read_) {
        read_names(it);
    }
}

void SyncEngine::MarkedHere::drop(std::map<RecordName, Mark>::iterator it)
{
    for (const auto& name : it->second.names.value_or(std::vector<std::string>{})) {
        const auto named = named_.find(name);  // there: ``read_names`` put each name in
        named->second.erase(it->first);
        if (named->second.empty()) {
            named_.erase(named);
        }
    }
    marks_.erase(it);
}

void SyncEngine::MarkedHere::read_names(std::map<RecordName, Mark>::iterator it)
{
    const auto& [table, key] = it->first;
    const auto current = db::read_record(db_, table, key);
    it->second.here = current.has_value();
    std::vector<std::string> names;
    if (current) {
        for (auto& name : db::names_in(table, key, *current)) {
            if (name != key) {
                named_[name].insert(it->first);
                names.push_back(std::move(name));
            }
        }
    }
    it->second.names = std::move(names);
}

bool SyncEngine::MarkedHere::here(const RecordName& record)
{
    auto& mark = marks_.at(record);
    if (!mark.here) {
        mark.here = db::record_exists(db_, record.first, record.second);
    }
    return *mark.here;
}

void SyncEngine::MarkedHere::read_all_names()
{
    if (!names_read_) {
        names_read_ = true;
        for (auto it = marks_.begin(); it != marks_.end(); ++it) {
            read_names(it);
        }
    }
}

bool SyncEngine::MarkedHere::shared_live(const RecordName& record)
{
    auto& mark = marks_.at(record);
    if (!mark.shared_live) {
        const auto shared = book_.shared(record.first, record.second);
        mark.shared_live = shared && shared->document;
    }
    return *mark.shared_live;
}

std::vector<SyncEngine::RecordName> SyncEngine::MarkedHere::naming(const std::string& name)
{
    read_all_names();
    const auto it = named_.find(name);
    return it == named_.end() ? std::vector<RecordName>{} : std::vector<RecordName>(it->second.begin(), it->second.end());
}

std::vector<SyncEngine::RecordName> SyncEngine::marked_now()
{
    std::vector<RecordName> records;
    for (const auto& mark : db::marked_records(db_)) {
        if (db::marked_whole(mark.table) && mark.key.empty()) {
            for (auto& key : db::record_keys(db_, mark.table)) {
                records.emplace_back(mark.table, std::move(key));
            }
            for (auto& key : book_.shared_keys(mark.table)) {
                records.emplace_back(mark.table, std::move(key));
            }
        } else {
            records.emplace_back(mark.table, mark.key);
        }
    }
    std::ranges::sort(records, [](const RecordName& a, const RecordName& b) {
        return std::pair(table_rank(a.first), a.second) < std::pair(table_rank(b.first), b.second);
    });
    const auto [first, last] = std::ranges::unique(records);
    records.erase(first, last);
    return records;
}

}  // namespace seventhslide::sync
