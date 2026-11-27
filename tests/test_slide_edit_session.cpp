// The slide editor's session through the facade (SlideEditSession): one
// object for a Freeform item or a theme, split into what the window reads.

#include "session_facade_fixture.hpp"
#include "slide_edit_session.hpp"
#include "test_qml.hpp"

#include <seventhslide/render/scene_presenter.hpp>

#include <QColor>
#include <QMetaObject>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QVariantMap>

using namespace seventhslide::test::facade;
using seventhslide::app::SlideEditSession;

namespace {

/// Every property a QML binding reads must notify through one of the
/// session's signals, or a constant child whose own ``changed`` follows them.
void require_properties_notify(const QObject* object, const QByteArrayList& notifiers)
{
    const QMetaObject* meta = object->metaObject();
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        const QMetaProperty prop = meta->property(i);
        INFO(prop.name());
        if (prop.isConstant()) {
            continue;
        }
        REQUIRE(prop.hasNotifySignal());
        REQUIRE(notifiers.contains(prop.notifySignal().name()));
    }
}

/// Play the Build preview of the slide open in ``edit`` and give the colour
/// at the middle of what it draws.
QColor build_preview_centre(SlideEditSession* edit)
{
    edit->animations()->play();
    auto* presenter = edit->animations()->presenter();
    presenter->settle();
    const QImage picture = presenter->surface(QSize(160, 90), QColor(Qt::black));
    edit->animations()->stop();
    REQUIRE_FALSE(picture.isNull());
    return picture.pixelColor(picture.width() / 2, picture.height() / 2);
}

/// Open a new Freeform item in ``edit`` whose slide's background is the
/// library picture ``name`` (in its images folder).
void open_with_background(SessionFacade& facade, SlideEditSession* edit, const std::string& name)
{
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Card"));
    REQUIRE(id > 0);
    auto doc = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(doc.has_value());
    auto& background = doc->decks.front().slides.front().background;
    background.kind = seventhslide::freeform::SlideBackgroundKind::Image;
    background.image = name;
    REQUIRE(facade.freeformLibrary()->commit_freeform_document(id, *doc).isEmpty());
    REQUIRE(edit->openFreeform(id));
}

/// Write a picture of ``colour`` as ``name`` in the open library's images.
void write_picture(SessionFacade& facade, const std::string& name, Qt::GlobalColor colour)
{
    QImage picture(64, 36, QImage::Format_RGB32);
    picture.fill(colour);
    const auto images = facade.media_root() / "images";
    std::filesystem::create_directories(images);
    REQUIRE(picture.save(QString::fromStdString((images / name).string())));
}

}  // namespace

TEST_CASE("SessionFacade exposes the slide editor as session.slideEdit, split for QML",
          "[app][slide-edit][qml]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto* edit = qvariant_cast<SlideEditSession*>(facade.property("slideEdit"));
    REQUIRE(edit != nullptr);
    REQUIRE(edit == facade.slideEdit());
    REQUIRE(edit->parent() == &facade);
    require_properties_notify(edit, {"documentChanged", "selectionChanged", "clipboardChanged"});
    // canvasObjectsChanged follows each documentChanged, once the list is rebuilt.
    require_properties_notify(edit->objects(), {"documentChanged", "selectionChanged",
                                                "clipboardChanged", "canvasObjectsChanged"});
    for (const QObject* child : {static_cast<QObject*>(edit->slides()),
                                 static_cast<QObject*>(edit->cues()),
                                 static_cast<QObject*>(edit->fields()),
                                 static_cast<QObject*>(edit->theme())}) {
        REQUIRE(child->parent() == edit);
        require_properties_notify(child, {"changed"});
    }
    CHECK(edit->property("mode").toString().isEmpty());
    CHECK_FALSE(edit->property("active").toBool());
}

TEST_CASE("The slide editor opens a Freeform item and its parts read the document",
          "[app][slide-edit][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Welcome"));
    REQUIRE(id > 0);
    auto* edit = facade.slideEdit();
    QSignalSpy documents(edit, &SlideEditSession::documentChanged);

    REQUIRE(edit->openFreeform(id));
    CHECK(documents.count() == 1);
    CHECK(edit->property("mode").toString() == QStringLiteral("freeform"));
    CHECK(edit->property("name").toString() == QStringLiteral("Welcome"));
    CHECK(edit->property("output").toString() == QStringLiteral("Main"));
    CHECK(edit->property("canvasWidth").toInt() == 1920);
    CHECK_FALSE(edit->property("dirty").toBool());
    CHECK(edit->slides()->property("count").toInt() == 1);
    CHECK(edit->slides()->list()->rowCount() == 1);
    CHECK_FALSE(edit->cues()->property("lineControl").toBool());

    // Nothing selected: a new object's values, by the property tables' names.
    const auto idle = edit->objects()->selection();
    CHECK(idle.value(QStringLiteral("font_size")).toInt() == 48);
    CHECK(idle.value(QStringLiteral("fill")).toString() == QStringLiteral("#ffffff"));
    CHECK(idle.value(QStringLiteral("id")).toString().isEmpty());

    SECTION("inserting a text box selects it and the canvas shows it")
    {
        REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
        CHECK(edit->property("dirty").toBool());
        CHECK(edit->objects()->hasSelection());
        CHECK(edit->objects()->selectedType() == QStringLiteral("text"));
        const auto objects = edit->objects()->canvasObjects();
        REQUIRE(objects.size() == 1);
        const auto first = objects.front().toMap();
        CHECK(first.value(QStringLiteral("type")).toString() == QStringLiteral("text"));
        CHECK(first.value(QStringLiteral("id")).toString()
              == edit->objects()->selection().value(QStringLiteral("id")).toString());
        // A property written by the ribbon's name reads back by it.
        REQUIRE(edit->objects()->setSelectedProperty(QStringLiteral("font_size"), 72).isEmpty());
        CHECK(edit->objects()->selection().value(QStringLiteral("font_size")).toInt() == 72);
        REQUIRE(edit->objects()
                    ->setText(first.value(QStringLiteral("id")).toString(), QStringLiteral("Hi"),
                              true, false, false)
                    .isEmpty());
        const auto sel = edit->objects()->selection();
        CHECK(sel.value(QStringLiteral("text")).toString() == QStringLiteral("Hi"));
        CHECK(sel.value(QStringLiteral("bold")).toBool());
        REQUIRE(edit->undo().isEmpty());
        CHECK(edit->objects()->selection().value(QStringLiteral("text")).toString().isEmpty());
    }
    SECTION("a new shape is visible: it has its fill and outline, neither see-through")
    {
        REQUIRE(edit->objects()->insert(QStringLiteral("shape"),
                                         {{QStringLiteral("shapeKind"), QStringLiteral("star")}})
                    .isEmpty());
        const auto sel = edit->objects()->selection();
        CHECK(sel.value(QStringLiteral("shape_kind")).toString() == QStringLiteral("star"));
        const QColor fill(sel.value(QStringLiteral("fill")).toString());
        const QColor stroke(sel.value(QStringLiteral("stroke")).toString());
        REQUIRE(fill.isValid());
        REQUIRE(stroke.isValid());
        CHECK(fill.alpha() == 255);
        CHECK(stroke.alpha() == 255);
    }
    SECTION("slides: insert, background, select")
    {
        REQUIRE(edit->slides()->insert().isEmpty());
        CHECK(edit->slides()->property("count").toInt() == 2);
        CHECK(edit->slides()->property("index").toInt() == 1);
        REQUIRE(edit->slides()->setBackground(QStringLiteral("color"), QStringLiteral("#102030")).isEmpty());
        const auto bg = edit->slides()->background();
        CHECK(bg.value(QStringLiteral("kind")).toString() == QStringLiteral("color"));
        CHECK(bg.value(QStringLiteral("color")).toString() == QStringLiteral("#102030"));
        edit->slides()->select(0);
        CHECK(edit->slides()->property("index").toInt() == 0);
        CHECK(edit->slides()->background().value(QStringLiteral("kind")).toString()
              == QStringLiteral("none"));
        // Speaker notes go with the slide.
        REQUIRE(edit->slides()->setNotes(QStringLiteral("Pause here.")).isEmpty());
        CHECK(edit->slides()->property("notes").toString() == QStringLiteral("Pause here."));
        edit->slides()->select(1);
        CHECK(edit->slides()->property("notes").toString().isEmpty());
        edit->slides()->select(0);
        CHECK(edit->slides()->property("notes").toString() == QStringLiteral("Pause here."));
    }
    SECTION("fields and cues")
    {
        REQUIRE(edit->fields()->add(QStringLiteral("speaker")).isEmpty());
        REQUIRE(edit->fields()->set(QStringLiteral("speaker"), QStringLiteral("Dan")).isEmpty());
        CHECK(edit->fields()->names() == QStringList{QStringLiteral("speaker")});
        CHECK(edit->fields()->usage(QStringLiteral("speaker")) == 0);
        CHECK(edit->fields()->sanitize(QStringLiteral("Sermon Title")) == QStringLiteral("sermon_title"));
        REQUIRE(edit->cues()->setLineControl(true).isEmpty());
        CHECK(edit->cues()->property("lineControl").toBool());
        REQUIRE(edit->cues()->insert(QStringLiteral("Amazing grace")).isEmpty());
        CHECK(edit->cues()->current().value(QStringLiteral("text")).toString()
              == QStringLiteral("Amazing grace"));
        CHECK(edit->slides()->list()->rowCount() >= 2);  // the cue row and its slide
    }
    SECTION("save writes the item and closes; cancel just closes")
    {
        REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
        REQUIRE(edit->save().isEmpty());
        CHECK_FALSE(edit->property("active").toBool());
        REQUIRE(edit->openFreeform(id));
        CHECK(edit->objects()->canvasObjects().size() == 1);
        edit->cancel();
        CHECK(edit->property("mode").toString().isEmpty());
    }
}

// QML hears documentChanged before any C++ slot, so a binding on ``revision``
// reads it as the signal goes: it must be counted by then.
TEST_CASE("A QML binding on the slide editor's revision reads the new count",
          "[app][slide-edit][qml]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Welcome"));
    REQUIRE(id > 0);
    auto* edit = facade.slideEdit();
    seventhslide::test::QmlScene scene;
    QObject* reader = scene.create(R"QML(
import QtQml
QtObject {
    required property QtObject edit
    readonly property int seen: edit.revision
}
)QML",
                                   {}, QVariantMap{{QStringLiteral("edit"), QVariant::fromValue(edit)}});
    REQUIRE(reader != nullptr);
    CHECK(reader->property("seen").toInt() == edit->revision());

    // Opening, through the objects.
    REQUIRE(edit->openFreeform(id));
    REQUIRE(edit->revision() > 0);
    CHECK(reader->property("seen").toInt() == edit->revision());
    // A gesture ending, which says so itself.
    const int before = edit->revision();
    edit->objects()->beginGesture();
    edit->objects()->endGesture();
    REQUIRE(edit->revision() == before + 1);
    CHECK(reader->property("seen").toInt() == edit->revision());
}

TEST_CASE("The slide editor opens a text theme: its elements, contexts, settings and tags",
          "[app][slide-edit][theme]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto created =
        facade.themes()->create(QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("Base"));
    const QString theme_id = created.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(theme_id.isEmpty());
    auto* edit = facade.slideEdit();

    REQUIRE(edit->openTheme(QStringLiteral("Main"), QStringLiteral("song"), theme_id));
    CHECK(edit->property("mode").toString() == QStringLiteral("text"));
    CHECK(edit->property("name").toString() == QStringLiteral("Base"));
    CHECK(edit->property("context").toString() == QStringLiteral("song"));
    // A song theme's own contexts: the song and its title slides.
    CHECK(edit->property("contexts").toStringList()
          == QStringList{QStringLiteral("song"), QStringLiteral("titles")});
    CHECK(edit->theme()->property("kind").toString() == QStringLiteral("song"));
    CHECK_FALSE(edit->property("dirty").toBool());
    // The stock design: a lyrics box among the song context's elements.
    const auto song = edit->objects()->canvasObjects();
    REQUIRE_FALSE(song.isEmpty());
    bool lyrics = false;
    for (const auto& v : song) {
        lyrics = lyrics || v.toMap().value(QStringLiteral("binding")).toString() == QStringLiteral("lyrics");
    }
    CHECK(lyrics);
    // Insert offers no second lyrics box for this context.
    bool offers_lyrics = false;
    for (const auto& v : edit->theme()->insertKinds()) {
        offers_lyrics = offers_lyrics || v.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("lyrics");
    }
    CHECK_FALSE(offers_lyrics);

    // Another kind's context is not there to switch to.
    edit->setContext(QStringLiteral("bible"));
    CHECK(edit->property("context").toString() == QStringLiteral("song"));
    edit->setContext(QStringLiteral("titles"));
    CHECK(edit->property("context").toString() == QStringLiteral("titles"));
    const auto titles = edit->objects()->canvasObjects();

    SECTION("a theme-wide setting and the tags mark it dirty and save with it")
    {
        edit->theme()->setSetting(QStringLiteral("verse_gap"), 30);
        CHECK(edit->property("dirty").toBool());
        CHECK(edit->theme()->settings().value(QStringLiteral("verse_gap")).toInt() == 30);
        edit->theme()->setTags({QStringLiteral("Christmas"), QStringLiteral(" advent ")});
        CHECK(edit->theme()->tags().size() == 2);
        REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
        REQUIRE(edit->save().isEmpty());
        CHECK_FALSE(edit->property("active").toBool());
        const auto saved = facade.themes()->theme_map(QStringLiteral("Main"), QStringLiteral("song"), theme_id);
        CHECK(saved.value(QStringLiteral("style")).toMap().value(QStringLiteral("verse_gap")).toInt() == 30);
        CHECK(saved.value(QStringLiteral("tags")).toStringList().size() == 2);
        // The element went in with the rest of the design.
        REQUIRE(edit->openTheme(QStringLiteral("Main"), QStringLiteral("song"), theme_id));
        edit->setContext(QStringLiteral("titles"));
        CHECK(edit->objects()->canvasObjects().size() == titles.size() + 1);
    }
    SECTION("a Bible theme opens on its passage and keeps only Bible settings")
    {
        const auto bible = facade.themes()->create(QStringLiteral("Main"), QStringLiteral("bible"),
                                                   QStringLiteral("Scripture"));
        const auto bible_id = bible.value(QStringLiteral("id")).toString();
        REQUIRE(edit->openTheme(QStringLiteral("Main"), QStringLiteral("bible"), bible_id));
        CHECK(edit->property("context").toString() == QStringLiteral("bible"));
        CHECK(edit->property("contexts").toStringList() == QStringList{QStringLiteral("bible")});
        CHECK(edit->theme()->property("kind").toString() == QStringLiteral("bible"));
        edit->theme()->setSetting(QStringLiteral("bible_fluid_verses"), true);
        edit->theme()->setSetting(QStringLiteral("verse_gap"), 30);
        REQUIRE(edit->save().isEmpty());
        const auto style = facade.themes()
                               ->theme_map(QStringLiteral("Main"), QStringLiteral("bible"), bible_id)
                               .value(QStringLiteral("style"))
                               .toMap();
        CHECK(style.value(QStringLiteral("bible_fluid_verses")).toBool());
        // A song's setting is not a Bible theme's to keep.
        CHECK_FALSE(style.contains(QStringLiteral("verse_gap")));
        CHECK_FALSE(style.value(QStringLiteral("elements")).toMap().contains(QStringLiteral("song")));
    }
    SECTION("a media theme opens on the picture area")
    {
        const auto media = facade.themes()->create(QStringLiteral("Main"), QStringLiteral("media"),
                                                   QStringLiteral("Screens"));
        REQUIRE(edit->openTheme(QStringLiteral("Main"), QStringLiteral("media"),
                                media.value(QStringLiteral("id")).toString()));
        CHECK(edit->property("context").toString() == QStringLiteral("image"));
        CHECK(edit->property("contexts").toStringList()
              == QStringList{QStringLiteral("image"), QStringLiteral("video"), QStringLiteral("web"),
                             QStringLiteral("live")});
    }
    SECTION("a background theme has one context and no elements")
    {
        const auto bg = facade.themes()->create(QStringLiteral("Main"), QStringLiteral("bg"), QStringLiteral("Dark"));
        REQUIRE(edit->openTheme(QStringLiteral("Main"), QStringLiteral("bg"),
                                bg.value(QStringLiteral("id")).toString()));
        CHECK(edit->property("mode").toString() == QStringLiteral("bg"));
        CHECK(edit->property("contexts").toStringList() == QStringList{QStringLiteral("bg")});
        CHECK(edit->objects()->canvasObjects().isEmpty());
        edit->theme()->setSetting(QStringLiteral("background_color"), QStringLiteral("#d3d3d3"));
        CHECK(edit->theme()->settings().value(QStringLiteral("background_color")).toString()
              == QStringLiteral("#d3d3d3"));
        REQUIRE(edit->save().isEmpty());
    }
}

TEST_CASE("The Build preview draws pictures from the library open now",
          "[app][slide-edit][builds]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Other")).isEmpty());
    auto* edit = facade.slideEdit();
    // Each library has a picture of the same name, of its own colour, on a
    // Freeform item's slide, which the editor opens.
    write_picture(facade, "pic.png", Qt::red);
    open_with_background(facade, edit, "pic.png");
    CHECK(build_preview_centre(edit).red() > 200);

    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("other")).isEmpty());
    write_picture(facade, "pic.png", Qt::blue);
    open_with_background(facade, edit, "pic.png");
    const auto colour = build_preview_centre(edit);
    CHECK(colour.blue() > 200);
    CHECK(colour.red() < 60);
}

TEST_CASE("The Build preview draws a picture that arrives after it played without it",
          "[app][slide-edit][builds]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    auto* edit = facade.slideEdit();
    open_with_background(facade, edit, "late.png");
    // Not here yet: the slide plays without it.
    CHECK(build_preview_centre(edit).red() < 60);

    // It comes by sync, and the next play draws it.
    write_picture(facade, "late.png", Qt::red);
    facade.media_arrived({"images/late.png"});
    CHECK(build_preview_centre(edit).red() > 200);
}

TEST_CASE("A theme's text box takes text without the Qt CSS a draft leaked",
          "[app][slide-edit][theme]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto created =
        facade.themes()->create(QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("Base"));
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openTheme(QStringLiteral("Main"), QStringLiteral("song"),
                            created.value(QStringLiteral("id")).toString()));
    REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
    const QString id = edit->objects()->selection().value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(id.isEmpty());

    REQUIRE(edit->objects()
                ->setText(id, QStringLiteral("p, li { white-space: pre-wrap; }\nHello"), false, false,
                          false)
                .isEmpty());
    CHECK(edit->objects()->selection().value(QStringLiteral("text")).toString() == QStringLiteral("Hello"));
    REQUIRE(edit->objects()->setText(id, QStringLiteral("Hello\nworld"), false, false, false).isEmpty());
    CHECK(edit->objects()->selection().value(QStringLiteral("text")).toString()
          == QStringLiteral("Hello\nworld"));
}
