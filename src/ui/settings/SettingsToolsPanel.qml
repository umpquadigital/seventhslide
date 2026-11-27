pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Tools page of Settings: the programs SeventhSlide runs to convert and
// download media - ffmpeg, ffprobe and yt-dlp - where each was found and
// which version it is, looked for when the page opens and on Check again;
// then SeventhSlide's dedicated binary of yt-dlp, installed and kept up to
// date here.
// Nothing waits for Save: the switch and the buttons act at once.
ScrollPage {
    id: panel

    required property SessionFacade session
    readonly property ToolsController tools: panel.session.tools
    readonly property YtdlpUpdater ytdlp: panel.tools.ytdlp

    onVisibleChanged: {
        if (panel.visible)
            panel.tools.refresh()
    }

    PageHeader {
        width: panel.columnWidth
        title: qsTr("Tools")
        text: qsTr("SeventhSlide converts media and downloads it with these programs. "
                   + "Each is looked for in SeventhSlide’s own tools folder, beside "
                   + "SeventhSlide, and then where this computer keeps its programs.")
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
                AppButton {
                    label: panel.ytdlp.installed ? qsTr("Check for Updates") : qsTr("Install")
                    enabled: !panel.ytdlp.busy
                    onClicked: panel.ytdlp.update()
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
