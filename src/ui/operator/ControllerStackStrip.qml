pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide
import "../js/Stacks.js" as Stacks

// The controller's stack tabs: one per stack with something live or cued, the
// focused one raised, scrolling sideways when they do not fit.
Item {
    id: stripWrap
    /// Untyped, since ControllerColumn makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var column // ControllerColumn

    readonly property int tabGap: 3
    /// Fixed, so the More label can change without shifting the row.
    readonly property int moreWidth: 78
    /// Longest name a tab shows before eliding.
    readonly property int nameCap: 132
    readonly property var stacks: stripWrap.column.session ? stripWrap.column.session.stacks.occupied : []

    /// Name width reserved at DemiBold, so the weight change on focus
    /// can't widen a tab and nudge its neighbours.
    function nameWidth(stack) {
        return Math.min(stripWrap.nameCap,
                        Math.ceil(tabMetrics.advanceWidth(Stacks.displayName(stack))))
    }

    /// 8px padding, 13px glyph, 6px gap, name, 8px padding.
    function tabWidth(stack) {
        return 35 + stripWrap.nameWidth(stack)
    }

    /// How many stacks, in order, get a tab of their own.
    readonly property int shownCount: {
        const widths = []
        for (let i = 0; i < stripWrap.stacks.length; ++i)
            widths.push(stripWrap.tabWidth(stripWrap.stacks[i]))
        let total = 0
        for (let i = 0; i < widths.length; ++i)
            total += widths[i] + (i > 0 ? stripWrap.tabGap : 0)
        if (total <= stripWrap.width)
            return widths.length
        const avail = stripWrap.width - stripWrap.moreWidth - stripWrap.tabGap
        let used = 0
        let count = 0
        while (count < widths.length) {
            const next = used + widths[count] + (count > 0 ? stripWrap.tabGap : 0)
            if (next > avail)
                break
            used = next
            count += 1
        }
        return count
    }

    /// Stacks behind "+N more", in stack order.
    readonly property var overflow: {
        const out = []
        for (let i = stripWrap.shownCount; i < stripWrap.stacks.length; ++i)
            out.push(stripWrap.stacks[i])
        return out
    }
    readonly property var focusedOverflow: stripWrap.column.session
            ? Stacks.findById(stripWrap.overflow, stripWrap.column.session.stacks.focusedId)
            : null
    readonly property bool overflowAdvancing: {
        for (let i = 0; i < stripWrap.overflow.length; ++i) {
            if (stripWrap.column.advancingStackIds.indexOf(stripWrap.overflow[i].id) >= 0)
                return true
        }
        return false
    }

    /// A 5px dot of ``ink`` on a disc of ``face``, the colour behind it.
    component StatusDot: Rectangle {
        id: dot
        property color face: Style.panelBg
        property color ink: Style.accent
        width: 8
        height: 8
        radius: Style.radius
        color: dot.face

        Rectangle {
            anchors.centerIn: parent
            width: 5
            height: 5
            radius: 2.5
            color: dot.ink
        }
    }

    Layout.fillWidth: true
    Layout.preferredWidth: 0
    Layout.preferredHeight: 26
    Layout.alignment: Qt.AlignVCenter
    clip: true

    onOverflowChanged: {
        if (stripWrap.overflow.length === 0)
            moreMenu.close()
    }

    FontMetrics {
        id: tabMetrics
        font.pixelSize: Style.fontSmall
        font.weight: Font.DemiBold
    }

    Row {
        height: parent.height
        spacing: stripWrap.tabGap

        Repeater {
            model: stripWrap.stacks

            delegate: Rectangle {
                id: stackTab
                required property var modelData
                required property int index
                readonly property bool active: !!(stripWrap.column.session
                        && stripWrap.column.session.stacks.focusedId === modelData.id)
                readonly property bool hovered: tabPress.containsMouse
                readonly property bool advancing:
                    stripWrap.column.advancingStackIds.indexOf(modelData.id) >= 0

                visible: stackTab.index < stripWrap.shownCount
                width: stripWrap.tabWidth(stackTab.modelData)
                height: 26
                radius: Style.btnRadius
                opacity: modelData.blank ? 0.55 : 1
                // Focused: neutral button face. Idle: bare until hovered.
                gradient: stackTab.active ? tabFace : null
                color: (!stackTab.active && stackTab.hovered)
                       ? Style.ghostHover : "transparent"
                border.width: 1
                border.color: stackTab.active ? Style.btnEdge : "transparent"

                Gradient {
                    id: tabFace
                    GradientStop { position: 0; color: Style.btnNeutralTop }
                    GradientStop { position: 1; color: Style.btnNeutralBot }
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    spacing: 6

                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        source: Icons.url(
                                      stackTab.modelData.icon,
                                      stackTab.active ? Style.textStrong
                                      : (stackTab.hovered ? Style.textHover : Style.textMuted), 2)
                        sourceSize: Qt.size(12, 12)
                        smooth: true
                    }

                    // Always shown, at the width reserved for DemiBold.
                    Text {
                        id: tabName
                        anchors.verticalCenter: parent.verticalCenter
                        width: stripWrap.nameWidth(stackTab.modelData)
                        text: Stacks.displayName(stackTab.modelData)
                        color: stackTab.active ? Style.textStrong
                               : (stackTab.hovered ? Style.textHover : Style.stackTabInk)
                        font.pixelSize: Style.fontSmall
                        font.weight: stackTab.active ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                }

                // Auto-advance dot; the backing disc matches the face behind it.
                StatusDot {
                    visible: stackTab.advancing
                    x: 0.5
                    y: 0.5
                    face: stackTab.active ? Style.btnNeutralTop : Style.panelBg
                }

                // Blank and freeze marks, in the transport's colours, in the
                // corner the auto-advance dot leaves free.
                Row {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 0.5
                    anchors.rightMargin: 0.5
                    spacing: 1

                    StatusDot {
                        visible: !!stackTab.modelData.blank
                        face: stackTab.active ? Style.btnNeutralTop : Style.panelBg
                        ink: Style.btnBlankOnBot
                    }
                    StatusDot {
                        visible: !!stackTab.modelData.frozen
                        face: stackTab.active ? Style.btnNeutralTop : Style.panelBg
                        ink: Style.btnFreezeOnBot
                    }
                }

                // Click focuses; Shift+click or middle-click clears.
                // Not Alt: most Linux window managers grab Alt+drag.
                MouseArea {
                    id: tabPress
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton

                    onClicked: function (mouse) {
                        if (!stripWrap.column.session)
                            return
                        stripWrap.column.interacted()
                        if (mouse.button === Qt.MiddleButton
                                || (mouse.modifiers & Qt.ShiftModifier))
                            stripWrap.column.session.stacks.clearStack(stackTab.modelData.id)
                        else
                            stripWrap.column.session.stacks.focusedId = stackTab.modelData.id
                    }
                }
            }
        }

        // "+N more" holds the stacks that didn't fit. Its label and width
        // never follow focus; while the focused stack is one of them it
        // takes the focused face and an accent underline instead.
        Rectangle {
            id: moreBtn
            readonly property bool lit: !!stripWrap.focusedOverflow
            readonly property bool hovered: morePress.containsMouse || moreMenu.visible
            readonly property color ink: moreBtn.lit ? Style.textStrong
                                          : (moreBtn.hovered ? Style.textHover : Style.glyphInk)

            visible: stripWrap.overflow.length > 0
            width: stripWrap.moreWidth
            height: 26
            radius: Style.btnRadius
            gradient: moreBtn.lit ? moreFace : null
            color: (!moreBtn.lit && moreBtn.hovered)
                   ? Style.ghostHover : "transparent"
            border.width: 1
            border.color: moreBtn.lit ? Style.btnEdge : "transparent"

            Gradient {
                id: moreFace
                GradientStop { position: 0; color: Style.btnNeutralTop }
                GradientStop { position: 1; color: Style.btnNeutralBot }
            }

            Row {
                anchors.centerIn: parent
                spacing: 5

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("+%1 more").arg(stripWrap.overflow.length)
                    color: moreBtn.ink
                    font.pixelSize: Style.fontSmall
                    font.weight: moreBtn.lit ? Font.DemiBold : Font.Normal
                }

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    height: 10
                    sourceSize: Qt.size(10, 10)
                    source: Icons.svgUrl(
                                '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 12 12">'
                                + '<path d="M2.5 4.5L6 8l3.5-3.5H2.5z" fill="'
                                + moreBtn.ink + '"/></svg>')
                }
            }

            Rectangle {
                visible: moreBtn.lit
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.bottomMargin: 2
                height: 2
                radius: 1
                color: Style.accent
            }

            // Auto-advance dot when a stack behind the menu is advancing.
            StatusDot {
                visible: stripWrap.overflowAdvancing
                x: 0.5
                y: 0.5
                face: moreBtn.lit ? Style.btnNeutralTop : Style.panelBg
            }

            MouseArea {
                id: morePress
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor

                onClicked: {
                    stripWrap.column.interacted()
                    if (moreMenu.opened)
                        moreMenu.close()
                    else
                        moreMenu.openAt()
                }
            }

            // The stacks the strip has no room for.
            AppMenu {
                id: moreMenu
                anchorItem: moreBtn
                alignRight: true

                Repeater {
                    model: stripWrap.overflow

                    delegate: StackMenuRow {
                        id: overflowRow
                        required property var modelData
                        label: Stacks.displayName(overflowRow.modelData)
                        chosen: !!stripWrap.column.session
                                && stripWrap.column.session.stacks.focusedId === overflowRow.modelData.id
                        advancing: stripWrap.column.advancingStackIds.indexOf(overflowRow.modelData.id) >= 0
                        blank: !!overflowRow.modelData.blank
                        frozen: !!overflowRow.modelData.frozen
                        cued: !!overflowRow.modelData.cued
                        iconSource: Icons.url(overflowRow.modelData.icon,
                                                     overflowRow.glyphInk, 2)
                        onClicked: {
                            moreMenu.close()
                            if (!stripWrap.column.session)
                                return
                            stripWrap.column.interacted()
                            stripWrap.column.session.stacks.focusedId = overflowRow.modelData.id
                        }
                    }
                }
            }
        }
    }
}
