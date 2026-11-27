#include "seventhslide/server/control.hpp"

#include "seventhslide/link/frames.hpp"
#include "seventhslide/server/server.hpp"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QPointer>

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace seventhslide::server {
namespace {

using nlohmann::json;

/// Longest path a local socket takes everywhere (Linux allows 107 bytes,
/// macOS 103).
constexpr std::size_t k_longest_socket_path = 100;

json failure(const std::string& why)
{
    return {{"ok", false}, {"error", why}};
}

}  // namespace

QString control_name(const std::filesystem::path& data)
{
    std::error_code ec;
    auto absolute = std::filesystem::absolute(data, ec);
    if (ec) {
        absolute = data;
    }
#ifndef Q_OS_WIN
    const auto path = (absolute / "control").string();
    if (path.size() <= k_longest_socket_path) {
        return QString::fromStdString(path);
    }
#endif
    const auto hash = QCryptographicHash::hash(QByteArray::fromStdString(absolute.string()),
                                               QCryptographicHash::Sha256);
    return QStringLiteral("seventhslide-server-") + QString::fromLatin1(hash.toHex().left(16));
}

Control::Control(Server& server, QObject* parent) : QObject(parent), server_(server)
{
    local_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&local_, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = local_.nextPendingConnection()) {
            auto reader = std::make_shared<link::FrameReader>();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, reader] {
                reader->feed(socket->readAll());
                try {
                    if (const auto frame = reader->next(); frame && !frame->bytes) {
                        const auto request = json::parse(frame->payload.constData(),
                                                         frame->payload.constData() + frame->payload.size(),
                                                         nullptr, /*allow_exceptions=*/false);
                        answer(socket, request);
                    }
                } catch (const std::exception&) {
                    socket->abort();
                }
            });
        }
    });
}

void Control::listen(const std::filesystem::path& data)
{
    const auto name = control_name(data);
    // Only one server runs with this data folder (its lock), so a socket
    // left there is from one that stopped without tidying up.
    QLocalServer::removeServer(name);
    if (!local_.listen(name)) {
        throw std::runtime_error("The server could not open its socket for the commands ("
                                 + name.toStdString() + "): " + local_.errorString().toStdString());
    }
}

void Control::answer(QLocalSocket* socket, const json& request)
{
    // The socket may have gone by the time admin work replies.
    const QPointer<QLocalSocket> to(socket);
    const auto reply = [to](const json& j) {
        if (to) {
            to->write(link::encode_message(j));
            to->flush();
        }
    };
    if (!request.is_object() || !request.contains("command") || !request["command"].is_string()) {
        reply(failure("That is not a request this server knows."));
        return;
    }
    const auto command = request["command"].get<std::string>();
    if (command == "status") {
        reply(server_.status());
        return;
    }
    if (command == "join") {
        const auto code = request.find("code");
        if (code == request.end() || !code->is_string()) {
            reply(failure("Give the invite."));
            return;
        }
        server_.join(code->get<std::string>(), reply);
        return;
    }
    if (command == "sync") {
        server_.sync_now();
        reply({{"ok", true}});
        return;
    }
    if (command == "settings") {
        const auto changes = request.find("set");
        reply(changes != request.end() ? server_.set_settings(*changes) : server_.settings());
        return;
    }
    const auto library = request.find("library");
    if (library == request.end() || !library->is_string()) {
        reply(failure("Name the library."));
        return;
    }
    const auto choice = choose_library(server_.library_uids(), library->get<std::string>());
    if (!choice.error.empty()) {
        reply(failure(choice.error));
        return;
    }
    if (command == "admin") {
        server_.admin(choice.uid, request, reply);
        return;
    }
    if (command == "address") {
        const auto computer = request.find("computer");
        if (computer == request.end() || !computer->is_string()) {
            reply(failure("Name the computer."));
            return;
        }
        std::optional<std::vector<std::string>> addresses;
        if (const auto it = request.find("addresses"); it != request.end()) {
            if (!it->is_array() || !std::ranges::all_of(*it, [](const json& a) { return a.is_string(); })) {
                reply(failure("Addresses are a list of them."));
                return;
            }
            addresses = it->get<std::vector<std::string>>();
        }
        reply(server_.reach_at(choice.uid, computer->get<std::string>(), addresses));
        return;
    }
    if (command == "leave") {
        reply(server_.leave(choice.uid));
        return;
    }
    reply(failure("That is not a request this server knows."));
}

json ask_server(const std::filesystem::path& data, const json& request, int timeout_ms)
{
    QLocalSocket socket;
    socket.connectToServer(control_name(data));
    if (!socket.waitForConnected(3000)) {
        throw NoServer("No SeventhSlide server is running with the data folder "
                                 + data.string() + ". Start it with: seventhslide --server"
                                 + " --data \"" + data.string() + "\"");
    }
    socket.write(link::encode_message(request));
    socket.flush();
    link::FrameReader reader;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms) {
        if (socket.bytesAvailable() == 0
            && !socket.waitForReadyRead(static_cast<int>(timeout_ms - timer.elapsed()))) {
            break;
        }
        reader.feed(socket.readAll());
        if (const auto frame = reader.next(); frame && !frame->bytes) {
            auto reply = json::parse(frame->payload.constData(),
                                     frame->payload.constData() + frame->payload.size(), nullptr,
                                     /*allow_exceptions=*/false);
            if (reply.is_object()) {
                return reply;
            }
            break;
        }
    }
    throw std::runtime_error("The server did not answer. Its log may say why.");
}

}  // namespace seventhslide::server
