// ThemeMapEditor (src/ui/dialogs/ThemeMapEditor.qml): the song editor's and the
// service settings' theme map, edited through the theme picker.

#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "qvariant_json.hpp"
#include "test_qml.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace {

constexpr const char* k_editor_qml = R"QML(
import QtQuick
import SeventhSlide

Window {
    id: win
    required property SessionFacade facade
    width: 900; height: 700; visible: true
    property alias editor: editor
    property alias picker: picker
    property alias slot: slot
    property int edits: 0

    ThemePickerDialog {
        id: picker
        session: win.facade
    }
    ThemeMapEditor {
        id: editor
        session: win.facade
        picker: picker
        scope: "service"
        inheritChoices: [{ id: "", slotName: "Output Default", caption: "Output Default",
                           label: "", previous: false }]
        onEdited: win.edits++
    }
    ThemeSlot {
        id: slot
        map: editor
        pick: win.slotFor("song", "theme")
        outputId: "out-1"
        outputName: "Main"
    }

    /// The editor's slot for ``category``'s ``field``, as a ThemeSlot holds it.
    function slotFor(category, field) {
        for (const group of editor.slotGroups) {
            for (const slot of group.slots) {
                if (slot.category === category && slot.field === field)
                    return slot
            }
        }
        return null
    }

    function open(outputId, category, field) {
        editor.openPicker(outputId, "Main", win.slotFor(category, field))
    }

    function choose(outputId, category, field, themeId) {
        win.open(outputId, category, field)
        picker.selectedId = themeId
        picker.confirm()
    }
}
)QML";

struct EditorWindow {
    // First member: the facade below needs the application to exist.
    bool app = (seventhslide::test::ensure_qt_application(), true);
    seventhslide::app::SessionFacade facade;
    seventhslide::test::QmlScene scene;
    QObject* window = nullptr;
    QObject* editor = nullptr;
    QObject* picker = nullptr;

    EditorWindow()
    {
        // No base URL: the module's own types, as the app loads them. The
        // editor's ``picker`` is typed, and a copy loaded from the source
        // folder would be a different type.
        window = scene.create(k_editor_qml, {},
                              {{QStringLiteral("facade"), QVariant::fromValue(&facade)}});
        editor = window->property("editor").value<QObject*>();
        picker = window->property("picker").value<QObject*>();
        REQUIRE(editor != nullptr);
        REQUIRE(picker != nullptr);
    }

    void choose(const char* output_id, const char* category, const char* field,
                const char* theme_id)
    {
        REQUIRE(QMetaObject::invokeMethod(
            window, "choose", Q_ARG(QVariant, QString::fromLatin1(output_id)),
            Q_ARG(QVariant, QString::fromLatin1(category)),
            Q_ARG(QVariant, QString::fromLatin1(field)),
            Q_ARG(QVariant, QString::fromLatin1(theme_id))));
    }

    [[nodiscard]] QString theme_id(const char* output_id, const char* category,
                                   const char* field) const
    {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(editor, "themeId", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, QString::fromLatin1(output_id)),
                                          Q_ARG(QVariant, QString::fromLatin1(category)),
                                          Q_ARG(QVariant, QString::fromLatin1(field))));
        return out.toString();
    }

    /// ``category``/``field`` of each slot, in order, as "category.field".
    [[nodiscard]] QStringList slot_names() const
    {
        QStringList out;
        for (const auto& group : editor->property("slotGroups").toList()) {
            for (const auto& slot : group.toMap().value(QStringLiteral("slots")).toList()) {
                const auto map = slot.toMap();
                out << map.value(QStringLiteral("category")).toString() + QLatin1Char('.')
                           + map.value(QStringLiteral("field")).toString();
            }
        }
        return out;
    }
};

}  // namespace

TEST_CASE("A service's theme map has a theme and background per category, media none",
          "[ui][theme-map]")
{
    EditorWindow w;
    CHECK(w.slot_names()
          == QStringList{QStringLiteral("song.theme"), QStringLiteral("song.bg"),
                         QStringLiteral("bible.theme"), QStringLiteral("bible.bg"),
                         QStringLiteral("media.theme")});
    w.editor->setProperty("scope", QStringLiteral("song"));
    CHECK(w.slot_names() == QStringList{QStringLiteral("song.theme"), QStringLiteral("song.bg")});
}

TEST_CASE("Theme map editor writes the picker's choice into the slot that opened it", "[ui][theme-map]")
{
    EditorWindow w;
    w.choose("out-1", "bible", "bg", "dark");
    CHECK(w.theme_id("out-1", "bible", "bg") == QStringLiteral("dark"));
    CHECK(w.theme_id("out-1", "bible", "theme").isEmpty());
    CHECK(w.theme_id("out-1", "song", "bg").isEmpty());
    CHECK(w.window->property("edits").toInt() == 1);
    CHECK(!w.picker->property("open").toBool());

    // The Inherit tile clears the slot.
    w.choose("out-1", "bible", "bg", "");
    CHECK(w.theme_id("out-1", "bible", "bg").isEmpty());
    // An output left with no picks has no entry.
    CHECK(w.editor->property("themeMap").toMap().isEmpty());
}

TEST_CASE("Theme map editor leaves the map alone while it is not editable", "[ui][theme-map]")
{
    EditorWindow w;
    w.editor->setProperty("editable", false);
    w.choose("out-1", "song", "theme", "bold");
    CHECK(w.theme_id("out-1", "song", "theme").isEmpty());
    CHECK(w.window->property("edits").toInt() == 0);
}

TEST_CASE("Theme map editor opens the picker on the slot's current theme", "[ui][theme-map]")
{
    EditorWindow w;
    w.choose("out-1", "media", "theme", "bold");
    REQUIRE(QMetaObject::invokeMethod(w.window, "open",
                                      Q_ARG(QVariant, QStringLiteral("out-1")),
                                      Q_ARG(QVariant, QStringLiteral("media")),
                                      Q_ARG(QVariant, QStringLiteral("theme"))));
    CHECK(w.picker->property("open").toBool());
    CHECK(w.picker->property("selectedId").toString() == QStringLiteral("bold"));
    CHECK(w.picker->property("kind").toString() == QStringLiteral("media"));
    // The Inherit card is named as the slots name it.
    const auto tiles = w.picker->property("inheritTiles").toList();
    REQUIRE(tiles.size() == 1);
    CHECK(tiles[0].toMap().value(QStringLiteral("caption")).toString()
          == QStringLiteral("Output Default"));
    CHECK(tiles[0].toMap().value(QStringLiteral("id")).toString().isEmpty());
}

// A song's theme change offers Previous (unset) and Above; a slot on Previous
// shows the theme the change before it played with.
TEST_CASE("Theme map editor offers a song's Previous and Above", "[ui][theme-map]")
{
    EditorWindow w;
    w.editor->setProperty("scope", QStringLiteral("song"));
    const auto choice = [](const char* id, const char* slot_name, bool previous) {
        return QVariantMap{{QStringLiteral("id"), QString::fromLatin1(id)},
                           {QStringLiteral("slotName"), QString::fromLatin1(slot_name)},
                           {QStringLiteral("caption"), QString::fromLatin1(slot_name)},
                           {QStringLiteral("label"), QString()},
                           {QStringLiteral("previous"), previous}};
    };
    w.editor->setProperty("inheritChoices", QVariantList{choice("", "Previous", true),
                                                         choice("@above", "Above", false)});
    QObject* slot = w.window->property("slot").value<QObject*>();
    REQUIRE(slot != nullptr);

    // Unset is the first choice.
    CHECK(slot->property("isInherit").toBool());
    CHECK(slot->property("nameText").toString() == QStringLiteral("Previous"));

    // With the change before known, Previous shows what it takes (here a
    // theme the library does not have).
    w.editor->setProperty(
        "previousMap",
        QVariantMap{{QStringLiteral("out-1"),
                     QVariantMap{{QStringLiteral("song"),
                                  QVariantMap{{QStringLiteral("theme"), QStringLiteral("gone")}}}}}});
    CHECK(slot->property("shownId").toString() == QStringLiteral("gone"));
    CHECK(slot->property("nameText").toString() == QStringLiteral("Previous · Unknown"));

    // Above is stored as its id, and takes nothing from the change before.
    w.choose("out-1", "song", "theme", "@above");
    CHECK(w.theme_id("out-1", "song", "theme") == QStringLiteral("@above"));
    CHECK(slot->property("isInherit").toBool());
    CHECK(slot->property("shownId").toString().isEmpty());
    CHECK(slot->property("nameText").toString() == QStringLiteral("Above"));

    // The picker leads with both, Previous first.
    REQUIRE(QMetaObject::invokeMethod(w.window, "open", Q_ARG(QVariant, QStringLiteral("out-1")),
                                      Q_ARG(QVariant, QStringLiteral("song")),
                                      Q_ARG(QVariant, QStringLiteral("theme"))));
    const auto tiles = w.picker->property("inheritTiles").toList();
    REQUIRE(tiles.size() == 2);
    CHECK(tiles[0].toMap().value(QStringLiteral("id")).toString().isEmpty());
    CHECK(tiles[1].toMap().value(QStringLiteral("id")).toString() == QStringLiteral("@above"));
    CHECK(w.picker->property("selectedId").toString() == QStringLiteral("@above"));

    // Previous clears the slot again.
    w.choose("out-1", "song", "theme", "");
    CHECK(w.theme_id("out-1", "song", "theme").isEmpty());
}

// A slot on Previous names, and shows faded, the theme the change before it
// played with, as the library has it.
TEST_CASE("Theme map editor shows the theme a slot's Previous takes", "[ui][theme-map]")
{
    // Made first, so the library outlives the window.
    const seventhslide::test::TempDir dir;
    EditorWindow w;
    REQUIRE(w.facade.open_database(dir.path / "library.db"));
    const auto created = w.facade.themes()->create(QStringLiteral("Main"), QStringLiteral("song"),
                                                   QStringLiteral("Dawn"));
    const QString id = created.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(id.isEmpty());

    w.editor->setProperty("scope", QStringLiteral("song"));
    w.editor->setProperty(
        "inheritChoices",
        QVariantList{QVariantMap{{QStringLiteral("id"), QString()},
                                 {QStringLiteral("slotName"), QStringLiteral("Previous")},
                                 {QStringLiteral("previous"), true}}});
    w.editor->setProperty(
        "previousMap",
        QVariantMap{{QStringLiteral("out-1"),
                     QVariantMap{{QStringLiteral("song"), QVariantMap{{QStringLiteral("theme"), id}}}}}});
    QObject* slot = w.window->property("slot").value<QObject*>();
    REQUIRE(slot != nullptr);
    CHECK(slot->property("isInherit").toBool());
    CHECK(slot->property("shownId").toString() == id);
    CHECK(slot->property("nameText").toString() == QStringLiteral("Previous · Dawn"));
    CHECK_FALSE(slot->property("isUnknown").toBool());
    CHECK_FALSE(slot->property("thumbUrl").toString().isEmpty());
    // Nothing of its own is stored.
    CHECK(w.theme_id("out-1", "song", "theme").isEmpty());
}

// A slot names its theme as the library has it now: a rename shows at once.
TEST_CASE("Theme map editor names a slot's theme after a rename", "[ui][theme-map]")
{
    // Made first, so the library outlives the window.
    const seventhslide::test::TempDir dir;
    EditorWindow w;
    REQUIRE(w.facade.open_database(dir.path / "library.db"));
    const auto created = w.facade.themes()->create(QStringLiteral("Main"), QStringLiteral("song"),
                                                   QStringLiteral("Dawn"));
    const QString id = created.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(id.isEmpty());
    w.choose("out-1", "song", "theme", id.toUtf8().constData());
    QObject* slot = w.window->property("slot").value<QObject*>();
    REQUIRE(slot != nullptr);
    CHECK(slot->property("displayName").toString() == QStringLiteral("Dawn"));
    REQUIRE(w.facade.themes()
                ->update_theme(QStringLiteral("Main"), QStringLiteral("song"), id, QStringLiteral("Dusk"),
                               seventhslide::app::to_json(created.value(QStringLiteral("style"))))
                .isEmpty());
    CHECK(slot->property("displayName").toString() == QStringLiteral("Dusk"));
}
