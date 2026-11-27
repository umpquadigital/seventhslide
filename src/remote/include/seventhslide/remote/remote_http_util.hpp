#pragma once

// Small HTTP helpers shared by the server and its route table.

#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QHttpServerResponse>
#include <QString>

QT_BEGIN_NAMESPACE
class QHostAddress;
class QHttpServerRequest;
QT_END_NAMESPACE

namespace seventhslide::remote::http {

inline constexpr char k_cookie_name[] = "ss_remote";

[[nodiscard]] QByteArray mime_for(const QString& path);

[[nodiscard]] QHttpServerResponse json_response(
    const nlohmann::json& body,
    QHttpServerResponse::StatusCode status = QHttpServerResponse::StatusCode::Ok);

[[nodiscard]] QHttpServerResponse json_error(const std::string& message,
                                             QHttpServerResponse::StatusCode status);

/// Bearer token from the Authorization header, the session cookie, or a
/// ``?token=`` query (browsers cannot set headers on a WebSocket handshake).
[[nodiscard]] QString token_from(const QHttpServerRequest& request);

[[nodiscard]] QString peer_of(const QHttpServerRequest& request);

/// Whether the request may come from where it says: no ``Origin`` (not a
/// browser), or an ``Origin`` naming the host and port it was sent to (a page
/// this server served). A browser lets any page open a WebSocket anywhere, so
/// without this a page on another site could drive the remote.
[[nodiscard]] bool same_origin(const QHttpServerRequest& request);

/// Whether the request was sent to this computer by a name that can only be
/// this computer's: an address, ``localhost``, a ``.local`` name, or this
/// computer's own name (or none, from a client that sends no ``Host``).
/// Otherwise it may be a page whose own name was pointed at this computer
/// (DNS rebinding), which ``same_origin`` alone lets through.
[[nodiscard]] bool host_is_local(const QHttpServerRequest& request);

/// Loopback, link-local, RFC1918, CGNAT, and IPv6 unique-local.
[[nodiscard]] bool is_private_address(const QHostAddress& address);

} // namespace seventhslide::remote::http
