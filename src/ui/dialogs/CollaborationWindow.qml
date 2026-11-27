pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import SeventhSlide

// The Collaboration window: the open library's shared log, its messages and
// notes of changes (CollaborationPanel), in a WindowShell with no rail and a
// lone Close. The bottom bar opens it while the open library syncs; it closes
// itself when the library stops syncing. It opens at the newest. What is being
// written stays for the next time it opens, while the same library is open.
Item {
    id: root
    visible: open
    z: Style.sheetZ
    focus: open

    property SyncController sync: null
    property bool open: false
    /// A message could not be sent: why.
    signal failed(string message)

    readonly property bool available: !!root.sync && root.sync.log.available
    onAvailableChanged: if (!root.available) root.close()
    // What it shows is read; the bar's dot goes.
    onOpenChanged: if (root.sync) root.sync.log.setShowing(root.open)

    function show() {
        if (!root.available)
            return
        panel.toNewest()
        root.open = true
        root.forceActiveFocus()
        panel.focusField()
    }

    function close() {
        root.open = false
    }

    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    Keys.onEscapePressed: function (event) {
        root.close()
        event.accepted = true
    }

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: root.close()
    }

    WindowShell {
        anchors.centerIn: parent
        width: Math.min(Style.collaborationWindowW, parent.width - 40)
        height: Math.min(Style.collaborationWindowH, parent.height - 40)
        title: qsTr("Collaboration")
        closeOnly: true
        onCanceled: root.close()

        CollaborationPanel {
            id: panel
            anchors.fill: parent
            sync: root.sync
            onFailed: message => root.failed(message)
        }
    }
}
