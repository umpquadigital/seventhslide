import QtQuick
import SeventhSlide

// One panel of the operator window (Service, Library, Outputs, Audio Mixer): its title bar
// and the panel itself below it, placed where its DockArea says. A click on
// the bar collapses the panel to it, or opens it again; collapsed beside
// other panels (in a row), the bar stands on end as a strip, its title
// running down it. Hidden, the panel is not shown at all.
//
// In Edit Mode the panel is outlined and moves by its title bar: drag the bar
// to any side of another panel or the Controller, or to an edge of the
// window.
Item {
    id: panel

    required property DockArea area
    /// The panel's id in DockLayout ("service", "library", "outputs", "mixer").
    required property string dockId
    property string title: ""
    default property alias content: body.data

    readonly property var place: panel.area.geometry.places[panel.dockId] ?? null
    readonly property bool editing: panel.area.layout.editMode
    readonly property bool collapsed: panel.area.layout.collapsed.indexOf(panel.dockId) >= 0
    /// Collapsed in a row: the bar turned to run down the panel's height.
    readonly property bool strip: !!panel.place && panel.place.strip

    visible: panel.place !== null
    x: panel.place ? panel.place.x : 0
    y: panel.place ? panel.place.y : 0
    width: panel.place ? panel.place.width : 0
    height: panel.place ? panel.place.height : 0
    opacity: panel.area.dragId === panel.dockId ? 0.35 : 1
    clip: true

    Rectangle {
        anchors.fill: parent
        color: Style.panelBg
    }

    DockTitleBar {
        id: header
        // As a strip, turned a quarter clockwise about its top left corner and
        // moved back over the panel: it reads top to bottom.
        x: panel.strip ? panel.width : 0
        width: panel.strip ? panel.height : panel.width
        rotation: panel.strip ? 90 : 0
        transformOrigin: Item.TopLeft
        hot: titleArea.containsMouse

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: panel.title
            color: Style.textBright
            font.pixelSize: Style.fontUi
            font.bold: true
            elide: Text.ElideRight
        }

        // A click collapses or opens the panel; in Edit Mode a drag moves it.
        MouseArea {
            id: titleArea
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: !panel.editing ? Qt.PointingHandCursor
                       : (titleArea.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
            property bool dragging: false
            /// This press became a drag, so its release is not a click.
            property bool moved: false
            property point pressAt: Qt.point(0, 0)

            onPressed: function (mouse) {
                titleArea.dragging = false
                titleArea.moved = false
                titleArea.pressAt = Qt.point(mouse.x, mouse.y)
            }
            onPositionChanged: function (mouse) {
                if (!pressed || !panel.editing)
                    return
                const dx = mouse.x - titleArea.pressAt.x
                const dy = mouse.y - titleArea.pressAt.y
                if (!titleArea.dragging && dx * dx + dy * dy > 25) {
                    titleArea.dragging = true
                    titleArea.moved = true
                    panel.area.beginPanelDrag(panel.dockId, panel.title)
                }
                if (titleArea.dragging)
                    panel.area.updatePanelDrag(titleArea.mapToItem(panel.area, mouse.x, mouse.y))
            }
            onReleased: {
                if (titleArea.dragging)
                    panel.area.finishPanelDrag()
                titleArea.dragging = false
            }
            onClicked: {
                if (!titleArea.moved)
                    panel.area.layout.setCollapsed(panel.dockId, !panel.collapsed)
            }
            onCanceled: {
                if (titleArea.dragging)
                    panel.area.cancelPanelDrag()
                titleArea.dragging = false
            }
        }
    }

    Item {
        id: body
        visible: !panel.collapsed
        anchors.top: parent.top
        anchors.topMargin: header.height
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }

    // Edit Mode's outline, over the panel.
    Rectangle {
        anchors.fill: parent
        visible: panel.editing
        color: "transparent"
        border.color: Style.dockEditEdge
        z: 10
    }
}
