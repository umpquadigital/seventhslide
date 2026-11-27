#pragma once

// One web page drawn off screen: what WebSources asks of whatever draws it.
//
// The app's renderer is Qt WebEngine's (webengine_renderer.hpp), built only
// where Qt WebEngine is installed; the tests hand in their own. A renderer
// draws at the page's own size and keeps its latest picture for the outputs
// to fit wherever the page shows.

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

class QEvent;

namespace seventhslide::audio {
class AudioSource;
}

namespace seventhslide::app {

class WebRenderer : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    /// Lay the page at ``url`` out at ``size`` pixels and ``zoom``, and draw
    /// it from then on.
    virtual void load(const QUrl& url, QSize size, double zoom) = 0;
    /// Load the page again (the page it is on now, after a link).
    virtual void reload() = 0;
    /// The address it is on now: where it was loaded, or where a link took it.
    [[nodiscard]] virtual QUrl url() const = 0;
    /// It has a page to go back to.
    [[nodiscard]] virtual bool can_go_back() const = 0;
    /// Go back to the page before.
    virtual void go_back() = 0;
    /// Go to ``url``, as a link followed there would: back comes back from it.
    /// False when this renderer draws nothing (``problem()`` says why).
    virtual bool navigate(const QUrl& url) = 0;
    /// The latest picture of the page; null until the first one is drawn.
    [[nodiscard]] virtual QImage frame() const = 0;
    /// Why the page is not showing ("The page could not be loaded: ..."); empty
    /// while it loads or shows.
    [[nodiscard]] virtual QString problem() const = 0;
    /// The page's sound, for its stack's strip in the mixer; null when this
    /// renderer has none. It lives as long as the renderer.
    [[nodiscard]] virtual audio::AudioSource* sound() { return nullptr; }
    /// Deliver a mouse, wheel or key event to the page, in page pixels; a
    /// FocusOut takes the keys away from it until the next. False when this
    /// renderer takes no input.
    virtual bool send_input(QEvent* event)
    {
        (void)event;
        return false;
    }

signals:
    /// ``frame()`` is a new picture.
    void frameChanged();
    /// ``problem()`` changed.
    void problemChanged();
    /// The page finished loading (again, after a reload or a link).
    void loaded();
    /// ``url()`` or ``can_go_back()`` changed: a link was followed, an address
    /// gone to, or back.
    void navigated();
};

/// Makes a renderer for each page that starts showing; null when this build
/// has none.
using WebRendererFactory = std::function<std::unique_ptr<WebRenderer>()>;

} // namespace seventhslide::app
