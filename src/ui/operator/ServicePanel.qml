pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect
import "../js/Stacks.js" as Stacks
import "../js/Reorder.js" as Reorder

// The Service panel: the open service's toolbar and its rundown, which items
// are selected, and the drop target the Library drags into (LibraryPanel's
// ``serviceHost``). The panel's frame and title are DockPanel's.
Rectangle {
    id: root
    color: Style.panelBg

    required property SessionFacade session

    /// A press in the panel: the window ends a search being typed elsewhere.
    signal interacted()
    /// A change to the rundown was refused; the window says why.
    signal failed(string message)
    /// Something is being dragged in from the Library: the window opens the
    /// panel if it is collapsed, so there is somewhere to drop it.
    signal dropIncoming()
    /// The selected rundown items (by item id), and the one last picked.
    readonly property ListSelection itemSelection: ListSelection {
        model: root.session ? root.session.serviceItems : null
        keyRole: ServiceItemModel.ItemIdRole
        view: itemList
        onChanged: root.syncItemType()
    }
    readonly property var selectedItemIds: root.itemSelection.keys
    readonly property var selectedItemId: root.itemSelection.current !== null
                                          ? root.itemSelection.current : -1
    /// The current item's type; kept while its row is out of the model.
    property string selectedItemType: ""
    property string dividerDialogMode: "add"  // add | rename
    property var stackMenuIds: []
    property var stackMenuAnchor: null

    property real itemPressX: 0
    property real itemPressY: 0
    readonly property real itemDragThreshold: 6

    readonly property bool hasItemSelection: root.selectedItemIds.length > 0
    readonly property int itemSelectionCount: root.selectedItemIds.length
    readonly property bool canEditItem: root.itemSelectionCount === 1
                                        && (root.selectedItemType === "song"
                                            || root.selectedItemType === "divider")
    readonly property bool stacksActive: !!(root.session && root.session.stacks.all
                                            && root.session.stacks.all.length >= 2)
    readonly property int selectionLinkableCount: {
        const _ids = root.selectedItemIds
        const _focus = root.selectedItemId
        return root.selectionLinkableIds().length
    }
    readonly property bool canSetItemStack: root.serviceOpen && root.stacksActive
                                            && root.selectionLinkableCount > 0
    readonly property bool selectionIsLinked: {
        const model = root.session ? root.session.serviceItems : null
        const ids = root.selectionLinkableIds()
        return !!model && ids.length === 1 && model.linkedIds.indexOf(Number(ids[0])) >= 0
    }
    readonly property bool canLinkItems: root.serviceOpen && root.stacksActive
                                         && (root.selectionLinkableCount >= 2
                                             || (root.selectionLinkableCount === 1
                                                 && root.selectionIsLinked))
    readonly property bool serviceOpen: root.session && root.session.openServiceId >= 0

    /// ``error`` from a command: "" is done; anything else, the window shows.
    function report(error) {
        if (error && error.length)
            root.failed(error)
    }

    /// No service open again: the rundown empties and every stack clears,
    /// as they do when another service is opened.
    function closeService() {
        if (root.session)
            root.report(root.session.library.closeService())
    }

    function toolEditItem() {
        if (!root.canEditItem)
            return
        if (root.selectedItemType === "divider") {
            root.openDividerDialog("rename")
            return
        }
        root.songEditor().openItem(root.selectedItemId)
    }

    function toolDeleteItem() {
        if (!root.hasItemSelection || !root.session)
            return
        const err = root.session.library.removeServiceItems(root.selectedItemIds.slice())
        if (!err.length)
            root.itemSelection.clear()
        root.report(err)
    }

    function syncItemType() {
        const current = root.itemSelection.current
        if (current === null) {
            root.selectedItemType = ""
            return
        }
        const row = root.itemSelection.rowOfKey(current)
        if (row >= 0)
            root.selectedItemType = root.serviceItemTypeAtRow(row)
    }

    function serviceItemIdAtRow(row) {
        return Number(MultiSelect.modelValue(itemList.model, row, ServiceItemModel.ItemIdRole) || -1)
    }

    function serviceItemTypeAtRow(row) {
        return String(MultiSelect.modelValue(itemList.model, row, ServiceItemModel.ItemTypeRole) || "")
    }

    function beginItemMarqueeFrom(item, mx, my, additive) {
        itemList.dragFromIndex = -1
        itemList.dropInsertIndex = -1
        itemList.hideDragGhost()
        root.itemSelection.beginMarquee(item, mx, my, additive)
    }

    function openDividerDialog(mode) {
        dividerDialogMode = mode || "add"
        dividerDialog.ask(dividerDialogMode === "rename"
                          ? root.selectedItemTitle() : qsTr("Section"))
    }

    function selectedItemTitle() {
        if (!root.session || !root.session.serviceItems || root.selectedItemId < 0)
            return qsTr("Section")
        const model = root.session.serviceItems
        for (let i = 0; i < model.rowCount(); ++i) {
            if (root.serviceItemIdAtRow(i) === Number(root.selectedItemId))
                return String(MultiSelect.modelValue(model, i, ServiceItemModel.TitleRole)
                              || qsTr("Section"))
        }
        return qsTr("Section")
    }

    function submitDividerDialog() {
        if (!root.session)
            return
        const title = dividerDialog.trimmedValue || qsTr("Section")
        if (dividerDialogMode === "rename") {
            if (root.selectedItemId < 0)
                return
            root.report(root.session.library.renameServiceDivider(root.selectedItemId, title))
        } else {
            if (!root.serviceOpen)
                return
            root.session.library.addDividerToService(title)
        }
    }

    function stackName(id) {
        if (!root.session)
            return ""
        return Stacks.displayName(Stacks.findById(root.session.stacks.all, id)) || id || ""
    }

    function typeDefaultLabel(itemType) {
        if (!root.session)
            return qsTr("Type default")
        // resolvePlannedStack is a call, not a property: typeDefaults is read
        // as a value, so a binding on this label re-runs when the operator
        // repoints a type.
        if (!root.session.stacks.typeDefaults)
            return qsTr("Type default")
        const id = root.session.live.resolvePlannedStack(itemType || "song", "")
        const name = root.stackName(id)
        return name ? qsTr("Type default (%1)").arg(name) : qsTr("Type default")
    }

    function itemTypeForId(itemId) {
        const model = itemList.model
        if (!model)
            return ""
        for (let r = 0; r < model.rowCount(); ++r) {
            if (root.serviceItemIdAtRow(r) === Number(itemId))
                return root.serviceItemTypeAtRow(r)
        }
        return ""
    }

    function stackIdForItemId(itemId) {
        const model = itemList.model
        if (!model)
            return ""
        for (let r = 0; r < model.rowCount(); ++r) {
            if (root.serviceItemIdAtRow(r) === Number(itemId))
                return String(MultiSelect.modelValue(itemList.model, r, ServiceItemModel.StackIdRole) || "")
        }
        return ""
    }

    function selectionIds() {
        if (root.selectedItemIds.length)
            return root.selectedItemIds.slice()
        if (root.selectedItemId >= 0)
            return [root.selectedItemId]
        return []
    }

    function selectionLinkableIds() {
        const src = root.selectionIds()
        const out = []
        for (let i = 0; i < src.length; ++i) {
            const id = Number(src[i])
            if (id < 0)
                continue
            if (root.itemTypeForId(id) === "divider")
                continue
            out.push(id)
        }
        return out
    }

    function linkSelectedItems() {
        if (!root.session)
            return
        const ids = root.selectionLinkableIds()
        if (ids.length >= 2) {
            root.report(root.session.library.linkServiceItems(ids))
            return
        }
        if (ids.length === 1)
            root.report(root.session.library.unlinkServiceItem(ids[0]))
    }

    function selectionStackPin() {
        const ids = root.stackMenuIds
        if (!ids || !ids.length)
            return { mixed: false, pin: "" }
        let pin = null
        for (let i = 0; i < ids.length; ++i) {
            const sid = root.stackIdForItemId(ids[i])
            if (pin === null)
                pin = sid
            else if (pin !== sid)
                return { mixed: true, pin: "" }
        }
        return { mixed: false, pin: pin || "" }
    }

    function selectionTypeDefaultLabel() {
        const ids = root.stackMenuIds
        if (!ids || !ids.length)
            return qsTr("Type default")
        let typ = ""
        for (let i = 0; i < ids.length; ++i) {
            const t = root.itemTypeForId(ids[i])
            if (!typ)
                typ = t
            else if (typ !== t)
                return qsTr("Type default")
        }
        return root.typeDefaultLabel(typ)
    }

    function applyStackToMenuIds(stackId) {
        if (!root.session)
            return
        root.report(root.session.library.setServiceItemsStack(root.stackMenuIds || [], stackId))
    }

    function openStackMenu(anchor) {
        root.stackMenuIds = root.selectionLinkableIds()
        if (!root.stackMenuIds.length)
            return
        root.stackMenuAnchor = anchor
        if (stackMenu.opened)
            stackMenu.close()
        stackMenu.openAt()
    }

    /// A rundown glyph: ``kind`` is a content type (ContentTypes) or a glyph's
    /// own name. Rundown glyphs draw a lighter stroke.
    function iconUrl(kind, ink) {
        const type = ContentTypes.of(kind)
        return Icons.url(type ? type.icon : kind, ink, 2)
    }

    /// What iconUrl draws a row of ``itemType`` with; "" for nothing.
    function typeIconKind(itemType) {
        if (itemType === "image_folder")
            return "folder"
        if (itemType === "divider" || ContentTypes.of(itemType))
            return itemType
        return ""
    }

    property var expandedFolders: ({})

    // Library -> service drag-to-add.
    property bool libAddDragActive: false
    property var libAddPayload: null
    property string libAddLabel: ""
    property real libAddGrabX: 16
    property real libAddGrabY: 16

    function isFolderExpanded(itemId) {
        return !!root.expandedFolders[String(itemId)]
    }

    function toggleFolderExpanded(itemId) {
        const key = String(itemId)
        const next = Object.assign({}, root.expandedFolders)
        if (next[key])
            delete next[key]
        else
            next[key] = true
        root.expandedFolders = next
    }

    function beginLibAddDrag(payload, label, grabX, grabY) {
        if (!root.session || root.session.openServiceId < 0 || !payload)
            return false
        root.dropIncoming()
        root.libAddDragActive = true
        root.libAddPayload = payload
        root.libAddLabel = label || qsTr("Add to service")
        root.libAddGrabX = grabX !== undefined ? grabX : 16
        root.libAddGrabY = grabY !== undefined ? grabY : 16
        // Do not preview an insert line or ghost until the cursor is actually
        // tracked over the service list - otherwise a click in the library
        // flashes chrome at the top of the Service panel.
        itemList.dropInsertIndex = -1
        dragGhost.x = -10000
        dragGhost.y = -10000
        return true
    }

    /// Hit-test the service order without requiring an active library-add
    /// drag. Used so media tabs can arm the drop only when the pointer is
    /// actually over the service list (never while the panel is hidden).
    function serviceDropIndexAt(mapItem, mx, my) {
        if (!mapItem || !itemList.visible)
            return -1
        const p = mapItem.mapToItem(itemList, mx, my)
        if (p.x < -4 || p.y < -4 || p.x > itemList.width + 4 || p.y > itemList.height + 4)
            return -1
        const cy = mapItem.mapToItem(itemList.contentItem, mx, my).y
        return Reorder.slotInView(itemList, cy)
    }

    function libAddInsertAt(mapItem, mx, my) {
        if (!root.libAddDragActive)
            return -1
        return root.serviceDropIndexAt(mapItem, mx, my)
    }

    function updateLibAddDragFrom(mapItem, mx, my) {
        if (!root.libAddDragActive || !mapItem)
            return
        const at = root.libAddInsertAt(mapItem, mx, my)
        itemList.dropInsertIndex = at
        const gp = mapItem.mapToItem(dragGhost.parent, mx, my)
        dragGhost.x = gp.x - root.libAddGrabX
        dragGhost.y = gp.y - root.libAddGrabY
        if (at >= 0) {
            const viewY = mapItem.mapToItem(itemList, mx, my).y
            MultiSelect.autoScrollView(itemList, viewY, 28, 12)
        }
    }

    function cancelLibAddDrag() {
        root.libAddDragActive = false
        root.libAddPayload = null
        root.libAddLabel = ""
        itemList.dropInsertIndex = -1
        dragGhost.x = -10000
        dragGhost.y = -10000
    }

    /// Add each entry of ``list`` with ``add(entry, at)``, in order from ``at``
    /// (-1: the end). One entry may add several rows (a folder, a batch), so
    /// the next lands after however many the last one added.
    function addEach(list, at, add) {
        const rows = root.session.serviceItems
        let ok = false
        let insert = at
        for (let i = 0; i < list.length; ++i) {
            const before = rows ? rows.rowCount() : 0
            if (!add(list[i], insert))
                continue
            ok = true
            if (insert >= 0)
                insert += Math.max(1, (rows ? rows.rowCount() : before) - before)
        }
        return ok
    }

    function applyLibAddPayload(payload, atIndex) {
        if (!root.session || !payload || root.session.openServiceId < 0)
            return false
        const at = (atIndex === undefined || atIndex === null) ? -1 : atIndex
        const library = root.session.library
        const freeform = root.session.freeformLibrary
        const web = root.session.webLibrary
        const media = root.session.media
        const files = payload.filenames || []
        const folders = payload.folderIds || []
        switch (String(payload.kind || "")) {
        case "batch":
            return root.addEach(payload.items || [], at,
                                (item, i) => root.applyLibAddPayload(item, i))
        case "song":
        case "songs":
            return root.addEach((payload.songIds && payload.songIds.length)
                                ? payload.songIds : [payload.songId],
                                at, (id, i) => library.addSongToService(id, i))
        case "freeform":
            return freeform.addFreeformToService(payload.freeformId, at)
        case "freeform_items":
            return root.addEach(payload.freeformIds || [], at,
                                (id, i) => freeform.addFreeformToService(id, i))
        case "freeform_folder":
            return root.addEach(folders, at, (id, i) => freeform.addFreeformFolderToService(id, i))
        case "bible":
            return root.addEach((payload.ranges && payload.ranges.length)
                                ? payload.ranges
                                : [{ verseStart: payload.verseStart, verseEnd: payload.verseEnd }],
                                at, (r, i) => library.addBibleToService(r.verseStart, r.verseEnd, i))
        case "image":
            return root.addEach(files, at, (f, i) => media.addImageToService(f, i))
        case "image_folder":
            return root.addEach(folders, at, (id, i) => media.addImageFolderToService(id, i))
        case "video":
            return root.addEach(files, at, (f, i) => media.addVideoToService(f, i))
        case "video_folder":
            return root.addEach(folders, at, (id, i) => media.addVideoFolderToService(id, i))
        case "audio":
            return root.addEach(files, at, (f, i) => media.addAudioToService(f, i))
        case "audio_folder":
            return root.addEach(folders, at, (id, i) => media.addAudioFolderToService(id, i))
        case "web":
            return root.addEach(payload.pageIds || [], at, (id, i) => web.addPageToService(id, i))
        case "web_folder":
            return root.addEach(folders, at, (id, i) => web.addFolderToService(id, i))
        case "live":
            return root.addEach(payload.sources || [], at,
                                (s, i) => root.session.liveLibrary.addSourceToService(s.kind, s.name, i))
        }
        return false
    }

    /// Returns true when the drop landed on the service list and was applied.
    function finishLibAddDragFrom(mapItem, mx, my) {
        if (!root.libAddDragActive)
            return false
        const at = root.libAddInsertAt(mapItem, mx, my)
        const payload = root.libAddPayload
        root.cancelLibAddDrag()
        if (at < 0 || !payload)
            return false
        return root.applyLibAddPayload(payload, at)
    }

    function finishItemDrag() {
        const from = itemList.dragFromIndex
        let insertBefore = itemList.dropInsertIndex
        itemList.dragFromIndex = -1
        itemList.dropInsertIndex = -1
        itemList.hideDragGhost()
        if (!root.session || insertBefore < 0)
            return
        const n = itemList.count
        if (n <= 0)
            return
        const fromId = from >= 0 ? root.serviceItemIdAtRow(from) : -1
        const sel = root.selectedItemIds
        const moveGroup = sel.length > 1 && fromId >= 0 && MultiSelect.containsId(sel, fromId)
        const movingIds = []
        if (moveGroup) {
            for (let r = 0; r < n; ++r) {
                const id = root.serviceItemIdAtRow(r)
                if (MultiSelect.containsId(sel, id))
                    movingIds.push(id)
            }
        } else if (fromId >= 0) {
            movingIds.push(fromId)
        }
        if (!movingIds.length)
            return
        if (movingIds.length === 1) {
            const to = Reorder.landing(from, insertBefore, n)
            if (to >= 0)
                root.session.library.moveServiceItemTo(from, to)
            return
        }
        const movingSet = {}
        for (let i = 0; i < movingIds.length; ++i)
            movingSet[Number(movingIds[i])] = true
        const kept = []
        for (let r = 0; r < n; ++r) {
            const id = root.serviceItemIdAtRow(r)
            if (!movingSet[Number(id)])
                kept.push(id)
        }
        let dest = insertBefore
        for (let r = 0; r < insertBefore && r < n; ++r) {
            if (movingSet[Number(root.serviceItemIdAtRow(r))])
                dest -= 1
        }
        dest = Math.max(0, Math.min(kept.length, dest))
        const next = kept.slice(0, dest).concat(movingIds).concat(kept.slice(dest))
        if (next.length !== n)
            return
        let unchanged = true
        for (let i = 0; i < n; ++i) {
            if (Number(next[i]) !== Number(root.serviceItemIdAtRow(i))) {
                unchanged = false
                break
            }
        }
        if (unchanged)
            return
        root.session.library.reorderServiceItems(next)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ServiceToolbar {
            id: svcToolbar
            panel: root
            stackMenu: stackMenu
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Style.wellEdge
        }

        // Service items, in the panel's dark well: list rhythm + drag
        // reorder.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Style.wellBg

            ServiceItemList {
                id: itemList
                panel: root
                ghost: dragGhost
            }

            MouseArea {
                id: itemMarqueeLayer
                anchors.fill: parent
                z: 20
                hoverEnabled: false
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                onPressed: function (mouse) {
                    const p = mapToItem(itemList, mouse.x, mouse.y)
                    if (p.x > itemList.width - Style.scrollGutter(itemList)
                            && p.x <= itemList.width
                            && p.y >= 0 && p.y <= itemList.height) {
                        mouse.accepted = false
                        return
                    }
                    if (MultiSelect.isOverListRow(itemList, p.x, p.y)) {
                        mouse.accepted = false
                        return
                    }
                    root.interacted()
                    const toggle = MultiSelect.isToggle(mouse.modifiers)
                    if (!toggle)
                        root.itemSelection.clear()
                    root.beginItemMarqueeFrom(this, mouse.x, mouse.y, toggle)
                }
                onPositionChanged: function (mouse) {
                    if (!pressed || !root.itemSelection.marqueeActive)
                        return
                    root.itemSelection.updateMarquee(this, mouse.x, mouse.y)
                }
                onReleased: function (mouse) {
                    if (root.itemSelection.marqueeActive) {
                        root.itemSelection.updateMarquee(this, mouse.x, mouse.y)
                        root.itemSelection.endMarquee()
                    }
                }
                onCanceled: root.itemSelection.endMarquee()
                onWheel: function (wheel) { wheel.accepted = false }
            }
        }
    }

    // Floating drag ghost that follows the cursor. It sits on the window, so
    // a row dragged in from the Library shows over any panel.
    Rectangle {
        id: dragGhost
        parent: Window.window ? Window.window.contentItem : root
        visible: itemList.dragActive || root.libAddDragActive
        x: -10000
        y: -10000
        z: 2000
        width: itemList.width
        height: itemList.dragActive
                ? (itemList.dragIsDivider ? 26 : Style.listRowH)
                : Style.listRowH
        radius: Style.radiusSmall
        opacity: 0.92
        color: itemList.dragActive
               ? (itemList.dragIsDivider ? Style.dividerRow
                  : (itemList.dragIsLive ? Style.liveRow : Style.panelRowSel))
               : Style.panelRowSel
        border.width: 1
        border.color: Style.accent

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            radius: 1
            visible: itemList.dragActive ? !itemList.dragIsDivider : true
            color: itemList.dragActive
                   ? (itemList.dragIsLive ? Style.accent : Style.listAccentSel)
                   : Style.accent
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: itemList.dragActive && itemList.dragIsDivider ? 8 : 10
            anchors.rightMargin: 10
            spacing: 6

            Text {
                visible: itemList.dragActive && !itemList.dragIsDivider
                         && itemList.dragDisplayNumber > 0
                text: itemList.dragDisplayNumber + "."
                color: itemList.dragIsLive ? Style.textStrong : Style.textMuted
                font.pixelSize: Style.fontBody
            }
            Image {
                visible: itemList.dragActive && !itemList.dragIsDivider
                         && itemList.dragIconKind.length > 0
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                source: root.iconUrl(itemList.dragIconKind,
                                     itemList.dragIsLive ? Style.textStrong : Style.iconIdle)
                sourceSize: Qt.size(14, 14)
                smooth: true
                asynchronous: false
                cache: true
            }
            SectionTitle {
                visible: !root.libAddDragActive && itemList.dragActive && itemList.dragIsDivider
                Layout.fillWidth: true
                title: itemList.dragTitle
            }
            Text {
                visible: root.libAddDragActive || !(itemList.dragActive && itemList.dragIsDivider)
                Layout.fillWidth: true
                text: root.libAddDragActive ? root.libAddLabel : itemList.dragTitle
                color: itemList.dragActive && itemList.dragIsLive ? Style.textStrong : Style.text
                font.pixelSize: Style.fontUi
                elide: Text.ElideRight
            }
            Text {
                visible: itemList.dragActive && itemList.dragHasOverrides && !itemList.dragIsDivider
                text: "●"
                color: Style.overrideDot
                font.pixelSize: Style.fontMicro
            }
        }
    }

    // A service toolbar button. Pressing one ends a library search edit.

    // The Services window, a song's editor and the service's settings,
    // each made the first time it is asked for.
    function servicePicker(): ServicePickerDialog {
        return pickerLoader.load() as ServicePickerDialog
    }
    function songEditor(): SongEditDialog {
        return songEditLoader.load() as SongEditDialog
    }
    function serviceSettings(): ServiceSettingsDialog {
        return settingsLoader.load() as ServiceSettingsDialog
    }
    readonly property bool pickerOpen: !!pickerLoader.item && (pickerLoader.item as ServicePickerDialog).open

    LazyLoader {
        id: pickerLoader
        objectName: "ServicePickerDialog"
        sourceComponent: ServicePickerDialog {
            session: root.session
            onSettingsAsked: function (serviceId, isNew) {
                root.serviceSettings().openFor(serviceId, isNew)
            }
        }
    }

    LazyLoader {
        id: songEditLoader
        objectName: "SongEditDialog"
        sourceComponent: SongEditDialog {
            session: root.session
        }
    }

    LazyLoader {
        id: settingsLoader
        objectName: "ServiceSettingsDialog"
        sourceComponent: ServiceSettingsDialog {
            session: root.session
            onClosed: {
                if (pickerLoader.item)
                    (pickerLoader.item as ServicePickerDialog).settingsClosed()
            }
        }
    }

    AppPromptDialog {
        id: dividerDialog
        title: root.dividerDialogMode === "rename" ? qsTr("Rename divider")
                                                   : qsTr("Add divider")
        acceptLabel: root.dividerDialogMode === "rename" ? qsTr("Rename") : qsTr("Add")
        onAccepted: root.submitDividerDialog()
    }

    StackSendMenu {
        id: stackMenu
        session: root.session
        anchorItem: root.stackMenuAnchor
        alignRight: true
        mixed: root.selectionStackPin().mixed
        chosenId: root.selectionStackPin().pin
        defaultLabel: root.selectionTypeDefaultLabel()
        onPicked: function (stackId) { root.applyStackToMenuIds(stackId) }
    }
}
