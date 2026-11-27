pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// The bar along the bottom of the operator window: the open library and
// profile (opens Libraries & Profiles), a synced library's sync in a word or
// two, its icon turning while something is under way (opens Settings ›
// Sync), Live (on, the enabled destinations send; off, only those that
// ignore it), how many fonts the library is missing (opens Settings › Fonts),
// a ring while media imports, exports, converts or downloads (opens the list
// of them), then Layout ▾ (Edit Mode, which panels show, Reset
// Layout), Collaboration (while the open library syncs, a dot on its icon for
// a message not read yet) and Settings.
// An output's state is its card's coloured border, not here.
Rectangle {
    id: bar

    required property SessionFacade session
    required property DockLayout layout
    /// This computer's sync; null when it is off.
    property SyncController sync: null
    /// Libraries & Profiles, the Collaboration window, Settings, or its Sync
    /// page, asked for.
    signal librariesRequested()
    signal collaborationRequested()
    signal settingsRequested()
    signal syncRequested()
    signal fontsRequested()
    /// A press on the bar: the window ends a search being typed elsewhere.
    signal interacted()

    /// Work running or waiting in the background: the ring shows while there
    /// is any.
    readonly property bool importing: !!bar.session && bar.session.media.importing
    readonly property bool exporting: !!bar.session && bar.session.exporter.exporting
    readonly property bool converting: !!bar.session && bar.session.convert.converting
    readonly property bool downloading: !!bar.session && bar.session.download.downloading
    /// A Freeform file or a service's package being read or brought in.
    readonly property bool bringing: !!bar.session && bar.session.freeformImport.busy
    readonly property bool working: bar.importing || bar.exporting || bar.converting
                                    || bar.downloading || bar.bringing
    /// How many kinds of work are going on, for the button's words.
    readonly property int workKinds: (bar.importing ? 1 : 0) + (bar.exporting ? 1 : 0)
                                     + (bar.converting ? 1 : 0) + (bar.downloading ? 1 : 0)
                                     + (bar.bringing ? 1 : 0)
    /// 0 to 1 across all of it: each conversion and download counting once,
    /// and imports, then exports, once between them.
    readonly property real workProgress: {
        if (!bar.working)
            return 0
        const i = bar.importing ? 1 : 0
        const e = bar.exporting ? 1 : 0
        const c = bar.converting ? bar.session.convert.jobs.length : 0
        const d = bar.downloading ? bar.session.download.jobs.length : 0
        const b = bar.bringing ? 1 : 0
        if (i + e + c + d + b === 0)
            return 0
        return (bar.session.media.importProgress * i + bar.session.exporter.progress * e
                + bar.session.convert.progress * c + bar.session.download.progress * d
                + bar.session.freeformImport.progress * b)
               / (i + e + c + d + b)
    }

    /// Families text asks for that the library has no font for, shown in a
    /// stand-in.
    readonly property int missingFonts: bar.session ? bar.session.fonts.missing.length : 0

    /// The open library's and profile's names, for the left button.
    readonly property string libraryName: bar.session ? bar.session.libraries.activeName : ""
    readonly property string profileName: bar.session ? bar.session.profiles.activeName : ""
    /// The open library's sync state (``SyncController.libraryStates``);
    /// empty when it is not synced.
    readonly property var syncState: bar.sync && bar.session
                                     ? (bar.sync.libraryStates[bar.session.libraries.activeId] || ({}))
                                     : ({})
    /// Its sync is under way: sending, downloading, or any file going either
    /// way. The icon turns; it keeps on for a moment after (``syncHold``), so
    /// a short sync does not flicker.
    readonly property bool syncUnderWay: ["starting", "joining", "downloading", "sending"]
                                         .indexOf(bar.syncState.state || "") >= 0
                                         || (bar.syncState.state === "uptodate" && !!bar.syncState.active)
    readonly property bool syncing: bar.syncUnderWay
                                    || (syncHold.running && bar.syncState.state === "uptodate")
    onSyncUnderWayChanged: if (!bar.syncUnderWay) syncHold.restart()
    /// How its sync stands, in a word or two; Settings › Sync says it whole.
    readonly property string syncLabel: {
        switch (bar.syncState.state || "") {
        case "":
            return ""
        case "removed":
            return qsTr("Removed")
        case "problem":
            return qsTr("Sync problem")
        case "full":
            return qsTr("Over size limit")
        case "missing":
            return bar.syncState.missing === 1 ? qsTr("1 file missing")
                                               : qsTr("%1 files missing").arg(bar.syncState.missing)
        case "blocked":
            return qsTr("Sync held up")
        case "elsewhere":
            return bar.syncState.elsewhere === 1 ? qsTr("Waiting for 1 file")
                                                 : qsTr("Waiting for %1 files").arg(bar.syncState.elsewhere)
        case "starting":
            return qsTr("Starting")
        case "joining":
            return qsTr("Getting the library")
        case "paused":
            return qsTr("Paused")
        case "waiting":
            return qsTr("Not connected")
        }
        return bar.syncing ? qsTr("Syncing") : qsTr("Up to date")
    }

    Timer {
        id: syncHold
        interval: 1200
    }

    /// The panels Layout ▾ shows and hides, in the order it lists them.
    readonly property var panelChoices: [
        { id: "service", label: qsTr("Service") },
        { id: "library", label: qsTr("Library") },
        { id: "outputs", label: qsTr("Outputs") },
        { id: "mixer", label: qsTr("Audio Mixer") }
    ]

    implicitHeight: Style.appBarH
    color: Style.appBarBg

    Rectangle {
        width: parent.width
        height: 1
        color: Style.border
    }

    BarButton {
        id: libraryButton
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        icon: "database"
        enabled: !!bar.session && bar.session.ready
        onClicked: bar.librariesRequested()

        Text {
            visible: bar.libraryName.length > 0
            text: bar.libraryName
            color: Style.textSoft
            font.pixelSize: Style.fontSmall
            font.weight: Font.DemiBold
        }
        StatusBadge {
            visible: !!bar.session && bar.session.readOnly
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("View only")
        }
        Text {
            visible: bar.libraryName.length > 0 && bar.profileName.length > 0
            text: "/"
            color: Style.textFaint
            font.pixelSize: Style.fontSmall
        }
        Text {
            visible: bar.profileName.length > 0
            text: bar.profileName
            color: Style.textCaption
            font.pixelSize: Style.fontSmall
        }
    }

    // A synced library's sync: how it stands in a word or two. Opens
    // Settings › Sync, which says it whole.
    BarButton {
        id: syncButton
        objectName: "syncButton"
        anchors.left: libraryButton.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        visible: !!bar.syncState.state
        enabled: !!bar.session && bar.session.ready
        onClicked: bar.syncRequested()

        Image {
            id: syncIcon
            objectName: "syncIcon"
            anchors.verticalCenter: parent.verticalCenter
            width: 13
            height: 13
            source: Icons.url("sync", bar.syncState.trouble ? Style.textWarning
                                      : bar.syncState.state === "uptodate" && !bar.syncing
                                        ? Style.runningDot : Style.textCaption)
            sourceSize: Qt.size(13, 13)
            // Turning while under way; a turn begun is finished.
            RotationAnimation on rotation {
                running: bar.syncing
                from: 0
                to: 360
                duration: 1200
                loops: Animation.Infinite
                alwaysRunToEnd: true
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: bar.syncLabel
            color: bar.syncState.trouble ? Style.textWarning : Style.textCaption
            font.pixelSize: Style.fontSmall
        }
    }

    // Live: its dot lit red while the enabled destinations send. Turning it
    // off while something is sending asks first.
    BarButton {
        id: liveButton
        objectName: "liveButton"
        readonly property bool liveOn: !!bar.session && bar.session.destinations.liveOn
        anchors.left: syncButton.visible ? syncButton.right : libraryButton.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        enabled: !!bar.session && bar.session.ready
        onClicked: {
            const destinations = bar.session.destinations
            if (!liveButton.liveOn)
                destinations.setLiveOn(true)
            else if (destinations.liveOffStops())
                liveOffConfirm.open = true
            else
                destinations.setLiveOn(false)
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: liveButton.liveOn ? Style.liveDot : "transparent"
            border.width: liveButton.liveOn ? 0 : 1
            border.color: Style.textCaption
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Live")
            color: Style.textCaption
            font.pixelSize: Style.fontSmall
        }
    }

    AppDialog {
        id: liveOffConfirm
        objectName: "liveOffConfirm"
        title: qsTr("Turn Live off?")
        acceptLabel: qsTr("Turn Off")
        onAccepted: bar.session.destinations.setLiveOn(false)
    }

    // Text showing in something other than its font. Opens Settings › Fonts,
    // where they are added.
    BarButton {
        id: fontsButton
        objectName: "fontsButton"
        anchors.left: liveButton.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        visible: bar.missingFonts > 0
        enabled: !!bar.session && bar.session.ready
        onClicked: bar.fontsRequested()

        Image {
            anchors.verticalCenter: parent.verticalCenter
            width: 13
            height: 13
            source: Icons.url("textBox", Style.textWarning)
            sourceSize: Qt.size(13, 13)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: bar.missingFonts === 1 ? qsTr("1 font missing")
                                         : qsTr("%1 fonts missing").arg(bar.missingFonts)
            color: Style.textWarning
            font.pixelSize: Style.fontSmall
        }
    }

    // Imports, exports, conversions and downloads: a ring filling across them
    // all, the list above it.
    BarButton {
        id: jobsButton
        objectName: "jobsButton"
        anchors.left: fontsButton.visible ? fontsButton.right : liveButton.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        visible: bar.working
        on: jobsMenu.visible
        onClicked: jobsMenu.visible ? jobsMenu.close() : jobsMenu.openAt()

        ProgressRing {
            anchors.verticalCenter: parent.verticalCenter
            value: bar.workProgress
        }
        // What is going on, without how far: the list says that.
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: {
                if (bar.workKinds > 1)
                    return bar.workKinds === 2 && bar.converting && bar.downloading
                           ? qsTr("Converting and downloading") : qsTr("Working")
                if (bar.importing)
                    return qsTr("Importing")
                if (bar.exporting)
                    return qsTr("Exporting")
                if (bar.bringing)
                    return qsTr("Importing")
                if (bar.converting) {
                    const n = bar.session.convert.jobs.length
                    return n > 1 ? qsTr("Converting %1 files").arg(n) : qsTr("Converting")
                }
                const n = bar.downloading ? bar.session.download.jobs.length : 0
                return n > 1 ? qsTr("Downloading %1 links").arg(n) : qsTr("Downloading")
            }
            color: jobsButton.on ? Style.onInk : Style.textCaption
            font.pixelSize: Style.fontSmall
        }
    }

    AppMenu {
        id: jobsMenu
        objectName: "jobsMenu"
        anchorItem: jobsButton
        minWidth: 340

        // Imports and exports say only how far they are across them all, and
        // have no × here.
        JobRow {
            visible: bar.importing
            modelData: ({ title: qsTr("Importing media"), running: true,
                          progress: bar.importing ? bar.session.media.importProgress : 0 })
            stoppable: false
        }
        JobRow {
            visible: bar.exporting
            modelData: ({ title: qsTr("Exporting"), running: true,
                          progress: bar.exporting ? bar.session.exporter.progress : 0 })
            stoppable: false
        }
        // A Freeform import can be stopped: it leaves nothing behind.
        JobRow {
            objectName: "freeformImportJob"
            visible: bar.bringing
            modelData: ({ id: 0, title: qsTr("Importing a Freeform file"), running: true,
                          progress: bar.bringing ? bar.session.freeformImport.progress : 0 })
            onStopRequested: bar.session.freeformImport.cancel()
        }
        MenuRule {
            visible: (bar.importing || bar.exporting || bar.bringing) && (bar.converting || bar.downloading)
        }
        MenuHeading {
            visible: bar.converting
            text: qsTr("Converting")
        }
        Repeater {
            model: bar.converting ? bar.session.convert.jobs : []
            delegate: JobRow {
                onStopRequested: id => bar.session.convert.cancelConversion(id)
            }
        }
        MenuRule {
            visible: bar.converting && bar.downloading
        }
        MenuHeading {
            visible: bar.downloading
            text: qsTr("Downloading")
        }
        Repeater {
            model: bar.downloading ? bar.session.download.jobs : []
            delegate: JobRow {
                onStopRequested: id => bar.session.download.cancelDownload(id)
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        spacing: 4

        BarButton {
            id: layoutButton
            objectName: "layoutButton"
            icon: "layout"
            on: bar.layout.editMode
            caret: true
            onClicked: layoutMenu.visible ? layoutMenu.close() : layoutMenu.openAt()

            Text {
                text: qsTr("Layout")
                color: layoutButton.on ? Style.onInk : Style.textCaption
                font.pixelSize: Style.fontSmall
            }
        }
        // The open library's shared log, while it syncs.
        BarButton {
            objectName: "collaborationButton"
            icon: "users"
            visible: !!bar.sync && bar.sync.log.available
            dot: !!bar.sync && bar.sync.log.unread
            enabled: !!bar.session && bar.session.ready
            onClicked: bar.collaborationRequested()

            Text {
                text: qsTr("Collaboration")
                color: Style.textCaption
                font.pixelSize: Style.fontSmall
            }
        }
        BarButton {
            icon: "settings"
            enabled: !!bar.session && bar.session.ready
            onClicked: bar.settingsRequested()

            Text {
                text: qsTr("Settings")
                color: Style.textCaption
                font.pixelSize: Style.fontSmall
            }
        }
    }

    AppMenu {
        id: layoutMenu
        objectName: "layoutMenu"
        anchorItem: layoutButton
        alignRight: true

        MenuItemRow {
            checkable: true
            checked: bar.layout.editMode
            label: qsTr("Edit Mode")
            onTriggered: {
                layoutMenu.close()
                bar.layout.editMode = !bar.layout.editMode
            }
        }
        MenuRule {}
        MenuHeading {
            text: qsTr("Panels")
        }
        Repeater {
            model: bar.panelChoices
            delegate: MenuItemRow {
                required property var modelData
                checkable: true
                checked: bar.layout.hidden.indexOf(modelData.id) < 0
                label: modelData.label
                onTriggered: bar.layout.setShown(modelData.id, !checked)
            }
        }
        MenuRule {}
        MenuItemRow {
            icon: "reset"
            label: qsTr("Reset Layout")
            onTriggered: {
                layoutMenu.close()
                bar.layout.reset()
            }
        }
    }

    // A flat button for the bar: an icon, what is declared inside, and a
    // caret when it opens a menu; tinted while ``on``. ``dot`` marks the icon's
    // corner, for something waiting behind it.
    component BarButton: Rectangle {
        id: button
        property string icon: ""
        property bool on: false
        property bool caret: false
        property bool dot: false
        default property alias content: contentRow.data
        signal clicked()

        implicitWidth: contentRow.implicitWidth + 16 + (button.caret ? 11 : 0)
        implicitHeight: Style.btnCompactH
        radius: Style.radiusSmall
        opacity: enabled ? 1 : 0.5
        color: button.on ? Style.onTint
             : (buttonArea.containsMouse ? Style.ghostHover : "transparent")
        border.color: button.on ? Style.onEdge : "transparent"

        Row {
            id: contentRow
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Image {
                visible: button.icon.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: 13
                height: 13
                source: button.icon.length
                        ? Icons.url(button.icon, button.on ? Style.onGlyph : Style.textCaption)
                        : ""
                sourceSize: Qt.size(13, 13)

                // Over the corner, a ring of the button's own colour parting
                // it from the glyph.
                Rectangle {
                    visible: button.dot
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: -3
                    anchors.topMargin: -3
                    width: 9
                    height: 9
                    radius: 4.5
                    color: Style.unreadDot
                    border.width: 1
                    border.color: buttonArea.containsMouse ? Qt.tint(Style.appBarBg, Style.ghostHover)
                                                           : Style.appBarBg
                }
            }
        }
        Image {
            visible: button.caret
            anchors.left: contentRow.right
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: 9
            height: 9
            source: Icons.url("chevronDown", button.on ? Style.onGlyph : Style.textCaption, 2.5)
            sourceSize: Qt.size(9, 9)
        }
        MouseArea {
            id: buttonArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onPressed: bar.interacted()
            onClicked: button.clicked()
        }
    }

    // One piece of work in the list: its name, how far it is (or Waiting), a
    // bar, and × to stop it (``stopRequested``) where it can be.
    component JobRow: Item {
        id: job
        required property var modelData
        property bool stoppable: true
        signal stopRequested(var id)
        readonly property bool running: !!job.modelData.running
        readonly property real value: job.modelData.progress || 0

        width: parent ? parent.width : implicitWidth
        implicitWidth: 332
        implicitHeight: job.running ? 40 : 30

        Text {
            id: title
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: status.left
            anchors.rightMargin: 8
            y: 7
            text: job.modelData.title
            color: job.running ? Style.text : Style.textSoft
            font.pixelSize: Style.fontBody
            elide: Text.ElideMiddle
        }
        Text {
            id: status
            anchors.right: job.stoppable ? stop.left : parent.right
            anchors.rightMargin: job.stoppable ? 4 : 10
            anchors.baseline: title.baseline
            text: job.running ? qsTr("%1%").arg(Math.round(job.value * 100)) : qsTr("Waiting")
            color: job.running ? Style.textCaption : Style.textFaint
            font.pixelSize: Style.fontSmall
            font.features: { "tnum": 1 }
        }
        // A light ×, as a menu's own glyphs are, lit under the pointer.
        Rectangle {
            id: stop
            visible: job.stoppable
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: title.verticalCenter
            width: 20
            height: 20
            radius: Style.radiusSmall
            color: stopArea.containsMouse ? Style.menuHover : "transparent"
            Image {
                anchors.centerIn: parent
                width: 9
                height: 9
                source: Icons.url("close", stopArea.containsMouse ? Style.text : Style.textCaption,
                                  2.5)
                sourceSize: Qt.size(9, 9)
            }
            MouseArea {
                id: stopArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: job.stopRequested(job.modelData.id)
            }
        }
        Rectangle {
            visible: job.running
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 29
            height: 3
            radius: 1.5
            color: Style.controlEdge
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, job.value))
                height: parent.height
                radius: parent.radius
                color: Style.focusEdge
            }
        }
    }
}
