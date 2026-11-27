#pragma once

// The pointer kept inside an area of a window by the windowing system itself,
// as a game or a virtual machine's window keeps it: it cannot get past the
// area's edge, however fast it moves. The live web page holds it while it is
// worked from the Controller (WebPageItem).
//
// Each system has its own way, and the app uses it where the build has it
// (pointer_confinement_native.hpp):
//
//   Wayland  the compositor's pointer constraints (zwp_pointer_constraints_v1),
//            confined to a region of the window's surface
//   X11      four XFixes pointer barriers round the area, in the X server
//   Windows  the cursor clip (ClipCursor)
//
// macOS has no such thing; there, and under a compositor without pointer
// constraints, no pointer is held.

#include <QObject>
#include <QPointer>
#include <QRectF>

#include <memory>

class QWindow;

namespace seventhslide::app {

class PointerConfinement;

/**
 * The pointer held in an area of one window, for as long as this lives.
 *
 * The system may let it go before then (the window lost focus, the person
 * broke the hold with the compositor's shortcut), and a newer hold takes over
 * from this one: either way ``lost`` says so once, and the hold does nothing
 * more.
 */
class PointerHold : public QObject {
    Q_OBJECT

public:
    /// Each kind of hold lets the pointer go in its own destructor.
    ~PointerHold() override = default;

    /// Keep the pointer in ``area`` of the window instead (its logical
    /// pixels, as the window's items are placed).
    virtual void move(const QRectF& area) = 0;

signals:
    /// The pointer is held no more.
    void lost();

protected:
    PointerHold() = default;

    /// Let the pointer go, natively. Once only: again does nothing.
    virtual void let_go() = 0;

    /// Let the pointer go and say so; the system let it go, or another hold
    /// took over.
    void end();

    friend class PointerConfinement;
};

/// What holds the pointer. One hold at a time, as the systems allow: a new
/// hold ends the one before.
class PointerConfinement {
public:
    virtual ~PointerConfinement() = default;

    /// Hold the pointer in ``area`` of ``window`` (its logical pixels) until
    /// the hold is dropped; null when it cannot be held there.
    [[nodiscard]] std::unique_ptr<PointerHold> hold(QWindow& window, const QRectF& area);

protected:
    /// A hold of the pointer in ``area`` of ``window``; null when the system
    /// will not hold it there.
    virtual std::unique_ptr<PointerHold> make_hold(QWindow& window, const QRectF& area) = 0;

private:
    /// The hold made last, while it lives.
    QPointer<PointerHold> current_;
};

/// The windowing system's own, for the platform the app runs on; null where
/// this build has none for it, or the system offers none. After the
/// QGuiApplication exists.
[[nodiscard]] std::unique_ptr<PointerConfinement> make_native_pointer_confinement();

} // namespace seventhslide::app
