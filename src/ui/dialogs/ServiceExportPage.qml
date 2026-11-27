pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SeventhSlide

// Export page of Service Settings: the service's slides as PNG images in a
// ZIP file, for one output or every output; or the whole service as a
// package another library imports. ServiceExportState runs them; this page
// asks where the file goes.
ScrollPage {
    id: page

    required property ServiceExportState dialog

    PageHeader {
        width: page.columnWidth
        title: qsTr("Export")
        text: qsTr("Save this service’s slides as images, or save the whole service as a package "
                   + "for another library.")
    }

    GroupHeading {
        text: qsTr("Images")
    }
    Text {
        width: page.columnWidth
        bottomPadding: 8
        text: qsTr("A PNG file per slide, the size of its output, in one ZIP file. With every "
                   + "output, each has a folder of its own.")
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }
    GroupList {
        width: page.columnWidth

        SettingRow {
            label: qsTr("Slides as shown on")
            OutputPickField {
                implicitWidth: Style.fieldLongW
                outputs: page.dialog.session ? page.dialog.session.outputs : null
                value: page.dialog.outputId
                onPicked: function (value) { page.dialog.outputId = value }
            }
        }
    }

    // The action, and how the last export went, under the group.
    Item {
        width: page.columnWidth
        implicitHeight: actionRow.implicitHeight + 14

        RowLayout {
            id: actionRow
            y: 14
            width: parent.width
            spacing: 10

            AppButton {
                objectName: "serviceExportImages"
                size: "control"
                primary: true
                label: qsTr("Export…")
                enabled: !page.dialog.running
                onClicked: {
                    saveDialog.selectedFile = page.dialog.suggestedFile()
                    saveDialog.open()
                }
            }

            ProgressRing {
                visible: page.dialog.running && page.dialog.runningKind === "images"
                value: page.dialog.session ? page.dialog.session.exporter.progress : 0
            }

            Text {
                Layout.fillWidth: true
                text: page.dialog.running ? (page.dialog.runningKind === "images" ? qsTr("Exporting…") : "")
                                          : (page.dialog.outcomeKind === "images" ? page.dialog.outcomeText : "")
                color: page.dialog.outcome === "failed" ? Style.textWarning : Style.textCaption
                font.pixelSize: Style.fontBody
                wrapMode: Text.Wrap
            }
        }
    }

    GroupHeading {
        text: qsTr("Package")
    }
    Text {
        width: page.columnWidth
        text: qsTr("The whole service in one ZIP file: its rundown, its songs, Bible passages and "
                   + "Freeform items as a Freeform file with the pictures, videos and fonts they "
                   + "use, and its pictures, videos and audio. Songs and Bible passages go as "
                   + "Freeform, as they show with this window’s themes. Web pages and live "
                   + "sources go as their addresses, a stream’s key with it. Fonts go with "
                   + "the file: share it only where their licences allow.")
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }

    Item {
        width: page.columnWidth
        implicitHeight: packageRow.implicitHeight + 14

        RowLayout {
            id: packageRow
            y: 14
            width: parent.width
            spacing: 10

            AppButton {
                objectName: "serviceExportPackage"
                size: "control"
                primary: true
                label: qsTr("Export…")
                enabled: !page.dialog.running
                onClicked: {
                    packageDialog.selectedFile = page.dialog.suggestedPackageFile()
                    packageDialog.open()
                }
            }

            ProgressRing {
                visible: page.dialog.running && page.dialog.runningKind === "package"
                value: page.dialog.session ? page.dialog.session.exporter.progress : 0
            }

            Text {
                Layout.fillWidth: true
                text: page.dialog.running ? (page.dialog.runningKind === "package" ? qsTr("Exporting…") : "")
                                          : (page.dialog.outcomeKind === "package" ? page.dialog.outcomeText : "")
                color: page.dialog.outcome === "failed" ? Style.textWarning : Style.textCaption
                font.pixelSize: Style.fontBody
                wrapMode: Text.Wrap
            }
        }
    }

    FileDialog {
        id: packageDialog
        title: qsTr("Export the service as a package")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ZIP files (*.zip)")]
        defaultSuffix: "zip"
        onAccepted: page.dialog.exportPackageTo(packageDialog.selectedFile.toString())
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Export the service as images")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ZIP files (*.zip)")]
        defaultSuffix: "zip"
        onAccepted: page.dialog.exportTo(saveDialog.selectedFile.toString())
    }
}
