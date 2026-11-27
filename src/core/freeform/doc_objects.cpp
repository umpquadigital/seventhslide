// Objects on a slide: stacking, construction, canvas mapping, geometry,
// groups and locks.

#include "seventhslide/freeform/doc_edit.hpp"

#include "seventhslide/canvas/object_list.hpp"
#include "seventhslide/freeform/paint.hpp"
#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/util/find.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace seventhslide::freeform {
namespace {

/// Size ``obj`` to fractions of the reference canvas, centred horizontally and
/// a little above centre.
void place_centered(Object& obj, double frac_w, double frac_h)
{
    const double cw = geom::k_reference_canvas.width;
    const double ch = geom::k_reference_canvas.height;
    obj.w = std::max(40.0, cw * frac_w);
    obj.h = std::max(40.0, ch * frac_h);
    obj.x = (cw - obj.w) * 0.5;
    obj.y = (ch - obj.h) * 0.36;
}

/// ``obj`` was designed on the reference canvas; place it on ``canvas``.
[[nodiscard]] Object on_canvas(Object obj, geom::CanvasSize canvas)
{
    resize_object(obj, geom::k_reference_canvas, canvas);
    return obj;
}

} // namespace

void normalize_object_order(Slide& slide)
{
    std::stable_sort(slide.objects.begin(), slide.objects.end(),
                     [](const Object& a, const Object& b) { return a.z < b.z; });
    int z = 0;
    for (auto& obj : slide.objects) {
        obj.z = z++;
    }
}

namespace {

/// Slide ``slide_index`` of ``output_id``'s deck in ``doc``, or null; const
/// when ``doc`` is.
template <typename Doc>
auto slide_in(Doc& doc, std::string_view output_id, int slide_index)
{
    auto* deck = find_deck(doc, output_id);
    if (deck == nullptr || slide_index < 0 || slide_index >= deck->slide_count()) {
        return decltype(&deck->slides.front()){nullptr};
    }
    return &deck->slides[static_cast<std::size_t>(slide_index)];
}

/// Start ``obj``'s size and alignment on what its kind paints by default.
void set_type_defaults(Object& obj)
{
    const auto defaults = type_defaults(obj.type);
    obj.font_size = defaults.font_size;
    obj.text_align = defaults.text_align;
    obj.vertical_align = defaults.vertical_align;
}

/// A new indicator or clock: chrome one line high (its kind's type size),
/// from x = 10 to the right edge of the reference canvas, at ``y``.
[[nodiscard]] Object make_chrome_object(Document& doc, ObjectType type, double y)
{
    Object obj;
    obj.id = alloc_id(doc, 'o');
    obj.type = type;
    set_type_defaults(obj);
    obj.x = 10;
    obj.y = y;
    obj.w = geom::k_reference_canvas.width - obj.x;
    obj.h = obj.font_size * 1.4;
    return obj;
}

}  // namespace

Slide* find_slide(Document& doc, std::string_view output_id, int slide_index)
{
    return slide_in(doc, output_id, slide_index);
}

const Slide* find_slide(const Document& doc, std::string_view output_id, int slide_index)
{
    return slide_in(doc, output_id, slide_index);
}

Object* find_object(Document& doc, std::string_view output_id, int slide_index,
                    std::string_view object_id)
{
    auto* slide = find_slide(doc, output_id, slide_index);
    return slide != nullptr ? util::find_by(slide->objects, object_id, &Object::id) : nullptr;
}

const Object* find_object(const Document& doc, std::string_view output_id, int slide_index,
                          std::string_view object_id)
{
    const auto* slide = find_slide(doc, output_id, slide_index);
    return slide != nullptr ? util::find_by(slide->objects, object_id, &Object::id) : nullptr;
}

Object make_text_object(Document& doc, geom::CanvasSize canvas)
{
    Object obj;
    obj.id = alloc_id(doc, 'o');
    obj.type = ObjectType::Text;
    place_centered(obj, 0.84, 0.28);
    obj.y = geom::k_reference_canvas.height * 0.18;
    obj.font_size = 64;
    obj.text_align = "center";
    obj.vertical_align = "middle";
    return on_canvas(std::move(obj), canvas);
}

Object make_image_object(Document& doc, std::string filename, geom::CanvasSize canvas)
{
    Object obj;
    obj.id = alloc_id(doc, 'o');
    obj.type = ObjectType::Image;
    obj.filename = std::move(filename);
    obj.object_fit = "contain";
    place_centered(obj, 0.42, 0.42);
    return on_canvas(std::move(obj), canvas);
}

Object make_video_object(Document& doc, std::string filename, geom::CanvasSize canvas)
{
    Object obj;
    obj.id = alloc_id(doc, 'o');
    obj.type = ObjectType::Video;
    obj.filename = std::move(filename);
    obj.object_fit = "contain";
    // Half the reference canvas each way: 16:9, as most videos are.
    place_centered(obj, 0.5, 0.5);
    return on_canvas(std::move(obj), canvas);
}

Object make_table_object(Document& doc, int rows, int columns, geom::CanvasSize canvas)
{
    Object obj;
    obj.id = alloc_id(doc, 'o');
    obj.type = ObjectType::Table;
    obj.table.rows = std::clamp(rows, 1, canvas::k_max_table_side);
    obj.table.columns = std::clamp(columns, 1, canvas::k_max_table_side);
    obj.table.cells.resize(static_cast<std::size_t>(obj.table.rows) * static_cast<std::size_t>(obj.table.columns));
    obj.table.header = true;
    obj.table.border_color = "#ffffff";
    obj.table.border_width = 2;
    obj.font_size = 32;
    obj.font_color = "#ffffff";
    obj.text_align = "left";
    obj.vertical_align = "middle";
    // 60% of the canvas wide; 12% of its height for each row, the whole table
    // at most 90% of it.
    place_centered(obj, 0.6, std::min(0.9, 0.12 * obj.table.rows));
    return on_canvas(std::move(obj), canvas);
}

std::vector<geom::PathPoint> distinct_points(const std::vector<geom::PathPoint>& points)
{
    std::vector<geom::PathPoint> kept;
    for (const auto& pt : points) {
        if (kept.empty() || std::hypot(pt.x - kept.back().x, pt.y - kept.back().y) >= 2.0) {
            kept.push_back(pt);
        }
    }
    return kept;
}

void fit_drawn_box(canvas::Object& obj)
{
    const auto outline = geom::build_shape_path(obj.shape_kind, obj.w, obj.h, obj.shape_adjust);
    if (!geom::uses_path(obj.shape_kind) || outline.empty()) {
        return;
    }
    constexpr double k_min_extent = 8.0;
    constexpr int k_samples = 24;
    double left = outline.subpaths.front().start.x;
    double right = left;
    double top = outline.subpaths.front().start.y;
    double bottom = top;
    const auto reach = [&](double x, double y) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    };
    for (const auto& sub : outline.subpaths) {
        geom::ShapePoint from = sub.start;
        for (const auto& seg : sub.segments) {
            if (seg.kind == geom::SegmentKind::Cubic) {
                for (int i = 1; i < k_samples; ++i) {
                    const double t = static_cast<double>(i) / k_samples;
                    const double u = 1.0 - t;
                    const auto on = [&](double a, double b, double c, double d) {
                        return u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d;
                    };
                    reach(on(from.x, seg.c1.x, seg.c2.x, seg.to.x), on(from.y, seg.c1.y, seg.c2.y, seg.to.y));
                }
            }
            reach(seg.to.x, seg.to.y);
            from = seg.to;
        }
    }
    // A box too thin grows about its middle.
    if (right - left < k_min_extent) {
        const double mid = (left + right) * 0.5;
        left = mid - k_min_extent * 0.5;
        right = mid + k_min_extent * 0.5;
    }
    if (bottom - top < k_min_extent) {
        const double mid = (top + bottom) * 0.5;
        top = mid - k_min_extent * 0.5;
        bottom = mid + k_min_extent * 0.5;
    }
    for (auto& pt : obj.shape_adjust.path) {
        pt.x = (pt.x * obj.w - left) / (right - left);
        pt.y = (pt.y * obj.h - top) / (bottom - top);
    }
    obj.x += left;
    obj.y += top;
    obj.w = right - left;
    obj.h = bottom - top;
}

Object make_drawn_object(Document& doc, std::vector<geom::PathPoint> points, bool closed)
{
    double left = points.front().x;
    double right = left;
    double top = points.front().y;
    double bottom = top;
    for (const auto& pt : points) {
        left = std::min(left, pt.x);
        right = std::max(right, pt.x);
        top = std::min(top, pt.y);
        bottom = std::max(bottom, pt.y);
    }
    Object obj;
    obj.id = alloc_id(doc, 'o');
    obj.type = ObjectType::Shape;
    obj.shape_kind = geom::ShapeKind::Drawn;
    obj.x = left;
    obj.y = top;
    // The points' own box, any width at all; fit_drawn_box settles it.
    obj.w = std::max(1.0, right - left);
    obj.h = std::max(1.0, bottom - top);
    for (auto& pt : points) {
        pt.x = (pt.x - left) / obj.w;
        pt.y = (pt.y - top) / obj.h;
    }
    obj.shape_adjust.path = std::move(points);
    obj.shape_adjust.closed = closed && obj.shape_adjust.path.size() >= 3;
    fit_drawn_box(obj);
    if (obj.shape_adjust.closed) {
        obj.fill.color = k_default_shape_fill;
        obj.line.color = k_default_shape_stroke;
    } else {
        obj.fill.kind = canvas::FillKind::None;
        obj.line.color = "#ffffff";
        obj.line.width = 4;
    }
    return obj;
}

Object make_shape_object(Document& doc, geom::ShapeKind kind, geom::CanvasSize canvas)
{
    Object obj;
    obj.id = alloc_id(doc, 'o');
    obj.type = ObjectType::Shape;
    obj.fill.color = k_default_shape_fill;
    obj.line.color = k_default_shape_stroke;
    obj.shape_kind = kind;
    obj.shape_adjust = geom::clamp_adjust(kind, obj.shape_adjust);
    // Radial shapes read as squashed in a wide default box; start them square.
    const bool radial = kind == geom::ShapeKind::Ellipse || kind == geom::ShapeKind::Polygon
        || kind == geom::ShapeKind::Star || kind == geom::ShapeKind::Rhombus
        || kind == geom::ShapeKind::Cloud;
    if (radial) {
        place_centered(obj, 0.22, 0.39);
    } else {
        place_centered(obj, 0.32, 0.22);
    }
    return on_canvas(std::move(obj), canvas);
}

Object make_indicator_object(Document& doc, geom::CanvasSize canvas)
{
    // Along the bottom.
    Object obj = make_chrome_object(doc, ObjectType::Indicator, 1000);
    obj.indicator_fallback = IndicatorFallback::Slide;
    return on_canvas(std::move(obj), canvas);
}

Object make_clock_object(Document& doc, geom::CanvasSize canvas)
{
    // Along the top.
    Object obj = make_chrome_object(doc, ObjectType::Clock, 10);
    obj.clock_24h = false;
    obj.clock_seconds = false;
    return on_canvas(std::move(obj), canvas);
}

void map_object(Object& obj, const geom::CanvasMapping& mapping)
{
    if (mapping.is_identity()) {
        return;
    }
    canvas::map_object(obj, mapping);
    obj.line_gap = layout::clamp_overlay_line_gap(obj.line_gap);
    obj.letter_spacing = layout::clamp_overlay_letter_spacing(obj.letter_spacing);
    obj.upcoming_font_size = mapping.font_size(obj.upcoming_font_size);
}

void map_object_json(nlohmann::json& obj, const geom::CanvasMapping& mapping)
{
    if (!obj.is_object() || mapping.is_identity()) {
        return;
    }
    canvas::map_object_json(obj, mapping);
    // The same clamps and fields as map_object.
    const auto update = [&obj](const char* key, auto&& fn) {
        const auto it = obj.find(key);
        if (it != obj.end() && it->is_number()) {
            *it = fn(it->template get<double>());
        }
    };
    const auto whole = [](double v) { return static_cast<int>(std::lround(v)); };
    update("line_gap", [&](double v) { return layout::clamp_overlay_line_gap(whole(v)); });
    update("letter_spacing",
           [&](double v) { return layout::clamp_overlay_letter_spacing(v); });
    update("upcoming_font_size", [&](double v) { return mapping.font_size(whole(v)); });
}

void resize_object(Object& obj, geom::CanvasSize from, geom::CanvasSize to)
{
    const canvas::BoxRect before = obj;
    map_object(obj, geom::CanvasMapping::resize(from, to));
    canvas::keep_proportions(obj, before, from, to);
}

void resize_object_json(nlohmann::json& obj, geom::CanvasSize from, geom::CanvasSize to)
{
    if (!obj.is_object()) {
        return;
    }
    const canvas::BoxRect before = canvas::frame_of_json(obj);
    map_object_json(obj, geom::CanvasMapping::resize(from, to));
    canvas::keep_proportions_json(obj, before, from, to);
}

namespace {

/// True when ``deck`` would move to be laid out on ``canvas``: the canvas is
/// usable and the deck is on another one.
[[nodiscard]] bool deck_needs_fit(const Deck& deck, geom::CanvasSize canvas) noexcept
{
    return canvas.valid() && deck.canvas != canvas;
}

/// The canvas ``deck`` has to move onto, if any. A deck whose output is not in
/// ``canvases`` stays where it is.
[[nodiscard]] std::optional<geom::CanvasSize> pending_fit(const Deck& deck,
                                                          const DeckCanvases& canvases)
{
    const auto it = canvases.find(deck.output_id);
    if (it == canvases.end() || !deck_needs_fit(deck, it->second)) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace

bool resize_deck(Deck& deck, geom::CanvasSize canvas)
{
    if (!deck_needs_fit(deck, canvas)) {
        return false;
    }
    const auto mapping = geom::CanvasMapping::resize(deck.canvas, canvas);
    for (auto& slide : deck.slides) {
        for (auto& obj : slide.objects) {
            resize_object(obj, deck.canvas, canvas);
        }
    }
    for (auto& flow : deck.cue_flow) {
        flow.gap_above = mapping.pixels(flow.gap_above);
    }
    deck.canvas = canvas;
    return true;
}

void fit_deck_to_canvas(Deck& deck, geom::CanvasSize canvas)
{
    if (!deck_needs_fit(deck, canvas)) {
        return;
    }
    const auto mapping = geom::CanvasMapping::stretch(deck.canvas, canvas);
    for (auto& slide : deck.slides) {
        for (auto& obj : slide.objects) {
            map_object(obj, mapping);
        }
    }
    for (auto& flow : deck.cue_flow) {
        flow.gap_above = mapping.pixels(flow.gap_above);
    }
    deck.canvas = canvas;
}

bool resize_output_deck(Document& doc, std::string_view output_id, geom::CanvasSize canvas)
{
    auto* deck = find_deck(doc, output_id);
    return deck != nullptr && resize_deck(*deck, canvas);
}

bool fit_decks_to_canvases(Document& doc, const DeckCanvases& canvases)
{
    bool changed = false;
    for (auto& deck : doc.decks) {
        if (const auto canvas = pending_fit(deck, canvases)) {
            fit_deck_to_canvas(deck, *canvas);
            changed = true;
        }
    }
    return changed;
}

bool decks_fit_canvases(const Document& doc, const DeckCanvases& canvases)
{
    return std::none_of(doc.decks.begin(), doc.decks.end(),
                        [&](const Deck& deck) { return pending_fit(deck, canvases).has_value(); });
}

} // namespace seventhslide::freeform
