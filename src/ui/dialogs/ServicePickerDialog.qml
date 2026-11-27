pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import SeventhSlide

// The Services window: a table of the services in a dark well, as Settings'
// Bibles are, each with a thumbnail of its first slide, by date (newest
// first) or by name: click a column's name to sort by it, again to turn it
// round. Over the well the search, its tag button opening a menu of the tags
// in use (a search, the tags picked as chips, each with an ×, and the rest
// in a list that scrolls; as many picked as wanted: the services with every
// one), and
// Settings, Delete and New for the one picked: Settings (and
// New, for the service it makes) opens Service Settings on it, over this
// window. Double-click a service (or Open, or Enter) to load it. What it
// shows and does is ServicesState; this is the window around it.
Item {
    id: root
    visible: open
    z: Style.sheetZ
    focus: open

    required property SessionFacade session
    property bool open: false
    property string errorText: ""

    /// Service Settings is asked for, over this window, on ``serviceId``; for
    /// a new service, with the cursor in its title. The owner opens it, and
    /// calls settingsClosed() when it closes.
    signal settingsAsked(var serviceId, bool isNew)

    readonly property bool searching: services.search.trim().length > 0
    readonly property bool filtering: services.filterTags.length > 0
    /// The tag menu's search: the tags it lists are those with these words.
    property string tagSearch: ""
    /// The tag the keys are on in the menu's list (Up, Down; Enter picks
    /// it), or -1. A search puts it on the first match.
    property int tagCursor: -1
    onTagSearchChanged: root.tagCursor = root.tagSearch.trim().length ? 0 : -1
    /// The menu's list shows this many tags, and scrolls through the rest.
    readonly property int tagRowsShown: 9
    /// The menu's chips of the tags picked take up to this, then scroll.
    readonly property int tagChipsH: 92
    /// Bumped each time the window opens and Service Settings closes: the
    /// thumbnails are read again, as of now.
    property int showing: 0
    /// The table's thumbnail, in the first output's shape, and its Date and
    /// Time columns.
    readonly property int thumbH: 36
    readonly property int thumbW: {
        const aspect = root.session ? root.session.outputPreviews.slideAspect : 16 / 9
        return Math.round(root.thumbH * Math.max(1, Math.min(aspect, 2.4)))
    }
    readonly property int dateW: 132
    readonly property int timeW: 84

    ServicesState {
        id: services
        session: root.session
        onReported: function (error) { root.errorText = error }
        onDeleteAsked: function (id, message) {
            deleteConfirm.serviceId = id
            deleteConfirm.ask(message)
        }
        onOpened: root.close()
        onCreated: function (id) {
            serviceList.positionViewAtIndex(services.browser.shownIds.indexOf(id),
                                            ListView.Contain)
            root.settingsAsked(id, true)
        }
    }

    /// Back from Service Settings, the keys are this window's again, and
    /// the thumbnails show what it changed.
    function settingsClosed() {
        root.showing += 1
        if (root.open)
            root.forceActiveFocus()
    }

    /// How many items service ``id`` holds, as of ``showing``: read again
    /// each time the window opens.
    function itemCountOf(id, showing) {
        return root.session ? root.session.library.serviceItemCount(id) : -1
    }

    function show() {
        services.reset()
        tagMenu.close()
        root.tagSearch = ""
        root.errorText = ""
        root.showing += 1
        root.open = true
        root.forceActiveFocus()
        const at = services.browser.shownIds.indexOf(services.selectedId)
        if (at >= 0)
            Qt.callLater(serviceList.positionViewAtIndex, at, ListView.Contain)
    }

    function close() {
        tagMenu.close()
        root.open = false
        root.errorText = ""
    }

    function toggleTagMenu() {
        if (tagMenu.visible) {
            tagMenu.close()
            return
        }
        root.tagCursor = -1
        tagMenu.openAt()
        tagSearchField.focusInput()
    }

    /// The keys move ``by`` down the menu's tags (up, negative).
    function stepTagCursor(by) {
        const n = root.menuTags.length
        if (n === 0)
            return
        root.tagCursor = root.tagCursor < 0 ? (by > 0 ? 0 : n - 1)
                                            : Math.max(0, Math.min(n - 1, root.tagCursor + by))
        tagList.positionViewAtIndex(root.tagCursor, ListView.Contain)
    }

    /// Enter in the menu's search: the tag the keys are on is picked, and
    /// the search cleared for the next.
    function pickTagAtCursor() {
        const tag = root.menuTags[root.tagCursor]
        if (!tag)
            return
        services.toggleTag(tag.name)
        root.tagSearch = ""
    }

    /// The tag menu's list: the tags in use not picked (those are its
    /// chips) that the search leaves, each with its count ({name, count}).
    readonly property var menuTags: services.browser.tags.filter(function (t) {
        return !services.hasTag(t.name) && SearchText.matches(String(t.name), root.tagSearch)
    })
    // A pick shortens the list: the keys stay on the row in its place.
    onMenuTagsChanged: {
        if (root.tagCursor >= root.menuTags.length)
            root.tagCursor = root.menuTags.length - 1
    }

    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: root.close()
    }

    WindowShell {
        id: box
        anchors.centerIn: parent
        width: Math.min(Style.servicesWindowW, parent.width - 40)
        height: Math.min(Style.settingsWindowH, parent.height - 40)
        title: qsTr("Configure Services")
        errorText: root.errorText
        acceptLabel: qsTr("Open")
        acceptEnabled: services.canOpen
        onCanceled: root.close()
        onAccepted: services.openSelected()

        ColumnLayout {
            anchors.fill: parent
            spacing: 14

            PageHeader {
                Layout.fillWidth: true
                title: qsTr("Service List")
                text: qsTr("Every service in this library, past and upcoming, ready to open, plan or organize.")
            }

            // The search, and Settings, Delete and New, over the well.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                SearchFilterField {
                    id: searchField
                    objectName: "serviceSearchField"
                    Layout.preferredWidth: Style.fieldLongW
                    placeholder: qsTr("Search services")
                    text: services.search
                    filtering: root.filtering
                    filterLabel: services.filterTags.length === 1
                                 ? services.filterTags[0]
                                 : (root.filtering ? qsTr("%1 tags").arg(services.filterTags.length) : "")
                    onEdited: function (value) { services.search = value }
                    onEscaped: searchField.blur()
                    onFilterClicked: root.toggleTagMenu()
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    objectName: "serviceSettingsButton"
                    size: "control"
                    iconSource: Icons.url("settings", ink)
                    enabled: services.selected !== null
                    onClicked: root.settingsAsked(services.selectedId, false)
                }
                AppButton {
                    size: "control"
                    iconSource: Icons.url("trash", ink)
                    enabled: services.selected !== null
                    onClicked: services.beginDelete(services.selectedId, services.selected.name)
                }
                AppButton {
                    objectName: "serviceImportButton"
                    size: "control"
                    iconSource: Icons.url("import", ink)
                    enabled: !!root.session && root.session.ready
                    onClicked: (importLoader.load() as FreeformImportDialog).start()
                }
                AppButton {
                    size: "control"
                    primary: true
                    label: qsTr("New")
                    iconSource: Icons.url("plus", ink)
                    enabled: !!root.session && root.session.ready
                    onClicked: services.createService()
                }
            }

            ListWell {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: -4

                WellPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The columns' names are the well's bar.
                    showHeader: false

                    // The columns' names; Name and Date sort.
                    TableHeaderBar {
                        rightInset: Style.scrollGutter(serviceList)

                        // The thumbnails' column; the rows' inset from the
                        // well's edge with it, so the grooves line up.
                        TableColumnHead {
                            Layout.preferredWidth: root.thumbW + 16 + serviceList.leftMargin
                            label: qsTr("Preview")
                        }
                        TableGroove {}
                        TableColumnHead {
                            Layout.fillWidth: true
                            label: qsTr("Name")
                            sortable: true
                            sorted: services.sortKey === "name"
                            ascending: services.sortAscending
                            onClicked: services.sortBy("name")
                        }
                        TableGroove {}
                        TableColumnHead {
                            Layout.preferredWidth: root.dateW
                            label: qsTr("Date")
                            sortable: true
                            sorted: services.sortKey === "date"
                            ascending: services.sortAscending
                            onClicked: services.sortBy("date")
                        }
                        TableGroove {}
                        TableColumnHead {
                            Layout.preferredWidth: root.timeW + 6
                            label: qsTr("Time")
                        }
                    }

                    ListView {
                        id: serviceList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // The bar a gap in from the well's edge, as the Library's is.
                        Layout.rightMargin: Style.scrollGap
                        // Inset and apart, as Settings' tables' rows are.
                        topMargin: 6
                        bottomMargin: 6
                        leftMargin: 6
                        rightMargin: Style.scrollGutter(serviceList)
                        spacing: 3
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: services.browser
                        ScrollBar.vertical: AppScrollBar {}

                        // One row per service, keyed by the model: a change to
                        // a service updates its row in place.
                        delegate: ServiceTableRow {
                            id: serviceRow
                            thumbW: root.thumbW
                            thumbH: root.thumbH
                            thumbUrl: root.session
                                      ? root.session.serviceThumbs.thumbUrl(serviceRow.serviceId,
                                                                            root.showing)
                                      : ""
                            itemCount: root.itemCountOf(serviceRow.serviceId, root.showing)
                            dateW: root.dateW
                            timeW: root.timeW
                            selected: services.selectedId === serviceRow.serviceId
                            onClicked: services.select(serviceRow.serviceId)
                            onDoubleClicked: services.openService(serviceRow.serviceId)
                        }

                        // Nothing to list: no services, none with the tags, or none found.
                        EmptyListNote {
                            visible: serviceList.count === 0
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 14
                            title: root.searching || root.filtering ? qsTr("No matches")
                                                                    : qsTr("No services yet")
                            hint: root.searching
                              ? qsTr("Nothing found for “%1”.").arg(services.search.trim())
                              : root.filtering ? qsTr("No service has every tag picked.")
                                               : qsTr("Use New to make one.")
                        }
                    }
                }
            }
        }
    }

    // The tags, from the search field's tag button: a search over them; the
    // tags picked as chips, each with an × to drop it (Clear drops them all);
    // and the rest the search leaves, each with its count, in a list that
    // scrolls past ``tagRowsShown``. A tag picked leaves the list for the
    // chips, the menu staying open to pick another; the keys step through
    // the list from the search, and Enter picks the one they are on.
    AppMenu {
        id: tagMenu
        objectName: "serviceTagMenu"
        anchorItem: searchField.filterButton
        minWidth: 240
        onClosed: {
            root.tagSearch = ""
            root.tagCursor = -1
        }

        // As wide as the longest tag asks (to a limit): the list's rows are
        // made only as they scroll into sight, so cannot say.
        FontMetrics {
            id: tagFont
            font.pixelSize: Style.fontBody
        }
        readonly property real _tagsWidth: {
            let w = 0
            for (const t of services.browser.tags)
                w = Math.max(w, tagFont.advanceWidth(String(t.name)))
            // Check, icon and count around the name, and the bar's gutter.
            return Math.min(420, w + 110)
        }

        Item {
            visible: services.browser.tags.length > 0
            width: parent ? parent.width : 0
            height: 34
            DialogField {
                id: tagSearchField
                objectName: "serviceTagSearch"
                anchors.fill: parent
                anchors.margins: 4
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                placeholder: qsTr("Search tags")
                text: root.tagSearch
                stepsKeys: true
                onEdited: function (value) { root.tagSearch = value }
                onStepped: function (by) { root.stepTagCursor(by) }
                onSubmitted: root.pickTagAtCursor()
            }
        }
        // The tags picked, under the search. They wrap onto more lines as more are ticked, and
        // past ``tagChipsH`` scroll.
        Item {
            visible: root.filtering
            width: parent ? parent.width : 0
            height: Math.min(tagChips.implicitHeight, root.tagChipsH) + 10

            Flickable {
                id: tagChipsView
                anchors.fill: parent
                anchors.topMargin: 2
                anchors.bottomMargin: 6
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                contentWidth: width
                contentHeight: tagChips.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}

                Flow {
                    id: tagChips
                    objectName: "serviceTagChips"
                    width: tagChipsView.width - Style.scrollGutter(tagChipsView)
                    spacing: 4

                    Repeater {
                        model: services.filterTags
                        delegate: TagChip {
                            required property string modelData
                            text: modelData
                            removable: true
                            onRemoveClicked: services.toggleTag(modelData)
                        }
                    }
                    Text {
                        height: 20
                        leftPadding: 4
                        rightPadding: 4
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("Clear")
                        color: clearArea.containsMouse ? Style.textStrong : Style.textCaption
                        font.pixelSize: Style.fontSmall
                        MouseArea {
                            id: clearArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: services.clearTags()
                        }
                    }
                }
            }
        }
        MenuRule { visible: root.filtering && root.menuTags.length > 0 }
        Item {
            visible: root.menuTags.length > 0
            width: parent ? parent.width : 0
            implicitWidth: tagMenu._tagsWidth
            height: Math.min(root.menuTags.length, root.tagRowsShown) * 28

            ListView {
                id: tagList
                objectName: "serviceTagList"
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.menuTags
                ScrollBar.vertical: AppScrollBar {}

                delegate: MenuItemRow {
                    required property var modelData
                    required property int index
                    width: tagList.width - Style.scrollGutter(tagList)
                    current: index === root.tagCursor
                    icon: "tag"
                    label: modelData.name
                    shortcut: String(modelData.count)
                    onTriggered: services.toggleTag(modelData.name)
                }
            }
        }
        MenuItemRow {
            visible: services.browser.tags.length > 0 && root.menuTags.length === 0
            enabled: false
            label: root.tagSearch.trim().length ? qsTr("No tags match")
                                                : qsTr("Every tag is picked")
        }
        MenuItemRow {
            visible: services.browser.tags.length === 0
            enabled: false
            label: qsTr("No tags yet")
            detail: qsTr("Tag a service in its Settings.")
        }
    }

    // Import a service's package (or any Freeform file) into this library.
    LazyLoader {
        id: importLoader
        objectName: "FreeformImportDialog"
        sourceComponent: FreeformImportDialog {
            id: serviceImport
            session: root.session
            onFailed: function (why) { importNote.title = qsTr("Could not import"); importNote.ask(why) }
            onImported: function (result) {
                if (result.serviceId >= 0)
                    services.select(result.serviceId)
                importNote.title = qsTr("Imported")
                importNote.ask(serviceImport.describe(result))
            }
        }
    }

    AppDialog {
        id: importNote
        acknowledge: true
    }

    AppDialog {
        id: deleteConfirm
        property var serviceId: -1
        title: qsTr("Delete service")
        acceptLabel: qsTr("Delete")
        onAccepted: services.performDelete(deleteConfirm.serviceId)
    }

    Keys.onEscapePressed: function (event) {
        root.close()
        event.accepted = true
    }
    // Enter loads the selected service, as Open does (a field takes its own).
    Keys.onReturnPressed: function (event) {
        services.openSelected()
        event.accepted = true
    }
    Keys.onEnterPressed: function (event) {
        services.openSelected()
        event.accepted = true
    }
}
