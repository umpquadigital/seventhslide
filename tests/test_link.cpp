// The link (link.hpp): SeventhSlides syncing a library over TLS, here between
// computers on this computer's loopback address.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/crypto/password_box.hpp>
#include <seventhslide/crypto/sha256.hpp>
#include <seventhslide/crypto/signing.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/sync_records.hpp>
#include <seventhslide/link/frames.hpp>
#include <seventhslide/link/link.hpp>
#include <seventhslide/link/media_in.hpp>
#include <seventhslide/link/outbox.hpp>
#include <seventhslide/link/peer_shared.hpp>
#include <seventhslide/link/tls.hpp>
#include <seventhslide/sync/housekeeping.hpp>
#include <seventhslide/sync/identity.hpp>
#include <seventhslide/sync/media_pieces.hpp>
#include <seventhslide/sync/member_list.hpp>
#include <seventhslide/sync/sync_engine.hpp>
#include <seventhslide/sync/sync_store.hpp>
#include <seventhslide/util/read_file.hpp>

#include "link_test_helpers.hpp"
#include "sync_test_helpers.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"
#include "test_wait.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QString>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using seventhslide::crypto::SigningKey;
using seventhslide::db::Database;
using seventhslide::link::Link;
using seventhslide::sync::FolderStore;
using seventhslide::sync::Identity;
using seventhslide::sync::MemberLog;
using seventhslide::sync::Numbered;
using seventhslide::sync::Role;
using seventhslide::sync::SyncEngine;
using seventhslide::sync::k_media_piece_bytes;
using seventhslide::test::missing_media;

namespace {

using namespace seventhslide::test::link_fixture;

/// A computer that speaks the protocol by hand: whatever a test makes it
/// say, as one bent on mischief could.
struct RawPeer {
    Identity me;
    QSslSocket socket;
    seventhslide::link::FrameReader reader;
    std::vector<nlohmann::json> got;
    std::vector<QByteArray> bytes;

    explicit RawPeer(const std::string& machine, SigningKey key = SigningKey::generate())
        : me{machine, machine, std::move(key)}
    {
        socket.setSslConfiguration(seventhslide::link::tls_configuration(me));
        QObject::connect(&socket, &QSslSocket::sslErrors, [](const QList<QSslError>&) {});
    }

    /// Connect, running the event loop meanwhile: the computer at the other
    /// end answers from this same thread.
    bool connect_to(quint16 port)
    {
        socket.connectToHostEncrypted("127.0.0.1", port);
        QElapsedTimer timer;
        timer.start();
        while (!socket.isEncrypted() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return socket.isEncrypted();
    }
    void send(const nlohmann::json& message) { socket.write(seventhslide::link::encode_message(message)); }
    void send_bytes(const QByteArray& b) { socket.write(seventhslide::link::encode_bytes(b)); }
    /// The join entry the invite ``code`` makes for this computer, written by
    /// hand: an expired invite's too.
    [[nodiscard]] std::string join_entry(const std::string& library, const std::string& code) const
    {
        const auto invite = seventhslide::sync::read_invite(code);
        const nlohmann::json body{{"format", 1},
                                  {"library", library},
                                  {"machine", me.machine},
                                  {"sequence", 1},
                                  {"seen", nlohmann::json::object()},
                                  {"time", "2026-01-01T00:00:00Z"},
                                  {"kind", "join"},
                                  {"computer", {{"machine", me.machine}, {"key", me.key.public_key().base64()}, {"name", me.computer}}},
                                  {"invite", invite.sealed}};
        return seventhslide::sync::seal(body, {{seventhslide::sync::k_signed_as_computer, &me.key},
                                               {seventhslide::sync::k_signed_as_invite, &*invite.secret}});
    }
    void hello(const std::string& library, const std::string& invite = {}, nlohmann::json name = "Raw",
               int version = seventhslide::link::k_protocol_version)
    {
        nlohmann::json offered{{"library", library}};
        if (!invite.empty()) {
            offered["join"] = join_entry(library, invite);
        }
        send({{"type", "hello"},
              {"version", version},
              {"machine", me.machine},
              {"name", std::move(name)},
              {"libraries", nlohmann::json::array({offered})}});
    }
    /// Run everything for ``ms``, reading what arrives.
    void run(std::initializer_list<Computer*> computers, int ms = 400)
    {
        seventhslide::test::run_events_for(ms);
        reader.feed(socket.readAll());
        while (auto frame = reader.next()) {
            if (frame->bytes) {
                bytes.push_back(frame->payload);
            } else {
                got.push_back(nlohmann::json::parse(frame->payload.toStdString()));
            }
        }
        (void)computers;
    }
    [[nodiscard]] bool connected() const { return socket.state() == QAbstractSocket::ConnectedState; }
    [[nodiscard]] const nlohmann::json* first(const std::string& type) const
    {
        for (const auto& m : got) {
            if (m.value("type", "") == type) {
                return &m;
            }
        }
        return nullptr;
    }
};

/// A video of ``size`` bytes, no two megabytes alike.
std::string big_video(std::size_t size)
{
    std::string video;
    video.reserve(size + 32);
    for (int i = 0; video.size() < size; ++i) {
        video += "frame " + std::to_string(i) + '\n';
    }
    video.resize(size);
    return video;
}

/// ``put_online``, with ``video`` as the library's video.
MemberLog::Founded put_online_with(Computer& church, const std::string& video)
{
    seventhslide::test::fill_library(church.db);
    church.put_file("images/ab12.png", "lilies");
    church.put_file("videos/cd34.mp4", video);
    church.put_file("audio/ef56.mp3", "prelude");
    church.share(seventhslide::db::library_uid(church.db));
    auto founded = church.members().found(k_passphrase, k_quick);
    church.engine.share_everything();
    REQUIRE(church.sync().changes_sent > 0);
    return founded;
}

/// ``server`` invited to ``church``'s library as a viewer given the locked
/// admin key, saying where it is, and holding all of it.
void pair_server(Computer& church, Computer& server, const MemberLog::Founded& founded)
{
    const auto code = church.members().invite(Role::Viewer, k_expires, founded.admin);
    join_through(server, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &server}));
    church.members().set_key_holder("server", true, founded.admin);
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &server}));
    server.members().set_addresses({"127.0.0.1:" + std::to_string(server.port())});
    server.link.library_changed(church.uid);
    REQUIRE(settle({&church, &server}));
}

}  // namespace

TEST_CASE("A computer joins a library over the link, and the two stay in step", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    INFO("church closed: " << why(church) << "; laptop closed: " << why(laptop));
    REQUIRE(church.link.peers().size() == 1);
    REQUIRE(laptop.link.peers().size() == 1);
    CHECK(church.link.peers().front()->machine() == "laptop");
    CHECK(laptop.link.peers().front()->name() == "Church PC");
    CHECK(laptop.refused.empty());
    CHECK(church.refused.empty());

    // The laptop has the church's member list (its founding, and the invite
    // said), admin key, change file and media; the church has the laptop's
    // join entry.
    CHECK(laptop.store.sequences(Numbered::Members, "church") == std::vector<std::int64_t>{1, 2});
    CHECK(laptop.store.admin_keys().size() == 1);
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.file("images/ab12.png") == "lilies");
    CHECK(laptop.file("videos/cd34.mp4") == "welcome");
    CHECK(laptop.file("audio/ef56.mp3") == "prelude");
    REQUIRE(church.list().find("laptop") != nullptr);
    CHECK(church.list().find("laptop")->role == Role::Editor);

    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 1);
    seventhslide::test::check_same_records(laptop.db, church.db);

    // A change on the laptop reaches the church at once, and one back.
    laptop.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    REQUIRE(laptop.sync().changes_sent == 1);
    REQUIRE(settle({&church, &laptop}));
    CHECK(church.sync().files_applied == 1);
    CHECK(church.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "A");
    church.db.exec("UPDATE services SET name = 'Divine Service'");
    church.sync();
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.sync().files_applied == 1);
    CHECK(laptop.text("SELECT name FROM services") == "Divine Service");
    seventhslide::test::check_same_records(laptop.db, church.db);
}

TEST_CASE("A computer says what it has applied, and old files let go of are not fetched again",
          "[link][housekeeping]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    std::map<std::string, std::int64_t> laptop_said;
    QObject::connect(&church.link, &Link::progress,
                     [&](const std::string&, const std::string& machine, const std::map<std::string, std::int64_t>& applied) {
                         if (machine == "laptop") {
                             laptop_said = applied;
                         }
                     });
    const auto founded = put_online(church);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    laptop.engine.join(church.uid);
    laptop.sync();
    church.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    church.sync();
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.sync().files_applied == 1);
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(laptop_said.contains("church"));
    CHECK(laptop_said.at("church") == 2);

    // The church lets its old files go, by what the laptop said.
    REQUIRE(church.engine.snapshot());
    constexpr const char* now = "2026-06-01T00:00:00Z";
    seventhslide::sync::let_go(
        church.store, seventhslide::sync::old_files(church.store, church.list(), "church",
                                                    seventhslide::db::SyncBookkeeping(church.db).applied(),
                                                    {{"laptop", {laptop_said, now}}}, now));
    REQUIRE(church.store.sequences(Numbered::Changes, "church").empty());
    // The laptop still has them, and says so: they are not fetched back.
    laptop.link.library_changed(church.uid);
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &laptop}));
    CHECK(church.store.sequences(Numbered::Changes, "church").empty());
    CHECK(church.refused.empty());
    // The snapshot reached the laptop; the laptop's older one would not come back.
    CHECK(laptop.store.snapshots().size() == 1);
}

TEST_CASE("Two computers join at once through the same computer, and all three stay in step",
          "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    Computer office{"office", "Office"};
    const auto founded = put_online(church);
    const auto laptop_code = church.members().invite(Role::Editor, k_expires, founded.admin);
    const auto office_code = church.members().invite(Role::Viewer, k_expires, founded.admin);
    join_through(laptop, church.uid, laptop_code, church.key(), church.port());
    join_through(office, church.uid, office_code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop, &office}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop) << "; office: " << why(office));
    laptop.engine.join(church.uid);
    office.engine.join(church.uid);
    laptop.sync();
    office.sync();
    REQUIRE(settle({&church, &laptop, &office}));

    for (auto* c : {&church, &laptop, &office}) {
        INFO(c->me.machine);
        const auto list = c->list();
        REQUIRE(list.find("laptop") != nullptr);
        REQUIRE(list.find("office") != nullptr);
        CHECK(list.find("laptop")->role == Role::Editor);
        CHECK(list.find("office")->role == Role::Viewer);
        CHECK(list.refused.empty());
    }
    // An edit on the laptop reaches the office, through the church.
    laptop.db.exec("UPDATE songs SET key = 'B' WHERE title = 'Amazing Grace'");
    laptop.sync();
    REQUIRE(settle({&church, &laptop, &office}));
    church.sync();
    REQUIRE(settle({&church, &laptop, &office}));
    office.sync();
    CHECK(office.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "B");
    seventhslide::test::check_same_records(office.db, laptop.db);
}

TEST_CASE("A server passes the library between computers that never meet", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);

    // Invited as a viewer given the locked admin key, it says where it is.
    pair_server(church, server, founded);
    INFO("church closed: " << why(church) << "; server closed: " << why(server));
    REQUIRE(server.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(server.store.admin_keys().size() == 1);
    CHECK(server.file("images/ab12.png") == "lilies");
    REQUIRE(server.list().find("server") != nullptr);
    CHECK(server.list().find("server")->role == Role::Viewer);
    CHECK(server.list().find("server")->holds_admin_key);

    // The laptop joins through the server, which the invite names.
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    const auto invite = seventhslide::sync::read_invite(code);
    REQUIRE(invite.invite.through.size() == 1);
    CHECK(invite.invite.through[0].key == server.key());
    join_through(laptop, church.uid, code, server.key(), server.port());
    REQUIRE(settle({&church, &server, &laptop}));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.file("videos/cd34.mp4") == "welcome");
    laptop.engine.join(church.uid);
    REQUIRE(laptop.sync().files_applied == 1);
    // Its join entry went through the server to the church.
    CHECK(church.list().find("laptop") != nullptr);
    CHECK(church.link.peers().size() == 1);
    CHECK(server.link.peers().size() == 2);
    CHECK(laptop.link.peers().size() == 1);

    // A change on the laptop reaches the church through the server, media
    // and all.
    laptop.put_file("images/new1.png", "a new picture");
    laptop.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('new1.png', 'New')");
    REQUIRE(laptop.sync().changes_sent == 1);
    REQUIRE(settle({&church, &server, &laptop}));
    CHECK(server.file("images/new1.png") == "a new picture");
    CHECK(church.file("images/new1.png") == "a new picture");
    CHECK(church.sync().files_applied == 1);
    seventhslide::test::check_same_records(laptop.db, church.db);
    CHECK(church.refused.empty());
    CHECK(server.refused.empty());
    CHECK(laptop.refused.empty());
}

TEST_CASE("A transfer cut off mid-video resumes where it stopped", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    // Three pieces, the last one short.
    const auto video = big_video(2 * k_media_piece_bytes + 1024 * 1024);
    const auto founded = put_online_with(church, video);

    // The laptop's connection drops once the first piece of the video is in.
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    std::vector<std::int64_t> progress;
    std::map<std::string, seventhslide::link::Arriving> arriving;
    REQUIRE(laptop.link.peers().size() == 1);
    QObject::connect(laptop.link.peers().front(), &seventhslide::link::Peer::media_progress,
                     [&](const std::string&, const std::string& path, std::int64_t bytes) {
                         if (path == "videos/cd34.mp4" && !laptop.link.peers().empty()) {
                             progress.push_back(bytes);
                             arriving = laptop.link.media_arriving(church.uid);
                             laptop.link.peers().front()->close("the network cable came out");
                         }
                     });
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(progress.size() == 1);
    // How far it had come, of how much, as the window is told.
    CHECK(arriving == std::map<std::string, seventhslide::link::Arriving>{
                          {"videos/cd34.mp4", {progress.front(), static_cast<std::int64_t>(video.size())}}});
    CHECK(laptop.link.media_arriving(church.uid).empty());
    // Only a whole piece, checked, goes on the disk.
    const auto cut_at = progress.front();
    CHECK(cut_at == k_media_piece_bytes);
    CHECK(laptop.link.peers().empty());
    CHECK(static_cast<std::int64_t>(std::filesystem::file_size(laptop.media / "videos/cd34.mp4.part"))
          == cut_at);
    CHECK_FALSE(laptop.has_file("videos/cd34.mp4"));
    // What the connection held for its video it stored as it closed: the
    // edit is here, and taken in, its video still to come.
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 1);
    REQUIRE(missing_media(laptop.engine).size() == 1);
    CHECK(missing_media(laptop.engine).front().path == "videos/cd34.mp4");
    // Half a piece written when the power went: cut back to the piece's start.
    std::ofstream(laptop.media / "videos/cd34.mp4.part", std::ios::binary | std::ios::app)
        << video.substr(static_cast<std::size_t>(cut_at), 1000);

    // Connected again, the video goes on from where it stopped.
    progress.clear();
    laptop.link.connect_to("127.0.0.1", church.port(), church.key(), {{church.uid, laptop.join_entry()}});
    REQUIRE(laptop.link.peers().size() == 1);
    QObject::connect(laptop.link.peers().front(), &seventhslide::link::Peer::media_progress,
                     [&](const std::string&, const std::string& path, std::int64_t bytes) {
                         if (path == "videos/cd34.mp4") {
                             progress.push_back(bytes);
                         }
                     });
    REQUIRE(settle({&church, &laptop}));
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.front() == cut_at + k_media_piece_bytes);
    CHECK(laptop.file("videos/cd34.mp4") == video);
    CHECK_FALSE(std::filesystem::exists(laptop.media / "videos/cd34.mp4.part"));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.refused.empty());
    (void)laptop.sync();
    CHECK(missing_media(laptop.engine).empty());
}

TEST_CASE("A library is all here once nothing of it is still to come, a file no one has aside",
          "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    // Two pieces: the first comes while the change file waits.
    const std::string video(2 * k_media_piece_bytes, 'v');
    seventhslide::test::fill_library(church.db);
    church.put_file("videos/cd34.mp4", video);
    church.put_file("audio/ef56.mp3", "prelude");
    church.share(seventhslide::db::library_uid(church.db));
    const auto founded = church.members().found(k_passphrase, k_quick);
    church.engine.share_everything();
    REQUIRE(church.sync().changes_sent > 0);
    // The church has lost one of its files since: no computer has it now.
    std::filesystem::remove(church.media / "audio/ef56.mp3");

    // Before anyone has said what they have, nothing is known to be all here.
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    CHECK_FALSE(laptop.link.has_all_of(church.uid));

    // While the change file waits for its video, it is not.
    REQUIRE(laptop.link.peers().size() == 1);
    bool looked = false;
    bool all_here_meanwhile = false;
    QObject::connect(laptop.link.peers().front(), &seventhslide::link::Peer::media_progress,
                     [&](const std::string&, const std::string& path, std::int64_t bytes) {
                         if (path == "videos/cd34.mp4" && bytes < static_cast<std::int64_t>(video.size())) {
                             looked = true;
                             all_here_meanwhile = all_here_meanwhile || laptop.link.has_all_of(church.uid);
                         }
                     });
    REQUIRE(settle({&church, &laptop}));
    CHECK(looked);
    CHECK_FALSE(all_here_meanwhile);

    // All through: the file the church lost does not hold it back, taken in
    // or not.
    CHECK(laptop.link.has_all_of(church.uid));
    laptop.engine.join(church.uid);
    REQUIRE(laptop.sync().files_applied > 0);
    // Missing after the sync, it is asked for again, as the node does: until the
    // church says again that it has not got it, it may come.
    laptop.link.fetch_more(church.uid);
    REQUIRE(settle({&church, &laptop}));
    CHECK_FALSE(laptop.has_file("audio/ef56.mp3"));
    CHECK_FALSE(missing_media(laptop.engine).empty());
    CHECK(laptop.file("videos/cd34.mp4") == video);
    CHECK(laptop.link.has_all_of(church.uid));
    // The church said it has not got it: no one here has.
    CHECK(laptop.link.nobody_has(church.uid, "audio/ef56.mp3"));
    CHECK_FALSE(laptop.link.nobody_has(church.uid, "videos/cd34.mp4"));

    // No connection: nothing is known.
    laptop.link.peers().front()->close("the network cable came out");
    REQUIRE(settle({&church, &laptop}));
    CHECK_FALSE(laptop.link.has_all_of(church.uid));
    CHECK_FALSE(laptop.link.nobody_has(church.uid, "audio/ef56.mp3"));
}

TEST_CASE("Only members get a connection, and a viewer no admin key", "[link]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);

    SECTION("a stranger")
    {
        Computer stranger{"stranger", "Someone"};
        stranger.share(church.uid);
        stranger.link.connect_to("127.0.0.1", church.port(), church.key());
        REQUIRE(settle({&church, &stranger}));
        CHECK(church.link.peers().empty());
        CHECK(stranger.link.peers().empty());
        REQUIRE_FALSE(church.closed.empty());
        CHECK_THAT(church.closed.front().toStdString(), ContainsSubstring("shares no library"));
        CHECK(stranger.store.machines(Numbered::Changes).empty());
    }
    SECTION("not the computer expected")
    {
        Computer laptop{"laptop", "Laptop"};
        const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
        join_through(laptop, church.uid, code, SigningKey::generate().public_key(), church.port());
        REQUIRE(settle({&church, &laptop}));
        CHECK(laptop.link.peers().empty());
        REQUIRE_FALSE(laptop.closed.empty());
        CHECK_THAT(laptop.closed.front().toStdString(), ContainsSubstring("not the one expected"));
        CHECK(laptop.store.machines(Numbered::Changes).empty());
    }
    SECTION("a removed computer")
    {
        Computer laptop{"laptop", "Laptop"};
        const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
        join_through(laptop, church.uid, code, church.key(), church.port());
        REQUIRE(settle({&church, &laptop}));
        REQUIRE(laptop.link.peers().size() == 1);
        church.members().remove("laptop", founded.admin);
        church.link.library_changed(church.uid);
        REQUIRE(settle({&church, &laptop}));
        laptop.link.peers().front()->close("the day ends");
        REQUIRE(settle({&church, &laptop}));
        laptop.link.connect_to("127.0.0.1", church.port(), church.key());
        REQUIRE(settle({&church, &laptop}));
        CHECK(laptop.link.peers().empty());
        CHECK(church.link.peers().empty());
    }
    SECTION("a viewer")
    {
        Computer pew{"pew", "Pew tablet"};
        const auto code = church.members().invite(Role::Viewer, k_expires, founded.admin);
        join_through(pew, church.uid, code, church.key(), church.port());
        REQUIRE(settle({&church, &pew}));
        CHECK(pew.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
        CHECK(pew.file("images/ab12.png") == "lilies");
        CHECK(pew.store.admin_keys().empty());
        REQUIRE(church.list().find("pew") != nullptr);
        CHECK(church.list().find("pew")->role == Role::Viewer);
        // What it changes anyway goes nowhere.
        pew.engine.join(church.uid);
        pew.sync();
        pew.db.exec("UPDATE songs SET key = 'Z' WHERE title = 'Amazing Grace'");
        REQUIRE(pew.sync().changes_sent == 1);
        REQUIRE(settle({&church, &pew}));
        // The church never even asks for it.
        CHECK(church.store.sequences(Numbered::Changes, "pew").empty());
    }
}

TEST_CASE("Nothing a computer on the network sends brings a SeventhSlide down", "[link]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);

    SECTION("a hello whose name is not text")
    {
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, code, 1);
        raw.run({&church});
        // Taken as no name: the invite still lets it in.
        CHECK(raw.first("have") != nullptr);
    }
    SECTION("a machine id that would leave the folder")
    {
        RawPeer raw("../../escape");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, code);
        raw.run({&church});
        CHECK_FALSE(raw.connected());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("machine id"));
    }
    SECTION("values of the wrong kind everywhere")
    {
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, code);
        raw.send({{"type", "media"}, {"library", church.uid}, {"path", "images/x.png"}, {"offset", 0}, {"last", "yes"}});
        raw.send_bytes("x");
        raw.send({{"type", "want"}, {"library", church.uid}, {"changes", {{"..", 1}}}, {"admin_keys", {"../../x"}}});
        raw.send({{"type", "member"}, {"library", church.uid}, {"machine", "raw"}, {"sequence", 1}, {"text", "{\"format\":1,\"body\":{\"machine\":5},\"signatures\":[]}"}});
        raw.send({{"type", "have"}, {"library", church.uid}, {"changes", {{"church", "many"}}}});
        raw.run({&church});
        raw.send({{"type", 7}});
        raw.run({&church});
        CHECK_FALSE(raw.connected());
    }
    SECTION("files said to be coming that are not files, or too many")
    {
        const bool too_many = GENERATE(false, true);
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, code);
        raw.run({&church});
        std::vector<std::string> paths{"../../etc/passwd"};
        if (too_many) {
            paths.assign(1001, "images/a.png");
        }
        raw.send({{"type", "coming"}, {"library", church.uid}, {"paths", paths}});
        raw.run({&church});
        CHECK_FALSE(raw.connected());
    }
    SECTION("frames that are not frames")
    {
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.socket.write(QByteArray("\xff\xff\xff\xff\x07garbage", 12));
        raw.run({&church});
        CHECK_FALSE(raw.connected());
    }
    // The church is still there, and still syncs.
    Computer laptop{"laptop", "Laptop"};
    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
}

TEST_CASE("A viewer is not sent the locked admin key, even when it asks", "[link]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    const auto fingerprint = founded.admin.public_key().fingerprint();

    SECTION("joining as a viewer")
    {
        RawPeer raw("pew");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, church.members().invite(Role::Viewer, k_expires, founded.admin));
        raw.send({{"type", "want"}, {"library", church.uid}, {"admin_keys", {fingerprint}}});
        raw.run({&church});
        CHECK(raw.first("admin_key") == nullptr);
        const auto* missing = raw.first("missing");
        REQUIRE(missing != nullptr);
        CHECK(missing->value("kind", "") == "admin_key");
    }
    SECTION("joining as an editor")
    {
        RawPeer raw("office");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        raw.send({{"type", "want"}, {"library", church.uid}, {"admin_keys", {fingerprint}}});
        raw.run({&church});
        CHECK(raw.first("admin_key") != nullptr);
    }
}

TEST_CASE("A viewer given the admin key gets it, and one told to stop is sent no more", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer bridge{"bridge", "Bridge"};
    Computer stage{"stage", "Stage display"};
    const auto founded = put_online(church);
    join_through(bridge, church.uid, church.members().invite(Role::Viewer, k_expires, founded.admin), church.key(),
                 church.port());
    join_through(stage, church.uid, church.members().invite(Role::Viewer, k_expires, founded.admin), church.key(),
                 church.port());
    REQUIRE(settle({&church, &bridge, &stage}));
    CHECK(bridge.store.admin_keys().empty());

    church.members().set_key_holder("bridge", true, founded.admin);
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &bridge, &stage}));
    CHECK(bridge.store.admin_keys().size() == 1);
    CHECK(stage.store.admin_keys().empty());  // off for every other viewer
    CHECK(bridge.members().unlock_admin(k_passphrase).has_value());

    // Told to stop: a new key does not go to it, and the copy it has stays.
    church.members().set_key_holder("bridge", false, founded.admin);
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &bridge, &stage}));
    (void)church.members().replace_admin("consider the ravens now", founded.admin, k_quick);
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &bridge, &stage}));
    CHECK(bridge.store.admin_keys().size() == 1);
    CHECK(church.store.admin_keys().size() == 2);
}

TEST_CASE("A join entry gets in only the computer it adds, and a stranger hears no name", "[link]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);

    SECTION("someone else's entry, however it was come by")
    {
        RawPeer laptop("laptop");
        const auto entry = laptop.join_entry(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        RawPeer thief("thief");
        REQUIRE(thief.connect_to(church.port()));
        nlohmann::json offered{{"library", church.uid}, {"join", entry}};
        thief.send({{"type", "hello"},
                    {"version", seventhslide::link::k_protocol_version},
                    {"machine", "thief"},
                    {"libraries", nlohmann::json::array({offered})}});
        thief.run({&church});
        CHECK_FALSE(thief.connected());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("shares no library"));
    }
    SECTION("its own entry, on its own key")
    {
        RawPeer laptop("laptop");
        REQUIRE(laptop.connect_to(church.port()));
        laptop.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        laptop.run({&church});
        CHECK(laptop.connected());
    }
    SECTION("a code already used, however it was come by")
    {
        Computer laptop{"laptop", "Laptop"};
        const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
        join_through(laptop, church.uid, code, church.key(), church.port());
        REQUIRE(settle({&church, &laptop}));
        REQUIRE(church.list().find("laptop") != nullptr);
        church.closed.clear();

        RawPeer late("late");
        REQUIRE(late.connect_to(church.port()));
        late.hello(church.uid, code);
        late.run({&church});
        CHECK_FALSE(late.connected());
        REQUIRE_FALSE(church.closed.empty());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("shares no library"));
        CHECK(late.first("member") == nullptr);
        CHECK(late.first("have") == nullptr);

        // The computer that used it comes back as the member it is.
        CHECK(laptop.link.peer_with(church.key()) != nullptr);
    }
    SECTION("a stranger is told no name, and an invited computer is")
    {
        RawPeer stranger("stranger");
        REQUIRE(stranger.connect_to(church.port()));
        stranger.run({&church}, 150);
        const auto* hello = stranger.first("hello");
        REQUIRE(hello != nullptr);
        CHECK_FALSE(hello->contains("name"));
        CHECK(hello->at("libraries").empty());

        RawPeer laptop("laptop");
        REQUIRE(laptop.connect_to(church.port()));
        laptop.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        laptop.run({&church});
        const auto* offer = laptop.first("offer");
        REQUIRE(offer != nullptr);
        CHECK(offer->value("name", "") == "Church PC");
    }
}

TEST_CASE("Media nobody asked for is let go by, and an expired invite gets no connection",
          "[link]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);

    SECTION("media nobody asked for")
    {
        RawPeer raw("office");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        for (int i = 0; i < 4; ++i) {
            raw.send({{"type", "media"},
                      {"library", church.uid},
                      {"path", "images/filler.png"},
                      {"offset", i * 1024},
                      {"size", 1 << 30},
                      {"last", false}});
            raw.send_bytes(QByteArray(1024, 'x'));
        }
        raw.run({&church});
        CHECK(raw.connected());
        CHECK_FALSE(std::filesystem::exists(church.media / "images/filler.png.part"));
        CHECK_FALSE(church.has_file("images/filler.png"));
    }
    SECTION("an expired invite")
    {
        RawPeer raw("late");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, church.members().invite(Role::Editor, "2000-01-01T00:00:00Z", founded.admin));
        raw.run({&church});
        CHECK_FALSE(raw.connected());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("shares no library"));
    }
    SECTION("a withdrawn invite")
    {
        auto log = church.members();
        const auto code = log.invite(Role::Editor, k_expires, founded.admin);
        log.withdraw_invite(seventhslide::sync::read_invite(code).invite.id, founded.admin);
        RawPeer raw("late");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, code);
        raw.run({&church});
        CHECK_FALSE(raw.connected());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("shares no library"));
    }
}

TEST_CASE("A file stored out of turn leaves no gap behind", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    church.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    REQUIRE(church.sync().changes_sent == 1);
    church.db.exec("UPDATE songs SET key = 'B' WHERE title = 'Amazing Grace'");
    REQUIRE(church.sync().changes_sent == 1);

    // The laptop has the church's first and third files, not its second.
    for (const auto n : {1, 3}) {
        laptop.store.write(Numbered::Changes, "church", n, church.store.read(Numbered::Changes, "church", n));
    }
    for (const auto& path : {"images/ab12.png", "videos/cd34.mp4", "audio/ef56.mp3"}) {
        laptop.put_file(path, church.file(path));
    }
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1, 2, 3});
    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 3);
    CHECK(laptop.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "B");
}

TEST_CASE("A library taken offline while a computer is connected", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    laptop.engine.join(church.uid);
    laptop.sync();

    church.link.remove_library(church.uid);
    laptop.db.exec("UPDATE songs SET key = 'D' WHERE title = 'Amazing Grace'");
    REQUIRE(laptop.sync().changes_sent == 1);
    REQUIRE(settle({&church, &laptop}));
    CHECK(church.store.sequences(Numbered::Changes, "laptop").empty());

    // Put back online, it is offered to the laptop still connected, and
    // catches up.
    church.share(church.uid);
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(church.link.peers().size() == 1);
    CHECK(church.store.sequences(Numbered::Changes, "laptop") == std::vector<std::int64_t>{1});
}


namespace {

/// ``n`` songs of 2,000 characters each, about 2 MB a thousand.
void add_songs(Computer& c, int n)
{
    // Uids of their own, not random: where a snapshot's parts are cut hangs
    // on them, so the same songs are cut the same way each run.
    c.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < " + std::to_string(n)
              + ") INSERT INTO songs (id, uid, title, lyrics) SELECT 1000 + i, printf('%032x', 1000 + i), "
                "'Hymn ' || i, '[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
}

}  // namespace

TEST_CASE("A large library's change files and snapshot parts pass in pieces", "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    add_songs(church, 3000);
    const auto founded = put_online(church);
    const auto files = church.store.sequences(Numbered::Changes, "church");
    REQUIRE(files.size() >= 2);
    REQUIRE(church.store.read(Numbered::Changes, "church", 1).size() > seventhslide::link::k_inline_file);
    REQUIRE(church.engine.snapshot());
    REQUIRE(std::ranges::any_of(church.store.parts(), [](const seventhslide::sync::PartFile& part) {
        return part.size > seventhslide::link::k_inline_file;
    }));

    // Each part and piece that comes is said, for a join to wait on.
    int coming = 0;
    QObject::connect(&laptop.link, &Link::files_coming, [&](const std::string& uid) {
        CHECK(uid == church.uid);
        ++coming;
    });
    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    CHECK(laptop.refused.empty());
    CHECK(church.refused.empty());
    CHECK(laptop.store.snapshots() == church.store.snapshots());
    CHECK(laptop.store.parts().size() == church.store.parts().size());
    CHECK(coming > static_cast<int>(church.store.parts().size()));
    laptop.engine.join(church.uid);
    laptop.sync();
    seventhslide::test::check_same_records(laptop.db, church.db);
}

TEST_CASE("The largest record passes, in a change file and a snapshot part of its own", "[link][hardening]")
{
    using seventhslide::sync::k_largest_store_file;
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    // Lyrics just within the largest record.
    const auto lyrics = static_cast<std::int64_t>(seventhslide::db::k_largest_record) - 400;
    church.db.exec("INSERT INTO songs (id, title, lyrics) VALUES (999, 'Psalm 119', '[V1]\n' || substr(hex(randomblob("
                   + std::to_string(lyrics / 2 + 1) + ")), 1, " + std::to_string(lyrics) + "))");
    const auto founded = put_online(church);
    REQUIRE(church.sync().too_large.empty());
    REQUIRE(church.engine.snapshot());
    const auto largest = [](const std::vector<std::int64_t>& sizes) { return std::ranges::max(sizes); };
    std::vector<std::int64_t> files;
    for (const auto n : church.store.sequences(Numbered::Changes, "church")) {
        files.push_back(static_cast<std::int64_t>(church.store.read(Numbered::Changes, "church", n).size()));
    }
    std::vector<std::int64_t> parts;
    for (const auto& part : church.store.parts()) {
        parts.push_back(part.size);
    }
    REQUIRE(largest(files) > lyrics);
    REQUIRE(largest(parts) > lyrics);
    CHECK(largest(files) <= k_largest_store_file);
    CHECK(largest(parts) <= k_largest_store_file);

    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    CHECK(laptop.refused.empty());
    CHECK(church.closed.empty());
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == church.store.sequences(Numbered::Changes, "church"));
    CHECK(laptop.store.snapshots() == church.store.snapshots());
    CHECK(laptop.store.parts().size() == church.store.parts().size());
    laptop.engine.join(church.uid);
    laptop.sync();
    seventhslide::test::check_same_records(laptop.db, church.db);
}

TEST_CASE("A file too large to pass between computers is not sent, and is said once", "[link][hardening]")
{
    using seventhslide::sync::k_largest_store_file;
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    // One that could only be made by hand, or by a build with another cap.
    const auto n = church.store.sequences(Numbered::Changes, "church").back() + 1;
    church.store.write(Numbered::Changes, "church", n, std::string(static_cast<std::size_t>(k_largest_store_file) + 1, ' '));
    RawPeer raw("raw");
    REQUIRE(raw.connect_to(church.port()));
    raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
    raw.run({&church});
    REQUIRE(raw.first("have") != nullptr);

    const LoggedWarnings logged;
    // Asked for again, as at every sync.
    for (int round = 0; round < 2; ++round) {
        raw.got.clear();
        raw.send({{"type", "want"},
                  {"library", church.uid},
                  {"changes", nlohmann::json::array({nlohmann::json::array({"church", n})})}});
        raw.run({&church});
        const auto* missing = raw.first("missing");
        REQUIRE(missing != nullptr);
        CHECK(missing->value("kind", "") == "change");
        CHECK(missing->value("sequence", std::int64_t{0}) == n);
        CHECK(raw.first("change") == nullptr);
        CHECK(raw.first("piece") == nullptr);
    }
    CHECK(raw.connected());
    INFO(logged.all());
    CHECK(logged.count({"change/church/" + std::to_string(n), "is not sent",
                        std::to_string(k_largest_store_file + 1)})
          == 1);
}

namespace {

/// The parts in ``store``, by name.
std::vector<std::string> parts_in(FolderStore& store)
{
    std::vector<std::string> out;
    for (const auto& part : store.parts()) {
        out.push_back(part.sha256);
    }
    return out;
}

/// Send ``text`` as part ``sha256`` of ``uid``, in pieces as a computer sends one.
void send_part(RawPeer& raw, const std::string& uid, const std::string& sha256, const std::string& text)
{
    const auto size = static_cast<std::int64_t>(text.size());
    for (std::int64_t offset = 0; offset < size; offset += seventhslide::link::k_max_bytes_frame) {
        const auto n = std::min<std::int64_t>(seventhslide::link::k_max_bytes_frame, size - offset);
        raw.send({{"type", "piece"},
                  {"library", uid},
                  {"kind", "part"},
                  {"sha256", sha256},
                  {"offset", offset},
                  {"size", size}});
        raw.send_bytes(QByteArray(text.data() + offset, static_cast<qsizetype>(n)));
    }
}

/// Run ``raw`` and ``c`` until ``done``, for 20 seconds at most: whether it
/// came to be.
bool run_until(RawPeer& raw, Computer& c, const std::function<bool()>& done)
{
    for (int round = 0; round < 200 && !done(); ++round) {
        raw.run({&c}, 100);
    }
    return done();
}

/// The parts every ``want`` ``raw`` got asked for, in turn.
std::vector<std::string> parts_wanted(const RawPeer& raw)
{
    std::vector<std::string> out;
    for (const auto& m : raw.got) {
        if (m.value("type", "") == "want" && m.contains("parts")) {
            for (const auto& sha256 : m.at("parts")) {
                out.push_back(sha256.get<std::string>());
            }
        }
    }
    return out;
}

/// A library of several snapshot parts on ``church``, with ``laptop`` joined
/// and the two no longer connected, then the church's snapshot: what a
/// computer standing in for another passes on to the laptop (``RawPeer``).
struct SnapshotToPass {
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    MemberLog::Founded founded{songs_online(church)};
    seventhslide::sync::SnapshotName name;
    std::string index;
    std::vector<seventhslide::sync::SnapshotPartRef> parts;

    /// Some 12 MB of songs put online on ``c``.
    static MemberLog::Founded songs_online(Computer& c)
    {
        add_songs(c, 6000);
        return put_online(c);
    }

    SnapshotToPass()
    {
        join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                     church.key(), church.port());
        REQUIRE(settle({&church, &laptop}));
        for (auto* peer : church.link.peers()) {
            peer->close("the laptop went home");
        }
        REQUIRE(settle({&church, &laptop}));
        REQUIRE(church.engine.snapshot());
        name = church.store.snapshots().front();
        index = church.store.read_snapshot(name);
        parts = seventhslide::test::read_index(church.store, name).parts;
        REQUIRE(parts.size() >= 2);
        REQUIRE(laptop.store.snapshots().empty());
    }

    /// ``raw``, let in by an invite, says it has the snapshot (or ``which``,
    /// whose index is ``text``), and sends its index when asked.
    void offer(RawPeer& raw) { offer(raw, name, index); }
    void offer(RawPeer& raw, const seventhslide::sync::SnapshotName& which, const std::string& text)
    {
        let_in(raw);
        raw.send(have_of(which));
        raw.run({&laptop});
        const auto* want = raw.first("want");
        REQUIRE(want != nullptr);
        REQUIRE(want->at("snapshots").size() == 1);
        send_index(raw, which, text);
    }
    /// ``raw`` connects to the laptop, let in by an invite.
    void let_in(RawPeer& raw)
    {
        REQUIRE(raw.connect_to(laptop.port()));
        raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
    }
    /// A ``have`` that says it has snapshot ``which``, or the snapshot.
    [[nodiscard]] nlohmann::json have_of(const seventhslide::sync::SnapshotName& which) const
    {
        return {{"type", "have"},
                {"library", church.uid},
                {"snapshots", nlohmann::json::array({nlohmann::json::array({which.machine, which.files})})}};
    }
    [[nodiscard]] nlohmann::json have_of() const { return have_of(name); }
    void send_index(RawPeer& raw, const seventhslide::sync::SnapshotName& which, const std::string& text)
    {
        raw.send({{"type", "snapshot"},
                  {"library", church.uid},
                  {"machine", which.machine},
                  {"files", which.files},
                  {"text", text}});
        raw.run({&laptop});
    }
    /// Send each part ``raw`` is asked for, not ``sent`` already, from the
    /// church's store, until ``done`` or 10 seconds pass.
    void send_parts_asked(RawPeer& raw, std::set<std::string>& sent, const std::function<bool()>& done)
    {
        for (int round = 0; round < 100 && !done(); ++round) {
            for (const auto& sha256 : parts_wanted(raw)) {
                if (sent.insert(sha256).second) {
                    send_part(raw, church.uid, sha256, church.store.read_part(sha256));
                }
            }
            raw.run({&laptop}, 100);
        }
    }
    /// Whether the laptop holds snapshot ``which``, or the snapshot.
    [[nodiscard]] bool stored(const seventhslide::sync::SnapshotName& which)
    {
        const auto here = laptop.store.snapshots();
        return std::ranges::find(here, which) != here.end();
    }
    [[nodiscard]] bool stored() { return stored(name); }
    /// A second snapshot on the church, after an edit: most of its parts are
    /// the first's.
    seventhslide::sync::SnapshotName second_snapshot()
    {
        church.db.exec("UPDATE songs SET key = 'E' WHERE id = 3000");
        REQUIRE(church.sync().changes_sent == 1);
        REQUIRE(church.engine.snapshot());
        const auto second = church.store.snapshots().back();
        REQUIRE(second.files > name.files);
        return second;
    }
    /// The snapshot's index with ``change`` made to it, signed by the church.
    std::string forged(const std::function<void(seventhslide::sync::SnapshotIndex&)>& change)
    {
        auto label = seventhslide::test::read_index(church.store, name);
        change(label);
        return seventhslide::sync::seal(seventhslide::sync::snapshot_index_to_json(label),
                                        {{seventhslide::sync::k_signed_as_computer, &church.me.key}});
    }
    std::string part_text(std::size_t n) { return church.store.read_part(parts.at(n).sha256); }
};

}  // namespace

TEST_CASE("A snapshot is stored once its parts are; a bad part is blamed on its sender, the rest comes from another",
          "[link]")
{
    SnapshotToPass s;
    RawPeer bad("office");
    s.offer(bad);
    // It asks for the parts it has not got, a few at a time.
    const auto asked = parts_wanted(bad);
    REQUIRE(asked.size() == std::min<std::size_t>(s.parts.size(), seventhslide::link::k_parts_at_once));
    const auto first = std::ranges::find(s.parts, asked[0], &seventhslide::sync::SnapshotPartRef::sha256)
                       - s.parts.begin();
    const auto second = std::ranges::find(s.parts, asked[1], &seventhslide::sync::SnapshotPartRef::sha256)
                        - s.parts.begin();
    send_part(bad, s.church.uid, asked[0], s.part_text(static_cast<std::size_t>(first)));
    REQUIRE(run_until(bad, s.laptop, [&] { return !s.laptop.store.parts().empty(); }));
    bad.run({&s.laptop});
    // A part is kept as it comes; the snapshot is not, while one is missing.
    CHECK(parts_in(s.laptop.store) == std::vector<std::string>{asked[0]});
    CHECK(s.laptop.store.snapshots().empty());
    CHECK(s.laptop.link.busy());
    CHECK(s.laptop.refused.empty());

    // The next is not what it names: the sender is blamed, and the snapshot
    // let go of there.
    auto damaged = s.part_text(static_cast<std::size_t>(second));
    damaged[damaged.size() / 2] ^= 1;
    send_part(bad, s.church.uid, asked[1], damaged);
    REQUIRE(run_until(bad, s.laptop, [&] { return !s.laptop.refused.empty(); }));
    bad.run({&s.laptop});
    REQUIRE(s.laptop.refused.size() == 1);
    CHECK_THAT(s.laptop.refused.front().toStdString(), ContainsSubstring("not the part its snapshot names"));
    CHECK(s.laptop.store.snapshots().empty());
    CHECK(parts_in(s.laptop.store) == std::vector<std::string>{asked[0]});

    // Another has it: only the parts still missing are asked of it.
    RawPeer good("pew");
    s.offer(good);
    REQUIRE_FALSE(parts_wanted(good).empty());
    std::set<std::string> sent;
    for (int round = 0; round < 100 && s.laptop.store.snapshots().empty(); ++round) {
        for (const auto& sha256 : parts_wanted(good)) {
            CHECK(sha256 != asked[0]);
            if (sent.insert(sha256).second) {
                send_part(good, s.church.uid, sha256, s.church.store.read_part(sha256));
            }
        }
        good.run({&s.laptop}, 100);
    }
    CHECK(s.laptop.store.snapshots() == std::vector<seventhslide::sync::SnapshotName>{s.name});
    CHECK(parts_in(s.laptop.store).size() == s.parts.size());
    CHECK(s.laptop.refused.size() == 1);
    CHECK_NOTHROW(seventhslide::test::read_snapshot(s.laptop.store, s.name));
}

TEST_CASE("What came of a part let go of part way is let go of too, and the part comes whole later",
          "[link][hardening]")
{
    SnapshotToPass s;
    RawPeer raw("office");
    s.offer(raw);
    const auto asked = parts_wanted(raw);
    REQUIRE(asked.size() >= 2);
    const auto text = [&](const std::string& sha256) { return s.church.store.read_part(sha256); };
    const auto big = std::ranges::find_if(asked, [&](const std::string& sha256) {
        return static_cast<std::int64_t>(text(sha256).size()) > seventhslide::link::k_max_bytes_frame;
    });
    REQUIRE(big != asked.end());
    const auto other = *big == asked[0] ? asked[1] : asked[0];
    const auto whole = text(*big);
    const auto size = static_cast<std::int64_t>(whole.size());
    const auto piece = [&](std::int64_t offset) {
        const auto n = std::min<std::int64_t>(seventhslide::link::k_max_bytes_frame, size - offset);
        raw.send({{"type", "piece"},
                  {"library", s.church.uid},
                  {"kind", "part"},
                  {"sha256", *big},
                  {"offset", offset},
                  {"size", size}});
        raw.send_bytes(QByteArray(whole.data() + offset, static_cast<qsizetype>(n)));
    };

    // The first piece of one; then another, damaged: the snapshot is let go
    // of there, and with it the rest of the first as it comes.
    piece(0);
    auto damaged = text(other);
    damaged[damaged.size() / 2] ^= 1;
    send_part(raw, s.church.uid, other, damaged);
    REQUIRE(run_until(raw, s.laptop, [&] { return !s.laptop.refused.empty(); }));
    for (auto offset = seventhslide::link::k_max_bytes_frame; offset < size; offset += seventhslide::link::k_max_bytes_frame) {
        piece(offset);
    }
    raw.run({&s.laptop});
    REQUIRE(s.laptop.refused.size() == 1);
    CHECK(s.laptop.store.parts().empty());

    // Said again, and fetched again: the first comes whole from its start.
    raw.got.clear();
    raw.send({{"type", "have"},
              {"library", s.church.uid},
              {"snapshots", nlohmann::json::array({nlohmann::json::array({s.name.machine, s.name.files})})}});
    raw.run({&s.laptop});
    REQUIRE(raw.first("want") != nullptr);
    raw.send({{"type", "snapshot"},
              {"library", s.church.uid},
              {"machine", s.name.machine},
              {"files", s.name.files},
              {"text", s.index}});
    std::set<std::string> sent;
    for (int round = 0; round < 100 && s.laptop.store.snapshots().empty(); ++round) {
        for (const auto& sha256 : parts_wanted(raw)) {
            if (sent.insert(sha256).second) {
                send_part(raw, s.church.uid, sha256, text(sha256));
            }
        }
        raw.run({&s.laptop}, 100);
    }
    CHECK(sent.contains(*big));
    INFO(s.laptop.refused.back().toStdString());
    CHECK(s.laptop.refused.size() == 1);
    CHECK(s.laptop.store.snapshots() == std::vector<seventhslide::sync::SnapshotName>{s.name});
}

TEST_CASE("A snapshot is not taken while the disk has no room for its parts", "[link]")
{
    SnapshotToPass s;
    std::atomic<std::int64_t> free{1000 * 1000};
    s.laptop.link.shared().free_space = [&free](const std::filesystem::path&) { return free.load(); };
    RawPeer raw("office");
    s.offer(raw);
    // Refused for now, and said why; no part is asked for.
    REQUIRE(s.laptop.refused.size() == 1);
    CHECK_THAT(s.laptop.refused.front().toStdString(), ContainsSubstring("not room for it"));
    CHECK(parts_wanted(raw).empty());
    CHECK(s.laptop.store.parts().empty());
    // With room, it is asked for again when they next say what they have.
    free = std::int64_t{1} << 40;
    raw.got.clear();
    raw.send({{"type", "have"},
              {"library", s.church.uid},
              {"snapshots", nlohmann::json::array({nlohmann::json::array({s.name.machine, s.name.files})})}});
    raw.run({&s.laptop});
    const auto* want = raw.first("want");
    REQUIRE(want != nullptr);
    CHECK(want->at("snapshots").size() == 1);
}

TEST_CASE("A second snapshot sends only the parts that changed", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    add_songs(church, 6000);
    const auto founded = put_online(church);
    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(church.engine.snapshot());
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &laptop}));
    const auto first = church.store.snapshots().front();
    REQUIRE(laptop.store.snapshots() == std::vector<seventhslide::sync::SnapshotName>{first});
    const auto before = seventhslide::test::read_index(church.store, first).parts;

    church.db.exec("UPDATE songs SET key = 'E' WHERE id = 3000");
    REQUIRE(church.sync().changes_sent == 1);
    REQUIRE(church.engine.snapshot());
    const auto second = church.store.snapshots().back();
    const auto after = seventhslide::test::read_index(church.store, second).parts;
    // The parts both name go from the church: one asked for would not come,
    // and the snapshot would not be taken.
    std::size_t shared = 0;
    for (const auto& part : after) {
        if (std::ranges::find(before, part) != before.end()) {
            REQUIRE(church.store.remove_part(part.sha256, std::filesystem::file_time_type::max()));
            ++shared;
        }
    }
    REQUIRE(shared >= 1);
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &laptop}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    CHECK(laptop.refused.empty());
    CHECK(church.refused.empty());
    const auto here = laptop.store.snapshots();
    CHECK(std::ranges::find(here, second) != here.end());
}

TEST_CASE("A viewer stores a snapshot and its parts, and passes them on", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    add_songs(church, 3000);
    const auto founded = put_online(church);
    REQUIRE(church.engine.snapshot());
    pair_server(church, server, founded);
    CHECK(server.store.snapshots() == church.store.snapshots());
    CHECK(parts_in(server.store) == parts_in(church.store));

    // The laptop joins through the server alone.
    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 server.key(), server.port());
    REQUIRE(settle({&church, &server, &laptop}));
    INFO("server: " << why(server) << "; laptop: " << why(laptop));
    CHECK(laptop.link.peers().size() == 1);
    CHECK(laptop.store.snapshots() == church.store.snapshots());
    CHECK(parts_in(laptop.store) == parts_in(church.store));
    CHECK(laptop.refused.empty());
    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 0);  // the snapshot held it all
    seventhslide::test::check_same_records(laptop.db, church.db);
}

TEST_CASE("A snapshot's parts are asked for at once, not behind the media asked for", "[link]")
{
    SnapshotToPass s;
    // The laptop misses a long video, asked of the computer that offers the
    // snapshot, which never sends it.
    const seventhslide::db::MediaDigest video{"videos/long.mp4", 500 * 1000 * 1000, std::string(64, 'a'), {}};
    s.laptop.link.add_library({s.church.uid, s.laptop.shared_store, s.laptop.media, s.laptop.pieces,
                               [video] { return std::vector<seventhslide::db::MediaDigest>{video}; }, {}, {}});
    RawPeer raw("office");
    s.offer(raw);
    const auto* want = raw.first("want");
    REQUIRE(want->at("media").size() == 1);
    CHECK(parts_wanted(raw).size() == std::min(s.parts.size(), seventhslide::link::k_parts_at_once));
}

TEST_CASE("Only the newest snapshot another computer has is fetched, one at a time", "[link]")
{
    SnapshotToPass s;
    RawPeer raw("office");
    s.let_in(raw);
    // An older one beside it is not asked for.
    raw.send({{"type", "have"},
              {"library", s.church.uid},
              {"snapshots", nlohmann::json::array({nlohmann::json::array({s.name.machine, s.name.files - 1}),
                                                   nlohmann::json::array({s.name.machine, s.name.files})})}});
    raw.run({&s.laptop});
    const auto* want = raw.first("want");
    REQUIRE(want != nullptr);
    CHECK(want->at("snapshots") == nlohmann::json::array({nlohmann::json::array({s.name.machine, s.name.files})}));
    s.send_index(raw, s.name, s.index);
    REQUIRE_FALSE(parts_wanted(raw).empty());

    // A newer one is not asked for while that one waits for its parts.
    raw.got.clear();
    raw.send(s.have_of({s.name.machine, s.name.files + 5}));
    raw.run({&s.laptop});
    for (const auto& m : raw.got) {
        if (m.value("type", "") == "want") {
            CHECK(m.value("snapshots", nlohmann::json::array()).empty());
        }
    }
}

TEST_CASE("A snapshot waiting for its parts is let go of once one newer is stored", "[link]")
{
    SnapshotToPass s;
    RawPeer raw("office");
    s.offer(raw);
    REQUIRE_FALSE(parts_wanted(raw).empty());
    // A newer one comes another way, whole.
    const auto second = s.second_snapshot();
    for (const auto& part : seventhslide::test::read_index(s.church.store, second).parts) {
        s.laptop.store.write_part(part.sha256, s.church.store.read_part(part.sha256));
    }
    s.laptop.store.write_snapshot(second, s.church.store.read_snapshot(second));
    s.laptop.link.library_changed(s.church.uid);
    // Nothing more is waited for.
    CHECK(settle_while([&] { return s.laptop.link.busy(); }, 3000));
    CHECK_FALSE(s.stored());
}

TEST_CASE("A snapshot naming more parts than any library has is refused", "[link][hardening]")
{
    SnapshotToPass s;
    const auto index = s.forged([](seventhslide::sync::SnapshotIndex& label) {
        while (label.parts.size() <= seventhslide::link::k_most_parts) {
            label.parts.push_back({seventhslide::crypto::sha256_hex(std::to_string(label.parts.size())), 1});
        }
    });
    RawPeer raw("office");
    s.offer(raw, s.name, index);
    REQUIRE(s.laptop.refused.size() == 1);
    CHECK_THAT(s.laptop.refused.front().toStdString(), ContainsSubstring("more parts than any library has"));
    CHECK(parts_wanted(raw).empty());
}

TEST_CASE("A part not of the size its snapshot names is blamed on its sender", "[link][hardening]")
{
    SnapshotToPass s;
    // The index says a large part is of one byte, as if to pass the disk's
    // room; the part itself is what its hash names.
    const auto big = std::ranges::find_if(s.parts, [](const seventhslide::sync::SnapshotPartRef& part) {
        return part.size > seventhslide::link::k_max_bytes_frame;
    });
    REQUIRE(big != s.parts.end());
    const auto lied = big->sha256;
    const auto index = s.forged([&](seventhslide::sync::SnapshotIndex& label) {
        for (auto& part : label.parts) {
            if (part.sha256 == lied) {
                part.size = 1;
            }
        }
    });
    RawPeer raw("office");
    s.offer(raw, s.name, index);
    std::set<std::string> sent;
    s.send_parts_asked(raw, sent, [&] { return sent.contains(lied) || s.stored(); });
    REQUIRE(sent.contains(lied));
    raw.run({&s.laptop});
    REQUIRE_FALSE(s.laptop.refused.empty());
    CHECK_THAT(s.laptop.refused.back().toStdString(), ContainsSubstring("not of the size its snapshot names"));
    const auto here = parts_in(s.laptop.store);
    CHECK(std::ranges::find(here, lied) == here.end());
    raw.run({&s.laptop}, 1000);
    CHECK_FALSE(s.stored());
}

TEST_CASE("A part gone from the store before its snapshot is stored is fetched again", "[link]")
{
    SnapshotToPass s;
    // One part is here when the index comes; housekeeping lets it go
    // before the rest have come.
    const auto kept = s.parts.front().sha256;
    s.laptop.store.write_part(kept, s.church.store.read_part(kept));
    RawPeer raw("office");
    s.offer(raw);
    const auto asked = parts_wanted(raw);
    REQUIRE_FALSE(asked.empty());
    REQUIRE(std::ranges::find(asked, kept) == asked.end());
    REQUIRE(s.laptop.store.remove_part(kept, std::filesystem::file_time_type::max()));
    std::set<std::string> sent;
    s.send_parts_asked(raw, sent, [&] { return s.stored(); });
    CHECK(sent.contains(kept));
    REQUIRE(s.stored());
    CHECK(parts_in(s.laptop.store).size() == s.parts.size());
    CHECK_NOTHROW(seventhslide::test::read_snapshot(s.laptop.store, s.name));
}

TEST_CASE("A part two connections' snapshots share comes once, and is not missed where it came", "[link]")
{
    SnapshotToPass s;
    const auto second = s.second_snapshot();
    const auto second_index = s.church.store.read_snapshot(second);
    std::set<std::string> shared;
    for (const auto& part : seventhslide::test::read_index(s.church.store, second).parts) {
        if (std::ranges::find(s.parts, part) != s.parts.end()) {
            shared.insert(part.sha256);
        }
    }
    REQUIRE(shared.size() >= 2);
    RawPeer office("office");
    s.offer(office);
    RawPeer pew("pew");
    s.offer(pew, second, second_index);
    REQUIRE_FALSE(parts_wanted(pew).empty());

    // The first snapshot comes whole through one.
    std::set<std::string> sent;
    s.send_parts_asked(office, sent, [&] { return s.stored(); });
    REQUIRE(s.stored());

    // The other is asked for none of its parts that came, and says it has
    // not got those asked before: its snapshot still comes.
    pew.run({&s.laptop});
    const auto asked_before = parts_wanted(pew).size();
    std::set<std::string> answered;
    for (int round = 0; round < 100 && !s.stored(second); ++round) {
        for (const auto& sha256 : parts_wanted(pew)) {
            if (!answered.insert(sha256).second) {
                continue;
            }
            if (shared.contains(sha256)) {
                pew.send({{"type", "missing"}, {"library", s.church.uid}, {"kind", "part"}, {"sha256", sha256}});
            } else {
                send_part(pew, s.church.uid, sha256, s.church.store.read_part(sha256));
            }
        }
        pew.run({&s.laptop}, 100);
    }
    INFO("laptop: " << why(s.laptop));
    CHECK(s.laptop.refused.empty());
    CHECK(s.stored(second));
    const auto asked = parts_wanted(pew);
    for (auto i = asked_before; i < asked.size(); ++i) {
        CHECK_FALSE(shared.contains(asked[i]));
    }
}

TEST_CASE("A part the sender has not got lets its snapshot go there, for another to send", "[link]")
{
    SnapshotToPass s;
    RawPeer office("office");
    s.offer(office);
    const auto asked = parts_wanted(office);
    REQUIRE_FALSE(asked.empty());
    office.send({{"type", "missing"}, {"library", s.church.uid}, {"kind", "part"}, {"sha256", asked[0]}});
    office.run({&s.laptop});
    CHECK(s.laptop.refused.empty());  // nothing bad came
    // Not asked of it again until it next says what it has.
    office.got.clear();
    office.run({&s.laptop}, 1000);
    CHECK(parts_wanted(office).empty());
    CHECK(settle_while([&] { return s.laptop.link.busy(); }, 3000));

    RawPeer pew("pew");
    s.offer(pew);
    std::set<std::string> sent;
    s.send_parts_asked(pew, sent, [&] { return s.stored(); });
    CHECK(sent.contains(asked[0]));
    CHECK(s.stored());
}

TEST_CASE("A part this computer cannot write is not blamed on its sender", "[link]")
{
    SnapshotToPass s;
    // Where parts go is a file in the way.
    const auto parts = s.laptop.root.path / "sync" / "snapshot_parts";
    std::filesystem::remove_all(parts);
    std::ofstream(parts) << "in the way";
    RawPeer raw("office");
    s.offer(raw);
    const auto asked = parts_wanted(raw);
    REQUIRE_FALSE(asked.empty());
    const auto n = std::ranges::find(s.parts, asked[0], &seventhslide::sync::SnapshotPartRef::sha256) - s.parts.begin();
    send_part(raw, s.church.uid, asked[0], s.part_text(static_cast<std::size_t>(n)));
    REQUIRE(run_until(raw, s.laptop, [&] { return !s.laptop.refused.empty(); }));
    CHECK_THAT(s.laptop.refused.front().toStdString(), ContainsSubstring("could not be written"));
    CHECK_FALSE(s.stored());
    // Its own trouble: not asked again when the sender next says what it has,
    // as one the sender spoiled would be, but once more is known here.
    raw.got.clear();
    raw.send(s.have_of());
    raw.run({&s.laptop}, 1000);
    for (const auto& m : raw.got) {
        if (m.value("type", "") == "want") {
            CHECK(m.value("snapshots", nlohmann::json::array()).empty());
        }
    }
}

TEST_CASE("A part damaged where it is sent from is set aside there, and said missing", "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    add_songs(church, 3000);
    const auto founded = put_online(church);
    REQUIRE(church.engine.snapshot());
    const auto sha256 = church.store.parts().front().sha256;
    {
        auto text = church.store.read_part(sha256);
        text[text.size() / 2] ^= 1;
        std::ofstream(church.root.path / "sync" / "snapshot_parts" / (sha256 + ".json"), std::ios::binary) << text;
    }
    RawPeer raw("office");
    REQUIRE(raw.connect_to(church.port()));
    raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
    raw.run({&church});
    raw.got.clear();
    raw.send({{"type", "want"}, {"library", church.uid}, {"parts", nlohmann::json::array({sha256})}});
    raw.run({&church}, 1000);
    const auto* missing = raw.first("missing");
    REQUIRE(missing != nullptr);
    CHECK(missing->value("kind", "") == "part");
    CHECK(missing->value("sha256", "") == sha256);
    CHECK(raw.first("part") == nullptr);
    CHECK(raw.first("piece") == nullptr);
    const auto here = parts_in(church.store);
    CHECK(std::ranges::find(here, sha256) == here.end());
}

TEST_CASE("A snapshot's parts another connection was fetching when it paused or closed come through this one",
          "[link]")
{
    const bool pauses = GENERATE(true, false);
    INFO((pauses ? "paused" : "closed"));
    SnapshotToPass s;
    RawPeer office("office");
    s.offer(office);
    const auto asked = parts_wanted(office);
    REQUIRE_FALSE(asked.empty());
    send_part(office, s.church.uid, asked[0], s.church.store.read_part(asked[0]));
    REQUIRE(run_until(office, s.laptop, [&] { return !s.laptop.store.parts().empty(); }));

    // Another has it too; while the first fetches it, it is not asked for.
    RawPeer pew("pew");
    s.let_in(pew);
    pew.send(s.have_of());
    pew.run({&s.laptop});
    for (const auto& m : pew.got) {
        if (m.value("type", "") == "want") {
            REQUIRE(m.value("snapshots", nlohmann::json::array()).empty());
        }
    }
    if (pauses) {
        office.send({{"type", "pause"}, {"paused", true}});
        office.run({&s.laptop});
    } else {
        office.socket.abort();
    }
    pew.got.clear();
    REQUIRE(run_until(pew, s.laptop, [&] { return pew.first("want") != nullptr; }));
    REQUIRE(pew.first("want")->at("snapshots").size() == 1);
    s.send_index(pew, s.name, s.index);
    std::set<std::string> sent;
    s.send_parts_asked(pew, sent, [&] { return s.stored(); });
    CHECK(s.stored());
    CHECK_FALSE(sent.contains(asked[0]));  // here already
    CHECK(s.laptop.refused.empty());
}

TEST_CASE("A snapshot whose sender sends no part for a while is fetched from another", "[link]")
{
    SnapshotToPass s;
    s.laptop.link.shared().parts_quiet_ms = 1000;
    RawPeer quiet("office");
    s.offer(quiet);
    REQUIRE_FALSE(parts_wanted(quiet).empty());
    RawPeer pew("pew");
    s.let_in(pew);
    pew.send(s.have_of());
    pew.run({&s.laptop});
    REQUIRE(std::ranges::none_of(pew.got, [](const nlohmann::json& m) {
        return m.value("type", "") == "want" && !m.value("snapshots", nlohmann::json::array()).empty();
    }));
    pew.got.clear();
    REQUIRE(run_until(pew, s.laptop, [&] { return pew.first("want") != nullptr; }));
    REQUIRE(pew.first("want")->at("snapshots").size() == 1);
    s.send_index(pew, s.name, s.index);
    std::set<std::string> sent;
    s.send_parts_asked(pew, sent, [&] { return s.stored(); });
    CHECK(s.stored());
    CHECK(s.laptop.refused.empty());
}


TEST_CASE("No message is longer than a change file sent whole makes one", "[link][hardening]")
{
    using seventhslide::link::k_inline_file;
    using seventhslide::link::k_max_bytes_frame;
    using seventhslide::link::k_max_json_frame;
    // The longest a change file sent whole can make its message: every byte
    // written as six, and the longest names there are.
    const nlohmann::json change{{"type", "change"},
                                {"library", std::string(32, 'a')},
                                {"machine", std::string(64, 'm')},
                                {"sequence", std::int64_t{1} << 62},
                                {"text", std::string(static_cast<std::size_t>(k_inline_file), '\x01')}};
    CHECK(static_cast<std::int64_t>(seventhslide::link::encode_message(change).size()) - 5 <= k_max_json_frame);

    const auto header = [](std::int64_t payload, char kind) {
        const auto n = static_cast<std::uint32_t>(payload + 1);
        const char bytes[] = {static_cast<char>(n >> 24), static_cast<char>(n >> 16), static_cast<char>(n >> 8),
                              static_cast<char>(n), kind};
        return QByteArray(bytes, sizeof bytes);
    };
    seventhslide::link::FrameReader reader;
    SECTION("a message as long as one may be is taken")
    {
        reader.feed(header(k_max_json_frame, 0) + QByteArray(static_cast<qsizetype>(k_max_json_frame), ' '));
        const auto frame = reader.next();
        REQUIRE(frame);
        CHECK(frame->payload.size() == k_max_json_frame);
    }
    SECTION("one a byte longer is not")
    {
        reader.feed(header(k_max_json_frame + 1, 0));
        CHECK_THROWS_AS(reader.next(), std::runtime_error);
    }
    SECTION("nor a bytes frame longer than a piece")
    {
        reader.feed(header(k_max_bytes_frame + 1, 1));
        CHECK_THROWS_AS(reader.next(), std::runtime_error);
    }
}

TEST_CASE("Many frames come out of one large buffer whole and in order", "[link]")
{
    using seventhslide::link::encode_bytes;
    using seventhslide::link::encode_message;
    constexpr int count = 20000;
    QByteArray all;
    for (int i = 0; i < count; ++i) {
        all += i % 3 == 0 ? encode_bytes(QByteArray(i % 97, static_cast<char>(i))) : encode_message({{"n", i}});
    }
    // Most of it in one feed, the last frame cut in two, then the rest in odd
    // pieces between reads, so what is left over is kept across each feed.
    const qsizetype first = all.size() - 7;
    seventhslide::link::FrameReader reader;
    reader.feed(all.first(first));
    int taken = 0;
    const auto take_all = [&] {
        while (auto frame = reader.next()) {
            if (taken % 3 == 0) {
                REQUIRE(frame->bytes);
                REQUIRE(frame->payload == QByteArray(taken % 97, static_cast<char>(taken)));
            } else {
                REQUIRE_FALSE(frame->bytes);
                REQUIRE(nlohmann::json::parse(frame->payload.toStdString()) == nlohmann::json{{"n", taken}});
            }
            ++taken;
        }
    };
    take_all();
    CHECK(taken == count - 1);
    for (qsizetype at = first; at < all.size(); at += 3) {
        reader.feed(all.sliced(at, std::min<qsizetype>(3, all.size() - at)));
        take_all();
    }
    CHECK(taken == count);
    CHECK_FALSE(reader.next());

    // A reader drained to its last byte goes on as a new one would.
    reader.feed(encode_message({{"n", 1}}) + encode_message({{"n", 2}}).first(3));
    REQUIRE(reader.next());
    CHECK_FALSE(reader.next());
    reader.feed(encode_message({{"n", 2}}).sliced(3));
    const auto last = reader.next();
    REQUIRE(last);
    CHECK(nlohmann::json::parse(last->payload.toStdString()) == nlohmann::json{{"n", 2}});
}

TEST_CASE("A computer takes no more in pieces at once than one file may be", "[link][hardening]")
{
    using seventhslide::sync::k_largest_store_file;
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    RawPeer raw("raw");
    REQUIRE(raw.connect_to(church.port()));
    raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
    raw.run({&church});
    REQUIRE(raw.first("have") != nullptr);
    // A snapshot newer than any the church has, and a change file of its own
    // past its last: it asks for both.
    const auto next = church.store.sequences(Numbered::Changes, "church").back() + 1;
    raw.send({{"type", "have"},
              {"library", church.uid},
              {"changes", {{"church", next}}},
              {"snapshots", nlohmann::json::array({nlohmann::json::array({"raw", 1000})})}});
    raw.run({&church});
    const auto* want = raw.first("want");
    REQUIRE(want != nullptr);
    REQUIRE(want->at("snapshots").size() == 1);
    REQUIRE(want->at("changes").size() == 1);
    const auto piece = [&](std::int64_t number, std::int64_t size) {
        const bool snapshot = number == 1000;
        raw.send({{"type", "piece"},
                  {"library", church.uid},
                  {"kind", snapshot ? "snapshot" : "change"},
                  {"machine", snapshot ? "raw" : "church"},
                  {"number", number},
                  {"offset", 0},
                  {"size", size}});
        raw.send_bytes(QByteArray(static_cast<qsizetype>(seventhslide::link::k_max_bytes_frame), '{'));
        raw.run({&church});
    };

    SECTION("one larger than any sent in pieces")
    {
        const LoggedWarnings logged;
        piece(1000, k_largest_store_file + 1);
        CHECK_FALSE(raw.connected());
        REQUIRE_FALSE(church.closed.empty());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("larger than any sent in pieces"));
        // Said, with what it was and its size.
        INFO(logged.all());
        CHECK(logged.count({"Raw sent", "snapshot/raw/1000", std::to_string(k_largest_store_file + 1)}) == 1);
    }
    // What the first holds so far and the second whole: one file's worth, or
    // a byte more.
    SECTION("one as large as may be, then another that fits beside what came of it")
    {
        piece(1000, k_largest_store_file);
        REQUIRE(raw.connected());
        piece(next, k_largest_store_file - seventhslide::link::k_max_bytes_frame);
        CHECK(raw.connected());
    }
    SECTION("one as large as may be, then another a byte too large beside it")
    {
        piece(1000, k_largest_store_file);
        REQUIRE(raw.connected());
        const LoggedWarnings logged;
        piece(next, k_largest_store_file - seventhslide::link::k_max_bytes_frame + 1);
        CHECK_FALSE(raw.connected());
        REQUIRE_FALSE(church.closed.empty());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("more of files in pieces"));
        INFO(logged.all());
        CHECK(logged.count({"Raw sent", "change/church/" + std::to_string(next), "beside"}) == 1);
    }
}

TEST_CASE("A computer that sends a file too large is said once, whatever size it says", "[link][hardening]")
{
    using seventhslide::sync::k_largest_store_file;
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    const LoggedWarnings logged;
    // Once reconnected, it may send it again, of another size.
    for (const auto size : {k_largest_store_file + 1, k_largest_store_file + 2}) {
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        raw.send({{"type", "have"},
                  {"library", church.uid},
                  {"snapshots", nlohmann::json::array({nlohmann::json::array({"raw", 1000})})}});
        raw.run({&church});
        const auto* want = raw.first("want");
        REQUIRE(want != nullptr);
        REQUIRE(want->at("snapshots").size() == 1);
        raw.send({{"type", "piece"},
                  {"library", church.uid},
                  {"kind", "snapshot"},
                  {"machine", "raw"},
                  {"number", 1000},
                  {"offset", 0},
                  {"size", size}});
        raw.send_bytes(QByteArray(static_cast<qsizetype>(seventhslide::link::k_max_bytes_frame), '{'));
        raw.run({&church});
        REQUIRE_FALSE(raw.connected());
    }
    INFO(logged.all());
    CHECK(logged.count({"Raw sent", "snapshot/raw/1000"}) == 1);
}

TEST_CASE("A computer that speaks another protocol is refused, saying which to update",
          "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    constexpr int here = seventhslide::link::k_protocol_version;
    for (const int version : {here - 1, here + 1}) {
        INFO("protocol " << version);
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin), "Raw",
                  version);
        raw.run({&church});
        CHECK(raw.first("have") == nullptr);
        REQUIRE_FALSE(church.closed.empty());
        CHECK_THAT(church.closed.back().toStdString(),
                   ContainsSubstring(version < here ? "an earlier SeventhSlide" : "a newer SeventhSlide"));
    }
}

TEST_CASE("A computer not yet let in holds little open, and learns nothing", "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);

    SECTION("a large message before hello")
    {
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        // Only the header of a message longer than one may be before hello.
        const auto n = static_cast<std::uint32_t>(seventhslide::link::k_max_json_frame_unadmitted + 2);
        const char header[] = {static_cast<char>(n >> 24), static_cast<char>(n >> 16),
                               static_cast<char>(n >> 8), static_cast<char>(n), 0};
        raw.socket.write(header, sizeof header);
        raw.run({&church});
        CHECK_FALSE(raw.connected());
        REQUIRE_FALSE(church.closed.empty());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("not a sync message"));
    }
    SECTION("one that never says hello")
    {
        church.link.set_hello_timeout(300);
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.run({&church}, 1200);
        CHECK_FALSE(raw.connected());
        REQUIRE_FALSE(church.closed.empty());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("did not say who it is"));
    }
    SECTION("too many at once from one address")
    {
        std::vector<std::unique_ptr<RawPeer>> waiting;
        for (int i = 0; i < seventhslide::link::k_max_unadmitted_per_address; ++i) {
            waiting.push_back(std::make_unique<RawPeer>("raw" + std::to_string(i)));
            REQUIRE(waiting.back()->connect_to(church.port()));
            waiting.back()->run({&church}, 100);
        }
        RawPeer extra("extra");
        (void)extra.connect_to(church.port());
        extra.run({&church}, 300);
        CHECK_FALSE(extra.connected());
        for (const auto& raw : waiting) {
            CHECK(raw->connected());
        }
        // One let in makes room for another.
        waiting.front()->hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        waiting.front()->run({&church});
        REQUIRE(waiting.front()->first("have") != nullptr);
        RawPeer next("next");
        REQUIRE(next.connect_to(church.port()));
        next.run({&church}, 300);
        CHECK(next.connected());
    }
    SECTION("its hello names no library; an invite gets one offered")
    {
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.run({&church}, 200);
        const auto* hello = raw.first("hello");
        REQUIRE(hello != nullptr);
        CHECK(hello->at("libraries").empty());
        raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        raw.run({&church});
        const auto* offer = raw.first("offer");
        REQUIRE(offer != nullptr);
        CHECK(offer->at("library") == church.uid);
        CHECK(raw.first("have") != nullptr);
    }
}

#if !defined(_WIN32)
TEST_CASE("A computer that ran out of open files takes connections again once it has some",
          "[link][hardening]")
{
    Computer church{"church", "Church PC"};

    // Every descriptor this process may have, used up but one.
    std::vector<int> held;
    for (;;) {
        const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            REQUIRE(errno == EMFILE);
            break;
        }
        held.push_back(fd);
        if (held.size() > 200000) {
            for (const int h : held) {
                ::close(h);
            }
            SKIP("This process may open too many files to run out");
        }
    }
    REQUIRE(held.size() > 1);
    ::close(held.back());
    held.pop_back();

    // That one is the caller's socket: the computer's accept() then fails
    // with EMFILE, and QTcpServer stops taking connections.
    QTcpSocket caller;
    caller.connectToHost(QHostAddress::LocalHost, church.port());
    run_events_for(300);
    for (const int fd : held) {
        ::close(fd);
    }

    // Files free again: a computer that connects now is let in (within the
    // second the link waits before it tries again).
    RawPeer later("later");
    CHECK(later.connect_to(church.port()));
}
#endif

namespace {

/// ``pool``'s one thread kept busy until ``release`` (or the end of the test).
struct HeldThread {
    explicit HeldThread(QThreadPool& pool)
    {
        // The thread's to delete, once it has seen it set: it may look at it
        // once more after this is gone (between waking and seeing it).
        pool.start([flag = go_] {
            while (!flag->load()) {
                QThread::msleep(5);
            }
            delete flag;
        });
    }
    HeldThread(const HeldThread&) = delete;
    HeldThread& operator=(const HeldThread&) = delete;
    ~HeldThread() { release(); }
    /// Set once: after that the thread may have deleted it.
    void release()
    {
        if (!released_) {
            released_ = true;
            go_->store(true);
        }
    }

private:
    std::atomic<bool>* go_ = new std::atomic<bool>(false);
    bool released_ = false;
};

}  // namespace

using seventhslide::test::wait_until;

TEST_CASE("A change file waits a while at most for its media, then is stored without it", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    laptop.link.set_hold_for_media(400);
    const auto video = big_video(2 * k_media_piece_bytes);
    const auto founded = put_online_with(church, video);
    // Its files are not sent for now (the church's thread for reading them
    // through, held): as a video coming over a slow line.
    auto held = std::make_unique<HeldThread>(church.link.shared().checks);

    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    QElapsedTimer since;
    since.start();
    join_through(laptop, church.uid, code, church.key(), church.port());
    const auto stored = [&] { return !laptop.store.sequences(Numbered::Changes, "church").empty(); };
    REQUIRE(wait_until([&] { return laptop.link.media_coming(church.uid).contains("videos/cd34.mp4"); }));
    CHECK_FALSE(stored());

    // Held for it, but not for long: stored without it, the edit taken in.
    REQUIRE(wait_until(stored));
    CHECK(since.elapsed() >= 400);
    CHECK_FALSE(laptop.has_file("videos/cd34.mp4"));
    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 1);
    // Its files are named, and missing.
    const auto missing = missing_media(laptop.engine);
    CHECK(std::ranges::any_of(missing, [](const auto& d) { return d.path == "videos/cd34.mp4"; }));
    // Still on its way, and fetched on: the library is not all here yet.
    CHECK(laptop.link.media_coming(church.uid).contains("videos/cd34.mp4"));
    CHECK_FALSE(laptop.link.has_all_of(church.uid));

    held.reset();
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.file("videos/cd34.mp4") == video);
    CHECK(laptop.refused.empty());
    (void)laptop.sync();
    CHECK(missing_media(laptop.engine).empty());
    CHECK(laptop.link.has_all_of(church.uid));
}

TEST_CASE("An edit made while a big file is still coming is asked for at once", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto video = big_video(2 * k_media_piece_bytes);
    const auto founded = put_online_with(church, video);
    // The video is asked for, and none of it comes yet: as one coming over a
    // slow line, for as long as it takes.
    auto held = std::make_unique<HeldThread>(church.link.shared().checks);
    // What the laptop asks for, as it says it.
    static std::vector<std::string> asked;
    asked.clear();
    QLoggingCategory::setFilterRules(QStringLiteral("seventhslide.link.debug=true"));
    const auto previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& text) {
        if (text.startsWith(QStringLiteral("laptop -> church want"))) {
            asked.push_back(text.toStdString());
        }
    });
    const auto restore = qScopeGuard([previous] {
        qInstallMessageHandler(previous);
        QLoggingCategory::setFilterRules(QString());
    });
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(wait_until([&] { return laptop.link.media_coming(church.uid).contains("videos/cd34.mp4"); }));
    const auto stored = [&] { return laptop.store.sequences(Numbered::Changes, "church"); };

    // An edit at the church after the video: the laptop asks for it though the
    // video is still on its way, and has it long before the minute is up.
    church.db.exec("UPDATE services SET name = 'Evensong'");
    REQUIRE(church.sync().changes_sent == 1);
    REQUIRE(wait_until([&] {
        const auto s = stored();
        return std::ranges::find(s, std::int64_t{2}) != s.end();
    }, 10000));
    CHECK_FALSE(laptop.has_file("videos/cd34.mp4"));
    // The video was asked for once: what is on its way is not asked for again.
    run_events_for(500);
    const auto video_asks = std::ranges::count_if(asked, [](const std::string& line) {
        return line.find("videos/cd34.mp4") != std::string::npos;
    });
    CHECK(video_asks == 1);
    // Nor is the other end asked for nothing.
    CHECK(std::ranges::none_of(asked, [](const std::string& line) {
        return line.find("want") + 4 == line.size();
    }));

    held.reset();
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.file("videos/cd34.mp4") == video);
    CHECK(stored() == std::vector<std::int64_t>{1, 2});
    CHECK(laptop.refused.empty());
}

TEST_CASE("A small file is waited for, and the edit naming it never seen without it", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    // Each time something goes into the laptop's store, what it names is here.
    bool stored = false;
    bool all_there = true;
    QObject::connect(&laptop.link, &seventhslide::link::Link::store_changed, [&](const std::string&) {
        if (!laptop.store.sequences(Numbered::Changes, "church").empty()) {
            stored = true;
            all_there = all_there && laptop.has_file("images/ab12.png") && laptop.has_file("videos/cd34.mp4")
                        && laptop.has_file("audio/ef56.mp3");
        }
    });
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    CHECK(stored);
    CHECK(all_there);
}

TEST_CASE("A connection that closes stores what it held, and later edits are taken in after it",
          "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto video = big_video(2 * k_media_piece_bytes);
    const auto founded = put_online_with(church, video);
    // Its files are not sent for now.
    auto held = std::make_unique<HeldThread>(church.link.shared().checks);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(wait_until([&] { return laptop.link.media_coming(church.uid).contains("videos/cd34.mp4"); }));
    const auto stored = [&] { return laptop.store.sequences(Numbered::Changes, "church"); };
    CHECK(stored().empty());

    // The church goes away mid-video, as one switched off: not left to be
    // fetched again from a computer that may be gone for days.
    REQUIRE(laptop.link.peers().size() == 1);
    int told = 0;
    QObject::connect(&laptop.link, &seventhslide::link::Link::store_changed, [&](const std::string&) { ++told; });
    laptop.link.peers().front()->close("the church PC was switched off");
    REQUIRE(wait_until([&] { return stored() == std::vector<std::int64_t>{1}; }));
    // The library hears of it, as of anything stored, though the connection is gone.
    REQUIRE(wait_until([&] { return told > 0; }));
    CHECK(laptop.link.media_coming(church.uid).empty());  // no connection: nothing known to come
    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 1);

    // Back on, with an edit made since: it is taken in, the video still to come.
    church.db.exec("UPDATE services SET name = 'Evensong'");
    REQUIRE(church.sync().changes_sent > 0);
    laptop.link.connect_to("127.0.0.1", church.port(), church.key(), {{church.uid, laptop.join_entry()}});
    REQUIRE(wait_until([&] { return stored().size() == 2; }));
    CHECK(laptop.sync().files_applied == 1);
    CHECK(laptop.text("SELECT name FROM services") == "Evensong");
    CHECK_FALSE(laptop.has_file("videos/cd34.mp4"));

    held.reset();
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.file("videos/cd34.mp4") == video);
}

TEST_CASE("What a file was stored without is still to come, until it is here or no one has it",
          "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto video = big_video(2 * k_media_piece_bytes);
    const auto founded = put_online_with(church, video);
    auto held = std::make_unique<HeldThread>(church.link.shared().checks);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(wait_until([&] { return laptop.link.media_coming(church.uid).contains("videos/cd34.mp4"); }));
    REQUIRE(laptop.link.peers().size() == 1);
    const auto stored = [&] { return laptop.store.sequences(Numbered::Changes, "church"); };
    const auto cut_off = [&] { laptop.link.peers().front()->close("the church PC was switched off"); };
    const auto back_on = [&] {
        laptop.link.connect_to("127.0.0.1", church.port(), church.key(), {{church.uid, laptop.join_entry()}});
    };

    SECTION("the link is busy until what the connection held is written")
    {
        auto writing = std::make_unique<HeldThread>(laptop.link.shared().files);
        cut_off();
        CHECK(laptop.link.busy());
        CHECK(stored().empty());
        // Still being fetched: no other connection is to ask for it meanwhile.
        CHECK(laptop.link.shared().fetches.claimed(church.uid, "change/church/1"));
        writing.reset();
        laptop.link.wait_for_writes();
        CHECK(stored() == std::vector<std::int64_t>{1});
        REQUIRE(wait_until([&] { return !laptop.link.busy(); }));
        CHECK_FALSE(laptop.link.shared().fetches.claimed(church.uid, "change/church/1"));
    }
    SECTION("asked for by nothing while paused, it is not all here")
    {
        cut_off();
        REQUIRE(wait_until([&] { return stored() == std::vector<std::int64_t>{1}; }));
        laptop.link.set_paused(true);
        back_on();
        REQUIRE(wait_until([&] {
            return !laptop.link.peers().empty() && laptop.link.peers().front()->told(church.uid);
        }));
        run_events_for(300);
        CHECK(laptop.link.media_coming(church.uid).contains("videos/cd34.mp4"));
        CHECK_FALSE(laptop.link.has_all_of(church.uid));
    }
    SECTION("one no computer has holds nothing back")
    {
        cut_off();
        REQUIRE(wait_until([&] { return stored() == std::vector<std::int64_t>{1}; }));
        std::filesystem::remove(church.media / "videos/cd34.mp4");
        held.reset();
        back_on();
        REQUIRE(settle({&church, &laptop}));
        CHECK_FALSE(laptop.has_file("videos/cd34.mp4"));
        CHECK(laptop.has_file("images/ab12.png"));
        CHECK(laptop.link.media_coming(church.uid).empty());
        CHECK(laptop.link.has_all_of(church.uid));
    }
    SECTION("one past the size limit since is not fetched, nor waited for")
    {
        cut_off();
        REQUIRE(wait_until([&] { return stored() == std::vector<std::int64_t>{1}; }));
        laptop.room = 1000;  // room for the small files, not the video
        held.reset();
        back_on();
        REQUIRE(settle({&church, &laptop}));
        CHECK(laptop.has_file("images/ab12.png"));
        CHECK_FALSE(laptop.has_file("videos/cd34.mp4"));
        CHECK_FALSE(laptop.link.media_coming(church.uid).contains("videos/cd34.mp4"));
        CHECK(laptop.link.has_all_of(church.uid));
    }
    SECTION("one that has come is not said to be coming, though nothing more is asked for")
    {
        cut_off();
        REQUIRE(wait_until([&] { return stored() == std::vector<std::int64_t>{1}; }));
        held.reset();
        QObject::connect(&laptop.link, &Link::media_stored, [&](const std::string&, const std::string& path) {
            if (path == "videos/cd34.mp4") {
                laptop.link.set_paused(true);
            }
        });
        back_on();
        REQUIRE(wait_until([&] { return laptop.has_file("videos/cd34.mp4"); }, 30000));
        CHECK(wait_until([&] { return !laptop.link.media_coming(church.uid).contains("videos/cd34.mp4"); }, 5000));
    }
}

TEST_CASE("What is on its way is told once, within limits, and not while paused", "[link]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    church.link.set_tell_coming_every(20);
    RawPeer raw("raw");
    REQUIRE(raw.connect_to(church.port()));
    raw.hello(church.uid, code);
    raw.run({&church});
    REQUIRE(church.link.peers().size() == 1);
    auto* peer = church.link.peers().front();
    const auto said = [&] {
        return std::ranges::count_if(raw.got, [](const nlohmann::json& m) { return m.value("type", "") == "coming"; });
    };
    // Nothing on its way: nothing said.
    CHECK(said() == 0);

    church.link.set_tell_coming_every(3600 * 1000);
    church.link.set_paused(true);
    raw.run({&church}, 100);
    peer->tell_coming(church.uid, {"images/a.png"});
    raw.run({&church}, 100);
    CHECK(said() == 0);
    church.link.set_paused(false);
    raw.run({&church}, 100);

    peer->tell_coming(church.uid, {"images/a.png"});
    peer->tell_coming(church.uid, {"images/a.png"});
    raw.run({&church}, 100);
    CHECK(said() == 1);

    // It may have forgotten, having shared the library afresh: told again.
    raw.send({{"type", "offer"}, {"library", church.uid}, {"name", "Raw"}});
    raw.run({&church}, 100);
    peer->tell_coming(church.uid, {"images/a.png"});
    raw.run({&church}, 100);
    CHECK(said() == 2);

    // Never more than the other end will take.
    std::set<std::string> many;
    for (int i = 0; i < 1500; ++i) {
        many.insert("images/" + std::to_string(i) + ".png");
    }
    peer->tell_coming(church.uid, many);
    raw.run({&church}, 200);
    REQUIRE(said() == 3);
    CHECK(raw.got.back().value("paths", nlohmann::json::array()).size() == 1000);
    CHECK(raw.connected());

    // The library taken offline here: none of that is on its way any more.
    church.link.remove_library(church.uid);
    raw.run({&church}, 100);
    REQUIRE(said() == 4);
    CHECK(raw.got.back().value("paths", nlohmann::json::array({"x"})).empty());
}

TEST_CASE("A file a connected computer is itself getting reads as on its way to that one", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    pair_server(church, server, founded);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, server.key(), server.port());
    REQUIRE(settle({&church, &server, &laptop}));
    laptop.engine.join(church.uid);
    REQUIRE(laptop.sync().files_applied == 1);

    // The church's new video does not leave it for now. The server has the
    // edit and is getting the video; the laptop, connected only to the server,
    // has the edit and hears the server has not got the video.
    server.link.set_hold_for_media(100);
    laptop.link.set_hold_for_media(100);
    auto held = std::make_unique<HeldThread>(church.link.shared().checks);
    const std::string path = "videos/new1.mp4";
    const auto video = big_video(k_media_piece_bytes);
    church.put_file(path, video);
    church.db.exec("INSERT INTO video_files (filename, display_name) VALUES ('new1.mp4', 'New')");
    REQUIRE(church.sync().changes_sent == 1);
    REQUIRE(wait_until([&] { return laptop.link.peers().front()->said_missing(church.uid, path); }));

    REQUIRE(wait_until([&] { return laptop.link.coming_through(church.uid, path).has_value(); }));
    CHECK(*laptop.link.coming_through(church.uid, path) == "Church server");
    CHECK_FALSE(laptop.link.nobody_has(church.uid, path));

    // It comes on through the server, the laptop's library having taken in
    // the edit and named the video as missing.
    REQUIRE(wait_until([&] { return laptop.store.sequences(Numbered::Changes, "church").size() == 2; }));
    REQUIRE(laptop.sync().files_applied == 1);
    held.reset();
    REQUIRE(wait_until([&] { return laptop.has_file(path); }, 30000));
    CHECK(laptop.file(path) == video);
    REQUIRE(wait_until([&] { return !laptop.link.coming_through(church.uid, path).has_value(); }));
    CHECK(laptop.refused.empty());
}

TEST_CASE("A file being checked when the pause comes is let go of, and comes again after", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    {
        // The laptop's file thread held up: the change file waits there to
        // be checked when the laptop pauses, and is checked only after.
        HeldThread held(laptop.link.shared().files);
        join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                     church.key(), church.port());
        REQUIRE(wait_until([&] {
            return laptop.store.sequences(Numbered::Members, "church") == std::vector<std::int64_t>{1, 2}
                   && laptop.link.busy();
        }));
        laptop.link.set_paused(true);
        held.release();
        REQUIRE(settle({&church, &laptop}));
    }
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    CHECK(laptop.store.sequences(Numbered::Changes, "church").empty());
    CHECK_FALSE(laptop.has_file("images/ab12.png"));

    laptop.link.set_paused(false);
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.file("videos/cd34.mp4") == "welcome");
    CHECK(laptop.link.has_all_of(church.uid));
    CHECK(laptop.refused.empty());
    CHECK(church.refused.empty());
}

TEST_CASE("A connection that once replaced another gives way to a new one like any other", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin), church.key(),
                 church.port());
    REQUIRE(settle({&church, &laptop}));

    // A closer way: the new one stays at both ends, the old one goes.
    auto* old_one = laptop.link.peers().front();
    auto* closer = laptop.link.connect_instead("127.0.0.1", church.port(), church.key());
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(laptop.link.peers().size() == 1);
    CHECK(laptop.link.peers().front() == closer);
    CHECK(laptop.link.peers().front() != old_one);
    REQUIRE(church.link.peers().size() == 1);
    CHECK(church.link.peers().front()->replaces());

    // The laptop gone without a word, and back the usual way, as after a power
    // cut: its new connection is kept, not the dead one that once replaced.
    auto* dead_at_church = church.link.peers().front();
    auto* fresh = laptop.link.connect_to("127.0.0.1", church.port(), std::nullopt);
    REQUIRE(settle({&church, &laptop}));
    CHECK(church.link.peers().size() == 1);
    CHECK(laptop.link.peers().size() == 1);
    CHECK(laptop.link.peers().front() == fresh);
    CHECK(church.link.peers().front() != dead_at_church);
}

TEST_CASE("Connections that go away, however many, leave the door open", "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    const auto port = church.port();

    // As a port scanner does: in, and gone before any TLS. Far more than the 30
    // that used to stop a computer taking connections for good.
    std::atomic<bool> done{false};
    std::thread scanner([&] {
        for (int i = 0; i < 400; ++i) {
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, port);
            (void)socket.waitForConnected(500);
            socket.abort();
        }
        done = true;
    });
    while (!done) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    scanner.join();
    settle({&church});

    RawPeer raw("office");
    REQUIRE(raw.connect_to(port));
    raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
    raw.run({&church});
    CHECK(raw.connected());
    CHECK(raw.first("have") != nullptr);
}

TEST_CASE("Those hanging back from many addresses give way to a real computer", "[link][hardening]")
{
    if (!seventhslide::test::link_fixture::loopback_has(QStringLiteral("127.0.1.1"))) {
        SKIP("Needs the loopback address 127.0.1.1 (on macOS: sudo ifconfig lo0 alias 127.0.1.1 up)");
    }
    // One address for each share of the places, and one more for after.
    if (!seventhslide::test::link_fixture::loopback_has(QStringLiteral("127.0.1.9"))) {
        SKIP("Needs the loopback address 127.0.1.9 (on macOS: sudo ifconfig lo0 alias 127.0.1.9 up)");
    }
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    const auto port = church.port();

    // Every place taken, each address with its share.
    std::vector<std::unique_ptr<QTcpSocket>> idle;
    const auto from = [&](int address) {
        for (int i = 0; i < seventhslide::link::k_max_unadmitted_per_address; ++i) {
            idle.push_back(std::make_unique<QTcpSocket>());
            REQUIRE(idle.back()->bind(QHostAddress(QStringLiteral("127.0.1.%1").arg(address))));
            idle.back()->connectToHost(QHostAddress::LocalHost, port);
        }
        run_events_for(50);
    };
    const int addresses = seventhslide::link::k_max_unadmitted / seventhslide::link::k_max_unadmitted_per_address;
    static_assert(seventhslide::link::k_max_unadmitted / seventhslide::link::k_max_unadmitted_per_address < 9,
                  "the test binds 127.0.1.1 to 127.0.1.9 (macos.yml adds them)");
    for (int address = 1; address <= addresses; ++address) {
        from(address);
    }
    run_events_for(200);
    REQUIRE(static_cast<int>(church.link.peers().size()) == seventhslide::link::k_max_unadmitted);

    // The one waiting longest goes for a real computer, which gets in.
    RawPeer raw("office");
    REQUIRE(raw.connect_to(port));
    run_events_for(100);
    CHECK(idle.front()->state() == QAbstractSocket::UnconnectedState);
    CHECK(idle.back()->state() == QAbstractSocket::ConnectedState);
    CHECK(static_cast<int>(church.link.peers().size()) == seventhslide::link::k_max_unadmitted);
    raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
    raw.run({&church});
    REQUIRE(raw.first("have") != nullptr);

    // Let in, it is none of those that give way.
    from(addresses + 1);
    run_events_for(200);
    CHECK(raw.connected());
}

TEST_CASE("Connections count as from one address by IPv4 address, or by IPv6 network", "[link][hardening]")
{
    using seventhslide::link::same_source;
    const auto a = [](const char* text) { return QHostAddress(QString::fromLatin1(text)); };
    // One home's /64: every address in it is one.
    CHECK(same_source(a("2a01:4f8:1:2::5"), a("2a01:4f8:1:2:ffff:1:2:3")));
    CHECK_FALSE(same_source(a("2a01:4f8:1:2::5"), a("2a01:4f8:1:3::5")));
    // IPv4 as a listener taking both shows it: each address alone, not all of
    // ::ffff:0:0/96 together.
    CHECK_FALSE(same_source(a("::ffff:10.0.0.1"), a("::ffff:10.0.0.2")));
    CHECK(same_source(a("::ffff:10.0.0.1"), a("10.0.0.1")));
    CHECK_FALSE(same_source(a("10.0.0.1"), a("10.0.0.2")));
    CHECK_FALSE(same_source(a("10.0.0.1"), a("::1")));
}

TEST_CASE("On a port taking both IPv4 and IPv6, IPv4 addresses each have their share", "[link][hardening]")
{
    if (!seventhslide::test::link_fixture::loopback_has(QStringLiteral("127.0.1.1"))) {
        SKIP("Needs the loopback address 127.0.1.1 (on macOS: sudo ifconfig lo0 alias 127.0.1.1 up)");
    }
    if (!seventhslide::test::link_fixture::loopback_has(QStringLiteral("127.0.1.2"))) {
        SKIP("Needs the loopback address 127.0.1.2 (on macOS: sudo ifconfig lo0 alias 127.0.1.2 up)");
    }
    QTcpServer probe;
    if (!probe.listen(QHostAddress::Any)) {
        SKIP("This computer takes no IPv6");
    }
    probe.close();
    Computer church{"church", "Church PC", QHostAddress::Any};
    const auto founded = put_online(church);
    std::vector<std::unique_ptr<QTcpSocket>> idle;
    const auto from = [&](const QString& address, int count) {
        for (int i = 0; i < count; ++i) {
            idle.push_back(std::make_unique<QTcpSocket>());
            REQUIRE(idle.back()->bind(QHostAddress(address)));
            idle.back()->connectToHost(QHostAddress::LocalHost, church.port());
        }
        run_events_for(200);
    };
    from(QStringLiteral("127.0.1.1"), seventhslide::link::k_max_unadmitted_per_address + 1);
    REQUIRE(church.link.peers().size() == static_cast<std::size_t>(seventhslide::link::k_max_unadmitted_per_address));
    REQUIRE(church.link.peers().front()->address().protocol() == QAbstractSocket::IPv6Protocol);  // as ::ffff:
    CHECK(idle.back()->state() == QAbstractSocket::UnconnectedState);
    from(QStringLiteral("127.0.1.2"), 1);
    CHECK(idle.back()->state() == QAbstractSocket::ConnectedState);
    CHECK(church.link.peers().size() == static_cast<std::size_t>(seventhslide::link::k_max_unadmitted_per_address + 1));
}

TEST_CASE("Two computers sync over IPv6", "[link]")
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHostIPv6)) {
        SKIP("This computer has no IPv6 loopback");
    }
    probe.close();
    Computer church{"church", "Church PC", QHostAddress::LocalHostIPv6};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    laptop.share(church.uid);
    laptop.members().join(seventhslide::sync::read_invite(code));
    laptop.link.connect_to(QStringLiteral("::1"), church.port(), church.key(), {{church.uid, laptop.join_entry()}});
    REQUIRE(settle({&church, &laptop}));
    INFO("church closed: " << why(church) << "; laptop closed: " << why(laptop));
    REQUIRE(laptop.link.peers().size() == 1);
    CHECK(laptop.link.peers().front()->address() == QHostAddress(QHostAddress::LocalHostIPv6));
    CHECK(laptop.file("videos/cd34.mp4") == "welcome");
    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 1);
    seventhslide::test::check_same_records(laptop.db, church.db);
}

TEST_CASE("One that never finishes TLS goes before one that has yet to say hello", "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    (void)put_online(church);
    church.link.set_handshake_timeout(300);
    church.link.set_hello_timeout(5000);
    QTcpSocket plain;
    plain.connectToHost(QHostAddress::LocalHost, church.port());
    RawPeer raw("raw");
    REQUIRE(raw.connect_to(church.port()));
    run_events_for(800);
    CHECK(plain.state() == QAbstractSocket::UnconnectedState);
    CHECK(raw.connected());
}

TEST_CASE("Those that stay, saying nothing, hold only their share, and are counted before TLS", "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    (void)put_online(church);
    church.link.set_hello_timeout(600);
    const auto port = church.port();

    // Plain connections that never begin TLS, all from one address.
    std::vector<std::unique_ptr<QTcpSocket>> idle;
    for (int i = 0; i < seventhslide::link::k_max_unadmitted_per_address + 3; ++i) {
        idle.push_back(std::make_unique<QTcpSocket>());
        idle.back()->connectToHost(QHostAddress::LocalHost, port);
    }
    run_events_for(200);
    CHECK(static_cast<int>(church.link.peers().size()) == seventhslide::link::k_max_unadmitted_per_address);
    // And they are not this computer's work.
    CHECK_FALSE(church.link.busy());

    // Gone at the timeout, with room again.
    run_events_for(900);
    CHECK(church.link.peers().empty());
    idle.clear();
    RawPeer raw("office");
    CHECK(raw.connect_to(port));
}

TEST_CASE("A connection silent for long is closed; one that answers stays", "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);

    SECTION("one gone silent, as a laptop that went to sleep")
    {
        church.link.set_keep_alive(100, 800);
        RawPeer raw("raw");
        REQUIRE(raw.connect_to(church.port()));
        raw.hello(church.uid, church.members().invite(Role::Editor, k_expires, founded.admin));
        raw.run({&church}, 300);
        REQUIRE(raw.first("have") != nullptr);
        raw.run({&church}, 1500);  // reading, saying nothing
        CHECK(raw.first("ping") != nullptr);
        CHECK_FALSE(raw.connected());
        REQUIRE_FALSE(church.closed.empty());
        CHECK_THAT(church.closed.back().toStdString(), ContainsSubstring("Nothing heard"));
    }
    SECTION("two that answer each other, with nothing to sync")
    {
        // Asked often, and given time enough to answer on a busy machine.
        church.link.set_keep_alive(150, 2000);
        laptop.link.set_keep_alive(150, 2000);
        join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                     church.key(), church.port());
        REQUIRE(settle({&church, &laptop}));
        run_events_for(4000);  // longer than either waits, saying nothing
        INFO("church: " << why(church) << "; laptop: " << why(laptop));
        CHECK(laptop.link.peers().size() == 1);
        CHECK(church.link.peers().size() == 1);
        CHECK(church.closed.empty());
    }
}

TEST_CASE("A paused computer sends and fetches nothing until it goes on", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);

    // Paused at the church: the laptop is let in, and gets nothing.
    church.link.set_paused(true);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    REQUIRE(laptop.link.peers().size() == 1);
    CHECK(laptop.link.peers().front()->paused_there());
    CHECK(laptop.store.sequences(Numbered::Changes, "church").empty());
    CHECK_FALSE(laptop.has_file("images/ab12.png"));
    // Not all here: the church has what it has not sent.
    CHECK_FALSE(laptop.link.has_all_of(church.uid));

    // Going on, it all comes.
    church.link.set_paused(false);
    REQUIRE(settle({&church, &laptop}));
    CHECK_FALSE(laptop.link.peers().front()->paused_there());
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.file("videos/cd34.mp4") == "welcome");
    CHECK(laptop.link.has_all_of(church.uid));
    laptop.engine.join(church.uid);
    REQUIRE(laptop.sync().files_applied == 1);
    REQUIRE(settle({&church, &laptop}));

    // Paused at the laptop: with nothing more to come, all is here still,
    // and the church's next change waits there.
    laptop.link.set_paused(true);
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.link.has_all_of(church.uid));
    church.put_file("images/new1.png", "a new picture");
    church.db.exec("INSERT INTO image_files (filename, display_name) VALUES ('new1.png', 'New')");
    REQUIRE(church.sync().changes_sent == 1);
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(church.link.peers().size() == 1);
    CHECK(church.link.peers().front()->paused_there());
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK_FALSE(laptop.has_file("images/new1.png"));
    laptop.link.set_paused(false);
    REQUIRE(settle({&church, &laptop}));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1, 2});
    CHECK(laptop.file("images/new1.png") == "a new picture");
    CHECK(laptop.refused.empty());
    CHECK(church.refused.empty());
}

TEST_CASE("A computer paused mid-video leaves the rest to another that has it", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    // Large enough that a pause as it starts is heard while most of it is
    // still to come.
    const auto video = big_video(48 * 1024 * 1024);
    const auto founded = put_online_with(church, video);
    pair_server(church, server, founded);
    REQUIRE(server.file("videos/cd34.mp4") == video);

    // The laptop is connected to both; whichever sends it the video pauses
    // as its first piece comes, and the rest comes from the other.
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, server.key(), server.port());
    laptop.link.connect_to("127.0.0.1", church.port(), church.key(), {{church.uid, laptop.join_entry()}});
    REQUIRE(laptop.link.peers().size() == 2);
    Computer* paused = nullptr;
    const seventhslide::link::Peer* first = nullptr;
    bool from_the_other = false;
    for (auto* peer : laptop.link.peers()) {
        QObject::connect(peer, &seventhslide::link::Peer::media_progress,
                         [&, peer](const std::string&, const std::string& path, std::int64_t) {
                             if (path != "videos/cd34.mp4") {
                                 return;
                             }
                             if (paused == nullptr) {
                                 first = peer;
                                 paused = *peer->key() == church.key() ? &church : &server;
                                 paused->link.set_paused(true);
                             } else if (peer != first) {
                                 from_the_other = true;
                             }
                         });
    }
    REQUIRE(settle({&church, &server, &laptop}));
    INFO("church: " << why(church) << "; server: " << why(server) << "; laptop: " << why(laptop));
    REQUIRE(paused != nullptr);
    CHECK(from_the_other);
    CHECK(laptop.file("videos/cd34.mp4") == video);
    CHECK_FALSE(std::filesystem::exists(laptop.media / "videos/cd34.mp4.part"));
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.refused.empty());
}

TEST_CASE("A computer that had not got a media file says so once it has", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer office{"office", "Office"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join_through(office, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &office}));
    REQUIRE(office.file("audio/ef56.mp3") == "prelude");

    // The office goes home; the church loses its copy.
    REQUIRE(church.link.peers().size() == 1);
    church.link.peers().front()->close("the office went home");
    REQUIRE(settle({&church, &office}));
    std::filesystem::remove(church.media / "audio/ef56.mp3");
    church.sync();
    REQUIRE_FALSE(missing_media(church.engine).empty());

    // The laptop joins through the church, which says it has not got it.
    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    laptop.engine.join(church.uid);
    laptop.sync();
    laptop.link.fetch_more(church.uid);
    REQUIRE(settle({&church, &laptop}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    REQUIRE(laptop.link.nobody_has(church.uid, "audio/ef56.mp3"));

    // The office is back: the church gets it there, and the laptop, hearing
    // the church has it now, asks again.
    church.link.connect_to("127.0.0.1", office.port(), office.key());
    REQUIRE(settle({&church, &office, &laptop}));
    CHECK(church.file("audio/ef56.mp3") == "prelude");
    CHECK(laptop.file("audio/ef56.mp3") == "prelude");
    CHECK_FALSE(laptop.link.nobody_has(church.uid, "audio/ef56.mp3"));
}

TEST_CASE("A file asked only of a computer is not said to that one to be coming", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer office{"office", "Office"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join_through(office, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &office}));
    REQUIRE(office.file("audio/ef56.mp3") == "prelude");

    // The office goes home with the only other copy; the church loses its own.
    church.link.peers().front()->close("the office went home");
    REQUIRE(settle({&church, &office}));
    std::filesystem::remove(church.media / "audio/ef56.mp3");
    church.sync();
    REQUIRE_FALSE(missing_media(church.engine).empty());

    // The laptop joins, and has not got it either. The church asks it again
    // each time the laptop says what it has; told what is coming every few
    // milliseconds, it would tell the laptop of the file while asking it.
    church.link.set_tell_coming_every(5);
    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    laptop.engine.join(church.uid);
    laptop.sync();
    laptop.link.fetch_more(church.uid);

    // Nothing else is getting it for the church: coming from no one, the
    // laptop never hears it is coming, and says nobody has it.
    bool heard_coming = false;
    QElapsedTimer watched;
    watched.start();
    while (watched.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        for (const auto* peer : laptop.link.peers()) {
            heard_coming = heard_coming || peer->says_coming(church.uid, "audio/ef56.mp3");
        }
    }
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    CHECK_FALSE(heard_coming);
    CHECK(laptop.link.nobody_has(church.uid, "audio/ef56.mp3"));
    CHECK_FALSE(laptop.link.coming_through(church.uid, "audio/ef56.mp3").has_value());
}

TEST_CASE("A part here that is not of the file is fetched again from the start, and blames no one",
          "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto video = big_video(2 * k_media_piece_bytes + 1024 * 1024);
    const auto founded = put_online_with(church, video);
    laptop.put_file("videos/cd34.mp4.part", std::string(k_media_piece_bytes, 'x'));
    SECTION("no hashes kept for it")
    {
    }
    SECTION("the hashes kept are another file's")
    {
        const auto other = seventhslide::sync::hash_media_file(laptop.media / "videos/cd34.mp4.part");
        REQUIRE(other);
        REQUIRE(seventhslide::sync::PieceHashes(laptop.pieces)
                    .save("videos/cd34.mp4", static_cast<std::int64_t>(video.size()), *other + *other + *other));
    }

    join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                 church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    CHECK(laptop.file("videos/cd34.mp4") == video);
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.refused.empty());
}

TEST_CASE("A damaged piece is blamed on the computer that sent it, and the rest comes from another",
          "[link][hardening]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    const auto video = big_video(3 * k_media_piece_bytes);
    const auto founded = put_online_with(church, video);
    pair_server(church, server, founded);
    REQUIRE(server.file("videos/cd34.mp4") == video);
    // The church's copy goes bad in its second piece, after its hashes were
    // worked out.
    auto damaged = video;
    damaged[static_cast<std::size_t>(k_media_piece_bytes) + 10] ^= 1;
    church.put_file("videos/cd34.mp4", damaged);
    // Bit rot leaves the time the file was last written as it was.
    std::filesystem::last_write_time(church.media / "videos/cd34.mp4",
                                     std::filesystem::last_write_time(church.pieces / "videos/cd34.mp4"));
    church.link.peers().front()->close("the server went home");
    REQUIRE(settle({&church, &server}));

    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}));
    INFO("church: " << why(church) << "; laptop: " << why(laptop));
    REQUIRE(laptop.refused.size() == 1);
    CHECK_THAT(laptop.refused.front().toStdString(), ContainsSubstring("arrived damaged"));
    // The first piece is kept, checked; the church is not asked again, and
    // sends nothing more of it.
    CHECK(std::filesystem::file_size(laptop.media / "videos/cd34.mp4.part")
          == static_cast<std::uintmax_t>(k_media_piece_bytes));
    CHECK_FALSE(laptop.has_file("videos/cd34.mp4"));
    CHECK_FALSE(church.link.busy());
    // The change file is kept without it, and the video is still missing.
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    laptop.engine.join(church.uid);
    laptop.sync();
    REQUIRE(missing_media(laptop.engine).size() == 1);

    // The server has it: the rest comes from there, from the piece that was bad.
    std::vector<std::int64_t> progress;
    laptop.link.connect_to("127.0.0.1", server.port(), server.key(), {{church.uid, laptop.join_entry()}});
    for (auto* peer : laptop.link.peers()) {
        if (peer->expected() && *peer->expected() == server.key()) {
            QObject::connect(peer, &seventhslide::link::Peer::media_progress,
                             [&](const std::string&, const std::string& path, std::int64_t bytes) {
                                 if (path == "videos/cd34.mp4") {
                                     progress.push_back(bytes);
                                 }
                             });
        }
    }
    REQUIRE(settle({&church, &server, &laptop}));
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.front() == 2 * k_media_piece_bytes);
    CHECK(laptop.file("videos/cd34.mp4") == video);
    CHECK(laptop.refused.size() == 1);
}


namespace {

/// A connection's media coming in, with no connection: what it is told.
struct Arrivals {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir root;
    std::vector<seventhslide::link::SyncedLibrary> libraries;
    seventhslide::link::PeerShared shared;
    std::vector<std::int64_t> progress;
    std::vector<std::string> refused;
    std::vector<std::string> failed;
    std::vector<std::string> again;
    int done = 0;
    int room = 0;
    int part_free = 0;

    Arrivals()
    {
        shared.checks.setMaxThreadCount(1);
        shared.pieces.setMaxThreadCount(1);
        shared.part_free = [this](const std::string&) { ++part_free; };
        libraries.push_back({"lib", nullptr, root.path / "media", root.path / "pieces", {}, {}, {}});
    }
    const seventhslide::link::SyncedLibrary& lib() const { return libraries.front(); }
    std::unique_ptr<seventhslide::link::MediaIn> connection()
    {
        const auto note = [](std::vector<std::string>& into) {
            return [&into](const std::string&, const std::string&, const QString& why) {
                into.push_back(why.toStdString());
            };
        };
        return std::make_unique<seventhslide::link::MediaIn>(
            shared, libraries,
            seventhslide::link::MediaIn::Events{
                [this](const std::string&, const std::string&, std::int64_t bytes) { progress.push_back(bytes); },
                note(refused), note(failed), note(again),
                [this](const std::string&, const std::string&) { ++done; }, [this] { ++room; }});
    }
    static seventhslide::db::MediaDigest digest_of(const std::string& path, const std::string& bytes,
                                                   const std::filesystem::path& scratch)
    {
        std::filesystem::create_directories(scratch.parent_path());
        std::ofstream(scratch, std::ios::binary) << bytes;
        const auto hashes = seventhslide::sync::hash_media_file(scratch);
        std::filesystem::remove(scratch);
        return {path, static_cast<std::int64_t>(bytes.size()), seventhslide::sync::media_root(*hashes)};
    }
    static QByteArray hashes_of(const std::string& bytes, const std::filesystem::path& scratch)
    {
        std::ofstream(scratch, std::ios::binary) << bytes;
        const auto hashes = seventhslide::sync::hash_media_file(scratch);
        std::filesystem::remove(scratch);
        return QByteArray::fromStdString(*hashes);
    }
    /// ``bytes`` from ``from``, frames as a sender sends them; ``last``: and
    /// then it says it has no more.
    static void send(seventhslide::link::MediaIn& in, const std::string& path, const std::string& bytes,
                     std::int64_t from = 0, bool last = true)
    {
        const auto size = static_cast<std::int64_t>(bytes.size());
        do {
            const auto n = std::min(seventhslide::link::k_media_piece, size - from);
            in.take_bytes("lib", path, from, QByteArray(bytes.data() + from, static_cast<qsizetype>(n)),
                          last && from + n >= size);
            from += n;
        } while (from < size);
    }
    template <typename Until>
    static bool wait(Until until)
    {
        QElapsedTimer timer;
        timer.start();
        while (!until() && timer.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return until();
    }
};

}  // namespace

TEST_CASE("Pieces that come are written off the thread, and no more is read while they wait",
          "[link][hardening]")
{
    Arrivals a;
    const auto video = big_video(3 * k_media_piece_bytes);
    const auto digest = Arrivals::digest_of("videos/cd34.mp4", video, a.root.path / "scratch");
    auto in = a.connection();
    REQUIRE(in->begin(a.lib(), digest) == 0);
    in->take_hashes("lib", digest.path, Arrivals::hashes_of(video, a.root.path / "scratch"));
    {
        HeldThread held(a.shared.pieces);
        Arrivals::send(*in, digest.path, video.substr(0, 2 * k_media_piece_bytes), 0, false);
        CHECK_FALSE(in->has_room());
        CHECK(in->busy());
        CHECK_FALSE(in->free_to_begin("lib", digest.path));
        CHECK(a.progress.empty());
        held.release();
        REQUIRE(Arrivals::wait([&] { return in->has_room() && a.progress.size() == 2; }));
    }
    CHECK(a.room >= 1);
    CHECK(std::filesystem::file_size(a.lib().media / "videos/cd34.mp4.part")
          == static_cast<std::uintmax_t>(2 * k_media_piece_bytes));
    Arrivals::send(*in, digest.path, video, 2 * k_media_piece_bytes);
    REQUIRE(Arrivals::wait([&] { return a.done == 1; }));
    CHECK(seventhslide::util::read_file(a.lib().media / digest.path) == video);
    CHECK(a.refused.empty());
    CHECK(a.failed.empty());
    CHECK_FALSE(in->busy());
    CHECK(in->free_to_begin("lib", digest.path));
}

TEST_CASE("A part being put in place is begun on by no other connection, even once its own has gone",
          "[link][hardening]")
{
    Arrivals a;
    const auto video = big_video(k_media_piece_bytes + 1000);
    const auto digest = Arrivals::digest_of("videos/cd34.mp4", video, a.root.path / "scratch");
    auto first = a.connection();
    auto second = a.connection();
    REQUIRE(first->begin(a.lib(), digest) == 0);
    first->take_hashes("lib", digest.path, Arrivals::hashes_of(video, a.root.path / "scratch"));
    {
        HeldThread held(a.shared.checks);
        Arrivals::send(*first, digest.path, video);
        REQUIRE(Arrivals::wait([&] { return a.progress.size() == 2; }));
        // Whole, waiting to go in place; its connection goes.
        first.reset();
        CHECK_FALSE(second->free_to_begin("lib", digest.path));
        held.release();
        REQUIRE(Arrivals::wait([&] { return second->free_to_begin("lib", digest.path); }));
    }
    CHECK(a.part_free == 1);
    CHECK(seventhslide::util::read_file(a.lib().media / digest.path) == video);
    CHECK_FALSE(std::filesystem::exists(a.lib().media / "videos/cd34.mp4.part"));
}

TEST_CASE("A sender that stops short, or turns to another file within a piece, is refused",
          "[link][hardening]")
{
    Arrivals a;
    const auto video = big_video(k_media_piece_bytes + 1000);
    const auto digest = Arrivals::digest_of("videos/cd34.mp4", video, a.root.path / "scratch");
    auto in = a.connection();
    SECTION("short of the file")
    {
        REQUIRE(in->begin(a.lib(), digest) == 0);
        in->take_hashes("lib", digest.path, Arrivals::hashes_of(video, a.root.path / "scratch"));
        Arrivals::send(*in, digest.path, video.substr(0, k_media_piece_bytes - 5));
        REQUIRE(a.refused.size() == 1);
        CHECK_THAT(a.refused.front(), ContainsSubstring("shorter than declared"));
        CHECK_FALSE(in->expects_bytes("lib", digest.path, k_media_piece_bytes - 5));
    }
    SECTION("another file within a piece")
    {
        const auto other = big_video(1000) + "x";
        const auto other_digest = Arrivals::digest_of("videos/ef56.mp4", other, a.root.path / "scratch");
        REQUIRE(in->begin(a.lib(), digest) == 0);
        REQUIRE(in->begin(a.lib(), other_digest) == 0);
        in->take_hashes("lib", digest.path, Arrivals::hashes_of(video, a.root.path / "scratch"));
        in->take_hashes("lib", other_digest.path, Arrivals::hashes_of(other, a.root.path / "scratch"));
        in->take_bytes("lib", digest.path, 0, QByteArray(video.data(), 1000), false);
        Arrivals::send(*in, other_digest.path, other);
        REQUIRE(a.refused.size() == 1);
        CHECK_THAT(a.refused.front(), ContainsSubstring("out of turn"));
        // The first goes on.
        Arrivals::send(*in, digest.path, video, 1000);
        REQUIRE(Arrivals::wait([&] { return a.done == 1; }));
        CHECK(seventhslide::util::read_file(a.lib().media / digest.path) == video);
    }
}

TEST_CASE("A part gone on from is read through before it goes in place, and cut back to what is good",
          "[link][hardening]")
{
    Arrivals a;
    const auto video = big_video(3 * k_media_piece_bytes);
    const auto digest = Arrivals::digest_of("videos/cd34.mp4", video, a.root.path / "scratch");
    const auto hashes = Arrivals::hashes_of(video, a.root.path / "scratch");
    // Two pieces on the disk from before the power went, the second torn.
    auto torn = video.substr(0, 2 * k_media_piece_bytes);
    std::fill_n(torn.begin() + k_media_piece_bytes + 4096, 4096, '\0');
    const auto part = a.lib().media / "videos/cd34.mp4.part";
    std::filesystem::create_directories(part.parent_path());
    std::ofstream(part, std::ios::binary) << torn;
    REQUIRE(seventhslide::sync::PieceHashes(a.lib().pieces).save(digest.path, digest.size, hashes.toStdString()));

    auto in = a.connection();
    REQUIRE(in->begin(a.lib(), digest) == 2 * k_media_piece_bytes);
    in->take_hashes("lib", digest.path, hashes);
    Arrivals::send(*in, digest.path, video, 2 * k_media_piece_bytes);
    REQUIRE(Arrivals::wait([&] { return a.again.size() == 1; }));
    CHECK(a.done == 0);
    CHECK(a.refused.empty());  // no one's fault
    CHECK_FALSE(std::filesystem::exists(a.lib().media / digest.path));
    CHECK(std::filesystem::file_size(part) == static_cast<std::uintmax_t>(k_media_piece_bytes));

    // Fetched on from the piece that was bad.
    REQUIRE(Arrivals::wait([&] { return in->free_to_begin("lib", digest.path); }));
    REQUIRE(in->begin(a.lib(), digest) == k_media_piece_bytes);
    in->take_hashes("lib", digest.path, hashes);
    Arrivals::send(*in, digest.path, video, k_media_piece_bytes);
    REQUIRE(Arrivals::wait([&] { return a.done == 1; }));
    CHECK(seventhslide::util::read_file(a.lib().media / digest.path) == video);
}

TEST_CASE("A part written in this run is gone on from without being read through; one from before is",
          "[link][hardening]")
{
    Arrivals a;
    const auto video = big_video(3 * k_media_piece_bytes);
    const auto digest = Arrivals::digest_of("videos/cd34.mp4", video, a.root.path / "scratch");
    const auto hashes = Arrivals::hashes_of(video, a.root.path / "scratch");
    const auto part = a.lib().media / "videos/cd34.mp4.part";
    auto first = a.connection();
    REQUIRE(first->begin(a.lib(), digest) == 0);
    first->take_hashes("lib", digest.path, hashes);
    for (std::int64_t at = 0; at < 2 * k_media_piece_bytes; at += seventhslide::link::k_media_piece) {
        first->take_bytes("lib", digest.path, at,
                          QByteArray(video.data() + at, static_cast<qsizetype>(seventhslide::link::k_media_piece)), false);
    }
    REQUIRE(Arrivals::wait([&] {
        std::error_code ec;
        return std::filesystem::file_size(part, ec) == static_cast<std::uintmax_t>(2 * k_media_piece_bytes)
               && first->free_to_begin("lib", digest.path);
    }));
    first.reset();  // the connection is replaced

    // Marked here, so a read through would be seen: it would cut this back.
    const auto marked = [&] {
        std::fstream file(part, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(100);
        file.put(static_cast<char>(video[100] ^ 1));
    };
    SECTION("gone on from in the same run")
    {
        marked();
        auto second = a.connection();
        REQUIRE(second->begin(a.lib(), digest) == 2 * k_media_piece_bytes);
        second->take_hashes("lib", digest.path, hashes);
        Arrivals::send(*second, digest.path, video, 2 * k_media_piece_bytes);
        REQUIRE(Arrivals::wait([&] { return a.done == 1; }));
        CHECK(a.again.empty());
        CHECK(a.shared.parts_written.empty());
    }
    SECTION("a piece still being written as the connection is replaced counts too")
    {
        std::unique_ptr<seventhslide::link::MediaIn> second;
        {
            HeldThread held(a.shared.pieces);
            second = a.connection();
            REQUIRE(second->begin(a.lib(), digest) == 2 * k_media_piece_bytes);
            second->take_hashes("lib", digest.path, hashes);
            // The rest: held at the piece thread.
            Arrivals::send(*second, digest.path, video, 2 * k_media_piece_bytes);
            second.reset();  // replaced, with a piece on its way to the disk
        }
        REQUIRE(Arrivals::wait([&] { return a.shared.parts_at_work.empty(); }));
        marked();
        auto third = a.connection();
        const auto from = third->begin(a.lib(), digest);
        third->take_hashes("lib", digest.path, hashes);
        Arrivals::send(*third, digest.path, video, from);
        REQUIRE(Arrivals::wait([&] { return a.done == 1; }));
        CHECK(a.again.empty());
    }
    SECTION("after a restart, when nothing is known of it")
    {
        marked();
        a.shared.parts_written.clear();
        auto second = a.connection();
        REQUIRE(second->begin(a.lib(), digest) == 2 * k_media_piece_bytes);
        second->take_hashes("lib", digest.path, hashes);
        Arrivals::send(*second, digest.path, video, 2 * k_media_piece_bytes);
        REQUIRE(Arrivals::wait([&] { return a.again.size() == 1; }));
        CHECK(a.done == 0);
    }
}

TEST_CASE("A piece that cannot be written is this computer's trouble, not the sender's", "[link][hardening]")
{
    Arrivals a;
    const std::string bytes = "lilies";
    const auto digest = Arrivals::digest_of("images/ab12.png", bytes, a.root.path / "scratch");
    // Something stands where the part would go.
    std::filesystem::create_directories(a.lib().media / "images/ab12.png.part");
    auto in = a.connection();
    REQUIRE(in->begin(a.lib(), digest) == 0);
    in->take_hashes("lib", digest.path, Arrivals::hashes_of(bytes, a.root.path / "scratch"));
    Arrivals::send(*in, digest.path, bytes);
    REQUIRE(Arrivals::wait([&] { return !a.failed.empty(); }));
    CHECK_THAT(a.failed.front(), ContainsSubstring("could not be written"));
    CHECK(a.refused.empty());
    CHECK(a.done == 0);
}

TEST_CASE("Change files are checked, stored and read off the connection's thread", "[link]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);

    // The laptop's file thread busy: its member list entry and admin key are
    // stored as they come, and the connection goes on, while the change file
    // waits there to be checked.
    {
        HeldThread held(laptop.link.shared().files);
        join_through(laptop, church.uid, church.members().invite(Role::Editor, k_expires, founded.admin),
                     church.key(), church.port());
        // The change file waits on the held thread once the list and key are in.
        CHECK(wait_until([&] {
            return laptop.store.sequences(Numbered::Members, "church") == std::vector<std::int64_t>{1, 2}
                   && laptop.store.admin_keys().size() == 1 && laptop.link.busy();
        }));
        INFO("church: " << why(church) << "; laptop: " << why(laptop));
        REQUIRE(laptop.link.peers().size() == 1);
        CHECK(laptop.store.sequences(Numbered::Changes, "church").empty());
        held.release();
        REQUIRE(settle({&church, &laptop}));
    }
    CHECK(laptop.refused.empty());
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
    CHECK(laptop.file("videos/cd34.mp4") == "welcome");

    // The church's busy: a change asked for is read from its store there,
    // and goes once it is.
    church.db.exec("UPDATE services SET name = 'Divine Service'");
    {
        HeldThread held(church.link.shared().files);
        REQUIRE(church.sync().changes_sent == 1);
        // Asked for, it waits on the held thread.
        CHECK(wait_until([&] { return church.link.busy(); }));
        CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1});
        held.release();
        REQUIRE(settle({&church, &laptop}));
    }
    CHECK(laptop.store.sequences(Numbered::Changes, "church") == std::vector<std::int64_t>{1, 2});
    laptop.engine.join(church.uid);
    laptop.sync();
    seventhslide::test::check_same_records(laptop.db, church.db);
}

// Hidden: how much a connection flood holds up the thread the link runs on, which
// on a desktop is the window's. Run by hand on the computer to judge:
//   seventhslide_tests_sync "[.scanners]"
TEST_CASE("What connections from strangers cost the thread the link runs on", "[.scanners]")
{
    Computer church{"church", "Church PC"};
    const auto founded = put_online(church);
    (void)founded;
    const auto port = church.port();

    // Clients on threads of their own, as computers elsewhere would be.
    enum class Kind { Nothing, Refused, Hanging, Handshake };
    const auto measure = [&](Kind kind, const char* what, int clients) {
        std::atomic<bool> stop{false};
        std::atomic<std::int64_t> made{0};
        std::vector<std::thread> threads;
        if (kind != Kind::Nothing) {
            for (int i = 0; i < clients; ++i) {
                threads.emplace_back([&, kind] {
                    const Identity me{"scanner", "Scanner", SigningKey::generate()};
                    const auto config = seventhslide::link::tls_configuration(me);
                    std::vector<std::unique_ptr<QTcpSocket>> hanging;
                    int from = 0;
                    while (!stop) {
                        if (kind == Kind::Hanging) {
                            // Saying nothing, from address after address.
                            hanging.push_back(std::make_unique<QTcpSocket>());
                            auto& socket = *hanging.back();
                            (void)socket.bind(QHostAddress(0x7f010001U + static_cast<quint32>(from++ % 250)));
                            socket.connectToHost(QHostAddress::LocalHost, port);
                            if (socket.waitForConnected(1000)) {
                                ++made;
                            }
                            if (hanging.size() > 64) {
                                hanging.erase(hanging.begin());
                            }
                        } else if (kind == Kind::Refused) {
                            QTcpSocket socket;
                            socket.connectToHost(QHostAddress::LocalHost, port);
                            if (socket.waitForConnected(1000)) {
                                ++made;
                            }
                            socket.abort();
                        } else {
                            QSslSocket socket;
                            socket.setSslConfiguration(config);
                            QObject::connect(&socket, &QSslSocket::sslErrors, [](const QList<QSslError>&) {});
                            socket.connectToHostEncrypted(QStringLiteral("127.0.0.1"), port);
                            if (socket.waitForEncrypted(2000)) {
                                ++made;
                            }
                            socket.abort();
                        }
                    }
                });
            }
        }
        // A timer every millisecond: how late it comes is how long this thread
        // was held up.
        std::vector<double> gaps;
        QElapsedTimer since;
        since.start();
        QElapsedTimer total;
        total.start();
        QTimer tick;
        tick.setTimerType(Qt::PreciseTimer);
        tick.setInterval(1);
        QObject::connect(&tick, &QTimer::timeout, [&] {
            gaps.push_back(static_cast<double>(since.nsecsElapsed()) / 1e6);
            since.restart();
        });
        tick.start();
        while (total.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        tick.stop();
        stop = true;
        while (std::ranges::any_of(threads, [](const std::thread& t) { return t.joinable(); })) {
            for (auto& t : threads) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                if (t.joinable()) {
                    t.join();
                }
            }
        }
        std::ranges::sort(gaps);
        const auto at = [&](double share) { return gaps[static_cast<std::size_t>(share * static_cast<double>(gaps.size() - 1))]; };
        const auto over = std::ranges::count_if(gaps, [](double g) { return g > 16.7; });
        std::cout << what << ": " << made.load() / 5 << " connections/s; thread held up p50 " << at(0.5) << " ms, p99 "
                  << at(0.99) << " ms, longest " << gaps.back() << " ms, " << over << " over a 60 Hz frame in 5 s\n"
                  << std::flush;
    };
    // A real computer's connection, right after: whether the door is still open.
    const auto door = [&](const char* after) {
        for (const int wait_ms : {0, 1000, 6000, 15000}) {
            QElapsedTimer waited;
            waited.start();
            while (waited.elapsed() < wait_ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            RawPeer raw("raw");
            QElapsedTimer took;
            took.start();
            const bool through = raw.connect_to(port);
            std::cout << "  after " << after << ", " << wait_ms << " ms on: "
                      << (through ? "a computer gets through in " : "NO connection after ") << took.elapsed() << " ms\n"
                      << std::flush;
            if (through) {
                return;
            }
        }
    };
    measure(Kind::Nothing, "quiet", 0);
    door("quiet");
    measure(Kind::Refused, "port scanners, one", 1);
    door("port scanners, one");
    measure(Kind::Refused, "port scanners, four", 4);
    door("port scanners, four");
    measure(Kind::Hanging, "hanging back from many addresses, one", 1);
    door("hanging back, one");
    measure(Kind::Hanging, "hanging back from many addresses, four", 4);
    door("hanging back, four");
    measure(Kind::Handshake, "TLS strangers, one", 1);
    door("TLS strangers, one");
    measure(Kind::Handshake, "TLS strangers, four", 4);
    door("TLS strangers, four");
    measure(Kind::Nothing, "quiet again", 0);
}

// Better routes (routes.hpp): media shared between connections, held to the
// upload speed cap, and moved to a better route as it comes.

namespace {

using seventhslide::link::Peer;
using seventhslide::link::k_large_at_once;
using seventhslide::link::k_media_at_once;

/// ``put_online``, with ``files`` (by media path, with their bytes) in the
/// library besides.
MemberLog::Founded put_online_with_files(Computer& church, const std::map<std::string, std::string>& files)
{
    seventhslide::test::fill_library(church.db);
    church.put_file("images/ab12.png", "lilies");
    church.put_file("videos/cd34.mp4", "welcome");
    church.put_file("audio/ef56.mp3", "prelude");
    for (const auto& [path, bytes] : files) {
        church.put_file(path, bytes);
        const auto slash = path.find('/');
        const std::string table = path.substr(0, slash) == "videos" ? "video_files" : "image_files";
        const auto name = path.substr(slash + 1);
        church.db.exec("INSERT INTO " + table + " (filename, display_name) VALUES ('" + name + "', '" + name + "')");
    }
    church.share(seventhslide::db::library_uid(church.db));
    auto founded = church.members().found(k_passphrase, k_quick);
    church.engine.share_everything();
    REQUIRE(church.sync().changes_sent > 0);
    return founded;
}

/// ``count`` pictures, each its own.
std::map<std::string, std::string> pictures(int count)
{
    std::map<std::string, std::string> out;
    for (int i = 0; i < count; ++i) {
        out["images/p" + std::to_string(i) + ".png"] = "picture " + std::to_string(i);
    }
    return out;
}

/// ``laptop`` joins ``church``'s library through it and takes it in, then loses
/// ``lost`` of its files: they are missing here, and asked for over the next
/// connection.
void join_and_lose(Computer& church, Computer& laptop, const MemberLog::Founded& founded,
                   const std::map<std::string, std::string>& lost)
{
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &laptop}, 60000));
    laptop.engine.join(church.uid);
    REQUIRE(laptop.sync().files_applied > 0);
    for (const auto& [path, bytes] : lost) {
        REQUIRE(laptop.file(path) == bytes);
        std::filesystem::remove(laptop.media / path);
    }
    laptop.sync();
    REQUIRE(missing_media(laptop.engine).size() == lost.size());
    for (auto* peer : laptop.link.peers()) {
        peer->close("the laptop slept");
    }
    REQUIRE(settle({&church, &laptop}));
}

/// The connection of ``c`` to the computer with ``key``.
Peer* connection_to(Computer& c, const seventhslide::crypto::PublicKey& key)
{
    for (auto* peer : c.link.peers()) {
        if (!peer->is_closed() && peer->key() == key) {
            return peer;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("A connection asks for a few media files at a time, and two large ones at most", "[link][routes]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    // Large ones first, as the library lists what is missing by path.
    auto files = pictures(20);
    for (int i = 0; i < 3; ++i) {
        files["images/a" + std::to_string(i) + ".png"] =
            big_video(static_cast<std::size_t>(seventhslide::link::k_large_media) + i);
    }
    const auto founded = put_online_with_files(church, files);
    join_and_lose(church, laptop, founded, files);

    {
        // Nothing comes whole meanwhile: what is asked for stays asked for.
        HeldThread held(laptop.link.shared().pieces);
        laptop.link.connect_to("127.0.0.1", church.port(), church.key());
        REQUIRE(wait_until([&] { return laptop.link.media_coming(church.uid).size() >= k_media_at_once; }));
        run_events_for(300);
        const auto coming = laptop.link.media_coming(church.uid);
        CHECK(coming.size() == k_media_at_once);
        CHECK(std::ranges::count_if(coming, [](const std::string& p) { return p.starts_with("images/a"); })
              == static_cast<std::ptrdiff_t>(k_large_at_once));
        // The rest is still to come from it.
        CHECK_FALSE(laptop.link.has_all_of(church.uid));
    }
    REQUIRE(settle({&church, &laptop}, 60000));
    INFO("laptop: " << why(laptop));
    for (const auto& [path, bytes] : files) {
        CHECK(laptop.file(path) == bytes);
    }
    CHECK(laptop.link.has_all_of(church.uid));
    CHECK(laptop.refused.empty());
}

TEST_CASE("A library is not all here while media missing here may still come from a connection", "[link][routes]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto files = pictures(5);
    const auto founded = put_online_with_files(church, files);
    join_and_lose(church, laptop, founded, files);

    // Every part is being worked on, as by a connection gone: none can be asked
    // for yet, and nothing is on its way.
    for (const auto& [path, bytes] : files) {
        laptop.link.shared().parts_at_work.insert({church.uid, path});
    }
    laptop.link.connect_to("127.0.0.1", church.port(), church.key());
    REQUIRE(wait_until([&] {
        auto* peer = connection_to(laptop, church.key());
        return peer != nullptr && peer->told(church.uid);
    }));
    run_events_for(300);
    CHECK(laptop.link.media_coming(church.uid).empty());
    CHECK_FALSE(laptop.link.has_all_of(church.uid));
    CHECK_FALSE(laptop.link.nobody_has(church.uid, "images/p0.png"));

    laptop.link.shared().parts_at_work.clear();
    laptop.link.shared().part_free(church.uid);
    REQUIRE(settle({&church, &laptop}));
    for (const auto& [path, bytes] : files) {
        CHECK(laptop.file(path) == bytes);
    }
    CHECK(laptop.link.has_all_of(church.uid));
}

TEST_CASE("Media to a computer not on this network goes no faster than the upload speed cap", "[link][routes]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    Computer stage{"stage", "Stage display"};
    const auto video = big_video(3'000'000);
    const auto founded = put_online_with(church, video);
    church.link.set_upload_cap(1'000'000);  // 8 Mbit/s

    QElapsedTimer timer;
    timer.start();
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    REQUIRE(wait_until([&] { return laptop.has_file("videos/cd34.mp4"); }, 30000));
    // Three megabytes at one a second, a moment's worth aside.
    CHECK(timer.elapsed() >= 2500);
    CHECK(laptop.file("videos/cd34.mp4") == video);

    // A computer on this network is not held to it: it uses no uplink. Held to
    // it, the video would take half a minute now.
    church.link.set_upload_cap(100'000);
    church.link.shared().close_by = [&](const Peer& peer) { return peer.key() == stage.key(); };
    timer.restart();
    const auto viewer = church.members().invite(Role::Viewer, k_expires, founded.admin);
    join_through(stage, church.uid, viewer, church.key(), church.port());
    REQUIRE(wait_until([&] { return stage.has_file("videos/cd34.mp4"); }, 60000));
    CHECK(timer.elapsed() < 10000);
    CHECK(stage.file("videos/cd34.mp4") == video);
    REQUIRE(settle({&church, &laptop, &stage}));
    CHECK(laptop.refused.empty());
    CHECK(stage.refused.empty());
}

TEST_CASE("A file is left to a connection to a computer on this network", "[link][routes]")
{
    // As many as the church's connection has room for, and more: the rest the
    // server sends meanwhile, rather than wait.
    const auto count = GENERATE(10, 24);
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    const auto files = pictures(count);
    const auto founded = put_online_with_files(church, files);
    pair_server(church, server, founded);
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    // The server's connection first: it is the first asked.
    join_through(laptop, church.uid, code, server.key(), server.port());
    REQUIRE(settle({&church, &server, &laptop}));
    laptop.engine.join(church.uid);
    REQUIRE(laptop.sync().files_applied > 0);
    laptop.link.connect_to("127.0.0.1", church.port(), church.key(), {{church.uid, laptop.join_entry()}});
    REQUIRE(settle({&church, &server, &laptop}));
    REQUIRE(laptop.link.peers().size() == 2);
    REQUIRE(*laptop.link.peers().front()->key() == server.key());

    // Both have it all; the church is on the laptop's network, the server is not.
    laptop.link.shared().close_by = [&](const Peer& peer) { return peer.key() == church.key(); };
    std::map<std::string, int> came;
    for (auto* peer : laptop.link.peers()) {
        QObject::connect(peer, &Peer::media_done, [&, peer](const std::string&, const std::string&) {
            ++came[peer->key() == church.key() ? "church" : "server"];
        });
    }
    for (const auto& [path, bytes] : files) {
        std::filesystem::remove(laptop.media / path);
    }
    laptop.sync();
    REQUIRE(missing_media(laptop.engine).size() == files.size());
    laptop.link.fetch_more(church.uid);
    REQUIRE(settle({&church, &server, &laptop}));
    INFO("laptop: " << why(laptop));
    for (const auto& [path, bytes] : files) {
        CHECK(laptop.file(path) == bytes);
    }
    const auto room = static_cast<int>(k_media_at_once);
    CHECK(came["church"] == std::min(count, room));
    CHECK(came["server"] == count - std::min(count, room));
}

TEST_CASE("A computer that comes later and is faster takes the files over, from the end of their last piece",
          "[link][routes]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    std::map<std::string, std::string> videos;
    for (int i = 0; i < 4; ++i) {
        videos["videos/v" + std::to_string(i) + ".mp4"] = big_video(3 * static_cast<std::size_t>(k_media_piece_bytes) + i);
    }
    const auto founded = put_online_with_files(church, videos);
    pair_server(church, server, founded);
    // The server is slow to the laptop; the church, once it is there, is not.
    server.link.set_upload_cap(1'000'000);
    laptop.link.set_hold_for_media(300);
    laptop.link.set_rebalance_every(250);
    std::vector<std::string> moved;
    QObject::connect(&laptop.link, &Link::media_moved,
                     [&](const std::string&, const std::string& path) { moved.push_back(path); });

    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, server.key(), server.port());
    // The server measured, a piece of one video here from it.
    REQUIRE(wait_until([&] {
        auto* peer = connection_to(laptop, server.key());
        return peer != nullptr && peer->route().per_second && !laptop.link.media_arriving(church.uid).empty()
               && std::ranges::any_of(laptop.link.media_arriving(church.uid), [](const auto& entry) {
                      return entry.second.bytes >= k_media_piece_bytes;
                  });
    }, 30000));
    CHECK(moved.empty());

    laptop.link.connect_to("127.0.0.1", church.port(), church.key(), {{church.uid, laptop.join_entry()}});
    REQUIRE(wait_until([&] { return connection_to(laptop, church.key()) != nullptr; }));
    // Where each file began over the church's connection.
    std::map<std::string, std::int64_t> first_written;
    QObject::connect(connection_to(laptop, church.key()), &Peer::media_progress,
                     [&](const std::string&, const std::string& path, std::int64_t bytes) {
                         first_written.try_emplace(path, bytes);
                     });
    QElapsedTimer timer;
    timer.start();
    REQUIRE(settle({&church, &server, &laptop}, 60000));
    INFO("laptop: " << why(laptop));
    for (const auto& [path, bytes] : videos) {
        CHECK(laptop.file(path) == bytes);
        CHECK_FALSE(std::filesystem::exists(laptop.media / (path + ".part")));
    }
    // Well before the server alone would have sent the rest, some forty
    // seconds: a file part way through a piece there finishes it first.
    CHECK(timer.elapsed() < 20000);
    CHECK_FALSE(moved.empty());
    // At least one went on from a piece the server sent: the next piece is the
    // first written over the church's connection.
    CHECK(std::ranges::any_of(first_written, [](const auto& entry) { return entry.second > k_media_piece_bytes; }));
    // Each moved once.
    auto once = moved;
    std::ranges::sort(once);
    CHECK(std::ranges::adjacent_find(once) == once.end());
    CHECK(laptop.refused.empty());
}

TEST_CASE("Two routes about as fast do not pass files to and fro", "[link][routes]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    // Large: each connection takes two at most, so both have work from the start.
    std::map<std::string, std::string> videos;
    for (int i = 0; i < 4; ++i) {
        videos["videos/v" + std::to_string(i) + ".mp4"] =
            big_video(static_cast<std::size_t>(seventhslide::link::k_large_media) + i);
    }
    const auto founded = put_online_with_files(church, videos);
    pair_server(church, server, founded);
    church.link.set_upload_cap(8'000'000);
    server.link.set_upload_cap(8'000'000);
    laptop.link.set_hold_for_media(300);
    laptop.link.set_rebalance_every(250);
    std::vector<std::string> moved;
    QObject::connect(&laptop.link, &Link::media_moved,
                     [&](const std::string&, const std::string& path) { moved.push_back(path); });

    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    join_through(laptop, church.uid, code, church.key(), church.port());
    laptop.link.connect_to("127.0.0.1", server.port(), server.key(), {{church.uid, laptop.join_entry()}});
    REQUIRE(settle({&church, &server, &laptop}, 60000));
    INFO("laptop: " << why(laptop));
    for (const auto& [path, bytes] : videos) {
        CHECK(laptop.file(path) == bytes);
    }
    // Neither is clearly better, so nothing moves for it.
    CHECK(moved.empty());
    auto once = moved;
    std::ranges::sort(once);
    CHECK(std::ranges::adjacent_find(once) == once.end());
    CHECK(laptop.refused.empty());
}

TEST_CASE("A file handed over is taken to the end of its piece, and gone on from there without a read through",
          "[link][routes]")
{
    Arrivals a;
    const auto video = big_video(3 * k_media_piece_bytes);
    const auto digest = Arrivals::digest_of("videos/cd34.mp4", video, a.root.path / "scratch");
    const auto hashes = Arrivals::hashes_of(video, a.root.path / "scratch");
    int handed = 0;
    std::unique_ptr<seventhslide::link::MediaIn> first;
    {
        // A connection's events, and when what was handed over is written.
        auto events = seventhslide::link::MediaIn::Events{
            [&](const std::string&, const std::string&, std::int64_t bytes) { a.progress.push_back(bytes); },
            [&](const std::string&, const std::string&, const QString& why) { a.refused.push_back(why.toStdString()); },
            [&](const std::string&, const std::string&, const QString& why) { a.failed.push_back(why.toStdString()); },
            [&](const std::string&, const std::string&, const QString& why) { a.again.push_back(why.toStdString()); },
            [&](const std::string&, const std::string&) { ++a.done; },
            [] {},
            [&](const std::string&, const std::string&) { ++handed; }};
        first = std::make_unique<seventhslide::link::MediaIn>(a.shared, a.libraries, std::move(events));
    }
    REQUIRE(first->begin(a.lib(), digest) == 0);
    first->take_hashes("lib", digest.path, hashes);
    const auto frame = seventhslide::link::k_media_piece;
    // A piece and a half: handed over part way through the second.
    std::int64_t at = 0;
    for (; at < k_media_piece_bytes + k_media_piece_bytes / 2; at += frame) {
        first->take_bytes("lib", digest.path, at, QByteArray(video.data() + at, static_cast<qsizetype>(frame)), false);
    }
    REQUIRE(first->hand_over("lib", digest.path));
    CHECK(first->flowing("lib").empty());
    // The rest of that piece is still taken, and nothing after it.
    for (; at < 2 * k_media_piece_bytes; at += frame) {
        REQUIRE(first->expects_bytes("lib", digest.path, at));
        first->take_bytes("lib", digest.path, at, QByteArray(video.data() + at, static_cast<qsizetype>(frame)), false);
    }
    CHECK_FALSE(first->expects_bytes("lib", digest.path, at));
    REQUIRE(Arrivals::wait([&] { return handed == 1; }));
    CHECK(first->arriving("lib").empty());
    CHECK(first->free_to_begin("lib", digest.path));
    CHECK(std::filesystem::file_size(a.lib().media / "videos/cd34.mp4.part")
          == static_cast<std::uintmax_t>(2 * k_media_piece_bytes));

    // Marked, so a read through would be seen: it would cut this back.
    {
        std::fstream file(a.lib().media / "videos/cd34.mp4.part", std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(100);
        file.put(static_cast<char>(video[100] ^ 1));
    }
    auto second = a.connection();
    REQUIRE(second->begin(a.lib(), digest) == 2 * k_media_piece_bytes);
    second->take_hashes("lib", digest.path, hashes);
    Arrivals::send(*second, digest.path, video, 2 * k_media_piece_bytes);
    REQUIRE(Arrivals::wait([&] { return a.done == 1; }));
    CHECK(a.again.empty());
    CHECK(a.refused.empty());
}

namespace {

/// Takes one TLS connection, as ``me``.
struct TlsListener : QTcpServer {
    QSslConfiguration config;
    std::unique_ptr<QSslSocket> accepted;

    void incomingConnection(qintptr descriptor) override
    {
        accepted = std::make_unique<QSslSocket>();
        accepted->setSslConfiguration(config);
        QObject::connect(accepted.get(), &QSslSocket::sslErrors, [](const QList<QSslError>&) {});
        accepted->setSocketDescriptor(descriptor);
        accepted->startServerEncryption();
    }
};

}  // namespace

TEST_CASE("The outbox reads no store file while one it read waits to go", "[link][hardening]")
{
    using seventhslide::link::StoreFile;
    seventhslide::test::QtBootstrap qt;
    REQUIRE(seventhslide::link::use_openssl_tls());
    seventhslide::test::TempDir dir;
    const auto store = std::make_shared<FolderStore>(dir.path / "sync");
    store->write(Numbered::Changes, "church", 1, R"({"change":1})");
    const std::string part(3 * 1024 * 1024 + 17, 'p');
    const auto sha256 = seventhslide::crypto::sha256_hex(part);
    store->write_part(sha256, part);

    // A connection whose far end reads nothing until told.
    TlsListener listener;
    listener.config = seventhslide::link::tls_configuration({"laptop", "Laptop", SigningKey::generate()});
    REQUIRE(listener.listen(QHostAddress::LocalHost, 0));
    QSslSocket near;
    near.setSslConfiguration(seventhslide::link::tls_configuration({"church", "Church PC", SigningKey::generate()}));
    QObject::connect(&near, &QSslSocket::sslErrors, [](const QList<QSslError>&) {});
    near.connectToHostEncrypted(QStringLiteral("127.0.0.1"), listener.serverPort());
    const auto run_until = [](const std::function<bool()>& done) {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < 10000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return done();
    };
    REQUIRE(run_until([&] { return near.isEncrypted() && listener.accepted && listener.accepted->isEncrypted(); }));
    auto& far = *listener.accepted;
    far.setReadBufferSize(64 * 1024);

    seventhslide::link::PeerShared shared;
    shared.files.setMaxThreadCount(1);
    const std::vector<seventhslide::link::SyncedLibrary> libraries{
        {.uid = "lib", .store = store, .media = dir.path / "media"}};
    seventhslide::link::Outbox out(near, shared, libraries);
    QObject::connect(&near, &QSslSocket::bytesWritten, [&](qint64) { out.pump(); });
    QObject::connect(&near, &QSslSocket::encryptedBytesWritten, [&](qint64) { out.pump(); });

    // The part is read while the file thread is held; meanwhile more than
    // the write-ahead goes out, so once read it waits.
    std::atomic<bool> go = false;
    shared.files.start([&go] {
        while (!go) {
            QThread::msleep(5);
        }
    });
    const auto release = qScopeGuard([&] {
        go = true;
        shared.files.waitForDone();
    });
    out.part("lib", sha256);
    out.pump();
    out.message({{"type", "filler"}, {"text", std::string(24 * 1024 * 1024, 'f')}});
    go = true;
    shared.files.waitForDone();
    run_events_for(200);
    REQUIRE(out.unsent() > 2 * 1024 * 1024);
    // A change file asked for then, which goes before parts when neither is
    // read yet.
    out.file("lib", StoreFile::Change, "church", 1);
    out.pump();

    // Drained: the part goes first, then the change file is read and goes.
    seventhslide::link::FrameReader reader;
    reader.set_limits(32 * 1024 * 1024, seventhslide::link::k_max_bytes_frame);
    std::vector<std::string> came;
    const auto take = [&] {
        reader.feed(far.readAll());
        while (auto frame = reader.next()) {
            if (frame->bytes) {
                continue;
            }
            const auto m = nlohmann::json::parse(frame->payload.toStdString());
            const auto type = m.value("type", "");
            if (type == "piece" || type == "part" || type == "change") {
                const auto kind = type == "piece" ? m.value("kind", "") : type;
                if (came.empty() || came.back() != kind) {
                    came.push_back(kind);
                }
            }
        }
    };
    QObject::connect(&far, &QSslSocket::readyRead, take);
    take();
    REQUIRE(run_until([&] { return came.size() == 2 && !out.busy(); }));
    CHECK(came == std::vector<std::string>{"part", "change"});
}
