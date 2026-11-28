pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide
import "../js/ThemeGallery.js" as ThemeGallery
import "../js/ThemeMap.js" as ThemeMap

// A scrolling gallery of theme cards: a search field fused with a filter
// button (tags, sort, defaults only), then same-size cards, each a preview over
// its name. The Output window's theme library pages and
// the theme picker both show this; ThemeGallery.js holds the browsing rules.
//
// The view owns how the owner's themes are browsed (query, sort, tag filter,
// its menus). The owner supplies the themes and the category defaults and owns
// the selection: a click emits ``picked``, a double click ``activated``, and
// ``selectedId`` is read back. With ``editable``, each card shows under the
// pointer the Song / Bible chips it does not hold, a click on which makes it
// that default (``defaultToggled``); the owner's tools act on the selection. With a ``title`` the view leads with a page
// header and holds its cards in a dark well, whose bar has the search and
// what is declared inside the view (the owner's New).
Item {
    id: view

    required property SessionFacade session
    /// ``session.themes.list(outputName, kind)``.
    property var themes: []
    /// ``session.themes.categoryDefaults(outputName)``: marks the defaults a
    /// card holds (``defaultSlots``) on it.
    property var defaults: ({})
    property string outputName: ""
    property string kind: "song" // one of session.themes.kinds
    property string selectedId: ""
    property bool editable: false

    /// Leading tiles that stand for "no theme here" (the picker's Inherit
    /// choices), each ``{id, label, caption}``: the id it picks, the words in
    /// its empty frame and its name. They lead the unfiltered list only.
    property var inheritTiles: []

    /// A page header over the gallery, or none (the picker).
    property string title: ""
    property string hint: ""

    default property alias tools: toolSlot.data

    signal picked(string themeId)
    signal activated(string themeId)
    /// Make ``themeId`` the default ``field`` of ``category``: one of
    /// ``defaultSlots``.
    signal defaultToggled(string category, string field, string themeId)
    /// Escape in the search field.
    signal searchEscaped()
    /// The view closed its menus. An owner with menus of its own closes them
    /// too, so only one gallery menu is ever open.
    signal menusClosed()

    // --- Browsing ---
    property string searchText: ""
    property string sortKey: "name" // name | added
    property bool sortAsc: true
    property bool defaultsOnly: false
    property string tagFilter: ""
    property string tagSearch: ""

    /// The output's canvas, width over height: the cards take its shape. Read
    /// again with the themes, which reload when the output is edited.
    readonly property real canvasAspect: {
        const reload = view.themes
        const s = view.session && view.outputName
                  ? view.session.outputSettings.getOutputSettings(view.outputName) : null
        return s && s.canvasWidth > 0 && s.canvasHeight > 0 ? s.canvasWidth / s.canvasHeight : 16 / 9
    }

    /// Titled: a page header, and the cards in a well (the Output window).
    readonly property bool welled: view.title.length > 0
    readonly property bool menuOpen: filterMenu.visible
    readonly property bool searchFocused: titleSearch.inputFocused || topSearch.inputFocused
                                          || tagSearchField.activeFocus
    readonly property bool filtered: view.searchText.trim().length > 0
                                     || view.tagFilter.length > 0
                                     || view.defaultsOnly
    /// The well bar's count: how many there are, or "1 of 3" while
    /// searching or filtering.
    readonly property string countText: view.filtered
            ? qsTr("%1 of %2").arg(view.matches.length).arg(view.themes.length)
            : String(view.themes.length)

    /// Search and filter cleared, scrolled to the top; sort is kept.
    function reset() {
        view.searchText = ""
        view.tagFilter = ""
        grid.contentY = 0
        view.closeMenus()
    }

    function closeMenus() {
        filterMenu.close()
        view.tagSearch = ""
        view.menusClosed()
    }

    function releaseSearch() {
        titleSearch.blur()
        topSearch.blur()
    }

    function toggleFilterMenu(field) {
        if (filterMenu.visible) {
            filterMenu.close()
            return
        }
        filterMenu.anchorItem = field.filterButton
        filterMenu.openAt()
    }

    function setTagFilter(tag) {
        view.tagFilter = String(tag || "")
        view.tagSearch = ""
        filterMenu.close()
    }

    // Each list is a property, computed once per change of what it reads and
    // shared by everything that shows it.
    /// The defaults a card of this kind can hold, in order (its chips):
    /// [{category, field, label}].
    readonly property var defaultSlots: view.session ? view.session.themes.defaultSlots(view.kind)
                                                     : []
    /// The theme each of ``defaultSlots`` names now ("" for none).
    readonly property var _defaultIds: view.defaultSlots.map(function (slot) {
        return String(((view.defaults[slot.category] || {})[slot.field]) || "")
    })
    readonly property var matches: ThemeGallery.filterAndSort(view.themes, {
        query: view.searchText,
        tagFilter: view.tagFilter,
        defaultsOnly: view.defaultsOnly,
        defaultIds: view._defaultIds,
        sortKey: view.sortKey,
        sortAsc: view.sortAsc,
        tagsOf: view.tagsOf
    })
    readonly property var items: (view.inheritTiles.length && !view.filtered)
                                 ? view.inheritTiles.map(function (tile) {
                                     return { inherit: true, id: String(tile.id || ""),
                                              label: String(tile.label || ""),
                                              caption: String(tile.caption || "") }
                                 }).concat(view.matches)
                                 : view.matches
    readonly property var tagCatalog: ThemeGallery.tagCatalog(view.themes, view.tagsOf)
    readonly property var tagMenuItems: ThemeGallery.tagMenuItems(view.tagCatalog, view.tagSearch,
                                                                  qsTr("All tags"))

    /// Tags for a card through the stored rule (theme::normalize_theme_tags).
    function tagsOf(theme) {
        if (!theme || !view.session)
            return []
        return Array.from(view.session.themes.normalizeTags(theme.tags))
    }

    function holdsDefault(category, themeId) {
        for (let i = 0; i < view.defaultSlots.length; ++i) {
            if (view.defaultSlots[i].category === category)
                return view._defaultIds[i] === themeId
        }
        return false
    }

    function thumbUrl(themeId) {
        if (!view.session)
            return ""
        return ThemeMap.thumbUrl(view.outputName, view.kind, themeId,
                                 view.session.themes.libraryRevision)
    }

    /// Scroll so the card for ``themeId`` shows (a new or duplicated theme).
    function reveal(themeId) {
        const i = view.items.findIndex(function (t) { return String(t.id || "") === themeId })
        if (i < 0)
            return
        const top = grid.cardTop(i)
        if (top < grid.contentY)
            grid.contentY = Math.max(0, top - grid.pad)
        else if (top + grid.cardH > grid.contentY + grid.height)
            grid.contentY = Math.min(top + grid.cardH + grid.pad - grid.height,
                                     Math.max(0, grid.contentHeight - grid.height))
    }

    onSelectedIdChanged: Qt.callLater(view.reveal, view.selectedId)

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            visible: view.welled
            Layout.fillWidth: true
            title: view.title
            text: view.hint
        }
        // No title (the picker): the search runs across the top.
        SearchFilterField {
            id: topSearch
            visible: view.title.length === 0
            Layout.fillWidth: true
            text: view.searchText
            filtering: view.tagFilter.length > 0 || view.defaultsOnly
            filterLabel: view.tagFilter
            onEdited: function (v) { view.searchText = v }
            onEscaped: view.searchEscaped()
            onFilterClicked: view.toggleFilterMenu(topSearch)
        }

        // Titled (the Output window's pages): the search and the owner's
        // tools over the well, as Settings' Stacks has its tools.
        RowLayout {
            visible: view.welled
            Layout.fillWidth: true
            spacing: 6

            SearchFilterField {
                id: titleSearch
                implicitWidth: 220
                text: view.searchText
                filtering: view.tagFilter.length > 0 || view.defaultsOnly
                filterLabel: view.tagFilter
                onEdited: function (v) { view.searchText = v }
                onEscaped: view.searchEscaped()
                onFilterClicked: view.toggleFilterMenu(titleSearch)
            }
            Item { Layout.fillWidth: true }
            Row {
                id: toolSlot
                spacing: 5
            }
        }

        // ---- Cards ----
        // Titled, the cards sit in a dark well whose bar is the height of
        // Settings' wells' bars: what they are, and how many; bare (the
        // picker), on the page under its search.
        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: view.welled ? -4 : 0
            color: view.welled ? Style.wellBg : "transparent"
            border.width: view.welled ? 1 : 0

            WellPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                showHeader: view.welled
                title: qsTr("Gallery")
                tools: Text {
                    text: view.countText
                    color: Style.textCaption
                    font.pixelSize: Style.fontSmall
                    font.features: { "tnum": 1 }
                }

                Flickable {
                    id: grid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: view.welled ? 6 : 0
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick
                    contentWidth: width - Style.scrollGutter(grid)
                    contentHeight: flow.implicitHeight + grid.pad * 2
                    ScrollBar.vertical: AppScrollBar {}

                    /// In the well, room round the cards (10 from its edge, with
                    /// the 6 inset); bare, room for their outline.
                    readonly property int pad: view.welled ? 4 : 1
                    readonly property int gap: 12
                    /// The narrowest a card gets before a column goes: in the
                    /// Output window's well, four across at its size.
                    readonly property int cardMin: view.welled ? 140 : 170
                    readonly property int innerW: Math.max(0, width - Style.scrollGutter(grid) - pad * 2)
                    readonly property int cols: Math.max(1, Math.floor((innerW + gap) / (cardMin + gap)))
                    readonly property int cellW: Math.floor((innerW - gap * (cols - 1)) / cols)
                    /// A card is a picture of the output: its shape.
                    readonly property int thumbH: Math.max(1, Math.round(cellW / view.canvasAspect))
                    /// The name, centred.
                    readonly property int footH: 34
                    readonly property int cardH: thumbH + footH

                    /// Where card ``i`` starts, worked out so it is known
                    /// before the Flow has placed it.
                    function cardTop(i) {
                        return grid.pad + Math.floor(i / grid.cols) * (grid.cardH + grid.gap)
                    }

                    Flow {
                        id: flow
                        x: grid.pad
                        y: grid.pad
                        width: grid.innerW
                        spacing: grid.gap

                        Repeater {
                            model: view.items
                            delegate: Rectangle {
                                id: card
                                required property var modelData
                                required property int index
                                readonly property bool isInherit: !!modelData.inherit
                                readonly property string themeId: String(modelData.id || "")
                                readonly property bool selected: card.themeId === view.selectedId
                                // A HoverHandler, not the MouseArea's containsMouse:
                                // a chip's MouseArea takes the hover from the card's,
                                // which hid the chip, gave the hover back, and so on.
                                readonly property bool hovered: cardHover.hovered

                                // A card asks for its picture once it is within a
                                // screen of the view, and keeps it: a long gallery
                                // paints what is looked at, what is looked at first.
                                readonly property bool nearView: {
                                    const top = grid.cardTop(card.index)
                                    return grid.cellW > 0
                                           && top < grid.contentY + grid.height * 2
                                           && top + grid.cardH > grid.contentY - grid.height
                                }
                                property bool wantsThumb: false
                                onNearViewChanged: if (card.nearView) card.wantsThumb = true
                                Component.onCompleted: card.wantsThumb = card.nearView

                                width: grid.cellW
                                height: grid.cardH
                                radius: Style.radiusLarge
                                color: Style.groupBg
                                clip: true

                                HoverHandler {
                                    id: cardHover
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: view.picked(card.themeId)
                                    onDoubleClicked: view.activated(card.themeId)
                                }

                                // Preview.
                                Rectangle {
                                    id: preview
                                    width: parent.width
                                    height: grid.thumbH
                                    color: card.isInherit ? Style.inheritWell : Style.thumbWellDark
                                    topLeftRadius: Style.radiusLarge
                                    topRightRadius: Style.radiusLarge
                                    Image {
                                        id: thumb
                                        objectName: "themeThumb"
                                        anchors.fill: parent
                                        visible: !card.isInherit
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                        sourceSize: Qt.size(Math.max(160, grid.cellW), Math.max(90, grid.thumbH))
                                        source: card.isInherit || !card.wantsThumb
                                                ? "" : view.thumbUrl(card.themeId)
                                    }
                                    Text {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        visible: card.isInherit
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                        text: card.isInherit ? card.modelData.label : ""
                                        color: Style.textCaption
                                        font.pixelSize: Style.fontSmall
                                    }
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: Style.groupEdge
                                    }
                                }

                                // The defaults it holds; under the pointer, the
                                // ones it could take, blue under the pointer as
                                // they would be.
                                Row {
                                    x: 6
                                    y: 6
                                    spacing: 4
                                    visible: !card.isInherit
                                    Repeater {
                                        model: view.defaultSlots
                                        delegate: StatusBadge {
                                            id: chip
                                            required property var modelData
                                            readonly property bool held: view.holdsDefault(chip.modelData.category, card.themeId)
                                            visible: chip.held || (view.editable && card.hovered)
                                            tone: chip.held || chipArea.containsMouse ? "accent" : "ghost"
                                            text: chip.modelData.label
                                            MouseArea {
                                                id: chipArea
                                                anchors.fill: parent
                                                enabled: view.editable && !chip.held
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: view.defaultToggled(chip.modelData.category,
                                                                               chip.modelData.field,
                                                                               card.themeId)
                                            }
                                        }
                                    }
                                }

                                // The name, centred in the footer.
                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: preview.bottom
                                    anchors.bottom: parent.bottom
                                    leftPadding: 10
                                    rightPadding: 10
                                    verticalAlignment: Text.AlignVCenter
                                    text: card.isInherit ? card.modelData.caption
                                                         : (card.modelData.name || qsTr("Untitled"))
                                    color: card.selected ? Style.textStrong : Style.textBright
                                    font.pixelSize: Style.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: card.radius
                                    color: "transparent"
                                    border.width: card.selected ? 2 : 1
                                    border.color: card.selected ? Style.edgeSelected
                                                : (card.hovered ? Style.edgeHoverAccent : Style.groupEdge)
                                }
                            }
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        visible: view.items.length === 0
                        spacing: 6
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: view.filtered ? qsTr("No matches") : qsTr("No themes")
                            color: Style.text
                            font.pixelSize: Style.fontUi
                            font.bold: true
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: view.filtered
                            text: qsTr("Nothing matches the search or filter.")
                            color: Style.textCaption
                            font.pixelSize: Style.fontBody
                        }
                    }
                }
            }
        }
    }

    // Tags, sort and defaults only, from the filter button.
    AppMenu {
        id: filterMenu
        alignRight: true
        minWidth: 220
        onClosed: view.tagSearch = ""

        MenuHeading { text: qsTr("Tags") }
        Item {
            visible: view.tagCatalog.length > 8
            width: parent ? parent.width : 0
            height: 34
            DialogField {
                id: tagSearchField
                anchors.fill: parent
                anchors.margins: 4
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                placeholder: qsTr("Find a tag…")
                text: view.tagSearch
                onEdited: function (v) { view.tagSearch = v }
            }
        }
        Repeater {
            model: view.tagMenuItems
            delegate: MenuItemRow {
                required property var modelData
                checkable: true
                checked: modelData.tag === view.tagFilter
                label: modelData.label
                onTriggered: view.setTagFilter(modelData.tag)
            }
        }
        MenuItemRow {
            visible: view.tagMenuItems.length === 0
            enabled: false
            label: qsTr("No matching tags")
        }
        MenuRule {}
        MenuHeading { text: qsTr("Sort") }
        Repeater {
            model: [
                { key: "name", asc: true, label: qsTr("Name, A to Z") },
                { key: "name", asc: false, label: qsTr("Name, Z to A") },
                { key: "added", asc: false, label: qsTr("Newest first") },
                { key: "added", asc: true, label: qsTr("Oldest first") }
            ]
            delegate: MenuItemRow {
                required property var modelData
                checkable: true
                checked: view.sortKey === modelData.key && view.sortAsc === modelData.asc
                label: modelData.label
                onTriggered: {
                    view.sortKey = modelData.key
                    view.sortAsc = modelData.asc
                    filterMenu.close()
                }
            }
        }
        MenuRule {}
        MenuItemRow {
            checkable: true
            checked: view.defaultsOnly
            label: qsTr("Defaults only")
            onTriggered: {
                view.defaultsOnly = !view.defaultsOnly
                filterMenu.close()
            }
        }
    }
}
