#pragma once

// What the sync_engine*.cpp files share: the engine's log, where a table
// comes in the order records are written, a record's fields and name, the
// media files a record owns, and RecordWriter, which writes records in the
// order a snapshot holds them. Not part of the public API.
// Seam: db (the records and their tables); Qt Core for the log.

#include "seventhslide/sync/sync_engine.hpp"

#include "seventhslide/db/sync_records.hpp"
#include "seventhslide/db/sync_tables.hpp"

#include <QLoggingCategory>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <stdexcept>

namespace seventhslide::sync {

Q_DECLARE_LOGGING_CATEGORY(lc_engine)

using nlohmann::json;

/// Where ``table`` comes in ``db::record_tables_in_order``: records sorted by
/// it come after every record they refer to.
inline std::ptrdiff_t table_rank(std::string_view table)
{
    static const auto order = db::record_tables_in_order();
    return std::ranges::find(order, table) - order.begin();
}

/// The last change file applied from ``machine``; 0 before any.
inline std::int64_t applied_from(const std::map<std::string, std::int64_t>& applied,
                                 const std::string& machine)
{
    const auto it = applied.find(machine);
    return it != applied.end() ? it->second : 0;
}

/// ``doc``'s ``field``, null when it has none.
inline const json& field_of(const json& doc, const std::string& field)
{
    static const json none;
    if (!doc.is_object()) {
        return none;
    }
    const auto it = doc.find(field);
    return it == doc.end() ? none : *it;
}

inline std::string record_name(std::string_view table, std::string_view key)
{
    return std::string(table) + " " + std::string(key);
}

/// A table whose records others point at by their key, which they hold: a
/// uid, or a file's name. Pointing at one counts as using it, which wins over
/// a delete.
inline bool named_by_others(std::string_view table)
{
    return db::syncs_by_uid(table) || db::is_file_table(table);
}

/// The media files ``table``'s record ``key``, holding ``document``, owns
/// (``db::k_sync_files``): paths under the media folder, "images/<name>".
inline std::vector<std::string> files_of(std::string_view table, std::string_view key,
                                         const json& document)
{
    std::vector<std::string> out;
    for (const auto& f : db::k_sync_files) {
        if (f.table != table) {
            continue;
        }
        const std::string folder(f.folder);
        if (f.in == db::FileIn::Key) {
            if (db::is_media_file_name(key)) {
                out.push_back(folder + "/" + std::string(key));
            }
            continue;
        }
        // Each URL "/static/<folder>/<name>" in it (".../<owner>/<name>" for
        // a folder of owners), each part running to the first character no
        // file name here has (a quote, a backslash).
        const auto text = document.dump();
        const auto prefix = "/static/" + folder + "/";
        const auto part_end = [&text](std::size_t from) {
            while (from < text.size()
                   && ((text[from] >= 'a' && text[from] <= 'z') || (text[from] >= 'A' && text[from] <= 'Z')
                       || (text[from] >= '0' && text[from] <= '9') || text[from] == '.'
                       || text[from] == '_' || text[from] == '-')) {
                ++from;
            }
            return from;
        };
        for (auto at = text.find(prefix); at != std::string::npos; at = text.find(prefix, at)) {
            at += prefix.size();
            auto end = part_end(at);
            if (f.per_owner) {
                if (end >= text.size() || text[end] != '/'
                    || !db::is_media_file_name(text.substr(at, end - at))) {
                    continue;
                }
                end = part_end(end + 1);
            }
            if (const auto name = text.substr(at, end - at); db::is_sync_media_path(folder + "/" + name)) {
                out.push_back(folder + "/" + name);
            }
        }
    }
    std::ranges::sort(out);
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

/// ``a`` without what is also in ``b``; both sorted.
inline std::vector<std::string> without(const std::vector<std::string>& a,
                                        const std::vector<std::string>& b)
{
    std::vector<std::string> out;
    std::ranges::set_difference(a, b, std::back_inserter(out));
    return out;
}

/// Writes a snapshot's records in the order its parts hold them, a part at a
/// time, each with its shared copy. What a record points at in a table before
/// its own came before it; one of its own table may come after it (a folder's
/// parent, a service item another plays after), across parts too. A record
/// that cannot be without such a one waits, and one pointing at such a one is
/// written again, each once its table is done, its document held till then.
/// Keys are random, so that is about half the records naming one of their
/// own table: folders and service items, which are small.
class RecordWriter {
public:
    /// ``what`` begins what a record it cannot write throws.
    RecordWriter(db::Database& db, db::SyncBookkeeping& book, std::string what)
        : db_(db), book_(book), what_(std::move(what))
    {
    }

    /// Write ``document`` as ``table``'s record ``key``, taking ``id_here``
    /// if free, then ``shared`` as its shared copy (with the id the record
    /// has here, when it has none), now or once it can be.
    void write(const std::string& table, const std::string& key, json document, std::optional<std::int64_t> id_here,
               std::optional<db::SharedCopy> shared)
    {
        if (table != table_) {
            table_done();
            table_ = table;
        }
        Waiting w{table, key, std::move(document), id_here, std::move(shared)};
        if (!try_write(w)) {
            waiting_.push_back(std::move(w));
        }
    }

    /// Write what waits. Returns "table key" of each not written whole: one
    /// that needs a record not here.
    std::vector<std::string> write_waiting()
    {
        table_done();
        std::vector<std::string> broken;
        for (const auto* list : {&waiting_, &again_}) {
            for (const auto& w : *list) {
                broken.push_back(record_name(w.table, w.key));
            }
        }
        waiting_.clear();
        again_.clear();
        return broken;
    }

    /// Write what waits. Throws ``std::runtime_error``, after ``what``, for a
    /// record this library cannot take, or one that needs a record the
    /// snapshot lacks.
    void finish()
    {
        if (const auto broken = write_waiting(); !broken.empty()) {
            throw std::runtime_error(what_ + ": " + broken.front() + " needs a record it does not hold");
        }
    }

private:
    struct Waiting {
        std::string table;
        std::string key;
        json document;
        std::optional<std::int64_t> id_here;
        std::optional<db::SharedCopy> shared;
    };

    db::RecordWrite write_record(const Waiting& w, std::optional<std::int64_t> id_here)
    {
        try {
            return db::write_record(db_, w.table, w.key, w.document, id_here);
        } catch (const std::invalid_argument& ex) {
            throw std::runtime_error(what_ + ": a record in it is not one this library can take (" + ex.what()
                                     + ")");
        }
    }

    /// Whether ``w`` was written; one pointing at a record not here yet goes
    /// on to be written again.
    bool try_write(Waiting& w)
    {
        const auto written = write_record(w, w.id_here);
        if (!written.written) {
            return false;
        }
        if (w.shared) {
            if (!w.shared->id_here) {
                w.shared->id_here = db::record_id(db_, w.table, w.key);
            }
            book_.set_shared(w.table, w.key, *w.shared);
            w.shared.reset();
        }
        if (!written.unresolved.empty()) {
            again_.push_back(std::move(w));
        }
        return true;
    }

    /// The last table's records are all here: what waited for one of them.
    void table_done()
    {
        for (bool progress = true; progress && !waiting_.empty();) {
            progress = false;
            std::vector<Waiting> still;
            for (auto& w : waiting_) {
                if (try_write(w)) {
                    progress = true;
                } else {
                    still.push_back(std::move(w));
                }
            }
            waiting_ = std::move(still);
        }
        std::erase_if(again_, [&](const Waiting& w) { return write_record(w, std::nullopt).unresolved.empty(); });
    }

    db::Database& db_;
    db::SyncBookkeeping& book_;
    std::string what_;
    std::string table_;
    std::vector<Waiting> waiting_;
    std::vector<Waiting> again_;
};

}  // namespace seventhslide::sync
