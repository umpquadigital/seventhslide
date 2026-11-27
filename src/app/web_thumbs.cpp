#include "web_thumbs.hpp"

#include "media_thumb_cache.hpp"

#include <seventhslide/scene/scene_frame.hpp>
#include <seventhslide/util/hash.hpp>

#include <QDateTime>
#include <QFileInfo>
#include <QUrl>

#include <system_error>
#include <utility>

namespace seventhslide::app {

WebThumbs::WebThumbs(ThumbsDir dir, QObject* parent)
    : QObject(parent)
    , dir_(std::move(dir))
{
    settle_.setSingleShot(true);
    give_up_.setSingleShot(true);
    connect(&settle_, &QTimer::timeout, this, [this] { finish(true); });
    connect(&give_up_, &QTimer::timeout, this, [this] { finish(false); });
}

WebThumbs::~WebThumbs() = default;

void WebThumbs::set_factory(WebRendererFactory factory)
{
    factory_ = std::move(factory);
    next();
}

std::string WebThumbs::key_of(const model::WebPage& page)
{
    return util::fnv1a64_hex(scene::web_source_key(page.url, page.width, page.height, page.zoom));
}

std::filesystem::path WebThumbs::path_of(const model::WebPage& page) const
{
    const auto dir = dir_ ? dir_() : std::filesystem::path();
    return dir.empty() ? dir : dir / (key_of(page) + ".jpg");
}

QString WebThumbs::url_for(const model::WebPage& page) const
{
    const auto path = path_of(page);
    if (path.empty()) {
        return {};
    }
    const QFileInfo file(QString::fromStdString(path.string()));
    if (!file.exists()) {
        return {};
    }
    QUrl url = QUrl::fromLocalFile(file.absoluteFilePath());
    url.setQuery(QStringLiteral("v=%1").arg(file.lastModified().toMSecsSinceEpoch()));
    return url.toString();
}

void WebThumbs::want(const std::vector<model::WebPage>& pages)
{
    for (const auto& page : pages) {
        if (page.url.empty()) {
            continue;
        }
        const auto key = key_of(page);
        if (asked_.contains(key) || failed_.contains(key)) {
            continue;
        }
        const auto path = path_of(page);
        std::error_code ec;
        if (path.empty() || std::filesystem::exists(path, ec)) {
            continue;
        }
        asked_.insert(key);
        queue_.push_back(page);
    }
    next();
}

void WebThumbs::store(const model::WebPage& page, const QImage& picture)
{
    const auto path = path_of(page);
    if (path.empty() || picture.isNull()) {
        return;
    }
    if (write_thumb_jpeg(picture, k_max_edge, path)) {
        emit made();
    }
}

void WebThumbs::prune(const std::vector<model::WebPage>& pages)
{
    const auto dir = dir_ ? dir_() : std::filesystem::path();
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) {
        return;
    }
    std::set<std::string> kept;
    for (const auto& page : pages) {
        kept.insert(key_of(page) + ".jpg");
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && !kept.contains(entry.path().filename().string())) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

void WebThumbs::stop()
{
    settle_.stop();
    give_up_.stop();
    renderer_.reset();
    queue_.clear();
    asked_.clear();
}

std::size_t WebThumbs::pending() const noexcept
{
    return queue_.size() + (renderer_ ? 1 : 0);
}

void WebThumbs::next()
{
    if (renderer_ || queue_.empty() || !factory_) {
        return;
    }
    current_ = queue_.front();
    queue_.pop_front();
    renderer_ = factory_();
    if (!renderer_) {
        asked_.erase(key_of(current_));
        return;
    }
    // Settled once loaded; a later load (a redirect) starts the wait again.
    connect(renderer_.get(), &WebRenderer::loaded, this, [this] { settle_.start(settle_ms_); });
    give_up_.start(give_up_ms_);
    renderer_->load(QUrl(QString::fromStdString(current_.url)),
                    QSize(current_.width, current_.height), current_.zoom);
}

void WebThumbs::finish(bool keep)
{
    if (!renderer_) {
        return;
    }
    settle_.stop();
    give_up_.stop();
    const QImage picture = renderer_->frame();
    const auto key = key_of(current_);
    asked_.erase(key);
    if (keep && !picture.isNull()) {
        store(current_, picture);
    } else {
        failed_.insert(key);
    }
    // Let go of the renderer once its signal has returned.
    renderer_.release()->deleteLater();
    next();
}

} // namespace seventhslide::app
