pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// The Animations tab: an effect for the selected object - a new
// one, or the one picked in the Build pane - and when and how long it plays.
// The slide's animations are listed, ordered and previewed in the sidebar's
// Build tab.
Row {
    id: tab
    required property SlideEditState dialog
    readonly property var current: tab.dialog.currentAnimation
    readonly property var effects: [
        { value: "appear", label: qsTr("Appear"), kind: "entrance" },
        { value: "fade", label: qsTr("Fade in"), kind: "entrance" },
        { value: "fly_in", label: qsTr("Fly in"), kind: "entrance" },
        { value: "wipe", label: qsTr("Wipe in"), kind: "entrance" },
        { value: "zoom", label: qsTr("Zoom in"), kind: "entrance" },
        { value: "float", label: qsTr("Float in"), kind: "entrance" },
        { value: "pulse", label: qsTr("Pulse"), kind: "emphasis" },
        { value: "grow", label: qsTr("Grow"), kind: "emphasis" },
        { value: "disappear", label: qsTr("Disappear"), kind: "exit" },
        { value: "fade_out", label: qsTr("Fade out"), kind: "exit" },
        { value: "fly_out", label: qsTr("Fly out"), kind: "exit" },
        { value: "wipe_out", label: qsTr("Wipe out"), kind: "exit" },
        { value: "zoom_out", label: qsTr("Zoom out"), kind: "exit" },
        { value: "float_out", label: qsTr("Float out"), kind: "exit" }
    ]
    readonly property var kinds: [
        { value: "entrance", label: qsTr("In") },
        { value: "emphasis", label: qsTr("Show") },
        { value: "exit", label: qsTr("Out") }
    ]
    // The kind of effect the gallery shows: the current animation's, until
    // another is switched to. All fourteen at once do not fit the ribbon.
    property string kindShown: ""
    readonly property string kind: tab.kindShown.length ? tab.kindShown
                                   : tab.current ? String(tab.current.kind) : "entrance"
    /// Where the current animation is in the slide's order; -1 for none.
    readonly property int currentIndex: {
        const list = tab.dialog.animationList || []
        for (let i = 0; tab.current && i < list.length; ++i) {
            if (list[i].objectId === tab.current.objectId && list[i].order === tab.current.order)
                return i
        }
        return -1
    }
    readonly property string currentKey: tab.current ? tab.current.objectId + "#" + tab.current.order : ""
    onCurrentKeyChanged: tab.kindShown = ""
    readonly property var triggers: [
        { value: "on_click", label: qsTr("On click") },
        { value: "with_previous", label: qsTr("With previous") },
        { value: "after_previous", label: qsTr("After previous") }
    ]
    readonly property var directions: [
        { value: "left", label: qsTr("Left") },
        { value: "right", label: qsTr("Right") },
        { value: "up", label: qsTr("Top") },
        { value: "down", label: qsTr("Bottom") }
    ]
    readonly property bool sided: !!tab.current
                                  && ["fly_in", "fly_out", "wipe", "wipe_out"].indexOf(String(tab.current.effect)) >= 0
    spacing: 0
    height: parent ? parent.height : implicitHeight

    RibbonGroup {
        first: true
        height: parent.height
        enabled: tab.dialog.hasSel
        tall: [
            Column {
                id: kindSwitch
                objectName: "ribbonAnimationKind"
                spacing: 2
                height: Style.ribbonTallH
                Repeater {
                    model: tab.kinds
                    delegate: Rectangle {
                        id: kindRow
                        required property var modelData
                        readonly property bool on: tab.kind === String(kindRow.modelData.value)
                        signal clicked()
                        objectName: "animationKind_" + String(kindRow.modelData.value)
                        width: 44
                        height: (kindSwitch.height - 2 * kindSwitch.spacing) / 3
                        radius: Style.radiusSmall
                        color: kindRow.on ? Style.panelRowSel
                                          : (kindArea.containsMouse ? Style.btnGhostHover : "transparent")
                        border.color: kindRow.on ? Style.selectedBar : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: String(kindRow.modelData.label)
                            color: kindRow.on ? Style.textStrong : Style.text
                            font.pixelSize: Style.fontSmall
                        }
                        MouseArea {
                            id: kindArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: kindRow.clicked()
                        }
                        onClicked: tab.kindShown = String(kindRow.modelData.value)
                    }
                }
            },
            RibbonGallery {
                objectName: "ribbonAnimationEffect"
                sketch: "effect"
                cardWidth: 70
                options: tab.effects.filter(e => e.kind === tab.kind)
                value: tab.current ? String(tab.current.effect) : ""
                onPicked: function (v) { tab.dialog.pickEffect(v) }
            }
        ]
    }
    RibbonGroup {
        height: parent.height
        enabled: tab.dialog.hasSel
        row1: [
            RibbonDropdown {
                objectName: "ribbonAnimationStart"
                label: qsTr("Start")
                fieldWidth: 120
                enabled: !!tab.current
                options: tab.triggers
                value: tab.current ? String(tab.current.trigger) : "on_click"
                onPicked: function (v) { tab.dialog.setAnimationField("trigger", v) }
            },
            RibbonDropdown {
                objectName: "ribbonAnimationDirection"
                label: qsTr("From")
                fieldWidth: 100
                enabled: tab.sided
                options: tab.directions
                value: tab.current ? String(tab.current.direction) : "left"
                onPicked: function (v) { tab.dialog.setAnimationField("direction", v) }
            }
        ]
        row2: [
            RibbonField {
                objectName: "ribbonAnimationMs"
                label: qsTr("Duration")
                unit: "s"
                displayScale: 0.001
                decimals: 2
                fieldWidth: 84
                step: 50
                minValue: 0
                maxValue: 60000
                enabled: !!tab.current
                value: tab.current ? Number(tab.current.durationMs) : 500
                onCommit: function (n) { tab.dialog.setAnimationField("duration_ms", n) }
            },
            RibbonField {
                objectName: "ribbonAnimationDelay"
                label: qsTr("Delay")
                unit: "s"
                displayScale: 0.001
                decimals: 2
                fieldWidth: 84
                step: 50
                minValue: 0
                maxValue: 60000
                enabled: !!tab.current
                value: tab.current ? Number(tab.current.delayMs) : 0
                onCommit: function (n) { tab.dialog.setAnimationField("delay_ms", n) }
            }
        ]
    }
    RibbonGroup {
        grow: true
        height: parent.height
        row1: [
            RibbonButton {
                objectName: "ribbonAnimationUp"
                icon: "up"
                enabled: tab.currentIndex > 0
                onClicked: tab.dialog.buildDrag.moved(tab.currentIndex, tab.currentIndex - 1)
            },
            RibbonButton {
                objectName: "ribbonAnimationDown"
                icon: "down"
                enabled: tab.currentIndex >= 0 && tab.currentIndex < (tab.dialog.animationList || []).length - 1
                onClicked: tab.dialog.buildDrag.moved(tab.currentIndex, tab.currentIndex + 1)
            },
            RibbonButton {
                objectName: "ribbonAnimationAdd"
                icon: "plus"
                label: qsTr("Add")
                enabled: tab.dialog.hasSel && !!tab.current
                onClicked: tab.dialog.addAnimation(String(tab.current.effect))
            },
            RibbonButton {
                objectName: "ribbonAnimationRemove"
                icon: "trash"
                label: qsTr("Remove")
                enabled: !!tab.current
                onClicked: {
                    tab.dialog.report(tab.dialog.edit.animations.remove(tab.current.objectId, tab.current.order))
                    tab.dialog.selectedAnimation = null
                }
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonAnimationPreview"
                icon: "play"
                label: qsTr("Preview")
                enabled: (tab.dialog.animationList || []).length > 0
                on: !!(tab.dialog.edit && tab.dialog.edit.animations.playing)
                onClicked: tab.dialog.edit.animations.playing ? tab.dialog.edit.animations.stop()
                                                              : tab.dialog.edit.animations.play()
            }
        ]
    }
}
