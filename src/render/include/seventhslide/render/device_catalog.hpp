#pragma once

// The screens, virtual cameras and DeckLink devices a destination can point
// at, and what to call them.
// Seam: Qt Gui. Call on the GUI thread.

#include "seventhslide/render/bmd_runtime.hpp"
#include "seventhslide/render/destination.hpp"
#include "seventhslide/render/virtual_camera.hpp"

#include <QHash>
#include <QRect>
#include <QSize>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

class QScreen;
class QWindow;

namespace seventhslide::render {

class DeviceCatalog {
public:
    DeviceCatalog();
    ~DeviceCatalog();
    DeviceCatalog(const DeviceCatalog&) = delete;
    DeviceCatalog& operator=(const DeviceCatalog&) = delete;

    /// Stable-ish key for a screen: its serial (``serial:``), else (macOS) its
    /// display's UUID (``display:``), else its maker
    /// and model with where it is (``model:Dell U2720Q@0,0,3840,2160``),
    /// else its name with where it is (``name:``), else where it is
    /// (``geom:``). The model before the name: on Windows the name is
    /// ``\\.\DISPLAY1``, renumbered as displays come and go. A key never
    /// depends on the other screens, so plugging in a second changes none.
    [[nodiscard]] static QString screen_key(const QScreen* screen);
    /// The window the operator works in, whose screen no new output goes to
    /// first and where an output stays an ordinary window (macOS). Null
    /// (the tests, the server) for none.
    static void set_operator_window(QWindow* window);
    /// The screen the operator's window is on; null for none.
    [[nodiscard]] static QScreen* operator_screen();
    /// A screen for an output with nowhere else to go: one the operator is
    /// not on, else the first.
    [[nodiscard]] static QScreen* audience_screen();
    /// "x,y,w,h" of the screen's logical geometry; empty for none.
    [[nodiscard]] static QString bounds_string(const QScreen* screen);
    /// The screen ``key`` names; the primary screen for an empty key.
    [[nodiscard]] static QScreen* screen_by_key(const QString& key);
    /// The screen display destination ``d`` was last on: by key; then, for a
    /// ``model:`` key, the one screen of that model (moved, as screens were
    /// rearranged); then by the screen's name, then by its geometry. Null
    /// when none matches.
    [[nodiscard]] static QScreen* match_screen(const Destination& d);
    /// Point display destination ``dest`` at ``screen``, remembering the
    /// screen's name and geometry to find it again. Null does nothing.
    static void apply_screen(Destination& dest, const QScreen* screen);
    /// A name that says nothing: empty, or a Wayland object id ("0x40a0").
    [[nodiscard]] static bool opaque_name(const QString& name);

    struct Screen {
        QString key;
        /// 1-based, in QGuiApplication::screens() order.
        int index = 0;
        /// "Screen 1 · DELL U2720Q · 3840×2160".
        QString label;
        QString name;
        bool primary = false;
        qreal scale = 1.0;
        QRect bounds;
        /// Device pixels.
        QSize native;
    };
    [[nodiscard]] static std::vector<Screen> screens();

    /// The camera's label, or ``device_path`` when it is not attached.
    [[nodiscard]] static QString camera_label(const QString& device_path);
    /// The DeckLink device's label; empty when it is not attached.
    [[nodiscard]] static QString bmd_device_label(const QString& device_id);

    /// What the destination list calls ``d``'s target.
    [[nodiscard]] QString target_label(const Destination& d) const;
    /// ``d``'s target is not there: no screen, device, runtime or name.
    [[nodiscard]] bool target_missing(const Destination& d) const;

private:
    /**
     * Hardware lists for the destination list, which is read on every session
     * change that refreshes models. Enumerating DeckLink and V4L2 devices per
     * destination per call was its cost; one snapshot serves for a second.
     * The operator's own actions (adding, binding, starting) read the
     * hardware fresh: they are rare, and a device just plugged in must count.
     */
    struct Snapshot;
    [[nodiscard]] Snapshot& snapshot() const;
    [[nodiscard]] QString bmd_label_for(const Destination& dest, Snapshot& devices) const;

    mutable std::unique_ptr<Snapshot> snapshot_;
};

}  // namespace seventhslide::render
