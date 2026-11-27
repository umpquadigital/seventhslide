#include "seventhslide/sync/snapshot.hpp"

#include "json_read.hpp"
#include "seventhslide/crypto/sha256.hpp"
#include "seventhslide/db/sync_records.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace seventhslide::sync {
namespace {

using nlohmann::json;

const JsonRead k_read("A sync snapshot");
const JsonRead k_read_part("A snapshot's part");

/// How many change files ``files`` holds, all computers' together.
std::int64_t count_files(const std::map<std::string, std::int64_t>& files)
{
    std::int64_t n = 0;
    for (const auto& [m, count] : files) {
        // Saturating: a file that claims more is not a snapshot of anything.
        n = count > k_max_counter - n ? k_max_counter : n + count;
    }
    return n;
}

/// ``r`` as a part holds it: what its JSON dumps to, members in order, made
/// without a copy of its document (a Bible's is large).
std::string record_text(const SnapshotRecord& r)
{
    std::string out = r.document ? R"({"document":)" + r.document->dump() : std::string(R"({"deleted":true)");
    out += R"(,"key":)" + json(r.key).dump();
    out += R"(,"table":)" + json(r.table).dump();
    out += R"(,"versions":)" + db::versions_to_json(r.versions).dump();
    out += '}';
    return out;
}

json held_to_json(const db::HeldChange& h)
{
    return json{{"table", h.table},
                {"key", h.key},
                {"machine", h.machine},
                {"sequence", h.sequence},
                {"change", h.change}};
}

SnapshotRecord record_from_json(const JsonRead& read, const json& r)
{
    if (!r.is_object()) {
        read.malformed("a record is not an object");
    }
    SnapshotRecord record{read.text(r, "table"), read.text(r, "key"), std::nullopt, {}};
    try {
        record.versions = db::versions_from_json(read.member(r, "versions"));
    } catch (const std::invalid_argument& ex) {
        read.malformed(ex.what());
    }
    if (!read.optional_flag(r, "deleted", false)) {
        record.document = read.member(r, "document");
        if (!record.document->is_object()) {
            read.malformed("a record's document is not an object");
        }
    }
    return record;
}

db::HeldChange held_from_json(const JsonRead& read, const json& h)
{
    if (!h.is_object()) {
        read.malformed("a held change is not an object");
    }
    return {read.text(h, "table"), read.text(h, "key"), read.text(h, "machine"),
            read.number(h, "sequence"), read.member(h, "change")};
}

/// ``j``'s list ``name``.
const json& list(const JsonRead& read, const json& j, const char* name)
{
    const auto& v = read.member(j, name);
    if (!v.is_array()) {
        read.malformed(std::string(name) + " is not a list");
    }
    return v;
}

/// The change files a snapshot holds, from each computer.
std::map<std::string, std::int64_t> files_of(const JsonRead& read, const json& j)
{
    const auto& files = read.member(j, "files");
    if (!files.is_object()) {
        read.malformed("files is not an object");
    }
    std::map<std::string, std::int64_t> out;
    for (const auto& [machine, count] : files.items()) {
        if (!is_machine_id(machine) || !count.is_number_integer()
            || count.get<std::int64_t>() < 0) {
            read.malformed("files holds something but computers' file counts");
        }
        out.emplace(machine, count.get<std::int64_t>());
    }
    return out;
}

using detail::EntryPlace;

enum Kind : int { Records = 0, Held = 1, Media = 2 };

/// The lists of a part, in the order it holds them.
constexpr std::array<const char*, 3> k_lists{"records", "held", "media"};

/// A part with nothing in it: the most a part's frame adds to its entries.
constexpr std::string_view k_empty_part = R"({"format":3,"records":[],"held":[],"media":[]})";
/// The largest entry a part takes: a record of ``db::k_largest_record`` (its
/// document, as sending measures it) with its table, key and versions, so
/// one that is sent is never refused here.
constexpr auto k_largest_entry = static_cast<std::int64_t>(db::k_largest_record + db::k_record_allowance);
// Every part the writer makes may pass between computers: one of the largest
// entry alone, or one of several records.
static_assert(k_largest_entry + static_cast<std::int64_t>(k_empty_part.size()) <= k_largest_store_file);
static_assert(k_snapshot_part_most <= k_largest_store_file);

/// Where ``table`` falls in ``db::record_tables_in_order``; nullopt for a
/// table that does not sync by record.
std::optional<std::size_t> table_rank(std::string_view table)
{
    static const auto tables = db::record_tables_in_order();
    const auto it = std::ranges::find(tables, table);
    if (it == tables.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - tables.begin());
}

EntryPlace place_of(const SnapshotRecord& r, std::size_t rank)
{
    return {Records, rank, r.key, 0, {}, {}};
}

EntryPlace place_of(const db::HeldChange& h)
{
    return {Held, 0, h.machine, h.sequence, h.table, h.key};
}

EntryPlace place_of(const db::MediaDigest& m)
{
    return {Media, 0, m.path, 0, {}, {}};
}

/// The first 64 bits of the SHA-256 of ``name``.
std::uint64_t name_hash(std::string_view name)
{
    const auto digest = crypto::sha256(name);
    std::uint64_t u = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        u = (u << 8) | static_cast<unsigned char>(digest[i]);
    }
    return u;
}

/// 2^64 / ``target``, rounded down: (2^64 - ``target``) / ``target`` + 1.
std::uint64_t per_byte(std::uint64_t target)
{
    return (UINT64_MAX - (target - 1)) / target + 1;
}

SnapshotIndex read_index_label(const json& j)
{
    if (!j.is_object()) {
        k_read.malformed("not a JSON object");
    }
    SnapshotIndex s;
    s.format = k_read.format(j);
    if (s.format < k_snapshot_format) {
        k_read.malformed("not a snapshot in parts");
    }
    s.schema = k_read.schema(j);
    s.library = k_read.text(j, "library");
    s.machine = k_read.text(j, "machine");
    if (!is_machine_id(s.machine)) {
        k_read.malformed("machine is not a machine id");
    }
    s.computer = k_read.text(j, "computer");
    s.time = k_read.text(j, "time");
    s.files = files_of(k_read, j);
    for (const auto& p : list(k_read, j, "parts")) {
        if (!p.is_object()) {
            k_read.malformed("a part is not an object");
        }
        SnapshotPartRef part{k_read.text(p, "sha256"), k_read.number(p, "size")};
        if (!is_part_name(part.sha256) || part.size < 0) {
            k_read.malformed("parts holds something but parts' hashes and sizes");
        }
        s.parts.push_back(std::move(part));
    }
    return s;
}

SnapshotIndex read_index(const json& j)
{
    if (j.is_object()) {
        refuse_newer(k_read, j, k_snapshot_format);
    }
    SnapshotIndex s = read_index_label(j);
    std::set<std::string_view> named;
    for (const auto& p : s.parts) {
        if (p.size < 1 || p.size > k_largest_store_file) {
            k_read.malformed("a part's size is out of range");
        }
        if (!named.insert(p.sha256).second) {
            k_read.malformed("a part is named twice");
        }
    }
    s.counter = k_read.counter(j, "counter");
    s.records = k_read.counter(j, "records");
    s.held = k_read.counter(j, "held");
    s.media = k_read.counter(j, "media");
    return s;
}

SnapshotPart read_part(const json& j)
{
    if (!j.is_object()) {
        k_read_part.malformed("not a JSON object");
    }
    const auto format = k_read_part.format(j);
    if (format > k_snapshot_format) {
        throw NewerFormat();
    }
    if (format < k_snapshot_format) {
        k_read_part.malformed("not a snapshot's part");
    }
    SnapshotPart part;
    std::optional<EntryPlace> last;
    const auto next = [&](EntryPlace place) {
        if (last && !(*last < place)) {
            k_read_part.malformed("its entries are out of order, or one is there twice");
        }
        last = std::move(place);
    };
    for (const auto& r : list(k_read_part, j, "records")) {
        auto record = record_from_json(k_read_part, r);
        const auto rank = table_rank(record.table);
        if (!rank) {
            k_read_part.malformed("a record is of a table that does not sync");
        }
        next(place_of(record, *rank));
        part.records.push_back(std::move(record));
    }
    for (const auto& h : list(k_read_part, j, "held")) {
        auto held = held_from_json(k_read_part, h);
        if (!is_machine_id(held.machine)) {
            k_read_part.malformed("a held change's machine is not a machine id");
        }
        next(place_of(held));
        part.held.push_back(std::move(held));
    }
    for (const auto& m : list(k_read_part, j, "media")) {
        db::MediaDigest media;
        try {
            media = db::media_digest_from_json(m);
        } catch (const std::invalid_argument& ex) {
            k_read_part.malformed(ex.what());
        }
        if (!media.version) {
            k_read_part.malformed("media holds a file without its version");
        }
        next(place_of(media));
        part.media.push_back(std::move(media));
    }
    return part;
}

}  // namespace

std::int64_t SnapshotIndex::file_count() const
{
    return count_files(files);
}

json snapshot_index_to_json(const SnapshotIndex& s)
{
    json parts = json::array();
    for (const auto& p : s.parts) {
        parts.push_back(json{{"sha256", p.sha256}, {"size", p.size}});
    }
    return json{{"format", s.format},     {"schema", s.schema},   {"library", s.library},
                {"machine", s.machine},   {"computer", s.computer}, {"time", s.time},
                {"counter", s.counter},   {"files", json(s.files)}, {"parts", std::move(parts)},
                {"records", s.records},   {"held", s.held},       {"media", s.media}};
}

SnapshotIndex snapshot_index_from_json(const json& j)
{
    return k_read.whole(j, read_index);
}

SnapshotIndex snapshot_index_label(const json& j)
{
    return k_read.whole(j, read_index_label);
}

SnapshotPart read_snapshot_part(std::string_view text, std::string_view sha256)
{
    if (crypto::sha256_hex(text) != sha256) {
        throw std::runtime_error("A snapshot's part is not the one its snapshot names");
    }
    return read_checked_snapshot_part(text);
}

SnapshotPart read_checked_snapshot_part(std::string_view text)
{
    json j;
    try {
        j = json::parse(text);
    } catch (const json::exception& ex) {
        k_read_part.malformed(ex.what());
    }
    return k_read_part.whole(j, read_part);
}

PartWriter::PartWriter(Emit on_part, std::int64_t target)
    : emit_(std::move(on_part)), target_(target), most_(target * (k_snapshot_part_most / k_snapshot_part_bytes)),
      per_byte_(target >= 2 ? per_byte(static_cast<std::uint64_t>(target)) : 0)
{
    if (target < 2) {
        throw std::invalid_argument("A snapshot's parts are of 2 bytes or more");
    }
}

void PartWriter::add(const SnapshotRecord& record)
{
    const auto rank = table_rank(record.table);
    if (!rank) {
        throw std::invalid_argument("A snapshot holds records only of tables that sync by record, not "
                                    + record.table);
    }
    add_entry(place_of(record, *rank), record.table + '\x1f' + record.key, record_text(record));
}

void PartWriter::add(const db::HeldChange& held)
{
    add_entry(place_of(held),
              held.machine + '\x1f' + std::to_string(held.sequence) + '\x1f' + held.table + '\x1f'
                  + held.key,
              held_to_json(held).dump());
}

void PartWriter::add(const db::MediaDigest& media)
{
    if (!media.version) {
        throw std::invalid_argument("A snapshot holds each media file with its version");
    }
    add_entry(place_of(media), media.path, db::media_digest_to_json(media).dump());
}

void PartWriter::add_entry(EntryPlace place, std::string_view name, const std::string& text)
{
    if (last_ && !(*last_ < place)) {
        throw std::invalid_argument("A snapshot's entries are given in order, each once");
    }
    const auto bytes = static_cast<std::int64_t>(text.size());
    if (bytes > k_largest_entry) {
        constexpr std::int64_t mb = 1000 * 1000;
        throw EntryTooLarge("A record is too large to sync (" + std::to_string((bytes + mb - 1) / mb)
                                 + " MB; the most is "
                                 + std::to_string(static_cast<std::int64_t>(db::k_largest_record) / mb) + " MB)",
                            bytes);
    }
    const int kind = place.kind;
    last_ = std::move(place);
    if (bytes >= target_) {
        cut();
        append(kind, text);
        cut();
        return;
    }
    // ``part_`` holds some of the frame already: this overcounts a little.
    const auto most = static_cast<std::int64_t>(part_.size() + 1 + k_empty_part.size()) + bytes;
    if (!part_.empty() && most > most_) {
        cut();
    }
    append(kind, text);
    if (name_hash(name) < static_cast<std::uint64_t>(bytes) * per_byte_) {
        cut();
    }
}

void PartWriter::append(int kind, std::string_view text)
{
    if (part_.empty()) {
        part_ = R"({"format":3)";
        list_ = -1;
    }
    open_list(kind);
    if (!empty_list_) {
        part_ += ',';
    }
    part_ += text;
    empty_list_ = false;
    auto& count = kind == Records ? index_.records : kind == Held ? index_.held : index_.media;
    ++count;
}

void PartWriter::open_list(int kind)
{
    for (; list_ < kind; ++list_) {
        part_ += list_ < 0 ? R"(,")" : R"(],")";
        part_ += k_lists.at(static_cast<std::size_t>(list_ + 1));
        part_ += R"(":[)";
        empty_list_ = true;
    }
}

void PartWriter::cut()
{
    if (part_.empty()) {
        return;
    }
    open_list(Media);
    part_ += "]}";
    SnapshotPartRef ref{crypto::sha256_hex(part_), static_cast<std::int64_t>(part_.size())};
    index_.parts.push_back(ref);
    emit_(ref, part_);
    part_.clear();
}

SnapshotIndex PartWriter::finish()
{
    cut();
    return std::move(index_);
}

SnapshotPart PartReader::next(std::string_view text)
{
    if (read_ >= index_.parts.size()) {
        k_read_part.malformed("its snapshot names no more parts");
    }
    auto part = read_snapshot_part(text, index_.parts[read_].sha256);
    ++read_;
    // Each part's entries are in order already: its first comes after the
    // last part's last.
    std::optional<EntryPlace> first;
    std::optional<EntryPlace> last;
    const auto saw = [&](EntryPlace place) {
        if (!first) {
            first = place;
        }
        last = std::move(place);
    };
    for (const auto& r : part.records) {
        saw(place_of(r, table_rank(r.table).value_or(0)));
    }
    for (const auto& h : part.held) {
        saw(place_of(h));
    }
    for (const auto& m : part.media) {
        saw(place_of(m));
    }
    if (first && last_ && !(*last_ < *first)) {
        k_read_part.malformed("its entries are not in order after the last part's");
    }
    if (last) {
        last_ = std::move(last);
    }
    records_ += static_cast<std::int64_t>(part.records.size());
    held_ += static_cast<std::int64_t>(part.held.size());
    media_ += static_cast<std::int64_t>(part.media.size());
    return part;
}

void PartReader::finish() const
{
    if (read_ != index_.parts.size() || records_ != index_.records || held_ != index_.held
        || media_ != index_.media) {
        k_read.malformed("its parts do not hold what it says");
    }
}

}  // namespace seventhslide::sync
