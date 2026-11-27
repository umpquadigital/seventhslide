pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A stream for the Live tab, new or edited: its address (SRT, RTMP, RTSP,
// HLS, ...) and the name it is listed and remembered by
// (session.liveLibrary).
AppDialog {
    id: root

    required property SessionFacade session

    /// The address of the stream edited; empty for a new one.
    property string oldUrl: ""
    property string name: ""
    property string url: ""

    /// What the address stands for, as it would be saved; empty when it is
    /// not a stream's.
    readonly property string savedUrl: root.session && root.url.trim().length
                                       ? root.session.liveLibrary.streamUrlFromText(root.url) : ""

    /// A stream saved: ``result`` is session.liveLibrary.saveStream's.
    signal saved(var result)

    function openNew() {
        root.oldUrl = ""
        root.name = ""
        root.url = ""
        root.open = true
    }

    function openFor(name, url) {
        root.oldUrl = url
        root.name = name
        root.url = url
        root.open = true
    }

    title: root.oldUrl.length ? qsTr("Edit stream") : qsTr("New stream")
    blurb: qsTr("A live stream to show on the outputs.")
    cardWidth: 500
    danger: false
    acceptLabel: root.oldUrl.length ? qsTr("Save") : qsTr("Add")
    acceptEnabled: root.savedUrl.length > 0
    onAccepted: root.saved(root.session.liveLibrary.saveStream(root.oldUrl, root.name, root.url))

    GroupList {
        Layout.fillWidth: true

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Address")
                DialogField {
                    objectName: "streamUrl"
                    implicitWidth: Style.fieldLongW
                    text: root.url
                    placeholder: qsTr("srt://host:9000")
                    onEdited: v => root.url = v
                    onSubmitted: root.submit()
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Name")
                hint: qsTr("The address when left empty.")
                DialogField {
                    objectName: "streamName"
                    implicitWidth: Style.fieldLongW
                    text: root.name
                    onEdited: v => root.name = v
                    onSubmitted: root.submit()
                }
            }
        }
    }

    // Why Add is off: an address that is not one.
    Text {
        Layout.fillWidth: true
        visible: root.url.trim().length > 0 && !root.savedUrl.length
        text: qsTr("Enter a stream address: srt://, rtmp://, rtsp://, rist://, udp://, rtp://, or http(s):// for HLS.")
        color: Style.textDanger
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }
}
