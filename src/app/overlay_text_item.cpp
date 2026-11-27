#include "overlay_text_item.hpp"

#include "preview_resources.hpp"
#include "qvariant_json.hpp"
#include "session_facade.hpp"

#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/overlays.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/render/paint_context.hpp>
#include <seventhslide/render/text_painter.hpp>

#include <QPainter>

#include <algorithm>

namespace seventhslide::app {

OverlayTextItem::OverlayTextItem(QQuickItem* parent)
    : SessionBoundPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &OverlayTextItem::textChanged, this, [this] { update(); });
    connect(this, &QQuickItem::widthChanged, this, [this] { update(); });
    connect(this, &QQuickItem::heightChanged, this, [this] { update(); });
}

template <typename T>
void OverlayTextItem::assign(T& slot, const T& value)
{
    if (slot == value) {
        return;
    }
    slot = value;
    emit textChanged();
}

void OverlayTextItem::paint(QPainter* painter)
{
    SessionFacade* facade = session();
    const double scale = design_scale_ > 0 ? design_scale_ : 1.0;
    if (facade == nullptr || markup_.isEmpty() || width() <= 0 || height() <= 0) {
        return;
    }
    auto& measurer = facade->previews().measurer();

    const double pad = std::max(0.0, padding_);
    layout::OverlayBox box;
    box.x = pad / scale;
    box.y = pad / scale;
    box.w = (width() - 2 * pad) / scale;
    box.h = (height() - 2 * pad) / scale;
    box.font_family = font_family_.isEmpty() ? std::string(canvas::k_default_font_family)
                                             : font_family_.toStdString();
    box.font_size = std::max(1, font_size_);
    box.font_color = font_color_.toStdString();
    box.bold = bold_;
    box.italic = italic_;
    box.underline = underline_;
    box.text_align = text_align_.toStdString();
    box.vertical_align = vertical_align_.toStdString();
    box.line_height = layout::clamp_overlay_line_height(line_height_);
    box.line_gap = layout::clamp_overlay_line_gap(line_gap_);
    box.letter_spacing = layout::clamp_overlay_letter_spacing(letter_spacing_);
    box.show_chords = show_chords_;
    box.chords = layout::clamped({.size_pct = chord_size_pct_,
                                  .gap_pct = chord_gap_pct_,
                                  .color = chord_color_.toStdString()});
    for (const auto& line : model::parse_markup_lines(markup_.toStdString())) {
        box.lines.push_back(layout::styled_line(line, bold_, italic_, underline_));
    }
    for (const auto& gap : gaps_above_) {
        box.gaps_above.push_back(gap.toInt());
    }
    if (!effects_.isEmpty()) {
        box.effects = to_json(QVariant(effects_)).get<canvas::Effects>();
    }
    if (!paragraph_.isEmpty()) {
        box.paragraph = to_json(QVariant(paragraph_)).get<canvas::Paragraph>();
    }
    box.layout = layout::lay_out_overlay(box, measurer);

    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter->scale(scale, scale);
    render::paint_overlay_box(*painter, box, {.faces = &measurer.faces()});
    painter->restore();
}

void OverlayTextItem::setMarkup(const QString& markup) { assign(markup_, markup); }
void OverlayTextItem::setDesignScale(qreal scale) { assign(design_scale_, scale); }
void OverlayTextItem::setFontFamily(const QString& family) { assign(font_family_, family); }
void OverlayTextItem::setFontSize(int size) { assign(font_size_, size); }
void OverlayTextItem::setFontColor(const QString& color) { assign(font_color_, color); }
void OverlayTextItem::setBold(bool on) { assign(bold_, on); }
void OverlayTextItem::setItalic(bool on) { assign(italic_, on); }
void OverlayTextItem::setUnderline(bool on) { assign(underline_, on); }
void OverlayTextItem::setTextAlign(const QString& align) { assign(text_align_, align); }
void OverlayTextItem::setVerticalAlign(const QString& align) { assign(vertical_align_, align); }
void OverlayTextItem::setLineHeight(qreal factor) { assign(line_height_, factor); }
void OverlayTextItem::setLineGap(int gap) { assign(line_gap_, gap); }
void OverlayTextItem::setGapsAbove(const QVariantList& gaps) { assign(gaps_above_, gaps); }
void OverlayTextItem::setLetterSpacing(qreal spacing) { assign(letter_spacing_, spacing); }
void OverlayTextItem::setShowChords(bool on) { assign(show_chords_, on); }
void OverlayTextItem::setChordSizePct(int pct) { assign(chord_size_pct_, pct); }
void OverlayTextItem::setChordGapPct(int pct) { assign(chord_gap_pct_, pct); }
void OverlayTextItem::setChordColor(const QString& color) { assign(chord_color_, color); }
void OverlayTextItem::setEffects(const QVariantMap& effects) { assign(effects_, effects); }
void OverlayTextItem::setPadding(qreal padding) { assign(padding_, padding); }
void OverlayTextItem::setParagraph(const QVariantMap& paragraph) { assign(paragraph_, paragraph); }

}  // namespace seventhslide::app
