// The Freeform library and editor through the facade (FreeformLibraryController, SlideEditSession).

#include "canvas_test_helpers.hpp"
#include "session_facade_fixture.hpp"

#include "freeform_model_test_helpers.hpp"
#include "font_controller.hpp"
#include "font_fixtures.hpp"
#include "font_store.hpp"
#include "freeform_thumb_cache.hpp"
#include "media_controller.hpp"

#include <filesystem>
#include <memory>

#include <QImage>
#include <QUrl>
#include <QUrlQuery>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using namespace seventhslide::test::facade;

TEST_CASE("SessionFacade playFreeform plays a library Freeform item", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    auto doc = seventhslide::freeform::make_document("Hello Ann", {facade.session().primary_output()->id});
    seventhslide::freeform::Object box;
    box.type = seventhslide::freeform::ObjectType::Text;
    box.x = 10;
    box.y = 20;
    box.w = 400;
    box.h = 100;
    box.bold = true;
    box.text = "{title}";
    doc.decks[0].slides[0].objects.push_back(box);
    doc.fields["title"] = "On Screen";

    seventhslide::model::FreeformItem item;
    item.name = "Hello Ann";
    item.document = doc.to_json();
    const auto freeform_id = facade.freeform_repo()->create_item(item);
    facade.refresh_library();

    REQUIRE(facade.live()->playFreeform(static_cast<qint64>(freeform_id)));
    REQUIRE(facade.live()->mode() == QStringLiteral("freeform"));
    REQUIRE(facade.live()->current_title().contains(QStringLiteral("Hello")));

    const auto frames = facade.session().scene_frames();
    REQUIRE_FALSE(frames.empty());
    REQUIRE(frames[0].mode == seventhslide::scene::SceneMode::Freeform);
    REQUIRE(frames[0].overlay.empty());
    REQUIRE_FALSE(frames[0].objects.empty());
    REQUIRE(seventhslide::model::plain_text(frames[0].objects[0].text->lines[0]) == "On Screen");
    REQUIRE(facade.program()->freeformGallery());
}

TEST_CASE("SessionFacade says why a Freeform item from another version will not open", "[app][freeform]")
{
    // Refused rather than misread (freeform::document_refusal). Opening or
    // playing it must say so, not silently do nothing.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto document = seventhslide::freeform::make_document("Newer", {facade.session().primary_output()->id})
                        .to_json();
    document["schema_version"] = seventhslide::freeform::k_schema_version + 1;
    seventhslide::model::FreeformItem item;
    item.name = "Newer";
    item.document = document;
    const auto id = static_cast<qint64>(facade.freeform_repo()->create_item(item));

    QSignalSpy errors(&facade, &SessionFacade::errorOccurred);
    CHECK_FALSE(facade.live()->playFreeform(id));
    CHECK_FALSE(facade.slideEdit()->openFreeform(id));
    REQUIRE(errors.size() == 2);
    for (const auto& error : errors) {
        const QString message = error.at(0).toString();
        CHECK(message.contains(QStringLiteral("\"Newer\"")));
        CHECK(message.contains(QStringLiteral("different version")));
    }

    // Refused, it is still listed, and it can be deleted: an item nothing can
    // open must not be one nothing can remove. (The row went in under the
    // controller, so the browser is told to look again.)
    facade.freeformLibrary()->rebuild_freeform_browser();
    auto* browser = facade.freeformLibrary()->browser();
    bool listed = false;
    for (int r = 0; r < browser->rowCount(); ++r) {
        listed = listed
            || browser->data(browser->index(r), seventhslide::app::MediaBrowserModel::NameRole)
                       .toString()
                == QStringLiteral("Newer");
    }
    CHECK(listed);
    REQUIRE(facade.freeformLibrary()->deleteItem(id).isEmpty());
    CHECK_FALSE(facade.freeform_repo()->get_item(id).has_value());
}

TEST_CASE("SessionFacade Freeform library CRUD and play", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto created = facade.freeformLibrary()->createFreeform(QStringLiteral("Untitled"));
    REQUIRE(created >= 0);
    facade.refresh_library();
    REQUIRE(facade.freeform_repo()->list_items_in_folder(std::nullopt).size() == 1);

    const auto dup = facade.freeformLibrary()->duplicateItem(created);
    REQUIRE(dup >= 0);
    REQUIRE(dup != created);
    facade.refresh_library();
    REQUIRE(facade.freeform_repo()->list_items_in_folder(std::nullopt).size() == 2);
    REQUIRE(facade.freeformLibrary()->deleteItem(created).isEmpty());
    facade.refresh_library();
    REQUIRE(facade.freeform_repo()->list_items_in_folder(std::nullopt).size() == 1);

    REQUIRE(facade.library()->createService(QStringLiteral("Svc")) > 0);
    REQUIRE(facade.freeformLibrary()->addFreeformToService(dup));
    REQUIRE(facade.serviceItems()->rowCount() == 1);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(0),
                       seventhslide::app::ServiceItemModel::ItemTypeRole)
                .toString()
            == QStringLiteral("freeform"));

    std::ifstream in(std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "freeform"
                     / "simple_two_outputs.json");
    REQUIRE(in);
    nlohmann::json j;
    in >> j;
    using seventhslide::model::FreeformItem;
    // The fixture's decks are keyed by fixture ids; bind them to this
    // library's outputs.
    j["decks"][0]["output"] = facade.session().primary_output()->id;
    FreeformItem item;
    item.name = "Welcome";
    item.document = j;
    const auto fid = facade.freeform_repo()->create_item(item);
    REQUIRE(facade.live()->playFreeform(static_cast<qint64>(fid)));
    REQUIRE(facade.program()->freeformGallery());
    REQUIRE(facade.program()->totalLines() == 3);
    REQUIRE(facade.program()->lines()->rowCount() == 3);
    REQUIRE(facade.program()->jumpToLine(2));
    REQUIRE(facade.program()->lineCursor() == 2);
    const auto thumb = facade.outputPreviews()->slideThumbUrl(facade.program(), 2);
    REQUIRE(thumb.contains(QStringLiteral("slide=2")));
    // What is live names no side; its slide is drawn from the Program.
    CHECK_FALSE(thumb.contains(QStringLiteral("side=")));
    const auto out = facade.session().primary_output()->id;
    const auto at = facade.session().scene_frame_at({facade.session().focused_stack_id()}, out, 2);
    REQUIRE(at.has_value());
    REQUIRE_FALSE(at->objects.empty());
    REQUIRE(seventhslide::model::plain_text(at->objects[0].text->lines[0]) == "Main only — slot 3");
    CHECK_FALSE(facade.outputPreviews()
                    ->render_output_preview(QString::fromStdString(out), QSize(160, 90), 2)
                    .isNull());

    // A Preview's slide says so in its address, and is drawn from the
    // Preview: nothing, while the stack has none.
    using seventhslide::session::StackSide;
    seventhslide::app::SideView preview(facade.session(), StackSide::Preview);
    const auto cued = facade.outputPreviews()->slideThumbUrl(&preview, 2);
    CHECK(cued.contains(QStringLiteral("slide=2")));
    CHECK(cued.contains(QStringLiteral("side=preview")));
    CHECK(facade.outputPreviews()
              ->render_output_preview(QString::fromStdString(out), QSize(160, 90), 2,
                                      StackSide::Preview)
              .isNull());
}

TEST_CASE("A browser inside a deleted folder goes back to the root and says so",
          "[app][freeform][media-browser]")
{
    // The browsers only offer to delete the folders they list, but a folder
    // can still go with an ancestor (any caller may name one). The browser
    // must not keep showing, or reporting, a folder that is gone.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    auto* freeform = facade.freeformLibrary();
    const auto outer = freeform->createFolder(QStringLiteral("Outer"));
    freeform->setFolder(outer);
    const auto inner = freeform->createFolder(QStringLiteral("Inner"));
    freeform->setFolder(inner);
    REQUIRE(freeform->folderId() == inner);
    QSignalSpy freeform_changed(freeform, &seventhslide::app::FreeformLibraryController::changed);
    REQUIRE(freeform->deleteFolder(outer).isEmpty());
    REQUIRE(freeform->folderId() == -1);
    REQUIRE(freeform_changed.count() >= 1);

    auto* media = facade.media();
    const auto image = MediaKindQml::Image;
    const auto media_outer = media->library(image)->createFolder(QStringLiteral("Outer"));
    media->library(image)->setFolder(media_outer);
    const auto media_inner = media->library(image)->createFolder(QStringLiteral("Inner"));
    media->library(image)->setFolder(media_inner);
    REQUIRE(media->folder_id(seventhslide::db::MediaKind::Image) == media_inner);
    QSignalSpy media_changed(media, &seventhslide::app::MediaController::changed);
    REQUIRE(media->library(image)->deleteFolder(media_outer).isEmpty());
    REQUIRE(media->folder_id(seventhslide::db::MediaKind::Image) == -1);
    REQUIRE(media_changed.count() >= 1);
}

TEST_CASE("SessionFacade Freeform browser folders search and service expand", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto root_item = facade.freeformLibrary()->createFreeform(QStringLiteral("Root Card"));
    REQUIRE(root_item >= 0);
    REQUIRE(facade.freeformLibrary()->folderId() == -1);
    REQUIRE(facade.freeformLibrary()->browser()->rowCount() >= 1);

    const auto folder = facade.freeformLibrary()->createFolder(QStringLiteral("Service"));
    REQUIRE(folder >= 0);
    facade.freeformLibrary()->setFolder(folder);
    REQUIRE(facade.freeformLibrary()->folderId() == folder);

    const auto nested = facade.freeformLibrary()->createFreeform(QStringLiteral("Nested Card"));
    REQUIRE(nested >= 0);

    bool found_nested = false;
    bool found_root = false;
    for (int i = 0; i < facade.freeformLibrary()->browser()->rowCount(); ++i) {
        const auto ix = facade.freeformLibrary()->browser()->index(i, 0);
        const auto kind = facade.freeformLibrary()->browser()
                              ->data(ix, seventhslide::app::MediaBrowserModel::KindRole)
                              .toString();
        const auto name = facade.freeformLibrary()->browser()
                              ->data(ix, seventhslide::app::MediaBrowserModel::NameRole)
                              .toString();
        if (kind == QStringLiteral("file") && name == QStringLiteral("Nested Card")) {
            found_nested = true;
            const auto thumb = facade.freeformLibrary()->browser()
                                   ->data(ix, seventhslide::app::MediaBrowserModel::ThumbUrlRole)
                                   .toString();
            REQUIRE(thumb.contains(QStringLiteral("image://freeformthumb/")));
        }
        if (name == QStringLiteral("Root Card")) {
            found_root = true;
        }
    }
    REQUIRE(found_nested);
    REQUIRE_FALSE(found_root);

    facade.freeformLibrary()->setSearch(QStringLiteral("nope"));
    int files = 0;
    for (int i = 0; i < facade.freeformLibrary()->browser()->rowCount(); ++i) {
        const auto ix = facade.freeformLibrary()->browser()->index(i, 0);
        if (facade.freeformLibrary()->browser()->data(ix, seventhslide::app::MediaBrowserModel::KindRole)
                .toString()
            == QStringLiteral("file")) {
            ++files;
        }
    }
    REQUIRE(files == 0);
    facade.freeformLibrary()->setSearch(QString());

    REQUIRE(facade.library()->createService(QStringLiteral("Svc")) > 0);
    REQUIRE(facade.freeformLibrary()->addFreeformFolderToService(folder));
    REQUIRE(facade.serviceItems()->rowCount() == 1);

    REQUIRE(facade.freeformLibrary()->deleteFolder(folder).isEmpty());
    REQUIRE_FALSE(facade.freeform_repo()->get_item(static_cast<std::int64_t>(nested)));
    facade.freeformLibrary()->setFolder(-1);
    found_root = false;
    for (int i = 0; i < facade.freeformLibrary()->browser()->rowCount(); ++i) {
        const auto ix = facade.freeformLibrary()->browser()->index(i, 0);
        if (facade.freeformLibrary()->browser()->data(ix, seventhslide::app::MediaBrowserModel::NameRole)
                .toString()
            == QStringLiteral("Root Card")) {
            found_root = true;
        }
    }
    REQUIRE(found_root);
}

TEST_CASE("SessionFacade Freeform editor save writes document", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Untitled"));
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(id));
    REQUIRE(edit->active());
    REQUIRE(edit->setName(QStringLiteral("Welcome")).isEmpty());
    REQUIRE(add_text(facade, QStringLiteral("Hello {title}")).isEmpty());
    REQUIRE(edit->objects()->hasSelection());
    REQUIRE(edit->fields()->add(QStringLiteral("title")).isEmpty());
    REQUIRE(edit->fields()->set(QStringLiteral("title"), QStringLiteral("Sunday")).isEmpty());
    REQUIRE(edit->slides()->list()->rowCount() == 1);
    REQUIRE(edited_object_count(facade) == 1);
    REQUIRE(edit->slides()->insert().isEmpty());
    REQUIRE(edit->slides()->list()->rowCount() == 2);
    REQUIRE(edit->save().isEmpty());
    REQUIRE_FALSE(edit->active());

    REQUIRE(edit->openFreeform(id));
    REQUIRE(edit->name() == QStringLiteral("Welcome"));
    REQUIRE(edit->slides()->list()->rowCount() == 2);
    edit->slides()->select(0);
    REQUIRE(edited_object_count(facade) == 1);
    REQUIRE(edit->fields()->list()->rowCount() >= 1);
    edit->cancel();
}

TEST_CASE("Saving a different Freeform leaves the live one on air", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto live_id = add_library_freeform(facade, "On Air", 1);
    const auto edited_id = add_library_freeform(facade, "Draft", 1);

    REQUIRE(facade.live()->playFreeform(live_id));
    REQUIRE(facade.slideEdit()->openFreeform(edited_id));
    REQUIRE(facade.slideEdit()->setName(QStringLiteral("Draft v2")).isEmpty());
    REQUIRE(facade.slideEdit()->save().isEmpty());

    REQUIRE(facade.session().focused_stack().program.freeform().name == "On Air");
    REQUIRE(facade.session().focused_stack().program.freeform().library_id == live_id);
    REQUIRE(facade.live()->current_title() == QStringLiteral("On Air"));
}

TEST_CASE("Saving the live Freeform updates it in place and keeps the slide", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = add_library_freeform(facade, "Welcome", 3);

    REQUIRE(facade.live()->playFreeform(id));
    REQUIRE(facade.program()->nextSlide());
    const int cursor = facade.program()->lineCursor();
    REQUIRE(cursor == 1);

    REQUIRE(facade.slideEdit()->openFreeform(id));
    REQUIRE(facade.slideEdit()->setName(QStringLiteral("Welcome v2")).isEmpty());
    REQUIRE(facade.slideEdit()->save().isEmpty());

    REQUIRE(facade.session().focused_stack().program.freeform().name == "Welcome v2");
    REQUIRE(facade.live()->current_title() == QStringLiteral("Welcome v2"));
    REQUIRE(facade.program()->lineCursor() == cursor);
}

TEST_CASE("Saving a library Freeform does not touch a rundown copy of it", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = add_library_freeform(facade, "Announcements", 1);

    const auto service_id = facade.service_repo()->create_service("Sunday");
    REQUIRE(facade.library()->openService(static_cast<qint64>(service_id)));
    REQUIRE(facade.freeformLibrary()->addFreeformToService(id));
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.live()->service_item_index() == 0);

    REQUIRE(facade.slideEdit()->openFreeform(id));
    REQUIRE(facade.slideEdit()->setName(QStringLiteral("Changed")).isEmpty());
    REQUIRE(facade.slideEdit()->save().isEmpty());

    REQUIRE(facade.live()->service_item_index() == 0);
    REQUIRE(facade.session().focused_stack().program.freeform().name == "Announcements");
}

TEST_CASE("Freeform editor clipboard is reachable from the editor's shortcuts", "[app][freeform][clipboard]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Clip"));
    REQUIRE(facade.slideEdit()->openFreeform(id));
    auto* edit = facade.slideEdit()->objects();
    REQUIRE(edit->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(edited_object_count(facade) == 1);

    // The editor's Ctrl+V handler reads this as a property. A plain C++
    // member function is invisible to the meta-object system, so QML saw
    // `undefined`, the guard was always false, and paste silently did nothing.
    REQUIRE(edit->metaObject()->indexOfProperty("canPaste") >= 0);
    REQUIRE_FALSE(edit->property("canPaste").toBool());

    REQUIRE(edit->copySelected().isEmpty());
    REQUIRE(edit->property("canPaste").toBool());
    REQUIRE(edit->paste().isEmpty());
    REQUIRE(edited_object_count(facade) == 2);

    facade.slideEdit()->cancel();
}

namespace {

/// A slide editor on a document store of its own: the facade's outputs,
/// previews and themes, but the documents the test hands it.
struct StoreEditor {
    SessionFacade facade;
    FakeDocumentStore host;
    std::unique_ptr<seventhslide::app::SlideEditSession> edit;

    explicit StoreEditor(std::vector<OutputConfig> outputs)
    {
        REQUIRE(facade.open_memory_database());
        facade.set_outputs(std::move(outputs));
        edit = std::make_unique<seventhslide::app::SlideEditSession>(facade.session(), facade.previews(),
                                                                     host, *facade.themes());
    }
};

}  // namespace

TEST_CASE("The slide editor runs on any document store", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    StoreEditor w({make_main()});
    auto& edit = *w.edit;
    auto& host = w.host;

    SECTION("nothing to open when the store has no such document")
    {
        REQUIRE_FALSE(edit.openFreeform(7));
        REQUIRE_FALSE(edit.active());
    }

    host.stored = seventhslide::freeform::make_document("Promo", {"out-main"});

    SECTION("opening, editing and saving go through the store")
    {
        int changes = 0;
        QObject::connect(&edit, &seventhslide::app::SlideEditSession::documentChanged, &edit,
                         [&changes]() { ++changes; });

        REQUIRE(edit.openFreeform(7));
        REQUIRE(edit.active());
        REQUIRE_FALSE(edit.dirty());
        REQUIRE(changes == 1);

        REQUIRE(edit.objects()->insert(QStringLiteral("text")).isEmpty());
        REQUIRE(edit.dirty());
        REQUIRE(edit.objects()->hasSelection());

        REQUIRE(edit.save().isEmpty());
        // The store was handed the edited document, and the session closed.
        REQUIRE(host.committed_id == 7);
        REQUIRE(host.committed.has_value());
        REQUIRE_FALSE(edit.active());
    }

    SECTION("a store that refuses the save keeps the session open")
    {
        REQUIRE(edit.openFreeform(7));
        REQUIRE(edit.objects()->insert(QStringLiteral("text")).isEmpty());
        host.commit_error = QStringLiteral("disk full");

        REQUIRE(edit.save() == QStringLiteral("disk full"));
        // Still open, still dirty: the operator has not lost the edit.
        REQUIRE(edit.active());
        REQUIRE(edit.dirty());
    }
}

TEST_CASE("Opening a Freeform item lays its decks out on their outputs' canvases", "[app][freeform][canvas]")
{
    seventhslide::test::ensure_qt_application();
    auto main = make_main();
    main.canvas_width = 1280;
    main.canvas_height = 720;
    StoreEditor w({main});
    auto& edit = *w.edit;
    auto& host = w.host;
    const seventhslide::geom::CanvasSize main_canvas{1280, 720};

    SECTION("a stored deck is fitted to its output")
    {
        host.stored = seventhslide::freeform::make_document("Promo", {"out-main"});
        REQUIRE(edit.openFreeform(7));
        const auto& decks = edit.freeform_editor().document().decks;
        REQUIRE(decks.size() == 1);
        CHECK(decks[0].canvas == main_canvas);
        CHECK(edit.freeform_editor().output_id() == "out-main");
    }

    SECTION("an item with no deck yet gets one for the primary output, fitted")
    {
        seventhslide::freeform::Document empty;
        empty.name = "New";
        host.stored = empty;
        REQUIRE(edit.openFreeform(7));
        const auto& decks = edit.freeform_editor().document().decks;
        REQUIRE(decks.size() == 1);
        CHECK(decks[0].output_id == "out-main");
        CHECK(decks[0].canvas == main_canvas);
        CHECK(edit.freeform_editor().output_id() == "out-main");
    }

    SECTION("a document without the primary output's deck opens on its first deck")
    {
        host.stored = seventhslide::freeform::make_document("Stage", {"out-stage"});
        REQUIRE(edit.openFreeform(7));
        const auto& decks = edit.freeform_editor().document().decks;
        REQUIRE(decks.size() == 1);
        CHECK(edit.freeform_editor().output_id() == "out-stage");
        // No output paints out-stage, so its deck keeps the canvas it was made on.
        CHECK(decks[0].canvas == seventhslide::geom::k_reference_canvas);
    }
}

TEST_CASE("Freeform editor offers only the outputs that are on", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    auto stage = make_main();
    stage.id = "out-stage";
    stage.name = "Stage";
    auto lobby = make_main();
    lobby.id = "out-lobby";
    lobby.name = "Lobby";
    StoreEditor w({make_main(), stage, lobby});
    REQUIRE(w.facade.session().set_output_on("out-stage", false));
    auto& edit = *w.edit;
    auto& host = w.host;

    SECTION("an output that is off is not offered")
    {
        host.stored = seventhslide::freeform::make_document(
            "Promo", {"out-main", "out-stage", "out-lobby"});
        REQUIRE(edit.openFreeform(7));
        CHECK(edit.slides()->deckOutputs()
              == QStringList{QStringLiteral("Main"), QStringLiteral("Lobby")});
    }
    SECTION("an item with no deck on the primary opens on an output that is on")
    {
        host.stored = seventhslide::freeform::make_document("Promo", {"out-stage", "out-lobby"});
        REQUIRE(edit.openFreeform(7));
        CHECK(edit.output() == QStringLiteral("Lobby"));
    }
    SECTION("an item only an output that is off has still opens")
    {
        host.stored = seventhslide::freeform::make_document("Promo", {"out-stage"});
        REQUIRE(edit.openFreeform(7));
        CHECK(edit.output() == QStringLiteral("Stage"));
    }
}

TEST_CASE("Freeform editor: deck slide and cue changes reload the lists and object picks only reselect",
          "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    auto stage = make_main();
    stage.id = "out-stage";
    stage.name = "Stage";
    StoreEditor w({make_main(), stage});
    w.host.stored = seventhslide::freeform::make_document("Promo", {"out-main", "out-stage"});
    auto& edit = *w.edit;
    auto* objects = edit.objects();
    REQUIRE(edit.openFreeform(7));
    REQUIRE(edit.slides()->insert().isEmpty());
    REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
    const QString obj = objects->selectedIds().value(0);
    REQUIRE_FALSE(obj.isEmpty());

    using seventhslide::app::FreeformSlideListModel;
    const auto selected_slide = [&edit]() {
        const auto* m = edit.slides()->list();
        for (int r = 0; r < m->rowCount(); ++r) {
            const auto ix = m->index(r);
            if (m->data(ix, FreeformSlideListModel::KindRole).toString() == QLatin1String("slide")
                && m->data(ix, FreeformSlideListModel::SelectedRole).toBool()) {
                return m->data(ix, FreeformSlideListModel::IndexRole).toInt();
            }
        }
        return -1;
    };
    QSignalSpy doc(&edit, &seventhslide::app::SlideEditSession::documentChanged);
    QSignalSpy sel(&edit, &seventhslide::app::SlideEditSession::selectionChanged);
    // Signals since the last call, as (document, selection).
    const auto signals_since = [&]() {
        const auto out = std::pair{static_cast<int>(doc.count()), static_cast<int>(sel.count())};
        doc.clear();
        sel.clear();
        return out;
    };
    signals_since();

    // Which objects are selected is in neither list.
    objects->selectObject(obj, false);
    CHECK(signals_since() == std::pair{0, 1});
    objects->clearSelection();
    CHECK(signals_since() == std::pair{0, 1});
    CHECK(objects->selectedIds().isEmpty());
    objects->selectIds({obj}, false);
    CHECK(signals_since() == std::pair{0, 1});
    CHECK(objects->selectedIds() == QStringList{obj});
    objects->selectAll();
    CHECK(signals_since() == std::pair{0, 1});

    // The slide, the deck and the cue are what the lists show.
    REQUIRE(selected_slide() == 1);
    edit.slides()->select(0);
    CHECK(signals_since() == std::pair{1, 1});
    CHECK(selected_slide() == 0);
    edit.setOutput(QStringLiteral("Stage"));
    CHECK(signals_since() == std::pair{1, 1});
    CHECK(edit.freeform_editor().output_id() == "out-stage");
    edit.cues()->select(0);
    CHECK(signals_since() == std::pair{1, 1});

    // Ending a gesture can sync mirrored decks: the document changed, the
    // selection did not.
    objects->endGesture();
    CHECK(signals_since() == std::pair{1, 0});

    // No cues: the cue fields read as empty, and the verse number as 1.
    REQUIRE(edit.freeform_editor().document().cues.empty());
    const auto cue = edit.cues()->current();
    CHECK(cue.value(QStringLiteral("text")).toString().isEmpty());
    CHECK(cue.value(QStringLiteral("verse_type")).toString().isEmpty());
    CHECK(cue.value(QStringLiteral("verse_number")).toInt() == 1);
}

TEST_CASE("Freeform editor's background fields show only the kind the slide uses", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    StoreEditor w({make_main()});
    w.host.stored = seventhslide::freeform::make_document("Promo", {"out-main"});
    auto& edit = *w.edit;
    REQUIRE(edit.openFreeform(7));

    const auto fields = [&edit]() {
        const auto bg = edit.slides()->background();
        return QStringList{bg.value(QStringLiteral("kind")).toString(),
                           bg.value(QStringLiteral("color")).toString(),
                           bg.value(QStringLiteral("image")).toString(),
                           bg.value(QStringLiteral("video")).toString()};
    };
    CHECK(fields() == QStringList{QStringLiteral("none"), QString(), QString(), QString()});
    // This editor has nothing to copy a background theme with, and says so.
    CHECK_FALSE(edit.slides()->copyBackgroundTheme(QStringLiteral("t-dusk")).isEmpty());
    CHECK(fields() == QStringList{QStringLiteral("none"), QString(), QString(), QString()});
    REQUIRE(edit.slides()->setBackground(QStringLiteral("color"), QStringLiteral("#123456")).isEmpty());
    CHECK(fields() == QStringList{QStringLiteral("color"), QStringLiteral("#123456"), QString(), QString()});
    REQUIRE(edit.slides()->setBackground(QStringLiteral("image"), QStringLiteral("dawn.png")).isEmpty());
    CHECK(fields() == QStringList{QStringLiteral("image"), QString(), QStringLiteral("dawn.png"), QString()});
    REQUIRE(edit.slides()->setBackground(QStringLiteral("video"), QStringLiteral("dusk.mp4")).isEmpty());
    CHECK(fields() == QStringList{QStringLiteral("video"), QString(), QString(), QStringLiteral("dusk.mp4")});
}

TEST_CASE("SessionFacade slideEdit.dirty gates Save", "[app][freeform][dirty]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Untitled"));
    auto* edit = facade.slideEdit();

    REQUIRE(edit->openFreeform(id));
    REQUIRE(edit->active());
    REQUIRE_FALSE(edit->dirty());

    SECTION("a mutation marks dirty and undoing back to the baseline clears it")
    {
        REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
        REQUIRE(edit->dirty());
        REQUIRE(edit->undo().isEmpty());
        REQUIRE_FALSE(edit->dirty());
    }

    SECTION("renaming counts as dirty, so Save stays reachable for a rename")
    {
        REQUIRE(edit->setName(QStringLiteral("Welcome")).isEmpty());
        REQUIRE(edit->dirty());
    }

    SECTION("saving and reopening starts clean")
    {
        REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
        REQUIRE(edit->save().isEmpty());
        REQUIRE(edit->openFreeform(id));
        REQUIRE_FALSE(edit->dirty());
        edit->cancel();
    }
}

TEST_CASE("SessionFacade Freeform editor thumbs render the draft slide", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Thumbs"));
    auto* edit = facade.slideEdit();
    auto* slides = edit->slides();
    auto* objects = edit->objects();
    const auto thumb = [slides](int slide) {
        return seventhslide::test::slide_thumb_url(*slides->list(), slide);
    };
    REQUIRE(edit->openFreeform(id));
    REQUIRE(add_text(facade, QStringLiteral("Hello Sunday")).isEmpty());

    const auto url = thumb(0);
    REQUIRE(url.contains(QStringLiteral("image://freeformpreview/")));
    REQUIRE(url.contains(QStringLiteral("id=")));
    REQUIRE(url.contains(QStringLiteral("v=")));
    REQUIRE_FALSE(url.contains(QStringLiteral("slide=")));
    const auto rev = slides->preview_revision();
    REQUIRE(rev > 0);

    const QImage img = slides->render_preview(
        QString::fromStdString(edit->freeform_editor().output_id()), 0, QSize(160, 90));
    REQUIRE_FALSE(img.isNull());
    REQUIRE(img.width() == 160);
    REQUIRE(img.height() == 90);

    const auto object_id = objects->selection().value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(object_id.isEmpty());
    objects->clearSelection();
    REQUIRE(slides->preview_revision() == rev);
    REQUIRE(thumb(0) == url);
    objects->selectObject(object_id);
    REQUIRE(slides->preview_revision() == rev);
    REQUIRE(thumb(0) == url);

    REQUIRE(edit->setName(QStringLiteral("Thumbs renamed")).isEmpty());
    REQUIRE(thumb(0) == url);
    REQUIRE(slides->preview_revision() == rev);

    REQUIRE(slides->insert().isEmpty());
    slides->select(1);
    REQUIRE(add_text(facade, QStringLiteral("Other slide")).isEmpty());
    const auto url1 = thumb(1);
    REQUIRE_FALSE(url1.isEmpty());
    REQUIRE(url1 != url);
    slides->select(0);
    REQUIRE(thumb(0) == url);

    REQUIRE(edit->cues()->setLineControl(true).isEmpty());
    edit->cues()->select(0);
    REQUIRE(edit->cues()->rename(QStringLiteral("Intro")).isEmpty());
    REQUIRE(edit->cues()->setVerse(QStringLiteral("Verse"), 1).isEmpty());
    REQUIRE(thumb(0) == url);
    REQUIRE(thumb(1) == url1);

    slides->select(0);
    objects->selectObject(object_id);
    REQUIRE(objects->setGeometries(seventhslide::test::geometry(object_id, 40, 50, 400, 200))
                .isEmpty());
    const auto url0_moved = thumb(0);
    REQUIRE(url0_moved != url);
    REQUIRE(thumb(1) == url1);
    REQUIRE(slides->preview_revision() > rev);

    REQUIRE(objects->setText(object_id, QStringLiteral("Changed"), false, false, false).isEmpty());
    const auto url2 = thumb(0);
    REQUIRE(url2 != url0_moved);
    REQUIRE(thumb(1) == url1);

    REQUIRE(thumb(-1).isEmpty());
    edit->cancel();
    REQUIRE(thumb(0).isEmpty());
    REQUIRE(slides->render_preview(QStringLiteral("Main"), 0, QSize(64, 36)).isNull());
}

namespace {

QString freeform_tile_url(SessionFacade& facade, const QString& name)
{
    auto* browser = facade.freeformLibrary()->browser();
    for (int i = 0; i < browser->rowCount(); ++i) {
        const auto ix = browser->index(i, 0);
        if (browser->data(ix, seventhslide::app::MediaBrowserModel::NameRole).toString() == name) {
            return browser->data(ix, seventhslide::app::MediaBrowserModel::ThumbUrlRole).toString();
        }
    }
    return {};
}

} // namespace

TEST_CASE("Freeform library tiles follow theme and canvas edits, the same in every run",
          "[app][freeform][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    const auto db_path = tmp.path / "library.db";
    qint64 id = -1;
    QString edited;
    {
        SessionFacade facade;
        REQUIRE(facade.open_database(db_path));
        id = facade.freeformLibrary()->createFreeform(QStringLiteral("Card"));
        REQUIRE(id >= 0);
        const auto first = freeform_tile_url(facade, QStringLiteral("Card"));
        REQUIRE(first.contains(QStringLiteral("&t=")));

        // Nothing else touches the browser: the edit alone moves the tile on.
        REQUIRE_FALSE(facade.themes()
                          ->create(QStringLiteral("Main"), QStringLiteral("song"),
                                   QStringLiteral("Stage"))
                          .contains(QStringLiteral("error")));
        const auto themed = freeform_tile_url(facade, QStringLiteral("Card"));
        CHECK(themed != first);

        REQUIRE(facade.outputSettings()
                    ->updateOutputSettings(QStringLiteral("Main"),
                                           {{QStringLiteral("canvasWidth"), 1080},
                                            {QStringLiteral("canvasHeight"), 1920}})
                    .contains(QStringLiteral("name")));
        edited = freeform_tile_url(facade, QStringLiteral("Card"));
        CHECK(edited != themed);

        REQUIRE_FALSE(facade.freeformLibrary()->render_freeform_library_thumb(id, QSize(160, 90))
                          .isNull());
    }

    // The next run names the same themes the same way, so the thumb written
    // above is still current and the provider serves it from disk.
    SessionFacade again;
    REQUIRE(again.open_database(db_path));
    const auto url = freeform_tile_url(again, QStringLiteral("Card"));
    CHECK(url == edited);
    const QUrlQuery query(QUrl(url).query());
    CHECK(seventhslide::app::freeform_thumb_fresh(
        again.media_root(), id,
        query.queryItemValue(QStringLiteral("k"), QUrl::FullyDecoded).toStdString(),
        query.queryItemValue(QStringLiteral("t"), QUrl::FullyDecoded).toStdString()));
}

TEST_CASE("SessionFacade freeform library thumbs match output canvas aspect", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const seventhslide::test::TempDir tmp;
    facade.set_media_root(tmp.path);

    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Aspect Card"));
    REQUIRE(id >= 0);

    const QImage landscape = facade.freeformLibrary()->render_freeform_library_thumb(id, QSize(512, 512));
    REQUIRE_FALSE(landscape.isNull());
    REQUIRE(landscape.width() == 512);
    REQUIRE(landscape.height() == 288);

    const QImage tile = facade.freeformLibrary()->render_freeform_library_thumb(id, QSize(160, 90));
    REQUIRE_FALSE(tile.isNull());
    REQUIRE(tile.width() == 160);
    REQUIRE(tile.height() == 90);

    REQUIRE(facade.outputSettings()->updateOutputSettings(QStringLiteral("Main"),
                                        {{QStringLiteral("canvasWidth"), 1080},
                                         {QStringLiteral("canvasHeight"), 1920}})
                .value(QStringLiteral("name"))
                .toString()
            == QStringLiteral("Main"));
    facade.refresh_library();

    const QImage portrait = facade.freeformLibrary()->render_freeform_library_thumb(id, QSize(512, 512));
    REQUIRE_FALSE(portrait.isNull());
    REQUIRE(portrait.width() == 288);
    REQUIRE(portrait.height() == 512);
}

TEST_CASE("A Freeform tile is painted when it is first asked for, not on save",
          "[app][freeform][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    facade.set_media_root(tmp.path);

    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Card"));
    REQUIRE(id >= 0);
    const auto url = freeform_tile_url(facade, QStringLiteral("Card"));
    REQUIRE_FALSE(url.isEmpty());
    const QUrlQuery query(QUrl(url).query());
    const auto fresh = [&] {
        return seventhslide::app::freeform_thumb_fresh(
            facade.media_root(), id,
            query.queryItemValue(QStringLiteral("k"), QUrl::FullyDecoded).toStdString(),
            query.queryItemValue(QStringLiteral("t"), QUrl::FullyDecoded).toStdString());
    };
    // Creating it stored nothing: the GUI thread painted nothing.
    CHECK_FALSE(fresh());

    // The tile the browser asks for is prepared, laid out and painted (on the
    // provider's thread in the app) and stored under the URL's stamp.
    const auto job = facade.freeformLibrary()->prepare_library_thumb(id, QSize(160, 90));
    REQUIRE(job.lay_out);
    CHECK_FALSE(seventhslide::app::finish_preview(job).isNull());
    CHECK(fresh());
}

TEST_CASE("A Freeform tile drawn before its picture came is drawn again once it has",
          "[app][freeform][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    facade.set_media_root(tmp.path);

    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Card"));
    REQUIRE(id >= 0);
    auto doc = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(doc.has_value());
    REQUIRE_FALSE(doc->decks.empty());
    REQUIRE_FALSE(doc->decks.front().slides.empty());
    auto& background = doc->decks.front().slides.front().background;
    background.kind = seventhslide::freeform::SlideBackgroundKind::Image;
    background.image = "late.png";
    REQUIRE(facade.freeformLibrary()->commit_freeform_document(id, *doc).isEmpty());
    facade.freeformLibrary()->rebuild_freeform_browser();

    const auto fresh = [&](const QString& url) {
        const QUrlQuery query(QUrl(url).query());
        return seventhslide::app::freeform_thumb_fresh(
            facade.media_root(), id,
            query.queryItemValue(QStringLiteral("k"), QUrl::FullyDecoded).toStdString(),
            query.queryItemValue(QStringLiteral("t"), QUrl::FullyDecoded).toStdString());
    };
    const auto centre = [&] {
        const QImage tile = facade.freeformLibrary()->render_freeform_library_thumb(id, QSize(160, 90));
        REQUIRE_FALSE(tile.isNull());
        return tile.pixelColor(tile.width() / 2, tile.height() / 2);
    };

    // Its picture is still on its way: the tile is drawn, but not kept as current.
    const auto before = freeform_tile_url(facade, QStringLiteral("Card"));
    const auto without = centre();
    CHECK_FALSE(fresh(before));
    const auto slide_before = facade.outputPreviews()->slideThumbUrl(facade.program(), 0);

    // Another picture arriving: nothing drawn without it, nothing asked for again.
    std::filesystem::create_directories(tmp.path / "images");
    const auto themes_before = facade.previews().theme_revision();
    const auto previews_before = facade.preview_revision();
    facade.media_arrived({"images/other.png"});
    CHECK(freeform_tile_url(facade, QStringLiteral("Card")) == before);
    CHECK(facade.outputPreviews()->slideThumbUrl(facade.program(), 0) == slide_before);
    CHECK(facade.previews().theme_revision() == themes_before);
    CHECK(facade.preview_revision() == previews_before);

    QImage red(64, 36, QImage::Format_RGB32);
    red.fill(Qt::red);
    REQUIRE(red.save(QString::fromStdString((tmp.path / "images" / "late.png").string())));
    facade.media_arrived({"images/late.png"});

    // Asked for again, and drawn with it.
    const auto after = freeform_tile_url(facade, QStringLiteral("Card"));
    CHECK(after != before);
    // So is the controller's slide thumbnail, drawn as the outputs draw, and
    // every preview of what is shown.
    CHECK(facade.outputPreviews()->slideThumbUrl(facade.program(), 0) != slide_before);
    CHECK(facade.preview_revision() != previews_before);
    const auto with = centre();
    CHECK(with != without);
    CHECK(with.red() > 200);
    CHECK(with.green() < 60);
    CHECK(fresh(after));
}

TEST_CASE("SessionFacade Freeform cue verse type and number", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Verses"));
    REQUIRE(facade.slideEdit()->openFreeform(id));
    auto* cues = facade.slideEdit()->cues();
    REQUIRE(cues->setLineControl(true).isEmpty());
    cues->select(0);
    REQUIRE(cues->setVerse(QStringLiteral("Bridge"), 2).isEmpty());
    REQUIRE(cues->current().value(QStringLiteral("verse_type")).toString() == QStringLiteral("Bridge"));
    REQUIRE(cues->current().value(QStringLiteral("verse_number")).toInt() == 2);
    REQUIRE(cues->verseTypes().contains(QStringLiteral("Verse")));
    REQUIRE(cues->setVerse(QStringLiteral(""), 1).isEmpty());
    REQUIRE(cues->current().value(QStringLiteral("verse_type")).toString().isEmpty());
    facade.slideEdit()->cancel();
}

TEST_CASE("SessionFacade Freeform delete cue is gated on orphans", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Cues"));
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(id));
    REQUIRE(edit->slides()->insert().isEmpty());
    auto* cues = edit->cues();
    REQUIRE(cues->setLineControl(true).isEmpty());
    cues->select(0);
    REQUIRE_FALSE(cues->canDelete());
    REQUIRE_FALSE(cues->remove().isEmpty());

    REQUIRE(cues->insert(QStringLiteral("extra")).isEmpty());
    cues->select(1);
    REQUIRE(cues->canDelete());
    REQUIRE(cues->remove().isEmpty());
    REQUIRE(edit->slides()->list()->rowCount() >= 2);
    REQUIRE_FALSE(cues->canDelete());
    edit->cancel();
}

TEST_CASE("SessionFacade Freeform cue starts a section of its own", "[app][freeform][sections]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Repeat"));
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(id));
    auto* cues = edit->cues();
    REQUIRE(cues->setLineControl(true).isEmpty());
    cues->select(0);
    REQUIRE(cues->setVerse(QStringLiteral("Verse"), 3).isEmpty());
    REQUIRE(cues->insert(QStringLiteral("again")).isEmpty());

    cues->select(0);
    CHECK_FALSE(cues->current().value(QStringLiteral("can_start_section")).toBool());
    CHECK_FALSE(cues->setSectionStart(true).isEmpty());

    cues->select(1);
    CHECK(cues->current().value(QStringLiteral("can_start_section")).toBool());
    CHECK_FALSE(cues->current().value(QStringLiteral("section_start")).toBool());
    REQUIRE(cues->setSectionStart(true).isEmpty());
    CHECK(cues->current().value(QStringLiteral("section_start")).toBool());
    REQUIRE(cues->setSectionStart(false).isEmpty());
    CHECK_FALSE(cues->current().value(QStringLiteral("section_start")).toBool());
    edit->cancel();
}

TEST_CASE("SessionFacade Freeform controller labels use cue verse codes", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Verses"));
    REQUIRE(facade.slideEdit()->openFreeform(id));
    auto* cues = facade.slideEdit()->cues();
    REQUIRE(cues->setLineControl(true).isEmpty());
    cues->select(0);
    REQUIRE(cues->rename(QStringLiteral("A mighty fortress")).isEmpty());
    REQUIRE(cues->setVerse(QStringLiteral("Verse"), 1).isEmpty());
    REQUIRE(cues->insert(QStringLiteral("A bulwark never failing")).isEmpty());
    REQUIRE(cues->setVerse(QStringLiteral("Chorus"), 2).isEmpty());
    REQUIRE(facade.slideEdit()->save().isEmpty());
    REQUIRE(facade.live()->playFreeform(id));
    REQUIRE(facade.session().focused_stack().program.freeform().document.line_control);
    auto* map = facade.program()->lines();
    REQUIRE(map->rowCount() == 2);
    REQUIRE(map->data(map->index(0), seventhslide::app::LineMapModel::LabelRole).toString()
            == QStringLiteral("v1"));
    REQUIRE(map->data(map->index(0), seventhslide::app::LineMapModel::TextRole).toString()
            == QStringLiteral("A mighty fortress"));
    REQUIRE(map->data(map->index(1), seventhslide::app::LineMapModel::LabelRole).toString()
            == QStringLiteral("c2"));
    REQUIRE(map->data(map->index(1), seventhslide::app::LineMapModel::TextRole).toString()
            == QStringLiteral("A bulwark never failing"));
}

TEST_CASE("SessionFacade Freeform line-control uses line list", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    std::ifstream in(std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "freeform"
                     / "line_control_fortress.json");
    REQUIRE(in);
    nlohmann::json j;
    in >> j;
    using seventhslide::model::FreeformItem;
    FreeformItem item;
    item.name = "Fortress";
    item.document = j;
    const auto fid = facade.freeform_repo()->create_item(item);
    REQUIRE(facade.live()->playFreeform(static_cast<qint64>(fid)));
    REQUIRE_FALSE(facade.program()->freeformGallery());
    REQUIRE(facade.session().focused_stack().program.freeform().document.line_control);
    REQUIRE(facade.program()->totalLines() == 8);
    REQUIRE(facade.program()->lines()->rowCount() == 8);
    REQUIRE(facade.program()->jumpToLine(4));
    REQUIRE(facade.program()->lineCursor() == 4);
}

TEST_CASE("SessionFacade Freeform editor cue map and sorter", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    std::ifstream in(std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "freeform"
                     / "line_control_fortress.json");
    REQUIRE(in);
    nlohmann::json j;
    in >> j;
    using seventhslide::model::FreeformItem;
    FreeformItem item;
    item.name = "Fortress";
    item.document = j;
    const auto fid = facade.freeform_repo()->create_item(item);
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(static_cast<qint64>(fid)));
    auto* cues = edit->cues();
    auto* rows = edit->slides()->list();
    REQUIRE(cues->lineControl());
    REQUIRE_FALSE(cues->canDisableLineControl());
    REQUIRE(rows->rowCount() == 10);
    REQUIRE(rows->rowAt(0).value(QStringLiteral("kind")).toString() == QStringLiteral("cue"));
    REQUIRE(rows->rowAt(4).value(QStringLiteral("kind")).toString() == QStringLiteral("slide"));

    const auto& doc = edit->freeform_editor().document();
    REQUIRE(doc.line_control);
    REQUIRE(doc.cues.size() == 8);
    REQUIRE(doc.decks.size() == 2);
    REQUIRE(seventhslide::freeform::deck_spans(doc, doc.decks[0]).size() == 2);
    REQUIRE(seventhslide::freeform::deck_spans(doc, doc.decks[1]).size() == 4);

    REQUIRE_FALSE(cues->setLineControl(false).isEmpty());
    REQUIRE(cues->lineControl());
    REQUIRE(cues->setLineControl(false, true).isEmpty());
    REQUIRE_FALSE(cues->lineControl());
    REQUIRE(rows->rowCount() == 2);
    edit->cancel();
}

TEST_CASE("SessionFacade saveSongAsFreeform leaves the live song alone", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());
    const auto live_title = facade.live()->current_title();
    Song lib;
    lib.title = "Library Hymn";
    lib.lyrics = "[V1]\nBe thou my vision\nO Lord of my heart";
    lib.verse_order = "v1";
    const auto song_id = facade.song_repo()->insert(lib);
    const auto result = facade.saveSongAsFreeform(static_cast<qint64>(song_id),
                                                  QStringLiteral("Library Hymn (edit)"));
    REQUIRE(result.value(QStringLiteral("error")).toString().isEmpty());
    const auto id = result.value(QStringLiteral("id")).toLongLong();
    REQUIRE(id >= 0);
    REQUIRE(facade.live()->current_title() == live_title);
    REQUIRE(facade.live()->mode() == QStringLiteral("song"));
    REQUIRE_FALSE(facade.slideEdit()->active());
    REQUIRE(facade.slideEdit()->openFreeform(id));
    REQUIRE(facade.slideEdit()->cues()->lineControl());
    REQUIRE(facade.slideEdit()->name() == QStringLiteral("Library Hymn (edit)"));
    facade.slideEdit()->cancel();
}

TEST_CASE("A song saved as Freeform waits for a font still on its way", "[app][freeform][fonts]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    facade.set_outputs({make_main()});
    facade.fonts()->set_may_reload([] { return true; });

    // Its row has come from another computer; its file has not.
    seventhslide::app::FontStore store(*facade.open_library().database(), facade.media_root());
    const auto added = store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"));
    REQUIRE(added.faces.size() == 1);
    const auto path = "fonts/" + added.faces.front().filename;
    const auto file = facade.media_root() / path;
    const auto aside = dir.path / "aside.ttf";
    std::filesystem::rename(file, aside);
    facade.fonts()->library_fonts_changed();

    Song lib;
    lib.title = "Library Hymn";
    lib.lyrics = "[V1]\n<font=SSFixture Sans>Be thou my vision</font>\nO Lord of my heart";
    lib.verse_order = "v1";
    const auto song_id = static_cast<qint64>(facade.song_repo()->insert(lib));
    const auto items = [&] { return facade.freeform_repo()->list_items().size(); };
    const auto before = items();

    using seventhslide::app::FileArrival;
    facade.media()->set_arrivals({{path, {FileArrival::State::Downloading, 10, 100}}});
    const auto waiting = facade.saveSongAsFreeform(song_id, QStringLiteral("Hymn"));
    CHECK(waiting.value(QStringLiteral("id")).toLongLong() == -1);
    CHECK(waiting.value(QStringLiteral("error")).toString().contains(QStringLiteral("SSFixture Sans")));
    CHECK(waiting.value(QStringLiteral("error")).toString().contains(QStringLiteral("on its way")));
    CHECK(items() == before);
    // A computer that has it is not connected yet: still on its way.
    facade.media()->set_arrivals({{path, {FileArrival::State::Waiting, 0, 100}}});
    CHECK_FALSE(facade.saveSongAsFreeform(song_id, QStringLiteral("Hymn"))
                    .value(QStringLiteral("error")).toString().isEmpty());

    // On no computer connected: nothing to wait for, so it saves as before.
    facade.media()->set_arrivals({{path, {FileArrival::State::Nowhere, 0, 100}}});
    const auto lost = facade.saveSongAsFreeform(song_id, QStringLiteral("Hymn"));
    CHECK(lost.value(QStringLiteral("error")).toString().isEmpty());
    CHECK(items() == before + 1);

    // Here, before sync has looked again: saved, laid out in it.
    std::filesystem::rename(aside, file);
    facade.media()->set_arrivals({{path, {FileArrival::State::Downloading, 100, 100}}});
    const auto saved = facade.saveSongAsFreeform(song_id, QStringLiteral("Hymn"));
    CHECK(saved.value(QStringLiteral("error")).toString().isEmpty());
    CHECK(saved.value(QStringLiteral("id")).toLongLong() >= 0);
    CHECK(items() == before + 2);
}

TEST_CASE("A Bible passage saved as Freeform waits for its theme's font still on its way",
          "[app][freeform][fonts]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    facade.set_outputs({make_main()});
    facade.fonts()->set_may_reload([] { return true; });

    // Every text theme in a font whose row has come and whose file has not.
    seventhslide::app::FontStore store(*facade.open_library().database(), facade.media_root());
    const auto added = store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"));
    REQUIRE(added.faces.size() == 1);
    const auto path = "fonts/" + added.faces.front().filename;
    std::filesystem::remove(facade.media_root() / path);
    facade.fonts()->library_fonts_changed();
    const auto tid = facade.themes()
                         ->create(QStringLiteral("Main"), QStringLiteral("bible"), QStringLiteral("Fonty"))
                         .value(QStringLiteral("id"))
                         .toString();
    REQUIRE_FALSE(tid.isEmpty());
    REQUIRE(facade.themes()
                ->setDefault(QStringLiteral("Main"), QStringLiteral("bible"), QStringLiteral("theme"), tid)
                .isEmpty());
    auto outputs = facade.session().outputs();
    REQUIRE_FALSE(outputs.empty());
    REQUIRE_FALSE(outputs.front().bible_themes.empty());
    for (auto& theme : outputs.front().bible_themes) {
        std::vector<nlohmann::json*> open{&theme.style};
        while (!open.empty()) {
            auto* value = open.back();
            open.pop_back();
            if (value->is_object()) {
                for (auto& [key, item] : value->items()) {
                    if (key == "font_family") {
                        item = "SSFixture Sans";
                    } else {
                        open.push_back(&item);
                    }
                }
            } else if (value->is_array()) {
                for (auto& item : *value) {
                    open.push_back(&item);
                }
            }
        }
    }
    facade.set_outputs(outputs);

    using seventhslide::model::BibleVerse;
    std::vector<BibleVerse> verses{
        {.book = "John", .chapter = 3, .verse_num = 16, .text = "For God so loved the world"},
    };
    const auto bible_id = facade.bible_repo()->import_bible("KJV", "", verses);
    facade.refresh_library();
    REQUIRE(facade.library()->selectBible(static_cast<qint64>(bible_id)));
    REQUIRE(facade.library()->resolveBibleRef(QStringLiteral("John 3")).value(QStringLiteral("success")).toBool());

    using seventhslide::app::FileArrival;
    facade.media()->set_arrivals({{path, {FileArrival::State::Queued, 0, 100}}});
    const auto waiting = facade.saveBibleAsFreeform(16, 16, QStringLiteral("John 3:16"));
    CHECK(waiting.value(QStringLiteral("id")).toLongLong() == -1);
    CHECK(waiting.value(QStringLiteral("error")).toString().contains(QStringLiteral("SSFixture Sans")));

    facade.media()->set_arrivals({});
    CHECK(facade.saveBibleAsFreeform(16, 16, QStringLiteral("John 3:16"))
              .value(QStringLiteral("id")).toLongLong() >= 0);
}

// Switching an output off leaves it out of the show only. Building a Freeform
// document is editing, and lays the song out for every output, on or off.
TEST_CASE("An output switched off still gets its deck in a song saved as Freeform", "[app][freeform][outputs]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto song = demo_song();
    const auto song_id = facade.song_repo()->insert(song);

    const auto saved_deck_slides = [&facade, song_id] {
        const auto result = facade.saveSongAsFreeform(static_cast<qint64>(song_id),
                                                      QStringLiteral("Copy"));
        REQUIRE(result.value(QStringLiteral("error")).toString().isEmpty());
        const auto item = facade.freeform_repo()->get_item(
            result.value(QStringLiteral("id")).toLongLong());
        REQUIRE(item.has_value());
        const auto doc = seventhslide::freeform::load_document(item->document, "Copy");
        REQUIRE(doc.has_value());
        const auto* deck = seventhslide::freeform::find_deck(*doc, "out-stage");
        REQUIRE(deck != nullptr);
        std::vector<std::size_t> objects;
        for (const auto& slide : deck->slides) {
            objects.push_back(slide.objects.size());
        }
        return objects;
    };

    auto stage = make_main();
    stage.id = "out-stage";
    stage.name = "Stage";
    facade.set_outputs({make_main(), stage});
    const auto shown = saved_deck_slides();
    REQUIRE_FALSE(shown.empty());
    REQUIRE(facade.outputSettings()->setOutputOn(QStringLiteral("Stage"), false).isEmpty());
    REQUIRE_FALSE(facade.session().output_is_on("out-stage"));
    REQUIRE(saved_deck_slides() == shown);
}

TEST_CASE("SessionFacade saveBibleAsFreeform leaves the live item alone", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    facade.live()->play_song(demo_song());
    const auto live_title = facade.live()->current_title();

    using seventhslide::model::BibleVerse;
    std::vector<BibleVerse> verses{
        {.book = "John", .chapter = 3, .verse_num = 16, .text = "For God so loved the world"},
        {.book = "John", .chapter = 3, .verse_num = 17, .text = "For God sent not his Son"},
    };
    const auto bible_id = facade.bible_repo()->import_bible("KJV", "", verses);
    facade.refresh_library();
    REQUIRE(facade.library()->selectBible(static_cast<qint64>(bible_id)));
    REQUIRE(facade.library()->resolveBibleRef(QStringLiteral("John 3")).value(QStringLiteral("success")).toBool());

    const auto result = facade.saveBibleAsFreeform(16, 17, QStringLiteral("John 3:16-17 (edit)"));
    REQUIRE(result.value(QStringLiteral("error")).toString().isEmpty());
    const auto id = result.value(QStringLiteral("id")).toLongLong();
    REQUIRE(id >= 0);
    REQUIRE(facade.live()->current_title() == live_title);
    REQUIRE(facade.live()->mode() == QStringLiteral("song"));
    REQUIRE_FALSE(facade.slideEdit()->active());
    REQUIRE(facade.slideEdit()->openFreeform(id));
    REQUIRE(facade.slideEdit()->cues()->lineControl());
    REQUIRE(facade.slideEdit()->name() == QStringLiteral("John 3:16-17 (edit)"));
    REQUIRE_FALSE(facade.slideEdit()->freeform_editor().document().cues.empty());
    facade.slideEdit()->cancel();
}

TEST_CASE("A stored Freeform that cannot be read is refused, not overwritten", "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const nlohmann::json foreign = {{"slides", {"one", "two"}}};
    seventhslide::model::FreeformItem item;
    item.name = "Imported";
    item.document = foreign;
    const auto id = static_cast<qint64>(facade.freeform_repo()->create_item(item));

    REQUIRE_FALSE(facade.slideEdit()->openFreeform(id));
    REQUIRE_FALSE(facade.slideEdit()->active());
    REQUIRE_FALSE(facade.live()->playFreeform(id));
    REQUIRE(facade.freeform_repo()->get_item(id)->document == foreign);
}

TEST_CASE("The Freeform canvas leaves the room a linked box keeps between its lines",
          "[app][freeform]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto doc = seventhslide::freeform::make_document("Gaps", {facade.session().primary_output()->id});
    doc.line_control = true;
    doc.cues = {seventhslide::freeform::Cue{.id = "c1", .text = "Verse one"},
                seventhslide::freeform::Cue{.id = "c2", .text = "Verse two"}};
    auto& deck = doc.decks[0];
    deck.line_to_slide = {0, 0};
    deck.cue_flow = {{.id = "c2", .gap_above = 30}};
    seventhslide::freeform::Object box;
    box.type = seventhslide::freeform::ObjectType::Text;
    box.w = 1600;
    box.h = 900;
    box.cue_linked = true;
    deck.slides[0].objects.push_back(box);

    seventhslide::model::FreeformItem item;
    item.name = "Gaps";
    item.document = doc.to_json();
    const auto id = facade.freeform_repo()->create_item(item);
    REQUIRE(facade.slideEdit()->openFreeform(static_cast<qint64>(id)));

    const auto objects = facade.slideEdit()->objects()->canvasObjects();
    REQUIRE(objects.size() == 1);
    const auto gaps = objects.front().toMap().value(QStringLiteral("gaps_above")).toList();
    REQUIRE(gaps.size() == 2);
    CHECK(gaps[0].toInt() == 0);
    CHECK(gaps[1].toInt() == 30);
}

TEST_CASE("Slides picked in the slide editor copy and paste onto another output",
          "[app][freeform][paste]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    OutputConfig stage = make_main();
    stage.id = "out-stage";
    stage.name = "Stage";
    facade.set_outputs({make_main(), stage});
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Slides"));
    REQUIRE(id >= 0);
    auto* edit = facade.slideEdit();
    auto* slides = edit->slides();
    REQUIRE(edit->openFreeform(id));
    edit->setOutput(QStringLiteral("Main"));
    REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(slides->insert().isEmpty());
    REQUIRE(slides->count() == 2);

    slides->select(0);
    slides->extendTo(1);
    CHECK(slides->selectedCount() == 2);
    const auto picked = [&] {
        int n = 0;
        auto* list = slides->list();
        for (int r = 0; r < list->rowCount(); ++r) {
            n += list->rowAt(r).value(QStringLiteral("selected")).toBool() ? 1 : 0;
        }
        return n;
    };
    CHECK(picked() == 2);
    REQUIRE(slides->copy().isEmpty());

    edit->setOutput(QStringLiteral("Stage"));
    CHECK(slides->selectedCount() == 1);
    REQUIRE(edit->objects()->canPaste());
    REQUIRE(edit->objects()->paste().isEmpty());

    // Both outputs have the slides; Stage's are Main's, and they are picked.
    CHECK(slides->count() == 4);
    CHECK(slides->index() == 2);
    CHECK(slides->selectedCount() == 2);
    CHECK(picked() == 2);
    CHECK(edited_object_count(facade) == 1);
    edit->cancel();
}

TEST_CASE("The slide editor's canvas commands announce what they changed",
          "[app][freeform][clipboard]")
{
    using seventhslide::app::SlideEditSession;
    using seventhslide::app::SlideObjectsSession;
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* edit = facade.slideEdit();
    auto* objects = edit->objects();
    REQUIRE(edit->openFreeform(facade.freeformLibrary()->createFreeform(QStringLiteral("Said"))));
    REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());

    QSignalSpy object_docs(objects, &SlideObjectsSession::documentChanged);
    QSignalSpy picks(objects, &SlideObjectsSession::selectionChanged);
    QSignalSpy object_clips(objects, &SlideObjectsSession::clipboardChanged);
    QSignalSpy stage(objects, &SlideObjectsSession::canvasObjectsChanged);
    QSignalSpy docs(edit, &SlideEditSession::documentChanged);
    QSignalSpy edit_picks(edit, &SlideEditSession::selectionChanged);
    QSignalSpy clips(edit, &SlideEditSession::clipboardChanged);
    const auto clear = [&] {
        for (auto* spy : {&object_docs, &picks, &object_clips, &stage, &docs, &edit_picks, &clips}) {
            spy->clear();
        }
    };

    // Copying changes only what Paste would bring.
    REQUIRE(objects->copySelected().isEmpty());
    CHECK(object_clips.size() == 1);
    CHECK(clips.size() == 1);
    CHECK(object_docs.isEmpty());
    CHECK(docs.isEmpty());
    clear();

    // Pasting is an edit: the document, the selection, the stage.
    REQUIRE(objects->paste().isEmpty());
    CHECK(object_docs.size() == 1);
    CHECK(docs.size() == 1);
    CHECK(picks.size() == 1);
    CHECK(edit_picks.size() == 1);
    CHECK(stage.size() == 1);
    CHECK(object_clips.isEmpty());
    CHECK(objects->canvasObjects().size() == 2);
    clear();

    // Picking changes the selection alone.
    objects->clearSelection();
    CHECK(picks.size() == 1);
    CHECK(object_docs.isEmpty());
    clear();

    // A refused edit says why and announces nothing.
    CHECK_FALSE(objects->deleteSelected().isEmpty());
    CHECK(object_docs.isEmpty());
    CHECK(picks.isEmpty());

    // A gesture's end announces the document once more.
    objects->selectAll();
    clear();
    objects->beginGesture();
    REQUIRE(objects->nudge(5, 0).isEmpty());
    objects->endGesture();
    CHECK(object_docs.size() == 1);
    CHECK(docs.size() == 2);
    REQUIRE(edit->undo().isEmpty());
    CHECK(objects->canvasObjects().size() == 2);

    // Copying slides fills the same Paste.
    clear();
    REQUIRE(edit->slides()->copy().isEmpty());
    CHECK(object_clips.size() == 1);
    CHECK_FALSE(clips.isEmpty());
    CHECK(objects->canPaste());
    edit->cancel();
    CHECK_FALSE(objects->canPaste());
    CHECK(objects->selectedIds().isEmpty());
}
