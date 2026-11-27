// Image and video libraries through the facade (MediaController).

#include "session_facade_fixture.hpp"
#include "refusals.hpp"
#include "test_temp_dir.hpp"

#include "media_thumb_cache.hpp"
#include "theme_controller.hpp"

#include <seventhslide/theme/theme_library.hpp>

#include <QSignalSpy>

#include <fstream>
#include <map>
#include <thread>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using namespace seventhslide::test::facade;
namespace refusal = seventhslide::app::refusal;

TEST_CASE("SessionFacade image folder add + nav", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Img svc")) > 0);

    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "a.png", "A");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "b.png", "B");
    const auto folder_id = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, "a.png");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, "b.png");
    facade.refresh_library();
    REQUIRE(facade.media_repo()->folders(MediaKind::Image).list().size() == 1);

    REQUIRE(facade.media()->addImageFolderToService(static_cast<qint64>(folder_id)));
    REQUIRE(facade.serviceItems()->rowCount() == 1);
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.program()->imageMode());
    REQUIRE(facade.program()->totalLines() == 2);
    REQUIRE(facade.program()->lineCursor() == 0);
    REQUIRE(facade.program()->lines()->rowCount() == 2);
    REQUIRE(facade.program()->nextSlide());
    REQUIRE(facade.program()->lineCursor() == 1);
    REQUIRE(facade.program()->jumpToLine(0));
    REQUIRE(facade.program()->lineCursor() == 0);

    const auto frames = facade.session().scene_frames();
    REQUIRE_FALSE(frames.empty());
    REQUIRE(frames[0].image.has_value());
    REQUIRE(frames[0].image->url == "/static/images/a.png");

    const auto folder_images =
        facade.serviceItems()
            ->data(facade.serviceItems()->index(0),
                   seventhslide::app::ServiceItemModel::FolderImagesRole)
            .toStringList();
    REQUIRE(folder_images.size() == 2);
    REQUIRE(folder_images[0] == QStringLiteral("a.png"));

    REQUIRE(facade.live()->selectServiceItemAtImage(0, 1));
    REQUIRE(facade.program()->lineCursor() == 1);
}

TEST_CASE("SessionFacade playImageFolder standalone", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "x.png", "X");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "y.png", "Y");
    const auto folder_id = facade.media_repo()->folders(MediaKind::Image).create("LivePack");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, "x.png");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, "y.png");
    facade.refresh_library();

    REQUIRE(facade.media()->playImageFolder(static_cast<qint64>(folder_id), 1));
    REQUIRE(facade.program()->imageMode());
    REQUIRE(facade.program()->totalLines() == 2);
    REQUIRE(facade.program()->lineCursor() == 1);
    REQUIRE(facade.live()->mode() == QStringLiteral("image"));
}

TEST_CASE("SessionFacade reorder media folder items and folders", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "a.png", "A");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "b.png", "B");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "c.png", "C");
    const auto folder_id = facade.media_repo()->folders(MediaKind::Image).create("Order");
    const auto id_a = facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, "a.png");
    const auto id_b = facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, "b.png");
    const auto id_c = facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, "c.png");

    QVariantList reversed;
    reversed << static_cast<qint64>(id_c) << static_cast<qint64>(id_b) << static_cast<qint64>(id_a);
    REQUIRE(facade.media()->library(MediaKindQml::Image)->reorderItems(static_cast<qint64>(folder_id),
                                           reversed));
    const auto items = facade.media_repo()->list_folder_items(MediaKind::Image, folder_id);
    REQUIRE(items.size() == 3);
    REQUIRE(items[0].filename == "c.png");
    REQUIRE(items[1].filename == "b.png");
    REQUIRE(items[2].filename == "a.png");

    const auto f1 = facade.media_repo()->folders(MediaKind::Image).create("F1");
    const auto f2 = facade.media_repo()->folders(MediaKind::Image).create("F2");
    QVariantList folder_order;
    folder_order << static_cast<qint64>(f2) << static_cast<qint64>(f1)
                 << static_cast<qint64>(folder_id);
    REQUIRE(facade.media()->library(MediaKindQml::Image)->reorderFolders(-1, folder_order));
    const auto children = facade.media_repo()->folders(MediaKind::Image).children(std::nullopt);
    REQUIRE(children.size() >= 3);
    REQUIRE(children[0].id.has_value());
    REQUIRE(*children[0].id == f2);
    REQUIRE(children[1].id.has_value());
    REQUIRE(*children[1].id == f1);
}

TEST_CASE("SessionFacade media folder browser navigate + CRUD", "[app][media-browser]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "loose.png", "Loose");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "nested.png", "Nested");
    const auto root_id = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    const auto child_id =
        facade.media_repo()->folders(MediaKind::Image).create("Inner", root_id);
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, child_id, "nested.png");
    facade.refresh_library();
    auto* images = facade.media()->browser(MediaKind::Image);

    // Root: loose file + Pack folder (nested.png not at root).
    REQUIRE(facade.media()->folder_id(seventhslide::db::MediaKind::Image) == -1);
    int folders = 0;
    int files = 0;
    int parents = 0;
    for (int i = 0; i < images->rowCount(); ++i) {
        const auto idx = images->index(i, 0);
        const auto kind = images->data(idx, MediaBrowserModel::KindRole).toString();
        if (kind == QLatin1String("folder"))
            ++folders;
        else if (kind == QLatin1String("file"))
            ++files;
        else if (kind == QLatin1String("parent"))
            ++parents;
    }
    REQUIRE(parents == 0);
    REQUIRE(folders == 1);
    REQUIRE(files == 1);

    facade.media()->library(MediaKindQml::Image)->setFolder(static_cast<qint64>(root_id));
    REQUIRE(facade.media()->folder_id(seventhslide::db::MediaKind::Image) == static_cast<qint64>(root_id));

    facade.media()->library(MediaKindQml::Image)->setFolder(static_cast<qint64>(child_id));
    REQUIRE(facade.media()->folder_id(seventhslide::db::MediaKind::Image) == static_cast<qint64>(child_id));
    // parent + Inner's file
    bool saw_file = false;
    bool saw_parent = false;
    for (int i = 0; i < images->rowCount(); ++i) {
        const auto idx = images->index(i, 0);
        const auto kind = images->data(idx, MediaBrowserModel::KindRole).toString();
        if (kind == QLatin1String("parent"))
            saw_parent = true;
        if (kind == QLatin1String("file")) {
            saw_file = true;
            REQUIRE(images->data(idx, MediaBrowserModel::FilenameRole).toString()
                    == QStringLiteral("nested.png"));
        }
    }
    REQUIRE(saw_parent);
    REQUIRE(saw_file);

    facade.media()->library(MediaKindQml::Image)->folderUp();
    REQUIRE(facade.media()->folder_id(seventhslide::db::MediaKind::Image) == static_cast<qint64>(root_id));

    const auto made = facade.media()->library(MediaKindQml::Image)->createFolder(QStringLiteral("Extra"));
    REQUIRE(made >= 0);
    REQUIRE(facade.media()->library(MediaKindQml::Image)->renameFolder(made, QStringLiteral("Renamed")).isEmpty());
    REQUIRE(facade.media()->library(MediaKindQml::Image)->moveFolder(made, -1));
    REQUIRE(facade.media()->library(MediaKindQml::Image)->moveItems(QVariantList{QStringLiteral("nested.png")},
                                  made)
                .isEmpty());
    facade.media()->library(MediaKindQml::Image)->setFolder(made);
    bool moved = false;
    for (int i = 0; i < images->rowCount(); ++i) {
        const auto idx = images->index(i, 0);
        if (images->data(idx, MediaBrowserModel::KindRole).toString()
            == QLatin1String("file")) {
            moved = images->data(idx, MediaBrowserModel::FilenameRole).toString()
                    == QStringLiteral("nested.png");
        }
    }
    REQUIRE(moved);

    facade.media()->library(MediaKindQml::Image)->setSearch(QStringLiteral("nest"));
    REQUIRE(images->rowCount() == 1);
    REQUIRE(images->data(images->index(0), MediaBrowserModel::KindRole).toString()
            == QLatin1String("file"));
    REQUIRE(images->data(images->index(0), MediaBrowserModel::FilenameRole).toString()
            == QStringLiteral("nested.png"));
    facade.media()->library(MediaKindQml::Image)->setSearch(QString());

    facade.media()->setMediaViewMode(QStringLiteral("icons"));
    REQUIRE(facade.media()->mediaViewMode() == QStringLiteral("icons"));
    facade.media()->setMediaViewMode(QStringLiteral("list"));
    REQUIRE(facade.media()->mediaViewMode() == QStringLiteral("list"));

    const auto renamed_img = facade.media()->renameMediaFile(MediaKindQml::Image,
                                                    QStringLiteral("nested.png"),
                                                    QStringLiteral("Deep Dive"));
    REQUIRE_FALSE(renamed_img.contains(QStringLiteral("error")));
    REQUIRE(renamed_img.value(QStringLiteral("filename")).toString()
            == QStringLiteral("nested.png"));
    REQUIRE(facade.media()->mediaFileDisplayName(MediaKindQml::Image, QStringLiteral("nested.png"))
            == QStringLiteral("Deep Dive.png"));

    REQUIRE(facade.media()->library(MediaKindQml::Image)->deleteFolder(made).isEmpty());
    REQUIRE(facade.media()->folder_id(seventhslide::db::MediaKind::Image) == -1);
}

TEST_CASE("SessionFacade video folder browser + addVideoFolderToService", "[app][media-browser]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Vid folders")) > 0);

    const seventhslide::test::TempDir tmp;
    const auto& root = tmp.path;
    std::filesystem::create_directories(root / "videos");
    {
        std::ofstream(root / "videos" / "a.mp4") << "x";
        std::ofstream(root / "videos" / "b.mp4") << "y";
        std::ofstream(root / "videos" / "loose.mp4") << "z";
    }
    facade.set_media_root(root);

    using seventhslide::db::MediaKind;
    using seventhslide::app::MediaBrowserModel;
    // Registered under the names they have.
    for (const auto* name : {"a.mp4", "b.mp4", "loose.mp4"}) {
        facade.media_repo()->upsert_file(MediaKind::Video, name, name);
    }
    // In the folder but never imported: not the library's.
    std::ofstream(root / "videos" / "dropped.mp4") << "d";
    const auto folder_id = facade.media_repo()->folders(MediaKind::Video).create("Clips");
    (void)facade.media_repo()->add_folder_item(MediaKind::Video, folder_id, "a.mp4");
    (void)facade.media_repo()->add_folder_item(MediaKind::Video, folder_id, "b.mp4");
    facade.refresh_library();

    auto* videos = facade.media()->browser(MediaKind::Video);
    facade.media()->library(MediaKindQml::Video)->setFolder(static_cast<qint64>(folder_id));
    int files = 0;
    for (int i = 0; i < videos->rowCount(); ++i) {
        if (videos->data(videos->index(i, 0), MediaBrowserModel::KindRole).toString()
            == QLatin1String("file")) {
            ++files;
        }
    }
    REQUIRE(files == 2);

    facade.media()->library(MediaKindQml::Video)->setFolder(-1);
    int loose = 0;
    for (int i = 0; i < videos->rowCount(); ++i) {
        if (videos->data(videos->index(i, 0), MediaBrowserModel::KindRole).toString()
            == QLatin1String("file")) {
            ++loose;
        }
    }
    REQUIRE(loose == 1);

    const auto renamed = facade.media()->renameMediaFile(MediaKindQml::Video,
                                                QStringLiteral("a.mp4"),
                                                QStringLiteral("alpha"));
    REQUIRE_FALSE(renamed.contains(QStringLiteral("error")));
    // Retitled; the file keeps its name.
    REQUIRE(renamed.value(QStringLiteral("filename")).toString() == QStringLiteral("a.mp4"));
    REQUIRE(facade.media()->mediaFileDisplayName(MediaKindQml::Video, QStringLiteral("a.mp4"))
            == QStringLiteral("alpha.mp4"));
    REQUIRE(std::filesystem::exists(root / "videos" / "a.mp4"));

    {
        std::ofstream(root / "videos" / "gone.mp4") << "g";
        facade.media_repo()->upsert_file(MediaKind::Video, "gone.mp4", "gone.mp4");
        const auto doomed = facade.media_repo()->folders(MediaKind::Video).create("Doomed");
        (void)facade.media_repo()->add_folder_item(MediaKind::Video, doomed, "gone.mp4");
        REQUIRE(facade.media()->library(MediaKindQml::Video)->deleteFolder(static_cast<qint64>(doomed))
                    .isEmpty());
        REQUIRE_FALSE(std::filesystem::exists(root / "videos" / "gone.mp4"));
        REQUIRE_FALSE(facade.media_repo()->get_file(MediaKind::Video, "gone.mp4"));
    }

    REQUIRE(facade.media()->addVideoFolderToService(static_cast<qint64>(folder_id)));
    REQUIRE(facade.serviceItems()->rowCount() == 2);
}

TEST_CASE("SessionFacade audio library: browse rename move reorder delete", "[app][media-browser][audio]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const seventhslide::test::TempDir tmp;
    const auto& root = tmp.path;
    const auto audio = root / "audio";
    std::filesystem::create_directories(audio);
    for (const auto* name : {"a.mp3", "b.wav", "c.flac", "loose.flac", "notes.txt"}) {
        std::ofstream(audio / name) << name;
    }
    facade.set_media_root(root);

    using seventhslide::db::MediaKind;
    // Registered under the names they have. notes.txt sits in the folder but
    // was never imported.
    for (const auto* name : {"a.mp3", "b.wav", "c.flac", "loose.flac"}) {
        facade.media_repo()->upsert_file(MediaKind::Audio, name, name);
    }
    auto& media = *facade.media();
    const auto kind = MediaKindQml::Audio;
    const auto hymns = media.library(kind)->createFolder(QStringLiteral("Hymns"));
    REQUIRE(hymns >= 0);
    REQUIRE(media.library(kind)->moveItems({QStringLiteral("a.mp3"), QStringLiteral("b.wav"),
                                        QStringLiteral("c.flac")},
                                 hymns)
                .isEmpty());

    /// The browser's rows as "kind:name", in order.
    const auto rows = [&] {
        QStringList out;
        auto* m = media.browser(MediaKind::Audio);
        for (int i = 0; i < m->rowCount(); ++i) {
            const auto ix = m->index(i, 0);
            out.push_back(m->data(ix, MediaBrowserModel::KindRole).toString() + QLatin1Char(':')
                          + m->data(ix, MediaBrowserModel::NameRole).toString());
        }
        return out;
    };
    /// The ids of the file rows, in order.
    const auto file_ids = [&] { return media.browser(MediaKind::Audio)->entryIds(QStringLiteral("file")); };

    // The root: the folder and the one unfiled file. notes.txt is not the library's.
    REQUIRE(media.audioFolderId() == -1);
    REQUIRE(rows() == QStringList{QStringLiteral("folder:Hymns"), QStringLiteral("file:loose.flac")});

    media.library(kind)->setFolder(hymns);
    REQUIRE(media.audioFolderId() == hymns);
    REQUIRE(rows()
            == QStringList{QStringLiteral("parent:.."), QStringLiteral("file:a.mp3"),
                           QStringLiteral("file:b.wav"), QStringLiteral("file:c.flac")});

    // Reorder: c first.
    auto ids = file_ids();
    REQUIRE(ids.size() == 3);
    REQUIRE(media.library(kind)->reorderItems(hymns, QVariantList{ids[2], ids[0], ids[1]}));
    REQUIRE(rows()
            == QStringList{QStringLiteral("parent:.."), QStringLiteral("file:c.flac"),
                           QStringLiteral("file:a.mp3"), QStringLiteral("file:b.wav")});

    // Retitle; the file keeps its name, and the folder keeps it in its place.
    // A typed extension is dropped. Two files may share a title.
    auto renamed = media.renameMediaFile(kind, QStringLiteral("a.mp3"), QStringLiteral("Alpha"));
    REQUIRE_FALSE(renamed.contains(QStringLiteral("error")));
    REQUIRE(renamed.value(QStringLiteral("filename")).toString() == QStringLiteral("a.mp3"));
    REQUIRE(std::filesystem::exists(audio / "a.mp3"));
    renamed = media.renameMediaFile(kind, QStringLiteral("b.wav"), QStringLiteral("Beta.wav"));
    REQUIRE(renamed.value(QStringLiteral("filename")).toString() == QStringLiteral("b.wav"));
    renamed = media.renameMediaFile(kind, QStringLiteral("c.flac"), QStringLiteral("loose"));
    REQUIRE(renamed.value(QStringLiteral("filename")).toString() == QStringLiteral("c.flac"));
    REQUIRE(rows()
            == QStringList{QStringLiteral("parent:.."), QStringLiteral("file:loose.flac"),
                           QStringLiteral("file:Alpha.mp3"), QStringLiteral("file:Beta.wav")});
    REQUIRE(media.mediaFileDisplayName(kind, QStringLiteral("a.mp3"))
            == QStringLiteral("Alpha.mp3"));
    REQUIRE(media.renameMediaFile(kind, QStringLiteral("missing.mp3"), QStringLiteral("x"))
                .value(QStringLiteral("error"))
                .toString()
            == refusal::media_file_gone());

    // Search inside the folder.
    media.library(kind)->setSearch(QStringLiteral("alp"));
    REQUIRE(rows() == QStringList{QStringLiteral("file:Alpha.mp3")});
    media.library(kind)->setSearch(QString());

    // A nested folder, then a file moved out to the root.
    const auto inner = media.library(kind)->createFolder(QStringLiteral("Inner"));
    REQUIRE(inner >= 0);
    REQUIRE(media.library(kind)->listFoldersFlat().size() == 2);
    REQUIRE(media.library(kind)->moveItems({QStringLiteral("b.wav")}, -1).isEmpty());
    media.library(kind)->folderUp();
    REQUIRE(rows().contains(QStringLiteral("file:Beta.wav")));

    // Delete one file: gone from disk and from the folder.
    REQUIRE(media.library(kind)->deleteItem(QStringLiteral("b.wav")).isEmpty());
    REQUIRE_FALSE(std::filesystem::exists(audio / "b.wav"));
    REQUIRE_FALSE(rows().contains(QStringLiteral("file:Beta.wav")));

    // Delete the folder: its files and its subfolder go too.
    REQUIRE(media.library(kind)->deleteFolder(hymns).isEmpty());
    REQUIRE_FALSE(std::filesystem::exists(audio / "a.mp3"));
    REQUIRE_FALSE(std::filesystem::exists(audio / "c.flac"));
    REQUIRE(std::filesystem::exists(audio / "loose.flac"));
    REQUIRE(media.library(kind)->listFoldersFlat().isEmpty());
    REQUIRE(rows() == QStringList{QStringLiteral("file:loose.flac")});

    // The file dialog's filter comes from the same extension list.
    REQUIRE(media.mediaFilePatterns(kind).split(QLatin1Char(' ')).contains(QStringLiteral("*.mp3")));
    REQUIRE(media.mediaFilePatterns(static_cast<MediaKindQml::Kind>(7)).isEmpty());
}

TEST_CASE("A video or an audio file a service plays is not deleted, and the refusal names its kind",
          "[app][media-browser]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(MediaKind::Video, "clip.mp4", "Clip");
    facade.media_repo()->upsert_file(MediaKind::Audio, "hymn.mp3", "Hymn");
    facade.refresh_library();
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("clip.mp4")));
    REQUIRE(facade.media()->addAudioToService(QStringLiteral("hymn.mp3")));

    auto* videos = facade.media()->library(MediaKindQml::Video);
    auto* sounds = facade.media()->library(MediaKindQml::Audio);
    CHECK(videos->deleteItem(QStringLiteral("clip.mp4"))
          == QStringLiteral("This video is used by 1 service item(s); remove it from those "
                            "services first."));
    CHECK(sounds->deleteItem(QStringLiteral("hymn.mp3"))
          == QStringLiteral("This audio file is used by 1 service item(s); remove it from those "
                            "services first."));

    const auto clips = videos->createFolder(QStringLiteral("Clips"));
    REQUIRE(clips >= 0);
    REQUIRE(videos->moveItems({QStringLiteral("clip.mp4")}, clips).isEmpty());
    CHECK(videos->deleteFolder(clips)
          == QStringLiteral("1 video(s) in this folder are used by service items; remove them "
                            "from those services first."));
    CHECK(facade.media_repo()->get_file(MediaKind::Video, "clip.mp4").has_value());
}

TEST_CASE("SessionFacade media search omits non-matching rows and parent", "[app][media-browser]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    using seventhslide::db::MediaKind;
    using seventhslide::app::MediaBrowserModel;
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "alpha.png", "Alpha");
    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "beta.png", "Beta Shot");
    const auto folder_id = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, folder_id, "beta.png");
    facade.refresh_library();

    auto kinds = [&](QString needle = {}) {
        if (!needle.isNull())
            facade.media()->library(MediaKindQml::Image)->setSearch(needle);
        QStringList out;
        auto* images = facade.media()->browser(MediaKind::Image);
        for (int i = 0; i < images->rowCount(); ++i) {
            const auto idx = images->index(i, 0);
            out.push_back(images->data(idx, MediaBrowserModel::KindRole).toString()
                          + QLatin1Char(':')
                          + images->data(idx, MediaBrowserModel::NameRole).toString());
        }
        return out;
    };

    REQUIRE(kinds(QStringLiteral(""))
            == QStringList{QStringLiteral("folder:Pack"), QStringLiteral("file:Alpha")});
    REQUIRE(kinds(QStringLiteral("alpha")) == QStringList{QStringLiteral("file:Alpha")});
    REQUIRE(kinds(QStringLiteral("pack")) == QStringList{QStringLiteral("folder:Pack")});
    REQUIRE(kinds(QStringLiteral("nope")).isEmpty());

    facade.media()->library(MediaKindQml::Image)->setFolder(static_cast<qint64>(folder_id));
    REQUIRE(kinds(QStringLiteral(""))
            == QStringList{QStringLiteral("parent:.."), QStringLiteral("file:Beta Shot")});
    REQUIRE(kinds(QStringLiteral("beta")) == QStringList{QStringLiteral("file:Beta Shot")});
    REQUIRE(kinds(QStringLiteral("alpha")).isEmpty());
    facade.media()->library(MediaKindQml::Image)->setSearch(QString());
}

TEST_CASE("Tearing down the app does not wait for a thumbnail backlog", "[app][media]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto root = std::filesystem::path(dir.path().toStdString());

    // Large enough that three thumbnail tiers per image take real time.
    constexpr int k_images = 24;
    QStringList sources;
    for (int i = 0; i < k_images; ++i) {
        QImage img(1600, 1600, QImage::Format_RGB32);
        img.fill(QColor::fromHsv((i * 15) % 360, 200, 200));
        const auto path = dir.filePath(QStringLiteral("src_%1.png").arg(i));
        REQUIRE(img.save(path));
        sources.push_back(path);
    }

    std::filesystem::path thumbs;
    {
        SessionFacade facade;
        REQUIRE(facade.open_database(root / "library.db"));
        thumbs = facade.media_root() / "thumbs";
        const auto result = seventhslide::test::wait_for_import(
            *facade.media(),
            facade.media()->import_files(seventhslide::db::MediaKind::Image, sources, -1));
        REQUIRE(result.value(QStringLiteral("count")).toInt() == k_images);
    }  // teardown: the queued batch is cancelled, the running job stops early

    int warmed = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(thumbs, ec)) {
        if (entry.is_regular_file() && entry.path().parent_path().filename() == "512") {
            ++warmed;
        }
    }
    INFO("images with a finished large thumbnail: " << warmed);
    REQUIRE(warmed < k_images);
}

TEST_CASE("A video row gets its thumbnail once its frame is made", "[app][media][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    SessionFacade facade;
    REQUIRE(facade.open_database(std::filesystem::path(dir.path().toStdString()) / "library.db"));

    using seventhslide::db::MediaKind;
    const auto videos = facade.media_root() / "videos";
    std::filesystem::create_directories(videos);
    std::filesystem::copy_file(std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video"
                                   / "solid_3366cc_64x36.mp4",
                               videos / "real.mp4");
    std::ofstream(videos / "junk.mp4") << "not a video";
    facade.media_repo()->upsert_file(MediaKind::Video, "real.mp4", "Real");
    facade.media_repo()->upsert_file(MediaKind::Video, "junk.mp4", "Junk");
    facade.refresh_library();

    auto* browser = facade.media()->browser(MediaKind::Video);
    const auto thumb_of = [browser](const QString& filename) {
        for (int row = 0; row < browser->rowCount(); ++row) {
            const auto idx = browser->index(row);
            if (browser->data(idx, MediaBrowserModel::FilenameRole).toString() == filename) {
                return browser->data(idx, MediaBrowserModel::ThumbUrlRole).toString();
            }
        }
        return QString();
    };

    // Made in the background; the listing picks it up when it is there.
    REQUIRE(QTest::qWaitFor([&] { return !thumb_of(QStringLiteral("real.mp4")).isEmpty(); },
                            30000));
    CHECK(thumb_of(QStringLiteral("real.mp4")).startsWith(QStringLiteral("image://mediathumb/videos/")));
    CHECK(facade.media()->mediaThumbUrl(MediaKindQml::Video, QStringLiteral("real.mp4"))
          == thumb_of(QStringLiteral("real.mp4")));
    // A file with no picture never gets one, so its row shows the glyph and
    // nothing asks the provider for a frame that is not there.
    CHECK(thumb_of(QStringLiteral("junk.mp4")).isEmpty());

    // Deleting the video takes its thumbs with it.
    const auto thumb = seventhslide::app::video_thumb_path(
        facade.media_root(), "real.mp4", seventhslide::app::k_media_thumb_tier_sm);
    REQUIRE(std::filesystem::exists(thumb));
    REQUIRE(facade.media()->library(MediaKindQml::Video)->deleteItem(QStringLiteral("real.mp4"))
                .isEmpty());
    CHECK_FALSE(std::filesystem::exists(thumb));
}

TEST_CASE("Media that arrives after its rows shows in them without reopening",
          "[app][media][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    SessionFacade facade;
    REQUIRE(facade.open_database(std::filesystem::path(dir.path().toStdString()) / "library.db"));

    // Named by the library, and not here yet.
    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(MediaKind::Video, "late.mp4", "Late");
    facade.media_repo()->upsert_file(MediaKind::Image, "late.png", "Late");
    facade.refresh_library();
    const auto thumb_of = [](MediaBrowserModel* browser, const QString& filename) {
        for (int row = 0; row < browser->rowCount(); ++row) {
            const auto idx = browser->index(row);
            if (browser->data(idx, MediaBrowserModel::FilenameRole).toString() == filename) {
                return browser->data(idx, MediaBrowserModel::ThumbUrlRole).toString();
            }
        }
        return QString();
    };
    auto* videos = facade.media()->browser(MediaKind::Video);
    auto* images = facade.media()->browser(MediaKind::Image);
    // The warmer has looked for the video and found nothing.
    QTest::qWait(500);
    REQUIRE(thumb_of(videos, QStringLiteral("late.mp4")).isEmpty());
    REQUIRE(thumb_of(images, QStringLiteral("late.png")).isEmpty());
    REQUIRE(facade.media()->mediaFileUrl(MediaKindQml::Image, QStringLiteral("late.png")).isEmpty());

    const auto root = facade.media_root();
    std::filesystem::create_directories(root / "videos");
    std::filesystem::create_directories(root / "images");
    std::filesystem::copy_file(std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR) / "video"
                                   / "solid_3366cc_64x36.mp4",
                               root / "videos" / "late.mp4");
    QImage picture(64, 36, QImage::Format_RGB32);
    picture.fill(Qt::red);
    REQUIRE(picture.save(QString::fromStdString((root / "images" / "late.png").string())));
    QSignalSpy arrived(facade.media(), &seventhslide::app::MediaController::filesArrived);
    const auto revision = facade.media()->filesRevision();
    facade.media_arrived({"videos/late.mp4", "images/late.png"});

    // Bindings calling for its URLs are told to ask again.
    CHECK(arrived.size() >= 1);
    CHECK(facade.media()->filesRevision() != revision);
    CHECK_FALSE(facade.media()->mediaFileUrl(MediaKindQml::Image, QStringLiteral("late.png")).isEmpty());
    CHECK(thumb_of(images, QStringLiteral("late.png")).startsWith(QStringLiteral("image://mediathumb/images/")));
    // The video's frame is made, as for one imported here.
    CHECK(QTest::qWaitFor([&] { return !thumb_of(videos, QStringLiteral("late.mp4")).isEmpty(); }, 30000));
}

TEST_CASE("The media view mode can be set from QML and is remembered", "[app][media]")
{
    seventhslide::test::ensure_qt_application();
    {
        SessionFacade first;
        // QML assigns the property; it must be writable.
        REQUIRE(first.media()->setProperty("mediaViewMode", QStringLiteral("icons")));
        REQUIRE(first.media()->mediaViewMode() == QStringLiteral("icons"));
    }
    SessionFacade next_run;
    REQUIRE(next_run.media()->mediaViewMode() == QStringLiteral("icons"));
    next_run.media()->setMediaViewMode(QStringLiteral("list"));
}

TEST_CASE("Media files resolve from any thread, once the folder exists", "[app][media]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto base = std::filesystem::path(dir.path().toStdString());
    const auto root = base / "media";

    SessionFacade facade;
    facade.set_media_root(root);
    const auto& library = facade.open_library();
    // Not created yet: nothing resolves, and nothing is remembered for later.
    REQUIRE(library.resolve_media_path("images", "a.png").empty());

    std::filesystem::create_directories(root / "images");
    std::ofstream(root / "images" / "a.png") << "x";
    std::ofstream(base / "outside.png") << "x";
    std::error_code ec;
    std::filesystem::create_symlink(base / "outside.png", root / "images" / "link.png", ec);
    const bool linked = !ec;

    std::filesystem::path found;
    std::filesystem::path escaped;
    std::thread reader([&] {
        found = library.resolve_media_path("images", "a.png");
        escaped = library.resolve_media_path("images", "link.png");
    });
    reader.join();
    REQUIRE(found == root / "images" / "a.png");
    if (linked) {
        // A link inside the folder that points out of it does not count.
        REQUIRE(escaped.empty());
    }
}

TEST_CASE("Media files resolve by name inside the media folder only", "[app][media]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto root = std::filesystem::path(dir.path().toStdString()) / "media";
    std::filesystem::create_directories(root / "videos");
    const auto video = root / "videos" / "Café Intro (1).mp4";
    std::ofstream(video) << "x";
    std::ofstream(std::filesystem::path(dir.path().toStdString()) / "outside.mp4") << "x";

    SessionFacade facade;
    facade.set_media_root(root);
    // Any name the file system accepts, accents and brackets included.
    REQUIRE(facade.open_library().resolve_media_path("videos", "Café Intro (1).mp4") == video);
    // Only the leaf counts: a stored name cannot reach outside the folder.
    REQUIRE(facade.open_library().resolve_media_path("videos", "../../outside.mp4").empty());
    REQUIRE(facade.open_library().resolve_media_path("videos", "missing.mp4").empty());
    REQUIRE(facade.open_library().resolve_media_path("videos", "..").empty());
}

TEST_CASE("Media added to the service keeps only its file name", "[app][media]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Names")) > 0);

    REQUIRE(facade.media()->addImageToService(QStringLiteral("../../slide.png")));
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("sub/clip.mp4")));
    REQUIRE(facade.media()->addAudioToService(QStringLiteral("/tmp/bed.mp3")));
    // A name that is no file name adds nothing.
    REQUIRE_FALSE(facade.media()->addImageToService(QStringLiteral("..")));
    REQUIRE_FALSE(facade.media()->addVideoToService(QStringLiteral(".")));

    const auto& items = facade.session().service_items();
    REQUIRE(items.size() == 3);
    CHECK(items[0].data.value("filename", "") == "slide.png");
    CHECK(items[1].data.value("filename", "") == "clip.mp4");
    CHECK(items[2].data.value("filename", "") == "bed.mp3");

    // Each row names its file and kind, for what it shows of a file not here.
    using seventhslide::app::ServiceItemModel;
    auto* rows = facade.serviceItems();
    REQUIRE(rows->rowCount() == 3);
    const auto at = [rows](int row, int role) { return rows->data(rows->index(row, 0), role); };
    CHECK(at(0, ServiceItemModel::MediaFileRole).toString() == QStringLiteral("slide.png"));
    CHECK(at(0, ServiceItemModel::MediaKindRole).toInt() == MediaKindQml::Image);
    CHECK(at(1, ServiceItemModel::MediaFileRole).toString() == QStringLiteral("clip.mp4"));
    CHECK(at(1, ServiceItemModel::MediaKindRole).toInt() == MediaKindQml::Video);
    CHECK(at(2, ServiceItemModel::MediaKindRole).toInt() == MediaKindQml::Audio);
}

TEST_CASE("A media file not here says what sync knows of it", "[app][media]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* media = facade.media();
    using seventhslide::app::FileArrival;
    QSignalSpy changed(media, &seventhslide::app::MediaController::arrivalsChanged);
    CHECK(media->arrival(MediaKindQml::Image, QStringLiteral("a.png")).isEmpty());

    const std::map<std::string, FileArrival> arrivals{
        {"images/a.png", {FileArrival::State::Downloading, 250, 1000}},
        {"images/b.png", {FileArrival::State::Queued, 0, 1000}},
        {"videos/c.mp4", {FileArrival::State::Waiting, 0, 0}},
        {"audio/d.mp3", {FileArrival::State::Nowhere, 0, 0}},
    };
    media->set_arrivals(arrivals);
    CHECK(changed.size() == 1);
    // Told again, unchanged: no binding is asked to look again.
    const auto revision = media->arrivalsRevision();
    media->set_arrivals(arrivals);
    CHECK(changed.size() == 1);
    CHECK(media->arrivalsRevision() == revision);

    const auto a = media->arrival(MediaKindQml::Image, QStringLiteral("a.png"));
    CHECK(a.value(QStringLiteral("state")).toString() == QStringLiteral("downloading"));
    CHECK(a.value(QStringLiteral("progress")).toDouble() == 0.25);
    CHECK(a.value(QStringLiteral("text")).toString() == QStringLiteral("Downloading 25%"));
    const auto b = media->arrival(MediaKindQml::Image, QStringLiteral("b.png"));
    CHECK(b.value(QStringLiteral("state")).toString() == QStringLiteral("queued"));
    CHECK(b.value(QStringLiteral("progress")).toDouble() < 0);
    CHECK(media->arrival(MediaKindQml::Video, QStringLiteral("c.mp4")).value(QStringLiteral("state"))
          == QStringLiteral("waiting"));
    const auto d = media->arrival(MediaKindQml::Audio, QStringLiteral("d.mp3"));
    CHECK(d.value(QStringLiteral("state")).toString() == QStringLiteral("missing"));
    CHECK(d.value(QStringLiteral("text")).toString().contains(QStringLiteral("every computer")));
    // By kind: a video of that name is another file.
    CHECK(media->arrival(MediaKindQml::Video, QStringLiteral("a.png")).isEmpty());

    // On its way to a computer connected, which has none to give yet.
    auto relayed = arrivals;
    relayed["images/e.png"] = FileArrival{.state = FileArrival::State::Elsewhere, .via = "Church server"};
    relayed["images/f.png"] = FileArrival{.state = FileArrival::State::Elsewhere};
    media->set_arrivals(relayed);
    const auto e = media->arrival(MediaKindQml::Image, QStringLiteral("e.png"));
    CHECK(e.value(QStringLiteral("state")).toString() == QStringLiteral("elsewhere"));
    CHECK(e.value(QStringLiteral("text")).toString() == QStringLiteral("On its way to Church server"));
    CHECK(media->arrival(MediaKindQml::Image, QStringLiteral("f.png")).value(QStringLiteral("text")).toString()
          == QStringLiteral("On its way to another computer"));
    CHECK(media->on_its_way("images/e.png"));
    media->set_arrivals(arrivals);
    changed.clear();

    // Come: nothing more to say of it.
    facade.media_arrived({"images/a.png"});
    CHECK(media->arrival(MediaKindQml::Image, QStringLiteral("a.png")).isEmpty());
    CHECK(changed.size() == 1);
    CHECK_FALSE(media->arrival(MediaKindQml::Image, QStringLiteral("b.png")).isEmpty());
}

TEST_CASE("Deleting media still on its way says it goes everywhere", "[app][media][sync]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    using seventhslide::db::MediaKind;
    for (const auto* name : {"a.png", "b.png", "c.png", "d.png"}) {
        facade.media_repo()->upsert_file(MediaKind::Image, name, name);
    }
    const auto pack = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    const auto inner = facade.media_repo()->folders(MediaKind::Image).create("Inner", pack);
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, inner, "c.png");
    facade.refresh_library();

    auto* media = facade.media();
    using seventhslide::app::FileArrival;
    const auto note = [&](const QStringList& files, const QVariantList& folders = {}) {
        return media->arrivingDeleteNote(MediaKindQml::Image, files, folders);
    };
    // Nothing coming: the confirm asks as it always has.
    CHECK(note({QStringLiteral("a.png")}).isEmpty());

    media->set_arrivals({
        {"images/a.png", {FileArrival::State::Downloading, 10, 100}},
        {"images/c.png", {FileArrival::State::Waiting, 0, 0}},
        {"images/d.png", {FileArrival::State::Nowhere, 0, 0}},
    });
    CHECK(note({QStringLiteral("a.png")}).contains(QStringLiteral("every computer")));
    CHECK(note({QStringLiteral("b.png")}).isEmpty());
    // Lost on every computer connected: deleting it loses nothing anywhere.
    CHECK(note({QStringLiteral("d.png")}).isEmpty());
    CHECK(note({QStringLiteral("a.png"), QStringLiteral("b.png"), QStringLiteral("c.png")})
              .startsWith(QStringLiteral("2 of them are")));
    CHECK(note({QStringLiteral("a.png"), QStringLiteral("b.png")}).startsWith(QStringLiteral("One of them is")));
    // A folder, however deep the file in it: "it" would be the folder.
    CHECK(note({}, {QVariant::fromValue(static_cast<qint64>(pack))}).startsWith(QStringLiteral("One of them is")));
    // By kind: a video of that name is another file.
    CHECK(media->arrivingDeleteNote(MediaKindQml::Video, {QStringLiteral("a.png")}, {}).isEmpty());
}

TEST_CASE("What is live says what of it is still on its way", "[app][media][sync]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    using seventhslide::db::MediaKind;
    facade.media_repo()->upsert_file(MediaKind::Image, "a.png", "A");
    facade.media_repo()->upsert_file(MediaKind::Image, "b.png", "B");
    const auto pack = facade.media_repo()->folders(MediaKind::Image).create("Pack");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, pack, "a.png");
    (void)facade.media_repo()->add_folder_item(MediaKind::Image, pack, "b.png");
    facade.refresh_library();
    REQUIRE(facade.library()->createService(QStringLiteral("Svc")) > 0);
    REQUIRE(facade.media()->addImageFolderToService(static_cast<qint64>(pack)));

    using seventhslide::app::FileArrival;
    auto* media = facade.media();
    QSignalSpy told(&facade, &SessionFacade::liveArrivalsChanged);
    media->set_arrivals({
        {"images/b.png", {FileArrival::State::Downloading, 50, 100}},
        {"images/elsewhere.png", {FileArrival::State::Queued, 0, 0}},
    });
    // Nothing live: nothing to say.
    CHECK(facade.liveArrivals().isEmpty());

    // Live, showing the first picture: the next is still coming.
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(QTest::qWaitFor([&] { return !facade.liveArrivals().isEmpty(); }, 2000));
    REQUIRE(facade.liveArrivals().size() == 1);
    auto b = facade.liveArrivals().front().toMap();
    CHECK(b.value(QStringLiteral("file")).toString() == QStringLiteral("b.png"));
    CHECK(b.value(QStringLiteral("text")).toString() == QStringLiteral("Downloading 50%"));

    // Paused for what is live: so is the file.
    media->set_sync_paused(true);
    b = facade.liveArrivals().front().toMap();
    CHECK(b.value(QStringLiteral("text")).toString() == QStringLiteral("Paused at 50%"));
    CHECK(media->arrival(MediaKindQml::Image, QStringLiteral("b.png"))
              .value(QStringLiteral("text")).toString() == QStringLiteral("Paused at 50%"));
    media->set_sync_paused(false);

    // Come: nothing more to say, and told once.
    const auto before = told.size();
    media->set_arrivals({{"images/elsewhere.png", {FileArrival::State::Queued, 0, 0}}});
    CHECK(facade.liveArrivals().isEmpty());
    CHECK(told.size() == before + 1);
    media->set_arrivals({});
    CHECK(told.size() == before + 1);

    // A Freeform item's later slides count, and a video's file.
    const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Card"));
    auto doc = facade.freeformLibrary()->load_freeform_document(id);
    REQUIRE(doc.has_value());
    REQUIRE_FALSE(doc->decks.empty());
    REQUIRE_FALSE(doc->decks.front().slides.empty());
    auto later = doc->decks.front().slides.front();
    later.id = "later";
    later.background.kind = seventhslide::freeform::SlideBackgroundKind::Image;
    later.background.image = "late.png";
    doc->decks.front().slides.push_back(later);
    REQUIRE(facade.freeformLibrary()->commit_freeform_document(id, *doc).isEmpty());
    media->set_arrivals({{"images/late.png", {FileArrival::State::Waiting, 0, 0}},
                         {"videos/clip.mp4", {FileArrival::State::Nowhere, 0, 0}}});
    REQUIRE(facade.live()->playFreeform(id));
    REQUIRE(QTest::qWaitFor([&] { return facade.liveArrivals().size() == 1; }, 2000));
    CHECK(facade.liveArrivals().front().toMap().value(QStringLiteral("file")).toString()
          == QStringLiteral("late.png"));
    REQUIRE(facade.live()->playVideo(QStringLiteral("clip.mp4")));
    REQUIRE(QTest::qWaitFor([&] {
        const auto list = facade.liveArrivals();
        return !list.isEmpty()
               && list.back().toMap().value(QStringLiteral("file")).toString() == QStringLiteral("clip.mp4");
    }, 2000));
    CHECK(facade.liveArrivals().back().toMap().value(QStringLiteral("state")).toString()
          == QStringLiteral("missing"));

    // A sound, which nothing shows.
    facade.stacks()->clearAllStacks();
    media->set_arrivals({{"audio/hymn.mp3", {FileArrival::State::Queued, 0, 0}}});
    REQUIRE(facade.live()->playAudio(QStringLiteral("hymn.mp3")));
    REQUIRE(QTest::qWaitFor([&] { return facade.liveArrivals().size() == 1; }, 2000));
    CHECK(facade.liveArrivals().front().toMap().value(QStringLiteral("file")).toString()
          == QStringLiteral("hymn.mp3"));

    // A song's theme picture, which only what the outputs show names.
    facade.stacks()->clearAllStacks();
    auto* themes = facade.themes();
    const auto bg = themes->create(QStringLiteral("Main"), QStringLiteral("bg"), QStringLiteral("Bg"))
                        .value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(bg.isEmpty());
    const auto picture = seventhslide::theme::new_owned_asset_name(bg.toStdString(), "bg", ".png");
    auto style = themes->theme_map(QStringLiteral("Main"), QStringLiteral("bg"), bg).value(QStringLiteral("style")).toMap();
    style.insert(QStringLiteral("background_image"),
                 QString::fromStdString("/static/theme_backgrounds/" + picture));
    style.insert(QStringLiteral("background_type"), QStringLiteral("image"));
    REQUIRE(themes->update_theme(QStringLiteral("Main"), QStringLiteral("bg"), bg, QStringLiteral("Bg"), seventhslide::app::to_json(QVariant(style)), {}, false)
                .isEmpty());
    REQUIRE(themes->setDefault(QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("bg"), bg).isEmpty());
    media->set_arrivals({{"theme_backgrounds/" + picture, {FileArrival::State::Queued, 0, 0}}});
    auto song = demo_song();
    REQUIRE(facade.live()->playSong(static_cast<qint64>(facade.song_repo()->insert(song))));
    REQUIRE(QTest::qWaitFor([&] { return facade.liveArrivals().size() == 1; }, 2000));
    CHECK(facade.liveArrivals().front().toMap().value(QStringLiteral("file")).toString()
          == QString::fromStdString(picture));

    facade.stacks()->clearAllStacks();
    REQUIRE(QTest::qWaitFor([&] { return facade.liveArrivals().isEmpty(); }, 2000));
}
