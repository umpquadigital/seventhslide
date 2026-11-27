#include "freeform_picture_item.hpp"

#include "media_controller.hpp"
#include "preview_resources.hpp"
#include "qvariant_json.hpp"
#include "session_facade.hpp"

#include <seventhslide/canvas/parts.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/effects_painter.hpp>
#include <seventhslide/render/picture_painter.hpp>

#include <QPainter>

#include <algorithm>

namespace seventhslide::app {

FreeformPictureItem::FreeformPictureItem(QQuickItem* parent)
    : SessionBoundPaintedItem(parent)
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::NoButton);
    connect(this, &FreeformPictureItem::pictureChanged, this, [this] { update(); });
    connect(this, &QQuickItem::widthChanged, this, [this] { update(); });
    connect(this, &QQuickItem::heightChanged, this, [this] { update(); });
}

template <typename T>
void FreeformPictureItem::assign(T& slot, const T& value)
{
    if (slot == value) {
        return;
    }
    slot = value;
    emit pictureChanged();
}

void FreeformPictureItem::connect_session(SessionFacade& session)
{
    keep(connect(session.media(), &MediaController::filesArrived, this, [this] { update(); }));
}

void FreeformPictureItem::setFilename(const QString& filename) { assign(filename_, filename); }
void FreeformPictureItem::setFit(const QString& fit) { assign(fit_, fit); }
void FreeformPictureItem::setPicture(const QVariantMap& picture) { assign(picture_, picture); }
void FreeformPictureItem::setEffects(const QVariantMap& effects) { assign(effects_, effects); }
void FreeformPictureItem::setScaleFactor(qreal factor) { assign(scale_factor_, factor); }
void FreeformPictureItem::setPadding(qreal padding) { assign(padding_, padding); }

void FreeformPictureItem::paint(QPainter* painter)
{
    SessionFacade* facade = session();
    const double pad = std::max(0.0, padding_);
    const double box_w = width() - 2 * pad;
    const double box_h = height() - 2 * pad;
    if (painter == nullptr || facade == nullptr || filename_.isEmpty() || box_w <= 0 || box_h <= 0) {
        return;
    }
    auto* cache = facade->previews().image_cache();
    const auto url = model::media_ref_url(model::k_images_subdir, filename_.toStdString());
    if (cache == nullptr || !url) {
        return;
    }
    const QImage full = cache->get(*url);
    if (full.isNull()) {
        return;
    }
    const qreal dpr = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
    const QImage picture = render::shrunk_to_show(full, box_w * dpr, box_h * dpr);
    canvas::Picture adjust;
    if (!picture_.isEmpty()) {
        adjust = to_json(QVariant(picture_)).get<canvas::Picture>();
    }
    canvas::Effects effects;
    if (!effects_.isEmpty()) {
        effects = to_json(QVariant(effects_)).get<canvas::Effects>();
    }
    const double scale = scale_factor_ > 0 ? scale_factor_ : 1.0;
    const std::string fit = fit_.toStdString();
    // The effects' sizes are design px: the painter is scaled so they come
    // out as the stage shows the box.
    painter->save();
    painter->translate(pad, pad);
    painter->scale(scale, scale);
    const QRectF box(0, 0, box_w / scale, box_h / scale);
    render::paint_with_effects(*painter, effects, box, [&](QPainter& p) {
        render::paint_picture(p, picture, box, fit, adjust.crop, adjust);
    });
    painter->restore();
}

}  // namespace seventhslide::app
