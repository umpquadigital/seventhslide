#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "verse_editor.hpp"

#include <QFont>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>

using seventhslide::app::apply_verse_editor_spacing;
using seventhslide::app::insert_verse_break;
using seventhslide::app::load_verse_document;
using seventhslide::app::plain_lyric_preview;
using seventhslide::app::set_verse_chord;
using seventhslide::app::toggle_verse_format;
using seventhslide::app::verse_chord_at;
using seventhslide::app::verse_chord_marks;
using seventhslide::app::verse_document_markup;
using seventhslide::app::verse_has_format;

TEST_CASE("plain_lyric_preview strips tags, chords, and breaks", "[app][verse_editor]")
{
    REQUIRE(plain_lyric_preview(QString()) == QString());
    REQUIRE(plain_lyric_preview(QStringLiteral("<b>Hello</b> <i>world</i>"))
            == QStringLiteral("Hello world"));
    REQUIRE(plain_lyric_preview(QStringLiteral("[G]Amazing <u>grace</u>"))
            == QStringLiteral("Amazing grace"));
    REQUIRE(plain_lyric_preview(QStringLiteral("[G] Amazing\n[C]  Hello"))
            == QStringLiteral("Amazing\nHello"));
    REQUIRE(plain_lyric_preview(
                QStringLiteral("[D/F#] Fullness of[G] God in[Em7] helpless[D] babe!"))
            == QStringLiteral("Fullness of God in helpless babe!"));
    REQUIRE(plain_lyric_preview(QStringLiteral("Line one\n[---][---]\nLine two"))
            == QStringLiteral("Line one\nLine two"));
    REQUIRE(plain_lyric_preview(QStringLiteral("<size=80>Soft</size>"))
            == QStringLiteral("Soft"));
}

TEST_CASE("verse document round-trips markup", "[app][verse_editor]")
{
    seventhslide::test::ensure_qt_application();
    QTextDocument doc;
    doc.setDefaultFont(QFont(QStringLiteral("Sans Serif"), 12));

    SECTION("bold italic underline")
    {
        const QString src = QStringLiteral("<b>Bold</b> <i>em</i> <u>under</u>");
        load_verse_document(&doc, src, QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc) == src);
    }

    SECTION("chords stay attached")
    {
        const QString src = QStringLiteral("[G]Amazing [C]grace");
        load_verse_document(&doc, src, QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc) == src);
        REQUIRE(verse_chord_at(&doc, 0) == QStringLiteral("G"));
        apply_verse_editor_spacing(&doc, 400);
        const auto marks = verse_chord_marks(&doc);
        REQUIRE(marks.size() == 2);
        REQUIRE(marks[0].toMap().value(QStringLiteral("chord")).toString()
                == QStringLiteral("G"));
        REQUIRE(marks[0].toMap().value(QStringLiteral("length")).toInt() >= 7);
        REQUIRE(doc.begin().blockFormat().topMargin() == 0);
        REQUIRE(marks[1].toMap().value(QStringLiteral("chord")).toString()
                == QStringLiteral("C"));
    }

    SECTION("later chord lines keep a compact top margin")
    {
        load_verse_document(&doc, QStringLiteral("[G]Hello\n[C]World"),
                            QStringLiteral("Slide break"));
        apply_verse_editor_spacing(&doc, 400);
        REQUIRE(doc.begin().blockFormat().topMargin() == 0);
        REQUIRE(doc.begin().next().blockFormat().topMargin() >= 12);
        REQUIRE(doc.begin().next().blockFormat().topMargin() <= 16);
    }

    SECTION("second-line chord does not pad the first block")
    {
        load_verse_document(&doc, QStringLiteral("Amazing grace\n[G]How sweet"),
                            QStringLiteral("Slide break"));
        apply_verse_editor_spacing(&doc, 400);
        REQUIRE(doc.begin().blockFormat().topMargin() == 0);
        REQUIRE(doc.begin().next().blockFormat().topMargin() >= 12);
        REQUIRE(doc.begin().next().blockFormat().topMargin() <= 16);
    }

    SECTION("leading chord does not insert a space")
    {
        load_verse_document(&doc, QStringLiteral("[G] Amazing"), QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc) == QStringLiteral("[G]Amazing"));
        REQUIRE(plain_lyric_preview(verse_document_markup(&doc)) == QStringLiteral("Amazing"));
    }

    SECTION("mid-line spaces after chords are kept")
    {
        const QString src =
            QStringLiteral("[D/F#] Fullness of[G] God in[Em7] helpless[D] babe!");
        load_verse_document(&doc, src, QStringLiteral("Slide break"));
        REQUIRE(doc.toPlainText() == QStringLiteral("Fullness of God in helpless babe!"));
        REQUIRE(plain_lyric_preview(verse_document_markup(&doc))
                == QStringLiteral("Fullness of God in helpless babe!"));
    }

    SECTION("mid-line spaces survive across style spans")
    {
        load_verse_document(&doc, QStringLiteral("<b>of</b>[G] God"),
                            QStringLiteral("Slide break"));
        REQUIRE(doc.toPlainText() == QStringLiteral("of God"));
        REQUIRE(plain_lyric_preview(verse_document_markup(&doc))
                == QStringLiteral("of God"));
    }

    SECTION("a chord with no word after it is kept")
    {
        for (const QString& src : {QStringLiteral("I can[E]not give an a[Bsus]nswer.[E]"),
                                  QStringLiteral("[Am7][D]Lord"),
                                  QStringLiteral("[G] [D] [Em7] [Cmaj7]"),
                                  QStringLiteral("Hello\n[G]\nWorld")}) {
            load_verse_document(&doc, src, QStringLiteral("Slide break"));
            REQUIRE(verse_document_markup(&doc) == src);
        }
        REQUIRE(plain_lyric_preview(verse_document_markup(&doc))
                == QStringLiteral("Hello\nWorld"));
    }

    SECTION("a chord between spaces goes on the next word without doubling a space")
    {
        load_verse_document(&doc, QStringLiteral("love[E/G#] for [A2] us,"),
                            QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc) == QStringLiteral("love [E/G#]for [A2]us,"));
    }

    SECTION("a chord before a styled word goes on it")
    {
        load_verse_document(&doc, QStringLiteral("[G]<b>Amazing</b> grace"),
                            QStringLiteral("Slide break"));
        REQUIRE(doc.toPlainText() == QStringLiteral("Amazing grace"));
        REQUIRE(verse_chord_at(&doc, 0) == QStringLiteral("G"));
    }

    SECTION("an unclosed bracket is text")
    {
        // A '[' with no ']' after it used to stop the loader in its tracks.
        const QString src = QStringLiteral("[G]a [b and c");
        load_verse_document(&doc, src, QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc) == src);
    }

    SECTION("chords with no word are marked bare, labels show accidentals")
    {
        load_verse_document(&doc, QStringLiteral("[F#m7]word[Bb]"), QStringLiteral("Slide break"));
        const auto marks = verse_chord_marks(&doc);
        REQUIRE(marks.size() == 2);
        REQUIRE(marks[0].toMap().value(QStringLiteral("label")).toString()
                == QStringLiteral("F♯m7"));
        REQUIRE_FALSE(marks[0].toMap().value(QStringLiteral("bare")).toBool());
        REQUIRE(marks[1].toMap().value(QStringLiteral("chord")).toString()
                == QStringLiteral("Bb"));
        REQUIRE(marks[1].toMap().value(QStringLiteral("label")).toString()
                == QStringLiteral("B♭"));
        REQUIRE(marks[1].toMap().value(QStringLiteral("bare")).toBool());
    }

    SECTION("forced break")
    {
        const QString src = QStringLiteral("Hello\n[---][---]\nWorld");
        load_verse_document(&doc, src, QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc) == src);
        REQUIRE(plain_lyric_preview(verse_document_markup(&doc))
                == QStringLiteral("Hello\nWorld"));
        QTextBlock brk = doc.begin().next();
        REQUIRE(brk.blockFormat().alignment() == Qt::AlignLeft);
        REQUIRE(seventhslide::app::verse_pos_in_break(&doc, brk.position()));
        QTextCursor cur(&doc);
        cur.setPosition(brk.position());
        cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cur.insertText(QStringLiteral("Sli"));
        seventhslide::app::sanitize_verse_breaks(&doc, QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc) == QStringLiteral("Hello\nWorld"));
    }
}

TEST_CASE("verse editor toggles style and chords", "[app][verse_editor]")
{
    seventhslide::test::ensure_qt_application();
    QTextDocument doc;
    doc.setDefaultFont(QFont(QStringLiteral("Sans Serif"), 12));
    load_verse_document(&doc, QStringLiteral("Hello world"), QStringLiteral("Slide break"));

    toggle_verse_format(&doc, 0, 5, QStringLiteral("b"));
    REQUIRE(verse_has_format(&doc, 0, 5, QStringLiteral("b")));
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("<b>Hello</b> world"));

    set_verse_chord(&doc, 6, 11, QStringLiteral("G"));
    REQUIRE(verse_chord_at(&doc, 6) == QStringLiteral("G"));
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("<b>Hello</b> [G]world"));

    set_verse_chord(&doc, 6, 11, QString());
    REQUIRE(verse_chord_at(&doc, 6).isEmpty());
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("<b>Hello</b> world"));
    REQUIRE(verse_has_format(&doc, 0, 5, QStringLiteral("b")));

    insert_verse_break(&doc, 5, QStringLiteral("Slide break"));
    const QString out = verse_document_markup(&doc);
    REQUIRE(out.contains(QStringLiteral("[---][---]")));
}

TEST_CASE("the chord field changes a chord with no word", "[app][verse_editor]")
{
    seventhslide::test::ensure_qt_application();
    QTextDocument doc;
    doc.setDefaultFont(QFont(QStringLiteral("Sans Serif"), 12));
    load_verse_document(&doc, QStringLiteral("word[E]"), QStringLiteral("Slide break"));

    // "word" then the holder: the cursor at the line's end is just after it.
    REQUIRE(verse_chord_at(&doc, 5) == QStringLiteral("E"));
    set_verse_chord(&doc, 5, 5, QStringLiteral("F#"));
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("word[F#]"));

    // Before the holder (the end of "word") it is still the chord shown.
    REQUIRE(verse_chord_at(&doc, 4) == QStringLiteral("F#"));
    set_verse_chord(&doc, 4, 4, QString());
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("word"));
    REQUIRE(doc.toPlainText() == QStringLiteral("word"));
}

TEST_CASE("insert verse break does not add a blank line after the break", "[app][verse_editor]")
{
    seventhslide::test::ensure_qt_application();
    QTextDocument doc;
    doc.setDefaultFont(QFont(QStringLiteral("Sans Serif"), 12));
    const auto empty_blocks = [](const QTextDocument& d) {
        int n = 0;
        for (QTextBlock b = d.begin(); b.isValid(); b = b.next()) {
            if (b.text().isEmpty()) {
                ++n;
            }
        }
        return n;
    };

    SECTION("between two lyric lines at end of first")
    {
        load_verse_document(&doc, QStringLiteral("Hello\nWorld"),
                            QStringLiteral("Slide break"));
        const QTextBlock first = doc.begin();
        insert_verse_break(&doc, first.position() + first.length() - 1,
                           QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc)
                == QStringLiteral("Hello\n[---][---]\nWorld"));
        REQUIRE(empty_blocks(doc) == 0);
    }

    SECTION("at the start of the next lyric line")
    {
        load_verse_document(&doc, QStringLiteral("Hello\nWorld"),
                            QStringLiteral("Slide break"));
        insert_verse_break(&doc, doc.begin().next().position(),
                           QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc)
                == QStringLiteral("Hello\n[---][---]\nWorld"));
        REQUIRE(empty_blocks(doc) == 0);
    }

    SECTION("on a blank line between lyrics")
    {
        load_verse_document(&doc, QStringLiteral("Hello\n\nWorld"),
                            QStringLiteral("Slide break"));
        insert_verse_break(&doc, doc.begin().next().position(),
                           QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc)
                == QStringLiteral("Hello\n[---][---]\nWorld"));
        REQUIRE(empty_blocks(doc) == 0);
    }

    SECTION("mid-line remainder stays on the following block")
    {
        load_verse_document(&doc, QStringLiteral("Hello world"),
                            QStringLiteral("Slide break"));
        insert_verse_break(&doc, 5, QStringLiteral("Slide break"));
        REQUIRE(verse_document_markup(&doc)
                == QStringLiteral("Hello\n[---][---]\n world"));
        REQUIRE(empty_blocks(doc) == 0);
    }
}

TEST_CASE("verse format reads and toggles over the range, the caret, and breaks", "[app][verse_editor]")
{
    seventhslide::test::ensure_qt_application();
    QTextDocument doc;
    doc.setDefaultFont(QFont(QStringLiteral("Sans Serif"), 12));
    load_verse_document(&doc, QStringLiteral("Hello world"), QStringLiteral("Slide break"));
    const QString b = QStringLiteral("b");

    toggle_verse_format(&doc, 5, 0, b);  // either way round
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("<b>Hello</b> world"));
    CHECK(verse_has_format(&doc, 0, 5, b));
    CHECK(verse_has_format(&doc, 5, 0, b));
    CHECK_FALSE(verse_has_format(&doc, 0, 11, b));   // only part of it
    CHECK_FALSE(verse_has_format(&doc, 5, 8, b));    // "Hello" ends where this starts
    // A caret reads the style it sits in, and a toggle there writes nothing.
    CHECK(verse_has_format(&doc, 2, 2, b));
    CHECK_FALSE(verse_has_format(&doc, 8, 8, b));
    toggle_verse_format(&doc, 3, 3, b);
    CHECK(verse_document_markup(&doc) == QStringLiteral("<b>Hello</b> world"));
    // An unknown tag changes nothing and reads as off.
    toggle_verse_format(&doc, 0, 11, QStringLiteral("x"));
    CHECK(verse_document_markup(&doc) == QStringLiteral("<b>Hello</b> world"));
    CHECK_FALSE(verse_has_format(&doc, 0, 5, QStringLiteral("x")));

    // A fragment that only touches an end of the range is outside it: bold
    // " world" alone, then read ranges that start or stop at its edge.
    toggle_verse_format(&doc, 0, 5, b);
    toggle_verse_format(&doc, 5, 11, b);
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("Hello<b> world</b>"));
    CHECK(verse_has_format(&doc, 5, 11, b));        // "Hello" ends at 5
    CHECK_FALSE(verse_has_format(&doc, 0, 5, b));   // " world" starts at 5
    toggle_verse_format(&doc, 5, 11, b);
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("Hello world"));
    toggle_verse_format(&doc, 0, 5, b);
    REQUIRE(verse_document_markup(&doc) == QStringLiteral("<b>Hello</b> world"));

    // Partly on turns all of it on; all on turns it off.
    toggle_verse_format(&doc, 0, 11, b);
    CHECK(verse_document_markup(&doc) == QStringLiteral("<b>Hello world</b>"));
    toggle_verse_format(&doc, 11, 0, b);
    CHECK(verse_document_markup(&doc) == QStringLiteral("Hello world"));

    // A slide break inside the range is passed over.
    insert_verse_break(&doc, 5, QStringLiteral("Slide break"));
    const int end = doc.characterCount() - 1;
    toggle_verse_format(&doc, 0, end, QStringLiteral("i"));
    CHECK(verse_has_format(&doc, 0, end, QStringLiteral("i")));
    const QString marked = verse_document_markup(&doc);
    CHECK(marked.contains(QStringLiteral("[---]")));
    CHECK(marked.contains(QStringLiteral("<i>Hello</i>")));
}
