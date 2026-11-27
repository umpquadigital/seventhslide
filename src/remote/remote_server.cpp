#include <seventhslide/remote/remote_server.hpp>

#include <seventhslide/remote/remote_http_util.hpp>
#include <seventhslide/remote/remote_protocol.hpp>
#include <seventhslide/util/find.hpp>

#include <QDateTime>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QUrlQuery>
#include <QWebSocket>

#include <QtLogging>

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <utility>
#include <vector>

namespace seventhslide::remote {

namespace {

/// A connection's address and port, which its upgrade request and the
/// socket made from it share.
QString peer_key(const QHostAddress& address, quint16 port)
{
    return address.toString() + QLatin1Char('#') + QString::number(port);
}

/// Patch coalescing window. One service open marks six sections and fires
/// sessionChanged several times; this turns that into a single broadcast.
constexpr int k_flush_interval_ms = 50;

/// The largest message, and frame, a client may send. The bundled client's
/// largest is a few hundred bytes; the largest command any client may send
/// is an int array with a few bytes for each row of a service. A frame or
/// message over this closes the socket (CloseCodeTooMuchData) unread; Qt's
/// own limit is 2 GiB.
constexpr quint64 k_max_client_message = 64 * 1024;

/// Run a slot body that reads the bridge. Slots are called from the event
/// loop, where an escaping exception terminates the process, so a failure is
/// logged and that one delivery is skipped instead.
template <typename Fn>
void guarded(const char* what, Fn&& fn) noexcept
{
    try {
        fn();
    } catch (const std::exception& ex) {
        qWarning("SeventhSlide remote: %s failed: %s", what, ex.what());
    } catch (...) {
        qWarning("SeventhSlide remote: %s failed", what);
    }
}

} // namespace

RemoteServer::RemoteServer(IRemoteBridge& bridge, QObject* parent)
    : QObject(parent)
    , bridge_(bridge)
{
    boot_ = QString::number(QRandomGenerator::system()->generate64(), 16).toStdString();
    flush_timer_.setSingleShot(true);
    flush_timer_.setInterval(k_flush_interval_ms);
    // Two at a time: a phone and a tablet asking for a new frame's preview.
    encode_pool_.setMaxThreadCount(2);
    connect(&flush_timer_, &QTimer::timeout, this, &RemoteServer::flush_dirty);
    connect(&render_timer_, &QTimer::timeout, this, &RemoteServer::render_waiting);
}

RemoteServer::~RemoteServer()
{
    stop();
}

bool RemoteServer::start(const Config& cfg)
{
    stop();
    cfg_ = cfg;
    last_error_.clear();
    auth_.set_pin(cfg.pin);
    auth_.set_token_ttl(cfg.token_ttl);
    cfg_.render_burst = std::max(cfg_.render_burst, 1);
    cfg_.render_interval = std::max(cfg_.render_interval, std::chrono::milliseconds{1});
    render_tokens_ = cfg_.render_burst;
    render_refilled_ = std::chrono::steady_clock::now();
    render_timer_.setInterval(cfg_.render_interval);

    http_ = std::make_unique<QHttpServer>();
    register_routes();

    http_->addWebSocketUpgradeVerifier(
        http_.get(), [this](const QHttpServerRequest& request) {
            if (request.url().path() != QLatin1String("/ws")) {
                return QHttpServerWebSocketUpgradeResponse::passToNext();
            }
            if (!peer_allowed(request) || !http::same_origin(request)) {
                return QHttpServerWebSocketUpgradeResponse::deny();
            }
            // The upgrade is the authentication step: an accepted socket is
            // already authorised, until its token expires.
            if (!auth_.open_access()) {
                const QString token = http::token_from(request);
                if (!auth_.valid_token(token)) {
                    return QHttpServerWebSocketUpgradeResponse::deny();
                }
                admitted_tokens_.insert(
                    peer_key(request.remoteAddress(), request.remotePort()), token);
            }
            return QHttpServerWebSocketUpgradeResponse::accept();
        });

    connect(http_.get(), &QHttpServer::newWebSocketConnection, this,
            &RemoteServer::on_new_web_socket_connection);

    auto tcp = std::make_unique<QTcpServer>();
    if (!tcp->listen(QHostAddress::Any, cfg.port)) {
        last_error_ = tr("Could not listen on port %1: %2")
                          .arg(cfg.port)
                          .arg(tcp->errorString());
        http_.reset();
        return false;
    }
    cfg_.port = tcp->serverPort();
    // Out of open files (EMFILE), QTcpServer pauses accepting for good: the
    // remote would stop answering until the app restarts. Try again in a
    // second (the timer goes with the server).
    QTcpServer* listener = tcp.get();
    connect(listener, &QTcpServer::acceptError, listener, [listener](QAbstractSocket::SocketError) {
        QTimer::singleShot(1000, listener, [listener] { listener->resumeAccepting(); });
    });
    tcp_ = tcp.get();
    if (!http_->bind(tcp.release())) {
        last_error_ = tr("Could not bind the HTTP server to port %1.").arg(cfg_.port);
        tcp_ = nullptr;
        http_.reset();
        return false;
    }

    running_ = true;
    revision_ = 0;
    Q_EMIT runningChanged();
    return true;
}

void RemoteServer::stop()
{
    flush_timer_.stop();
    dirty_.clear();
    for (Client& c : clients_) {
        if (c.socket != nullptr) {
            c.socket->disconnect(this);
            c.socket->close();
            c.socket->deleteLater();
        }
    }
    clients_.clear();
    admitted_tokens_.clear();
    thumbs_.clear();
    waiting_.clear();  // their connections are gone with http_
    render_queue_.clear();
    render_timer_.stop();
    ++thumb_generation_;
    http_.reset();
    tcp_ = nullptr;
    if (running_) {
        running_ = false;
        Q_EMIT runningChanged();
    }
}

int RemoteServer::client_count() const
{
    return static_cast<int>(std::count_if(
        clients_.begin(), clients_.end(),
        [](const Client& c) { return c.socket != nullptr; }));
}

void RemoteServer::set_pin(const QString& pin)
{
    cfg_.pin = pin;
    auth_.set_pin(pin);
    admitted_tokens_.clear();
    // Tokens are gone; drop anyone the new PIN no longer admits.
    if (!auth_.open_access()) {
        for (Client& c : clients_) {
            if (c.socket != nullptr) {
                c.socket->close();
            }
        }
    }
}

void RemoteServer::mark_dirty(Section s)
{
    if (!running_ || clients_.empty()) {
        return;
    }
    dirty_.insert(static_cast<int>(s), true);
    if (!flush_timer_.isActive()) {
        flush_timer_.start();
    }
}

void RemoteServer::mark_all_dirty()
{
    for (const Section s : all_sections()) {
        mark_dirty(s);
    }
}

void RemoteServer::flush_now()
{
    flush_timer_.stop();
    flush_dirty();
}

void RemoteServer::flush_dirty()
{
    if (!running_ || dirty_.isEmpty()) {
        return;
    }
    guarded("patch", [this] { broadcast_dirty(); });
    dirty_.clear();
}

void RemoteServer::broadcast_dirty()
{
    // Each client sees the sections through its own tab, so each gets its
    // own patch: what changed for it since it was last sent. Clients on the
    // same tab share one read of each section, and its hash.
    struct Read {
        nlohmann::json body;
        std::size_t hash = 0;
    };
    std::map<std::pair<int, std::string>, Read> read;
    bool sent = false;
    for (Client& c : clients_) {
        if (c.socket == nullptr || !c.greeted) {
            continue;
        }
        bridge_.settle(c.view);
        // Everything that can throw (reading the bridge) happens before this
        // client's hashes are touched: a section only counts as sent once the
        // frame carrying it has been built.
        nlohmann::json sections = nlohmann::json::object();
        std::vector<std::pair<int, std::size_t>> sent_hashes;
        for (const Section s : all_sections()) {
            const int key = static_cast<int>(s);
            if (!dirty_.value(key, false)) {
                continue;
            }
            auto [it, fresh] = read.try_emplace({key, c.view.stack});
            if (fresh) {
                it->second.body = bridge_.section(s, c.view);
                it->second.hash = std::hash<std::string>{}(it->second.body.dump());
            }
            const std::size_t hash = it->second.hash;
            if (c.last_hash.value(key, 0) == hash) {
                continue;  // marked, but identical to what this client has
            }
            sent_hashes.emplace_back(key, hash);
            sections[std::string(section_name(s))] = it->second.body;
        }
        if (sections.empty()) {
            continue;
        }
        for (const auto& [key, hash] : sent_hashes) {
            c.last_hash.insert(key, hash);
        }
        if (!sent) {
            ++revision_;
            sent = true;
        }
        send_json(c.socket, nlohmann::json{
                                {"type", "patch"}, {"rev", revision_}, {"sections", std::move(sections)}});
    }
}

void RemoteServer::on_new_web_socket_connection()
{
    while (http_ && http_->hasPendingWebSocketConnections()) {
        std::unique_ptr<QWebSocket> owned = http_->nextPendingWebSocketConnection();
        if (!owned) {
            break;
        }
        QWebSocket* socket = owned.release();
        socket->setParent(this);
        socket->setMaxAllowedIncomingMessageSize(k_max_client_message);
        socket->setMaxAllowedIncomingFrameSize(k_max_client_message);

        connect(socket, &QWebSocket::textMessageReceived, this,
                [this, socket](const QString& text) { on_text_message(socket, text); });
        connect(socket, &QWebSocket::disconnected, this,
                [this, socket]() { drop_client(socket); });

        // A signed-in device's socket lasts as long as its token: when that
        // expires, the socket closes and the device asks for the PIN again,
        // as its next request would.
        if (const QString token =
                admitted_tokens_.take(peer_key(socket->peerAddress(), socket->peerPort()));
            !token.isEmpty()) {
            close_when_expired(socket, token);
        }

        // A device that reconnects names the tab it had (the client keeps it
        // across a dropped connection or a reload); a new one starts on the
        // console's.
        Client client{socket, /*greeted=*/false};
        client.view.stack =
            QUrlQuery(socket->requestUrl()).queryItemValue(QStringLiteral("tab")).toStdString();
        clients_.push_back(std::move(client));
        send_json(socket, protocol::hello_frame(boot_));
        if (Client* c = find(socket)) {
            send_snapshot(*c);
        }
    }
}

void RemoteServer::close_when_expired(QWebSocket* socket, const QString& token)
{
    // With no PIN now, every device may stay.
    if (auth_.open_access()) {
        return;
    }
    // A valid token's expiry is known.
    const auto expiry = auth_.valid_token(token) ? auth_.token_expiry(token) : std::nullopt;
    if (!expiry) {
        socket->close();
        return;
    }
    const qint64 left = std::max<qint64>(0, QDateTime::currentDateTimeUtc().msecsTo(*expiry));
    // Precise, and looked at again on waking: a coarse timer may fire up to
    // 5% early, half an hour before a 12-hour token runs out.
    QTimer::singleShot(std::chrono::milliseconds(left + 1), Qt::PreciseTimer, socket,
                       [this, socket, token] { close_when_expired(socket, token); });
}

RemoteServer::Client* RemoteServer::find(QWebSocket* socket)
{
    return util::find_by(clients_, socket, &Client::socket);
}

void RemoteServer::drop_client(QWebSocket* socket)
{
    const Client* client = find(socket);
    if (client == nullptr) {
        return;
    }
    clients_.erase(clients_.begin() + (client - clients_.data()));
    socket->deleteLater();
}

void RemoteServer::send_snapshot(Client& c)
{
    c.greeted = true;
    guarded("snapshot", [this, &c] {
        bridge_.settle(c.view);
        const nlohmann::json frame = protocol::snapshot_frame(bridge_, c.view, revision_);
        // Re-baseline this client's diff table: everything in a snapshot is
        // now known-sent to it.
        c.last_hash.clear();
        if (const auto it = frame.find("state"); it != frame.end() && it->is_object()) {
            for (const Section s : all_sections()) {
                const auto body = it->find(std::string(section_name(s)));
                if (body != it->end()) {
                    c.last_hash.insert(static_cast<int>(s),
                                       std::hash<std::string>{}(body->dump()));
                }
            }
        }
        send_json(c.socket, frame);
    });
}

void RemoteServer::send_json(QWebSocket* socket, const nlohmann::json& msg)
{
    if (socket == nullptr) {
        return;
    }
    socket->sendTextMessage(QString::fromStdString(msg.dump()));
}

void RemoteServer::on_text_message(QWebSocket* socket, const QString& text)
{
    Client* client = find(socket);
    if (client == nullptr) {
        return;
    }

    const protocol::DecodedMessage decoded = protocol::decode_client_message(text.toStdString());
    if (!decoded.message) {
        send_json(socket, protocol::error_frame(decoded.error));
        return;
    }
    const protocol::ClientMessage& msg = *decoded.message;
    if (msg.type == "resync") {
        send_snapshot(*client);
        return;
    }
    if (msg.type != "cmd") {
        send_json(socket, protocol::error_frame("unsupported message type"));
        return;
    }

    // On a copy of the view: the command runs the session, and the client
    // list may change before it returns.
    bridge_.settle(client->view);
    View view = client->view;
    const protocol::Dispatch outcome = protocol::dispatch(bridge_, msg.name, msg.args, view);
    if (Client* still = find(socket)) {
        still->view = std::move(view);
    }
    send_json(socket, protocol::ack_frame(msg.id, outcome.result));
    for (const Section s : outcome.dirty) {
        mark_dirty(s);
    }
}

} // namespace seventhslide::remote
