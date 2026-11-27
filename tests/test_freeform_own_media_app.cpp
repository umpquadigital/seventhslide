// A Freeform item's pictures and videos are its own, never the library's:
// brought in from the editor, kept while a document names them, and gone once
// none does.

#include "session_facade_fixture.hpp"

#include "freeform_library_controller.hpp"
#include "media_controller.hpp"
#include "media_import_helpers.hpp"
#include "slide_edit_session.hpp"
#include "test_temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/freeform/own_media.hpp>
#include <seventhslide/model/library.hpp>

#include <QImage>

#include <filesystem>
#include <string>

using namespace seventhslide::test::facade;
namespace MediaKindQml = seventhslide::app::MediaKindQml;
using seventhslide::db::MediaKind;
namespace ff = seventhslide::freeform;
namespace fs = std::filesystem;

namespace {

/// A picture on the disk outside the library, as an operator picks one.
[[nodiscard]] fs::path picture_on_disk(const fs::path& dir, const std::string& name, Qt::GlobalColor colour)
{
    fs::create_directories(dir);
    QImage image(32, 18, QImage::Format_RGB32);
    image.fill(colour);
    const auto path = dir / name;
    REQUIRE(image.save(QString::fromStdString(path.string())));
    return path;
}

/// Bring ``path`` into the open item from the editor; the URL it is named by.
[[nodiscard]] std::string import_into_open_item(SessionFacade& facade, const fs::path& path)
{
    const auto id = facade.slideEdit()->importMedia(false, QString::fromStdString(path.string()));
    const auto result = seventhslide::test::wait_for_import(*facade.media(), id);
    INFO(result.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const auto names = result.value(QStringLiteral("filenames")).toStringList();
    REQUIRE(names.size() == 1);
    return names.front().toStdString();
}

[[nodiscard]] std::string uid_of(SessionFacade& facade, qint64 id)
{
    const auto item = facade.freeform_repo()->get_item(id);
    REQUIRE(item);
    REQUIRE_FALSE(item->uid.empty());
    return item->uid;
}

void name_picture(SessionFacade& facade, const std::string& url)
{
    auto* edit = facade.slideEdit();
    REQUIRE(edit->slides()->setBackground(QStringLiteral("image"), QString::fromStdString(url)).isEmpty());
}

}  // namespace

TEST_CASE("A picture brought into a Freeform item is its own, not the library's", "[app][freeform][own-media]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    fs::create_directories(tmp.path / "media");
    facade.set_media_root(tmp.path / "media");
    const auto picked = picture_on_disk(tmp.path / "Pictures", "Sunrise over the lake.png", Qt::red);

    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Welcome"));
    REQUIRE(id >= 0);
    const auto owner = uid_of(facade, id);
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(id));
    const auto url = import_into_open_item(facade, picked);
    const auto own = ff::own_media_of(url);
    REQUIRE(own);
    CHECK(own->owner == owner);
    CHECK(fs::exists(ff::own_media_path(facade.media_root(), *own)));

    // The library has nothing new: no file, no row.
    CHECK(facade.media_repo()->list_files(MediaKind::Image, false).empty());
    CHECK_FALSE(fs::exists(facade.media_root() / "images" / own->name));
    // The editor calls it what it was, and shows it.
    const auto qurl = QString::fromStdString(url);
    CHECK(facade.media()->mediaFileDisplayName(MediaKindQml::Image, qurl)
          == QStringLiteral("Sunrise over the lake.png"));
    CHECK_FALSE(facade.media()->mediaFileUrl(MediaKindQml::Image, qurl).isEmpty());
    CHECK(facade.media()->mediaThumbUrl(MediaKindQml::Image, qurl).startsWith(QStringLiteral("image://mediathumb/")));
    CHECK(facade.media()->resolve_media_path(QStringLiteral("images"), qurl)
          == ff::own_media_path(facade.media_root(), *own));

    // Saved, it stays with the item.
    name_picture(facade, url);
    REQUIRE(edit->save().isEmpty());
    const auto saved = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(saved);
    CHECK(saved->decks[0].slides[0].background.image == url);
    CHECK(fs::exists(ff::own_media_path(facade.media_root(), *own)));
}

TEST_CASE("A picture brought in and not kept goes when the editor closes", "[app][freeform][own-media]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    fs::create_directories(tmp.path / "media");
    facade.set_media_root(tmp.path / "media");
    const auto picked = picture_on_disk(tmp.path / "Pictures", "Unused.png", Qt::blue);

    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Draft"));
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(id));
    const auto own = ff::own_media_of(import_into_open_item(facade, picked));
    REQUIRE(own);
    const auto path = ff::own_media_path(facade.media_root(), *own);
    REQUIRE(fs::exists(path));
    edit->cancel();
    CHECK_FALSE(fs::exists(path));
    // The picture it came from is the operator's still.
    CHECK(fs::exists(picked));
}

TEST_CASE("An item's own files go with it, but for those a service's copy shows", "[app][freeform][own-media]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    fs::create_directories(tmp.path / "media");
    facade.set_media_root(tmp.path / "media");

    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Notices"));
    auto* edit = facade.slideEdit();
    REQUIRE(edit->openFreeform(id));
    const auto url = import_into_open_item(facade, picture_on_disk(tmp.path / "in", "Notice.png", Qt::green));
    name_picture(facade, url);
    REQUIRE(edit->save().isEmpty());
    const auto own = ff::own_media_of(url);
    REQUIRE(own);

    // A copy of it is its own, with its own files.
    const auto copy = facade.freeformLibrary()->duplicateItem(id);
    REQUIRE(copy >= 0);
    const auto copied = facade.freeformLibrary()->load_freeform_document(copy);
    REQUIRE(copied);
    const auto copy_own = ff::own_media_of(copied->decks[0].slides[0].background.image);
    REQUIRE(copy_own);
    CHECK(copy_own->owner == uid_of(facade, copy));
    CHECK(copy_own->owner != own->owner);
    CHECK(fs::exists(ff::own_media_path(facade.media_root(), *copy_own)));

    // A service's copy of the item names the item's file.
    const auto service = facade.library()->createService(QStringLiteral("Sunday"));
    REQUIRE(service > 0);
    const auto item = facade.freeform_repo()->get_item(id);
    REQUIRE(item);
    const auto row = facade.service_repo()->add_item(service, seventhslide::model::k_item_type_freeform,
                                                     seventhslide::model::freeform_item_data(*item));

    REQUIRE(facade.freeformLibrary()->deleteItem(id).isEmpty());
    CHECK(fs::exists(ff::own_media_path(facade.media_root(), *own)));
    // The row gone too, the next opening of the library takes the file.
    REQUIRE(facade.service_repo()->remove_item(row));
    facade.freeformLibrary()->prune_own_media();
    CHECK_FALSE(fs::exists(ff::own_media_path(facade.media_root(), *own)));
    // The copy keeps its own.
    CHECK(fs::exists(ff::own_media_path(facade.media_root(), *copy_own)));
    REQUIRE(facade.freeformLibrary()->deleteItem(copy).isEmpty());
    CHECK_FALSE(fs::exists(ff::own_media_dir(facade.media_root(), copy_own->owner)));
}

TEST_CASE("A library picture an item names is copied in as its own when it is saved", "[app][freeform][own-media]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    fs::create_directories(tmp.path / "media");
    facade.set_media_root(tmp.path / "media");
    const auto media_dir = facade.media_root();
    (void)picture_on_disk(media_dir / "images", "ab12.png", Qt::yellow);
    facade.media_repo()->upsert_file(MediaKind::Image, "ab12.png", "Lilies.png");

    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Easter"));
    auto doc = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(doc);
    auto& background = doc->decks[0].slides[0].background;
    background.kind = ff::SlideBackgroundKind::Image;
    background.image = "ab12.png";
    REQUIRE(facade.freeformLibrary()->commit_freeform_document(id, *doc).isEmpty());

    const auto saved = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(saved);
    const auto own = ff::own_media_of(saved->decks[0].slides[0].background.image);
    REQUIRE(own);
    CHECK(own->owner == uid_of(facade, id));
    CHECK(ff::own_media_title(own->name) == "Lilies.png");
    CHECK(fs::exists(ff::own_media_path(media_dir, *own)));
    // The library's picture is the library's still, and only it lists it.
    CHECK(fs::exists(media_dir / "images" / "ab12.png"));
    CHECK(facade.media_repo()->list_files(MediaKind::Image, false).size() == 1);
}

TEST_CASE("A live item shows its own files, and keeps them while it is live", "[app][freeform][own-media]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp;
    fs::create_directories(tmp.path / "media");
    facade.set_media_root(tmp.path / "media");
    const auto media_dir = facade.media_root();
    (void)picture_on_disk(media_dir / "images", "cd34.png", Qt::cyan);
    facade.media_repo()->upsert_file(MediaKind::Image, "cd34.png", "Banner.png");

    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Live"));
    REQUIRE(facade.live()->playFreeform(id));
    auto doc = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(doc);
    auto& background = doc->decks[0].slides[0].background;
    background.kind = ff::SlideBackgroundKind::Image;
    background.image = "cd34.png";
    REQUIRE(facade.freeformLibrary()->commit_freeform_document(id, *doc).isEmpty());

    // The outputs show what was stored: the item's own copy, not the library's.
    const auto& live = facade.session().focused_stack().program.freeform();
    REQUIRE(live.library_id == id);
    const auto own = ff::own_media_of(live.document.decks[0].slides[0].background.image);
    REQUIRE(own);
    CHECK(own->owner == uid_of(facade, id));

    // Deleted while it is live, its files stay until it leaves the screen.
    REQUIRE(facade.freeformLibrary()->deleteItem(id).isEmpty());
    CHECK(fs::exists(ff::own_media_path(media_dir, *own)));
    facade.stacks()->clearAllStacks();
    facade.freeformLibrary()->prune_own_media();
    CHECK_FALSE(fs::exists(ff::own_media_path(media_dir, *own)));
}
