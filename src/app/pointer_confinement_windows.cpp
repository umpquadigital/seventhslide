// The pointer held by Windows: the cursor clipped to the area (ClipCursor).

#include "pointer_confinement.hpp"
#include "pointer_confinement_native.hpp"

#include <QPointer>
#include <QWindow>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace seventhslide::app {
namespace {

class WindowsPointerHold final : public PointerHold {
public:
    WindowsPointerHold(QWindow& window, const QRectF& area)
        : window_(&window)
        , area_(area)
    {
        // The window moved: so does its area on the screen.
        connect(&window, &QWindow::xChanged, this, &WindowsPointerHold::place);
        connect(&window, &QWindow::yChanged, this, &WindowsPointerHold::place);
        place();
    }

    ~WindowsPointerHold() override { let_go(); }

    void move(const QRectF& area) override
    {
        area_ = area;
        place();
    }

protected:
    void let_go() override
    {
        window_.clear();
        if (clipped_) {
            ClipCursor(nullptr);
            clipped_ = false;
        }
    }

private:
    void place()
    {
        if (!window_) {
            return;
        }
        const QRect area = native_screen_area(*window_, area_);
        if (area.isEmpty()) {
            if (clipped_) {
                ClipCursor(nullptr);
                clipped_ = false;
            }
            return;
        }
        // Right and bottom are one past the area, as Windows takes them. A
        // cursor outside is moved into it.
        const RECT clip{area.left(), area.top(), area.left() + area.width(),
                        area.top() + area.height()};
        clipped_ = ClipCursor(&clip) != 0;
    }

    QPointer<QWindow> window_;
    /// In the window's logical pixels.
    QRectF area_;
    bool clipped_ = false;
};

class WindowsPointerConfinement final : public PointerConfinement {
protected:
    std::unique_ptr<PointerHold> make_hold(QWindow& window, const QRectF& area) override
    {
        return std::make_unique<WindowsPointerHold>(window, area);
    }
};

} // namespace

std::unique_ptr<PointerConfinement> make_windows_pointer_confinement()
{
    return std::make_unique<WindowsPointerConfinement>();
}

} // namespace seventhslide::app
