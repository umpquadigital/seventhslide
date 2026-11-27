#include "canvas_rich_edit.hpp"

#include "font_controller.hpp"
#include "rich_line_variant.hpp"
#include "text_offsets.hpp"

#include <seventhslide/layout/editor_html.hpp>
#include <seventhslide/model/rich_text.hpp>

#include <algorithm>
#include <cmath>

namespace seventhslide::app {
namespace {

[[nodiscard]] QVariantList maps_from_lines(const std::vector<model::RichLine>& lines)
{
    QVariantList out;
    out.reserve(static_cast<int>(lines.size()));
    for (const auto& line : lines) {
        out.push_back(rich_line_to_map(line));
    }
    if (out.isEmpty()) {
        out.push_back(rich_line_to_map({model::TextSpan{}}));
    }
    return out;
}

/// A box's type as both editors hand it over (camelCase, the way QML names
/// it): fontSize, fontColor, fontFamily, lineHeight, letterSpacing, lineGap.
/// A key left out takes the canvas default.
[[nodiscard]] layout::EditorHtmlOptions html_opts(const QVariantMap& box, qreal scale)
{
    layout::EditorHtmlOptions opts;
    opts.font_size =
        std::max(8, box.value(QStringLiteral("fontSize"), canvas::k_default_font_size).toInt());
    const QString color = box.value(QStringLiteral("fontColor")).toString();
    opts.color = color.isEmpty() ? std::string(canvas::k_default_font_color) : color.toStdString();
    const QString family = box.value(QStringLiteral("fontFamily")).toString();
    opts.family = family.isEmpty() ? std::string(canvas::k_default_font_family) : family.toStdString();
    opts.stage_scale = scale > 0 ? scale : 1.0;
    const double lh =
        box.value(QStringLiteral("lineHeight"), canvas::k_default_line_height).toDouble();
    opts.line_height = lh > 0 ? lh : canvas::k_default_line_height;
    opts.letter_spacing = box.value(QStringLiteral("letterSpacing"), 0).toDouble();
    opts.line_gap = box.value(QStringLiteral("lineGap"), 0).toInt();
    return opts;
}

/// ``rows`` as lines; none gives one empty line.
[[nodiscard]] std::vector<model::RichLine> html_lines_from_variant(const QVariantList& rows)
{
    auto out = rich_lines_from_variant(rows);
    if (out.empty()) {
        out.emplace_back();
    }
    return out;
}

} // namespace

model::SpanAttrs span_attrs_from_qml(const QVariantMap& m)
{
    model::SpanAttrs a;
    if (m.contains(QStringLiteral("scalePct"))) {
        a.size_pct = m.value(QStringLiteral("scalePct")).toInt();
    } else if (m.contains(QStringLiteral("scale"))) {
        a.size_pct = m.value(QStringLiteral("scale")).toInt();
    }
    if (m.contains(QStringLiteral("bold"))) {
        a.bold = m.value(QStringLiteral("bold")).toBool();
    }
    if (m.contains(QStringLiteral("italic"))) {
        a.italic = m.value(QStringLiteral("italic")).toBool();
    }
    if (m.contains(QStringLiteral("underline"))) {
        a.underline = m.value(QStringLiteral("underline")).toBool();
    }
    if (m.contains(QStringLiteral("color"))) {
        a.color = m.value(QStringLiteral("color")).toString().toStdString();
    }
    if (m.contains(QStringLiteral("fontFamily"))) {
        a.font_family = m.value(QStringLiteral("fontFamily")).toString().toStdString();
    }
    if (m.contains(QStringLiteral("baselineRise"))) {
        a.baseline_rise_pct = m.value(QStringLiteral("baselineRise")).toInt();
    }
    return a;
}

CanvasRichEdit::CanvasRichEdit(QObject* parent)
    : QObject(parent)
{
}

void CanvasRichEdit::setLines(const QVariantList& lines)
{
    if (lines_ == lines) {
        return;
    }
    lines_ = lines;
    emit linesChanged();
}

QString CanvasRichEdit::plain() const
{
    return QString::fromStdString(model::plain_text_lines(rich_lines_from_variant(lines_)));
}

void CanvasRichEdit::loadMarkup(const QString& text, bool bold, bool italic, bool underline)
{
    setLines(maps_from_lines(
        model::parse_editor_lines(text.toStdString(), bold, italic, underline)));
}

void CanvasRichEdit::reconcile(const QString& plain)
{
    const auto next =
        model::reconcile_rich_lines(rich_lines_from_variant(lines_), plain.toStdString());
    setLines(maps_from_lines(next));
}

bool CanvasRichEdit::applySelection(int start, int end, const QVariantMap& attrs)
{
    int a = std::min(start, end);
    int b = std::max(start, end);
    if (b <= a) {
        return false;
    }
    const QString draft = plain();
    const int n = static_cast<int>(draft.size());
    a = std::max(0, std::min(a, n));
    b = std::max(a, std::min(b, n));
    auto lines = rich_lines_from_variant(lines_);
    lines = model::apply_attrs_to_selection(std::move(lines), utf8_offset(draft, a),
                                            utf8_offset(draft, b), span_attrs_from_qml(attrs));
    setLines(maps_from_lines(lines));
    return true;
}

void CanvasRichEdit::applyAll(const QVariantMap& attrs)
{
    auto lines = rich_lines_from_variant(lines_);
    lines = model::apply_attrs_to_all(std::move(lines), span_attrs_from_qml(attrs));
    setLines(maps_from_lines(lines));
}

void CanvasRichEdit::applyParagraph(int start, int end, const QVariantMap& attrs, int boxFontSize)
{
    const QString draft = plain();
    const int n = static_cast<int>(draft.size());
    const int a = std::clamp(std::min(start, end), 0, n);
    const int b = std::clamp(std::max(start, end), a, n);
    const double fs = std::max(8, boxFontSize);
    const auto pct_of = [&](const QString& key) -> std::optional<int> {
        if (!attrs.contains(key)) {
            return std::nullopt;
        }
        return static_cast<int>(std::lround(std::max(0.0, attrs.value(key).toDouble()) / fs * 100.0));
    };
    model::LineFormatAttrs format;
    if (attrs.contains(QStringLiteral("align"))) {
        format.align = attrs.value(QStringLiteral("align")).toString().toStdString();
    }
    format.before_pct = pct_of(QStringLiteral("before"));
    format.after_pct = pct_of(QStringLiteral("after"));
    format.indent_pct = pct_of(QStringLiteral("indent"));
    if (attrs.contains(QStringLiteral("lineHeight"))) {
        format.line_pct = static_cast<int>(std::lround(attrs.value(QStringLiteral("lineHeight")).toDouble() * 100.0));
    }
    setLines(maps_from_lines(model::apply_line_format(rich_lines_from_variant(lines_), utf8_offset(draft, a),
                                                      utf8_offset(draft, b), format)));
}

QVariantMap CanvasRichEdit::styleAt(int offset, int boxFontSize, const QString& defaultColor,
                                    const QString& defaultFamily) const
{
    const auto lines = rich_lines_from_variant(lines_);
    const auto at = utf8_offset(plain(), offset);
    const auto st = model::style_at_offset(lines, at);
    const int fs = std::max(8, boxFontSize);
    QVariantMap m;
    m.insert(QStringLiteral("size"),
             std::max(8, static_cast<int>(std::lround(
                             static_cast<double>(fs) * (st.size_pct / 100.0)))));
    m.insert(QStringLiteral("bold"), st.bold);
    m.insert(QStringLiteral("italic"), st.italic);
    m.insert(QStringLiteral("underline"), st.underline);
    m.insert(QStringLiteral("color"),
             st.color.empty() ? defaultColor : QString::fromStdString(st.color));
    m.insert(QStringLiteral("family"),
             st.font_family.empty() ? defaultFamily : QString::fromStdString(st.font_family));
    m.insert(QStringLiteral("baselineRise"), st.baseline_rise_pct);
    // The paragraph's own settings; "" or -1 where it goes by the box.
    const auto format = model::line_format_at_offset(lines, at);
    const auto px_of = [fs](int pct) { return pct < 0 ? -1.0 : pct * fs / 100.0; };
    m.insert(QStringLiteral("align"), QString::fromStdString(format.align));
    m.insert(QStringLiteral("before"), px_of(format.before_pct));
    m.insert(QStringLiteral("after"), px_of(format.after_pct));
    m.insert(QStringLiteral("indent"), px_of(format.indent_pct));
    m.insert(QStringLiteral("lineHeight"), format.line_pct > 0 ? format.line_pct / 100.0 : -1.0);
    return m;
}

QString CanvasRichEdit::html(const QVariantMap& box, qreal scale) const
{
    return htmlFor(lines_, box, scale);
}

QString CanvasRichEdit::htmlFor(const QVariantList& lines, const QVariantMap& box,
                                qreal scale) const
{
    auto opts = html_opts(box, scale);
    if (fonts_ != nullptr) {
        // Only what the view draws: the text keeps the family it names.
        opts.painted_family = [fonts = fonts_](const std::string& family) {
            return fonts->painted(QString::fromStdString(family)).toStdString();
        };
    }
    return QString::fromStdString(layout::editor_html(html_lines_from_variant(lines), opts));
}

QString CanvasRichEdit::markup() const
{
    return QString::fromStdString(model::format_markup_lines(rich_lines_from_variant(lines_)));
}

QVariantMap CanvasRichEdit::objectFlags() const
{
    const auto formatted = model::format_editor_object(rich_lines_from_variant(lines_));
    QVariantMap m;
    m.insert(QStringLiteral("text"), QString::fromStdString(formatted.text));
    m.insert(QStringLiteral("bold"), formatted.bold);
    m.insert(QStringLiteral("italic"), formatted.italic);
    m.insert(QStringLiteral("underline"), formatted.underline);
    m.insert(QStringLiteral("mixed"), formatted.mixed);
    return m;
}

QString CanvasRichEdit::sanitizePlain(const QString& text) const
{
    return QString::fromStdString(sanitize_editor_plain(text.toStdString()));
}

bool editor_plain_polluted(std::string_view plain) noexcept
{
    auto has = [&](std::string_view needle) {
        return plain.find(needle) != std::string_view::npos;
    };
    // Qt RichText serialisation leaks stylesheet rules into toPlainText() if a
    // caller ever reads the HTML source instead of the document.
    // A known limit, kept as it was and not wanted: any text holding both
    // words counts, and then has its tags taken out.
    if (plain.find("white-space") != std::string_view::npos
        && plain.find("pre") != std::string_view::npos) {
        return true;
    }
    return has("li.unchecked::marker") || has("li.checked::marker") || has("p, li {")
        || has("p,li {");
}

std::string sanitize_editor_plain(std::string_view plain)
{
    if (!editor_plain_polluted(plain)) {
        return std::string(plain);
    }
    std::string t = model::strip_markup_tags(plain);
    auto strip_rule = [&](std::string_view open) {
        for (;;) {
            const auto pos = t.find(open);
            if (pos == std::string::npos) {
                break;
            }
            const auto end = t.find('}', pos);
            if (end == std::string::npos) {
                // A known limit, kept as it was and not wanted: a rule never
                // closed takes the rest of the draft with it.
                t.erase(pos);
                break;
            }
            t.erase(pos, end - pos + 1);
        }
    };
    strip_rule("p, li {");
    strip_rule("p,li {");
    strip_rule("hr {");
    strip_rule("li.unchecked::marker {");
    strip_rule("li.checked::marker {");
    std::string out;
    out.reserve(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '\n') {
            while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
                out.pop_back();
            }
            if (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') {
                continue;
            }
            out.push_back('\n');
            continue;
        }
        out.push_back(t[i]);
    }
    std::size_t b = 0;
    while (b < out.size() && (out[b] == ' ' || out[b] == '\t' || out[b] == '\n')) {
        ++b;
    }
    std::size_t e = out.size();
    while (e > b && (out[e - 1] == ' ' || out[e - 1] == '\t' || out[e - 1] == '\n')) {
        --e;
    }
    return out.substr(b, e - b);
}

} // namespace seventhslide::app
