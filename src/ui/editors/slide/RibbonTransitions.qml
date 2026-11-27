pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// The Transitions tab: how the selection comes and goes, and the slide's
// background (Freeform); a text theme's context's own slide change (which
// is also how one of its items comes in), or a background theme's fade.
Row {
    id: tab
    required property SlideEditState dialog
    readonly property var bg: tab.dialog.edit ? tab.dialog.edit.slides.background : ({})
    /// What fades in place: an object, a background.
    readonly property var kinds: [
        { value: "cut", label: qsTr("Cut") },
        { value: "fade", label: qsTr("Fade") },
        { value: "fade_through_black", label: qsTr("Fade through black") }
    ]
    /// What a slide, or an item of a theme's content type, comes in by: the
    /// gallery.
    readonly property var gallery: [
        { value: "cut", label: qsTr("None") },
        { value: "fade", label: qsTr("Fade") },
        { value: "fade_through_black", label: qsTr("Black") },
        { value: "push", label: qsTr("Push") },
        { value: "wipe", label: qsTr("Wipe") },
        { value: "split", label: qsTr("Split") },
        { value: "zoom", label: qsTr("Zoom") },
        { value: "cover", label: qsTr("Cover") },
        { value: "reveal", label: qsTr("Reveal") }
    ]
    readonly property var directions: TransitionKinds.directions
    /// Whether ``type`` moves, and so has a side it comes from.
    function moves(type) {
        return TransitionKinds.moves(type)
    }
    spacing: 0
    height: parent ? parent.height : implicitHeight

    // ---- a Freeform slide: how it comes in (the first slide's is the
    // item's entrance), for this slide or every slide
    RibbonGroup {
        first: true
        visible: tab.dialog.isFreeform
        height: parent.height
        tall: [
            RibbonGallery {
                objectName: "ribbonSlideTransition"
                options: tab.gallery
                value: String(tab.bg.slide_transition_type)
                onPicked: function (v) {
                    tab.dialog.report(tab.dialog.edit.slides.setSlideTransition(
                                          v, Number(tab.bg.slide_transition_ms), false,
                                          String(tab.bg.slide_transition_direction)))
                }
            }
        ]
    }
    RibbonGroup {
        visible: tab.dialog.isFreeform
        height: parent.height
        row1: [
            RibbonField {
                objectName: "ribbonSlideTransitionMs"
                label: qsTr("Duration")
                unit: "s"
                displayScale: 0.001
                decimals: 2
                fieldWidth: 84
                step: 50
                minValue: 0
                maxValue: 10000
                enabled: String(tab.bg.slide_transition_type) !== "cut"
                value: Number(tab.bg.slide_transition_ms)
                onCommit: function (n) {
                    tab.dialog.report(tab.dialog.edit.slides.setSlideTransition(
                                          String(tab.bg.slide_transition_type), n, false,
                                          String(tab.bg.slide_transition_direction)))
                }
            }
        ]
        row2: [
            RibbonDropdown {
                objectName: "ribbonSlideDirection"
                label: qsTr("Direction")
                enabled: tab.moves(tab.bg.slide_transition_type)
                fieldWidth: 114
                options: tab.directions
                value: String(tab.bg.slide_transition_direction)
                onPicked: function (v) {
                    tab.dialog.report(tab.dialog.edit.slides.setSlideTransition(
                                          String(tab.bg.slide_transition_type),
                                          Number(tab.bg.slide_transition_ms), false, v))
                }
            }
        ]
    }
    RibbonGroup {
        visible: tab.dialog.isFreeform
        height: parent.height
        row1: [
            RibbonDropdown {
                objectName: "ribbonBackgroundTransition"
                label: qsTr("Background")
                fieldWidth: 120
                options: tab.kinds
                value: String(tab.bg.transition_type)
                onPicked: function (v) {
                    tab.dialog.report(tab.dialog.edit.slides.setBackgroundTransition(v, Number(tab.bg.transition_ms)))
                }
            }
        ]
        row2: [
            RibbonField {
                objectName: "ribbonBackgroundTransitionMs"
                label: qsTr("Duration")
                unit: "s"
                displayScale: 0.001
                decimals: 2
                fieldWidth: 84
                step: 50
                minValue: 0
                maxValue: 10000
                enabled: String(tab.bg.transition_type) !== "cut"
                value: Number(tab.bg.transition_ms)
                onCommit: function (n) {
                    tab.dialog.report(tab.dialog.edit.slides.setBackgroundTransition(String(tab.bg.transition_type), n))
                }
            }
        ]
    }
    RibbonGroup {
        visible: tab.dialog.isFreeform
        grow: true
        height: parent.height
        row1: [
            RibbonButton {
                objectName: "ribbonSlideTransitionAll"
                label: qsTr("Apply to all")
                onClicked: tab.dialog.report(tab.dialog.edit.slides.setSlideTransition(
                                                 String(tab.bg.slide_transition_type),
                                                 Number(tab.bg.slide_transition_ms), true,
                                                 String(tab.bg.slide_transition_direction)))
            }
        ]
    }
    RibbonGroup {
        visible: tab.dialog.isTextTheme
        first: true
        height: parent.height
        // A title slide is a song slide: it changes as the Song's.
        enabled: tab.dialog.themeContext !== "titles"
        tall: [
            RibbonGallery {
                objectName: "ribbonSlideChange"
                options: tab.gallery
                value: String(tab.dialog.contextTransition.type)
                onPicked: function (v) {
                    tab.dialog.edit.theme.setContextTransition(
                        v, Number(tab.dialog.contextTransition.ms),
                        String(tab.dialog.contextTransition.direction))
                }
            }
        ]
    }
    RibbonGroup {
        visible: tab.dialog.isTextTheme
        height: parent.height
        enabled: tab.dialog.themeContext !== "titles"
        row1: [
            RibbonField {
                objectName: "ribbonSlideChangeMs"
                label: qsTr("Duration")
                unit: "s"
                displayScale: 0.001
                decimals: 2
                fieldWidth: 84
                step: 50
                minValue: 0
                maxValue: 10000
                enabled: tab.dialog.contextTransition.type !== "cut"
                value: Number(tab.dialog.contextTransition.ms)
                onCommit: function (n) {
                    tab.dialog.edit.theme.setContextTransition(
                        String(tab.dialog.contextTransition.type), n,
                        String(tab.dialog.contextTransition.direction))
                }
            }
        ]
        row2: [
            RibbonDropdown {
                objectName: "ribbonSlideChangeDirection"
                label: qsTr("Direction")
                enabled: tab.moves(tab.dialog.contextTransition.type)
                fieldWidth: 120
                options: tab.directions
                value: String(tab.dialog.contextTransition.direction)
                onPicked: function (v) {
                    tab.dialog.edit.theme.setContextTransition(
                        String(tab.dialog.contextTransition.type),
                        Number(tab.dialog.contextTransition.ms), v)
                }
            }
        ]
    }
    // ---- an object's own fade in and out
    RibbonGroup {
        height: parent.height
        enabled: tab.dialog.hasSel
        row1: [
            RibbonDropdown {
                objectName: "ribbonObjectTransition"
                label: qsTr("Object")
                fieldWidth: 100
                options: tab.kinds
                value: String(tab.dialog.sel.transition_type)
                onPicked: function (v) {
                    tab.dialog.report(tab.dialog.edit.objects.setTransition(v, Number(tab.dialog.sel.transition_ms)))
                }
            }
        ]
        row2: [
            RibbonField {
                objectName: "ribbonObjectTransitionMs"
                label: qsTr("Duration")
                unit: "s"
                displayScale: 0.001
                decimals: 2
                fieldWidth: 80
                step: 50
                minValue: 0
                maxValue: 10000
                enabled: String(tab.dialog.sel.transition_type) !== "cut"
                value: Number(tab.dialog.sel.transition_ms)
                onCommit: function (n) {
                    tab.dialog.report(tab.dialog.edit.objects.setTransition(String(tab.dialog.sel.transition_type), n))
                }
            }
        ]
    }
    // A background theme's one fade; a text theme has no background of its own.
    RibbonGroup {
        visible: tab.dialog.isTheme && !tab.dialog.isTextTheme
        grow: true
        height: parent.height
        row1: [
            RibbonDropdown {
                objectName: "ribbonBgFade"
                label: qsTr("Background")
                fieldWidth: 100
                // A background theme's fade is stored as on / off.
                options: [tab.kinds[0], tab.kinds[1]]
                value: tab.dialog.setting("bg_enable_fade") ? "fade" : "cut"
                onPicked: function (v) { tab.dialog.setSetting("bg_enable_fade", v !== "cut") }
            }
        ]
        row2: [
            RibbonField {
                objectName: "ribbonBgFadeMs"
                label: qsTr("Duration")
                unit: "s"
                displayScale: 0.001
                decimals: 2
                fieldWidth: 84
                step: 50
                minValue: 0
                maxValue: 10000
                enabled: !!tab.dialog.setting("bg_enable_fade")
                value: Number(tab.dialog.setting("bg_fade_duration"))
                onCommit: function (n) { tab.dialog.setSetting("bg_fade_duration", n) }
            }
        ]
    }
}
