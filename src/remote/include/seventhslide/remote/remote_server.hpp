#pragma once

// RemoteServer - HTTP + WebSocket transport for the web remote.
//
// One listening socket serves everything: the embedded web app, the thumbnail
// and library REST routes, and the WebSocket the operator state is pushed over.
// QAbstractHttpServer performs the upgrade on the same port, so the operator
// hands out a single URL.
//
// Everything that reads the session runs on the GUI thread. Session and
// SessionFacade have no locking, and a thumbnail shows live session state.
// The one exception is making a prepared thumbnail - painting or decoding it
// and encoding the result - which reads nothing else: it runs on
// encode_pool_, and the request is answered when it finishes.

#include <seventhslide/model/machine_settings.hpp>
#include <seventhslide/remote/remote_auth.hpp>
#include <seventhslide/remote/remote_bridge.hpp>
#include <seventhslide/remote/remote_thumbs.hpp>

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QPromise>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QtGlobal>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <QHttpServerResponse>

QT_BEGIN_NAMESPACE
class QHttpServer;
class QHttpServerRequest;
class QTcpServer;
class QWebSocket;
QT_END_NAMESPACE

namespace seventhslide::remote {

class RemoteServer final : public QObject {
    Q_OBJECT

public:
    struct Config {
        quint16 port = static_cast<quint16>(model::k_default_remote_port);
        QString pin;
        /// Refuse clients outside RFC1918 / loopback / link-local. A church
        /// projection system should not answer the open internet because a
        /// router happened to forward a port.
        bool private_networks_only = true;
        /// How long a device stays signed in after entering the PIN. When
        /// its token expires its socket closes too, and the PIN is asked
        /// for again.
        std::chrono::seconds token_ttl = Auth::k_token_ttl;
        /// Thumbnails the bridge draws on the GUI thread (an output's
        /// picture, a Freeform item's) that cache misses may draw at once,
        /// and how often they may draw one more after those, so asking for
        /// new revisions cannot keep the GUI thread drawing. The rest wait
        /// their turn; beyond max_waiting_renders waiting a miss is answered
        /// 503. The rate is what spares the GUI thread; the queue's cap only
        /// bounds its memory, and is above a large deck's gallery.
        /// render_burst is at least 1, render_interval at least 1 ms.
        int render_burst = 32;
        std::chrono::milliseconds render_interval{25};
        std::size_t max_waiting_renders = 512;
    };

    explicit RemoteServer(IRemoteBridge& bridge, QObject* parent = nullptr);
    ~RemoteServer() override;

    /// Bind and begin serving. Returns false and sets last_error() on failure -
    /// most often the port already being in use.
    bool start(const Config& cfg);
    void stop();

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] quint16 port() const noexcept { return cfg_.port; }
    [[nodiscard]] int client_count() const;
    [[nodiscard]] QString last_error() const { return last_error_; }

    /// Update the PIN without restarting. Clearing it opens access; changing it
    /// invalidates every existing token.
    void set_pin(const QString& pin);

    /// Note that a section changed. Patches are coalesced and flushed on a
    /// short timer, so callers may mark freely; a single service open marking
    /// six sections still produces one broadcast.
    void mark_dirty(Section s);
    void mark_all_dirty();
    /// Send any pending patch immediately. Used so a line-cursor move reaches
    /// the remote before a long ``present_frames`` blocks the event loop.
    void flush_now();

Q_SIGNALS:
    void runningChanged();

private:
    struct Client {
        QWebSocket* socket = nullptr;
        /// Has had its first snapshot; only greeted clients get patches.
        bool greeted = false;
        /// This device's own stack tab (see IRemoteBridge).
        View view;
        /// Hash of each section as this client last got it, so a
        /// marked-but-unchanged section costs nothing on the wire. The
        /// facade has no fine-grained change signals, so a desktop-side
        /// edit marks everything dirty; this is what keeps the patch small
        /// anyway. Per client: two devices on different tabs see different
        /// sections.
        QHash<int, std::size_t> last_hash;
    };

    /// A thumbnail missing from the cache; its requests wait in waiting_
    /// under ``key``.
    struct ThumbDraw {
        std::string key;
        std::string revision;
        ThumbRequest request;
    };

    void on_new_web_socket_connection();
    void on_text_message(QWebSocket* socket, const QString& text);
    void drop_client(QWebSocket* socket);
    /// Close ``socket`` once ``token``, which admitted it, is no longer
    /// valid: at once if it already is not, else when it expires. Not while
    /// no PIN is set: then every device may stay.
    void close_when_expired(QWebSocket* socket, const QString& token);

    void flush_dirty();
    void broadcast_dirty();
    void send_snapshot(Client& c);
    void send_json(QWebSocket* socket, const nlohmann::json& msg);

    [[nodiscard]] Client* find(QWebSocket* socket);

    void register_routes();
    void register_asset_routes();
    void register_api_routes();
    /// index.html with the bridge's glyph sprite where its marker is; made
    /// once, on the first request.
    [[nodiscard]] QHttpServerResponse index_page();
    /// True when the request comes from a network the remote serves
    /// (Config::private_networks_only), sent to this computer by a name of
    /// its own (``http::host_is_local``). Every route checks it.
    [[nodiscard]] bool peer_allowed(const QHttpServerRequest& request) const;
    /// True when this request may read operator data.
    [[nodiscard]] bool authorize(const QHttpServerRequest& request);
    /// A cached thumbnail at once; otherwise its pixels are rendered here
    /// (an output's picture and a Freeform item's in turn, render_waiting)
    /// and encoded on encode_pool_, and the response follows.
    [[nodiscard]] QFuture<QHttpServerResponse> serve_thumb(const QHttpServerRequest& request,
                                                          const ThumbRequest& r);
    /// Encode ``pending`` on encode_pool_; finish_encode gets the result.
    void start_encode(const std::string& key, const std::string& revision, PendingThumb pending);
    /// Back on the GUI thread: cache ``thumb`` and answer everyone waiting
    /// for ``key``.
    void finish_encode(const std::string& key,
                      const std::string& revision,
                      std::uint64_t generation,
                      const ThumbResult& thumb);
    /// Answer everyone waiting for ``key`` with ``response()``.
    void answer_waiting(const std::string& key,
                        const std::function<QHttpServerResponse()>& response);
    /// Take one draw from the render budget (Config::render_burst); false
    /// when it is spent.
    [[nodiscard]] bool take_render();
    /// Draw the misses waiting in render_queue_ that the budget allows now,
    /// first asked first.
    void render_waiting();
    /// Have the bridge draw ``job`` and start its encode, or answer its
    /// requests if there is nothing to encode or the bridge throws.
    void draw(const ThumbDraw& job);

    IRemoteBridge& bridge_;
    /// This run's name, sent in the hello frame (see protocol::hello_frame).
    std::string boot_;
    Config cfg_;
    bool running_ = false;
    QString last_error_;

    std::unique_ptr<QHttpServer> http_;
    QTcpServer* tcp_ = nullptr;  ///< owned by http_ once bound

    std::vector<Client> clients_;
    Auth auth_;
    /// The token each admitted upgrade carried, by the peer's address and
    /// port, until its socket arrives (on_new_web_socket_connection) and is
    /// set to close when the token expires. Emptied when the PIN changes or
    /// every device is signed out, which ends the tokens it holds.
    QHash<QString, QString> admitted_tokens_;
    ThumbCache thumbs_;
    /// index_page's answer, once made.
    QByteArray index_html_;
    /// Requests waiting on an encode, by cache key. The first one starts the
    /// encode; the rest share it.
    std::unordered_map<std::string, std::vector<std::shared_ptr<QPromise<QHttpServerResponse>>>>
        waiting_;
    /// Bumped by stop(), which empties thumbs_: an encode that started
    /// before is not cached after.
    std::uint64_t thumb_generation_ = 0;
    /// Cache misses waiting for the render budget, in the order asked.
    std::deque<ThumbDraw> render_queue_;
    /// Draws the budget holds now, and when it last earned one.
    int render_tokens_ = 0;
    std::chrono::steady_clock::time_point render_refilled_;
    /// Runs render_waiting while render_queue_ holds anything.
    QTimer render_timer_;

    QTimer flush_timer_;
    QHash<int, bool> dirty_;
    quint64 revision_ = 0;
    /// Thumbnail encoders. Destroyed first (declared last), which waits for
    /// any still running; they hold only their own pixels.
    QThreadPool encode_pool_;
};

} // namespace seventhslide::remote
