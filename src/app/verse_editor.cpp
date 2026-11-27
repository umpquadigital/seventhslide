#include "verse_editor.hpp"

#include <seventhslide/layout/chords.hpp>
#include <seventhslide/model/rich_text.hpp>

#include <QColor>
#include <QHash>
#include <QList>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextFormat>
#include <QVariant>

#include <algorithm>
#include <iterator>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seventhslide::app {
namespace {

constexpr int k_chord_prop = QTextFormat::UserProperty + 81;
constexpr int k_break_prop = QTextFormat::UserProperty + 82;
constexpr int k_size_pct_prop = QTextFormat::UserProperty + 83;
constexpr int k_color_prop = QTextFormat::UserProperty + 84;
const QChar k_empty_chord_holder(0x200B);
const QColor k_break_bg(36, 32, 24);
const QColor k_break_fg(0x88, 0x88, 0x88);
// Room for a ~10px chord label + a small gap above the lyric. First-block
// topMargin is ignored by QTextDocument; the editor uses extra topPadding.
constexpr int k_chord_top_margin = 14;
constexpr qreal k_plain_line_height = 118.0;
constexpr qreal k_chord_line_height = 118.0;

[[nodiscard]] QString chord_color_hex(const QString& chord)
{
    // Muted so a translucent wash still leaves lyric text readable.
    static const QString pal[] = {
        QStringLiteral("#b8963c"),  // gold
        QStringLiteral("#3d8f82"),  // teal
        QStringLiteral("#b06a5c"),  // coral
        QStringLiteral("#6e7eae"),  // periwinkle
        QStringLiteral("#7a9a48"),  // lime
        QStringLiteral("#a06890"),  // rose
        QStringLiteral("#4a7eaa"),  // sky
        QStringLiteral("#b08648"),  // apricot
    };
    const quint32 h = qHash(chord.trimmed().toLower());
    return pal[h % std::size(pal)];
}

[[nodiscard]] bool is_break_block(const QTextBlock& block)
{
    return block.blockFormat().boolProperty(k_break_prop);
}

[[nodiscard]] QString chord_of(const QTextCharFormat& fmt)
{
    if (!fmt.hasProperty(k_chord_prop)) {
        return {};
    }
    return fmt.stringProperty(k_chord_prop).trimmed();
}

[[nodiscard]] bool block_has_chord(const QTextBlock& block)
{
    if (is_break_block(block)) {
        return false;
    }
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        if (!chord_of(it.fragment().charFormat()).isEmpty()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] model::TextStyle style_from_format(const QTextCharFormat& fmt)
{
    model::TextStyle st;
    st.bold = fmt.fontWeight() >= QFont::Bold;
    st.italic = fmt.fontItalic();
    st.underline = fmt.fontUnderline();
    if (fmt.hasProperty(k_size_pct_prop)) {
        st.size_pct = fmt.intProperty(k_size_pct_prop);
    }
    if (fmt.hasProperty(k_color_prop)) {
        st.color = fmt.stringProperty(k_color_prop).toStdString();
    }
    return st;
}

[[nodiscard]] QTextCharFormat format_from_style(const model::TextStyle& st, const QTextCharFormat& base)
{
    QTextCharFormat fmt = base;
    fmt.setFontWeight(st.bold ? QFont::Bold : QFont::Normal);
    fmt.setFontItalic(st.italic);
    fmt.setFontUnderline(st.underline);
    if (st.size_pct != 100) {
        fmt.setProperty(k_size_pct_prop, st.size_pct);
        const qreal pts = base.fontPointSize() > 0 ? base.fontPointSize() : 12.0;
        fmt.setFontPointSize(std::max(8.0, pts * static_cast<qreal>(st.size_pct) / 100.0));
    }
    if (!st.color.empty()) {
        const QString color = QString::fromStdString(st.color);
        fmt.setProperty(k_color_prop, color);
        QColor parsed(color);
        if (parsed.isValid()) {
            fmt.setForeground(parsed);
        }
    }
    return fmt;
}

void apply_chord(QTextCharFormat& fmt, const QString& chord)
{
    // Always set the property (including empty). mergeCharFormat ignores
    // clearProperty(), so an empty value is how the editor removes a chord.
    fmt.setProperty(k_chord_prop, chord.trimmed());
}

/// A chord read from markup, waiting for the word it goes on.
struct PendingChord {
    QString name;
    QTextCharFormat format;
};

/// A chord with no word under it, kept on an empty-chord holder.
void insert_chord_holder(QTextCursor& cursor, const PendingChord& chord)
{
    QTextCharFormat fmt = chord.format;
    apply_chord(fmt, chord.name);
    cursor.insertText(QString(k_empty_chord_holder), fmt);
}

/// One span of a line. A chord goes on the word after it; one with no word
/// after it (another chord or the line's end comes first) goes on a holder.
/// ``pending`` carries a chord into the next span: its word may start there,
/// and the caller puts any chord still pending at the line's end on a holder.
void insert_text_with_chords(QTextCursor& cursor,
                             const QString& text,
                             const QTextCharFormat& base,
                             std::optional<PendingChord>& pending)
{
    const int n = text.size();
    auto insert_run = [&](const QString& chunk, const QString& chord) {
        if (chunk.isEmpty()) {
            return;
        }
        QTextCharFormat fmt = base;
        apply_chord(fmt, chord);
        cursor.insertText(chunk, fmt);
    };
    // An unclosed '[' is a character of its own.
    auto chord_at = [&](int k) {
        return k < n && text[k] == QLatin1Char('[') && text.indexOf(QLatin1Char(']'), k + 1) >= 0;
    };
    auto is_line_space = [&](int k) {
        return text[k].isSpace() && text[k] != QLatin1Char('\n') && text[k] != QLatin1Char('\r');
    };
    // The spaces between a chord and its word go only where the text has
    // none already: not at the line's start ([G] Amazing), nor after a space
    // (for [A2] us). Mid-word ones stay (of[G] God).
    auto insert_spaces = [&](int from, int to) {
        const int pos = cursor.position();
        if (pos == cursor.block().position() || cursor.document()->characterAt(pos - 1).isSpace()) {
            return;
        }
        insert_run(text.mid(from, to - from), {});
    };

    int i = 0;
    while (i < n) {
        if (chord_at(i)) {
            if (pending) {
                insert_chord_holder(cursor, *pending);
                pending.reset();
            }
            const int close = text.indexOf(QLatin1Char(']'), i + 1);
            const QString name = text.mid(i + 1, close - i - 1).trimmed();
            if (!name.isEmpty()) {
                pending = PendingChord{name, base};
            }
            i = close + 1;
            continue;
        }
        if (pending) {
            int k = i;
            while (k < n && is_line_space(k)) {
                ++k;
            }
            if (k == n) {
                insert_spaces(i, k);
                i = k;
                continue;
            }
            if (!chord_at(k) && text[k] != QLatin1Char('\n') && text[k] != QLatin1Char('\r')) {
                insert_spaces(i, k);
                int j = k + 1;
                while (j < n && !chord_at(j) && !text[j].isSpace()) {
                    ++j;
                }
                insert_run(text.mid(k, j - k), pending->name);
                pending.reset();
                i = j;
                continue;
            }
            insert_chord_holder(cursor, *pending);
            pending.reset();
        }
        int j = i + 1;
        while (j < n && !chord_at(j)) {
            ++j;
        }
        insert_run(text.mid(i, j - i), {});
        i = j;
    }
}

void apply_break_to_block(QTextCursor& cursor, const QString& label, const QTextCharFormat& base)
{
    QTextBlockFormat bf = cursor.blockFormat();
    bf.setProperty(k_break_prop, true);
    bf.setAlignment(Qt::AlignLeft);
    bf.setBackground(k_break_bg);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf = base;
    cf.setFontItalic(true);
    cf.setFontWeight(QFont::Normal);
    cf.setFontUnderline(false);
    cf.setForeground(k_break_fg);
    cf.clearBackground();
    cf.clearProperty(k_chord_prop);
    cursor.insertText(label, cf);
}

void clear_break_format(QTextCursor& cursor)
{
    QTextBlockFormat bf = cursor.blockFormat();
    bf.setProperty(k_break_prop, false);
    bf.setAlignment(Qt::AlignLeft);
    bf.clearBackground();
    cursor.setBlockFormat(bf);
}

[[nodiscard]] QString serialize_block(const QTextBlock& block)
{
    if (is_break_block(block)) {
        return QString::fromUtf8(model::k_forced_split);
    }

    model::RichLine line;
    QString last_chord;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        const QTextCharFormat fmt = frag.charFormat();
        const QString chord = chord_of(fmt);
        QString visible;
        visible.reserve(frag.length());
        for (const QChar c : frag.text()) {
            if (c != k_empty_chord_holder && c != QChar::ParagraphSeparator
                && c != QChar::LineSeparator) {
                visible += c;
            }
        }

        QString body;
        if (!chord.isEmpty() && chord != last_chord) {
            body += QLatin1Char('[');
            body += chord;
            body += QLatin1Char(']');
        }
        if (chord.isEmpty()) {
            last_chord.clear();
        } else {
            last_chord = chord;
        }
        body += visible;
        if (body.isEmpty()) {
            continue;
        }

        model::TextSpan span;
        span.style = style_from_format(fmt);
        span.text = body.toStdString();
        if (!line.empty() && line.back().style == span.style) {
            line.back().text += span.text;
        } else {
            line.push_back(std::move(span));
        }
    }
    return QString::fromStdString(model::format_inline_markup(line));
}

[[nodiscard]] bool format_on(const QTextCharFormat& fmt, const QString& tag)
{
    if (tag == QLatin1String("b")) {
        return fmt.fontWeight() >= QFont::Bold;
    }
    if (tag == QLatin1String("i")) {
        return fmt.fontItalic();
    }
    if (tag == QLatin1String("u")) {
        return fmt.fontUnderline();
    }
    return false;
}

/// Each fragment of ``doc`` that overlaps ``start`` .. ``end`` (start <= end),
/// skipping slide-break blocks. A fragment that only touches an end is
/// outside. ``visit`` returns false to stop.
template <class Visit>
void for_each_fragment(const QTextDocument* doc, int start, int end, Visit visit)
{
    QTextBlock block = doc->findBlock(start);
    while (block.isValid() && block.position() < end) {
        if (!is_break_block(block)) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                const int fs = frag.position();
                const int fe = fs + frag.length();
                if (fe <= start || fs >= end) {
                    continue;
                }
                if (!visit(frag)) {
                    return;
                }
            }
        }
        block = block.next();
    }
}

void expand_to_word(const QTextDocument* doc, int pos, int& start, int& end)
{
    const QString t = doc->toPlainText();
    const int n = t.size();
    pos = std::clamp(pos, 0, n);
    start = pos;
    end = pos;
    auto is_word = [](QChar c) {
        return !c.isSpace() && c != QChar::ParagraphSeparator && c != k_empty_chord_holder;
    };
    while (start > 0 && is_word(t[start - 1])) {
        --start;
    }
    while (end < n && is_word(t[end])) {
        ++end;
    }
}

void trim_range(const QTextDocument* doc, int& start, int& end)
{
    const QString t = doc->toPlainText();
    const int n = t.size();
    start = std::clamp(start, 0, n);
    end = std::clamp(end, 0, n);
    if (end < start) {
        std::swap(start, end);
    }
    while (start < end && t[start].isSpace()) {
        ++start;
    }
    while (end > start && t[end - 1].isSpace()) {
        --end;
    }
}

/// The empty-chord holder a cursor at ``pos`` in ``block`` is on, else the
/// one just before it; -1 when neither is. Matches ``verse_chord_at``.
[[nodiscard]] int chord_holder_at(const QTextDocument* doc, const QTextBlock& block, int pos)
{
    for (const int at : {pos, pos - 1}) {
        if (doc->characterAt(at) != k_empty_chord_holder) {
            continue;
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (at >= frag.position() && at < frag.position() + frag.length()
                && !chord_of(frag.charFormat()).isEmpty()) {
                return at;
            }
        }
    }
    return -1;
}

}  // namespace

QString plain_lyric_preview(const QString& markup)
{
    std::string s = layout::strip_chords(markup.toStdString());
    s = model::strip_markup_tags(s);
    QString q = QString::fromStdString(s);
    q.replace(QChar(0x200B), QString());
    const QStringList raw_lines = q.split(QLatin1Char('\n'));
    QStringList lines;
    lines.reserve(raw_lines.size());
    for (QString line : raw_lines) {
        int b = 0;
        while (b < line.size()) {
            const QChar c = line[b];
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QChar(0x200B)
                || c == QChar::Nbsp) {
                ++b;
                continue;
            }
            break;
        }
        line = line.mid(b);
        while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t'))) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        lines.push_back(line);
    }
    while (!lines.isEmpty() && lines.back().isEmpty()) {
        lines.removeLast();
    }
    while (!lines.isEmpty() && lines.front().isEmpty()) {
        lines.removeFirst();
    }
    return lines.join(QLatin1Char('\n'));
}

void load_verse_document(QTextDocument* doc, const QString& markup, const QString& break_label)
{
    if (!doc) {
        return;
    }
    QTextCharFormat base;
    base.setFont(doc->defaultFont());
    base.setForeground(QColor(0xd4, 0xd4, 0xd4));
    const QString label = break_label.isEmpty() ? QStringLiteral("Slide break") : break_label;

    doc->setDefaultFont(base.font());
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();
    cursor.setCharFormat(base);
    cursor.setBlockCharFormat(base);
    clear_break_format(cursor);

    const auto lines = model::parse_markup_lines(markup.toStdString());
    bool first = true;
    for (const auto& line : lines) {
        if (!first) {
            cursor.insertBlock();
            clear_break_format(cursor);
            cursor.setBlockCharFormat(base);
            cursor.setCharFormat(base);
        }
        first = false;
        if (model::is_forced_break_line(line)) {
            apply_break_to_block(cursor, label, base);
            continue;
        }
        std::optional<PendingChord> pending;
        for (const auto& span : line) {
            const QTextCharFormat fmt = format_from_style(span.style, base);
            insert_text_with_chords(cursor, QString::fromStdString(span.text), fmt, pending);
        }
        if (pending) {
            insert_chord_holder(cursor, *pending);
        }
    }
    cursor.endEditBlock();
    doc->clearUndoRedoStacks();
}

QString verse_document_markup(const QTextDocument* doc)
{
    if (!doc) {
        return {};
    }
    QString out;
    bool first = true;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (!first) {
            out += QLatin1Char('\n');
        }
        first = false;
        out += serialize_block(block);
    }
    while (out.endsWith(QLatin1Char('\n'))) {
        out.chop(1);
    }
    return out;
}

void toggle_verse_format(QTextDocument* doc, int start, int end, const QString& tag)
{
    if (!doc || tag.isEmpty() || start == end) {
        return;
    }
    if (end < start) {
        std::swap(start, end);
    }
    bool all_on = true;
    bool any = false;
    // One fragment that is off decides it: the range turns on.
    for_each_fragment(doc, start, end, [&](const QTextFragment& frag) {
        any = true;
        all_on = format_on(frag.charFormat(), tag);
        return all_on;
    });
    if (!any) {
        return;
    }
    const bool on = !all_on;
    QTextCursor cursor(doc);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QTextCharFormat fmt;
    if (tag == QLatin1String("b")) {
        fmt.setFontWeight(on ? QFont::Bold : QFont::Normal);
    } else if (tag == QLatin1String("i")) {
        fmt.setFontItalic(on);
    } else if (tag == QLatin1String("u")) {
        fmt.setFontUnderline(on);
    } else {
        return;
    }
    cursor.mergeCharFormat(fmt);
}

bool verse_has_format(const QTextDocument* doc, int start, int end, const QString& tag)
{
    if (!doc || tag.isEmpty()) {
        return false;
    }
    if (end < start) {
        std::swap(start, end);
    }
    if (start == end) {
        const QTextBlock block = doc->findBlock(start);
        if (!block.isValid() || is_break_block(block)) {
            return false;
        }
        QTextCursor cursor(const_cast<QTextDocument*>(doc));
        cursor.setPosition(start);
        return format_on(cursor.charFormat(), tag);
    }
    bool on = true;
    for_each_fragment(doc, start, end, [&](const QTextFragment& frag) {
        on = format_on(frag.charFormat(), tag);
        return on;
    });
    return on;
}

void insert_verse_break(QTextDocument* doc, int pos, const QString& break_label)
{
    if (!doc) {
        return;
    }
    QTextCharFormat base;
    base.setFont(doc->defaultFont());
    const QString label = break_label.isEmpty() ? QStringLiteral("Slide break") : break_label;
    pos = std::clamp(pos, 0, doc->characterCount() > 0 ? doc->characterCount() - 1 : 0);

    QTextCursor cursor(doc);
    cursor.setPosition(pos);
    cursor.beginEditBlock();

    QTextCursor rest(cursor);
    rest.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    const QTextDocumentFragment remainder = rest.selection();
    const bool had_remainder = rest.hasSelection();
    rest.removeSelectedText();

    if (cursor.positionInBlock() > 0) {
        cursor.insertBlock();
        clear_break_format(cursor);
    }

    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    apply_break_to_block(cursor, label, base);

    // Only split a following block when this line had leftover text. An extra
    // empty block after the break shows as a blank line that backspace then
    // treats as deleting the slide break itself.
    if (had_remainder) {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock();
        clear_break_format(cursor);
        cursor.insertFragment(remainder);
        cursor.setPosition(cursor.block().position());
    }
    cursor.endEditBlock();
}

void set_verse_chord(QTextDocument* doc, int start, int end, const QString& chord)
{
    if (!doc) {
        return;
    }
    if (end < start) {
        std::swap(start, end);
    }
    QString name = chord.trimmed();
    name.remove(QLatin1Char('['));
    name.remove(QLatin1Char(']'));

    if (start == end) {
        const QTextBlock block = doc->findBlock(start);
        if (!block.isValid() || is_break_block(block)) {
            return;
        }
        // A cursor at a chord with no word (a holder) changes that chord,
        // the one the chord field shows; clearing it removes the holder.
        const int holder = chord_holder_at(doc, block, start);
        if (holder >= 0) {
            QTextCursor cursor(doc);
            cursor.setPosition(holder);
            cursor.setPosition(holder + 1, QTextCursor::KeepAnchor);
            if (name.isEmpty()) {
                cursor.removeSelectedText();
            } else {
                QTextCharFormat fmt = cursor.charFormat();
                apply_chord(fmt, name);
                cursor.setCharFormat(fmt);
            }
            return;
        }
        expand_to_word(doc, start, start, end);
    }
    trim_range(doc, start, end);
    if (start == end) {
        return;
    }
    const QTextBlock block = doc->findBlock(start);
    if (!block.isValid() || is_break_block(block)) {
        return;
    }

    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    int pos = start;
    while (pos < end) {
        cursor.setPosition(pos);
        const QTextBlock cur_block = cursor.block();
        if (!cur_block.isValid() || is_break_block(cur_block)) {
            break;
        }
        const int block_end = cur_block.position() + std::max(0, cur_block.length() - 1);
        int run_end = std::min(end, block_end);
        for (auto it = cur_block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            const int fs = frag.position();
            const int fe = fs + frag.length();
            if (pos >= fs && pos < fe) {
                run_end = std::min(run_end, fe);
                break;
            }
        }
        if (run_end <= pos) {
            ++pos;
            continue;
        }
        QTextCharFormat fmt = cursor.charFormat();
        apply_chord(fmt, name);
        cursor.setPosition(pos);
        cursor.setPosition(run_end, QTextCursor::KeepAnchor);
        cursor.setCharFormat(fmt);
        pos = run_end;
    }
    cursor.endEditBlock();
}

void apply_verse_editor_spacing(QTextDocument* doc, qreal text_width)
{
    if (!doc) {
        return;
    }
    doc->setDocumentMargin(0);
    if (text_width > 32.0) {
        doc->setTextWidth(text_width);
    }

    QTextCursor cursor(doc);
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    cursor.beginEditBlock();
    bool first = true;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat fmt = block.blockFormat();
        if (is_break_block(block)) {
            fmt.setLineHeight(k_plain_line_height, QTextBlockFormat::ProportionalHeight);
            fmt.setTopMargin(6);
            fmt.setBottomMargin(6);
        } else if (block_has_chord(block)) {
            fmt.setLineHeight(k_chord_line_height, QTextBlockFormat::ProportionalHeight);
            // First-block topMargin is consumed by QTextDocument; QML adds
            // extra topPadding only when this first line itself has a chord.
            fmt.setTopMargin(first ? 0 : k_chord_top_margin);
            fmt.setBottomMargin(0);
        } else {
            fmt.setLineHeight(k_plain_line_height, QTextBlockFormat::ProportionalHeight);
            fmt.setTopMargin(0);
            fmt.setBottomMargin(0);
        }
        cursor.setPosition(block.position());
        cursor.setBlockFormat(fmt);
        first = false;
    }
    cursor.endEditBlock();
    doc->setUndoRedoEnabled(undo);
}

void sanitize_verse_breaks(QTextDocument* doc, const QString& break_label)
{
    if (!doc) {
        return;
    }
    const QString label = break_label.isEmpty() ? QStringLiteral("Slide break") : break_label;
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    QTextBlock block = doc->begin();
    while (block.isValid()) {
        const QTextBlock next = block.next();
        if (is_break_block(block)) {
            const QString t = block.text();
            if (t != label) {
                if (t.isEmpty() || label.startsWith(t)) {
                    cursor.setPosition(block.position());
                    if (next.isValid()) {
                        cursor.setPosition(next.position(), QTextCursor::KeepAnchor);
                        cursor.removeSelectedText();
                    } else {
                        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                        cursor.removeSelectedText();
                        clear_break_format(cursor);
                    }
                } else {
                    cursor.setPosition(block.position());
                    clear_break_format(cursor);
                }
            }
        }
        block = next;
    }
    cursor.endEditBlock();
}

bool verse_pos_in_break(const QTextDocument* doc, int pos)
{
    if (!doc) {
        return false;
    }
    const QTextBlock block = doc->findBlock(pos);
    return block.isValid() && is_break_block(block);
}

void delete_verse_break_at(QTextDocument* doc, int pos)
{
    if (!doc) {
        return;
    }
    QTextBlock block = doc->findBlock(pos);
    if (!block.isValid() || !is_break_block(block)) {
        return;
    }
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    cursor.setPosition(block.position());
    if (block.next().isValid()) {
        cursor.setPosition(block.next().position(), QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        clear_break_format(cursor);
        if (block.previous().isValid()) {
            cursor.deletePreviousChar();
        }
    }
    cursor.endEditBlock();
}

int place_after_verse_break(QTextDocument* doc, int pos)
{
    if (!doc) {
        return pos;
    }
    QTextBlock block = doc->findBlock(pos);
    if (!block.isValid() || !is_break_block(block)) {
        return pos;
    }
    QTextCursor cursor(doc);
    cursor.setPosition(block.position());
    cursor.movePosition(QTextCursor::EndOfBlock);
    if (!block.next().isValid()) {
        cursor.insertBlock();
        clear_break_format(cursor);
    } else {
        cursor.setPosition(block.next().position());
        if (is_break_block(cursor.block())) {
            cursor.insertBlock();
            clear_break_format(cursor);
        }
    }
    return cursor.position();
}

void verse_break_selection(const QTextDocument* doc, int pos, int& start, int& end)
{
    start = pos;
    end = pos;
    if (!doc) {
        return;
    }
    const QTextBlock block = doc->findBlock(pos);
    if (!block.isValid() || !is_break_block(block)) {
        return;
    }
    start = block.position();
    end = start + std::max(0, block.length() - 1);
}

QString verse_chord_at(const QTextDocument* doc, int pos)
{
    if (!doc) {
        return {};
    }
    const QTextBlock block = doc->findBlock(pos);
    if (!block.isValid() || is_break_block(block)) {
        return {};
    }
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        const int fs = frag.position();
        const int fe = fs + frag.length();
        if (pos >= fs && pos < fe) {
            return chord_of(frag.charFormat());
        }
        if (pos == fe && pos == fs + frag.length()) {
            const QString c = chord_of(frag.charFormat());
            if (!c.isEmpty()) {
                return c;
            }
        }
    }
    QTextCursor cursor(const_cast<QTextDocument*>(doc));
    cursor.setPosition(std::max(0, pos));
    return chord_of(cursor.charFormat());
}

QVariantList verse_chord_marks(const QTextDocument* doc)
{
    QVariantList out;
    if (!doc) {
        return out;
    }

    struct Run {
        int pos = 0;
        int len = 0;
        QString chord;
        bool bare = true;
    };
    QList<Run> runs;
    QString last;
    int run_pos = -1;
    int run_len = 0;
    bool run_bare = true;

    auto flush = [&]() {
        if (run_pos >= 0 && run_len > 0 && !last.isEmpty()) {
            runs.push_back(Run{run_pos, run_len, last, run_bare});
        }
        run_pos = -1;
        run_len = 0;
        run_bare = true;
        last.clear();
    };

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (is_break_block(block)) {
            flush();
            continue;
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            QString visible;
            for (const QChar c : frag.text()) {
                if (c != k_empty_chord_holder && c != QChar::ParagraphSeparator
                    && c != QChar::LineSeparator) {
                    visible += c;
                }
            }
            const QString chord = chord_of(frag.charFormat());
            // A chord with no word under it sits on a holder: it is marked bare.
            const bool bare = visible.trimmed().isEmpty();
            if (chord.isEmpty() || (bare && !frag.text().contains(k_empty_chord_holder))) {
                flush();
                continue;
            }
            if (chord == last && run_pos + run_len == frag.position()) {
                run_len += frag.length();
                run_bare = run_bare && bare;
                continue;
            }
            flush();
            last = chord;
            run_pos = frag.position();
            run_len = frag.length();
            run_bare = bare;
        }
        flush();
    }

    for (const Run& run : runs) {
        QVariantMap row;
        row.insert(QStringLiteral("chord"), run.chord);
        row.insert(QStringLiteral("label"),
                   QString::fromStdString(layout::format_chord_accidentals(run.chord.toStdString())));
        row.insert(QStringLiteral("bare"), run.bare);
        row.insert(QStringLiteral("color"), chord_color_hex(run.chord));
        row.insert(QStringLiteral("position"), run.pos);
        row.insert(QStringLiteral("length"), run.len);
        out.push_back(row);
    }
    return out;
}

}  // namespace seventhslide::app
