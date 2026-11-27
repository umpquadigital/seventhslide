pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A web page for the Browser tab, new or edited: its name, its address, the
// size it is drawn at and its zoom (session.webLibrary). The page is drawn at
// that size whatever size it is shown at, as an image is.
AppDialog {
    id: root

    required property SessionFacade session

    /// The page edited; -1 for a new one.
    property var pageId: -1
    property string name: ""
    property string url: ""
    property int pageWidth: 1920
    property int pageHeight: 1080
    /// Percent.
    property int zoom: 100
    /// The size isn't one of the choices, so it is typed in.
    property bool customSize: false

    /// What the address stands for, as it would be saved; empty when it is
    /// not one a page can show.
    readonly property string savedUrl: root.session && root.url.trim().length
                                       ? root.session.webLibrary.urlFromText(root.url) : ""
    readonly property int minSide: root.session ? root.session.webLibrary.minSide : 1
    readonly property int maxSide: root.session ? root.session.webLibrary.maxSide : 1
    readonly property bool sizeOk: root.pageWidth >= root.minSide && root.pageWidth <= root.maxSide
                                   && root.pageHeight >= root.minSide
                                   && root.pageHeight <= root.maxSide
    /// Larger than 4K: allowed, with a word about what it costs.
    readonly property bool sizeHeavy: root.sizeOk && !!root.session
                                      && root.pageWidth * root.pageHeight
                                         > root.session.webLibrary.heavyPixels

    readonly property var sizes: [{ w: 1920, h: 1080 }, { w: 1280, h: 720 }]

    /// A page saved: ``result`` is session.webLibrary.savePage's.
    signal saved(var result)

    function openNew() {
        root.pageId = -1
        root.name = ""
        root.url = ""
        root.pageWidth = 1920
        root.pageHeight = 1080
        root.zoom = 100
        root.customSize = false
        root.open = true
    }

    /// Open on ``page`` (session.webLibrary.page's map).
    function openFor(page) {
        if (!page || page.id === undefined)
            return
        root.pageId = page.id
        root.name = page.name
        root.url = page.url
        root.pageWidth = page.width
        root.pageHeight = page.height
        root.zoom = Math.round(page.zoom * 100)
        root.customSize = !root.sizes.some(s => s.w === page.width && s.h === page.height)
        root.open = true
    }

    function sizeOptions() {
        const out = root.sizes.map(s => ({ value: s.w + "x" + s.h,
                                           label: qsTr("%1 × %2").arg(s.w).arg(s.h) }))
        out.push({ value: "custom", label: qsTr("Custom…") })
        return out
    }

    function zoomOptions() {
        const out = [50, 67, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300]
                    .map(z => ({ value: z, label: qsTr("%1%").arg(z) }))
        if (!out.some(o => o.value === root.zoom))
            out.push({ value: root.zoom, label: qsTr("%1%").arg(root.zoom) })
        return out.sort((a, b) => a.value - b.value)
    }

    title: root.pageId >= 0 ? qsTr("Edit web page") : qsTr("New web page")
    blurb: qsTr("A web page to show on the outputs. It is drawn at the size below, and fitted to wherever it shows.")
    cardWidth: 500
    danger: false
    acceptLabel: root.pageId >= 0 ? qsTr("Save") : qsTr("Add")
    acceptEnabled: root.savedUrl.length > 0 && root.sizeOk
    onAccepted: root.saved(root.session.webLibrary.savePage(root.pageId, {
        name: root.name,
        url: root.url,
        width: root.pageWidth,
        height: root.pageHeight,
        zoom: root.zoom / 100
    }))

    GroupList {
        Layout.fillWidth: true

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Address")
                DialogField {
                    objectName: "webPageUrl"
                    implicitWidth: Style.fieldLongW
                    text: root.url
                    placeholder: qsTr("https://")
                    onEdited: v => root.url = v
                    onSubmitted: root.submit()
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Name")
                DialogField {
                    objectName: "webPageName"
                    implicitWidth: Style.fieldLongW
                    text: root.name
                    onEdited: v => root.name = v
                    onSubmitted: root.submit()
                }
            }
        }
        SettingRow {
            label: qsTr("Size")
            OptionField {
                options: root.sizeOptions()
                value: root.customSize ? "custom" : root.pageWidth + "x" + root.pageHeight
                onPicked: function (v) {
                    if (v === "custom") {
                        root.customSize = true
                        return
                    }
                    const [w, h] = v.split("x").map(Number)
                    root.customSize = false
                    root.pageWidth = w
                    root.pageHeight = h
                }
            }
        }
        GroupRow {
            visible: root.customSize
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Width & Height")
                hint: root.sizeOk ? "" : qsTr("Each from %1 to %2.").arg(root.minSide).arg(root.maxSide)
                RowLayout {
                    spacing: 6
                    DialogField {
                        implicitWidth: 64
                        text: String(root.pageWidth)
                        onEdited: v => root.pageWidth = parseInt(v, 10) || 0
                    }
                    Text {
                        text: "×"
                        color: Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                    DialogField {
                        implicitWidth: 64
                        text: String(root.pageHeight)
                        onEdited: v => root.pageHeight = parseInt(v, 10) || 0
                    }
                    Text {
                        text: qsTr("px")
                        color: Style.textCaption
                        font.pixelSize: Style.fontSmall
                    }
                }
            }
        }
        SettingRow {
            label: qsTr("Zoom")
            OptionField {
                options: root.zoomOptions()
                value: root.zoom
                onPicked: v => root.zoom = v
            }
        }
    }

    // A page larger than 4K is drawn, at a price.
    Text {
        objectName: "webPageHeavy"
        Layout.fillWidth: true
        visible: root.sizeHeavy
        text: qsTr("Pages larger than 4K (3840 × 2160) take a lot of computing power to draw, and may slow the outputs down.")
        color: Style.textWarning
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }

    // Why Add is off: an address that is not one.
    Text {
        Layout.fillWidth: true
        visible: root.url.trim().length > 0 && !root.savedUrl.length
        text: qsTr("Enter a web address, such as example.com or https://example.com/page, or a file's path.")
        color: Style.textDanger
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }
}
