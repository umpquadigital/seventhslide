#pragma once

// The Services window's thumbnails: a service's first slide on the first
// output that is on, under the service's own themes, as it would show live.
// QML: ``session.serviceThumbs.thumbUrl(serviceId, showing)``.
//
// The slide is laid out from copies, as an export lays one out
// (still_frames.hpp), away from the GUI thread. The rows ahead of it that show
// no still picture of their own - a divider, a video, a sound, a web page, a
// live source - are passed over. A thumbnail's URL carries a key over all it
// is made from, so one already drawn is served again until the service, its
// themes or the output change.

#include "image_lru_cache.hpp"
#include "preview_job.hpp"

#include <seventhslide/model/content.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/theme/theme_picks.hpp>

#include <QtQml/qqmlregistration.h>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace seventhslide::session {
class Session;
}

namespace seventhslide::app {

class LibraryController;
class PreviewResources;

class ServiceThumbs : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

public:
    /// At most this many rows are tried for a slide: one with none (a song
    /// with no lyrics) gives way to the next.
    static constexpr int k_rows_tried = 5;

    ServiceThumbs(const session::Session& session,
                  LibraryController& library,
                  PreviewResources& previews,
                  QObject* parent = nullptr);

    /// ``image://servicethumb/<id>?k=<key>&p=<pictures>``, or "" when the
    /// service has no row that shows a still picture (the row shows a
    /// placeholder). ``showing`` is only there for a binding to follow: the
    /// window bumps it each time it opens, reading every service again.
    Q_INVOKABLE QString thumbUrl(qint64 serviceId, int showing = 0) const;

    /// What was drawn for URL key ``key`` at ``size``; null when nothing
    /// was. Any thread.
    [[nodiscard]] QImage cached(const QString& key, const QSize& size);
    /// Service ``id``'s thumbnail, at most ``size`` in the output's shape:
    /// prepared here, laid out and painted by whoever finishes it, and kept
    /// for ``cached``. GUI thread.
    [[nodiscard]] PreviewJob prepare(qint64 id, const QSize& size);

    /// What a thumbnail is drawn at when its Image asks for no size.
    [[nodiscard]] static QSize requested_or_default(const QSize& requested);

private:
    /// Everything a service's thumbnail is made from, copied.
    struct Source {
        model::OutputConfig output;
        std::vector<std::string> theme_priority;
        std::string ccli_licence_number;
        theme::OutputThemeMap service_themes;
        /// The rows that may show a still picture, in order.
        std::vector<model::ServiceItem> rows;
        /// Over all of the above and the themes' revision.
        QString key;
    };
    [[nodiscard]] std::optional<Source> source_of(qint64 id) const;

    const session::Session& session_;
    LibraryController& library_;
    PreviewResources& previews_;
    ImageLruCache cache_{128};
};

}  // namespace seventhslide::app
