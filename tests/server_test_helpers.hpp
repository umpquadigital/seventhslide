#pragma once

// A server for a test: on loopback, in a data folder of its own, with what it takes
// for computers to reach it.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/library/library.hpp>
#include <seventhslide/server/server.hpp>

#include "link_test_helpers.hpp"
#include "test_temp_dir.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace seventhslide::test::server_fixture {

using namespace seventhslide::test::link_fixture;
using nlohmann::json;
using seventhslide::server::Server;
using seventhslide::server::ServerOptions;

/// A server on loopback, in a data folder of its own.
struct ServerHost {
    seventhslide::test::QtBootstrap qt;
    seventhslide::test::TempDir dir;
    std::unique_ptr<Server> server;
    /// Announcing on this computer's loopback only, to a port of the test's.
    bool discovery = false;
    std::string name = "Church server";

    ServerHost() { start(); }
    explicit ServerHost(bool announcing) : discovery(announcing) { start(); }

    std::filesystem::path data() const { return dir.path / "server"; }
    void start()
    {
        ServerOptions options;
        options.data = data();
        options.listen = QHostAddress::LocalHost;
        options.port = 0;
        options.addresses = {"127.0.0.1"};
        options.name = name;
        options.box_params = k_quick;
        // Nothing is announced on this computer's network.
        options.discovery = discovery;
        options.discovery_options.port = 0;
        options.discovery_options.targets = {{QHostAddress::LocalHost, 9}};
        server = std::make_unique<Server>(std::move(options));
    }
    void stop() { server.reset(); }
    Server* operator->() { return server.get(); }
    const seventhslide::crypto::PublicKey& key() const
    {
        return server->identity().key.public_key();
    }
    std::filesystem::path root(const std::string& uid) const
    {
        return data() / "libraries" / uid;
    }
    /// The library's database as the server has it.
    Database database(const std::string& uid) const
    {
        return Database::open_file(seventhslide::library::database_file(root(uid)));
    }
    json library(const std::string& uid)
    {
        const auto status = server->status();
        for (const auto& lib : status["libraries"]) {
            if (lib["uid"] == uid) {
                return lib;
            }
        }
        return nullptr;
    }
    json computer(const std::string& uid, const std::string& machine)
    {
        const auto lib = library(uid);
        for (const auto& c : lib["computers"]) {
            if (c["machine"] == machine) {
                return c;
            }
        }
        return nullptr;
    }
    /// Join by an invite, waited for.
    json join(const std::string& code)
    {
        std::optional<json> reply;
        server->join(code, [&](json r) { reply = std::move(r); });
        QElapsedTimer timer;
        timer.start();
        while (!reply && timer.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        REQUIRE(reply.has_value());
        return *reply;
    }
    /// Admin work, waited for.
    json admin(const std::string& uid, json request)
    {
        std::optional<json> reply;
        server->admin(uid, std::move(request), [&](json r) { reply = std::move(r); });
        QElapsedTimer timer;
        timer.start();
        while (!reply && timer.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        REQUIRE(reply.has_value());
        return *reply;
    }
};

/// Settle the computers and the server.
inline bool settle_all(std::initializer_list<Computer*> computers, ServerHost& host, int ms = 20000)
{
    const std::vector<Computer*> all(computers);
    return settle_while(
        [&] {
            return host->busy()
                   || std::ranges::any_of(all, [](Computer* c) { return c->link.busy(); });
        },
        ms);
}

/// An invite naming ``church``, at no address: the computer joining waits to
/// be reached (``Reach::set_invite_addresses``).
inline std::string invite_to_reach(Computer& church, const seventhslide::crypto::SigningKey& admin)
{
    return church.members().invite(Role::Viewer, k_expires, admin, {},
                                   seventhslide::sync::ComputerAddress{church.me.machine, church.key(), {}});
}

/// ``church`` puts its library on the server: invited as a viewer given the
/// locked admin key, as a person would for a computer always on, and reached
/// out to where it is, with no key expected.
inline void keep_at(Computer& church, ServerHost& host, const seventhslide::crypto::SigningKey& admin)
{
    REQUIRE(host.join(invite_to_reach(church, admin))["ok"] == true);
    church.link.connect_to("127.0.0.1", host->port(), std::nullopt);
    REQUIRE(settle_all({&church}, host));
    INFO("church closed: " << why(church));
    REQUIRE(church.list().find(host->identity().machine) != nullptr);
    church.members().set_key_holder(host->identity().machine, true, admin);
    church.link.library_changed(church.uid);
}

/// ``joiner`` joins through the server on ``code``.
inline void join_via(Computer& joiner, const std::string& uid, const std::string& code, ServerHost& host)
{
    join_through(joiner, uid, code, host.key(), host->port());
}

/// Connect ``c`` to the server as a member.
inline void reconnect(Computer& c, ServerHost& host)
{
    c.link.connect_to("127.0.0.1", host->port(), host.key());
}

}  // namespace seventhslide::test::server_fixture
