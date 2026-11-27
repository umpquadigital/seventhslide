pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// The menu a destination's picker (DestinationPicker) opens to pick its
// target: a screen, a virtual camera, a Blackmagic device or mode, or a
// display's or window's sound device. The current one is checked. A target
// taken closes the menu; one refused leaves it open, the reason shown, to
// pick another.
AppMenu {
    id: menu

    /// The state this shows. Every `menu.dialog.` below is its interface to it.
    required property OutputDestinationsState dialog
    /// Which targets it lists: display, vcam, bmd, bmdmode or sound.
    property string kind: "display"
    /// The destination it picks for.
    property string destId: ""
    /// The destination's current target, which is checked.
    property string currentKey: ""

    /// A pick's result: the menu closes once it was ``taken``. A row calls
    /// this with what its setter returned, ``menu.take`` read before the
    /// setter runs: a pick taken reloads the lists, and the row that made it
    /// is gone by the time the setter returns.
    function take(taken) {
        if (taken)
            menu.close()
    }

    // Past this the rows scroll.
    maxHeight: 260

    Repeater {
        id: displayRepeater
        model: menu.kind === "display" && menu.dialog.session
               ? menu.dialog.session.destinations.displays : null
        delegate: MenuItemRow {
            id: displayRow
            required property var model
            checkable: true
            checked: displayRow.model.key === menu.currentKey
            label: menu.dialog.destMenuLabel(displayRow.model.label, displayRow.model.assignedOutput)
            onTriggered: menu.take(menu.dialog.setDestinationScreen(menu.destId, displayRow.model.key))
        }
    }
    Repeater {
        model: menu.kind === "vcam" ? menu.dialog.cameras : []
        delegate: MenuItemRow {
            id: cameraRow
            required property var modelData
            checkable: true
            checked: cameraRow.modelData.key === menu.currentKey
            label: menu.dialog.destMenuLabel(cameraRow.modelData.label && cameraRow.modelData.label.length
                                             ? cameraRow.modelData.label : cameraRow.modelData.key,
                                             cameraRow.modelData.assignedOutput)
            onTriggered: menu.take(menu.dialog.setDestinationCamera(menu.destId, cameraRow.modelData.key))
        }
    }
    Repeater {
        model: menu.kind === "bmd" ? menu.dialog.bmdDevices : []
        delegate: MenuItemRow {
            id: bmdDeviceRow
            required property var modelData
            checkable: true
            checked: bmdDeviceRow.modelData.key === menu.currentKey
            label: {
                const d = bmdDeviceRow.modelData
                let t = d.label && d.label.length ? d.label : d.key
                if (d.assignedOutput && d.assignedOutput.length
                        && d.assignedOutput !== menu.dialog.outputName)
                    t += " \u2014 " + d.assignedOutput
                return t
            }
            onTriggered: menu.take(menu.dialog.setDestinationBmdDevice(menu.destId, bmdDeviceRow.modelData.key))
        }
    }
    Repeater {
        model: menu.kind === "bmdmode" ? menu.dialog.bmdModes : []
        delegate: MenuItemRow {
            id: bmdModeRow
            required property var modelData
            checkable: true
            checked: bmdModeRow.modelData.key === menu.currentKey
            label: bmdModeRow.modelData.name
            onTriggered: menu.take(menu.dialog.setDestinationBmdMode(menu.destId, bmdModeRow.modelData.key))
        }
    }
    Repeater {
        model: menu.kind === "sound" ? menu.dialog.soundDevices : []
        delegate: MenuItemRow {
            id: soundRow
            required property var modelData
            checkable: true
            checked: soundRow.modelData.key === menu.currentKey
            label: soundRow.modelData.key.length ? soundRow.modelData.label : qsTr("None")
            onTriggered: menu.take(menu.dialog.setDestinationAudioDevice(menu.destId, soundRow.modelData.key))
        }
    }
    Text {
        width: parent ? parent.width : implicitWidth
        height: 28
        visible: (menu.kind === "bmd" && menu.dialog.bmdDevices.length === 0)
                 || (menu.kind === "bmdmode" && menu.dialog.bmdModes.length === 0)
        text: menu.kind === "bmdmode" ? qsTr("Choose a device first") : qsTr("No Blackmagic device found")
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    Text {
        width: parent ? parent.width : implicitWidth
        height: 28
        visible: menu.kind === "vcam" && menu.dialog.cameras.length === 0
        text: qsTr("No device found")
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    Text {
        width: parent ? parent.width : implicitWidth
        height: 28
        visible: menu.kind === "display" && displayRepeater.count === 0
        text: qsTr("No monitor found")
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
