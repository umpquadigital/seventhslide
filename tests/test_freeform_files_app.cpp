// Freeform files and service packages end to end: exported from one library
// by session.exporter and imported into another by session.freeformImport,
// whose outputs have other ids and sizes.

#include "font_fixtures.hpp"
#include "pixel_compare.hpp"
#include "session_facade_fixture.hpp"
#include "test_temp_dir.hpp"
#include "theme_test_helpers.hpp"

#include "export_controller.hpp"
#include "font_controller.hpp"
#include "font_store.hpp"
#include "freeform_import_controller.hpp"
#include "freeform_library_controller.hpp"
#include "preview_resources.hpp"

#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/exchange/bundle_io.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/session/still_frames.hpp>
#include <seventhslide/model/live_source.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/model/web_page.hpp>
#include <seventhslide/render/scene_compositor.hpp>

#include <QBuffer>
#include <QImage>
#include <QTest>
#include <QUrl>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

using seventhslide::app::ExportController;
using seventhslide::app::FreeformImportController;
using seventhslide::app::SessionFacade;
using seventhslide::db::MediaKind;
namespace ff = seventhslide::freeform;
namespace model = seventhslide::model;

namespace {

[[nodiscard]] QString url_of(const fs::path& path)
{
    return QUrl::fromLocalFile(QString::fromStdString(path.string())).toString();
}

[[nodiscard]] QVariantMap wait_for_export(ExportController& exporter, qint64 id)
{
    QVariantMap result;
    bool done = false;
    const auto c = QObject::connect(&exporter, &ExportController::exportFinished,
                                    [&](qint64 request, const QVariantMap& r) {
                                        if (request == id) {
                                            result = r;
                                            done = true;
                                        }
                                    });
    (void)QTest::qWaitFor([&] { return done; }, 60000);
    QObject::disconnect(c);
    REQUIRE(done);
    return result;
}

[[nodiscard]] QVariantMap wait_for_read(FreeformImportController& import, qint64 id)
{
    QVariantMap result;
    bool done = false;
    const auto c = QObject::connect(&import, &FreeformImportController::fileRead,
                                    [&](qint64 request, const QVariantMap& r) {
                                        if (request == id) {
                                            result = r;
                                            done = true;
                                        }
                                    });
    (void)QTest::qWaitFor([&] { return done; }, 60000);
    QObject::disconnect(c);
    REQUIRE(done);
    return result;
}

[[nodiscard]] QVariantMap wait_for_import(FreeformImportController& import, qint64 id)
{
    QVariantMap result;
    bool done = false;
    const auto c = QObject::connect(&import, &FreeformImportController::importFinished,
                                    [&](qint64 request, const QVariantMap& r) {
                                        if (request == id) {
                                            result = r;
                                            done = true;
                                        }
                                    });
    (void)QTest::qWaitFor([&] { return done; }, 60000);
    QObject::disconnect(c);
    REQUIRE(done);
    return result;
}

[[nodiscard]] std::string png_bytes(QColor colour)
{
    QImage image(16, 9, QImage::Format_RGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    REQUIRE(image.save(&buffer, "PNG"));
    return bytes.toStdString();
}

[[nodiscard]] std::string read_bytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    REQUIRE(out);
}

[[nodiscard]] model::OutputConfig output(SessionFacade& facade, std::string id, std::string name, int w, int h)
{
    auto oc = facade.session().outputs().front();
    oc.id = std::move(id);
    oc.name = std::move(name);
    oc.canvas_width = w;
    oc.canvas_height = h;
    return oc;
}

/// A document with a picture and a line of text in ``family`` on each output.
[[nodiscard]] ff::Document sample_document(std::string name, const std::vector<std::string>& outputs,
                                           const std::string& picture, std::string_view family)
{
    auto doc = ff::make_document(std::move(name), outputs);
    for (auto& deck : doc.decks) {
        ff::Object image;
        image.type = ff::ObjectType::Image;
        image.filename = picture;
        image.x = 100;
        image.y = 50;
        image.w = 400;
        image.h = 225;
        ff::Object text;
        text.type = ff::ObjectType::Text;
        text.text = "Welcome";
        text.font_family = std::string(family);
        deck.slides.front().objects = {image, text};
    }
    return doc;
}

}  // namespace

TEST_CASE("A Freeform file goes from one library to another with its folders, files and fonts",
          "[freeform-files][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir from_root;
    const seventhslide::test::TempDir to_root;
    const auto picture = png_bytes(Qt::red);

    // The library it leaves: a 16:9 output and a 4:3 one.
    SessionFacade from;
    REQUIRE(from.open_memory_database());
    from.set_media_root(from_root.path);
    from.set_outputs({output(from, "src-main", "Main", 1920, 1080), output(from, "src-stage", "Stage", 1024, 768)});
    write_file(from_root.path / "images" / "aaa.png", picture);
    from.media_repo()->upsert_file(MediaKind::Image, "aaa.png", "Sunrise");
    {
        seventhslide::app::FontStore fonts(*from.open_library().database(), from_root.path);
        REQUIRE(fonts.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf")).error.empty());
        REQUIRE(fonts.import_file(seventhslide::test::fixture_font("SSFixtureSans-Bold.ttf")).error.empty());
    }
    auto* repo = from.freeformLibrary()->repo();
    const auto easter = repo->folders().create("Easter");
    model::FreeformItem welcome;
    welcome.name = "Welcome";
    welcome.folder_id = easter;
    welcome.tags = {"easter"};
    welcome.document = sample_document("Welcome", {"src-main", "src-stage"}, "aaa.png",
                                       seventhslide::test::k_fixture_sans)
                           .to_json();
    (void)repo->create_item(welcome);
    model::FreeformItem notices;
    notices.name = "Notices";
    notices.document =
        sample_document("Notices", {"src-main", "src-stage"}, "aaa.png", seventhslide::test::k_fixture_sans).to_json();
    const auto notices_id = repo->create_item(notices);

    auto& exporter = *from.exporter();
    const auto exported = wait_for_export(
        exporter, exporter.exportFreeformFile(QVariantList{QVariant::fromValue<qint64>(notices_id)},
                                              QVariantList{QVariant::fromValue<qint64>(easter)},
                                              url_of(out.path / "Sunday things")));
    INFO(exported.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString());
    REQUIRE(exported.value(QStringLiteral("ok")).toBool());
    CHECK(exported.value(QStringLiteral("count")).toInt() == 2);
    const auto file = out.path / "Sunday things.ssfb";
    REQUIRE(fs::exists(file));
    for (const auto& entry : fs::directory_iterator(out.path)) {
        CHECK(entry.path().filename().string().front() != '.');  // no staging left
    }
    {
        std::string refusal;
        const auto opened = seventhslide::exchange::open_freeform_file(file, refusal);
        REQUIRE(opened);
        CHECK(opened->manifest.media.size() == 1);  // one picture, named by both
        CHECK(opened->manifest.fonts.size() == 2);
        CHECK(opened->manifest.folders.size() == 1);
        CHECK_FALSE(seventhslide::exchange::read_preview(*opened, 0).empty());
    }

    // The library it comes into: other ids, a 4K output and a laptop's, and
    // the picture already, under a name of its own.
    SessionFacade to;
    REQUIRE(to.open_memory_database());
    to.set_media_root(to_root.path);
    to.set_outputs({output(to, "dst-uhd", "Projector", 3840, 2160), output(to, "dst-laptop", "Laptop", 1366, 768)});
    write_file(to_root.path / "images" / "zzz.png", picture);
    to.media_repo()->upsert_file(MediaKind::Image, "zzz.png", "Old sunrise");

    auto& import = *to.freeformImport();
    const auto summary = wait_for_read(import, import.readFile(url_of(file)));
    INFO(summary.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE_FALSE(summary.contains(QStringLiteral("error")));
    CHECK(summary.value(QStringLiteral("kind")).toString() == QStringLiteral("bundle"));
    const auto items = summary.value(QStringLiteral("items")).toList();
    REQUIRE(items.size() == 2);
    const auto outputs = summary.value(QStringLiteral("outputs")).toList();
    REQUIRE(outputs.size() == 2);
    const auto main = outputs[0].toMap();
    CHECK(main.value(QStringLiteral("shape")).toString() == QStringLiteral("16:9"));
    REQUIRE(main.value(QStringLiteral("choices")).toList().size() == 1);  // not the laptop's 683:384
    CHECK(main.value(QStringLiteral("suggested")).toString() == QStringLiteral("dst-uhd"));
    CHECK(outputs[1].toMap().value(QStringLiteral("choices")).toList().isEmpty());
    CHECK(outputs[1].toMap().value(QStringLiteral("suggested")).toString().isEmpty());

    // Two outputs of the file cannot share one here.
    const auto twice = wait_for_import(
        import, import.importRead({{QStringLiteral("items"), QVariantList{0, 1}},
                                   {QStringLiteral("mapping"), QStringList{"dst-uhd", "dst-uhd"}}}));
    CHECK_FALSE(twice.value(QStringLiteral("ok")).toBool());

    const auto result = wait_for_import(
        import, import.importRead({{QStringLiteral("items"), QVariantList{0, 1}},
                                   {QStringLiteral("mapping"), QStringList{"dst-uhd", ""}},
                                   {QStringLiteral("folderName"), QStringLiteral("Sunday things")}}));
    INFO(result.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("items")).toInt() == 2);

    // The folders: the one named, with the file's beneath it.
    auto* to_repo = to.freeformLibrary()->repo();
    const auto tree = to_repo->folders();
    const auto top = tree.children(std::nullopt);
    REQUIRE(top.size() == 1);
    CHECK(top[0].name == "Sunday things");
    const auto inner = tree.children(top[0].id);
    REQUIRE(inner.size() == 1);
    CHECK(inner[0].name == "Easter");
    const auto in_easter = to_repo->list_items_in_folder(inner[0].id);
    REQUIRE(in_easter.size() == 1);
    CHECK(in_easter[0].name == "Welcome");
    CHECK(in_easter[0].tags == std::vector<std::string>{"easter"});

    // The document: on the 4K output alone, scaled, naming the library's own copy of the picture.
    const auto doc = ff::load_document(in_easter[0].document, "Welcome");
    REQUIRE(doc);
    REQUIRE(doc->decks.size() == 1);
    CHECK(doc->decks[0].output_id == "dst-uhd");
    CHECK(doc->decks[0].canvas == seventhslide::geom::CanvasSize{3840, 2160});
    const auto& image = doc->decks[0].slides[0].objects[0];
    CHECK(image.filename == "zzz.png");
    CHECK(image.x == 200);
    CHECK(image.w == 800);
    CHECK(to.media_repo()->list_files(MediaKind::Image, false).size() == 1);

    // The fonts come once the font controller has taken them.
    seventhslide::db::FontRepository to_fonts(*to.open_library().database());
    CHECK(QTest::qWaitFor([&] { return to_fonts.list_family(seventhslide::test::k_fixture_sans).size() == 2; },
                          20000));

    // What was read is gone with the import.
    const auto again = wait_for_import(import, import.importRead({{QStringLiteral("items"), QVariantList{0}},
                                                                  {QStringLiteral("mapping"), QStringList{"dst-uhd", ""}}}));
    CHECK_FALSE(again.value(QStringLiteral("ok")).toBool());
}

TEST_CASE("A viewer's library takes no import, and a file that is not one is refused", "[freeform-files][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir root;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_media_root(root.path);
    write_file(root.path / "not-a-bundle.ssfb", "these are not the bytes of a ZIP archive at all, no");

    auto& import = *facade.freeformImport();
    const auto damaged = wait_for_read(import, import.readFile(url_of(root.path / "not-a-bundle.ssfb")));
    CHECK_FALSE(damaged.value(QStringLiteral("error")).toString().isEmpty());

    facade.set_read_only(true);
    const auto viewer = wait_for_read(import, import.readFile(url_of(root.path / "not-a-bundle.ssfb")));
    CHECK(viewer.value(QStringLiteral("error")).toString().contains(QStringLiteral("viewer")));
}

TEST_CASE("A service's package carries every row, and imports as a service", "[freeform-files][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir from_root;
    const seventhslide::test::TempDir to_root;

    SessionFacade from;
    REQUIRE(from.open_memory_database());
    from.set_media_root(from_root.path);
    from.set_outputs({output(from, "src-main", "Main", 1920, 1080)});
    write_file(from_root.path / "videos" / "v1.mp4", "video bytes");
    from.media_repo()->upsert_file(MediaKind::Video, "v1.mp4", "Welcome loop");
    write_file(from_root.path / "images" / "p1.png", png_bytes(Qt::green));
    write_file(from_root.path / "images" / "p2.png", png_bytes(Qt::blue));
    from.media_repo()->upsert_file(MediaKind::Image, "p1.png", "Bake sale");
    from.media_repo()->upsert_file(MediaKind::Image, "p2.png", "Picnic");

    const auto service_id = from.library()->createService(QStringLiteral("Sunday 30 November"));
    REQUIRE(service_id > 0);
    auto* services = from.service_repo();
    model::Song song;
    song.title = "Test Song";
    song.lyrics = "---[Verse:1]---\nPraise to the Lord\nThe Almighty";
    song.verse_order = "v1";
    auto song_data = song.to_json();
    song_data["stack_id"] = "lyrics";
    const auto song_row = services->add_item(service_id, model::k_item_type_song, song_data);
    (void)services->add_item(service_id, model::k_item_type_divider, nlohmann::json{{"title", "Sermon"}});
    model::WebPage page;
    page.id = 99;
    page.name = "Giving";
    page.url = "https://example.org/give";
    (void)services->add_item(service_id, model::k_item_type_web, model::web_item_data(page));
    model::LiveSource stream;
    stream.kind = std::string(model::k_live_kind_stream);
    stream.name = "Stream";
    stream.url = "srt://host:9000?passphrase=secret";
    (void)services->add_item(service_id, model::k_item_type_live, model::live_item_data(stream));
    const auto video_row = services->add_item(service_id, model::k_item_type_video,
                                              model::playback_item_data("v1.mp4", "Welcome loop", true));
    (void)services->add_item(service_id, model::k_item_type_image_folder,
                             nlohmann::json{{"folder_id", 7}, {"folder_name", "Notices"},
                                            {"images", {"p1.png", "p2.png"}}});
    // The song goes live with the video.
    {
        auto item = *services->get_item(song_row);
        model::set_service_item_also_play(item, {video_row});
        services->replace_item_data(song_row, item.data);
    }

    auto& exporter = *from.exporter();
    const auto file = out.path / "Sunday.zip";
    const auto exported = wait_for_export(exporter, exporter.exportServicePackage(service_id, {}, url_of(file)));
    INFO(exported.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString());
    REQUIRE(exported.value(QStringLiteral("ok")).toBool());
    CHECK(exported.value(QStringLiteral("count")).toInt() == 6);
    // Nothing was written to the library it left.
    CHECK(from.freeformLibrary()->repo()->list_items().empty());
    CHECK(services->list_items(service_id).size() == 6);
    CHECK(from.media_repo()->list_files(MediaKind::Image, false).size() == 2);

    {
        std::string refusal;
        const auto opened = seventhslide::exchange::open_freeform_file(file, refusal);
        REQUIRE(opened);
        REQUIRE(opened->package);
        REQUIRE(opened->rundown);
        const auto& rows = opened->rundown->rows;
        REQUIRE(rows.size() == 6);
        CHECK(rows[0].kind == model::ContentKind::Song);
        CHECK(rows[0].also_play == std::vector<std::size_t>{4});
        CHECK(rows[0].stack_id == "lyrics");
        CHECK_FALSE(rows[2].data.contains("page_id"));
        CHECK(rows[3].data["url"] == "srt://host:9000?passphrase=secret");
        REQUIRE(rows[4].files.size() == 1);
        CHECK(opened->package->find(rows[4].files[0]) != nullptr);
        CHECK(rows[5].files.size() == 2);
        REQUIRE(opened->items.size() == 1);
        REQUIRE(opened->items[0].document);
        CHECK(opened->items[0].document->decks.size() == 1);
    }

    SessionFacade to;
    REQUIRE(to.open_memory_database());
    to.set_media_root(to_root.path);
    to.set_outputs({output(to, "dst-hd", "Projector", 1280, 720)});
    auto& import = *to.freeformImport();
    const auto summary = wait_for_read(import, import.readFile(url_of(file)));
    INFO(summary.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE_FALSE(summary.contains(QStringLiteral("error")));
    CHECK(summary.value(QStringLiteral("kind")).toString() == QStringLiteral("package"));
    const auto service = summary.value(QStringLiteral("service")).toMap();
    CHECK(service.value(QStringLiteral("rows")).toInt() == 6);
    CHECK_FALSE(service.value(QStringLiteral("notes")).toStringList().isEmpty());  // the stack "lyrics"

    const auto result = wait_for_import(
        import, import.importRead({{QStringLiteral("items"), QVariantList{0}},
                                   {QStringLiteral("mapping"), QStringList{"dst-hd"}},
                                   {QStringLiteral("addService"), true},
                                   {QStringLiteral("addItemsToLibrary"), false}}));
    INFO(result.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const auto imported_id = result.value(QStringLiteral("serviceId")).toLongLong();
    REQUIRE(imported_id > 0);
    CHECK(to.freeformLibrary()->repo()->list_items().empty());

    const auto imported = to.service_repo()->get_service(imported_id);
    REQUIRE(imported);
    CHECK(imported->name == "Sunday 30 November");
    const auto rows = to.service_repo()->list_items(imported_id);
    REQUIRE(rows.size() == 6);
    CHECK(rows[0].kind() == model::ContentKind::Freeform);
    CHECK(rows[1].kind() == model::ContentKind::Divider);
    CHECK(rows[2].kind() == model::ContentKind::Web);
    CHECK(rows[3].kind() == model::ContentKind::Live);
    CHECK(rows[4].kind() == model::ContentKind::Video);
    CHECK(rows[5].kind() == model::ContentKind::ImageFolder);
    // The stack this library does not have is left for the row's own.
    CHECK_FALSE(rows[0].data.contains("stack_id"));
    CHECK(model::service_item_also_play(rows[0]) == std::vector<std::int64_t>{*rows[4].item_id});
    CHECK(rows[3].data["url"] == "srt://host:9000?passphrase=secret");
    CHECK_FALSE(rows[2].data.contains("page_id"));

    // The video is the library's, in a folder named for the service.
    const auto video = rows[4].data["filename"].get<std::string>();
    CHECK(fs::exists(to_root.path / "videos" / video));
    CHECK(rows[4].data["loop"] == true);
    const auto video_folders = to.media_repo()->folders(MediaKind::Video).children(std::nullopt);
    REQUIRE(video_folders.size() == 1);
    CHECK(video_folders[0].name == "Sunday 30 November");
    // The pictures, in a folder of their own beneath the service's.
    const auto pictures = rows[5].data["images"];
    REQUIRE(pictures.size() == 2);
    const auto folder_id = rows[5].data["folder_id"].get<std::int64_t>();
    CHECK(to.media_repo()->list_folder_items(MediaKind::Image, folder_id).size() == 2);
    // The song, as Freeform, on the output here.
    const auto doc = ff::load_document(rows[0].data["document"], "Test Song");
    REQUIRE(doc);
    REQUIRE(doc->decks.size() == 1);
    CHECK(doc->decks[0].output_id == "dst-hd");
    CHECK(doc->decks[0].canvas == seventhslide::geom::CanvasSize{1280, 720});
}

TEST_CASE("A service row becomes the Freeform it plays as", "[freeform-files][app][session]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::layout::ApproxTextMeasurer measurer;
    std::vector<model::OutputConfig> outputs{seventhslide::test::facade::make_main()};
    auto hall = outputs.front();
    hall.id = "out-hall";
    hall.name = "Hall";
    outputs.push_back(hall);
    const std::vector<std::string> priority;
    const std::string ccli;
    const seventhslide::theme::OutputThemeMap service_themes;
    const seventhslide::session::StackBuildContext ctx{measurer, outputs, priority, ccli, service_themes};

    model::Song song;
    song.title = "Test Song";
    song.lyrics = "---[Verse:1]---\nPraise to the Lord\n\n---[Verse:2]---\nO my soul praise Him";
    song.verse_order = "v1 v2";
    model::ServiceItem song_row;
    song_row.item_type = model::k_item_type_song;
    song_row.data = song.to_json();
    const auto from_song = seventhslide::session::freeform_from_service_item(song_row, ctx);
    REQUIRE(from_song);
    CHECK(from_song->name == "Test Song");
    REQUIRE(from_song->decks.size() == 2);
    // A slide for each that the row shows as stills.
    const auto stills = seventhslide::session::still_frames(song_row, ctx);
    CHECK(from_song->decks[0].slides.size() == stills[0].frames.size());

    model::BiblePassage passage;
    passage.book = "John";
    passage.chapter = 3;
    passage.verse_start = 16;
    passage.verse_end = 17;
    passage.ref = "John 3:16-17";
    passage.verses = {{16, "For God so loved the world"}, {17, "For God sent not his Son"}};
    model::ServiceItem bible_row;
    bible_row.item_type = model::k_item_type_bible;
    bible_row.data = passage.to_json();
    const auto from_bible = seventhslide::session::freeform_from_service_item(bible_row, ctx);
    REQUIRE(from_bible);
    CHECK_FALSE(from_bible->decks.empty());
    CHECK_FALSE(from_bible->cues.empty());

    // A Freeform row is the document it holds, named as the row is.
    model::FreeformItem snapshot;
    snapshot.name = "Notices";
    snapshot.document = ff::make_document("", {"out-main"}).to_json();
    model::ServiceItem freeform_row;
    freeform_row.item_type = model::k_item_type_freeform;
    freeform_row.data = model::freeform_item_data(snapshot);
    const auto from_freeform = seventhslide::session::freeform_from_service_item(freeform_row, ctx);
    REQUIRE(from_freeform);
    CHECK(from_freeform->name == "Notices");

    model::ServiceItem divider;
    divider.item_type = model::k_item_type_divider;
    divider.data = {{"title", "Sermon"}};
    CHECK_FALSE(seventhslide::session::freeform_from_service_item(divider, ctx));
}

TEST_CASE("A package carries each kind of row, each file once, and a row whose file is not here",
          "[freeform-files][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir from_root;
    const seventhslide::test::TempDir to_root;
    SessionFacade from;
    REQUIRE(from.open_memory_database());
    from.set_media_root(from_root.path);
    from.set_outputs({output(from, "src-main", "Main", 1920, 1080)});
    write_file(from_root.path / "images" / "p1.png", png_bytes(Qt::green));
    from.media_repo()->upsert_file(MediaKind::Image, "p1.png", "Bake sale.png");  // a title with its extension
    write_file(from_root.path / "audio" / "a1.mp3", "sound bytes");
    from.media_repo()->upsert_file(MediaKind::Audio, "a1.mp3", "Offering");

    const auto service_id = from.library()->createService(QStringLiteral("Evening"));
    auto* services = from.service_repo();
    (void)services->add_item(service_id, model::k_item_type_image, model::image_item_data("p1.png", "Bake sale.png"));
    (void)services->add_item(service_id, model::k_item_type_image, model::image_item_data("p1.png", "Again"));
    (void)services->add_item(service_id, model::k_item_type_audio, model::playback_item_data("a1.mp3", "Offering"));
    (void)services->add_item(service_id, model::k_item_type_video, model::playback_item_data("gone.mp4", "Gone"));
    model::BiblePassage passage;
    passage.book = "John";
    passage.chapter = 3;
    passage.verse_start = 16;
    passage.verse_end = 16;
    passage.ref = "John 3:16";
    passage.verses = {{16, "For God so loved the world"}};
    (void)services->add_item(service_id, model::k_item_type_bible, passage.to_json());
    model::FreeformItem snapshot;
    snapshot.name = "Welcome";
    snapshot.document = ff::make_document("Welcome", {"src-main"}).to_json();
    (void)services->add_item(service_id, model::k_item_type_freeform, model::freeform_item_data(snapshot));

    auto& exporter = *from.exporter();
    const auto file = out.path / "Evening.zip";
    const auto exported = wait_for_export(exporter, exporter.exportServicePackage(service_id, {}, url_of(file)));
    INFO(exported.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString());
    REQUIRE(exported.value(QStringLiteral("ok")).toBool());
    CHECK(exported.value(QStringLiteral("count")).toInt() == 6);
    // The missing video is said.
    CHECK(exported.value(QStringLiteral("notes")).toStringList().join(QStringLiteral(" ")).contains(QStringLiteral("Gone")));

    std::string refusal;
    const auto opened = seventhslide::exchange::open_freeform_file(file, refusal);
    REQUIRE(opened);
    const auto& rows = opened->rundown->rows;
    REQUIRE(rows.size() == 6);
    REQUIRE(rows[0].files.size() == 1);
    CHECK(rows[0].files == rows[1].files);  // one file for both rows
    CHECK(rows[0].files[0] == "Media/01 Bake sale.png");  // its extension once
    CHECK(rows[3].files.empty());            // the row goes without its file
    CHECK(opened->items.size() == 2);        // the Bible passage and the Freeform
    std::size_t media_entries = 0;
    for (const auto& e : opened->package->entries()) {
        media_entries += e.name.starts_with("Media/") ? 1 : 0;
    }
    CHECK(media_entries == 2);  // the picture and the audio

    // Into a library of another shape altogether: the media rows need no
    // output, so the service comes in with its items left out.
    SessionFacade to;
    REQUIRE(to.open_memory_database());
    to.set_media_root(to_root.path);
    to.set_outputs({output(to, "dst-square", "Square", 1080, 1080)});
    auto& import = *to.freeformImport();
    REQUIRE_FALSE(wait_for_read(import, import.readFile(url_of(file))).contains(QStringLiteral("error")));
    const auto result = wait_for_import(
        import, import.importRead({{QStringLiteral("items"), QVariantList{}},
                                   {QStringLiteral("mapping"), QStringList{""}},
                                   {QStringLiteral("addService"), true},
                                   {QStringLiteral("addItemsToLibrary"), false}}));
    INFO(result.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const auto imported = to.service_repo()->list_items(result.value(QStringLiteral("serviceId")).toLongLong());
    REQUIRE(imported.size() == 4);  // the two pictures, the audio, the video without its file
    CHECK(imported[0].data["filename"] == imported[1].data["filename"]);
    CHECK(imported[3].data["filename"] == "");
    CHECK(result.value(QStringLiteral("notes")).toStringList().join(QStringLiteral(" ")).contains(QStringLiteral("John 3:16")));
    CHECK(to.media_repo()->list_files(MediaKind::Image, false).size() == 1);
}

TEST_CASE("An import stopped, or whose library changes, leaves nothing", "[freeform-files][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir root;
    const seventhslide::test::TempDir other_root;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_media_root(root.path);
    std::vector<std::string> ids;
    seventhslide::exchange::BundleSource source;
    for (const auto& oc : facade.session().outputs()) {
        ids.push_back(oc.id);
        source.outputs.push_back({oc.id, oc.name, model::output_canvas(oc)});
    }
    write_file(out.path / "pic.png", png_bytes(Qt::yellow));
    source.items.push_back({"Welcome", {}, {}, sample_document("Welcome", ids, "pic.png", "Liberation Sans"), {}});
    source.media["pic.png"] = {out.path / "pic.png", "Sun", seventhslide::canvas::MediaRefKind::Picture};
    const auto file = out.path / "Welcome.ssff";
    REQUIRE(seventhslide::exchange::write_bundle(source, file, {}).ok);

    auto& import = *facade.freeformImport();
    const QVariantMap choices{{QStringLiteral("items"), QVariantList{0}},
                              {QStringLiteral("mapping"), QStringList{QString::fromStdString(ids.front())}}};
    const auto nothing_left = [&] {
        CHECK(facade.freeformLibrary()->repo()->list_items().empty());
        CHECK(facade.media_repo()->list_files(MediaKind::Image, false).empty());
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(root.path / "images", ec)) {
            CHECK_FALSE(e.is_regular_file());  // no copy, staged or adopted
        }
    };

    // Stopped before it commits.
    REQUIRE_FALSE(wait_for_read(import, import.readFile(url_of(file))).contains(QStringLiteral("error")));
    const auto stopped_id = import.importRead(choices);
    import.cancel();
    const auto stopped = wait_for_import(import, stopped_id);
    CHECK_FALSE(stopped.value(QStringLiteral("ok")).toBool());
    CHECK(stopped.value(QStringLiteral("error")).toString().contains(QStringLiteral("stopped")));
    nothing_left();

    // The library's media folder changes before it commits.
    REQUIRE_FALSE(wait_for_read(import, import.readFile(url_of(file))).contains(QStringLiteral("error")));
    const auto moved_id = import.importRead(choices);
    facade.set_media_root(other_root.path);
    const auto moved = wait_for_import(import, moved_id);
    CHECK_FALSE(moved.value(QStringLiteral("ok")).toBool());
    CHECK(moved.value(QStringLiteral("error")).toString().contains(QStringLiteral("library changed")));
    facade.set_media_root(root.path);
    nothing_left();
}

TEST_CASE("An import keeps the library's own face in a slot it fills", "[freeform-files][app][font]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir from_root;
    const seventhslide::test::TempDir to_root;
    SessionFacade from;
    REQUIRE(from.open_memory_database());
    from.set_media_root(from_root.path);
    {
        seventhslide::app::FontStore fonts(*from.open_library().database(), from_root.path);
        REQUIRE(fonts.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf")).error.empty());
        REQUIRE(fonts.import_file(seventhslide::test::fixture_font("SSFixtureSans-Bold.ttf")).error.empty());
    }
    std::vector<std::string> ids;
    for (const auto& oc : from.session().outputs()) {
        ids.push_back(oc.id);
    }
    model::FreeformItem item;
    item.name = "Words";
    item.document = sample_document("Words", ids, "", seventhslide::test::k_fixture_sans).to_json();
    const auto item_id = from.freeformLibrary()->repo()->create_item(item);
    auto& exporter = *from.exporter();
    const auto file = out.path / "Words.ssff";
    REQUIRE(wait_for_export(exporter, exporter.exportFreeformFile(QVariantList{QVariant::fromValue<qint64>(item_id)},
                                                                  {}, url_of(file)))
                .value(QStringLiteral("ok"))
                .toBool());

    // The library it comes into has its own SSFixture Sans Regular: other bytes, one slot.
    SessionFacade to;
    REQUIRE(to.open_memory_database());
    to.set_media_root(to_root.path);
    auto own = read_bytes(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"));
    own += std::string(4, '\0');
    write_file(out.path / "own-regular.ttf", own);
    std::string own_name;
    {
        seventhslide::app::FontStore fonts(*to.open_library().database(), to_root.path);
        const auto added = fonts.import_file(out.path / "own-regular.ttf");
        REQUIRE(added.error.empty());
        REQUIRE(added.faces.size() == 1);
        own_name = added.faces.front().filename;
    }
    to.fonts()->refresh();

    auto& import = *to.freeformImport();
    const auto summary = wait_for_read(import, import.readFile(url_of(file)));
    REQUIRE_FALSE(summary.contains(QStringLiteral("error")));
    QStringList mapping;
    for (const auto& o : summary.value(QStringLiteral("outputs")).toList()) {
        mapping.push_back(o.toMap().value(QStringLiteral("suggested")).toString());
    }
    const auto result = wait_for_import(import, import.importRead({{QStringLiteral("items"), QVariantList{0}},
                                                                   {QStringLiteral("mapping"), mapping}}));
    INFO(result.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("notes")).toStringList().join(QStringLiteral(" ")).contains(QStringLiteral("kept")));
    seventhslide::db::FontRepository to_fonts(*to.open_library().database());
    REQUIRE(QTest::qWaitFor([&] { return to_fonts.list_family(seventhslide::test::k_fixture_sans).size() == 2; },
                            20000));
    bool own_kept = false;
    for (const auto& face : to_fonts.list_family(seventhslide::test::k_fixture_sans)) {
        if (!face.bold) {
            own_kept = face.filename == own_name;
        }
    }
    CHECK(own_kept);
}

TEST_CASE("A package's files come in once a folder, in their rows' order", "[freeform-files][app]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir from_root;
    const seventhslide::test::TempDir to_root;
    SessionFacade from;
    REQUIRE(from.open_memory_database());
    from.set_media_root(from_root.path);
    const auto green = png_bytes(Qt::green);
    const auto blue = png_bytes(Qt::blue);
    const auto red = png_bytes(Qt::red);
    write_file(from_root.path / "images" / "p1.png", green);
    write_file(from_root.path / "images" / "p2.png", green);  // the same bytes under another name
    write_file(from_root.path / "images" / "p3.png", blue);
    write_file(from_root.path / "images" / "p4.png", red);
    for (const auto* name : {"p1.png", "p2.png", "p3.png", "p4.png"}) {
        from.media_repo()->upsert_file(MediaKind::Image, name, name);
    }
    const auto service_id = from.library()->createService(QStringLiteral("Morning"));
    auto* services = from.service_repo();
    (void)services->add_item(service_id, model::k_item_type_image, model::image_item_data("p1.png", "One"));
    (void)services->add_item(service_id, model::k_item_type_image, model::image_item_data("p2.png", "Two"));
    (void)services->add_item(service_id, model::k_item_type_image_folder,
                             nlohmann::json{{"folder_id", 3}, {"folder_name", "Notices"},
                                            {"images", {"p3.png", "p4.png", "p1.png"}}});
    auto& exporter = *from.exporter();
    const auto file = out.path / "Morning.zip";
    REQUIRE(wait_for_export(exporter, exporter.exportServicePackage(service_id, {}, url_of(file)))
                .value(QStringLiteral("ok"))
                .toBool());

    // The library it comes into has the red picture already.
    SessionFacade to;
    REQUIRE(to.open_memory_database());
    to.set_media_root(to_root.path);
    write_file(to_root.path / "images" / "own-red.png", red);
    to.media_repo()->upsert_file(MediaKind::Image, "own-red.png", "Red");
    auto& import = *to.freeformImport();
    REQUIRE_FALSE(wait_for_read(import, import.readFile(url_of(file))).contains(QStringLiteral("error")));
    const auto result = wait_for_import(import, import.importRead({{QStringLiteral("items"), QVariantList{}},
                                                                   {QStringLiteral("mapping"), QStringList{}},
                                                                   {QStringLiteral("addService"), true}}));
    INFO(result.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    const auto rows = to.service_repo()->list_items(result.value(QStringLiteral("serviceId")).toLongLong());
    REQUIRE(rows.size() == 3);
    // The two green pictures are one library file.
    const auto green_name = rows[0].data["filename"].get<std::string>();
    CHECK(rows[1].data["filename"] == green_name);
    // Blue, the library's own red, then green: in the row's order.
    const auto folder_id = rows[2].data["folder_id"].get<std::int64_t>();
    const auto in_folder = to.media_repo()->list_folder_items(MediaKind::Image, folder_id);
    REQUIRE(in_folder.size() == 3);
    CHECK(in_folder[1].filename == "own-red.png");
    CHECK(in_folder[2].filename == green_name);
    CHECK(rows[2].data["images"] == nlohmann::json{in_folder[0].filename, "own-red.png", green_name});
    // Green and blue new, red the library's.
    CHECK(to.media_repo()->list_files(MediaKind::Image, false).size() == 3);
}

TEST_CASE("A font is kept by what its file is, whatever the file says of it", "[freeform-files][app][font]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir to_root;
    // A file whose manifest calls SSFixture Sans Regular "Zzz".
    seventhslide::exchange::BundleSource source;
    SessionFacade to;
    REQUIRE(to.open_memory_database());
    to.set_media_root(to_root.path);
    std::vector<std::string> ids;
    for (const auto& oc : to.session().outputs()) {
        ids.push_back(oc.id);
        source.outputs.push_back({oc.id, oc.name, model::output_canvas(oc)});
    }
    source.items.push_back({"Words", {}, {}, sample_document("Words", ids, "", "Zzz"), {}});
    seventhslide::db::LibraryFont claimed;
    claimed.filename = "x.ttf";
    claimed.family = "Zzz";
    claimed.style = "Regular";
    claimed.display_name = "Zzz Regular";
    source.fonts.push_back({seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"), claimed});
    const auto file = out.path / "Words.ssff";
    REQUIRE(seventhslide::exchange::write_bundle(source, file, {}).ok);

    // The library has its own SSFixture Sans Regular: other bytes, that slot.
    auto own = read_bytes(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"));
    own += std::string(4, '\0');
    write_file(out.path / "own-regular.ttf", own);
    std::string own_name;
    {
        seventhslide::app::FontStore fonts(*to.open_library().database(), to_root.path);
        const auto added = fonts.import_file(out.path / "own-regular.ttf");
        REQUIRE(added.error.empty());
        own_name = added.faces.front().filename;
    }
    auto& import = *to.freeformImport();
    REQUIRE_FALSE(wait_for_read(import, import.readFile(url_of(file))).contains(QStringLiteral("error")));
    const auto result = wait_for_import(
        import, import.importRead({{QStringLiteral("items"), QVariantList{0}},
                                   {QStringLiteral("mapping"), QStringList{QString::fromStdString(ids.front())}}}));
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("fonts")).toInt() == 1);  // handed to the font controller
    REQUIRE(QTest::qWaitFor([&] { return !to.fonts()->adding(); }, 20000));
    seventhslide::db::FontRepository to_fonts(*to.open_library().database());
    const auto faces = to_fonts.list_family(seventhslide::test::k_fixture_sans);
    REQUIRE(faces.size() == 1);
    CHECK(faces.front().filename == own_name);
    CHECK(to_fonts.list_family("Zzz").empty());
}

namespace {

/// A picture of four flat quarters, so where each part of it lands shows.
[[nodiscard]] std::string quarters_png(int w, int h, std::array<QColor, 4> colours)
{
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto quarter = (y < h / 2 ? 0 : 2) + (x < w / 2 ? 0 : 1);
            image.setPixelColor(x, y, colours[static_cast<std::size_t>(quarter)]);
        }
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    REQUIRE(image.save(&buffer, "PNG"));
    return bytes.toStdString();
}

/// Every slide of ``source`` on output ``output_id``, as ``library`` paints
/// it when its outputs are ``outputs``.
[[nodiscard]] std::vector<QImage> paint_slides(SessionFacade& library,
                                               const seventhslide::session::StillSource& source,
                                               const std::vector<model::OutputConfig>& outputs,
                                               const std::string& output_id)
{
    auto& previews = library.previews();
    const std::vector<std::string> priority;
    const std::string ccli;
    const seventhslide::theme::OutputThemeMap service_themes;
    const seventhslide::session::StackBuildContext ctx{previews.measurer(), outputs, priority, ccli,
                                                       service_themes};
    auto stills = seventhslide::session::still_frames(source, ctx);
    const auto on = std::ranges::find(stills, output_id, &seventhslide::session::OutputStills::output_id);
    REQUIRE(on != stills.end());
    const auto* oc = model::find_output_by_id(outputs, output_id);
    REQUIRE(oc != nullptr);
    const auto canvas = model::output_canvas(*oc);
    std::vector<QImage> images;
    for (auto& frame : on->frames) {
        seventhslide::render::StillPaint still;
        still.frame = seventhslide::scene::share_frame(std::move(frame));
        still.size = QSize(canvas.width, canvas.height);
        still.cache = previews.image_cache();
        still.faces = &previews.measurer().faces();
        auto image = seventhslide::render::paint_still(still);
        REQUIRE_FALSE(image.isNull());
        images.push_back(image.convertToFormat(QImage::Format_ARGB32));
    }
    return images;
}

[[nodiscard]] std::vector<QImage> paint_slides(SessionFacade& library, const ff::Document& doc,
                                               const std::vector<model::OutputConfig>& outputs,
                                               const std::string& output_id)
{
    return paint_slides(library, seventhslide::session::LiveFreeformContent{doc.name, doc, std::nullopt}, outputs,
                        output_id);
}

/// How two paintings differ: the pixels that do, the most any channel does,
/// and the first that does.
struct PixelDifference {
    qint64 pixels = 0;
    int worst = 0;
    QPoint first{-1, -1};
};

[[nodiscard]] PixelDifference pixel_difference(const QImage& a, const QImage& b)
{
    REQUIRE(a.size() == b.size());
    PixelDifference out;
    for (int y = 0; y < a.height(); ++y) {
        const auto* ra = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const auto* rb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            if (ra[x] == rb[x]) {
                continue;
            }
            if (out.pixels++ == 0) {
                out.first = {x, y};
            }
            out.worst = std::max({out.worst, std::abs(qRed(ra[x]) - qRed(rb[x])),
                                  std::abs(qGreen(ra[x]) - qGreen(rb[x])), std::abs(qBlue(ra[x]) - qBlue(rb[x])),
                                  std::abs(qAlpha(ra[x]) - qAlpha(rb[x]))});
        }
    }
    return out;
}

[[nodiscard]] std::string describe(const PixelDifference& d)
{
    return std::to_string(d.pixels) + " pixels differ, by up to " + std::to_string(d.worst) + ", the first at ("
           + std::to_string(d.first.x()) + ", " + std::to_string(d.first.y()) + ")";
}

[[nodiscard]] bool looks_like(QRgb px, const QColor& colour)
{
    return seventhslide::test::nearly_rgb(px, colour.red(), colour.green(), colour.blue(), 4);
}

/// Each picture's top-left quarter, which a sample inside its box shows.
const std::array<QColor, 4> k_back{QColor(20, 40, 90), QColor(30, 120, 130), QColor(60, 20, 70), QColor(10, 80, 40)};
const std::array<QColor, 4> k_photo{QColor(240, 140, 20), QColor(120, 40, 160), QColor(250, 250, 250),
                                    QColor(0, 0, 0)};
const std::array<QColor, 4> k_texture{QColor(40, 200, 60), QColor(240, 230, 40), QColor(200, 30, 30),
                                      QColor(30, 30, 200)};
const std::array<QColor, 4> k_poster{QColor(220, 20, 120), QColor(255, 255, 255), QColor(90, 90, 90),
                                     QColor(20, 220, 220)};

/// A document that names every kind of file and both styles of a library
/// font, laid out on each deck's own canvas: a slide over a picture, with a
/// picture, a video's poster, a shape filled with a picture and two lines of
/// text; and a slide of a colour, a turned shape and text that wraps.
[[nodiscard]] ff::Document pixel_document(const std::vector<model::OutputConfig>& outputs)
{
    std::vector<std::string> ids;
    for (const auto& oc : outputs) {
        ids.push_back(oc.id);
    }
    auto doc = ff::make_document("Pixels", ids);
    for (auto& deck : doc.decks) {
        deck.canvas = model::output_canvas(*model::find_output_by_id(outputs, deck.output_id));
        const double w = deck.canvas.width;
        const double h = deck.canvas.height;
        auto& first = deck.slides.front();
        first.background.kind = ff::SlideBackgroundKind::Image;
        first.background.image = "back.png";

        ff::Object photo;
        photo.type = ff::ObjectType::Image;
        photo.filename = "photo.png";
        photo.object_fit = "fill";
        photo.x = w * 0.05;
        photo.y = h * 0.1;
        photo.w = w * 0.3;
        photo.h = h * 0.3;

        ff::Object clip;
        clip.type = ff::ObjectType::Video;
        clip.filename = "clip.mp4";
        clip.video.poster = "poster.png";
        clip.object_fit = "fill";
        clip.x = w * 0.6;
        clip.y = h * 0.1;
        clip.w = w * 0.32;
        clip.h = h * 0.32;

        ff::Object shape;
        shape.type = ff::ObjectType::Shape;
        shape.shape_kind = seventhslide::geom::ShapeKind::Ellipse;
        shape.fill.kind = seventhslide::canvas::FillKind::Picture;
        shape.fill.picture.file = "texture.png";
        shape.fill.picture.fit = "fill";
        shape.line.color = "#ffffff";
        shape.line.width = 6;
        shape.x = w * 0.4;
        shape.y = h * 0.5;
        shape.w = h * 0.3;
        shape.h = h * 0.3;

        ff::Object words;
        words.type = ff::ObjectType::Text;
        words.text = "Welcome home, everyone";
        words.font_family = std::string(seventhslide::test::k_fixture_wide);
        words.font_size = 64;
        words.x = w * 0.05;
        words.y = h * 0.75;
        words.w = w * 0.9;
        words.h = h * 0.1;

        ff::Object heavy = words;
        heavy.text = "Sunday 30 November";
        heavy.bold = true;
        heavy.font_color = "#ffee00";
        heavy.text_align = "right";
        heavy.y = h * 0.86;

        first.objects = {photo, clip, shape, words, heavy};

        ff::Slide second;
        second.id = deck.output_id + "-second";
        second.background.kind = ff::SlideBackgroundKind::Color;
        second.background.color = "#203040";
        ff::Object block;
        block.type = ff::ObjectType::Shape;
        block.fill.color = "#c04020";
        block.line.color = "#f0f0f0";
        block.line.width = 4;
        block.rotation = 15;
        block.x = w * 0.1;
        block.y = h * 0.2;
        block.w = w * 0.25;
        block.h = h * 0.25;
        ff::Object wraps = words;
        wraps.text = "Praise to the Lord, the Almighty, the King of creation; O my soul, praise him";
        wraps.font_size = 56;
        wraps.y = h * 0.5;
        wraps.h = h * 0.4;
        wraps.italic = true;
        second.objects = {block, wraps};
        deck.slides.push_back(second);
    }
    return doc;
}

}  // namespace

TEST_CASE("An imported Freeform paints pixel for pixel as it did where it was made", "[freeform-files][app][pixels]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir from_root;
    const seventhslide::test::TempDir to_root;
    const seventhslide::test::TempDir bare_root;
    const auto back = quarters_png(64, 36, k_back);
    const auto photo = quarters_png(40, 30, k_photo);
    const auto texture = quarters_png(16, 16, k_texture);
    const auto poster = quarters_png(32, 18, k_poster);

    // Where it is made: a 16:9 output and a 4:3 one, its files and a font of
    // two styles.
    SessionFacade from;
    REQUIRE(from.open_memory_database());
    from.set_media_root(from_root.path);
    from.set_outputs({output(from, "src-main", "Main", 1920, 1080), output(from, "src-stage", "Stage", 1024, 768)});
    for (const auto& [name, bytes] : {std::pair{"back.png", back}, std::pair{"photo.png", photo},
                                      std::pair{"texture.png", texture}, std::pair{"poster.png", poster}}) {
        write_file(from_root.path / "images" / name, bytes);
        from.media_repo()->upsert_file(MediaKind::Image, name, name);
    }
    write_file(from_root.path / "videos" / "clip.mp4", "video bytes");
    from.media_repo()->upsert_file(MediaKind::Video, "clip.mp4", "Clip");
    {
        seventhslide::app::FontStore fonts(*from.open_library().database(), from_root.path);
        REQUIRE(fonts.import_file(seventhslide::test::fixture_font("SSFixtureWide-Regular.ttf")).error.empty());
        REQUIRE(fonts.import_file(seventhslide::test::fixture_font("SSFixtureWide-Bold.ttf")).error.empty());
    }
    from.fonts()->library_fonts_changed();
    const auto from_outputs = from.session().outputs();
    const auto made = pixel_document(from_outputs);
    model::FreeformItem item;
    item.name = "Pixels";
    item.document = made.to_json();
    const auto id = from.freeformLibrary()->repo()->create_item(item);

    auto& exporter = *from.exporter();
    const auto exported = wait_for_export(
        exporter, exporter.exportFreeformFile(QVariantList{QVariant::fromValue<qint64>(id)}, QVariantList{},
                                              url_of(out.path / "Pixels")));
    INFO(exported.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString());
    REQUIRE(exported.value(QStringLiteral("ok")).toBool());
    CHECK(exported.value(QStringLiteral("notes")).toStringList().isEmpty());
    const auto file = out.path / "Pixels.ssff";
    REQUIRE(fs::exists(file));

    // Where it goes: other ids and names, outputs of both shapes and a smaller
    // 16:9 one; a picture of its own under the name the photo had, and the
    // background picture already, under another.
    SessionFacade to;
    REQUIRE(to.open_memory_database());
    to.set_media_root(to_root.path);
    to.set_outputs({output(to, "dst-front", "Front", 1920, 1080), output(to, "dst-side", "Side", 1024, 768),
                    output(to, "dst-small", "Small", 1280, 720)});
    write_file(to_root.path / "images" / "photo.png", quarters_png(40, 30, k_texture));
    to.media_repo()->upsert_file(MediaKind::Image, "photo.png", "Another photo");
    write_file(to_root.path / "images" / "own-back.png", back);
    to.media_repo()->upsert_file(MediaKind::Image, "own-back.png", "Back");

    auto& import = *to.freeformImport();
    const auto bring = [&](const QStringList& mapping) {
        const auto summary = wait_for_read(import, import.readFile(url_of(file)));
        INFO(summary.value(QStringLiteral("error")).toString().toStdString());
        REQUIRE_FALSE(summary.contains(QStringLiteral("error")));
        const auto result = wait_for_import(import, import.importRead({{QStringLiteral("items"), QVariantList{0}},
                                                                       {QStringLiteral("mapping"), mapping}}));
        INFO(result.value(QStringLiteral("error")).toString().toStdString());
        REQUIRE(result.value(QStringLiteral("ok")).toBool());
        CHECK(result.value(QStringLiteral("notes")).toStringList().isEmpty());
    };
    bring({QStringLiteral("dst-front"), QStringLiteral("dst-side")});
    seventhslide::db::FontRepository to_fonts(*to.open_library().database());
    REQUIRE(QTest::qWaitFor(
        [&] {
            return !to.fonts()->adding() && to_fonts.list_family(seventhslide::test::k_fixture_wide).size() == 2;
        },
        20000));
    auto items = to.freeformLibrary()->repo()->list_items();
    REQUIRE(items.size() == 1);
    const auto came = ff::load_document(items.front().document, "Pixels");
    REQUIRE(came);
    const auto to_outputs = to.session().outputs();

    // The library it was made in paints its pictures and its font: a sample
    // inside each box shows that picture's top-left quarter.
    const auto main_made = paint_slides(from, made, from_outputs, "src-main");
    REQUIRE(main_made.size() == 2);
    const auto& first = main_made[0];
    CHECK(looks_like(first.pixel(10, 10), k_back[0]));
    CHECK(looks_like(first.pixel(1920 * 5 / 100 + 20, 1080 / 10 + 20), k_photo[0]));
    CHECK(looks_like(first.pixel(1920 * 6 / 10 + 20, 1080 / 10 + 20), k_poster[0]));
    CHECK(looks_like(first.pixel(1920 * 4 / 10 + 1080 * 3 / 40, 1080 / 2 + 1080 * 3 / 40), k_texture[0]));
    {
        // Without its font and its files, it paints otherwise.
        SessionFacade bare;
        REQUIRE(bare.open_memory_database());
        bare.set_media_root(bare_root.path);
        bare.set_outputs(from_outputs);
        const auto main_bare = paint_slides(bare, made, from_outputs, "src-main");
        REQUIRE(main_bare.size() == 2);
        CHECK(pixel_difference(main_made[0], main_bare[0]).pixels > 0);
        CHECK(pixel_difference(main_made[1], main_bare[1]).pixels > 0);  // the font alone
        // Each line of it, in the stand-in, is narrower.
        const auto ink_wide = [](const QImage& image, int top, int bottom) {
            int left = image.width();
            int right = -1;
            for (int y = top; y < bottom; ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const auto px = image.pixel(x, y);
                    if (qRed(px) > 200 && qGreen(px) > 200) {  // white and yellow words
                        left = std::min(left, x);
                        right = std::max(right, x);
                    }
                }
            }
            return right - left;
        };
        // "Welcome home, everyone", below the circle; "Sunday 30 November".
        CHECK(ink_wide(main_made[0], 1080 * 77 / 100, 1080 * 85 / 100)
              > ink_wide(main_bare[0], 1080 * 77 / 100, 1080 * 85 / 100) * 5 / 4);
        CHECK(ink_wide(main_made[0], 1080 * 88 / 100, 1080 * 95 / 100)
              > ink_wide(main_bare[0], 1080 * 88 / 100, 1080 * 95 / 100) * 5 / 4);
    }

    // Onto outputs of the same size: every slide, every pixel.
    for (const auto& [made_on, came_on] : {std::pair{"src-main", "dst-front"}, std::pair{"src-stage", "dst-side"}}) {
        const auto before = paint_slides(from, made, from_outputs, made_on);
        const auto after = paint_slides(to, *came, to_outputs, came_on);
        REQUIRE(before.size() == 2);
        REQUIRE(after.size() == before.size());
        for (std::size_t s = 0; s < before.size(); ++s) {
            const auto diff = pixel_difference(before[s], after[s]);
            INFO(came_on << " slide " << s + 1 << ": " << describe(diff));
            CHECK(diff.pixels == 0);
        }
    }

    // Onto a smaller output of its shape: as the library it was made in would
    // paint it, had its output been that size.
    bring({QStringLiteral("dst-small"), QString{}});
    items = to.freeformLibrary()->repo()->list_items();
    REQUIRE(items.size() == 2);
    const auto small_item = std::ranges::find_if(items, [](const model::FreeformItem& i) {
        const auto doc = ff::load_document(i.document, i.name);
        return doc && doc->decks.size() == 1 && doc->decks.front().output_id == "dst-small";
    });
    REQUIRE(small_item != items.end());
    const auto small = ff::load_document(small_item->document, "Pixels");
    REQUIRE(small);
    auto from_small = from_outputs;
    from_small.front().canvas_width = 1280;
    from_small.front().canvas_height = 720;
    const auto before = paint_slides(from, made, from_small, "src-main");
    const auto after = paint_slides(to, *small, to_outputs, "dst-small");
    REQUIRE(after.size() == before.size());
    for (std::size_t s = 0; s < before.size(); ++s) {
        const auto diff = pixel_difference(before[s], after[s]);
        const double mean = seventhslide::test::mean_abs_diff(before[s], after[s]);
        INFO("dst-small slide " << s + 1 << ": " << describe(diff) << ", mean " << mean);
        CHECK(diff.pixels == 0);
    }
}

TEST_CASE("A song or passage in a package paints, imported, as it played in its service",
          "[freeform-files][app][pixels]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir out;
    const seventhslide::test::TempDir from_root;
    const seventhslide::test::TempDir to_root;
    const auto dawn = quarters_png(64, 36, k_back);

    // Where it is made: songs and passages on Main in the wide font, over a
    // picture their background theme owns.
    SessionFacade from;
    REQUIRE(from.open_memory_database());
    from.set_media_root(from_root.path);
    auto main = output(from, "src-main", "Main", 1920, 1080);
    main.song_themes = {model::ThemeEntry{
        "wide-song", "Wide",
        seventhslide::test::flat_style({{"font_family", std::string(seventhslide::test::k_fixture_wide)},
                                        {"font_size", 72},
                                        {"font_color", "#ffffff"}})}};
    main.bible_themes = {model::ThemeEntry{
        "wide-verse", "Wide verse",
        seventhslide::test::flat_style({{"font_family", std::string(seventhslide::test::k_fixture_wide)},
                                        {"font_size", 60},
                                        {"font_color", "#ffee00"}})}};
    main.bg_themes = {model::ThemeEntry{
        "dawn", "Dawn",
        nlohmann::json{{"background_type", "image"}, {"background_image", "/static/theme_backgrounds/dawn.png"}}}};
    main.category_defaults = {{"song", {{"theme", "wide-song"}, {"bg", "dawn"}}},
                              {"bible", {{"theme", "wide-verse"}, {"bg", "dawn"}}}};
    from.set_outputs({main});
    write_file(from_root.path / "theme_backgrounds" / "dawn.png", dawn);
    {
        seventhslide::app::FontStore fonts(*from.open_library().database(), from_root.path);
        REQUIRE(fonts.import_file(seventhslide::test::fixture_font("SSFixtureWide-Regular.ttf")).error.empty());
        REQUIRE(fonts.import_file(seventhslide::test::fixture_font("SSFixtureWide-Bold.ttf")).error.empty());
    }
    from.fonts()->library_fonts_changed();

    const auto service_id = from.library()->createService(QStringLiteral("Sunday 30 November"));
    REQUIRE(service_id > 0);
    auto* services = from.service_repo();
    model::Song song;
    song.title = "Praise to the Lord";
    song.lyrics = "---[Verse:1]---\nPraise to the Lord, the Almighty\nThe King of creation\n\n"
                  "---[Chorus:1]---\nO my soul, praise him\nFor he is thy health and salvation";
    song.verse_order = "v1 c1 v1";
    (void)services->add_item(service_id, model::k_item_type_song, song.to_json());
    model::BiblePassage passage;
    passage.book = "John";
    passage.chapter = 3;
    passage.verse_start = 16;
    passage.verse_end = 17;
    passage.ref = "John 3:16-17";
    passage.verses = {{16, "For God so loved the world, that he gave his only begotten Son"},
                      {17, "For God sent not his Son into the world to condemn the world"}};
    (void)services->add_item(service_id, model::k_item_type_bible, passage.to_json());
    const auto played = services->list_items(service_id);
    REQUIRE(played.size() == 2);

    auto& exporter = *from.exporter();
    const auto file = out.path / "Sunday.zip";
    const auto exported = wait_for_export(exporter, exporter.exportServicePackage(service_id, {}, url_of(file)));
    INFO(exported.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString());
    REQUIRE(exported.value(QStringLiteral("ok")).toBool());

    // Where it goes: an output of that size by another id and name, and no
    // themes or font of its own.
    SessionFacade to;
    REQUIRE(to.open_memory_database());
    to.set_media_root(to_root.path);
    to.set_outputs({output(to, "dst-front", "Front", 1920, 1080)});
    auto& import = *to.freeformImport();
    const auto summary = wait_for_read(import, import.readFile(url_of(file)));
    INFO(summary.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE_FALSE(summary.contains(QStringLiteral("error")));
    const auto result = wait_for_import(
        import, import.importRead({{QStringLiteral("items"), QVariantList{0, 1}},
                                   {QStringLiteral("mapping"), QStringList{"dst-front"}},
                                   {QStringLiteral("addService"), true},
                                   {QStringLiteral("addItemsToLibrary"), false}}));
    INFO(result.value(QStringLiteral("error")).toString().toStdString());
    REQUIRE(result.value(QStringLiteral("ok")).toBool());
    seventhslide::db::FontRepository to_fonts(*to.open_library().database());
    REQUIRE(QTest::qWaitFor(
        [&] {
            return !to.fonts()->adding() && to_fonts.list_family(seventhslide::test::k_fixture_wide).size() == 2;
        },
        20000));
    const auto rows = to.service_repo()->list_items(result.value(QStringLiteral("serviceId")).toLongLong());
    REQUIRE(rows.size() == 2);

    const auto from_outputs = from.session().outputs();
    const auto to_outputs = to.session().outputs();
    for (std::size_t r = 0; r < rows.size(); ++r) {
        CHECK(rows[r].kind() == model::ContentKind::Freeform);
        // The theme's picture is the library's own now.
        const auto doc = ff::load_document(rows[r].data["document"], "row");
        REQUIRE(doc);
        REQUIRE(doc->decks.size() == 1);
        for (const auto& slide : doc->decks[0].slides) {
            CHECK(slide.background.kind == ff::SlideBackgroundKind::Image);
            CHECK_FALSE(slide.background.image.starts_with("/static/"));
            CHECK(fs::exists(to_root.path / "images" / slide.background.image));
        }
        const auto before = paint_slides(from, played[r], from_outputs, "src-main");
        const auto after = paint_slides(to, rows[r], to_outputs, "dst-front");
        REQUIRE(before.size() >= 2);
        REQUIRE(after.size() == before.size());
        CHECK(looks_like(before[0].pixel(10, 10), k_back[0]));
        CHECK(seventhslide::test::count_ink_in(before[0], QRect(0, 0, 1920, 1080), 230) > 0);
        for (std::size_t s = 0; s < before.size(); ++s) {
            const auto diff = pixel_difference(before[s], after[s]);
            INFO("row " << r + 1 << " slide " << s + 1 << ": " << describe(diff));
            CHECK(diff.pixels == 0);
        }
    }
}
