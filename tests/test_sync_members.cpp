// The member list (member_list.hpp), the gate (gate.hpp), the admin key
// (admin_key.hpp) and the signed envelope (envelope.hpp): who belongs to a
// synced library, what each may do, and how that is checked.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/crypto/encoding.hpp>
#include <seventhslide/crypto/password_box.hpp>
#include <seventhslide/crypto/signing.hpp>
#include <seventhslide/sync/admin_key.hpp>
#include <seventhslide/sync/change_file.hpp>
#include <seventhslide/sync/envelope.hpp>
#include <seventhslide/sync/gate.hpp>
#include <seventhslide/sync/identity.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/sync/snapshot.hpp>
#include <seventhslide/sync/sync_store.hpp>

#include "sync_test_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using nlohmann::json;
using seventhslide::crypto::SigningKey;
using seventhslide::sync::FolderStore;
using seventhslide::sync::Gate;
using seventhslide::sync::Identity;
using seventhslide::sync::k_signed_as_admin;
using seventhslide::sync::k_signed_as_computer;
using seventhslide::sync::MemberLog;
using seventhslide::sync::Members;
using seventhslide::sync::Numbered;
using seventhslide::sync::read_invite;
using seventhslide::sync::read_members;
using seventhslide::sync::Role;
using seventhslide::sync::seal;
using seventhslide::sync::unseal;

namespace {

const std::string k_library = "0123456789abcdef0123456789abcdef";
constexpr const char* k_passphrase = "lilies of the field";
constexpr const char* k_expires = "2999-01-01T00:00:00Z";
constexpr const char* k_expired = "2000-01-01T00:00:00Z";
constexpr auto k_quick = seventhslide::crypto::k_box_params_quick;

Identity who(const std::string& machine)
{
    return {machine, machine, SigningKey::generate()};
}

/// A store with a founded library in it, the admin key in hand.
struct Founded {
    seventhslide::test::TempDir dir{(seventhslide::test::ensure_qt_application(),
                                     seventhslide::test::TempDir{})};
    FolderStore store{dir.path / "sync"};
    Identity church{who("church")};
    MemberLog log{store, church, k_library};
    MemberLog::Founded founded{log.found(k_passphrase, k_quick)};

    const SigningKey& admin() { return founded.admin; }
    Members members() { return log.members(); }
    /// ``computer`` joins as ``role``, writing into this store as the link
    /// would deliver its entry.
    void admit(const Identity& computer, Role role = Role::Editor)
    {
        MemberLog(store, computer, k_library).join(read_invite(log.invite(role, k_expires, admin())));
    }
    /// ``machine`` has written ``n`` change files so far.
    void wrote(const std::string& machine, int n)
    {
        const auto have = store.sequences(Numbered::Changes, machine);
        for (int i = have.empty() ? 1 : static_cast<int>(have.back()) + 1; i <= n; ++i) {
            store.write(Numbered::Changes, machine, i, "{}");
        }
    }
};

/// An entry written by hand, as a computer bent on getting round the rules
/// could: ``seen`` whatever it likes, signed by ``signers``.
void write_entry(FolderStore& store, const Identity& by, std::int64_t sequence, json seen,
                 const std::string& kind, json fields,
                 std::vector<std::pair<std::string_view, const SigningKey*>> signers)
{
    json body{{"format", 1},           {"library", k_library}, {"machine", by.machine},
              {"sequence", sequence},  {"seen", std::move(seen)},
              {"time", "2026-01-01T00:00:00Z"}, {"kind", kind}};
    for (auto& [k, v] : fields.items()) {
        body[k] = v;
    }
    signers.insert(signers.begin(), {k_signed_as_computer, &by.key});
    store.write(Numbered::Members, by.machine, sequence, seal(body, signers));
}

/// What ``store`` has of each computer's entries: a ``seen`` as of now.
json seen_in(FolderStore& store, const std::string& except)
{
    json out = json::object();
    for (const auto& [machine, last] : read_members(store, k_library).heads) {
        if (machine != except) {
            out[machine] = last;
        }
    }
    return out;
}

/// The refused and waiting entries, for a failing test to say.
std::string describe(const Members& m)
{
    std::string out;
    for (const auto& r : m.refused) {
        out += r.machine + ":" + std::to_string(r.sequence) + " refused: " + r.why + "\n";
    }
    for (const auto& w : m.waiting) {
        out += w.machine + ":" + std::to_string(w.sequence) + " waiting\n";
    }
    return out;
}

/// Whether a refused entry of ``machine``'s gives a reason containing ``why``.
bool refused_for(const Members& m, const std::string& machine, const std::string& why)
{
    return std::ranges::any_of(m.refused, [&](const Members::Refused& r) {
        return r.machine == machine && r.why.find(why) != std::string::npos;
    });
}

/// Every entry of ``from`` copied into ``to``, as the link would.
void copy_entries(FolderStore& from, FolderStore& to)
{
    for (const auto& machine : from.machines(Numbered::Members)) {
        const auto have = to.sequences(Numbered::Members, machine);
        for (const auto n : from.sequences(Numbered::Members, machine)) {
            if (std::ranges::find(have, n) == have.end()) {
                to.write(Numbered::Members, machine, n, from.read(Numbered::Members, machine, n));
            }
        }
    }
}

}  // namespace

TEST_CASE("Founding a library makes its first editor, its admin key and its recovery code",
          "[sync][members]")
{
    Founded f;
    const auto m = f.members();
    REQUIRE(m.founded());
    CHECK(m.library == k_library);
    CHECK(*m.admin == f.admin().public_key());
    CHECK(m.size_limit == 0);
    CHECK(m.refused.empty());
    REQUIRE(m.members.size() == 1);
    const auto* church = m.find("church");
    REQUIRE(church != nullptr);
    CHECK(church->key == f.church.key.public_key());
    CHECK(church->name == "church");
    CHECK(church->role == Role::Editor);
    CHECK_FALSE(church->removed);
    CHECK(m.may_write("church"));
    CHECK(m.may_have_written("church", 1000));
    CHECK_FALSE(m.may_write("nobody"));

    // The locked admin key is in the store under the key's fingerprint.
    CHECK(f.store.admin_keys() == std::vector<std::string>{f.admin().public_key().fingerprint()});

    // The recovery code is 13 groups of four, and is the recovery key.
    CHECK(f.founded.recovery_code.size() == 52 + 12);
    CHECK(std::ranges::count(f.founded.recovery_code, '-') == 12);
    const auto recovery = seventhslide::sync::key_from_recovery_code(f.founded.recovery_code);
    REQUIRE(recovery.has_value());
    CHECK(recovery->public_key() == *m.recovery);
    // As typed: lower case, the dashes left out.
    std::string typed;
    for (const char c : f.founded.recovery_code) {
        if (c != '-') {
            typed.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    CHECK(seventhslide::sync::key_from_recovery_code(typed) == recovery);
    CHECK_FALSE(seventhslide::sync::key_from_recovery_code("ABCD-EFGH").has_value());

    SECTION("founded once")
    {
        CHECK_THROWS_AS(f.log.found(k_passphrase, k_quick), std::logic_error);
    }
    SECTION("a short password is refused before anything is written")
    {
        seventhslide::test::TempDir dir;
        FolderStore store(dir.path / "sync");
        MemberLog log(store, who("pc"), k_library);
        CHECK_THROWS_WITH(log.found("church1", k_quick), ContainsSubstring("12 characters"));
        CHECK(store.admin_keys().empty());
        CHECK(store.machines(Numbered::Members).empty());
        CHECK(seventhslide::sync::passphrase_problem("  lilies of the field  ") == std::nullopt);
        CHECK(seventhslide::sync::passphrase_problem("    short     ").has_value());
    }
}

namespace {

/// A store on a folder that counts the member list entries and change files
/// read from it.
class CountingStore final : public seventhslide::sync::SyncStore {
public:
    explicit CountingStore(std::filesystem::path root) : folder_(std::move(root)) {}

    int member_reads = 0;
    int change_reads = 0;
    int listings = 0;

    std::vector<std::string> machines(Numbered kind) override
    {
        ++listings;
        return folder_.machines(kind);
    }
    std::vector<std::int64_t> sequences(Numbered kind, std::string_view machine) override
    {
        return folder_.sequences(kind, machine);
    }
    std::string read(Numbered kind, std::string_view machine, std::int64_t sequence) override
    {
        member_reads += kind == Numbered::Members ? 1 : 0;
        change_reads += kind == Numbered::Changes ? 1 : 0;
        return folder_.read(kind, machine, sequence);
    }
    void write(Numbered kind, std::string_view machine, std::int64_t sequence,
               std::string_view contents) override
    {
        folder_.write(kind, machine, sequence, contents);
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
    std::int64_t dropped_through(std::string_view machine) override
    {
        return folder_.dropped_through(machine);
    }
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

}  // namespace

TEST_CASE("The member list is read once, and again only for an entry that came", "[sync][members]")
{
    // Written through one store and read through another on the same
    // folder, as the link and a library's own thread each have one.
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir dir;
    FolderStore writer(dir.path / "sync");
    CountingStore reader(dir.path / "sync");
    MemberLog log(writer, who("church"), k_library);
    const auto founded = log.found(k_passphrase, k_quick);

    CHECK(read_members(reader, k_library).founded());
    CHECK(reader.member_reads == 1);
    // Nothing new: no entry read again, nor its signature checked.
    CHECK(read_members(reader, k_library).founded());
    CHECK(reader.member_reads == 1);

    // A computer joins: its entry and the one saying the invite was made, alone, are read.
    MemberLog(writer, who("laptop"), k_library)
        .join(read_invite(log.invite(Role::Editor, k_expires, founded.admin)));
    const auto joined = read_members(reader, k_library);
    REQUIRE(joined.find("laptop") != nullptr);
    CHECK(joined.find("laptop")->role == Role::Editor);
    CHECK(reader.member_reads == 3);

    // One set aside is gone from the list, with nothing read.
    writer.set_aside(Numbered::Members, "laptop", 1);
    CHECK(read_members(reader, k_library).find("laptop") == nullptr);
    CHECK(reader.member_reads == 3);
}

// A gate asks on the window's thread; reading the files is refresh's alone,
// off it, and each file is read once.
TEST_CASE("Vouches reads the store only when refreshed, each file once", "[sync][members]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir dir;
    CountingStore store(dir.path / "sync");
    seventhslide::sync::Vouches vouches(store);
    store.write(Numbered::Changes, "church", 1, "not a change file");

    CHECK(vouches.up_to("laptop") == 0);
    CHECK(store.change_reads == 0);
    vouches.refresh();
    CHECK(store.change_reads == 1);
    vouches.refresh();
    CHECK(vouches.up_to("laptop") == 0);
    CHECK(store.change_reads == 1);

    // One just stored is read alone, with no listing of the store; and not
    // again by the next refresh.
    store.write(Numbered::Changes, "church", 2, "not a change file either");
    const int listed = store.listings;
    vouches.stored("church", 2);
    CHECK(store.change_reads == 2);
    CHECK(store.listings == listed);
    vouches.refresh();
    CHECK(store.change_reads == 2);
}

TEST_CASE("The admin key unlocks with the password alone", "[sync][members]")
{
    Founded f;
    const auto unlocked = f.log.unlock_admin(k_passphrase);
    REQUIRE(unlocked.has_value());
    CHECK(*unlocked == f.admin());
    CHECK_FALSE(f.log.unlock_admin("lilies of the fields").has_value());

    SECTION("a computer without the locked key")
    {
        seventhslide::test::TempDir dir;
        FolderStore store(dir.path / "sync");
        copy_entries(f.store, store);
        MemberLog log(store, who("viewer"), k_library);
        CHECK_THROWS_WITH(log.unlock_admin(k_passphrase), ContainsSubstring("holds no copy"));
    }
    SECTION("a damaged locked key is told from a wrong password")
    {
        // Another key locked under the password, under the admin key's name.
        const auto other = SigningKey::generate();
        auto locked = seventhslide::sync::lock_admin_key(other, k_passphrase, k_quick);
        locked.admin = f.admin().public_key();
        std::ofstream(f.dir.path / "sync" / "admin_keys"
                      / (f.admin().public_key().fingerprint() + ".json"))
            << locked.to_json().dump();
        CHECK_THROWS_WITH(f.log.unlock_admin(k_passphrase), ContainsSubstring("damaged"));
    }
}

TEST_CASE("An invite admits a computer as the role it names", "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    const auto pew = who("pew");
    f.admit(laptop);
    f.admit(pew, Role::Viewer);
    const auto m = f.members();
    CHECK(m.refused.empty());
    REQUIRE(m.members.size() == 3);
    CHECK(m.find("laptop")->role == Role::Editor);
    CHECK(m.find("laptop")->key == laptop.key.public_key());
    CHECK(m.find("pew")->role == Role::Viewer);
    CHECK(m.may_write("laptop"));
    CHECK_FALSE(m.may_write("pew"));
    CHECK_FALSE(m.may_have_written("pew", 1));

    SECTION("the invite as a code")
    {
        const auto code = f.log.invite(Role::Editor, k_expires, f.admin());
        const auto invite = read_invite(code);
        CHECK(invite.invite.library == k_library);
        CHECK(invite.invite.role == Role::Editor);
        CHECK(invite.invite.expires == k_expires);
        CHECK(invite.invite.id.size() == 16);
        CHECK(invite.admin == f.admin().public_key());
        CHECK(code != f.log.invite(Role::Editor, k_expires, f.admin()));  // its own id
        CHECK_THROWS_WITH(read_invite("not a code"), ContainsSubstring("not an invite"));
        CHECK_THROWS_WITH(read_invite(seventhslide::crypto::to_base64("{}")),
                          ContainsSubstring("not an invite"));
        // The code holds a one-time key; what a join entry carries does not.
        CHECK(invite.secret.has_value());
        CHECK(invite.secret->public_key() == invite.invite.proof);
        CHECK_FALSE(read_invite(invite.sealed).secret.has_value());
        CHECK(invite.sealed.find(seventhslide::crypto::to_base64(invite.secret->seed())) == std::string::npos);
    }
    SECTION("an invite names a viewer that takes connections, one with the admin key too")
    {
        const auto server = who("server");
        f.admit(server, Role::Viewer);
        f.log.set_key_holder("server", true, f.admin());
        MemberLog(f.store, server, k_library).set_addresses({"church.example.org:7433"});
        const auto invite = read_invite(f.log.invite(Role::Editor, k_expires, f.admin()));
        REQUIRE(invite.invite.through.size() == 1);
        CHECK(invite.invite.through[0].machine == "server");
        CHECK(invite.invite.through[0].key == server.key.public_key());
        CHECK(invite.invite.through[0].addresses == std::vector<std::string>{"church.example.org:7433"});
        CHECK(f.members().find("server")->role == Role::Viewer);
        CHECK(f.members().find("server")->holds_admin_key);
        // It holds the library, and writes none of it.
        CHECK_FALSE(f.members().may_write("server"));
        CHECK_FALSE(f.members().may_have_written("server", 1));
    }
    SECTION("an expired invite, and one for another library")
    {
        const auto code = f.log.invite(Role::Editor, k_expired, f.admin());
        MemberLog late(f.store, who("late"), k_library);
        CHECK_THROWS_WITH(late.join(read_invite(code)), ContainsSubstring("expired"));
        MemberLog other(f.store, who("other"), "ffffffffffffffffffffffffffffffff");
        CHECK_THROWS_WITH(other.join(read_invite(f.log.invite(Role::Editor, k_expires, f.admin()))),
                          ContainsSubstring("another library"));
        CHECK(f.members().members.size() == 3);
    }
    SECTION("an invite not signed by the admin key admits nobody")
    {
        seventhslide::sync::Invite forged;
        forged.library = k_library;
        forged.expires = k_expires;
        forged.id = "0000000000000000";
        const auto code = seventhslide::sync::invite_code(forged, SigningKey::generate());
        MemberLog(f.store, who("intruder"), k_library).join(read_invite(code));
        const auto after = f.members();
        CHECK(after.find("intruder") == nullptr);
        REQUIRE(after.refused.size() == 1);
        CHECK(after.refused[0].machine == "intruder");
        CHECK_THAT(after.refused[0].why, ContainsSubstring("admin key"));
    }
    SECTION("the same computer joining again is nothing new; a machine id is never two")
    {
        f.admit(laptop);
        CHECK(f.members().members.size() == 3);
        CHECK(f.members().refused.empty());
        f.admit({"laptop", "Laptop (reinstalled)", SigningKey::generate()});
        const auto after = f.members();
        CHECK(after.find("laptop")->key == laptop.key.public_key());
        REQUIRE(after.refused.size() == 1);
        CHECK_THAT(after.refused[0].why, ContainsSubstring("machine id"));
    }
}

TEST_CASE("A join written before its computer had the list comes after what the invite saw",
          "[sync][members]")
{
    // The founder's id sorts after the joining computer's, and the password
    // changed before the invite: the join must still come after both.
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir dir;
    FolderStore church_store{dir.path / "church"};
    const auto church = who("zz-church");
    MemberLog church_log{church_store, church, k_library};
    const auto founded = church_log.found(k_passphrase, k_quick);
    const auto changed = church_log.replace_admin("consider the ravens now", founded.admin, k_quick);
    const auto code = church_log.invite(Role::Editor, k_expires, changed.admin);
    CHECK(read_invite(code).invite.seen == std::map<std::string, std::int64_t>{{"zz-church", 2}});

    // The laptop writes its join into a store with nothing else in it yet.
    FolderStore laptop_store{dir.path / "laptop"};
    const auto laptop = who("aa-laptop");
    MemberLog(laptop_store, laptop, k_library).join(read_invite(code));
    {
        const auto alone = read_members(laptop_store, k_library);
        CHECK_FALSE(alone.founded());
        REQUIRE(alone.waiting.size() == 1);  // for what the invite saw
    }
    copy_entries(church_store, laptop_store);
    copy_entries(laptop_store, church_store);
    for (auto* store : {&church_store, &laptop_store}) {
        const auto m = read_members(*store, k_library);
        INFO(describe(m));
        CHECK(m.refused.empty());
        CHECK(m.waiting.empty());
        REQUIRE(m.find("aa-laptop") != nullptr);
        CHECK(m.find("aa-laptop")->role == Role::Editor);
    }
}

TEST_CASE("A computer takes itself off the list, viewer or editor, its work kept",
          "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    const auto pew = who("pew");
    f.admit(laptop);
    f.admit(pew, Role::Viewer);
    f.wrote("laptop", 3);
    MemberLog(f.store, laptop, k_library).leave();
    MemberLog(f.store, pew, k_library).leave();
    const auto m = f.members();
    INFO(describe(m));
    CHECK(m.refused.empty());
    CHECK(m.find("laptop")->removed);
    CHECK(m.find("laptop")->files_kept == 3);
    CHECK(m.find("pew")->removed);
    CHECK(m.may_have_written("laptop", 3));
    CHECK_FALSE(m.may_have_written("laptop", 4));
    // Once off, for good: it leaves no second time, and joins no more.
    CHECK_THROWS(MemberLog(f.store, laptop, k_library).leave());
    // No other computer can take one off in its name: the entry must be
    // signed by the computer that leaves.
    const auto office = who("office");
    f.admit(office);
    const Identity forged{"office", "office", SigningKey::generate()};
    write_entry(f.store, forged, 2, seen_in(f.store, "office"), "leave", json{{"files_kept", 0}}, {});
    const auto after = f.members();
    CHECK_FALSE(after.find("office")->removed);
    CHECK(refused_for(after, "office", "not that computer's"));
}

TEST_CASE("A computer gives itself a new name on the list, viewer or editor, and no other can",
          "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    const auto pew = who("pew");
    f.admit(laptop);
    f.admit(pew, Role::Viewer);
    MemberLog(f.store, laptop, k_library).rename("Pastor's Laptop");
    MemberLog(f.store, pew, k_library).rename("Stage Display");
    MemberLog(f.store, laptop, k_library).rename("Pastor's Laptop (2)");
    const auto m = f.members();
    INFO(describe(m));
    CHECK(m.refused.empty());
    // The later of its names counts; nothing else about it changes.
    CHECK(m.find("laptop")->name == "Pastor's Laptop (2)");
    CHECK(m.find("laptop")->role == Role::Editor);
    CHECK(m.find("pew")->name == "Stage Display");
    CHECK(m.find("pew")->role == Role::Viewer);

    // No other computer can rename one: the entry must be signed by the
    // computer it names, which is the computer that wrote it.
    const auto office = who("office");
    f.admit(office);
    const Identity forged{"office", "office", SigningKey::generate()};
    write_entry(f.store, forged, 2, seen_in(f.store, "office"), "name", json{{"name", "Mine"}}, {});
    const auto after = f.members();
    CHECK(after.find("office")->name == "office");
    CHECK(refused_for(after, "office", "not that computer's"));

    // Off the list, it names itself no more.
    MemberLog(f.store, laptop, k_library).leave();
    CHECK_THROWS(MemberLog(f.store, laptop, k_library).rename("Gone"));
}

TEST_CASE("A member says where it takes connections, and invites name each that does",
          "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    const auto hall = who("hall");
    const auto stage = who("stage");
    const auto pew = who("pew");
    f.admit(laptop);
    f.admit(hall);
    f.admit(stage);
    f.admit(pew, Role::Viewer);
    const auto server = who("server");
    f.admit(server, Role::Viewer);
    MemberLog(f.store, server, k_library).set_addresses({"church.example.org:7433"});
    // No password: a computer says this of itself alone.
    MemberLog(f.store, laptop, k_library).set_addresses({"192.168.1.20:8766"});
    MemberLog(f.store, hall, k_library).set_addresses({"192.168.1.30:8766", "[fd00::30]:8766"});
    MemberLog(f.store, stage, k_library).set_addresses({"192.168.1.40:8766"});
    // A server moved: it says where it is now.
    MemberLog(f.store, server, k_library).set_addresses({"sync.church.example.org:7433"});
    const auto m = f.members();
    INFO(describe(m));
    CHECK(m.refused.empty());
    CHECK(m.find("laptop")->addresses == std::vector<std::string>{"192.168.1.20:8766"});
    CHECK(m.find("hall")->addresses == std::vector<std::string>{"192.168.1.30:8766", "[fd00::30]:8766"});
    CHECK(m.find("server")->addresses == std::vector<std::string>{"sync.church.example.org:7433"});
    CHECK(m.find("church")->addresses.empty());

    // The invite names each one that takes connections, the church not.
    const auto machines_in = [](const seventhslide::sync::SignedInvite& invite) {
        std::vector<std::string> out;
        for (const auto& c : invite.invite.through) {
            out.push_back(c.machine);
        }
        return out;
    };
    const auto invite = read_invite(f.log.invite(Role::Editor, k_expires, f.admin()));
    CHECK(machines_in(invite) == std::vector<std::string>{"hall", "laptop", "server", "stage"});
    CHECK(invite.invite.through[1].key == laptop.key.public_key());
    CHECK(invite.invite.through[1].addresses == std::vector<std::string>{"192.168.1.20:8766"});

    // A viewer says so too: it is a computer always on, that keeps the library
    // for the others, as often as not.
    MemberLog(f.store, pew, k_library).set_addresses({"192.168.1.50:8766"});
    CHECK(f.members().find("pew")->addresses == std::vector<std::string>{"192.168.1.50:8766"});
    CHECK(f.members().refused.empty());

    // Its role changing leaves them as they are.
    f.log.set_role("hall", Role::Viewer, f.admin());
    CHECK(f.members().find("hall")->addresses
          == std::vector<std::string>{"192.168.1.30:8766", "[fd00::30]:8766"});
    f.log.set_role("hall", Role::Editor, f.admin());
    CHECK(f.members().find("hall")->addresses
          == std::vector<std::string>{"192.168.1.30:8766", "[fd00::30]:8766"});
    MemberLog(f.store, hall, k_library).set_addresses({});
    MemberLog(f.store, pew, k_library).set_addresses({});

    // None, and it is named no more; nor once it is off the list.
    MemberLog(f.store, laptop, k_library).set_addresses({});
    MemberLog(f.store, stage, k_library).leave();
    const auto later = read_invite(f.log.invite(Role::Editor, k_expires, f.admin()));
    CHECK(machines_in(later) == std::vector<std::string>{"server"});
    CHECK_THROWS(MemberLog(f.store, stage, k_library).set_addresses({"192.168.1.40:8766"}));

    // Only the computer itself, and only addresses.
    CHECK_THROWS_AS(MemberLog(f.store, laptop, k_library).set_addresses({"no port"}), std::invalid_argument);
    CHECK_THROWS_AS(MemberLog(f.store, laptop, k_library)
                        .set_addresses(std::vector<std::string>(seventhslide::sync::k_most_addresses + 1,
                                                                "10.0.0.1:8766")),
                    std::invalid_argument);
    // Nor is anything written that would not read back.
    CHECK_THROWS_AS(f.log.invite(Role::Editor, k_expires, f.admin(), {},
                                 seventhslide::sync::ComputerAddress{"church", who("church").key.public_key(),
                                                                     {"10.0.0.5:0"}}),
                    std::invalid_argument);
    const auto office = who("office");
    f.admit(office);
    const Identity forged{"office", "office", SigningKey::generate()};
    write_entry(f.store, forged, 2, seen_in(f.store, "office"), "addresses",
                json{{"addresses", {"203.0.113.9:8766"}}}, {});
    write_entry(f.store, laptop, 4, seen_in(f.store, "laptop"), "addresses",
                json{{"addresses", {"not an address"}}}, {});
    const auto after = f.members();
    CHECK(after.find("office")->addresses.empty());
    CHECK(refused_for(after, "office", "not that computer's"));
    CHECK(after.find("laptop")->addresses.empty());
    CHECK(refused_for(after, "laptop", "an address is not one"));
}

TEST_CASE("An invite carries the library's name and the computer that made it", "[sync][members]")
{
    Founded f;
    const auto church_key = f.church.key.public_key();
    const auto code = f.log.invite(Role::Editor, k_expires, f.admin(), "Grace Church",
                                   seventhslide::sync::ComputerAddress{"church", church_key, {"10.0.0.5:8766"}});
    const auto invite = read_invite(code).invite;
    CHECK(invite.name == "Grace Church");
    REQUIRE(invite.from.has_value());
    CHECK(invite.from->machine == "church");
    CHECK(invite.from->key == church_key);
    CHECK(invite.from->addresses == std::vector<std::string>{"10.0.0.5:8766"});
    CHECK_FALSE(read_invite(f.log.invite(Role::Viewer, k_expires, f.admin())).invite.from.has_value());
}

TEST_CASE("Roles change, and a removal is final", "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    f.admit(laptop);
    f.wrote("laptop", 3);

    f.log.set_role("laptop", Role::Viewer, f.admin());
    auto m = f.members();
    CHECK(m.find("laptop")->role == Role::Viewer);
    CHECK(m.find("laptop")->files_kept == 3);
    CHECK(m.may_have_written("laptop", 3));
    CHECK_FALSE(m.may_have_written("laptop", 4));
    CHECK_FALSE(m.may_write("laptop"));

    f.log.set_role("laptop", Role::Editor, f.admin());
    m = f.members();
    CHECK(m.find("laptop")->role == Role::Editor);
    CHECK(m.may_have_written("laptop", 4));

    f.wrote("laptop", 5);
    f.log.remove("laptop", f.admin());
    m = f.members();
    CHECK(m.find("laptop")->removed);
    CHECK(m.find("laptop")->files_kept == 5);
    CHECK(m.may_have_written("laptop", 5));
    CHECK_FALSE(m.may_have_written("laptop", 6));
    CHECK_FALSE(m.may_write("laptop"));

    // Nothing about it counts after.
    f.log.set_role("laptop", Role::Editor, f.admin());
    m = f.members();
    CHECK(m.find("laptop")->removed);
    REQUIRE(m.refused.size() == 1);
    CHECK_THAT(m.refused[0].why, ContainsSubstring("not a member"));

    SECTION("the size limit")
    {
        f.log.set_size_limit(50LL * 1024 * 1024 * 1024, f.admin());
        CHECK(f.members().size_limit == 50LL * 1024 * 1024 * 1024);
        CHECK_THROWS_AS(f.log.set_size_limit(-1, f.admin()), std::invalid_argument);
    }
    SECTION("a viewer with the admin key may be made an editor")
    {
        f.admit(who("server"), Role::Viewer);
        f.log.set_key_holder("server", true, f.admin());
        CHECK(f.members().find("server")->role == Role::Viewer);
        CHECK_FALSE(f.members().may_have_written("server", 1));
        f.log.set_role("server", Role::Editor, f.admin());
        m = f.members();
        CHECK(m.find("server")->role == Role::Editor);
        CHECK(m.may_have_written("server", 1));
        // Made a viewer again, it holds no admin key until given one.
        f.log.set_role("server", Role::Viewer, f.admin());
        CHECK_FALSE(f.members().find("server")->holds_admin_key);
    }
    SECTION("a viewer given the admin key, by the admin key alone")
    {
        const auto pew = who("pew");
        f.admit(pew, Role::Viewer);
        CHECK_FALSE(f.members().find("pew")->holds_admin_key);
        f.log.set_key_holder("pew", true, f.admin());
        CHECK(f.members().find("pew")->holds_admin_key);
        // It writes none of the library all the same.
        CHECK_FALSE(f.members().may_write("pew"));
        CHECK_FALSE(f.members().may_have_written("pew", 1));
        // It does admin work there, with the key; told to stop, it does no more.
        MemberLog(f.store, pew, k_library).set_size_limit(7, f.admin());
        CHECK(f.members().size_limit == 7);
        f.log.set_key_holder("pew", false, f.admin());
        CHECK_FALSE(f.members().find("pew")->holds_admin_key);
        MemberLog(f.store, pew, k_library).set_size_limit(9, f.admin());
        CHECK(f.members().size_limit == 7);
        CHECK(refused_for(f.members(), "pew", "may not"));
        f.log.set_key_holder("pew", true, SigningKey::generate());
        CHECK_FALSE(f.members().find("pew")->holds_admin_key);
        CHECK(refused_for(f.members(), "church", "admin key"));
    }
    SECTION("admin work without the admin key counts for nothing")
    {
        const auto office = who("office");
        f.admit(office);
        f.log.set_role("office", Role::Viewer, SigningKey::generate());
        m = f.members();
        CHECK(m.find("office")->role == Role::Editor);
        REQUIRE(m.refused.size() == 2);
        CHECK_THAT(m.refused[1].why, ContainsSubstring("admin key"));
    }
    SECTION("a viewer may not do admin work, even with the key")
    {
        const auto pew = who("pew");
        f.admit(pew, Role::Viewer);
        MemberLog(f.store, pew, k_library).set_size_limit(1, f.admin());
        m = f.members();
        CHECK(m.size_limit == 0);
        CHECK_THAT(m.refused.back().why, ContainsSubstring("may not"));
    }
}

TEST_CASE("Two computers apart: a removal wins over a role change", "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    const auto office = who("office");
    f.admit(laptop);
    f.admit(office);

    // The laptop has the same list, then the two work apart.
    seventhslide::test::TempDir dir;
    FolderStore laptop_store(dir.path / "sync");
    copy_entries(f.store, laptop_store);
    MemberLog on_laptop(laptop_store, laptop, k_library);

    SECTION("neither having seen the other")
    {
        f.log.remove("office", f.admin());
        on_laptop.set_role("office", Role::Viewer, f.admin());
    }
    SECTION("the laptop had seen more")
    {
        f.log.remove("office", f.admin());
        on_laptop.set_size_limit(1, f.admin());
        on_laptop.set_role("office", Role::Viewer, f.admin());
    }
    SECTION("the church had seen more")
    {
        on_laptop.set_role("office", Role::Viewer, f.admin());
        f.log.set_size_limit(1, f.admin());
        f.log.remove("office", f.admin());
    }
    copy_entries(laptop_store, f.store);
    copy_entries(f.store, laptop_store);
    const auto at_church = f.members();
    const auto at_laptop = read_members(laptop_store, k_library);
    CHECK(at_church.find("office")->removed);
    CHECK(at_laptop.find("office")->removed);
    CHECK(at_church.members == at_laptop.members);
    CHECK(at_church.size_limit == at_laptop.size_limit);
}

TEST_CASE("Changing the password replaces the admin key, and the old one signs nothing after",
          "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    f.admit(laptop);
    const auto old_admin = f.admin();

    const auto changed = f.log.replace_admin("roses of sharon", f.admin(), k_quick);
    CHECK(changed.recovery_code.empty());  // the code on paper stands
    auto m = f.members();
    CHECK(*m.admin == changed.admin.public_key());
    CHECK(*m.recovery == seventhslide::sync::key_from_recovery_code(f.founded.recovery_code)
                             ->public_key());
    CHECK(m.refused.empty());
    CHECK(f.store.admin_keys().size() == 2);
    CHECK(f.log.unlock_admin("roses of sharon") == changed.admin);
    CHECK_FALSE(f.log.unlock_admin(k_passphrase).has_value());

    // Someone who kept the old key gets nowhere.
    f.log.set_role("laptop", Role::Viewer, old_admin);
    m = f.members();
    CHECK(m.find("laptop")->role == Role::Editor);
    REQUIRE(m.refused.size() == 1);
    CHECK_THAT(m.refused[0].why, ContainsSubstring("admin key"));
    // An invite made with the old key admits nobody either.
    MemberLog(f.store, who("late"), k_library)
        .join(read_invite(seventhslide::sync::invite_code(
            {k_library, Role::Editor, k_expires, "0000000000000000", {}}, old_admin)));
    CHECK(f.members().find("late") == nullptr);

    SECTION("the recovery code sets a new password, and nothing else")
    {
        const auto recovery = *seventhslide::sync::key_from_recovery_code(f.founded.recovery_code);
        f.log.set_role("laptop", Role::Viewer, recovery);
        CHECK(f.members().find("laptop")->role == Role::Editor);

        const auto recovered = f.log.replace_admin("hyssop and myrrh", recovery, k_quick);
        CHECK_FALSE(recovered.recovery_code.empty());  // the code is spent: a new one
        m = f.members();
        CHECK(*m.admin == recovered.admin.public_key());
        CHECK(*m.recovery == seventhslide::sync::key_from_recovery_code(recovered.recovery_code)
                                 ->public_key());
        CHECK(f.log.unlock_admin("hyssop and myrrh") == recovered.admin);
        // The code is spent with the key it belonged to.
        f.log.replace_admin("cedars of lebanon", recovery, k_quick);
        CHECK(*f.members().admin == recovered.admin.public_key());
        CHECK(f.log.unlock_admin("hyssop and myrrh") == recovered.admin);
        CHECK_FALSE(f.log.unlock_admin("cedars of lebanon").has_value());
    }
    SECTION("a new recovery code with the password")
    {
        const auto code = f.log.replace_recovery(changed.admin);
        CHECK(code != f.founded.recovery_code);
        CHECK(*f.members().recovery
              == seventhslide::sync::key_from_recovery_code(code)->public_key());
    }
}

TEST_CASE("The gate lets in only what members may have written", "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    const auto pew = who("pew");
    const auto stranger = who("stranger");
    f.admit(laptop);
    f.admit(pew, Role::Viewer);
    const Gate gate(f.members());

    seventhslide::sync::ChangeFile file;
    file.library = k_library;
    file.machine = "laptop";
    file.sequence = 1;
    const auto body = seventhslide::sync::change_file_to_json(file);
    const auto by = [&](const Identity& id, const json& j) {
        return seal(j, {{k_signed_as_computer, &id.key}});
    };

    CHECK(gate.change_file("laptop", 1, by(laptop, body)).machine == "laptop");
    CHECK_THROWS_WITH(gate.change_file("laptop", 1, by(stranger, body)),
                      ContainsSubstring("not that computer's"));
    CHECK_THROWS_WITH(gate.change_file("laptop", 2, by(laptop, body)),
                      ContainsSubstring("not the file its name says"));
    file.machine = "stranger";
    CHECK_THROWS_AS(gate.change_file("stranger", 1, by(stranger, change_file_to_json(file))),
                    seventhslide::sync::NotYet);
    file.machine = "pew";
    CHECK_THROWS_WITH(gate.change_file("pew", 1, by(pew, change_file_to_json(file))),
                      ContainsSubstring("may not change"));
    file.machine = "laptop";
    file.library = "ffffffffffffffffffffffffffffffff";
    CHECK_THROWS_WITH(gate.change_file("laptop", 1, by(laptop, change_file_to_json(file))),
                      ContainsSubstring("another library"));

    SECTION("snapshots")
    {
        seventhslide::sync::SnapshotIndex snapshot;
        snapshot.library = k_library;
        snapshot.machine = "laptop";
        snapshot.files = {{"church", 2}};
        CHECK(gate.snapshot({"laptop", 2}, by(laptop, snapshot_index_to_json(snapshot))).machine
              == "laptop");
        CHECK_THROWS_WITH(gate.snapshot({"laptop", 3}, by(laptop, snapshot_index_to_json(snapshot))),
                          ContainsSubstring("not the snapshot its name says"));
        snapshot.machine = "pew";
        CHECK_THROWS_WITH(gate.snapshot({"pew", 2}, by(pew, snapshot_index_to_json(snapshot))),
                          ContainsSubstring("may not write snapshots"));
        // One of an earlier format, with no parts, is not one.
        auto earlier = snapshot_index_to_json(snapshot);
        earlier["machine"] = "laptop";
        earlier["format"] = 2;
        CHECK_THROWS_WITH(gate.snapshot({"laptop", 2}, by(laptop, earlier)), ContainsSubstring("not a snapshot in parts"));
        // One of a later format, by its label.
        auto later = earlier;
        later["format"] = seventhslide::sync::k_snapshot_format + 1;
        later.erase("records");
        CHECK(gate.snapshot({"laptop", 2}, by(laptop, later)).format == seventhslide::sync::k_snapshot_format + 1);
    }
    SECTION("snapshots' parts")
    {
        using seventhslide::sync::Gate;
        const std::string part = R"({"format":3,"records":[],"held":[],"media":[]})";
        const auto sha256 = seventhslide::crypto::sha256_hex(part);
        CHECK_NOTHROW(Gate::snapshot_part(sha256, part));
        CHECK_THROWS_WITH(Gate::snapshot_part(sha256, part + " "), ContainsSubstring("not the part its snapshot names"));
        const std::string broken = R"({"format":3,"records":[1],"held":[],"media":[]})";
        CHECK_THROWS_WITH(Gate::snapshot_part(seventhslide::crypto::sha256_hex(broken), broken),
                          ContainsSubstring("could not be read"));
        // A later format's, by its hash alone.
        const std::string later = R"({"format":4,"whatever":"comes"})";
        CHECK_NOTHROW(Gate::snapshot_part(seventhslide::crypto::sha256_hex(later), later));
    }
    SECTION("the key of a viewer with the admin key writes none of the library")
    {
        const auto server = who("server");
        f.admit(server, Role::Viewer);
        f.log.set_key_holder("server", true, f.admin());
        const Gate after(f.members());
        file.library = k_library;
        file.machine = "server";
        file.sequence = 1;
        CHECK_THROWS_AS(after.change_file("server", 1, by(server, change_file_to_json(file))),
                        seventhslide::sync::NotYet);
        seventhslide::sync::SnapshotIndex snapshot;
        snapshot.library = k_library;
        snapshot.machine = "server";
        snapshot.files = {{"church", 2}};
        CHECK_THROWS_WITH(after.snapshot({"server", 2}, by(server, snapshot_index_to_json(snapshot))),
                          ContainsSubstring("may not write snapshots"));
        // Its own entry still goes: where it is, what it is called.
        MemberLog(f.store, server, k_library).set_addresses({"sync.church.example.org:7433"});
        CHECK_NOTHROW(after.member_entry("server", 2, f.store.read(Numbered::Members, "server", 2)));
    }
    SECTION("member list entries")
    {
        // The two joins pass; a founding entry does not, the library being
        // founded (the one it has is never offered again).
        CHECK_NOTHROW(gate.member_entry("laptop", 1, f.store.read(Numbered::Members, "laptop", 1)));
        CHECK_NOTHROW(gate.member_entry("pew", 1, f.store.read(Numbered::Members, "pew", 1)));
        CHECK_THROWS_WITH(gate.member_entry("church", 1, f.store.read(Numbered::Members, "church", 1)),
                          ContainsSubstring("founded already"));
        // A second founding, a join on a forged invite, a viewer's admin work.
        seventhslide::test::TempDir dir;
        FolderStore other(dir.path / "sync");
        MemberLog(other, who("usurper"), k_library).found(k_passphrase, k_quick);
        CHECK_THROWS_WITH(gate.member_entry("usurper", 1, other.read(Numbered::Members, "usurper", 1)),
                          ContainsSubstring("founded already"));
        // Genuine entries are let in, whether or not they count: that is the
        // list's to resolve, the same everywhere. A join on a forged invite,
        // a viewer's admin work.
        MemberLog(other, who("intruder"), k_library)
            .join(read_invite(seventhslide::sync::invite_code(
                {k_library, Role::Editor, k_expires, "0000000000000000", {}},
                SigningKey::generate())));
        CHECK_NOTHROW(gate.member_entry("intruder", 1, other.read(Numbered::Members, "intruder", 1)));
        MemberLog(other, pew, k_library).set_size_limit(1, f.admin());
        CHECK_NOTHROW(gate.member_entry("pew", 1, other.read(Numbered::Members, "pew", 1)));
        // Not genuine: under another name, signed by another, from nobody known.
        CHECK_THROWS_WITH(gate.member_entry("pew", 2, other.read(Numbered::Members, "pew", 1)),
                          ContainsSubstring("not the entry its name says"));
        MemberLog(other, {"pew", "pew", SigningKey::generate()}, k_library).set_size_limit(2, f.admin());
        CHECK_THROWS_WITH(gate.member_entry("pew", 2, other.read(Numbered::Members, "pew", 2)),
                          ContainsSubstring("not that computer's"));
        MemberLog(other, who("nobody"), k_library).set_size_limit(3, f.admin());
        CHECK_THROWS_AS(gate.member_entry("nobody", 1, other.read(Numbered::Members, "nobody", 1)),
                        seventhslide::sync::NotYet);
    }
    SECTION("locked admin keys")
    {
        const auto fingerprint = f.admin().public_key().fingerprint();
        const auto text = f.store.read_admin_key(fingerprint);
        CHECK_NOTHROW(gate.admin_key(fingerprint, text));
        CHECK_THROWS_WITH(gate.admin_key(std::string(64, '0'), text),
                          ContainsSubstring("not the key its name says"));
        const auto other = SigningKey::generate();
        CHECK_THROWS_WITH(gate.admin_key(other.public_key().fingerprint(),
                                         seventhslide::sync::lock_admin_key(other, "x", k_quick)
                                             .to_json()
                                             .dump()),
                          ContainsSubstring("not this library's admin key"));
        CHECK_THROWS_WITH(gate.admin_key(fingerprint, "{}"), ContainsSubstring("not one"));
    }
}

TEST_CASE("A signed envelope is read back as it was, or refused", "[sync][members]")
{
    const auto key = SigningKey::generate();
    const auto admin = SigningKey::generate();
    const json body{{"kind", "test"}, {"n", 3}, {"list", {1, 2}}};
    const auto text = seal(body, {{k_signed_as_computer, &key}, {k_signed_as_admin, &admin}});
    const auto back = unseal(text, "A test");
    CHECK(back.body == body);
    REQUIRE(back.signatures.size() == 2);
    CHECK(*back.signed_as(k_signed_as_computer) == key.public_key());
    CHECK(*back.signed_as(k_signed_as_admin) == admin.public_key());
    CHECK(back.signed_as("recovery") == nullptr);

    // A line of who signed it, then the body's bytes, which the signatures
    // cover as they are: the same document written another way is not
    // what was signed.
    const auto line = text.find('\n');
    REQUIRE(line != std::string::npos);
    const auto head = text.substr(0, line);
    CHECK(text.substr(line + 1) == body.dump());
    CHECK_THROWS_WITH(unseal(head + "\n" + body.dump(2), "A test"), ContainsSubstring("not its key's"));
    auto changed = body;
    changed["n"] = 4;
    CHECK_THROWS_WITH(unseal(head + "\n" + changed.dump(), "A test"), ContainsSubstring("not its key's"));
    auto unsigned_head = json::parse(head);
    unsigned_head["signatures"] = json::array();
    CHECK_THROWS_WITH(unseal(unsigned_head.dump() + "\n" + body.dump(), "A test"),
                      ContainsSubstring("not signed"));
    auto newer = json::parse(head);
    newer["format"] = 99;
    CHECK_THROWS_WITH(unseal(newer.dump() + "\n" + body.dump(), "A test"), ContainsSubstring("newer version"));
    // A head with no body after it, or of a format below this one's, is none.
    CHECK_THROWS_WITH(unseal(head, "A test"), ContainsSubstring("not a signed document"));
    auto lower = json::parse(head);
    lower["format"] = 1;
    CHECK_THROWS_WITH(unseal(lower.dump() + "\n" + body.dump(), "A test"),
                      ContainsSubstring("not a signed document"));
    CHECK_THROWS_WITH(unseal("[]", "A test"), ContainsSubstring("A test could not be read"));
    CHECK_THROWS_WITH(unseal(body.dump(), "A test"), ContainsSubstring("no format"));
}

TEST_CASE("An entry made with an old admin key does not count, however early it claims to be",
          "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    const auto office = who("office");
    f.admit(laptop);
    f.admit(office);
    const auto old_admin = f.admin();

    f.log.replace_admin("roses of sharon", f.admin(), k_quick);
    const auto new_admin_key = *f.members().admin;

    // The laptop, holding the old key, writes as if it had not seen the
    // change: it claims to have seen only the founding and the office's join.
    write_entry(f.store, laptop, 2, {{"church", 1}, {"office", 1}}, "role",
                {{"target", "office"}, {"role", "viewer"}, {"files_kept", 0}},
                {{k_signed_as_admin, &old_admin}});
    const auto m = f.members();
    INFO(describe(m));
    CHECK(*m.admin == new_admin_key);
    CHECK(m.find("office")->role == Role::Editor);
    CHECK(refused_for(m, "laptop", "admin key replaced meanwhile"));
}

TEST_CASE("A removed computer's entries made apart from its removal do not count",
          "[sync][members]")
{
    Founded f;
    // "abel" sorts before "church": where two password changes tie, its
    // would win.
    const auto abel = who("abel");
    f.admit(abel);
    const auto old_admin = f.admin();
    const auto apart = seen_in(f.store, "abel");

    // The church removes abel, then changes the password.
    f.log.remove("abel", f.admin());
    const auto changed = f.log.replace_admin("roses of sharon", f.admin(), k_quick);

    // Abel, apart, holding the old password, tries to take the library: a
    // password change of its own, and a size limit.
    const auto stolen = SigningKey::generate();
    write_entry(f.store, abel, 2, apart, "admin", {{"admin", stolen.public_key().base64()}},
                {{k_signed_as_admin, &old_admin}});
    write_entry(f.store, abel, 3, apart, "limit", {{"bytes", 1}},
                {{k_signed_as_admin, &old_admin}});

    const auto m = f.members();
    INFO(describe(m));
    CHECK(m.find("abel")->removed);
    CHECK(*m.admin == changed.admin.public_key());
    CHECK(m.size_limit == 0);
    CHECK(refused_for(m, "abel", "removal"));
    CHECK(f.log.unlock_admin("roses of sharon") == changed.admin);
}

TEST_CASE("The recovery code wins over a password change made apart from it", "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    f.admit(laptop);
    seventhslide::test::TempDir dir;
    FolderStore laptop_store(dir.path / "sync");
    copy_entries(f.store, laptop_store);

    // The laptop changes the password; the church, apart, recovers with the
    // code.
    MemberLog(laptop_store, laptop, k_library).replace_admin("thorns and thistles", f.admin(), k_quick);
    const auto recovery = *seventhslide::sync::key_from_recovery_code(f.founded.recovery_code);
    const auto recovered = f.log.replace_admin("roses of sharon", recovery, k_quick);
    copy_entries(laptop_store, f.store);
    copy_entries(f.store, laptop_store);

    for (auto* store : {&f.store, &laptop_store}) {
        const auto m = read_members(*store, k_library);
        INFO(describe(m));
        CHECK(*m.admin == recovered.admin.public_key());
        CHECK(refused_for(m, "laptop", "admin key replaced meanwhile"));
    }
}

TEST_CASE("What an invite leaves on the list gets nobody else in", "[sync][members]")
{
    Founded f;
    const auto pew = who("pew");
    f.admit(pew, Role::Viewer);
    const auto code = f.log.invite(Role::Editor, k_expires, f.admin());
    MemberLog(f.store, who("laptop"), k_library).join(read_invite(code));
    const auto invite_on_the_list = [&] {
        return unseal(f.store.read(Numbered::Members, "laptop", 1), "entry").body.at("invite").get<std::string>();
    };
    // A computer that has seen nothing since the invite was made, as a viewer
    // with the list could pretend to be.
    const auto first_entries = [&](const Identity&) {
        return json{{"church", read_invite(code).invite.seen.at("church")}};
    };

    SECTION("the secret is not on the list")
    {
        CHECK(f.store.read(Numbered::Members, "laptop", 1).find(seventhslide::crypto::to_base64(read_invite(code).secret->seed()))
              == std::string::npos);
        CHECK_FALSE(read_invite(invite_on_the_list()).secret.has_value());
        // MemberLog will not write a join without it.
        MemberLog thief(f.store, who("thief"), k_library);
        CHECK_THROWS_WITH(thief.join(read_invite(invite_on_the_list())), ContainsSubstring("Ask for a new one"));
    }
    SECTION("a join on it, unproved, is refused")
    {
        const auto thief = who("thief");
        write_entry(f.store, thief, 1, first_entries(thief), "join",
                    {{"computer", {{"machine", "thief"}, {"key", thief.key.public_key().base64()}}},
                     {"invite", invite_on_the_list()}},
                    {});
        const auto m = f.members();
        CHECK(m.find("thief") == nullptr);
        CHECK(refused_for(m, "thief", "does not prove"));
    }
    SECTION("a proof by another key is no proof")
    {
        const auto thief = who("thief");
        const auto other = SigningKey::generate();
        write_entry(f.store, thief, 1, first_entries(thief), "join",
                    {{"computer", {{"machine", "thief"}, {"key", thief.key.public_key().base64()}}},
                     {"invite", invite_on_the_list()}},
                    {{seventhslide::sync::k_signed_as_invite, &other}});
        CHECK(f.members().find("thief") == nullptr);
        CHECK(refused_for(f.members(), "thief", "does not prove"));
    }
    SECTION("the laptop's entry proves the invite for the laptop only")
    {
        const auto entry = f.store.read(Numbered::Members, "laptop", 1);
        const auto laptop_key = *unseal(entry, "entry").signed_as(k_signed_as_computer);
        CHECK(seventhslide::sync::proved_invite(entry, k_library, laptop_key).has_value());
        CHECK_FALSE(seventhslide::sync::proved_invite(entry, k_library, pew.key.public_key()).has_value());
        CHECK_FALSE(seventhslide::sync::proved_invite(entry, "ffffffffffffffffffffffffffffffff", laptop_key).has_value());
        CHECK_FALSE(seventhslide::sync::proved_invite("not an entry", k_library, laptop_key).has_value());
    }
}

TEST_CASE("A join on an invite without a proof is refused", "[sync][members]")
{
    Founded f;
    seventhslide::sync::Invite bare;
    bare.library = k_library;
    bare.role = Role::Editor;
    bare.expires = k_expires;
    bare.id = "0123456701234567";
    bare.seen = {{"church", 1}};
    auto body = bare.to_json();
    body.erase("proof");
    const auto sealed = seal(body, {{k_signed_as_admin, &f.admin()}});
    CHECK_THROWS_WITH(read_invite(sealed), ContainsSubstring("no proof"));

    const auto laptop = who("laptop");
    const auto one_time = SigningKey::generate();
    write_entry(f.store, laptop, 1, {{"church", 1}}, "join",
                {{"computer", {{"machine", "laptop"}, {"key", laptop.key.public_key().base64()}}},
                 {"invite", sealed}},
                {{seventhslide::sync::k_signed_as_invite, &one_time}});
    const auto m = f.members();
    INFO(describe(m));
    CHECK(m.find("laptop") == nullptr);
    CHECK(refused_for(m, "laptop", "no proof"));
    // Still, it joined rather than founded the library.
    CHECK(seventhslide::sync::joined_here(f.store, k_library, "laptop"));
    CHECK_FALSE(seventhslide::sync::joined_here(f.store, k_library, "church"));
    CHECK_FALSE(seventhslide::sync::joined_here(f.store, "ffffffffffffffffffffffffffffffff", "laptop"));
    CHECK_FALSE(seventhslide::sync::proved_invite(f.store.read(Numbered::Members, "laptop", 1), k_library,
                                                  laptop.key.public_key())
                    .has_value());
}

TEST_CASE("The list says which invites were used, and who has been an editor", "[sync][members]")
{
    Founded f;
    const auto code = f.log.invite(Role::Editor, k_expires, f.admin());
    const auto unused = read_invite(f.log.invite(Role::Editor, k_expires, f.admin()));
    MemberLog(f.store, who("laptop"), k_library).join(read_invite(code));
    f.admit(who("pew"), Role::Viewer);
    auto m = f.members();
    CHECK(m.invites_used.contains(read_invite(code).invite.id));
    CHECK_FALSE(m.invites_used.contains(unused.invite.id));

    CHECK(m.find("church")->has_edited);
    CHECK(m.find("laptop")->has_edited);
    CHECK_FALSE(m.find("pew")->has_edited);
    // A viewer made so keeps having been one; one made an editor has now.
    f.log.set_role("laptop", Role::Viewer, f.admin());
    CHECK(f.members().find("laptop")->has_edited);
    f.log.set_role("pew", Role::Editor, f.admin());
    f.log.set_role("pew", Role::Viewer, f.admin());
    CHECK(f.members().find("pew")->has_edited);
}

TEST_CASE("The list says each invite made, until it is used or withdrawn", "[sync][members]")
{
    Founded f;
    const auto viewer = read_invite(f.log.invite(Role::Viewer, k_expires, f.admin()));
    const auto editor = read_invite(f.log.invite(Role::Editor, k_expires, f.admin()));
    const auto expired = read_invite(f.log.invite(Role::Editor, k_expired, f.admin()));
    auto m = f.members();
    INFO(describe(m));
    REQUIRE(m.invites.size() == 3);
    CHECK(m.invites.at(viewer.invite.id)
          == seventhslide::sync::InviteMade{viewer.invite.id, Role::Viewer, k_expires, "church"});
    // The invite did not see what says it was made: a join waits for nothing
    // the computer that made it has not passed on.
    CHECK(viewer.invite.seen.at("church") == 1);
    // One run out is on the list still, but not waiting.
    const auto waiting = seventhslide::sync::waiting_invites(m);
    REQUIRE(waiting.size() == 2);
    CHECK(std::ranges::none_of(waiting, [&](const auto& w) { return w.id == expired.invite.id; }));

    MemberLog(f.store, who("pew"), k_library).join(viewer);
    m = f.members();
    CHECK_FALSE(m.invites.contains(viewer.invite.id));
    CHECK(m.invites.contains(editor.invite.id));

    f.log.withdraw_invite(editor.invite.id, f.admin());
    m = f.members();
    CHECK_FALSE(m.invites.contains(editor.invite.id));
    CHECK(m.invites_withdrawn.contains(editor.invite.id));

    // A withdrawn invite admits nobody.
    MemberLog(f.store, who("late"), k_library).join(editor);
    m = f.members();
    CHECK(m.find("late") == nullptr);
    CHECK(refused_for(m, "late", "withdrawn"));
    CHECK(m.find("pew") != nullptr);
}

TEST_CASE("A new admin key ends the invites made before it", "[sync][members]")
{
    Founded f;
    const auto before = read_invite(f.log.invite(Role::Viewer, k_expires, f.admin()));
    REQUIRE(f.members().invites.contains(before.invite.id));

    SECTION("a new password")
    {
        const auto changed = f.log.replace_admin("roses of sharon", f.admin(), k_quick);
        const auto after = read_invite(f.log.invite(Role::Editor, k_expires, changed.admin));
        const auto m = f.members();
        INFO(describe(m));
        // Not waiting: it admits nobody now. The one made since is.
        CHECK_FALSE(m.invites.contains(before.invite.id));
        CHECK(m.invites.contains(after.invite.id));
        MemberLog(f.store, who("late"), k_library).join(before);
        CHECK(f.members().find("late") == nullptr);
    }
    SECTION("the recovery code")
    {
        const auto recovery = *seventhslide::sync::key_from_recovery_code(f.founded.recovery_code);
        (void)f.log.replace_admin("hyssop and myrrh", recovery, k_quick);
        CHECK(f.members().invites.empty());
    }
}

TEST_CASE("A withdrawal cancels a join by its invite that it had not seen", "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    f.admit(laptop);
    const auto code = read_invite(f.log.invite(Role::Editor, k_expires, f.admin()));
    seventhslide::test::TempDir dir;
    FolderStore laptop_store(dir.path / "sync");
    copy_entries(f.store, laptop_store);
    MemberLog on_laptop(laptop_store, laptop, k_library);
    const auto pew = who("pew");

    SECTION("a join made apart from the withdrawal does not count")
    {
        // The pew joins at the church, which makes it a viewer; the laptop,
        // apart, withdraws the invite.
        MemberLog(f.store, pew, k_library).join(code);
        f.log.set_role("pew", Role::Viewer, f.admin());
        on_laptop.withdraw_invite(code.invite.id, f.admin());
        copy_entries(laptop_store, f.store);
        copy_entries(f.store, laptop_store);
        const auto at_church = f.members();
        const auto at_laptop = read_members(laptop_store, k_library);
        INFO(describe(at_church));
        for (const auto* m : {&at_church, &at_laptop}) {
            CHECK(m->find("pew") == nullptr);
            CHECK(refused_for(*m, "pew", "its invite was withdrawn"));
            CHECK(refused_for(*m, "church", "not a member"));
            CHECK(m->invites_withdrawn.contains(code.invite.id));
        }
        CHECK(at_church.members == at_laptop.members);
    }
    SECTION("a join the withdrawal had seen stands")
    {
        MemberLog(f.store, pew, k_library).join(code);
        copy_entries(f.store, laptop_store);
        on_laptop.withdraw_invite(code.invite.id, f.admin());
        copy_entries(laptop_store, f.store);
        const auto m = f.members();
        INFO(describe(m));
        CHECK(m.find("pew") != nullptr);
        CHECK(m.refused.empty());
    }
    SECTION("a withdrawal made with a password changed meanwhile does not count")
    {
        MemberLog(f.store, pew, k_library).join(code);
        f.log.replace_admin("roses of sharon", f.admin(), k_quick);
        on_laptop.withdraw_invite(code.invite.id, f.admin());
        copy_entries(laptop_store, f.store);
        const auto m = f.members();
        INFO(describe(m));
        CHECK(m.find("pew") != nullptr);
        CHECK(refused_for(m, "laptop", "admin key replaced meanwhile"));
        CHECK_FALSE(m.invites_withdrawn.contains(code.invite.id));
    }
}

TEST_CASE("Only a computer holding the admin key says an invite was made or withdrawn",
          "[sync][members]")
{
    Founded f;
    const auto pew = who("pew");
    f.admit(pew, Role::Viewer);
    const auto code = read_invite(f.log.invite(Role::Editor, k_expires, f.admin()));
    const auto next = static_cast<std::int64_t>(f.store.sequences(Numbered::Members, "pew").size()) + 1;
    // Signed by the pew alone: no admin key.
    write_entry(f.store, pew, next, seen_in(f.store, "pew"), "withdraw", {{"invite", code.invite.id}}, {});
    write_entry(f.store, pew, next + 1, seen_in(f.store, "pew"), "invite",
                {{"invite", "feedfacefeedface"}, {"role", "editor"}, {"expires", k_expires}}, {});
    const auto m = f.members();
    CHECK(m.invites.contains(code.invite.id));
    CHECK_FALSE(m.invites.contains("feedfacefeedface"));
    CHECK(m.invites_withdrawn.empty());
    CHECK(refused_for(m, "pew", "may not"));
}

TEST_CASE("An invite admits one computer, or two that used it apart", "[sync][members]")
{
    Founded f;
    const auto code = f.log.invite(Role::Editor, k_expires, f.admin());
    MemberLog(f.store, who("first"), k_library).join(read_invite(code));

    SECTION("used again by a computer that had seen it used")
    {
        MemberLog(f.store, who("second"), k_library).join(read_invite(code));
        const auto m = f.members();
        CHECK(m.find("first") != nullptr);
        CHECK(m.find("second") == nullptr);
        CHECK(refused_for(m, "second", "used already"));
    }
    SECTION("used by two computers apart: both are in, for the password to remove one")
    {
        seventhslide::test::TempDir dir;
        FolderStore apart(dir.path / "sync");
        for (const auto n : f.store.sequences(Numbered::Members, "church")) {
            apart.write(Numbered::Members, "church", n, f.store.read(Numbered::Members, "church", n));
        }
        MemberLog(apart, who("second"), k_library).join(read_invite(code));
        copy_entries(apart, f.store);
        const auto m = f.members();
        CHECK(m.find("first") != nullptr);
        CHECK(m.find("second") != nullptr);
        CHECK(m.refused.empty());
    }
}

TEST_CASE("An entry that saw one not here waits for it", "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    seventhslide::test::TempDir dir;
    FolderStore other(dir.path / "sync");
    copy_entries(f.store, other);
    f.admit(laptop);
    f.log.set_size_limit(10, f.admin());  // seen: laptop's join

    // A store with the church's entries but not the laptop's join.
    for (const auto n : f.store.sequences(Numbered::Members, "church")) {
        if (other.sequences(Numbered::Members, "church").size() < static_cast<std::size_t>(n)) {
            other.write(Numbered::Members, "church", n, f.store.read(Numbered::Members, "church", n));
        }
    }
    auto m = read_members(other, k_library);
    CHECK(m.size_limit == 0);
    REQUIRE(m.waiting.size() == 1);
    CHECK(m.waiting[0].machine == "church");
    // A new entry builds only on what is placed, never on one that waits:
    // the church's first and the one saying the invite was made.
    CHECK(m.heads.at("church") == 2);

    copy_entries(f.store, other);
    m = read_members(other, k_library);
    CHECK(m.size_limit == 10);
    CHECK(m.waiting.empty());
    CHECK(m.find("laptop") != nullptr);
}

TEST_CASE("A hostile entry is refused, and the list goes on", "[sync][members]")
{
    Founded f;
    const auto laptop = who("laptop");
    f.admit(laptop);
    const auto stranger = who("stranger");

    SECTION("a name that is not text")
    {
        const auto invite = read_invite(f.log.invite(Role::Editor, k_expires, f.admin()));
        write_entry(f.store, stranger, 1, seen_in(f.store, "stranger"), "join",
                    {{"computer", {{"machine", "stranger"}, {"key", stranger.key.public_key().base64()}, {"name", 5}}},
                     {"invite", invite.sealed}},
                    {{seventhslide::sync::k_signed_as_invite, &*invite.secret}});
        const auto m = f.members();
        CHECK(m.find("stranger") == nullptr);
        CHECK(refused_for(m, "stranger", "name is not text"));
    }
    SECTION("a machine id that would leave the folder")
    {
        const auto code = f.log.invite(Role::Editor, k_expires, f.admin());
        const Identity escaping{"x", "x", stranger.key};
        write_entry(f.store, escaping, 1, seen_in(f.store, "x"), "join",
                    {{"computer", {{"machine", "../../escape"}, {"key", stranger.key.public_key().base64()}}},
                     {"invite", read_invite(code).sealed}},
                    {});
        CHECK(f.members().find("../../escape") == nullptr);
        CHECK_THROWS_AS(f.store.write(Numbered::Members, "../../escape", 1, "{}"),
                        std::invalid_argument);
        CHECK_THROWS_AS(f.store.sequences(Numbered::Changes, ".."), std::invalid_argument);
        CHECK_THROWS_AS(f.store.read_admin_key("../../../etc/settings"), std::invalid_argument);
    }
    SECTION("a seen that claims far more than there is")
    {
        write_entry(f.store, laptop, 2, {{"church", std::numeric_limits<std::int64_t>::max()}},
                    "limit", {{"bytes", 1}}, {{k_signed_as_admin, &f.admin()}});
        const auto m = f.members();
        CHECK(m.size_limit == 0);
        REQUIRE(m.waiting.size() == 1);
        CHECK(m.waiting[0].machine == "laptop");
    }
    // Whatever came, the church's next entry counts.
    f.log.set_size_limit(20, f.admin());
    INFO(describe(f.members()));
    CHECK(f.members().size_limit == 20);
}
