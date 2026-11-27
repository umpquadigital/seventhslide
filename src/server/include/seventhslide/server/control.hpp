#pragma once

// How the commands (``join``, ``status``, ``admin``, ``leave`` and the rest) and
// the settings screens reach the server running with the same data folder: a local socket, this user's
// only, beside its data (``<data>/control``; on Windows, and where that path
// is too long for a socket, a name made from the data folder's path). One
// request, one reply, each a JSON frame as the link sends them.
//
// Requests: ``{"command": "join", "code": ...}``, ``{"command": "status"}``,
// ``{"command": "sync"}``, ``{"command": "settings"}`` with ``"set": {...}`` to
// change them (``settings_edit.hpp``), ``{"command": "admin", "library": uid, ...}``
// (``admin_work``'s request), ``{"command": "address", "library": uid, "computer":
// ...}``, ``{"command": "leave", "library": uid}``. A reply is ``{"ok": true, ...}``
// or ``{"ok": false, "error": why}``.
//
// Seam: Qt Network (QLocalServer, QLocalSocket).

#include <QLocalServer>
#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <stdexcept>

namespace seventhslide::server {

class Server;

/// The name the commands find the server running with ``data`` by.
[[nodiscard]] QString control_name(const std::filesystem::path& data);

class Control final : public QObject {
    Q_OBJECT
public:
    explicit Control(Server& server, QObject* parent = nullptr);

    /// Throws ``std::runtime_error`` when the socket cannot be had.
    void listen(const std::filesystem::path& data);

private:
    void answer(QLocalSocket* socket, const nlohmann::json& request);

    Server& server_;
    QLocalServer local_;
};

/// No server takes requests there.
struct NoServer : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Ask the server running with ``data``, and wait up to ``timeout_ms`` for
/// its reply. Throws ``NoServer`` when none takes it, ``std::runtime_error`` when
/// the one that did gave no answer.
[[nodiscard]] nlohmann::json ask_server(const std::filesystem::path& data,
                                        const nlohmann::json& request, int timeout_ms = 120000);

}  // namespace seventhslide::server
