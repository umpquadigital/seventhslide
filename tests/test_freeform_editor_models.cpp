#include <catch2/catch_test_macros.hpp>

#include "freeform_editor_models.hpp"
#include "freeform_model_test_helpers.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/freeform/editor.hpp>

#include <QAbstractItemModel>
#include <QString>
#include <QStringList>

using seventhslide::app::FreeformFieldListModel;
using seventhslide::app::FreeformSlideListModel;
using seventhslide::freeform::Editor;
using seventhslide::freeform::make_document;

namespace {

struct ModelSignals {
    int resets = 0;
    int removes = 0;
    int inserts = 0;
    int moves = 0;
    int changes = 0;

    explicit ModelSignals(QAbstractItemModel& model)
    {
        QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
        QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&] { ++removes; });
        QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&] { ++inserts; });
        QObject::connect(&model, &QAbstractItemModel::rowsMoved, [&] { ++moves; });
        QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });
    }
};

QStringList slide_ids(const FreeformSlideListModel& model)
{
    QStringList ids;
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto idx = model.index(i);
        if (model.data(idx, FreeformSlideListModel::KindRole).toString() != QStringLiteral("slide")) {
            continue;
        }
        ids.push_back(model.data(idx, FreeformSlideListModel::SlideIdRole).toString());
    }
    return ids;
}

QStringList labels(const FreeformSlideListModel& model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto idx = model.index(i);
        if (model.data(idx, FreeformSlideListModel::KindRole).toString() != QStringLiteral("slide")) {
            continue;
        }
        out.push_back(model.data(idx, FreeformSlideListModel::LabelRole).toString());
    }
    return out;
}

} // namespace

TEST_CASE("FreeformSlideListModel delete emits remove instead of reset", "[freeform][editor][models]")
{
    seventhslide::test::ensure_qt_application();
    Editor ed;
    ed.load(make_document("Deck", {"out-main"}), "out-main");
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.slide_count() == 4);

    FreeformSlideListModel model;
    model.reload(ed);
    const auto before = slide_ids(model);
    REQUIRE(before.size() == 4);

    ed.select_slide(1);
    model.reload(ed);

    ModelSignals spy(model);
    REQUIRE(ed.delete_slide());
    model.reload(ed);

    REQUIRE(spy.resets == 0);
    REQUIRE(spy.removes == 1);
    REQUIRE(spy.inserts == 0);
    REQUIRE(model.rowCount() == 3);
    REQUIRE(slide_ids(model) == QStringList() << before[0] << before[2] << before[3]);
    REQUIRE(labels(model) == QStringList() << QStringLiteral("1") << QStringLiteral("2")
                                          << QStringLiteral("3"));
}

TEST_CASE("FreeformSlideListModel insert emits insert instead of reset", "[freeform][editor][models]")
{
    seventhslide::test::ensure_qt_application();
    Editor ed;
    ed.load(make_document("Deck", {"out-main"}), "out-main");
    REQUIRE(ed.insert_slide());

    FreeformSlideListModel model;
    model.reload(ed);
    REQUIRE(model.rowCount() == 2);

    ModelSignals spy(model);
    REQUIRE(ed.insert_slide());
    model.reload(ed);

    REQUIRE(spy.resets == 0);
    REQUIRE(spy.inserts == 1);
    REQUIRE(spy.removes == 0);
    REQUIRE(model.rowCount() == 3);
    REQUIRE(labels(model) == QStringList() << QStringLiteral("1") << QStringLiteral("2")
                                          << QStringLiteral("3"));
}

TEST_CASE("FreeformSlideListModel selection change does not rebuild rows", "[freeform][editor][models]")
{
    seventhslide::test::ensure_qt_application();
    Editor ed;
    ed.load(make_document("Deck", {"out-main"}), "out-main");
    REQUIRE(ed.insert_slide());

    FreeformSlideListModel model;
    model.reload(ed);

    ModelSignals spy(model);
    ed.select_slide(0);
    model.reload(ed);

    REQUIRE(spy.resets == 0);
    REQUIRE(spy.removes == 0);
    REQUIRE(spy.inserts == 0);
    REQUIRE(spy.moves == 0);
    REQUIRE(model.data(model.index(0), FreeformSlideListModel::SelectedRole).toBool());
    REQUIRE_FALSE(model.data(model.index(1), FreeformSlideListModel::SelectedRole).toBool());
}

TEST_CASE("FreeformSlideListModel exposes cue verse indicators", "[freeform][editor][models]")
{
    seventhslide::test::ensure_qt_application();
    Editor ed;
    ed.load(make_document("Deck", {"out-main"}), "out-main");
    REQUIRE(ed.set_line_control(true));
    REQUIRE(ed.set_cue_verse("Verse", 1));

    FreeformSlideListModel model;
    model.reload(ed);
    bool found = false;
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto idx = model.index(i);
        if (model.data(idx, FreeformSlideListModel::KindRole).toString() != QStringLiteral("cue"))
            continue;
        REQUIRE(model.data(idx, FreeformSlideListModel::CueIndicatorRole).toString()
                == QStringLiteral("V1"));
        found = true;
    }
    REQUIRE(found);
}

TEST_CASE("FreeformSlideListModel thumb URLs follow visual keys", "[freeform][editor][models]")
{
    seventhslide::test::ensure_qt_application();
    Editor ed;
    ed.load(make_document("Deck", {"out-main"}), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.set_text("Hello"));
    REQUIRE(ed.insert_slide());
    ed.select_slide(1);
    REQUIRE(ed.add_text());
    REQUIRE(ed.set_text("Other"));

    FreeformSlideListModel model;
    model.reload(ed);
    const auto url0 = seventhslide::test::slide_thumb_url(model, 0);
    const auto url1 = seventhslide::test::slide_thumb_url(model, 1);
    REQUIRE(url0.contains(QStringLiteral("image://freeformpreview/")));
    REQUIRE(url0.contains(QStringLiteral("v=")));
    REQUIRE(url1.contains(QStringLiteral("v=")));
    REQUIRE(url0 != url1);

    REQUIRE(ed.set_line_control(true));
    model.reload(ed);
    REQUIRE(seventhslide::test::slide_thumb_url(model, 0) == url0);
    REQUIRE(seventhslide::test::slide_thumb_url(model, 1) == url1);

    ed.select_cue(0);
    REQUIRE(ed.rename_cue("Intro"));
    model.reload(ed);
    REQUIRE(seventhslide::test::slide_thumb_url(model, 0) == url0);
    REQUIRE(seventhslide::test::slide_thumb_url(model, 1) == url1);

    ed.select_slide(0);
    const auto* slide = ed.current_slide();
    REQUIRE(slide);
    REQUIRE_FALSE(slide->objects.empty());
    REQUIRE(ed.set_geometry(slide->objects[0].id, 40, 50, 400, 200, 0));
    model.reload(ed);
    REQUIRE(seventhslide::test::slide_thumb_url(model, 0) != url0);
    REQUIRE(seventhslide::test::slide_thumb_url(model, 1) == url1);
}

TEST_CASE("FreeformSlideListModel line-control delete keeps cue identities", "[freeform][editor][models]")
{
    seventhslide::test::ensure_qt_application();
    Editor ed;
    ed.load(make_document("Deck", {"out-main"}), "out-main");
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.insert_slide());
    REQUIRE(ed.set_line_control(true));

    FreeformSlideListModel model;
    model.reload(ed);
    REQUIRE(model.rowCount() == 6);

    ed.select_slide(1);
    model.reload(ed);

    ModelSignals spy(model);
    REQUIRE(ed.delete_slide());
    model.reload(ed);

    REQUIRE(spy.resets == 0);
    REQUIRE(spy.removes == 1);
    REQUIRE(model.rowCount() == 5);
}

TEST_CASE("FreeformFieldListModel patches a value without a reset", "[freeform][editor][models]")
{
    seventhslide::test::ensure_qt_application();
    Editor ed;
    ed.load(make_document("Deck", {"out-main"}), "out-main");
    REQUIRE(ed.add_text());
    REQUIRE(ed.set_text("{title}\n{-subtitle}"));

    FreeformFieldListModel model;
    model.reload(ed);
    REQUIRE(model.rowCount() == 2);
    REQUIRE(model.data(model.index(0), FreeformFieldListModel::TokenRole).toString()
            == QStringLiteral("{title}"));
    REQUIRE(model.data(model.index(1), FreeformFieldListModel::TokenRole).toString()
            == QStringLiteral("{-subtitle}"));

    // Typing a value must not tear down the delegate holding the caret.
    ModelSignals spy(model);
    REQUIRE(ed.set_field("title", "Sunday"));
    model.reload(ed);
    REQUIRE(spy.resets == 0);
    REQUIRE(spy.changes == 1);
    REQUIRE(model.data(model.index(0), FreeformFieldListModel::ValueRole).toString()
            == QStringLiteral("Sunday"));

    // A new row is a change of shape, so it still resets.
    REQUIRE(ed.add_field("speaker"));
    model.reload(ed);
    REQUIRE(spy.resets == 1);
    REQUIRE(model.rowCount() == 3);
    REQUIRE(model.data(model.index(2), FreeformFieldListModel::UnusedRole).toBool());
}
