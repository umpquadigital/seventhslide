#pragma once

// A library with one of everything that syncs, every reference set, for the
// sync tests; a check that two libraries hold the same records; the media
// files a library misses; and a snapshot written or read whole.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/sync_bookkeeping.hpp>
#include <seventhslide/db/sync_records.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/sync/envelope.hpp>
#include <seventhslide/crypto/signing.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/sync/snapshot.hpp>
#include <seventhslide/sync/sync_engine.hpp>
#include <seventhslide/sync/sync_store.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::test {

/// One of everything, every reference set.
inline void fill_library(db::Database& db)
{
    db.exec("INSERT INTO songs (id, title, lyrics, key) VALUES (1, 'Amazing Grace', '[V1]\nline', 'G')");
    db.exec("INSERT INTO songs (id, title, lyrics) VALUES (2, 'It Is Well', '[V1]\nline')");
    db.exec("INSERT INTO bibles (id, name, abbreviation) VALUES (1, 'King James', 'KJV')");
    db.exec("INSERT INTO verses (bible_id, book, chapter, verse_num, text) VALUES "
            "(1, 'John', 3, 16, 'For God so loved'), (1, 'Genesis', 1, 1, 'In the beginning')");
    db.exec(R"(INSERT INTO style_profiles (id, name, data) VALUES (1, 'Default',
            '{"songs":{"1":{"main":"t1"},"99":{"main":"gone"}},"song_verse_orders":{"2":"V1 C"}}'))");
    db::ConfigRepository config(db);
    config.set_setting("active_profile_id", 1);
    config.set_setting("songs_sort", "title");
    db.exec("INSERT INTO image_folders (id, name) VALUES (1, 'Backgrounds')");
    db.exec("INSERT INTO image_folders (id, name, parent_id) VALUES (2, 'Easter', 1)");
    db.exec("INSERT INTO video_folders (id, name) VALUES (1, 'Intros')");
    db.exec("INSERT INTO audio_folders (id, name) VALUES (1, 'Preludes')");
    db.exec("INSERT INTO freeform_folders (id, name) VALUES (1, 'Sermons')");
    db.exec("INSERT INTO services (id, name, tags) VALUES (1, 'Sabbath', '[\"Easter\"]')");
    db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
            "VALUES (1, 1, 0, 'song')");
    db.exec(R"(INSERT INTO service_items (service_id, order_num, item_type, data)
            VALUES (1, 1, 'bible', '{"bible_id":1,"book":"John","chapter":3}'))");
    db.exec(R"(INSERT INTO service_items (service_id, order_num, item_type, data)
            VALUES (1, 2, 'image_folder', '{"folder_id":2,"folder_name":"Easter","also_play":[1]}'))");
    db.exec("INSERT INTO freeform_items (name, folder_id, document) "
            "VALUES ('Sermon slides', 1, '{\"schema_version\":1}')");
    db.exec("INSERT INTO web_folders (id, name) VALUES (1, 'Screens')");
    db.exec("INSERT INTO web_pages (name, url, folder_id) "
            "VALUES ('Countdown', 'https://example.com/countdown', 1)");
    db.exec("INSERT INTO image_files (filename, display_name) VALUES ('ab12.png', 'Lilies')");
    db.exec("INSERT INTO video_files (filename, display_name) VALUES ('cd34.mp4', 'Welcome')");
    db.exec("INSERT INTO audio_files (filename, display_name) VALUES ('ef56.mp3', 'Prelude')");
    db.exec("INSERT INTO image_folder_items (folder_id, filename, sort_order) VALUES (2, 'ab12.png', 3)");
    db.exec("INSERT INTO video_folder_items (folder_id, filename) VALUES (1, 'cd34.mp4')");
    db.exec("INSERT INTO audio_folder_items (folder_id, filename) VALUES (1, 'ef56.mp3')");
    model::OutputConfig main;
    main.id = "main";
    main.name = "Main";
    model::OutputConfig foldback;
    foldback.id = "foldback";
    foldback.name = "Foldback";
    config.save_outputs({main, foldback});
}

/// ``a`` and ``b`` hold the same records, each the same.
inline void check_same_records(db::Database& a, db::Database& b)
{
    for (const auto table : db::record_tables_in_order()) {
        INFO(table);
        auto keys = db::record_keys(a, table);
        auto others = db::record_keys(b, table);
        std::ranges::sort(keys);
        std::ranges::sort(others);
        CHECK(keys == others);
        for (const auto& key : keys) {
            INFO(key);
            CHECK(db::read_record(a, table, key) == db::read_record(b, table, key));
        }
    }
}

/// The media files records in ``engine``'s library own that are not on the
/// disk (``SyncEngine::missing_media``).
inline std::vector<db::MediaDigest> missing_media(sync::SyncEngine& engine)
{
    return engine.missing_media(engine.media_use());
}

/// A snapshot put together whole: its index's label and every part's
/// entries, in order.
struct Snapshot {
    int format = sync::k_snapshot_format;
    int schema = db::k_schema_version;
    std::string library;
    std::string machine;
    std::string computer;
    std::string time;
    std::int64_t counter = 0;
    std::map<std::string, std::int64_t> files;
    std::vector<sync::SnapshotRecord> records;
    std::vector<db::HeldChange> held;
    std::vector<db::MediaDigest> media;
};

/// Write ``snapshot``, its entries in order, to ``store`` as ``name``: its
/// parts (of about ``part_bytes``), then its index, signed with ``key``.
/// What it was written as.
inline sync::SnapshotIndex write_snapshot(sync::SyncStore& store, const sync::SnapshotName& name,
                                          const Snapshot& snapshot, const crypto::SigningKey& key,
                                          std::int64_t part_bytes = sync::k_snapshot_part_bytes)
{
    sync::PartWriter parts(
        [&](const sync::SnapshotPartRef& part, std::string_view bytes) { store.write_part(part.sha256, bytes); },
        part_bytes);
    for (const auto& r : snapshot.records) {
        parts.add(r);
    }
    for (const auto& h : snapshot.held) {
        parts.add(h);
    }
    for (const auto& m : snapshot.media) {
        parts.add(m);
    }
    auto index = parts.finish();
    index.format = snapshot.format;
    index.schema = snapshot.schema;
    index.library = snapshot.library;
    index.machine = snapshot.machine;
    index.computer = snapshot.computer;
    index.time = snapshot.time;
    index.counter = snapshot.counter;
    index.files = snapshot.files;
    store.write_snapshot(name, sync::seal(sync::snapshot_index_to_json(index), {{sync::k_signed_as_computer, &key}}));
    return index;
}

/// The index of snapshot ``name`` in ``store``.
inline sync::SnapshotIndex read_index(sync::SyncStore& store, const sync::SnapshotName& name)
{
    return sync::snapshot_index_from_json(sync::unseal(store.read_snapshot(name), "A test").body);
}

/// Snapshot ``name`` in ``store``, its parts put together.
inline Snapshot read_snapshot(sync::SyncStore& store, const sync::SnapshotName& name)
{
    const auto index = read_index(store, name);
    Snapshot out{index.format, index.schema, index.library, index.machine, index.computer,
                       index.time,   index.counter, index.files, {}, {}, {}};
    sync::PartReader reader(index);
    for (const auto& ref : index.parts) {
        auto part = reader.next(store.read_part(ref.sha256));
        std::ranges::move(part.records, std::back_inserter(out.records));
        std::ranges::move(part.held, std::back_inserter(out.held));
        std::ranges::move(part.media, std::back_inserter(out.media));
    }
    reader.finish();
    return out;
}

}  // namespace seventhslide::test
