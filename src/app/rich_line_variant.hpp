#pragma once

// model::RichLine <-> the QVariant shape the QML rich-text editors speak.
//
// Both the freeform text editor and the song verse editor hand lines to QML as
// ``{text, spans:[{text, scale, bold, ...}]}`` and read them back the same way,
// so the conversion lives here rather than in whichever facade file needs it
// first.

#include <algorithm>
#include <seventhslide/model/rich_text.hpp>

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

namespace seventhslide::app {

[[nodiscard]] inline bool rich_span_styled(const model::TextSpan& sp) noexcept
{
    return sp.style.has_inline_style();
}

[[nodiscard]] inline bool rich_lines_styled(const std::vector<model::RichLine>& lines) noexcept
{
    for (const auto& line : lines) {
        for (const auto& sp : line) {
            if (rich_span_styled(sp)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] inline QVariantMap rich_span_to_map(const model::TextSpan& sp)
{
    QVariantMap m;
    m.insert(QStringLiteral("text"), QString::fromStdString(sp.text));
    m.insert(QStringLiteral("scale"), sp.style.size_pct);
    m.insert(QStringLiteral("bold"), sp.style.bold);
    m.insert(QStringLiteral("italic"), sp.style.italic);
    m.insert(QStringLiteral("underline"), sp.style.underline);
    m.insert(QStringLiteral("color"), QString::fromStdString(sp.style.color));
    m.insert(QStringLiteral("fontFamily"), QString::fromStdString(sp.style.font_family));
    m.insert(QStringLiteral("baselineRise"), sp.style.baseline_rise_pct);
    if (sp.style.margin_for_space) {
        m.insert(QStringLiteral("marginRight"), sp.style.margin_right_pct);
    }
    if (sp.style.line.any()) {
        m.insert(QStringLiteral("line"), QString::fromStdString(model::format_line_format(sp.style.line)));
    }
    return m;
}

[[nodiscard]] inline model::TextSpan rich_span_from_map(const QVariantMap& m)
{
    model::TextSpan sp;
    sp.text = m.value(QStringLiteral("text")).toString().toStdString();
    sp.style.size_pct = model::clamp_size_pct(m.value(QStringLiteral("scale"), 100).toInt());
    sp.style.bold = m.value(QStringLiteral("bold")).toBool();
    sp.style.italic = m.value(QStringLiteral("italic")).toBool();
    sp.style.underline = m.value(QStringLiteral("underline")).toBool();
    sp.style.color = m.value(QStringLiteral("color")).toString().toStdString();
    sp.style.font_family = m.value(QStringLiteral("fontFamily")).toString().toStdString();
    sp.style.baseline_rise_pct =
        model::clamp_rise_pct(m.value(QStringLiteral("baselineRise"), 0).toInt());
    if (m.contains(QStringLiteral("marginRight"))) {
        sp.style.margin_right_pct =
            std::clamp(m.value(QStringLiteral("marginRight")).toInt(), 0, model::k_max_gap_pct);
        sp.style.margin_for_space = true;
    }
    if (m.contains(QStringLiteral("line"))) {
        sp.style.line = model::parse_line_format(m.value(QStringLiteral("line")).toString().toStdString())
                            .value_or(model::LineFormat{});
    }
    return sp;
}

/// Editor line map -> runs. A line with no ``spans`` carries its style at the top
/// level (theme title boxes); that becomes the single run.
[[nodiscard]] inline model::RichLine rich_line_from_map(const QVariantMap& row)
{
    const QVariantList spans = row.value(QStringLiteral("spans")).toList();
    if (!spans.isEmpty()) {
        model::RichLine line;
        line.reserve(static_cast<std::size_t>(spans.size()));
        for (const auto& spv : spans) {
            line.push_back(rich_span_from_map(spv.toMap()));
        }
        return line;
    }
    return model::RichLine{rich_span_from_map(row)};
}

[[nodiscard]] inline std::vector<model::RichLine> rich_lines_from_variant(const QVariantList& lines)
{
    std::vector<model::RichLine> out;
    out.reserve(static_cast<std::size_t>(lines.size()));
    for (const auto& rowv : lines) {
        out.push_back(rich_line_from_map(rowv.toMap()));
    }
    return out;
}

[[nodiscard]] inline QVariantMap rich_line_to_map(const model::RichLine& line)
{
    QVariantList spans;
    spans.reserve(static_cast<int>(line.size()));
    QString joined;
    for (const auto& sp : line) {
        spans.push_back(rich_span_to_map(sp));
        joined += QString::fromStdString(sp.text);
    }
    QVariantMap row;
    row.insert(QStringLiteral("text"), joined);
    row.insert(QStringLiteral("spans"), spans);
    return row;
}

}  // namespace seventhslide::app
