#pragma once

// QML helpers over a TextEdit's QTextDocument. None of these hold state or
// touch the session: they read and rewrite the document QML hands them.
//
//   TextEditTools - line boxes and plain text for the in-canvas rich editors
//   VerseEditor   - the song editor's guided verse document (see verse_editor.hpp)
//
// A TextEdit arrives as QObject* because QQuickTextEdit is private API; its
// document is reached through the public ``textDocument`` property.

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QVariantList>

class QTextDocument;

namespace seventhslide::app {

class TextEditTools : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// Apply CSS line-height (``ceil(font_px * factor)`` as FixedHeight) to a
    /// TextEdit. ``cssFactor`` is unitless (1.15). ``documentMargin`` < 0
    /// leaves the document margin unchanged; 0 matches QML Text.
    Q_INVOKABLE void setLineHeight(QObject* edit, qreal cssFactor,
                                   qreal documentMargin = -1.0) const;
    /// Plain text of a RichText TextEdit, straight from its QTextDocument.
    /// Scraping the ``text`` property's serialized HTML instead inserts a blank
    /// line per paragraph, because Qt writes each block tag on its own line.
    Q_INVOKABLE QString plainText(QObject* edit) const;
};

class VerseEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// Lyrics with tags / chords / slide-break markers removed (verse list preview).
    Q_INVOKABLE QString plainPreview(const QString& markup) const;
    /// Load inline verse markup into the TextEdit as WYSIWYG rich text.
    Q_INVOKABLE void load(QObject* edit, const QString& markup,
                          const QString& breakLabel = QString()) const;
    Q_INVOKABLE QString markup(QObject* edit) const;
    Q_INVOKABLE void toggleFormat(QObject* edit, const QString& tag) const;
    Q_INVOKABLE bool hasFormat(QObject* edit, const QString& tag) const;
    Q_INVOKABLE void insertBreak(QObject* edit, const QString& breakLabel = QString()) const;
    Q_INVOKABLE void setChord(QObject* edit, const QString& chord) const;
    Q_INVOKABLE void applySpacing(QObject* edit) const;
    Q_INVOKABLE bool inBreak(QObject* edit) const;
    Q_INVOKABLE bool breakAt(QObject* edit, int pos) const;
    Q_INVOKABLE void selectBreak(QObject* edit) const;
    Q_INVOKABLE void deleteBreak(QObject* edit) const;
    Q_INVOKABLE void placeCursorAfterBreak(QObject* edit) const;
    Q_INVOKABLE void sanitizeBreaks(QObject* edit, const QString& breakLabel) const;
    Q_INVOKABLE QString chordAt(QObject* edit) const;
    /// ``{chord, label, color, position, length, bare}``; map to pixels with
    /// TextEdit.positionToRectangle.
    Q_INVOKABLE QVariantList chordMarks(QObject* edit) const;
};

/// Match output CSS line-height on a QTextDocument (used by TextEdit).
/// Each block gets ``FixedHeight = ceil(max_font_px * factor)`` - Qt's unitless
/// CSS ``line-height`` is ProportionalHeight (font leading), which is taller than
/// the painter's ``size_px * factor`` and makes upcoming lines overflow the box.
/// ``css_factor`` is unitless (1.15); values >= 10 are treated as percent (115 -> 1.15).
/// ``document_margin`` < 0 leaves the margin unchanged; 0 matches QQuickText.
/// ``fallback_pixel_size`` is used when a block has no explicit px size (TextEdit font).
void apply_text_edit_line_height(QTextDocument* doc, qreal css_factor,
                                 qreal document_margin = -1.0,
                                 qreal fallback_pixel_size = -1.0);

}  // namespace seventhslide::app
