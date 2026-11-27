// HTTP route table for the web remote.
//
// The WebSocket carries everything stateful. HTTP serves only the embedded web
// app, authentication, paged library reads, and thumbnails - the things a
// socket is a poor fit for.

#include <seventhslide/remote/remote_http_util.hpp>
#include <seventhslide/remote/remote_protocol.hpp>
#include <seventhslide/remote/remote_server.hpp>

#include <seventhslide/json/read.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFuture>
#include <QHostAddress>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QMetaObject>
#include <QPromise>
#include <QUrl>
#include <QUrlQuery>
#include <QtLogging>

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::remote {

namespace {

using Status = QHttpServerResponse::StatusCode;

/// A file of the web app's, as it is sent.
QHttpServerResponse asset_response(const QString& resource_path, const QByteArray& bytes)
{
    QHttpServerResponse response(http::mime_for(resource_path), bytes);
    QHttpHeaders headers = response.headers();
    // The app shell must not be cached - a rebuilt binary ships new assets.
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-cache");
    response.setHeaders(std::move(headers));
    return response;
}

/// The bytes of resource ``resource_path``; empty when there is none.
QByteArray read_asset(const QString& resource_path)
{
    QFile file(QStringLiteral(":/remote/") + resource_path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QHttpServerResponse asset(const QString& resource_path)
{
    const QByteArray bytes = read_asset(resource_path);
    if (bytes.isEmpty()) {
        return http::json_error("not found", Status::NotFound);
    }
    return asset_response(resource_path, bytes);
}

/// The request's query as a browser's form encoding writes it
/// (URLSearchParams, a form's GET): "+" is a space, as QUrlQuery does not
/// read it; a "+" itself comes as "%2B".
QUrlQuery form_query(const QHttpServerRequest& request)
{
    QString raw = request.url().query(QUrl::FullyEncoded);
    raw.replace(QLatin1Char('+'), QStringLiteral("%20"));
    return QUrlQuery(raw);
}

/// The largest /api/auth body parsed; a bigger one is answered 413. The client
/// sends {"pin": ...}, and this holds any PIN of ASCII characters the Settings
/// field takes (32767 at most). It spares the parse and nothing else:
/// QHttpServer has read the whole body into memory before any handler sees
/// it, and Qt 6.8 has no limit of its own on a body's size.
constexpr qsizetype k_max_auth_body = 64 * 1024;

/// Where index.html takes the glyph sprite.
constexpr std::string_view k_glyphs_marker = "<!-- glyphs -->";

/// The request body as JSON; ``discarded`` when it does not parse. Callers
/// read it with the tolerant ``json::`` readers, which treat anything that is
/// not an object as empty.
nlohmann::json parse_body(const QHttpServerRequest& request)
{
    return nlohmann::json::parse(request.body().toStdString(), nullptr,
                                 /*allow_exceptions=*/false);
}

/// Wrap an API route handler. QHttpServer calls handlers straight from the
/// event loop, where an escaping exception terminates the process, so a
/// failure becomes a 500 for that one request. ``Args`` spells out the handler
/// signature because QHttpServer deduces route arguments from it.
template <typename... Args, typename Handler>
auto guarded(const char* route, Handler handler)
{
    return [route, handler = std::move(handler)](Args... args) -> QHttpServerResponse {
        try {
            return handler(std::forward<Args>(args)...);
        } catch (const std::exception& ex) {
            qWarning("SeventhSlide remote: %s failed: %s", route, ex.what());
        } catch (...) {
            qWarning("SeventhSlide remote: %s failed", route);
        }
        return http::json_error("internal error", Status::InternalServerError);
    };
}

/// ``guarded`` for a handler that answers later, through a QFuture. A failure
/// before the future exists becomes a 500 at once; the future's own work is
/// the handler's to keep from throwing.
template <typename... Args, typename Handler>
auto guarded_async(const char* route, Handler handler)
{
    return [route, handler = std::move(handler)](Args... args) -> QFuture<QHttpServerResponse> {
        try {
            return handler(std::forward<Args>(args)...);
        } catch (const std::exception& ex) {
            qWarning("SeventhSlide remote: %s failed: %s", route, ex.what());
        } catch (...) {
            qWarning("SeventhSlide remote: %s failed", route);
        }
        return QtFuture::makeReadyValueFuture(
            http::json_error("internal error", Status::InternalServerError));
    };
}

/// Register an API route. Every one goes through ``guarded``.
template <typename... Args, typename Handler>
void add_api_route(QHttpServer& server, const char* path, Handler handler)
{
    server.route(QString::fromLatin1(path), guarded<Args...>(path, std::move(handler)));
}

template <typename... Args, typename Handler>
void add_api_route(QHttpServer& server,
                   const char* path,
                   QHttpServerRequest::Method method,
                   Handler handler)
{
    server.route(QString::fromLatin1(path), method, guarded<Args...>(path, std::move(handler)));
}

/// Register an API route that answers through a QFuture.
template <typename... Args, typename Handler>
void add_async_api_route(QHttpServer& server, const char* path, Handler handler)
{
    server.route(QString::fromLatin1(path), guarded_async<Args...>(path, std::move(handler)));
}

/// A response that finish_encode will give; it joins ``waiters``.
QFuture<QHttpServerResponse> add_waiter(
    std::vector<std::shared_ptr<QPromise<QHttpServerResponse>>>& waiters)
{
    auto waiter = std::make_shared<QPromise<QHttpServerResponse>>();
    waiter->start();
    waiters.push_back(waiter);
    return waiter->future();
}

/// A thumbnail as an HTTP response.
QHttpServerResponse thumb_response(const ThumbResult& thumb, const std::string& revision)
{
    if (!thumb.ok) {
        return http::json_error("not found", Status::NotFound);
    }
    QHttpServerResponse response(QByteArray::fromStdString(thumb.mime),
                                 QByteArray::fromStdString(thumb.bytes));
    QHttpHeaders headers = response.headers();
    // The revision is the cache buster: with one present the bytes can never
    // change, so the browser refetches exactly when the scene did. Private:
    // they are operator data behind the PIN, which a shared cache or proxy
    // between phone and app must not keep.
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl,
                   revision.empty() ? "no-cache" : "private, max-age=31536000, immutable");
    response.setHeaders(std::move(headers));
    return response;
}

QHttpServerResponse internal_error()
{
    return http::json_error("internal error", Status::InternalServerError);
}

int int_param(const QUrlQuery& query, const QString& key, int fallback)
{
    bool ok = false;
    const int value = query.queryItemValue(key).toInt(&ok);
    return ok ? value : fallback;
}

qint64 id_param(const QUrlQuery& query, const QString& key, qint64 fallback)
{
    bool ok = false;
    const qint64 value = query.queryItemValue(key).toLongLong(&ok);
    return ok ? value : fallback;
}

} // namespace

void RemoteServer::register_routes()
{
    register_asset_routes();
    register_api_routes();
}

void RemoteServer::register_asset_routes()
{
    // Static assets carry no operator data, so they stay reachable before the
    // PIN is entered - the gate itself is part of the app. Like every route,
    // they answer only the networks the remote serves.
    struct AssetRoute {
        const char* path;
        const char* file;
    };
    static constexpr AssetRoute k_asset_routes[] = {
        {"/manifest.webmanifest", "manifest.webmanifest"},
        // Browser tab / home-screen icons. index.html links favicon.png, but
        // browsers auto-probe /favicon.ico regardless, so the multi-resolution
        // ICO answers that.
        {"/favicon.png", "icons/favicon.png"},
        {"/favicon.ico", "icons/favicon.ico"},
        // iOS requests both spellings when adding the remote to the home screen.
        {"/apple-touch-icon.png", "icons/apple-touch-icon.png"},
        {"/apple-touch-icon-precomposed.png", "icons/apple-touch-icon.png"},
        // Manifest icons (Android "Add to Home Screen").
        {"/icons/icon-192.png", "icons/icon-192.png"},
        {"/icons/icon-512.png", "icons/icon-512.png"},
    };
    const auto serve = [this](const QString& path, const QString& file) {
        http_->route(path, [this, file](const QHttpServerRequest& request) {
            if (!peer_allowed(request)) {
                return http::json_error("forbidden", Status::Forbidden);
            }
            return asset(file);
        });
    };
    for (const auto& route : k_asset_routes) {
        serve(QString::fromLatin1(route.path), QString::fromLatin1(route.file));
    }
    for (const char* path : {"/", "/index.html"}) {
        http_->route(QString::fromLatin1(path), [this](const QHttpServerRequest& request) {
            if (!peer_allowed(request)) {
                return http::json_error("forbidden", Status::Forbidden);
            }
            return index_page();
        });
    }
    // The client's modules (index.html loads js/main.js, which imports the
    // rest) and its stylesheets: every file the resources hold there, at its
    // own path.
    for (const char* dir : {"js", "css"}) {
        const QString prefix = QString::fromLatin1(dir) + QLatin1Char('/');
        for (QDirIterator it(QStringLiteral(":/remote/") + prefix, QDir::Files); it.hasNext();) {
            const QString file = prefix + it.nextFileInfo().fileName();
            serve(QLatin1Char('/') + file, file);
        }
    }
}

QHttpServerResponse RemoteServer::index_page()
{
    if (index_html_.isEmpty()) {
        QByteArray page = read_asset(QStringLiteral("index.html"));
        const QByteArray marker = QByteArray::fromStdString(std::string(k_glyphs_marker));
        const qsizetype at = page.indexOf(marker);
        if (at >= 0) {
            page.replace(at, marker.size(), QByteArray::fromStdString(bridge_.glyph_sprite()));
        }
        index_html_ = std::move(page);
    }
    if (index_html_.isEmpty()) {
        return http::json_error("not found", Status::NotFound);
    }
    return asset_response(QStringLiteral("index.html"), index_html_);
}

void RemoteServer::register_api_routes()
{
    using Request = const QHttpServerRequest&;

    // --- gate ---------------------------------------------------------------
    add_api_route<Request>(*http_, "/api/hello", [this](Request request) {
        if (!peer_allowed(request)) {
            return http::json_error("forbidden", Status::Forbidden);
        }
        return http::json_response(nlohmann::json{
            {"protocol", protocol::k_protocol_version},
            {"needsPin", !auth_.open_access()},
            {"authed", auth_.open_access() || auth_.valid_token(http::token_from(request))},
        });
    });

    add_api_route<Request>(
        *http_, "/api/auth", QHttpServerRequest::Method::Post, [this](Request request) {
            const QString peer = http::peer_of(request);
            if (!peer_allowed(request)) {
                return http::json_error("forbidden", Status::Forbidden);
            }
            if (auth_.open_access()) {
                return http::json_response(nlohmann::json{{"token", ""}, {"open", true}});
            }
            if (request.body().size() > k_max_auth_body) {
                return http::json_error("request too large", Status::PayloadTooLarge);
            }
            if (!auth_.allow_attempt(peer)) {
                nlohmann::json body{{"error", "too many attempts"},
                                    {"retryAfter", auth_.lockout_remaining(peer)}};
                return http::json_response(body, Status::TooManyRequests);
            }
            // A PIN is a string: a number would silently drop leading zeros.
            const QString pin =
                QString::fromStdString(json::str_or(parse_body(request), "pin"));
            if (!auth_.check_pin(pin)) {
                auth_.note_failure(peer);
                return http::json_error("incorrect PIN", Status::Unauthorized);
            }
            auth_.note_success(peer);
            const QString token = auth_.mint_token();

            QHttpServerResponse response = http::json_response(
                nlohmann::json{{"token", token.toStdString()}, {"open", false}});
            QHttpHeaders headers = response.headers();
            headers.append(QHttpHeaders::WellKnownHeader::SetCookie,
                           QString::fromLatin1(http::k_cookie_name) + "=" + token
                               + "; Path=/; HttpOnly; SameSite=Strict; Max-Age="
                               + QString::number(cfg_.token_ttl.count()));
            response.setHeaders(std::move(headers));
            return response;
        });

    // --- library ------------------------------------------------------------
    add_api_route<const QString&, Request>(
        *http_, "/api/library/<arg>", [this](const QString& kind, Request request) {
            if (!authorize(request)) {
                return http::json_error("unauthorized", Status::Unauthorized);
            }
            const QUrlQuery query = form_query(request);
            LibraryQuery q;
            q.kind = kind.toStdString();
            q.query = query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded).toStdString();
            q.folder_id = id_param(query, QStringLiteral("folderId"), -1);
            q.chapter = int_param(query, QStringLiteral("chapter"), 0);
            q.offset = std::max(0, int_param(query, QStringLiteral("offset"), 0));
            q.limit = std::clamp(int_param(query, QStringLiteral("limit"), 50), 1, 200);
            return http::json_response(bridge_.library_page(q));
        });

    add_api_route<Request>(*http_, "/api/services", [this](Request request) {
        if (!authorize(request)) {
            return http::json_error("unauthorized", Status::Unauthorized);
        }
        const QUrlQuery query = form_query(request);
        return http::json_response(bridge_.service_list(
            query.queryItemValue(QStringLiteral("tag"), QUrl::FullyDecoded).toStdString(),
            query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded).toStdString()));
    });

    // --- thumbnails ---------------------------------------------------------
    add_async_api_route<const QString&, Request>(
        *http_, "/api/preview/<arg>", [this](const QString& output_id, Request request) {
            const QUrlQuery query(request.query());
            ThumbRequest r;
            r.kind = "preview";
            r.name = QUrl::fromPercentEncoding(output_id.toUtf8()).toStdString();
            r.slide_index = int_param(query, QStringLiteral("slide"), -1);
            r.stack = query.queryItemValue(QStringLiteral("stack")).toStdString();
            r.side = query.queryItemValue(QStringLiteral("side")).toStdString();
            r.width = clamp_width(int_param(query, QStringLiteral("w"), 320));
            return serve_thumb(request, r);
        });

    add_async_api_route<const QString&, const QString&, Request>(
        *http_, "/api/thumb/<arg>/<arg>",
        [this](const QString& kind, const QString& filename, Request request) {
            const QUrlQuery query(request.query());
            ThumbRequest r;
            r.kind = "media";
            r.sub = kind.toStdString();
            r.name = QUrl::fromPercentEncoding(filename.toUtf8()).toStdString();
            r.width = clamp_width(int_param(query, QStringLiteral("w"), 320));
            return serve_thumb(request, r);
        });

    add_async_api_route<qint64, Request>(
        *http_, "/api/freeform-thumb/<arg>", [this](qint64 freeform_id, Request request) {
            const QUrlQuery query(request.query());
            ThumbRequest r;
            r.kind = "freeform";
            r.id = freeform_id;
            r.width = clamp_width(int_param(query, QStringLiteral("w"), 320));
            return serve_thumb(request, r);
        });

    add_async_api_route<qint64, Request>(
        *http_, "/api/web-thumb/<arg>", [this](qint64 page_id, Request request) {
            const QUrlQuery query(request.query());
            ThumbRequest r;
            r.kind = "web";
            r.id = page_id;
            r.width = clamp_width(int_param(query, QStringLiteral("w"), 320));
            return serve_thumb(request, r);
        });

    add_async_api_route<const QString&, Request>(
        *http_, "/api/live-thumb/<arg>", [this](const QString& key, Request request) {
            const QUrlQuery query(request.query());
            ThumbRequest r;
            r.kind = "live";
            r.name = QUrl::fromPercentEncoding(key.toUtf8()).toStdString();
            r.width = clamp_width(int_param(query, QStringLiteral("w"), 320));
            return serve_thumb(request, r);
        });
}

bool RemoteServer::peer_allowed(const QHttpServerRequest& request) const
{
    return http::host_is_local(request)
           && (!cfg_.private_networks_only || http::is_private_address(request.remoteAddress()));
}

bool RemoteServer::authorize(const QHttpServerRequest& request)
{
    return peer_allowed(request)
        && (auth_.open_access() || auth_.valid_token(http::token_from(request)));
}

QFuture<QHttpServerResponse> RemoteServer::serve_thumb(const QHttpServerRequest& request,
                                                      const ThumbRequest& r)
{
    if (!authorize(request)) {
        return QtFuture::makeReadyValueFuture(
            http::json_error("unauthorized", Status::Unauthorized));
    }
    const QUrlQuery query(request.query());
    const std::string revision = query.queryItemValue(QStringLiteral("r")).toStdString();
    const std::string key = ThumbCache::key_for(r, revision);
    if (std::optional<ThumbResult> hit = thumbs_.get(key)) {
        return QtFuture::makeReadyValueFuture(thumb_response(*hit, revision));
    }

    if (const auto waiting = waiting_.find(key); waiting != waiting_.end()) {
        return add_waiter(waiting->second);  // that encode answers this one too
    }
    // The bridge draws these here, on the GUI thread, so every miss queues
    // for the budget and is drawn in turn (render_waiting).
    const bool budgeted = r.kind == "preview" || r.kind == "freeform";
    if (budgeted && render_queue_.size() >= cfg_.max_waiting_renders) {
        QHttpServerResponse busy = http::json_error("busy", Status::ServiceUnavailable);
        QHttpHeaders headers = busy.headers();
        headers.append(QHttpHeaders::WellKnownHeader::RetryAfter, "1");
        busy.setHeaders(std::move(headers));
        return QtFuture::makeReadyValueFuture(std::move(busy));
    }
    QFuture<QHttpServerResponse> answer = add_waiter(waiting_[key]);
    if (budgeted) {
        render_queue_.push_back(ThumbDraw{key, revision, r});
        render_waiting();
    } else {
        draw(ThumbDraw{key, revision, r});
    }
    return answer;
}

void RemoteServer::start_encode(const std::string& key,
                               const std::string& revision,
                               PendingThumb pending)
{
    encode_pool_.start([this, key, revision, generation = thumb_generation_,
                        encode = std::move(pending.encode)] {
        ThumbResult thumb;
        try {
            thumb = encode();
        } catch (const std::exception& ex) {
            qWarning("SeventhSlide remote: thumbnail encode failed: %s", ex.what());
        } catch (...) {
            qWarning("SeventhSlide remote: thumbnail encode failed");
        }
        // Queued to the server's thread. The pool is destroyed before the
        // server's QObject part, so ``this`` is still a valid receiver; a call
        // still queued at destruction is dropped with it.
        QMetaObject::invokeMethod(
            this,
            [this, key, revision, generation, thumb = std::move(thumb)] {
                finish_encode(key, revision, generation, thumb);
            },
            Qt::QueuedConnection);
    });
}

bool RemoteServer::take_render()
{
    const auto now = std::chrono::steady_clock::now();
    const auto earned = (now - render_refilled_) / cfg_.render_interval;
    if (earned > 0) {
        render_refilled_ += earned * cfg_.render_interval;
        render_tokens_ = static_cast<int>(
            std::min<decltype(earned)>(cfg_.render_burst, render_tokens_ + earned));
    }
    if (render_tokens_ <= 0) {
        return false;
    }
    --render_tokens_;
    return true;
}

void RemoteServer::render_waiting()
{
    while (!render_queue_.empty() && take_render()) {
        ThumbDraw next = std::move(render_queue_.front());
        render_queue_.pop_front();
        draw(next);
    }
    if (render_queue_.empty()) {
        render_timer_.stop();
    } else if (!render_timer_.isActive()) {
        render_timer_.start();
    }
}

void RemoteServer::draw(const ThumbDraw& job)
{
    PendingThumb pending;
    try {
        pending = bridge_.thumb(job.request);
    } catch (const std::exception& ex) {
        qWarning("SeventhSlide remote: thumbnail render failed: %s", ex.what());
        answer_waiting(job.key, internal_error);
        return;
    } catch (...) {
        qWarning("SeventhSlide remote: thumbnail render failed");
        answer_waiting(job.key, internal_error);
        return;
    }
    if (!pending.encode) {
        answer_waiting(job.key, [&job] { return thumb_response(ThumbResult{}, job.revision); });
        return;
    }
    start_encode(job.key, job.revision, std::move(pending));
}

void RemoteServer::finish_encode(const std::string& key,
                                const std::string& revision,
                                std::uint64_t generation,
                                const ThumbResult& thumb)
{
    if (generation != thumb_generation_) {
        return;  // stop() dropped the cache and the requests meanwhile
    }
    thumbs_.put(key, thumb);
    answer_waiting(key, [&thumb, &revision] { return thumb_response(thumb, revision); });
}

void RemoteServer::answer_waiting(const std::string& key,
                                  const std::function<QHttpServerResponse()>& response)
{
    const auto waiting = waiting_.find(key);
    if (waiting == waiting_.end()) {
        return;
    }
    const auto waiters = std::move(waiting->second);
    waiting_.erase(waiting);
    for (const auto& waiter : waiters) {
        waiter->addResult(response());
        waiter->finish();
    }
}

} // namespace seventhslide::remote
