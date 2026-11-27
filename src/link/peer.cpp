// One connection to another SeventhSlide (peer.hpp): making it, reading and
// sending its messages, closing it, and pausing. The rest is split by what it
// does: peer_hello.cpp (who may share what: the hello, offers, joining, and
// introductions), peer_want.cpp (what each end has, what is asked for, and
// what is sent when asked), peer_media.cpp (media shared between
// connections, and what came of each file), peer_pieces.cpp (what comes in
// pieces, and the bytes that follow), peer_files.cpp (member list entries,
// change files and snapshots' indexes: checked at the gate and stored) and
// peer_parts.cpp (snapshots' parts, and the indexes waiting for them).

#include "seventhslide/link/peer.hpp"

#include "peer_internal.hpp"

#include <QSslError>

#include <algorithm>
#include <utility>

namespace seventhslide::link {

// QT_LOGGING_RULES="seventhslide.link.debug=true" traces every message.
Q_LOGGING_CATEGORY(lc_link, "seventhslide.link", QtWarningMsg)

namespace {

/// A message in one line, for the trace.
QString brief(const json& message)
{
    QString out = QString::fromStdString(message.value("type", "?"));
    for (const char* field : {"machine", "sequence", "files", "sha256", "path", "offset", "last", "kind",
                              "fingerprint"}) {
        if (const auto it = message.find(field); it != message.end()) {
            out += QStringLiteral(" %1=%2").arg(field, QString::fromStdString(it->dump()));
        }
    }
    for (const char* field : {"members", "changes", "snapshots", "parts", "admin_keys", "media", "paths"}) {
        if (const auto it = message.find(field); it != message.end() && !it->empty()) {
            out += QStringLiteral(" %1=%2").arg(field, QString::fromStdString(it->dump()));
        }
    }
    return out;
}

/// What a connection reads ahead of handling it. TCP holds the rest back at
/// the other end, so one read is little work, a pause is heard soon after it
/// is said, and a window too busy to keep up does not gather a fast sender's
/// bytes in memory.
constexpr qint64 k_read_ahead = 2 * 1024 * 1024;

}  // namespace

Peer::Peer(QSslSocket* socket, const sync::Identity& me, const std::vector<SyncedLibrary>& libraries,
           PeerShared& shared, std::optional<crypto::PublicKey> expected,
           std::vector<Joining> joining, bool outgoing, QObject* parent)
    : QObject(parent), socket_(socket), me_(me), libraries_(libraries), shared_(shared), fetches_(shared.fetches), expected_(std::move(expected)),
      joining_(std::move(joining)), outgoing_(outgoing), outbox_(*socket, shared, libraries),
      media_in_(shared, libraries,
                {[this](const std::string& uid, const std::string& path, std::int64_t bytes) {
                     emit media_progress(uid, path, bytes);
                 },
                 [this](const std::string& uid, const std::string& path, const QString& why) {
                     drop_media(uid, path, why);
                 },
                 [this](const std::string& uid, const std::string& path, const QString& why) {
                     media_failed(uid, path, why);
                 },
                 [this](const std::string& uid, const std::string& path, const QString& why) {
                     emit refused(why.arg(QString::fromStdString(path)));
                     got(uid, item("media", path), Got::NotHere);
                 },
                 [this](const std::string& uid, const std::string& path) { media_placed(uid, path); },
                 [this] { read_frames(); },
                 [this](const std::string& uid, const std::string& path) { handed_over(uid, path); }})
{
    // A computer on this network is not held to the upload speed cap: the cap
    // keeps room on the uplink, which it does not use.
    outbox_.set_capped([this] { return !(shared_.close_by && shared_.close_by(*this)); });
    socket_->setParent(this);
    socket_->setReadBufferSize(k_read_ahead);
    opened_.start();
    heard_.start();
    files_heard_.start();
    keep_alive_.setInterval(
        std::max(100, std::min({shared_.ping_ms, shared_.silent_ms, shared_.hello_ms, shared_.handshake_ms}) / 3));
    connect(&keep_alive_, &QTimer::timeout, this, &Peer::keep_alive);
    keep_alive_.start();
    held_long_enough_.setSingleShot(true);
    connect(&held_long_enough_, &QTimer::timeout, this, [this] {
        std::vector<std::string> held;
        for (const auto& [uid, st] : state_) {
            if (!st.pending.empty()) {
                held.push_back(uid);
            }
        }
        for (const auto& uid : held) {
            settle_pending(uid);
        }
    });
    // Small messages go at once, not held back for a fuller packet.
    const auto low_delay = [this] { socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1); };
    if (socket_->state() == QAbstractSocket::ConnectedState) {
        low_delay();
    } else {
        connect(socket_, &QSslSocket::connected, this, low_delay);
    }
    connect(socket_, &QSslSocket::readyRead, this, &Peer::on_read);
    // Written on as what went drains: into TLS (``bytesWritten``), and out to
    // the network (``encryptedBytesWritten``), which is what ``unsent``
    // waits on.
    connect(socket_, &QSslSocket::bytesWritten, this, [this](qint64) { outbox_.pump(); });
    connect(socket_, &QSslSocket::encryptedBytesWritten, this, [this](qint64) { outbox_.pump(); });
    connect(socket_, &QSslSocket::disconnected, this,
            [this] { close(QStringLiteral("The connection closed")); });
    connect(socket_, &QSslSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { close(socket_->errorString()); });
    // QueryPeer: a self-signed certificate is noted, not refused; peer_key
    // says whose it is.
    connect(socket_, &QSslSocket::sslErrors, this, [](const QList<QSslError>&) {});
    if (socket_->isEncrypted()) {
        QMetaObject::invokeMethod(this, [this] { on_encrypted(); }, Qt::QueuedConnection);
    } else {
        connect(socket_, &QSslSocket::encrypted, this, &Peer::on_encrypted);
    }
}

Peer::~Peer() = default;

void Peer::keep_alive()
{
    if (closed_) {
        return;
    }
    if (!ready_) {
        const bool hanging_back = !outgoing_ && !socket_->isEncrypted();
        if (opened_.elapsed() >= (hanging_back ? std::min(shared_.handshake_ms, shared_.hello_ms) : shared_.hello_ms)) {
            close(QStringLiteral("The other computer did not say who it is in time"));
        }
        return;
    }
    if (heard_.elapsed() >= shared_.silent_ms) {
        close(QStringLiteral("Nothing heard from the other computer for a while: the connection "
                             "is gone"));
    } else if (outbox_.quiet_ms() >= shared_.ping_ms) {
        send({{"type", "ping"}});
    }
    let_go_of_quiet_parts();
}

bool Peer::busy() const
{
    if (closed_) {
        return false;
    }
    // A connection from elsewhere that has not said who it is may be anyone's,
    // and is not this computer's work.
    if (!ready_) {
        return outgoing_;
    }
    if (outbox_.busy() || incoming_ || media_in_.busy() || !arrived_.empty() || checking_file_) {
        return true;
    }
    return std::ranges::any_of(state_, [](const auto& entry) {
        const auto& st = entry.second;
        return !st.requested.empty() || !st.pending.empty() || !st.waiting.empty() || !st.storing.empty();
    });
}

void Peer::library_changed(const std::string& uid)
{
    if (ready_ && allowed_.contains(uid)) {
        if (const auto* lib = library(uid)) {
            send(have(*lib));
        }
    }
    // A snapshot stored since may be newer than one waiting here.
    settle_waiting(uid);
}

bool Peer::moving(const std::string& library) const
{
    if (closed_) {
        return false;
    }
    if (incoming_ && incoming_->library == library) {
        return true;
    }
    if (outbox_.sending(library)) {
        return true;
    }
    const auto st = state_.find(library);
    return st != state_.end()
           && (!st->second.requested.empty() || !st->second.pending.empty() || !st->second.waiting.empty()
               || !st->second.storing.empty());
}

void Peer::upload_cap_changed()
{
    if (!closed_) {
        outbox_.pump();
    }
}

void Peer::close(const QString& why)
{
    if (closed_) {
        return;
    }
    qCDebug(lc_link).noquote() << QString::fromStdString(me_.machine) << "closes"
                               << QString::fromStdString(machine_) << ":" << why;
    closed_ = true;
    held_long_enough_.stop();
    store_held();
    outbox_.close();
    media_in_.forget_all();
    socket_->abort();
    emit closed(why);
}

const SyncedLibrary* Peer::library(const std::string& uid) const
{
    const auto it = std::ranges::find(libraries_, uid, &SyncedLibrary::uid);
    return it == libraries_.end() ? nullptr : &*it;
}

void Peer::on_read()
{
    on_encrypted();  // greet before answering, whichever came first
    heard_.restart();
    read_frames();
}

void Peer::read_frames()
{
    // Pieces waiting to be written: the rest stays where it is, and TCP
    // holds the sender back until they are (``MediaIn::room``).
    if (closed_ || !media_in_.has_room()) {
        return;
    }
    heard_.restart();
    reader_.feed(socket_->readAll());
    try {
        while (!closed_ && media_in_.has_room()) {
            // Before a library is shared, only small messages: one that has
            // proved nothing holds little memory.
            if (allowed_.empty()) {
                reader_.set_limits(k_max_json_frame_unadmitted, 0);
            } else {
                reader_.set_limits(k_max_json_frame, k_max_bytes_frame);
            }
            const auto frame = reader_.next();
            if (!frame) {
                break;
            }
            if (frame->bytes) {
                handle_bytes(frame->payload);
            } else {
                handle(json::parse(frame->payload.constData(),
                                   frame->payload.constData() + frame->payload.size(), nullptr,
                                   /*allow_exceptions=*/false));
            }
        }
    } catch (const std::exception& ex) {
        // Whatever the other end sent, nothing it sends leaves this slot.
        close(QString::fromUtf8(ex.what()));
    }
    tell_stored();
}

void Peer::send(const json& message)
{
    qCDebug(lc_link).noquote() << QString::fromStdString(me_.machine) << "->"
                               << QString::fromStdString(machine_.empty() ? "?" : machine_)
                               << brief(message);
    outbox_.message(message);
}

void Peer::store_changed(const std::string& uid)
{
    changed_.insert(uid);
    // Who may write, or what was built on, may have changed: what waited is
    // asked for again.
    state_[uid].not_yet.clear();
}

void Peer::tell_stored()
{
    for (const auto& uid : std::exchange(changed_, {})) {
        emit stored(uid);
    }
    for (const auto& uid : std::exchange(logged_, {})) {
        emit logged(uid);
    }
}

void Peer::handle(const json& message)
{
    if (!message.is_object()) {
        malformed("not an object");
    }
    const auto type = text_of(message, "type");
    qCDebug(lc_link).noquote() << QString::fromStdString(me_.machine) << "<-"
                               << QString::fromStdString(machine_.empty() ? "?" : machine_)
                               << brief(message);
    if (type == "hello") {
        take_hello(message);
        return;
    }
    if (!ready_) {
        malformed("spoke before saying hello");
    }
    if (type == "ping") {
        return;  // heard: that is all it asks
    }
    if (type == "pause") {
        take_pause(message);
        return;
    }
    const auto uid = text_of(message, "library");
    if (type == "offer") {
        take_offer(uid, message);
        return;
    }
    const auto handler = handlers().find(type);
    if (handler == handlers().end()) {
        return;  // a message this version does not know: left alone
    }
    if (!allowed_.contains(uid)) {
        return;  // not shared over this connection, or not yet
    }
    const auto* lib = library(uid);
    if (lib == nullptr) {
        return;  // taken offline here meanwhile
    }
    (this->*handler->second)(*lib, message);
}

const std::map<std::string, Peer::Handler, std::less<>>& Peer::handlers()
{
    static const std::map<std::string, Handler, std::less<>> table{
        {"have", &Peer::take_have},
        {"have_media", &Peer::take_have_media},
        {"coming", &Peer::take_coming},
        {"want", &Peer::serve},
        {"member", &Peer::take_member},
        {"log", &Peer::take_log},
        {"admin_key", &Peer::take_admin_key},
        {store_file_name(StoreFile::Change), &Peer::take_store_file},
        {store_file_name(StoreFile::Snapshot), &Peer::take_store_file},
        {store_file_name(StoreFile::Part), &Peer::take_part},
        {"piece", &Peer::take_piece_announced},
        {"pieces", &Peer::take_media_announced},
        {"media", &Peer::take_media_announced},
        {"cancel", &Peer::take_cancel},
        {"missing", &Peer::take_missing},
        {"introduce", &Peer::take_introduce},
        {"met", &Peer::take_met},
    };
    return table;
}

void Peer::take_pause(const json& message)
{
    const auto paused = message.find("paused");
    const bool now = paused != message.end() && paused->is_boolean() && paused->get<bool>();
    const bool was = halted();
    their_pause_ = now;
    if (halted() != was) {
        halted() ? halt() : go_on();
    }
}

void Peer::pause_changed()
{
    if (!ready_ || closed_) {
        return;  // the hello says so, once it goes
    }
    const bool paused = shared_.paused;
    send({{"type", "pause"}, {"paused", paused}});
    if (paused) {
        if (!their_pause_) {
            halt();
        }
        return;
    }
    // What is here, again: what the other end let go of, it asks for anew.
    for (const auto& uid : allowed_) {
        if (const auto* lib = library(uid)) {
            send(have(*lib));
        }
    }
    if (!their_pause_) {
        go_on();
    }
}

void Peer::halt()
{
    outbox_.drop_files();
    media_in_.forget_all();
    handing_to_.clear();
    arrived_.clear();
    if (incoming_) {
        incoming_->accept = false;
    }
    std::vector<std::string> let_go_of;
    for (auto& [uid, st] : state_) {
        bool any = false;
        for (auto it = st.requested.begin(); it != st.requested.end();) {
            // A media file being put in place stays this connection's: no
            // other may write its part meanwhile.
            const bool checking = it->starts_with("media/") && media_in_.placing(uid, it->substr(6));
            // The shared log goes on moving.
            if (checking || it->starts_with("log/")) {
                ++it;
                continue;
            }
            fetches_.release(uid, *it, this);
            it = st.requested.erase(it);
            any = true;
        }
        // What waits here for its media or parts too: another connection
        // fetches it, and with it what it waits for (from where the part
        // ends).
        for (const auto& p : st.pending) {
            fetches_.release(uid, item(StoreFile::Change, p.machine, p.number), this);
            any = true;
        }
        for (const auto& w : st.waiting) {
            fetches_.release(uid, w.name(), this);
            any = true;
        }
        st.pending.clear();
        st.waiting.clear();
        st.in_pieces.clear();
        if (any) {
            let_go_of.push_back(uid);
        }
    }
    // The other end paused: another connection may have it. (This computer
    // paused: none fetches anything now.)
    if (!shared_.paused) {
        for (const auto& uid : let_go_of) {
            emit let_go(uid);
        }
    }
    // The shared log, which moves while halted, is asked for again.
    for (const auto& uid : allowed_) {
        want_more(uid);
    }
}

void Peer::go_on()
{
    for (const auto& uid : allowed_) {
        want_more(uid);
    }
}

}  // namespace seventhslide::link
