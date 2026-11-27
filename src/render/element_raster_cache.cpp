// Retained element rasters; see element_raster_cache.hpp.

#include <seventhslide/render/element_raster_cache.hpp>

#include <QPaintDevice>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string_view>

namespace seventhslide::render {
namespace {

/// Larger than this on either side and an element draws live: a raster that
/// size would cost more memory than its copy saves.
constexpr int k_max_raster_side = 8192;

class Fingerprint {
public:
    Fingerprint& add(std::string_view text)
    {
        mix(std::hash<std::string_view>{}(text));
        return *this;
    }
    Fingerprint& add(double value)
    {
        mix(std::hash<double>{}(value));
        return *this;
    }
    Fingerprint& add(std::uint64_t value)
    {
        mix(value);
        return *this;
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return h_; }

private:
    void mix(std::uint64_t v) noexcept
    {
        h_ ^= v + 0x9e3779b97f4a7c15ull + (h_ << 6) + (h_ >> 2);
    }
    std::uint64_t h_ = 1469598103934665603ull;
};

void add_runs(Fingerprint& f, const model::RichLine& runs)
{
    for (const auto& run : runs) {
        f.add(run.text);
    }
}

}  // namespace

ElementRasterCache::ElementRasterCache(std::size_t budget_bytes)
    : budget_bytes_(budget_bytes)
{
}

void ElementRasterCache::begin_pass()
{
    ++pass_;
    std::erase_if(entries_, [this](const Entry& entry) {
        if (entry.last_pass + k_keep_passes >= pass_) {
            return false;
        }
        bytes_ -= static_cast<std::size_t>(entry.raster.sizeInBytes());
        return true;
    });
}

std::optional<ElementRasterCache::Target> ElementRasterCache::target_for(
    const QPainter& painter, const ElementPlacement& placement) const
{
    // A raster is the element at full strength, copied with SourceOver at
    // the painter's opacity. Any other painter state would make the copy
    // differ from drawing directly; so would an opacity, but for a lone
    // picture on whole pixels (checked below, once its pixels are known).
    const bool translucent = painter.opacity() != 1.0;
    if ((translucent && (!placement.one_picture || painter.hasClipping()))
        || painter.compositionMode() != QPainter::CompositionMode_SourceOver) {
        return std::nullopt;
    }
    const QPaintDevice* device = painter.device();
    if (device == nullptr || placement.bounds.isEmpty()) {
        return std::nullopt;
    }
    const QTransform device_transform = painter.deviceTransform();
    if (device_transform.type() == QTransform::TxProject) {
        return std::nullopt;
    }
    const QRectF covered = device_transform.mapRect(placement.bounds);
    const int left = static_cast<int>(std::floor(covered.left()));
    const int top = static_cast<int>(std::floor(covered.top()));
    const int right = static_cast<int>(std::ceil(covered.right()));
    const int bottom = static_cast<int>(std::ceil(covered.bottom()));
    // Qt's raster engine fills a picture's rectangle on whole pixels at the
    // painter's opacity as it blits; off them its edges are antialiased, and
    // the opacity rounds differently all over. The tests check this; Qt does
    // not promise it.
    if (translucent
        && (device_transform.type() > QTransform::TxScale || left != covered.left()
            || top != covered.top() || right != covered.right()
            || bottom != covered.bottom())) {
        return std::nullopt;
    }
    const QSize size(right - left, bottom - top);
    if (size.isEmpty() || size.width() > k_max_raster_side
        || size.height() > k_max_raster_side) {
        return std::nullopt;
    }
    Target target;
    target.origin = QPoint(left, top);
    target.size = size;
    // The layer differs from the painter only by a whole-pixel offset, which
    // is what keeps the raster identical to the direct draw.
    target.layer = device_transform * QTransform::fromTranslate(-left, -top);
    target.dpr = device->devicePixelRatioF();
    target.hints = static_cast<int>(painter.renderHints());
    return target;
}

QImage ElementRasterCache::render(const Target& target,
                                  const std::function<void(QPainter&)>& paint) const
{
    QImage raster(target.size, QImage::Format_ARGB32_Premultiplied);
    raster.fill(Qt::transparent);
    raster.setDevicePixelRatio(target.dpr);
    QPainter painter(&raster);
    painter.setRenderHints(QPainter::RenderHints(target.hints));
    // Qt applies the device pixel ratio after the world transform.
    painter.setWorldTransform(target.layer
                              * QTransform::fromScale(1.0 / target.dpr, 1.0 / target.dpr));
    paint(painter);
    painter.end();
    return raster;
}

void ElementRasterCache::blit(QPainter& painter,
                              const Target& target,
                              const ElementPlacement& placement,
                              const QImage& raster)
{
    painter.save();
    if (placement.replace_clip) {
        placement.replace_clip(painter);
    }
    // One raster pixel per device pixel, at the painter's opacity: no
    // resampling. The raster is at full strength, so the opacity is applied
    // once, as the direct draw applies it.
    painter.resetTransform();
    painter.drawImage(QPointF(target.origin.x() / target.dpr, target.origin.y() / target.dpr),
                      raster);
    painter.restore();
}

void ElementRasterCache::insert(Entry entry)
{
    bytes_ += static_cast<std::size_t>(entry.raster.sizeInBytes());
    entries_.push_back(std::move(entry));
    // Over budget: release the rasters drawn longest ago, never the new one.
    while (bytes_ > budget_bytes_ && entries_.size() > 1) {
        const auto oldest = std::min_element(
            entries_.begin(), std::prev(entries_.end()),
            [](const Entry& a, const Entry& b) { return a.last_pass < b.last_pass; });
        bytes_ -= static_cast<std::size_t>(oldest->raster.sizeInBytes());
        entries_.erase(oldest);
    }
}

std::uint64_t ElementRasterCache::hash_of(const scene::SceneTextContent& text)
{
    Fingerprint f;
    f.add(std::uint64_t{1}).add(double(text.box.x)).add(double(text.box.y))
        .add(double(text.box.width)).add(double(text.box.height))
        .add(double(text.box.font_size)).add(text.box.font_family);
    for (const auto& line : text.lines) {
        add_runs(f, line.runs);
    }
    return f.value();
}

std::uint64_t ElementRasterCache::hash_of(const layout::OverlayBox& box)
{
    Fingerprint f;
    f.add(std::uint64_t{2}).add(box.x).add(box.y).add(box.w).add(box.h)
        .add(double(box.font_size)).add(box.font_family);
    for (const auto& line : box.lines) {
        add_runs(f, line);
    }
    return f.value();
}

std::uint64_t ElementRasterCache::hash_of(const canvas::Object& object)
{
    Fingerprint f;
    f.add(std::uint64_t{3}).add(object.x).add(object.y).add(object.w).add(object.h)
        .add(object.rotation).add(static_cast<std::uint64_t>(object.type))
        .add(static_cast<std::uint64_t>(object.shape_kind)).add(object.fill.color)
        .add(static_cast<std::uint64_t>(object.fill.kind)).add(object.line.color)
        .add(object.line.width).add(static_cast<std::uint64_t>(object.line.dash))
        .add(object.opacity).add(object.filename);
    return f.value();
}

std::uint64_t ElementRasterCache::hash_of(const scene::SceneBackground& background)
{
    Fingerprint f;
    f.add(std::uint64_t{5})
        .add(static_cast<std::uint64_t>(background.type))
        .add(background.image)
        .add(background.color);
    return f.value();
}

std::uint64_t ElementRasterCache::hash_of(const scene::SceneImageMedia& media)
{
    Fingerprint f;
    f.add(std::uint64_t{6}).add(media.url).add(static_cast<std::uint64_t>(media.index));
    return f.value();
}

}  // namespace seventhslide::render
