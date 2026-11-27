import QtQuick

// A short label in a disc, a badge or a pill: initials, a count, a verse's
// tag. A Text centred in a box is centred by its line, which reaches up for
// accents and down for descenders, so capitals and figures ride high in it;
// this one moves down by the difference, so the capitals' middle is the
// box's. Anchor it with ``anchors.centerIn`` or ``anchors.verticalCenter``.
Text {
    id: label

    FontMetrics {
        id: metrics
        font: label.font
    }
    // The line's middle is height / 2 down; the capitals' is half their
    // height above the baseline.
    anchors.verticalCenterOffset: Math.round(label.height / 2 - label.baselineOffset
                                             + metrics.tightBoundingRect("H").height / 2)
}
