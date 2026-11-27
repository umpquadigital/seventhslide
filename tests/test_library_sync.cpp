// One synced library on its own thread (library_sync.hpp): an edit made in
// the window sent a moment after it settles, a pause, and changes from
// another computer reported by table.
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/query.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/library/library.hpp>
#include <seventhslide/link/library_sync.hpp>
#include <seventhslide/link/sync_node.hpp>
#include <seventhslide/link/sync_turns.hpp>
#include <seventhslide/sync/envelope.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/sync/snapshot.hpp>
#include <seventhslide/sync/sync_engine.hpp>

#include "link_test_helpers.hpp"
#include "sync_test_helpers.hpp"
#include "test_temp_dir.hpp"
#include "test_wait.hpp"

#include <QElapsedTimer>
#include <QScopeGuard>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

using seventhslide::link::LibrarySync;
using namespace seventhslide::test::link_fixture;
using seventhslide::test::wait_until;

namespace {

LibrarySync::Options quick()
{
    LibrarySync::Options o;
    o.box_params = k_quick;
    o.watch_edits = true;
    o.edit_settle_ms = 200;
    return o;
}

/// A library put online in a folder, with the window's own connection to it.
struct Online {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    std::filesystem::path root{dir.path / "library"};
    Identity me{"church", "Church PC", SigningKey::generate()};
    Database window;
    std::string uid;
    std::unique_ptr<LibrarySync> sync;

    Online()
        : window((std::filesystem::create_directories(root),
                  Database::open_file(seventhslide::library::database_file(root))))
    {
        seventhslide::db::ensure_schema(window);
        seventhslide::test::fill_library(window);
        uid = seventhslide::db::library_uid(window);
        FolderStore store(seventhslide::library::sync_root(root));
        MemberLog(store, me, uid).found(k_passphrase, k_quick);
        sync = std::make_unique<LibrarySync>(root, uid, me, nullptr, quick());
        sync->share_everything();
    }
    std::vector<std::int64_t> sent() { return sync->store().sequences(Numbered::Changes, "church"); }
};

}  // namespace

TEST_CASE("A library put online sends everything, then each edit made in the window", "[librarysync]")
{
    Online o;
    REQUIRE(wait_until([&] { return o.sent().size() == 1 && !o.sync->busy(); }));
    CHECK(o.sync->report().joined);
    CHECK_FALSE(o.sync->report().unsent);

    // An edit in the window, on its own connection: sent once it settles.
    o.window.exec("UPDATE songs SET key = 'D' WHERE title = 'Amazing Grace'");
    // Its file is in the store before that sync is over: an edit made before
    // the sync looks at what is unsent would be found by it.
    REQUIRE(wait_until([&] { return o.sent().size() == 2 && !o.sync->busy(); }));
    CHECK(o.sync->report().changes_sent >= 2);

    // Paused: the edit waits, and goes when the pause ends.
    o.sync->set_paused(true);
    o.window.exec("UPDATE songs SET key = 'E' WHERE title = 'Amazing Grace'");
    run_events_for(2500);
    CHECK(o.sent().size() == 2);
    CHECK(o.sync->report().unsent == false);  // not looked at while paused
    o.sync->set_paused(false);
    REQUIRE(wait_until([&] { return o.sent().size() == 3; }));
}

TEST_CASE("A record too large to sync is said once, is not left to send, and goes from the report once fixed",
          "[librarysync]")
{
    Online o;
    REQUIRE(wait_until([&] { return o.sent().size() == 1 && !o.sync->busy(); }));
    const LoggedWarnings logged;
    // Some 13 MB of lyrics: past the largest record.
    o.window.exec("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', "
                  "'[V1]\n' || substr(hex(randomblob(6600000)), 1, 13000000))");
    REQUIRE(wait_until([&] { return o.sync->report().too_large.size() == 1 && !o.sync->busy(); }));
    CHECK(o.sync->report().too_large.front().title == "Psalm 119");
    // Marked still, but nothing it can send.
    CHECK_FALSE(o.sync->report().unsent);

    // Another edit is sent; the record is not said again.
    o.window.exec("UPDATE songs SET key = 'D' WHERE title = 'Amazing Grace'");
    REQUIRE(wait_until([&] { return o.sent().size() == 2 && !o.sync->busy(); }));
    CHECK(o.sync->report().too_large.size() == 1);
    INFO(logged.all());
    CHECK(logged.count({"too large to sync", "Psalm 119"}) == 1);

    // Made smaller, it goes, and so does what the report says of it.
    o.window.exec("UPDATE songs SET lyrics = '[V1]\nGreat peace' WHERE title = 'Psalm 119'");
    REQUIRE(wait_until([&] { return o.sent().size() == 3 && !o.sync->busy(); }));
    CHECK(o.sync->report().too_large.empty());
    CHECK(logged.count({"too large to sync", "Psalm 119"}) == 1);
}

TEST_CASE("A library's sync clears the temp files a crash left in its store", "[librarysync]")
{
    Online church;
    church.sync.reset();
    const auto place = seventhslide::library::sync_root(church.root) / "log" / "church";
    std::filesystem::create_directories(place);
    const auto old_temp = place / "7.json.999999999-0.tmp";
    const auto new_temp = place / "8.json.999999999-1.tmp";
    std::ofstream(old_temp) << "x";
    std::ofstream(new_temp) << "x";
    std::filesystem::last_write_time(old_temp, std::filesystem::file_time_type::clock::now()
                                                   - std::chrono::hours(2));

    church.sync = std::make_unique<LibrarySync>(church.root, church.uid, church.me, nullptr, quick());
    REQUIRE(wait_until([&] { return church.sync->report().opened; }));
    CHECK_FALSE(std::filesystem::exists(old_temp));
    // One written moments ago may be a write still going on.
    CHECK(std::filesystem::exists(new_temp));
}

TEST_CASE("Changes from another computer are applied, and said by record", "[librarysync]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    church.db.exec("UPDATE services SET name = 'Divine Service'");
    REQUIRE(church.sync().changes_sent == 1);

    // The laptop's copy: its store holds what the link would have brought.
    seventhslide::test::TempDir dir;
    const auto root = dir.path / "laptop";
    Identity laptop{"laptop", "Laptop", SigningKey::generate()};
    auto sync = std::make_unique<LibrarySync>(root, church.uid, laptop, nullptr, quick());
    seventhslide::sync::Changed changed;
    QObject::connect(sync.get(), &LibrarySync::applied,
                     [&](const seventhslide::sync::Changed& c) { changed.add(c); });
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    MemberLog(sync->store(), laptop, church.uid).join(seventhslide::sync::read_invite(code));
    for (const auto kind : {Numbered::Members, Numbered::Changes}) {
        for (const auto& machine : church.store.machines(kind)) {
            for (const auto n : church.store.sequences(kind, machine)) {
                sync->store().write(kind, machine, n, church.store.read(kind, machine, n));
            }
        }
    }
    for (const auto& fp : church.store.admin_keys()) {
        sync->store().write_admin_key(fp, church.store.read_admin_key(fp));
    }
    sync->schedule_sync();
    REQUIRE(wait_until([&] { return sync->report().joined && !changed.empty(); }));
    // Each song by its uid, so a window reads again only those.
    std::set<std::string> songs;
    for (auto q = seventhslide::db::exec(church.db, QStringLiteral("SELECT uid FROM songs"), {}, "uids");
         q.next();) {
        songs.insert(q.value(0).toString().toStdString());
    }
    REQUIRE_FALSE(songs.empty());
    CHECK_FALSE(changed.whole("songs"));
    CHECK(changed.keys("songs") == songs);
    CHECK(changed.touches("services"));
    CHECK(sync->report().files_applied == 2);
    // Its media is not here yet: said, for the link to fetch.
    CHECK(sync->missing_media().size() == 3);
    CHECK(sync->report().arrived);
    // One that comes through the link is no longer missing.
    sync->media_arrived(sync->missing_media().front().path);
    CHECK(sync->missing_media().size() == 2);
    sync.reset();

    auto db = Database::open_file(seventhslide::library::database_file(root));
    seventhslide::test::check_same_records(db, church.db);
}

TEST_CASE("Joining waits while the library's files go on coming, and not once they stop", "[librarysync]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    seventhslide::test::TempDir dir;
    Identity laptop{"laptop", "Laptop", SigningKey::generate()};
    // The link is never done: a large snapshot still coming, or stalled.
    auto options = quick();
    options.join_wait_ms = 1000;
    auto sync = std::make_unique<LibrarySync>(dir.path / "laptop", church.uid, laptop, [] { return true; }, options);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    MemberLog(sync->store(), laptop, church.uid).join(seventhslide::sync::read_invite(code));
    for (const auto kind : {Numbered::Members, Numbered::Changes}) {
        for (const auto& machine : church.store.machines(kind)) {
            for (const auto n : church.store.sequences(kind, machine)) {
                sync->store().write(kind, machine, n, church.store.read(kind, machine, n));
            }
        }
    }
    for (const auto& fp : church.store.admin_keys()) {
        sync->store().write_admin_key(fp, church.store.read_admin_key(fp));
    }
    sync->schedule_sync();
    QElapsedTimer timer;
    timer.start();
    SECTION("files still coming")
    {
        // Something comes every few tenths of a second, for three times as
        // long as it would wait with nothing.
        qint64 last = 0;
        while (timer.elapsed() < 3000) {
            last = timer.elapsed();
            sync->files_coming();
            run_events_for(200);
            REQUIRE_FALSE(sync->report().joined);
        }
        // Then nothing: it joins once the wait is up.
        REQUIRE(wait_until([&] { return sync->report().joined; }));
        CHECK(timer.elapsed() >= last + options.join_wait_ms);
    }
    SECTION("nothing coming")
    {
        REQUIRE(wait_until([&] { return sync->report().joined; }));
        CHECK(timer.elapsed() >= options.join_wait_ms);
        CHECK(timer.elapsed() < 3000);
    }
    CHECK(sync->report().files_applied == static_cast<int>(church.store.sequences(Numbered::Changes, "church").size()));
}

TEST_CASE("A large join through the node keeps waiting while its snapshot's parts come", "[librarysync][hardening]")
{
    Computer church{"church", "Church PC"};
    // Some 18 MB of songs: several change files, several parts.
    church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 9000) "
                   "INSERT INTO songs (id, title, lyrics) SELECT 1000 + i, 'Hymn ' || i, "
                   "'[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
    const auto founded = put_online(church);
    REQUIRE(church.engine.snapshot());
    REQUIRE(church.store.parts().size() >= 3);

    seventhslide::test::TempDir dir;
    seventhslide::link::SyncNode::Options options;
    options.data = dir.path / "node";
    options.network = false;
    options.discovery = false;
    options.box_params = k_quick;
    options.join_wait_ms = 1500;
    std::filesystem::create_directories(options.data);
    seventhslide::link::SyncNode node(Identity{"laptop", "Laptop", SigningKey::generate()}, options);
    auto& lib = node.hold(church.uid, dir.path / "laptop");
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    MemberLog(lib.store(), node.identity(), church.uid).join(seventhslide::sync::read_invite(code));
    const auto own = seventhslide::sync::own_invite(lib.store(), church.uid, node.identity().machine);
    REQUIRE(own.has_value());

    // The church reads one file at a time, each a while after the last, so
    // its files take some seconds to come: a gate on its file thread, opened
    // for one task each turn.
    std::atomic<int> opened = 0;
    int gates = 0;
    const auto gate = [&] {
        const int k = ++gates;
        church.link.shared().files.start([&opened, k] {
            while (opened.load() < k) {
                QThread::msleep(5);
            }
        });
    };
    const auto release = qScopeGuard([&] {
        opened = 1 << 30;
        church.link.shared().files.waitForDone();
    });
    gate();
    node.link().connect_to(QStringLiteral("127.0.0.1"), church.port(), church.key(), {{church.uid, own->entry}});
    QElapsedTimer timer;
    timer.start();
    // Never joined while they come, though each turn is more than a quarter
    // of the wait, and all of them twice it or more.
    bool early = false;
    while (lib.store().snapshots() != church.store.snapshots() && timer.elapsed() < 60000) {
        run_events_for(400);
        early = early || lib.report().joined || !lib.report().problem.empty();
        gate();
        ++opened;
    }
    opened = 1 << 30;
    INFO("church: " << why(church));
    REQUIRE(lib.store().snapshots() == church.store.snapshots());
    CHECK(timer.elapsed() > 2 * options.join_wait_ms);
    CHECK_FALSE(early);
    // Then it joins from the snapshot: no change file is replayed.
    REQUIRE(wait_until([&] { return lib.report().joined; }, 20000));
    CHECK(lib.report().problem.empty());
    CHECK(lib.report().files_applied == 0);
}

TEST_CASE("Old files go once every member has said it has them, and what each said is kept; old snapshots go",
          "[librarysync][housekeeping]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const auto root = dir.path / "library";
    std::filesystem::create_directories(root);
    Identity me{"church", "Church PC", SigningKey::generate()};
    auto window = Database::open_file(seventhslide::library::database_file(root));
    seventhslide::db::ensure_schema(window);
    seventhslide::test::fill_library(window);
    const auto uid = seventhslide::db::library_uid(window);
    FolderStore store(seventhslide::library::sync_root(root));
    const auto founded = MemberLog(store, me, uid).found(k_passphrase, k_quick);
    // A laptop has joined; it has said nothing here yet.
    Identity laptop{"laptop", "Laptop", SigningKey::generate()};
    MemberLog(store, laptop, uid)
        .join(seventhslide::sync::read_invite(
            MemberLog(store, me, uid).invite(Role::Editor, k_expires, founded.admin)));

    // Snapshots of the format before parts, which nothing reads.
    const auto old_snapshots = seventhslide::library::sync_root(root) / "snapshots";
    std::filesystem::create_directories(old_snapshots / "church");
    std::ofstream(old_snapshots / "church" / "0000000002.json") << "{}";

    auto options = quick();
    options.prune_every_ms = 0;  // after every sync
    auto sync = std::make_unique<LibrarySync>(root, uid, me, nullptr, options);
    sync->share_everything();
    REQUIRE(wait_until([&] { return store.sequences(Numbered::Changes, "church").size() == 1 && !sync->busy(); }));
    // They go, once.
    CHECK_FALSE(std::filesystem::exists(old_snapshots));
    CHECK_FALSE(store.remove_old_snapshots());
    window.exec("UPDATE songs SET key = 'D' WHERE title = 'Amazing Grace'");
    REQUIRE(wait_until([&] { return store.sequences(Numbered::Changes, "church").size() == 2 && !sync->busy(); }));
    // A snapshot holds both (written here as any computer due one would).
    SyncEngine(window, store, root / "media", me).snapshot();

    // The laptop, never heard from, holds them.
    sync->sync_now();
    REQUIRE(wait_until([&] { return !sync->busy(); }));
    CHECK(store.sequences(Numbered::Changes, "church").size() == 2);
    // It says it has them: they go.
    sync->heard_progress("laptop", {{"church", 2}});
    sync->sync_now();
    REQUIRE(wait_until([&] { return store.sequences(Numbered::Changes, "church").empty(); }));
    CHECK(store.dropped_through("church") == 2);
    CHECK(sync->applied_files().at("church") == 2);

    // What it said is kept, for after a restart.
    sync.reset();
    const auto kept = nlohmann::json::parse(std::ifstream(seventhslide::library::sync_root(root) / "progress.json"));
    CHECK(kept["laptop"]["applied"]["church"] == 2);
}

namespace {

/// Each of ``kind``'s files ``from`` has that ``to`` lacks, of every computer
/// but ``except``'s, put in by no gate.
void copy_files(seventhslide::sync::SyncStore& from, seventhslide::sync::SyncStore& to, Numbered kind,
                const std::string& except = {})
{
    for (const auto& machine : from.machines(kind)) {
        if (machine == except) {
            continue;
        }
        const auto have = to.sequences(kind, machine);
        for (const auto n : from.sequences(kind, machine)) {
            if (std::ranges::find(have, n) == have.end()) {
                to.write(kind, machine, n, from.read(kind, machine, n));
            }
        }
    }
}

/// A join entry for ``who`` on an invite with no proof, as SeventhSlide made
/// them before an invite had to prove itself: today's rules refuse it.
std::string unproved_join(const Identity& who, const std::string& library, const SigningKey& admin,
                          const std::map<std::string, std::int64_t>& seen)
{
    seventhslide::sync::Invite bare;
    bare.library = library;
    bare.role = Role::Editor;
    bare.expires = k_expires;
    bare.id = "0123456701234567";
    bare.seen = seen;
    auto invite = bare.to_json();
    invite.erase("proof");
    const auto one_time = SigningKey::generate();
    const nlohmann::json body{
        {"format", 1},
        {"library", library},
        {"machine", who.machine},
        {"sequence", 1},
        {"seen", seen},
        {"time", "2026-11-02T00:15:44Z"},
        {"kind", "join"},
        {"computer",
         {{"machine", who.machine}, {"key", who.key.public_key().base64()}, {"name", who.computer}}},
        {"invite", seventhslide::sync::seal(invite, {{seventhslide::sync::k_signed_as_admin, &admin}})}};
    return seventhslide::sync::seal(body, {{seventhslide::sync::k_signed_as_computer, &who.key},
                                           {seventhslide::sync::k_signed_as_invite, &one_time}});
}

}  // namespace

TEST_CASE("A computer whose join today's rules refuse holds up no one: its files go in a snapshot "
          "at once, and a computer waiting on them says so",
          "[librarysync]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const auto root = dir.path / "church";
    std::filesystem::create_directories(root);
    Identity me{"church", "Church PC", SigningKey::generate()};
    auto window = Database::open_file(seventhslide::library::database_file(root));
    seventhslide::db::ensure_schema(window);
    seventhslide::test::fill_library(window);
    const auto uid = seventhslide::db::library_uid(window);
    FolderStore store(seventhslide::library::sync_root(root));
    const auto founded = MemberLog(store, me, uid).found(k_passphrase, k_quick);
    auto church = std::make_unique<LibrarySync>(root, uid, me, nullptr, quick());
    church->share_everything();
    REQUIRE(wait_until([&] {
        return store.sequences(Numbered::Changes, "church").size() == 1 && !church->busy();
    }));

    // "fedora" joined before an invite had to prove itself, and the church
    // took in what it wrote. Its join counts no longer.
    // ``who``, with all the church has, makes ``edit`` and sends it to the church.
    const auto send_from = [&](const Identity& who, const std::string& edit) {
        seventhslide::test::TempDir there;
        FolderStore theirs(there.path / "sync");
        copy_files(store, theirs, Numbered::Members);
        copy_files(store, theirs, Numbered::Changes);
        auto db = new_library();
        SyncEngine engine(db, theirs, there.path / "media", who);
        engine.join(uid);
        REQUIRE(engine.sync().files_applied > 0);
        db.exec(edit);
        REQUIRE(engine.sync().changes_sent == 1);
        copy_files(theirs, store, Numbered::Changes);
    };
    Identity fedora{"fedora", "Fedora", SigningKey::generate()};
    store.write(Numbered::Members, "fedora", 1,
                unproved_join(fedora, uid, founded.admin,
                              seventhslide::sync::read_members(store, uid).heads));
    REQUIRE(seventhslide::sync::read_members(store, uid).find("fedora") == nullptr);
    send_from(fedora, "UPDATE songs SET key = 'F' WHERE title = 'Amazing Grace'");
    CHECK(church->store().snapshots().empty());
    church->sync_now();
    // Taken in here, it goes in a snapshot straight away: the others refuse it.
    REQUIRE(wait_until([&] { return !church->store().snapshots().empty() && !church->busy(); }));
    CHECK(church->applied_files().at("fedora") == 1);
    CHECK(window.query_int("SELECT COUNT(*) FROM songs WHERE key = 'F'", -1) == 1);
    const auto snapshots = church->store().snapshots();
    REQUIRE(snapshots.size() == 1);
    const auto label = seventhslide::test::read_index(church->store(), snapshots.front());
    CHECK(label.files.at("fedora") == 1);
    // Once is enough.
    church->sync_now();
    REQUIRE(wait_until([&] { return !church->busy(); }));
    CHECK(church->store().snapshots() == snapshots);
    window.exec("UPDATE songs SET title = 'It Is Well With My Soul' WHERE id = 2");
    REQUIRE(wait_until([&] {
        return store.sequences(Numbered::Changes, "church").size() == 2 && !church->busy();
    }));
    // A member's file, which the church builds on too.
    Identity pew{"pew", "Pew PC", SigningKey::generate()};
    MemberLog(store, pew, uid)
        .join(seventhslide::sync::read_invite(
            MemberLog(store, me, uid).invite(Role::Editor, k_expires, founded.admin)));
    send_from(pew, "UPDATE songs SET lyrics = '[V1]\nfrom the pew' WHERE title = 'Amazing Grace'");
    church->sync_now();
    REQUIRE(wait_until([&] { return church->applied_files().contains("pew") && !church->busy(); }));
    window.exec("UPDATE services SET name = 'Divine Service'");
    REQUIRE(wait_until([&] {
        return store.sequences(Numbered::Changes, "church").size() == 3 && !church->busy();
    }));
    CHECK(church->store().snapshots() == snapshots);  // the one it wrote holds the fedora's

    // A laptop joining gets every file but the fedora's, which its gate
    // refuses, and the pew's, not come yet; and no snapshot yet: the church's
    // second file waits.
    const auto laptop_root = dir.path / "laptop";
    Identity laptop{"laptop", "Laptop", SigningKey::generate()};
    auto joiner = std::make_unique<LibrarySync>(laptop_root, uid, laptop, nullptr, quick());
    MemberLog(joiner->store(), laptop, uid)
        .join(seventhslide::sync::read_invite(
            MemberLog(store, me, uid).invite(Role::Editor, k_expires, founded.admin)));
    copy_files(store, joiner->store(), Numbered::Members);
    for (const auto n : store.sequences(Numbered::Changes, "church")) {
        joiner->store().write(Numbered::Changes, "church", n, store.read(Numbered::Changes, "church", n));
    }
    joiner->schedule_sync();
    REQUIRE(wait_until([&] { return !joiner->report().waiting_on.empty() && !joiner->busy(); }));
    const auto waiting = joiner->report().waiting_on;
    REQUIRE(waiting.size() == 1);
    CHECK(waiting.front().machine == "fedora");
    CHECK(waiting.front().name == "Fedora");
    CHECK(joiner->applied_files().at("church") == 1);

    // The snapshot comes, its parts first: it catches up from it, and has all
    // the church has.
    for (const auto& part : church->store().parts()) {
        joiner->store().write_part(part.sha256, church->store().read_part(part.sha256));
    }
    for (const auto& name : church->store().snapshots()) {
        joiner->store().write_snapshot(name, church->store().read_snapshot(name));
    }
    joiner->sync_now();
    REQUIRE(wait_until([&] { return joiner->applied_files().contains("fedora") && !joiner->busy(); }));
    CHECK(joiner->applied_files().at("church") == 2);
    CHECK(joiner->applied_files().at("fedora") == 1);
    // The church's third file waits for the pew's, which this computer would
    // take in once it comes: nothing to say.
    CHECK(joiner->report().waiting_on.empty());

    copy_files(store, joiner->store(), Numbered::Changes, "fedora");
    joiner->sync_now();
    REQUIRE(wait_until([&] { return joiner->applied_files().contains("pew") && !joiner->busy(); }));
    CHECK(joiner->applied_files().at("church") == 3);
    CHECK(joiner->report().waiting_on.empty());
    joiner.reset();
    church.reset();
    auto copy = Database::open_file(seventhslide::library::database_file(laptop_root));
    seventhslide::test::check_same_records(copy, window);
}

namespace {

/// ``n`` libraries put online on this computer, each in its own folder, with
/// what ``fill`` puts in; synced by ``LibrarySync``s sharing ``turns``.
struct ManyOnline {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    Identity me{"church", "Church PC", SigningKey::generate()};
    std::vector<std::filesystem::path> roots;
    std::vector<std::unique_ptr<LibrarySync>> syncs;

    ManyOnline(int n, const std::shared_ptr<seventhslide::link::SyncTurns>& turns,
               const std::function<void(Database&)>& fill)
    {
        for (int i = 0; i < n; ++i) {
            const auto root = dir.path / ("library" + std::to_string(i));
            std::filesystem::create_directories(root);
            auto db = Database::open_file(seventhslide::library::database_file(root));
            seventhslide::db::ensure_schema(db);
            fill(db);
            const auto uid = seventhslide::db::library_uid(db);
            FolderStore store(seventhslide::library::sync_root(root));
            MemberLog(store, me, uid).found(k_passphrase, k_quick);
            auto options = quick();
            options.watch_edits = false;
            options.turns = turns;
            syncs.push_back(std::make_unique<LibrarySync>(root, uid, me, nullptr, options));
            syncs.back()->share_everything();
            roots.push_back(root);
        }
    }
    bool all_sent()
    {
        return std::ranges::all_of(syncs, [](const auto& s) {
            return !s->busy() && s->store().sequences(Numbered::Changes, "church").size() >= 1;
        });
    }
};

}  // namespace

TEST_CASE("Libraries on one computer sync a few at a time, every one in its turn", "[librarysync][hardening]")
{
    const auto turns = std::make_shared<seventhslide::link::SyncTurns>(2);
    ManyOnline many(5, turns, [](Database& db) { seventhslide::test::fill_library(db); });
    REQUIRE(wait_until([&] { return many.all_sent(); }, 30000));
    CHECK(turns->most_at_once() == 2);
    // One going while it waits for its turn gives it up cleanly.
    many.syncs.front()->sync_now();
    many.syncs.erase(many.syncs.begin());
    for (const auto& s : many.syncs) {
        s->sync_now();
    }
    REQUIRE(wait_until([&] { return many.all_sent(); }, 30000));
    CHECK(turns->most_at_once() == 2);
}

// Hidden: a server's work with many libraries (SEVENTHSLIDE_LIBRARIES,
// default 30; each about 2 MB), two syncing at once, as on a small computer:
// how long until all are sent, and the memory it took.
TEST_CASE("Measure many libraries", "[.pi]")
{
    const char* env = std::getenv("SEVENTHSLIDE_LIBRARIES");
    const int n = env != nullptr ? std::atoi(env) : 30;
    const auto turns = std::make_shared<seventhslide::link::SyncTurns>(2);
    QElapsedTimer timer;
    timer.start();
    ManyOnline many(n, turns, [](Database& db) {
        seventhslide::test::fill_library(db);
        db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1000) "
                "INSERT INTO songs (id, title, lyrics) SELECT 1000 + i, 'Hymn ' || i, "
                "'[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
    });
    const auto made_ms = timer.restart();
    REQUIRE(wait_until([&] { return many.all_sent(); }, 600000));
    std::cout << n << " libraries: made in " << made_ms << " ms, all sent in " << timer.elapsed()
              << " ms, at most " << turns->most_at_once() << " at once\n";
}
