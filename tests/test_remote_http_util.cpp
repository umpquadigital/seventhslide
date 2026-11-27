// Shared HTTP helpers behind the web remote.
//
// ``is_private_address`` is the only thing standing between the remote and the
// open internet when ``private_networks_only`` is set, so it is pinned here as
// a table rather than left to the end-to-end server test, which can only ever
// reach it from loopback.

#include "test_qt_app.hpp"
#include "test_wait.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/remote/remote_http_util.hpp>
#include <seventhslide/remote/remote_server.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace seventhslide::remote;
using Status = QHttpServerResponse::StatusCode;

namespace {

struct AddressCase {
    const char* literal;
    bool priv;
};

} // namespace

TEST_CASE("http::is_private_address admits exactly the LAN ranges", "[remote][http]")
{
    // Loopback, link-local, RFC1918, CGNAT, IPv6 unique-local - and the
    // addresses just outside each block, which must stay public.
    const std::vector<AddressCase> cases{
        // Loopback.
        {"127.0.0.1", true},
        {"127.255.255.254", true},
        {"::1", true},
        // Link-local (v4 169.254/16 and v6 fe80::/10).
        {"169.254.1.1", true},
        {"fe80::1", true},
        // RFC1918 10/8.
        {"10.0.0.0", true},
        {"10.255.255.255", true},
        {"9.255.255.255", false},
        {"11.0.0.1", false},
        // RFC1918 172.16/12 - the block that is easiest to get wrong.
        {"172.15.255.255", false},
        {"172.16.0.0", true},
        {"172.31.255.255", true},
        {"172.32.0.1", false},
        // RFC1918 192.168/16.
        {"192.167.255.255", false},
        {"192.168.0.1", true},
        {"192.168.255.255", true},
        {"192.169.0.1", false},
        // CGNAT 100.64/10.
        {"100.63.255.255", false},
        {"100.64.0.1", true},
        {"100.127.255.255", true},
        {"100.128.0.1", false},
        // IPv6 unique-local fc00::/7.
        {"fc00::1", true},
        {"fd00::1", true},
        {"fdff:ffff::1", true},
        {"fe00::1", false},
        // Public.
        {"8.8.8.8", false},
        {"1.1.1.1", false},
        {"203.0.113.5", false},
        {"2001:db8::1", false},
        {"2606:4700:4700::1111", false},
    };

    for (const AddressCase& c : cases) {
        const QHostAddress address{QString::fromLatin1(c.literal)};
        INFO("address " << c.literal);
        REQUIRE(address.protocol() != QAbstractSocket::UnknownNetworkLayerProtocol);
        REQUIRE(http::is_private_address(address) == c.priv);
    }
}

TEST_CASE("http::is_private_address unwraps IPv4-mapped IPv6 clients", "[remote][http]")
{
    // A v4 client reaching a dual-stack listener arrives as ::ffff:a.b.c.d. The
    // mapped form must classify the same as the bare v4 address, or a LAN phone
    // is locked out (or a WAN client let in) depending on how the socket bound.
    REQUIRE(http::is_private_address(QHostAddress(QStringLiteral("::ffff:10.0.0.5"))));
    REQUIRE(http::is_private_address(QHostAddress(QStringLiteral("::ffff:192.168.1.20"))));
    REQUIRE(http::is_private_address(QHostAddress(QStringLiteral("::ffff:172.16.4.4"))));
    REQUIRE_FALSE(http::is_private_address(QHostAddress(QStringLiteral("::ffff:8.8.8.8"))));
    REQUIRE_FALSE(http::is_private_address(QHostAddress(QStringLiteral("::ffff:172.32.0.1"))));
}

TEST_CASE("http::is_private_address fails closed on unusable addresses", "[remote][http]")
{
    // A wildcard or unparseable peer is not evidence of a LAN client, so the
    // gate must refuse rather than wave it through.
    REQUIRE_FALSE(http::is_private_address(QHostAddress()));
    REQUIRE_FALSE(http::is_private_address(QHostAddress(QStringLiteral("not-an-address"))));
    REQUIRE_FALSE(http::is_private_address(QHostAddress(QHostAddress::Any)));
    REQUIRE_FALSE(http::is_private_address(QHostAddress(QHostAddress::AnyIPv4)));
    REQUIRE_FALSE(http::is_private_address(QHostAddress(QHostAddress::AnyIPv6)));
    REQUIRE_FALSE(http::is_private_address(QHostAddress(QStringLiteral("0.0.0.0"))));
}

TEST_CASE("http::mime_for types every asset the remote ships", "[remote][http]")
{
    REQUIRE(http::mime_for(QStringLiteral("index.html")) == "text/html; charset=utf-8");
    REQUIRE(http::mime_for(QStringLiteral("app.css")) == "text/css; charset=utf-8");
    REQUIRE(http::mime_for(QStringLiteral("js/main.js")) == "application/javascript; charset=utf-8");
    REQUIRE(http::mime_for(QStringLiteral("icons/logo.svg")) == "image/svg+xml");
    REQUIRE(http::mime_for(QStringLiteral("icons/favicon.png")) == "image/png");
    REQUIRE(http::mime_for(QStringLiteral("icons/favicon.ico")) == "image/x-icon");
    REQUIRE(http::mime_for(QStringLiteral("manifest.webmanifest"))
            == "application/manifest+json");

    // Unknown and extensionless paths fall back to a type no browser will
    // execute or render.
    REQUIRE(http::mime_for(QStringLiteral("notes.txt")) == "application/octet-stream");
    REQUIRE(http::mime_for(QString()) == "application/octet-stream");

    // Matching is on the suffix, so a directory that merely contains the
    // spelling must not win.
    REQUIRE(http::mime_for(QStringLiteral(".html/evil.bin")) == "application/octet-stream");
}

TEST_CASE("http::json_response and json_error carry body, type, and status", "[remote][http]")
{
    const QHttpServerResponse ok =
        http::json_response(nlohmann::json{{"a", 1}, {"b", "two"}});
    REQUIRE(ok.statusCode() == Status::Ok);
    REQUIRE(ok.mimeType() == "application/json; charset=utf-8");
    const nlohmann::json parsed = nlohmann::json::parse(ok.data().toStdString());
    REQUIRE(parsed["a"] == 1);
    REQUIRE(parsed["b"] == "two");

    const QHttpServerResponse created =
        http::json_response(nlohmann::json::object(), Status::Created);
    REQUIRE(created.statusCode() == Status::Created);

    const QHttpServerResponse err = http::json_error("unauthorized", Status::Unauthorized);
    REQUIRE(err.statusCode() == Status::Unauthorized);
    const nlohmann::json err_body = nlohmann::json::parse(err.data().toStdString());
    REQUIRE(err_body["error"] == "unauthorized");
}

// --- token_from ----------------------------------------------------------
//
// QHttpServerRequest has no public way to set headers or a query, so the three
// token sources are exercised through a real listening server. The precedence
// between them is the point: the WebSocket ``?token=`` fallback exists only
// because a browser cannot set Authorization on an upgrade, and it must not
// quietly override a header the client did send.

namespace {

class NullBridge final : public IRemoteBridge {
public:
    void settle(View&) const override {}
    [[nodiscard]] nlohmann::json section(Section, const View&) const override
    {
        return nlohmann::json::object();
    }
    [[nodiscard]] CommandResult invoke(seventhslide::remote::Command,
                                       const nlohmann::json&,
                                       View&) override
    {
        return CommandResult::good();
    }
    [[nodiscard]] nlohmann::json library_page(const LibraryQuery&) const override
    {
        return nlohmann::json::object();
    }
    [[nodiscard]] nlohmann::json service_list(std::string_view, std::string_view) const override
    {
        return nlohmann::json::object();
    }
    [[nodiscard]] std::string glyph_sprite() const override { return {}; }
    [[nodiscard]] PendingThumb thumb(const ThumbRequest&) const override { return {}; }
};

using seventhslide::test::wait_until;

/// GET ``url`` with optional raw headers; returns the parsed JSON body.
nlohmann::json get_json(const QString& url,
                        const std::vector<std::pair<QByteArray, QByteArray>>& headers = {})
{
    QNetworkAccessManager nam;
    QNetworkRequest request{QUrl(url)};
    for (const auto& [name, value] : headers) {
        request.setRawHeader(name, value);
    }
    QNetworkReply* reply = nam.get(request);
    wait_until([reply] { return reply->isFinished(); });
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return nlohmann::json::parse(body.toStdString(), nullptr, false);
}

QString mint_token(const QString& base, const QString& pin)
{
    QNetworkAccessManager nam;
    QNetworkRequest request{QUrl(base + QStringLiteral("/api/auth"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const nlohmann::json body{{"pin", pin.toStdString()}};
    QNetworkReply* reply = nam.post(request, QByteArray::fromStdString(body.dump()));
    wait_until([reply] { return reply->isFinished(); });
    const QByteArray raw = reply->readAll();
    reply->deleteLater();
    const nlohmann::json parsed = nlohmann::json::parse(raw.toStdString(), nullptr, false);
    if (!parsed.is_object()) {
        return {};
    }
    return QString::fromStdString(parsed.value("token", std::string{}));
}

} // namespace

TEST_CASE("http::token_from reads header, cookie, and query alike", "[remote][http]")
{
    seventhslide::test::ensure_qt_application();

    NullBridge bridge;
    RemoteServer server{bridge};
    RemoteServer::Config cfg;
    cfg.port = 0;
    cfg.pin = QStringLiteral("2468");
    REQUIRE(server.start(cfg));
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.port());

    const QString token = mint_token(base, QStringLiteral("2468"));
    REQUIRE_FALSE(token.isEmpty());

    const QString hello = base + QStringLiteral("/api/hello");

    // No credential at all.
    const nlohmann::json anon = get_json(hello);
    REQUIRE(anon["needsPin"] == true);
    REQUIRE(anon["authed"] == false);

    // 1. Authorization: Bearer.
    const nlohmann::json by_header =
        get_json(hello, {{"Authorization", "Bearer " + token.toUtf8()}});
    REQUIRE(by_header["authed"] == true);

    // 2. Session cookie.
    const nlohmann::json by_cookie =
        get_json(hello, {{"Cookie", QByteArray(http::k_cookie_name) + "=" + token.toUtf8()}});
    REQUIRE(by_cookie["authed"] == true);

    // The cookie is found among neighbours, not only when it stands alone.
    const nlohmann::json among_cookies = get_json(
        hello,
        {{"Cookie", "theme=dark; " + QByteArray(http::k_cookie_name) + "=" + token.toUtf8()
                        + "; tz=UTC"}});
    REQUIRE(among_cookies["authed"] == true);

    // 3. ?token= on the URL (the WebSocket upgrade path).
    const nlohmann::json by_query =
        get_json(hello + QStringLiteral("?token=") + token);
    REQUIRE(by_query["authed"] == true);

    // A wrong token in any position is refused.
    REQUIRE(get_json(hello, {{"Authorization", "Bearer wrong"}})["authed"] == false);
    REQUIRE(get_json(hello + QStringLiteral("?token=wrong"))["authed"] == false);

    // Precedence: the header is consulted first, so a stale bearer is NOT
    // rescued by a good query token. A silent fallback here would mean a
    // revoked credential could be revived by appending a URL parameter.
    const nlohmann::json header_beats_query = get_json(
        hello + QStringLiteral("?token=") + token, {{"Authorization", "Bearer wrong"}});
    REQUIRE(header_beats_query["authed"] == false);

    // Likewise the cookie is consulted before the query.
    const nlohmann::json cookie_beats_query = get_json(
        hello + QStringLiteral("?token=") + token,
        {{"Cookie", QByteArray(http::k_cookie_name) + "=wrong"}});
    REQUIRE(cookie_beats_query["authed"] == false);

    server.stop();
}
