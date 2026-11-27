#pragma once

// Retained rasters of scene elements.
//
// A destination re-composites its whole slide whenever anything on it moves:
// every new background-video frame, every clock second, every fade step. Most
// of the slide - its text, shapes, pictures, background picture - has not
// changed, yet drawing it from scratch is where the time goes: text over
// 64 device px is filled glyph by glyph, shapes are path fills, pictures are
// rescaled.
//
// So each static element is drawn once into a raster of the device pixels it
// covers, at the same sub-pixel phase, and every later composite copies that
// raster. The copy is pixel-identical to drawing the element directly: only an
// integer translation separates the two.
//
// An entry is found by the element's own value and everything else its pixels
// depend on (fonts, decoded picture, device transform, render hints), compared
// exactly. A changed element is a different entry, so nothing ever needs
// invalidating; entries that stop being drawn age out.
//
// Draws live (and caches nothing) whenever a raster could differ from the
// direct draw: the painter is translucent (a fade without fade layers), uses
// another composition mode, or has a projective transform. One exception: a
// lone picture on whole device pixels (``ElementPlacement::one_picture``)
// fades by copying its raster at the painter's opacity, which Qt blends
// exactly as it would the picture drawn there.
//
// Seam: Qt Gui. Use from the thread that paints with it.

#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/scene/scene_frame.hpp"

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRectF>
#include <QTransform>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace seventhslide::render {

/// What, besides the element itself, decides its pixels. Compared exactly.
struct ElementInputs {
    const void* faces = nullptr;
    /// ``QImage::cacheKey`` of the decoded picture the element draws, if any.
    qint64 image_key = 0;
    /// A rectangle the element is drawn into that it does not carry itself
    /// (a background's destination, an image area).
    QRectF rect;

    [[nodiscard]] bool operator==(const ElementInputs&) const noexcept = default;
};

/// Where an element paints, in the painter's coordinates when it is drawn.
struct ElementPlacement {
    /// Everything the element can touch (a little more is fine). Pixels
    /// outside are never drawn.
    QRectF bounds;
    /// Only for an element that *replaces* the painter's clip with its own (a
    /// text box clips to its box, dropping any outer clip): re-applies that
    /// replacement, with the same painter call, when the raster is copied. An
    /// element that only narrows the clip leaves this empty - its raster is
    /// already clipped, and clipping the copy again would apply an
    /// anti-aliased edge twice.
    std::function<void(QPainter&)> replace_clip;
    /// The element is one picture drawn into exactly ``bounds``, and nothing
    /// else (a background picture). Drawn at an opacity onto whole device
    /// pixels, unclipped and unturned, a picture comes out as its raster
    /// copied at that opacity, so a fade copies it rather than scaling the
    /// picture again each frame. Off whole pixels the two round apart, by a
    /// few levels, and the picture is drawn live. That the copy matches the
    /// live draw is how Qt's raster paint engine behaves, checked by the
    /// tests across every opacity step; Qt does not promise it, and another
    /// paint engine may round otherwise. An element that paints over itself
    /// (a plate under its picture, a fill under a line) blends inside its
    /// raster first and must not set this.
    bool one_picture = false;
};

class ElementRasterCache {
public:
    using Element = std::variant<scene::SceneTextContent,
                                 layout::OverlayBox,
                                 canvas::Object,
                                 scene::SceneBackground,
                                 scene::SceneImageMedia>;

    /// Rasters kept while unused for this many composites (a few seconds at
    /// video rate), so a cut back to the previous slide is still warm.
    static constexpr std::uint64_t k_keep_passes = 120;
    static constexpr std::size_t k_default_budget_bytes = 192u * 1024u * 1024u;

    explicit ElementRasterCache(std::size_t budget_bytes = k_default_budget_bytes);

    /// A composite starts: rasters not drawn in the last ``k_keep_passes``
    /// composites are released.
    void begin_pass();

    /// Draw ``element`` through the cache: copy its raster when one matches,
    /// else draw it with ``paint`` once into a new raster. ``paint`` draws the
    /// element in the painter's current coordinates, exactly as it would
    /// without a cache. Falls back to calling ``paint`` on ``painter`` when a
    /// raster could not reproduce the direct draw exactly.
    template <typename T>
    void draw(QPainter& painter,
              const T& element,
              const ElementInputs& inputs,
              const ElementPlacement& placement,
              const std::function<void(QPainter&)>& paint);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
    /// Rasters drawn from scratch / copied, since construction.
    [[nodiscard]] std::uint64_t misses() const noexcept { return misses_; }
    [[nodiscard]] std::uint64_t hits() const noexcept { return hits_; }

private:
    /// Device-space placement of one draw.
    struct Target {
        /// The layer's device transform: the painter's, less the integer
        /// device offset ``origin``.
        QTransform layer;
        QPoint origin;
        QSize size;
        qreal dpr = 1.0;
        int hints = 0;
    };

    struct Entry {
        std::uint64_t hash = 0;
        Element element;
        ElementInputs inputs;
        QTransform layer;
        int hints = 0;
        QImage raster;
        std::uint64_t last_pass = 0;
    };

    [[nodiscard]] std::optional<Target> target_for(const QPainter& painter,
                                                   const ElementPlacement& placement) const;
    [[nodiscard]] QImage render(const Target& target,
                                const std::function<void(QPainter&)>& paint) const;
    static void blit(QPainter& painter,
                     const Target& target,
                     const ElementPlacement& placement,
                     const QImage& raster);
    void insert(Entry entry);
    // Cheap fingerprints that pick the candidates; equality decides.
    [[nodiscard]] static std::uint64_t hash_of(const scene::SceneTextContent& text);
    [[nodiscard]] static std::uint64_t hash_of(const layout::OverlayBox& box);
    [[nodiscard]] static std::uint64_t hash_of(const canvas::Object& object);
    [[nodiscard]] static std::uint64_t hash_of(const scene::SceneBackground& background);
    [[nodiscard]] static std::uint64_t hash_of(const scene::SceneImageMedia& media);

    std::size_t budget_bytes_;
    std::size_t bytes_ = 0;
    std::uint64_t pass_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t hits_ = 0;
    std::vector<Entry> entries_;
};

template <typename T>
void ElementRasterCache::draw(QPainter& painter,
                              const T& element,
                              const ElementInputs& inputs,
                              const ElementPlacement& placement,
                              const std::function<void(QPainter&)>& paint)
{
    const auto target = target_for(painter, placement);
    if (!target) {
        paint(painter);
        return;
    }
    // Hashed and compared in place: a hit copies nothing.
    const std::uint64_t hash = hash_of(element);
    for (auto& entry : entries_) {
        if (entry.hash == hash && entry.hints == target->hints && entry.layer == target->layer
            && entry.inputs == inputs && std::holds_alternative<T>(entry.element)
            && std::get<T>(entry.element) == element) {
            entry.last_pass = pass_;
            ++hits_;
            blit(painter, *target, placement, entry.raster);
            return;
        }
    }
    ++misses_;
    Entry entry{hash, Element(std::in_place_type<T>, element), inputs, target->layer,
                target->hints, render(*target, paint), pass_};
    blit(painter, *target, placement, entry.raster);
    insert(std::move(entry));
}

/// ``cache->draw(...)`` when there is a cache, else ``paint(painter)``.
template <typename T>
void draw_element(QPainter& painter,
                  ElementRasterCache* cache,
                  const T& element,
                  const ElementInputs& inputs,
                  const ElementPlacement& placement,
                  const std::function<void(QPainter&)>& paint)
{
    if (cache == nullptr) {
        paint(painter);
        return;
    }
    cache->draw(painter, element, inputs, placement, paint);
}

}  // namespace seventhslide::render
