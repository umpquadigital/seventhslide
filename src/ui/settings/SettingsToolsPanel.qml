pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Tools page of Settings: the programs SeventhSlide runs to convert and
// download media - ffmpeg, ffprobe and yt-dlp - where each was found and
// which version it is, looked for when the page opens and on Check again;
// then SeventhSlide's dedicated binary of yt-dlp, installed, kept up to
// date and uninstalled here. The line under it says whether it is installed
// and what downloads use without it, then how a check is going or what the
// last one on this page came to.
// Nothing waits for Save: the switch and the buttons act at once.
ScrollPage {
    id: panel

    required property SessionFacade session
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

    onVisibleChanged: {
        if (panel.visible) {
            panel.ytdlp.clearStatus()
            panel.tools.refresh()
        }
    }

    PageHeader {
        width: panel.columnWidth
        title: qsTr("Tools")
        text: qsTr("SeventhSlide uses these programs to convert and download media.")
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
