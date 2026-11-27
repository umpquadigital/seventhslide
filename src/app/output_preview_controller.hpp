#pragma once

// The operator's previews of the outputs: the live preview in each output
// card, the controller gallery's slide thumbnails, and the live video picture.
// QML: ``session.outputPreviews``.
//
// An output card's preview (OutputPreviewItem) draws from the presenter the
// output's destinations share, held from the output host this hands out; it
// composes nothing of its own.

#include "preview_job.hpp"
#include "preview_resources.hpp"

#include <seventhslide/scene/scene_frame.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

namespace seventhslide::render {
class OutputWindowHost;
}

namespace seventhslide::app {

class OpenLibrary;
class PlaybackController;
class SideView;
class LiveSources;
class WebSources;

class OutputPreviewController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    /// The slide thumbnails' width over height: the primary output's shape
    /// (16:9 with no output). The gallery's cells take it.
    Q_PROPERTY(qreal slideAspect READ slideAspect NOTIFY slideAspectChanged)

public:
    OutputPreviewController(const session::Session& session,
                            const PlaybackController& playback,
                            const OpenLibrary& library,
                            PreviewResources& previews,
                            QObject* parent = nullptr);

    /// ``image://outputpreview/<primary output id>?slide=N&side=S&r=build&p=pictures``:
    /// slide ``slide_index`` of the side ``view`` shows, for a well's gallery.
    /// ``revision`` (``MediaController::filesRevision``) is only there for a
    /// binding to follow.
    Q_INVOKABLE QString slideThumbUrl(seventhslide::app::SideView* view,
                                      int slide_index,
                                      int revision = 0) const;

    [[nodiscard]] qreal slideAspect() const noexcept { return slide_aspect_; }
    /// Read the primary output's shape again, after the outputs changed.
    void refresh_slide_aspect();
    /// A preview of output ``output_id`` ``width`` wide, in its shape.
    [[nodiscard]] QSize preview_size(const QString& output_id, int width) const;

    /// Thumbnail for ``image://outputpreview/...``: what the output shows now
    /// (``slide_index`` < 0), or slide ``slide_index`` of the focused stack's
    /// ``side``.
    [[nodiscard]] QImage render_output_preview(
        const QString& output_id,
        const QSize& size,
        int slide_index = -1,
        session::StackSide side = session::StackSide::Program) const;
    /// ``render_output_preview``, prepared here and painted by whoever
    /// finishes it (see preview_job.hpp). GUI thread.
    [[nodiscard]] PreviewJob prepare_output_preview(
        const QString& output_id,
        const QSize& size,
        int slide_index = -1,
        session::StackSide side = session::StackSide::Program) const;
    /// The current picture of the video ``view``'s side shows, for
    /// LiveVideoItem (no themed scene plate); null while it shows none.
    [[nodiscard]] QImage video_frame(const SideView* view) const;
    /// The latest picture of the web page ``source``, for LiveWebItem; null
    /// while it has none.
    [[nodiscard]] QImage live_web_frame(const QString& source) const;
    /// Where web pages' pictures come from; set once by the facade.
    void set_web_sources(const WebSources* sources) noexcept { web_sources_ = sources; }
    /// The latest picture of the live source ``source``, for LiveSourceItem;
    /// null while it has none.
    [[nodiscard]] QImage live_source_frame(const QString& source) const;
    /// Where live sources' pictures come from; set once by the facade.
    void set_live_sources(const LiveSources* sources) noexcept { live_sources_ = sources; }

    /// The host whose presenters the output cards' previews draw; null where
    /// there is none (a test without one), and they show nothing.
    [[nodiscard]] render::OutputWindowHost* output_host() const noexcept { return host_; }
    void set_output_host(render::OutputWindowHost* host);

signals:
    /// The previews draw from another host's presenters now.
    void outputHostChanged();
    void slideAspectChanged();

private:
    const session::Session& session_;
    const PlaybackController& playback_;
    const OpenLibrary& library_;
    PreviewResources& previews_;
    render::OutputWindowHost* host_ = nullptr;
    const WebSources* web_sources_ = nullptr;
    const LiveSources* live_sources_ = nullptr;
    qreal slide_aspect_ = 16.0 / 9.0;
};

}  // namespace seventhslide::app
