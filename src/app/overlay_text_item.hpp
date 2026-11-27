#pragma once

// QML: OverlayTextItem - markup laid out and painted the way an output paints
// a text box (layout::lay_out_overlay, render::paint_overlay_box), chords
// included. The slide editor's stage draws a slide's text box with it while
// the box is not being edited: QML's rich text has no way to put chords over
// words. The session's fonts and measurer lay it out and paint it.

#include "session_bound_item.hpp"

#include <seventhslide/canvas/box.hpp>
#include <seventhslide/layout/chords.hpp>

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace seventhslide::app {

class OverlayTextItem : public SessionBoundPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    /// Inline markup, ``[chord]`` markers and all.
    Q_PROPERTY(QString markup READ markup WRITE setMarkup NOTIFY textChanged)
    /// Item px per design px: the box is laid out at design size and drawn
    /// scaled, so it wraps exactly as the output does.
    Q_PROPERTY(qreal designScale READ designScale WRITE setDesignScale NOTIFY textChanged)
    // The box's type, in design px (see canvas::BoxType).
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY textChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY textChanged)
    Q_PROPERTY(QString fontColor READ fontColor WRITE setFontColor NOTIFY textChanged)
    Q_PROPERTY(bool bold READ bold WRITE setBold NOTIFY textChanged)
    Q_PROPERTY(bool italic READ italic WRITE setItalic NOTIFY textChanged)
    Q_PROPERTY(bool underline READ underline WRITE setUnderline NOTIFY textChanged)
    Q_PROPERTY(QString textAlign READ textAlign WRITE setTextAlign NOTIFY textChanged)
    Q_PROPERTY(QString verticalAlign READ verticalAlign WRITE setVerticalAlign NOTIFY textChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY textChanged)
    Q_PROPERTY(int lineGap READ lineGap WRITE setLineGap NOTIFY textChanged)
    /// Room above each line, in design pixels: what a linked box's text does not hold.
    Q_PROPERTY(QVariantList gapsAbove READ gapsAbove WRITE setGapsAbove NOTIFY textChanged)
    Q_PROPERTY(qreal letterSpacing READ letterSpacing WRITE setLetterSpacing NOTIFY textChanged)
    // Chords: shown over their words when on, in this style.
    Q_PROPERTY(bool showChords READ showChords WRITE setShowChords NOTIFY textChanged)
    Q_PROPERTY(int chordSizePct READ chordSizePct WRITE setChordSizePct NOTIFY textChanged)
    Q_PROPERTY(int chordGapPct READ chordGapPct WRITE setChordGapPct NOTIFY textChanged)
    Q_PROPERTY(QString chordColor READ chordColor WRITE setChordColor NOTIFY textChanged)
    /// The effects on the words (an outline, a text shadow), as the selection
    /// map carries them (``effects``: canvas::Effects' stored form).
    Q_PROPERTY(QVariantMap effects READ effects WRITE setEffects NOTIFY textChanged)
    /// The box's paragraphs (bullets, spacing, indent, margins, autofit), as
    /// the selection map carries them (``paragraph``: canvas::Paragraph's
    /// stored form).
    Q_PROPERTY(QVariantMap paragraph READ paragraph WRITE setParagraph NOTIFY textChanged)
    /// Item px the box is inset inside this item: room for the words' shadow,
    /// which a painted item clips at its own bounds.
    Q_PROPERTY(qreal padding READ padding WRITE setPadding NOTIFY textChanged)

public:
    explicit OverlayTextItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    [[nodiscard]] QString markup() const { return markup_; }
    void setMarkup(const QString& markup);
    [[nodiscard]] qreal designScale() const { return design_scale_; }
    void setDesignScale(qreal scale);
    [[nodiscard]] QString fontFamily() const { return font_family_; }
    void setFontFamily(const QString& family);
    [[nodiscard]] int fontSize() const { return font_size_; }
    void setFontSize(int size);
    [[nodiscard]] QString fontColor() const { return font_color_; }
    void setFontColor(const QString& color);
    [[nodiscard]] bool bold() const { return bold_; }
    void setBold(bool on);
    [[nodiscard]] bool italic() const { return italic_; }
    void setItalic(bool on);
    [[nodiscard]] bool underline() const { return underline_; }
    void setUnderline(bool on);
    [[nodiscard]] QString textAlign() const { return text_align_; }
    void setTextAlign(const QString& align);
    [[nodiscard]] QString verticalAlign() const { return vertical_align_; }
    void setVerticalAlign(const QString& align);
    [[nodiscard]] qreal lineHeight() const { return line_height_; }
    void setLineHeight(qreal factor);
    [[nodiscard]] int lineGap() const { return line_gap_; }
    void setLineGap(int gap);
    [[nodiscard]] QVariantList gapsAbove() const { return gaps_above_; }
    void setGapsAbove(const QVariantList& gaps);
    [[nodiscard]] qreal letterSpacing() const { return letter_spacing_; }
    void setLetterSpacing(qreal spacing);
    [[nodiscard]] bool showChords() const { return show_chords_; }
    void setShowChords(bool on);
    [[nodiscard]] int chordSizePct() const { return chord_size_pct_; }
    void setChordSizePct(int pct);
    [[nodiscard]] int chordGapPct() const { return chord_gap_pct_; }
    void setChordGapPct(int pct);
    [[nodiscard]] QString chordColor() const { return chord_color_; }
    void setChordColor(const QString& color);
    [[nodiscard]] QVariantMap effects() const { return effects_; }
    void setEffects(const QVariantMap& effects);
    [[nodiscard]] qreal padding() const { return padding_; }
    void setPadding(qreal padding);
    [[nodiscard]] QVariantMap paragraph() const { return paragraph_; }
    void setParagraph(const QVariantMap& paragraph);

signals:
    void textChanged();

private:
    /// Assign + repaint only when the value actually moved.
    template <typename T>
    void assign(T& slot, const T& value);

    QString markup_;
    qreal design_scale_ = 1.0;
    QString font_family_;
    int font_size_ = canvas::k_default_font_size;
    QString font_color_ = QString::fromUtf8(canvas::k_default_font_color);
    bool bold_ = false;
    bool italic_ = false;
    bool underline_ = false;
    QString text_align_ = QStringLiteral("center");
    QString vertical_align_ = QStringLiteral("middle");
    qreal line_height_ = canvas::k_default_line_height;
    int line_gap_ = 0;
    QVariantList gaps_above_;
    qreal letter_spacing_ = 0.0;
    bool show_chords_ = false;
    int chord_size_pct_ = layout::k_default_chord_size_pct;
    int chord_gap_pct_ = layout::k_default_chord_gap_pct;
    QString chord_color_ = QString::fromUtf8(layout::k_default_chord_color);
    QVariantMap effects_;
    qreal padding_ = 0.0;
    QVariantMap paragraph_;
};

}  // namespace seventhslide::app
