// SyncEngine: sending what changed here.
//
// One slice of SyncEngine (declared in sync_engine.hpp): send_changes,
// which puts what changed in change files, and what it asks first: the
// records that would wait for one too large to send (waiting_ahead), and
// the media files the changes bring (new_files, read_new_digests). What
// the size limit keeps here is sync_engine_media.cpp's.
// Seam: db (the records, their shared copies and the outbox) and the store.

#include "seventhslide/sync/sync_engine.hpp"

#include "sync_engine_internal.hpp"

#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_outbox.hpp"
#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/media_pieces.hpp"
#include "seventhslide/sync/now_utc.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <stdexcept>
#include <utility>

namespace seventhslide::sync {
namespace {

// A change file of the largest record fits, with what comes with it.
static_assert(static_cast<std::int64_t>(db::k_largest_record + db::k_record_allowance) <= k_largest_store_file);

/// Whether ``field``, holding ``value``, is as ``shared_doc`` has it.
bool field_as_shared(const json& shared_doc, const std::string& field, const json& value)
{
    return field_of(shared_doc, field) == value && shared_doc.contains(field);
}

/// Add ``item`` to ``list`` unless it is there.
template <typename T>
void add_once(std::vector<T>& list, T item)
{
    if (std::ranges::find(list, item) == list.end()) {
        list.push_back(std::move(item));
    }
}

/// The media file ``path`` as it is in ``media``: its size, and its root
/// from its pieces' hashes (kept in ``pieces``, for the link to send it by);
/// nullopt when it is not there.
std::optional<db::MediaDigest> read_digest(const std::filesystem::path& media,
                                           const PieceHashes& pieces, const std::string& path)
{
    const auto file = media / path;
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto hashes = pieces.of(path, file);
    if (!hashes) {
        return std::nullopt;
    }
    return db::MediaDigest{path, static_cast<std::int64_t>(size), media_root(*hashes)};
}

}  // namespace

std::set<SyncEngine::RecordName> SyncEngine::waiting_ahead(const std::vector<RecordName>& records,
                                                          std::size_t from, const std::set<RecordName>& held,
                                                          std::set<std::string>& unsent,
                                                          std::map<RecordName, std::size_t>& measured)
{
    const auto& table = records[from].first;
    // What each names, and whether an older version of it was shared.
    std::map<RecordName, std::pair<std::set<std::string>, bool>> names;
    for (auto i = from; i < records.size() && records[i].first == table; ++i) {
        if (held.contains(records[i])) {
            continue;
        }
        const auto& key = records[i].second;
        const auto current = db::read_record(db_, table, key);
        if (!current) {
            continue;
        }
        const auto shared = book_.shared(table, key);
        const bool live = shared && shared->document;
        if (live) {
            bool same = true;
            const auto form = shared_form(table, *current);
            for (const auto& [field, value] : form.items()) {
                same = same && field_as_shared(*shared->document, field, value);
            }
            if (same) {
                continue;  // nothing to send: its mark goes
            }
        } else {
            const auto bytes = current->dump().size();
            measured.emplace(records[i], bytes);
            if (bytes > db::k_largest_record) {
                unsent.insert(key);
            }
        }
        names.emplace(records[i], std::pair(db::names_in(table, key, *current), live));
    }
    // One naming another that waits waits too; the others wait for it only
    // when they have no version of it.
    std::set<RecordName> out;
    for (bool more = true; more;) {
        more = false;
        for (const auto& [name, in] : names) {
            const auto& [named, live] = in;
            if (!out.contains(name)
                && std::ranges::any_of(named, [&](const std::string& n) { return unsent.contains(n); })) {
                out.insert(name);
                if (!live) {
                    unsent.insert(name.second);
                    more = true;
                }
            }
        }
    }
    return out;
}

std::vector<std::string> SyncEngine::new_files(const std::string& table, const std::string& key,
                                               const json& current)
{
    const auto shared = book_.shared(table, key);
    const auto before = shared && shared->document ? files_of(table, key, *shared->document)
                                                   : std::vector<std::string>{};
    return without(files_of(table, key, current), before);
}

std::map<std::string, db::MediaDigest> SyncEngine::read_new_digests(const std::set<RecordName>& held)
{
    std::map<std::string, db::MediaDigest> out;
    for (const auto& [table, key] : marked_now()) {
        if (!db::owns_files(table) || held.contains({table, key})) {
            continue;
        }
        const auto current = db::read_record(db_, table, key);
        if (!current) {
            continue;
        }
        for (const auto& path : new_files(table, key, *current)) {
            // A file missing here is missing on the computers the change goes
            // to too, until whoever has it declares it.
            if (auto digest = read_digest(media_, pieces_, path)) {
                out.emplace(path, std::move(*digest));
            }
        }
    }
    return out;
}

void SyncEngine::send_changes(SyncResult& result)
{
    if (holds_only_) {
        // Nothing of its own: what the merge rules marked here is let go of.
        if (!db::marked_records(db_).empty()) {
            book_.clear_marks();
        }
        return;
    }
    // Nothing to send, or joined again and waiting for what it sent before
    // (a change made now must come after it): found out before any file is
    // fingerprinted. This engine alone writes what says so.
    if (db::marked_records(db_).empty()) {
        return;
    }
    if (applied_from(book_.applied(), machine_) < book_.last_sent()) {
        // What is too large is said all the same.
        db::ReadTransaction read(db_);
        for (const auto& [table, key] : marked_now()) {
            auto current = db::read_record(db_, table, key);
            const auto bytes = current ? current->dump().size() : 0;
            if (bytes > db::k_largest_record) {
                const auto shared = book_.shared(table, key);
                const Change change{table, key, false, {}, std::move(*current), {}};
                const auto title =
                    sent_record(change, shared && shared->document ? *shared->document : json()).title;
                add_once(result.too_large, TooLarge{table, key, title, static_cast<std::int64_t>(bytes)});
            }
        }
        return;
    }
    // What the size limit keeps here, and the files the others' records
    // bring, read from the disk first: a video takes a while to fingerprint.
    const auto held = size_limit_ > 0 ? held_back(media_use()) : std::set<RecordName>{};
    auto new_digests = read_new_digests(held);

    // Each change file is made from a read of the library (which no edit in
    // the window waits for), signed and written to the store, and only then
    // recorded, in a transaction of its own: the window's edits wait for
    // that short write alone. A record edited after it was read is left
    // marked, and goes in a later file.
    std::vector<db::MarkedRecord> marks;
    std::vector<RecordName> records;
    std::map<RecordName, std::vector<Referrer>> referrers;
    {
        db::ReadTransaction read(db_);
        marks = db::marked_records(db_);
        records = marked_now();
        // What points at each record deleted here, as the others last had
        // it: taken before any file of this send updates a shared copy.
        std::vector<RecordName> deleted;
        std::set<std::string> deleted_names;
        for (const auto& [table, key] : records) {
            if (!named_by_others(table) || db::record_exists(db_, table, key)) {
                continue;
            }
            if (const auto shared = book_.shared(table, key); shared && shared->document) {
                deleted.emplace_back(table, key);
                deleted_names.insert(key);
            }
        }
        auto named = book_.mentioning(deleted_names);
        for (const auto& record : deleted) {
            auto& list = referrers[record];
            for (const auto& [t, k] : named[record.second]) {
                list.push_back({t, k, book_.shared(t, k)->versions});
            }
        }
    }
    const auto library = db::library_uid(db_);
    auto applied = book_.applied();
    applied.erase(machine_);

    // A file sent before a crash, and never recorded here, keeps its number
    // and its counter: the next file comes after both.
    std::int64_t sequence = book_.last_sent();
    std::int64_t counter = book_.counter();
    for (const auto n : store_.sequences(Numbered::Changes, machine_)) {
        if (n > book_.last_sent()) {
            sequence = std::max(sequence, n);
            // Never set aside: its number is taken, whatever it holds. One
            // that cannot be read had a counter past those recorded.
            try {
                const auto sent = unseal(store_.read(Numbered::Changes, machine_, n),
                                         "A sync change file this computer sent");
                counter = std::max(counter, change_file_from_json(sent.body).counter);
            } catch (const std::exception&) {
                counter = std::max(counter, book_.counter() + (n - book_.last_sent()));
            }
        }
    }
    ++sequence;
    db::SyncVersion version{counter + 1, machine_};

    // A record as the others will have it (its shared form), or absent.
    const auto form_now = [&](const std::string& table, const std::string& key) {
        const auto current = db::read_record(db_, table, key);
        return current ? std::optional(shared_form(table, *current)) : std::nullopt;
    };
    // The outputs are marked together (``db::marked_whole``: every save
    // rewrites them all), so their mark goes once all of them are sent, none
    // edited since.
    std::optional<std::size_t> last_whole;
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (db::marked_whole(records[i].first)) {
            last_whole = i;
        }
    }
    std::map<RecordName, std::optional<json>> whole_read;
    std::vector<bool> resolved(marks.size(), false);
    // By key, the records the others would wait for if one named them: each
    // too large to send that was never shared, and each never shared naming
    // one of those. What names one stays here, marked, with it; the others
    // have an older version of any other kept so. A record names those of
    // tables before its own, read by then (or a media file's, never that
    // large); a table whose records name each other has its looked at before
    // any is sent (``waiting_ahead``).
    std::set<std::string> unsent;
    std::set<RecordName> waiting;
    std::string looked_ahead;

    // What changed goes in files of at most ``k_change_file_bytes`` each, in
    // order (parents first), each with the next number and version: a whole
    // library put online is many files, none too large to pass on. A record
    // that would take a file past that starts the next; one larger has a
    // file to itself.
    const auto signed_head = seal(json::object(), {{k_signed_as_computer, &key_}}).size() - 2;
    std::size_t next = 0;
    do {
        ChangeFile file;
        file.library = library;
        file.machine = machine_;
        file.computer = computer_;
        file.time = now_utc();
        file.sequence = sequence;
        file.counter = version.counter;
        file.seen = applied;
        // The file's bytes as written, counted as it fills: each entry adds
        // itself and a comma.
        const std::size_t label = signed_head + change_file_to_json(file).dump().size();
        std::size_t size = label;
        std::vector<std::pair<RecordName, db::SharedCopy>> shared_next;
        std::vector<db::MediaDigest> declared;
        std::vector<SentRecord> noted;
        // Each record as it was read, to see at the write whether it changed.
        std::map<RecordName, std::optional<json>> read_as;
        {
            db::ReadTransaction read(db_);
            // Documents already dumped in this read: the same now.
            std::map<RecordName, std::size_t> measured;
            for (; next < records.size(); ++next) {
                const auto& [table, key] = records[next];
                if (held.contains({table, key})) {
                    continue;  // stays marked, until there is room
                }
                if (table != looked_ahead && db::names_its_own(table)) {
                    looked_ahead = table;
                    waiting.merge(waiting_ahead(records, next, held, unsent, measured));
                }
                if (waiting.contains({table, key})) {
                    add_once(result.kept_with_too_large, records[next]);
                    continue;  // stays marked, with what it names
                }
                const auto shared = book_.shared(table, key);
                const auto current = db::read_record(db_, table, key);
                const bool live = shared && shared->document;
                const auto current_form =
                    current ? std::optional(shared_form(table, *current)) : std::nullopt;
                const auto take_read = [&] {
                    read_as.emplace(records[next], current_form);
                    if (db::marked_whole(records[next].first)) {
                        whole_read.emplace(records[next], current_form);
                    }
                };
                const json shared_doc = live ? *shared->document : json();
                Change change{table, key, !current, {}, json(), {}};
                db::SharedCopy copy;
                std::vector<db::MediaDigest> media;
                std::size_t bytes = 0;  // the document's
                // Not sent: it stays marked, and the others wait for none
                // that was never shared.
                const auto keep_too_large = [&](std::size_t of) {
                    if (!live) {
                        unsent.insert(key);
                    }
                    add_once(result.too_large, TooLarge{table, key, sent_record(change, shared_doc).title,
                                                        static_cast<std::int64_t>(of)});
                };
                if (!current) {
                    if (!live) {
                        take_read();
                        continue;
                    }
                    for (const auto& [field, by] : shared->versions) {
                        change.base.emplace(field, by);
                    }
                    if (const auto it = referrers.find({table, key}); it != referrers.end()) {
                        change.referrers = it->second;
                    }
                    copy = db::SharedCopy{std::nullopt, {{"", version}}, shared->id_here};
                } else {
                    for (const auto& [field, value] : current_form->items()) {
                        if (live && field_as_shared(shared_doc, field, value)) {
                            continue;
                        }
                        std::optional<db::SyncVersion> before;
                        if (live) {
                            if (const auto it = shared->versions.find(field); it != shared->versions.end()) {
                                before = it->second;
                            }
                        }
                        change.base.emplace(field, before);
                    }
                    if (change.base.empty()) {
                        take_read();
                        continue;
                    }
                    change.document = *current;
                    // Never sent, so never read as sent: it stays marked.
                    if (!unsent.empty()
                        && std::ranges::any_of(db::names_in(table, key, *current),
                                               [&](const std::string& n) { return unsent.contains(n); })) {
                        if (!live) {
                            unsent.insert(key);
                        }
                        add_once(result.kept_with_too_large, records[next]);
                        continue;
                    }
                    const auto was = measured.find(records[next]);
                    bytes = was != measured.end() ? was->second : change.document.dump().size();
                    if (bytes > db::k_largest_record) {
                        keep_too_large(bytes);
                        continue;
                    }
                    if (live) {
                        for (const auto& [field, before] : change.base) {
                            if (db::is_word_list(table, field)) {
                                change.before.emplace(field, field_of(shared_doc, field));
                            }
                        }
                    }
                    if (db::owns_files(table)) {
                        for (const auto& path : new_files(table, key, *current)) {
                            const auto found = new_digests.find(path);
                            // A record changed since the digests were read: its
                            // file is read now.
                            auto digest = found != new_digests.end() ? std::optional(found->second)
                                                                     : read_digest(media_, pieces_, path);
                            if (digest) {
                                media.push_back(std::move(*digest));
                            }
                        }
                    }
                    copy = live ? *shared : db::SharedCopy{};
                    copy.document = current_form;
                    copy.id_here = db::record_id(db_, table, key);
                    for (const auto& [field, before] : change.base) {
                        copy.versions[field] = version;
                    }
                }
                std::size_t adds = 1;  // its comma
                if (change.deleted) {
                    adds += change_to_json(change).dump().size();
                } else {
                    // The document dumped once: the rest is measured with
                    // null in its place.
                    auto document = std::exchange(change.document, json());
                    adds += change_to_json(change).dump().size() - std::string_view("null").size() + bytes;
                    change.document = std::move(document);
                }
                for (const auto& digest : media) {
                    adds += db::media_digest_to_json(digest).dump().size() + 1;
                }
                if (label + adds > static_cast<std::size_t>(k_largest_store_file)) {
                    // No computer would take the file (its word lists as
                    // they were, a delete's referrers, are that large).
                    keep_too_large(adds);
                    continue;
                }
                if (!file.changes.empty() && size + adds > k_change_file_bytes) {
                    break;  // the next file's first
                }
                size += adds;
                take_read();
                for (auto& digest : media) {
                    auto mine = digest;
                    mine.version = version;
                    declared.push_back(std::move(mine));
                    file.media.push_back(std::move(digest));
                }
                shared_next.push_back({records[next], std::move(copy)});
                noted.push_back(sent_record(change, shared_doc));
                file.changes.push_back(std::move(change));
            }
        }

        const bool sending = !file.changes.empty();
        if (sending) {
            // By path, each once: two records can name one file.
            std::ranges::sort(file.media, {}, &db::MediaDigest::path);
            const auto [dup, end] = std::ranges::unique(file.media, {}, &db::MediaDigest::path);
            file.media.erase(dup, end);
            const auto text = seal(change_file_to_json(file), {{k_signed_as_computer, &key_}});
            if (static_cast<std::int64_t>(text.size()) > k_largest_store_file) {
                // Counted as it filled, so never; written, every later file
                // would wait behind it for ever. Nothing is recorded.
                qCWarning(lc_engine).nospace() << "Change file " << sequence << " of library "
                                               << QString::fromUtf8(library) << " is " << text.size()
                                               << " bytes, more than a file may be: not sent";
                throw std::runtime_error("Change file " + std::to_string(sequence) + " is "
                                         + std::to_string(text.size()) + " bytes, more than the "
                                         + std::to_string(k_largest_store_file) + " a file may be");
            }
            // On the disk before anything here says it was sent: when this
            // throws, nothing of the file is recorded, and its records stay
            // marked.
            store_.write(Numbered::Changes, machine_, sequence, text);
            last_in_store_[machine_] = sequence;
        }

        db::Transaction tx(db_);
        for (const auto& [name, copy] : shared_next) {
            book_.set_shared(name.first, name.second, copy);
        }
        for (const auto& digest : declared) {
            book_.set_media_digest(digest);
        }
        if (sending) {
            book_.set_last_sent(sequence);
            book_.set_applied(machine_, sequence);
            book_.set_counter(version.counter);
        }
        // A mark goes once what it marks is read, unless it was edited since
        // (the window's edit then left the mark as it was).
        for (std::size_t i = 0; i < marks.size(); ++i) {
            if (resolved[i]) {
                continue;
            }
            const auto& mark = marks[i];
            bool unchanged = true;
            if (db::marked_whole(mark.table) && mark.key.empty()) {
                if (last_whole && next <= *last_whole) {
                    continue;  // not all read yet
                }
                for (const auto& [name, form] : whole_read) {
                    unchanged = unchanged
                                && (name.first != mark.table || form_now(name.first, name.second) == form);
                }
                for (const auto& key : db::record_keys(db_, mark.table)) {
                    unchanged = unchanged && whole_read.contains({mark.table, key});
                }
            } else {
                const auto it = read_as.find({mark.table, mark.key});
                if (it == read_as.end()) {
                    continue;  // in another file, or kept here (the size limit, too large, naming one)
                }
                unchanged = form_now(mark.table, mark.key) == it->second;
            }
            resolved[i] = true;
            if (unchanged) {
                book_.unmark(mark);
            }
        }
        tx.commit();
        if (sending) {
            result.changes_sent += static_cast<int>(file.changes.size());
            std::ranges::move(noted, std::back_inserter(result.sent));
            ++sequence;
            version = {version.counter + 1, machine_};
        }
    } while (next < records.size());
}

}  // namespace seventhslide::sync
