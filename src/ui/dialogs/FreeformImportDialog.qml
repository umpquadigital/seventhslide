pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import SeventhSlide

// Import a Freeform file (.ssff, .ssfb) or a service's package (.zip):
// ``start`` asks which file, session.freeformImport reads it, and this shows
// what it holds. Each item to bring, ticked to begin with; where each of the
// file's outputs goes here, among the outputs of exactly its shape, or Don't
// import; the folder the items go in; and for a file with a service, whether
// to add the service and its items. Import brings what was chosen; Cancel
// brings nothing.
AppDialog {
    id: root

    required property SessionFacade session

    /// ``fileRead``'s summary of the file read.
    property var summary: ({})
    /// The ``index`` of each item unticked.
    property var unchecked: ({})
    /// Each file output's target here, by place: an output id, or "".
    property var mapping: []
    property string folderName: ""
    property bool addService: true
    property bool addItems: true
    /// The read or import this window waits for.
    property int readRequest: -1
    property int importRequest: -1

    readonly property var items: root.summary.items ?? []
    readonly property var importable: root.items.filter(i => !i.refusal)
    readonly property var chosen: root.importable.filter(i => !root.unchecked[i.index]).map(i => i.index)
    readonly property var fileOutputs: root.summary.outputs ?? []
    readonly property var service: root.summary.service ?? null
    readonly property bool anyOutput: root.mapping.some(id => id.length > 0)

    /// What an import came to: ``importFinished``' result, when it worked.
    signal imported(var result)
    /// Why a read or an import did not happen, for the window that opened this.
    signal failed(string why)

    readonly property bool busy: !!root.session && root.session.freeformImport.busy
    /// Items are chosen but no output for them: what Import waits for.
    readonly property bool needsOutput: root.chosen.length > 0 && !root.anyOutput

    /// Ask which file to import, then read it. Not while another is being
    /// read or brought in: the bar shows that one, and can stop it.
    function start() {
        if (!root.busy)
            fileDialog.open()
    }

    function read(url) {
        if (!root.busy)
            root.readRequest = root.session.freeformImport.readFile(url)
    }

    function show(summary) {
        root.summary = summary
        root.unchecked = ({})
        root.mapping = (summary.outputs ?? []).map(o => o.suggested)
        root.folderName = summary.defaultFolderName ?? ""
        root.addService = !!summary.service
        root.addItems = true
        root.open = true
    }

    function toggle(index) {
        const next = Object.assign({}, root.unchecked)
        if (next[index])
            delete next[index]
        else
            next[index] = true
        root.unchecked = next
    }

    function setAll(on) {
        const next = {}
        if (!on)
            root.importable.forEach(i => { next[i.index] = true })
        root.unchecked = next
    }

    /// Output ``place`` of the file goes to ``id`` here ("": not imported).
    /// An output here takes one: another row that had it lets it go.
    function choose(place, id) {
        const next = root.mapping.slice()
        if (id.length > 0) {
            for (let i = 0; i < next.length; ++i) {
                if (i !== place && next[i] === id)
                    next[i] = ""
            }
        }
        next[place] = id
        root.mapping = next
    }

    /// The name of the file output that has output ``id`` here, other than ``place``'s.
    function takenBy(place, id) {
        for (let i = 0; i < root.mapping.length; ++i) {
            if (i !== place && root.mapping[i] === id)
                return root.fileOutputs[i].name
        }
        return ""
    }

    /// What an import came to, for people: what it made, then what it left out.
    function describe(result) {
        const parts = []
        if (result.items > 0)
            parts.push(qsTr("%n Freeform item(s) added.", "", result.items))
        if (result.serviceId >= 0)
            parts.push(qsTr("The service is in the service list."))
        if (result.files > 0)
            parts.push(qsTr("%n file(s) added to the library.", "", result.files))
        if (result.fonts > 0)
            parts.push(qsTr("%n font(s) are being added.", "", result.fonts))
        const lines = [parts.join(" ")]
        return lines.concat(result.notes ?? []).join("\n")
    }

    function sizeText(bytes) {
        if (bytes >= 1024 * 1024 * 1024)
            return qsTr("%1 GB").arg((bytes / (1024 * 1024 * 1024)).toFixed(1))
        if (bytes >= 1024 * 1024)
            return qsTr("%1 MB").arg(Math.round(bytes / (1024 * 1024)))
        return qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1024)))
    }

    readonly property string addsText: {
        const parts = []
        const pictures = root.summary.pictures ?? 0
        const videos = root.summary.videos ?? 0
        const fonts = root.summary.fonts ?? 0
        if (pictures > 0)
            parts.push(qsTr("%n picture(s)", "", pictures))
        if (videos > 0)
            parts.push(qsTr("%n video(s)", "", videos))
        if (fonts > 0)
            parts.push(qsTr("%n font(s)", "", fonts))
        if (parts.length === 0)
            return ""
        return qsTr("Brings up to %1 (%2).").arg(parts.join(", ")).arg(root.sizeText(root.summary.bytes ?? 0))
    }

    title: root.service ? qsTr("Import service") : qsTr("Import Freeform")
    cardWidth: 560
    maxCardHeight: Math.min(720, root.height - 80)
    closeOnScrim: false
    danger: false
    acceptLabel: qsTr("Import")
    // Items need an output to land on; a service's other rows need none.
    acceptEnabled: (root.addItems || root.addService)
                   && ((root.chosen.length > 0 && root.anyOutput)
                       || (root.chosen.length === 0 && root.addService && !!root.service))
    onAccepted: {
        root.importRequest = root.session.freeformImport.importRead({
            items: root.chosen,
            mapping: root.mapping,
            folderName: root.folderName,
            addService: root.addService,
            addItemsToLibrary: root.addItems
        })
    }
    // After ``accepted``, which closing comes before: what was read is let go
    // of once the import has its own hold on it.
    onClosed: Qt.callLater(() => root.session.freeformImport.discardRead())

    Connections {
        target: root.session ? root.session.freeformImport : null
        function onFileRead(requestId, summary) {
            if (requestId !== root.readRequest)
                return
            root.readRequest = -1
            if (summary.error)
                root.failed(summary.error)
            else
                root.show(summary)
        }
        function onImportFinished(requestId, result) {
            if (requestId !== root.importRequest)
                return
            root.importRequest = -1
            if (!result.ok) {
                root.failed(result.error)
                return
            }
            root.imported(result)
        }
    }

    // Items.
    GroupHeading {
        Layout.fillWidth: true
        visible: root.items.length > 0
        topGap: 0
        text: qsTr("Items")
        note: root.importable.length > 1
              ? qsTr("%1 of %2").arg(root.chosen.length).arg(root.importable.length) : ""
    }

    Item {
        visible: root.importable.length > 1
        Layout.fillWidth: true
        Layout.preferredHeight: 20

        RowLayout {
            anchors.fill: parent
            spacing: 8

            CheckMark {
                checked: root.chosen.length === root.importable.length
                partial: root.chosen.length > 0
            }
            Text {
                text: qsTr("All items")
                color: Style.text
                font.pixelSize: Style.fontBody
            }
        }
        MouseArea {
            objectName: "freeformImportAll"
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.setAll(root.chosen.length !== root.importable.length)
        }
    }

    ListWell {
        visible: root.items.length > 0
        Layout.fillWidth: true
        // Takes what the card can spare, so a short window squeezes the list.
        Layout.fillHeight: true
        Layout.minimumHeight: 60
        Layout.preferredHeight: Math.min(itemList.contentHeight + itemList.topMargin + itemList.bottomMargin + 2, 240)

        WellPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            showHeader: false

            ListView {
                id: itemList
                objectName: "freeformImportItems"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.rightMargin: Style.scrollGap
                topMargin: 6
                bottomMargin: 6
                leftMargin: 6
                rightMargin: Style.scrollGutter(itemList)
                spacing: 3
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.items
                ScrollBar.vertical: AppScrollBar {}

                delegate: PickRow {
                    id: itemRow
                    required property var modelData
                    readonly property bool importableRow: !itemRow.modelData.refusal
                    readonly property bool checkedRow: itemRow.importableRow
                                                       && !root.unchecked[itemRow.modelData.index]

                    title: itemRow.modelData.name
                    subtitle: itemRow.modelData.refusal || itemRow.modelData.folder
                    subtitleInk: itemRow.modelData.refusal ? Style.textWarning : Style.textCaption
                    dimmed: !itemRow.checkedRow
                    leading: CheckMark {
                        enabled: itemRow.importableRow
                        checked: itemRow.checkedRow
                    }
                    trailing: Image {
                        visible: itemRow.modelData.preview.length > 0
                        source: itemRow.modelData.preview
                        sourceSize.height: 36
                        height: 36
                        width: 64
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    onClicked: {
                        if (itemRow.importableRow)
                            root.toggle(itemRow.modelData.index)
                    }
                }
            }
        }
    }

    // Outputs.
    GroupHeading {
        Layout.fillWidth: true
        text: qsTr("Outputs")
        note: qsTr("Each goes to an output here of exactly its shape.")
    }

    Repeater {
        model: root.fileOutputs

        delegate: FormRow {
            id: outputRow
            required property var modelData
            required property int index

            Layout.fillWidth: true
            label: qsTr("%1 · %2 · %3").arg(outputRow.modelData.name).arg(outputRow.modelData.size)
                                        .arg(outputRow.modelData.shape)
            hint: outputRow.modelData.choices.length === 0
                  ? qsTr("This library has no output of its shape.") : ""
            hintInk: Style.textWarning

            OptionField {
                objectName: "freeformImportOutput" + outputRow.index
                implicitWidth: Style.fieldLongW
                value: root.mapping[outputRow.index] ?? ""
                options: outputRow.modelData.choices.map(c => ({
                    value: c.id,
                    label: c.label,
                    note: root.takenBy(outputRow.index, c.id).length > 0
                          ? qsTr("for %1").arg(root.takenBy(outputRow.index, c.id)) : ""
                })).concat([{ value: "", label: qsTr("Don't import"), note: "" }])
                onPicked: function (value) { root.choose(outputRow.index, value) }
            }
        }
    }

    // Where the items go.
    FormRow {
        Layout.fillWidth: true
        visible: root.addItems && root.chosen.length > 0
        label: qsTr("Put in folder")
        hint: qsTr("Empty: the top of the Freeform library.")
        DialogField {
            objectName: "freeformImportFolder"
            implicitWidth: Style.fieldLongW
            text: root.folderName
            placeholder: qsTr("Freeform library")
            onEdited: function (value) { root.folderName = value }
        }
    }

    // A package's service.
    GroupHeading {
        Layout.fillWidth: true
        visible: !!root.service
        text: qsTr("Service")
    }

    FormRow {
        Layout.fillWidth: true
        visible: !!root.service
        label: root.service ? qsTr("Add the service “%1”").arg(root.service.name) : ""
        hint: root.service ? qsTr("%n row(s). Rows whose items are unticked are left out.", "", root.service.rows) : ""
        ToggleSwitch {
            objectName: "freeformImportAddService"
            checked: root.addService
            onToggled: root.addService = !root.addService
        }
    }

    FormRow {
        Layout.fillWidth: true
        visible: !!root.service && root.items.length > 0
        label: qsTr("Also add its items to the Freeform library")
        ToggleSwitch {
            objectName: "freeformImportAddItems"
            checked: root.addItems
            onToggled: root.addItems = !root.addItems
        }
    }

    // What it brings, and what it cannot.
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 10
        visible: text.length > 0
        text: root.addsText
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }

    Text {
        objectName: "freeformImportNeedsOutput"
        Layout.fillWidth: true
        visible: root.needsOutput
        text: qsTr("Choose where at least one of the file's outputs goes, or untick its items.")
        color: Style.textWarning
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }

    Text {
        Layout.fillWidth: true
        readonly property var notes: (root.summary.notIncluded ?? []).concat(root.service ? root.service.notes : [])
        visible: notes.length > 0
        text: notes.join("\n")
        color: Style.textWarning
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
        maximumLineCount: 8
        elide: Text.ElideRight
        textFormat: Text.PlainText
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Import Freeform")
        fileMode: FileDialog.OpenFile
        currentFolder: root.session ? root.session.exporter.lastFolder : ""
        nameFilters: {
            if (!root.session)
                return []
            const freeform = root.session.freeformImport.freeformFilePatterns()
            const packages = root.session.freeformImport.packageFilePatterns()
            return [qsTr("Freeform files and service packages (%1 %2)").arg(freeform).arg(packages),
                    qsTr("Freeform files (%1)").arg(freeform),
                    qsTr("Service packages (%1)").arg(packages)]
        }
        onAccepted: root.read(fileDialog.selectedFile.toString())
    }
}
