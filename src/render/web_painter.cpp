#include <seventhslide/render/web_painter.hpp>

#include <seventhslide/render/object_fit.hpp>

#include <QColor>


namespace seventhslide::render {

QRectF web_area_rect(const scene::SceneFrame& frame)
{
    const auto area = (frame.web ? frame.web->area : scene::SceneMediaArea{})
                          .on_canvas(frame.canvas_width, frame.canvas_height);
    return QRectF(area.x, area.y, area.width, area.height);
}

void paint_web_media(QPainter& painter, const scene::SceneFrame& frame,
                     const SourceFrames* frames)
{
    if (frame.mode != scene::SceneMode::Web || frame.is_blank || !frame.web
        || !frame.web->area.enabled) {
        return;
    }
    const QRectF area = web_area_rect(frame);
    if (!area.isValid()) {
        return;
    }
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    painter.fillRect(area, QColor(Qt::black));
    const QImage* picture = source_image(frames, frame.web->source);
    if (picture != nullptr && !picture->isNull()) {
        const ObjectFitPlacement place = object_fit(frame.web->fit, picture->width(),
                                                    picture->height(), area.x(), area.y(),
                                                    area.width(), area.height());
        if (!place.empty()) {
            painter.drawImage(QRectF(place.dst_x, place.dst_y, place.dst_w, place.dst_h), *picture,
                              QRectF(place.src_x, place.src_y, place.src_w, place.src_h));
        }
    }
    painter.restore();
}

} // namespace seventhslide::render
