pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import SeventhSlide

// The library's Live tab: the live sources found now (NDI on the network,
// webcams and capture cards on this computer), grouped by kind, each with a
// thumbnail, its quality and frame rate, and a dot by its name:
// green while its picture comes, orange when it does not. Found sources are
// not saved; a service remembers one by its name, and shows it whenever it is
// on. Double-click shows a source live; Service adds the selected sources to
// the open service, as dragging them onto the service does.
LibraryTab {
    id: root

    searchPlaceholder: qsTr("Search Live…")
    addableSelectionCount: root.selectedKeys.length
    // Streams are added by hand, and edited and forgotten here; found
    // sources come and go by themselves.
    offersCreate: true
    createIcon: "plus"
    createChoices: [{ id: "stream", label: qsTr("Add a stream…"), icon: "plus" }]
    offersEdit: true
    offersDelete: true
    /// One stream alone is selected: it can be edited or forgotten.
    readonly property bool streamSelected: root.selectedKeys.length === 1 && root.selectedKind === "stream"
    canEdit: root.streamSelected
    canDelete: root.streamSelected

    readonly property var found: root.session ? root.session.liveLibrary.sources : []
    readonly property string unavailable: root.session ? root.session.liveLibrary.unavailable : ""

    /// The sources selected, by key, and the one last picked.
    readonly property ListSelection selection: ListSelection {
        model: root.session ? root.session.liveLibrary.rows : null
        keyRole: LiveSourceListModel.KeyRole
        rowOrder: true
        view: list
    }
    readonly property var selectedKeys: root.selection.keys
    // A source no longer found (an NDI sender gone from the network) leaves
    // the selection, so Service adds what shows as selected. One the search
    // hides is still found, and stays, as a song does.
    onFoundChanged: {
        const kept = root.selectedKeys.filter(key => root.sourceOf(key) !== null)
        if (kept.length !== root.selectedKeys.length)
            root.selection.set(kept, root.selection.current)
    }
    /// The source last picked: its kind and name, and a stream's address,
    /// which is what a stream is known by.
    readonly property var selectedSource: root.sourceOf(root.selection.current)
    readonly property string selectedKind: root.selectedSource ? root.selectedSource.kind : ""
    readonly property string selectedName: root.selectedSource ? root.selectedSource.name : ""
    readonly property string selectedUrl: root.selectedSource ? root.selectedSource.url : ""

    // The search is the live library's, which lists what it matches.
    Binding {
        target: root.session ? root.session.liveLibrary : null
        property: "search"
        value: root.search
        when: !!root.session
    }
    // The network is looked at while the tab shows.
    Binding {
        target: root.session ? root.session.liveLibrary : null
        property: "searching"
        value: root.current
        when: !!root.session
    }

    /// Source ``key`` as found, or null.
    function sourceOf(key) {
        return key === null ? null : root.found.find(s => s.key === key) ?? null
    }

    function clearSelection() {
        root.selection.clear()
        root.selection.endMarquee()
    }

    /// What a call names a source by: a stream its address, the others their
    /// name.
    function callName(source) {
        return source.kind === "stream" ? source.url : source.name
    }

    /// Show source ``key`` on its stack: cued where it has a Preview, unless
    /// ``live`` (Shift+double-click).
    function play(key, live) {
        const source = root.sourceOf(key)
        if (root.session && source)
            root.session.liveLibrary.playSource(source.kind, root.callName(source), !!live)
    }

    /// The selected sources, in the list's order, as the service takes them.
    function dragPayload() {
        const sources = []
        for (const key of root.selectedKeys) {
            const source = root.sourceOf(key)
            if (source)
                sources.push({ kind: source.kind, name: root.callName(source) })
        }
        return { kind: "live", sources: sources }
    }

    function dragLabel() {
        return root.selectedKeys.length > 1 ? qsTr("%1 sources").arg(root.selectedKeys.length)
                                            : root.selectedName
    }

    function addToService() {
        if (root.selectedKeys.length)
            root.applyLibraryPayload(root.dragPayload())
    }

    function createItem() {
        root.streamDialog().openNew()
    }

    function createChosen(id) {
        if (id === "stream")
            root.createItem()
    }

    function editSelected() {
        if (root.streamSelected)
            root.streamDialog().openFor(root.selectedName, root.selectedUrl)
    }

    function deleteText() {
        return qsTr("Forget the stream \u201c%1\u201d?").arg(root.selectedName)
    }

    function deleteSelected() {
        if (!root.streamSelected || !root.session)
            return ""
        const err = root.session.liveLibrary.deleteStream(root.selectedUrl)
        if (!err.length)
            root.selection.clear()
        return err
    }

    /// The stream dialog, made the first time it is asked for.
    function streamDialog(): StreamDialog {
        return streamLoader.load() as StreamDialog
    }

    LazyLoader {
        id: streamLoader
        objectName: "StreamDialog"
        sourceComponent: StreamDialog {
            session: root.session
            onSaved: function (result) {
                if (result.error)
                    return
                if (root.sourceOf(result.key))
                    root.selection.selectOnly(result.key)
            }
        }
    }

    ListView {
        id: list
        objectName: "liveSourceList"
        anchors.fill: parent
        clip: true
        spacing: 3
        model: root.session ? root.session.liveLibrary.rows : null
        interactive: !root.libDragging
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        delegate: Rectangle {
            id: row
            required property string key
            required property string kind
            required property string kindLabel
            required property string name
            required property string url
            required property string detail
            required property string signal
            readonly property bool selected: root.selection.contains(row.key)
            width: ListView.view.width - Style.scrollGutter(ListView.view)
            height: 48
            radius: Style.radiusSmall
            color: Style.panelRowBg(row.selected, hover.containsMouse)

            ListRowAccent {
                cornerRadius: parent.radius
                selected: row.selected
                hovered: hover.containsMouse
            }
            // A still of its picture, taken each time the tab shows; the
            // kind's glyph until there is one.
            Rectangle {
                id: thumbBox
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 64
                height: 36
                radius: 2
                color: Style.thumbWell
                clip: true
                LiveThumbItem {
                    id: thumb
                    anchors.fill: parent
                    session: root.session
                    sourceKey: row.key
                }
                Image {
                    anchors.centerIn: parent
                    visible: thumb.implicitWidth <= 0
                    width: 18
                    height: 18
                    source: Icons.url(row.kind === "device" ? "video"
                                      : row.kind === "decklink" ? "bmd"
                                      : row.kind === "stream" ? "cast" : "ndi",
                                      Style.textCaption)
                    sourceSize: Qt.size(18, 18)
                }
            }
            Column {
                anchors.left: thumbBox.right
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Row {
                    width: parent.width
                    spacing: 6
                    Rectangle {
                        objectName: "signalDot"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.signal.length > 0
                        width: 7
                        height: 7
                        radius: 3.5
                        color: row.signal === "ok" ? Style.signalOk : Style.signalLost
                    }
                    Text {
                        width: parent.width - (x > 0 ? x : 0)
                        text: row.name
                        color: Style.text
                        font.pixelSize: Style.fontBody
                        elide: Text.ElideRight
                    }
                }
                Text {
                    width: parent.width
                    text: [row.kindLabel, row.detail]
                          .filter(t => t.length).join(", ")
                    color: Style.textCaption
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
            }
            LibraryRowPress {
                id: hover
                libraryTab: root
                selection: root.selection
                key: row.key
                onDragStarted: function (x, y) {
                    root.beginLibAdd(root.dragPayload(), root.dragLabel(), hover, x, y)
                }
                onActivated: function (live) {
                    root.play(row.key, live)
                }
            }
        }

        MarqueeRect {
            view: list
            active: root.selection.marqueeActive
            contentX1: root.selection.marqueeX1
            contentY1: root.selection.marqueeY1
            contentX2: root.selection.marqueeX2
            contentY2: root.selection.marqueeY2
        }
    }

    LibraryMarqueeArea {
        libraryTab: root
        list: list
        selection: root.selection
    }

    // Why the list is empty.
    Text {
        objectName: "liveSourcesNote"
        anchors.centerIn: parent
        width: parent.width - 32
        visible: list.count === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: {
            if (root.unavailable.length)
                return root.unavailable
            if (root.found.length)
                return qsTr("No live sources match")
            return qsTr("No live sources found.")
        }
        color: Style.textMuted
        font.pixelSize: Style.fontUi
    }
}
