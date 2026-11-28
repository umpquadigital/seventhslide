// End-to-end transport test: a real listening socket, a real WebSocket client,
// and a fake bridge. Everything above the socket is covered by
// test_remote_protocol; this proves the wiring.

#include "test_qt_app.hpp"
#include "test_wait.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/remote/remote_protocol.hpp>
#include <seventhslide/remote/remote_server.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrl>
#include <QThread>
#include <QWebSocket>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace seventhslide::remote;

namespace {

class FakeBridge final : public IRemoteBridge {
public:
    std::vector<std::string> invoked;

    /// A view starts on the "console" tab; setFocusedStackId moves it, and
    /// the Stacks section says which tab a client sees.
    void settle(View& view) const override
    {
        if (view.stack.empty()) {
            view.stack = "console";
        }
    }
    [[nodiscard]] nlohmann::json section(Section s, const View& view) const override
    {
        nlohmann::json body{{"name", std::string(section_name(s))}, {"tick", tick}};
        if (s == Section::Stacks) {
            body["focusedStackId"] = view.stack;
        }
        return body;
    }
    [[nodiscard]] CommandResult invoke(seventhslide::remote::Command command,
                                       const nlohmann::json& args,
                                       View& view) override
    {
        invoked.emplace_back(seventhslide::remote::protocol::command_spec(command).name);
        if (command == seventhslide::remote::Command::SetFocusedStackId) {
            view.stack = args.at("stackId").get<std::string>();
            return CommandResult::good();
        }
        ++tick;
        return CommandResult::good();
    }
    [[nodiscard]] nlohmann::json library_page(const LibraryQuery& q) const override
    {
        if (library_throws) {
            throw std::runtime_error("database is locked");
        }
        return {{"total", 1}, {"offset", q.offset},
                {"rows", nlohmann::json::array({{{"kind", q.kind}, {"query", q.query}}})}};
    }
    [[nodiscard]] nlohmann::json service_list(std::string_view tag,
                                              std::string_view) const override
    {
        return {{"tag", tag}, {"tags", nlohmann::json::array()},
                {"services", nlohmann::json::array()}};
    }
    [[nodiscard]] std::string glyph_sprite() const override
    {
        return R"(<svg><defs><symbol id="ic-song"></symbol></defs></svg>)";
    }
    [[nodiscard]] PendingThumb thumb(const ThumbRequest& r) const override
    {
        ++thumb_renders;
        return thumbs ? thumbs(r) : PendingThumb{};
    }

    int tick = 0;
    std::function<PendingThumb(const ThumbRequest&)> thumbs;
    mutable int thumb_renders = 0;
    bool library_throws = false;
};

using seventhslide::test::run_events_for;
using seventhslide::test::wait_until;

struct Fixture {
    FakeBridge bridge;
    RemoteServer server{bridge};

    bool listen(const QString& pin = {},
                std::chrono::seconds token_ttl = seventhslide::remote::Auth::k_token_ttl)
    {
        RemoteServer::Config cfg;
        cfg.port = 0;  // ask the OS for a free port
        cfg.pin = pin;
        cfg.token_ttl = token_ttl;
        return server.start(cfg);
    }

    bool start(RemoteServer::Config cfg)
    {
        cfg.port = 0;
        return server.start(cfg);
    }

    [[nodiscard]] QString base() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(server.port());
    }
};

QByteArray http_get(const QString& url, int* status = nullptr,
                    const QByteArray& bearer = {})
{
    QNetworkAccessManager nam;
    QNetworkRequest request{QUrl(url)};
    if (!bearer.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + bearer);
    }
    QNetworkReply* reply = nam.get(request);
    wait_until([reply] { return reply->isFinished(); });
    if (status != nullptr) {
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return body;
}

QByteArray http_post_json(const QString& url, const QByteArray& body, int* status)
{
    QNetworkAccessManager nam;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = nam.post(request, body);
    wait_until([reply] { return reply->isFinished(); });
    *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray out = reply->readAll();
    reply->deleteLater();
    return out;
}

} // namespace

TEST_CASE("RemoteServer binds, serves the app, and stops cleanly", "[remote][R1]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;

    REQUIRE(fx.listen());
    REQUIRE(fx.server.running());
    REQUIRE(fx.server.port() > 0);
    REQUIRE(fx.server.last_error().isEmpty());

    int status = 0;
    const QByteArray page = http_get(fx.base() + "/", &status);
    REQUIRE(status == 200);
    REQUIRE(page.contains("SeventhSlide Remote"));
    // The bridge's glyph sprite is inlined where index.html marks it:
    // <use href="external.svg#id"> does not resolve in Chrome or Safari.
    REQUIRE(page.contains("<symbol id=\"ic-song\""));
    REQUIRE_FALSE(page.contains("<!-- glyphs -->"));
    // Remote chrome: left rail and controller, no outputs column.
    REQUIRE(page.contains("col-left"));
    REQUIRE(page.contains("Controller"));
    REQUIRE(page.contains("col-center"));
    REQUIRE_FALSE(page.contains("col-right"));
    // A playback remote: no service picker.
    REQUIRE_FALSE(page.contains("svcPickModal"));

    REQUIRE(page.contains("src=\"/js/main.js\""));

    // Every stylesheet the page links is served.
    const QRegularExpression sheet(QStringLiteral(R"re(href="(/css/[\w-]+\.css)")re"));
    int sheets = 0;
    for (const auto& match : sheet.globalMatch(QString::fromUtf8(page))) {
        CAPTURE(match.captured(1).toStdString());
        http_get(fx.base() + match.captured(1), &status);
        REQUIRE(status == 200);
        ++sheets;
    }
    REQUIRE(sheets > 0);
    http_get(fx.base() + "/css/nothing.css", &status);
    REQUIRE(status == 404);

    fx.server.stop();
    REQUIRE_FALSE(fx.server.running());
}

TEST_CASE("RemoteServer serves the client's modules and every module they import",
          "[remote][R1]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    QStringList bundled;
    for (QDirIterator it(QStringLiteral(":/remote/js"), QDir::Files); it.hasNext();) {
        bundled << it.nextFileInfo().fileName();
    }
    REQUIRE(bundled.contains(QStringLiteral("main.js")));

    const QRegularExpression import_from(QStringLiteral(R"(from '\./([\w-]+\.js)')"));
    for (const QString& name : bundled) {
        CAPTURE(name.toStdString());
        int status = 0;
        const QByteArray body = http_get(fx.base() + "/js/" + name, &status);
        REQUIRE(status == 200);
        QFile file(QStringLiteral(":/remote/js/") + name);
        REQUIRE(file.open(QIODevice::ReadOnly));
        REQUIRE(body == file.readAll());
        // A module imported but missing from the resources fails the whole
        // page in the browser, before any of it runs.
        for (const auto& match : import_from.globalMatch(QString::fromUtf8(body))) {
            CAPTURE(match.captured(1).toStdString());
            REQUIRE(bundled.contains(match.captured(1)));
        }
    }

    int status = 0;
    http_get(fx.base() + "/js/nothing.js", &status);
    REQUIRE(status == 404);
    http_get(fx.base() + "/app.js", &status);
    REQUIRE(status == 404);
}

TEST_CASE("RemoteServer serves the browser and home-screen icons", "[remote][R1]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    int status = 0;
    const QByteArray page = http_get(fx.base() + "/", &status);
    REQUIRE(status == 200);
    REQUIRE(page.contains("/favicon.png"));
    REQUIRE(page.contains("/apple-touch-icon.png"));

    // Every icon the page links, the manifest names, or a browser auto-probes.
    const QStringList png_paths{
        QStringLiteral("/favicon.png"),
        QStringLiteral("/apple-touch-icon.png"),
        QStringLiteral("/apple-touch-icon-precomposed.png"),
        QStringLiteral("/icons/icon-192.png"),
        QStringLiteral("/icons/icon-512.png"),
    };
    for (const QString& path : png_paths) {
        const QByteArray body = http_get(fx.base() + path, &status);
        INFO(path.toStdString());
        REQUIRE(status == 200);
        REQUIRE(body.startsWith(QByteArray("\x89PNG", 4)));
    }

    // Browsers probe /favicon.ico regardless of the PNG <link>.
    const QByteArray ico = http_get(fx.base() + "/favicon.ico", &status);
    REQUIRE(status == 200);
    REQUIRE(ico.startsWith(QByteArray("\x00\x00\x01\x00", 4)));

    const QByteArray manifest = http_get(fx.base() + "/manifest.webmanifest", &status);
    REQUIRE(status == 200);
    REQUIRE(manifest.contains("/icons/icon-192.png"));
    REQUIRE(manifest.contains("/icons/icon-512.png"));

    fx.server.stop();
}

TEST_CASE("RemoteServer reports a port it cannot take", "[remote][R1]")
{
    seventhslide::test::ensure_qt_application();
    Fixture first;
    REQUIRE(first.listen());

    FakeBridge bridge;
    RemoteServer second(bridge);
    RemoteServer::Config cfg;
    cfg.port = static_cast<quint16>(first.server.port());
    REQUIRE_FALSE(second.start(cfg));
    REQUIRE_FALSE(second.running());
    // The settings dialog shows this string, so it must not be empty.
    REQUIRE_FALSE(second.last_error().isEmpty());
}

TEST_CASE("RemoteServer: a client gets hello then a full snapshot", "[remote][R2]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    QWebSocket client;
    std::vector<nlohmann::json> received;
    QObject::connect(&client, &QWebSocket::textMessageReceived,
                     [&received](const QString& text) {
                         received.push_back(nlohmann::json::parse(text.toStdString()));
                     });
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(fx.server.port())));

    REQUIRE(wait_until([&received] { return received.size() >= 2; }));
    REQUIRE(received[0].at("type") == "hello");
    REQUIRE(received[1].at("type") == "snapshot");
    for (const Section s : all_sections()) {
        REQUIRE(received[1].at("state").contains(std::string(section_name(s))));
    }
    REQUIRE(wait_until([&fx] { return fx.server.client_count() == 1; }));
}

TEST_CASE("RemoteServer: a page from another site cannot open the socket", "[remote][R2]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());
    const QUrl ws(QStringLiteral("ws://127.0.0.1:%1/ws").arg(fx.server.port()));

    // A browser names the page that opens a socket; with no PIN, a page from
    // anywhere could otherwise drive the console.
    for (const QString& origin : {QStringLiteral("http://evil.example"),
                                  QStringLiteral("http://127.0.0.1:1"), QStringLiteral("null")}) {
        QWebSocket foreign(origin);
        bool greeted = false;
        bool ended = false;
        QObject::connect(&foreign, &QWebSocket::textMessageReceived,
                         [&greeted](const QString&) { greeted = true; });
        QObject::connect(&foreign, &QWebSocket::errorOccurred, [&ended] { ended = true; });
        QObject::connect(&foreign, &QWebSocket::disconnected, [&ended] { ended = true; });
        foreign.open(ws);
        INFO(origin.toStdString());
        REQUIRE(wait_until([&ended] { return ended; }));
        CHECK_FALSE(greeted);
        CHECK(fx.server.client_count() == 0);
    }

    // The server's own page connects.
    QWebSocket own(QStringLiteral("http://127.0.0.1:%1").arg(fx.server.port()));
    bool greeted = false;
    QObject::connect(&own, &QWebSocket::textMessageReceived,
                     [&greeted](const QString&) { greeted = true; });
    own.open(ws);
    REQUIRE(wait_until([&greeted] { return greeted; }));
    REQUIRE(wait_until([&fx] { return fx.server.client_count() == 1; }));
}

// A page whose own name was pointed at this computer (DNS rebinding) sends
// its own name as the Host, and matches its Origin to it: refused, while the
// addresses and names that can only be this computer's are served.
TEST_CASE("RemoteServer: a request sent by another site's name is refused", "[remote][R2]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());
    const auto status_for = [&fx](const QByteArray& host) {
        QNetworkAccessManager nam;
        QNetworkRequest request{QUrl(fx.base() + QStringLiteral("/api/hello"))};
        request.setRawHeader("Host", host);
        QNetworkReply* reply = nam.get(request);
        wait_until([reply] { return reply->isFinished(); });
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return status;
    };
    const QByteArray port = QByteArray::number(fx.server.port());
    CHECK(status_for("evil.example:" + port) == 403);
    CHECK(status_for("church.example.org:" + port) == 403);
    CHECK(status_for("127.0.0.1:" + port) == 200);
    CHECK(status_for("localhost:" + port) == 200);
    CHECK(status_for("seventhslide.local:" + port) == 200);
}

TEST_CASE("RemoteServer: a command reaches the bridge and a patch comes back", "[remote][R3]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    QWebSocket client;
    std::vector<nlohmann::json> received;
    QObject::connect(&client, &QWebSocket::textMessageReceived,
                     [&received](const QString& text) {
                         received.push_back(nlohmann::json::parse(text.toStdString()));
                     });
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(fx.server.port())));
    REQUIRE(wait_until([&received] { return received.size() >= 2; }));
    received.clear();

    client.sendTextMessage(QStringLiteral(
        R"({"type":"cmd","id":"c1","name":"nextSlide","args":{}})"));

    REQUIRE(wait_until([&received] {
        return std::any_of(received.begin(), received.end(), [](const nlohmann::json& m) {
            return m.at("type") == "ack";
        });
    }));
    REQUIRE(fx.bridge.invoked.size() == 1);
    REQUIRE(fx.bridge.invoked[0] == "nextSlide");

    // The coalescing timer then produces exactly one patch for the change.
    REQUIRE(wait_until([&received] {
        return std::any_of(received.begin(), received.end(), [](const nlohmann::json& m) {
            return m.at("type") == "patch";
        });
    }));
}

TEST_CASE("RemoteServer: one client's snapshot does not swallow another client's patch", "[remote][R3]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    const auto connect_client = [&fx](QWebSocket& socket, std::vector<nlohmann::json>& received) {
        QObject::connect(&socket, &QWebSocket::textMessageReceived,
                         [&received](const QString& text) {
                             received.push_back(nlohmann::json::parse(text.toStdString()));
                         });
        socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(fx.server.port())));
        return wait_until([&received] { return received.size() >= 2; });
    };
    QWebSocket first;
    std::vector<nlohmann::json> first_received;
    REQUIRE(connect_client(first, first_received));
    QWebSocket second;
    std::vector<nlohmann::json> second_received;
    REQUIRE(connect_client(second, second_received));
    first_received.clear();

    // A desktop-side change, then the second client resyncs before the
    // coalescing timer would have flushed it.
    ++fx.bridge.tick;
    fx.server.mark_dirty(Section::Lines);
    second.sendTextMessage(QStringLiteral(R"({"type":"resync"})"));

    REQUIRE(wait_until([&first_received] {
        return std::any_of(first_received.begin(), first_received.end(),
                           [](const nlohmann::json& m) {
                               return m.at("type") == "patch"
                                   && m.at("sections").contains("lines");
                           });
    }));
}

// Every device keeps its own stack tab. Before, the bridge held one tab
// for every connected device, so one phone picking a tab moved the other's.
TEST_CASE("RemoteServer: each client keeps its own stack tab", "[remote][R3]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    struct Device {
        QWebSocket socket;
        std::vector<nlohmann::json> received;
        /// The tab the newest snapshot or patch carrying Stacks says.
        [[nodiscard]] std::string tab() const
        {
            std::string seen;
            for (const auto& m : received) {
                if (m.at("type") == "snapshot") {
                    seen = m.at("state").at("stacks").at("focusedStackId");
                } else if (m.at("type") == "patch" && m.at("sections").contains("stacks")) {
                    seen = m.at("sections").at("stacks").at("focusedStackId");
                }
            }
            return seen;
        }
    };
    const auto open = [&fx](Device& d, const QString& query = {}) {
        QObject::connect(&d.socket, &QWebSocket::textMessageReceived,
                         [&d](const QString& text) {
                             d.received.push_back(nlohmann::json::parse(text.toStdString()));
                         });
        d.socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws%2").arg(fx.server.port()).arg(query)));
        return wait_until([&d] { return d.received.size() >= 2; });
    };
    Device phone;
    Device tablet;
    REQUIRE(open(phone));
    REQUIRE(open(tablet));
    CHECK(phone.tab() == "console");
    CHECK(tablet.tab() == "console");

    phone.socket.sendTextMessage(QStringLiteral(
        R"({"type":"cmd","id":"c1","name":"setFocusedStackId","args":{"stackId":"lobby"}})"));
    REQUIRE(wait_until([&phone] { return phone.tab() == "lobby"; }));
    // Let any patch for the tablet arrive: it must not move the tablet.
    run_events_for(300);
    CHECK(tablet.tab() == "console");

    // A device that reconnects names its tab and gets it back.
    Device again;
    REQUIRE(open(again, QStringLiteral("?tab=lobby")));
    CHECK(again.tab() == "lobby");
}

TEST_CASE("RemoteServer: an unknown command is refused without reaching the bridge", "[remote][R3]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    QWebSocket client;
    std::vector<nlohmann::json> received;
    QObject::connect(&client, &QWebSocket::textMessageReceived,
                     [&received](const QString& text) {
                         received.push_back(nlohmann::json::parse(text.toStdString()));
                     });
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(fx.server.port())));
    REQUIRE(wait_until([&received] { return received.size() >= 2; }));
    received.clear();

    client.sendTextMessage(QStringLiteral(
        R"({"type":"cmd","id":"c9","name":"deleteLibrarySong","args":{"songId":1}})"));

    REQUIRE(wait_until([&received] { return !received.empty(); }));
    REQUIRE(received[0].at("type") == "ack");
    REQUIRE(received[0].at("ok") == false);
    REQUIRE(fx.bridge.invoked.empty());
}

TEST_CASE("RemoteServer: a PIN gates the API and the socket", "[remote][R4]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen(QStringLiteral("4321")));

    int status = 0;
    // Static assets stay open - the gate page is part of the app.
    http_get(fx.base() + "/", &status);
    REQUIRE(status == 200);

    // Operator data does not.
    http_get(fx.base() + "/api/library/songs", &status);
    REQUIRE(status == 401);

    QByteArray body = http_post_json(fx.base() + "/api/auth", R"({"pin":"0000"})", &status);
    REQUIRE(status == 401);

    body = http_post_json(fx.base() + "/api/auth", R"({"pin":"4321"})", &status);
    REQUIRE(status == 200);
    const auto token = nlohmann::json::parse(body.toStdString())
                           .at("token").get<std::string>();
    REQUIRE_FALSE(token.empty());

    http_get(fx.base() + "/api/library/songs", &status, QByteArray::fromStdString(token));
    REQUIRE(status == 200);

    // Browsers cannot set Authorization on a WebSocket constructor; the token
    // travels as a query parameter on the upgrade URL.
    QWebSocket client;
    std::vector<nlohmann::json> received;
    QObject::connect(&client, &QWebSocket::textMessageReceived,
                     [&received](const QString& text) {
                         received.push_back(nlohmann::json::parse(text.toStdString()));
                     });
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws?token=%2")
                         .arg(fx.server.port())
                         .arg(QString::fromStdString(token))));
    REQUIRE(wait_until([&received] { return received.size() >= 2; }));
    REQUIRE(received[0].at("type") == "hello");
    REQUIRE(received[1].at("type") == "snapshot");
}

TEST_CASE("RemoteServer: a socket closes when the token it opened with expires", "[remote][R4]")
{
    // The API refuses an expired token; an open socket must not outlive it,
    // or a device would keep the live state with no PIN for as long as it
    // stayed connected.
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen(QStringLiteral("4321"), std::chrono::seconds(2)));

    int status = 0;
    const QByteArray body =
        http_post_json(fx.base() + "/api/auth", R"({"pin":"4321"})", &status);
    REQUIRE(status == 200);
    const auto token = nlohmann::json::parse(body.toStdString()).at("token").get<std::string>();

    QWebSocket client;
    bool greeted = false;
    QObject::connect(&client, &QWebSocket::textMessageReceived, [&greeted](const QString& text) {
        greeted = greeted || nlohmann::json::parse(text.toStdString()).at("type") == "snapshot";
    });
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws?token=%2")
                         .arg(fx.server.port())
                         .arg(QString::fromStdString(token))));
    REQUIRE(wait_until([&greeted] { return greeted; }));
    CHECK(client.state() == QAbstractSocket::ConnectedState);

    REQUIRE(wait_until([&client] { return client.state() == QAbstractSocket::UnconnectedState; },
                       6000));
    // And the token is refused from here on, as the client finds when it
    // tries to reconnect.
    http_get(fx.base() + "/api/library/songs", &status, QByteArray::fromStdString(token));
    CHECK(status == 401);
}

TEST_CASE("RemoteServer: a signed-in socket stays open past its token once the PIN is removed",
          "[remote][R4]")
{
    // With no PIN everyone may connect, so a socket admitted under the old
    // PIN must not be dropped when that PIN's token would have expired.
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen(QStringLiteral("4321"), std::chrono::seconds(2)));

    int status = 0;
    const QByteArray body =
        http_post_json(fx.base() + "/api/auth", R"({"pin":"4321"})", &status);
    REQUIRE(status == 200);
    const auto token = nlohmann::json::parse(body.toStdString()).at("token").get<std::string>();

    QWebSocket client;
    bool greeted = false;
    QObject::connect(&client, &QWebSocket::textMessageReceived, [&greeted](const QString& text) {
        greeted = greeted || nlohmann::json::parse(text.toStdString()).at("type") == "snapshot";
    });
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws?token=%2")
                         .arg(fx.server.port())
                         .arg(QString::fromStdString(token))));
    REQUIRE(wait_until([&greeted] { return greeted; }));

    fx.server.set_pin({});
    // Past the token's two seconds, with margin.
    run_events_for(3500);
    CHECK(client.state() == QAbstractSocket::ConnectedState);
}

TEST_CASE("RemoteServer: with no PIN the library is readable", "[remote][R8]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    int status = 0;
    const QByteArray body = http_get(fx.base() + "/api/library/songs?q=abc&limit=5", &status);
    REQUIRE(status == 200);
    const auto parsed = nlohmann::json::parse(body.toStdString());
    REQUIRE(parsed.at("rows").is_array());
    REQUIRE(parsed.at("rows")[0].at("kind") == "songs");
}

TEST_CASE("RemoteServer: a search reaches the bridge as it was typed", "[remote][R8]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    // As a browser's form encoding writes it (URLSearchParams): a space is
    // "+", a "+" is "%2B"; and percent-encoded UTF-8.
    const auto query_of = [&fx](const char* encoded) {
        int status = 0;
        const QByteArray body =
            http_get(fx.base() + "/api/library/songs?q=" + encoded + "&limit=5", &status);
        REQUIRE(status == 200);
        return nlohmann::json::parse(body.toStdString()).at("rows")[0].at("query").get<std::string>();
    };
    CHECK(query_of("amazing+grace") == "amazing grace");
    CHECK(query_of("amazing%20grace") == "amazing grace");
    CHECK(query_of("c%2B%2B") == "c++");
    CHECK(query_of("%C3%89glise") == "\xC3\x89glise");
}

TEST_CASE("RemoteServer: wrong-typed or non-object frames are refused, not fatal", "[remote][R3]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    QWebSocket client;
    std::vector<nlohmann::json> received;
    QObject::connect(&client, &QWebSocket::textMessageReceived,
                     [&received](const QString& text) {
                         received.push_back(nlohmann::json::parse(text.toStdString()));
                     });
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(fx.server.port())));
    REQUIRE(wait_until([&received] { return received.size() >= 2; }));
    received.clear();

    // Each of these used to throw nlohmann::json::type_error out of the slot.
    client.sendTextMessage(QStringLiteral(R"({"type":"cmd","id":5,"name":"nextSlide"})"));
    client.sendTextMessage(QStringLiteral(R"({"type":7})"));
    client.sendTextMessage(QStringLiteral(R"({"type":"cmd","id":"c2","name":["nextSlide"]})"));
    client.sendTextMessage(QStringLiteral("[1,2,3]"));
    REQUIRE(wait_until([&received] { return received.size() >= 4; }));

    // The server is still serving: a well-formed command goes through.
    received.clear();
    client.sendTextMessage(
        QStringLiteral(R"({"type":"cmd","id":"ok","name":"nextSlide","args":{}})"));
    REQUIRE(wait_until([&received] {
        return std::any_of(received.begin(), received.end(), [](const nlohmann::json& m) {
            return m.at("type") == "ack" && m.at("id") == "ok" && m.at("ok") == true;
        });
    }));
}

TEST_CASE("RemoteServer: a malformed PIN body is refused before auth, not fatal", "[remote][R4]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen(QStringLiteral("0123")));

    int status = 0;
    for (const QByteArray& body : {QByteArray(R"({"pin":123})"), QByteArray("[]"),
                                  QByteArray("5"), QByteArray("{nope")}) {
        http_post_json(fx.base() + "/api/auth", body, &status);
        REQUIRE(status == 401);
    }
    // Still up, and the real PIN (leading zero intact) still works.
    http_post_json(fx.base() + "/api/auth", R"({"pin":"0123"})", &status);
    REQUIRE(status == 200);
}

TEST_CASE("RemoteServer: a message or frame over 64 KiB closes the socket", "[remote][limits]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    QWebSocket client;
    int snapshots = 0;
    QObject::connect(&client, &QWebSocket::textMessageReceived, [&snapshots](const QString& text) {
        if (nlohmann::json::parse(text.toStdString()).at("type") == "snapshot") {
            ++snapshots;
        }
    });
    SECTION("in one frame") { client.setOutgoingFrameSize(2 * 1024 * 1024); }
    SECTION("in frames under the cap") { client.setOutgoingFrameSize(16 * 1024); }
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/ws").arg(fx.server.port())));
    REQUIRE(wait_until([&snapshots] { return snapshots == 1; }));
    REQUIRE(fx.server.client_count() == 1);

    const auto padded = [](int bytes) {
        return QStringLiteral(R"({"type":"resync","pad":"%1"})").arg(QString(bytes, QLatin1Char('x')));
    };
    // Under the cap: answered.
    client.sendTextMessage(padded(60 * 1024));
    REQUIRE(wait_until([&snapshots] { return snapshots == 2; }));

    // Over it: closed. Just over, so the client has sent it all by then; even
    // so, a close while bytes are still arriving may reset the connection,
    // and a reset drops the close frame unread (macOS does). The code on the
    // wire is the next test's, read off a raw socket.
    client.sendTextMessage(padded(65 * 1024));
    REQUIRE(wait_until([&client] { return client.state() == QAbstractSocket::UnconnectedState; }));
    INFO("close code " << client.closeCode() << ", socket error " << client.error());
    CHECK((client.closeCode() == QWebSocketProtocol::CloseCodeTooMuchData
           || client.error() == QAbstractSocket::RemoteHostClosedError));
    CHECK(wait_until([&fx] { return fx.server.client_count() == 0; }));
    CHECK(snapshots == 2);
}

namespace {

/// The server's close code, from the frames it sent: 0 until a close frame
/// arrives. The server's frames are not masked.
int close_code_in(const QByteArray& frames)
{
    qsizetype at = 0;
    while (at + 2 <= frames.size()) {
        const auto opcode = static_cast<quint8>(frames[at]) & 0x0F;
        quint64 length = static_cast<quint8>(frames[at + 1]) & 0x7F;
        qsizetype header = 2;
        const int extra = length == 126 ? 2 : length == 127 ? 8 : 0;
        if (at + header + extra > frames.size()) {
            return 0;
        }
        if (extra > 0) {
            length = 0;
            for (int i = 0; i < extra; ++i) {
                length = (length << 8) | static_cast<quint8>(frames[at + header + i]);
            }
            header += extra;
        }
        if (at + header + static_cast<qsizetype>(length) > frames.size()) {
            return 0;
        }
        if (opcode == 0x8 && length >= 2) {
            return (static_cast<quint8>(frames[at + header]) << 8)
                   | static_cast<quint8>(frames[at + header + 1]);
        }
        at += header + static_cast<qsizetype>(length);
    }
    return 0;
}

}  // namespace

TEST_CASE("RemoteServer: a frame announced over 64 KiB closes the socket before it is read",
          "[remote][limits]")
{
    // The message cap alone reads the whole frame first. A frame whose header
    // says 1 MiB, of which a little is sent, is refused at the header.
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, fx.server.port());
    REQUIRE(raw.waitForConnected(4000));
    const QByteArray host = "127.0.0.1:" + QByteArray::number(fx.server.port());
    raw.write("GET /ws HTTP/1.1\r\nHost: " + host + "\r\nOrigin: http://" + host
              + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n");
    QByteArray in;
    QObject::connect(&raw, &QTcpSocket::readyRead, [&raw, &in] { in += raw.readAll(); });
    REQUIRE(wait_until([&in] { return in.contains("\r\n\r\n"); }));
    REQUIRE(in.startsWith("HTTP/1.1 101"));
    REQUIRE(wait_until([&fx] { return fx.server.client_count() == 1; }));
    in.remove(0, in.indexOf("\r\n\r\n") + 4);

    // A final text frame of 1 MiB, masked as a client's are, and 1 KiB of it.
    QByteArray frame("\x81\xff", 2);
    for (int shift = 56; shift >= 0; shift -= 8) {
        frame += static_cast<char>((quint64{1024 * 1024} >> shift) & 0xFF);
    }
    frame += QByteArray(4, '\0');  // a mask of zeros leaves the payload as it is
    frame += QByteArray(1024, 'x');
    raw.write(frame);

    // Answered at once (QWebSocket waits 5 s for the rest of a frame).
    QElapsedTimer clock;
    clock.start();
    REQUIRE(wait_until([&in] { return close_code_in(in) != 0; }, 3000));
    CHECK(close_code_in(in) == QWebSocketProtocol::CloseCodeTooMuchData);
    CHECK(clock.elapsed() < 3000);
    CHECK(wait_until([&fx] { return fx.server.client_count() == 0; }));
}

TEST_CASE("RemoteServer: a sign-in body over 64 KiB is answered 413", "[remote][limits]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen(QStringLiteral("2468")));

    int status = 0;
    const QByteArray big = R"({"pin":"2468","pad":")" + QByteArray(1024 * 1024, 'x') + R"("})";
    http_post_json(fx.base() + "/api/auth", big, &status);
    CHECK(status == 413);
    // Not a wrong PIN, and the server still signs a device in.
    http_post_json(fx.base() + "/api/auth", R"({"pin":"2468"})", &status);
    CHECK(status == 200);
}

TEST_CASE("RemoteServer: a failing route answers 500 and keeps serving", "[remote][R8]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    REQUIRE(fx.listen());

    fx.bridge.library_throws = true;
    int status = 0;
    const QByteArray body = http_get(fx.base() + "/api/library/songs", &status);
    REQUIRE(status == 500);
    REQUIRE(nlohmann::json::parse(body.toStdString()).at("error") == "internal error");

    fx.bridge.library_throws = false;
    http_get(fx.base() + "/api/library/songs", &status);
    REQUIRE(status == 200);
}

namespace {

/// An encode the test releases, recording where it ran.
struct HeldEncode {
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::atomic<int> runs = 0;
    std::atomic<bool> off_main_thread = false;
    QThread* main_thread = QThread::currentThread();

    PendingThumb pending()
    {
        return {[this] {
            ++runs;
            off_main_thread = QThread::currentThread() != main_thread;
            released.wait_for(std::chrono::seconds(5));
            ThumbResult out;
            out.ok = true;
            out.mime = "image/png";
            out.bytes = "encoded-pixels";
            return out;
        }};
    }
};

struct PendingGet {
    QNetworkAccessManager nam;
    QNetworkReply* reply = nullptr;

    explicit PendingGet(const QString& url) { reply = nam.get(QNetworkRequest{QUrl(url)}); }
    bool finish() { return wait_until([this] { return reply->isFinished(); }); }
    [[nodiscard]] int status() const
    {
        return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }
};

}  // namespace

TEST_CASE("RemoteServer encodes a thumbnail off the GUI thread and keeps serving", "[remote][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    HeldEncode held;
    fx.bridge.thumbs = [&](const ThumbRequest&) { return held.pending(); };
    REQUIRE(fx.listen());

    PendingGet thumb(fx.base() + "/api/preview/Main?w=320&r=7");
    REQUIRE(wait_until([&] { return held.runs == 1; }));

    // The encode is still held, and the server answers other requests.
    int status = 0;
    (void)http_get(fx.base() + "/api/hello", &status);
    CHECK(status == 200);
    CHECK(!thumb.reply->isFinished());

    held.release.set_value();
    REQUIRE(thumb.finish());
    CHECK(thumb.status() == 200);
    CHECK(thumb.reply->header(QNetworkRequest::ContentTypeHeader).toString() == "image/png");
    // Behind the PIN: a browser may keep it, a shared cache may not.
    CHECK(thumb.reply->rawHeader("Cache-Control") == "private, max-age=31536000, immutable");
    CHECK(thumb.reply->readAll() == "encoded-pixels");
    CHECK(held.off_main_thread);
}

TEST_CASE("RemoteServer shares one encode between requests for the same thumbnail", "[remote][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    HeldEncode held;
    fx.bridge.thumbs = [&](const ThumbRequest&) { return held.pending(); };
    REQUIRE(fx.listen());

    const QString url = fx.base() + "/api/preview/Main?w=640&r=3";
    PendingGet first(url);
    REQUIRE(wait_until([&] { return held.runs == 1; }));
    PendingGet second(url);
    // Let the second request reach the server while the encode is held.
    run_events_for(200);
    held.release.set_value();
    REQUIRE(first.finish());
    REQUIRE(second.finish());
    CHECK(first.status() == 200);
    CHECK(second.status() == 200);
    CHECK(first.reply->readAll() == "encoded-pixels");
    CHECK(second.reply->readAll() == "encoded-pixels");

    // And the result was cached: a third request neither renders nor encodes.
    int status = 0;
    CHECK(http_get(url, &status) == "encoded-pixels");
    CHECK(status == 200);
    CHECK(held.runs == 1);
    CHECK(fx.bridge.thumb_renders == 1);
}

TEST_CASE("RemoteServer answers a failed thumbnail encode with 404", "[remote][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    fx.bridge.thumbs = [](const ThumbRequest&) {
        return PendingThumb{[]() -> ThumbResult { throw std::runtime_error("bad pixels"); }};
    };
    REQUIRE(fx.listen());
    int status = 0;
    (void)http_get(fx.base() + "/api/preview/Main?w=320&r=1", &status);
    CHECK(status == 404);
    // No thumbnail at all is a 404 too.
    fx.bridge.thumbs = nullptr;
    (void)http_get(fx.base() + "/api/preview/Main?w=320&r=2", &status);
    CHECK(status == 404);
}

TEST_CASE("RemoteServer recovers when rendering a thumbnail throws", "[remote][thumbs]")
{
    // A render that throws answers 500, and must leave nothing behind that a
    // later request for the same thumbnail would wait on.
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    bool fail = true;
    fx.bridge.thumbs = [&](const ThumbRequest&) -> PendingThumb {
        if (fail) {
            throw std::runtime_error("render failed");
        }
        return {[] {
            ThumbResult out;
            out.ok = true;
            out.mime = "image/png";
            out.bytes = "second-try";
            return out;
        }};
    };
    REQUIRE(fx.listen());
    const QString url = fx.base() + "/api/preview/Main?w=320&r=5";
    int status = 0;
    (void)http_get(url, &status);
    CHECK(status == 500);

    fail = false;
    status = 0;
    CHECK(http_get(url, &status) == "second-try");
    CHECK(status == 200);
}

TEST_CASE("RemoteServer stops cleanly with a thumbnail encode in flight", "[remote][thumbs]")
{
    // stop() drops the waiting requests and the cache. The encode still
    // running must not answer a dead request or refill the cache, and the
    // restarted server encodes afresh.
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    HeldEncode held;
    fx.bridge.thumbs = [&](const ThumbRequest&) { return held.pending(); };
    REQUIRE(fx.listen());
    {
        PendingGet thumb(fx.base() + "/api/preview/Main?w=320&r=9");
        REQUIRE(wait_until([&] { return held.runs == 1; }));
        fx.server.stop();
        held.release.set_value();
        // Let the finished encode's result reach the stopped server.
        run_events_for(200);
    }

    REQUIRE(fx.listen());
    int status = 0;
    CHECK(http_get(fx.base() + "/api/preview/Main?w=320&r=9", &status) == "encoded-pixels");
    CHECK(status == 200);
    CHECK(held.runs == 2);
}

namespace {

PendingThumb ready_thumb(const ThumbRequest&)
{
    return {[] {
        ThumbResult out;
        out.ok = true;
        out.mime = "image/png";
        out.bytes = "pixels";
        return out;
    }};
}

}  // namespace

TEST_CASE("RemoteServer draws a burst of new thumbnails at once and the rest in turn",
          "[remote][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    fx.bridge.thumbs = ready_thumb;
    RemoteServer::Config cfg;
    cfg.render_burst = 2;
    cfg.render_interval = std::chrono::milliseconds(100);
    REQUIRE(fx.start(cfg));

    QElapsedTimer clock;
    clock.start();
    std::vector<std::unique_ptr<PendingGet>> gets;
    for (int r = 0; r < 5; ++r) {
        gets.push_back(std::make_unique<PendingGet>(
            fx.base() + QStringLiteral("/api/preview/Main?w=320&r=%1").arg(r)));
    }
    for (const auto& get : gets) {
        REQUIRE(get->finish());
        CHECK(get->status() == 200);
    }
    CHECK(fx.bridge.thumb_renders == 5);
    // Two at once, then one each 100 ms: the fifth is drawn 300 ms after the
    // server starts at the soonest.
    CHECK(clock.elapsed() >= 250);
}

TEST_CASE("RemoteServer draws no new revision past its budget, and answers 503 past the queue",
          "[remote][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    fx.bridge.thumbs = ready_thumb;
    RemoteServer::Config cfg;
    cfg.render_burst = 1;
    cfg.render_interval = std::chrono::hours(1);
    cfg.max_waiting_renders = 2;
    REQUIRE(fx.start(cfg));

    const auto url = [&fx](int r) {
        return fx.base() + QStringLiteral("/api/freeform-thumb/7?w=320&r=%1").arg(r);
    };
    int status = 0;
    CHECK(http_get(url(0), &status) == "pixels");
    CHECK(status == 200);
    CHECK(fx.bridge.thumb_renders == 1);

    // Revisions asked for past the budget: two wait, the third is turned away.
    std::vector<std::unique_ptr<PendingGet>> gets;
    for (int r = 1; r <= 3; ++r) {
        gets.push_back(std::make_unique<PendingGet>(url(r)));
    }
    const auto finished = [&gets] {
        return std::count_if(gets.begin(), gets.end(),
                             [](const auto& get) { return get->reply->isFinished(); });
    };
    REQUIRE(wait_until([&finished] { return finished() == 1; }));
    run_events_for(200);
    CHECK(finished() == 1);
    for (const auto& get : gets) {
        if (get->reply->isFinished()) {
            CHECK(get->status() == 503);
            CHECK(get->reply->rawHeader("Retry-After") == "1");
        }
    }
    CHECK(fx.bridge.thumb_renders == 1);

    // What is cached is still served.
    CHECK(http_get(url(0), &status) == "pixels");
    CHECK(status == 200);
}

TEST_CASE("RemoteServer draws waiting thumbnails first asked first drawn", "[remote][thumbs]")
{
    // One draw a second. The first is drawn at once and the second waits;
    // a third asked once the budget has earned a draw, but before the queue's
    // timer has run, is drawn after the second, not ahead of it.
    seventhslide::test::ensure_qt_application();
    std::vector<int> drawn;
    Fixture fx;
    fx.bridge.thumbs = [&drawn](const ThumbRequest& r) {
        drawn.push_back(r.slide_index);
        return ready_thumb(r);
    };
    RemoteServer::Config cfg;
    cfg.render_burst = 1;
    cfg.render_interval = std::chrono::milliseconds(1000);
    QElapsedTimer clock;
    clock.start();
    REQUIRE(fx.start(cfg));

    const auto slide = [&fx](int i) {
        return std::make_unique<PendingGet>(
            fx.base() + QStringLiteral("/api/preview/Main?w=320&slide=%1&r=%1").arg(i));
    };
    std::vector<std::unique_ptr<PendingGet>> gets;
    gets.push_back(slide(0));
    REQUIRE(wait_until([&drawn] { return drawn.size() == 1; }));
    // The budget earns its next draw a second from the start; the second
    // slide's timer runs a second after it is asked for. Asked at 0.6 s, it
    // leaves half a second between the two for the third, which a busy
    // computer's event loop can be late into.
    wait_until([&clock] { return clock.elapsed() >= 600; }, 2000);
    gets.push_back(slide(1));  // waits; its timer runs at about 1.6 s
    wait_until([&clock] { return clock.elapsed() >= 1100; }, 2000);
    INFO("the third asked at " << clock.elapsed() << " ms");
    REQUIRE(drawn.size() == 1);
    gets.push_back(slide(2));
    for (const auto& get : gets) {
        REQUIRE(get->finish());
        CHECK(get->status() == 200);
    }
    CHECK(drawn == std::vector<int>{0, 1, 2});
}

TEST_CASE("RemoteServer serves a large deck's gallery of new thumbnails, none turned away",
          "[remote][thumbs]")
{
    // A tablet showing a deck of 130 slides asks for them all at once.
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    fx.bridge.thumbs = ready_thumb;
    REQUIRE(fx.start(RemoteServer::Config{}));

    std::vector<std::unique_ptr<PendingGet>> gets;
    for (int i = 0; i < 130; ++i) {
        gets.push_back(std::make_unique<PendingGet>(
            fx.base() + QStringLiteral("/api/preview/Main?w=320&slide=%1&r=1").arg(i)));
    }
    int served = 0;
    for (const auto& get : gets) {
        REQUIRE(get->finish());
        served += get->status() == 200 ? 1 : 0;
    }
    CHECK(served == 130);
    CHECK(fx.bridge.thumb_renders == 130);
}

TEST_CASE("RemoteServer answers a waiting thumbnail the bridge cannot draw",
          "[remote][thumbs]")
{
    // Slide 1's render throws (500) and slide 2 has no thumbnail (404), both
    // drawn after waiting their turn behind slide 0. Neither answer is kept:
    // asked again, each is drawn again.
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    bool fail = true;
    fx.bridge.thumbs = [&fail](const ThumbRequest& r) -> PendingThumb {
        if (fail && r.slide_index == 1) {
            throw std::runtime_error("render failed");
        }
        if (fail && r.slide_index == 2) {
            return {};
        }
        return ready_thumb(r);
    };
    RemoteServer::Config cfg;
    cfg.render_burst = 1;
    cfg.render_interval = std::chrono::milliseconds(500);
    REQUIRE(fx.start(cfg));

    const auto url = [&fx](int i) {
        return fx.base() + QStringLiteral("/api/preview/Main?w=320&slide=%1&r=1").arg(i);
    };
    int status = 0;
    CHECK(http_get(url(0), &status) == "pixels");
    PendingGet throws(url(1));
    PendingGet missing(url(2));
    REQUIRE(throws.finish());
    REQUIRE(missing.finish());
    CHECK(throws.status() == 500);
    CHECK(missing.status() == 404);
    CHECK(fx.bridge.thumb_renders == 3);

    fail = false;
    CHECK(http_get(url(1), &status) == "pixels");
    CHECK(status == 200);
    CHECK(http_get(url(2), &status) == "pixels");
    CHECK(status == 200);
}

TEST_CASE("RemoteServer draws thumbnails with a burst of none", "[remote][thumbs]")
{
    // Taken as a burst of one: a budget of nothing would leave every miss
    // waiting for ever.
    seventhslide::test::ensure_qt_application();
    Fixture fx;
    fx.bridge.thumbs = ready_thumb;
    RemoteServer::Config cfg;
    cfg.render_burst = 0;
    cfg.render_interval = std::chrono::milliseconds(10);
    REQUIRE(fx.start(cfg));

    std::vector<std::unique_ptr<PendingGet>> gets;
    for (int r = 0; r < 3; ++r) {
        gets.push_back(std::make_unique<PendingGet>(
            fx.base() + QStringLiteral("/api/freeform-thumb/7?w=320&r=%1").arg(r)));
    }
    for (const auto& get : gets) {
        REQUIRE(get->finish());
        CHECK(get->status() == 200);
    }
}
