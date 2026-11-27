// The sync engine (sync_engine.hpp): libraries on several computers trading
// changes, each through the sync folder of its own, linked as the link will
// link them.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/db/style_profile_repository.hpp>
#include <seventhslide/db/style_profiles.hpp>
#include <seventhslide/db/sync_bookkeeping.hpp>
#include <seventhslide/db/sync_outbox.hpp>
#include <seventhslide/db/sync_records.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/crypto/password_box.hpp>
#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/crypto/signing.hpp>
#include <seventhslide/sync/change_file.hpp>
#include <seventhslide/sync/envelope.hpp>
#include <seventhslide/sync/gate.hpp>
#include <seventhslide/sync/housekeeping.hpp>
#include <seventhslide/sync/identity.hpp>
#include <seventhslide/sync/media_pieces.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/sync/snapshot.hpp>
#include <seventhslide/sync/sync_engine.hpp>
#include <seventhslide/sync/sync_store.hpp>

#include "sync_test_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QString>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using seventhslide::db::Database;
using seventhslide::db::MediaDigest;
using seventhslide::crypto::SigningKey;
using seventhslide::sync::FolderStore;
using seventhslide::sync::Gate;
using seventhslide::sync::Identity;
using seventhslide::sync::MemberLog;
using seventhslide::sync::Members;
using seventhslide::sync::Numbered;
using seventhslide::sync::Role;
using seventhslide::sync::SnapshotName;
using seventhslide::sync::SyncEngine;
using seventhslide::sync::SyncStore;
using seventhslide::test::missing_media;

namespace {

/// A new library in memory, or on the disk at ``file``, as the app has it,
/// for a test that edits it from a second connection, as the window does.
Database new_library(const std::filesystem::path& file = {})
{
    seventhslide::test::ensure_qt_application();
    Database db = file.empty() ? Database::open_memory() : Database::open_file(file);
    seventhslide::db::ensure_schema(db);
    return db;
}

/// The root a file of ``bytes`` (one piece) is declared by.
std::string root_of(const std::string& bytes)
{
    return seventhslide::sync::media_root(seventhslide::sync::piece_hash(bytes));
}

/// ``media`` without the versions that declared them.
std::vector<MediaDigest> unversioned(std::vector<MediaDigest> media)
{
    for (auto& m : media) {
        m.version.reset();
    }
    return media;
}

std::vector<std::string> paths_of(const std::vector<MediaDigest>& media)
{
    std::vector<std::string> out;
    for (const auto& m : media) {
        out.push_back(m.path);
    }
    return out;
}

/// The admin password of every library here, and an invite's expiry no test
/// outlives.
constexpr const char* k_passphrase = "lilies of the field";
constexpr const char* k_expires = "2999-01-01T00:00:00Z";

/// A computer's identity. Its key is made once for its name, as its key file
/// would be: a computer whose library was lost joins again as itself.
Identity identity(const std::string& machine, const std::string& name = {})
{
    static std::map<std::string, SigningKey> keys;
    const auto it = keys.find(machine);
    const auto& key = it != keys.end() ? it->second
                                       : keys.emplace(machine, SigningKey::generate()).first->second;
    return {machine, name, key};
}

/// ``body`` as ``who`` would write it to the store.
std::string sealed(const Identity& who, const json& body)
{
    return seventhslide::sync::seal(body, {{seventhslide::sync::k_signed_as_computer, &who.key}});
}

/// A computer's store, with what a test needs to go wrong: a write that fails
/// before or after the file is in.
class TestStore final : public SyncStore {
public:
    explicit TestStore(std::filesystem::path root) : folder_(std::move(root)) {}

    bool fail_before_write = false;
    bool fail_after_write = false;
    /// Run as each change file is written, before it is in.
    std::function<void(std::int64_t sequence)> on_write;
    /// Run as each numbered file is read, before it is.
    std::function<void(Numbered kind, std::string_view machine, std::int64_t sequence)> on_read;
    /// Run as each snapshot part is read, on its bytes as read.
    std::function<void(std::string_view sha256, std::string& bytes)> on_read_part;
    /// How many more parts may be written before a write fails; none fails
    /// while negative.
    int parts_until_full = -1;
    /// A snapshot's index that will not go.
    bool keep_snapshots = false;
    /// Run as each part is let go of, before it goes.
    std::function<void(std::string_view sha256)> on_remove_part;

    std::vector<std::string> machines(Numbered kind) override { return folder_.machines(kind); }
    std::vector<std::int64_t> sequences(Numbered kind, std::string_view machine) override
    {
        return folder_.sequences(kind, machine);
    }
    std::string read(Numbered kind, std::string_view machine, std::int64_t sequence) override
    {
        if (on_read) {
            on_read(kind, machine, sequence);
        }
        return folder_.read(kind, machine, sequence);
    }
    void write(Numbered kind, std::string_view machine, std::int64_t sequence,
               std::string_view contents) override
    {
        if (fail_before_write) {
            throw std::runtime_error("the disk is full");
        }
        if (on_write && kind == Numbered::Changes) {
            on_write(sequence);
        }
        folder_.write(kind, machine, sequence, contents);
        if (fail_after_write) {
            throw std::runtime_error("the disk is full");
        }
    }
    void set_aside(Numbered kind, std::string_view machine, std::int64_t sequence) override
    {
        folder_.set_aside(kind, machine, sequence);
    }
    void set_aside_snapshot(const SnapshotName& name) override { folder_.set_aside_snapshot(name); }
    void drop_changes_through(std::string_view machine, std::int64_t sequence) override
    {
        folder_.drop_changes_through(machine, sequence);
    }
    std::int64_t dropped_through(std::string_view machine) override
    {
        return folder_.dropped_through(machine);
    }
    std::vector<SnapshotName> snapshots() override { return folder_.snapshots(); }
    std::string read_snapshot(const SnapshotName& name) override
    {
        return folder_.read_snapshot(name);
    }
    void write_snapshot(const SnapshotName& name, std::string_view contents) override
    {
        folder_.write_snapshot(name, contents);
    }
    bool remove_snapshot(const SnapshotName& name) override
    {
        return !keep_snapshots && folder_.remove_snapshot(name);
    }
    std::vector<seventhslide::sync::PartFile> parts() override { return folder_.parts(); }
    bool keep_part(std::string_view sha256) override { return folder_.keep_part(sha256); }
    std::string read_part(std::string_view sha256) override
    {
        auto bytes = folder_.read_part(sha256);
        if (on_read_part) {
            on_read_part(sha256, bytes);
        }
        return bytes;
    }
    void write_part(std::string_view sha256, std::string_view contents) override
    {
        if (parts_until_full == 0) {
            throw std::runtime_error("the disk is full");
        }
        folder_.write_part(sha256, contents);
        if (parts_until_full > 0) {
            --parts_until_full;
        }
    }
    bool remove_part(std::string_view sha256, std::filesystem::file_time_type touched_before) override
    {
        if (on_remove_part) {
            on_remove_part(sha256);
        }
        return folder_.remove_part(sha256, touched_before);
    }
    void set_aside_part(std::string_view sha256) override { folder_.set_aside_part(sha256); }
    std::vector<std::string> admin_keys() override { return folder_.admin_keys(); }
    std::string read_admin_key(std::string_view fingerprint) override
    {
        return folder_.read_admin_key(fingerprint);
    }
    void write_admin_key(std::string_view fingerprint, std::string_view contents) override
    {
        folder_.write_admin_key(fingerprint, contents);
    }

private:
    FolderStore folder_;
};

/// A computer with a library and the sync engine on it.
struct Computer {
    seventhslide::test::TempDir root;
    std::filesystem::path media{root.path / "media"};
    TestStore store{root.path / "sync"};
    Identity me;
    std::string machine{me.machine};
    Database db;
    SyncEngine engine;

    /// ``on_disk``: its library is a file, which a second connection, as
    /// the window's, may open (``db.path()``).
    explicit Computer(const std::string& machine_id, const std::string& name = {}, bool on_disk = false)
        : me(identity(machine_id, name)), db(new_library(on_disk ? root.path / "library.db" : std::filesystem::path())),
          engine(db, store, media, me)
    {
    }
    Computer(const Computer&) = delete;
    Computer& operator=(const Computer&) = delete;
    Computer(Computer&&) = delete;
    Computer& operator=(Computer&&) = delete;
    ~Computer() = default;

    std::string text(const std::string& sql)
    {
        QSqlQuery q(db.handle());
        REQUIRE(q.exec(QString::fromStdString(sql)));
        REQUIRE(q.next());
        return q.value(0).toString().toStdString();
    }
    std::int64_t count(const std::string& sql) { return db.query_int64(sql, -1); }

    /// This computer's view of ``library``'s member list, and its pen.
    MemberLog members(const std::string& library) { return {store, me, library}; }

    /// Put a media file in this computer's media folder ("images/<name>").
    void put_file(const std::string& path, const std::string& bytes)
    {
        std::filesystem::create_directories((media / path).parent_path());
        std::ofstream(media / path, std::ios::binary) << bytes;
    }
    /// A media file's bytes; empty when it is not there.
    std::string file(const std::string& path)
    {
        std::ifstream in(media / path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    bool has_file(const std::string& path) { return std::filesystem::exists(media / path); }
    /// The change file this computer's store holds from ``machine``.
    seventhslide::sync::ChangeFile change_file(const std::string& from, std::int64_t sequence)
    {
        return seventhslide::sync::change_file_from_json(
            seventhslide::sync::unseal(store.read(Numbered::Changes, from, sequence), "A test").body);
    }
    /// The snapshot this computer's store holds under ``name``.
    seventhslide::test::Snapshot snapshot(const SnapshotName& name)
    {
        return seventhslide::test::read_snapshot(store, name);
    }
};

/// The link between computers' stores, as the real one will be: whatever one
/// has and another lacks is checked at the gate and copied, member list
/// entries first, then the locked admin key (not to a viewer), change files
/// (their media first) and snapshots (their parts first).
struct Link {
    std::vector<Computer*> computers;
    /// A computer whose change files are not passed on, for a while.
    std::string hidden;
    /// The library the stores are of.
    std::string library;
    /// Why each file the gate refused was refused.
    std::vector<std::string> refused;
    /// Files stored by the passes so far.
    int stored = 0;

    void copy(Computer& from, Computer& to, const std::string& path, const std::string& root)
    {
        if (!from.has_file(path) || to.has_file(path)) {
            return;
        }
        if (!root.empty()) {
            REQUIRE(root_of(from.file(path)) == root);
        }
        to.put_file(path, from.file(path));
    }
    void fetch(Computer& from, Computer& to, const std::vector<MediaDigest>& media)
    {
        for (const auto& m : media) {
            copy(from, to, m.path, m.root);
        }
    }
    /// ``check``, or note why it was refused; whether it passed.
    template <typename Check>
    bool passes(Check check)
    {
        try {
            check();
            return true;
        } catch (const std::runtime_error& ex) {
            refused.emplace_back(ex.what());
            return false;
        }
    }
    /// Everything ``from`` has that ``to`` lacks.
    void pass(Computer& from, Computer& to)
    {
        for (const auto& machine : from.store.machines(Numbered::Members)) {
            const auto have = to.store.sequences(Numbered::Members, machine);
            for (const auto n : from.store.sequences(Numbered::Members, machine)) {
                if (std::ranges::find(have, n) == have.end()) {
                    const auto contents = from.store.read(Numbered::Members, machine, n);
                    // Each entry may change who the next is from.
                    const Gate gate(seventhslide::sync::read_members(to.store, library));
                    if (passes([&] { gate.member_entry(machine, n, contents); })) {
                        to.store.write(Numbered::Members, machine, n, contents);
                        ++stored;
                    }
                }
            }
        }
        seventhslide::sync::Vouches vouches(to.store);
        vouches.refresh();
        const Gate gate(seventhslide::sync::read_members(to.store, library), &vouches);
        const auto* receiver = gate.members().find(to.machine);
        if (receiver == nullptr || receiver->role != Role::Viewer) {
            const auto have = to.store.admin_keys();
            for (const auto& fingerprint : from.store.admin_keys()) {
                if (std::ranges::find(have, fingerprint) == have.end()) {
                    const auto contents = from.store.read_admin_key(fingerprint);
                    if (passes([&] { gate.admin_key(fingerprint, contents); })) {
                        to.store.write_admin_key(fingerprint, contents);
                        ++stored;
                    }
                }
            }
        }
        for (const auto& machine : from.store.machines(Numbered::Changes)) {
            if (machine == hidden) {
                continue;
            }
            const auto have = to.store.sequences(Numbered::Changes, machine);
            const auto dropped = to.store.dropped_through(machine);  // old, let go of there
            for (const auto n : from.store.sequences(Numbered::Changes, machine)) {
                if (n > dropped && std::ranges::find(have, n) == have.end()) {
                    const auto contents = from.store.read(Numbered::Changes, machine, n);
                    seventhslide::sync::ChangeFile file;
                    if (passes([&] { file = gate.change_file(machine, n, contents); })) {
                        fetch(from, to, file.media);
                        to.store.write(Numbered::Changes, machine, n, contents);
                        vouches.refresh();
                        ++stored;
                    }
                }
            }
        }
        // Only a snapshot newer than any there.
        std::int64_t newest = 0;
        for (const auto& name : to.store.snapshots()) {
            newest = std::max(newest, name.files);
        }
        for (const auto& name : from.store.snapshots()) {
            if (name.files <= newest) {
                continue;
            }
            const auto contents = from.store.read_snapshot(name);
            seventhslide::sync::SnapshotIndex index;
            if (!passes([&] { index = gate.snapshot(name, contents); })) {
                continue;
            }
            // Stored only once its parts are.
            const bool whole = std::ranges::all_of(index.parts, [&](const auto& part) {
                return to.store.keep_part(part.sha256) || passes([&] {
                           const auto text = from.store.read_part(part.sha256);
                           Gate::snapshot_part(part.sha256, text);
                           to.store.write_part(part.sha256, text);
                       });
            });
            if (whole) {
                to.store.write_snapshot(name, contents);
                ++stored;
            }
        }
        for (const auto& m : missing_media(to.engine)) {
            copy(from, to, m.path, m.root);
        }
    }
    /// Every computer passes to every other, again while anything moves: a
    /// file arriving may let in one refused for now, as the link asks again
    /// whenever its store changes.
    void all()
    {
        for (int before = -1; before != stored;) {
            before = stored;
            for (auto* a : computers) {
                for (auto* b : computers) {
                    if (a != b) {
                        pass(*a, *b);
                    }
                }
            }
        }
    }
};

/// A library put online on the computer "church", which founded it with the
/// admin password, and joined by "laptop" on an invite.
struct Shared {
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    std::string library{seventhslide::db::library_uid(church.db)};
    Link link{{&church, &laptop}, {}, library, {}};
    std::string recovery_code;

    /// ``laptop_on_disk``: the laptop's library is a file (``Computer``).
    explicit Shared(bool laptop_on_disk = false) : laptop("laptop", "Laptop", laptop_on_disk)
    {
        seventhslide::test::fill_library(church.db);
        // The files of the media fill_library registers.
        church.put_file("images/ab12.png", "lilies");
        church.put_file("videos/cd34.mp4", "welcome");
        church.put_file("audio/ef56.mp3", "prelude");
        auto founded = church.members(library).found(k_passphrase,
                                                     seventhslide::crypto::k_box_params_quick);
        admin_ = std::move(founded.admin);
        recovery_code = std::move(founded.recovery_code);
        church.engine.share_everything();
        REQUIRE(sync(church).changes_sent > 0);
        invite(laptop);
        laptop.engine.join(library);
        REQUIRE(sync(laptop).files_applied == 1);
    }

    /// The admin key, as an operator who typed the password holds it.
    const SigningKey& admin() const { return *admin_; }
    /// The member list as the church has it.
    Members members() { return seventhslide::sync::read_members(church.store, library); }

    /// Invite ``c`` as ``role``: it joins, and its entry reaches the others.
    void invite(Computer& c, Role role = Role::Editor)
    {
        admit(c.me, c.store, role);
    }
    /// ``who`` joins through ``store`` (its own, or another's standing in).
    void admit(const Identity& who, SyncStore& store, Role role = Role::Editor)
    {
        const auto code = church.members(library).invite(role, k_expires, admin());
        MemberLog(store, who, library).join(seventhslide::sync::read_invite(code));
        link.all();
    }
    /// Link ``c`` with the others: invited as ``role`` unless it is a member
    /// already, it gets what they have.
    void add(Computer& c, Role role = Role::Editor)
    {
        link.computers.push_back(&c);
        if (members().find(c.machine) == nullptr) {
            invite(c, role);
        }
        link.all();
    }
    /// ``c`` syncs, and what it sent reaches the others.
    seventhslide::sync::SyncResult sync(Computer& c)
    {
        auto result = c.engine.sync();
        link.all();
        return result;
    }
    /// ``c`` writes a snapshot, which reaches the others.
    bool snapshot(Computer& c)
    {
        const bool written = c.engine.snapshot();
        link.all();
        return written;
    }
    /// Each computer syncs until both have everything.
    void settle()
    {
        sync(church);
        sync(laptop);
        sync(church);
    }

private:
    std::optional<SigningKey> admin_;
};

}  // namespace

TEST_CASE("A media file is named by the root of its pieces' hashes, kept once worked out", "[sync]")
{
    using namespace seventhslide::sync;
    seventhslide::test::TempDir dir;
    const auto write = [&](const std::string& name, const std::string& bytes) {
        std::ofstream(dir.path / name, std::ios::binary) << bytes;
        return dir.path / name;
    };
    CHECK(piece_count(0) == 1);
    CHECK(piece_count(1) == 1);
    CHECK(piece_count(k_media_piece_bytes) == 1);
    CHECK(piece_count(k_media_piece_bytes + 1) == 2);

    // An empty file is one empty piece.
    const auto empty = hash_media_file(write("empty", ""));
    REQUIRE(empty);
    CHECK(*empty == piece_hash(""));
    CHECK(media_root(*empty) == root_of(""));
    // Pieces of 4 MiB, the last one shorter.
    std::string bytes(static_cast<std::size_t>(2 * k_media_piece_bytes + 5), 'a');
    bytes[static_cast<std::size_t>(k_media_piece_bytes)] = 'b';
    const auto file = write("video", bytes);
    const auto hashes = hash_media_file(file);
    REQUIRE(hashes);
    CHECK(*hashes
          == piece_hash(std::string_view(bytes).substr(0, k_media_piece_bytes))
                 + piece_hash(std::string_view(bytes).substr(k_media_piece_bytes, k_media_piece_bytes))
                 + piece_hash(std::string_view(bytes).substr(2 * k_media_piece_bytes)));
    CHECK_FALSE(hash_media_file(dir.path / "not there"));

    // Kept by path once worked out, and read back only for a file of the
    // size they are for.
    const PieceHashes kept(dir.path / "pieces");
    CHECK_FALSE(kept.load("videos/cd34.mp4", static_cast<std::int64_t>(bytes.size())));
    CHECK(kept.of("videos/cd34.mp4", file) == hashes);
    CHECK(kept.load("videos/cd34.mp4", static_cast<std::int64_t>(bytes.size())) == hashes);
    CHECK_FALSE(kept.load("videos/cd34.mp4", 10));
    // Not used for a file of another size, even of as many pieces, or one
    // written since.
    const auto shorter = write("shorter", bytes.substr(0, bytes.size() - 1));
    CHECK(kept.of("images/ab12.png", shorter) == hash_media_file(shorter));
    std::string same_size = bytes;
    same_size[0] = 'z';
    write("video", same_size);
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    CHECK(kept.of("videos/cd34.mp4", file) == hash_media_file(file));
    CHECK(*hash_media_file(file) != *hashes);
    kept.forget("videos/cd34.mp4");
    CHECK_FALSE(kept.load("videos/cd34.mp4", static_cast<std::int64_t>(bytes.size())));
    // Only under the folder, and none kept without one.
    CHECK_FALSE(kept.save("../../escape", 1, *hashes));
    CHECK_FALSE(PieceHashes({}).save("videos/cd34.mp4", 1, *hashes));
    CHECK(PieceHashes({}).of("videos/cd34.mp4", file) == hash_media_file(file));
}

TEST_CASE("A library put online arrives whole on a computer that joins it", "[sync]")
{
    Shared s;
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM verses") == 2);
    // With its media files, declared by the change file that brought them.
    CHECK(s.church.change_file("church", 1).media
          == std::vector<MediaDigest>{{"audio/ef56.mp3", 7, root_of("prelude")},
                                      {"images/ab12.png", 6, root_of("lilies")},
                                      {"videos/cd34.mp4", 7, root_of("welcome")}});
    CHECK(s.laptop.file("images/ab12.png") == "lilies");
    CHECK(s.laptop.file("videos/cd34.mp4") == "welcome");
    CHECK(s.laptop.file("audio/ef56.mp3") == "prelude");
    CHECK(missing_media(s.laptop.engine).empty());

    // What arrived is not sent back, and there is nothing more to bring.
    const auto again = s.sync(s.laptop);
    CHECK(again.changes_sent == 0);
    CHECK(again.files_applied == 0);
    CHECK(again.media_missing.empty());
    CHECK(s.sync(s.church).files_applied == 0);
    CHECK(s.church.store.sequences(Numbered::Changes, "laptop").empty());
}

TEST_CASE("Changes to different fields of a record both arrive", "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE songs SET title = 'Amazing Grace (Chris Tomlin)' WHERE id = 1");
    s.laptop.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.laptop.text("SELECT title || ' in ' || key FROM songs WHERE key = 'A'")
          == "Amazing Grace (Chris Tomlin) in A");
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs") == 2);  // no copy: no disagreement
}

TEST_CASE("The same field changed on two computers: the one sent last wins everywhere",
          "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE services SET name = 'Sabbath Worship'");
    s.laptop.db.exec("UPDATE services SET name = 'Divine Service'");
    SECTION("the church sends first")
    {
        s.sync(s.church);
        s.sync(s.laptop);  // takes the church's, keeps its own, and sends it
        s.sync(s.church);
        CHECK(s.church.text("SELECT name FROM services") == "Divine Service");
    }
    SECTION("the laptop sends first")
    {
        s.sync(s.laptop);
        s.sync(s.church);
        s.sync(s.laptop);
        CHECK(s.church.text("SELECT name FROM services") == "Sabbath Worship");
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A save that changed nothing sends nothing", "[sync]")
{
    Shared s;
    seventhslide::db::ConfigRepository config(s.church.db);
    config.save_outputs(config.load_outputs());
    config.set_setting("songs_sort", "title");
    s.church.db.exec("UPDATE songs SET title = title");
    REQUIRE_FALSE(seventhslide::db::marked_records(s.church.db).empty());
    CHECK(s.sync(s.church).changes_sent == 0);
    CHECK(seventhslide::db::marked_records(s.church.db).empty());
}

TEST_CASE("Capturing the style profile writes it only when the live state differs", "[sync]")
{
    Shared s;
    seventhslide::db::StyleProfileRepository profiles(s.church.db);
    seventhslide::db::SongRepository songs(s.church.db);
    seventhslide::db::ConfigRepository config(s.church.db);
    auto outputs = config.load_outputs();
    seventhslide::db::StyleProfileService service(profiles, songs, config, outputs);
    const auto profile_uid = s.church.text("SELECT uid FROM style_profiles WHERE id = 1");
    const std::vector<seventhslide::db::MarkedRecord> profile_marked{{"style_profiles", profile_uid}};

    // fill_library's profile is not what is live (it names a song that is
    // gone): written, and sent.
    service.capture_active();
    CHECK(seventhslide::db::marked_records(s.church.db) == profile_marked);
    CHECK(s.sync(s.church).changes_sent == 1);

    // Nothing has changed since: nothing is written.
    service.capture_active();
    CHECK(seventhslide::db::marked_records(s.church.db).empty());

    // A song's theme set: written, and it reaches the laptop.
    songs.set_theme_maps({{1, {{"main", {{"song", {{"theme", "t2"}}}}}}}});
    s.sync(s.church);
    REQUIRE(seventhslide::db::marked_records(s.church.db).empty());
    service.capture_active();
    CHECK(seventhslide::db::marked_records(s.church.db) == profile_marked);
    CHECK(s.sync(s.church).changes_sent == 1);
    s.sync(s.laptop);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(profiles.get(1)->data["songs"]["1"]["main"]["song"]["theme"] == "t2");
}

TEST_CASE("Records added, changed and deleted arrive, outputs among them", "[sync]")
{
    Shared s;
    s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('Be Thou My Vision', '[V1]\nline')");
    s.church.db.exec("DELETE FROM songs WHERE id = 2");
    seventhslide::db::ConfigRepository config(s.church.db);
    auto outputs = config.load_outputs();
    outputs.erase(outputs.begin() + 1);
    outputs.front().name = "Sanctuary";
    seventhslide::model::OutputConfig lobby;
    lobby.id = "lobby";
    lobby.name = "Lobby";
    outputs.push_back(lobby);
    config.save_outputs(outputs);

    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'It Is Well'") == 0);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'Be Thou My Vision'") == 1);
    CHECK(seventhslide::db::record_keys(s.laptop.db, "outputs")
          == std::vector<std::string>{"main", "lobby"});
}

TEST_CASE("An edit made meanwhile wins over a delete", "[sync]")
{
    Shared s;
    // "It Is Well" is in no service: what a delete takes with it comes later.
    s.church.db.exec("DELETE FROM songs WHERE title = 'It Is Well'");
    s.laptop.db.exec("UPDATE songs SET lyrics = '[V1]\nWhen peace like a river' "
                     "WHERE title = 'It Is Well'");
    SECTION("the delete is sent first")
    {
        s.sync(s.church);
        s.sync(s.laptop);  // keeps its edit, and sends the song whole
        s.sync(s.church);
    }
    SECTION("the edit is sent first")
    {
        s.sync(s.laptop);
        s.sync(s.church);  // the delete it had not sent is undone
        s.sync(s.laptop);
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.text("SELECT lyrics FROM songs WHERE title = 'It Is Well'")
          == "[V1]\nWhen peace like a river");
}

TEST_CASE("A file waits for the file it was made on top of", "[sync]")
{
    Shared s;
    s.laptop.db.exec("UPDATE songs SET title = 'Amazing Grace!' WHERE title = 'Amazing Grace'");
    s.sync(s.laptop);  // made on top of the church's first file

    // The office gets the laptop's file, and not yet the church's.
    s.link.hidden = "church";
    Computer office{"office"};
    s.add(office);
    office.engine.join(seventhslide::db::library_uid(s.church.db));
    const auto early = s.sync(office);
    CHECK(early.files_applied == 0);
    CHECK(early.files_waiting == 1);
    CHECK(office.count("SELECT COUNT(*) FROM songs") == 0);

    s.link.hidden.clear();
    s.link.all();
    CHECK(s.sync(office).files_applied == 2);
    seventhslide::test::check_same_records(office.db, s.laptop.db);
}

TEST_CASE("A Bible's verses arrive with it, and stay when it is renamed", "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE bibles SET name = 'Authorized Version'");
    CHECK(s.sync(s.church).changes_sent == 1);
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT name FROM bibles") == "Authorized Version");
    CHECK(s.laptop.count("SELECT COUNT(*) FROM verses") == 2);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("New folders sent together find their new parents", "[sync]")
{
    Shared s;
    // The child sorts before its parent, so it is written first.
    s.church.db.exec("INSERT INTO image_folders (id, name, uid) "
                     "VALUES (10, 'Seasons', 'ffffffffffffffffffffffffffffffff')");
    s.church.db.exec("INSERT INTO image_folders (name, parent_id, uid) "
                     "VALUES ('Advent', 10, '00000000000000000000000000000000')");
    s.sync(s.church);
    const auto result = s.sync(s.laptop);
    CHECK(result.held.empty());
    CHECK(s.laptop.text("SELECT p.name FROM image_folders c JOIN image_folders p "
                        "ON p.id = c.parent_id WHERE c.name = 'Advent'")
          == "Seasons");
}

TEST_CASE("Nothing is lost when a change file cannot be sent", "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE songs SET key = 'D' WHERE id = 1");

    SECTION("before the file is written")
    {
        s.church.store.fail_before_write = true;
        CHECK_THROWS_AS(s.sync(s.church), std::runtime_error);
        CHECK_FALSE(seventhslide::db::marked_records(s.church.db).empty());
        s.church.store.fail_before_write = false;
        CHECK(s.sync(s.church).changes_sent == 1);
    }
    SECTION("after the file is written, before this computer knows")
    {
        s.church.store.fail_after_write = true;
        CHECK_THROWS_AS(s.sync(s.church), std::runtime_error);
        s.church.store.fail_after_write = false;
        // Sent again in a file of its own; the first is not replaced.
        CHECK(s.sync(s.church).changes_sent == 1);
        CHECK(s.church.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1, 2, 3});
    }
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "D");
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("An edit in the window never waits while a library is sent, and none is lost", "[sync]")
{
    // On the disk, as the app has them: the sync's connection and the
    // window's, to one file. The window's waits for no lock at all.
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir dir;
    const auto file = dir.path / "library.db";
    auto db = Database::open_file(file);
    seventhslide::db::ensure_schema(db);
    seventhslide::test::fill_library(db);
    db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 2000) "
            "INSERT INTO songs (id, title, lyrics) SELECT 1000 + i, 'Hymn ' || i, "
            "'[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
    auto window = Database::open_file(file);
    window.exec("PRAGMA busy_timeout = 0");
    TestStore store(dir.path / "sync");
    const auto me = identity("church", "Church PC");
    SyncEngine engine(db, store, dir.path / "media", me);
    engine.share_everything();

    // While each file is signed and written, the window edits every song.
    // Those that file holds were read already: they stay marked, as they are
    // now; those the next files hold go as they are now.
    std::vector<std::int64_t> edited_at;
    store.on_write = [&](std::int64_t sequence) {
        window.exec("UPDATE songs SET key = 'K" + std::to_string(sequence) + "'");
        edited_at.push_back(sequence);
    };
    const auto first = engine.sync();
    store.on_write = nullptr;
    const auto files = store.sequences(Numbered::Changes, "church");
    REQUIRE(files.size() >= 2);
    CHECK(edited_at == files);
    CHECK_FALSE(seventhslide::db::marked_records(db).empty());
    const auto again = engine.sync();
    CHECK(again.changes_sent > 0);
    CHECK(again.changes_sent < first.changes_sent);
    CHECK(seventhslide::db::marked_records(db).empty());
    CHECK(engine.sync().changes_sent == 0);

    // Another computer taking every file has the library as it is here.
    auto laptop_db = new_library();
    TestStore laptop_store(dir.path / "laptop" / "sync");
    for (const auto n : store.sequences(Numbered::Changes, "church")) {
        laptop_store.write(Numbered::Changes, "church", n, store.read(Numbered::Changes, "church", n));
    }
    SyncEngine laptop(laptop_db, laptop_store, dir.path / "laptop" / "media", identity("laptop"));
    laptop.join(seventhslide::db::library_uid(db));
    laptop.sync();
    seventhslide::test::check_same_records(laptop_db, db);
    CHECK(laptop_db.query_int64("SELECT COUNT(*) FROM songs WHERE key != 'K"
                                + std::to_string(files.back()) + "'")
          == 0);
}

TEST_CASE("A change file from another library or a newer SeventhSlide is refused", "[sync]")
{
    Shared s;
    const auto office = identity("office");
    seventhslide::sync::ChangeFile file;
    file.machine = "office";
    file.sequence = 1;
    file.counter = 1;
    SECTION("another library")
    {
        file.library = "00000000000000000000000000000000";
        s.laptop.store.write(Numbered::Changes, "office", 1,
                             sealed(office, seventhslide::sync::change_file_to_json(file)));
        CHECK_THROWS_WITH(s.sync(s.laptop),
                          Catch::Matchers::ContainsSubstring("another library"));
    }
    SECTION("a newer SeventhSlide")
    {
        file.library = seventhslide::db::library_uid(s.church.db);
        file.format = seventhslide::sync::k_change_format + 1;
        s.laptop.store.write(Numbered::Changes, "office", 1,
                             sealed(office, seventhslide::sync::change_file_to_json(file)));
        CHECK_THROWS_WITH(s.sync(s.laptop),
                          Catch::Matchers::ContainsSubstring("newer version of SeventhSlide"));
        // Kept, to be applied once this computer is updated.
        CHECK(s.laptop.store.sequences(Numbered::Changes, "office") == std::vector<std::int64_t>{1});
    }
    SECTION("an envelope of a newer SeventhSlide")
    {
        file.library = seventhslide::db::library_uid(s.church.db);
        const auto text = sealed(office, seventhslide::sync::change_file_to_json(file));
        const auto end_of_head = text.find('\n');
        auto head = json::parse(text.substr(0, end_of_head));
        head["format"] = seventhslide::sync::k_envelope_format + 1;
        s.laptop.store.write(Numbered::Changes, "office", 1, head.dump() + text.substr(end_of_head));
        CHECK_THROWS_WITH(s.sync(s.laptop),
                          Catch::Matchers::ContainsSubstring("newer version of SeventhSlide"));
        CHECK(s.laptop.store.sequences(Numbered::Changes, "office") == std::vector<std::int64_t>{1});
    }
    SECTION("a library of a newer schema")
    {
        file.library = seventhslide::db::library_uid(s.church.db);
        file.schema = seventhslide::db::k_schema_version + 1;
        s.laptop.store.write(Numbered::Changes, "office", 1,
                             sealed(office, seventhslide::sync::change_file_to_json(file)));
        CHECK_THROWS_WITH(s.sync(s.laptop),
                          Catch::Matchers::ContainsSubstring("newer version of SeventhSlide"));
    }
}

TEST_CASE("Only a new, empty library joins a synced library", "[sync]")
{
    Computer full{"full"};
    seventhslide::test::fill_library(full.db);
    CHECK_THROWS_AS(full.engine.join("00000000000000000000000000000000"), std::logic_error);
}

namespace {

/// A change file from the computer "office", an editor that joined after the
/// church's first file and sends what a test needs, as the church receives it.
void send_from_office(Shared& s, std::int64_t sequence, std::vector<seventhslide::sync::Change> changes)
{
    const auto office = identity("office");
    if (s.members().find("office") == nullptr) {
        s.admit(office, s.church.store);
    }
    seventhslide::sync::ChangeFile file;
    file.library = s.library;
    file.machine = "office";
    file.sequence = sequence;
    file.counter = 100 + sequence;
    file.seen = {{"church", 1}};
    file.changes = std::move(changes);
    s.church.store.write(Numbered::Changes, "office", sequence,
                         sealed(office, seventhslide::sync::change_file_to_json(file)));
    s.link.all();
}

const std::string k_new_song = "5050505050505050505050505050505a";
const std::string k_new_item = "6060606060606060606060606060606b";

seventhslide::sync::Change new_item(Shared& s)
{
    return {"service_items", k_new_item, false,
            {{"service_id", std::nullopt}, {"song_id", std::nullopt}, {"order_num", std::nullopt}},
            json{{"service_id", s.church.text("SELECT uid FROM services")},
                 {"song_id", k_new_song},
                 {"order_num", 9},
                 {"item_type", "song"},
                 {"data", nullptr}}};
}

seventhslide::sync::Change new_song()
{
    return {"songs", k_new_song, false, {{"title", std::nullopt}, {"lyrics", std::nullopt}},
            json{{"title", "Be Thou My Vision"}, {"lyrics", "[V1]\nline"}}};
}

}  // namespace

TEST_CASE("A large send never makes the window's saves wait", "[sync]")
{
    // The window saves through a connection of its own, which waits at most
    // its busy timeout for the write lock. Putting a library online sends it
    // all, in several files: none may be built, signed or written while the
    // lock is held.
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir root;
    TestStore store{root.path / "sync"};
    const auto me = identity("church", "Church PC");
    const auto file = root.path / "seventhslide.db";
    Database db = Database::open_file(file);
    seventhslide::db::ensure_schema(db);
    const std::string verse(500 * 1024, 'x');
    for (int i = 0; i < 12; ++i) {
        db.exec("INSERT INTO songs (title, lyrics) VALUES ('Song " + std::to_string(i) + "', '[V1]\n"
                + verse + "')");
    }
    const auto library = seventhslide::db::library_uid(db);
    SyncEngine engine(db, store, root.path / "media", me);
    (void)MemberLog(store, me, library).found(k_passphrase, seventhslide::crypto::k_box_params_quick);
    engine.share_everything();

    Database window = Database::open_file(file);
    window.exec("PRAGMA busy_timeout = 100");
    int saved = 0;
    // Song 0 changes as each file is written, the last time after every
    // file has read it, whichever file that was (keys are random).
    store.on_write = [&](std::int64_t) {
        ++saved;
        window.exec("UPDATE songs SET copyright = 'Saved " + std::to_string(saved)
                    + "' WHERE title = 'Song 0'");
    };
    const auto first = engine.sync();
    CHECK(saved >= 2);
    CHECK(first.changes_sent >= 12);

    // Song 0, saved after its file read it, goes in the next send.
    store.on_write = nullptr;
    CHECK(engine.sync().changes_sent == 1);
    CHECK(engine.sync().changes_sent == 0);
}

TEST_CASE("A change waits for what it needs, then gets through", "[sync]")
{
    Shared s;
    send_from_office(s, 1, {new_item(s)});
    const auto early = s.sync(s.laptop);
    CHECK(early.files_applied == 1);
    CHECK(early.held == std::vector<std::string>{"service_items " + k_new_item});
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items WHERE order_num = 9") == 0);
    CHECK(s.sync(s.laptop).held.size() == 1);  // still waiting, and kept

    send_from_office(s, 2, {new_song()});
    const auto later = s.sync(s.laptop);
    CHECK(later.held.empty());
    CHECK(s.laptop.text("SELECT s.title FROM service_items i JOIN songs s ON s.id = i.song_id "
                        "WHERE i.order_num = 9")
          == "Be Thou My Vision");
    // What arrived is the other computers' already: nothing goes back.
    CHECK(later.changes_sent == 0);
}

TEST_CASE("A delete made on top of a waiting change takes it away", "[sync]")
{
    Shared s;
    send_from_office(s, 1, {new_item(s)});
    s.sync(s.laptop);
    send_from_office(s, 2,
                     {{"service_items", k_new_item, true,
                       {{"service_id", seventhslide::db::SyncVersion{101, "office"}},
                        {"song_id", seventhslide::db::SyncVersion{101, "office"}},
                        {"order_num", seventhslide::db::SyncVersion{101, "office"}}},
                       json()}});
    send_from_office(s, 3, {new_song()});
    CHECK(s.sync(s.laptop).held.empty());
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items WHERE order_num = 9") == 0);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'Be Thou My Vision'") == 1);
}

TEST_CASE("A service deleted while another computer edits one of its items comes back whole",
          "[sync]")
{
    Shared s;
    s.church.db.exec("DELETE FROM services");
    s.laptop.db.exec(R"(UPDATE service_items SET data = '{"bible_id":null,"book":"Psalms"}'
                        WHERE order_num = 1)");
    SECTION("the delete is sent first")
    {
        s.sync(s.church);
        s.sync(s.laptop);  // keeps the service, and sends it whole
        s.sync(s.church);
    }
    SECTION("the edit is sent first")
    {
        s.sync(s.laptop);
        // The item uses its service, deleted on the church PC and not sent: it
        // comes back there at once, with its items, and the delete is never sent.
        const auto back = s.sync(s.church);
        CHECK(back.held.empty());
        CHECK(back.changes_sent == 0);
        CHECK(s.church.count("SELECT COUNT(*) FROM services") == 1);
        s.sync(s.laptop);
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.count("SELECT COUNT(*) FROM service_items") == 3);
    CHECK(s.church.text("SELECT data FROM service_items WHERE order_num = 1")
          == R"({"bible_id":null,"book":"Psalms"})");
}

TEST_CASE("A file a sync applies reads nothing again of what is marked here that it does not touch", "[sync]")
{
    // The statements the laptop prepares to apply ``files`` of the church's,
    // each adding a song and deleting the one before, with 300 records of its
    // own marked and not sent: songs, or the items of a service, which point
    // at songs.
    const bool items = GENERATE(false, true);
    CAPTURE(items);
    const auto applying = [&](int files) {
        Shared s;
        s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('New 0', '')");
        s.sync(s.church);
        s.sync(s.laptop);
        const std::string three_hundred =
            "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 300) ";
        if (items) {
            s.laptop.db.exec("INSERT INTO services (name) VALUES ('Laptop')");
            s.laptop.db.exec(three_hundred
                             + "INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                               "SELECT (SELECT id FROM services WHERE name = 'Laptop'), 1, i, 'song' FROM n");
        } else {
            s.laptop.db.exec(three_hundred + "INSERT INTO songs (title, lyrics) SELECT 'Laptop ' || i, '' FROM n");
        }
        for (int i = 1; i <= files; ++i) {
            s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('New " + std::to_string(i) + "', '')");
            s.church.db.exec("DELETE FROM songs WHERE title = 'New " + std::to_string(i - 1) + "'");
            s.sync(s.church);
        }
        const auto before = s.laptop.db.statements_prepared();
        REQUIRE(s.sync(s.laptop).files_applied == files);
        CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title LIKE 'New %'") == 1);
        return s.laptop.db.statements_prepared() - before;
    };
    const auto one = applying(1);
    const auto eleven = applying(11);
    CAPTURE(one, eleven);
    // Ten more files cost what ten files do, not ten times what is marked.
    CHECK(eleven - one < 10 * 100);
}

TEST_CASE("What the files a sync applies mark costs the files after them nothing", "[sync]")
{
    // The statements the laptop prepares to apply ``files`` of the church's,
    // each adding a service of 12 items and a song, and deleting the song
    // before. Each record the engine writes stays marked until the laptop
    // sends, so each file finds more marked than the one before.
    const auto applying = [](int files) {
        Shared s;
        s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('New 0', '')");
        s.sync(s.church);
        s.sync(s.laptop);
        for (int i = 1; i <= files; ++i) {
            const auto n = std::to_string(i);
            s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('New " + n + "', '')");
            s.church.db.exec("DELETE FROM songs WHERE title = 'New " + std::to_string(i - 1) + "'");
            s.church.db.exec("INSERT INTO services (name) VALUES ('Week " + n + "')");
            s.church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 12) "
                             "INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                             "SELECT (SELECT id FROM services WHERE name = 'Week "
                             + n + "'), 1, i, 'song' FROM n");
            s.sync(s.church);
        }
        const auto before = s.laptop.db.statements_prepared();
        REQUIRE(s.sync(s.laptop).files_applied == files);
        CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items") == 3 + 12 * files);
        return s.laptop.db.statements_prepared() - before;
    };
    const auto one = applying(1);
    const auto eleven = applying(11);
    const auto twenty_one = applying(21);
    CAPTURE(one, eleven, twenty_one);
    // The second ten files cost what the first ten do.
    CHECK(twenty_one - eleven < (eleven - one) + 50);
}

TEST_CASE("What the window writes between two files a sync applies is read for the second", "[sync]")
{
    // The church adds a song to a service in its second file, which the
    // window deletes on the laptop as that file is read: it comes back.
    Shared s(true);
    s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('Be Still', '')");
    s.settle();
    s.church.db.exec("UPDATE songs SET key = 'D' WHERE title = 'Amazing Grace'");
    s.sync(s.church);
    s.church.db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                     "SELECT 1, id, 9, 'song' FROM songs WHERE title = 'Be Still'");
    s.sync(s.church);
    const auto second = s.church.store.sequences(Numbered::Changes, "church").back();
    auto window = Database::open_file(s.laptop.db.path());
    bool deleted = false;
    s.laptop.store.on_read = [&](Numbered kind, std::string_view machine, std::int64_t sequence) {
        if (kind == Numbered::Changes && machine == "church" && sequence == second
            && !std::exchange(deleted, true)) {
            window.exec("DELETE FROM songs WHERE title = 'Be Still'");
        }
    };
    const auto result = s.sync(s.laptop);
    s.laptop.store.on_read = nullptr;
    REQUIRE(deleted);
    CHECK(result.files_applied == 2);
    CHECK(result.held.empty());
    CHECK(s.laptop.text("SELECT s.title FROM service_items i JOIN songs s ON s.id = i.song_id "
                        "WHERE i.order_num = 9")
          == "Be Still");
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("What a delete in one file a sync applies takes with it is read for the next", "[sync]")
{
    // A service item marked on the laptop, as it was shared, names a song.
    // The church's first file deletes its service, and the item with it; the
    // second deletes the song, which nothing names now.
    Shared s;
    s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('Be Still', '')");
    s.church.db.exec("INSERT INTO services (name) VALUES ('Vespers')");
    s.church.db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                     "SELECT (SELECT id FROM services WHERE name = 'Vespers'), id, 9, 'song' FROM songs "
                     "WHERE title = 'Be Still'");
    s.settle();
    s.laptop.db.exec("UPDATE service_items SET order_num = 9 WHERE order_num = 9");
    s.church.db.exec("DELETE FROM services WHERE name = 'Vespers'");
    s.sync(s.church);
    s.church.db.exec("DELETE FROM songs WHERE title = 'Be Still'");
    s.sync(s.church);
    CHECK(s.sync(s.laptop).files_applied == 2);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'Be Still'") == 0);
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A record one file a sync applies writes is read again for the next", "[sync]")
{
    // A service item marked on the laptop, as it was shared, names a song.
    // The church's first file moves it to another song (and deletes one, so
    // what each mark names is read before it is applied); the second deletes
    // the first song, which nothing names now.
    Shared s;
    s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('Be Still', ''), ('Abide', '')");
    s.church.db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                     "SELECT 1, id, 9, 'song' FROM songs WHERE title = 'Be Still'");
    s.settle();
    s.laptop.db.exec("UPDATE service_items SET order_num = 9 WHERE order_num = 9");
    s.church.db.exec("UPDATE service_items SET song_id = (SELECT id FROM songs WHERE title = 'Abide') "
                     "WHERE order_num = 9");
    s.church.db.exec("DELETE FROM songs WHERE title = 'It Is Well'");
    s.sync(s.church);
    s.church.db.exec("DELETE FROM songs WHERE title = 'Be Still'");
    s.sync(s.church);
    CHECK(s.sync(s.laptop).files_applied == 2);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'Be Still'") == 0);
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A song deleted while another computer adds it to a service comes back", "[sync]")
{
    Shared s;
    s.church.db.exec("DELETE FROM songs WHERE title = 'It Is Well'");
    s.laptop.db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                     "SELECT (SELECT id FROM services), id, 3, 'song' FROM songs "
                     "WHERE title = 'It Is Well'");
    SECTION("the delete is sent first")
    {
        s.sync(s.church);
        s.sync(s.laptop);
        s.sync(s.church);
    }
    SECTION("the service item is sent first")
    {
        s.sync(s.laptop);
        // The item uses the song, deleted on the church PC and not sent: it
        // comes back there at once.
        const auto back = s.sync(s.church);
        CHECK(back.held.empty());
        CHECK(back.changes_sent == 0);
        s.sync(s.laptop);
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.text("SELECT s.title FROM service_items i JOIN songs s ON s.id = i.song_id "
                        "WHERE i.order_num = 3")
          == "It Is Well");
    // The song took back its id, so the style profile still finds its verse order.
    CHECK(s.church.text("SELECT data FROM style_profiles").find("V1 C") != std::string::npos);
}

TEST_CASE("A song in use, deleted while its service item changes, stays named by the item",
          "[sync]")
{
    Shared s;
    s.church.db.exec("DELETE FROM songs WHERE title = 'Amazing Grace'");
    REQUIRE(s.church.count("SELECT COUNT(*) FROM service_items WHERE song_id IS NULL "
                           "AND order_num = 0")
            == 1);
    s.laptop.db.exec(R"(UPDATE service_items SET data = '{"user_modified":true}'
                        WHERE order_num = 0)");
    s.sync(s.church);
    s.sync(s.laptop);
    s.sync(s.church);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.text("SELECT s.title FROM service_items i JOIN songs s ON s.id = i.song_id "
                        "WHERE i.order_num = 0")
          == "Amazing Grace");
}

TEST_CASE("A computer that holds the library for the others writes none of it, and they all agree",
          "[sync]")
{
    Shared s;
    Computer server{"server", "Church server"};
    server.engine.set_holds_only(true);
    server.engine.set_snapshot_after(1);  // it would write one at every turn
    s.add(server, Role::Viewer);
    server.engine.join(s.library);
    REQUIRE(s.sync(server).files_applied == 1);
    seventhslide::test::check_same_records(s.church.db, server.db);

    // The merge rules keep a record here and would send it again; its
    // keeping is the computers' that kept it.
    s.church.db.exec("DELETE FROM songs WHERE title = 'It Is Well'");
    s.laptop.db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                     "SELECT (SELECT id FROM services), id, 3, 'song' FROM songs "
                     "WHERE title = 'It Is Well'");
    for (int turn = 0; turn < 3; ++turn) {
        s.sync(s.church);
        s.sync(server);
        s.sync(s.laptop);
    }
    s.sync(server);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    seventhslide::test::check_same_records(s.church.db, server.db);
    CHECK(s.church.text("SELECT s.title FROM service_items i JOIN songs s ON s.id = i.song_id "
                        "WHERE i.order_num = 3")
          == "It Is Well");
    CHECK(s.link.refused.empty());

    // It wrote nothing, and has nothing left to write.
    CHECK(server.store.sequences(Numbered::Changes, "server").empty());
    CHECK_FALSE(server.engine.snapshot());
    CHECK(std::ranges::none_of(server.store.snapshots(),
                               [](const SnapshotName& name) { return name.machine == "server"; }));
    CHECK(seventhslide::db::marked_records(server.db).empty());
}

TEST_CASE("A delete goes through when nothing that belongs to it changed meanwhile", "[sync]")
{
    Shared s;
    // The folder takes its folder, the files filed in it, and the service item
    // showing it points at it from inside its JSON.
    s.church.db.exec("DELETE FROM service_items WHERE item_type = 'image_folder'");
    s.church.db.exec("DELETE FROM image_folders WHERE parent_id IS NULL");
    s.laptop.db.exec("UPDATE songs SET key = 'F' WHERE title = 'It Is Well'");
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM image_folders") == 0);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM image_folder_items") == 0);
}

TEST_CASE("A song changed differently on two computers keeps both versions", "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE songs SET lyrics = '[V1]\nchurch words' WHERE id = 1");
    s.laptop.db.exec("UPDATE songs SET lyrics = '[V1]\nlaptop words' "
                     "WHERE title = 'Amazing Grace'");
    std::string winner;
    std::string copy;
    SECTION("the church sends first: the laptop's, sent last, wins")
    {
        s.sync(s.church);
        s.sync(s.laptop);
        s.sync(s.church);
        winner = "laptop words";
        copy = "Amazing Grace (from Church PC)|[V1]\nchurch words";
    }
    SECTION("the laptop sends first: the church's, sent last, wins")
    {
        s.sync(s.laptop);
        s.sync(s.church);
        s.sync(s.laptop);
        winner = "church words";
        copy = "Amazing Grace (from Laptop)|[V1]\nlaptop words";
    }
    SECTION("both send before seeing the other's: the later version wins")
    {
        s.link.hidden = "church";
        s.sync(s.church);
        s.sync(s.laptop);  // sends without the church's
        s.link.hidden.clear();
        s.sync(s.church);  // its sent change lost: it makes the copy
        s.sync(s.laptop);
        // Same counter on both; "laptop" sorts after "church".
        winner = "laptop words";
        copy = "Amazing Grace (from Church PC)|[V1]\nchurch words";
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.text("SELECT lyrics FROM songs WHERE title = 'Amazing Grace'")
          == "[V1]\n" + winner);
    CHECK(s.church.text("SELECT title || '|' || lyrics FROM songs WHERE title LIKE '%(from %'")
          == copy);
    CHECK(s.church.count("SELECT COUNT(*) FROM songs") == 3);

    // A third computer joining has the same, once.
    Computer office{"office"};
    s.add(office);
    office.engine.join(seventhslide::db::library_uid(s.church.db));
    s.sync(office);
    seventhslide::test::check_same_records(office.db, s.church.db);
}

TEST_CASE("The same change made on two computers is no disagreement", "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE songs SET key = 'Bb' WHERE id = 1");
    s.laptop.db.exec("UPDATE songs SET key = 'Bb' WHERE title = 'Amazing Grace'");
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.count("SELECT COUNT(*) FROM songs") == 2);
}

TEST_CASE("A Freeform document changed differently on two computers keeps both", "[sync]")
{
    Shared s;
    s.church.db.exec(R"(UPDATE freeform_items SET document = '{"schema_version":1,"by":"church"}')");
    s.laptop.db.exec(R"(UPDATE freeform_items SET document = '{"schema_version":1,"by":"laptop"}')");
    s.sync(s.church);
    s.sync(s.laptop);
    s.sync(s.church);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.text("SELECT document FROM freeform_items WHERE name = 'Sermon slides'")
          == R"({"schema_version":1,"by":"laptop"})");
    CHECK(s.church.text("SELECT document FROM freeform_items "
                        "WHERE name = 'Sermon slides (from Church PC)'")
          == R"({"schema_version":1,"by":"church"})");
}

TEST_CASE("Anything else changed differently on two computers takes the later change", "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE bibles SET name = 'KJV 1611'");
    s.laptop.db.exec("UPDATE bibles SET name = 'KJV 1769'");
    s.sync(s.church);
    s.sync(s.laptop);
    s.sync(s.church);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.count("SELECT COUNT(*) FROM bibles") == 1);
    CHECK(s.church.text("SELECT name FROM bibles") == "KJV 1769");
}

namespace {

/// A service of four items, n 0 to 3, on both computers.
std::int64_t four_item_service(Shared& s)
{
    seventhslide::db::ServiceRepository services(s.church.db);
    const auto id = services.create_service("Vespers");
    for (int n = 0; n < 4; ++n) {
        (void)services.add_item(id, "divider", json{{"n", n}});
    }
    s.sync(s.church);
    s.sync(s.laptop);
    return id;
}

std::vector<int> order_of(Computer& c, const std::string& service = "Vespers")
{
    seventhslide::db::ServiceRepository services(c.db);
    const auto id = c.count("SELECT id FROM services WHERE name = '" + service + "'");
    std::vector<int> out;
    for (const auto& item : services.list_items(id)) {
        out.push_back(item.data.value("n", -1));
    }
    return out;
}

std::vector<std::int64_t> item_ids(Computer& c)
{
    seventhslide::db::ServiceRepository services(c.db);
    std::vector<std::int64_t> out;
    for (const auto& item :
         services.list_items(c.count("SELECT id FROM services WHERE name = 'Vespers'"))) {
        out.push_back(*item.item_id);
    }
    return out;
}

}  // namespace

TEST_CASE("Items added to one service on two computers are all there, in one order", "[sync]")
{
    Shared s;
    four_item_service(s);
    seventhslide::db::ServiceRepository church(s.church.db);
    seventhslide::db::ServiceRepository laptop(s.laptop.db);
    (void)church.add_item(s.church.count("SELECT id FROM services WHERE name = 'Vespers'"), "divider",
                    json{{"n", 10}});
    (void)laptop.add_item(s.laptop.count("SELECT id FROM services WHERE name = 'Vespers'"), "divider",
                    json{{"n", 20}}, std::nullopt, 2);
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(order_of(s.church) == order_of(s.laptop));
    CHECK(order_of(s.church) == std::vector<int>{0, 1, 20, 2, 3, 10});
}

TEST_CASE("An item moved on one computer stays moved when another adds one", "[sync]")
{
    Shared s;
    four_item_service(s);
    auto ids = item_ids(s.church);
    seventhslide::db::ServiceRepository church(s.church.db);
    church.reorder_items(s.church.count("SELECT id FROM services WHERE name = 'Vespers'"),
                         {ids[3], ids[0], ids[1], ids[2]});
    seventhslide::db::ServiceRepository laptop(s.laptop.db);
    (void)laptop.add_item(s.laptop.count("SELECT id FROM services WHERE name = 'Vespers'"), "divider",
                    json{{"n", 20}}, std::nullopt, 2);
    SECTION("the move is sent first") { s.settle(); }
    SECTION("the addition is sent first")
    {
        s.sync(s.laptop);
        s.sync(s.church);
        s.sync(s.laptop);
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(order_of(s.church) == order_of(s.laptop));
    CHECK(order_of(s.church) == std::vector<int>{3, 0, 1, 20, 2});
}

TEST_CASE("Two computers moving different items keep both moves", "[sync]")
{
    Shared s;
    four_item_service(s);
    auto ids = item_ids(s.church);
    const auto church_service = s.church.count("SELECT id FROM services WHERE name = 'Vespers'");
    const auto laptop_service = s.laptop.count("SELECT id FROM services WHERE name = 'Vespers'");
    auto laptop_ids = item_ids(s.laptop);
    seventhslide::db::ServiceRepository(s.church.db)
        .reorder_items(church_service, {ids[3], ids[0], ids[1], ids[2]});  // 3 to the top
    seventhslide::db::ServiceRepository(s.laptop.db)
        .reorder_items(laptop_service,
                       {laptop_ids[1], laptop_ids[2], laptop_ids[0], laptop_ids[3]});  // 0 down
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(order_of(s.church) == order_of(s.laptop));
    CHECK(order_of(s.church) == std::vector<int>{3, 1, 2, 0});
}

TEST_CASE("Tags added and removed on two computers at once all count", "[sync]")
{
    Shared s;
    // The service is tagged Easter on both.
    SECTION("each adds one")
    {
        s.church.db.exec(R"(UPDATE services SET tags = '["Easter","Youth"]')");
        s.laptop.db.exec(R"(UPDATE services SET tags = '["Easter","Choir"]')");
        s.settle();
        CHECK(s.church.text("SELECT tags FROM services") == R"(["Choir","Easter","Youth"])");
    }
    SECTION("one takes one away while the other adds one")
    {
        s.church.db.exec(R"(UPDATE services SET tags = '[]')");
        s.laptop.db.exec(R"(UPDATE services SET tags = '["Easter","Choir"]')");
        s.settle();
        CHECK(s.church.text("SELECT tags FROM services") == R"(["Choir"])");
    }
    SECTION("both send before seeing the other's")
    {
        s.church.db.exec(R"(UPDATE services SET tags = '["Easter","Youth"]')");
        s.laptop.db.exec(R"(UPDATE services SET tags = '["Choir"]')");
        s.link.hidden = "church";
        s.sync(s.church);
        s.sync(s.laptop);
        s.link.hidden.clear();
        s.sync(s.church);
        s.sync(s.laptop);
        CHECK(s.church.text("SELECT tags FROM services") == R"(["Choir","Youth"])");
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("Topics added to a song on two computers combine, with no copy", "[sync]")
{
    Shared s;
    s.church.db.exec(R"(UPDATE songs SET topics = '["grace"]' WHERE id = 1)");
    s.laptop.db.exec(R"(UPDATE songs SET topics = '["hope"]' WHERE title = 'Amazing Grace')");
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.text("SELECT topics FROM songs WHERE title = 'Amazing Grace'")
          == R"(["grace","hope"])");
    CHECK(s.church.count("SELECT COUNT(*) FROM songs") == 2);
}

TEST_CASE("Tags changed on one computer arrive as they are", "[sync]")
{
    Shared s;
    s.church.db.exec(R"(UPDATE services SET tags = '["Youth","Easter"]')");
    s.settle();
    CHECK(s.laptop.text("SELECT tags FROM services") == R"(["Youth","Easter"])");
}

// Snapshots and joining.

namespace {

void remove_change_file(Computer& c, const std::string& machine, const std::string& name)
{
    std::filesystem::remove(c.root.path / "sync" / "changes" / machine / name);
}

/// ``n`` songs of 2,000 characters each, about 2 MB a thousand: a library of
/// several snapshot parts.
void add_songs(Computer& c, int n)
{
    c.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < " + std::to_string(n)
              + ") INSERT INTO songs (id, title, lyrics) SELECT 1000 + i, 'Hymn ' || i, "
                "'[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
}

/// The SHA-256 of each of ``parts``, sorted.
std::vector<std::string> part_names(const std::vector<seventhslide::sync::SnapshotPartRef>& parts)
{
    std::vector<std::string> out;
    for (const auto& p : parts) {
        out.push_back(p.sha256);
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace

TEST_CASE("A computer joining starts from the newest snapshot, and replays only what came after",
          "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE songs SET key = 'D' WHERE id = 1");
    s.settle();
    REQUIRE(s.snapshot(s.church));
    CHECK_FALSE(s.snapshot(s.church));  // it would hold nothing more
    CHECK(s.laptop.store.snapshots() == std::vector<SnapshotName>{{"church", 2}});
    s.church.db.exec("UPDATE songs SET title = 'It Is Well With My Soul' WHERE id = 2");
    s.sync(s.church);

    // The files the snapshot holds are not needed (old ones may go one day).
    for (auto* c : {&s.church, &s.laptop}) {
        remove_change_file(*c, "church", "0000000001.json");
        remove_change_file(*c, "church", "0000000002.json");
    }
    Computer office{"office"};
    s.add(office);  // the snapshot
    office.engine.join(seventhslide::db::library_uid(s.church.db));
    CHECK(office.count("SELECT COUNT(*) FROM verses") == 2);
    // Its media follows the join.
    CHECK(missing_media(office.engine).size() == 3);
    s.link.all();
    CHECK(missing_media(office.engine).empty());
    const auto first = s.sync(office);
    CHECK(first.files_applied == 1);
    CHECK(first.changes_sent == 0);  // what it has is shared, not its own to send
    CHECK(first.media_missing.empty());
    CHECK(office.file("images/ab12.png") == "lilies");
    seventhslide::test::check_same_records(office.db, s.church.db);
    s.sync(s.laptop);
    seventhslide::test::check_same_records(office.db, s.laptop.db);

    // And it syncs on from there.
    office.db.exec("UPDATE songs SET key = 'E' WHERE title = 'Amazing Grace'");
    CHECK(s.sync(office).changes_sent == 1);
    s.settle();
    CHECK(s.church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "E");
}

namespace {

/// Each of ``machine``'s ``kind`` files ``from`` has and ``to`` lacks, put in
/// by no gate: as a SeventhSlide whose rules let them in did.
void copy_unchecked(Computer& from, Computer& to, Numbered kind, const std::string& machine)
{
    const auto have = to.store.sequences(kind, machine);
    for (const auto n : from.store.sequences(kind, machine)) {
        if (std::ranges::find(have, n) == have.end()) {
            to.store.write(kind, machine, n, from.store.read(kind, machine, n));
        }
    }
}

}  // namespace

TEST_CASE("Files from a computer the member list turns away now hold up no one: an editor's "
          "snapshot carries them",
          "[sync]")
{
    Shared s;
    // "fedora" joined under an earlier SeventhSlide, by rules today's refuse:
    // it is no member here. The church took its file in, as that SeventhSlide
    // did, and made its own on top of it.
    Computer fedora{"fedora", "Fedora"};
    for (const auto kind : {Numbered::Members, Numbered::Changes}) {
        for (const auto& machine : s.church.store.machines(kind)) {
            copy_unchecked(s.church, fedora, kind, machine);
        }
    }
    fedora.engine.join(s.library);
    REQUIRE(fedora.engine.sync().files_applied == 1);
    fedora.db.exec("UPDATE songs SET key = 'F' WHERE title = 'Amazing Grace'");
    REQUIRE(fedora.engine.sync().changes_sent == 1);
    copy_unchecked(fedora, s.church, Numbered::Changes, "fedora");
    REQUIRE(s.church.engine.sync().files_applied == 1);
    s.church.db.exec("UPDATE songs SET title = 'It Is Well With My Soul' WHERE id = 2");
    REQUIRE(s.sync(s.church).changes_sent == 1);

    // The laptop refuses the fedora's file, so the church's waits for it.
    CHECK(s.laptop.store.sequences(Numbered::Changes, "fedora").empty());
    const auto stuck = s.sync(s.laptop);
    CHECK(stuck.files_waiting == 1);
    CHECK(stuck.waiting_on == std::set<std::string>{"fedora"});
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'It Is Well With My Soul'") == 0);
    // Still, once nothing more is coming.
    CHECK(s.sync(s.laptop).waiting_on == std::set<std::string>{"fedora"});

    // The church, holding a file the others refuse, writes a snapshot of it
    // once, and the laptop catches up from it.
    s.church.engine.set_refused_by_others({{"fedora", 1}});
    CHECK(s.sync(s.church).snapshot_written);
    CHECK_FALSE(s.sync(s.church).snapshot_written);
    CHECK_FALSE(s.church.engine.snapshot_refused());
    const auto caught = s.sync(s.laptop);
    CHECK(caught.caught_up);
    CHECK(caught.waiting_on.empty());
    CHECK(caught.files_waiting == 0);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "F");
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'It Is Well With My Soul'") == 1);
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);

    // From there on the two sync as before, and the fedora's next file is
    // still refused.
    s.laptop.db.exec("UPDATE songs SET key = 'G' WHERE title = 'Amazing Grace'");
    s.settle();
    CHECK(s.church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "G");
    fedora.db.exec("UPDATE songs SET key = 'B' WHERE title = 'Amazing Grace'");
    REQUIRE(fedora.engine.sync().changes_sent == 1);
    s.link.computers.push_back(&fedora);
    s.link.all();
    CHECK(s.church.store.sequences(Numbered::Changes, "fedora") == std::vector<std::int64_t>{1});
    CHECK(s.laptop.store.sequences(Numbered::Changes, "fedora").empty());
}

TEST_CASE("A snapshot holds what was shared, not what is unsent here", "[sync]")
{
    Shared s;
    s.laptop.db.exec("UPDATE songs SET key = 'C' WHERE title = 'Amazing Grace'");
    // The church's file, as it left the song.
    REQUIRE(s.snapshot(s.laptop));
    REQUIRE(s.church.store.snapshots() == std::vector<SnapshotName>{{"laptop", 1}});
    const auto key_in = [&](const SnapshotName& name) {
        const auto snapshot = s.church.snapshot(name);
        const auto song = std::ranges::find_if(snapshot.records, [](const auto& r) {
            return r.table == "songs" && r.document && r.document->value("title", "") == "Amazing Grace";
        });
        REQUIRE(song != snapshot.records.end());
        return song->document->value("key", "");
    };
    CHECK(key_in({"laptop", 1}) == "G");
    // Sent, it is in the next.
    s.sync(s.laptop);
    CHECK(s.snapshot(s.laptop));
    CHECK(key_in({"laptop", 2}) == "C");
}

TEST_CASE("A snapshot is written once enough change files have come since the newest", "[sync]")
{
    Shared s;
    s.church.engine.set_snapshot_after(3);
    std::vector<bool> written;
    for (int i = 2; i <= 6; ++i) {
        s.church.db.exec("UPDATE songs SET key = '" + std::to_string(i) + "' WHERE id = 1");
        written.push_back(s.sync(s.church).snapshot_written);
    }
    // Files 2 to 6: due at the third file since none, and the third since that.
    CHECK(written == std::vector<bool>{false, true, false, false, true});
    CHECK(s.laptop.store.snapshots()
          == std::vector<SnapshotName>{{"church", 3}, {"church", 6}});
    CHECK_FALSE(s.sync(s.laptop).snapshot_written);  // 200 by default
}

TEST_CASE("A snapshot is due at once when there is none and change files were let go of", "[sync]")
{
    Shared s;
    CHECK_FALSE(s.sync(s.laptop).snapshot_written);  // none, and nothing let go of
    // As once snapshots of an earlier format are no more: old files went, and
    // no snapshot here holds them.
    s.laptop.store.drop_changes_through("church", 1);
    SECTION("an editor writes one")
    {
        CHECK(s.sync(s.laptop).snapshot_written);
        CHECK_FALSE(s.sync(s.laptop).snapshot_written);  // one is there now
        CHECK(s.church.store.snapshots() == std::vector<SnapshotName>{{"laptop", 1}});
    }
    SECTION("once what is here is sent")
    {
        s.laptop.db.exec("UPDATE songs SET key = 'C' WHERE title = 'Amazing Grace'");
        const auto result = s.sync(s.laptop);
        CHECK(result.changes_sent == 1);
        CHECK(result.snapshot_written);
        CHECK(s.church.store.snapshots() == std::vector<SnapshotName>{{"laptop", 2}});
    }
    SECTION("nor by a viewer, nor when snapshots are never due")
    {
        s.laptop.engine.set_writes_snapshots(false);
        CHECK_FALSE(s.sync(s.laptop).snapshot_written);
        s.laptop.engine.set_writes_snapshots(true);
        s.laptop.engine.set_snapshot_after(0);
        CHECK_FALSE(s.sync(s.laptop).snapshot_written);
    }
}

TEST_CASE("A second snapshot writes only the parts that changed, and touches the rest", "[sync]")
{
    Shared s;
    // Parts of some 32 KiB: about 60 of them, whatever the songs' random uids.
    s.church.engine.set_part_bytes(32 * 1024);
    add_songs(s.church, 1000);
    s.settle();
    REQUIRE(s.snapshot(s.church));
    const auto first = seventhslide::test::read_index(s.church.store, s.church.store.snapshots().back());
    REQUIRE(first.parts.size() >= 20);
    // Last wanted long ago.
    const auto long_ago = std::filesystem::file_time_type::clock::now() - std::chrono::days(30);
    for (const auto& part : s.church.store.parts()) {
        std::filesystem::last_write_time(s.church.root.path / "sync" / "snapshot_parts" / (part.sha256 + ".json"),
                                         long_ago);
    }

    s.church.db.exec("UPDATE songs SET key = 'E' WHERE id = 1500");
    s.settle();
    REQUIRE(s.snapshot(s.church));
    const auto second = seventhslide::test::read_index(s.church.store, s.church.store.snapshots().back());
    std::vector<std::string> made;
    for (const auto& part : second.parts) {
        if (std::ranges::find(first.parts, part) == first.parts.end()) {
            made.push_back(part.sha256);
        }
    }
    CHECK(!made.empty());
    CHECK(made.size() <= 2);  // the song's part, or it and the next
    // Each part it names was written or touched just now; one only the first
    // named was not.
    for (const auto& part : s.church.store.parts()) {
        const bool named = std::ranges::find(second.parts, part.sha256, &seventhslide::sync::SnapshotPartRef::sha256)
                           != second.parts.end();
        CHECK((part.touched > long_ago + std::chrono::days(1)) == named);
    }
    CHECK(s.laptop.store.parts().size() == s.church.store.parts().size());
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);
}

TEST_CASE("Two computers with the same records write the same parts, byte for byte", "[sync]")
{
    Shared s;
    s.church.engine.set_part_bytes(16 * 1024);
    s.laptop.engine.set_part_bytes(16 * 1024);
    add_songs(s.church, 300);
    // A Bible from the laptop, a part of its own.
    s.laptop.db.exec("INSERT INTO bibles (id, name) VALUES (2, 'Long Bible')");
    s.laptop.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 3000) "
                     "INSERT INTO verses (bible_id, book, chapter, verse_num, text) SELECT 2, 'Book ' || (i / 1000), "
                     "(i / 30) % 150 + 1, i % 30 + 1, 'In the beginning ' || i FROM n");
    s.laptop.db.exec("UPDATE songs SET key = 'G' WHERE id = 1005");
    s.settle();
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);

    // Each writes its own, neither having the other's.
    REQUIRE(s.church.engine.snapshot());
    REQUIRE(s.laptop.engine.snapshot());
    const auto index_of = [](Computer& c) {
        const auto names = c.store.snapshots();
        const auto own = std::ranges::find(names, c.machine, &SnapshotName::machine);
        REQUIRE(own != names.end());
        return seventhslide::test::read_index(c.store, *own);
    };
    const auto church = index_of(s.church);
    const auto laptop = index_of(s.laptop);
    CHECK(church.parts.size() > 10);
    CHECK(church.parts == laptop.parts);
    CHECK(church.records == laptop.records);
    CHECK(church.held == laptop.held);
    CHECK(church.media == laptop.media);
    for (const auto& part : church.parts) {
        CHECK(s.church.store.read_part(part.sha256) == s.laptop.store.read_part(part.sha256));
    }
    const auto snapshot = s.laptop.snapshot({"laptop", laptop.file_count()});
    const auto bible = std::ranges::find_if(snapshot.records, [](const seventhslide::sync::SnapshotRecord& r) {
        return r.table == "bibles" && r.document && r.document->at("name") == "Long Bible";
    });
    REQUIRE(bible != snapshot.records.end());
    CHECK(bible->document->at("verses").at("rows").size() == 3000);
}

TEST_CASE("A change still waiting goes in a snapshot, and gets through after joining from it",
          "[sync]")
{
    Shared s;
    send_from_office(s, 1, {new_item(s)});
    REQUIRE(s.sync(s.laptop).held.size() == 1);
    REQUIRE(s.snapshot(s.laptop));

    Computer pew{"pew"};
    s.add(pew);
    pew.engine.join(seventhslide::db::library_uid(s.church.db));
    const auto early = s.sync(pew);
    CHECK(early.files_applied == 0);
    CHECK(early.held == std::vector<std::string>{"service_items " + k_new_item});

    send_from_office(s, 2, {new_song()});
    CHECK(s.sync(pew).held.empty());
    CHECK(pew.count("SELECT COUNT(*) FROM service_items WHERE order_num = 9") == 1);
    s.sync(s.laptop);
    seventhslide::test::check_same_records(pew.db, s.laptop.db);
}

TEST_CASE("A computer joining again gets back what it sent that no snapshot holds", "[sync]")
{
    Shared s;
    s.laptop.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    s.settle();
    std::int64_t replayed = 0;
    SECTION("from a snapshot")
    {
        REQUIRE(s.snapshot(s.church));
        replayed = 1;  // its second file
    }
    SECTION("with no snapshot")
    {
        replayed = 3;  // the church's file and both of its own
    }
    // The laptop sends a change, and loses its library before anyone sees it.
    s.laptop.db.exec("UPDATE songs SET title = 'It Is Well With My Soul' "
                     "WHERE title = 'It Is Well'");
    s.sync(s.laptop);

    Computer again{"laptop", "Laptop"};
    s.add(again);
    again.engine.join(seventhslide::db::library_uid(s.church.db));
    const auto back = s.sync(again);
    CHECK(back.files_applied == replayed);
    CHECK(back.changes_sent == 0);
    CHECK(again.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "A");
    CHECK(again.count("SELECT COUNT(*) FROM songs WHERE title = 'It Is Well With My Soul'") == 1);

    // Its next file comes after the ones it sent before.
    again.db.exec("UPDATE songs SET key = 'B' WHERE title = 'Amazing Grace'");
    CHECK(s.sync(again).changes_sent == 1);
    CHECK(again.store.sequences(Numbered::Changes, "laptop") == std::vector<std::int64_t>{1, 2, 3});
    s.sync(s.church);
    seventhslide::test::check_same_records(again.db, s.church.db);
    CHECK(s.church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "B");
}

TEST_CASE("A computer joining again sends nothing until it has back what it sent", "[sync]")
{
    Shared s;
    send_from_office(s, 1, {new_song()});
    s.sync(s.laptop);
    s.laptop.db.exec("UPDATE songs SET key = 'F' WHERE title = 'Be Thou My Vision'");
    REQUIRE(s.sync(s.laptop).changes_sent == 1);  // made on top of the office's file

    // It joins again without the office's file, which its own was made on
    // top of.
    s.link.hidden = "office";
    Computer again{"laptop", "Laptop"};
    s.add(again);
    again.engine.join(seventhslide::db::library_uid(s.church.db));
    s.sync(again);  // its own file waits for the office's
    again.db.exec("UPDATE songs SET key = 'Bb' WHERE title = 'Amazing Grace'");
    CHECK(s.sync(again).changes_sent == 0);
    CHECK(again.store.sequences(Numbered::Changes, "laptop") == std::vector<std::int64_t>{1});

    s.link.hidden.clear();
    s.link.all();
    const auto caught_up = s.sync(again);
    CHECK(caught_up.files_applied == 2);
    CHECK(caught_up.changes_sent == 1);
    CHECK(again.store.sequences(Numbered::Changes, "laptop") == std::vector<std::int64_t>{1, 2});
    CHECK(again.text("SELECT key FROM songs WHERE title = 'Be Thou My Vision'") == "F");
    s.sync(s.church);
    CHECK(s.church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "Bb");
}

TEST_CASE("A copy made before joining again is not made twice", "[sync]")
{
    Shared s;
    s.church.db.exec("UPDATE songs SET lyrics = '[V1]\nchurch words' WHERE id = 1");
    s.laptop.db.exec("UPDATE songs SET lyrics = '[V1]\nlaptop words' "
                     "WHERE title = 'Amazing Grace'");
    s.link.hidden = "church";
    s.sync(s.church);
    s.sync(s.laptop);
    s.link.hidden.clear();
    s.sync(s.church);  // its sent change lost: it makes the copy, and sends it
    s.sync(s.laptop);
    REQUIRE(s.laptop.count("SELECT COUNT(*) FROM songs") == 3);

    // Replaying, the church comes to the same disagreement, and the same copy.
    Computer again{"church", "Church PC"};
    s.add(again);
    again.engine.join(seventhslide::db::library_uid(s.church.db));
    CHECK(s.sync(again).changes_sent == 0);
    CHECK(again.count("SELECT COUNT(*) FROM songs") == 3);
    seventhslide::test::check_same_records(again.db, s.laptop.db);
}

TEST_CASE("A snapshot from another library or a newer SeventhSlide is refused", "[sync]")
{
    Shared s;
    seventhslide::test::Snapshot snapshot;
    snapshot.library = seventhslide::db::library_uid(s.church.db);
    snapshot.machine = "office";
    SECTION("another library")
    {
        snapshot.library = "00000000000000000000000000000000";
        Computer pew{"pew"};
        seventhslide::test::write_snapshot(pew.store, {"office", 9}, snapshot, identity("office").key);
        const auto before = seventhslide::db::library_uid(pew.db);
        CHECK_THROWS_WITH(pew.engine.join(seventhslide::db::library_uid(s.church.db)),
                          Catch::Matchers::ContainsSubstring("another library"));
        CHECK(seventhslide::db::library_uid(pew.db) == before);
    }
    SECTION("a newer SeventhSlide")
    {
        snapshot.schema = seventhslide::db::k_schema_version + 1;
        Computer pew{"pew"};
        seventhslide::test::write_snapshot(pew.store, {"office", 9}, snapshot, identity("office").key);
        CHECK_THROWS_WITH(pew.engine.join(snapshot.library),
                          Catch::Matchers::ContainsSubstring("newer version of SeventhSlide"));
    }
}

TEST_CASE("A snapshot without what every one says is set aside, not a crash", "[sync]")
{
    Shared s;
    REQUIRE(s.snapshot(s.church));
    Computer office{"office", "Office"};
    s.add(office);
    const auto name = office.store.snapshots().front();
    const auto index_file = office.root.path / "sync" / "snapshot_index" / name.machine
                            / (std::string(10 - std::to_string(name.files).size(), '0') + std::to_string(name.files)
                               + ".json");
    REQUIRE(std::filesystem::exists(index_file));
    auto j = seventhslide::sync::unseal(office.store.read_snapshot(name), "A test").body;
    const auto part = j.at("parts").at(0).at("sha256").get<std::string>();
    const auto part_file = office.root.path / "sync" / "snapshot_parts" / (part + ".json");
    const auto rewrite = [&] {
        std::ofstream(index_file, std::ios::binary | std::ios::trunc) << sealed(identity(name.machine), j);
    };
    bool part_set_aside = false;
    SECTION("no schema")
    {
        j.erase("schema");
        CHECK_THROWS_AS(seventhslide::sync::snapshot_index_from_json(j), std::runtime_error);
        rewrite();
    }
    SECTION("no computer")
    {
        j.erase("computer");
        rewrite();
    }
    SECTION("no time")
    {
        j.erase("time");
        rewrite();
    }
    SECTION("counts that are not what its parts hold")
    {
        j["records"] = j.at("records").get<std::int64_t>() + 1;
        rewrite();
    }
    SECTION("a part not what it names")
    {
        std::ofstream(part_file, std::ios::binary | std::ios::app) << " ";
        part_set_aside = true;
    }
    SECTION("a part gone")
    {
        std::filesystem::remove(part_file);
    }
    // One joining takes the files instead.
    REQUIRE_NOTHROW(office.engine.join(s.library));
    CHECK(office.store.snapshots().empty());
    CHECK(std::filesystem::exists(office.root.path / "sync" / "damaged" / ("part-" + part)) == part_set_aside);
    s.sync(office);
    seventhslide::test::check_same_records(office.db, s.church.db);
}

TEST_CASE("A snapshot holding a record this library cannot take says so", "[sync]")
{
    Shared s;
    REQUIRE(s.snapshot(s.church));
    const auto name = s.church.store.snapshots().back();
    auto snapshot = s.church.snapshot(name);
    const auto song = std::ranges::find_if(snapshot.records, [](const auto& r) { return r.table == "songs"; });
    REQUIRE(song != snapshot.records.end());
    REQUIRE(song->document);
    (*song->document)["no_such_field"] = 1;
    Computer pew{"pew"};
    seventhslide::test::write_snapshot(pew.store, name, snapshot, identity(name.machine).key);
    using Catch::Matchers::ContainsSubstring;
    CHECK_THROWS_WITH(pew.engine.join(snapshot.library),
                      ContainsSubstring("could not be applied: a record in it is not one this library can take")
                          && ContainsSubstring("songs has no field no_such_field")
                          && !ContainsSubstring("needs a record"));
}

TEST_CASE("A viewer's changes go nowhere, and a removed computer's stop", "[sync]")
{
    Shared s;
    Computer pew{"pew", "Pew tablet"};
    s.link.computers.push_back(&pew);
    s.invite(pew, Role::Viewer);
    pew.engine.join(s.library);
    REQUIRE(s.sync(pew).files_applied == 1);
    seventhslide::test::check_same_records(pew.db, s.church.db);
    // It got everything but the locked admin key.
    CHECK(pew.store.admin_keys().empty());
    CHECK(s.laptop.store.admin_keys().size() == 1);

    // A change it makes anyway (the app opens its library read-only) reaches
    // nobody.
    pew.db.exec("UPDATE songs SET key = 'Z' WHERE title = 'Amazing Grace'");
    CHECK(s.sync(pew).changes_sent == 1);
    CHECK(s.church.store.sequences(Numbered::Changes, "pew").empty());
    REQUIRE_FALSE(s.link.refused.empty());
    CHECK_THAT(s.link.refused.front(), Catch::Matchers::ContainsSubstring("may not change"));
    s.sync(s.church);
    CHECK(s.church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "G");

    SECTION("a removed laptop's later files")
    {
        s.laptop.db.exec("UPDATE songs SET key = 'E' WHERE title = 'Amazing Grace'");
        REQUIRE(s.sync(s.laptop).changes_sent == 1);
        const auto before = s.church.store.sequences(Numbered::Changes, "laptop");
        s.church.members(s.library).remove("laptop", s.admin());
        s.link.all();
        s.link.refused.clear();
        CHECK(s.laptop.members(s.library).members().find("laptop")->removed);

        s.laptop.db.exec("UPDATE songs SET key = 'F' WHERE title = 'Amazing Grace'");
        REQUIRE(s.sync(s.laptop).changes_sent == 1);
        CHECK(s.church.store.sequences(Numbered::Changes, "laptop") == before);
        CHECK(pew.store.sequences(Numbered::Changes, "laptop") == before);
        CHECK_FALSE(s.link.refused.empty());
        s.sync(s.church);
        CHECK(s.church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "E");
    }
    SECTION("a forged file")
    {
        // A computer on the network that is no member, offering a file under
        // the laptop's name and one under its own.
        Computer forger{"forger"};
        s.link.computers.push_back(&forger);
        auto j = seventhslide::sync::change_file_to_json(s.church.change_file("church", 1));
        j["machine"] = "laptop";
        j["sequence"] = 7;
        forger.store.write(Numbered::Changes, "laptop", 7, sealed(forger.me, j));
        j["machine"] = "forger";
        j["sequence"] = 1;
        forger.store.write(Numbered::Changes, "forger", 1, sealed(forger.me, j));
        s.link.refused.clear();
        s.link.all();
        CHECK(s.church.store.sequences(Numbered::Changes, "laptop").empty());
        CHECK(s.church.store.sequences(Numbered::Changes, "forger").empty());
        // (The pew's file is refused again on every pass, among these.)
        const auto refused_for = [&](const std::string& why) {
            return std::ranges::any_of(s.link.refused, [&](const std::string& message) {
                return message.find(why) != std::string::npos;
            });
        };
        CHECK(refused_for("not that computer's"));
        CHECK(refused_for("does not know as a member"));
    }
}

TEST_CASE("A computer demoted while another built on its files: every computer takes them",
          "[sync]")
{
    Shared s;
    Computer office{"office", "Office"};
    s.add(office);
    office.engine.join(s.library);
    REQUIRE(s.sync(office).files_applied == 1);

    // The laptop's change reaches the office, not the church, and the
    // office builds on it.
    s.link.hidden = "laptop";
    s.laptop.db.exec("UPDATE songs SET key = 'E' WHERE title = 'Amazing Grace'");
    REQUIRE(s.laptop.engine.sync().changes_sent == 1);
    office.store.write(Numbered::Changes, "laptop", 1, s.laptop.store.read(Numbered::Changes, "laptop", 1));
    REQUIRE(office.engine.sync().files_applied == 1);
    office.db.exec("UPDATE services SET name = 'Vespers'");
    REQUIRE(office.engine.sync().changes_sent == 1);
    CHECK(office.change_file("office", 1).seen.at("laptop") == 1);

    // Meanwhile the church, which has none of the laptop's files, makes it
    // a viewer: no file of its counts by the list.
    s.church.members(s.library).set_role("laptop", Role::Viewer, s.admin());
    REQUIRE(s.members().find("laptop")->files_kept == 0);
    s.link.hidden.clear();
    s.link.all();

    // The office built on the laptop's file, so it counts everywhere: the
    // church takes it, and nothing waits.
    CHECK(s.church.store.sequences(Numbered::Changes, "laptop") == std::vector<std::int64_t>{1});
    const auto result = s.sync(s.church);
    CHECK(result.files_waiting == 0);
    CHECK(s.church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "E");
    CHECK(s.church.text("SELECT name FROM services") == "Vespers");
    seventhslide::test::check_same_records(s.church.db, office.db);

    // A file of the laptop's that nobody built on stays out.
    s.laptop.db.exec("UPDATE songs SET key = 'F' WHERE title = 'Amazing Grace'");
    REQUIRE(s.sync(s.laptop).changes_sent == 1);
    CHECK(s.church.store.sequences(Numbered::Changes, "laptop") == std::vector<std::int64_t>{1});
}

TEST_CASE("A change file with values of the wrong kind is refused, not a crash", "[sync]")
{
    Shared s;
    auto j = seventhslide::sync::change_file_to_json(s.church.change_file("church", 1));
    j["machine"] = "office";
    SECTION("a name that is not text")
    {
        j["computer"] = 5;
    }
    SECTION("a counter that would overflow")
    {
        j["counter"] = std::numeric_limits<std::int64_t>::max();
    }
    SECTION("a seen naming no computer")
    {
        j["seen"] = {{"../..", 1}};
    }
    SECTION("deleted that is not true or false")
    {
        j["changes"][0]["deleted"] = "yes";
    }
    SECTION("no schema")
    {
        j.erase("schema");
    }
    SECTION("no computer")
    {
        j.erase("computer");
    }
    SECTION("no time")
    {
        j.erase("time");
    }
    CHECK_THROWS_AS(seventhslide::sync::change_file_from_json(j), std::runtime_error);
    s.laptop.store.write(Numbered::Changes, "office", 1, sealed(identity("office"), j));
    // Never applied: set aside as damaged, to be fetched whole (the gate keeps
    // such a file out of a store; one here was damaged on the disk).
    seventhslide::sync::SyncResult result;
    REQUIRE_NOTHROW(result = s.laptop.engine.sync());
    CHECK(result.files_applied == 0);
    CHECK(s.laptop.store.sequences(Numbered::Changes, "office").empty());
}

TEST_CASE("The password changes on one computer, and the others follow", "[sync]")
{
    Shared s;
    CHECK(s.laptop.members(s.library).unlock_admin(k_passphrase) == s.admin());
    const auto changed = s.laptop.members(s.library)
                             .replace_admin("roses of sharon", s.admin(),
                                            seventhslide::crypto::k_box_params_quick);
    s.link.all();
    CHECK(s.church.store.admin_keys().size() == 2);
    CHECK(*s.members().admin == changed.admin.public_key());
    CHECK(s.church.members(s.library).unlock_admin("roses of sharon") == changed.admin);
    CHECK_FALSE(s.church.members(s.library).unlock_admin(k_passphrase).has_value());

    // An invite made with the old password admits nobody now.
    Computer office{"office"};
    s.link.computers.push_back(&office);
    office.members(s.library).join(seventhslide::sync::read_invite(
        s.church.members(s.library).invite(Role::Editor, k_expires, s.admin())));
    s.link.all();
    CHECK(s.members().find("office") == nullptr);
    // One made with the new one does.
    office.members(s.library).join(seventhslide::sync::read_invite(
        s.church.members(s.library).invite(Role::Editor, k_expires, changed.admin)));
    s.link.all();
    REQUIRE(s.members().find("office") != nullptr);
    office.engine.join(s.library);
    CHECK(s.sync(office).files_applied == 1);
}

// Media files.

TEST_CASE("A font arrives with its record, and goes with it", "[sync]")
{
    Shared s;
    s.church.db.exec("INSERT INTO font_files (filename, display_name, family, style, bold, added_at) "
                     "VALUES ('abcd.ttf', 'Lato Bold', 'Lato', 'Bold', 1, 1000)");
    s.church.put_file("fonts/abcd.ttf", "a font");
    s.sync(s.church);
    CHECK(s.church.change_file("church", 2).media
          == std::vector<MediaDigest>{{"fonts/abcd.ttf", 6, root_of("a font")}});
    // The link brought the file before the change naming it.
    CHECK(s.laptop.file("fonts/abcd.ttf") == "a font");
    CHECK(s.sync(s.laptop).files_applied == 1);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM font_files WHERE family = 'Lato' AND bold = 1 "
                         "AND added_at = 1000")
          == 1);

    s.church.db.exec("DELETE FROM font_files WHERE filename = 'abcd.ttf'");
    std::filesystem::remove(s.church.media / "fonts/abcd.ttf");
    s.settle();
    CHECK_FALSE(s.laptop.has_file("fonts/abcd.ttf"));
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A media file added later arrives with its record, and goes with it", "[sync]")
{
    Shared s;
    s.church.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('gh78.png', 'Cross')");
    s.church.put_file("images/gh78.png", "cross");
    s.sync(s.church);
    CHECK(s.church.change_file("church", 2).media
          == std::vector<MediaDigest>{{"images/gh78.png", 5, root_of("cross")}});
    // The link brought the file before the change naming it.
    CHECK(s.laptop.file("images/gh78.png") == "cross");
    CHECK(s.sync(s.laptop).files_applied == 1);

    // Deleted on the church PC, as the library deletes one: the row, then the
    // file.
    s.church.db.exec("DELETE FROM image_files WHERE filename = 'gh78.png'");
    std::filesystem::remove(s.church.media / "images/gh78.png");
    s.settle();
    CHECK_FALSE(s.laptop.has_file("images/gh78.png"));
    CHECK(s.laptop.has_file("images/ab12.png"));
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A theme's background arrives with its output, and the one it replaced goes",
          "[sync]")
{
    Shared s;
    const auto set_background = [&](const std::string& name) {
        s.church.db.exec("UPDATE outputs SET data = json_set(data, '$.bg', "
                         "'/static/theme_backgrounds/" + name + "') WHERE data LIKE '%\"main\"%'");
    };
    s.church.put_file("theme_backgrounds/main.bg.11111111.png", "dawn");
    set_background("main.bg.11111111.png");
    s.settle();
    CHECK(s.church.change_file("church", 2).media
          == std::vector<MediaDigest>{{"theme_backgrounds/main.bg.11111111.png", 4,
                                       root_of("dawn")}});
    CHECK(s.laptop.file("theme_backgrounds/main.bg.11111111.png") == "dawn");

    s.church.put_file("theme_backgrounds/main.bg.22222222.png", "dusk");
    set_background("main.bg.22222222.png");
    std::filesystem::remove(s.church.media / "theme_backgrounds/main.bg.11111111.png");
    s.settle();
    CHECK(s.laptop.file("theme_backgrounds/main.bg.22222222.png") == "dusk");
    CHECK_FALSE(s.laptop.has_file("theme_backgrounds/main.bg.11111111.png"));
    CHECK(missing_media(s.laptop.engine).empty());
}

TEST_CASE("A file lost before it was ever sent is missing, and arrives once someone has it",
          "[sync]")
{
    Shared s;
    // Registered on the church PC with its file already lost: nothing can
    // declare it.
    s.church.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('ij90.png', 'Lost')");
    CHECK(s.sync(s.church).media_missing
          == std::vector<MediaDigest>{{"images/ij90.png", 0, ""}});
    CHECK(s.church.change_file("church", 2).media.empty());
    const auto first = s.sync(s.laptop);
    CHECK(first.files_applied == 1);  // the record still arrives
    CHECK(paths_of(first.media_missing) == std::vector<std::string>{"images/ij90.png"});

    // The church PC gets the file back (from a backup); the link brings it.
    s.church.put_file("images/ij90.png", "found");
    s.link.all();
    CHECK(s.laptop.file("images/ij90.png") == "found");
    CHECK(s.sync(s.laptop).media_missing.empty());
}

TEST_CASE("A picture deleted while another computer puts it in a service comes back", "[sync]")
{
    Shared s;
    s.church.db.exec("DELETE FROM image_folder_items WHERE filename = 'ab12.png'");
    s.church.db.exec("DELETE FROM image_files WHERE filename = 'ab12.png'");
    std::filesystem::remove(s.church.media / "images/ab12.png");
    s.laptop.db.exec(R"(INSERT INTO service_items (service_id, order_num, item_type, data)
                        VALUES (1, 40960, 'image', '{"filename":"ab12.png","title":"Lilies"}'))");
    SECTION("the delete is sent first") { s.settle(); }
    SECTION("the service item is sent first")
    {
        s.sync(s.laptop);
        s.sync(s.church);
        s.sync(s.laptop);
        s.sync(s.church);
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    CHECK(s.church.count("SELECT COUNT(*) FROM image_files WHERE filename = 'ab12.png'") == 1);
    CHECK(s.church.file("images/ab12.png") == "lilies");
    CHECK(s.laptop.file("images/ab12.png") == "lilies");
}

TEST_CASE("A song titled as a picture's file does not keep the picture from a delete", "[sync]")
{
    Shared s;
    // A title is text, not a use of the file it reads as.
    s.church.db.exec("UPDATE songs SET title = 'ab12.png' WHERE id = 1");
    s.settle();
    // Changed on the church and not sent, while the laptop deletes the picture.
    s.church.db.exec("UPDATE songs SET key = 'D' WHERE id = 1");
    s.laptop.db.exec("DELETE FROM image_folder_items WHERE filename = 'ab12.png'");
    s.laptop.db.exec("DELETE FROM image_files WHERE filename = 'ab12.png'");
    s.sync(s.laptop);
    s.sync(s.church);
    CHECK(s.church.count("SELECT COUNT(*) FROM image_files WHERE filename = 'ab12.png'") == 0);
    CHECK_FALSE(s.church.has_file("images/ab12.png"));
    CHECK(s.church.text("SELECT key FROM songs WHERE id = 1") == "D");
    s.settle();
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A snapshot declares every file declared to the computer that wrote it", "[sync]")
{
    Shared s;
    // A deleted record's file stays declared: an edit elsewhere may bring the
    // record back, and a computer joining from the snapshot must know the file.
    s.church.db.exec("DELETE FROM audio_folder_items WHERE filename = 'ef56.mp3'");
    s.church.db.exec("DELETE FROM audio_files WHERE filename = 'ef56.mp3'");
    s.settle();
    REQUIRE(s.snapshot(s.laptop));
    const auto snapshot = s.laptop.snapshot({"laptop", 2});
    // Each with the version of the church's first file, which declared it.
    const auto first = s.church.change_file("church", 1).version();
    CHECK(std::ranges::all_of(snapshot.media, [&](const MediaDigest& m) { return m.version == first; }));
    CHECK(unversioned(snapshot.media)
          == std::vector<MediaDigest>{{"audio/ef56.mp3", 7, root_of("prelude")},
                                      {"images/ab12.png", 6, root_of("lilies")},
                                      {"videos/cd34.mp4", 7, root_of("welcome")}});
}

TEST_CASE("The first declaration of a file stands, under the earliest version", "[sync]")
{
    Shared s;
    seventhslide::db::SyncBookkeeping book(s.laptop.db);
    const auto first = s.church.change_file("church", 1).version();
    // Later: its bytes do not stand, nor its version.
    book.set_media_digest({"images/ab12.png", 1, root_of("not lilies"), {{first.counter + 5, "office"}}});
    CHECK(book.media_digest("images/ab12.png") == MediaDigest{"images/ab12.png", 6, root_of("lilies"), first});
    // Earlier (it reached this computer late): its version is the file's.
    const seventhslide::db::SyncVersion earlier{first.counter - 1, "office"};
    book.set_media_digest({"images/ab12.png", 1, root_of("not lilies"), earlier});
    CHECK(book.media_digest("images/ab12.png") == MediaDigest{"images/ab12.png", 6, root_of("lilies"), earlier});
}

TEST_CASE("A change file declaring a file it cannot is refused", "[sync]")
{
    Shared s;
    auto j = seventhslide::sync::change_file_to_json(s.church.change_file("church", 1));
    j["machine"] = "office";
    SECTION("a path that leaves the folder")
    {
        j["media"] = json::array({{{"path", "images/../../seventhslide.db"},
                                   {"size", 1},
                                   {"root", root_of("x")}}});
    }
    SECTION("a fingerprint that is not one")
    {
        j["media"] = json::array({{{"path", "images/zz99.png"}, {"size", 1}, {"root", ""}}});
    }
    s.laptop.store.write(Numbered::Changes, "office", 1, sealed(identity("office"), j));
    seventhslide::sync::SyncResult result;
    REQUIRE_NOTHROW(result = s.laptop.engine.sync());
    CHECK(result.files_applied == 0);
    CHECK(s.laptop.store.sequences(Numbered::Changes, "office").empty());  // set aside
}

namespace {

/// The paths of ``use``'s files over the limit.
std::vector<std::string> over_paths(const seventhslide::sync::MediaUse& use)
{
    std::vector<std::string> out;
    for (const auto& o : use.over) {
        out.push_back(o.path);
    }
    return out;
}

/// ``limit`` set on the list by the admin, and taken by both computers'
/// engines (as LibrarySync takes it from the list at each sync).
void limit_to(Shared& s, std::int64_t limit)
{
    s.church.members(s.library).set_size_limit(limit, s.admin());
    s.link.all();
    REQUIRE(s.members().size_limit == limit);
    s.church.engine.set_size_limit(limit);
    s.laptop.engine.set_size_limit(limit);
}

}  // namespace

TEST_CASE("A file imported over the size limit stays here, with what names it, until there is room",
          "[sync][limit]")
{
    Shared s;
    limit_to(s, 25);  // the library's three files take 20 bytes
    CHECK(s.church.engine.media_use().used == 20);
    CHECK(s.church.engine.media_use().over.empty());

    // 10 bytes more would be 30.
    s.church.put_file("images/gh78.png", "lilies two");
    s.church.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('gh78.png', 'Lilies 2')");
    s.church.db.exec("INSERT INTO image_folder_items (folder_id, filename, sort_order) VALUES (2, 'gh78.png', 9)");
    s.church.db.exec("UPDATE songs SET key = 'A' WHERE id = 1");  // and something that fits
    const auto sent = s.sync(s.church);
    CHECK(sent.changes_sent == 1);
    const auto use = sent.media;
    REQUIRE(use.over.size() == 1);
    CHECK(use.over.front().path == "images/gh78.png");
    CHECK(use.over.front().title == "Lilies 2");
    CHECK(use.over.front().here);
    CHECK(use.used == 20);
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "A");
    CHECK(s.laptop.count("SELECT COUNT(*) FROM image_files WHERE filename = 'gh78.png'") == 0);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM image_folder_items WHERE filename = 'gh78.png'") == 0);
    CHECK_FALSE(s.laptop.has_file("images/gh78.png"));
    // Still marked here: it goes once there is room.
    CHECK_FALSE(seventhslide::db::marked_records(s.church.db).empty());

    limit_to(s, 40);
    CHECK(s.sync(s.church).changes_sent == 2);
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM image_folder_items WHERE filename = 'gh78.png'") == 1);
    CHECK(s.laptop.file("images/gh78.png") == "lilies two");
    CHECK(s.laptop.engine.media_use().used == 30);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("Every computer takes the same files under the size limit, in the order they were declared",
          "[sync][limit]")
{
    Shared s;
    limit_to(s, 26);  // 20 used: one more 4-byte file fits, not two
    // Each imports one while apart: each fits as each computer sees it.
    s.church.put_file("images/aa11.png", "aaaa");
    s.church.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('aa11.png', 'A')");
    s.laptop.put_file("images/bb22.png", "bbbb");
    s.laptop.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('bb22.png', 'B')");
    s.link.hidden = "church";  // the church's file reaches no one yet
    CHECK(s.sync(s.church).changes_sent == 1);
    CHECK(s.sync(s.laptop).changes_sent == 1);
    s.link.hidden.clear();
    s.settle();

    // Together they go over: both records are kept, and the file declared
    // later is the one over, on both computers alike.
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    const auto church = s.church.engine.media_use();
    const auto laptop = s.laptop.engine.media_use();
    REQUIRE(church.over.size() == 1);
    CHECK(over_paths(church) == over_paths(laptop));
    CHECK(church.used == 24);
    CHECK(laptop.used == 24);
    // Not fetched by the one without it.
    for (auto* c : {&s.church, &s.laptop}) {
        CHECK(std::ranges::none_of(missing_media(c->engine),
                                   [&](const MediaDigest& m) { return m.path == church.over.front().path; }));
    }

    // A snapshot keeps the order: each file with the version that declared it.
    REQUIRE(s.snapshot(s.church));
    const auto snapshot = s.church.snapshot(s.church.store.snapshots().front());
    CHECK(std::ranges::all_of(snapshot.media, [](const MediaDigest& m) { return m.version.has_value(); }));
}

TEST_CASE("A computer away while old files went catches up from the newest snapshot, keeping what "
          "it had not sent",
          "[sync][catchup]")
{
    Shared s;
    limit_to(s, 25);
    const auto amazing = s.laptop.text("SELECT id FROM songs WHERE title = 'Amazing Grace'");

    // The laptop goes away: nothing passes either way.
    s.link.computers = {&s.church};
    s.laptop.db.exec("UPDATE songs SET key = 'E' WHERE title = 'It Is Well'");
    s.laptop.db.exec("UPDATE services SET name = 'Vespers'");
    // A file the size limit keeps here, and its folder entry.
    s.laptop.put_file("images/gh78.png", "lilies two");
    s.laptop.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('gh78.png', 'Lilies 2')");
    s.laptop.db.exec("INSERT INTO image_folder_items (folder_id, filename, sort_order) VALUES (2, 'gh78.png', 9)");

    // Meanwhile at church: changes, and a delete of a song the laptop edits.
    s.church.db.exec("UPDATE songs SET title = 'Amazing Grace (My Chains)' WHERE title = 'Amazing Grace'");
    s.sync(s.church);
    s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('How Great Thou Art', '[V1]\nline')");
    s.church.db.exec("UPDATE services SET name = 'Divine Service'");
    s.sync(s.church);
    s.church.db.exec("DELETE FROM songs WHERE title = 'It Is Well'");
    s.sync(s.church);
    // In one part, and in a part for each record, nearly.
    const auto part_bytes = GENERATE(seventhslide::sync::k_snapshot_part_bytes, std::int64_t{64});
    CAPTURE(part_bytes);
    s.church.engine.set_part_bytes(part_bytes);
    REQUIRE(s.snapshot(s.church));
    // The files the snapshot holds go, as old files do once every member
    // has them: the laptop, not heard from for long, holds nothing up.
    const auto old = seventhslide::sync::old_files(
        s.church.store, s.members(), "church", seventhslide::db::SyncBookkeeping(s.church.db).applied(),
        {{"laptop", {{{"church", 1}}, "2026-01-01T00:00:00Z"}}}, "2026-06-01T00:00:00Z");
    REQUIRE(old.changes_through.contains("church"));
    seventhslide::sync::let_go(s.church.store, old);
    REQUIRE(s.church.store.sequences(Numbered::Changes, "church").empty());
    // And one after the snapshot, which the laptop takes as a file.
    s.church.db.exec("UPDATE songs SET key = 'D' WHERE title = 'How Great Thou Art'");
    s.sync(s.church);

    // The laptop is back.
    s.link.computers = {&s.church, &s.laptop};
    s.link.all();
    const auto back = s.sync(s.laptop);
    CHECK(back.caught_up);
    s.settle();
    s.settle();

    // What came while it was away, under the ids it had.
    CHECK(s.laptop.text("SELECT id FROM songs WHERE title = 'Amazing Grace (My Chains)'") == amazing);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'How Great Thou Art'") == "D");
    // What it had not sent: its edit brought the deleted song back, on both.
    CHECK(s.church.text("SELECT key FROM songs WHERE title = 'It Is Well'") == "E");
    // A name changed on both: one wins, the same on both.
    CHECK(s.church.text("SELECT name FROM services") == s.laptop.text("SELECT name FROM services"));
    // What the size limit keeps here is still here, and still waits.
    CHECK(s.church.count("SELECT COUNT(*) FROM image_files WHERE filename = 'gh78.png'") == 0);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM image_folder_items WHERE filename = 'gh78.png'") == 1);
    CHECK(s.laptop.has_file("images/gh78.png"));
    CHECK(s.laptop.file("images/ab12.png") == "lilies");  // nothing it still owns was let go
    limit_to(s, 40);
    s.settle();
    CHECK(s.church.count("SELECT COUNT(*) FROM image_folder_items WHERE filename = 'gh78.png'") == 1);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

namespace {

/// The laptop goes away for more than 90 days (``k_stale_days``) while
/// ``meanwhile`` changes the church's library; the church writes a snapshot
/// in parts of about ``part_bytes`` and lets go of the files it holds; the
/// laptop is back, and what it missed is only in the snapshot.
void away_while(Shared& s, const std::function<void()>& meanwhile, std::int64_t part_bytes)
{
    s.link.computers = {&s.church};
    meanwhile();
    s.sync(s.church);
    s.church.engine.set_part_bytes(part_bytes);
    REQUIRE(s.snapshot(s.church));
    const auto old = seventhslide::sync::old_files(
        s.church.store, s.members(), "church", seventhslide::db::SyncBookkeeping(s.church.db).applied(),
        {{"laptop", {{{"church", 1}}, "2026-01-01T00:00:00Z"}}}, "2026-04-15T00:00:00Z");
    REQUIRE(old.changes_through.contains("church"));
    seventhslide::sync::let_go(s.church.store, old);
    s.link.computers = {&s.church, &s.laptop};
    s.link.all();
}

/// Where among the parts of ``c``'s newest snapshot ``table``'s record
/// ``key`` is.
std::size_t part_holding(Computer& c, const std::string& table, const std::string& key)
{
    const auto names = c.store.snapshots();
    const auto index = seventhslide::test::read_index(c.store, names.back());
    for (std::size_t i = 0; i < index.parts.size(); ++i) {
        const auto part = seventhslide::sync::read_snapshot_part(c.store.read_part(index.parts[i].sha256),
                                                                 index.parts[i].sha256);
        if (std::ranges::any_of(part.records, [&](const auto& r) { return r.table == table && r.key == key; })) {
            return i;
        }
    }
    FAIL("no part holds " << table << " " << key);
    return 0;
}

/// Every record in ``db``, and all it keeps about syncing: what a write
/// that is rolled back leaves as it was.
std::string everything(Computer& c)
{
    std::string out;
    for (const auto table : seventhslide::db::record_tables_in_order()) {
        auto keys = seventhslide::db::record_keys(c.db, table);
        std::ranges::sort(keys);
        for (const auto& key : keys) {
            out += std::string(table) + " " + key + " " + seventhslide::db::read_record(c.db, table, key)->dump()
                   + "\n";
        }
    }
    for (const char* table : {"sync_shared", "sync_applied", "sync_state", "sync_outbox", "sync_held", "sync_media"}) {
        QSqlQuery q(c.db.handle());
        REQUIRE(q.exec(QStringLiteral("SELECT * FROM %1 ORDER BY 1, 2").arg(QString::fromLatin1(table))));
        while (q.next()) {
            out += table;
            for (int i = 0; i < q.record().count(); ++i) {
                out += " " + q.value(i).toString().toStdString();
            }
            out += "\n";
        }
    }
    return out;
}

const std::string k_parent_folder = "ffffffffffffffffffffffffffffff01";
const std::string k_child_folder = "00000000000000000000000000000003";
const std::string k_grandchild_folder = "00000000000000000000000000000002";

/// A folder, a folder in it and one in that, whose keys put each before the
/// folder it is in, with a picture in the last; and a service item that
/// plays after one whose key comes after it.
void add_folders_backwards(Computer& c)
{
    c.db.exec("INSERT INTO image_folders (id, name, uid) VALUES (10, 'Seasons', '" + k_parent_folder + "')");
    c.db.exec("INSERT INTO image_folders (id, name, parent_id, uid) VALUES (11, 'Spring', 10, '" + k_child_folder
              + "')");
    c.db.exec("INSERT INTO image_folders (id, name, parent_id, uid) VALUES (12, 'April', 11, '"
              + k_grandchild_folder + "')");
    c.db.exec("INSERT INTO image_folder_items (folder_id, filename, sort_order) VALUES (12, 'ab12.png', 1)");
    c.db.exec("INSERT INTO service_items (id, service_id, order_num, item_type, uid) VALUES (20, 1, 5, 'song', "
              "'fffffffffffffffffffffffffffffff4')");
    c.db.exec(R"(INSERT INTO service_items (service_id, order_num, item_type, data, uid) VALUES
              (1, 6, 'image_folder', '{"folder_id":12,"also_play":[20]}', '00000000000000000000000000000005'))");
}

/// The name of ``c``'s image folder ``uid``'s parent; empty for none.
std::string parent_of(Computer& c, const std::string& uid)
{
    return c.text("SELECT coalesce((SELECT p.name FROM image_folders f JOIN image_folders p ON p.id = f.parent_id "
                  "WHERE f.uid = '" + uid + "'), '')");
}

}  // namespace

TEST_CASE("A computer joining from a snapshot in parts takes a folder before the folder it is in",
          "[sync][parts]")
{
    Shared s;
    add_folders_backwards(s.church);
    s.settle();
    s.church.engine.set_part_bytes(64);  // a part for each record, nearly
    REQUIRE(s.snapshot(s.church));
    // Each folder comes in a part before the one it is in.
    CHECK(part_holding(s.church, "image_folders", k_grandchild_folder)
          < part_holding(s.church, "image_folders", k_child_folder));
    CHECK(part_holding(s.church, "image_folders", k_child_folder)
          < part_holding(s.church, "image_folders", k_parent_folder));

    Computer office{"office"};
    s.add(office);
    office.engine.join(s.library);
    CHECK(parent_of(office, k_grandchild_folder) == "Spring");
    CHECK(parent_of(office, k_child_folder) == "Seasons");
    CHECK(office.count("SELECT COUNT(*) FROM image_folder_items i JOIN image_folders f ON f.id = i.folder_id "
                       "WHERE f.name = 'April'")
          == 1);
    CHECK(office.text("SELECT json_extract(data, '$.also_play[0]') FROM service_items WHERE order_num = 6")
          == office.text("SELECT id FROM service_items WHERE order_num = 5"));
    seventhslide::test::check_same_records(office.db, s.church.db);
    // Nothing is left to send: every record took its shared copy.
    CHECK(s.sync(office).changes_sent == 0);
}

TEST_CASE("A computer catching up from a snapshot in parts takes a new folder before the new folder it is in",
          "[sync][parts][catchup]")
{
    Shared s;
    const auto easter = s.church.text("SELECT uid FROM image_folders WHERE name = 'Easter'");
    away_while(
        s,
        [&] {
            add_folders_backwards(s.church);
            // And a folder the laptop has moves into the new ones.
            s.church.db.exec("UPDATE image_folders SET parent_id = 12 WHERE name = 'Easter'");
        },
        64);
    REQUIRE(part_holding(s.church, "image_folders", k_child_folder)
            < part_holding(s.church, "image_folders", k_parent_folder));
    CHECK(s.sync(s.laptop).caught_up);
    CHECK(parent_of(s.laptop, k_grandchild_folder) == "Spring");
    CHECK(parent_of(s.laptop, k_child_folder) == "Seasons");
    CHECK(parent_of(s.laptop, easter) == "April");
    CHECK(s.laptop.count("SELECT COUNT(*) FROM image_folder_items WHERE filename = 'ab12.png'") == 2);
    CHECK(s.laptop.text("SELECT json_extract(data, '$.also_play[0]') FROM service_items WHERE order_num = 6")
          == s.laptop.text("SELECT id FROM service_items WHERE order_num = 5"));
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);
    CHECK(s.sync(s.laptop).changes_sent == 0);
}

TEST_CASE("An edit in the window while catching up is worked out is kept, and the lock is not held meanwhile",
          "[sync][parts][catchup]")
{
    Shared s(true);
    away_while(s, [&] { s.church.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'"); }, 64);
    // The window waits for no lock: planning holds none.
    auto window = Database::open_file(s.laptop.db.path());
    window.exec("PRAGMA busy_timeout = 0");
    const auto parts = seventhslide::test::read_index(s.laptop.store, s.laptop.store.snapshots().back()).parts.size();
    // Whether the window could take the lock now, as an edit would.
    const auto lock_free = [&] {
        try {
            window.exec("BEGIN IMMEDIATE");
            window.exec("ROLLBACK");
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    // Each plan reads every part once. Halfway through each of the first
    // ``edits`` the window edits; halfway through each, and the one after,
    // it takes the lock, but in the fourth, which holds it.
    constexpr std::size_t k_plans_unlocked = 3;
    std::size_t edits = 0;
    std::string title = "Amazing Grace";
    SECTION("an edit during the first plan")
    {
        edits = 1;
    }
    SECTION("an edit during each of the first three")
    {
        edits = k_plans_unlocked;
    }
    std::size_t reads = 0;
    bool locked = false;
    s.laptop.store.on_read_part = [&](std::string_view, std::string&) {
        const auto plan = reads / parts;
        if (reads++ % parts != parts / 2 || plan > edits) {
            return;
        }
        if (plan < k_plans_unlocked) {
            locked = locked || !lock_free();
        }
        if (plan < edits) {
            const auto next = "Amazing Grace (" + std::to_string(plan + 1) + ")";
            window.exec("UPDATE songs SET title = '" + next + "' WHERE title = '" + title + "'");
            title = next;
        }
    };
    CHECK(s.sync(s.laptop).caught_up);
    s.laptop.store.on_read_part = nullptr;
    // Worked out again, from a new read each time, once it found an edit;
    // after three, once more under the lock.
    CHECK(reads >= (edits + 1) * parts);
    CHECK_FALSE(locked);
    s.settle();
    for (auto* c : {&s.church, &s.laptop}) {
        CHECK(c->text("SELECT key FROM songs WHERE title = '" + title + "'") == "A");
    }
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);
}

TEST_CASE("A part changed after catching up is worked out stops it, with nothing applied",
          "[sync][parts][catchup]")
{
    Shared s;
    away_while(s, [&] { s.church.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'"); }, 64);
    const auto before = everything(s.laptop);
    std::map<std::string, int> reads;
    s.laptop.store.on_read_part = [&](std::string_view sha256, std::string& bytes) {
        if (++reads[std::string(sha256)] == 2) {
            bytes += " ";  // damaged since it was read to plan
        }
    };
    CHECK_THROWS_AS(s.laptop.engine.sync(), std::runtime_error);
    CHECK(everything(s.laptop) == before);
    // Read whole again, it catches up.
    s.laptop.store.on_read_part = nullptr;
    CHECK(s.sync(s.laptop).caught_up);
    s.settle();
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);
}

TEST_CASE("A computer catching up from a snapshot in parts keeps the changes it holds and the files it declares",
          "[sync][parts][catchup]")
{
    Shared s;
    away_while(
        s,
        [&] {
            // Held at church: its song is not there.
            send_from_office(s, 1, {new_item(s)});
            s.church.put_file("images/gh90.png", "palms");
            s.church.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('gh90.png', 'Palms')");
        },
        64);
    REQUIRE(s.church.count("SELECT COUNT(*) FROM sync_held") == 1);
    // Each in a part with no record in it.
    const auto index = seventhslide::test::read_index(s.church.store, s.church.store.snapshots().back());
    bool held = false;
    bool declared = false;
    for (const auto& ref : index.parts) {
        const auto part = seventhslide::sync::read_snapshot_part(s.church.store.read_part(ref.sha256), ref.sha256);
        const bool palms = std::ranges::any_of(part.media, [](const auto& m) { return m.path == "images/gh90.png"; });
        if (!part.held.empty() || palms) {
            CHECK(part.records.empty());
        }
        held = held || !part.held.empty();
        declared = declared || palms;
    }
    REQUIRE(held);
    REQUIRE(declared);

    const auto back = s.sync(s.laptop);
    CHECK(back.caught_up);
    CHECK(back.held == std::vector<std::string>{"service_items " + k_new_item});
    CHECK(s.laptop.count("SELECT COUNT(*) FROM sync_held WHERE record_key = '" + k_new_item + "'") == 1);
    for (const char* sql : {"SELECT group_concat(path || size || sha256, ' ') FROM (SELECT * FROM sync_media ORDER BY path)",
                            "SELECT group_concat(change, ' ') FROM sync_held"}) {
        CHECK(s.laptop.text(sql) == s.church.text(sql));
    }
    CHECK(s.laptop.file("images/gh90.png") == "palms");
    // The held change gets through once its song comes.
    send_from_office(s, 2, {new_song()});
    s.settle();
    for (auto* c : {&s.church, &s.laptop}) {
        CHECK(c->count("SELECT COUNT(*) FROM service_items WHERE order_num = 9") == 1);
        CHECK(c->count("SELECT COUNT(*) FROM sync_held") == 0);
    }
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);
}

TEST_CASE("A record deleted here and changed there is kept, changed, after catching up", "[sync][parts][catchup]")
{
    Shared s(true);
    const auto delete_here = [&](Database& db) { db.exec("DELETE FROM songs WHERE title = 'It Is Well'"); };
    std::optional<Database> window;
    bool deleted = false;
    SECTION("deleted while away, and sent before catching up")
    {
        away_while(
            s,
            [&] {
                delete_here(s.laptop.db);
                s.church.db.exec("UPDATE songs SET key = 'C' WHERE title = 'It Is Well'");
            },
            64);
    }
    SECTION("deleted in the window while catching up is worked out, so not sent")
    {
        away_while(s, [&] { s.church.db.exec("UPDATE songs SET key = 'C' WHERE title = 'It Is Well'"); }, 64);
        window = Database::open_file(s.laptop.db.path());
        s.laptop.store.on_read_part = [&](std::string_view, std::string&) {
            if (!std::exchange(deleted, true)) {
                delete_here(*window);
            }
        };
    }
    CHECK(s.sync(s.laptop).caught_up);
    s.laptop.store.on_read_part = nullptr;
    s.settle();
    for (auto* c : {&s.church, &s.laptop}) {
        CHECK(c->text("SELECT key FROM songs WHERE title = 'It Is Well'") == "C");
    }
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);
}

TEST_CASE("A record deleted there that something unsent here uses comes back after catching up",
          "[sync][parts][catchup]")
{
    Shared s;
    s.church.db.exec("INSERT INTO image_folders (name, parent_id) VALUES ('Lent', 1)");
    s.settle();
    limit_to(s, 25);
    away_while(
        s,
        [&] {
            // A file the size limit keeps here, in the folder.
            s.laptop.put_file("images/gh78.png", "lilies two");
            s.laptop.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('gh78.png', 'Lilies 2')");
            s.laptop.db.exec("INSERT INTO image_folder_items (folder_id, filename, sort_order) "
                             "SELECT id, 'gh78.png', 1 FROM image_folders WHERE name = 'Lent'");
            s.church.db.exec("DELETE FROM image_folders WHERE name = 'Lent'");
        },
        64);
    CHECK(s.sync(s.laptop).caught_up);
    s.settle();
    for (auto* c : {&s.church, &s.laptop}) {
        CHECK(c->count("SELECT COUNT(*) FROM image_folders WHERE name = 'Lent'") == 1);
    }
    CHECK(s.laptop.count("SELECT COUNT(*) FROM image_folder_items WHERE filename = 'gh78.png'") == 1);
    limit_to(s, 40);
    s.settle();
    CHECK(s.church.count("SELECT COUNT(*) FROM image_folder_items i JOIN image_folders f ON f.id = i.folder_id "
                         "WHERE f.name = 'Lent' AND i.filename = 'gh78.png'")
          == 1);
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);
}

TEST_CASE("A join that fails partway leaves the library empty", "[sync][parts]")
{
    Shared s;
    s.church.engine.set_part_bytes(64);
    REQUIRE(s.snapshot(s.church));
    // The change files it holds are gone: there is nothing else to join from.
    const auto files = s.church.store.sequences(Numbered::Changes, "church");
    for (auto* c : {&s.church, &s.laptop}) {
        c->store.drop_changes_through("church", files.back());
    }
    Computer office{"office"};
    s.add(office);
    REQUIRE(office.store.sequences(Numbered::Changes, "church").empty());
    const auto library = seventhslide::db::library_uid(office.db);
    const auto name = office.store.snapshots().back();
    const auto parts = seventhslide::test::read_index(office.store, name).parts.size();
    REQUIRE(parts >= 10);
    std::size_t reads = 0;
    // Each part is read once, and written: this is partway through writing.
    const auto partway = parts / 2;
    bool set_aside = true;
    using Catch::Matchers::ContainsSubstring;
    SECTION("a part that cannot be read")
    {
        office.store.on_read_part = [&](std::string_view, std::string&) {
            if (reads++ == partway) {
                throw std::runtime_error("the disk failed");
            }
        };
        CHECK_THROWS_WITH(office.engine.join(s.library), ContainsSubstring("damaged, and set aside"));
    }
    SECTION("a part damaged")
    {
        office.store.on_read_part = [&](std::string_view, std::string& bytes) {
            if (reads++ == partway) {
                bytes += " ";
            }
        };
        CHECK_THROWS_WITH(office.engine.join(s.library), ContainsSubstring("damaged, and set aside"));
    }
    SECTION("a record in a later part this library cannot take")
    {
        auto snapshot = s.church.snapshot(name);
        const auto last = std::ranges::find_if(snapshot.records | std::views::reverse,
                                               [](const auto& r) { return r.document.has_value(); });
        REQUIRE(last != (snapshot.records | std::views::reverse).end());
        (*last->document)["no_such_field"] = 1;
        office.store.remove_snapshot(name);
        seventhslide::test::write_snapshot(office.store, name, snapshot, identity(name.machine).key, 64);
        CHECK_THROWS_WITH(office.engine.join(s.library), Catch::Matchers::ContainsSubstring("no_such_field"));
        set_aside = false;
    }
    office.store.on_read_part = nullptr;
    CHECK(seventhslide::db::library_uid(office.db) == library);
    for (const auto table : seventhslide::db::record_tables_in_order()) {
        if (table != "library_meta") {
            CHECK(seventhslide::db::record_keys(office.db, table).empty());
        }
    }
    CHECK(office.count("SELECT COUNT(*) FROM sync_shared") == 0);
    CHECK(office.count("SELECT COUNT(*) FROM sync_media") == 0);
    CHECK(office.count("SELECT COUNT(*) FROM sync_applied") == 0);
    if (set_aside) {
        // Set aside, to be fetched again; then it joins.
        CHECK(office.store.snapshots().empty());
        s.link.all();
        office.engine.join(s.library);
        s.sync(office);
        seventhslide::test::check_same_records(office.db, s.church.db);
    }
}

TEST_CASE("A join that finds a part of the newest snapshot damaged takes the next-newest at once, reading each "
          "part once",
          "[sync][parts]")
{
    Shared s;
    s.church.engine.set_part_bytes(64);
    REQUIRE(s.snapshot(s.church));
    // The pew has only the older snapshot.
    Computer pew{"pew"};
    s.add(pew);
    s.link.computers.pop_back();
    s.church.db.exec("UPDATE songs SET key = 'D' WHERE title = 'Amazing Grace'");
    s.church.db.exec("UPDATE songs SET key = 'E' WHERE title = 'It Is Well'");
    s.sync(s.church);
    REQUIRE(s.snapshot(s.church));
    Computer office{"office"};
    s.add(office);
    auto names = office.store.snapshots();
    std::ranges::sort(names, {}, &SnapshotName::files);
    REQUIRE(names.size() == 2);
    const auto older = seventhslide::test::read_index(office.store, names[0]);
    const auto newer = seventhslide::test::read_index(office.store, names[1]);
    // The last of the newer's own parts is damaged, after the others are
    // written.
    std::vector<std::size_t> own;
    for (std::size_t i = 0; i < newer.parts.size(); ++i) {
        if (std::ranges::none_of(older.parts, [&](const auto& p) { return p.sha256 == newer.parts[i].sha256; })) {
            own.push_back(i);
        }
    }
    REQUIRE(own.size() >= 2);
    const auto damaged = newer.parts[own.back()].sha256;
    std::size_t reads = 0;
    office.store.on_read_part = [&](std::string_view sha256, std::string& bytes) {
        ++reads;
        if (sha256 == damaged) {
            bytes += " ";
        }
    };
    office.engine.join(s.library);
    office.store.on_read_part = nullptr;
    // The newer's parts up to the damaged one, then the older's, each once.
    CHECK(reads == own.back() + 1 + older.parts.size());
    CHECK(office.store.snapshots() == std::vector<SnapshotName>{names[0]});
    // As one joining from the older alone: what the newer wrote is undone.
    // Each notes its own files, of which it has none yet.
    pew.engine.join(s.library);
    auto as_pew = everything(office);
    const std::string office_files = "sync_applied office 0\n";
    REQUIRE(as_pew.find(office_files) != std::string::npos);
    as_pew.replace(as_pew.find(office_files), office_files.size(), "sync_applied pew 0\n");
    CHECK(as_pew == everything(pew));
    // The files after it bring the rest.
    s.sync(office);
    seventhslide::test::check_same_records(office.db, s.church.db);
}

TEST_CASE("A computer back after more than 90 days, with little changed, reads again and writes only what differs",
          "[sync][parts][catchup]")
{
    Shared s;
    add_songs(s.church, 300);
    s.settle();
    std::vector<std::string> changed;
    for (const auto* title : {"Hymn 5", "Hymn 250", "Hymn 99"}) {
        changed.push_back(s.church.text(std::string("SELECT uid FROM songs WHERE title = '") + title + "'"));
    }
    away_while(
        s,
        [&] {
            s.church.db.exec("UPDATE songs SET key = 'A' WHERE title IN ('Hymn 5', 'Hymn 250')");
            s.church.db.exec("DELETE FROM songs WHERE title = 'Hymn 99'");
        },
        16 * 1024);
    const auto parts = seventhslide::test::read_index(s.laptop.store, s.laptop.store.snapshots().back()).parts.size();
    REQUIRE(parts >= 20);
    std::set<std::size_t> holding;
    for (const auto& uid : changed) {
        holding.insert(part_holding(s.church, "songs", uid));
    }

    // What is written to the library and to the shared copies.
    s.laptop.db.exec("CREATE TEMP TABLE written (what TEXT)");
    for (const char* table : {"songs", "sync_shared"}) {
        for (const char* how : {"INSERT", "UPDATE", "DELETE"}) {
            s.laptop.db.exec(std::string("CREATE TEMP TRIGGER written_") + table + "_" + how + " AFTER " + how
                             + " ON main." + table + " BEGIN INSERT INTO written VALUES ('" + table + "'); END");
        }
    }
    std::map<std::string, int> reads;
    s.laptop.store.on_read_part = [&](std::string_view sha256, std::string&) { ++reads[std::string(sha256)]; };
    CHECK(s.sync(s.laptop).caught_up);
    s.laptop.store.on_read_part = nullptr;
    // Every part read to work it out; only those holding what changed read
    // again, to write it.
    CHECK(reads.size() == parts);
    CHECK(std::ranges::count_if(reads, [](const auto& r) { return r.second == 2; })
          == static_cast<std::ptrdiff_t>(holding.size()));
    CHECK(s.laptop.count("SELECT COUNT(*) FROM written WHERE what = 'songs'") == 3);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM written WHERE what = 'sync_shared'") == 3);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE key = 'A'") == 2);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'Hymn 99'") == 0);
    s.settle();
    seventhslide::test::check_same_records(s.laptop.db, s.church.db);
}


namespace {

/// What ``c`` has applied, as it would say it over a connection.
std::map<std::string, std::int64_t> applied_by(Computer& c)
{
    return seventhslide::db::SyncBookkeeping(c.db).applied();
}

constexpr const char* k_now = "2026-06-01T00:00:00Z";

}  // namespace

TEST_CASE("Old files go once a snapshot holds them and every member has applied them",
          "[sync][housekeeping]")
{
    Shared s;
    for (const auto* key : {"A", "B", "C"}) {
        s.church.db.exec(std::string("UPDATE songs SET key = '") + key + "' WHERE id = 1");
        s.sync(s.church);
    }
    s.settle();
    REQUIRE(s.church.store.sequences(Numbered::Changes, "church").size() == 4);
    const auto church_applied = applied_by(s.church);
    const auto old = [&](const std::map<std::string, seventhslide::sync::Progress>& heard) {
        return seventhslide::sync::old_files(s.church.store, s.members(), "church", church_applied, heard, k_now);
    };
    const seventhslide::sync::Progress laptop_now{applied_by(s.laptop), k_now};

    // No snapshot holds them yet: nothing goes.
    CHECK(old({{"laptop", laptop_now}}).empty());
    REQUIRE(s.snapshot(s.church));

    // A member never heard from holds everything up.
    CHECK(old({}).changes_through.empty());
    // One behind holds up what it has not applied.
    CHECK(old({{"laptop", {{{"church", 2}}, k_now}}}).changes_through
          == std::map<std::string, std::int64_t>{{"church", 2}});
    // Every member has them: they go, and are not taken back.
    const auto all = old({{"laptop", laptop_now}});
    CHECK(all.changes_through == std::map<std::string, std::int64_t>{{"church", 4}});
    seventhslide::sync::let_go(s.church.store, all);
    CHECK(s.church.store.sequences(Numbered::Changes, "church").empty());
    CHECK(s.church.store.dropped_through("church") == 4);
    s.link.all();  // the laptop still has them
    CHECK(s.church.store.sequences(Numbered::Changes, "church").empty());
    CHECK_THROWS(s.church.store.write(Numbered::Changes, "church", 2, "{}"));

    // Syncing goes on after them.
    s.church.db.exec("UPDATE songs SET key = 'D' WHERE id = 1");
    CHECK(s.sync(s.church).changes_sent == 1);
    CHECK(s.church.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{5});
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "D");

    // One not heard from for long holds nothing up; heard lately, it does.
    s.church.db.exec("UPDATE songs SET key = 'E' WHERE id = 1");
    s.sync(s.church);
    REQUIRE(s.snapshot(s.church));
    const auto applied_now = applied_by(s.church);
    const auto since = [&](const char* at) {
        return seventhslide::sync::old_files(s.church.store, s.members(), "church", applied_now,
                                             {{"laptop", {{{"church", 4}}, at}}}, k_now);
    };
    CHECK(since("2026-05-20T00:00:00Z").changes_through.empty());
    CHECK(since("2026-02-01T00:00:00Z").changes_through == std::map<std::string, std::int64_t>{{"church", 6}});
    // And the older snapshot goes: the newest holds what it did.
    const auto newest = since("2026-02-01T00:00:00Z");
    REQUIRE(newest.snapshots.size() == 1);
    CHECK(newest.snapshots.front().files == 4);
}

TEST_CASE("A part goes once no snapshot that stays names it, and no one has wanted it for a while",
          "[sync][housekeeping]")
{
    Shared s;
    add_songs(s.church, 4000);
    s.settle();
    REQUIRE(s.snapshot(s.church));
    const auto first_name = s.church.store.snapshots().back();
    const auto first = seventhslide::test::read_index(s.church.store, first_name);
    s.church.db.exec("UPDATE songs SET key = 'E' WHERE id = 3000");
    s.settle();
    REQUIRE(s.snapshot(s.church));
    const auto second = seventhslide::test::read_index(s.church.store, s.church.store.snapshots().back());
    std::vector<std::string> only_first;
    std::ranges::set_difference(part_names(first.parts), part_names(second.parts), std::back_inserter(only_first));
    REQUIRE_FALSE(only_first.empty());
    // A part a writer stopped part way left: no snapshot names it.
    const std::string stray_text = R"({"format":3,"records":[],"held":[],"media":[]})";
    const auto stray = seventhslide::crypto::sha256_hex(stray_text);
    s.church.store.write_part(stray, stray_text);

    const auto days_on = [](int days) {
        return QDateTime::currentDateTimeUtc().addDays(days).toString(Qt::ISODate).toStdString();
    };
    const auto old = [&](const std::string& now) {
        return seventhslide::sync::old_files(s.church.store, s.members(), "church", applied_by(s.church),
                                             {{"laptop", {applied_by(s.laptop), now}}}, now);
    };
    // Within the grace, every part stays; the older snapshot goes.
    const auto soon = old(days_on(1));
    CHECK(soon.snapshots == std::vector<SnapshotName>{first_name});
    CHECK(soon.parts.empty());
    // After it, what only the older one named goes, and the stray; what both
    // named stays with the newer.
    const auto later = old(days_on(seventhslide::sync::k_part_grace_days + 1));
    CHECK(later.snapshots == std::vector<SnapshotName>{first_name});
    auto going = later.parts;
    std::ranges::sort(going);
    auto expected = only_first;
    expected.push_back(stray);
    std::ranges::sort(expected);
    CHECK(going == expected);
    seventhslide::sync::let_go(s.church.store, later);
    std::vector<std::string> kept;
    for (const auto& part : s.church.store.parts()) {
        kept.push_back(part.sha256);
    }
    CHECK(kept == part_names(second.parts));
    // The snapshot left is whole.
    Computer office{"office"};
    s.add(office);
    office.engine.join(s.library);
    CHECK(s.sync(office).files_applied == 0);
    seventhslide::test::check_same_records(office.db, s.church.db);

    SECTION("with no snapshot to go by, a stray part still goes")
    {
        Computer pew{"pew"};
        pew.store.write_part(stray, stray_text);
        const auto now = days_on(seventhslide::sync::k_part_grace_days + 1);
        CHECK(seventhslide::sync::old_files(pew.store, s.members(), "pew", {}, {}, now).parts
              == std::vector<std::string>{stray});
    }
}

namespace {

/// ``days`` from now, ISO 8601 UTC.
std::string days_from_now(int days)
{
    return QDateTime::currentDateTimeUtc().addDays(days).toString(Qt::ISODate).toStdString();
}

/// Where ``c`` keeps the part ``sha256``.
std::filesystem::path part_file(Computer& c, const std::string& sha256)
{
    return c.root.path / "sync" / "snapshot_parts" / (sha256 + ".json");
}

/// Two snapshots the church wrote of a library in many parts, the second
/// after a song changed, and the parts only each names.
struct TwoSnapshots {
    SnapshotName first;
    SnapshotName second;
    std::vector<std::string> only_first;
    std::vector<std::string> only_second;
};

TwoSnapshots two_snapshots(Shared& s)
{
    s.church.engine.set_part_bytes(16 * 1024);
    add_songs(s.church, 300);
    s.settle();
    TwoSnapshots out;
    REQUIRE(s.snapshot(s.church));
    out.first = s.church.store.snapshots().back();
    s.church.db.exec("UPDATE songs SET key = 'E' WHERE title = 'Hymn 150'");
    s.settle();
    REQUIRE(s.snapshot(s.church));
    out.second = s.church.store.snapshots().back();
    const auto first = part_names(seventhslide::test::read_index(s.church.store, out.first).parts);
    const auto second = part_names(seventhslide::test::read_index(s.church.store, out.second).parts);
    std::ranges::set_difference(first, second, std::back_inserter(out.only_first));
    std::ranges::set_difference(second, first, std::back_inserter(out.only_second));
    REQUIRE_FALSE(out.only_first.empty());
    REQUIRE_FALSE(out.only_second.empty());
    return out;
}

}  // namespace

TEST_CASE("A part a snapshot arriving wants as it is let go of stays", "[sync][housekeeping]")
{
    Shared s;
    const std::string stray_text = R"({"format":3,"records":[],"held":[],"media":[]})";
    const auto stray = seventhslide::crypto::sha256_hex(stray_text);
    s.church.store.write_part(stray, stray_text);
    std::filesystem::last_write_time(part_file(s.church, stray),
                                     std::filesystem::file_time_type::clock::now() - std::chrono::days(30));
    const auto now = days_from_now(0);
    const auto old = seventhslide::sync::old_files(s.church.store, s.members(), "church", applied_by(s.church),
                                                   {{"laptop", {applied_by(s.laptop), now}}}, now);
    REQUIRE(old.parts == std::vector<std::string>{stray});
    // Meanwhile the link, for an index arriving that names it, keeps it.
    s.church.store.on_remove_part = [&](std::string_view sha256) { CHECK(s.church.store.keep_part(sha256)); };
    seventhslide::sync::let_go(s.church.store, old);
    CHECK(s.church.store.read_part(stray) == stray_text);

    // Not wanted again, it goes.
    s.church.store.on_remove_part = nullptr;
    std::filesystem::last_write_time(part_file(s.church, stray),
                                     std::filesystem::file_time_type::clock::now() - std::chrono::days(30));
    seventhslide::sync::let_go(s.church.store, old);
    CHECK(s.church.store.parts().empty());
}

TEST_CASE("Old files go only by a snapshot whose parts are all here", "[sync][housekeeping]")
{
    Shared s;
    const auto two = two_snapshots(s);
    const auto old = [&] {
        return seventhslide::sync::old_files(s.church.store, s.members(), "church", applied_by(s.church),
                                             {{"laptop", {applied_by(s.laptop), k_now}}}, k_now);
    };
    const auto first_files = seventhslide::test::read_index(s.church.store, two.first).files;
    const auto second_files = seventhslide::test::read_index(s.church.store, two.second).files;
    REQUIRE(first_files.at("church") < second_files.at("church"));
    // Both whole: the newer is gone by, and the older goes.
    CHECK(old().snapshots == std::vector<SnapshotName>{two.first});
    CHECK(old().changes_through.at("church") == second_files.at("church"));

    // A part of the newer lost here: the older is gone by, and stays.
    std::filesystem::remove(part_file(s.church, two.only_second.front()));
    SECTION("lost") {}
    SECTION("damaged, of another size")
    {
        std::ofstream(part_file(s.church, two.only_second.front()), std::ios::binary) << "{}";
    }
    const auto now = old();
    CHECK(now.snapshots.empty());
    CHECK(now.changes_through.at("church") == first_files.at("church"));
}

TEST_CASE("The parts of a snapshot that will not go stay with it", "[sync][housekeeping]")
{
    Shared s;
    const auto two = two_snapshots(s);
    const auto later = days_from_now(seventhslide::sync::k_part_grace_days + 1);
    const auto old = seventhslide::sync::old_files(s.church.store, s.members(), "church", applied_by(s.church),
                                                   {{"laptop", {applied_by(s.laptop), later}}}, later);
    REQUIRE(old.snapshots == std::vector<SnapshotName>{two.first});
    auto going = old.parts;
    std::ranges::sort(going);
    REQUIRE(going == two.only_first);

    s.church.store.keep_snapshots = true;
    seventhslide::sync::let_go(s.church.store, old);
    CHECK(s.church.store.snapshots() == std::vector<SnapshotName>{two.first, two.second});
    for (const auto& part : seventhslide::test::read_index(s.church.store, two.first).parts) {
        CHECK(std::filesystem::exists(part_file(s.church, part.sha256)));
    }
    // It is still whole: one joining from it would take it.
    REQUIRE_NOTHROW(seventhslide::test::read_snapshot(s.church.store, two.first));

    // Once it goes, they go.
    s.church.store.keep_snapshots = false;
    seventhslide::sync::let_go(s.church.store, old);
    CHECK(s.church.store.snapshots() == std::vector<SnapshotName>{two.second});
    for (const auto& part : two.only_first) {
        CHECK_FALSE(std::filesystem::exists(part_file(s.church, part)));
    }
}

TEST_CASE("No part goes while a snapshot here cannot be read", "[sync][housekeeping]")
{
    Shared s;
    const std::string stray_text = R"({"format":3,"records":[],"held":[],"media":[]})";
    const auto stray = seventhslide::crypto::sha256_hex(stray_text);
    s.church.store.write_part(stray, stray_text);
    const auto later = days_from_now(seventhslide::sync::k_part_grace_days + 1);
    const auto old = [&] {
        return seventhslide::sync::old_files(s.church.store, s.members(), "church", applied_by(s.church),
                                             {{"laptop", {applied_by(s.laptop), later}}}, later)
            .parts;
    };
    REQUIRE(old() == std::vector<std::string>{stray});
    const auto index = s.church.root.path / "sync" / "snapshot_index" / "office" / "0000000099.json";
    std::filesystem::create_directories(index.parent_path());
    SECTION("damaged")
    {
        std::ofstream(index, std::ios::binary) << "{";
    }
    SECTION("a newer SeventhSlide's")
    {
        std::ofstream(index, std::ios::binary) << R"({"format":99,"signatures":[]})" << "\n{}";
    }
    REQUIRE(s.church.store.snapshots().size() == 1);
    CHECK(old().empty());
    seventhslide::sync::let_go(s.church.store, seventhslide::sync::old_files(
                                                   s.church.store, s.members(), "church", applied_by(s.church),
                                                   {{"laptop", {applied_by(s.laptop), later}}}, later));
    CHECK(s.church.store.read_part(stray) == stray_text);
}

TEST_CASE("A computer removed after its old files went keeps every file it wrote", "[sync][housekeeping]")
{
    Shared s;
    Computer office{"office", "Office"};
    s.add(office);
    office.engine.join(s.library);
    s.sync(office);
    office.db.exec("UPDATE songs SET key = 'F' WHERE id = 1");
    s.sync(office);
    office.db.exec("UPDATE songs SET key = 'G' WHERE id = 1");
    s.sync(office);
    s.settle();
    REQUIRE(s.snapshot(s.church));
    seventhslide::sync::let_go(
        s.church.store,
        seventhslide::sync::old_files(s.church.store, s.members(), "church", applied_by(s.church),
                                      {{"laptop", {applied_by(s.laptop), k_now}}, {"office", {applied_by(office), k_now}}},
                                      k_now));
    REQUIRE(s.church.store.sequences(Numbered::Changes, "office").empty());

    s.church.members(s.library).remove("office", s.admin());
    s.link.all();
    // members() is a fresh copy: kept, so the pointer into it stays good.
    const auto members = s.members();
    const auto* removed = members.find("office");
    REQUIRE(removed != nullptr);
    CHECK(removed->removed);
    CHECK(removed->files_kept == 2);
}

// Hidden: how big a library's first change file and its snapshot are, and how
// long they take, with full-size Bibles and a hymnal (SEVENTHSLIDE_BIBLES,
// default 3).
TEST_CASE("Measure a large library", "[.measure]")
{
    const char* env = std::getenv("SEVENTHSLIDE_BIBLES");
    const int bibles = env != nullptr ? std::atoi(env) : 3;
    Computer church{"church", "Church PC"};
    const auto uid = seventhslide::db::library_uid(church.db);
    for (int b = 1; b <= bibles; ++b) {
        church.db.exec("INSERT INTO bibles (id, name) VALUES (" + std::to_string(b) + ", 'Bible " + std::to_string(b) + "')");
        // 31,102 verses of about 130 characters, as a whole Bible has.
        church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 31102) "
                       "INSERT INTO verses (bible_id, book, chapter, verse_num, text) SELECT "
                       + std::to_string(b)
                       + ", 'Book ' || (i / 1000), (i / 30) % 150 + 1, i % 30 + 1, "
                         "substr(replace(hex(randomblob(70)), '0', ' '), 1, 130) FROM n");
    }
    church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 2000) "
                   "INSERT INTO songs (title, lyrics) SELECT 'Hymn ' || i, "
                   "'[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
    (void)church.members(uid).found(k_passphrase, seventhslide::crypto::k_box_params_quick);
    church.engine.share_everything();
    QElapsedTimer timer;
    timer.start();
    const auto sent = church.engine.sync();
    const auto send_ms = timer.restart();
    std::int64_t largest = 0;
    std::int64_t total = 0;
    for (const auto n : church.store.sequences(Numbered::Changes, "church")) {
        const auto size = static_cast<std::int64_t>(church.store.read(Numbered::Changes, "church", n).size());
        largest = std::max(largest, size);
        total += size;
    }
    REQUIRE(church.engine.snapshot());
    const auto snapshot_ms = timer.restart();
    std::int64_t snapshot_size = 0;
    for (const auto& part : church.store.parts()) {
        snapshot_size += part.size;
    }
    std::cout << bibles << " Bibles, 2000 songs: " << sent.changes_sent << " changes in "
              << church.store.sequences(Numbered::Changes, "church").size() << " file(s), largest "
              << largest / 1'000'000 << " MB, all " << total / 1'000'000 << " MB, in " << send_ms
              << " ms; snapshot " << snapshot_size / 1'000'000 << " MB in " << snapshot_ms << " ms\n";
}

TEST_CASE("A computer back after months, while the password changed, a computer joined and old "
          "files went, catches up and goes on",
          "[sync][catchup][hardening]")
{
    Shared s;
    Computer office{"office", "Office"};
    // The laptop goes away for months, with an edit not yet sent.
    s.link.computers = {&s.church};
    s.laptop.db.exec("UPDATE songs SET key = 'F' WHERE title = 'It Is Well'");

    // Meanwhile: a new password (so a new admin key), a computer joins, and
    // the library changes a good deal.
    const auto changed = s.church.members(s.library).replace_admin("roses of sharon", s.admin(),
                                                                    seventhslide::crypto::k_box_params_quick);
    const auto code = s.church.members(s.library).invite(Role::Editor, k_expires, changed.admin);
    s.link.computers.push_back(&office);
    MemberLog(office.store, office.me, s.library).join(seventhslide::sync::read_invite(code));
    s.link.all();
    office.engine.join(s.library);
    s.sync(office);
    for (int i = 0; i < 5; ++i) {
        s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('Psalm " + std::to_string(i) + "', '[V1]\nline')");
        s.sync(s.church);
        office.db.exec("UPDATE services SET name = 'Week " + std::to_string(i) + "'");
        s.sync(office);
    }
    s.sync(s.church);
    REQUIRE(s.snapshot(s.church));
    // Old files go everywhere but the laptop, not heard from since January.
    const std::map<std::string, seventhslide::sync::Progress> heard{
        {"laptop", {{{"church", 1}}, "2026-01-01T00:00:00Z"}},
        {"office", {seventhslide::db::SyncBookkeeping(office.db).applied(), "2026-06-01T00:00:00Z"}}};
    for (auto* c : {&s.church, &office}) {
        seventhslide::sync::let_go(
            c->store, seventhslide::sync::old_files(c->store, s.members(), c->machine,
                                                    seventhslide::db::SyncBookkeeping(c->db).applied(), heard,
                                                    "2026-06-01T00:00:00Z"));
    }
    REQUIRE(s.church.store.sequences(Numbered::Changes, "church").empty());

    // Back.
    s.link.computers = {&s.church, &s.laptop, &office};
    s.link.all();
    CHECK(s.sync(s.laptop).caught_up);
    for (int round = 0; round < 3; ++round) {
        s.sync(s.church);
        s.sync(office);
        s.sync(s.laptop);
    }
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
    seventhslide::test::check_same_records(office.db, s.laptop.db);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'It Is Well'") == "F");
    CHECK(office.text("SELECT key FROM songs WHERE title = 'It Is Well'") == "F");
    // It knows the new member and the new admin key, and the new password
    // works there; the old one does not.
    const auto list = seventhslide::sync::read_members(s.laptop.store, s.library);
    CHECK(list.find("office") != nullptr);
    CHECK(*list.admin == changed.admin.public_key());
    CHECK(s.laptop.members(s.library).unlock_admin("roses of sharon").has_value());
    CHECK_FALSE(s.laptop.members(s.library).unlock_admin(k_passphrase).has_value());
    // And it goes on: an edit there reaches the others.
    s.laptop.db.exec("UPDATE songs SET key = 'G' WHERE title = 'Psalm 4'");
    s.sync(s.laptop);
    s.sync(office);
    CHECK(office.text("SELECT key FROM songs WHERE title = 'Psalm 4'") == "G");
}

namespace {

/// Each change file ``c`` sent is at most ``k_change_file_bytes`` as written,
/// or holds one record alone.
void check_change_file_sizes(Computer& c)
{
    for (const auto n : c.store.sequences(Numbered::Changes, c.machine)) {
        const auto size = c.store.read(Numbered::Changes, c.machine, n).size();
        const auto changes = c.change_file(c.machine, n).changes.size();
        INFO("file " << n << ": " << size << " bytes, " << changes << " changes");
        CHECK(static_cast<std::int64_t>(size) <= seventhslide::sync::k_largest_store_file);
        if (changes > 1) {
            CHECK(size <= seventhslide::sync::k_change_file_bytes);
        }
    }
}

}  // namespace

TEST_CASE("A library too large for one change file goes in several, each of a size to pass on",
          "[sync][hardening]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto uid = seventhslide::db::library_uid(church.db);
    church.db.exec("INSERT INTO bibles (id, name) VALUES (1, 'King James')");
    church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 31102) "
                   "INSERT INTO verses (bible_id, book, chapter, verse_num, text) SELECT 1, 'Book ' || (i / 1000), "
                   "(i / 30) % 150 + 1, i % 30 + 1, substr(replace(hex(randomblob(70)), '0', ' '), 1, 130) FROM n");
    church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 3000) "
                   "INSERT INTO songs (title, lyrics) SELECT 'Hymn ' || i, "
                   "'[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
    const auto founded = church.members(uid).found(k_passphrase, seventhslide::crypto::k_box_params_quick);
    church.engine.share_everything();
    const auto sent = church.engine.sync();
    CHECK(sent.changes_sent == 3002);
    const auto files = church.store.sequences(Numbered::Changes, "church");
    REQUIRE(files.size() >= 3);
    check_change_file_sizes(church);
    Link link{{&church, &laptop}, {}, uid, {}};
    MemberLog(laptop.store, laptop.me, uid)
        .join(seventhslide::sync::read_invite(church.members(uid).invite(Role::Editor, k_expires, founded.admin)));
    link.all();
    laptop.engine.join(uid);
    CHECK(laptop.engine.sync().files_applied == static_cast<int>(files.size()));
    seventhslide::test::check_same_records(church.db, laptop.db);
}

TEST_CASE("A record that would take a change file past its size starts the next", "[sync][hardening]")
{
    Shared s;
    // Each some 1.5 MB: two to a file, where a third would pass 4 MB.
    s.church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 5) "
                     "INSERT INTO songs (title, lyrics) SELECT 'Long Hymn ' || i, "
                     "'[V1]\n' || substr(hex(randomblob(800000)), 1, 1500000) FROM n");
    const auto before = s.church.store.sequences(Numbered::Changes, "church").size();
    CHECK(s.sync(s.church).changes_sent == 5);
    CHECK(s.church.store.sequences(Numbered::Changes, "church").size() == before + 3);
    check_change_file_sizes(s.church);
    s.sync(s.laptop);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A record too large to sync stays here, says so, and snapshots leave it out meanwhile",
          "[sync][hardening]")
{
    Shared s;
    s.church.engine.set_snapshot_after(1);
    // Some 13 MB of lyrics: past the largest record.
    s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', "
                     "'[V1]\n' || substr(hex(randomblob(6600000)), 1, 13000000))");
    s.church.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    const auto key = s.church.text("SELECT uid FROM songs WHERE title = 'Psalm 119'");
    auto result = s.sync(s.church);
    CHECK(result.changes_sent == 1);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().table == "songs");
    CHECK(result.too_large.front().key == key);
    CHECK(result.too_large.front().title == "Psalm 119");
    CHECK(result.too_large.front().bytes > static_cast<std::int64_t>(seventhslide::db::k_largest_record));
    // A snapshot of what was shared, without it.
    CHECK(result.snapshot_written);
    CHECK(result.snapshot_problem.empty());
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "A");
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'Psalm 119'") == 0);
    // Still marked here, and said again at the next sync.
    CHECK_FALSE(seventhslide::db::marked_records(s.church.db).empty());
    result = s.sync(s.church);
    CHECK(result.too_large.size() == 1);
    // One joining from the snapshot has what the laptop has.
    Computer office{"office", "Office"};
    s.add(office);
    office.engine.join(s.library);
    CHECK(s.sync(office).files_applied == 0);
    seventhslide::test::check_same_records(office.db, s.laptop.db);

    // Made smaller, it goes, and a snapshot can be written.
    s.church.db.exec("UPDATE songs SET lyrics = '[V1]\nGreat peace' WHERE title = 'Psalm 119'");
    result = s.sync(s.church);
    CHECK(result.too_large.empty());
    CHECK(result.changes_sent == 1);
    CHECK(result.snapshot_written);
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'Psalm 119'") == 1);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

namespace {

/// Some 13 MB of lyrics: past the largest record.
constexpr const char* k_too_large_lyrics = "'[V1]\n' || substr(hex(randomblob(6600000)), 1, 13000000)";

}  // namespace

TEST_CASE("A service item naming a song too large to sync stays here with it, and goes when the song does",
          "[sync][hardening]")
{
    Shared s;
    s.church.engine.set_snapshot_after(1);
    s.church.db.exec(std::string("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', ") + k_too_large_lyrics
                     + ")");
    s.church.db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                     "SELECT (SELECT id FROM services), id, 3, 'song' FROM songs WHERE title = 'Psalm 119'");
    s.church.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    const auto items = s.laptop.count("SELECT COUNT(*) FROM service_items");
    auto result = s.sync(s.church);
    CHECK(result.changes_sent == 1);
    // The song alone says so.
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().title == "Psalm 119");
    CHECK(result.snapshot_written);
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "A");
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items") == items);
    CHECK(s.laptop.engine.sync().held.empty());
    // Both still marked here.
    CHECK(seventhslide::db::marked_records(s.church.db).size() == 2);
    result = s.sync(s.church);
    CHECK(result.changes_sent == 0);
    CHECK(result.too_large.size() == 1);
    // A snapshot left them out.
    Computer office{"office", "Office"};
    s.add(office);
    office.engine.join(s.library);
    s.sync(office);
    seventhslide::test::check_same_records(office.db, s.laptop.db);

    // Made smaller, the song goes, and the item with it.
    s.church.db.exec("UPDATE songs SET lyrics = '[V1]\nGreat peace' WHERE title = 'Psalm 119'");
    result = s.sync(s.church);
    CHECK(result.too_large.empty());
    CHECK(result.changes_sent == 2);
    CHECK(result.snapshot_written);
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items i JOIN songs g ON g.id = i.song_id "
                         "WHERE g.title = 'Psalm 119'")
          == 1);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A service item playing with one that waits for a song too large to sync waits too",
          "[sync][hardening]")
{
    Shared s;
    s.church.db.exec(std::string("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', ") + k_too_large_lyrics
                     + ")");
    // The one playing with it comes first, by its uid.
    s.church.db.exec("INSERT INTO service_items (uid, service_id, song_id, order_num, item_type) "
                     "SELECT 'ffffffffffffffffffffffffffffffff', (SELECT id FROM services), id, 3, 'song' "
                     "FROM songs WHERE title = 'Psalm 119'");
    s.church.db.exec("INSERT INTO service_items (uid, service_id, order_num, item_type, data) "
                     "SELECT '00000000000000000000000000000000', (SELECT id FROM services), 4, 'audio', "
                     "json_object('also_play', json_array(id)) FROM service_items "
                     "WHERE uid = 'ffffffffffffffffffffffffffffffff'");
    const auto items = s.laptop.count("SELECT COUNT(*) FROM service_items");
    auto result = s.sync(s.church);
    CHECK(result.changes_sent == 0);
    CHECK(result.too_large.size() == 1);
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items") == items);

    s.church.db.exec("UPDATE songs SET lyrics = '[V1]\nGreat peace' WHERE title = 'Psalm 119'");
    result = s.sync(s.church);
    CHECK(result.changes_sent == 3);
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items") == items + 2);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A record naming one too large to sync is sent when an older version of it was shared",
          "[sync][hardening]")
{
    Shared s;
    s.church.db.exec("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', '[V1]\nBlessed are the undefiled')");
    s.sync(s.church);
    s.sync(s.laptop);
    REQUIRE(s.laptop.count("SELECT COUNT(*) FROM songs WHERE title = 'Psalm 119'") == 1);

    // Grown past the largest record, then put in a service.
    s.church.db.exec(std::string("UPDATE songs SET lyrics = ") + k_too_large_lyrics
                     + " WHERE title = 'Psalm 119'");
    s.church.db.exec("INSERT INTO service_items (service_id, song_id, order_num, item_type) "
                     "SELECT (SELECT id FROM services), id, 3, 'song' FROM songs WHERE title = 'Psalm 119'");
    const auto result = s.sync(s.church);
    CHECK(result.changes_sent == 1);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().title == "Psalm 119");
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items i JOIN songs g ON g.id = i.song_id "
                         "WHERE g.title = 'Psalm 119' AND g.lyrics LIKE '%undefiled'")
          == 1);
    CHECK(s.laptop.engine.sync().held.empty());
}

namespace {

using Kept = std::vector<std::pair<std::string, std::string>>;

/// Some 13 MB of verses for Bible ``id``: past the largest record.
std::string too_large_verses(int id)
{
    return "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1000) "
           "INSERT INTO verses (bible_id, book, chapter, verse_num, text) SELECT "
           + std::to_string(id) + ", 'Psalms', i / 100 + 1, i % 100 + 1, substr(hex(randomblob(7000)), 1, 13500) FROM n";
}

}  // namespace

TEST_CASE("An item naming one kept with a song too large to sync is sent, as that one's older version was shared",
          "[sync][hardening]")
{
    Shared s;
    // A plays a song, and C plays with A; C comes first, by its uid.
    const std::string a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string c = "00000000000000000000000000000000";
    s.church.db.exec("INSERT INTO service_items (uid, service_id, song_id, order_num, item_type) "
                     "SELECT '" + a + "', (SELECT id FROM services), id, 5, 'song' FROM songs "
                     "WHERE title = 'Amazing Grace'");
    s.church.db.exec("INSERT INTO service_items (uid, service_id, order_num, item_type, data) "
                     "SELECT '" + c + "', (SELECT id FROM services), 6, 'audio', "
                     "json_object('also_play', json_array(id)) FROM service_items WHERE uid = '" + a + "'");
    s.sync(s.church);
    s.sync(s.laptop);
    REQUIRE(s.laptop.count("SELECT COUNT(*) FROM service_items WHERE uid IN ('" + a + "', '" + c + "')") == 2);

    // A now plays a song too large to sync, and C is moved.
    s.church.db.exec(std::string("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', ") + k_too_large_lyrics
                     + ")");
    s.church.db.exec("UPDATE service_items SET song_id = (SELECT id FROM songs WHERE title = 'Psalm 119') "
                     "WHERE uid = '" + a + "'");
    s.church.db.exec("UPDATE service_items SET order_num = 7 WHERE uid = '" + c + "'");
    const auto result = s.sync(s.church);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().title == "Psalm 119");
    // A stays here; the others have its older version, which C names.
    CHECK(result.kept_with_too_large == Kept{{"service_items", a}});
    CHECK(result.changes_sent == 1);
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT order_num FROM service_items WHERE uid = '" + c + "'") == 7);
    CHECK(s.laptop.text("SELECT g.title FROM service_items i JOIN songs g ON g.id = i.song_id WHERE i.uid = '" + a
                        + "'")
          == "Amazing Grace");
    CHECK(s.laptop.engine.sync().held.empty());
}

TEST_CASE("An item naming a Bible too large to sync stays with it, and one playing with that item goes",
          "[sync][hardening]")
{
    Shared s;
    const auto passage = s.church.text("SELECT uid FROM service_items WHERE item_type = 'bible'");
    // The passage, shared, now reads from a Bible too large to sync; an item
    // new here plays with it.
    s.church.db.exec("INSERT INTO bibles (id, name) VALUES (2, 'Long Study Bible')");
    s.church.db.exec(too_large_verses(2));
    s.church.db.exec("UPDATE service_items SET data = json_set(data, '$.bible_id', 2) WHERE item_type = 'bible'");
    s.church.db.exec("INSERT INTO service_items (uid, service_id, order_num, item_type, data) "
                     "SELECT '00000000000000000000000000000000', (SELECT id FROM services), 6, 'audio', "
                     "json_object('also_play', json_array(id)) FROM service_items WHERE item_type = 'bible'");
    const auto items = s.laptop.count("SELECT COUNT(*) FROM service_items");
    auto result = s.sync(s.church);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().title == "Long Study Bible");
    CHECK(result.kept_with_too_large == Kept{{"service_items", passage}});
    CHECK(result.changes_sent == 1);
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items") == items + 1);
    CHECK(s.laptop.text("SELECT b.name FROM service_items i JOIN bibles b ON b.id = json_extract(i.data, "
                        "'$.bible_id') WHERE i.item_type = 'bible'")
          == "King James");
    CHECK(s.laptop.engine.sync().held.empty());

    // Made smaller, the Bible goes, and the passage with it.
    s.church.db.exec("DELETE FROM verses WHERE bible_id = 2 AND verse_num > 10");
    result = s.sync(s.church);
    CHECK(result.too_large.empty());
    CHECK(result.kept_with_too_large.empty());
    CHECK(result.changes_sent == 2);
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT b.name FROM service_items i JOIN bibles b ON b.id = json_extract(i.data, "
                        "'$.bible_id') WHERE i.item_type = 'bible'")
          == "Long Study Bible");
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("Folders naming a folder too large to sync stay with it, but not those in a folder shared before",
          "[sync][hardening]")
{
    Shared s;
    // In uid order: R, Q, G, P.
    const std::string r = "00000000000000000000000000000000";
    const std::string q = "11111111111111111111111111111111";
    const std::string g = "22222222222222222222222222222222";
    const std::string p = "ffffffffffffffffffffffffffffffff";
    const auto id_of = [](const std::string& uid) {
        return "(SELECT id FROM freeform_folders WHERE uid = '" + uid + "')";
    };
    s.church.db.exec("INSERT INTO freeform_folders (uid, name) VALUES ('" + q + "', 'Easter')");
    s.sync(s.church);
    s.sync(s.laptop);

    // P, new, is too large to sync. Q, shared, moves into it; G, new, is made
    // in it, and R, new, in Q. An item new in each of Q and G.
    s.church.db.exec("INSERT INTO freeform_folders (uid, name) VALUES ('" + p
                     + "', substr(hex(randomblob(6600000)), 1, 13000000))");
    s.church.db.exec("UPDATE freeform_folders SET parent_id = " + id_of(p) + " WHERE uid = '" + q + "'");
    s.church.db.exec("INSERT INTO freeform_folders (uid, name, parent_id) VALUES ('" + g + "', 'Lent', "
                     + id_of(p) + ")");
    s.church.db.exec("INSERT INTO freeform_folders (uid, name, parent_id) VALUES ('" + r + "', 'Palm Sunday', "
                     + id_of(q) + ")");
    s.church.db.exec("INSERT INTO freeform_items (uid, name, folder_id) VALUES "
                     "('33333333333333333333333333333333', 'Welcome', " + id_of(q) + "), "
                     "('44444444444444444444444444444444', 'Ash Wednesday', " + id_of(g) + ")");
    auto result = s.sync(s.church);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().key == p);
    // Q stays, but the others have it; G stays, and what is in it.
    CHECK(result.kept_with_too_large
          == Kept{{"freeform_folders", q}, {"freeform_folders", g},
                  {"freeform_items", "44444444444444444444444444444444"}});
    CHECK(result.changes_sent == 2);
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT p.name FROM freeform_folders f JOIN freeform_folders p ON p.id = f.parent_id "
                        "WHERE f.uid = '" + r + "'")
          == "Easter");
    CHECK(s.laptop.text("SELECT f.name FROM freeform_items i JOIN freeform_folders f ON f.id = i.folder_id "
                        "WHERE i.name = 'Welcome'")
          == "Easter");
    CHECK(s.laptop.count("SELECT COUNT(*) FROM freeform_folders WHERE uid IN ('" + g + "', '" + p + "')") == 0);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM freeform_items WHERE name = 'Ash Wednesday'") == 0);
    CHECK(s.laptop.engine.sync().held.empty());

    // Renamed shorter, all of it goes.
    s.church.db.exec("UPDATE freeform_folders SET name = 'Holy Week' WHERE uid = '" + p + "'");
    result = s.sync(s.church);
    CHECK(result.too_large.empty());
    CHECK(result.changes_sent == 4);
    s.sync(s.laptop);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A record naming one too large to sync, with nothing to send, is not kept marked", "[sync][hardening]")
{
    Shared s;
    // C plays with A; both shared. A goes, and is sent as gone; C still names
    // it.
    const std::string a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string c = "00000000000000000000000000000000";
    s.church.db.exec("INSERT INTO service_items (id, uid, service_id, order_num, item_type) "
                     "VALUES (900, '" + a + "', (SELECT id FROM services), 5, 'song')");
    s.church.db.exec("INSERT INTO service_items (uid, service_id, order_num, item_type, data) "
                     "VALUES ('" + c + "', (SELECT id FROM services), 6, 'audio', '{\"also_play\":[900]}')");
    s.sync(s.church);
    s.church.db.exec("DELETE FROM service_items WHERE uid = '" + a + "'");
    s.sync(s.church);

    // A comes back, never shared since, playing a song too large to sync. C
    // is written again as it was.
    s.church.db.exec(std::string("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', ") + k_too_large_lyrics
                     + ")");
    s.church.db.exec("INSERT INTO service_items (id, uid, service_id, song_id, order_num, item_type) "
                     "SELECT 900, '" + a + "', (SELECT id FROM services), id, 5, 'song' FROM songs "
                     "WHERE title = 'Psalm 119'");
    s.church.db.exec("UPDATE service_items SET order_num = order_num WHERE uid = '" + c + "'");
    const auto result = s.sync(s.church);
    CHECK(result.changes_sent == 0);
    CHECK(result.too_large.size() == 1);
    CHECK(result.kept_with_too_large == Kept{{"service_items", a}});
    // The song and A stay marked; C's mark goes.
    CHECK(seventhslide::db::marked_records(s.church.db).size() == 2);
}

TEST_CASE("A record too large to sync, never shared, deleted, lets go of what waited for it", "[sync][hardening]")
{
    Shared s;
    s.church.db.exec(std::string("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', ") + k_too_large_lyrics
                     + ")");
    s.church.db.exec("INSERT INTO service_items (uid, service_id, song_id, order_num, item_type) "
                     "SELECT 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', (SELECT id FROM services), id, 3, 'song' "
                     "FROM songs WHERE title = 'Psalm 119'");
    auto result = s.sync(s.church);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.kept_with_too_large.size() == 1);
    CHECK(result.changes_sent == 0);

    s.church.db.exec("DELETE FROM songs WHERE title = 'Psalm 119'");
    result = s.sync(s.church);
    CHECK(result.too_large.empty());
    CHECK(result.kept_with_too_large.empty());
    CHECK(result.changes_sent == 1);
    CHECK(seventhslide::db::marked_records(s.church.db).empty());
    s.sync(s.laptop);
    CHECK(s.laptop.count("SELECT COUNT(*) FROM service_items WHERE uid = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' "
                         "AND song_id IS NULL")
          == 1);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A change that would make a file too large to pass on is not written, and stays marked",
          "[sync][hardening]")
{
    Shared s;
    // Some 10 MB of topics, under the largest record. A change to them holds
    // them as they were too, past what a file may be.
    const std::string topics = "UPDATE songs SET topics = (WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 "
                               "FROM n WHERE i < 300000) SELECT json_group_array('t' || hex(randomblob(12)) || i) "
                               "FROM n) WHERE title = 'Amazing Grace'";
    s.church.db.exec(topics);
    REQUIRE(s.sync(s.church).changes_sent == 1);
    s.church.db.exec(topics);
    const auto files = s.church.store.sequences(Numbered::Changes, "church");
    auto result = s.sync(s.church);
    CHECK(result.changes_sent == 0);
    CHECK(s.church.store.sequences(Numbered::Changes, "church") == files);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().title == "Amazing Grace");
    CHECK(result.too_large.front().bytes > seventhslide::sync::k_largest_store_file);
    CHECK(seventhslide::db::marked_records(s.church.db).size() == 1);

    // Fewer, it goes.
    s.church.db.exec("UPDATE songs SET topics = '[\"peace\"]' WHERE title = 'Amazing Grace'");
    result = s.sync(s.church);
    CHECK(result.too_large.empty());
    CHECK(result.changes_sent == 1);
    check_change_file_sizes(s.church);
    s.sync(s.laptop);
    seventhslide::test::check_same_records(s.church.db, s.laptop.db);
}

TEST_CASE("A computer joining again says what is too large to sync while it waits to send", "[sync][hardening]")
{
    Shared s;
    send_from_office(s, 1, {new_song()});
    s.sync(s.laptop);
    s.laptop.db.exec("UPDATE songs SET key = 'F' WHERE title = 'Be Thou My Vision'");
    REQUIRE(s.sync(s.laptop).changes_sent == 1);  // made on top of the office's file

    // It joins again without the office's file, so it sends nothing yet.
    s.link.hidden = "office";
    Computer again{"laptop", "Laptop"};
    s.add(again);
    again.engine.join(seventhslide::db::library_uid(s.church.db));
    s.sync(again);
    again.db.exec(std::string("INSERT INTO songs (title, lyrics) VALUES ('Psalm 119', ") + k_too_large_lyrics + ")");
    auto result = s.sync(again);
    CHECK(result.changes_sent == 0);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().title == "Psalm 119");

    // And once it has back what it sent.
    s.link.hidden.clear();
    s.link.all();
    result = s.sync(again);
    CHECK(result.files_applied == 2);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().title == "Psalm 119");
}

TEST_CASE("A snapshot that cannot be written is said, and the sync it was due at stands", "[sync][hardening]")
{
    Shared s;
    s.church.engine.set_part_bytes(1024);
    s.church.engine.set_snapshot_after(3);
    // The disk fills partway through the snapshot's parts.
    s.church.store.parts_until_full = 2;
    s.laptop.db.exec("UPDATE songs SET key = 'D' WHERE title = 'Amazing Grace'");
    s.sync(s.laptop);
    s.church.db.exec("UPDATE services SET name = 'Divine Service'");
    seventhslide::sync::SyncResult result;
    REQUIRE_NOTHROW(result = s.sync(s.church));
    CHECK(result.files_applied == 1);
    CHECK(result.changes_sent == 1);
    CHECK(result.sent.size() == 1);
    CHECK_FALSE(result.changed.empty());
    CHECK(s.church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "D");
    CHECK_FALSE(result.snapshot_written);
    CHECK_THAT(result.snapshot_problem, Catch::Matchers::ContainsSubstring("the disk is full"));
    CHECK(s.church.store.snapshots().empty());
    s.sync(s.laptop);
    CHECK(s.laptop.text("SELECT name FROM services") == "Divine Service");

    // Not tried again at every sync, though it would go now: once as many
    // files have come again.
    s.church.store.parts_until_full = -1;
    for (const auto* key : {"E", "F"}) {
        s.church.db.exec(std::string("UPDATE songs SET key = '") + key + "' WHERE id = 2");
        result = s.sync(s.church);
        CHECK(result.changes_sent == 1);
        CHECK_FALSE(result.snapshot_written);
        CHECK_THAT(result.snapshot_problem, Catch::Matchers::ContainsSubstring("the disk is full"));
    }
    s.church.db.exec("UPDATE songs SET key = 'G' WHERE id = 2");
    result = s.sync(s.church);
    CHECK(result.snapshot_written);
    CHECK(result.snapshot_problem.empty());
}

TEST_CASE("A record merged from two computers' changes too large for a snapshot is named, and the sync stands",
          "[sync][hardening]")
{
    Shared s;
    // Some 7.5 MB in a field each: each change small enough to send, the two
    // together not.
    const std::string half = "substr(hex(randomblob(3750000)), 1, 7500000)";
    s.church.db.exec("UPDATE songs SET lyrics = '[V1]\n' || " + half + " WHERE title = 'Amazing Grace'");
    s.laptop.db.exec("UPDATE songs SET comments = " + half + " WHERE title = 'Amazing Grace'");
    s.church.db.exec("UPDATE services SET name = 'Divine Service'");
    // Each sends before it has the other's.
    REQUIRE(s.church.engine.sync().changes_sent == 2);
    REQUIRE(s.laptop.engine.sync().changes_sent == 1);
    s.link.all();
    s.church.engine.set_snapshot_after(1);
    seventhslide::sync::SyncResult result;
    REQUIRE_NOTHROW(result = s.church.engine.sync());
    CHECK(result.files_applied == 1);
    CHECK(result.changed.keys("songs").contains(s.church.text("SELECT uid FROM songs WHERE title = 'Amazing Grace'")));
    CHECK(result.too_large.empty());  // nothing here to send
    CHECK(seventhslide::db::marked_records(s.church.db).empty());
    CHECK_FALSE(result.snapshot_written);
    CHECK_THAT(result.snapshot_problem, Catch::Matchers::ContainsSubstring("“Amazing Grace”")
                                            && Catch::Matchers::ContainsSubstring("too large to sync"));
    CHECK(s.church.store.snapshots().empty());
    CHECK(s.church.count("SELECT length(comments) FROM songs WHERE title = 'Amazing Grace'") == 7500000);
}

TEST_CASE("A Bible shared, then made too large to sync here, keeps snapshots from being written until it goes",
          "[sync][hardening]")
{
    Shared s;
    s.church.engine.set_snapshot_after(1);
    // Some 13 MB of verses: its verses as the others have them are not here.
    s.church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 2000) "
                     "INSERT INTO verses (bible_id, book, chapter, verse_num, text) "
                     "SELECT 1, 'Psalms', 119, i, substr(hex(randomblob(3300)), 1, 6500) FROM n");
    s.church.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    auto result = s.sync(s.church);
    CHECK(result.changes_sent == 1);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().table == "bibles");
    CHECK_FALSE(result.snapshot_written);
    CHECK_THAT(result.snapshot_problem, Catch::Matchers::ContainsSubstring("“King James”"));
    CHECK_FALSE(s.snapshot(s.church));
    CHECK(s.church.store.snapshots().empty());

    // Made smaller, it goes, and a snapshot with it.
    s.church.db.exec("DELETE FROM verses WHERE book = 'Psalms' AND verse_num > 10");
    result = s.sync(s.church);
    CHECK(result.too_large.empty());
    CHECK(result.snapshot_written);
    CHECK(result.snapshot_problem.empty());
}

TEST_CASE("A Bible too large to sync whose verses are as shared goes in a snapshot as shared", "[sync][hardening]")
{
    Shared s;
    s.church.engine.set_snapshot_after(1);
    s.church.db.exec("UPDATE bibles SET copyright = substr(hex(randomblob(6600000)), 1, 13000000)");
    s.church.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    const auto result = s.sync(s.church);
    REQUIRE(result.too_large.size() == 1);
    CHECK(result.too_large.front().table == "bibles");
    CHECK(result.snapshot_written);
    CHECK(result.snapshot_problem.empty());
    // As the others have it: its verses, and not the copyright made here.
    const auto snapshot = s.church.snapshot(s.church.store.snapshots().back());
    const auto bible = std::ranges::find(snapshot.records, std::string("bibles"), &seventhslide::sync::SnapshotRecord::table);
    REQUIRE(bible != snapshot.records.end());
    REQUIRE(bible->document);
    CHECK(bible->document->value("copyright", json()).is_null());
    CHECK(bible->document->at("verses").size() == 2);
    s.sync(s.laptop);
    Computer office{"office", "Office"};
    s.add(office);
    office.engine.join(s.library);
    CHECK(s.sync(office).files_applied == 0);
    seventhslide::test::check_same_records(office.db, s.laptop.db);
}

TEST_CASE("A file damaged on the disk is set aside and fetched again", "[sync][hardening]")
{
    Shared s;
    s.church.db.exec("UPDATE songs SET key = 'B' WHERE title = 'Amazing Grace'");
    s.link.hidden = "church";
    s.sync(s.church);
    s.link.hidden.clear();
    s.link.all();
    REQUIRE(s.laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1, 2});
    // Cut short, as a failing disk might leave it.
    const auto path = s.laptop.root.path / "sync" / "changes" / "church" / "0000000002.json";
    const auto whole = s.laptop.store.read(Numbered::Changes, "church", 2);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << whole.substr(0, whole.size() / 2);

    // The sync goes on without it, and it is set aside...
    seventhslide::sync::SyncResult result;
    REQUIRE_NOTHROW(result = s.laptop.engine.sync());
    CHECK(result.files_applied == 0);
    CHECK(s.laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(std::filesystem::exists(s.laptop.root.path / "sync" / "damaged" / "change-church-2"));
    // ...to be fetched again, whole.
    s.link.all();
    CHECK(s.laptop.engine.sync().files_applied == 1);
    CHECK(s.laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "B");

    SECTION("a damaged snapshot: one joining takes the files instead")
    {
        REQUIRE(s.snapshot(s.church));
        Computer office{"office", "Office"};
        s.add(office);
        const auto name = office.store.snapshots().front();
        const auto digits = std::to_string(name.files);
        std::ofstream(office.root.path / "sync" / "snapshot_index" / name.machine
                          / (std::string(10 - digits.size(), '0') + digits + ".json"),
                      std::ios::binary | std::ios::trunc)
            << "{";
        REQUIRE_NOTHROW(office.engine.join(s.library));
        CHECK(office.store.snapshots().empty());
        s.sync(office);
        seventhslide::test::check_same_records(office.db, s.church.db);
    }
}

TEST_CASE("A web page in a service names that page on every computer", "[sync]")
{
    Shared s;
    // Its id on the laptop will not be the church's.
    s.laptop.db.exec("INSERT INTO web_pages (name, url) VALUES ('Laptop only', 'https://example.net')");
    s.church.db.exec("INSERT INTO web_pages (id, name, url) VALUES (7, 'Weather', 'https://example.org')");
    s.church.db.exec(R"(INSERT INTO service_items (service_id, order_num, item_type, data)
                        VALUES (1, 9000, 'web', '{"page_id":7,"name":"Weather","url":"https://example.org"}'))");
    s.settle();
    const auto page_here = std::stoll(s.laptop.text("SELECT id FROM web_pages WHERE name = 'Weather'"));
    CHECK(page_here != 7);
    const auto data = json::parse(s.laptop.text("SELECT data FROM service_items WHERE item_type = 'web'"));
    CHECK(data.at("page_id").get<std::int64_t>() == page_here);
    CHECK(data.at("url") == "https://example.org");
}
