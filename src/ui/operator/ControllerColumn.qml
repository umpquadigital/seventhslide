pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide
import "../js/Stacks.js" as Stacks

// The Controller: the focused stack's tabs and options over its well. A stack
// with Preview on shows two wells side by side, its Preview (what is cued)
// and its Program (what is live), with Transition between them (its menu
// takes by a Cut or a Fade instead). The arrow keys drive the Program's well until the Preview's
// is worked; a take, or working the Program's, gives them back.
Rectangle {
    id: root
    color: Style.panelBg

    required property SessionFacade session
    signal interacted()

    /// The least width the Controller asks of the dock, which keeps the
    /// larger of this and its own least (enough for one well): two wells and
    /// what is between them need more. The panels beside it give way.
    readonly property int minimumWidth: root.previewOn
                                        ? 2 * root.wellMinWidth + root.takeWidth
                                          + 2 * wellRow.spacing + 2 * Style.pad
                                        : 0
    readonly property int wellMinWidth: 300
    readonly property int takeWidth: 120

    /// Keep the live line centered while the map advances (web auto-scroll,
    /// auto-advance, or a line change from the web remote).
    property bool autoScroll: true
    /// Bumped on autoAdvanceChanged so bindings calling advanceState() re-evaluate.
    property int advanceRev: 0

    readonly property string focusedAdvanceId: session ? (session.stacks.focusedId || "") : ""
    readonly property var focusedAdvance: root.advanceState(root.focusedAdvanceId)
    readonly property bool autoAdvance: !!(focusedAdvance && focusedAdvance.enabled)
    readonly property int autoAdvanceIntervalSec: focusedAdvance ? focusedAdvance.intervalSec : 0
    readonly property bool autoAdvanceLoop: !!(focusedAdvance && focusedAdvance.loop)
    readonly property bool autoAdvanceThrough: !!(focusedAdvance && focusedAdvance.through)
    readonly property bool autoAdvanceStopAtDividers: !!(focusedAdvance && focusedAdvance.stopAtDividers)

    readonly property var advancingStackIds: session ? session.stacks.advancingIds : []

    /// The stack strip only exists once a second stack is occupied; the title
    /// column grows a subtitle at the same moment, so both read one flag.
    readonly property bool stackTabsVisible: !!(session && session.stacks.showTabs)

    /// The focused stack's entry (``stacks.occupied``), or null while
    /// nothing is live or cued on it.
    readonly property var focusedEntry: session
            ? Stacks.findById(session.stacks.occupied, session.stacks.focusedId) : null
    /// The focused stack has a Preview: two wells.
    readonly property bool previewOn: {
        if (!session)
            return false
        const stack = Stacks.findById(session.stacks.all, session.stacks.focusedId)
        return !!(stack && stack.preview)
    }
    /// Something is cued to take.
    readonly property bool canTake: root.previewOn && !!root.session && root.session.preview.occupied
    /// Which well the arrow keys drive: "program" or "preview".
    property string keyedSide: "program"
    /// The well they drive: the Preview's once it is worked, while it holds
    /// something to step through; otherwise what is live.
    readonly property var keyedWell: root.canTake && root.keyedSide === "preview"
                                     ? previewWell : programWell

    /// Take what is cued live, by the theme's transition (Transition, T).
    function take() {
        if (!root.canTake)
            return false
        root.keyedSide = "program"
        return root.session.stacks.transition(root.session.stacks.focusedId).length === 0
    }
    /// The same by a transition of its own, ``entry`` ({type, durationMs}):
    /// Transition's menu's Cut or Fade.
    function takeWith(entry) {
        if (!root.canTake)
            return false
        root.keyedSide = "program"
        return root.session.stacks.transitionWith(root.session.stacks.focusedId, entry.type,
                                                  entry.durationMs, "", "").length === 0
    }

    // Another stack: the keys go back to what is live.
    onFocusedAdvanceIdChanged: root.keyedSide = "program"

    /// A stack's auto-advance settings, every field (``autoAdvanceFor``
    /// fills in the defaults for a stack with none of its own).
    function advanceState(stackId) {
        root.advanceRev
        return root.session ? root.session.stacks.autoAdvanceFor(String(stackId || "")) : null
    }

    /// Blank (or freeze) the stack the column shows, on every output:
    /// Shift+B / Shift+F, as the Program well's Blank and Freeze.
    function toggleStackBlank() {
        programWell.toggleBlank()
    }
    function toggleStackFreeze() {
        programWell.toggleStackFreeze()
    }

    /// Merge ``fields`` into the focused stack's auto-advance settings.
    function patchFocusedAdvance(fields) {
        const id = root.focusedAdvanceId
        if (!id || !root.session)
            return
        const next = Object.assign({}, root.advanceState(id), fields)
        root.session.stacks.setAutoAdvance(id, !!next.enabled, next.intervalSec,
                                    !!next.loop, !!next.through, !!next.stopAtDividers)
    }

    /// Scroll the wells to their current lines (the options' Follow live line).
    function followLiveAfterNav(animated) {
        programWell.followLiveAfterNav(animated)
        if (root.previewOn)
            previewWell.followLiveAfterNav(animated)
    }

    /// The arrows and Page keys: a step through the keyed well.
    function navigateNext() {
        return root.keyedWell.navigateNext()
    }
    function navigatePrev() {
        return root.keyedWell.navigatePrev()
    }

    /// Give up the keys: the address bars show where the pages are again.
    function clearFieldFocus() {
        programWell.clearFieldFocus()
        previewWell.clearFieldFocus()
    }

    Connections {
        target: root.session ? root.session.stacks : null

        function onAutoAdvanceChanged() {
            root.advanceRev += 1
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: 10

        RowLayout {
            id: ctrlHeader
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            spacing: 8

            // Sized to its text; the stack strip takes the slack.
            Text {
                id: ctrlTitle
                text: qsTr("Controller")
                color: Style.textBright
                font.pixelSize: Style.fontUi
                font.bold: true
                Layout.rightMargin: 6
                Layout.alignment: Qt.AlignVCenter
            }

            // Stack tabs: every tab shows its icon and name. Tabs fill the strip in
            // stack order and the stacks that don't fit go behind "+N more". The fit
            // reads width and order only, never focus, so focusing a tab restyles
            // it without moving anything.
            ControllerStackStrip {
                column: root
            }

            AppButton {
                id: advanceChip
                size: "compact"
                label: qsTr("Auto")
                iconSource: Icons.url("advance", ink)
                toggled: root.autoAdvance
                enabled: !!root.session && root.focusedAdvanceId.length > 0
                Layout.alignment: Qt.AlignVCenter
                onPressed: root.interacted()
                onClicked: root.patchFocusedAdvance({ enabled: !root.autoAdvance })
            }

            StackTargetButton {
                session: root.session
                Layout.alignment: Qt.AlignVCenter
            }

            // Options menu: follow live line and auto-advance timing.
            AppButton {
                id: optsBtn
                size: "compact"
                z: 20
                Layout.alignment: Qt.AlignVCenter
                toggled: ctrlMenu.visible
                iconSource: Icons.url("more", ink)
                onPressed: root.interacted()
                onClicked: {
                    if (ctrlMenu.opened)
                        ctrlMenu.close()
                    else
                        ctrlMenu.openAt()
                }

                ControllerOptionsMenu {
                    id: ctrlMenu
                    objectName: "controllerOptionsMenu"
                    column: root
                    button: optsBtn
                }
            }
        }

        // What is live needs files still on their way here.
        ControllerArrivalNote {
            Layout.fillWidth: true
            session: root.session
        }

        RowLayout {
            id: wellRow
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            ControllerWell {
                id: previewWell
                objectName: "previewWell"
                visible: root.previewOn
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 0
                Layout.minimumWidth: root.wellMinWidth
                session: root.session
                view: root.session ? root.session.preview : null
                stackEntry: root.focusedEntry
                autoScroll: root.autoScroll
                keyed: root.keyedWell === previewWell
                onInteracted: {
                    root.keyedSide = "preview"
                    root.interacted()
                }
            }

            ControllerTakeColumn {
                visible: root.previewOn
                Layout.preferredWidth: root.takeWidth
                Layout.fillHeight: true
                column: root
            }

            ControllerWell {
                id: programWell
                objectName: "programWell"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 0
                Layout.minimumWidth: root.previewOn ? root.wellMinWidth : 0
                session: root.session
                view: root.session ? root.session.program : null
                stackEntry: root.focusedEntry
                autoScroll: root.autoScroll
                keyed: root.keyedWell === programWell
                onInteracted: {
                    root.keyedSide = "program"
                    root.interacted()
                }
            }
        }
    }
}
