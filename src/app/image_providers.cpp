#include "image_providers.hpp"

#include "freeform_library_controller.hpp"
#include "freeform_thumb_cache.hpp"
#include "freeform_thumb_provider.hpp"
#include "gui_thread.hpp"
#include "icon_image_provider.hpp"
#include "media_controller.hpp"
#include "media_thumb_provider.hpp"
#include "output_preview_provider.hpp"
#include "service_thumb_provider.hpp"
#include "service_thumbs.hpp"
#include "session_facade.hpp"
#include "slide_edit_session.hpp"
#include "theme_controller.hpp"
#include "theme_preview_provider.hpp"

#include <QQmlEngine>
#include <QUrl>
#include <QUrlQuery>

namespace seventhslide::app {

void add_image_providers(QQmlEngine& engine, SessionFacade& session)
{
    // Qt may call these on its pixmap-reader thread. The providers that read
    // live session state prepare on the GUI thread via call_on_gui_thread and
    // paint here, on the worker (see preview_job.hpp); file decoding stays on
    // the worker too. mediathumb needs only the media folder, which
    // OpenLibrary keeps readable from any thread.
    engine.addImageProvider(
        QStringLiteral("outputpreview"),
        new OutputPreviewProvider(
            [&session](const QString& output_id, int slide_index, const QSize& size,
                       const QUrlQuery& query) {
                // A slide of a stack's Preview, or of what is live.
                const auto side = query.queryItemValue(QStringLiteral("side"))
                                          == QLatin1String("preview")
                                      ? session::StackSide::Preview
                                      : session::StackSide::Program;
                return finish_preview(call_on_gui_thread(&session, [=, &session] {
                    return session.outputPreviews()->prepare_output_preview(output_id, size,
                                                                            slide_index, side);
                }));
            }));
    engine.addImageProvider(
        QStringLiteral("themepreview"),
        new ThemePreviewProvider([&session](const QString& id, const QSize& size) {
            return finish_preview(call_on_gui_thread(
                &session, [=, &session] { return session.themes()->prepare_preview(id, size); }));
        }));
    engine.addImageProvider(
        QStringLiteral("freeformpreview"),
        new OutputPreviewProvider(
            [&session](const QString& output_id, int slide_index, const QSize& size,
                       const QUrlQuery& query) {
                const QString slide_id =
                    QUrl::fromPercentEncoding(query.queryItemValue(QStringLiteral("id")).toUtf8());
                return finish_preview(call_on_gui_thread(&session, [=, &session] {
                    return session.slideEdit()->slides()->prepare_preview(output_id, slide_index,
                                                                          size, slide_id);
                }));
            }));
    engine.addImageProvider(
        QStringLiteral("mediathumb"),
        new MediaThumbProvider(
            [&session](const QString& subdir, const QString& filename) {
                return session.media()->resolve_media_path(subdir, filename);
            },
            [&session]() { return session.open_library().media_root_any_thread(); }));
    // UI glyph set. Provider-backed (not data: URLs) so recoloured icons load
    // synchronously and never blank for a frame - see IconImageProvider.
    engine.addImageProvider(
        QStringLiteral("ssicon"),
        new IconImageProvider());
    engine.addImageProvider(
        QStringLiteral("freeformthumb"),
        new FreeformThumbProvider([&session](qint64 freeform_id, const QSize& size,
                                             const QString& key, const QString& themes) {
            // A disk thumb whose stamp matches the URL is current; anything
            // else is prepared on the GUI thread, then painted and stored here.
            const auto root = session.open_library().media_root_any_thread();
            if (freeform_thumb_fresh(root, freeform_id, key.toStdString(), themes.toStdString())) {
                if (QImage disk = load_freeform_thumb_fitted(root, freeform_id, size);
                    !disk.isNull()) {
                    return disk;
                }
            }
            return finish_preview(call_on_gui_thread(&session, [=, &session] {
                return session.freeformLibrary()->prepare_library_thumb(freeform_id, size);
            }));
        }));
    engine.addImageProvider(
        QStringLiteral("servicethumb"),
        new ServiceThumbProvider([&session](qint64 service_id, const QString& key, const QSize& size) {
            // One drawn under the URL's key is current; anything else is
            // prepared on the GUI thread, then laid out and painted here.
            auto* thumbs = session.serviceThumbs();
            if (QImage hit = thumbs->cached(key, size); !hit.isNull()) {
                return hit;
            }
            return finish_preview(call_on_gui_thread(
                &session, [=] { return thumbs->prepare(service_id, size); }));
        }));
}

}  // namespace seventhslide::app
