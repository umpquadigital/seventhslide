import QtQuick
import SeventhSlide

// A number with a + and - at its right (``stepper``), and a quiet label
// before it: the slide editor's ribbon and sidebar, and a form's (an audio
// fade's length, FadeField). Enter or leaving the field commits; the
// value is clamped to ``minValue`` / ``maxValue``. ``unit`` follows the number
// ("px", "%"). With ``displayScale`` the field shows ``value * displayScale``
// to ``decimals`` places (milliseconds as seconds) and commits in the value's
// own unit; ``trimZeros`` drops the zeros that end it ("1.5 s", not "1.50 s").
//
// In a dialog's form: ``live`` commits each number as it is typed, so a Save
// clicked straight after typing has it; ``allowEmpty`` lets the field stand
// empty (``empty``, showing ``placeholder``), and emptying it emits
// ``cleared``. A step from empty starts at ``value``. Enter emits
// ``submitted``; ``tabFocus`` puts the field in the form's Tab order.
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
    property bool trimZeros: false
    property bool live: false
    property bool allowEmpty: false
    /// Nothing of its own: the field shows ``placeholder`` (``allowEmpty``).
    property bool empty: false
    property string placeholder: ""
    property bool tabFocus: false
    /// What the field holds, as typed.
    readonly property alias text: input.text
    signal commit(real n)
    signal cleared()
    signal submitted()

    spacing: 6
    height: field.compact ? Style.btnCompactH : Style.ribbonH
    opacity: enabled ? 1 : Style.offOpacity

    function formatValue(n) {
        let s = field.displayScale !== 1 ? (n * field.displayScale).toFixed(field.decimals)
                : field.isFloat ? String(Math.round(n * 1000) / 1000) : String(Math.round(n))
        if (field.trimZeros && s.indexOf(".") >= 0)
            s = s.replace(/\.?0+$/, "")
        return field.unit.length ? s + " " + field.unit : s
    }
    /// What the field shows when the caret is elsewhere.
    function shown() {
        return field.allowEmpty && field.empty ? "" : field.formatValue(field.value)
    }
    /// Whether ``text`` holds nothing but the unit.
    function isBlank(text) {
        return !String(text).replace(field.unit, "").trim().length
    }
    function parseValue(text) {
        const v = String(text).replace(field.unit, "").trim()
        if (!v.length)
            return NaN
        if (field.displayScale !== 1)
            return parseFloat(v) / field.displayScale
        return field.isFloat ? parseFloat(v) : parseInt(v, 10)
    }
    /// ``keepText``: as typed (``live``), the text is left as it is.
    function commitValue(n, keepText) {
        if (isNaN(n))
            return
        if (field.minValue !== null && n < field.minValue)
            n = field.minValue
        if (field.maxValue !== null && n > field.maxValue)
            n = field.maxValue
        const out = field.isFloat ? Math.round(n * 1000) / 1000 : Math.round(n)
        field.commit(out)
        if (input.activeFocus && !keepText)
            input.text = field.formatValue(out)
    }
    /// Test seam: the caret into the field, without a click.
    function focusInput() { input.forceActiveFocus() }

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
            activeFocusOnTab: field.tabFocus
            enabled: field.enabled
            Binding on text {
                when: !input.activeFocus
                value: field.shown()
                restoreMode: Binding.RestoreNone
            }
            onTextChanged: {
                if (!field.live || !input.activeFocus)
                    return
                if (field.allowEmpty && field.isBlank(text))
                    field.cleared()
                else
                    field.commitValue(field.parseValue(text), true)
            }
            onEditingFinished: {
                // Leaving a field untouched writes nothing: what it shows
                // may be another object's by then.
                if (text === field.shown())
                    return
                if (field.allowEmpty && field.isBlank(text)) {
                    field.cleared()
                    return
                }
                const n = field.parseValue(text)
                if (isNaN(n)) {
                    text = field.shown()
                    return
                }
                field.commitValue(n)
            }
            // Enter commits and gives the keys back to the editor.
            Keys.onReturnPressed: {
                field.forceActiveFocus()
                field.submitted()
            }
            Keys.onEnterPressed: {
                field.forceActiveFocus()
                field.submitted()
            }
            Keys.onUpPressed: field.commitValue(Number(field.value) + field.step)
            Keys.onDownPressed: field.commitValue(Number(field.value) - field.step)
        }
        Text {
            anchors.left: input.left
            anchors.right: input.right
            anchors.verticalCenter: parent.verticalCenter
            visible: field.placeholder.length > 0 && !input.text.length && !input.activeFocus
            elide: Text.ElideRight
            text: field.placeholder
            color: Style.fieldPlaceholder
            font.pixelSize: input.font.pixelSize
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
                    objectName: "stepUp"
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
                    objectName: "stepDown"
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
