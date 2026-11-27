// Finding each other (discovery.hpp) and keeping connected (reach.hpp):
// computers on one network, here this computer's loopback address, each
// announcing to the others' discovery ports.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <seventhslide/link/discovery.hpp>
#include <seventhslide/link/reach.hpp>
#include <seventhslide/link/tls.hpp>

#include "link_test_helpers.hpp"
#include "test_wait.hpp"

#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using seventhslide::link::Announcement;
using seventhslide::link::Discovery;
using seventhslide::link::Reach;
using namespace seventhslide::test::link_fixture;
using seventhslide::test::wait_until;

namespace {

/// Quick to try again, for tests.
Reach::Options quick_reach()
{
    Reach::Options o;
    o.check_ms = 200;
    o.lan_grace_ms = 1500;
    o.first_retry_ms = 300;
    o.longest_retry_ms = 2000;
    o.startup_wait_ms = 0;
    return o;
}

/// A computer finding the others: its discovery and its reach.
struct Finding {
    std::unique_ptr<Discovery> discovery;
    std::unique_ptr<Reach> reach;
};

/// Discovery and reach for each of ``computers``, each announcing to all
/// the others, started.
std::vector<Finding> find_each_other(std::initializer_list<Computer*> computers)
{
    std::vector<Finding> out;
    Discovery::Options options;
    options.port = 0;
    options.interval_ms = 1000;
    for (auto* c : computers) {
        Finding f;
        f.discovery = std::make_unique<Discovery>(c->key().fingerprint(), c->port(), options);
        f.reach = std::make_unique<Reach>(c->link, c->root.path / "addresses.json", quick_reach());
        QObject::connect(f.discovery.get(), &Discovery::heard, f.reach.get(), &Reach::heard);
        REQUIRE(f.discovery->start());
        out.push_back(std::move(f));
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::vector<std::pair<QHostAddress, quint16>> targets;
        for (std::size_t j = 0; j < out.size(); ++j) {
            if (j != i) {
                targets.emplace_back(QHostAddress::LocalHost, out[j].discovery->port());
            }
        }
        out[i].discovery->set_targets(std::move(targets));
    }
    for (auto& f : out) {
        f.reach->start();
        f.discovery->announce();
    }
    return out;
}

std::size_t ready_peers(const Computer& c)
{
    return static_cast<std::size_t>(std::ranges::count_if(
        c.link.peers(), [](const seventhslide::link::Peer* p) { return p->ready() && !p->is_closed(); }));
}

/// Join ``joiner`` to ``church``'s library through it, and apply it.
void join(Computer& joiner, Computer& church, const MemberLog::Founded& founded, Role role = Role::Editor)
{
    const auto code = church.members().invite(role, k_expires, founded.admin);
    join_through(joiner, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &joiner}));
    joiner.engine.join(church.uid);
    joiner.sync();
}

void drop_all(std::initializer_list<Computer*> computers)
{
    for (auto* c : computers) {
        for (auto* p : std::vector(c->link.peers())) {
            p->close(QStringLiteral("test"));
        }
    }
    REQUIRE(settle(computers));
}

}  // namespace

TEST_CASE("An announcement says only a key's fingerprint and a port", "[discovery]")
{
    const Announcement a{std::string(64, 'a'), 8766};
    CHECK(seventhslide::link::read_announcement(seventhslide::link::encode_announcement(a)) == a);
    const auto bytes = seventhslide::link::encode_announcement(a).toStdString();
    CHECK(bytes.find("library") == std::string::npos);

    using seventhslide::link::read_announcement;
    CHECK_FALSE(read_announcement("hello"));
    CHECK_FALSE(read_announcement(R"({"kind":"seventhslide-announce","version":1,"fingerprint":"abc","port":8766})"));
    CHECK_FALSE(read_announcement(QByteArray::fromStdString(
        R"({"kind":"other","version":1,"fingerprint":")" + std::string(64, 'a') + R"(","port":8766})")));
    CHECK_FALSE(read_announcement(QByteArray::fromStdString(
        R"({"kind":"seventhslide-announce","version":1,"fingerprint":")" + std::string(64, 'a') + R"(","port":0})")));
    CHECK_FALSE(read_announcement(QByteArray(600, ' ')));
    // A later version is heard by what it shares with this one.
    CHECK(read_announcement(QByteArray::fromStdString(
              R"({"kind":"seventhslide-announce","version":2,"fingerprint":")" + std::string(64, 'b')
              + R"(","port":9000,"more":true})"))
          == Announcement{std::string(64, 'b'), 9000});
}

TEST_CASE("Two SeventhSlides on one network hear each other", "[discovery]")
{
    seventhslide::test::ensure_qt_application();
    Discovery::Options options;
    options.port = 0;
    Discovery first(std::string(64, '1'), 1111, options);
    Discovery second(std::string(64, '2'), 2222, options);
    std::vector<std::pair<std::string, quint16>> heard_by_first;
    std::vector<std::pair<std::string, quint16>> heard_by_second;
    QObject::connect(&first, &Discovery::heard, [&](const std::string& fp, const QHostAddress& from, quint16 port) {
        CHECK(from == QHostAddress::LocalHost);
        heard_by_first.emplace_back(fp, port);
    });
    QObject::connect(&second, &Discovery::heard, [&](const std::string& fp, const QHostAddress&, quint16 port) {
        heard_by_second.emplace_back(fp, port);
    });
    REQUIRE(first.start());
    REQUIRE(second.start());
    first.set_targets({{QHostAddress::LocalHost, second.port()}});
    second.set_targets({{QHostAddress::LocalHost, first.port()}});

    // The second, starting, announces; the first answers at once, not half
    // a minute later.
    second.announce();
    REQUIRE(wait_until([&] { return !heard_by_first.empty() && !heard_by_second.empty(); }, 5000));
    CHECK(heard_by_first.front() == std::pair{std::string(64, '2'), quint16{2222}});
    CHECK(heard_by_second.front() == std::pair{std::string(64, '1'), quint16{1111}});

    // Its own announcements and anything else on the port are let go by.
    run_events_for(1000);  // the answers to the answers
    QUdpSocket noise;
    noise.writeDatagram("not an announcement", QHostAddress::LocalHost, first.port());
    first.set_targets({{QHostAddress::LocalHost, first.port()}});
    first.announce();
    const auto before = heard_by_first.size();
    run_events_for(300);
    CHECK(heard_by_first.size() == before);
}

namespace {

/// The ``n``th of many keys' fingerprints, none of them a real computer's.
std::string made_up_fingerprint(unsigned n)
{
    char hex[65];
    std::snprintf(hex, sizeof hex, "%064x", n);
    return hex;
}

}  // namespace

TEST_CASE("Keys announced by the thousand are kept track of few at a time", "[discovery]")
{
    seventhslide::test::ensure_qt_application();
    Discovery::Options options;
    options.port = 0;
    Discovery here(std::string(64, '1'), 1111, options);
    std::set<std::string> heard;
    QObject::connect(&here, &Discovery::heard,
                     [&](const std::string& fp, const QHostAddress&, quint16) { heard.insert(fp); });
    REQUIRE(here.start());

    // Someone on the network announces ten thousand keys, a few at a time so
    // that most arrive.
    QUdpSocket flood;
    std::size_t most = 0;
    for (unsigned n = 0; n < 10000; ++n) {
        flood.writeDatagram(seventhslide::link::encode_announcement({made_up_fingerprint(n), 9000}),
                            QHostAddress::LocalHost, here.port());
        if (n % 50 == 49) {
            wait_until([&] { return heard.contains(made_up_fingerprint(n)); }, 1000);
            most = std::max(most, here.heard_count());
        }
    }
    CHECK(heard.size() > seventhslide::link::k_most_heard);
    CHECK(most <= seventhslide::link::k_most_heard);
}

TEST_CASE("Reach keeps only computers heard lately, few at a time", "[discovery]")
{
    Computer laptop{"laptop", "Laptop"};
    auto options = quick_reach();
    options.heard_fresh_ms = 300;
    Reach reach(laptop.link, {}, options);
    for (unsigned n = 0; n < 10000; ++n) {
        reach.heard(made_up_fingerprint(n), QHostAddress::LocalHost, 9000);
    }
    CHECK(reach.heard_count() == seventhslide::link::k_most_heard);

    // Not heard for longer than counts as there: forgotten at the next look.
    run_events_for(400);
    reach.check();
    CHECK(reach.heard_count() == 0);
    const std::string real(64, 'f');
    reach.heard(real, QHostAddress::LocalHost, 2222);
    CHECK(reach.heard_count() == 1);
    CHECK(std::ranges::any_of(reach.neighbours(), [&](const Reach::Neighbour& n) { return n.fingerprint == real; }));
}

TEST_CASE("Keys announced by the thousand push out no computer of a library, only the strangers heard "
          "longest ago",
          "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    std::string office;
    {
        // A member that is not connected now.
        Computer gone{"office", "Office"};
        join(gone, church, founded);
        office = gone.key().fingerprint();
    }
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(laptop.list().find("office") != nullptr);
    auto* to_church = laptop.link.peer_with(church.key());
    REQUIRE(to_church != nullptr);

    Reach reach(laptop.link, {}, quick_reach());
    reach.heard(church.key().fingerprint(), to_church->address(), church.port());
    reach.heard(office, QHostAddress::LocalHost, 1);
    reach.check();  // which computers are members
    REQUIRE(reach.close_by(*to_church));

    // A host on the network announces a thousand keys after them, each heard
    // after the one above it in order.
    const QHostAddress host(QStringLiteral("192.168.1.50"));
    for (unsigned n = 1000; n > 0; --n) {
        reach.heard(made_up_fingerprint(n), host, 9000);
    }
    const auto heard = [&](const std::string& fp) {
        return std::ranges::any_of(reach.neighbours(), [&](const Reach::Neighbour& n) { return n.fingerprint == fp; });
    };
    CHECK(reach.heard_count() == seventhslide::link::k_most_heard + 2);
    CHECK(reach.close_by(*to_church));
    CHECK(heard(office));
    // The strangers heard last are kept.
    const auto most = static_cast<unsigned>(seventhslide::link::k_most_heard);
    CHECK(heard(made_up_fingerprint(1)));
    CHECK(heard(made_up_fingerprint(most)));
    CHECK_FALSE(heard(made_up_fingerprint(most + 1)));
}

TEST_CASE("Computers of a library on one network find each other, one connection each", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    Computer office{"office", "Office"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    join(office, church, founded);
    drop_all({&church, &laptop, &office});

    auto finding = find_each_other({&church, &laptop, &office});
    REQUIRE(wait_until([&] {
        return ready_peers(church) == 2 && ready_peers(laptop) == 2 && ready_peers(office) == 2;
    }));
    REQUIRE(settle({&church, &laptop, &office}));
    for (auto* c : {&church, &laptop, &office}) {
        INFO(c->me.computer << " closed: " << why(*c));
        REQUIRE(c->link.peers().size() == 2);
        for (const auto* p : c->link.peers()) {
            // The lower key reached out.
            CHECK(p->outgoing() == (c->key().fingerprint() < p->key()->fingerprint()));
        }
    }

    // A change on the laptop reaches the office.
    laptop.db.exec("UPDATE songs SET key = 'A' WHERE title = 'Amazing Grace'");
    REQUIRE(laptop.sync().changes_sent == 1);
    REQUIRE(settle({&church, &laptop, &office}));
    CHECK(office.sync().files_applied >= 1);
    CHECK(office.text("SELECT key FROM songs WHERE title = 'Amazing Grace'") == "A");
}

TEST_CASE("A computer reaches the library's server at its address, and again after a cut",
          "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    const auto server_address = "127.0.0.1:" + std::to_string(server.port());
    join(server, church, founded, Role::Viewer);
    server.members().set_addresses({server_address});
    server.link.library_changed(church.uid);
    REQUIRE(settle({&church, &server}));
    join(laptop, church, founded);
    drop_all({&church, &laptop});

    // No network to hear on: the member list's address is how.
    Reach reach(laptop.link, laptop.root.path / "addresses.json", quick_reach());
    reach.start();
    REQUIRE(wait_until([&] { return laptop.link.peer_with(server.key()) != nullptr && ready_peers(laptop) == 1; }));
    REQUIRE(settle({&laptop, &server}));
    CHECK(reach.remembered(server.key().fingerprint()) == std::vector<std::string>{server_address});

    // Cut: reached again.
    for (auto* p : std::vector(server.link.peers())) {
        p->close(QStringLiteral("cut"));
    }
    REQUIRE(wait_until([&] { return ready_peers(laptop) == 1; }, 5000));

    // The address is kept for next time.
    Reach later(laptop.link, laptop.root.path / "addresses.json", quick_reach());
    CHECK(later.remembered(server.key().fingerprint()) == std::vector<std::string>{server_address});
}

TEST_CASE("An address that answers with another key is tried again only after a wait",
          "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    // A computer on the list whose address is the church's: whoever answers
    // there is not it.
    Computer pretend{"pretend", "Pretend server"};
    join(pretend, church, founded, Role::Viewer);
    pretend.members().set_addresses({"127.0.0.1:" + std::to_string(church.port())});
    pretend.link.library_changed(church.uid);
    REQUIRE(settle({&church, &laptop, &pretend}));
    REQUIRE(laptop.list().find("pretend") != nullptr);
    drop_all({&church, &laptop, &pretend});
    church.link.remove_library(church.uid);  // the church itself is not wanted here
    laptop.closed.clear();

    Reach reach(laptop.link, {}, quick_reach());
    reach.start();
    run_events_for(2500);
    const auto wrong = std::ranges::count_if(laptop.closed, [](const QString& w) {
        return w.contains(QStringLiteral("not the one expected"));
    });
    // 0.3 s, then 0.6, 1.2: four tries at most in 2.5 s, not one each check.
    CHECK(wrong >= 2);
    CHECK(wrong <= 4);
    CHECK(laptop.link.peer_with(pretend.key()) == nullptr);
}

TEST_CASE("An address that never answers is given up, for the computer's others", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    // The church's old address takes the connection and says nothing.
    QTcpServer silent;
    REQUIRE(silent.listen(QHostAddress::LocalHost));
    std::vector<QTcpSocket*> held;
    QObject::connect(&silent, &QTcpServer::newConnection, [&] { held.push_back(silent.nextPendingConnection()); });
    const auto old_address = "127.0.0.1:" + std::to_string(silent.serverPort());
    const auto new_address = "127.0.0.1:" + std::to_string(church.port());
    {
        const auto file = laptop.root.path / "addresses.json";
        std::ofstream(file) << nlohmann::json{{"format", 1},
                                              {"computers", {{church.key().fingerprint(), {old_address, new_address}}}}}
                                   .dump();
    }
    drop_all({&church, &laptop});

    auto options = quick_reach();
    options.attempt_ms = 1000;
    Reach reach(laptop.link, laptop.root.path / "addresses.json", options);
    reach.start();
    REQUIRE(wait_until([&] { return ready_peers(laptop) == 1; }, 10000));
    CHECK_FALSE(held.empty());  // tried first, and given up
    CHECK(std::ranges::any_of(laptop.closed, [](const QString& w) { return w.contains(QStringLiteral("in time")); }));
    CHECK(reach.remembered(church.key().fingerprint()).front() == new_address);
}

TEST_CASE("A name whose IPv6 address does not answer is reached at its IPv4 one", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    // At the same port on IPv6, something takes the connection and says nothing, as
    // far as Reach can tell like a firewall that drops it: Qt would wait 30 seconds there.
    QTcpServer silent;
    if (!silent.listen(QHostAddress::LocalHostIPv6, church.port())) {
        SKIP("This computer has no IPv6 loopback");
    }
    std::vector<QTcpSocket*> held;
    QObject::connect(&silent, &QTcpServer::newConnection, [&] { held.push_back(silent.nextPendingConnection()); });
    const auto name = "church.example.org:" + std::to_string(church.port());
    church.members().set_addresses({name});
    church.link.library_changed(church.uid);
    join(laptop, church, founded);
    drop_all({&church, &laptop});

    auto options = quick_reach();
    options.attempt_ms = 1000;
    std::vector<QString> looked_up;
    options.look_up = [&](const QString& host) {
        looked_up.push_back(host);
        return std::vector<QHostAddress>{QHostAddress(QHostAddress::LocalHostIPv6), QHostAddress(QHostAddress::LocalHost)};
    };
    Reach reach(laptop.link, laptop.root.path / "addresses.json", options);
    reach.start();
    REQUIRE(wait_until([&] { return ready_peers(laptop) == 1; }, 10000));
    CHECK_FALSE(held.empty());  // tried first, and given up
    CHECK(laptop.link.peers().front()->address() == QHostAddress(QHostAddress::LocalHost));
    CHECK(looked_up == std::vector<QString>{QStringLiteral("church.example.org")});  // once, not each try
    // The name is kept, not the address it had today.
    CHECK(reach.remembered(church.key().fingerprint()) == std::vector<std::string>{name});
}

TEST_CASE("A computer is reached at a name, looked up as any other", "[discovery]")
{
    Computer church{"church", "Church PC", QHostAddress::Any};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    const auto name = "localhost:" + std::to_string(church.port());
    church.members().set_addresses({name});
    church.link.library_changed(church.uid);
    join(laptop, church, founded);
    drop_all({&church, &laptop});

    Reach reach(laptop.link, laptop.root.path / "addresses.json", quick_reach());
    reach.start();
    REQUIRE(wait_until([&] { return ready_peers(laptop) == 1; }, 10000));
    CHECK(reach.remembered(church.key().fingerprint()) == std::vector<std::string>{name});
}

TEST_CASE("Two computers reaching each other at once keep one connection", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    drop_all({&church, &laptop});

    church.link.connect_to("127.0.0.1", laptop.port(), laptop.key());
    laptop.link.connect_to("127.0.0.1", church.port(), church.key());
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(church.link.peers().size() == 1);
    REQUIRE(laptop.link.peers().size() == 1);
    const bool church_lower = church.key().fingerprint() < laptop.key().fingerprint();
    CHECK(church.link.peers().front()->outgoing() == church_lower);
    CHECK(laptop.link.peers().front()->outgoing() == !church_lower);

    // And reaching out again to a computer connected already is that
    // connection.
    auto* again = laptop.link.connect_to("127.0.0.1", church.port(), church.key());
    CHECK(again == laptop.link.peers().front());
    CHECK(laptop.link.peers().size() == 1);
}

TEST_CASE("A file two computers have is fetched from one of them", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    church.put_file("videos/big.mp4", std::string(3 * 256 * 1024 + 7, 'v'));
    church.db.exec("INSERT INTO video_files (filename, display_name) VALUES ('big.mp4', 'Big')");
    REQUIRE(church.sync().changes_sent == 1);
    join(server, church, founded, Role::Viewer);
    REQUIRE(server.file("videos/big.mp4").size() == 3 * 256 * 1024 + 7);

    // The laptop joins through both at once.
    std::map<std::string, std::set<const void*>> fetched_through;
    QObject::connect(&laptop.link, &seventhslide::link::Link::peer_ready, [&](seventhslide::link::Peer* p) {
        QObject::connect(p, &seventhslide::link::Peer::media_progress,
                         [&, p](const std::string&, const std::string& path, std::int64_t) {
                             fetched_through[path].insert(p);
                         });
    });
    const auto code = church.members().invite(Role::Editor, k_expires, founded.admin);
    laptop.share(church.uid);
    laptop.members().join(seventhslide::sync::read_invite(code));
    laptop.link.connect_to("127.0.0.1", church.port(), church.key(), {{church.uid, laptop.join_entry()}});
    laptop.link.connect_to("127.0.0.1", server.port(), server.key(), {{church.uid, laptop.join_entry()}});
    REQUIRE(settle({&church, &server, &laptop}));
    INFO("laptop refused: " << (laptop.refused.empty() ? "" : laptop.refused.front().toStdString()));
    CHECK(laptop.refused.empty());
    CHECK(laptop.file("videos/big.mp4") == church.file("videos/big.mp4"));
    REQUIRE_FALSE(fetched_through.empty());
    for (const auto& [path, through] : fetched_through) {
        INFO(path);
        CHECK(through.size() == 1);
    }
    laptop.engine.join(church.uid);
    CHECK(laptop.sync().files_applied == 2);
    seventhslide::test::check_same_records(laptop.db, church.db);
}

TEST_CASE("A computer invited at an address is reached out to, and nothing else there gets in",
          "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer far{"far", "Cloud computer"};
    const auto founded = put_online(church);
    const auto far_address = "127.0.0.1:" + std::to_string(far.port());

    // The invite names the church, at no address the other can dial.
    const auto code = church.members().invite(Role::Viewer, k_expires, founded.admin, {},
                                              seventhslide::sync::ComputerAddress{"church", church.key(), {}});
    const auto invite_id = seventhslide::sync::read_invite(code).invite.id;
    Reach here(church.link, church.root.path / "addresses.json", quick_reach());

    SECTION("the computer invited")
    {
        far.share(church.uid);
        far.members().join(seventhslide::sync::read_invite(code));
        Reach there(far.link, far.root.path / "addresses.json", quick_reach());
        there.start();
        here.start();
        here.set_invite_addresses(church.uid, invite_id, k_expires, {far_address});
        REQUIRE(wait_until([&] { return far.store.sequences(Numbered::Changes, "church").size() == 1; }));
        REQUIRE(settle({&church, &far}));
        INFO("church closed: " << why(church) << "; far closed: " << why(far));
        CHECK(church.list().find("far") != nullptr);
        CHECK(far.file("images/ab12.png") == "lilies");
        // Nobody is waited for there now (at its next look), and it stays so
        // after a restart.
        CHECK(wait_until([&] { return here.invite_addresses(church.uid, invite_id).empty(); }));
        CHECK(Reach(church.link, church.root.path / "addresses.json", quick_reach()).invite_addresses(church.uid, invite_id).empty());
    }
    SECTION("a stranger at that address")
    {
        here.start();
        here.set_invite_addresses(church.uid, invite_id, k_expires, {far_address});
        // It shares nothing: one end or the other closes the connection, and
        // nothing has moved.
        REQUIRE(wait_until([&] { return !church.closed.empty(); }));
        CHECK(church.link.peer_with(far.key()) == nullptr);
        CHECK(far.store.machines(Numbered::Changes).empty());
        // It is tried again after a wait, and is still waited for.
        CHECK(here.invite_addresses(church.uid, invite_id) == std::vector<std::string>{far_address});
        CHECK(Reach(church.link, church.root.path / "addresses.json", quick_reach()).invite_addresses(church.uid, invite_id)
              == std::vector<std::string>{far_address});
    }
    SECTION("an invite run out")
    {
        here.start();
        here.set_invite_addresses(church.uid, invite_id, "2000-01-01T00:00:00Z", {far_address});
        REQUIRE(wait_until([&] { return here.invite_addresses(church.uid, invite_id).empty(); }, 3000));
        CHECK(church.closed.empty());
    }
}

TEST_CASE("A computer invited at an address is still waited for after a restart", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer far{"far", "Cloud computer"};
    const auto founded = put_online(church);
    const auto far_address = "127.0.0.1:" + std::to_string(far.port());
    const auto file = church.root.path / "addresses.json";
    const auto code = church.members().invite(Role::Viewer, k_expires, founded.admin, {},
                                              seventhslide::sync::ComputerAddress{"church", church.key(), {}});
    const auto invite_id = seventhslide::sync::read_invite(code).invite.id;
    far.share(church.uid);
    far.members().join(seventhslide::sync::read_invite(code));
    Reach there(far.link, far.root.path / "addresses.json", quick_reach());
    there.start();
    {
        Reach before(church.link, file, quick_reach());
        before.set_invite_addresses(church.uid, invite_id, k_expires, {far_address});
    }

    // Started before its libraries are taken up, as every computer is.
    church.link.remove_library(church.uid);
    Reach here(church.link, file, quick_reach());
    here.start();
    run_events_for(500);
    CHECK(here.invite_addresses(church.uid, invite_id) == std::vector<std::string>{far_address});
    CHECK(far.store.machines(Numbered::Changes).empty());

    church.share(church.uid);
    REQUIRE(wait_until([&] { return far.store.sequences(Numbered::Changes, "church").size() == 1; }));
    REQUIRE(settle({&church, &far}));
    CHECK(church.list().find("far") != nullptr);
    CHECK(wait_until([&] { return here.invite_addresses(church.uid, invite_id).empty(); }));
}

TEST_CASE("An old computer's entry at an invited address leaves room for the one invited", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer old{"old", "Old server"};
    Computer far{"far", "New server"};
    const auto founded = put_online(church);
    const auto far_address = "127.0.0.1:" + std::to_string(far.port());

    // The server being replaced: still on the list, at the address the new one
    // has now, and trying there gets the wrong key.
    join_through(old, church.uid, church.members().invite(Role::Viewer, k_expires, founded.admin), church.key(), church.port());
    REQUIRE(settle({&church, &old}));
    old.members().set_addresses({far_address});
    old.link.library_changed(church.uid);
    REQUIRE(settle({&church, &old}));
    REQUIRE(church.list().find("old") != nullptr);
    REQUIRE(church.list().find("old")->addresses == std::vector<std::string>{far_address});
    church.link.peers().front()->close(QStringLiteral("the old server stops"));
    REQUIRE(settle({&church, &old}));

    const auto code = church.members().invite(Role::Viewer, k_expires, founded.admin, {},
                                              seventhslide::sync::ComputerAddress{"church", church.key(), {}});
    const auto invite_id = seventhslide::sync::read_invite(code).invite.id;
    far.share(church.uid);
    far.members().join(seventhslide::sync::read_invite(code));
    Reach there(far.link, far.root.path / "addresses.json", quick_reach());
    there.start();
    Reach here(church.link, church.root.path / "addresses.json", quick_reach());
    here.start();
    here.set_invite_addresses(church.uid, invite_id, k_expires, {far_address});
    INFO("church closed: " << why(church));
    REQUIRE(wait_until([&] { return far.store.sequences(Numbered::Changes, "church").size() == 1; }));
    REQUIRE(wait_until([&] { return church.list().find("far") != nullptr; }));
    // Its invite used: nothing is waited for at its next look.
    CHECK(wait_until([&] { return here.invite_addresses(church.uid, invite_id).empty(); }));
}

TEST_CASE("A computer of the library answering at an invited address is not the one invited", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer there_already{"there", "Old server"};
    Computer late{"late", "New server"};
    const auto founded = put_online(church);
    const auto address = "127.0.0.1:" + std::to_string(there_already.port());
    join_through(there_already, church.uid, church.members().invite(Role::Viewer, k_expires, founded.admin), church.key(), church.port());
    REQUIRE(settle({&church, &there_already}));
    REQUIRE(church.list().find("there") != nullptr);
    church.link.peers().front()->close(QStringLiteral("to be reached again"));
    REQUIRE(settle({&church, &there_already}));

    const auto code = church.members().invite(Role::Viewer, k_expires, founded.admin);
    const auto id = seventhslide::sync::read_invite(code).invite.id;
    const auto file = church.root.path / "addresses.json";
    auto here = std::make_unique<Reach>(church.link, file, quick_reach());
    here->start();
    here->set_invite_addresses(church.uid, id, k_expires, {address});

    // Reached, and the wait goes on, without pulling down the connection there is.
    REQUIRE(wait_until([&] { return ready_peers(church) == 1; }));
    run_events_for(1000);
    CHECK(here->invite_addresses(church.uid, id) == std::vector<std::string>{address});
    CHECK(ready_peers(church) == 1);

    // Its invite used, by some other way: nothing is waited for, a restart or not.
    here = std::make_unique<Reach>(church.link, file, quick_reach());
    here->start();
    CHECK(here->invite_addresses(church.uid, id) == std::vector<std::string>{address});
    join_through(late, church.uid, code, church.key(), church.port());
    REQUIRE(settle({&church, &late}));
    REQUIRE(wait_until([&] { return here->invite_addresses(church.uid, id).empty(); }));
    CHECK(church.list().find("late") != nullptr);
}

TEST_CASE("A computer reaching out for an invite stops only for the library that came", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer far{"far", "Cloud computer"};
    const auto founded = put_online(church);
    const auto far_address = "127.0.0.1:" + std::to_string(far.port());
    // A second library here, for which the same computer was also invited.
    const std::string other(32, 'b');
    church.link.add_library({other, std::make_shared<FolderStore>(church.root.path / "other" / "sync"),
                             church.root.path / "other" / "media", {}, {}, {}, {}});
    const auto code = church.members().invite(Role::Viewer, k_expires, founded.admin, {},
                                              seventhslide::sync::ComputerAddress{"church", church.key(), {}});
    const auto invite_id = seventhslide::sync::read_invite(code).invite.id;
    far.share(church.uid);
    far.members().join(seventhslide::sync::read_invite(code));
    Reach there(far.link, far.root.path / "addresses.json", quick_reach());
    there.start();
    Reach here(church.link, church.root.path / "addresses.json", quick_reach());
    here.start();
    here.set_invite_addresses(church.uid, invite_id, k_expires, {far_address});
    here.set_invite_addresses(other, invite_id, k_expires, {far_address});
    REQUIRE(wait_until([&] { return far.store.sequences(Numbered::Changes, "church").size() == 1; }));
    REQUIRE(settle({&church, &far}));
    CHECK(wait_until([&] { return here.invite_addresses(church.uid, invite_id).empty(); }));
    CHECK(here.invite_addresses(other, invite_id) == std::vector<std::string>{far_address});

    // It goes on trying for the other, without pulling down the one there is.
    run_events_for(1000);
    CHECK(here.invite_addresses(other, invite_id) == std::vector<std::string>{far_address});
    CHECK(ready_peers(church) == 1);
}

TEST_CASE("Connected some other way, a computer heard on the network moves there, both ends alike",
          "[discovery]")
{
    if (!seventhslide::test::link_fixture::loopback_has(QStringLiteral("127.0.0.2"))) {
        SKIP("Needs the loopback address 127.0.0.2 (on macOS: sudo ifconfig lo0 alias 127.0.0.2 up)");
    }
    // 127.0.0.2 plays the address through a router; only 127.0.0.1 is "local".
    const QHostAddress near_by(QHostAddress::LocalHost);
    const QHostAddress router(QStringLiteral("127.0.0.2"));
    Computer church{"church", "Church PC", QHostAddress::Any};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    drop_all({&church, &laptop});
    laptop.link.connect_to(router.toString(), church.port(), church.key());
    REQUIRE(settle({&church, &laptop}));
    REQUIRE(laptop.link.peers().size() == 1);
    auto* through_the_router = laptop.link.peers().front();
    REQUIRE(through_the_router->address() == router);
    laptop.closed.clear();
    church.closed.clear();

    auto options = quick_reach();
    options.is_local = [near_by](const QHostAddress& a) { return a == near_by; };
    const auto church_address = near_by.toString().toStdString() + ":" + std::to_string(church.port());

    SECTION("it moves, and stays moved")
    {
        Reach reach(laptop.link, laptop.root.path / "addresses.json", options);
        reach.start();
        run_events_for(400);
        CHECK(laptop.link.peers().front() == through_the_router);  // nothing heard: left as it is

        reach.heard(church.key().fingerprint(), near_by, church.port());
        REQUIRE(wait_until([&] { return ready_peers(laptop) == 1 && laptop.link.peers().front()->address() == near_by; }));
        REQUIRE(settle({&church, &laptop}));
        INFO("laptop closed: " << why(laptop) << "; church closed: " << why(church));
        REQUIRE(ready_peers(church) == 1);
        CHECK(laptop.link.peers().size() == 1);
        CHECK(church.link.peers().size() == 1);
        CHECK(laptop.closed.size() == 1);  // the old one, and nothing else
        CHECK(church.closed.size() == 1);
        CHECK(reach.remembered(church.key().fingerprint()).front() == church_address);

        // Moved: nothing more happens, and the library goes on syncing over it.
        reach.heard(church.key().fingerprint(), near_by, church.port());
        run_events_for(800);
        CHECK(laptop.closed.size() == 1);
        church.db.exec("UPDATE services SET name = 'Divine Service'");
        REQUIRE(church.sync().changes_sent == 1);
        REQUIRE(settle({&church, &laptop}));
        CHECK(laptop.store.sequences(Numbered::Changes, "church").size() == 2);
    }
    SECTION("through an address that looks local and is not where it is heard, as over a VPN, it moves too")
    {
        options.is_local = [](const QHostAddress&) { return true; };
        options.lan_grace_ms = 0;
        Reach reach(laptop.link, laptop.root.path / "addresses.json", options);
        reach.start();
        reach.heard(church.key().fingerprint(), near_by, church.port());
        REQUIRE(wait_until([&] { return ready_peers(laptop) == 1 && laptop.link.peers().front()->address() == near_by; }));
    }
    SECTION("heard at the address it is connected through as well as another, it is left be")
    {
        options.lan_grace_ms = 0;
        Reach reach(laptop.link, laptop.root.path / "addresses.json", options);
        reach.start();
        reach.heard(church.key().fingerprint(), router, church.port());
        reach.heard(church.key().fingerprint(), near_by, church.port());
        run_events_for(800);
        CHECK(laptop.link.peers().front() == through_the_router);
        CHECK(laptop.closed.empty());
    }
    SECTION("the connection that moved it is the last, wherever it is heard next")
    {
        auto* moved = laptop.link.connect_instead(router.toString(), church.port(), church.key());
        REQUIRE(wait_until([&] { return moved->ready() && ready_peers(laptop) == 1; }));
        laptop.closed.clear();
        options.lan_grace_ms = 0;
        Reach reach(laptop.link, laptop.root.path / "addresses.json", options);
        reach.start();
        reach.heard(church.key().fingerprint(), near_by, church.port());
        run_events_for(800);
        CHECK(laptop.link.peers().front() == moved);
        CHECK(laptop.closed.empty());
    }
    SECTION("the network address failing leaves the connection alone, and is not tried in a loop")
    {
        // Nothing listens here now.
        quint16 dead = 0;
        {
            QTcpServer gone;
            REQUIRE(gone.listen(near_by));
            dead = gone.serverPort();
        }
        Reach reach(laptop.link, laptop.root.path / "addresses.json", options);
        reach.start();
        reach.heard(church.key().fingerprint(), near_by, dead);
        run_events_for(2500);
        CHECK(laptop.link.peers().front() == through_the_router);
        CHECK(through_the_router->ready());
        CHECK(ready_peers(laptop) == 1);
        // First wait 1.5 s for the higher key, or 0.3 s, 0.6, 1.2: never each check.
        CHECK(laptop.closed.size() <= 4);
        CHECK(church.closed.empty());
    }
    SECTION("a transfer under way goes on over the new one")
    {
        std::string video;
        for (int i = 0; video.size() < 96 * 1024 * 1024; ++i) {
            video += "frame " + std::to_string(i) + '\n';
        }
        church.put_file("videos/moved.mp4", video);
        church.db.exec("INSERT INTO video_files (filename, display_name) VALUES ('moved.mp4', 'Moved')");
        options.lan_grace_ms = 0;
        Reach reach(laptop.link, laptop.root.path / "addresses.json", options);
        reach.start();
        bool moved = false;
        QObject::connect(through_the_router, &seventhslide::link::Peer::media_progress,
                         [&](const std::string&, const std::string&, std::int64_t) {
                             if (!moved) {
                                 moved = true;
                                 reach.heard(church.key().fingerprint(), near_by, church.port());
                                 QTimer::singleShot(0, &reach, &Reach::check);
                             }
                         });
        // What came over the new one, so it did not all come over the old.
        bool came_over_the_new = false;
        QObject::connect(&laptop.link, &seventhslide::link::Link::peer_ready, [&](seventhslide::link::Peer* peer) {
            QObject::connect(peer, &seventhslide::link::Peer::media_progress,
                             [&](const std::string&, const std::string&, std::int64_t) { came_over_the_new = true; });
        });
        REQUIRE(church.sync().changes_sent == 1);
        REQUIRE(wait_until([&] { return laptop.file("videos/moved.mp4").size() == video.size(); }, 60000));
        REQUIRE(settle({&church, &laptop}));
        INFO("laptop closed: " << why(laptop));
        CHECK(moved);
        CHECK(came_over_the_new);
        CHECK(laptop.refused.empty());
        CHECK(laptop.file("videos/moved.mp4") == video);
        CHECK(laptop.link.peers().front()->address() == near_by);
    }
}

TEST_CASE("Addresses further off wait for the network at the start, and give way to it", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    Computer far{"far", "Far computer"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    join(far, church, founded, Role::Viewer);
    // Far off, at an address that never answers; the church right here.
    far.members().set_addresses({"192.0.2.1:9"});
    far.link.library_changed(church.uid);
    church.members().set_addresses({"127.0.0.1:" + std::to_string(church.port())});
    church.link.library_changed(church.uid);
    REQUIRE(settle({&church, &laptop, &far}));
    REQUIRE(laptop.list().find("far")->addresses == std::vector<std::string>{"192.0.2.1:9"});
    drop_all({&church, &laptop, &far});

    SECTION("at the start")
    {
        auto options = quick_reach();
        options.startup_wait_ms = 800;
        Reach reach(laptop.link, {}, options);
        reach.start();
        // On this network: reached at once. Further off: not yet.
        REQUIRE(wait_until([&] { return ready_peers(laptop) == 1; }, 500));
        CHECK(laptop.link.peer_with(church.key()) != nullptr);
        CHECK(laptop.link.peer_with(far.key()) == nullptr);
        run_events_for(1200);
        CHECK_FALSE(laptop.closed.size() + laptop.link.peers().size() == 1);  // tried by now
    }
    SECTION("an attempt further off, once it is heard here")
    {
        Reach reach(laptop.link, {}, quick_reach());
        reach.start();
        wait_until([&] { return laptop.link.peer_with(far.key()) != nullptr; }, 1000);
        reach.heard(far.key().fingerprint(), QHostAddress::LocalHost, far.port());
        // Well before the 15 s an attempt that never answers would hold it up.
        REQUIRE(wait_until([&] {
            const auto* peer = laptop.link.peer_with(far.key());
            return peer != nullptr && peer->ready();
        }, 5000));
    }
    SECTION("heard here but not reachable here, what is further off is not held up")
    {
        // A firewall lets announcements through and not connections: an attempt
        // further off is not given up again and again for one that cannot be made.
        quint16 dead = 0;
        {
            QTcpServer gone;
            REQUIRE(gone.listen(QHostAddress::LocalHost));
            dead = gone.serverPort();
        }
        auto options = quick_reach();
        options.longest_retry_ms = 60000;  // once failed, not soon tried again
        options.first_retry_ms = 60000;
        options.lan_grace_ms = 0;
        Reach reach(laptop.link, {}, options);
        reach.heard(far.key().fingerprint(), QHostAddress::LocalHost, dead);
        reach.start();
        // The address heard fails at once; then the one further off is tried,
        // and left to get on with it.
        REQUIRE(wait_until([&] {
            const auto* peer = laptop.link.peer_with(far.key());
            return peer != nullptr;
        }, 2000));
        const auto closed_before = std::ranges::count_if(laptop.closed, [](const QString& w) {
            return w.contains(QStringLiteral("Heard on the network"));
        });
        run_events_for(1000);
        CHECK(std::ranges::count_if(laptop.closed, [](const QString& w) {
                  return w.contains(QStringLiteral("Heard on the network"));
              }) == closed_before);
    }
}

TEST_CASE("An attempt that gives way leaves one coming from the other end to get here", "[discovery]")
{
    if (!seventhslide::test::link_fixture::loopback_has(QStringLiteral("127.0.0.2"))) {
        SKIP("Needs the loopback address 127.0.0.2 (on macOS: sudo ifconfig lo0 alias 127.0.0.2 up)");
    }
    // Takes the connection and says nothing, so the attempt stays on its way.
    QTcpServer silent;
    REQUIRE(silent.listen(QHostAddress(QStringLiteral("127.0.0.2"))));
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    Computer far{"far", "Far computer"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    join(far, church, founded, Role::Viewer);
    far.members().set_addresses({"127.0.0.2:" + std::to_string(silent.serverPort())});
    far.link.library_changed(church.uid);
    REQUIRE(settle({&church, &laptop, &far}));
    drop_all({&church, &laptop, &far});
    laptop.closed.clear();

    auto options = quick_reach();
    options.lan_grace_ms = 0;
    options.attempt_ms = 800;
    options.is_local = [](const QHostAddress& a) { return a == QHostAddress(QHostAddress::LocalHost); };
    Reach reach(laptop.link, {}, options);
    reach.start();
    REQUIRE(wait_until([&] { return laptop.link.peer_with(far.key()) != nullptr; }, 1000));

    // That computer connects meanwhile, and has yet to say hello.
    QSslSocket coming;
    coming.setSslConfiguration(seventhslide::link::tls_configuration(far.me));
    QObject::connect(&coming, &QSslSocket::sslErrors, [](const QList<QSslError>&) {});
    coming.connectToHostEncrypted(QStringLiteral("127.0.0.1"), laptop.port());
    REQUIRE(wait_until([&] { return coming.isEncrypted() && laptop.link.peers().size() == 2; }));
    run_events_for(100);

    reach.heard(far.key().fingerprint(), QHostAddress::LocalHost, far.port());
    run_events_for(1400);  // past the time an attempt has
    INFO("laptop closed: " << why(laptop));
    CHECK(std::ranges::count_if(laptop.closed, [](const QString& w) {
              return w.contains(QStringLiteral("Heard on the network"));
          }) == 1);
    CHECK(std::ranges::none_of(laptop.closed, [](const QString& w) {
        return w.contains(QStringLiteral("No answer in time"));
    }));
    CHECK(coming.state() == QAbstractSocket::ConnectedState);
    CHECK(far.link.peers().empty());  // nothing reached out as well
    CHECK(reach.remembered(far.key().fingerprint()).empty());
}

TEST_CASE("An address typed for a computer is tried first, kept, and checked", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer server{"server", "Church server"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join(server, church, founded, Role::Viewer);
    join(laptop, church, founded);
    // The server moved, and the laptop did not hear: what it knows goes nowhere.
    server.members().set_addresses({"192.0.2.1:9"});
    server.link.library_changed(church.uid);
    REQUIRE(settle({&church, &server, &laptop}));
    drop_all({&church, &server, &laptop});
    const auto fingerprint = server.key().fingerprint();
    const auto right = "127.0.0.1:" + std::to_string(server.port());
    const auto file = laptop.root.path / "addresses.json";

    SECTION("the stranded computer gets through, and it is kept")
    {
        {
            Reach reach(laptop.link, file, quick_reach());
            reach.start();
            reach.set_typed_addresses(fingerprint, {right});
            CHECK(reach.typed_addresses(fingerprint) == std::vector<std::string>{right});
            REQUIRE(wait_until([&] {
                const auto* peer = laptop.link.peer_with(server.key());
                return peer != nullptr && peer->ready();
            }));
        }
        CHECK(Reach(laptop.link, file, quick_reach()).typed_addresses(fingerprint) == std::vector<std::string>{right});
        // On no list.
        CHECK(laptop.list().find("server")->addresses == std::vector<std::string>{"192.0.2.1:9"});

        // Cleared: gone, and not gone back to by way of where it was last reached.
        Reach again(laptop.link, file, quick_reach());
        CHECK(again.remembered(fingerprint).front() == right);
        again.set_typed_addresses(fingerprint, {});
        CHECK(again.typed_addresses(fingerprint).empty());
        CHECK(again.remembered(fingerprint).empty());
        CHECK(Reach(laptop.link, file, quick_reach()).typed_addresses(fingerprint).empty());
    }
    SECTION("several, tried in turn: one that goes nowhere does not keep it from the next")
    {
        // Given up on soon, as one that never answers is.
        auto options = quick_reach();
        options.attempt_ms = 1000;
        Reach reach(laptop.link, file, options);
        reach.start();
        reach.set_typed_addresses(fingerprint, {"192.0.2.2:9", right, right});
        CHECK(reach.typed_addresses(fingerprint) == std::vector<std::string>{"192.0.2.2:9", right});
        REQUIRE(wait_until([&] {
            const auto* peer = laptop.link.peer_with(server.key());
            return peer != nullptr && peer->ready();
        }));
        // One taken away, and the other kept.
        reach.set_typed_addresses(fingerprint, {right});
        CHECK(Reach(laptop.link, file, quick_reach()).typed_addresses(fingerprint) == std::vector<std::string>{right});
    }
    SECTION("one kept as an earlier SeventhSlide kept it is read")
    {
        std::ofstream(file) << nlohmann::json{{"format", 1}, {"computers", nlohmann::json::object()},
                                              {"typed", {{fingerprint, right}}}}
                                   .dump();
        CHECK(Reach(laptop.link, file, quick_reach()).typed_addresses(fingerprint) == std::vector<std::string>{right});
    }
    SECTION("an invite reached out to as an earlier SeventhSlide kept it, an address an entry, is read")
    {
        const auto at = [](const char* address, const char* invite) {
            return nlohmann::json{{"library", "lib"}, {"address", address}, {"invite", invite}, {"expires", k_expires}};
        };
        std::ofstream(file) << nlohmann::json{{"format", 1},
                                              {"computers", nlohmann::json::object()},
                                              {"expected", {at("far.example.org:8766", "feedface"),
                                                            at("203.0.113.9:9000", "feedface"),
                                                            at("far.example.org:8766", "feedface"),
                                                            at("elsewhere.example.org:8766", "")}}}
                                   .dump();
        const Reach read(laptop.link, file, quick_reach());
        CHECK(read.invite_addresses("lib", "feedface")
              == std::vector<std::string>{"far.example.org:8766", "203.0.113.9:9000"});
        CHECK(read.invite_addresses("lib", "").empty());
    }
    SECTION("cleared while it is being tried, it does not come back by getting through")
    {
        Reach reach(laptop.link, file, quick_reach());
        reach.start();
        reach.set_typed_addresses(fingerprint, {right});
        // At once: before the connection on its way can be ready.
        reach.check();
        reach.set_typed_addresses(fingerprint, {});
        run_events_for(500);
        CHECK(reach.typed_addresses(fingerprint).empty());
        const auto remembered = reach.remembered(fingerprint);
        CHECK(std::ranges::find(remembered, right) == remembered.end());
    }
    SECTION("whatever answers there with another key gets nothing")
    {
        Reach reach(laptop.link, file, quick_reach());
        reach.start();
        // The church answers at the address typed for the server.
        reach.set_typed_addresses(fingerprint, {"127.0.0.1:" + std::to_string(church.port())});
        REQUIRE(wait_until([&] {
            return std::ranges::any_of(laptop.closed, [](const QString& w) {
                return w.contains(QStringLiteral("not the one expected"));
            });
        }));
        const auto* peer = laptop.link.peer_with(server.key());
        CHECK((peer == nullptr || !peer->ready()));
    }
}

// Introductions (Link::introduce, Reach::introduced): a computer connected to two
// members of a library tells each where the other is.

namespace {

/// Every address counts as one on the internet: here all are loopback.
bool nothing_local(const QHostAddress&)
{
    return false;
}

/// ``quick_reach``, taking loopback addresses as on the internet.
Reach::Options reach_far()
{
    auto o = quick_reach();
    o.is_local = nothing_local;
    return o;
}

/// ``home`` and ``stage`` members of ``church``'s library, connected to the church
/// alone, each with what the other's entry on the list says.
void members_apart(Computer& church, Computer& home, Computer& stage, const MemberLog::Founded& founded)
{
    join(home, church, founded);
    join(stage, church, founded, Role::Viewer);
    home.link.connect_to("127.0.0.1", church.port(), church.key());
    REQUIRE(settle({&church, &home, &stage}));
    REQUIRE(home.list().find("stage") != nullptr);
    REQUIRE(stage.list().find("home") != nullptr);
    drop_all({&church, &home, &stage});
}

/// Connect ``c`` to ``church``, and wait for it to be let in.
void connect_to_church(Computer& c, Computer& church)
{
    c.link.connect_to("127.0.0.1", church.port(), church.key());
    REQUIRE(wait_until([&] {
        const auto* peer = c.link.peer_with(church.key());
        return peer != nullptr && peer->ready();
    }));
}

}  // namespace

TEST_CASE("Two members connected to a third are told where the other is, and reach each other there",
          "[discovery][introductions]")
{
    Computer church{"church", "Church server"};
    Computer home{"home", "Home laptop"};
    Computer stage{"stage", "Stage display"};
    const auto founded = put_online(church);
    members_apart(church, home, stage, founded);
    church.link.shared().is_local = nothing_local;

    Reach home_reach(home.link, {}, reach_far());
    Reach stage_reach(stage.link, {}, reach_far());
    connect_to_church(home, church);
    connect_to_church(stage, church);
    home_reach.start();
    stage_reach.start();
    REQUIRE(wait_until([&] {
        const auto* peer = home.link.peer_with(stage.key());
        return peer != nullptr && peer->ready();
    }));
    CHECK(home_reach.introduced_at(stage.key().fingerprint())
          == std::vector<std::string>{"127.0.0.1:" + std::to_string(stage.port())});
    CHECK(stage_reach.introduced_at(home.key().fingerprint())
          == std::vector<std::string>{"127.0.0.1:" + std::to_string(home.port())});
    // Never put on a list.
    REQUIRE(settle({&church, &home, &stage}));
    CHECK(home.list().find("stage")->addresses.empty());
    CHECK(church.list().find("home")->addresses.empty());
}

TEST_CASE("A computer that does not share its address is introduced to nobody, nor told of anyone",
          "[discovery][introductions]")
{
    Computer church{"church", "Church server"};
    Computer home{"home", "Home laptop"};
    Computer stage{"stage", "Stage display"};
    const auto founded = put_online(church);
    members_apart(church, home, stage, founded);
    church.link.shared().is_local = nothing_local;
    stage.link.set_share_address(false);

    Reach home_reach(home.link, {}, reach_far());
    Reach stage_reach(stage.link, {}, reach_far());
    connect_to_church(home, church);
    connect_to_church(stage, church);
    home_reach.start();
    stage_reach.start();
    run_events_for(1500);
    CHECK(home_reach.introduced_at(stage.key().fingerprint()).empty());
    CHECK(stage_reach.introduced_at(home.key().fingerprint()).empty());
    CHECK(home.link.peer_with(stage.key()) == nullptr);

    // Turned on, it asks, and both are told.
    stage.link.set_share_address(true);
    REQUIRE(wait_until([&] {
        const auto* peer = home.link.peer_with(stage.key());
        return peer != nullptr && peer->ready();
    }));
}

TEST_CASE("An address of this place's own networks is not passed on", "[discovery][introductions]")
{
    Computer church{"church", "Church server"};
    Computer home{"home", "Home laptop"};
    Computer stage{"stage", "Stage display"};
    const auto founded = put_online(church);
    members_apart(church, home, stage, founded);
    // The church sees both at loopback, which is its own: not told.

    Reach home_reach(home.link, {}, reach_far());
    Reach stage_reach(stage.link, {}, reach_far());
    connect_to_church(home, church);
    connect_to_church(stage, church);
    home_reach.start();
    stage_reach.start();
    run_events_for(1500);
    CHECK(home_reach.introduced_at(stage.key().fingerprint()).empty());
    CHECK(stage_reach.introduced_at(home.key().fingerprint()).empty());
    CHECK(home.link.peer_with(stage.key()) == nullptr);

    // Nor a name, which would be looked up wherever it pointed.
    home_reach.introduced(church.uid, church.key().fingerprint(), stage.key().fingerprint(), "example.org:8766");
    CHECK(home_reach.introduced_at(stage.key().fingerprint()).empty());
}

TEST_CASE("An address of this computer's own networks is not taken when told of one", "[discovery][introductions]")
{
    Computer home{"home", "Home laptop"};
    Reach reach(home.link, {}, quick_reach());
    const std::string fp(64, 'a');
    for (const auto* address : {"127.0.0.1:8766", "192.168.1.20:8766", "10.0.0.5:8766", "[fd00::5]:8766",
                                "[fe80::1]:8766"}) {
        reach.introduced("lib", std::string(64, 'b'), fp, address);
    }
    CHECK(reach.introduced_at(fp).empty());
    reach.introduced("lib", std::string(64, 'b'), fp, "203.0.113.9:8766");
    CHECK(reach.introduced_at(fp) == std::vector<std::string>{"203.0.113.9:8766"});
}

TEST_CASE("A connection refused at once leaves nothing of itself to the next check", "[discovery]")
{
    Computer church{"church", "Church PC"};
    Computer laptop{"laptop", "Laptop"};
    const auto founded = put_online(church);
    join(laptop, church, founded);
    drop_all({&church, &laptop});

    // No route to the broadcast address, on Linux and macOS alike: refused
    // inside connectToHost, so the peer is closed before connect_to returns,
    // as one is when the network is down (a Mac just awake).
    const auto* refused = laptop.link.connect_to(QStringLiteral("255.255.255.255"), 9, std::nullopt);
    if (!refused->is_closed()) {
        SKIP("This system does not refuse the broadcast address at once");
    }

    int refusals = 0;
    QObject::connect(&laptop.link, &seventhslide::link::Link::peer_closed,
                     [&refusals](seventhslide::link::Peer*, const QString&) { ++refusals; });
    Reach reach(laptop.link, laptop.root.path / "addresses.json", quick_reach());
    reach.start();
    reach.set_typed_addresses(church.key().fingerprint(), {"255.255.255.255:9"});
    // Tried again as any address that does not answer is: after 300 ms, then
    // twice as long each time (quick_reach), each refused peer deleted before
    // the next check. Noted as an attempt after it had closed, one was never
    // told to wait (peer_closed had been and gone): tried at every check, 200
    // ms apart, and the attempt kept after the peer was deleted, for a later
    // check to use (it crashed the app on a Mac).
    // It deletes what was let go of (deleteLater) as it goes.
    run_events_for(2500);
    CHECK(refusals >= 2);
    CHECK(refusals <= 6);
    CHECK(std::ranges::none_of(laptop.link.peers(),
                               [](const seventhslide::link::Peer* p) { return p->is_closed(); }));
}

TEST_CASE("An introduction to a wrong address gets nothing, and is not tried in a loop", "[discovery][introductions]")
{
    Computer church{"church", "Church server"};
    Computer home{"home", "Home laptop"};
    Computer stage{"stage", "Stage display"};
    Computer pretend{"pretend", "Another computer"};
    const auto founded = put_online(church);
    members_apart(church, home, stage, founded);
    church.link.shared().is_local = nothing_local;
    // The stage says it takes connections where another computer does, and does
    // not reach out itself.
    stage.link.shared().listening_port = pretend.port();
    home.closed.clear();

    Reach home_reach(home.link, {}, reach_far());
    connect_to_church(home, church);
    connect_to_church(stage, church);
    home_reach.start();
    REQUIRE(wait_until([&] { return !home_reach.introduced_at(stage.key().fingerprint()).empty(); }));
    run_events_for(2500);
    const auto wrong = std::ranges::count_if(home.closed, [](const QString& w) {
        return w.contains(QStringLiteral("not the one expected"));
    });
    // 0.3 s, then 0.6, 1.2: four tries at most in 2.5 s, not one each check.
    CHECK(wrong >= 1);
    CHECK(wrong <= 4);
    CHECK(home.link.peer_with(stage.key()) == nullptr);
}

TEST_CASE("Few introductions are kept, and fewer taken a minute", "[discovery][introductions]")
{
    Computer home{"home", "Home laptop"};
    Reach reach(home.link, {}, reach_far());
    const std::string fp(64, 'a');
    for (int port = 1000; port < 1010; ++port) {
        reach.introduced("lib", std::string(64, 'b'), fp, "203.0.113.9:" + std::to_string(port));
    }
    // Two in the minute, the latest first.
    CHECK(reach.introduced_at(fp) == std::vector<std::string>{"203.0.113.9:1001", "203.0.113.9:1000"});
    // One told again is not new.
    reach.introduced("lib", std::string(64, 'b'), fp, "203.0.113.9:1000");
    CHECK(reach.introduced_at(fp).size() == 2);
    // Of this computer itself, nothing.
    reach.introduced("lib", std::string(64, 'b'), home.key().fingerprint(), "203.0.113.9:2000");
    CHECK(reach.introduced_at(home.key().fingerprint()).empty());
}

TEST_CASE("One that comes back by another connection is introduced again", "[discovery][introductions]")
{
    Computer church{"church", "Church server"};
    Computer home{"home", "Home laptop"};
    Computer stage{"stage", "Stage display"};
    const auto founded = put_online(church);
    members_apart(church, home, stage, founded);
    church.link.shared().is_local = nothing_local;
    int told = 0;
    QObject::connect(&home.link, &seventhslide::link::Link::introduced,
                     [&](const std::string&, const std::string&, const std::string& fingerprint, const std::string&) {
                         told += fingerprint == stage.key().fingerprint() ? 1 : 0;
                     });
    connect_to_church(home, church);
    connect_to_church(stage, church);
    REQUIRE(wait_until([&] { return told == 1; }));
    // Nothing changed: not told again.
    church.link.library_changed(church.uid);
    run_events_for(500);
    CHECK(told == 1);

    // The stage went away a while, and came back.
    for (auto* peer : std::vector(stage.link.peers())) {
        peer->close(QStringLiteral("the stage display was switched off"));
    }
    REQUIRE(settle({&church, &stage}));
    connect_to_church(stage, church);
    REQUIRE(wait_until([&] { return told == 2; }));
}
