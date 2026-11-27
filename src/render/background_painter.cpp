#include <seventhslide/render/background_painter.hpp>

#include <seventhslide/render/cover_fit.hpp>
#include <seventhslide/render/css_color.hpp>
#include <seventhslide/render/element_raster_cache.hpp>

#include <QImageReader>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

namespace seventhslide::render {
namespace {

[[nodiscard]] QSize fit_inside(const QSize& orig, int max_edge)
{
    if (max_edge <= 0 || orig.width() <= 0 || orig.height() <= 0) {
        return orig;
    }
    const int long_side = std::max(orig.width(), orig.height());
    if (long_side <= max_edge) {
        return orig;
    }
    const qreal s = qreal(max_edge) / qreal(long_side);
    return QSize(std::max(1, int(orig.width() * s)), std::max(1, int(orig.height() * s)));
}

[[nodiscard]] int effective_edge(int requested, int default_edge) noexcept
{
    if (requested > 0) {
        return requested;
    }
    return default_edge;
}

}  // namespace

BackgroundImageCache::BackgroundImageCache(Decoder decode)
    : decode_(decode ? std::move(decode) : Decoder(&BackgroundImageCache::read_scaled))
{
}

void BackgroundImageCache::set_static_root(std::filesystem::path root)
{
    QMutexLocker lock(&mutex_);
    if (root == root_) {
        return;
    }
    root_ = std::move(root);
    empty_locked();
    missed_.clear();
}

std::filesystem::path BackgroundImageCache::static_root() const
{
    QMutexLocker lock(&mutex_);
    return root_;
}

void BackgroundImageCache::set_max_edge(int edge) noexcept
{
    max_edge_.store(std::max(0, edge));
}

std::string BackgroundImageCache::cache_key(std::string_view safe_url, int edge)
{
    return std::string(safe_url) + "@" + std::to_string(std::max(0, edge));
}

QImage BackgroundImageCache::read_scaled(const std::filesystem::path& path, int edge)
{
    QImageReader reader(QString::fromStdString(path.string()));
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        return {};
    }
    const QSize orig = reader.size();
    if (orig.isValid() && edge > 0) {
        const QSize scaled = fit_inside(orig, edge);
        if (scaled != orig) {
            reader.setScaledSize(scaled);
        }
    } else if (!orig.isValid() && edge > 0) {
        // Unknown size - bound decode so we never pull a multi‑megapixel buffer blindly.
        reader.setScaledSize(QSize(edge, edge));
    }
    QImage img = reader.read();
    if (img.isNull()) {
        return {};
    }
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void BackgroundImageCache::touch_locked(std::list<Entry>::iterator it)
{
    lru_.splice(lru_.begin(), lru_, it);
}

void BackgroundImageCache::evict_locked()
{
    while (static_cast<int>(lru_.size()) > max_entries_) {
        const auto& doomed = lru_.back();
        index_.erase(doomed.key);
        lru_.pop_back();
    }
}

void BackgroundImageCache::empty_locked()
{
    lru_.clear();
    index_.clear();
    ++generation_;
}

const QImage* BackgroundImageCache::find_locked(std::string_view url,
                                                const std::string& key,
                                                int edge)
{
    if (const auto it = index_.find(key); it != index_.end()) {
        touch_locked(it->second);
        return &it->second->image;
    }
    // A larger decode of the same file serves a smaller request.
    if (edge > 0) {
        for (auto it = lru_.begin(); it != lru_.end(); ++it) {
            if (it->url == url && it->edge >= edge && !it->image.isNull()) {
                touch_locked(it);
                return &it->image;
            }
        }
    }
    return nullptr;
}

void BackgroundImageCache::insert_locked(std::string key,
                                         std::string url,
                                         int edge,
                                         QImage image)
{
    lru_.push_front(Entry{key, std::move(url), edge, std::move(image)});
    index_[std::move(key)] = lru_.begin();
    evict_locked();
}

QImage BackgroundImageCache::acquire(std::string_view url, int edge)
{
    const int use_edge = effective_edge(edge, max_edge_.load());

    // Only a URL that passed safe_static_url is ever cached, so finding the
    // same string again needs no second check: the common case, a hit,
    // skips it.
    QMutexLocker lock(&mutex_);
    if (const QImage* cached = find_locked(url, cache_key(url, use_edge), use_edge)) {
        if (cached->isNull()) {
            missed_locked(std::string(url));
        }
        return *cached;
    }
    lock.unlock();

    const auto safe = safe_static_url(url);
    if (!safe) {
        return {};
    }
    // The checked form (trimmed) may differ from what the caller passed.
    const std::string key = cache_key(*safe, use_edge);

    lock.relock();
    for (;;) {
        if (const QImage* cached = find_locked(*safe, key, use_edge)) {
            if (cached->isNull()) {
                missed_locked(*safe);
            }
            return *cached;
        }
        if (!decoding_.contains(key)) {
            break;
        }
        // Another thread is decoding this one; take its result.
        decoded_.wait(&mutex_);
    }
    decoding_.emplace(key, *safe);
    const std::filesystem::path root = root_;
    const std::uint64_t generation = generation_;
    lock.unlock();

    QImage image;
    try {
        if (const auto path = resolve_static_asset(*safe, root)) {
            image = decode_(*path, use_edge);
        }
    } catch (...) {
        // Release the key, or every later request for it would wait forever.
        lock.relock();
        decoding_.erase(key);
        decoded_.wakeAll();
        throw;
    }

    lock.relock();
    decoding_.erase(key);
    // A missing or unreadable file is cached too (as a null image), so it is
    // not looked for again on every paint.
    if (generation == generation_) {
        insert_locked(key, *safe, use_edge, image);
    }
    if (image.isNull()) {
        missed_locked(*safe);
    }
    decoded_.wakeAll();
    return image;
}

bool BackgroundImageCache::ensure(std::string_view url)
{
    return !acquire(url, max_edge_.load()).isNull();
}

bool BackgroundImageCache::ensure(std::string_view url, int edge)
{
    return !acquire(url, edge).isNull();
}

QImage BackgroundImageCache::get(std::string_view url)
{
    return acquire(url, max_edge_.load());
}

QImage BackgroundImageCache::get(std::string_view url, int edge)
{
    return acquire(url, edge);
}

void BackgroundImageCache::clear()
{
    QMutexLocker lock(&mutex_);
    empty_locked();
}

void BackgroundImageCache::missed_locked(std::string url)
{
    ++misses_;
    missed_.insert(std::move(url));
}

bool BackgroundImageCache::forget_missing(const std::vector<std::string>& urls)
{
    QMutexLocker lock(&mutex_);
    bool lacked = false;
    for (const auto& url : urls) {
        if (missed_.erase(url) > 0) {
            lacked = true;
            const auto lost = [&](const Entry& e) { return e.url == url && e.image.isNull(); };
            std::erase_if(index_, [&](const auto& entry) { return lost(*entry.second); });
            std::erase_if(lru_, lost);
        }
        // A decode that began before the file came may find it missing: not
        // kept, and what waits on it is drawn again.
        if (std::ranges::any_of(decoding_, [&](const auto& entry) { return entry.second == url; })) {
            lacked = true;
            ++generation_;
        }
    }
    return lacked;
}

void paint_background(QPainter& painter,
                      const scene::SceneBackground& bg,
                      const QRectF& dest,
                      const PaintContext& ctx,
                      const QImage* video_frame)
{
    if (!dest.isValid()) {
        return;
    }

    auto paint_cover = [&](QPainter& p, const QImage& img) {
        if (img.isNull()) {
            return;
        }
        const auto c = cover_source(img.width(), img.height(), dest.width(), dest.height());
        if (c.src_w <= 0 || c.src_h <= 0) {
            return;
        }
        p.drawImage(dest, img, QRectF(c.src_x, c.src_y, c.src_w, c.src_h));
    };

    switch (bg.type) {
    case layout::BackgroundType::Transparent:
        return;
    case layout::BackgroundType::Color:
        painter.fillRect(dest, css_color_or(bg.color, Qt::black));
        return;
    case layout::BackgroundType::Video:
        if (video_frame && !video_frame->isNull()) {
            paint_cover(painter, *video_frame);
        } else if (!bg.color.empty()) {
            painter.fillRect(dest, css_color_or(bg.color, Qt::black));
        }
        return;
    case layout::BackgroundType::Image: {
        QImage img;
        if (ctx.cache) {
            img = ctx.cache->get(bg.image);
        }
        if (img.isNull()) {
            return;
        }
        ElementPlacement placement;
        placement.bounds = dest;
        // Nothing but the picture, into ``dest``: a fade copies its raster.
        placement.one_picture = true;
        draw_element(painter, ctx.rasters, bg, ElementInputs{nullptr, img.cacheKey(), dest},
                     placement, [&](QPainter& p) { paint_cover(p, img); });
        return;
    }
    }
}

namespace {

[[nodiscard]] bool canvas_fills_viewport(const CanvasFit& fit) noexcept
{
    return fit.scale == 1.0 && fit.offset_x == 0.0 && fit.offset_y == 0.0
        && fit.canvas_width == fit.viewport_width && fit.canvas_height == fit.viewport_height
        && fit.canvas_width > 0 && fit.canvas_height > 0;
}

[[nodiscard]] bool has_video_frame(const QImage* video_frame) noexcept
{
    return video_frame != nullptr && !video_frame->isNull();
}

/// ``paint_background`` fills the destination edge to edge with opaque pixels.
/// Images may carry alpha, so they never qualify. A video with no decoded
/// frame qualifies only when its poster color is opaque.
[[nodiscard]] bool background_paints_opaque(const scene::SceneBackground& bg,
                                           const QImage* video_frame)
{
    switch (bg.type) {
    case layout::BackgroundType::Color:
        return css_color_or(bg.color, Qt::black).alpha() == 255;
    case layout::BackgroundType::Video:
        return has_video_frame(video_frame)
            || (!bg.color.empty() && css_color_or(bg.color, Qt::black).alpha() == 255);
    case layout::BackgroundType::Transparent:
    case layout::BackgroundType::Image:
        break;
    }
    return false;
}

/// ``paint_background`` leaves the destination untouched.
[[nodiscard]] bool background_paints_nothing(const scene::SceneBackground& bg,
                                            const QImage* video_frame)
{
    switch (bg.type) {
    case layout::BackgroundType::Transparent:
        return true;
    case layout::BackgroundType::Video:
        return !has_video_frame(video_frame) && bg.color.empty();
    case layout::BackgroundType::Color:
    case layout::BackgroundType::Image:
        break;
    }
    return false;
}

}  // namespace

bool canvas_background_covers_viewport(const scene::SceneBackground& bg,
                                       const CanvasFit& fit,
                                       const QImage* video_frame)
{
    if (!canvas_fills_viewport(fit)) {
        return false;
    }
    // Same pixels as painting both passes: an opaque fill covers the viewport
    // pass, and a no-op fill paints in neither pass.
    return background_paints_opaque(bg, video_frame) || background_paints_nothing(bg, video_frame);
}

bool background_hides_stacks(const scene::SceneBackground& bg,
                             const CanvasFit& fit,
                             const QImage* video_frame)
{
    return canvas_fills_viewport(fit) && background_paints_opaque(bg, video_frame);
}

int transparency_checkerboard_cell_px(int width, int height) noexcept
{
    const int m = std::max(1, std::min(width, height));
    return std::clamp(m / 20, 8, 24);
}

void paint_colour_bars(QPainter& painter, const QRectF& dest)
{
    if (!dest.isValid()) {
        return;
    }
    // One band of a row: its colour and its share of the width.
    struct Band {
        QColor colour;
        double share;
    };
    const QColor grey(191, 191, 191);
    const QColor yellow(191, 191, 0);
    const QColor cyan(0, 191, 191);
    const QColor green(0, 191, 0);
    const QColor magenta(191, 0, 191);
    const QColor red(191, 0, 0);
    const QColor blue(0, 0, 191);
    // Black at 7.5 IRE set-up, and the PLUGE's steps 4 IRE either side of it.
    const QColor black(19, 19, 19);
    const QColor below_black(9, 9, 9);
    const QColor above_black(29, 29, 29);
    constexpr double k_bar = 1.0 / 7.0;
    constexpr double k_wide = 5.0 / 28.0;
    constexpr double k_pluge = 1.0 / 21.0;
    const Band top[] = {{grey, k_bar},    {yellow, k_bar}, {cyan, k_bar}, {green, k_bar},
                        {magenta, k_bar}, {red, k_bar},    {blue, k_bar}};
    const Band middle[] = {{blue, k_bar},  {black, k_bar}, {magenta, k_bar}, {black, k_bar},
                           {cyan, k_bar},  {black, k_bar}, {grey, k_bar}};
    const Band foot[] = {{QColor(0, 33, 76), k_wide},   {QColor(255, 255, 255), k_wide},
                         {QColor(50, 0, 106), k_wide},  {black, k_wide},
                         {below_black, k_pluge},        {black, k_pluge},
                         {above_black, k_pluge},        {black, k_bar}};

    painter.save();
    painter.setPen(Qt::NoPen);
    // Bands meet edge to edge: smoothing their edges would leave seams.
    painter.setRenderHint(QPainter::Antialiasing, false);
    const auto paint_row = [&](const auto& bands, double top_share, double bottom_share) {
        const double y0 = dest.top() + dest.height() * top_share;
        const double y1 = dest.top() + dest.height() * bottom_share;
        const std::size_t n = std::size(bands);
        double at = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            // From the running share, so rounding never opens a gap; the last
            // band ends on the area's edge.
            const double x0 = dest.left() + dest.width() * at;
            at += bands[i].share;
            const double x1 = i + 1 == n ? dest.right() : dest.left() + dest.width() * at;
            painter.fillRect(QRectF(QPointF(x0, y0), QPointF(x1, y1)), bands[i].colour);
        }
    };
    constexpr double k_top_end = 2.0 / 3.0;
    constexpr double k_middle_end = 0.75;
    paint_row(top, 0.0, k_top_end);
    paint_row(middle, k_top_end, k_middle_end);
    paint_row(foot, k_middle_end, 1.0);
    painter.restore();
}

void paint_transparency_checkerboard(QPainter& painter, const QRectF& dest, int cell_px)
{
    if (!dest.isValid()) {
        return;
    }
    const int cell = cell_px > 0
        ? cell_px
        : transparency_checkerboard_cell_px(static_cast<int>(dest.width()),
                                            static_cast<int>(dest.height()));
    if (cell <= 0) {
        return;
    }

    const QColor light(0x48, 0x48, 0x48);
    const QColor dark(0x33, 0x33, 0x33);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.fillRect(dest, light);

    const int x0 = static_cast<int>(std::floor(dest.left()));
    const int y0 = static_cast<int>(std::floor(dest.top()));
    const int x1 = static_cast<int>(std::ceil(dest.right()));
    const int y1 = static_cast<int>(std::ceil(dest.bottom()));
    for (int y = y0; y < y1; y += cell) {
        const int row = (y - y0) / cell;
        const int h = std::min(cell, y1 - y);
        for (int x = x0; x < x1; x += cell) {
            const int col = (x - x0) / cell;
            if (((row + col) & 1) == 0) {
                continue;
            }
            painter.fillRect(QRect(x, y, std::min(cell, x1 - x), h), dark);
        }
    }
    painter.restore();
}

QImage composite_on_transparency_checkerboard(const QImage& src)
{
    if (src.isNull()) {
        return src;
    }
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&out);
    paint_transparency_checkerboard(
        painter, QRectF(QPointF(0, 0), QSizeF(src.size())));
    painter.drawImage(0, 0, src);
    painter.end();
    return out;
}

}  // namespace seventhslide::render
