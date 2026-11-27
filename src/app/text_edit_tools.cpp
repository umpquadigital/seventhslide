#include "text_edit_tools.hpp"

#include "verse_editor.hpp"

#include <seventhslide/layout/text_measurer.hpp>

#include <QFont>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <utility>

namespace seventhslide::app {

namespace {

[[nodiscard]] qreal char_format_font_px(const QTextCharFormat& fmt)
{
    const QFont font = fmt.font();
    if (font.pixelSize() > 0) {
        return static_cast<qreal>(font.pixelSize());
    }
    return 0.0;
}

[[nodiscard]] qreal block_max_font_px(const QTextBlock& block, qreal fallback)
{
    qreal best = 0.0;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (!frag.isValid()) {
            continue;
        }
        best = std::max(best, char_format_font_px(frag.charFormat()));
    }
    return best > 0.0 ? best : fallback;
}

/// Unitless CSS line-height factor (1.15), clamped to the range layout uses;
/// 0 when ``raw`` is unset or not a number.
[[nodiscard]] double css_line_height_factor(qreal raw) noexcept
{
    if (!(raw > 0.0) || !std::isfinite(raw)) {
        return 0.0;
    }
    return layout::clamp_css_line_height(static_cast<double>(raw));
}

[[nodiscard]] qreal document_fallback_font_px(const QTextDocument* doc, qreal explicit_px)
{
    if (explicit_px > 0.0) {
        return explicit_px;
    }
    if (doc == nullptr) {
        return 48.0;
    }
    const int ps = doc->defaultFont().pixelSize();
    return ps > 0 ? static_cast<qreal>(ps) : 48.0;
}

QTextDocument* qml_text_document(QObject* edit)
{
    if (!edit) {
        return nullptr;
    }
    auto* wrap = edit->property("textDocument").value<QQuickTextDocument*>();
    if (!wrap) {
        return nullptr;
    }
    return wrap->textDocument();
}

void selection_range(QObject* edit, int& start, int& end)
{
    start = edit ? edit->property("selectionStart").toInt() : 0;
    end = edit ? edit->property("selectionEnd").toInt() : 0;
    if (end < start) {
        std::swap(start, end);
    }
}

void layout_verse_edit(QObject* edit)
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return;
    }
    const qreal w = edit->property("width").toReal()
                    - edit->property("leftPadding").toReal()
                    - edit->property("rightPadding").toReal();
    const int pos = edit->property("cursorPosition").toInt();
    apply_verse_editor_spacing(doc, w);
    edit->setProperty("cursorPosition", pos);
}

}  // namespace

void apply_text_edit_line_height(QTextDocument* doc, qreal css_factor, qreal document_margin,
                                 qreal fallback_pixel_size)
{
    if (!doc) {
        return;
    }
    const double factor = css_line_height_factor(css_factor);
    if (!(factor > 0.0) && document_margin < 0.0) {
        return;
    }
    if (document_margin >= 0.0) {
        doc->setDocumentMargin(document_margin);
    }

    const qreal fallback = document_fallback_font_px(doc, fallback_pixel_size);

    const auto wanted = [&](const QTextBlock& block) {
        QTextBlockFormat fmt;
        if (factor > 0.0) {
            const qreal size = block_max_font_px(block, fallback);
            fmt.setLineHeight(layout::css_line_height_px(size, factor),
                              QTextBlockFormat::FixedHeight);
        }
        fmt.setTopMargin(0);
        fmt.setBottomMargin(0);
        return fmt;
    };
    const auto settled = [&](const QTextBlock& block) {
        const QTextBlockFormat now = block.blockFormat();
        const QTextBlockFormat fmt = wanted(block);
        return now.topMargin() == 0 && now.bottomMargin() == 0
               && (!fmt.hasProperty(QTextFormat::LineHeight)
                   || (now.lineHeightType() == QTextBlockFormat::FixedHeight
                       && qFuzzyCompare(now.lineHeight(), fmt.lineHeight())));
    };
    // Nothing to do, not even an empty edit: that too tells the TextEdit its
    // text changed, and it asks for this again.
    bool all_settled = true;
    for (QTextBlock block = doc->begin(); block.isValid() && all_settled; block = block.next()) {
        all_settled = settled(block);
    }
    if (all_settled) {
        return;
    }

    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        cursor.setPosition(block.position());
        cursor.mergeBlockFormat(wanted(block));
    }
    cursor.endEditBlock();
    doc->setUndoRedoEnabled(undo);
}

void TextEditTools::setLineHeight(QObject* edit, qreal css_factor, qreal document_margin) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return;
    }

    qreal fallback = -1.0;
    const QVariant font_v = edit->property("font");
    if (font_v.canConvert<QFont>()) {
        const int ps = font_v.value<QFont>().pixelSize();
        if (ps > 0) {
            fallback = static_cast<qreal>(ps);
        }
    }

    const int user_pos = edit->property("cursorPosition").toInt();
    apply_text_edit_line_height(doc, css_factor, document_margin, fallback);
    edit->setProperty("cursorPosition", user_pos);
}

QString TextEditTools::plainText(QObject* edit) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return {};
    }
    // toPlainText() maps block separators to '\n' and Qt's ``&nbsp;`` back to U+0020.
    return doc->toPlainText();
}

QString VerseEditor::plainPreview(const QString& markup) const
{
    return plain_lyric_preview(markup);
}

void VerseEditor::load(QObject* edit, const QString& markup, const QString& break_label) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return;
    }
    load_verse_document(doc, markup, break_label);
    layout_verse_edit(edit);
    edit->setProperty("cursorPosition", 0);
}

QString VerseEditor::markup(QObject* edit) const
{
    return verse_document_markup(qml_text_document(edit));
}

void VerseEditor::toggleFormat(QObject* edit, const QString& tag) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return;
    }
    int start = 0;
    int end = 0;
    selection_range(edit, start, end);
    toggle_verse_format(doc, start, end, tag);
}

bool VerseEditor::hasFormat(QObject* edit, const QString& tag) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return false;
    }
    int start = 0;
    int end = 0;
    selection_range(edit, start, end);
    if (start == end) {
        start = edit->property("cursorPosition").toInt();
        end = start;
    }
    return verse_has_format(doc, start, end, tag);
}

void VerseEditor::insertBreak(QObject* edit, const QString& break_label) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return;
    }
    insert_verse_break(doc, edit->property("cursorPosition").toInt(), break_label);
    layout_verse_edit(edit);
}

void VerseEditor::setChord(QObject* edit, const QString& chord) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return;
    }
    int start = 0;
    int end = 0;
    selection_range(edit, start, end);
    if (start == end) {
        start = edit->property("cursorPosition").toInt();
        end = start;
    }
    set_verse_chord(doc, start, end, chord);
    layout_verse_edit(edit);
}

void VerseEditor::applySpacing(QObject* edit) const
{
    layout_verse_edit(edit);
}

bool VerseEditor::inBreak(QObject* edit) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return false;
    }
    return verse_pos_in_break(doc, edit->property("cursorPosition").toInt());
}

bool VerseEditor::breakAt(QObject* edit, int pos) const
{
    return verse_pos_in_break(qml_text_document(edit), pos);
}

void VerseEditor::selectBreak(QObject* edit) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc || !edit) {
        return;
    }
    int start = 0;
    int end = 0;
    verse_break_selection(doc, edit->property("cursorPosition").toInt(), start, end);
    if (start == end) {
        return;
    }
    QMetaObject::invokeMethod(edit, "select", Q_ARG(int, start), Q_ARG(int, end));
}

void VerseEditor::deleteBreak(QObject* edit) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return;
    }
    delete_verse_break_at(doc, edit->property("cursorPosition").toInt());
    layout_verse_edit(edit);
}

void VerseEditor::placeCursorAfterBreak(QObject* edit) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc || !edit) {
        return;
    }
    const int pos = place_after_verse_break(doc, edit->property("cursorPosition").toInt());
    edit->setProperty("cursorPosition", pos);
}

void VerseEditor::sanitizeBreaks(QObject* edit, const QString& break_label) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return;
    }
    sanitize_verse_breaks(doc, break_label);
}

QString VerseEditor::chordAt(QObject* edit) const
{
    QTextDocument* doc = qml_text_document(edit);
    if (!doc) {
        return {};
    }
    int start = 0;
    int end = 0;
    selection_range(edit, start, end);
    const int pos = (start != end) ? start : edit->property("cursorPosition").toInt();
    return verse_chord_at(doc, pos);
}

QVariantList VerseEditor::chordMarks(QObject* edit) const
{
    return verse_chord_marks(qml_text_document(edit));
}

} // namespace seventhslide::app
