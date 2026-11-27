#pragma once

// QML facade over the in-canvas rich-text rules. Both editors keep one of these
// for the box being typed into; HTML generation also works on a passed line list.

#include <seventhslide/model/rich_text.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <string>
#include <string_view>

Q_MOC_INCLUDE("font_controller.hpp")

namespace seventhslide::app {

/// Run attributes from a QML ribbon map (``bold``, ``scalePct``, ``fontFamily``,
/// ...). Keys that are absent stay unset, so only what the ribbon changed applies.
[[nodiscard]] model::SpanAttrs span_attrs_from_qml(const QVariantMap& m);

/// True when ``plain`` looks like leaked Qt RichText CSS rather than lyrics.
[[nodiscard]] bool editor_plain_polluted(std::string_view plain) noexcept;

/// Strip leaked Qt HTML/CSS chrome from a TextEdit draft.
[[nodiscard]] std::string sanitize_editor_plain(std::string_view plain);

class FontController;

class CanvasRichEdit : public QObject {
    Q_OBJECT
    QML_ELEMENT
    /// Draws a family as slides do. Without one, by the family's own name.
    Q_PROPERTY(seventhslide::app::FontController* fonts MEMBER fonts_)
    Q_PROPERTY(QVariantList lines READ lines WRITE setLines NOTIFY linesChanged)
    Q_PROPERTY(QString plain READ plain NOTIFY linesChanged)
public:
    explicit CanvasRichEdit(QObject* parent = nullptr);

    [[nodiscard]] QVariantList lines() const { return lines_; }
    void setLines(const QVariantList& lines);
    [[nodiscard]] QString plain() const;

    Q_INVOKABLE void loadMarkup(const QString& text, bool bold, bool italic, bool underline);
    Q_INVOKABLE void reconcile(const QString& plain);
    Q_INVOKABLE bool applySelection(int start, int end, const QVariantMap& attrs);
    Q_INVOKABLE void applyAll(const QVariantMap& attrs);
    /// A paragraph's own settings on each one the range touches (the caret's
    /// when ``start`` is ``end``): ``align``, ``before``, ``after``, ``indent``
    /// in px at ``boxFontSize``, ``lineHeight`` as a multiple.
    Q_INVOKABLE void applyParagraph(int start, int end, const QVariantMap& attrs, int boxFontSize);
    Q_INVOKABLE [[nodiscard]] QVariantMap styleAt(int offset, int boxFontSize,
                                                  const QString& defaultColor,
                                                  const QString& defaultFamily) const;
    Q_INVOKABLE [[nodiscard]] QString html(const QVariantMap& box, qreal scale) const;
    Q_INVOKABLE [[nodiscard]] QString htmlFor(const QVariantList& lines, const QVariantMap& box,
                                              qreal scale) const;
    Q_INVOKABLE [[nodiscard]] QString markup() const;
    Q_INVOKABLE [[nodiscard]] QVariantMap objectFlags() const;
    Q_INVOKABLE [[nodiscard]] QString sanitizePlain(const QString& text) const;

signals:
    void linesChanged();

private:
    QVariantList lines_;
    FontController* fonts_ = nullptr;
};

} // namespace seventhslide::app
