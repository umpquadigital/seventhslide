#pragma once

// The app's glyph set: one table of icons, drawn in whatever ink the caller
// passes, so the same idea gets the same glyph everywhere - the operator UI
// (``Icons`` in QML, as ``image://ssicon`` URLs that load synchronously) and
// the web remote (as ``data:`` URLs, since a browser cannot load
// ``image://``).
//
// A glyph's style says how its body is painted:
//   Stroke - outlined; the caller picks the width (2.5 unless a view draws
//            lighter)
//   Fill   - solid in the ink
//   Solid  - solid, with a 1.5 outline in the ink
//   Raw    - every element carries its own paint; {ink} marks the colour
// Glyphs are on a 24x24 viewBox unless they give their own.

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QVariant>

#include <span>
#include <string_view>

namespace seventhslide::app {

namespace glyphs {

enum class Style { Stroke, Fill, Solid, Raw };

struct Glyph {
    std::string_view name;
    Style style = Style::Stroke;
    /// Empty for the usual 24x24.
    std::string_view view_box;
    std::string_view body;
};

[[nodiscard]] std::span<const Glyph> all() noexcept;
/// Glyph ``name``, or null.
[[nodiscard]] const Glyph* find(std::string_view name) noexcept;
/// The SVG document for glyph ``name`` in ``ink`` (a colour as SVG reads it;
/// empty for the default grey). ``stroke_width`` applies to stroke glyphs. An
/// unknown name draws nothing.
[[nodiscard]] QString svg(std::string_view name, const QString& ink, double stroke_width = 2.5);
/// ``svg`` as a ``data:`` URL, for the web remote.
[[nodiscard]] QString data_url(const QString& svg);
/// Every glyph as an SVG ``<symbol id="ic-NAME">``, inside one hidden
/// ``<svg>``, for the web remote to draw with ``<use href="#ic-NAME">``. The
/// symbols paint in ``currentColor``, and stroke glyphs take their width
/// from the page (``stroke-width`` on the using element), so CSS styles them.
[[nodiscard]] QString sprite();

}  // namespace glyphs

/// ``Icons`` in QML: ``Icons.url("trash", Style.textSoft)``.
class Icons : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// Glyph ``kind`` in ``ink`` (a Style colour, or a colour string; none
    /// for the default grey) as an image URL. ``strokeWidth`` applies to
    /// stroke glyphs.
    Q_INVOKABLE [[nodiscard]] QString url(const QString& kind, const QVariant& ink = {},
                                          double strokeWidth = 2.5) const;
    /// A whole SVG document a view built itself, as an image URL.
    Q_INVOKABLE [[nodiscard]] QString svgUrl(const QString& svg) const;

    /// ``ink`` as SVG reads it: a QML colour as #rrggbb (#aarrggbb when it is
    /// see-through), a string as it is.
    [[nodiscard]] static QString ink_name(const QVariant& ink);
};

}  // namespace seventhslide::app
