import QtQuick
import SeventhSlide

// A number on the ribbon or the sidebar: a quiet label, the field, and a +
// and - at its right (``stepper``). Enter or leaving the field commits; the
// value is clamped to ``minValue`` / ``maxValue``. ``unit`` follows the number
// ("px", "%"). With ``displayScale`` the field shows ``value * displayScale``
// to ``decimals`` places (milliseconds as seconds) and commits in the value's
// own unit.
Row {
    id: field
    property string label: ""
    property real value: 0
    property real step: 1
    property bool isFloat: false
    property var minValue: null
    property var maxValue: null
    property int fieldWidth: 64
    property string unit: ""
    /// What the field shows of the value, and to how many places.
    property real displayScale: 1
    property int decimals: 0
    /// The + and - beside the number; off, just the number.
    property bool stepper: true
    /// In the sidebar: the shorter control height.
    property bool compact: false
    signal commit(real n)

    spacing: 6
    height: field.compact ? Style.btnCompactH : Style.ribbonH
    opacity: enabled ? 1 : Style.offOpacity

    function formatValue(n) {
        const s = field.displayScale !== 1 ? (n * field.displayScale).toFixed(field.decimals)
                  : field.isFloat ? String(Math.round(n * 1000) / 1000) : String(Math.round(n))
        return field.unit.length ? s + " " + field.unit : s
    }
    function parseValue(text) {
        const v = String(text).replace(field.unit, "").trim()
        if (!v.length)
            return NaN
        if (field.displayScale !== 1)
            return parseFloat(v) / field.displayScale
        return field.isFloat ? parseFloat(v) : parseInt(v, 10)
    }
    function commitValue(n) {
        if (isNaN(n))
            return
        if (field.minValue !== null && n < field.minValue)
            n = field.minValue
        if (field.maxValue !== null && n > field.maxValue)
            n = field.maxValue
        const out = field.isFloat ? Math.round(n * 1000) / 1000 : Math.round(n)
        field.commit(out)
        if (input.activeFocus)
            input.text = field.formatValue(out)
    }

    Text {
        visible: field.label.length > 0
        anchors.verticalCenter: parent.verticalCenter
        text: field.label
        color: Style.textCaption
        font.pixelSize: field.compact ? Style.fontSmall : Style.ribbonCaption
    }
    Rectangle {
        width: field.fieldWidth
        height: field.height
        radius: Style.radiusSmall
        color: input.activeFocus ? Style.fieldFaceFocus : Style.fieldFace
        border.color: input.activeFocus ? Style.focusEdge
                    : (hover.containsMouse ? Style.controlEdgeHover : Style.controlEdge)
        clip: true

        TextInput {
            id: input
            anchors.left: parent.left
            anchors.right: field.stepper ? spinner.left : parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 9
            verticalAlignment: TextInput.AlignVCenter
            color: Style.text
            font.pixelSize: field.compact ? Style.fontBody : Style.ribbonFont
            selectByMouse: true
            enabled: field.enabled
            Binding on text {
                when: !input.activeFocus
                value: field.formatValue(field.value)
                restoreMode: Binding.RestoreNone
            }
            onEditingFinished: {
                // Leaving a field untouched writes nothing: what it shows
                // may be another object's by then.
                if (text === field.formatValue(field.value))
                    return
                const n = field.parseValue(text)
                if (isNaN(n)) {
                    text = field.formatValue(field.value)
                    return
                }
                field.commitValue(n)
            }
            // Enter commits and gives the keys back to the editor.
            Keys.onReturnPressed: field.forceActiveFocus()
            Keys.onEnterPressed: field.forceActiveFocus()
            Keys.onUpPressed: field.commitValue(Number(field.value) + field.step)
            Keys.onDownPressed: field.commitValue(Number(field.value) - field.step)
        }
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            cursorShape: Qt.IBeamCursor
        }
        Column {
            id: spinner
            visible: field.stepper
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            anchors.rightMargin: 1
            width: field.compact ? 15 : Style.ribbonStepW
            Rectangle {
                width: parent.width
                height: Math.floor(parent.height / 2)
                color: plus.containsMouse ? Style.stepperBgHover : "transparent"
                Rectangle { width: 1; height: parent.height; color: Style.controlEdge }
                BadgeText {
                    anchors.centerIn: parent
                    text: "+"
                    color: Style.chevronInk
                    font.pixelSize: Style.fontMicro
                }
                MouseArea {
                    id: plus
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: field.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: field.commitValue(Number(field.value) + field.step)
                }
            }
            Rectangle {
                width: parent.width
                height: Math.ceil(parent.height / 2)
                color: minus.containsMouse ? Style.stepperBgHover : "transparent"
                Rectangle { width: 1; height: parent.height; color: Style.controlEdge }
                Rectangle { width: parent.width; height: 1; color: Style.controlEdge }
                BadgeText {
                    anchors.centerIn: parent
                    text: "\u2212"
                    color: Style.chevronInk
                    font.pixelSize: Style.fontMicro
                }
                MouseArea {
                    id: minus
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: field.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: field.commitValue(Number(field.value) - field.step)
                }
            }
        }
    }
}
