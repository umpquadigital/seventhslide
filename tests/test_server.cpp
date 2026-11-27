// SeventhSlide as a server (server.hpp): joining by an invite, holding libraries as
// libraries of its own, passing them between computers, admin work at the
// server, coming back after a restart, leaving; and the program itself run
// as a server with no window system to be had.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/db/database.hpp>
#include <seventhslide/library/library.hpp>
#include <seventhslide/link/admin.hpp>
#include <seventhslide/link/sync_node.hpp>
#include <seventhslide/server/command_line.hpp>
#include <seventhslide/server/server.hpp>
#include <seventhslide/sync/change_file.hpp>
#include <seventhslide/sync/envelope.hpp>
#include <seventhslide/link/discovery.hpp>

#include "../src/server/commands.hpp"
#include "link_test_helpers.hpp"
#include "server_test_helpers.hpp"
#include "sync_test_helpers.hpp"
#include "test_temp_dir.hpp"

#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlQuery>

#include <QScopeGuard>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>

using Catch::Matchers::ContainsSubstring;
using nlohmann::json;
using seventhslide::server::CommandLine;
using seventhslide::server::Command;
using seventhslide::server::parse_command_line;
using seventhslide::server::Server;
using seventhslide::server::ServerOptions;
using namespace seventhslide::test::link_fixture;
using namespace seventhslide::test::server_fixture;

namespace {

std::string text(Database& db, const std::string& sql)
{
    QSqlQuery q(db.handle());
    REQUIRE(q.exec(QString::fromStdString(sql)));
    REQUIRE(q.next());
    return q.value(0).toString().toStdString();
}

}  // namespace

TEST_CASE("A computer joins a second library over its connection to the server", "[server]")
{
    Computer church{"church", "Church PC"};
    Computer school{"school", "School PC"};
    Computer laptop{"laptop", "Laptop"};
    ServerHost host;
    const auto church_admin = put_online(church);
    const auto school_admin = put_online(school);
    keep_at(church, host, church_admin.admin);
    keep_at(school, host, school_admin.admin);
    REQUIRE(settle_all({&church, &school}, host));
    REQUIRE(host->library_uids().size() == 2);
    join_via(laptop, church.uid, church.members().invite(Role::Editor, k_expires, church_admin.admin), host);
    REQUIRE(settle_all({&laptop}, host));
    REQUIRE(laptop.link.peers().size() == 1);
    auto* connection = laptop.link.peers().front();

    // The school's library too, on an invite, through the same connection.
    const auto code = school.members().invite(Role::Viewer, k_expires, school_admin.admin);
    const auto second = std::make_shared<FolderStore>(laptop.root.path / "second" / "sync");
    FolderStore& second_store = *second;
    const auto second_media = laptop.root.path / "second" / "media";
    laptop.link.add_library({school.uid, second, second_media, {}, {}, {}});
    MemberLog(second_store, laptop.me, school.uid).join(seventhslide::sync::read_invite(code));
    const auto own = seventhslide::sync::own_invite(second_store, school.uid, "laptop");
    REQUIRE(own.has_value());
    CHECK(laptop.link.connect_to("127.0.0.1", host->port(), host.key(), {{school.uid, own->entry}}) == connection);
    REQUIRE(settle_all({&laptop, &church, &school}, host));
    INFO("laptop closed: " << why(laptop));
    CHECK(laptop.link.peers().size() == 1);
    CHECK(second_store.sequences(Numbered::Changes, "school") == std::vector<std::int64_t>{1});
    CHECK(std::filesystem::exists(second_media / "videos" / "cd34.mp4"));
    CHECK(second_store.admin_keys().empty());  // a viewer's
    REQUIRE(host->library(school.uid) != nullptr);
    const auto held_list = seventhslide::sync::read_members(host->library(school.uid)->store(), school.uid);
    REQUIRE(held_list.find("laptop") != nullptr);
    CHECK(held_list.find("laptop")->role == Role::Viewer);
}

TEST_CASE("The server hears the SeventhSlides on its network, and says which", "[server]")
{
    Computer church{"church", "Church PC"};
    ServerHost host(true);
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    REQUIRE(host->discovery() != nullptr);

    seventhslide::link::Discovery::Options options;
    options.port = 0;
    options.targets = {{QHostAddress::LocalHost, host->discovery_port()}};
    seventhslide::link::Discovery church_says(church.key().fingerprint(), church.port(), options);
    REQUIRE(church_says.start());
    seventhslide::link::Discovery stranger(std::string(64, 'e'), 4242, options);
    REQUIRE(stranger.start());
    QElapsedTimer timer;
    timer.start();
    while (host->status()["neighbours"].size() < 2 && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    const auto status = host->status();
    REQUIRE(status["neighbours"].size() == 2);
    std::set<std::string> names;
    for (const auto& n : status["neighbours"]) {
        names.insert(n["name"].get<std::string>());
        CHECK(n["address"].get<std::string>().starts_with("127.0.0.1:"));
    }
    CHECK(names == std::set<std::string>{"Church PC", ""});
}

TEST_CASE("The server's command line", "[server]")
{
    const auto parse = [](std::vector<std::string> args) { return parse_command_line(args); };

    const auto run = parse({});
    CHECK(run.command == Command::Run);
    CHECK_FALSE(run.port.has_value());  // as its settings say
    CHECK(run.data == seventhslide::server::default_data_dir());

    const auto full = parse({"--data", "/srv/ss", "--port", "9000", "--address", "church.example.org",
                             "--address", "10.0.0.5:9001", "--name", "Church server"});
    CHECK(full.data == "/srv/ss");
    CHECK(full.port == 9000);
    CHECK(full.addresses == std::vector<std::string>{"church.example.org", "10.0.0.5:9001"});
    CHECK(full.name == "Church server");

    const auto join = parse({"join", "CODE"});
    CHECK(join.command == Command::Join);
    CHECK(join.code == "CODE");
    CHECK(parse({"sync"}).command == Command::Sync);
    CHECK(parse({"admin", "3f2a", "invite", "viewer", "--at", "far.example.org", "--at", "203.0.113.9"}).at
          == std::vector<std::string>{"far.example.org", "203.0.113.9"});
    CHECK(parse({"status", "--json"}).json);
    const auto invite = parse({"admin", "3f2a", "invite", "viewer", "--days", "3"});
    CHECK(invite.command == Command::Admin);
    CHECK(invite.library == "3f2a");
    CHECK(invite.action == std::vector<std::string>{"invite", "viewer"});
    CHECK(invite.days == 3);
    CHECK(parse({"admin", "3f2a", "password", "--recovery"}).recovery);
    const auto leave = parse({"leave", "3f2a", "--yes", "--data", "/srv/ss"});
    CHECK(leave.command == Command::Leave);
    CHECK(leave.yes);
    CHECK(leave.data == "/srv/ss");
    CHECK(parse({"--help"}).command == Command::Help);

    CHECK_THROWS_WITH(parse({"bogus"}), ContainsSubstring("no command"));
    CHECK_THROWS_WITH(parse({"status", "--port", "9000"}), ContainsSubstring("for running"));
    CHECK_THROWS_WITH(parse({"leave"}), ContainsSubstring("needs a library"));
    CHECK_THROWS_WITH(parse({"status", "extra"}), ContainsSubstring("more than it takes"));
    CHECK_THROWS_WITH(parse({"--port", "99999"}), ContainsSubstring("whole number"));
    CHECK_THROWS_WITH(parse({"admin", "x", "invite", "editor", "--days", "90"}),
                      ContainsSubstring("whole number"));
    CHECK_THROWS_WITH(parse({"--data"}), ContainsSubstring("needs a value"));
    CHECK_THROWS(parse({"--json"}));
    CHECK(parse({"join"}).command == Command::Join);
    CHECK(parse({"join"}).code.empty());  // asked for
    CHECK_THROWS_WITH(parse({"join", "CODE", "more"}), ContainsSubstring("takes the invite"));
    const auto reach_at = parse({"address", "3f2a", "Laptop", "home.example.org"});
    CHECK(reach_at.command == Command::Address);
    CHECK(reach_at.library == "3f2a");
    CHECK(reach_at.action == std::vector<std::string>{"Laptop", "home.example.org"});
    CHECK(parse({"address", "3f2a", "Laptop", "home.example.org", "203.0.113.9"}).action
          == std::vector<std::string>{"Laptop", "home.example.org", "203.0.113.9"});
    CHECK_THROWS_WITH(parse({"address", "3f2a"}), ContainsSubstring("address LIB COMPUTER"));
    CHECK_THROWS_WITH(parse({"status", "--at", "x"}), ContainsSubstring("--at goes with"));

    using seventhslide::server::parse_size;
    CHECK(parse_size("50GB") == 50'000'000'000);
    CHECK(parse_size("1.5 TB") == 1'500'000'000'000);
    CHECK(parse_size("500mb") == 500'000'000);
    CHECK(parse_size("1234") == 1234);
    CHECK(parse_size("none") == 0);
    CHECK_THROWS(parse_size("lots"));
    CHECK_THROWS(parse_size("-5GB"));
    CHECK_THROWS(parse_size("GB"));
}

TEST_CASE("An address is one, or it is not", "[server]")
{
    using seventhslide::sync::HostPort;
    using seventhslide::sync::parse_address;
    CHECK(parse_address("church.example.org:8766") == HostPort{"church.example.org", 8766});
    CHECK(parse_address("10.0.0.5:1") == HostPort{"10.0.0.5", 1});
    CHECK(parse_address("[2001:db8::1]:8766") == HostPort{"2001:db8::1", 8766});
    CHECK_FALSE(parse_address("church.example.org"));
    CHECK_FALSE(parse_address("church:0"));
    CHECK_FALSE(parse_address("church:70000"));
    CHECK_FALSE(parse_address("::1:8766"));
    CHECK_FALSE(parse_address(":8766"));
}

TEST_CASE("A server joins by an invite, and keeps the library as a library of its own", "[server]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    INFO("church closed: " << why(church));
    CHECK(church.refused.empty());

    // A library of its own: the store, the media and the database.
    REQUIRE(host->library_uids() == std::vector<std::string>{church.uid});
    auto* held = host->library(church.uid);
    REQUIRE(held != nullptr);
    CHECK(held->store().sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(held->store().admin_keys().size() == 1);
    CHECK(std::filesystem::exists(host.root(church.uid) / "media" / "videos" / "cd34.mp4"));
    {
        auto db = host.database(church.uid);
        CHECK(seventhslide::db::library_uid(db) == church.uid);
        seventhslide::test::check_same_records(db, church.db);
    }

    const auto lib = host.library(church.uid);
    CHECK(lib["standing"] == "member");
    CHECK(lib["joined"] == true);
    CHECK(lib["problem"] == "");
    CHECK(lib["missing_media"] == 0);
    CHECK(lib["change_files"] == 1);
    CHECK(host.computer(church.uid, "church")["connected"] == true);
    CHECK(host.computer(church.uid, "church")["role"] == "editor");
    CHECK(host.computer(church.uid, host->identity().machine)["this_computer"] == true);
    CHECK(host.computer(church.uid, host->identity().machine)["role"] == "viewer");
    CHECK(host.computer(church.uid, host->identity().machine)["holds_admin_key"] == true);

    // Joining what it holds is refused, and leaves it as it was.
    const auto again = host.join(invite_to_reach(church, founded.admin));
    CHECK(again["ok"] == false);
    CHECK_THAT(again["error"].get<std::string>(), ContainsSubstring("holds that library already"));
    CHECK(host->library_uids().size() == 1);
}

TEST_CASE("A server reached out to shares nothing with a computer it was not invited through",
          "[server]")
{
    Computer church{"church", "Church PC"};
    Computer other{"other", "Other PC"};
    ServerHost host;
    const auto founded = put_online(church);
    REQUIRE(host.join(invite_to_reach(church, founded.admin))["ok"] == true);
    other.share(seventhslide::db::library_uid(other.db));
    other.link.connect_to("127.0.0.1", host->port(), std::nullopt);
    REQUIRE(settle_all({&other}, host));
    CHECK(other.link.peers().empty());
    CHECK(other.store.machines(Numbered::Members).empty());
}

TEST_CASE("The same settings on a server: a file, which a flag overrides for a run", "[server]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const auto data = dir.path / "server";
    std::filesystem::create_directories(data);
    seventhslide::link::SyncSettings settings;
    settings.computer_name = "Named in the file";
    settings.addresses = {"file.example.org:9000"};
    settings.sync_automatically = false;
    REQUIRE(settings.save(data));
    CHECK(seventhslide::link::SyncSettings::load(data) == settings);

    const auto server_with = [&](std::string name, std::vector<std::string> addresses) {
        ServerOptions options;
        options.data = data;
        options.listen = QHostAddress::LocalHost;
        options.port = 0;
        options.name = std::move(name);
        options.addresses = std::move(addresses);
        options.control = false;
        options.discovery = false;
        return std::make_unique<Server>(std::move(options));
    };
    {
        const auto server = server_with({}, {});
        CHECK(server->identity().computer == "Named in the file");
        CHECK(server->addresses() == std::vector<std::string>{"file.example.org:9000"});
        CHECK_FALSE(server->node().sync_automatically());
        CHECK(server->status()["server"]["sync_automatically"] == false);
    }
    {
        const auto server = server_with("From a flag", {"flag.example.org:9001"});
        CHECK(server->identity().computer == "From a flag");
        CHECK(server->addresses() == std::vector<std::string>{"flag.example.org:9001"});
    }
    // The flags were for that run: the file is as it was.
    CHECK(seventhslide::link::SyncSettings::load(data) == settings);
}

TEST_CASE("A server lacking what it needs says so before it starts", "[server]")
{
    using seventhslide::server::lacking_for_server;
    CHECK(lacking_for_server(true, true, {"/opt/qt/plugins"}).empty());
    CHECK_THAT(lacking_for_server(false, true, {"/opt/qt/plugins", "/usr/bin"}),
               ContainsSubstring("no SQLite driver (the sqldrivers plugin). It looked for plugins in: /opt/qt/plugins, /usr/bin."));
    const auto no_tls = lacking_for_server(true, false, {"/opt/qt/plugins"});
    CHECK_THAT(no_tls, ContainsSubstring("no TLS (the tls plugin, and OpenSSL)"));
    CHECK_THAT(no_tls, !ContainsSubstring("SQLite"));
    const auto neither = lacking_for_server(false, false, {});
    CHECK_THAT(neither, ContainsSubstring("no SQLite driver"));
    CHECK_THAT(neither, ContainsSubstring("no TLS"));
    CHECK_THAT(neither, ContainsSubstring("in: nowhere."));
}

TEST_CASE("The server's status says what media is on its way, in and out", "[server]")
{
    const auto said = [](std::int64_t coming, std::int64_t missing, std::int64_t going) {
        return seventhslide::server::describe_library_state({{"uid", std::string(32, 'a')},
                                                             {"standing", "member"},
                                                             {"problem", ""},
                                                             {"joined", true},
                                                             {"over_limit", 0},
                                                             {"media_coming", coming},
                                                             {"missing_media", missing},
                                                             {"media_going", going}});
    };
    CHECK(said(0, 0, 0) == "Up to date.");
    CHECK(said(2, 0, 0) == "Getting 2 media file(s).");
    CHECK(said(0, 3, 0) == "Waiting for 3 media file(s).");
    CHECK(said(0, 0, 4) == "Sending 4 media file(s).");
    CHECK(said(1, 3, 4) == "Getting 1 media file(s).");

    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    join_via(laptop, church.uid, church.members().invite(Role::Viewer, k_expires, founded.admin), host);
    REQUIRE(settle_all({&church, &laptop}, host));
    CHECK(host.library(church.uid)["media_coming"] == 0);
    CHECK(host.library(church.uid)["media_going"] == 0);

    // A computer's thread for reading files through, held: what is asked of it stays
    // on its way.
    std::atomic<bool> let_go = false;
    const auto hold = [&](seventhslide::link::Link& link) {
        let_go = false;
        link.shared().checks.start([&] {
            while (!let_go) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    };
    const auto release = qScopeGuard([&] { let_go = true; });
    const auto until = [&](const std::function<bool()>& done) {
        return settle_while([&] { return !done(); });
    };
    laptop.link.peers().front()->close(QStringLiteral("going home"));
    REQUIRE(settle_all({&church, &laptop}, host));

    // A file new at the church: on its way to the server.
    const std::set<std::string> file{"images/new1.png"};
    hold(church.link);
    church.put_file("images/new1.png", "a new picture");
    church.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('new1.png', 'New')");
    REQUIRE(church.sync().changes_sent == 1);
    REQUIRE(until([&] { return host.library(church.uid)["media_coming"] == 1; }));
    CHECK(church.link.media_going(church.uid) == file);
    CHECK(host.library(church.uid)["media_going"] == 0);
    let_go = true;
    REQUIRE(until([&] { return host.library(church.uid)["media_coming"] == 0; }));
    REQUIRE(settle_all({&church}, host));
    CHECK(church.link.media_going(church.uid).empty());

    // The server has it now, and the laptop back asks it of the server.
    hold(host.server->link());
    reconnect(laptop, host);
    REQUIRE(until([&] { return host.library(church.uid)["media_going"] == 1; }));
    CHECK(laptop.link.media_coming(church.uid) == file);
    let_go = true;
    REQUIRE(until([&] { return host.library(church.uid)["media_going"] == 0; }));
    REQUIRE(settle_all({&church, &laptop}, host));
    CHECK(laptop.file("images/new1.png") == "a new picture");
}

TEST_CASE("The server's status says why no snapshot is written, below what is on its way", "[server]")
{
    const auto said = [](std::int64_t going, const std::string& why) {
        return seventhslide::server::describe_library_state({{"uid", std::string(32, 'a')},
                                                             {"standing", "member"},
                                                             {"problem", ""},
                                                             {"snapshot_problem", why},
                                                             {"joined", true},
                                                             {"over_limit", 0},
                                                             {"media_coming", 0},
                                                             {"missing_media", 0},
                                                             {"media_going", going}});
    };
    const std::string why = "No snapshot is written while “King James” (bibles 1), changed here, is too large to sync";
    CHECK(said(0, why) == why + ". Old change files are kept until a snapshot can be written. Sync goes on.");
    CHECK(said(0, "The disk is full.") == "The disk is full. Old change files are kept until a snapshot can be "
                                          "written. Sync goes on.");
    CHECK(said(2, why) == "Sending 2 media file(s).");
    CHECK(said(0, "") == "Up to date.");
}

TEST_CASE("Computers that never meet sync through the server", "[server]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));

    // The church goes home; the laptop joins through the server alone.
    church.link.peers().front()->close(QStringLiteral("going home"));
    REQUIRE(settle_all({&church}, host));
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    REQUIRE(seventhslide::sync::read_invite(code).invite.through.size() == 1);
    join_via(laptop, church.uid, code, host);
    REQUIRE(settle_all({&laptop}, host));
    INFO("laptop closed: " << why(laptop));
    CHECK(laptop.file("images/ab12.png") == "lilies");
    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 1);
    seventhslide::test::check_same_records(laptop.db, church.db);

    // The laptop's change waits at the server for the church.
    laptop.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    REQUIRE(laptop.sync().changes_sent == 1);
    REQUIRE(settle_all({&laptop}, host));
    {
        auto db = host.database(church.uid);
        CHECK(text(db, "SELECT key FROM songs WHERE title = 'Amazing Grace'") == "A");
    }
    reconnect(church, host);
    REQUIRE(settle_all({&church, &laptop}, host));
    CHECK(church.sync().files_applied >= 1);
    CHECK(church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "A");
    CHECK(host.computer(church.uid, "laptop")["connected"] == true);
    CHECK_FALSE(host.computer(church.uid, "laptop")["last_heard"].get<std::string>().empty());
    // It passed all of that on, and wrote none of it.
    const auto holder = host->identity().machine;
    CHECK(church.store.sequences(Numbered::Changes, holder).empty());
    CHECK(std::ranges::none_of(church.store.snapshots(), [&](const seventhslide::sync::SnapshotName& name) {
        return name.machine == holder;
    }));
}

TEST_CASE("The server joins from the newest snapshot, not from the first change file", "[server]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    REQUIRE(church.engine.snapshot());
    church.db.exec("UPDATE services SET name = 'Divine Service'");
    REQUIRE(church.sync().changes_sent == 1);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    const auto lib = host.library(church.uid);
    CHECK(lib["joined"] == true);
    // The snapshot held the first file: only the second was replayed.
    CHECK(lib["files_applied"] == 1);
    auto db = host.database(church.uid);
    CHECK(text(db, "SELECT name FROM services") == "Divine Service");
    seventhslide::test::check_same_records(db, church.db);
}

TEST_CASE("A server older than a computer keeps and passes on its files, and says to update",
          "[server]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));

    // The church's next file, as a newer SeventhSlide would write it: a
    // format this one does not read, beyond its label.
    seventhslide::sync::ChangeFile newer;
    newer.format = seventhslide::sync::k_change_format + 1;
    newer.library = church.uid;
    newer.machine = "church";
    newer.sequence = 2;
    newer.counter = 50;
    newer.seen = {{"church", 1}};
    auto body = seventhslide::sync::change_file_to_json(newer);
    body["changes"] = "in a shape only the newer SeventhSlide knows";
    church.store.write(Numbered::Changes, "church", 2,
                       seventhslide::sync::seal(body, {{seventhslide::sync::k_signed_as_computer, &church.me.key}}));
    church.link.library_changed(church.uid);
    REQUIRE(settle_all({&church}, host));
    INFO("church refused: " << (church.refused.empty() ? "" : church.refused.front().toStdString()));
    REQUIRE(host->library(church.uid)->store().sequences(Numbered::Changes, "church")
            == std::vector<std::int64_t>{1, 2});
    CHECK_THAT(host.library(church.uid)["problem"].get<std::string>(),
               ContainsSubstring("newer version of SeventhSlide"));

    // Passed on to a computer that joins through the server.
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_via(laptop, church.uid, code, host);
    REQUIRE(settle_all({&church, &laptop}, host));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1, 2});

    // A file of a format this SeventhSlide reads is read whole: one that
    // would stop the engine is not let in.
    seventhslide::sync::ChangeFile broken;
    broken.library = church.uid;
    broken.machine = "church";
    broken.sequence = 3;
    broken.seen = {{"church", 2}};
    auto broken_body = seventhslide::sync::change_file_to_json(broken);
    broken_body["changes"] = "not a list";
    church.store.write(Numbered::Changes, "church", 3,
                       seventhslide::sync::seal(broken_body, {{seventhslide::sync::k_signed_as_computer, &church.me.key}}));
    church.link.library_changed(church.uid);
    REQUIRE(settle_all({&church, &laptop}, host));
    CHECK(host->library(church.uid)->store().sequences(Numbered::Changes, "church")
          == std::vector<std::int64_t>{1, 2});
}

TEST_CASE("Admin work at the server reaches the computers", "[server]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    Computer stage{"stage", "Stage display"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    const auto& uid = church.uid;

    // An invite made at the server, with the password.
    CHECK(host.admin(uid, {{"action", "invite"}, {"role", "viewer"}, {"password", "wrong password here"}})["error"]
          == "That is not the library's admin password.");
    const auto invite = host.admin(uid, {{"action", "invite"}, {"role", "viewer"}, {"days", 2}, {"password", k_passphrase}});
    REQUIRE(invite["ok"] == true);
    const auto signed_invite = seventhslide::sync::read_invite(invite["code"].get<std::string>());
    CHECK(signed_invite.invite.role == Role::Viewer);
    CHECK(signed_invite.invite.through.front().key == host.key());
    join_via(stage, uid, invite["code"], host);
    REQUIRE(settle_all({&church, &stage}, host));
    INFO("stage closed: " << why(stage));
    REQUIRE(church.list().find("stage") != nullptr);
    CHECK(church.list().find("stage")->role == Role::Viewer);
    CHECK(stage.store.admin_keys().empty());

    const auto editor_code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_via(laptop, uid, editor_code, host);
    REQUIRE(settle_all({&church, &laptop, &stage}, host));

    // A role, a removal and the size limit, each reaching the church.
    CHECK(host.admin(uid, {{"action", "role"}, {"computer", "Laptop"}, {"role", "viewer"}, {"password", k_passphrase}})["ok"] == true);
    CHECK(host.admin(uid, {{"action", "role"}, {"computer", "laptop"}, {"role", "viewer"}, {"password", k_passphrase}})["error"]
          == "Laptop is a viewer already.");
    // The role and the admin key in one, as the Sync page's Edit card asks.
    CHECK(host.admin(uid, {{"action", "role"}, {"computer", "laptop"}, {"role", "viewer"}, {"holds", true},
                           {"password", k_passphrase}})["ok"]
          == true);
    // An invite waiting, withdrawn by the start of its id.
    const auto unused = host.admin(uid, {{"action", "invite"}, {"role", "editor"}, {"password", k_passphrase}});
    const auto unused_id = seventhslide::sync::read_invite(unused["code"].get<std::string>()).invite.id;
    // Said to the computers here, as any entry written is.
    REQUIRE(settle_while([&] { return !church.list().invites.contains(unused_id); }, 10000));
    CHECK(host.admin(uid, {{"action", "withdraw"}, {"invite", "feedface"}, {"password", k_passphrase}})["error"]
              .get<std::string>()
              .starts_with("No invite of this library"));
    const auto withdrawn = host.admin(uid, {{"action", "withdraw"}, {"invite", unused_id.substr(0, 6)},
                                            {"password", k_passphrase}});
    CHECK(withdrawn["invite"] == unused_id);
    CHECK(host.admin(uid, {{"action", "remove"}, {"computer", "Nobody"}, {"password", k_passphrase}})["error"].get<std::string>().starts_with("No computer"));
    CHECK(host.admin(uid, {{"action", "remove"}, {"computer", "stage"}, {"password", k_passphrase}})["ok"] == true);
    CHECK(host.admin(uid, {{"action", "limit"}, {"bytes", 50'000'000'000}, {"password", k_passphrase}})["ok"] == true);
    REQUIRE(settle_all({&church, &laptop}, host));
    const auto list = church.list();
    CHECK(list.find("laptop")->role == Role::Viewer);
    CHECK(list.find("laptop")->holds_admin_key);
    CHECK(list.invites_withdrawn.contains(unused_id));
    CHECK(list.find("stage")->removed);
    CHECK(list.size_limit == 50'000'000'000);
    CHECK(list.refused.empty());

    // A new recovery code, then a new password with it: the code is spent
    // and another made; the old password no longer unlocks anything.
    const auto recovery = host.admin(uid, {{"action", "recovery"}, {"password", k_passphrase}});
    REQUIRE(recovery["ok"] == true);
    const auto by_code = host.admin(uid, {{"action", "password"},
                                          {"recovery_code", recovery["recovery_code"]},
                                          {"new_password", "consider the ravens now"}});
    REQUIRE(by_code["ok"] == true);
    CHECK_FALSE(by_code["recovery_code"].get<std::string>().empty());
    CHECK(host.admin(uid, {{"action", "password"}, {"recovery_code", recovery["recovery_code"]},
                           {"new_password", "a third password here"}})["error"]
          == "That is not the library's recovery code.");
    CHECK(host.admin(uid, {{"action", "password"}, {"password", "consider the ravens now"},
                           {"new_password", "short"}})["ok"] == false);
    const auto changed = host.admin(uid, {{"action", "password"}, {"password", "consider the ravens now"},
                                          {"new_password", "the sparrows are fed"}});
    REQUIRE(changed["ok"] == true);
    CHECK_FALSE(changed.contains("recovery_code"));  // the code stands
    REQUIRE(settle_all({&church, &laptop}, host));
    CHECK_FALSE(church.members().unlock_admin(k_passphrase).has_value());
    CHECK(church.members().unlock_admin("the sparrows are fed").has_value());
    CHECK(church.list().refused.empty());
}

TEST_CASE("The server comes back as it was, and lets a library go", "[server]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    const auto server_machine = host->identity().machine;
    const auto server_key = host.key();

    // Stopped and started again: the same server, holding the same library;
    // the church reconnects as a member.
    host.stop();
    REQUIRE(settle({&church}));
    CHECK(std::filesystem::exists(host.data() / "held.json"));
    host.start();
    CHECK(host->identity().machine == server_machine);
    CHECK(host.key() == server_key);
    REQUIRE(host->library_uids() == std::vector<std::string>{church.uid});
    CHECK_FALSE(host.computer(church.uid, "church")["last_heard"].get<std::string>().empty());
    church.db.exec("UPDATE services SET name = 'Divine Service'");
    church.sync();
    reconnect(church, host);
    REQUIRE(settle_all({&church}, host));
    INFO("church closed: " << why(church));
    {
        auto db = host.database(church.uid);
        CHECK(text(db, "SELECT name FROM services") == "Divine Service");
    }

    // Another server cannot run with the same data folder.
    {
        ServerOptions again;
        again.data = host.data();
        again.listen = QHostAddress::LocalHost;
        again.port = 0;
        again.discovery = false;
        CHECK_THROWS_WITH(Server(again), ContainsSubstring("Another SeventhSlide server is running"));
    }

    // Leaving: off the link, the copy kept aside, the list still naming it.
    const auto left = host->leave(church.uid);
    REQUIRE(left["ok"] == true);
    CHECK(left["still_listed"] == true);
    CHECK(host->library_uids().empty());
    CHECK(host->link().libraries().empty());
    CHECK_FALSE(std::filesystem::exists(host.root(church.uid)));
    CHECK(std::filesystem::exists(std::filesystem::path(left["kept_at"].get<std::string>())
                                  / "seventhslide.db"));
    host.stop();
    host.start();
    CHECK(host->library_uids().empty());
}

TEST_CASE("A server given a new name goes by it on its libraries' lists", "[server]")
{
    // The sync every computer runs puts its name on each list, a server's
    // as a desktop's.
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    const auto machine = host->identity().machine;
    REQUIRE(church.list().find(machine) != nullptr);
    CHECK(church.list().find(machine)->name == "Church server");

    host.stop();
    REQUIRE(settle({&church}));  // the church sees it go
    host.name = "Hall server";
    host.start();
    reconnect(church, host);
    REQUIRE(settle_all({&church}, host));
    INFO("church closed: " << why(church));
    CHECK(church.list().find(machine)->name == "Hall server");
    CHECK(church.list().refused.empty());
}

TEST_CASE("How a computer is reached is always what the list can keep", "[server]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    const auto node_with = [&](std::vector<std::string> addresses, bool network) {
        seventhslide::link::SyncNode::Options options;
        options.data = dir.path;
        options.network = network;
        options.listen = QHostAddress::LocalHost;
        options.port = 0;
        options.discovery = false;
        options.addresses = std::move(addresses);
        return std::make_unique<seventhslide::link::SyncNode>(
            seventhslide::sync::Identity{"node", "Node", seventhslide::crypto::SigningKey::generate()}, options);
    };
    {
        const auto node = node_with({"slides.example.org", "slides.example.org:9001", "[::1]", "::1"}, true);
        const auto port = std::to_string(node->port());
        CHECK(node->addresses()
              == std::vector<std::string>{"slides.example.org:" + port, "slides.example.org:9001", "[::1]:" + port,
                                          "[::1]:" + port});
    }
    {
        const auto node = node_with(std::vector<std::string>(seventhslide::sync::k_most_addresses + 5, "host.example.org"),
                                    true);
        CHECK(node->addresses().size() == seventhslide::sync::k_most_addresses);
    }
    {
        // Not listening: only what names its own port.
        const auto node = node_with({"slides.example.org", "slides.example.org:9001"}, false);
        CHECK(node->addresses() == std::vector<std::string>{"slides.example.org:9001"});
        CHECK(node_with({}, false)->addresses().empty());
    }
}

TEST_CASE("Of the IPv6 addresses a computer finds, it gives the global ones that last", "[server]")
{
    using seventhslide::link::ipv6_to_give;
    const auto entry = [](const char* ip) {
        QNetworkAddressEntry e;
        e.setIp(QHostAddress(QString::fromLatin1(ip)));
        e.setDnsEligibility(QNetworkAddressEntry::DnsEligible);
        // As a home router hands them out: an end, a week off.
        e.setAddressLifetime(QDeadlineTimer(std::chrono::hours(24 * 7)), QDeadlineTimer(std::chrono::hours(24 * 7)));
        return e;
    };
    CHECK(ipv6_to_give(entry("2a01:4f8:1:2::5")));
    CHECK_FALSE(ipv6_to_give(entry("fd05:ac4d:7313::5")));  // unique local
    CHECK_FALSE(ipv6_to_give(entry("fe80::1")));
    CHECK_FALSE(ipv6_to_give(entry("::1")));
    CHECK_FALSE(ipv6_to_give(entry("192.168.1.5")));
    auto privacy = entry("2a01:4f8:1:2:8821:f5c7:cf90:56af");
    privacy.setDnsEligibility(QNetworkAddressEntry::DnsIneligible);
    CHECK_FALSE(ipv6_to_give(privacy));
    auto unknown = entry("2a01:4f8:1:2::6");  // where Qt cannot tell (macOS)
    unknown.setDnsEligibility(QNetworkAddressEntry::DnsEligibilityUnknown);
    CHECK(ipv6_to_give(unknown));
    auto given_up = entry("2a01:4f8:1:2::7");  // its network moved on: still valid, no longer used
    given_up.setAddressLifetime(QDeadlineTimer(0), QDeadlineTimer(std::chrono::hours(1)));
    CHECK_FALSE(ipv6_to_give(given_up));
    auto by_hand = entry("2a01:4f8:1:2::8");  // set by hand
    by_hand.clearAddressLifetime();
    CHECK(ipv6_to_give(by_hand));

    // What this computer finds, each one the list takes.
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    seventhslide::link::SyncNode::Options options;
    options.data = dir.path;
    options.listen = QHostAddress::LocalHost;
    options.port = 0;
    options.discovery = false;
    const seventhslide::link::SyncNode node(
        seventhslide::sync::Identity{"node", "Node", seventhslide::crypto::SigningKey::generate()}, options);
    for (const auto& a : node.addresses()) {
        INFO(a);
        const auto at = seventhslide::sync::parse_address(a);
        REQUIRE(at);
        const QHostAddress ip(QString::fromStdString(at->host));
        CHECK((ip.protocol() == QAbstractSocket::IPv4Protocol || (ip.isGlobal() && !ip.isUniqueLocalUnicast())));
    }
}

TEST_CASE("An address that is not one is said, and takes no address's place", "[server]")
{
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    for (const std::string bad : {"my host.org", "[2001:db8::1", "[not v6]", "a:b:c d"}) {
        INFO(bad);
        ServerOptions options;
        options.data = dir.path / "server";
        options.listen = QHostAddress::LocalHost;
        options.port = 0;
        options.addresses = {bad};
        options.control = false;
        options.discovery = false;
        CHECK_THROWS_WITH(Server(std::move(options)), ContainsSubstring("is not an address"));
    }

    // Changed while running: what the list has stands.
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    const auto machine = host->identity().machine;
    const auto before = church.list().find(machine)->addresses;
    REQUIRE_FALSE(before.empty());
    host->node().set_addresses({"my host.org"});
    CHECK(host->node().addresses().empty());
    REQUIRE(settle_all({&church}, host));
    CHECK(church.list().find(machine)->addresses == before);
    // One good among them: that one.
    host->node().set_addresses({"my host.org", "slides.example.org:9001"});
    CHECK(host->node().unusable_addresses() == std::vector<std::string>{"my host.org"});
    REQUIRE(settle_all({&church}, host));
    run_events_for(300);
    REQUIRE(settle_all({&church}, host));
    CHECK(church.list().find(machine)->addresses == std::vector<std::string>{"slides.example.org:9001"});
}

TEST_CASE("Where a server reaches a computer can be typed for it, there alone", "[server]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    const auto& uid = church.uid;

    using Addresses = std::vector<std::string>;
    const auto set = host->reach_at(uid, "Church PC", Addresses{"church.example.org", "203.0.113.9:9000"});
    CHECK(set["ok"] == true);
    CHECK(set["name"] == "Church PC");
    const auto both = nlohmann::json::array({"church.example.org:8766", "203.0.113.9:9000"});
    CHECK(set["addresses"] == both);
    CHECK(host.computer(uid, "church")["typed_addresses"] == both);
    CHECK_THAT(seventhslide::server::describe_status(host->status()),
               ContainsSubstring("set to be reached at church.example.org:8766, 203.0.113.9:9000"));
    // Asked, not said; and on no list.
    CHECK(host->reach_at(uid, "church", std::nullopt)["addresses"] == both);
    CHECK(church.list().find("church")->addresses.empty());

    CHECK_THAT(host->reach_at(uid, "church", Addresses{"not an address at all"})["error"].get<std::string>(),
               ContainsSubstring("is not an address"));
    CHECK_THAT(host->reach_at(uid, "Nobody", Addresses{"x.example.org"})["error"].get<std::string>(),
               ContainsSubstring("Neither a computer nor an invite"));
    CHECK_THAT(host->reach_at(uid, "Church server", Addresses{"x.example.org"})["error"].get<std::string>(),
               ContainsSubstring("this server"));

    CHECK(host->reach_at(uid, "church", Addresses{})["addresses"].empty());
    CHECK(host.computer(uid, "church")["typed_addresses"].empty());

    // An invite waiting, by the start of its id: where its computer is.
    const auto code = church.members().invite(Role::Viewer, k_expires, founded.admin);
    church.link.library_changed(uid);
    REQUIRE(settle_all({&church}, host));
    const auto id = seventhslide::sync::read_invite(code).invite.id;
    const auto at = host->reach_at(uid, id.substr(0, 6), Addresses{"far.example.org"});
    CHECK(at["invite"] == id);
    CHECK(at["addresses"] == nlohmann::json::array({"far.example.org:8766"}));
    CHECK(host->reach().invite_addresses(uid, id) == Addresses{"far.example.org:8766"});
    const auto said = seventhslide::server::describe_status(host->status());
    CHECK_THAT(said, ContainsSubstring("Invites waiting"));
    CHECK_THAT(said, ContainsSubstring("reached out to at far.example.org:8766"));
}

TEST_CASE("Joined while not syncing automatically, a server waits for the library to come", "[server]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    host.stop();
    seventhslide::link::SyncSettings settings;
    settings.sync_automatically = false;
    REQUIRE(settings.save(host.data()));
    host.start();
    REQUIRE(host->node().paused());
    CHECK_THAT(seventhslide::server::describe_status(host->status()), ContainsSubstring("Syncs only when asked"));

    REQUIRE(host.join(invite_to_reach(church, founded.admin))["ok"] == true);
    // Longer than syncing would wait with nothing moving: nobody has come yet.
    run_events_for(3500);
    CHECK_FALSE(host->node().paused());

    church.link.connect_to("127.0.0.1", host->port(), std::nullopt);
    REQUIRE(settle_all({&church}, host));
    INFO("church closed: " << why(church));
    CHECK(host->library(church.uid)->store().sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    // All of it here: it waits to be asked again.
    QElapsedTimer waited;
    waited.start();
    while (!host->node().paused() && waited.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    CHECK(host->node().paused());
}

TEST_CASE("A server says where it is on its libraries' lists, and invites name it there", "[server]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    keep_at(church, host, founded.admin);
    REQUIRE(settle_all({&church}, host));
    const auto machine = host->identity().machine;
    REQUIRE(church.list().find(machine) != nullptr);
    // Once in, it says where it is: its join, then that.
    CHECK(church.list().find(machine)->addresses == host->addresses());
    CHECK(church.store.sequences(Numbered::Members, machine) == std::vector<std::int64_t>{1, 2});

    // Back on another port: it says so, and an invite names it there.
    host.stop();
    REQUIRE(settle({&church}));
    host.start();
    reconnect(church, host);
    REQUIRE(settle_all({&church}, host));
    INFO("church closed: " << why(church));
    CHECK(church.list().find(machine)->addresses == host->addresses());
    CHECK(church.store.sequences(Numbered::Members, machine) == std::vector<std::int64_t>{1, 2, 3});
    CHECK(church.list().refused.empty());
    const auto invite = seventhslide::sync::read_invite(church.members().invite(Role::Editor, k_expires, founded.admin));
    REQUIRE(invite.invite.through.size() == 1);
    CHECK(invite.invite.through.front().addresses == host->addresses());
}

TEST_CASE("A server takes no library without a good invite", "[server]")
{
    Computer church{"church", "Church PC"};
    ServerHost host;
    const auto founded = put_online(church);
    CHECK_THAT(host.join("not an invite")["error"].get<std::string>(), ContainsSubstring("not an invite"));
    CHECK(host->library_uids().empty());

    // Expired: nothing is left behind of it.
    const auto expired = host.join(church.members().invite(Role::Viewer, "2000-01-01T00:00:00Z", founded.admin));
    CHECK(expired["ok"] == false);
    CHECK_THAT(expired["error"].get<std::string>(), ContainsSubstring("expired"));
    CHECK(host->library_uids().empty());
    CHECK_FALSE(std::filesystem::exists(host.root(church.uid)));

    // Nor one whose uid could name somewhere else on the disk.
    seventhslide::sync::Invite sly;
    sly.library = "../../etc";
    sly.expires = k_expires;
    sly.id = "0000000000000000";
    CHECK(host.join(seventhslide::sync::invite_code(sly, SigningKey::generate()))["ok"] == false);
    CHECK(host->library_uids().empty());
    CHECK_FALSE(std::filesystem::exists(host.data() / "etc"));
}

TEST_CASE("The program runs as a server with no window system to be had", "[server]")
{
#ifndef SEVENTHSLIDE_APP_PROGRAM
    SKIP("The program is not built here");
#else
    seventhslide::test::TempDir dir;
    const auto data = dir.path / "server";
    const QString program = QStringLiteral(SEVENTHSLIDE_APP_PROGRAM);
    // Nothing graphical can start: a QGuiApplication would stop the program.
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("no-such-platform"));
    env.remove(QStringLiteral("DISPLAY"));
    env.remove(QStringLiteral("WAYLAND_DISPLAY"));
    const auto command = [&](QStringList args, const QByteArray& input = {}) {
        QProcess p;
        p.setProcessEnvironment(env);
        args.prepend(QStringLiteral("--server"));
        args << QStringLiteral("--data") << QString::fromStdString(data.string());
        p.start(program, args);
        REQUIRE(p.waitForStarted());
        if (!input.isEmpty()) {
            p.write(input);
        }
        p.closeWriteChannel();
        // The computers here answer the server meanwhile.
        QElapsedTimer timer;
        timer.start();
        while (p.state() != QProcess::NotRunning && timer.elapsed() < 30000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            p.waitForFinished(20);
        }
        REQUIRE(p.state() == QProcess::NotRunning);
        return std::pair{p.exitCode(), p.readAllStandardOutput().toStdString()
                                           + p.readAllStandardError().toStdString()};
    };

    QProcess server;
    server.setProcessEnvironment(env);
    server.start(program, {QStringLiteral("--server"), QStringLiteral("--data"),
                           QString::fromStdString(data.string()), QStringLiteral("--port"),
                           QStringLiteral("0"), QStringLiteral("--address"), QStringLiteral("127.0.0.1"),
                           QStringLiteral("--name"), QStringLiteral("Process server"),
                           QStringLiteral("--no-discovery")});
    REQUIRE(server.waitForStarted());
    json status;
    QElapsedTimer started;
    started.start();
    while (started.elapsed() < 20000) {
        const auto [code, out] = command({QStringLiteral("status"), QStringLiteral("--json")});
        if (code == 0) {
            status = json::parse(out);
            break;
        }
        REQUIRE(server.state() == QProcess::Running);
        QThread::msleep(100);
    }
    INFO(server.readAllStandardError().toStdString());
    REQUIRE(status.is_object());
    CHECK(status["server"]["name"] == "Process server");
    CHECK(status["libraries"].empty());

    // Joined by the command, on an invite from a computer here, which reaches
    // out to it.
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    // Asked for, so it is in no process list; pasted with its line end.
    const auto [join_status, join_out] = command({QStringLiteral("join")},
                                                 QByteArray::fromStdString(invite_to_reach(church, founded.admin) + " \n"));
    INFO(join_out);
    REQUIRE(join_status == 0);
    church.link.connect_to("127.0.0.1", status["server"]["port"].get<quint16>(), std::nullopt);
    json lib;
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 30000) {
        settle({&church}, 500);
        const auto [code, out] = command({QStringLiteral("status"), QStringLiteral("--json")});
        REQUIRE(code == 0);
        const auto now = json::parse(out);
        if (!now["libraries"].empty() && now["libraries"][0]["joined"] == true
            && now["libraries"][0]["last_sync"] != "") {
            lib = now["libraries"][0];
            break;
        }
    }
    INFO("church closed: " << why(church));
    REQUIRE(lib.is_object());
    CHECK(lib["uid"] == church.uid);
    CHECK(lib["standing"] == "member");
    CHECK(lib["problem"] == "");
    {
        auto db = Database::open_file(seventhslide::library::database_file(data / "libraries" / church.uid));
        seventhslide::test::check_same_records(db, church.db);
    }

    // The status a person reads, admin work with the password on stdin, and
    // leaving.
    const auto [read_code, read] = command({QStringLiteral("status")});
    CHECK(read_code == 0);
    CHECK_THAT(read, ContainsSubstring("Process server"));
    CHECK_THAT(read, ContainsSubstring("Church PC"));
    CHECK_THAT(read, ContainsSubstring("connected now"));
    const auto lib_arg = QString::fromStdString(church.uid.substr(0, 6));
    // A viewer holds no admin key until given one.
    const auto [no_key_code, no_key] =
        command({QStringLiteral("admin"), lib_arg, QStringLiteral("invite"), QStringLiteral("viewer")},
                QByteArray(k_passphrase) + "\n");
    CHECK(no_key_code == 1);
    CHECK_THAT(no_key, ContainsSubstring("holds no copy of the library's admin key"));
    church.members().set_key_holder(status["server"]["machine"].get<std::string>(), true, founded.admin);
    church.link.library_changed(church.uid);
    int invite_code = 1;
    std::string invite;
    QElapsedTimer key_came;
    key_came.start();
    while (invite_code != 0 && key_came.elapsed() < 20000) {
        settle({&church}, 500);
        std::tie(invite_code, invite) =
            command({QStringLiteral("admin"), lib_arg, QStringLiteral("invite"), QStringLiteral("viewer")},
                    QByteArray(k_passphrase) + "\n");
    }
    CHECK(invite_code == 0);
    CHECK_THAT(invite, ContainsSubstring("Invite for a viewer"));
    const auto [wrong_code, wrong] =
        command({QStringLiteral("admin"), lib_arg, QStringLiteral("invite"), QStringLiteral("editor")},
                "not the password\n");
    CHECK(wrong_code == 1);
    CHECK_THAT(wrong, ContainsSubstring("not the library's admin password"));
    const auto [leave_code, leave] = command({QStringLiteral("leave"), lib_arg, QStringLiteral("--yes")});
    CHECK(leave_code == 0);
    CHECK_THAT(leave, ContainsSubstring("still lists this server"));

    server.terminate();
    REQUIRE(server.waitForFinished(10000));
    CHECK(server.exitStatus() == QProcess::NormalExit);
    CHECK(server.exitCode() == 0);
    CHECK_THAT(server.readAllStandardError().toStdString(), ContainsSubstring("The server stopped."));
#endif
}
