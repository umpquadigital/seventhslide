// A geometry edit refreshes only the slide it moved.

#include <catch2/catch_test_macros.hpp>

#include "slide_edit_session.hpp"
#include "freeform_editor_models.hpp"
#include "canvas_test_helpers.hpp"
#include "freeform_model_test_helpers.hpp"
#include "freeform_library_controller.hpp"
#include "facade_all.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/model/output_config.hpp>

#include <QAbstractItemModel>
#include <QImage>
#include <QSignalSpy>
#include <QVariantMap>

#include <functional>
#include <utility>
#include <vector>

namespace CanvasAlignQml = seventhslide::app::CanvasAlignQml;
namespace CanvasDistributeQml = seventhslide::app::CanvasDistributeQml;
namespace CanvasFlipQml = seventhslide::app::CanvasFlipQml;
namespace CanvasOrderQml = seventhslide::app::CanvasOrderQml;

using seventhslide::app::SlideEditSession;
using seventhslide::app::FreeformSlideListModel;
using seventhslide::app::SessionFacade;
using seventhslide::model::OutputConfig;

namespace {

OutputConfig make_output(const char* id, const char* name)
{
    OutputConfig oc;
    oc.id = id;
    oc.name = name;
    oc.canvas_width = 1920;
    oc.canvas_height = 1080;
    return oc;
}

/// Every row of ``model`` with every role it names.
std::vector<QVariantMap> dump(const QAbstractItemModel& model)
{
    std::vector<QVariantMap> rows;
    const auto roles = model.roleNames();
    for (int r = 0; r < model.rowCount(); ++r) {
        QVariantMap row;
        for (auto it = roles.begin(); it != roles.end(); ++it) {
            row.insert(QString::fromLatin1(it.value()), model.data(model.index(r, 0), it.key()));
        }
        rows.push_back(row);
    }
    return rows;
}

/// Watches every signal a list model uses to tell a view it changed.
struct ModelSignals {
    explicit ModelSignals(QAbstractItemModel* model)
        : reset(model, &QAbstractItemModel::modelReset)
        , inserted(model, &QAbstractItemModel::rowsInserted)
        , removed(model, &QAbstractItemModel::rowsRemoved)
        , moved(model, &QAbstractItemModel::rowsMoved)
        , data(model, &QAbstractItemModel::dataChanged)
    {
    }

    [[nodiscard]] bool structure_untouched() const
    {
        return reset.isEmpty() && inserted.isEmpty() && removed.isEmpty() && moved.isEmpty();
    }

    void clear()
    {
        reset.clear();
        inserted.clear();
        removed.clear();
        moved.clear();
        data.clear();
    }

    QSignalSpy reset;
    QSignalSpy inserted;
    QSignalSpy removed;
    QSignalSpy moved;
    QSignalSpy data;
};

/// Three slides of three boxes, with a field token in them, on slide 1.
SlideEditSession& open_deck(SessionFacade& facade)
{
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_output("out-main", "Main")});
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Deck"));
    auto& edit = *facade.slideEdit();
    auto* objects = edit.objects();
    REQUIRE(edit.openFreeform(id));
    for (int s = 0; s < 3; ++s) {
        if (s > 0) {
            REQUIRE(edit.slides()->insert().isEmpty());
            edit.slides()->select(s);
        }
        // Spread out and unevenly spaced, so aligning or distributing them
        // always moves something.
        constexpr double k_x[] = {100.0, 250.0, 700.0};
        for (int b = 0; b < 3; ++b) {
            REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());  // the new box arrives selected
            const auto object_id = objects->selection().value(QStringLiteral("id")).toString();
            REQUIRE(objects->setText(object_id, QStringLiteral("{speaker} %1.%2").arg(s).arg(b), false,
                                     false, false)
                        .isEmpty());
            REQUIRE(objects->setGeometries(seventhslide::test::geometry(object_id, k_x[b],
                                                                        100.0 + 200.0 * b, 400, 100))
                        .isEmpty());
        }
    }
    REQUIRE(edit.fields()->add(QStringLiteral("speaker")).isEmpty());
    REQUIRE(edit.fields()->set(QStringLiteral("speaker"), QStringLiteral("Ada")).isEmpty());
    edit.slides()->select(1);
    objects->selectAll();
    return edit;
}

}  // namespace

TEST_CASE("A geometry edit updates only the moved slide's thumbnail", "[app][freeform][S3]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto& edit = open_deck(facade);
    auto* objects = edit.objects();
    auto* slides = edit.slides()->list();
    auto* fields = edit.fields()->list();

    // Each edit changes how slide 1 looks and nothing else.
    const std::vector<std::pair<const char*, std::function<QString()>>> edits = {
        {"nudge", [&] { return objects->nudge(4, 0); }},
        {"distribute", [&] { return objects->distribute(CanvasDistributeQml::Horizontal); }},
        {"align", [&] { return objects->align(CanvasAlignQml::Left); }},
        {"flip", [&] { return objects->flip(CanvasFlipQml::Horizontal); }},
        {"setGeometries", [&] {
             const auto obj = objects->canvasObjects().front().toMap();
             QVariantMap g;
             g.insert(QStringLiteral("id"), obj.value(QStringLiteral("id")));
             g.insert(QStringLiteral("x"), 333.0);
             g.insert(QStringLiteral("y"), 222.0);
             g.insert(QStringLiteral("w"), 500.0);
             g.insert(QStringLiteral("h"), 120.0);
             g.insert(QStringLiteral("rotation"), 0.0);
             return objects->setGeometries({g});
         }},
    };
    for (const auto& [name, run] : edits) {
        INFO(name);
        const auto other0 = seventhslide::test::slide_thumb_url(*slides, 0);
        const auto other2 = seventhslide::test::slide_thumb_url(*slides, 2);
        const auto before = seventhslide::test::slide_thumb_url(*slides, 1);
        const auto revision = edit.slides()->preview_revision();
        ModelSignals slide_signals(slides);
        ModelSignals field_signals(fields);
        QSignalSpy changed(&edit, &SlideEditSession::documentChanged);

        REQUIRE(run().isEmpty());

        REQUIRE(seventhslide::test::slide_thumb_url(*slides, 1) != before);
        REQUIRE(seventhslide::test::slide_thumb_url(*slides, 0) == other0);
        REQUIRE(seventhslide::test::slide_thumb_url(*slides, 2) == other2);
        REQUIRE(edit.slides()->preview_revision() > revision);
        REQUIRE(changed.size() == 1);
        // One row, its key and thumb, and the row list itself untouched.
        REQUIRE(slide_signals.structure_untouched());
        REQUIRE(slide_signals.data.size() == 1);
        const auto top = slide_signals.data.front().at(0).toModelIndex();
        REQUIRE(slides->data(top, FreeformSlideListModel::IndexRole).toInt() == 1);
        // The field list does not depend on where objects sit.
        REQUIRE(field_signals.structure_untouched());
        REQUIRE(field_signals.data.isEmpty());
    }
}

TEST_CASE("After geometry edits the lists match a full rebuild", "[app][freeform][S3]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto& edit = open_deck(facade);
    auto* objects = edit.objects();
    REQUIRE(objects->nudge(7, -3).isEmpty());
    REQUIRE(objects->align(CanvasAlignQml::Center).isEmpty());
    REQUIRE(objects->flip(CanvasFlipQml::Vertical).isEmpty());
    objects->beginGesture();
    REQUIRE(objects->nudge(1, 1).isEmpty());
    objects->endGesture();

    const auto slides_after = dump(*edit.slides()->list());
    const auto fields_after = dump(*edit.fields()->list());
    // Selecting the slide that is already current rebuilds both lists.
    edit.slides()->select(1);
    REQUIRE(dump(*edit.slides()->list()) == slides_after);
    REQUIRE(dump(*edit.fields()->list()) == fields_after);

    // Undo goes back through the full refresh and restores the thumbnail.
    const auto moved = seventhslide::test::slide_thumb_url(*edit.slides()->list(), 1);
    REQUIRE(edit.undo().isEmpty());
    REQUIRE(seventhslide::test::slide_thumb_url(*edit.slides()->list(), 1) != moved);
}

TEST_CASE("Ending a gesture leaves the lists alone", "[app][freeform][S3]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    auto& edit = open_deck(facade);
    edit.objects()->beginGesture();
    REQUIRE(edit.objects()->nudge(12, 0).isEmpty());
    ModelSignals slide_signals(edit.slides()->list());
    ModelSignals field_signals(edit.fields()->list());
    QSignalSpy changed(&edit, &SlideEditSession::documentChanged);

    edit.objects()->endGesture();

    REQUIRE(changed.size() == 1);
    REQUIRE(slide_signals.structure_untouched());
    REQUIRE(slide_signals.data.isEmpty());
    REQUIRE(field_signals.structure_untouched());
    REQUIRE(field_signals.data.isEmpty());
}
