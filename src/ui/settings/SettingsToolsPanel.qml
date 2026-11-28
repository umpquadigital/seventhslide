pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeventhSlide

// Updates & Tools page of Settings. First SeventhSlide itself: the version
// running, Check for updates (Check Now, and when the last check was), Query
// automatically and the release channel (held while something is under
// way); then, apart from them, a newer release on GitHub when there is one
// (What's New and Download, then Install, or its page for a build that was
// not packaged, over its notes in a well). An install that closes the app
// is confirmed first (``installAsked``); one beside it (Linux) ends with
// Restart Now. Then
// the programs SeventhSlide runs to convert and download media - ffmpeg,
// ffprobe and yt-dlp - where each was found and which version it is, looked
// for when the page opens and on Check again; then SeventhSlide's dedicated
// binary of yt-dlp, installed, kept up to date and uninstalled here. The
// line under it says whether it is installed and what downloads use without
// it, then how a check is going or what the last one on this page came to.
// Nothing waits for Save: the switches and the buttons act at once.
ScrollPage {
    id: panel

    required property SessionFacade session
    readonly property AppUpdater updates: panel.session.updates
    /// Install asked for, where installing closes SeventhSlide: the dialog
    /// confirms, then calls ``updates.install()``.
    signal installAsked()
    readonly property ToolsController tools: panel.session.tools
    readonly property YtdlpUpdater ytdlp: panel.tools.ytdlp
    /// A yt-dlp is found somewhere, the app's own or another.
    readonly property bool ytdlpFound: panel.tools.tools.some(t => t.name === "yt-dlp" && t.found)
    /// Whether the app's own yt-dlp is installed, and if not, what downloads
    /// use instead.
    readonly property string ytdlpState: panel.ytdlp.installed
        ? qsTr("Installed in the tools folder.")
        : panel.ytdlpFound
          ? qsTr("Not installed. Downloads use the yt-dlp listed above, which "
                 + "SeventhSlide does not update.")
          : qsTr("Not installed. Downloading needs it.")
    /// Under Check for updates: what a check is doing or came to, else when
    /// the last one was.
    readonly property string updateState: {
        if (panel.updates.status.length)
            return panel.updates.status
        const last = panel.updates.lastChecked
        return !isNaN(last.getTime())
            ? qsTr("Last checked %1.").arg(last.toLocaleString(Qt.locale(), Locale.ShortFormat))
            : qsTr("Not checked yet.")
    }

    onVisibleChanged: {
        if (panel.visible) {
            panel.updates.clearStatus()
            panel.ytdlp.clearStatus()
            panel.tools.refresh()
        }
    }

    PageHeader {
        width: panel.columnWidth
        title: qsTr("Updates & Tools")
        text: qsTr("Keep SeventhSlide up to date, and see the programs it uses to convert and "
                   + "download media.")
    }

    GroupHeading {
        text: qsTr("SeventhSlide")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Version")
                hint: panel.updates.checks
                      ? ""
                      : qsTr("This copy of SeventhSlide gets its updates from wherever it was "
                             + "installed from.")
                Text {
                    text: panel.updates.version
                    color: Style.textStrong
                    font.pixelSize: Style.fontBody
                }
            }
        }
        GroupRow {
            visible: panel.updates.checks
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Check for updates")
                hint: panel.updateState
                hintInk: panel.updates.failed && !panel.updates.busy ? Style.textWarning
                                                                     : Style.textHint
                AppButton {
                    size: "control"
                    label: qsTr("Check Now")
                    enabled: !panel.updates.busy
                    onClicked: panel.updates.checkNow()
                }
            }
        }
        SettingRow {
            visible: panel.updates.checks
            label: qsTr("Query automatically")
            ToggleSwitch {
                checked: panel.updates.autoCheck
                onToggled: panel.updates.autoCheck = !panel.updates.autoCheck
            }
        }
        SettingRow {
            visible: panel.updates.checks
            label: qsTr("Release channel")
            SegmentedControl {
                // The release being downloaded or installed stays the one
                // it was.
                enabled: !panel.updates.busy
                options: [qsTr("Stable"), qsTr("Beta")]
                current: panel.updates.channel === "beta" ? 1 : 0
                onChosen: index => panel.updates.channel = index === 1 ? "beta" : "stable"
            }
        }
    }

    // A newer release, in a group of its own under the settings.
    Item {
        width: 1
        height: 12
        visible: panel.updates.available
    }
    GroupList {
        width: panel.columnWidth
        visible: panel.updates.available

        // A newer release: what can be done with it, then its notes across
        // the row's whole width.
        GroupRow {
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                Layout.bottomMargin: 6
                spacing: 6
                FormRow {
                    Layout.fillWidth: true
                    label: panel.updates.installed
                           ? qsTr("SeventhSlide %1 is installed").arg(panel.updates.latestVersion)
                           : qsTr("SeventhSlide %1 is available").arg(panel.updates.latestVersion)
                    hint: panel.updates.installed ? qsTr("Restart SeventhSlide to use it.") : ""
                    Row {
                        spacing: 6
                        ProgressRing {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: panel.updates.downloading
                            value: panel.updates.progress
                        }
                        AppButton {
                            visible: panel.updates.canDownload
                            size: "control"
                            label: qsTr("What’s New")
                            onClicked: Qt.openUrlExternally(panel.updates.releaseUrl)
                        }
                        // A build that was not packaged gets the release's
                        // page, where each package is.
                        AppButton {
                            visible: !panel.updates.canDownload
                            size: "control"
                            primary: true
                            label: qsTr("Open Release Page")
                            onClicked: Qt.openUrlExternally(panel.updates.releaseUrl)
                        }
                        AppButton {
                            visible: panel.updates.canDownload && panel.updates.downloading
                            size: "control"
                            label: qsTr("Cancel")
                            onClicked: panel.updates.cancelDownload()
                        }
                        AppButton {
                            visible: panel.updates.canDownload && !panel.updates.downloading
                                     && !panel.updates.installed
                            size: "control"
                            primary: true
                            label: panel.updates.downloaded ? qsTr("Install") : qsTr("Download")
                            enabled: !panel.updates.busy
                            onClicked: {
                                if (!panel.updates.downloaded)
                                    panel.updates.download()
                                else if (panel.updates.installCloses)
                                    panel.installAsked()
                                else
                                    panel.updates.install()
                            }
                        }
                        AppButton {
                            visible: panel.updates.installed
                            size: "control"
                            primary: true
                            label: qsTr("Restart Now")
                            onClicked: panel.updates.restart()
                        }
                    }
                }
                // The notes, as GitHub's page lays them out: headings,
                // bulleted and numbered items, paragraphs. The well keeps
                // one height; notes longer than it scroll.
                ListWell {
                    Layout.fillWidth: true
                    // One height, whatever the notes: longer ones scroll.
                    Layout.preferredHeight: 140
                    visible: panel.updates.notes.length > 0

                    WellPane {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        showHeader: false

                        Flickable {
                            id: notesView
                            objectName: "releaseNotes"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.rightMargin: Style.scrollGap
                            topMargin: 10
                            bottomMargin: 12
                            leftMargin: 14
                            rightMargin: Style.scrollGutter(notesView)
                            contentWidth: notesView.width - notesView.leftMargin - notesView.rightMargin
                            contentHeight: notesColumn.implicitHeight
                            clip: true
                            interactive: notesView.contentHeight + notesView.topMargin
                                         + notesView.bottomMargin > notesView.height
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: AppScrollBar {}

                            Column {
                                id: notesColumn
                                width: notesView.contentWidth

                                Repeater {
                                    model: panel.updates.notes
                                    delegate: Item {
                                        id: block
                                        required property var modelData
                                        required property int index
                                        readonly property string kind: block.modelData.kind
                                        readonly property bool item: block.kind === "item"
                                        // A heading after other text stands off it; an
                                        // item follows the one before closely.
                                        readonly property int gap: block.index === 0 ? 0
                                            : block.kind === "heading" ? 12
                                            : block.item ? 3 : 8

                                        width: notesColumn.width
                                        height: body.implicitHeight + block.gap

                                        Text {
                                            id: marker
                                            visible: block.item
                                            x: block.modelData.depth * 16
                                            y: block.gap
                                            // Room for a number as long as "10.".
                                            width: Math.max(16, marker.implicitWidth + 6)
                                            text: block.modelData.marker
                                            color: Style.textCaption
                                            font.pixelSize: Style.fontBody
                                        }
                                        Text {
                                            id: body
                                            x: block.item ? marker.x + marker.width : 0
                                            y: block.gap
                                            width: block.width - body.x
                                            text: block.modelData.text
                                            textFormat: Text.PlainText
                                            wrapMode: Text.Wrap
                                            color: block.kind === "heading" ? Style.textStrong
                                                 : block.kind === "code" ? Style.textCaption : Style.text
                                            font.family: block.kind === "code" ? Platform.monoFamily
                                                                               : Application.font.family
                                            font.pixelSize: block.kind === "code" ? Style.fontSmall
                                                                                  : Style.fontBody
                                            font.weight: block.kind === "heading" ? Font.DemiBold
                                                                                  : Font.Normal
                                            lineHeight: 1.15
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    GroupHeading {
        text: qsTr("Programs")
        tools: AppButton {
            size: "control"
            label: qsTr("Check again")
            enabled: !panel.tools.probing
            onClicked: panel.tools.refresh()
        }
    }
    GroupList {
        width: panel.columnWidth

        Repeater {
            model: panel.tools.tools
            delegate: GroupRow {
                id: toolRow
                required property var modelData
                readonly property bool found: !!toolRow.modelData.found
                readonly property string error: toolRow.modelData.error || ""
                readonly property string version: toolRow.modelData.version || ""
                readonly property bool probing: !!toolRow.modelData.probing
                readonly property bool good: toolRow.found && !toolRow.error.length
                                             && !toolRow.probing

                // The light before the name; the name fills, so the version
                // and path sit at the row's right edge.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    Layout.bottomMargin: 2
                    spacing: 3
                    RowLayout {
                        spacing: 8
                        Rectangle {
                            implicitWidth: 8
                            implicitHeight: 8
                            radius: 4
                            color: toolRow.error.length ? Style.textDanger
                                 : toolRow.good ? Style.runningDot
                                 : Style.textFaint
                        }
                        Text {
                            Layout.fillWidth: true
                            text: toolRow.modelData.name
                            color: Style.textStrong
                            font.pixelSize: Style.fontBody
                        }
                    }
                }
                ColumnLayout {
                    Layout.maximumWidth: 300
                    Layout.topMargin: 2
                    Layout.bottomMargin: 2
                    spacing: 3
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                        text: !toolRow.found ? qsTr("Not found")
                            : toolRow.error.length ? qsTr("Found, but it would not run")
                            : toolRow.probing ? qsTr("Checking…")
                            : toolRow.version.length ? toolRow.version : qsTr("Found")
                        color: toolRow.good ? Style.textStrong : Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: toolRow.found
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                        text: toolRow.error.length ? toolRow.error : toolRow.modelData.path
                        color: toolRow.error.length ? Style.textDanger : Style.textCaption
                        font.pixelSize: Style.fontSmall
                    }
                }
            }
        }
    }

    Text {
        width: panel.columnWidth
        topPadding: 10
        text: qsTr("A program that is not found can be installed, or put in %1.")
              .arg(panel.tools.toolsDir)
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }

    GroupHeading {
        text: qsTr("yt-dlp")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Dedicated binary")
                // A check's progress alone; else whether it is installed, then
                // the last outcome on a line of its own.
                hint: panel.ytdlp.busy ? panel.ytdlp.status
                    : panel.ytdlp.status.length ? panel.ytdlpState + "\n" + panel.ytdlp.status
                    : panel.ytdlpState
                hintInk: panel.ytdlp.failed && !panel.ytdlp.busy ? Style.textWarning
                                                                 : Style.textHint
                Row {
                    spacing: 6
                    // Not while a version is read: Windows will not delete a
                    // program that is running.
                    AppButton {
                        visible: panel.ytdlp.installed
                        size: "control"
                        label: qsTr("Uninstall")
                        enabled: !panel.ytdlp.busy && !panel.tools.probing
                        // Its refusal, if any, shows in the hint (status).
                        onClicked: panel.ytdlp.uninstall()
                    }
                    AppButton {
                        size: "control"
                        label: panel.ytdlp.installed ? qsTr("Check for Updates") : qsTr("Install")
                        enabled: !panel.ytdlp.busy
                        onClicked: panel.ytdlp.update()
                    }
                }
            }
        }
        SettingRow {
            label: qsTr("Update automatically")
            ToggleSwitch {
                checked: panel.ytdlp.autoUpdate
                onToggled: panel.ytdlp.autoUpdate = !panel.ytdlp.autoUpdate
            }
        }
    }
}
