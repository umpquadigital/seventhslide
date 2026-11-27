// The seam between the QML RichText overlays and model::reconcile_rich_lines.
// The regression here: reading a rich TextEdit's serialized HTML instead of its
// document put a blank line between every paragraph, because Qt writes each
// block tag on its own source line.

#include <catch2/catch_test_macros.hpp>

#include "canvas_rich_edit.hpp"
#include <seventhslide/layout/editor_html.hpp>
#include <seventhslide/model/rich_text.hpp>
#include "test_qml.hpp"
#include "test_qt_app.hpp"
#include "text_edit_tools.hpp"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QVariantList>
#include <QVariantMap>

#include <string>
#include <vector>

using seventhslide::app::CanvasRichEdit;
using seventhslide::app::TextEditTools;

namespace {

QVariantMap span(const QString& text, bool bold = false, int scale = 100)
{
    QVariantMap m;
    m.insert(QStringLiteral("text"), text);
    m.insert(QStringLiteral("scale"), scale);
    m.insert(QStringLiteral("bold"), bold);
    m.insert(QStringLiteral("italic"), false);
    m.insert(QStringLiteral("underline"), false);
    m.insert(QStringLiteral("color"), QString());
    return m;
}

QVariantMap line(const QVariantList& spans)
{
    QString joined;
    for (const auto& sp : spans) {
        joined += sp.toMap().value(QStringLiteral("text")).toString();
    }
    QVariantMap m;
    m.insert(QStringLiteral("text"), joined);
    m.insert(QStringLiteral("spans"), spans);
    return m;
}

QVariantList reconciled(const QVariantList& lines, const QString& text)
{
    CanvasRichEdit edit;
    edit.setLines(lines);
    edit.reconcile(text);
    return edit.lines();
}

QString plain_of(const QVariant& row)
{
    return row.toMap().value(QStringLiteral("text")).toString();
}

QVariantList spans_of(const QVariant& row)
{
    return row.toMap().value(QStringLiteral("spans")).toList();
}

} // namespace

TEST_CASE("TextEditTools.setLineHeight leaves a settled text alone", "[app][rich_edit]")
{
    // Any edit, even an empty one, says the text changed; the stage's boxes
    // answer that by setting the line height again.
    seventhslide::test::QmlScene scene;
    QObject* edit = scene.create("import QtQuick\nTextEdit { text: 'one\\ntwo' }");
    REQUIRE(edit != nullptr);
    TextEditTools tools;
    tools.setLineHeight(edit, 1.2, 0);
    QSignalSpy changed(edit, SIGNAL(textChanged()));
    tools.setLineHeight(edit, 1.2, 0);
    CHECK(changed.isEmpty());
    tools.setLineHeight(edit, 2.0, 0);
    CHECK_FALSE(changed.isEmpty());
}

TEST_CASE("TextEditTools.plainText reads the document, not the serialized HTML", "[app][rich_edit]")
{
    seventhslide::test::QmlScene scene;
    QObject* edit = scene.create("import QtQuick\nTextEdit { textFormat: TextEdit.RichText }\n");

    TextEditTools tools;

    SECTION("two styled lines come back as two lines")
    {
        // The shape SlideEditState feeds the overlay: one block per line,
        // regular spaces + pre-wrap so the box wraps by word (matching paint)
        // while leading / trailing / consecutive spaces still survive.
        edit->setProperty(
            "text",
            QStringLiteral(
                "<div style=\"margin:0; padding:0;\">"
                "<div style=\"padding:0; margin:0; line-height:60px;\">"
                "<span style=\"white-space:pre-wrap;\">"
                "<span style=\"font-weight:700;\">A Mighty Fortress</span>"
                "</span></div>"
                "<div style=\"padding:0; margin:0; line-height:60px;\">"
                "<span style=\"white-space:pre-wrap;\">"
                "<span style=\"font-weight:400;\">Hymn #506</span>"
                "</span></div></div>"));
        REQUIRE(tools.plainText(edit)
                == QStringLiteral("A Mighty Fortress\nHymn #506"));
    }

    SECTION("&nbsp; in overlay HTML round-trips to U+0020")
    {
        edit->setProperty(
            "text",
            QStringLiteral(
                "<div style=\"margin:0; padding:0;\">"
                "<div style=\"padding:0; margin:0; line-height:60px;\">"
                "<span style=\"white-space:pre;\">"
                "<span style=\"font-weight:700;\">A&nbsp;Mighty&nbsp;Fortress</span>"
                "</span></div></div>"));
        REQUIRE(tools.plainText(edit)
                == QStringLiteral("A Mighty Fortress"));
    }

    SECTION("pre-wrap keeps leading and trailing spaces without &nbsp;")
    {
        edit->setProperty(
            "text",
            QStringLiteral(
                "<div style=\"margin:0; padding:0;\">"
                "<div style=\"padding:0; margin:0; line-height:60px;\">"
                "<span style=\"white-space:pre-wrap;\">"
                "<span>  Hello  world  </span>"
                "</span></div></div>"));
        REQUIRE(tools.plainText(edit)
                == QStringLiteral("  Hello  world  "));
    }

    SECTION("a blank line survives the trip through the overlay")
    {
        // Qt drops a block with no text, so the dialogs mark empty lines the way
        // Qt's own serializer does. Without it the blank line disappears as soon
        // as the box is opened for editing.
        edit->setProperty(
            "text",
            QStringLiteral(
                "<div style=\"margin:0; padding:0;\">"
                "<div style=\"padding:0; margin:0; line-height:60px;\">"
                "<span style=\"white-space:pre-wrap;\"><span style=\"font-size:48px;\">a</span></span></div>"
                "<div style=\"padding:0; margin:0; line-height:60px; -qt-paragraph-type:empty;"
                " font-size:48px;\"><br/></div>"
                "<div style=\"padding:0; margin:0; line-height:60px;\">"
                "<span style=\"white-space:pre-wrap;\"><span style=\"font-size:48px;\">b</span></span></div>"
                "</div>"));
        REQUIRE(tools.plainText(edit) == QStringLiteral("a\n\nb"));
    }

    SECTION("no document → empty string")
    {
        REQUIRE(tools.plainText(nullptr).isEmpty());
    }
}

TEST_CASE("CanvasRichEdit::reconcile keeps run styling across an edit", "[app][canvas_rich_edit][rich_edit]")
{
    seventhslide::test::ensure_qt_application();

    const QVariantList lines{
        line({span(QStringLiteral("A Mighty Fortress"), true)}),
        line({span(QStringLiteral("Hymn #506"))}),
    };

    SECTION("unchanged text is a no-op")
    {
        const QVariantList out =
            reconciled(lines, QStringLiteral("A Mighty Fortress\nHymn #506"));
        REQUIRE(out.size() == 2);
        REQUIRE(spans_of(out[0]).first().toMap().value(QStringLiteral("bold")).toBool());
        REQUIRE_FALSE(spans_of(out[1]).first().toMap().value(QStringLiteral("bold")).toBool());
    }

    SECTION("inserting a line leaves the later line's styling alone")
    {
        const QVariantList out = reconciled(
            lines, QStringLiteral("A Mighty Fortress\nEin feste Burg\nHymn #506"));
        REQUIRE(out.size() == 3);
        REQUIRE(plain_of(out[2]) == QStringLiteral("Hymn #506"));
        REQUIRE(spans_of(out[0]).first().toMap().value(QStringLiteral("bold")).toBool());
        REQUIRE_FALSE(spans_of(out[2]).first().toMap().value(QStringLiteral("bold")).toBool());
    }

    SECTION("typing inside a mixed line does not flatten it")
    {
        const QVariantList mixed{line({
            span(QStringLiteral("A ")),
            span(QStringLiteral("Mighty"), true),
            span(QStringLiteral(" Fortress")),
        })};
        const QVariantList out =
            reconciled(mixed, QStringLiteral("A Migxhty Fortress"));
        REQUIRE(out.size() == 1);
        const QVariantList spans = spans_of(out[0]);
        REQUIRE(spans.size() == 3);
        REQUIRE(spans[1].toMap().value(QStringLiteral("text")).toString()
                == QStringLiteral("Migxhty"));
        REQUIRE(spans[1].toMap().value(QStringLiteral("bold")).toBool());
    }

    SECTION("a line carrying its style at the top level still reconciles")
    {
        QVariantMap bare;
        bare.insert(QStringLiteral("text"), QStringLiteral("Hymn"));
        bare.insert(QStringLiteral("scale"), 80);
        bare.insert(QStringLiteral("bold"), true);
        const QVariantList out =
            reconciled({bare}, QStringLiteral("Hymn #506"));
        REQUIRE(out.size() == 1);
        const QVariantList spans = spans_of(out[0]);
        REQUIRE(spans.size() == 1);
        REQUIRE(spans[0].toMap().value(QStringLiteral("text")).toString()
                == QStringLiteral("Hymn #506"));
        REQUIRE(spans[0].toMap().value(QStringLiteral("bold")).toBool());
        REQUIRE(spans[0].toMap().value(QStringLiteral("scale")).toInt() == 80);
    }
}

TEST_CASE("freeform overlay HTML wraps at word boundaries", "[app][rich_edit][wrap]")
{
    seventhslide::test::QmlScene scene;
    QObject* edit = scene.create("import QtQuick\n"
                                 "TextEdit {\n"
                                 "    textFormat: TextEdit.RichText\n"
                                 "    wrapMode: TextEdit.WordWrap\n"
                                 "    width: 160\n"
                                 "    font.pixelSize: 28\n"
                                 "}\n");

    edit->setProperty(
        "text",
        QStringLiteral(
            "<div style=\"margin:0; padding:0;\">"
            "<div style=\"padding:0; margin:0; line-height:34px;\">"
            "<span style=\"white-space:pre-wrap;\">"
            "<span style=\"font-size:28px;\">"
            "The Lord is my shepherd I shall not want"
            "</span></span></div></div>"));

    auto* wrap = edit->property("textDocument").value<QQuickTextDocument*>();
    REQUIRE(wrap != nullptr);
    QTextDocument* doc = wrap->textDocument();
    REQUIRE(doc != nullptr);
    doc->setTextWidth(160);

    int visual_lines = 0;
    bool split_inside_word = false;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        QTextLayout* layout = block.layout();
        if (!layout) {
            continue;
        }
        const QString t = block.text();
        visual_lines += layout->lineCount();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const int end = line.textStart() + line.textLength();
            if (end <= 0 || end >= t.size()) {
                continue;
            }
            // Word wrap never splits between two letters. Character wrap does.
            if (t.at(end - 1).isLetter() && t.at(end).isLetter()) {
                split_inside_word = true;
            }
        }
    }
    REQUIRE(visual_lines >= 2);
    REQUIRE_FALSE(split_inside_word);
}

TEST_CASE("CanvasRichEdit::html styles a box from its camelCase keys, or the defaults",
          "[app][canvas_rich_edit]")
{
    seventhslide::test::ensure_qt_application();
    CanvasRichEdit edit;
    edit.loadMarkup(QStringLiteral("Hello"), false, false, false);
    const QVariantMap box{{QStringLiteral("fontSize"), 60},
                          {QStringLiteral("fontColor"), QStringLiteral("#123456")},
                          {QStringLiteral("fontFamily"), QStringLiteral("Liberation Serif")},
                          {QStringLiteral("lineHeight"), 1.5},
                          {QStringLiteral("letterSpacing"), 2},
                          {QStringLiteral("lineGap"), 0}};
    const QString styled = edit.html(box, 1.0);
    CHECK(styled.contains(QStringLiteral("font-size:60px")));
    CHECK(styled.contains(QStringLiteral("color:#123456")));
    CHECK(styled.contains(QStringLiteral("Liberation Serif")));
    CHECK(styled.contains(QStringLiteral("letter-spacing:")));
    CHECK(styled.contains(QStringLiteral("line-height:90px")));  // 60 * 1.5

    // Nothing named: the canvas defaults.
    const QString plain = edit.html({}, 1.0);
    CHECK(plain.contains(QStringLiteral("font-size:48px")));
    CHECK(plain.contains(QStringLiteral("color:#ffffff")));
    CHECK(plain.contains(QStringLiteral("Liberation Sans")));
    CHECK_FALSE(plain.contains(QStringLiteral("letter-spacing:")));
}

TEST_CASE("Editing a box keeps a verse number's gap", "[rich_text][app]")
{
    CanvasRichEdit edit;
    edit.loadMarkup(QStringLiteral("<gap=40>3:16</gap> For God"), false, false, false);
    CHECK(edit.markup() == QStringLiteral("<gap=40>3:16</gap> For God"));
    edit.loadMarkup(QStringLiteral("<gap=0>3:16</gap> For God"), false, false, false);
    CHECK(edit.markup() == QStringLiteral("<gap=0>3:16</gap> For God"));
    edit.loadMarkup(QStringLiteral("For God"), false, false, false);
    CHECK(edit.markup() == QStringLiteral("For God"));
}

TEST_CASE("A verse number's gap draws as wide while its text is edited", "[app][rich_edit]")
{
    seventhslide::test::ensure_qt_application();
    const auto width_with_gap = [](int gap_pct) {
        const auto line = seventhslide::model::parse_inline_markup(
            "<gap=" + std::to_string(gap_pct) + ">3:16</gap> In");
        seventhslide::layout::EditorHtmlOptions opts;
        opts.font_size = 40;
        opts.family = "Liberation Sans";
        QTextDocument document;
        document.setDocumentMargin(0);
        document.setHtml(QString::fromStdString(seventhslide::layout::editor_html({line}, opts)));
        // The space is still there: places in the text do not move.
        CHECK(document.toPlainText() == QStringLiteral("3:16 In"));
        return document.idealWidth();
    };
    const double none = width_with_gap(0);
    CHECK(width_with_gap(100) - none > 37.0);
    CHECK(width_with_gap(100) - none < 43.0);
    CHECK(width_with_gap(50) - none > 17.0);
    CHECK(width_with_gap(50) - none < 23.0);
}

// The editor's HTML, loaded into the TextEdit typed into, reads back as the
// lines it was made from: an empty first line does not become two.
TEST_CASE("Editor HTML keeps a family with quotes and brackets whole", "[app][rich_edit]")
{
    seventhslide::test::ensure_qt_application();
    const std::string family = "Bad\" onclick=\"x<b>&amp;";
    seventhslide::model::TextStyle style;
    style.font_family = family;
    const seventhslide::model::RichLine line{seventhslide::model::TextSpan{"Hi", style}};
    seventhslide::layout::EditorHtmlOptions opts;
    opts.color = "#123456";
    QTextDocument document;
    document.setHtml(QString::fromStdString(seventhslide::layout::editor_html({line}, opts)));
    CHECK(document.toPlainText() == QStringLiteral("Hi"));
    const QTextCharFormat format = document.begin().begin().fragment().charFormat();
    CHECK(format.fontFamilies().toStringList() == QStringList{QString::fromStdString(family)});
    CHECK(format.foreground().color() == QColor(QStringLiteral("#123456")));
}

TEST_CASE("Editor HTML reads back as its lines, an empty first line included", "[app][rich_edit]")
{
    seventhslide::test::QmlScene scene;
    QObject* edit = scene.create("import QtQuick\nTextEdit { textFormat: TextEdit.RichText }\n");
    TextEditTools tools;
    seventhslide::layout::EditorHtmlOptions opts;
    opts.font_size = 48;
    opts.stage_scale = 1;
    const auto read_back = [&](const std::vector<std::string>& words) {
        std::vector<seventhslide::model::RichLine> lines;
        for (const auto& w : words) {
            lines.push_back({seventhslide::model::TextSpan{w, {}}});
        }
        edit->setProperty("text", QString::fromStdString(seventhslide::layout::editor_html(lines, opts)));
        return tools.plainText(edit);
    };
    CHECK(read_back({""}) == QString());
    CHECK(read_back({"", "abc"}) == QStringLiteral("\nabc"));
    CHECK(read_back({"abc", "", "def"}) == QStringLiteral("abc\n\ndef"));
    CHECK(read_back({"abc"}) == QStringLiteral("abc"));
}

// The cleaner keeps what it made in core: each draft, what it leaves of it,
// and whether it took it for leaked CSS. A row marked "known limit" is how
// the cleaner was, kept, not a result wanted: any text with both
// "white-space" and "pre" is taken for leaked CSS and loses its tags, and a
// rule never closed takes the rest of the draft.
TEST_CASE("A draft's leaked Qt CSS is cleaned as before", "[app][rich_text]")
{
    struct Case {
        std::string draft;
        std::string cleaned;
        bool polluted;
    };
    const std::vector<Case> cases{
        {"",
         "", false},
        {"Hello",
         "Hello", false},
        {"Hello\nworld",
         "Hello\nworld", false},
        {"p, li { white-space: pre-wrap; }\nHello",
         "Hello", true},
        {"p,li { white-space: pre-wrap; }\nHello",
         "Hello", true},
        {"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n<html><head><meta name=\"qrichtext\" content=\"1\" /><meta charset=\"utf-8\" /><style type=\"text/css\">\np, li { white-space: pre-wrap; }\nhr { height: 1px; border-width: 0; }\nli.unchecked::marker { content: \"\\2610\"; }\nli.checked::marker { content: \"\\2612\"; }\n</style></head><body style=\" font-family:'Sans Serif'; font-size:9pt; font-weight:400; font-style:normal;\">\n<p style=\" margin-top:0px; margin-bottom:0px;\">Amazing grace</p>\n<p style=\" margin-top:0px; margin-bottom:0px;\">How sweet the sound</p></body></html>",
         "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n<html><head><meta name=\"qrichtext\" content=\"1\" /><meta charset=\"utf-8\" /><style type=\"text/css\">\n\n</style></head><body style=\" font-family:'Sans Serif'; font-size:9pt; font-weight:400; font-style:normal;\">\n<p style=\" margin-top:0px; margin-bottom:0px;\">Amazing grace</p>\n<p style=\" margin-top:0px; margin-bottom:0px;\">How sweet the sound</p></body></html>", true},
        // Known limit.
        {"Keep the white-space in the present",
         "Keep the white-space in the present", true},
        {"li.checked::marker { content: x; }\nA",
         "A", true},
        {"li.unchecked::marker { content: x; }\nA",
         "A", true},
        {"p, li { white-space: pre-wrap; }\n\n\n  Verse one  \n\n\n\nVerse two\t\n  ",
         "Verse one\n\nVerse two", true},
        // Known limit: the whole draft goes.
        {"p, li { never closed\nHello",
         "", true},
        {"hr { height: 1px; }\nA",
         "hr { height: 1px; }\nA", false},
        // Known limit: the user's tags go.
        {"<b>bold</b> pre white-space",
         "bold pre white-space", true},
        // Known limit.
        {"Tom &amp; Jerry white-space pre",
         "Tom & Jerry white-space pre", true},
        {"[G]Amazing <i>grace</i>",
         "[G]Amazing <i>grace</i>", false},
        {"p, li {}\n\xC3\xA9t\xC3\xA9 white-space",
         "\xC3\xA9t\xC3\xA9 white-space", true},
    };
    for (const auto& c : cases) {
        INFO(c.draft);
        CHECK(seventhslide::app::editor_plain_polluted(c.draft) == c.polluted);
        CHECK(seventhslide::app::sanitize_editor_plain(c.draft) == c.cleaned);
    }
}
