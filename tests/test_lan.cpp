// Over a real network, with a server on another computer: hidden, run by
// hand (docs/SERVER.md has the server's side):
//
//   on the other computer:   seventhslide --server --data /tmp/lan-test
//   here:                    SEVENTHSLIDE_LAN_SERVER=<host:port> seventhslide_tests_sync "[.lan]"
//   on the other computer:   seventhslide --server join --data /tmp/lan-test <the code it prints>
//
// A library put online here goes to that server, large enough that its
// change files and snapshot go in pieces (protocol 2); a second computer
// here, which never meets the first, joins it through the server, from the
// snapshot; and the server's announcements reach this computer. Each run leaves a library on
// the server: start it with a data folder kept for the test.
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/link/discovery.hpp>
#include <seventhslide/link/reach.hpp>

#include "link_test_helpers.hpp"

#include <QElapsedTimer>

#include <iostream>
#include <optional>
#include <string>

using namespace seventhslide::test::link_fixture;

TEST_CASE("Over the network, a library goes through a server on another computer", "[.lan]")
{
    const auto address = qEnvironmentVariable("SEVENTHSLIDE_LAN_SERVER").toStdString();
    if (address.empty()) {
        SKIP("Set SEVENTHSLIDE_LAN_SERVER to the host:port of `seventhslide --server` elsewhere");
    }
    const auto at = seventhslide::sync::parse_address(address);
    REQUIRE(at.has_value());
    const auto host = QString::fromStdString(at->host);
    INFO("server at " << address);

    // Put online here, with 3,000 songs (change files over 1 MiB), a video
    // of several pieces and a snapshot, and paired.
    Computer church{"church", "Church PC"};
    church.db.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 3000) "
                   "INSERT INTO songs (id, title, lyrics) SELECT 1000 + i, 'Hymn ' || i, "
                   "'[V1]\n' || substr(replace(hex(randomblob(1000)), '0', ' '), 1, 2000) FROM n");
    const auto founded = put_online(church);
    REQUIRE(church.store.sequences(Numbered::Changes, "church").size() >= 2);
    std::string video(5 * 1024 * 1024 + 123, '\0');
    for (std::size_t i = 0; i < video.size(); ++i) {
        video[i] = static_cast<char>((i * 7919) % 251);
    }
    church.put_file("videos/lan.mp4", video);
    church.db.exec("INSERT INTO video_files (filename, display_name) VALUES ('lan.mp4', 'LAN test')");
    REQUIRE(church.sync().changes_sent == 1);
    REQUIRE(church.engine.snapshot());
    // An invite for the server, naming the church, which reaches out to it
    // once a person has joined it there.
    std::cout << "On the server: seventhslide --server join "
              << church.members().invite(Role::Viewer, k_expires, founded.admin, {},
                                         seventhslide::sync::ComputerAddress{church.me.machine, church.key(), {}})
              << "\n" << std::flush;
    std::optional<seventhslide::crypto::PublicKey> server_key;
    QElapsedTimer joined;
    joined.start();
    while (!server_key && joined.elapsed() < 300000) {
        if (church.link.peers().empty()) {
            church.link.connect_to(host, at->port, std::nullopt);
        }
        settle({&church}, 3000);
        for (const auto& [machine, m] : church.list().members) {
            if (machine != church.me.machine) {
                server_key = m.key;
            }
        }
    }
    REQUIRE(server_key.has_value());
    REQUIRE(settle({&church}, 120000));
    INFO("church closed: " << why(church));
    REQUIRE(church.link.peers().size() == 1);
    REQUIRE(church.link.peers().front()->ready());

    // A computer that never meets the church joins through the server. The
    // church stays connected: with nothing left to send here, what it sent
    // may still be on its way over the network.
    Computer laptop{"laptop", "Laptop"};
    const auto invite = church.members().invite(Role::Editor, k_expires, founded.admin);
    laptop.share(church.uid);
    laptop.members().join(seventhslide::sync::read_invite(invite));
    laptop.link.connect_to(host, at->port, *server_key, {{church.uid, laptop.join_entry()}});
    QElapsedTimer waited;
    waited.start();
    while (laptop.file("videos/lan.mp4").size() != video.size() && waited.elapsed() < 120000) {
        settle({&church, &laptop}, 2000);
    }
    REQUIRE(settle({&church, &laptop}, 120000));
    INFO("laptop closed: " << why(laptop));
    CHECK(laptop.refused.empty());
    CHECK(laptop.file("videos/lan.mp4") == video);
    CHECK(laptop.file("images/ab12.png") == "lilies");
    CHECK(laptop.store.snapshots() == church.store.snapshots());
    laptop.engine.join(church.uid);
    laptop.sync();
    seventhslide::test::check_same_records(laptop.db, church.db);

    // The laptop, cut off, finds the server again on the network: its
    // announcements reach this computer, and the laptop reconnects to where
    // they came from.
    for (auto* p : std::vector(laptop.link.peers())) {
        p->close(QStringLiteral("cut"));
    }
    REQUIRE(settle({&church, &laptop}));
    seventhslide::link::Discovery discovery(laptop.key().fingerprint(), laptop.port());
    seventhslide::link::Reach::Options quick;
    quick.check_ms = 500;
    quick.lan_grace_ms = 3000;
    quick.attempt_ms = 5000;
    seventhslide::link::Reach reach(laptop.link, {}, quick);
    QObject::connect(&discovery, &seventhslide::link::Discovery::heard, &reach,
                     &seventhslide::link::Reach::heard);
    std::string heard_at;
    QObject::connect(&discovery, &seventhslide::link::Discovery::heard,
                     [&](const std::string& fp, const QHostAddress& from, quint16 port) {
                         if (fp == server_key->fingerprint() && port == at->port) {
                             heard_at = from.toString().toStdString();
                         }
                     });
    REQUIRE(discovery.start());
    reach.start();
    waited.restart();
    const auto reconnected = [&] {
        const auto* p = laptop.link.peer_with(*server_key);
        return p != nullptr && p->ready();
    };
    while ((heard_at.empty() || !reconnected()) && waited.elapsed() < 90000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    CHECK(heard_at == at->host);
    CHECK(reconnected());
}
