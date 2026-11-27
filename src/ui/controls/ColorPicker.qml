pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import SeventhSlide

// Shared HTML hex field + clickable swatch that opens a GUI color picker.
// Used by Freeform and the text / background theme designers.
//
// The picker is an in-window overlay (same pattern as AppDialog), not
// QtQuick.Dialogs.ColorDialog. That type's native GTK helper is WindowModal
// and regularly fails to map on QGuiApplication + Wayland/GTK - the operator
// UI then looks frozen because the grab is held by a dialog that never
// appears. Qt 6.10+ also defaults the Quick fallback to a separate Window,
// which hits the same grab-without-surface failure from these editor overlays.
Item {
    // A bar's handle (hue, opacity) and the press that drags it: it stands at
    // ``value`` (0 to 1 across the bar), and a press or drag says where in
    // ``picked``.
    component BarGrip: Item {
        id: grip
        property real value: 0
        signal picked(real value)
        anchors.fill: parent

        Rectangle {
            width: 8
            height: parent.height + 4
            radius: 2
            x: grip.value * grip.width - width / 2
            y: -2
            color: "transparent"
            border.color: Style.pickerRing
            border.width: 2
        }

        MouseArea {
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.PointingHandCursor
            function apply(mx) {
                grip.picked(Math.max(0, Math.min(1, mx / Math.max(1, grip.width))))
            }
            onPressed: function (mouse) { apply(mouse.x) }
            onPositionChanged: function (mouse) {
                if (pressed)
                    apply(mouse.x)
            }
        }
    }

    id: root

    property string value: "#ffffff"
    property string placeholder: ""
    property int fieldWidth: 78
    property int controlHeight: 26
    property string fallbackColor: "#ffffff"
    property string dialogTitle: qsTr("Select Color")
    /// What the card opens under; the picker's own swatch when unset.
    property Item anchorItem: null
    /// Offer an opacity slider. A translucent pick is written ``#aarrggbb``;
    /// without one the pick is opaque.
    property bool alpha: false

    signal edited(string value)

    readonly property int gap: 4
    readonly property string liveValue: fieldInput.activeFocus ? fieldInput.text : root.value
    readonly property color previewColor: {
        const hex = CssColor.canonical(root.liveValue)
        return hex.length ? hex : Qt.rgba(0, 0, 0, 0)
    }

    implicitWidth: root.fieldWidth + root.gap + root.controlHeight
    implicitHeight: root.controlHeight
    opacity: enabled ? 1 : 0.4

    function openDialog() {
        if (!root.enabled)
            return
        const typed = CssColor.canonical(root.liveValue)
        const fallback = CssColor.canonical(root.fallbackColor)
        sheet.loadColor(typed.length ? typed : (fallback.length ? fallback : "#ffffff"))
        sheet.focusBefore = root.Window.activeFocusItem
        sheet.open = true
        Qt.callLater(function () {
            sheet.positionCard()
            sheet.forceActiveFocus()
        })
        // Again once the window is laid out: a swatch that has only just
        // appeared has not been placed yet.
        placeAgain.restart()
    }

    function commitHex(c) {
        const hex = root.toHtmlHex(c)
        fieldInput.text = hex
        root.edited(hex)
    }

    /// ``c`` as CSS: ``#rrggbb``, or ``#aarrggbb`` when it is translucent and
    /// the picker offers opacity. (QML colour channels are 0-1.)
    function toHtmlHex(c) {
        const toByte = function (n) {
            return Math.max(0, Math.min(255, Math.round(Number(n) * 255)))
        }
        const hex = function (n) {
            const h = toByte(n).toString(16)
            return h.length < 2 ? "0" + h : h
        }
        const rgb = hex(c.r) + hex(c.g) + hex(c.b)
        return (root.alpha && toByte(c.a) < 255) ? "#" + hex(c.a) + rgb : "#" + rgb
    }

    /// A checkerboard of ``cell``-pixel squares: what shows through a
    /// translucent colour.
    component Checkerboard: Item {
        id: board
        property int cell: 6
        readonly property int columns: Math.max(1, Math.ceil(board.width / board.cell))
        readonly property int rows: Math.max(1, Math.ceil(board.height / board.cell))
        clip: true
        Repeater {
            // None while it is not shown: a picker hidden behind a swatch.
            model: board.visible ? board.columns * board.rows : 0
            Rectangle {
                id: square
                required property int index
                readonly property int column: square.index % board.columns
                readonly property int row: Math.floor(square.index / board.columns)
                x: square.column * board.cell
                y: square.row * board.cell
                width: board.cell
                height: board.cell
                color: (square.column + square.row) % 2 === 0 ? Style.checkerLight : Style.checkerDark
            }
        }
    }

    Row {
        spacing: root.gap
        height: root.controlHeight

        Rectangle {
            id: fieldBox
            width: root.fieldWidth
            height: root.controlHeight
            radius: Style.radiusSmall
            color: Style.dialogField
            border.color: fieldInput.activeFocus ? Style.accent
                        : (fieldHover.containsMouse ? Style.dialogControlBorderHover
                                                    : Style.dialogControlBorder)

            TextInput {
                id: fieldInput
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: TextInput.AlignVCenter
                topPadding: 0
                bottomPadding: 0
                color: Style.text
                font.pixelSize: Style.fontUi
                selectByMouse: true
                clip: true
                enabled: root.enabled
                Binding on text {
                    when: !fieldInput.activeFocus
                    value: root.value
                    restoreMode: Binding.RestoreNone
                }
                onEditingFinished: {
                    const stored = CssColor.canonical(text)
                    if (!stored.length) {
                        fieldInput.text = root.value
                        return
                    }
                    fieldInput.text = stored
                    if (stored !== String(root.value))
                        root.edited(stored)
                }
            }
            Text {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: Text.AlignVCenter
                text: root.placeholder
                color: Style.textFaint
                font.pixelSize: Style.fontBody
                visible: !fieldInput.text.length && !fieldInput.activeFocus
            }
            MouseArea {
                id: fieldHover
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                hoverEnabled: true
                cursorShape: Qt.IBeamCursor
            }
        }

        Rectangle {
            id: swatch
            width: root.controlHeight
            height: root.controlHeight
            radius: Style.radiusSmall
            clip: true
            color: Style.swatchWell
            border.color: swatchHover.containsMouse ? Style.dialogControlBorderHover
                                                    : Style.dialogControlBorder

            Checkerboard {
                anchors.fill: parent
                anchors.margins: 1
                cell: Math.ceil(width / 2)

                Rectangle {
                    anchors.fill: parent
                    color: root.previewColor
                }
            }

            MouseArea {
                id: swatchHover
                anchors.fill: parent
                hoverEnabled: true
                enabled: root.enabled
                cursorShape: Qt.PointingHandCursor
                onClicked: root.openDialog()
            }
        }
    }

    Item {
        id: sheet
        property bool open: false
        /// What had the keys before the picker took them: they go back to it,
        /// so typing carries on in the text whose colour was picked.
        property Item focusBefore: null
        onOpenChanged: {
            if (sheet.open || !sheet.focusBefore)
                return
            const back = sheet.focusBefore
            sheet.focusBefore = null
            if (back.visible)
                back.forceActiveFocus()
        }
        property real hue: 0
        property real sat: 1
        property real val: 1
        /// The pick's alpha (0-1); 1 unless the picker offers opacity.
        property real pickOpacity: 1
        readonly property color opaqueColor: Qt.hsva(
            Math.max(0, Math.min(1, sheet.hue)),
            Math.max(0, Math.min(1, sheet.sat)),
            Math.max(0, Math.min(1, sheet.val)),
            1)
        readonly property color liveColor: Qt.rgba(sheet.opaqueColor.r, sheet.opaqueColor.g,
                                                   sheet.opaqueColor.b,
                                                   root.alpha ? sheet.pickOpacity : 1)

        // Typed, so a CSS string arrives as a colour.
        function loadColor(c: color) {
            const h = Number(c.hsvHue)
            sheet.hue = (isNaN(h) || h < 0) ? 0 : h
            const s = Number(c.hsvSaturation)
            sheet.sat = isNaN(s) ? 0 : Math.max(0, Math.min(1, s))
            const v = Number(c.hsvValue)
            sheet.val = isNaN(v) ? 1 : Math.max(0, Math.min(1, v))
            sheet.pickOpacity = Math.max(0, Math.min(1, Number(c.a)))
        }

        function positionCard() {
            const win = root.Window.window
            if (!win)
                return
            const ci = win.contentItem
            const gap = 6
            const under = root.anchorItem || swatch
            const p = under.mapToItem(ci, 0, 0)
            let cx = p.x
            let cy = p.y + under.height + gap
            if (cx + card.width > ci.width - 8)
                cx = Math.max(8, ci.width - card.width - 8)
            if (cy + card.height > ci.height - 8)
                cy = p.y - card.height - gap
            if (cx < 8)
                cx = 8
            if (cy < 8)
                cy = 8
            card.x = cx
            card.y = cy
        }

        Timer {
            id: placeAgain
            interval: 30
            onTriggered: if (sheet.open) sheet.positionCard()
        }

        visible: sheet.open
        z: Style.modalZ
        parent: Window.window ? Window.window.contentItem : parent
        width: visible && parent ? parent.width : 0
        height: visible && parent ? parent.height : 0
        focus: sheet.open

        Keys.onEscapePressed: function (event) {
            sheet.open = false
            event.accepted = true
        }

        ModalScrim {
            anchors.fill: parent
            onClicked: sheet.open = false
        }

        // The card, made while it is open: there is one picker behind every
        // swatch, and most are never opened.
        Loader {
            id: card
            active: sheet.open
            sourceComponent: Component {
                Rectangle {
                    id: cardFace
                    width: 248
                    height: cardCol.implicitHeight + 20
                    radius: Style.modalRadius
                    color: Style.dialogBg
                    border.color: Style.dialogEdge

                    MouseArea { anchors.fill: parent }

                    Column {
                        id: cardCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 10

                        Text {
                            text: root.dialogTitle
                            color: Style.dialogTitle
                            font.pixelSize: Style.fontUi
                            font.bold: true
                        }

                        Item {
                            id: svBox
                            width: parent.width
                            height: 140
                            clip: true

                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: "#ffffff" }
                                    GradientStop {
                                        position: 1
                                        color: Qt.hsva(sheet.hue, 1, 1, 1)
                                    }
                                }
                            }
                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "#00000000" }
                                    GradientStop { position: 1; color: "#ff000000" }
                                }
                            }

                            Rectangle {
                                width: 12
                                height: 12
                                radius: height / 2
                                x: sheet.sat * svBox.width - width / 2
                                y: (1 - sheet.val) * svBox.height - height / 2
                                color: "transparent"
                                border.color: Style.pickerRing
                                border.width: 2
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -1
                                    radius: width / 2
                                    color: "transparent"
                                    border.color: Style.pickerRingOuter
                                    border.width: 1
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                preventStealing: true
                                cursorShape: Qt.CrossCursor
                                function apply(mx, my) {
                                    sheet.sat = Math.max(0, Math.min(1, mx / Math.max(1, svBox.width)))
                                    sheet.val = Math.max(0, Math.min(1, 1 - my / Math.max(1, svBox.height)))
                                }
                                onPressed: function (mouse) { apply(mouse.x, mouse.y) }
                                onPositionChanged: function (mouse) {
                                    if (pressed)
                                        apply(mouse.x, mouse.y)
                                }
                            }
                        }

                        Item {
                            id: hueBar
                            width: parent.width
                            height: 14

                            Rectangle {
                                anchors.fill: parent
                                radius: Style.radiusSmall
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.00; color: "#ff0000" }
                                    GradientStop { position: 0.17; color: "#ffff00" }
                                    GradientStop { position: 0.33; color: "#00ff00" }
                                    GradientStop { position: 0.50; color: "#00ffff" }
                                    GradientStop { position: 0.67; color: "#0000ff" }
                                    GradientStop { position: 0.83; color: "#ff00ff" }
                                    GradientStop { position: 1.00; color: "#ff0000" }
                                }
                            }

                            BarGrip {
                                value: sheet.hue
                                onPicked: function (v) { sheet.hue = v }
                            }
                        }

                        // Opacity: from clear to the colour itself.
                        Item {
                            id: opacityBar
                            visible: root.alpha
                            width: parent.width
                            height: 14

                            Checkerboard {
                                anchors.fill: parent
                                cell: 7

                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop {
                                            position: 0
                                            color: Qt.rgba(sheet.opaqueColor.r, sheet.opaqueColor.g,
                                                           sheet.opaqueColor.b, 0)
                                        }
                                        GradientStop { position: 1; color: sheet.opaqueColor }
                                    }
                                }
                            }

                            BarGrip {
                                value: sheet.pickOpacity
                                onPicked: function (v) { sheet.pickOpacity = v }
                            }
                        }

                        Row {
                            spacing: 8
                            height: 26

                            Item {
                                width: 26
                                height: 26

                                Checkerboard {
                                    visible: root.alpha
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    cell: 12
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Style.radiusSmall
                                    color: sheet.liveColor
                                    border.color: Style.dialogControlBorder
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.toHtmlHex(sheet.liveColor)
                                      + (root.alpha ? "  " + Math.round(sheet.pickOpacity * 100) + "%" : "")
                                color: Style.textMuted
                                font.pixelSize: Style.fontBody
                            }
                        }

                        Item {
                            width: parent.width
                            height: 28

                            AppButton {
                                anchors.right: selectBtn.left
                                anchors.rightMargin: 8
                                label: qsTr("Cancel")
                                ghost: true
                                minWidth: 88
                                onClicked: sheet.open = false
                            }
                            AppButton {
                                id: selectBtn
                                anchors.right: parent.right
                                label: qsTr("Select")
                                primary: true
                                minWidth: 88
                                onClicked: {
                                    root.commitHex(sheet.liveColor)
                                    sheet.open = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
