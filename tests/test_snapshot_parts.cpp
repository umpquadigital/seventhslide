// A snapshot's parts: kept in the store by their hash, and where a snapshot
// is cut into them.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_bookkeeping.hpp>
#include <seventhslide/db/sync_records.hpp>
#include <seventhslide/sync/snapshot.hpp>
#include <seventhslide/sync/sync_store.hpp>

#include "file_time_string.hpp"
#include "test_temp_dir.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using seventhslide::crypto::sha256_hex;
using seventhslide::sync::FolderStore;

TEST_CASE("A part is kept by the hash of its bytes, once", "[sync][parts]")
{
    seventhslide::test::TempDir dir;
    FolderStore store(dir.path / "sync");
    const std::string bytes = R"({"format":3,"records":[],"held":[],"media":[]})";
    const auto name = sha256_hex(bytes);

    CHECK(store.parts().empty());
    CHECK_FALSE(store.keep_part(name));
    CHECK_THROWS_AS(store.read_part(name), std::runtime_error);

    store.write_part(name, bytes);
    CHECK(store.keep_part(name));
    CHECK(store.read_part(name) == bytes);
    CHECK(std::filesystem::exists(dir.path / "sync" / "snapshot_parts" / (name + ".json")));
    const auto parts = store.parts();
    REQUIRE(parts.size() == 1);
    CHECK(parts[0].sha256 == name);
    CHECK(parts[0].size == static_cast<std::int64_t>(bytes.size()));

    SECTION("bytes that are not what the name says are refused")
    {
        const std::string other = bytes + " ";
        CHECK_THROWS_AS(store.write_part(sha256_hex(other), bytes), std::invalid_argument);
        CHECK_THROWS_AS(store.write_part(name, other), std::invalid_argument);
        CHECK(store.parts().size() == 1);
        CHECK(store.read_part(name) == bytes);
    }
    SECTION("a name that is not a hash is refused")
    {
        for (const auto& bad : {std::string("../changes"), std::string(64, 'A'), std::string(63, 'a'),
                                      std::string(65, 'a'), std::string(62, 'a') + "/x"}) {
            CHECK_THROWS_AS(store.write_part(bad, bytes), std::invalid_argument);
            CHECK_THROWS_AS(store.read_part(bad), std::invalid_argument);
            CHECK_THROWS_AS(store.keep_part(bad), std::invalid_argument);
            CHECK_THROWS_AS(store.remove_part(bad, {}), std::invalid_argument);
            CHECK_THROWS_AS(store.set_aside_part(bad), std::invalid_argument);
        }
        // Nor is a file in the folder that is not a part listed.
        std::filesystem::copy_file(dir.path / "sync" / "snapshot_parts" / (name + ".json"),
                                   dir.path / "sync" / "snapshot_parts" / "notes.json");
        CHECK(store.parts().size() == 1);
    }
    SECTION("one written again is only touched, and a part kept likewise")
    {
        const auto file = dir.path / "sync" / "snapshot_parts" / (name + ".json");
        const auto long_ago = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 30);
        std::filesystem::last_write_time(file, long_ago);
        CHECK(store.parts().at(0).touched < long_ago + std::chrono::hours(1));

        store.write_part(name, bytes);
        CHECK(store.read_part(name) == bytes);
        CHECK(store.parts().at(0).touched > long_ago + std::chrono::hours(24));

        std::filesystem::last_write_time(file, long_ago);
        CHECK(store.keep_part(name));
        CHECK(store.parts().at(0).touched > long_ago + std::chrono::hours(24));
    }
    SECTION("one set aside is gone, and kept among the damaged")
    {
        store.set_aside_part(name);
        CHECK(store.parts().empty());
        CHECK_FALSE(store.keep_part(name));
        CHECK(std::filesystem::exists(dir.path / "sync" / "damaged" / ("part-" + name)));
        // And fetched again.
        store.write_part(name, bytes);
        CHECK(store.read_part(name) == bytes);
    }
    SECTION("one removed is gone")
    {
        const auto file = dir.path / "sync" / "snapshot_parts" / (name + ".json");
        const auto now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(file, now - std::chrono::hours(24 * 30));
        CHECK(store.remove_part(name, now - std::chrono::hours(24 * 7)));
        CHECK(store.parts().empty());
        CHECK_FALSE(store.keep_part(name));
        CHECK(store.remove_part(name, now));
    }
    SECTION("one touched since it was found old stays")
    {
        // Housekeeping found it untouched for a month; then a snapshot
        // arriving wanted it, before housekeeping let it go.
        const auto file = dir.path / "sync" / "snapshot_parts" / (name + ".json");
        const auto now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(file, now - std::chrono::hours(24 * 30));
        const auto week_ago = now - std::chrono::hours(24 * 7);
        REQUIRE(store.parts().at(0).touched < week_ago);
        REQUIRE(store.keep_part(name));
        CHECK_FALSE(store.remove_part(name, week_ago));
        CHECK(store.read_part(name) == bytes);
        CHECK(store.parts().size() == 1);
        // Nothing left beside it.
        CHECK(std::distance(std::filesystem::directory_iterator(file.parent_path()),
                            std::filesystem::directory_iterator())
              == 1);
    }
    SECTION("one a crash stopped going is put back")
    {
        const auto file = dir.path / "sync" / "snapshot_parts" / (name + ".json");
        auto away = file;
        away += ".going";
        std::filesystem::rename(file, away);
        CHECK(store.parts().empty());
        store.remove_old_temps();
        CHECK(store.read_part(name) == bytes);
        CHECK_FALSE(std::filesystem::exists(away));
    }
    SECTION("one damaged here is set aside and written again")
    {
        const auto file = dir.path / "sync" / "snapshot_parts" / (name + ".json");
        SECTION("cut short") { std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes.substr(0, 10); }
        SECTION("of its size")
        {
            auto other = bytes;
            other[3] = 'x';
            std::ofstream(file, std::ios::binary | std::ios::trunc) << other;
        }
        REQUIRE(store.read_part(name) != bytes);
        store.write_part(name, bytes);
        CHECK(store.read_part(name) == bytes);
        CHECK(std::filesystem::exists(dir.path / "sync" / "damaged" / ("part-" + name)));
    }
}

TEST_CASE("A snapshot removed says whether it went, and the old snapshots likewise", "[sync][parts]")
{
    seventhslide::test::TempDir dir;
    FolderStore store(dir.path / "sync");
    const seventhslide::sync::SnapshotName name{"church", 3};
    store.write_snapshot(name, "{}");
    CHECK(store.remove_snapshot(name));
    CHECK(store.snapshots().empty());
    CHECK(store.remove_snapshot(name));  // gone either way
    // One that will not go says so: a folder in its place.
    const auto in_place = dir.path / "sync" / "snapshot_index" / "church" / "0000000004.json";
    std::filesystem::create_directories(in_place / "inside");
    CHECK_FALSE(store.remove_snapshot({"church", 4}));

    CHECK_FALSE(store.remove_old_snapshots());
    // One that will not go is not said to have gone.
    const auto old = dir.path / "sync" / "snapshots" / "church";
    std::filesystem::create_directories(old);
    std::ofstream(old / "0000000001.json") << "{}";
    namespace fs = std::filesystem;
    fs::permissions(old, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    CHECK_FALSE(store.remove_old_snapshots());
    fs::permissions(old, fs::perms::owner_all, fs::perm_options::replace);
    CHECK(store.remove_old_snapshots());
    CHECK_FALSE(std::filesystem::exists(dir.path / "sync" / "snapshots"));
    CHECK_FALSE(store.remove_old_snapshots());
}

// A snapshot in parts: where the parts are cut, and what they hold.

namespace {

using seventhslide::db::HeldChange;
using seventhslide::db::k_largest_record;
using seventhslide::db::MediaDigest;
using seventhslide::db::SyncVersion;
using seventhslide::sync::k_snapshot_part_bytes;
using seventhslide::sync::k_snapshot_part_most;
using seventhslide::sync::PartWriter;
using seventhslide::sync::SnapshotIndex;
using seventhslide::sync::SnapshotPartRef;
using seventhslide::sync::SnapshotRecord;

constexpr std::int64_t k_mib = 1024 * 1024;

/// A record of about ``bytes`` bytes, named by ``n``, its text made of
/// ``fill``.
SnapshotRecord record(const std::string& table, int n, std::int64_t bytes, char fill = 'a')
{
    return {table, sha256_hex(table + std::to_string(n)).substr(0, 32),
            nlohmann::json{{"title", table + " " + std::to_string(n)},
                           {"text", std::string(static_cast<std::size_t>(bytes), fill)}},
            {{"title", SyncVersion{n + 1, "church"}}}};
}

/// Sizes from ``low`` to ``high``, the same ones each run.
class Sizes {
public:
    Sizes(std::int64_t low, std::int64_t high) : low_(low), high_(high) {}
    std::int64_t next()
    {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return low_ + static_cast<std::int64_t>((state_ >> 33) % static_cast<std::uint64_t>(high_ - low_ + 1));
    }

private:
    std::int64_t low_;
    std::int64_t high_;
    std::uint64_t state_ = 7;
};

/// Records in the order a snapshot holds them: by table, then key.
void sort(std::vector<SnapshotRecord>& records)
{
    const auto tables = seventhslide::db::record_tables_in_order();
    const auto rank = [&](const std::string& t) { return std::ranges::find(tables, t) - tables.begin(); };
    std::ranges::sort(records, [&](const SnapshotRecord& a, const SnapshotRecord& b) {
        return std::pair(rank(a.table), a.key) < std::pair(rank(b.table), b.key);
    });
}

/// ``count`` songs of 1 to 8 KB.
std::vector<SnapshotRecord> songs(int count)
{
    Sizes sizes(1000, 8000);
    std::vector<SnapshotRecord> out;
    for (int n = 0; n < count; ++n) {
        out.push_back(record("songs", n, sizes.next()));
    }
    sort(out);
    return out;
}

struct Written {
    std::vector<std::pair<SnapshotPartRef, std::string>> parts;
    SnapshotIndex index;

    [[nodiscard]] std::set<std::string> names() const
    {
        std::set<std::string> out;
        for (const auto& [ref, bytes] : parts) {
            out.insert(ref.sha256);
        }
        return out;
    }
};

Written write(const std::vector<SnapshotRecord>& records, const std::vector<HeldChange>& held = {},
              const std::vector<MediaDigest>& media = {})
{
    Written out;
    PartWriter writer([&](const SnapshotPartRef& ref, std::string_view bytes) {
        out.parts.emplace_back(ref, std::string(bytes));
    });
    for (const auto& r : records) {
        writer.add(r);
    }
    for (const auto& h : held) {
        writer.add(h);
    }
    for (const auto& m : media) {
        writer.add(m);
    }
    out.index = writer.finish();
    return out;
}

/// How many of ``after``'s parts ``before`` does not have.
std::size_t new_parts(const Written& before, const Written& after)
{
    const auto had = before.names();
    return static_cast<std::size_t>(std::ranges::count_if(
        after.parts, [&](const auto& p) { return !had.contains(p.first.sha256); }));
}

/// Each part's entries, all kinds together.
std::size_t entries(const seventhslide::sync::SnapshotPart& part)
{
    return part.records.size() + part.held.size() + part.media.size();
}

/// A part with nothing in it: what a part holds beside its entries.
constexpr std::string_view k_empty_part = R"({"format":3,"records":[],"held":[],"media":[]})";

/// The bytes ``r`` takes in a part, as an entry.
std::int64_t entry_bytes(const SnapshotRecord& r)
{
    const auto one = write({r});
    REQUIRE(one.parts.size() == 1);
    return one.parts[0].first.size - static_cast<std::int64_t>(k_empty_part.size());
}

/// ``table``'s record ``n`` (``record``) whose entry is ``bytes`` long: its
/// text, of one byte a character, makes up the rest.
SnapshotRecord record_of_entry(const std::string& table, int n, std::int64_t bytes)
{
    return record(table, n, bytes - entry_bytes(record(table, n, 0)));
}

}  // namespace

TEST_CASE("A snapshot's parts hold its entries in order, and read back", "[sync][parts]")
{
    auto records = songs(3000);
    records.push_back(record("bibles", 1, 6 * k_mib));
    records.push_back(record("app_settings", 1, 20));
    sort(records);
    const std::vector<HeldChange> held{
        {"service_items", "b1", "laptop", 3, nlohmann::json{{"op", "put"}}},
        {"service_items", "a1", "laptop", 4, nlohmann::json{{"op", "put"}}},
        {"songs", "c1", "office", 1, nlohmann::json{{"op", "delete"}}},
    };
    const std::vector<MediaDigest> media{
        {"images/a.png", 10, std::string(64, 'a'), SyncVersion{3, "church"}},
        {"images/b.png", 20, std::string(64, 'b'), SyncVersion{2, "laptop"}},
    };
    const auto written = write(records, held, media);

    CHECK(written.index.records == static_cast<std::int64_t>(records.size()));
    CHECK(written.index.held == 3);
    CHECK(written.index.media == 2);
    REQUIRE(written.index.parts.size() == written.parts.size());
    std::vector<SnapshotRecord> read_records;
    std::vector<HeldChange> read_held;
    std::vector<MediaDigest> read_media;
    for (std::size_t i = 0; i < written.parts.size(); ++i) {
        const auto& [ref, bytes] = written.parts[i];
        CHECK(written.index.parts[i] == ref);
        CHECK(ref.sha256 == sha256_hex(bytes));
        CHECK(ref.size == static_cast<std::int64_t>(bytes.size()));
        auto part = seventhslide::sync::read_snapshot_part(bytes, ref.sha256);
        std::ranges::move(part.records, std::back_inserter(read_records));
        std::ranges::move(part.held, std::back_inserter(read_held));
        std::ranges::move(part.media, std::back_inserter(read_media));
    }
    REQUIRE(read_records.size() == records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        CHECK(read_records[i].table == records[i].table);
        CHECK(read_records[i].key == records[i].key);
        CHECK(read_records[i].document == records[i].document);
        CHECK(read_records[i].versions == records[i].versions);
    }
    REQUIRE(read_held.size() == 3);
    CHECK(read_held[0].machine == "laptop");
    CHECK(read_held[0].sequence == 3);
    CHECK(read_held[2].machine == "office");
    CHECK(read_media == media);

    // A record deleted reads back as one.
    auto deleted = record("songs", 1, 0);
    deleted.document.reset();
    const auto one = write({deleted});
    REQUIRE(one.parts.size() == 1);
    const auto part = seventhslide::sync::read_snapshot_part(one.parts[0].second, one.parts[0].first.sha256);
    REQUIRE(part.records.size() == 1);
    CHECK_FALSE(part.records[0].document.has_value());

    // Nothing at all is no parts.
    CHECK(write({}).parts.empty());
}

TEST_CASE("A record in a part is its JSON as dumped", "[sync][parts]")
{
    // Parts are named by their bytes: any change to them makes every part
    // new to the computers that had them.
    auto song = record("songs", 1, 30);
    song.versions.emplace("lyrics", SyncVersion{4, "laptop"});
    song.key = "a\"b";
    auto deleted = record("songs", 2, 0);
    deleted.key = "b";
    deleted.document.reset();
    deleted.versions = {{"", SyncVersion{9, "church"}}};
    const auto as_json = [](const SnapshotRecord& r) {
        nlohmann::json j{{"table", r.table}, {"key", r.key},
                         {"versions", seventhslide::db::versions_to_json(r.versions)}};
        if (r.document) {
            j["document"] = *r.document;
        } else {
            j["deleted"] = true;
        }
        return j.dump();
    };
    const auto one = write({song, deleted});
    REQUIRE(one.parts.size() == 1);
    CHECK(one.parts[0].second
          == R"({"format":3,"records":[)" + as_json(song) + "," + as_json(deleted) + R"(],"held":[],"media":[]})");
}

TEST_CASE("The same entries make the same parts on any computer", "[sync][parts]")
{
    auto records = songs(4000);
    records.push_back(record("bibles", 1, 5 * k_mib));
    sort(records);
    const auto a = write(records);
    const auto b = write(records);
    REQUIRE(a.parts.size() == b.parts.size());
    for (std::size_t i = 0; i < a.parts.size(); ++i) {
        CHECK(a.parts[i] == b.parts[i]);
    }
    CHECK(a.index == b.index);
}

TEST_CASE("No part passes its limits, and a Bible is a part of its own", "[sync][parts]")
{
    // Songs of every size, a Bible just under the target, and some at it or
    // over.
    Sizes sizes(10, 900 * 1024);
    std::vector<SnapshotRecord> records;
    for (int n = 0; n < 150; ++n) {
        records.push_back(record("songs", n, sizes.next()));
    }
    records.push_back(record("bibles", 0, k_snapshot_part_bytes - 300));
    records.push_back(record("bibles", 1, k_snapshot_part_bytes));
    records.push_back(record("bibles", 2, k_snapshot_part_bytes + 100));
    records.push_back(record("bibles", 3, 9 * k_mib));
    sort(records);
    const auto written = write(records);
    int alone = 0;
    for (const auto& [ref, bytes] : written.parts) {
        const auto part = seventhslide::sync::read_snapshot_part(bytes, ref.sha256);
        const bool large = std::ranges::any_of(part.records, [](const SnapshotRecord& r) {
            return r.document->at("text").get<std::string>().size()
                   >= static_cast<std::size_t>(k_snapshot_part_bytes);
        });
        if (large) {
            CHECK(entries(part) == 1);
            ++alone;
        } else {
            CHECK(ref.size <= k_snapshot_part_most);
        }
    }
    CHECK(alone == 3);
}

TEST_CASE("An entry just under the target ends its part nearly always, whatever the target", "[sync][parts]")
{
    // One of ``b`` bytes ends its part one time in ``target / b``: for one a
    // byte short, all but one time in ``target``. A target of 2^12 + 1 and an
    // entry of 2^12 bytes once took that share as 2^64, which is 0.
    constexpr std::int64_t target = 4096 + 1;
    std::vector<int> order(40);
    std::iota(order.begin(), order.end(), 0);
    std::ranges::sort(order, {}, [](int n) { return record("songs", n, 0).key; });
    // Each large one with a small one after it, which starts a part.
    std::vector<SnapshotRecord> records;
    for (std::size_t i = 0; i < order.size(); ++i) {
        records.push_back(record_of_entry("songs", order[i], i % 2 == 0 ? target - 1 : 200));
    }
    REQUIRE(entry_bytes(records.front()) == target - 1);
    std::vector<std::string> parts;
    PartWriter writer([&](const SnapshotPartRef&, std::string_view bytes) { parts.emplace_back(bytes); }, target);
    for (const auto& r : records) {
        writer.add(r);
    }
    (void)writer.finish();
    std::size_t large_last = 0;
    for (const auto& bytes : parts) {
        const auto part = seventhslide::sync::read_snapshot_part(bytes, sha256_hex(bytes));
        REQUIRE_FALSE(part.records.empty());
        if (part.records.back().document->at("text").get<std::string>().size() > 1000) {
            ++large_last;
        }
    }
    CHECK(large_last == records.size() / 2);
}

TEST_CASE("A record too large to sync is refused", "[sync][parts]")
{
    std::vector<std::int64_t> sizes;
    PartWriter writer([&](const SnapshotPartRef& ref, std::string_view) { sizes.push_back(ref.size); });
    // One as large as sending lets go (its document at the limit) is taken,
    // in a part that may pass between computers.
    const auto largest = record("bibles", 1, static_cast<std::int64_t>(k_largest_record) - 100);
    REQUIRE(largest.document->dump().size() <= k_largest_record);
    auto larger = record("bibles", 2, static_cast<std::int64_t>(k_largest_record) + 2 * k_mib);
    larger.key = largest.key + "b";
    writer.add(largest);
    REQUIRE(sizes.size() == 1);
    CHECK(sizes.front() > static_cast<std::int64_t>(k_largest_record));
    CHECK(sizes.front() <= seventhslide::sync::k_largest_store_file);
    CHECK_THROWS_AS(writer.add(larger), std::runtime_error);

    // An entry of the record and what comes with it is taken up to the
    // byte, and refused one past.
    constexpr auto most = static_cast<std::int64_t>(k_largest_record + seventhslide::db::k_record_allowance);
    const auto at_most = record_of_entry("bibles", 3, most);
    const auto past = record_of_entry("bibles", 3, most + 1);
    REQUIRE(entry_bytes(at_most) == most);
    PartWriter taking([](const SnapshotPartRef& ref, std::string_view) {
        CHECK(ref.size <= seventhslide::sync::k_largest_store_file);
    });
    CHECK_NOTHROW(taking.add(at_most));
    PartWriter refusing([](const SnapshotPartRef&, std::string_view) {});
    CHECK_THROWS_AS(refusing.add(past), seventhslide::sync::EntryTooLarge);
}

TEST_CASE("Entries out of order or given twice are refused", "[sync][parts]")
{
    const auto a = record("services", 1, 10);
    auto b = a;
    b.key = a.key + "b";
    const HeldChange held{"songs", "k", "laptop", 2, nlohmann::json::object()};
    const MediaDigest media{"images/a.png", 10, std::string(64, 'a'), SyncVersion{3, "church"}};
    PartWriter writer([](const SnapshotPartRef&, std::string_view) {});
    writer.add(a);
    CHECK_THROWS_AS(writer.add(a), std::invalid_argument);
    writer.add(b);
    CHECK_THROWS_AS(writer.add(a), std::invalid_argument);
    // A table that comes before.
    CHECK_THROWS_AS(writer.add(record("songs", 1, 10)), std::invalid_argument);
    // A table that is not one, or does not sync by record.
    CHECK_THROWS_AS(writer.add(record("verses", 1, 10)), std::invalid_argument);
    CHECK_THROWS_AS(writer.add(record("sync_shared", 1, 10)), std::invalid_argument);
    writer.add(held);
    CHECK_THROWS_AS(writer.add(record("web_pages", 9, 10)), std::invalid_argument);
    CHECK_THROWS_AS(writer.add(HeldChange{"songs", "k", "laptop", 1, nlohmann::json::object()}),
                    std::invalid_argument);
    auto unversioned = media;
    unversioned.version.reset();
    CHECK_THROWS_AS(writer.add(unversioned), std::invalid_argument);
    writer.add(media);
    CHECK_THROWS_AS(writer.add(media), std::invalid_argument);
    CHECK_THROWS_AS(writer.add(held), std::invalid_argument);
    const auto index = writer.finish();
    CHECK(index.records == 2);
    CHECK(index.held == 1);
    CHECK(index.media == 1);
}

TEST_CASE("An edit changes the one part it is in", "[sync][parts]")
{
    auto records = songs(8000);
    const auto before = write(records);
    REQUIRE(before.parts.size() >= 4);
    for (const std::size_t at : {std::size_t{0}, std::size_t{1234}, std::size_t{4000}, std::size_t{7999}}) {
        INFO("song " << at);
        auto edited = records;
        auto& text = edited[at].document->at("text");
        text = text.get<std::string>() + " amen";
        const auto after = write(edited);
        CHECK(after.parts.size() == before.parts.size());
        CHECK(new_parts(before, after) == 1);
    }
}

TEST_CASE("An insert changes the part it falls in, sometimes two", "[sync][parts]")
{
    auto records = songs(5000);
    const auto before = write(records);
    std::map<std::size_t, int> changed;
    Sizes sizes(1000, 8000);
    for (int n = 0; n < 20; ++n) {
        auto added = records;
        added.push_back(record("songs", 100'000 + n, sizes.next()));
        sort(added);
        changed[new_parts(before, write(added))] += 1;
    }
    INFO("parts changed by each insert, and how often");
    for (const auto& [parts, times] : changed) {
        UNSCOPED_INFO(parts << ": " << times);
    }
    CHECK(changed.begin()->first >= 1);
    CHECK(changed.rbegin()->first <= 2);
    CHECK(changed[1] > changed[2]);
}

TEST_CASE("Parts are of the target size on average, with songs and Bibles", "[sync][parts]")
{
    auto records = songs(20'000);
    Sizes bibles(4 * k_mib, 9 * k_mib);
    for (int n = 0; n < 6; ++n) {
        records.push_back(record("bibles", n, bibles.next()));
    }
    Sizes small(50, 2000);
    for (int n = 0; n < 300; ++n) {
        records.push_back(record("services", n, small.next()));
        records.push_back(record("service_items", n, small.next() / 4));
    }
    for (int n = 0; n < 40; ++n) {
        records.push_back(record("app_settings", n, 30));
    }
    sort(records);
    const auto written = write(records);

    std::int64_t total = 0;
    std::int64_t smallest = k_snapshot_part_most;
    std::int64_t largest = 0;
    std::int64_t shared_total = 0;
    std::int64_t shared_count = 0;
    for (const auto& [ref, bytes] : written.parts) {
        total += ref.size;
        smallest = std::min(smallest, ref.size);
        largest = std::max(largest, ref.size);
        const auto part = seventhslide::sync::read_snapshot_part(bytes, ref.sha256);
        if (entries(part) > 1) {
            shared_total += ref.size;
            ++shared_count;
        }
    }
    const auto count = static_cast<std::int64_t>(written.parts.size());
    INFO(count << " parts of " << total / k_mib << " MiB; " << shared_count << " of several records, "
               << shared_total / shared_count / 1024 << " KiB on average; from " << smallest / 1024 << " to "
               << largest / 1024 << " KiB");
    CHECK(total / count > 3 * k_mib);
    CHECK(total / count < 6 * k_mib);
    CHECK(shared_total / shared_count > 2 * k_mib);
    CHECK(shared_total / shared_count < 6 * k_mib);
}

TEST_CASE("A part that is not the one named, or not a part, is refused", "[sync][parts]")
{
    using seventhslide::sync::read_snapshot_part;
    const auto read = [](const std::string& text) { return read_snapshot_part(text, sha256_hex(text)); };
    const auto written = write(songs(3));
    REQUIRE(written.parts.size() == 1);
    const auto& [ref, bytes] = written.parts[0];
    CHECK_NOTHROW(read_snapshot_part(bytes, ref.sha256));
    CHECK_THROWS_AS(read_snapshot_part(bytes + " ", ref.sha256), std::runtime_error);
    CHECK_THROWS_AS(read_snapshot_part(bytes, sha256_hex("another")), std::runtime_error);

    CHECK_NOTHROW(read(R"({"format":3,"records":[],"held":[],"media":[]})"));
    CHECK_THROWS_AS(read(R"({"format":4,"records":[],"held":[],"media":[]})"),
                    seventhslide::sync::NewerFormat);
    CHECK_THROWS_AS(read(R"({"format":2,"records":[],"held":[],"media":[]})"), std::runtime_error);
    CHECK_THROWS_AS(read(R"({"format":3,"records":[],"held":[]})"), std::runtime_error);
    CHECK_THROWS_AS(read("not JSON"), std::runtime_error);

    const std::string song_a = R"({"table":"songs","key":"a","document":{},"versions":{}})";
    const std::string song_b = R"({"table":"songs","key":"b","document":{},"versions":{}})";
    const std::string service = R"({"table":"services","key":"a","document":{},"versions":{}})";
    const auto part = [](const std::string& records, const std::string& held, const std::string& media) {
        return R"({"format":3,"records":[)" + records + R"(],"held":[)" + held + R"(],"media":[)" + media
               + "]}";
    };
    CHECK_NOTHROW(read(part(song_a + "," + song_b + "," + service, "", "")));
    CHECK_THROWS_AS(read(part(song_b + "," + song_a, "", "")), std::runtime_error);
    CHECK_THROWS_AS(read(part(song_a + "," + song_a, "", "")), std::runtime_error);
    CHECK_THROWS_AS(read(part(service + "," + song_b, "", "")), std::runtime_error);
    CHECK_THROWS_AS(read(part(R"({"table":"verses","key":"a","document":{},"versions":{}})", "", "")),
                    std::runtime_error);

    const std::string held_1 = R"({"table":"songs","key":"a","machine":"laptop","sequence":1,"change":{}})";
    const std::string held_2 = R"({"table":"songs","key":"a","machine":"laptop","sequence":2,"change":{}})";
    CHECK_NOTHROW(read(part("", held_1 + "," + held_2, "")));
    CHECK_THROWS_AS(read(part("", held_2 + "," + held_1, "")), std::runtime_error);
    CHECK_THROWS_AS(read(part("", held_1 + "," + held_1, "")), std::runtime_error);

    const std::string root(64, 'a');
    const auto file = [&](const std::string& path) {
        return R"({"path":")" + path + R"(","size":1,"root":")" + root + R"(","version":[1,"church"]})";
    };
    CHECK_NOTHROW(read(part("", "", file("images/a.png") + "," + file("images/b.png"))));
    CHECK_THROWS_AS(read(part("", "", file("images/b.png") + "," + file("images/a.png"))), std::runtime_error);
    CHECK_THROWS_AS(read(part("", "", R"({"path":"images/a.png","size":1,"root":")" + root + R"("})")),
                    std::runtime_error);
}

TEST_CASE("Parts are read as their index names them: in order across them, each entry once, as many as it says",
          "[sync][parts]")
{
    using seventhslide::sync::PartReader;
    // Several parts, each with records of one part and the next.
    const auto records = songs(40);
    Written written;
    PartWriter writer([&](const SnapshotPartRef& ref, std::string_view bytes) {
        written.parts.emplace_back(ref, std::string(bytes));
    }, 16 * 1024);
    for (const auto& r : records) {
        writer.add(r);
    }
    written.index = writer.finish();
    REQUIRE(written.parts.size() >= 3);
    const auto read_all = [&](const SnapshotIndex& index) {
        PartReader reader(index);
        for (const auto& ref : index.parts) {
            const auto it = std::ranges::find(written.parts, ref.sha256, [](const auto& p) { return p.first.sha256; });
            REQUIRE(it != written.parts.end());
            (void)reader.next(it->second);
        }
        reader.finish();
    };
    CHECK_NOTHROW(read_all(written.index));

    SECTION("parts out of order across a boundary")
    {
        auto index = written.index;
        std::swap(index.parts[1], index.parts[2]);
        PartReader reader(index);
        CHECK_NOTHROW((void)reader.next(written.parts[0].second));
        CHECK_NOTHROW((void)reader.next(written.parts[2].second));
        CHECK_THROWS_AS((void)reader.next(written.parts[1].second), std::runtime_error);
    }
    SECTION("a record in two parts")
    {
        // The last record of the first part begins the next one too.
        const auto first = seventhslide::sync::read_snapshot_part(written.parts[0].second,
                                                                   written.parts[0].first.sha256);
        const auto again = write({first.records.back(), records.back()});
        auto index = written.index;
        index.parts = {written.parts[0].first, again.parts.at(0).first};
        index.records = static_cast<std::int64_t>(first.records.size()) + 2;
        PartReader reader(index);
        CHECK_NOTHROW((void)reader.next(written.parts[0].second));
        CHECK_THROWS_AS((void)reader.next(again.parts[0].second), std::runtime_error);
    }
    SECTION("counts that are not the index's")
    {
        for (auto count : {&SnapshotIndex::records, &SnapshotIndex::held, &SnapshotIndex::media}) {
            auto index = written.index;
            index.*count += 1;
            CHECK_THROWS_AS(read_all(index), std::runtime_error);
        }
        auto index = written.index;
        index.records -= 1;
        CHECK_THROWS_AS(read_all(index), std::runtime_error);
    }
    SECTION("fewer parts read than the index names, or more")
    {
        PartReader reader(written.index);
        (void)reader.next(written.parts[0].second);
        CHECK_THROWS_AS(reader.finish(), std::runtime_error);
        auto index = written.index;
        index.parts.pop_back();
        index.records = 0;
        PartReader past(index);
        for (std::size_t i = 0; i < index.parts.size(); ++i) {
            (void)past.next(written.parts[i].second);
        }
        CHECK_THROWS_AS((void)past.next(written.parts.back().second), std::runtime_error);
    }
    SECTION("a part that is not the one named")
    {
        PartReader reader(written.index);
        CHECK_THROWS_AS((void)reader.next(written.parts[1].second), std::runtime_error);
    }
}

TEST_CASE("A snapshot's index reads back, and any later one's parts are known", "[sync][parts]")
{
    using seventhslide::sync::snapshot_index_from_json;
    using seventhslide::sync::snapshot_index_label;
    using seventhslide::sync::snapshot_index_to_json;
    SnapshotIndex index;
    index.library = std::string(32, 'c');
    index.machine = "church";
    index.computer = "Church PC";
    index.time = "2026-10-24T20:15:00Z";
    index.counter = 240;
    index.files = {{"church", 31}, {"laptop", 4}};
    index.parts = {{std::string(64, 'a'), 4190211}, {std::string(64, 'b'), 12}};
    index.records = 8123;
    index.held = 2;
    index.media = 1450;
    const auto j = snapshot_index_to_json(index);
    CHECK(j.at("format") == 3);
    CHECK(snapshot_index_from_json(j) == index);
    CHECK(index.file_count() == 35);

    const auto label = snapshot_index_label(j);
    CHECK(label.parts == index.parts);
    CHECK(label.files == index.files);

    // A later format: refused, but its label is read.
    auto later = j;
    later["format"] = 4;
    later["parts"][0]["pieces"] = 3;
    later.erase("records");
    CHECK_THROWS_AS(snapshot_index_from_json(later), seventhslide::sync::NewerFormat);
    CHECK(snapshot_index_label(later).parts == index.parts);
    auto newer_schema = j;
    newer_schema["schema"] = seventhslide::db::k_schema_version + 1;
    CHECK_THROWS_AS(snapshot_index_from_json(newer_schema), seventhslide::sync::NewerFormat);

    // Not an index.
    auto twice = j;
    twice["parts"][1] = twice["parts"][0];
    CHECK_THROWS_AS(snapshot_index_from_json(twice), std::runtime_error);
    auto bad_name = j;
    bad_name["parts"][0]["sha256"] = "../changes";
    CHECK_THROWS_AS(snapshot_index_from_json(bad_name), std::runtime_error);
    CHECK_THROWS_AS(snapshot_index_label(bad_name), std::runtime_error);
    auto empty_part = j;
    empty_part["parts"][0]["size"] = 0;
    CHECK_THROWS_AS(snapshot_index_from_json(empty_part), std::runtime_error);
    auto format_2 = j;
    format_2["format"] = 2;
    CHECK_THROWS_AS(snapshot_index_label(format_2), std::runtime_error);
    auto no_count = j;
    no_count.erase("media");
    CHECK_THROWS_AS(snapshot_index_from_json(no_count), std::runtime_error);
}
