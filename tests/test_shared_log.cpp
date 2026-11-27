// A synced library's shared log (shared_log.hpp, change_notes.hpp): the
// messages its computers leave each other and the notes of what each changed,
// who may write them, and how they move; and a computer's picture on the
// member list.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/crypto/signing.hpp>
#include <seventhslide/sync/change_notes.hpp>
#include <seventhslide/sync/envelope.hpp>
#include <seventhslide/sync/gate.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/sync/shared_log.hpp>
#include <seventhslide/sync/sync_store.hpp>

#include "link_test_helpers.hpp"
#include "sync_test_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using nlohmann::json;
using seventhslide::crypto::SigningKey;
using seventhslide::sync::FolderStore;
using seventhslide::sync::Gate;
using seventhslide::sync::Identity;
using seventhslide::sync::k_signed_as_computer;
using seventhslide::sync::LogBook;
using seventhslide::sync::LogEntry;
using seventhslide::sync::LogReader;
using seventhslide::sync::MemberLog;
using seventhslide::sync::Numbered;
using seventhslide::sync::Picture;
using seventhslide::sync::read_invite;
using seventhslide::sync::read_members;
using seventhslide::sync::Role;
using namespace seventhslide::test::link_fixture;

namespace {

const std::string k_library = "0123456789abcdef0123456789abcdef";

Identity who(const std::string& machine)
{
    return {machine, machine, SigningKey::generate()};
}

/// A store with a founded library in it, and its computers.
struct Library {
    seventhslide::test::TempDir dir{(seventhslide::test::ensure_qt_application(), seventhslide::test::TempDir{})};
    FolderStore store{dir.path / "sync"};
    Identity church{who("church")};
    MemberLog log{store, church, k_library};
    MemberLog::Founded founded{log.found(k_passphrase, k_quick)};

    void admit(const Identity& computer, Role role = Role::Editor)
    {
        MemberLog(store, computer, k_library).join(read_invite(log.invite(role, k_expires, founded.admin)));
    }
    Gate gate() { return Gate(read_members(store, k_library)); }
};

/// A log entry of ``by``'s written by hand.
std::string entry_text(const Identity& by, std::int64_t sequence, const std::string& library = k_library)
{
    LogEntry e;
    e.machine = by.machine;
    e.sequence = sequence;
    e.order = 1;
    e.time = "2026-11-05T15:00:00Z";
    e.kind = "message";
    e.text = "Hello";
    return seventhslide::sync::seal(seventhslide::sync::log_entry_to_json(e, library),
                                    {{k_signed_as_computer, &by.key}});
}

}  // namespace

TEST_CASE("A computer's picture goes on the list with its name, and a later name without one takes it away",
          "[sync][log]")
{
    Library lib;
    const auto laptop = who("laptop");
    lib.admit(laptop);
    MemberLog(lib.store, laptop, k_library).rename("Jane's PC", Picture{"song", "#2fb3a0", {}});
    auto list = read_members(lib.store, k_library);
    CHECK(list.refused.empty());
    CHECK(list.find("laptop")->name == "Jane's PC");
    CHECK(list.find("laptop")->picture == Picture{"song", "#2fb3a0", {}});

    MemberLog(lib.store, laptop, k_library).rename("Jane's PC", Picture{{}, {}, "iVBORw0KGgo="});
    CHECK(read_members(lib.store, k_library).find("laptop")->picture.image == "iVBORw0KGgo=");

    MemberLog(lib.store, laptop, k_library).rename("Jane's PC");
    CHECK(read_members(lib.store, k_library).find("laptop")->picture.empty());

    // A picture that is not one is none, and refuses nothing.
    CHECK(seventhslide::sync::picture_from_json({{"glyph", "song"}, {"colour", "red"}}).empty());
    CHECK(seventhslide::sync::picture_from_json({{"glyph", "../x"}, {"colour", "#ffffff"}}).empty());
    CHECK(seventhslide::sync::picture_from_json({{"image", "not base64!"}}).empty());
    CHECK(seventhslide::sync::picture_from_json(json::array()).empty());
}

TEST_CASE("Messages are posted in order and each is read once", "[sync][log]")
{
    Library lib;
    LogBook book(lib.store, lib.church, k_library);
    const auto first = book.post_message("  Could you fix the typos in In Christ Alone?  ");
    CHECK(first.text == "Could you fix the typos in In Christ Alone?");
    CHECK(first.sequence == 1);
    CHECK_THROWS_AS(book.post_message("   "), std::invalid_argument);
    CHECK_THROWS_AS(book.post_message(std::string(seventhslide::sync::k_longest_message + 1, 'a')),
                    std::invalid_argument);

    LogReader reader(k_library);
    auto read = reader.read_new(lib.store);
    REQUIRE(read.size() == 1);
    CHECK(read.front() == first);
    CHECK(reader.read_new(lib.store).empty());

    const auto second = book.post_changes({{"song", "u1", "In Christ Alone", "changed", {"lyrics", "verse_order"}}},
                                          {{"song", {{"changed", 1}}}});
    CHECK(second.sequence == 2);
    CHECK(second.order > first.order);
    read = reader.read_new(lib.store);
    REQUIRE(read.size() == 1);
    CHECK(read.front() == second);
}

TEST_CASE("An answer comes after what it answers, whatever the clocks say", "[sync][log]")
{
    Library lib;
    const auto laptop = who("laptop");
    lib.admit(laptop);
    LogBook(lib.store, lib.church, k_library).post_message("Hey, could you update the wording?");
    // The laptop's clock is an hour behind: its answer has the earlier time.
    const auto answer = LogBook(lib.store, laptop, k_library).post_message("Sure, I'll go do that!");
    LogReader reader(k_library);
    auto read = reader.read_new(lib.store);
    REQUIRE(read.size() == 2);
    CHECK(read[0].machine == "church");
    CHECK(read[1].machine == "laptop");
    CHECK(read[1].order == read[0].order + 1);
    CHECK(answer.order == 2);
}

TEST_CASE("Two threads writing this computer's entries at once each keep every one", "[sync][log]")
{
    // A message posted (the link's log thread) as a change is noted (the
    // library's thread): each takes a number of its own, and none replaces
    // another.
    constexpr int k_rounds = 20;
    constexpr int k_each = 25;
    for (int round = 0; round < k_rounds; ++round) {
        Library lib;
        std::atomic<bool> go{false};
        std::atomic<int> failed{0};
        const auto post = [&](bool messages) {
            LogBook book(lib.store, lib.church, k_library);
            while (!go) {
                std::this_thread::yield();
            }
            for (int i = 0; i < k_each; ++i) {
                try {
                    if (messages) {
                        (void)book.post_message("message " + std::to_string(i));
                    } else {
                        (void)book.post_changes({{"song", "u" + std::to_string(i), "Song", "changed", {"lyrics"}}},
                                                {{"song", {{"changed", 1}}}});
                    }
                } catch (const std::exception&) {
                    ++failed;
                }
            }
        };
        std::thread messages(post, true);
        std::thread notes(post, false);
        go = true;
        messages.join();
        notes.join();
        INFO("round " << round);
        REQUIRE(failed == 0);
        std::vector<std::int64_t> expected(2 * k_each);
        std::iota(expected.begin(), expected.end(), 1);
        REQUIRE(lib.store.sequences(Numbered::Log, "church") == expected);
        std::set<std::string> written;
        for (const auto n : expected) {
            const auto read = seventhslide::sync::read_log_entry(lib.store.read(Numbered::Log, "church", n));
            CHECK(read.entry.sequence == n);
            written.insert(read.entry.kind == "message" ? read.entry.text : read.entry.items.at(0).key);
        }
        CHECK(written.size() == expected.size());
    }
}

namespace {

/// A store whose log entries fail to be written: with nothing written, or
/// once the file is in.
class FailingStore final : public seventhslide::sync::SyncStore {
public:
    explicit FailingStore(std::filesystem::path root) : folder_(std::move(root)) {}

    bool fail_after_write = false;
    int writes = 0;

    std::vector<std::string> machines(Numbered kind) override { return folder_.machines(kind); }
    std::vector<std::int64_t> sequences(Numbered kind, std::string_view machine) override
    {
        return folder_.sequences(kind, machine);
    }
    std::string read(Numbered kind, std::string_view machine, std::int64_t sequence) override
    {
        return folder_.read(kind, machine, sequence);
    }
    void write(Numbered kind, std::string_view machine, std::int64_t sequence,
               std::string_view contents) override
    {
        ++writes;
        if (fail_after_write) {
            folder_.write(kind, machine, sequence, contents);
        }
        throw std::runtime_error("the disk is full");
    }
    void set_aside(Numbered kind, std::string_view machine, std::int64_t sequence) override
    {
        folder_.set_aside(kind, machine, sequence);
    }
    void set_aside_snapshot(const seventhslide::sync::SnapshotName& name) override
    {
        folder_.set_aside_snapshot(name);
    }
    void drop_changes_through(std::string_view machine, std::int64_t sequence) override
    {
        folder_.drop_changes_through(machine, sequence);
    }
    std::int64_t dropped_through(std::string_view machine) override { return folder_.dropped_through(machine); }
    std::vector<seventhslide::sync::SnapshotName> snapshots() override { return folder_.snapshots(); }
    std::string read_snapshot(const seventhslide::sync::SnapshotName& name) override
    {
        return folder_.read_snapshot(name);
    }
    void write_snapshot(const seventhslide::sync::SnapshotName& name, std::string_view contents) override
    {
        folder_.write_snapshot(name, contents);
    }
    bool remove_snapshot(const seventhslide::sync::SnapshotName& name) override
    {
        return folder_.remove_snapshot(name);
    }
    std::vector<seventhslide::sync::PartFile> parts() override { return folder_.parts(); }
    bool keep_part(std::string_view sha256) override { return folder_.keep_part(sha256); }
    std::string read_part(std::string_view sha256) override { return folder_.read_part(sha256); }
    void write_part(std::string_view sha256, std::string_view contents) override
    {
        folder_.write_part(sha256, contents);
    }
    bool remove_part(std::string_view sha256, std::filesystem::file_time_type touched_before) override
    {
        return folder_.remove_part(sha256, touched_before);
    }
    void set_aside_part(std::string_view sha256) override { folder_.set_aside_part(sha256); }
    std::vector<std::string> admin_keys() override { return folder_.admin_keys(); }
    std::string read_admin_key(std::string_view fingerprint) override { return folder_.read_admin_key(fingerprint); }
    void write_admin_key(std::string_view fingerprint, std::string_view contents) override
    {
        folder_.write_admin_key(fingerprint, contents);
    }

private:
    FolderStore folder_;
};

}  // namespace

TEST_CASE("A log entry that fails to be written is tried once, and not again under the next number",
          "[sync][log]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    FailingStore store(dir.path / "sync");
    LogBook book(store, who("church"), k_library);

    CHECK_THROWS_WITH(book.post_message("Hello"), ContainsSubstring("the disk is full"));
    CHECK(store.writes == 1);
    CHECK(store.sequences(Numbered::Log, "church").empty());

    // In, though the write said otherwise: it is not written again.
    store.fail_after_write = true;
    CHECK_THROWS(book.post_message("Hello"));
    CHECK(store.writes == 2);
    CHECK(store.sequences(Numbered::Log, "church") == std::vector<std::int64_t>{1});
}

TEST_CASE("The gate lets in a log entry only from an editor that signed it", "[sync][log]")
{
    Library lib;
    const auto laptop = who("laptop");
    const auto pew = who("pew");
    lib.admit(laptop);
    lib.admit(pew, Role::Viewer);
    auto gate = lib.gate();

    CHECK_NOTHROW(gate.log_entry("laptop", 1, entry_text(laptop, 1)));
    CHECK_THROWS_WITH(gate.log_entry("pew", 1, entry_text(pew, 1)), ContainsSubstring("may not"));
    // Not the computer it says; not the entry its name says; another library.
    CHECK_THROWS_WITH(gate.log_entry("laptop", 1, entry_text(Identity{"laptop", "x", SigningKey::generate()}, 1)),
                      ContainsSubstring("not that computer's"));
    CHECK_THROWS_WITH(gate.log_entry("laptop", 2, entry_text(laptop, 1)), ContainsSubstring("its name says"));
    CHECK_THROWS_WITH(gate.log_entry("laptop", 1, entry_text(laptop, 1, "ffffffffffffffffffffffffffffffff")),
                      ContainsSubstring("another library"));
    // One this computer does not know yet may be let in later.
    const auto stranger = who("stranger");
    CHECK_THROWS_AS(gate.log_entry("stranger", 1, entry_text(stranger, 1)), seventhslide::sync::NotYet);
}

TEST_CASE("A send notes the records people know, by what changed", "[sync][log]")
{
    Computer church{"church", "Church PC"};
    put_online(church);

    // A song's lyrics, and its verse order, which the style profile keeps.
    church.db.exec("UPDATE songs SET lyrics = '[V1]\nfixed line' WHERE id = 1");
    church.db.exec(R"(UPDATE style_profiles SET data = '{"songs":{"1":{"main":"t1"}},"song_verse_orders":{"1":"V1 V1","2":"V1 C"}}')");
    // A service's items, and a folder only moved.
    church.db.exec("UPDATE service_items SET order_num = order_num + 10");
    church.db.exec("UPDATE image_folders SET sort_order = 5 WHERE id = 2");
    const auto result = church.sync();
    REQUIRE(result.changes_sent > 0);

    const auto notes = seventhslide::sync::change_notes(church.db, result.sent);
    const auto find = [&](const std::string& kind, const std::string& title) {
        const auto it = std::ranges::find_if(notes.items, [&](const auto& i) { return i.kind == kind && i.title == title; });
        return it == notes.items.end() ? nullptr : &*it;
    };
    const auto* song = find("song", "Amazing Grace");
    REQUIRE(song != nullptr);
    CHECK(song->action == "changed");
    CHECK(std::ranges::find(song->fields, "lyrics") != song->fields.end());
    CHECK(std::ranges::find(song->fields, "verse_order") != song->fields.end());
    const auto* service = find("service", "Sabbath");
    REQUIRE(service != nullptr);
    CHECK(service->fields == std::vector<std::string>{"items"});
    // The profile itself did not change, and a folder only moved.
    CHECK(find("style_profile", "Default") == nullptr);
    CHECK(find("folder", "Easter") == nullptr);
    CHECK(notes.counts.at("song").at("changed") == 1);
    CHECK(notes.counts.at("service").at("changed") == 1);

    // A song added, and one removed, say so.
    church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('In Christ Alone', '[V1]\nline')");
    church.db.exec("DELETE FROM service_items WHERE song_id = 2");
    church.db.exec("DELETE FROM songs WHERE id = 2");
    const auto more = seventhslide::sync::change_notes(church.db, church.sync().sent);
    CHECK(std::ranges::any_of(more.items, [](const auto& i) { return i.title == "In Christ Alone" && i.action == "added"; }));
    CHECK(std::ranges::any_of(more.items, [](const auto& i) { return i.title == "It Is Well" && i.action == "removed"; }));
}

TEST_CASE("The shared log moves while syncing is paused, and nothing else does", "[sync][log][link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    laptop.engine.join(church.uid);
    REQUIRE(laptop.sync().files_applied == 1);
    REQUIRE(settle({&church, &laptop}));

    // Something is live at the church: its change waits, its message goes.
    church.link.set_paused(true);
    REQUIRE(settle({&church, &laptop}));
    church.db.exec("UPDATE songs SET key = 'A' WHERE id = 1");
    REQUIRE(church.sync().changes_sent == 1);
    LogBook(church.store, church.me, church.uid).post_message("Starting the service now");
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &laptop}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    CHECK(laptop.store.sequences(Numbered::Log, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});

    // And the laptop's answer reaches the church while it is paused.
    LogBook(laptop.store, laptop.me, laptop.uid).post_message("Thanks!");
    laptop.link.library_changed(laptop.uid);
    REQUIRE(settle({&church, &laptop}));
    CHECK(church.store.sequences(Numbered::Log, "laptop") == std::vector<std::int64_t>{1});
    LogReader reader(church.uid);
    const auto read = reader.read_new(church.store);
    REQUIRE(read.size() == 2);
    CHECK(read[0].text == "Starting the service now");
    CHECK(read[1].text == "Thanks!");

    church.link.set_paused(false);
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1, 2});
    CHECK(laptop.refused.empty());
    CHECK(church.refused.empty());
}
