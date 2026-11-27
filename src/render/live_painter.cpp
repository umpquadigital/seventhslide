#include <seventhslide/render/live_painter.hpp>

#include <seventhslide/render/object_fit.hpp>

#include <QColor>

namespace seventhslide::render {

QRectF live_area_rect(const scene::SceneFrame& frame)
{
    const auto area = (frame.live ? frame.live->area : scene::SceneMediaArea{})
                          .on_canvas(frame.canvas_width, frame.canvas_height);
    return QRectF(area.x, area.y, area.width, area.height);
}

void paint_live_media(QPainter& painter, const scene::SceneFrame& frame,
                      const SourceFrames* frames)
{
    if (frame.mode != scene::SceneMode::Live || frame.is_blank || !frame.live
        || !frame.live->area.enabled) {
        return;
    }
    const QRectF area = live_area_rect(frame);
    if (!area.isValid()) {
        return;
    }
    painter.save();
    painter.setClipRect(area, Qt::IntersectClip);
    painter.fillRect(area, QColor(Qt::black));
    if (const QImage* found = source_image(frames, frame.live->source)) {
        const QImage& picture = *found;
        const ObjectFitPlacement place = object_fit("contain", picture.width(), picture.height(),
                                                    area.x(), area.y(), area.width(),
                                                    area.height());
        if (!place.empty()) {
            painter.drawImage(QRectF(place.dst_x, place.dst_y, place.dst_w, place.dst_h),
                              picture, QRectF(place.src_x, place.src_y, place.src_w, place.src_h));
        }
    }
    painter.restore();
}

} // namespace seventhslide::render
