pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// Two or three choices in one outlined strip (Guided / XML). ``options`` are
// the labels, ``current`` the chosen index; ``chosen(index)`` asks for a
// change.
Rectangle {
    id: seg
    property var options: []
    property int current: 0
    /// The options share the control's width, rather than each its label's.
    property bool stretch: false
    readonly property int share: Math.floor((seg.width - 2) / Math.max(1, seg.options.length))
    signal chosen(int index)

    implicitWidth: row.implicitWidth + 2
    implicitHeight: Style.btnCompactH + 2
    radius: Style.radiusSmall
    color: "transparent"
    border.color: Style.btnEdge
    clip: true

    Row {
        id: row
        x: 1
        y: 1
        Repeater {
            model: seg.options
            delegate: Rectangle {
                id: option
                required property string modelData
                required property int index
                readonly property bool on: option.index === seg.current
                // Stretched, whole pixels: the last takes what rounding leaves.
                width: !seg.stretch ? label.implicitWidth + 24
                     : option.index < seg.options.length - 1 ? seg.share
                     : seg.width - 2 - seg.share * (seg.options.length - 1)
                height: Style.btnCompactH
                gradient: Gradient {
                    GradientStop { position: 0.0; color: option.on ? Style.btnNeutralHoverTop : "transparent" }
                    GradientStop { position: 1.0; color: option.on ? Style.btnNeutralBot : "transparent" }
                }
                Rectangle {
                    visible: option.index > 0
                    width: 1
                    height: parent.height
                    color: Style.btnEdge
                }
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: option.modelData
                    color: option.on ? Style.textStrong
                         : (hover.containsMouse ? Style.text : Style.textCaption)
                    font.pixelSize: Style.fontBody
                    font.weight: option.on ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (!option.on) seg.chosen(option.index)
                }
            }
        }
    }
}
