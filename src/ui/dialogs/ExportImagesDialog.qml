import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SeventhSlide

// Export as images, from a library tab: which output's slides, then where
// the ZIP file goes. The tab starts the export with what was chosen
// (``chosen``) and hears how it went from session.exporter.
AppDialog {
    id: root

    required property SessionFacade session
    /// What the ZIP file is named for (a song's title, "Songs").
    property string fileTitle: ""
    /// The output whose slides are exported; "" for every output. Kept from
    /// one export to the next.
    property string outputId: ""

    /// Export the slides of output ``outputId`` (empty: every output) to
    /// ``file``.
    signal chosen(string outputId, string file)

    title: qsTr("Export as images")
    acceptLabel: qsTr("Export…")
    danger: false

    /// Ask, saying what will be exported (``what``) and naming the file for
    /// ``fileTitle``.
    function openFor(what, fileTitle) {
        root.fileTitle = fileTitle
        root.ask(what)
    }

    onAccepted: {
        saveDialog.selectedFile = root.session.exporter.suggestedFile(root.fileTitle)
        saveDialog.open()
    }

    FormRow {
        Layout.fillWidth: true
        label: qsTr("Slides as shown on")
        hint: qsTr("Each slide is a PNG image the size of its output.")
        OutputPickField {
            outputs: root.session ? root.session.outputs : null
            value: root.outputId
            onPicked: function (value) { root.outputId = value }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Export as images")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ZIP files (*.zip)")]
        defaultSuffix: "zip"
        onAccepted: root.chosen(root.outputId, saveDialog.selectedFile.toString())
    }
}
