#pragma once

// Guided song-editor document helpers. Storage is inline markup
// (`<b>/<i>/<u>`, `[G]`, `[---][---]`); the QTextDocument is WYSIWYG.

#include <QString>
#include <QVariantList>

class QTextDocument;

namespace seventhslide::app {

/// Lyrics with tags, chords, and slide-break markers stripped.
[[nodiscard]] QString plain_lyric_preview(const QString& markup);

void load_verse_document(QTextDocument* doc, const QString& markup, const QString& break_label);
[[nodiscard]] QString verse_document_markup(const QTextDocument* doc);

void toggle_verse_format(QTextDocument* doc, int start, int end, const QString& tag);
[[nodiscard]] bool verse_has_format(const QTextDocument* doc, int start, int end, const QString& tag);

void insert_verse_break(QTextDocument* doc, int pos, const QString& break_label);
void set_verse_chord(QTextDocument* doc, int start, int end, const QString& chord);
void apply_verse_editor_spacing(QTextDocument* doc, qreal text_width = -1.0);
void sanitize_verse_breaks(QTextDocument* doc, const QString& break_label);
[[nodiscard]] bool verse_pos_in_break(const QTextDocument* doc, int pos);
void delete_verse_break_at(QTextDocument* doc, int pos);
/// Next insert position after a break block (creates a following block if needed).
[[nodiscard]] int place_after_verse_break(QTextDocument* doc, int pos);
void verse_break_selection(const QTextDocument* doc, int pos, int& start, int& end);
[[nodiscard]] QString verse_chord_at(const QTextDocument* doc, int pos);
/// `{chord, label, color, position, length, bare}` - map to pixels with
/// TextEdit.positionToRectangle. ``label`` shows ♯/♭; ``bare`` is a chord with
/// no word under it.
[[nodiscard]] QVariantList verse_chord_marks(const QTextDocument* doc);

}  // namespace seventhslide::app
