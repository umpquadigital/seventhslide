import QtQuick
import SeventhSlide

// The Settings dialog's Web Remote tab: whether the remote is on, its PIN and
// port. Saved with the dialog's Save, so they are only loaded and read back
// here.
QtObject {
    id: state

    property bool remoteOn: false
    property string remotePin: ""
    required property SessionFacade session
    property int remotePort: state.session.defaultRemotePort
    property RemoteController remote: null
    readonly property bool remoteAvailable: state.remote !== null

    /// The addresses a phone can open (one per network this computer is on),
    /// and the one the page shows, copies and draws as a QR code.
    readonly property var addresses: state.remoteAvailable && state.remote.running
                                     ? state.remote.joinUrls : []
    property int addressIndex: 0
    readonly property string address: state.addresses.length
                                      ? state.addresses[Math.min(state.addressIndex,
                                                                 state.addresses.length - 1)]
                                      : ""

    /// Take the stored values from app settings ``d``.
    function load(d) {
        state.remoteOn = !!d.remoteEnabled
        state.remotePin = d.remotePin || ""
        state.remotePort = d.remotePort
    }
}
