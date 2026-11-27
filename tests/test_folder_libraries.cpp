// Every library tab has the same folder calls (FolderLibrary): one test body
// runs against all five (images, videos, audio, Freeform, web pages), each
// with rows of its own kind, keyed its own way.

#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "refusals.hpp"
#include "folder_library.hpp"
#include "media_library.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/web_page_repository.hpp>
#include <seventhslide/model/library.hpp>
#include <seventhslide/model/web_page.hpp>

#include <QFile>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <string>
#include <vector>

using seventhslide::app::FolderLibrary;
using seventhslide::app::MediaBrowserModel;
namespace MediaKindQml = seventhslide::app::MediaKindQml;
using seventhslide::app::SessionFacade;
using seventhslide::db::MediaKind;
namespace refusal = seventhslide::app::refusal;

namespace {

/// One library tab, and how to put an item of its own kind in it: the item
/// is called ``name`` in the browser, and the key returned is what its
/// library moves it by.
struct Tab {
    const char* name;
    FolderLibrary* library;
    std::function<QVariant(const std::string& name)> add;
};

std::vector<Tab> tabs(SessionFacade& facade)
{
    const auto media_tab = [&facade](const char* tab, MediaKindQml::Kind kind, MediaKind db_kind,
                                     std::string ext) {
        return Tab{tab, facade.media()->library(kind),
                   [&facade, db_kind, ext](const std::string& name) {
                       const auto filename = name + ext;
                       facade.media_repo()->upsert_file(db_kind, filename, name);
                       return QVariant(QString::fromStdString(filename));
                   }};
    };
    return {
        media_tab("images", MediaKindQml::Image, MediaKind::Image, ".png"),
        media_tab("videos", MediaKindQml::Video, MediaKind::Video, ".mp4"),
        media_tab("audio", MediaKindQml::Audio, MediaKind::Audio, ".mp3"),
        {"Freeform", facade.freeformLibrary(),
         [&facade](const std::string& name) {
             seventhslide::model::FreeformItem item;
             item.name = name;
             return QVariant(static_cast<qint64>(facade.freeform_repo()->create_item(item)));
         }},
        {"web pages", facade.webLibrary(),
         [&facade](const std::string& name) {
             seventhslide::model::WebPage page;
             page.name = name;
             page.url = "https://example.com/" + name;
             return QVariant(static_cast<qint64>(facade.webLibrary()->repo()->create_item(page)));
         }},
    };
}

/// What the browser lists, as ``folder:Name`` or ``file:Name``, in order.
QStringList listed(FolderLibrary& library)
{
    QStringList out;
    const MediaBrowserModel& model = *library.browser();
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row);
        const auto kind = model.data(index, MediaBrowserModel::KindRole).toString();
        if (kind != QStringLiteral("parent")) {
            out << kind + QLatin1Char(':') + model.data(index, MediaBrowserModel::NameRole).toString();
        }
    }
    return out;
}

}  // namespace

TEST_CASE("Every library tab files its rows in folders by the same calls", "[app][folders]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    for (auto& tab : tabs(facade)) {
        INFO("tab: " << tab.name);
        REQUIRE(tab.library != nullptr);
        auto& lib = *tab.library;
        const auto alpha = tab.add("Alpha");
        const auto bravo = tab.add("Bravo");

        // Folders: made where the browser is, renamed, listed.
        const auto hymns = lib.createFolder(QStringLiteral("Hymns"));
        const auto spare = lib.createFolder(QStringLiteral("Spare"));
        REQUIRE(hymns >= 0);
        CHECK(lib.renameFolder(spare, QStringLiteral("Seasonal")).isEmpty());
        CHECK(lib.renameFolder(spare, QStringLiteral("  ")) == refusal::name_required());
        CHECK(lib.listFoldersFlat().size() == 2);

        // Rows move in, all of them, and only into a folder there is.
        CHECK(lib.moveItems({bravo, alpha}, 9999) == refusal::folder_gone());
        CHECK(lib.moveItems({bravo, alpha}, hymns).isEmpty());
        lib.setFolder(hymns);
        CHECK(lib.folderId() == hymns);
        CHECK(lib.folderParentId() == -1);
        CHECK(listed(lib) == QStringList{"file:Bravo", "file:Alpha"});

        // In the order asked, by the browser's own ids.
        auto ids = lib.browser()->entryIds(QStringLiteral("file"));
        REQUIRE(ids.size() == 2);
        CHECK(lib.reorderItems(hymns, {ids[1], ids[0]}));
        CHECK(listed(lib) == QStringList{"file:Alpha", "file:Bravo"});

        // A search filters what the folder shows, and clears.
        lib.setSearch(QStringLiteral("brav"));
        CHECK(listed(lib) == QStringList{"file:Bravo"});
        lib.setSearch({});

        // Folders move and reorder; the one shown going takes the browser up.
        lib.folderUp();
        CHECK(lib.folderId() == -1);
        CHECK(lib.reorderFolders(-1, {spare, hymns}));
        CHECK(listed(lib).mid(0, 2) == QStringList{"folder:Seasonal", "folder:Hymns"});
        CHECK(lib.moveFolder(spare, hymns));
        CHECK_FALSE(lib.moveFolder(hymns, spare));  // not into itself
        lib.setFolder(spare);
        CHECK(lib.deleteFolder(hymns).isEmpty());
        CHECK(lib.folderId() == -1);
        CHECK(lib.listFoldersFlat().isEmpty());
        CHECK(lib.deleteFolder(-1) == refusal::folder_gone());
    }
}

TEST_CASE("MediaBrowser asks the library it shows, never the media controller's folders",
          "[app][folders][ui]")
{
    // Every tab's folder calls are its library's (FolderLibrary); a call
    // made on session.media by a media name would be a second way in.
    QFile qml(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR "/operator/MediaBrowser.qml"));
    REQUIRE(qml.open(QIODevice::ReadOnly | QIODevice::Text));
    const auto text = QString::fromUtf8(qml.readAll());
    for (const char* call : {"setMediaFolder", "mediaFolderUp", "mediaFolderParentId",
                             "createMediaFolder", "renameMediaFolder", "deleteMediaFolder",
                             "moveMediaFolder", "moveMediaFiles", "reorderMediaFolderItems",
                             "reorderMediaFolders", "listMediaFoldersFlat", "setMediaSearch",
                             "deleteMediaFile", "itemLibrary"}) {
        INFO(call);
        CHECK_FALSE(text.contains(QLatin1String(call)));
    }
}
