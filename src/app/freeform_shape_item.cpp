#include "freeform_shape_item.hpp"

#include "media_controller.hpp"
#include "preview_resources.hpp"
#include "qvariant_json.hpp"
#include "session_facade.hpp"

#include <seventhslide/canvas/object.hpp>
#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/geom/shape_path.hpp>
#include <seventhslide/render/effects_painter.hpp>
#include <seventhslide/render/picture_painter.hpp>
#include <seventhslide/render/shape_painter.hpp>

#include <QImage>
#include <QPainter>

namespace seventhslide::app {

FreeformShapeItem::FreeformShapeItem(QQuickItem* parent)
    : SessionBoundPaintedItem(parent)
{
    setAntialiasing(true);
    // Shapes are decoration; clicks belong to the canvas' hit-testing layer.
    setAcceptedMouseButtons(Qt::NoButton);
}

template <typename T>
void FreeformShapeItem::assign(T& slot, const T& value)
{
    if (slot == value) {
        return;
    }
    slot = value;
    emit shapeChanged();
    update();
}

void FreeformShapeItem::connect_session(SessionFacade& session)
{
    keep(connect(session.media(), &MediaController::filesArrived, this, [this] { update(); }));
}

void FreeformShapeItem::setKind(const QString& kind) { assign(kind_, kind); }
void FreeformShapeItem::setFillPictureFile(const QString& file) { assign(fill_picture_file_, file); }
void FreeformShapeItem::setFillPictureFit(const QString& fit) { assign(fill_picture_fit_, fit); }
void FreeformShapeItem::setFill(const QString& fill) { assign(fill_, fill); }
void FreeformShapeItem::setFillKind(const QString& kind) { assign(fill_kind_, kind); }
void FreeformShapeItem::setStroke(const QString& stroke) { assign(stroke_, stroke); }
void FreeformShapeItem::setDash(const QString& dash) { assign(dash_, dash); }
void FreeformShapeItem::setEffects(const QVariantMap& effects) { assign(effects_, effects); }
void FreeformShapeItem::setStrokeWidth(qreal width) { assign(stroke_width_, width); }
void FreeformShapeItem::setScaleFactor(qreal factor) { assign(scale_factor_, factor); }
void FreeformShapeItem::setCorner(qreal value) { assign(corner_, value); }
void FreeformShapeItem::setPoints(int value) { assign(points_, value); }
void FreeformShapeItem::setInner(qreal value) { assign(inner_, value); }
void FreeformShapeItem::setHead(qreal value) { assign(head_, value); }
void FreeformShapeItem::setThickness(qreal value) { assign(thickness_, value); }
void FreeformShapeItem::setPadding(qreal value) { assign(padding_, value); }
void FreeformShapeItem::setGradientKind(const QString& value) { assign(gradient_kind_, value); }
void FreeformShapeItem::setGradientAngle(qreal value) { assign(gradient_angle_, value); }
void FreeformShapeItem::setGradientCenterX(qreal value) { assign(gradient_center_x_, value); }
void FreeformShapeItem::setGradientCenterY(qreal value) { assign(gradient_center_y_, value); }
void FreeformShapeItem::setGradientFrom(const QString& value) { assign(gradient_from_, value); }
void FreeformShapeItem::setGradientTo(const QString& value) { assign(gradient_to_, value); }
void FreeformShapeItem::setPath(const QVariantList& value) { assign(path_, value); }
void FreeformShapeItem::setClosed(bool value) { assign(closed_, value); }
void FreeformShapeItem::setLineStart(const QString& value) { assign(line_start_, value); }
void FreeformShapeItem::setLineEnd(const QString& value) { assign(line_end_, value); }

void FreeformShapeItem::paint(QPainter* painter)
{
    const qreal pad = padding_ > 0 ? padding_ : 0.0;
    const qreal box_w = width() - pad * 2.0;
    const qreal box_h = height() - pad * 2.0;
    if (!painter || box_w <= 0 || box_h <= 0) {
        return;
    }
    geom::ShapeAdjust adjust;
    adjust.corner = corner_;
    adjust.points = points_;
    adjust.inner = inner_;
    adjust.head = head_;
    adjust.thickness = thickness_;
    for (const auto& entry : path_) {
        if (const auto point = canvas::path_point_from_json(to_json(entry))) {
            adjust.path.push_back(*point);
        }
    }
    adjust.closed = closed_;

    canvas::Fill fill;
    fill.kind = canvas::fill_kind_from_string(fill_kind_.toStdString());
    fill.color = fill_.toStdString();
    fill.picture.file = fill_picture_file_.toStdString();
    fill.picture.fit = fill_picture_fit_.toStdString();
    fill.gradient.kind = canvas::gradient_kind_from_string(gradient_kind_.toStdString());
    fill.gradient.angle = gradient_angle_;
    fill.gradient.center_x = gradient_center_x_;
    fill.gradient.center_y = gradient_center_y_;
    if (!gradient_from_.isEmpty() && !gradient_to_.isEmpty()) {
        fill.gradient.stops = {{0.0, gradient_from_.toStdString()}, {1.0, gradient_to_.toStdString()}};
    }
    // A picture fill's file, through the session's cache; null paints nothing.
    QImage picture;
    if (SessionFacade* facade = session(); facade != nullptr && fill.kind == canvas::FillKind::Picture) {
        if (const auto url = canvas::fill_picture_url(fill); url) {
            if (auto* cache = facade->previews().image_cache()) {
                const qreal dpr = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
                picture = render::shrunk_to_show(cache->get(*url), box_w * dpr, box_h * dpr);
            }
        }
    }
    canvas::Line line;
    line.color = stroke_.toStdString();
    line.width = stroke_width_;
    line.dash = canvas::line_dash_from_string(dash_.toStdString());
    line.start = canvas::line_end_from_string(line_start_.toStdString());
    line.end = canvas::line_end_from_string(line_end_.toStdString());

    const auto kind = geom::shape_kind_from_string(kind_.toStdString());
    const qreal scale = scale_factor_ > 0 ? scale_factor_ : 1.0;
    // The effects' sizes are design px: the painter is scaled so they come
    // out as the stage shows the box, and the outline is built at stage size
    // underneath that scale so its geometry is unchanged.
    canvas::Effects effects;
    if (!effects_.isEmpty()) {
        effects = to_json(QVariant(effects_)).get<canvas::Effects>();
    }
    painter->save();
    painter->translate(pad, pad);
    painter->scale(scale, scale);
    // A mitred stroke reaches twice its width past the outline, as on the output.
    const double reach = render::line_reach(line);
    const QRectF body(-reach, -reach, box_w / scale + 2 * reach, box_h / scale + 2 * reach);
    render::paint_with_effects(*painter, effects, body, [&](QPainter& p) {
        p.save();
        p.scale(1.0 / scale, 1.0 / scale);
        render::paint_shape_outline(p, kind, box_w, box_h, adjust, fill, line, scale, &picture);
        p.restore();
    });
    painter->restore();
}

} // namespace seventhslide::app
