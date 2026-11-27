#include <seventhslide/render/video_painter.hpp>

#include <seventhslide/render/css_color.hpp>
#include <seventhslide/render/object_fit.hpp>

#include <QFont>
#include <QFontMetricsF>
#include <QString>

namespace seventhslide::render {

QRectF video_area_rect(const scene::SceneFrame& frame)
{
    const auto area = (frame.video ? frame.video->area : scene::SceneMediaArea{})
                          .on_canvas(frame.canvas_width, frame.canvas_height);
    return QRectF(area.x, area.y, area.width, area.height);
}

void paint_video_media(QPainter& painter,
                       const scene::SceneFrame& frame,
                       const SourceFrames* frames)
{
    if (frame.mode != scene::SceneMode::Video || frame.is_blank) {
        return;
    }
    if (!frame.video || frame.video->url.empty()) {
        return;
    }

    // ``video_enabled`` says the theme has a Video area element at all. The
    // countdown is a clock object of its own and paints with the object stack,
    // so it still shows over a theme that places no video area.
    const bool area_enabled = frame.video->area.enabled;
    const QRectF area = video_area_rect(frame);
    if (area_enabled && !area.isValid()) {
        return;
    }

    if (area_enabled) {
        painter.save();
        painter.setClipRect(area, Qt::IntersectClip);
        painter.fillRect(area, QColor(Qt::black));

        if (const QImage* frame_image = source_image(frames, frame.video->source)) {
            const ObjectFitPlacement place =
                object_fit("contain", frame_image->width(), frame_image->height(), area.x(),
                           area.y(), area.width(), area.height());
            if (!place.empty()) {
                painter.drawImage(QRectF(place.dst_x, place.dst_y, place.dst_w, place.dst_h),
                                  *frame_image,
                                  QRectF(place.src_x, place.src_y, place.src_w, place.src_h));
            }
        }
        painter.restore();
    }
}

} // namespace seventhslide::render
