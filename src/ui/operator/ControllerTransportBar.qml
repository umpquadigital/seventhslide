pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The controller's transport buttons: previous and next, and Blank and
// Freeze of the stack the well shows (a Preview's well: Blank and Clear of
// what is cued); play, pause and restart while media is there; the page
// before and reload while a web page is.
RowLayout {
    id: bar
    /// Untyped, since ControllerWell makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var well // ControllerWell
    spacing: 8
    Layout.fillWidth: true

    /// One of its buttons is narrower than its face asks (the wells side by
    /// side): every one closes its arrow up to its word, so Previous and
    /// Next keep one gap.
    readonly property bool squeezed: {
        for (let i = 0; i < buttons.count; ++i) {
            const button = buttons.itemAt(i) as AppButton
            if (button && button.squeezed)
                return true
        }
        return false
    }

    Repeater {
        id: buttons
        model: bar.well.mediaControls ? bar.well.mediaTransport
             : bar.well.webControls ? bar.well.webTransport
             : bar.well.slideTransport
        delegate: AppButton {
            id: transportBtn
            required property string modelData
            readonly property string action: modelData

            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.minimumWidth: 0
            Layout.preferredHeight: 38
            size: "command"
            label: {
                switch (transportBtn.action) {
                case "prev": return qsTr("Previous")
                case "next": return qsTr("Next")
                case "blank": return qsTr("Blank")
                case "freeze": return qsTr("Freeze")
                case "clear": return qsTr("Clear")
                case "mediaRestart": return qsTr("Restart")
                case "webReload": return qsTr("Reload")
                case "webBack": return qsTr("Previous")
                default:
                    return bar.well.ready && bar.well.view.mediaPlaying ? qsTr("Pause")
                                                                    : qsTr("Play")
                }
            }
            // The steps and the player's own buttons, drawn as the web
            // remote's are: the way it goes beside the word.
            readonly property string glyph: {
                switch (transportBtn.action) {
                case "prev":
                case "webBack":
                    return "chevronLeft"
                case "next":
                    return "chevronRight"
                case "mediaRestart":
                    return "reset"
                case "webReload":
                    return "reload"
                case "mediaToggle":
                    return bar.well.ready && bar.well.view.mediaPlaying ? "pause"
                                                                                       : "play"
                default:
                    return ""
                }
            }
            iconSource: glyph.length ? Icons.url(glyph, ink) : ""
            iconAfter: transportBtn.action === "next"
            closeUp: bar.squeezed
            readonly property bool stackToggle: transportBtn.action === "blank"
                                                || transportBtn.action === "freeze"
            primary: !transportBtn.stackToggle && transportBtn.action !== "clear"
            toggled: {
                if (transportBtn.action === "blank")
                    return bar.well.blanked
                const stack = bar.well.stackEntry
                return transportBtn.action === "freeze" && !!(stack && stack.live && stack.frozen)
            }
            toggledKind: transportBtn.stackToggle ? transportBtn.action : ""
            // Previous goes back a page: there is one only after a link.
            // Blank needs something on the well's side, Freeze a live stack,
            // and Clear something cued.
            enabled: bar.well.ready
                     && (transportBtn.action !== "webBack" || bar.well.view.webCanGoBack)
                     && (transportBtn.action !== "blank" || bar.well.canBlank)
                     && (transportBtn.action !== "freeze"
                         || !!(bar.well.stackEntry && bar.well.stackEntry.live))
                     && (transportBtn.action !== "clear" || bar.well.view.occupied)
            objectName: "transport_" + transportBtn.action
            onPressed: bar.well.interacted()
            onClicked: {
                switch (transportBtn.action) {
                case "prev":
                    bar.well.navigatePrev()
                    break
                case "next":
                    bar.well.navigateNext()
                    break
                case "blank":
                    bar.well.toggleBlank()
                    break
                case "freeze":
                    bar.well.toggleStackFreeze()
                    break
                case "clear":
                    bar.well.clearCue()
                    break
                case "mediaToggle":
                    if (bar.well.view.mediaPlaying)
                        bar.well.view.mediaPause()
                    else
                        bar.well.view.mediaPlay()
                    break
                case "mediaRestart":
                    bar.well.view.mediaRestart()
                    break
                case "webReload":
                    bar.well.view.reloadWeb()
                    break
                case "webBack":
                    bar.well.view.webGoBack()
                    break
                }
            }
        }
    }
}
