import QtQuick
import SeventhSlide

// Shared operator button. Variants: Neutral (default), Ghost, Primary, Danger.
// Sizes: compact (24, bars), control (26, beside a field), standard (28,
// window footers), command (36, the transport). Toggled-on reuses Primary
// chrome unless toggledKind is blank / freeze, or "on": the
// redesign's one tint for anything switched on (B / I / U, a filter, Edit
// Mode).
Rectangle {
    id: root

    property string label: ""
    property string iconSource: ""
    /// The icon after the label rather than before it ("Next ▶").
    property bool iconAfter: false

    property bool primary: false
    property bool ghost: false
    property bool danger: false

    property string size: "standard" // compact | control | ribbon | standard | command
    property bool toggled: false
    property string toggledKind: "" // "" | on | blank | freeze

    property bool boldLabel: false
    property bool italicLabel: false
    property bool underlineLabel: false
    property int minWidth: 0

    // Split variant: divider + caret zone on the trailing edge that opens a menu.
    property bool splitMenu: false
    property bool menuOpen: false

    readonly property bool hovered: (pressArea.containsMouse || splitArea.containsMouse
                                     || root.menuOpen) && root.enabled
    readonly property bool down: (pressArea.containsPress || splitArea.containsPress)
                                 && root.enabled
    readonly property bool hasIcon: root.iconSource.length > 0
    readonly property bool iconOnly: root.hasIcon && root.label.length === 0

    readonly property int _h: {
        if (root.size === "command")
            return Style.btnCommandH
        if (root.size === "compact")
            return Style.btnCompactH
        if (root.size === "control")
            return Style.controlH
        if (root.size === "ribbon")
            return Style.ribbonH
        return Style.btnStandardH
    }
    readonly property int _font: {
        if (root.size === "command")
            return Style.btnCommandFont
        if (root.size === "compact")
            return Style.btnCompactFont
        if (root.size === "ribbon")
            return Style.ribbonFont
        return Style.btnStandardFont
    }
    readonly property int _icon: {
        if (root.size === "command")
            return Style.btnCommandIcon
        if (root.size === "compact")
            return Style.btnCompactIcon
        if (root.size === "ribbon")
            return Style.ribbonIcon
        return Style.btnStandardIcon
    }
    // A command button's is a target of its own size beside a big face.
    readonly property int _splitW: root.size === "compact" ? 15
                                 : root.size === "ribbon" ? Style.ribbonSplitW
                                 : root.size === "command" ? 28 : 17
    readonly property int _pad: {
        if (root.size === "command")
            return 16
        if (root.size === "compact" || root.size === "control")
            return 10
        if (root.size === "ribbon")
            return Style.ribbonPad
        return 12
    }

    readonly property string _role: {
        if (root.toggled && root.toggledKind === "on")
            return "on"
        if (root.toggled && root.toggledKind === "blank")
            return "blank"
        if (root.toggled && root.toggledKind === "freeze")
            return "freeze"
        if (root.primary || root.toggled)
            return "primary"
        if (root.danger)
            return "danger"
        if (root.ghost)
            return "ghost"
        return "neutral"
    }

    readonly property bool _emphasis: _role === "primary" || _role === "blank"
                                      || _role === "freeze"

    readonly property color ink: {
        if (_role === "danger")
            return root.hovered ? Style.btnDangerInkHover : Style.btnDangerInk
        if (_role === "ghost")
            return root.hovered ? Style.text : Style.textMuted
        if (_role === "on")
            return Style.onInk
        if (_emphasis)
            return Style.textOnAccent
        return root.hovered ? Style.btnInkHover : Style.text
    }

    readonly property var _face: {
        const role = root._role
        const hov = root.hovered
        const press = root.down
        const face = { top: Style.btnNeutralTop, bot: Style.btnNeutralBot, edge: Style.btnEdge, sheen: 0.03 }
        if (role === "ghost") {
            face.top = press ? Style.btnGhostPress
                     : (hov ? Style.btnGhostHover : Style.btnGhost)
            face.bot = face.top
            face.edge = hov && !press ? Style.btnEdgeHover : Style.btnEdge
            face.sheen = 0
            return face
        }
        if (role === "on") {
            face.top = Style.onTint
            face.bot = Style.onTint
            face.edge = Style.onEdge
            face.sheen = 0
            return face
        }
        if (role === "danger") {
            face.top = press ? Style.btnDangerPress
                     : (hov ? Style.btnDangerHover : Style.btnDangerFill)
            face.bot = face.top
            face.edge = hov && !press ? Style.btnDangerEdgeHover : Style.btnDangerEdge
            face.sheen = 0
            return face
        }
        if (role === "primary") {
            face.top = press ? Style.btnPrimaryPressTop
                     : (hov ? Style.btnPrimaryHoverTop : Style.btnPrimaryTop)
            face.bot = press ? Style.btnPrimaryPressBot
                     : (hov ? Style.btnPrimaryHoverBot : Style.btnPrimaryBot)
            face.edge = hov ? Style.btnPrimaryEdgeHover : Style.btnPrimaryEdge
            face.sheen = press ? 0.03 : (hov ? 0.08 : 0.06)
            return face
        }
        if (role === "blank") {
            face.top = hov ? Style.btnBlankOnHoverTop : Style.btnBlankOnTop
            face.bot = hov ? Style.btnBlankOnHoverBot : Style.btnBlankOnBot
            face.edge = Style.btnBlankOnEdge
            face.sheen = 0.06
            return face
        }
        if (role === "freeze") {
            face.top = hov ? Style.btnFreezeOnHoverTop : Style.btnFreezeOnTop
            face.bot = hov ? Style.btnFreezeOnHoverBot : Style.btnFreezeOnBot
            face.edge = Style.btnFreezeOnEdge
            face.sheen = 0.06
            return face
        }
        face.top = press ? Style.btnNeutralPressTop
                 : (hov ? Style.btnNeutralHoverTop : Style.btnNeutralTop)
        face.bot = press ? Style.btnNeutralPressBot
                 : (hov ? Style.btnNeutralHoverBot : Style.btnNeutralBot)
        face.edge = hov && !press ? Style.btnEdgeHover : Style.btnEdge
        face.sheen = press ? 0.02 : (hov ? 0.05 : 0.03)
        return face
    }

    signal clicked()
    signal pressed()
    signal menuClicked()

    function _caretSvg(ink) {
        return Icons.svgUrl(
                    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 12 12">'
                    + '<path d="M2.5 4.5L6 8l3.5-3.5H2.5z" fill="' + String(ink) + '"/></svg>')
    }

    implicitHeight: root._h
    /// What its face asks for with its padding: the icon, the gap and the
    /// label (whole pixels: a fractional text width would leave an edge
    /// between pixels, half drawn, when the button is anchored by that edge).
    readonly property int contentWidth: Math.ceil(
        root._pad * 2 + (root.hasIcon ? root._icon + (root.label.length ? 6 : 0) : 0)
        + labelText.implicitWidth)
    /// Laid out narrower than that (a transport beside a Preview): it closes
    /// its icon up to its label, rather than keep the gap while the label
    /// runs to the edges.
    readonly property bool squeezed: root.width > 0
                                     && root.width - (root.splitMenu ? root._splitW : 0)
                                        < root.contentWidth
    /// Close the icon up to the label whether or not this one is squeezed:
    /// a row of buttons sets it on all of them when one is, so their icons
    /// keep one gap.
    property bool closeUp: false

    implicitWidth: {
        const split = root.splitMenu ? root._splitW : 0
        if (root.iconOnly)
            return Math.max(root._h, root.minWidth) + split
        return Math.max(root._h, root.minWidth, root.contentWidth) + split
    }

    radius: Style.btnRadius
    border.width: 1
    border.color: root._face.edge
    opacity: enabled ? 1 : 0.4
    clip: true
    gradient: Gradient {
        GradientStop { position: 0.0; color: root._face.top }
        GradientStop { position: 1.0; color: root._face.bot }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        height: 1
        visible: root._face.sheen > 0
        color: Style.sheen
        opacity: root._face.sheen
    }

    Item {
        id: mainZone
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: root.splitMenu ? splitZone.left : parent.right

        Row {
            id: contentRow
            anchors.centerIn: parent
            // A glyph leaves room inside its square: closed up, that is gap
            // enough.
            spacing: root.closeUp || (root.squeezed && root.hasIcon && !root.iconOnly) ? 0 : 6
            layoutDirection: root.iconAfter ? Qt.RightToLeft : Qt.LeftToRight
            Image {
                visible: root.hasIcon
                width: root._icon
                height: root._icon
                anchors.verticalCenter: parent.verticalCenter
                source: root.iconSource
                sourceSize: Qt.size(root._icon, root._icon)
                smooth: true
                asynchronous: false
                cache: true
            }
            Text {
                id: labelText
                visible: root.label.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: root.label
                color: root.ink
                font.pixelSize: root._font
                font.bold: root.boldLabel || root._emphasis || root.size === "command"
                font.italic: root.italicLabel
                font.underline: root.underlineLabel
            }
        }

        MouseArea {
            id: pressArea
            anchors.fill: parent
            enabled: root.enabled
            hoverEnabled: true
            cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onPressed: root.pressed()
            onClicked: root.clicked()
        }
    }

    Item {
        id: splitZone
        visible: root.splitMenu
        width: root.splitMenu ? root._splitW : 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            width: 1
            color: root.hovered ? Style.btnEdgeHover : Style.btnEdge
        }

        Image {
            anchors.centerIn: parent
            width: 10
            height: 10
            source: root._caretSvg(root.ink)
            sourceSize: Qt.size(10, 10)
            smooth: true
            cache: true
        }

        MouseArea {
            id: splitArea
            anchors.fill: parent
            enabled: root.enabled && root.splitMenu
            hoverEnabled: true
            cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: root.menuClicked()
        }
    }
}
