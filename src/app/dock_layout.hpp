#pragma once

// Where the operator window's panels sit: the Service, Library, Outputs and
// Audio Mixer panels and the Controller are the leaves of a tree of splits,
// each split laying its children out in a row (side by side) or a column
// (one over another), so a panel can sit beside, above or below any other
// panel, the Controller, or along a whole edge of the window. A panel can be
// collapsed to its title bar or hidden. Kept per computer (QSettings), not in
// the library: it is how this screen is laid out, not the church's content.
//
// DockState is the layout and its rules, plain and testable, and where its
// panels land in a window of a given size; DockLayout is it for QML, saved
// as it changes. The view (DockArea.qml) places items where the geometry
// says and asks for changes.

#include <nlohmann/json.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::app {

struct DockState {
    /// How a split lays out its children: side by side, or one over another.
    enum class Axis { Row, Column };
    /// Which side of a panel (or of the whole window) another lands on.
    enum class Edge { Left, Right, Top, Bottom };

    struct Node {
        /// A leaf: the panel's id, or k_controller. A split: empty.
        std::string id;
        /// A split's direction.
        Axis axis = Axis::Row;
        /// A split's children, first to last (left to right, top to bottom).
        std::vector<Node> children;
        /// Its share of its split's length, against the siblings sharing by
        /// weight (in a split without the Controller).
        double weight = 1.0;
        /// Its length in pixels along its split, in a split beside the
        /// Controller, which takes what the others leave; 0 is the default.
        int extent = 0;
        /// A leaf's: hidden, it keeps its place and takes no room.
        bool shown = true;
        /// A leaf's: down to its title bar along its split (a bar in a
        /// column, a strip in a row); the others take its room.
        bool collapsed = false;

        [[nodiscard]] bool is_leaf() const noexcept { return !id.empty(); }
        [[nodiscard]] bool operator==(const Node&) const = default;
    };

    /// The leaf where the Controller goes. It is always in the tree, and
    /// never hidden, collapsed or moved: the panels move round it.
    static constexpr std::string_view k_controller = "controller";

    Node root;

    static constexpr int k_max_extent = 10000;

    /// The sizes a layout is drawn with.
    struct Metrics {
        /// A panel's title bar: a collapsed panel is this long.
        int header = 36;
        /// The lines between children.
        int line = 1;
        /// The Controller keeps at least this much; the panels give way. The
        /// Controller asks for more while it shows two wells (Preview and
        /// Program side by side), so this is the layout's input.
        int controller_min_width = 360;
        int controller_min_height = 200;
        /// A line dragged leaves a panel at least this big.
        int panel_min_width = 160;
        int panel_min_height = 80;
        /// A panel beside the Controller with no size of its own yet.
        int default_width = 300;
        int default_height = 260;
    };

    struct Rect {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        [[nodiscard]] bool operator==(const Rect&) const = default;
    };

    /// Where everything lands in a window of a given size.
    struct Geometry {
        struct Place {
            std::string id;
            Rect rect;
            /// Collapsed in a row: a strip with its title running down it.
            bool strip = false;
        };
        struct Line {
            /// The split it divides, as child indices from the root joined by
            /// '.' ("" is the root), and the children either side.
            std::string path;
            int before = 0;
            int after = 0;
            /// A line between children of a row, dragged left and right.
            bool vertical = false;
            Rect rect;
            /// Neither side is collapsed.
            bool resizable = false;
            /// The child before it: where it starts, how long it and the one
            /// after are together, and how long it may be when the line moves.
            int start = 0;
            int total = 0;
            int min = 0;
            int max = 0;
        };
        /// Every leaf shown, the Controller among them.
        std::vector<Place> places;
        std::vector<Line> lines;

        [[nodiscard]] const Place* find(std::string_view id) const;
    };

    /// The panels there are, in their default places.
    static DockState defaults();
    /// The ids of every panel (not the Controller).
    static const std::vector<std::string>& panel_ids();

    /// ``j`` as a layout: what it says that makes sense, defaults for the
    /// rest (an unknown or repeated panel is dropped, a missing one goes
    /// along the right edge, a weight that is not a positive number is 1, a
    /// layout without the Controller or without "root" is the default).
    static DockState from_json(const nlohmann::json& j);
    [[nodiscard]] nlohmann::json to_json() const;

    [[nodiscard]] const Node* find(std::string_view id) const;
    /// The tree in short, for tests and logs: a split is "row(...)" or
    /// "column(...)", a leaf its id with ":hidden" or ":collapsed" after.
    [[nodiscard]] std::string describe() const;

    /// Move panel ``id`` to ``edge`` of leaf ``target`` (a panel shown, or
    /// the Controller), or of the whole window when ``target`` is empty. It
    /// is shown, at its default size there. False when nothing moved.
    bool move(std::string_view id, std::string_view target, Edge edge);
    bool set_shown(std::string_view id, bool shown);
    bool set_collapsed(std::string_view id, bool collapsed);
    /// Move the line between children ``before`` and ``after`` of the split
    /// at ``path`` (see Geometry::Line): ``before`` takes ``before_px`` of the
    /// ``total_px`` they share. False when there is no such pair, or either is
    /// collapsed.
    bool resize_line(std::string_view path, int before, int after, int before_px, int total_px);

    [[nodiscard]] Geometry layout(int width, int height, const Metrics& metrics) const;

    [[nodiscard]] bool operator==(const DockState&) const = default;
    /// The same panels in the same places, shown and collapsed alike, sizes
    /// aside.
    [[nodiscard]] bool same_places(const DockState& other) const;
};

class DockLayout : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Where the panels are and how big they are notify apart: a resize must
    // not rebuild what depends only on where they are (the lines being
    // dragged).
    /// The panels hidden.
    Q_PROPERTY(QStringList hidden READ hidden NOTIFY placesChanged)
    /// The panels collapsed to their title bars, shown or not.
    Q_PROPERTY(QStringList collapsed READ collapsed NOTIFY placesChanged)
    /// The lines between children, each { key, path, before, after,
    /// vertical }; geometry() says where each key is drawn.
    Q_PROPERTY(QVariantList lines READ lines NOTIFY placesChanged)
    /// Counts every change, so a binding that asks for geometry() runs
    /// again.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    /// Panels move by their title bars only while this is on, so none moves
    /// by accident during a service. Resizing and collapsing need no Edit
    /// Mode. Off at every start; not saved.
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)

public:
    /// Where the layout is kept in QSettings.
    static constexpr std::string_view k_settings_key = "mainWindow/dockLayout";

    /// The layout this computer last kept, or the default.
    explicit DockLayout(QObject* parent = nullptr);

    [[nodiscard]] const DockState& state() const noexcept { return state_; }

    [[nodiscard]] QStringList hidden() const;
    [[nodiscard]] QStringList collapsed() const;
    [[nodiscard]] QVariantList lines() const;
    [[nodiscard]] int revision() const noexcept { return revision_; }
    [[nodiscard]] bool editMode() const noexcept { return edit_mode_; }
    void setEditMode(bool on);

    /// Where everything lands in an area ``width`` by ``height`` with title
    /// bars ``header`` high, the Controller kept at least ``controllerWidth``
    /// wide (when the window has room): { places: { id: { x, y, width,
    /// height, strip } }, lines: { key: { x, y, width, height, resizable,
    /// start, total, min, max } } }. The Controller's place is "controller".
    Q_INVOKABLE QVariantMap geometry(int width, int height, int header, int controllerWidth) const;

    /// ``edge`` is "left", "right", "top" or "bottom"; ``target`` a leaf, or
    /// empty for the whole window. See DockState for each.
    Q_INVOKABLE void movePanel(const QString& id, const QString& target, const QString& edge);
    Q_INVOKABLE void setShown(const QString& id, bool shown);
    Q_INVOKABLE void setCollapsed(const QString& id, bool collapsed);
    Q_INVOKABLE void resizeLine(const QString& path, int before, int after, int beforePx,
                                int totalPx);
    /// Every panel back in its default place, shown, at the default sizes.
    Q_INVOKABLE void reset();

signals:
    /// A panel moved, was hidden or shown, or collapsed or opened.
    void placesChanged();
    /// A panel's size changed, and nothing moved.
    void sizesChanged();
    /// Anything changed.
    void changed();
    void editModeChanged();

private:
    /// Keep ``next`` if it differs: save it and say what changed.
    void apply(const DockState& next);

    /// Where the layout is kept. One for the life of the layout: a drag
    /// changes it many times a second, and this one writes the file when Qt
    /// next syncs it, not on every change.
    QSettings settings_;
    DockState state_;
    int revision_ = 0;
    bool edit_mode_ = false;
};

} // namespace seventhslide::app
