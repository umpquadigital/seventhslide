#pragma once

// Scene background painter - color / image / video / transparent.
// Image + video use CSS ``background-size: cover`` + center.
// Seam: Qt Gui (QPainter / QImage).

#include "seventhslide/render/canvas_fit.hpp"
#include "seventhslide/render/paint_context.hpp"
#include "seventhslide/render/static_assets.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QColor>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QPainter>
#include <QRectF>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace seventhslide::render {

/// Loads and caches ``/static/...`` images under the library media folder.
/// Decodes at most to ``max_edge`` (longest side) and evicts with an LRU so
/// large photo playlists do not keep full-resolution bitmaps in RAM.
///
/// Thread-safe: an output's GUI thread prefetches while its render thread
/// paints. A decode runs outside the lock, so a lookup of an image already
/// cached never waits behind one. A second request for an image being
/// decoded waits for that decode rather than repeating it.
class BackgroundImageCache {
public:
    /// Reads ``path`` scaled so its longest side is at most ``edge`` (0 =
    /// full size). A null image means unreadable.
    using Decoder = std::function<QImage(const std::filesystem::path& path, int edge)>;

    /// ``decode`` defaults to QImageReader; tests pass their own.
    explicit BackgroundImageCache(Decoder decode = {});

    void set_static_root(std::filesystem::path root);
    [[nodiscard]] std::filesystem::path static_root() const;

    /// Longest side for decode (default 2560). 0 decodes at full resolution.
    void set_max_edge(int edge) noexcept;

    /// Prefetch into cache (no-op if unsafe / missing). Returns true when a
    /// non-null image is available afterwards.
    bool ensure(std::string_view url);
    /// Like ``ensure``, but decode for at most ``edge`` (max with ``max_edge_``).
    bool ensure(std::string_view url, int edge);

    /// Cached image or null image if unavailable.
    [[nodiscard]] QImage get(std::string_view url);
    [[nodiscard]] QImage get(std::string_view url, int edge);

    void clear();
    /// Pictures ``urls`` ("/static/<folder>/<name>") have arrived: those found
    /// missing are looked for again. True if anything asked for one of them
    /// without getting it, or may yet (a decode under way).
    bool forget_missing(const std::vector<std::string>& urls);
    /// How many times a picture asked for was missing or unreadable, ever.
    /// A paint that saw it move drew without one. Any thread.
    [[nodiscard]] std::uint64_t misses() const noexcept { return misses_.load(); }

private:
    struct Entry {
        std::string key;
        /// The checked URL. Compared whole: a key cannot be split back into
        /// URL and edge, since "@" is legal in a file name.
        std::string url;
        int edge = 0;
        QImage image;
    };

    [[nodiscard]] static std::string cache_key(std::string_view safe_url, int edge);
    [[nodiscard]] static QImage read_scaled(const std::filesystem::path& path, int edge);
    /// The image for ``url`` at ``edge``: cached, or decoded and cached.
    [[nodiscard]] QImage acquire(std::string_view url, int edge);
    /// The cached decode under ``key``, else a larger one of ``url``
    /// (either touched as most recent). nullptr when neither is cached; a
    /// cached null image means the file is missing or unreadable.
    [[nodiscard]] const QImage* find_locked(std::string_view url,
                                            const std::string& key,
                                            int edge);
    void insert_locked(std::string key, std::string url, int edge, QImage image);
    void empty_locked();
    void touch_locked(std::list<Entry>::iterator it);
    void evict_locked();
    /// ``url`` was asked for and is missing or unreadable.
    void missed_locked(std::string url);

    const Decoder decode_;
    std::filesystem::path root_;
    std::atomic<int> max_edge_ = 2560;
    std::atomic<std::uint64_t> misses_ = 0;
    int max_entries_ = 32;
    mutable QMutex mutex_;
    QWaitCondition decoded_;
    std::list<Entry> lru_;
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
    /// Keys being decoded outside the lock, with their URLs.
    std::unordered_map<std::string, std::string> decoding_;
    /// The URLs found missing or unreadable since ``forget_missing`` was last
    /// told of them, whether or not still cached.
    std::unordered_set<std::string> missed_;
    /// Bumped whenever the cache is emptied, so a decode that started before
    /// (perhaps from the previous library's folder) is not published.
    std::uint64_t generation_ = 0;
};

/**
 * Paint ``bg`` into ``dest`` (already in painter coords).
 *
 * - ``color`` -> solid fill
 * - ``image`` -> cover-scaled image (transparent if load fails)
 * - ``video`` -> cover-scaled ``video_frame``; else solid ``color`` as poster
 * - ``transparent`` / other -> no fill
 *
 * Does not clear ``dest`` for transparent. Without ``ctx.cache`` an image
 * background paints nothing. A picture is one element of ``ctx.rasters``, so
 * it is scaled once per destination, not every frame; colour and video paint
 * live.
 */
void paint_background(QPainter& painter,
                      const scene::SceneBackground& bg,
                      const QRectF& dest,
                      const PaintContext& ctx = {},
                      const QImage* video_frame = nullptr);

/**
 * True when the viewport background pass would not change the picture.
 *
 * The canvas already fills the viewport, and the fill is either opaque (the
 * canvas pass paints over the viewport pass completely) or a no-op (neither
 * pass paints). Video frames are treated as opaque. Image backgrounds may
 * carry alpha and never qualify. Only valid at full painter opacity.
 *
 * This does not mean the stacks underneath are covered. A transparent fill,
 * and a video with neither a frame nor a poster, paint nothing: skipping the
 * viewport pass is safe, and whatever was painted before this background stays
 * visible. Stack culling uses ``background_hides_stacks``.
 */
[[nodiscard]] bool canvas_background_covers_viewport(const scene::SceneBackground& bg,
                                                     const CanvasFit& fit,
                                                     const QImage* video_frame);

/**
 * True when ``bg`` paints opaque pixels over the whole viewport, so layers
 * stacked underneath would be invisible.
 *
 * False for a transparent fill, a video that has neither a frame nor an
 * opaque poster, an image (it may carry alpha), a translucent color, and
 * any fit that is not exact. A no-op fill still returns false here: it skips
 * the redundant viewport pass and leaves the stacks showing.
 */
[[nodiscard]] bool background_hides_stacks(const scene::SceneBackground& bg,
                                           const CanvasFit& fit,
                                           const QImage* video_frame);

/// Cell size for a transparency grid that stays readable at thumb and stage sizes.
[[nodiscard]] int transparency_checkerboard_cell_px(int width, int height) noexcept;

/**
 * Paint a dark-UI transparency grid into ``dest``. ``cell_px`` <= 0 chooses
 * ``transparency_checkerboard_cell_px`` from ``dest`` size.
 */
void paint_transparency_checkerboard(QPainter& painter,
                                     const QRectF& dest,
                                     int cell_px = 0);

/**
 * Colour bars filling ``dest``, laid out after SMPTE's (EG 1): seven 75 %
 * bars over two thirds of the height, the reversed blue row under them, and
 * the -I, white, +Q and PLUGE row at the foot. Every band is a share of
 * ``dest``, so they fit an area of any size and shape. What a design preview
 * shows in a picture's or video's area, where the output's media will be.
 */
void paint_colour_bars(QPainter& painter, const QRectF& dest);

/// Composite ``src`` over a checkerboard (theme preview isolation surface).
[[nodiscard]] QImage composite_on_transparency_checkerboard(const QImage& src);

}  // namespace seventhslide::render
