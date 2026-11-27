pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// A panel's tab strip (underline style), a step under its title bar: the
// tabs choose what the panel shows, so they are part of its heading. The
// Library's and the Audio Mixer's. A click asks for a tab (``tabClicked``);
// the panel decides, and sets ``currentIndex``.
//
// Tabs that do not fit are on further pages, however narrow the panel gets: a
// ``›`` after a page's last tab slides the strip on, and a ``‹`` at its start
// slides back. The page follows the tab open.
Rectangle {
    id: strip
    /// One tab per title, in order.
    property list<string> titles
    /// The most tabs a page takes, however wide the strip; 0 for no limit.
    property int pageLimit: 0
    property int currentIndex: 0
    signal tabClicked(int index)

    readonly property real arrowW: 28
    readonly property real tabPad: 2
    /// The narrowest a tab gets with its title whole: all are as wide as the
    /// widest title in bold, the way the open tab shows it.
    readonly property real tabMinW: {
        let widest = 0
        for (let i = 0; i < strip.titles.length; ++i)
            widest = Math.max(widest, titleMetrics.advanceWidth(strip.titles[i]))
        return Math.ceil(widest) + 2 * strip.tabPad
    }

    /// Each tab's place: ``{page, x, width}``, x from the page's start. A page
    /// takes what fits beside its arrows, and at least one tab, which elides
    /// when even that does not fit; before the strip has a width, only the
    /// limit counts. No tab is wider than the first page's, so a few on the
    /// last do not stretch across the strip.
    readonly property var places: {
        const out = []
        const count = strip.titles.length
        const limit = strip.pageLimit > 0 ? strip.pageLimit : count
        const fit = room => strip.width > 0
                  ? Math.max(1, Math.min(limit, Math.floor(room / strip.tabMinW))) : limit
        let cellW = Infinity
        for (let page = 0; out.length < count; ++page) {
            const lead = page > 0 ? strip.arrowW : 0
            const room = Math.max(0, strip.width - lead)
            const left = count - out.length
            const last = fit(room) >= left
            const taken = last ? left : fit(room - strip.arrowW)
            cellW = Math.min(cellW, (room - (last ? 0 : strip.arrowW)) / taken)
            for (let i = 0; i < taken; ++i)
                out.push({ page: page, x: lead + i * cellW, width: cellW })
        }
        return out
    }
    readonly property int pageCount: strip.places.length > 0
                                     ? strip.places[strip.places.length - 1].page + 1 : 1

    /// The page tab ``index`` is on.
    function pageOf(index) {
        const place = strip.places[index]
        return place ? place.page : 0
    }

    /// The page showing.
    property int page: strip.pageOf(strip.currentIndex)
    // Set again whenever the tab or the fit changes, as the arrows' assignments
    // end the binding: the arrows turn the page without opening a tab.
    onCurrentIndexChanged: strip.page = strip.pageOf(strip.currentIndex)
    onPlacesChanged: strip.page = strip.pageOf(strip.currentIndex)

    /// ``page`` on its way there, so that a resize does not lag behind.
    property real slide: strip.page
    Behavior on slide {
        NumberAnimation {
            duration: 160
            easing.type: Easing.OutCubic
        }
    }

    implicitHeight: 26
    color: Style.libraryTabsBg
    clip: true

    FontMetrics {
        id: titleMetrics
        font.pixelSize: Style.fontSmall
        font.bold: true
    }

    Item {
        x: -strip.slide * strip.width
        width: strip.width * strip.pageCount
        height: strip.height

        Repeater {
            model: strip.titles
            delegate: TabCell {
                required property int index
                required property string modelData
                readonly property var place: strip.places[index] ?? { page: 0, x: 0, width: 0 }
                x: place.page * strip.width + place.x
                width: place.width
                height: strip.height
                enabled: place.page === strip.page
                title: modelData
                active: strip.currentIndex === index
                onClicked: strip.tabClicked(index)
            }
        }
    }

    // Over the tabs, which slide beneath them.
    ArrowCell {
        objectName: "moreTabsBack"
        visible: strip.page > 0
        anchors.left: parent.left
        width: strip.arrowW
        height: strip.height
        glyph: "chevronLeft"
        onClicked: strip.page = strip.page - 1
    }
    ArrowCell {
        objectName: "moreTabsArrow"
        visible: strip.page < strip.pageCount - 1
        anchors.right: parent.right
        width: strip.arrowW
        height: strip.height
        glyph: "chevronRight"
        onClicked: strip.page = strip.page + 1
    }

    component TabCell: Item {
        id: tabBtn
        property string title
        property bool active: false
        signal clicked()

        Rectangle {
            anchors.fill: parent
            color: tabHover.containsMouse && !tabBtn.active ? Style.tabHoverBg : "transparent"
        }
        Text {
            anchors.fill: parent
            anchors.leftMargin: strip.tabPad
            anchors.rightMargin: strip.tabPad
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: tabBtn.title
            color: tabBtn.active ? Style.text
                 : (tabHover.containsMouse ? Style.textHover : Style.textMuted)
            font.pixelSize: Style.fontSmall
            font.bold: tabBtn.active
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: tabBtn.active ? Style.accent : "transparent"
        }
        MouseArea {
            id: tabHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tabBtn.clicked()
        }
    }

    // ``›`` / ``‹``: no text, as the strip has no room for more.
    component ArrowCell: Rectangle {
        id: arrow
        property string glyph
        signal clicked()

        color: strip.color

        Rectangle {
            anchors.fill: parent
            color: arrowHover.containsMouse ? Style.tabHoverBg : "transparent"
        }
        Image {
            anchors.centerIn: parent
            width: 12
            height: 12
            source: Icons.url(arrow.glyph, arrowHover.containsMouse ? Style.text : Style.textMuted, 2)
            sourceSize: Qt.size(12, 12)
        }
        MouseArea {
            id: arrowHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: arrow.clicked()
        }
    }
}
