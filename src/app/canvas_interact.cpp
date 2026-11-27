#include "canvas_interact.hpp"

#include "qml_ids.hpp"

#include <seventhslide/canvas/interact.hpp>
#include <seventhslide/canvas/layout.hpp>

#include <QCursor>
#include <QStringList>

#include <vector>

namespace seventhslide::app {
namespace {

[[nodiscard]] canvas::Rect rect_from_map(const QVariantMap& m)
{
    canvas::Rect r;
    r.id = m.value(QStringLiteral("id")).toString().toStdString();
    r.x = m.value(QStringLiteral("x")).toDouble();
    r.y = m.value(QStringLiteral("y")).toDouble();
    r.w = m.value(QStringLiteral("w"), m.value(QStringLiteral("width"))).toDouble();
    r.h = m.value(QStringLiteral("h"), m.value(QStringLiteral("height"))).toDouble();
    r.rotation = m.value(QStringLiteral("rotation")).toDouble();
    r.locked = m.value(QStringLiteral("locked")).toBool();
    r.flip_h = m.value(QStringLiteral("flipH"), m.value(QStringLiteral("flip_h"))).toBool();
    r.flip_v = m.value(QStringLiteral("flipV"), m.value(QStringLiteral("flip_v"))).toBool();
    return r;
}

[[nodiscard]] QVariantMap map_from_rect(const canvas::Rect& r)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromStdString(r.id));
    m.insert(QStringLiteral("x"), r.x);
    m.insert(QStringLiteral("y"), r.y);
    m.insert(QStringLiteral("w"), r.w);
    m.insert(QStringLiteral("h"), r.h);
    m.insert(QStringLiteral("rotation"), r.rotation);
    m.insert(QStringLiteral("locked"), r.locked);
    m.insert(QStringLiteral("flipH"), r.flip_h);
    m.insert(QStringLiteral("flipV"), r.flip_v);
    return m;
}

[[nodiscard]] QVariantList list_from_rects(const std::vector<canvas::Rect>& rects)
{
    QVariantList out;
    out.reserve(static_cast<int>(rects.size()));
    for (const auto& r : rects) {
        out.push_back(map_from_rect(r));
    }
    return out;
}

[[nodiscard]] std::vector<canvas::Rect> rects_from_list(const QVariantList& list)
{
    std::vector<canvas::Rect> out;
    out.reserve(static_cast<std::size_t>(list.size()));
    for (const auto& row : list) {
        out.push_back(rect_from_map(row.toMap()));
    }
    return out;
}

[[nodiscard]] canvas::Limits limits_of(const CanvasInteract& self)
{
    canvas::Limits lim;
    lim.snap_threshold = self.snapThreshold();
    lim.handle_hit = self.handleHit();
    return lim;
}

} // namespace

CanvasInteract::CanvasInteract(QObject* parent)
    : QObject(parent)
{
}

void CanvasInteract::setCanvasWidth(int w)
{
    const int next = w > 0 ? w : geom::k_reference_canvas.width;
    if (canvas_w_ == next) {
        return;
    }
    canvas_w_ = next;
    emit canvasChanged();
}

void CanvasInteract::setCanvasHeight(int h)
{
    const int next = h > 0 ? h : geom::k_reference_canvas.height;
    if (canvas_h_ == next) {
        return;
    }
    canvas_h_ = next;
    emit canvasChanged();
}

void CanvasInteract::setSnapThreshold(qreal v)
{
    if (snap_threshold_ == v) {
        return;
    }
    snap_threshold_ = v;
    emit canvasChanged();
}

void CanvasInteract::setHandleHit(qreal v)
{
    if (handle_hit_ == v) {
        return;
    }
    handle_hit_ = v;
    emit canvasChanged();
}

void CanvasInteract::setObjects(const QVariantList& objects)
{
    if (objects_ == objects) {
        return;
    }
    objects_ = objects;
    emit objectsChanged();
}

QString CanvasInteract::hitHandle(qreal stageX, qreal stageY, const QVariantMap& rect,
                                  qreal scale) const
{
    const auto handle =
        canvas::hit_handle(stageX, stageY, rect_from_map(rect), scale, handle_hit_);
    return QString::fromUtf8(canvas::to_string(handle));
}

QString CanvasInteract::hitObject(qreal canvasX, qreal canvasY) const
{
    const auto objects = rects_from_list(objects_);
    const auto* hit = canvas::hit_object(canvasX, canvasY, objects);
    return hit ? QString::fromStdString(hit->id) : QString();
}

bool CanvasInteract::pointInRect(qreal canvasX, qreal canvasY, const QVariantMap& rect) const
{
    return canvas::point_in_rect(canvasX, canvasY, rect_from_map(rect));
}

QStringList CanvasInteract::hitMarquee(qreal x, qreal y, qreal w, qreal h) const
{
    const auto ids = canvas::hit_marquee(canvas::Rect{"", x, y, w, h}, rects_from_list(objects_));
    QStringList out;
    out.reserve(static_cast<int>(ids.size()));
    for (const auto& id : ids) {
        out.push_back(QString::fromStdString(id));
    }
    return out;
}

QVariantMap CanvasInteract::unionBounds(const QVariantList& rects) const
{
    return map_from_rect(canvas::union_bounds(rects_from_list(rects)));
}

QVariantList CanvasInteract::geometryFromDragMany(const QString& mode, const QVariantList& start,
                                                  qreal dx, qreal dy) const
{
    const auto handle = canvas::handle_from_string(mode.toStdString());
    return list_from_rects(canvas::geometry_from_drag_many(
        handle, rects_from_list(start), dx, dy, {canvas_w_, canvas_h_}, limits_of(*this)));
}

QVariantMap CanvasInteract::snapGeometryMany(const QVariantList& rects, const QString& mode,
                                             const QStringList& excludeIds, bool enabled) const
{
    const geom::CanvasSize canvas{canvas_w_, canvas_h_};
    const auto objects = rects_from_list(objects_);
    const auto targets = canvas::snap_axis_targets(canvas, objects, ids_from_list(excludeIds));
    const auto handle = canvas::handle_from_string(mode.toStdString());
    const auto snapped =
        canvas::snap_many(rects_from_list(rects), handle, targets, canvas, enabled, limits_of(*this));
    QVariantMap m;
    m.insert(QStringLiteral("rects"), list_from_rects(snapped.rects));
    m.insert(QStringLiteral("vGuide"), snapped.v_guide);
    m.insert(QStringLiteral("hGuide"), snapped.h_guide);
    return m;
}

int CanvasInteract::cursorFor(const QString& handle) const
{
    switch (canvas::handle_from_string(handle.toStdString())) {
    case canvas::Handle::N:
    case canvas::Handle::S:
        return static_cast<int>(Qt::SizeVerCursor);
    case canvas::Handle::E:
    case canvas::Handle::W:
        return static_cast<int>(Qt::SizeHorCursor);
    case canvas::Handle::NE:
    case canvas::Handle::SW:
        return static_cast<int>(Qt::SizeBDiagCursor);
    case canvas::Handle::NW:
    case canvas::Handle::SE:
        return static_cast<int>(Qt::SizeFDiagCursor);
    case canvas::Handle::Move:
    case canvas::Handle::Pending:
        return static_cast<int>(Qt::SizeAllCursor);
    case canvas::Handle::None:
        break;
    }
    return static_cast<int>(Qt::ArrowCursor);
}

} // namespace seventhslide::app
