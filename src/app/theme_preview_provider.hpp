#pragma once

// QQuickImageProvider for theme designer draft + gallery thumbs.

#include <QQuickImageProvider>

#include <functional>

namespace seventhslide::app {

/**
 * Serves theme preview images:
 * - ``image://themepreview/draft?r=<rev>`` - live designer draft (surface only)
 * - ``image://themepreview/grid?cell=<px>`` - display-sized transparency grid
 * - ``image://themepreview/theme/<output>/<kind>/<id>?r=<rev>[&c=<ctx>]`` - gallery
 *   thumb, in context ``ctx`` when it is one of the kind's, else the kind's first
 *
 * ``request`` receives the image id (path + optional query) and requested size.
 */
class ThemePreviewProvider final : public QQuickImageProvider {
public:
    using RequestFn = std::function<QImage(const QString& id, const QSize& requested)>;

    explicit ThemePreviewProvider(RequestFn request);

    [[nodiscard]] QImage requestImage(const QString& id,
                                      QSize* size,
                                      const QSize& requestedSize) override;

private:
    RequestFn request_;
};

} // namespace seventhslide::app
