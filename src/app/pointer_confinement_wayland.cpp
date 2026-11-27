// The pointer held by the Wayland compositor: a confinement of the seat's
// pointer to a region of the window's surface (pointer-constraints-unstable-v1).
// Qt's own connection to the compositor is used; this binds the one global
// Qt does not.

#include "pointer_confinement.hpp"
#include "pointer_confinement_native.hpp"

#include "pointer-constraints-unstable-v1-client-protocol.h"

#include <QGuiApplication>
#include <QPointer>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qplatformwindow_p.h>

#include <wayland-client.h>

#include <cstring>

namespace seventhslide::app {
namespace {

using WaylandApplication = QNativeInterface::QWaylandApplication;
using WaylandWindow = QNativeInterface::Private::QWaylandWindow;

/// The pointer held in a region of one surface. One-shot: once the
/// compositor lets it go, it is gone, and not taken again unasked.
class WaylandPointerHold final : public PointerHold {
public:
    /// Null when the window has no surface, or nothing of ``area`` is left.
    static std::unique_ptr<WaylandPointerHold> make(wl_display* display, wl_compositor* compositor,
                                                    zwp_pointer_constraints_v1* constraints,
                                                    wl_pointer* pointer, QWindow& window,
                                                    const QRectF& area)
    {
        auto* native = window.nativeInterface<WaylandWindow>();
        wl_surface* surface = native != nullptr ? native->surface() : nullptr;
        if (surface == nullptr) {
            return nullptr;
        }
        const QRect region = surface_area(window, area);
        if (region.isEmpty()) {
            return nullptr;
        }
        std::unique_ptr<WaylandPointerHold> hold(
            new WaylandPointerHold(display, compositor, window));
        wl_region* within = hold->make_region(region);
        hold->confined_ = zwp_pointer_constraints_v1_confine_pointer(
            constraints, surface, pointer, within, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        wl_region_destroy(within);
        zwp_confined_pointer_v1_add_listener(hold->confined_, &k_listener, hold.get());
        hold->region_ = region;
        // The confinement goes with the surface (the window hidden).
        QObject::connect(native, &WaylandWindow::surfaceDestroyed, hold.get(),
                         [held = hold.get()] { held->end(); });
        wl_display_flush(display);
        return hold;
    }

    ~WaylandPointerHold() override { let_go(); }

    void move(const QRectF& area) override
    {
        if (confined_ == nullptr || !window_) {
            return;
        }
        const QRect region = surface_area(*window_, area);
        if (region.isEmpty() || region == region_) {
            return;
        }
        // Taken up with the surface's next commit, which the window's next
        // frame makes: the item moved, so it draws.
        wl_region* within = make_region(region);
        zwp_confined_pointer_v1_set_region(confined_, within);
        wl_region_destroy(within);
        region_ = region;
        wl_display_flush(display_);
    }

protected:
    void let_go() override
    {
        if (confined_ == nullptr) {
            return;
        }
        zwp_confined_pointer_v1_destroy(confined_);
        confined_ = nullptr;
        wl_display_flush(display_);
    }

private:
    WaylandPointerHold(wl_display* display, wl_compositor* compositor, QWindow& window)
        : display_(display)
        , compositor_(compositor)
        , window_(&window)
    {
    }

    /// ``area`` on the window's surface, which takes in the frame Qt draws
    /// round the window itself where the compositor draws none.
    static QRect surface_area(QWindow& window, const QRectF& area)
    {
        const QRect local = native_window_area(window, area);
        if (local.isEmpty()) {
            return {};
        }
        const QMargins frame = window.handle()->frameMargins();
        return local.translated(frame.left(), frame.top());
    }

    wl_region* make_region(const QRect& area) const
    {
        wl_region* region = wl_compositor_create_region(compositor_);
        wl_region_add(region, area.x(), area.y(), area.width(), area.height());
        return region;
    }

    static void on_confined(void* /*data*/, zwp_confined_pointer_v1* /*confined*/) {}

    static void on_unconfined(void* data, zwp_confined_pointer_v1* /*confined*/)
    {
        // A one-shot confinement is spent once let go.
        static_cast<WaylandPointerHold*>(data)->end();
    }

    static constexpr zwp_confined_pointer_v1_listener k_listener{
        .confined = &WaylandPointerHold::on_confined,
        .unconfined = &WaylandPointerHold::on_unconfined,
    };

    wl_display* display_;
    wl_compositor* compositor_;
    QPointer<QWindow> window_;
    zwp_confined_pointer_v1* confined_ = nullptr;
    /// Where it is held, on the surface.
    QRect region_;
};

class WaylandPointerConfinement final : public PointerConfinement {
public:
    WaylandPointerConfinement(wl_display* display, wl_compositor* compositor,
                              zwp_pointer_constraints_v1* constraints)
        : display_(display)
        , compositor_(compositor)
        , constraints_(constraints)
    {
    }

    // The holds made outlive it safely: a confinement does not need the
    // global that made it.
    ~WaylandPointerConfinement() override { zwp_pointer_constraints_v1_destroy(constraints_); }

    WaylandPointerConfinement(const WaylandPointerConfinement&) = delete;
    WaylandPointerConfinement& operator=(const WaylandPointerConfinement&) = delete;

protected:
    std::unique_ptr<PointerHold> make_hold(QWindow& window, const QRectF& area) override
    {
        // The seat's pointer now: one may come and go with a mouse.
        auto* app = qGuiApp->nativeInterface<WaylandApplication>();
        wl_pointer* pointer = app != nullptr ? app->pointer() : nullptr;
        if (pointer == nullptr) {
            return nullptr;
        }
        return WaylandPointerHold::make(display_, compositor_, constraints_, pointer, window, area);
    }

private:
    wl_display* display_;
    wl_compositor* compositor_;
    zwp_pointer_constraints_v1* constraints_;
};

/// The compositor's pointer-constraints global, as the registry lists it.
struct Advertised {
    uint32_t name = 0;
};

void on_global(void* data, wl_registry* /*registry*/, uint32_t name, const char* interface,
               uint32_t /*version*/)
{
    if (std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        static_cast<Advertised*>(data)->name = name;
    }
}

void on_global_remove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {}

constexpr wl_registry_listener k_registry_listener{
    .global = &on_global,
    .global_remove = &on_global_remove,
};

/// The compositor's pointer constraints, on Qt's queue; null without.
zwp_pointer_constraints_v1* bind_constraints(wl_display* display)
{
    // A queue of our own for the registry's round trip, so none of Qt's
    // events are dispatched from here.
    wl_event_queue* queue = wl_display_create_queue(display);
    auto* wrapped = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapped), queue);
    wl_registry* registry = wl_display_get_registry(wrapped);
    wl_proxy_wrapper_destroy(wrapped);

    Advertised advertised;
    wl_registry_add_listener(registry, &k_registry_listener, &advertised);
    zwp_pointer_constraints_v1* constraints = nullptr;
    if (wl_display_roundtrip_queue(display, queue) >= 0 && advertised.name != 0) {
        constraints = static_cast<zwp_pointer_constraints_v1*>(
            wl_registry_bind(registry, advertised.name, &zwp_pointer_constraints_v1_interface, 1));
        // The confinements it makes are Qt's queue's, which the GUI thread
        // dispatches, so their events come on that thread.
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(constraints), nullptr);
    }
    wl_registry_destroy(registry);
    wl_event_queue_destroy(queue);
    return constraints;
}

} // namespace

std::unique_ptr<PointerConfinement> make_wayland_pointer_confinement()
{
    auto* app = qGuiApp->nativeInterface<WaylandApplication>();
    if (app == nullptr || app->display() == nullptr || app->compositor() == nullptr) {
        return nullptr;
    }
    zwp_pointer_constraints_v1* constraints = bind_constraints(app->display());
    if (constraints == nullptr) {
        qInfo("SeventhSlide: the compositor has no pointer constraints; a web page worked from "
              "the Controller cannot keep the pointer");
        return nullptr;
    }
    return std::make_unique<WaylandPointerConfinement>(app->display(), app->compositor(),
                                                       constraints);
}

} // namespace seventhslide::app
