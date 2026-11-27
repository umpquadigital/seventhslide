// The pointer held by the X server: four XFixes pointer barriers round the
// area, which the pointer cannot cross however fast it moves (XFixes 5).

#include "pointer_confinement.hpp"
#include "pointer_confinement_native.hpp"

#include <QGuiApplication>
#include <QPointer>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <algorithm>
#include <array>

namespace seventhslide::app {
namespace {

using X11Application = QNativeInterface::QX11Application;

class X11PointerHold final : public PointerHold {
public:
    X11PointerHold(Display* display, QWindow& window, const QRectF& area)
        : display_(display)
        , window_(&window)
        , area_(area)
    {
        // The window moved: so does its area on the screen.
        connect(&window, &QWindow::xChanged, this, &X11PointerHold::place);
        connect(&window, &QWindow::yChanged, this, &X11PointerHold::place);
        place();
    }

    ~X11PointerHold() override { let_go(); }

    void move(const QRectF& area) override
    {
        area_ = area;
        place();
    }

protected:
    void let_go() override
    {
        window_.clear();
        take_down();
    }

private:
    /// Barriers round the area where it is on the screen now.
    void place()
    {
        if (!window_) {
            return;
        }
        const QRect area = native_screen_area(*window_, area_);
        if (area == placed_) {
            return;
        }
        take_down();
        if (area.isEmpty()) {
            return;
        }
        const ::Window root = DefaultRootWindow(display_);
        bring_inside(root, area);
        // A barrier at x stops the pointer at x moving left, and at x - 1
        // moving right: the right and bottom ones go one past the area.
        const int left = area.left();
        const int top = area.top();
        const int right = area.left() + area.width();
        const int bottom = area.top() + area.height();
        // No direction is let through (0).
        barriers_ = {
            XFixesCreatePointerBarrier(display_, root, left, top, right, top, 0, 0, nullptr),
            XFixesCreatePointerBarrier(display_, root, left, bottom, right, bottom, 0, 0, nullptr),
            XFixesCreatePointerBarrier(display_, root, left, top, left, bottom, 0, 0, nullptr),
            XFixesCreatePointerBarrier(display_, root, right, top, right, bottom, 0, 0, nullptr),
        };
        placed_ = area;
        XFlush(display_);
    }

    void take_down()
    {
        for (PointerBarrier& barrier : barriers_) {
            if (barrier != 0) {
                XFixesDestroyPointerBarrier(display_, barrier);
                barrier = 0;
            }
        }
        if (!placed_.isEmpty()) {
            placed_ = {};
            XFlush(display_);
        }
    }

    /// Barriers shut the pointer out as firmly as in: one outside the area
    /// (on its rounded-off edge, or left out by a layout that shrank it) is
    /// moved to the nearest place in it first. A move by the app crosses
    /// barriers.
    void bring_inside(::Window root, const QRect& area)
    {
        ::Window root_back = 0;
        ::Window child = 0;
        int x = 0;
        int y = 0;
        int window_x = 0;
        int window_y = 0;
        unsigned int mask = 0;
        if (!XQueryPointer(display_, root, &root_back, &child, &x, &y, &window_x, &window_y,
                           &mask)) {
            return;
        }
        const int kept_x = std::clamp(x, area.left(), area.left() + area.width() - 1);
        const int kept_y = std::clamp(y, area.top(), area.top() + area.height() - 1);
        if (kept_x != x || kept_y != y) {
            XWarpPointer(display_, 0, root, 0, 0, 0, 0, kept_x, kept_y);
        }
    }

    Display* display_;
    QPointer<QWindow> window_;
    /// In the window's logical pixels.
    QRectF area_;
    /// Where the barriers are, on the screen; empty when there are none.
    QRect placed_;
    std::array<PointerBarrier, 4> barriers_{};
};

class X11PointerConfinement final : public PointerConfinement {
public:
    explicit X11PointerConfinement(Display* display)
        : display_(display)
    {
    }

protected:
    std::unique_ptr<PointerHold> make_hold(QWindow& window, const QRectF& area) override
    {
        return std::make_unique<X11PointerHold>(display_, window, area);
    }

private:
    Display* display_;
};

} // namespace

std::unique_ptr<PointerConfinement> make_x11_pointer_confinement()
{
    auto* app = qGuiApp->nativeInterface<X11Application>();
    // Null where Qt's xcb runs without Xlib.
    Display* display = app != nullptr ? app->display() : nullptr;
    if (display == nullptr) {
        return nullptr;
    }
    int event_base = 0;
    int error_base = 0;
    int major = 5;
    int minor = 0;
    // Asking for the version is what lets the client library use 5's
    // requests.
    if (!XFixesQueryExtension(display, &event_base, &error_base)
        || XFixesQueryVersion(display, &major, &minor) == 0 || major < 5) {
        qInfo("SeventhSlide: the X server has no pointer barriers (XFixes 5); a web page worked "
              "from the Controller cannot keep the pointer");
        return nullptr;
    }
    return std::make_unique<X11PointerConfinement>(display);
}

} // namespace seventhslide::app
