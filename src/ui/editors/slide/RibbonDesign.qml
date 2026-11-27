pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// The Design tab: a slide's background (Freeform); a theme's title slides and
// a background theme's fill.
Row {
    id: tab
    required property SlideEditState dialog
    readonly property SessionFacade session: tab.dialog.session
    readonly property var bg: tab.dialog.edit ? tab.dialog.edit.slides.background : ({})
    readonly property bool assetCopying: !!(tab.session && tab.session.themes.copyingAsset)
    readonly property int assetCopyPercent: tab.session
        ? Math.round(tab.session.themes.assetCopyProgress * 100) : 0
    spacing: 0
    height: parent ? parent.height : implicitHeight

    // A colour background just picked opens its picker under its swatch.
    Connections {
        target: tab.dialog
        function onSlideBgColorAsked() { Qt.callLater(slideBgSwatch.openPicker) }
    }

    // ---- a Freeform slide
    RibbonGroup {
        first: true
        visible: tab.dialog.isFreeform
        height: parent.height
        row1: [
            RibbonDropdown {
                objectName: "ribbonBackgroundKind"
                label: qsTr("Background")
                fieldWidth: 110
                // The slide's own background: a background theme is copied
                // in with From theme, never linked.
                options: [
                    { value: "none", label: qsTr("None") },
                    { value: "color", label: qsTr("Color") },
                    { value: "image", label: qsTr("Picture") },
                    { value: "video", label: qsTr("Video") }
                ]
                value: String(tab.bg.kind)
                onPicked: function (v) { tab.dialog.applySlideBgKind(v) }
            },
            RibbonSwatch {
                id: slideBgSwatch
                objectName: "ribbonBackgroundColor"
                visible: tab.bg.kind === "color"
                value: String(tab.bg.color || "")
                dialogTitle: qsTr("Slide background color")
                onEdited: function (v) { tab.dialog.report(tab.dialog.edit.slides.setBackground("color", v)) }
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonBackgroundTheme"
                icon: "themes"
                label: qsTr("From theme…")
                onClicked: tab.dialog.slideThemeAsked()
            },
            RibbonButton {
                objectName: "ribbonBackgroundPicture"
                visible: tab.bg.kind === "image"
                icon: "image"
                label: qsTr("Picture…")
                onClicked: tab.dialog.slideBgImageAsked()
            },
            RibbonButton {
                objectName: "ribbonBackgroundVideo"
                visible: tab.bg.kind === "video"
                icon: "video"
                label: qsTr("Video…")
                onClicked: tab.dialog.slideBgVideoAsked()
            }
        ]
    }
    // ---- a text theme: a song's title slides, in the song's contexts
    RibbonGroup {
        first: true
        visible: tab.dialog.isTextTheme
                 && (tab.dialog.themeContext === "song" || tab.dialog.themeContext === "titles")
        height: parent.height
        row1: [
            RibbonButton {
                objectName: "ribbonTitleSlides"
                icon: "titles"
                label: qsTr("Title slides")
                on: !!tab.dialog.setting("show_title_slides")
                onClicked: tab.dialog.setSetting("show_title_slides", !tab.dialog.setting("show_title_slides"))
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonTitleShowsLines"
                icon: "captions"
                label: qsTr("Title shows lines")
                enabled: !!tab.dialog.setting("show_title_slides")
                on: !!tab.dialog.setting("title_slide_show_lines")
                onClicked: tab.dialog.setSetting("title_slide_show_lines", !tab.dialog.setting("title_slide_show_lines"))
            }
        ]
    }

    // ---- a background theme
    RibbonGroup {
        first: true
        visible: tab.dialog.mode === "bg"
        height: parent.height
        row1: [
            RibbonDropdown {
                objectName: "ribbonBgType"
                label: qsTr("Fill")
                fieldWidth: 120
                options: [
                    { value: "transparent", label: qsTr("Transparent") },
                    { value: "color", label: qsTr("Color") },
                    { value: "image", label: qsTr("Picture") },
                    { value: "video", label: qsTr("Video") }
                ]
                value: String(tab.dialog.setting("background_type"))
                onPicked: function (v) { tab.dialog.setSetting("background_type", v) }
            },
            RibbonSwatch {
                objectName: "ribbonBgColor"
                visible: String(tab.dialog.setting("background_type")) === "color"
                value: String(tab.dialog.setting("background_color") || "")
                dialogTitle: qsTr("Background color")
                onEdited: function (v) { tab.dialog.setSetting("background_color", v) }
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonBgChoose"
                visible: String(tab.dialog.setting("background_type")) === "image"
                         || String(tab.dialog.setting("background_type")) === "video"
                icon: String(tab.dialog.setting("background_type")) === "video" ? "video" : "image"
                label: tab.assetCopying ? qsTr("Copying… %1%").arg(tab.assetCopyPercent) : qsTr("Choose…")
                enabled: !tab.assetCopying
                onClicked: {
                    const video = String(tab.dialog.setting("background_type")) === "video"
                    tab.dialog.assetAsked(video ? "bg_video" : "bg", video)
                }
            },
            RibbonButton {
                objectName: "ribbonBgClear"
                visible: String(tab.dialog.setting("background_type")) === "image"
                         || String(tab.dialog.setting("background_type")) === "video"
                icon: "close"
                label: qsTr("Clear")
                enabled: {
                    const video = String(tab.dialog.setting("background_type")) === "video"
                    return String(tab.dialog.setting(video ? "background_video" : "background_image") || "").length > 0
                }
                onClicked: {
                    const video = String(tab.dialog.setting("background_type")) === "video"
                    tab.dialog.report(tab.dialog.edit.theme.clearAsset(video ? "bg_video" : "bg"))
                }
            }
        ]
    }
}
