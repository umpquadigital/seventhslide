#pragma once

// A preview made ready on the GUI thread and finished on any thread.
//
// Image providers run on Qt's pixmap-reader thread and the web remote encodes
// on its own pool, but what a preview shows - the session, the outputs, the
// themes, the open Freeform document - may be read only on the GUI thread
// (see gui_thread.hpp). So a preview is prepared there: either the finished
// picture already (a cache hit), a StillPaint holding everything the paint
// reads, or copies of everything the layout reads, to lay it out from.
// Laying out and painting, the expensive parts, then run on the asking thread.

#include <seventhslide/render/scene_compositor.hpp>

#include <QImage>

#include <functional>
#include <optional>

namespace seventhslide::app {

struct PreviewJob {
    /// The picture, when nothing is left to paint (empty: none).
    QImage image;
    /// Stands in front of the paint, on the finishing thread: it answers with
    /// a picture kept from before, or calls ``paint`` and keeps what it gets.
    std::function<QImage(const std::function<QImage()>& paint)> store;
    /// What to paint instead of ``image``.
    std::optional<render::StillPaint> still;
    /// What to paint, laid out on the finishing thread, when ``still`` is
    /// empty. It holds copies of what it reads (and the preview resources,
    /// which outlive every worker and lock themselves); nothing when there is
    /// nothing to paint.
    std::function<std::optional<render::StillPaint>()> lay_out;
    /// Run on the finishing thread with the picture (``image``, or ``still``
    /// painted): to cache it or store it on disk. Its result is the preview.
    std::function<QImage(QImage)> then;
};

/// The finished preview. Any thread.
[[nodiscard]] inline QImage finish_preview(const PreviewJob& job)
{
    const auto paint = [&job] {
        std::optional<render::StillPaint> laid_out;
        if (!job.still && job.lay_out) {
            laid_out = job.lay_out();
        }
        const auto& still = job.still ? job.still : laid_out;
        return still ? render::paint_still(*still) : job.image;
    };
    QImage img = job.store ? job.store(paint) : paint();
    return job.then ? job.then(std::move(img)) : img;
}

}  // namespace seventhslide::app
