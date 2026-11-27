#include <seventhslide/remote/remote_http_util.hpp>

#include <QHostAddress>
#include <QHostInfo>
#include <QHttpHeaders>
#include <QHttpServerRequest>
#include <QUrl>
#include <QUrlQuery>

namespace seventhslide::remote::http {

QByteArray mime_for(const QString& path)
{
    if (path.endsWith(QLatin1String(".html"))) return "text/html; charset=utf-8";
    if (path.endsWith(QLatin1String(".css"))) return "text/css; charset=utf-8";
    if (path.endsWith(QLatin1String(".js"))) return "application/javascript; charset=utf-8";
    if (path.endsWith(QLatin1String(".svg"))) return "image/svg+xml";
    if (path.endsWith(QLatin1String(".png"))) return "image/png";
    if (path.endsWith(QLatin1String(".ico"))) return "image/x-icon";
    if (path.endsWith(QLatin1String(".webmanifest"))) return "application/manifest+json";
    return "application/octet-stream";
}

QHttpServerResponse json_response(const nlohmann::json& body,
                                  QHttpServerResponse::StatusCode status)
{
    return QHttpServerResponse(QByteArray("application/json; charset=utf-8"),
                               QByteArray::fromStdString(body.dump()), status);
}

QHttpServerResponse json_error(const std::string& message,
                               QHttpServerResponse::StatusCode status)
{
    return json_response(nlohmann::json{{"error", message}}, status);
}

QString token_from(const QHttpServerRequest& request)
{
    const QHttpHeaders headers = request.headers();
    const QByteArray auth =
        headers.value(QHttpHeaders::WellKnownHeader::Authorization).toByteArray();
    if (auth.startsWith("Bearer ")) {
        return QString::fromLatin1(auth.mid(7)).trimmed();
    }
    const QByteArray cookies =
        headers.value(QHttpHeaders::WellKnownHeader::Cookie).toByteArray();
    const QByteArray prefix = QByteArray(k_cookie_name) + "=";
    for (const QByteArray& part : cookies.split(';')) {
        const QByteArray trimmed = part.trimmed();
        if (trimmed.startsWith(prefix)) {
            return QString::fromLatin1(trimmed.mid(prefix.size()));
        }
    }
    // WebSocket constructors cannot set Authorization; the client passes the
    // token on the upgrade URL when the cookie is not yet available.
    const QUrlQuery query(request.query());
    const QString from_query = query.queryItemValue(QStringLiteral("token"));
    if (!from_query.isEmpty()) {
        return from_query;
    }
    return {};
}

QString peer_of(const QHttpServerRequest& request)
{
    return request.remoteAddress().toString();
}

bool same_origin(const QHttpServerRequest& request)
{
    const QHttpHeaders headers = request.headers();
    if (!headers.contains(QHttpHeaders::WellKnownHeader::Origin)) {
        return true;
    }
    const QUrl origin(QString::fromLatin1(
        headers.value(QHttpHeaders::WellKnownHeader::Origin).toByteArray()));
    const QUrl host(QStringLiteral("http://")
                    + QString::fromLatin1(
                        headers.value(QHttpHeaders::WellKnownHeader::Host).toByteArray()));
    // "null" (a sandboxed frame, a file) names no host, so it is refused.
    if (origin.host().isEmpty() || host.host().isEmpty()) {
        return false;
    }
    const int origin_port =
        origin.port(origin.scheme() == QLatin1String("https") ? 443 : 80);
    return origin.host().compare(host.host(), Qt::CaseInsensitive) == 0
        && origin_port == host.port(80);
}

bool host_is_local(const QHttpServerRequest& request)
{
    const QHttpHeaders headers = request.headers();
    if (!headers.contains(QHttpHeaders::WellKnownHeader::Host)) {
        return true;
    }
    // QUrl takes the brackets off an IPv6 address and folds the case.
    const QString name =
        QUrl(QStringLiteral("http://")
             + QString::fromLatin1(headers.value(QHttpHeaders::WellKnownHeader::Host).toByteArray()))
            .host();
    if (name.isEmpty()) {
        return false;
    }
    if (QHostAddress address; address.setAddress(name)) {
        return true;
    }
    if (name == QLatin1String("localhost") || name.endsWith(QLatin1String(".local"))) {
        return true;
    }
    const QString own = QHostInfo::localHostName().toLower();
    const QString domain = QHostInfo::localDomainName().toLower();
    return !own.isEmpty()
           && (name == own || (!domain.isEmpty() && name == own + QLatin1Char('.') + domain));
}

bool is_private_address(const QHostAddress& address)
{
    if (address.isLoopback() || address.isLinkLocal()) {
        return true;
    }
    // An IPv4 client reaching a dual-stack listener arrives mapped into IPv6.
    bool converted = false;
    const QHostAddress v4mapped(address.toIPv4Address(&converted));
    const QHostAddress effective = converted ? v4mapped : address;

    if (!converted && effective.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR raw = effective.toIPv6Address();
        return (raw[0] & 0xfe) == 0xfc;  // fc00::/7 unique-local
    }

    const quint32 v4 = effective.toIPv4Address();
    const auto within = [v4](const char* network, int bits) {
        const quint32 base = QHostAddress(QLatin1String(network)).toIPv4Address();
        const quint32 mask = bits == 0 ? 0u : (0xffffffffu << (32 - bits));
        return (v4 & mask) == (base & mask);
    };
    return within("10.0.0.0", 8) || within("172.16.0.0", 12)
        || within("192.168.0.0", 16) || within("100.64.0.0", 10);
}

} // namespace seventhslide::remote::http
