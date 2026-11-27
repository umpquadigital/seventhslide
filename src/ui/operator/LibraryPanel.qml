pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide
import "../js/LibraryTabs.js" as LibraryTabs

// The Library panel: a tab strip, a toolbar with a search field, and the tab
// showing. The toolbar and search work through the LibraryTab interface, so
// this file knows no tab's contents. The panel's frame and title are
// DockPanel's.
Item {
    id: root
    required property SessionFacade session
    /// The Service panel, which takes what is dragged or added from here.
    property ServicePanel serviceHost: null

    signal requestSaveAsFreeform(int songId, string name)
    signal requestSaveBibleAsFreeform(int verseStart, int verseEnd, string name)

    focus: true
    // Insert adds, or Ctrl+Return (Command+Return) where there is no Insert
    // key, as on a Mac's.
    Keys.onPressed: function (event) {
        const add = event.key === Qt.Key_Insert
                    || ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ControlModifier))
        if (add && root.canAdd) {
            root.toolAdd()
            event.accepted = true
        }
    }

    /// Padding round each tab's contents.
    readonly property int listPad: 6

    /// The tab showing, as an index into ``tabs``, which has them in their
    /// default order; Settings arranges the strip (``shownTabIds``).
    property int tabIndex: 0
    readonly property list<LibraryTab> tabs: [songsTab, freeformBrowser, biblesTab, videoBrowser,
                                              imageBrowser, audioBrowser, browserTab, liveTab]
    /// How many tabs the strip shows at once at most, as Settings left it; the
    /// rest are behind its arrow.
    readonly property int stripTabCount: root.session ? root.session.library.tabsShown : 6
    readonly property LibraryTab activeTab: root.tabs[root.tabIndex] ?? songsTab
    /// The tabs on the strip, in order, as Settings left them on this computer.
    readonly property list<string> shownTabIds: root.session
        ? LibraryTabs.shown(root.session.library.tabOrder, root.session.library.hiddenTabs)
        : LibraryTabs.shown([], [])
    readonly property list<string> tabTitles: {
        const titles = []
        for (let i = 0; i < root.shownTabIds.length; ++i)
            titles.push(root.tabs[root.tabIndexOf(root.shownTabIds[i])].title)
        return titles
    }

    /// The index of the tab ``id`` names; Songs for one there is not.
    function tabIndexOf(id) {
        for (let i = 0; i < root.tabs.length; ++i) {
            if (root.tabs[i].tabId === id)
                return i
        }
        return 0
    }

    // The tab showing when the app last closed, on this computer; the strip's
    // first if that one is hidden now.
    Component.onCompleted: {
        const id = root.session ? root.session.library.shownTab : ""
        const idx = root.tabIndexOf(root.shownTabIds.indexOf(id) >= 0 ? id : root.shownTabIds[0])
        if (idx !== root.tabIndex) {
            root.tabIndex = idx
            root.activeTab.enter()
            root.syncSearchField()
        }
        root.tabRemembered = true
    }

    /// The remembered tab has been opened. ``shownTabIds`` also changes as the
    /// panel is made, which must not choose a tab in its place.
    property bool tabRemembered: false

    // Settings hid the tab showing.
    onShownTabIdsChanged: {
        if (root.tabRemembered && root.shownTabIds.indexOf(root.activeTab.tabId) < 0)
            root.selectTab(root.tabIndexOf(root.shownTabIds[0]))
    }

    readonly property bool mediaIconsMode: !!root.session && root.session.media.mediaViewMode === "icons"
    readonly property bool sessionReady: !!root.session && root.session.ready

    readonly property bool canEdit: root.activeTab.canEdit
    readonly property bool canConvert: root.activeTab.canConvert
    readonly property bool canDuplicate: root.activeTab.canDuplicate
    readonly property bool canDelete: root.activeTab.canDelete
    readonly property bool canAdd: root.activeTab.canAdd

    function clearSelection() {
        for (let i = 0; i < root.tabs.length; ++i)
            root.tabs[i].clearSelection()
    }

    /// A tab-strip click: leave the tab showing and enter ``idx``, with
    /// nothing selected anywhere.
    function selectTab(idx) {
        root.clearFieldFocus()
        root.activeTab.leave()
        root.tabIndex = idx
        if (root.session)
            root.session.library.shownTab = root.activeTab.tabId
        root.clearSelection()
        root.activeTab.enter()
        root.syncSearchField()
    }

    /// Blur the library search field and drop any text selection.
    function clearFieldFocus() {
        if (libSearchInput.activeFocus) {
            libSearchInput.deselect()
            libSearchInput.focus = false
        }
        root.activeTab.closeMenus()
    }

    function syncSearchField() {
        const t = root.activeTab.search
        if (libSearchInput.text !== t) {
            libSearchInput.text = t
            libSearchInput.cursorPosition = t.length
        }
    }

    function toolAdd() {
        if (root.canAdd)
            root.activeTab.addToService()
    }

    function toolDelete() {
        if (root.canDelete)
            deleteConfirm.ask(root.activeTab.deleteText())
    }

    function performDelete() {
        if (!root.canDelete)
            return
        const err = root.activeTab.deleteSelected()
        if (err.length)
            deleteError.ask(err)
    }

    Connections {
        target: root.activeTab
        function onSearchChanged() {
            root.syncSearchField()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The tools below the tabs are on the panel's face, as under every
        // panel's heading.
        PanelTabStrip {
            Layout.fillWidth: true
            titles: root.tabTitles
            pageLimit: root.stripTabCount
            currentIndex: root.shownTabIds.indexOf(root.activeTab.tabId)
            onTabClicked: index => root.selectTab(root.tabIndexOf(root.shownTabIds[index]))
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Style.border
        }

        // The tab's tools, then its search, flat on the panel.
        Rectangle {
            id: toolbar
            Layout.fillWidth: true
            Layout.preferredHeight: 8 + Style.controlH + 6 + Style.controlH + 8
            color: Style.panelBg
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Style.controlH
                    spacing: 5

                    // The tab's own controls lead the row and take its free
                    // space, with the buttons after them; without any, a
                    // spacer before Service takes it.
                    Loader {
                        id: toolbarExtra
                        Layout.fillWidth: true
                        Layout.preferredHeight: Style.controlH
                        active: !!root.activeTab.toolbarExtra
                        visible: active
                        sourceComponent: root.activeTab.toolbarExtra
                    }

                    LibToolBtn {
                        id: createButton
                        visible: root.activeTab.offersCreate
                        icon: root.activeTab.createIcon
                        enabled: root.sessionReady
                        onClicked: {
                            const choices = root.activeTab.createChoices
                            if (choices.length === 1 && !root.activeTab.offersNewFolder)
                                root.activeTab.createChosen(choices[0].id)
                            else if (createMenu.visible)
                                createMenu.close()
                            else
                                createMenu.openAt()
                        }

                        // The tab's ways to create (a new song, or one
                        // from this computer; media from this computer or
                        // the internet), then a new folder where it has
                        // folders.
                        AppMenu {
                            id: createMenu
                            objectName: "createMenu"
                            anchorItem: createButton
                            Repeater {
                                model: root.activeTab.createChoices
                                delegate: MenuItemRow {
                                    required property var modelData
                                    icon: modelData.icon || ""
                                    label: modelData.label
                                    onTriggered: {
                                        createMenu.close()
                                        root.activeTab.createChosen(modelData.id)
                                    }
                                }
                            }
                            MenuRule {
                                visible: root.activeTab.offersNewFolder
                                         && root.activeTab.createChoices.length > 0
                            }
                            MenuItemRow {
                                visible: root.activeTab.offersNewFolder
                                icon: "folderPlus"
                                label: qsTr("New folder…")
                                onTriggered: {
                                    createMenu.close()
                                    root.activeTab.newFolder()
                                }
                            }
                        }
                    }
                    LibToolBtn {
                        id: exportButton
                        visible: root.activeTab.offersExport
                        icon: "export"
                        enabled: root.sessionReady && root.activeTab.canExport
                        onClicked: {
                            const choices = root.activeTab.exportChoices
                            if (choices.length === 1)
                                root.activeTab.exportChosen(choices[0].id)
                            else if (exportMenu.visible)
                                exportMenu.close()
                            else
                                exportMenu.openAt()
                        }

                        // The tab's ways to export what is selected (songs
                        // as XML or as images).
                        AppMenu {
                            id: exportMenu
                            objectName: "exportMenu"
                            anchorItem: exportButton
                            Repeater {
                                model: root.activeTab.exportChoices
                                delegate: MenuItemRow {
                                    required property var modelData
                                    icon: modelData.icon || ""
                                    label: modelData.label
                                    onTriggered: {
                                        exportMenu.close()
                                        root.activeTab.exportChosen(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                    LibToolBtn {
                        visible: root.activeTab.offersEdit
                        icon: "pen"
                        enabled: root.canEdit
                        onClicked: root.activeTab.editSelected()
                    }
                    LibToolBtn {
                        visible: root.activeTab.offersConvert
                        icon: "hammer"
                        enabled: root.canConvert
                        onClicked: root.activeTab.convertSelected()
                    }
                    LibToolBtn {
                        visible: root.activeTab.offersDuplicate
                        icon: "copy"
                        enabled: root.canDuplicate
                        onClicked: root.activeTab.duplicateSelected()
                    }
                    LibToolBtn {
                        visible: root.activeTab.offersDelete
                        icon: "trash"
                        enabled: root.canDelete
                        onClicked: root.toolDelete()
                    }
                    LibToolBtn {
                        visible: root.activeTab.offersViewToggle
                        icon: root.mediaIconsMode ? "list" : "grid"
                        enabled: root.sessionReady
                        onClicked: root.activeTab.toggleView()
                    }

                    Item {
                        Layout.fillWidth: true
                        visible: !toolbarExtra.active
                    }

                    LibToolBtn {
                        icon: "listPlus"
                        label: qsTr("Service")
                        primary: true
                        enabled: root.canAdd
                        onClicked: root.toolAdd()
                    }
                }

                Rectangle {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.preferredHeight: Style.controlH
                    radius: Style.radiusSmall
                    color: libSearchInput.activeFocus ? Style.fieldFaceFocus : Style.fieldFace
                    border.width: 1
                    border.color: libSearchInput.activeFocus ? Style.focusEdge
                                 : (searchHover.containsMouse ? Style.controlEdgeHover
                                                              : Style.controlEdge)

                    Image {
                        id: searchGlyph
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        source: Icons.url("search", Style.textCaption)
                        sourceSize: Qt.size(12, 12)
                        smooth: true
                    }

                    TextInput {
                        id: libSearchInput
                        anchors.left: searchGlyph.right
                        // The clear button shows only with text in the field.
                        anchors.right: searchClear.visible ? searchClear.left : parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 6
                        anchors.rightMargin: searchClear.visible ? 4 : 12
                        verticalAlignment: Text.AlignVCenter
                        color: Style.searchInk
                        font.pixelSize: Style.fontBody
                        selectByMouse: true
                        clip: true
                        onTextChanged: {
                            if (text !== root.activeTab.search)
                                root.activeTab.search = text
                        }
                        Component.onCompleted: root.syncSearchField()
                        Keys.onPressed: function (event) {
                            if (root.activeTab.searchKeyPressed(event.key))
                                event.accepted = true
                        }
                        Keys.onReturnPressed: function (event) {
                            if (root.activeTab.searchSubmitted())
                                event.accepted = true
                        }
                        Keys.onEnterPressed: function (event) {
                            if (root.activeTab.searchSubmitted())
                                event.accepted = true
                        }
                        Keys.onEscapePressed: function (event) {
                            if (root.activeTab.searchEscaped())
                                event.accepted = true
                            else
                                root.clearFieldFocus()
                        }
                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            text: root.activeTab.searchPlaceholder
                            color: Style.fieldPlaceholder
                            font.pixelSize: Style.fontBody
                            visible: libSearchInput.text.length === 0
                                     && !libSearchInput.activeFocus
                        }
                    }

                    Text {
                        id: searchClear
                        z: 2
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "×"
                        color: searchClearHover.containsMouse ? Style.text : Style.textMuted
                        font.pixelSize: Style.fontTitle
                        visible: libSearchInput.text.length > 0
                        MouseArea {
                            id: searchClearHover
                            anchors.fill: parent
                            anchors.margins: -4
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.activeTab.search = ""
                                libSearchInput.forceActiveFocus()
                            }
                        }
                    }

                    MouseArea {
                        id: searchHover
                        anchors.fill: parent
                        acceptedButtons: Qt.NoButton
                        hoverEnabled: true
                        cursorShape: Qt.IBeamCursor
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Style.wellEdge
        }

        // Body: the tab showing, in the panel's dark well.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Style.wellBg
            clip: true

            SongsTab {
                id: songsTab
                tabId: "songs"
                anchors.fill: parent
                anchors.margins: root.listPad
                marqueeBleed: root.listPad
                current: root.activeTab === songsTab
                session: root.session
                serviceHost: root.serviceHost
                onInteracted: root.clearFieldFocus()
                onSaveAsFreeformRequested: function (songId, name) {
                    root.requestSaveAsFreeform(songId, name)
                }
            }

            MediaBrowser {
                id: freeformBrowser
                tabId: "freeform"
                anchors.fill: parent
                anchors.margins: root.listPad
                marqueeBleed: root.listPad
                current: root.activeTab === freeformBrowser
                session: root.session
                serviceHost: root.serviceHost
                freeform: true
                onInteracted: root.clearFieldFocus()
            }

            BiblesTab {
                id: biblesTab
                tabId: "bibles"
                anchors.fill: parent
                anchors.margins: root.listPad
                marqueeBleed: root.listPad
                current: root.activeTab === biblesTab
                session: root.session
                serviceHost: root.serviceHost
                searchAnchor: searchField
                onInteracted: root.clearFieldFocus()
                onSaveAsFreeformRequested: function (verseStart, verseEnd, name) {
                    root.requestSaveBibleAsFreeform(verseStart, verseEnd, name)
                }
            }

            MediaBrowser {
                id: videoBrowser
                tabId: "videos"
                anchors.fill: parent
                anchors.margins: root.listPad
                marqueeBleed: root.listPad
                current: root.activeTab === videoBrowser
                session: root.session
                serviceHost: root.serviceHost
                kind: MediaKind.Video
                onInteracted: root.clearFieldFocus()
            }

            MediaBrowser {
                id: imageBrowser
                tabId: "images"
                anchors.fill: parent
                anchors.margins: root.listPad
                marqueeBleed: root.listPad
                current: root.activeTab === imageBrowser
                session: root.session
                serviceHost: root.serviceHost
                kind: MediaKind.Image
                onInteracted: root.clearFieldFocus()
            }

            MediaBrowser {
                id: audioBrowser
                tabId: "audio"
                anchors.fill: parent
                anchors.margins: root.listPad
                marqueeBleed: root.listPad
                current: root.activeTab === audioBrowser
                session: root.session
                serviceHost: root.serviceHost
                kind: MediaKind.Audio
                onInteracted: root.clearFieldFocus()
            }

            MediaBrowser {
                id: browserTab
                tabId: "browser"
                anchors.fill: parent
                anchors.margins: root.listPad
                marqueeBleed: root.listPad
                current: root.activeTab === browserTab
                session: root.session
                serviceHost: root.serviceHost
                web: true
                onInteracted: root.clearFieldFocus()
            }

            LiveSourcesTab {
                id: liveTab
                anchors.fill: parent
                anchors.margins: root.listPad
                current: root.activeTab === liveTab
                session: root.session
                serviceHost: root.serviceHost
                tabId: "live"
                onInteracted: root.clearFieldFocus()
            }
        }
    }

    // What an empty tab says, where a media tab says it.
    component TabEmptyNote: Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 24
        color: Style.textMuted
        font.pixelSize: Style.fontUi
    }

    // A library toolbar button. Pressing one ends a search-field edit.
    component LibToolBtn: IconToolButton {
        size: "control"
        onPressed: root.clearFieldFocus()
    }

    AppDialog {
        id: deleteConfirm
        title: qsTr("Delete")
        acceptLabel: qsTr("Delete")
        onAccepted: root.performDelete()
    }

    AppDialog {
        id: deleteError
        title: qsTr("Could not delete")
        acknowledge: true
    }
}
