pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import SeventhSlide

// The ribbon: two rows of controls for the tab chosen, edge to edge on the
// Library toolbar's surface, under the tab row. A tab wider than the window
// scrolls sideways, by a bar in the margin under the rows or the wheel.
Rectangle {
    id: bar
    required property SlideEditState dialog

    // The two rows, a margin above and below them, and the rule.
    implicitHeight: Style.ribbonEdge + Style.ribbonTallH + Style.ribbonEdge + 1
    color: Style.panelBg

    /// The open tab's width, all its groups.
    property real tabWidth: 0
    onTabWidthChanged: bar.keepInView()
    onWidthChanged: bar.keepInView()
    Connections {
        target: bar.dialog
        function onRibbonTabChanged() { strip.contentX = 0 }
    }
    /// After a resize, no gap at the end of a scrolled tab.
    function keepInView() {
        strip.contentX = Math.max(0, Math.min(strip.contentX, strip.contentWidth - strip.width))
    }

    component RibbonTab: Loader {
        id: slot
        required property string tab
        default property Component face
        readonly property bool current: bar.dialog.ribbonTab === slot.tab
        property bool kept: false
        active: slot.current || slot.kept
        onLoaded: slot.kept = true
        visible: slot.current
        height: parent ? parent.height : 0
        sourceComponent: slot.face
        /// Its groups' width while it shows; -1 when hidden.
        readonly property real shownWidth: {
            const face = slot.item as Item
            return slot.current && face ? face.implicitWidth : -1
        }
        onShownWidthChanged: if (slot.shownWidth >= 0) bar.tabWidth = slot.shownWidth
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Style.wellEdge
    }
    Flickable {
        id: strip
        anchors.fill: parent
        anchors.bottomMargin: 1
        clip: true
        contentWidth: Math.max(strip.width, bar.tabWidth)
        contentHeight: strip.height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        // Moved by its bar and the wheel, not a drag: a drag is a control's.
        interactive: false
        // Slim, in the margin under the rows, clear of them.
        ScrollBar.horizontal: AppScrollBar {
            implicitHeight: Style.ribbonEdge - 1
            padding: 1
        }
        WheelHandler {
            enabled: strip.contentWidth > strip.width
            onWheel: function (event) {
                const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                strip.contentX = Math.max(0, Math.min(strip.contentWidth - strip.width,
                                                      strip.contentX - delta / 2))
            }
        }
        Item {
            y: Style.ribbonEdge
            width: strip.contentWidth
            height: Style.ribbonTallH
            // A tab is made the first time it shows, then kept (a gallery keeps
            // its page): until then its bindings cost nothing.
            RibbonTab { tab: "home"; RibbonHome { dialog: bar.dialog } }
            RibbonTab { tab: "insert"; RibbonInsert { dialog: bar.dialog } }
            RibbonTab { tab: "design"; RibbonDesign { dialog: bar.dialog } }
            RibbonTab { tab: "transitions"; RibbonTransitions { dialog: bar.dialog } }
            RibbonTab { tab: "animations"; RibbonAnimations { dialog: bar.dialog } }
            RibbonTab { tab: "cues"; RibbonCues { dialog: bar.dialog } }
        }
    }
}
