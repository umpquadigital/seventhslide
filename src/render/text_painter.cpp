#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/render/effects_painter.hpp>
#include <seventhslide/render/shape_painter.hpp>

#include <seventhslide/render/css_color.hpp>
#include <seventhslide/freeform/paint.hpp>
#include <seventhslide/render/element_raster_cache.hpp>
#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/image_painter.hpp>
#include <seventhslide/render/video_painter.hpp>
#include <seventhslide/media/media_player.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/qt_font_faces.hpp>
#include <seventhslide/layout/word_wrap.hpp>
#include <seventhslide/scene/text_metrics.hpp>
#include <seventhslide/scene/chrome_text.hpp>

#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QImage>
#include <QBrush>
#include <QPaintDevice>
#include <QHash>
#include <QPainterPath>
#include <QRawFont>
#include <QString>
#include <QTextLayout>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::render {
namespace {

// Horizontal: left (the default), center, right. Vertical: top, middle (the
// default), bottom. Anything else takes the default.
[[nodiscard]] bool is_center_align(std::string_view a) noexcept
{
    return a == "center";
}

[[nodiscard]] bool is_right_align(std::string_view a) noexcept
{
    return a == "right";
}

[[nodiscard]] bool is_bottom_align(std::string_view a) noexcept
{
    return a == "bottom";
}

[[nodiscard]] bool is_top_align(std::string_view a) noexcept
{
    return a == "top";
}

[[nodiscard]] layout::FontStyle make_style(std::string_view family,
                                           double size_px,
                                           bool bold,
                                           bool italic,
                                           double letter_spacing = 0.0)
{
    layout::FontStyle st;
    st.family = std::string(family);
    st.size_px = size_px > 0.0 ? size_px : 1.0;
    st.bold = bold;
    st.italic = italic;
    st.letter_spacing = letter_spacing;
    return st;
}

/// ``style`` as a paint font: the shared face cache when there is one, else a
/// plain QFont with the same request.
[[nodiscard]] layout::ResolvedQtFont resolve_span_font(const layout::QtFontFaces* faces,
                                                       const layout::FontStyle& style)
{
    if (faces) {
        return faces->resolve(style);
    }
    layout::ResolvedQtFont out;
    out.request_bold = style.bold;
    out.request_italic = style.italic;
    out.font = QFont(QString::fromStdString(style.family));
    out.font.setPixelSize(std::max(1, static_cast<int>(std::lround(style.size_px))));
    out.font.setStyleStrategy(QFont::PreferAntialias);
    out.font.setWeight(style.bold ? QFont::Bold : QFont::Normal);
    out.font.setItalic(style.italic);
    const double tracking = layout::clamp_letter_spacing_px(style.letter_spacing);
    if (tracking != 0.0) {
        out.font.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
    }
    out.face_is_bold = style.bold;
    out.face_is_italic = style.italic;
    out.resolved = false;
    return out;
}

[[nodiscard]] double painter_design_scale(const QPainter& painter)
{
    const QTransform t = painter.worldTransform();
    const double world = std::hypot(t.m11(), t.m12());
    const QPaintDevice* dev = painter.device();
    const double dpr = (dev != nullptr) ? dev->devicePixelRatioF() : 1.0;
    if (!(dpr > 0.0)) {
        return world;
    }
    return world / dpr;
}

/// Whether the font engine's glyph raster carries the outline's weight at
/// output scale. FreeType's does; Core Text's does not (see draw_text), so on
/// macOS text is filled from outlines at every scale, and the outline-only
/// care below (bitmap glyphs, the fill origin) applies there alone - Linux
/// draws exactly as it did.
#if defined(Q_OS_DARWIN)
constexpr bool k_raster_is_faithful = false;
#else
constexpr bool k_raster_is_faithful = true;
#endif

struct GlyphOutline {
    QPainterPath path;
    /// False when ``path`` is empty, or (where the raster is not faithful)
    /// some glyph has no outline to fill - a colour-bitmap emoji in the run.
    /// Filling ``path`` would leave that glyph out.
    bool complete = true;
};

/// True when some glyph of ``text`` has ink but no outline (a colour bitmap).
[[nodiscard]] bool has_outline_less_glyph(const QFont& font, const QString& text)
{
    QTextLayout layout(text, font);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    for (const QGlyphRun& run : layout.glyphRuns()) {
        const QRawFont raw = run.rawFont();
        for (const quint32 glyph : run.glyphIndexes()) {
            if (raw.pathForGlyph(glyph).isEmpty() && !raw.boundingRect(glyph).isEmpty()) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Glyph outlines for one run, baseline at the origin, in design pixels.
 *
 * Shaping a slide's runs into paths roughly doubles its paint at preview
 * scale, so keep the recent runs for redraws (stepping back a slide,
 * thumbnails). Paths are pure geometry - one entry serves every scale the
 * run is drawn at. The cache simply empties past 512 runs: walking a
 * 420-slide service, stepping back costs the same as a warm repaint, so an
 * LRU would buy nothing ("bench: preview text across a service").
 */
[[nodiscard]] const GlyphOutline& glyph_outline(const QFont& font, const QString& text)
{
    static thread_local QHash<QString, GlyphOutline> cache;
    const QString key = font.key() + QLatin1Char('\x1f') + text;
    if (auto it = cache.constFind(key); it != cache.constEnd()) {
        return *it;
    }
    if (cache.size() > 512) {
        cache.clear();
    }
    GlyphOutline glyphs;
    glyphs.path.addText(QPointF(0, 0), font, text);
    glyphs.complete =
        !glyphs.path.isEmpty() && (k_raster_is_faithful || !has_outline_less_glyph(font, text));
    return *cache.insert(key, std::move(glyphs));
}

void apply_synthetic_embolden(QPainter& painter,
                              const QPointF& pos,
                              const QPainterPath& glyphs,
                              double width_px)
{
    // No lower cut-off beyond "does nothing": at preview scale the design-space
    // thicken lands around a tenth of a device pixel, and dropping it there is
    // what made faux-bold text vanish in the operator preview while the
    // audience screen still showed it bold.
    if (width_px < 0.01 || glyphs.isEmpty()) {
        return;
    }
    QPainterPathStroker stroker;
    stroker.setWidth(width_px);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    painter.save();
    painter.translate(pos);
    painter.fillPath(stroker.createStroke(glyphs), QBrush(painter.pen().color()));
    painter.restore();
}

/// Underline as geometry. `addText` returns glyph outlines only, so the
/// decoration Qt would have drawn with the text has to be drawn beside it.
void draw_underline(QPainter& painter,
                    const QPointF& pos,
                    const QString& text,
                    const QFont& font)
{
    const QFontMetricsF fm(font);
    const double width = fm.horizontalAdvance(text);
    if (width <= 0.0) {
        return;
    }
    const double thickness = std::max(1.0, fm.lineWidth());
    painter.fillRect(QRectF(pos.x(), pos.y() + fm.underlinePos(), width, thickness),
                     QBrush(painter.pen().color()));
}

/// The outline round each glyph, under the glyph: the stroke centred on its
/// edge, as PowerPoint draws a text outline.
void draw_text_outline(QPainter& painter,
                       const QPointF& pos,
                       const QString& text,
                       const layout::ResolvedQtFont& face,
                       const canvas::TextOutline& outline)
{
    const QColor color = css_color(outline.color);
    if (outline.width <= 0.0 || !color.isValid() || color.alpha() == 0) {
        return;
    }
    const QPainterPath& glyphs = glyph_outline(face.font, text).path;
    if (glyphs.isEmpty()) {
        return;
    }
    QPen pen(color);
    pen.setWidthF(outline.width);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(pos);
    painter.strokePath(glyphs, pen);
    painter.restore();
}

void draw_text(QPainter& painter,
               const QPointF& pos,
               const QString& text,
               const layout::ResolvedQtFont& face,
               const canvas::TextOutline* outline = nullptr)
{
    if (outline != nullptr) {
        draw_text_outline(painter, pos, text, face, *outline);
    }
    const double scale = painter_design_scale(painter);
    // Admin previews paint the 1920px stage through CanvasFit ~0.15-0.25, so a
    // 20px copyright line lands on 3 device pixels. A glyph raster that small
    // is grid-fit and darkened by the font engine - CFF faces (Nimbus Sans,
    // the usual Helvetica substitute) come out over twice the ink the audience
    // screen shows once it is scaled down, and no choice of pixel size, hinting
    // preference, or style strategy avoids it. Fill the design-size outlines
    // instead: geometry scales linearly, so weight and advances match the
    // output. On FreeType, output windows draw at scale >= 0.5, where the
    // raster is faithful and much cheaper, and keep the glyph path.
    //
    // Core Text is never faithful there: Qt rasterizes glyphs up to 64 device
    // px with font smoothing on, which dilates every stem - Liberation Sans
    // Bold carries ~30% more ink at 18px than its outline, ~10% at 60px, and
    // none from 65px, where Qt fills outlines itself. No QFont strategy or
    // hinting preference turns that off, so on macOS output text is filled
    // from outlines at every scale, and weighs the same at every size.
    const QPainterPath* glyphs = nullptr;
    if (scale > 0.0 && (scale < 0.5 || !k_raster_is_faithful)) {
        // A colour-bitmap glyph (emoji) has no outline to fill, so a run with
        // one falls through to the raster: a touch heavy, but filling the rest
        // would leave a hole the output screen does not have.
        const GlyphOutline& filled = glyph_outline(face.font, text);
        if (filled.complete) {
            glyphs = &filled.path;
        }
    }
    if (glyphs != nullptr) {
        // On macOS, at output scale too: the origin on the 1/64 px grid, as Qt
        // places the glyphs it fills itself, so the edge pixels round alike
        // wherever the run is drawn and an element's cached raster stays
        // identical to drawing it live.
        const QPointF origin = k_raster_is_faithful
            ? pos
            : QPointF(std::round(pos.x() * 64.0) / 64.0, std::round(pos.y() * 64.0) / 64.0);
        painter.save();
        painter.translate(origin);
        painter.fillPath(*glyphs, QBrush(painter.pen().color()));
        painter.restore();
        if (face.needs_synthetic_embolden()) {
            apply_synthetic_embolden(painter, origin, *glyphs, face.synthetic_embolden_px);
        }
        if (face.font.underline()) {
            draw_underline(painter, pos, text, face.font);
        }
        return;
    }

    painter.setFont(face.font);
    painter.drawText(pos, text);
    if (face.needs_synthetic_embolden()) {
        apply_synthetic_embolden(painter, pos, glyph_outline(face.font, text).path,
                                 face.synthetic_embolden_px);
    }
}

[[nodiscard]] double span_advance(const layout::ResolvedQtFont& face, const QString& text)
{
    double w = QFontMetricsF(face.font).horizontalAdvance(text);
    if (face.needs_synthetic_embolden()) {
        w += face.synthetic_embolden_px;
    }
    return w;
}

/**
 * Measures exactly as paint draws: the face ``faces`` resolves, or a plain
 * QFont when there is no face cache.
 *
 * Frames are measured when they are built (scene::lay_out_frame_text), so
 * paint normally measures nothing. This lays out what reaches paint without
 * that: a clock, whose words only exist now, and frames assembled by hand.
 * It goes through the same layout functions, so there is one way to wrap and
 * place text whichever side does it.
 */
class PaintTextMeasurer final : public layout::ITextMeasurer {
public:
    explicit PaintTextMeasurer(const layout::QtFontFaces* faces)
        : faces_(faces)
    {
    }

    [[nodiscard]] double measure_width(std::string_view text,
                                       const layout::FontStyle& style) override
    {
        if (text.empty()) {
            return 0.0;
        }
        return span_advance(resolve_span_font(faces_, style),
                            QString::fromUtf8(text));
    }

    [[nodiscard]] layout::FontMetrics font_metrics(const layout::FontStyle& style) override
    {
        return layout::vertical_metrics(resolve_span_font(faces_, style).font);
    }

private:
    const layout::QtFontFaces* faces_;
};

/// A chord cell's lyric font: the box font with the cell's style, at the box
/// size - chord rows have a fixed height, so ``<size>`` does not apply there.
[[nodiscard]] layout::ResolvedQtFont chord_cell_font(const layout::QtFontFaces* faces,
                                                     const layout::FontStyle& box,
                                                     const model::TextStyle& cell_style)
{
    model::TextStyle style = cell_style;
    style.size_pct = 100;
    auto face = resolve_span_font(faces, layout::span_font_style(box, style));
    face.font.setUnderline(cell_style.underline);
    return face;
}

/// ``runs`` at the offsets ``x`` holds for them, from ``left``.
void draw_runs(QPainter& painter,
               const model::RichLine& runs,
               const std::vector<double>& x,
               double left,
               double baseline_y,
               const layout::QtFontFaces* faces,
               const layout::FontStyle& box,
               const QColor& color,
               const canvas::TextOutline* outline = nullptr,
               bool underline = false)
{
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& span = runs[i];
        auto face = resolve_span_font(faces, layout::span_font_style(box, span.style));
        face.font.setUnderline(underline || span.style.underline);
        painter.setPen(span.style.color.empty() ? color : css_color_or(span.style.color, color));
        const double rise = box.size_px * (std::max(0, span.style.baseline_rise_pct) / 100.0);
        draw_text(painter, QPointF(left + x[i], baseline_y - rise),
                  QString::fromStdString(span.text), face, outline);
    }
}

void draw_chord_line(QPainter& painter,
                     const layout::ChordLine& chords,
                     const layout::ChordStyle& style,
                     const scene::SceneLineMetrics& metrics,
                     double left,
                     double top_y,
                     const layout::QtFontFaces* faces,
                     const layout::FontStyle& lyric_box,
                     const QColor& lyric_color,
                     const canvas::TextOutline* outline = nullptr,
                     bool underline = false)
{
    const auto chord_face = resolve_span_font(faces, layout::chord_font(lyric_box, style));
    const QColor chord_color =
        css_color_or(style.color, css_color_or(layout::k_default_chord_color, lyric_color));
    const double chord_baseline = top_y + metrics.chord_baseline;
    const double lyric_baseline = top_y + metrics.baseline;

    for (std::size_t i = 0; i < chords.items.size(); ++i) {
        const auto& it = chords.items[i];
        if (it.kind == layout::ChordItemKind::Space) {
            continue;
        }
        const double cx = left + metrics.x[i];
        if (it.cell.chord.has_value() && !it.cell.chord->empty()) {
            painter.setPen(chord_color);
            draw_text(painter, QPointF(cx, chord_baseline),
                      QString::fromStdString(*it.cell.chord), chord_face, outline);
        }
        if (!it.cell.syllable.empty()) {
            painter.setPen(it.cell.style.color.empty()
                               ? lyric_color
                               : css_color_or(it.cell.style.color, lyric_color));
            auto cell_face = chord_cell_font(faces, lyric_box, it.cell.style);
            if (underline) {
                cell_face.font.setUnderline(true);
            }
            draw_text(painter, QPointF(cx, lyric_baseline),
                      QString::fromStdString(it.cell.syllable), cell_face, outline);
        }
    }
    if (!metrics.hyphens.empty()) {
        const auto hyphen_face = resolve_span_font(faces, lyric_box);
        painter.setPen(lyric_color);
        for (const double hx : metrics.hyphens) {
            draw_text(painter, QPointF(left + hx, lyric_baseline), QStringLiteral("-"), hyphen_face,
                      outline);
        }
    }
}

/// Whether ``metrics`` still describes ``line``: one offset per run or chord item.
[[nodiscard]] bool metrics_fit(const scene::SceneTextLine& line,
                               const scene::SceneLineMetrics& metrics) noexcept
{
    return metrics.x.size() == (line.has_chords ? line.chords.items.size() : line.runs.size());
}

/// How far an overlay box's words paint past the box: their shadow.
[[nodiscard]] double overlay_reach(const layout::OverlayBox& box)
{
    canvas::Effects shadow_only;
    shadow_only.text_shadow = box.effects.text_shadow;
    return canvas::effects_reach(shadow_only);
}

/// Everything a body box paints: its words' rectangle, the margins round
/// it (the frame), its line, its effects and its words' shadow. Its raster
/// covers this, and the direct draw replaces the clip with it, so the two
/// agree whatever clip the painter arrives with.
[[nodiscard]] QRectF body_bounds(const canvas::TextBox& box)
{
    const double reach = line_reach(box.line) + 1.0 + canvas::effects_reach(box.effects);
    const auto& m = box.margins;
    return QRectF(box.x, box.y, box.width, box.height)
        .adjusted(-std::max(0.0, m.left) - reach, -std::max(0.0, m.top) - reach,
                  std::max(0.0, m.right) + reach, std::max(0.0, m.bottom) + reach);
}

/// The text box drawn directly. Callers have checked it has lines and area.
void paint_text_content_live(QPainter& painter,
                             const scene::SceneTextContent& text,
                             const layout::QtFontFaces* faces,
                             const QImage* fill_picture = nullptr)
{
    const auto& box = text.box;
    const QRectF inner(box.x, box.y, box.width, box.height);
    if (inner.width() <= 0 || inner.height() <= 0) {
        return;
    }

    // A built frame carries every line's metrics; anything else is measured
    // here, the same way.
    PaintTextMeasurer measurer(faces);
    std::vector<scene::SceneLineMetrics> measured;
    std::vector<const scene::SceneLineMetrics*> metrics;
    measured.reserve(text.lines.size());
    metrics.reserve(text.lines.size());
    double content_h = 0;
    for (const auto& line : text.lines) {
        if (line.metrics && metrics_fit(line, *line.metrics)) {
            metrics.push_back(&*line.metrics);
        } else {
            measured.push_back(scene::measure_text_line(line, box, text.chords, measurer));
            metrics.push_back(&measured.back());
        }
        content_h += line.margin_top_px + metrics.back()->height;
    }

    double block_y = inner.top();
    if (is_bottom_align(box.valign)) {
        block_y = inner.bottom() - content_h;
    } else if (!is_top_align(box.valign)) {
        block_y = inner.top() + (inner.height() - content_h) * 0.5;
    }

    painter.save();
    painter.setOpacity(painter.opacity() * std::clamp(box.opacity, 0.0, 1.0));
    // The box replaces the clip with everything it paints (its raster is
    // copied under the same clip: ElementPlacement::replace_clip), then the
    // frame behind the words, with its effects; then the words, clipped to
    // their rectangle, over their shadow.
    painter.setClipRect(body_bounds(box));
    // The frame is the element's rectangle: the words' plus the margins.
    const QRectF outer = inner.adjusted(-std::max(0.0, box.margins.left),
                                        -std::max(0.0, box.margins.top),
                                        std::max(0.0, box.margins.right),
                                        std::max(0.0, box.margins.bottom));
    paint_rect_frame(painter, outer, box.fill, box.line, box.effects, fill_picture);

    const QColor box_color = css_color_or(box.color, QColor(Qt::white));
    const canvas::TextOutline* outline =
        box.effects.text_outline.width > 0.0 ? &box.effects.text_outline : nullptr;
    const auto words = [&](QPainter& p) {
        p.save();
        p.setClipRect(inner, Qt::IntersectClip);
        double y = block_y;
        for (std::size_t i = 0; i < text.lines.size(); ++i) {
            const auto& line = text.lines[i];
            const auto& m = *metrics[i];
            y += line.margin_top_px;
            double x = inner.left();
            if (is_right_align(box.align)) {
                x = inner.right() - m.width;
            } else if (is_center_align(box.align)) {
                x = inner.left() + (inner.width() - m.width) * 0.5;
            }
            // The line's type over the box's: follow-lines sets its active
            // line and other lines apart.
            const auto type = canvas::painted_type(box, line.type);
            const auto font = make_style(type.family, static_cast<double>(type.size), type.bold,
                                         type.italic, box.letter_spacing);
            const QColor color = css_color_or(type.color, box_color);
            if (line.has_chords) {
                draw_chord_line(p, line.chords, text.chords, m, x, y, faces, font, color, outline,
                                type.underline);
            } else {
                draw_runs(p, line.runs, m.x, x, y + m.baseline, faces, font, color, outline,
                          type.underline);
            }
            y += m.height;
        }
        p.restore();
    };
    paint_with_text_shadow(painter, box.effects.text_shadow, inner, words);

    painter.restore();
}

/// What turns ``box`` about its middle; identity when it is not turned.
[[nodiscard]] QTransform overlay_turn(const layout::OverlayBox& box)
{
    QTransform turn;
    if (box.rotation != 0.0) {
        const QPointF middle(box.x + box.w * 0.5, box.y + box.h * 0.5);
        turn.translate(middle.x(), middle.y());
        turn.rotate(box.rotation);
        turn.translate(-middle.x(), -middle.y());
    }
    return turn;
}

/// One overlay box drawn directly. Callers have checked it has lines and area.
void paint_overlay_box_live(QPainter& painter,
                            const layout::OverlayBox& box,
                            const layout::QtFontFaces* faces)
{
    // A built frame carries the layout; a clock or a hand-built box is
    // laid out here, the same way.
    PaintTextMeasurer measurer(faces);
    std::optional<layout::OverlayLayout> measured;
    if (!box.layout) {
        measured = layout::lay_out_overlay(box, measurer);
    }
    const auto& laid_out = box.layout ? *box.layout : *measured;

    // The words sit inside the margins; they are clipped to the box, and
    // their shadow falls past it.
    const auto& margins = box.paragraph.margins;
    const QRectF outer(box.x, box.y, box.w, box.h);
    const QRectF rect = outer.adjusted(std::max(0.0, margins.left), std::max(0.0, margins.top),
                                       -std::max(0.0, margins.right),
                                       -std::max(0.0, margins.bottom));
    const double reach = overlay_reach(box);
    const QColor color = css_color_or(box.font_color, QColor(Qt::white));
    // Box bold and italic are already on the runs; autofit may have shrunk
    // the type (the runs carry their own sizes, scaled with it).
    const auto base = make_style(box.font_family, box.font_size * laid_out.font_scale, false,
                                 false, box.letter_spacing * laid_out.font_scale);

    double top = rect.top();
    if (is_bottom_align(box.vertical_align)) {
        top = rect.bottom() - laid_out.height;
    } else if (!is_top_align(box.vertical_align)) {
        top = rect.top() + (rect.height() - laid_out.height) * 0.5;
    }

    const canvas::TextOutline* outline =
        box.effects.text_outline.width > 0.0 ? &box.effects.text_outline : nullptr;
    const auto words = [&](QPainter& p) {
        p.save();
        p.setClipRect(outer, Qt::IntersectClip);
        // Each row aligned in its column (the whole width in one).
        const double column_w = layout::overlay_column_width(box);
        for (const auto& row : laid_out.rows) {
            double left = rect.left() + row.x;
            const std::string_view align = row.align.empty() ? std::string_view(box.text_align) : row.align;
            if (is_right_align(align)) {
                left += column_w - row.width;
            } else if (is_center_align(align)) {
                left += (column_w - row.width) * 0.5;
            }
            for (const auto& run : row.runs) {
                auto face = resolve_span_font(faces, layout::span_font_style(base, run.style));
                face.font.setUnderline(run.style.underline);
                p.setPen(run.style.color.empty() ? color : css_color_or(run.style.color, color));
                draw_text(p, QPointF(left + run.x, top + row.y + run.baseline),
                          QString::fromStdString(run.text), face, outline);
            }
            if (!row.chords.empty()) {
                p.setPen(css_color_or(box.chords.color,
                                      css_color_or(layout::k_default_chord_color, color)));
            }
            for (const auto& chord : row.chords) {
                auto chord_style = base;
                chord_style.size_px = chord.size_px;
                chord_style.bold = true;
                draw_text(p, QPointF(left + chord.x, top + row.y + chord.baseline),
                          QString::fromStdString(chord.text),
                          resolve_span_font(faces, chord_style), outline);
            }
        }
        p.restore();
    };
    painter.save();
    painter.setTransform(overlay_turn(box), true);
    // Narrows the clip: a Wipe's, say, holds for the words too (its raster
    // is copied under the same call: see ElementPlacement::replace_clip).
    painter.setClipRect(outer.adjusted(-reach, -reach, reach, reach), Qt::IntersectClip);
    paint_with_text_shadow(painter, box.effects.text_shadow, rect, words);
    painter.restore();
}

/// A text block clipped to ``rect``: its raster covers the rect, and the copy
/// replaces the clip with the rect exactly as the direct draw does.
[[nodiscard]] ElementPlacement clipped_to(const QRectF& rect)
{
    ElementPlacement placement;
    placement.bounds = rect;
    placement.replace_clip = [rect](QPainter& p) { p.setClipRect(rect); };
    return placement;
}

}  // namespace

void paint_text_content(QPainter& painter,
                        const scene::SceneTextContent& text,
                        const PaintContext& ctx)
{
    if (text.lines.empty() || text.box.width <= 0 || text.box.height <= 0) {
        return;
    }
    const QRectF box_rect = body_bounds(text.box);
    QImage picture;
    if (const auto url = canvas::fill_picture_url(text.box.fill); url && ctx.cache != nullptr) {
        picture = ctx.cache->get(*url);
    }
    draw_element(painter, ctx.rasters, text, ElementInputs{ctx.faces, picture.cacheKey()},
                 clipped_to(box_rect),
                 [&](QPainter& p) { paint_text_content_live(p, text, ctx.faces, &picture); });
}

void paint_overlay_box(QPainter& painter, const layout::OverlayBox& box, const PaintContext& ctx)
{
    if (box.lines.empty() || box.w <= 0 || box.h <= 0) {
        return;
    }
    const double reach = overlay_reach(box);
    const QRectF bounds =
        overlay_turn(box).mapRect(QRectF(box.x, box.y, box.w, box.h).adjusted(-reach, -reach, reach, reach));
    ElementPlacement placement;
    placement.bounds = bounds;
    placement.replace_clip = [bounds](QPainter& p) { p.setClipRect(bounds, Qt::IntersectClip); };
    draw_element(painter, ctx.rasters, box, ElementInputs{ctx.faces}, placement,
                 [&](QPainter& p) { paint_overlay_box_live(p, box, ctx.faces); });
}

namespace {

/// The string a clock object paints: the wall clock, or what is left of the
/// video it shows with. Without one a countdown shows a sample, so the theme
/// designer can place it against something the right size.
[[nodiscard]] std::string clock_object_text(const canvas::Object& clock,
                                            const QDateTime& when,
                                            const SourceClock* video)
{
    if (clock.clock_mode == scene::ClockMode::Countdown) {
        if (video == nullptr) {
            return "-0:12";
        }
        return media::format_video_countdown(video->position_sec, video->duration_sec);
    }
    return scene::format_wall_clock_hms(when.time().hour(), when.time().minute(),
                                        when.time().second(), clock.clock_24h,
                                        clock.clock_seconds);
}

}  // namespace

void paint_clock_object(QPainter& painter, const canvas::Object& clock,
                        const PaintContext& ctx)
{
    const QDateTime when = ctx.now.isValid() ? ctx.now : QDateTime::currentDateTime();
    const auto text = clock_object_text(clock, when, ctx.video_clock);
    if (text.empty()) {
        return;
    }
    // A clock is a text box whose words arrive at paint time, so it lays out
    // through the same painter: dragging its handles moves and aligns the time
    // exactly as it would any other box.
    layout::OverlayBox box;
    static_cast<canvas::BoxRect&>(box) = clock;
    static_cast<canvas::BoxType&>(box) = clock;
    box.rotation = clock.rotation;
    canvas::fill_type_defaults(box, freeform::type_defaults(freeform::ObjectType::Clock));
    box.line_height = canvas::k_default_line_height;
    box.effects = clock.effects;
    box.lines.push_back(layout::styled_line(model::RichLine{model::TextSpan{text, {}}},
                                            clock.bold, clock.italic, false));

    painter.save();
    if (clock.opacity < 1.0) {
        painter.setOpacity(std::clamp(painter.opacity() * clock.opacity, 0.0, 1.0));
    }
    // The clock's text is part of the box, so each second is its own raster.
    paint_overlay_box(painter, box, ctx);
    painter.restore();
}

bool scene_frame_has_clock(const scene::SceneFrame& frame)
{
    const bool own = std::any_of(frame.objects.begin(), frame.objects.end(),
                                 [](const scene::SceneObject& obj) {
                                     return obj.object
                                         && obj.object->type == canvas::ObjectType::Clock;
                                 });
    return own
        || std::any_of(frame.stacked_below.begin(), frame.stacked_below.end(),
                       [](const scene::SharedSceneFrame& layer) {
                           return scene_frame_has_clock(*layer);
                       });
}

}  // namespace seventhslide::render
