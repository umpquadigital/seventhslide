// Previews prepared on the GUI thread and laid out or painted elsewhere
// (preview_job.hpp): the image providers' worker threads and the remote's
// encode pool.

#include "session_facade_fixture.hpp"

#include "freeform_thumb_cache.hpp"
#include "media_thumb_cache.hpp"
#include "preview_job.hpp"
#include "service_thumbs.hpp"
#include "theme_thumb_cache.hpp"

#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/freeform/document.hpp>

#include <QColor>
#include <QUrl>
#include <QUrlQuery>

#include <thread>

using namespace seventhslide::test::facade;
using seventhslide::app::PreviewJob;

namespace {

/// ``job`` finished on a thread of its own, as a provider's worker would.
QImage finish_elsewhere(const PreviewJob& job)
{
    QImage out;
    std::thread worker([&] { out = seventhslide::app::finish_preview(job); });
    worker.join();
    return out;
}

/// The id of the first text theme of the output ``output``, as a themepreview URL.
QString text_theme_url(SessionFacade& facade, const QString& output)
{
    const auto themes = facade.themes()->list(output, QStringLiteral("song"));
    REQUIRE_FALSE(themes.isEmpty());
    return QStringLiteral("theme/") + output + QStringLiteral("/song/")
           + themes.front().toMap().value(QStringLiteral("id")).toString()
           + QStringLiteral("?c=song");
}

}  // namespace

TEST_CASE("A preview painted off the GUI thread matches one painted on it", "[app][previews]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.live()->play_song(demo_song());
    const QSize size(320, 180);

    SECTION("an output preview")
    {
        const QString out = first_output_id(facade);
        const QImage here = facade.outputPreviews()->render_output_preview(out, size);
        REQUIRE_FALSE(here.isNull());
        const auto job = facade.outputPreviews()->prepare_output_preview(out, size);
        REQUIRE(job.still.has_value());  // the paint is left to the worker
        CHECK(finish_elsewhere(job) == here);
    }

    SECTION("a theme preview")
    {
        const QString url = text_theme_url(facade, QStringLiteral("Main"));
        // Both prepared before either is painted, so neither is a cache hit.
        const auto mine = facade.themes()->prepare_preview(url, size);
        const auto theirs = facade.themes()->prepare_preview(url, size);
        // The layout, too, is left to the worker.
        REQUIRE(mine.lay_out);
        REQUIRE_FALSE(mine.still.has_value());
        REQUIRE(theirs.lay_out);
        const QImage here = seventhslide::app::finish_preview(mine);
        REQUIRE_FALSE(here.isNull());
        CHECK(finish_elsewhere(theirs) == here);
        // Painted, it is kept: the next ask is a hit with nothing to paint.
        const auto again = facade.themes()->prepare_preview(url, size);
        CHECK_FALSE(again.still.has_value());
        CHECK_FALSE(again.lay_out);
        CHECK(again.image == here);
    }

    SECTION("a Freeform editor slide")
    {
        const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Untitled"));
        auto* edit = facade.slideEdit();
        REQUIRE(edit->openFreeform(id));
        REQUIRE(edit->objects()->insert(QStringLiteral("text")).isEmpty());
        REQUIRE(edit->objects()
                    ->setText(edit->objects()->selection().value(QStringLiteral("id")).toString(),
                              QStringLiteral("Hello"), false, false, false)
                    .isEmpty());
        const QImage here = edit->slides()->render_preview({}, 0, size);
        REQUIRE_FALSE(here.isNull());
        const auto job = edit->slides()->prepare_preview({}, 0, size);
        REQUIRE(job.still.has_value());
        CHECK(finish_elsewhere(job) == here);
        edit->cancel();
    }
}

// The image is decoded by the paint, so off the GUI thread the worker decodes
// it through the shared preview cache. Nothing has painted it before the
// worker does: its paint is the cold decode, and the GUI thread's the hit.
TEST_CASE("A live library image paints off the GUI thread as it does on it", "[app][previews]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_database(tmp.path / "library.db"));
    const auto images = facade.media_root() / "images";
    std::filesystem::create_directories(images);
    QImage green(64, 64, QImage::Format_RGB32);
    green.fill(QColor(30, 200, 60));
    REQUIRE(green.save(QString::fromStdString((images / "green.png").string())));
    REQUIRE(facade.live()->playImage(QStringLiteral("green.png")));

    const QString out = first_output_id(facade);
    const QSize size(320, 180);
    const auto job = facade.outputPreviews()->prepare_output_preview(out, size);
    REQUIRE(job.still.has_value());
    const QImage there = finish_elsewhere(job);
    REQUIRE_FALSE(there.isNull());
    CHECK(QColor(there.pixel(size.width() / 2, size.height() / 2)).green() > 150);
    CHECK(there == facade.outputPreviews()->render_output_preview(out, size));
}

// A theme preview still painting when the theme library changes must not come
// back into the cache under the old key.
TEST_CASE("A theme preview painted after its cache was cleared is not kept", "[app][previews]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const QString url = text_theme_url(facade, QStringLiteral("Main"));
    const QSize size(160, 90);

    const auto job = facade.themes()->prepare_preview(url, size);
    REQUIRE(job.lay_out);
    facade.themes()->clear_previews();
    REQUIRE_FALSE(finish_elsewhere(job).isNull());
    CHECK(facade.themes()->prepare_preview(url, size).lay_out);  // still a miss
}

// A stale Freeform tile paints on a worker from the document as it was when
// the tile was asked for. A save that lands meanwhile stores the newer tile,
// and the worker's older picture must not replace it.
TEST_CASE("A stale Freeform tile painted across a save does not replace the saved one",
          "[app][previews]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_database(tmp.path / "library.db"));
    const auto root = facade.media_root();

    auto doc = seventhslide::freeform::make_document(
        "Tile", {facade.session().primary_output()->id});
    seventhslide::freeform::Object box;
    box.type = seventhslide::freeform::ObjectType::Text;
    box.w = 800;
    box.h = 200;
    box.text = "Hello";
    doc.decks[0].slides[0].objects.push_back(box);
    seventhslide::model::FreeformItem item;
    item.name = "Tile";
    item.document = doc.to_json();
    const auto id = facade.freeform_repo()->create_item(item);  // no tile on disk yet

    const auto job = facade.freeformLibrary()->prepare_library_thumb(id, QSize(320, 180));
    REQUIRE(job.lay_out);

    // The save, while the worker has not laid it out or painted it yet.
    QImage saved(640, 360, QImage::Format_RGB32);
    saved.fill(QColor(40, 40, 200));
    REQUIRE(seventhslide::app::write_freeform_thumbs(root, id, "saved", "themes", saved));
    const auto saved_stamp = seventhslide::app::freeform_thumb_stamp(root, id);
    REQUIRE(saved_stamp.has_value());

    const QImage shown = finish_elsewhere(job);
    CHECK(seventhslide::app::freeform_thumb_stamp(root, id) == saved_stamp);
    REQUIRE_FALSE(shown.isNull());
    CHECK(QColor(shown.pixel(shown.width() / 2, shown.height() / 2)).blue() > 150);
}

// Several workers laying out and painting at once share the measurer, the
// font faces and the image cache, which lock themselves: each picture is the
// one the GUI thread makes.
TEST_CASE("Previews laid out on several threads at once match the GUI thread's",
          "[app][previews]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_database(tmp.path / "library.db"));
    const QSize size(320, 180);

    auto doc = seventhslide::freeform::make_document(
        "Tile", {facade.session().primary_output()->id});
    seventhslide::freeform::Object box;
    box.type = seventhslide::freeform::ObjectType::Text;
    box.w = 1200;
    box.h = 400;
    box.text = "Amazing grace, how sweet the sound";
    doc.decks[0].slides[0].objects.push_back(box);
    seventhslide::model::FreeformItem item;
    item.name = "Tile";
    item.document = doc.to_json();
    const auto id = facade.freeform_repo()->create_item(item);
    const QString theme = text_theme_url(facade, QStringLiteral("Main"));

    constexpr int k_workers = 8;
    std::vector<PreviewJob> tiles;
    std::vector<PreviewJob> themes;
    for (int i = 0; i < k_workers; ++i) {
        tiles.push_back(facade.freeformLibrary()->prepare_library_thumb(id, size));
        themes.push_back(facade.themes()->prepare_preview(theme, size));
        REQUIRE(tiles.back().lay_out);
        REQUIRE(themes.back().lay_out);
        // Each paints: none reads back the JPEG another stored.
        themes.back().store = {};
    }
    std::vector<QImage> tile_out(k_workers);
    std::vector<QImage> theme_out(k_workers);
    {
        std::vector<std::thread> workers;
        for (int i = 0; i < k_workers; ++i) {
            workers.emplace_back([&, i] {
                theme_out[static_cast<std::size_t>(i)] =
                    seventhslide::app::finish_preview(themes[static_cast<std::size_t>(i)]);
                tile_out[static_cast<std::size_t>(i)] =
                    seventhslide::app::finish_preview(tiles[static_cast<std::size_t>(i)]);
            });
        }
        for (auto& w : workers) {
            w.join();
        }
    }
    facade.themes()->clear_previews();
    const QImage theme_here = facade.themes()->render_preview(theme, size);
    const QImage tile_here = facade.freeformLibrary()->render_freeform_library_thumb(id, size);
    REQUIRE_FALSE(theme_here.isNull());
    REQUIRE_FALSE(tile_here.isNull());
    for (int i = 0; i < k_workers; ++i) {
        INFO("worker " << i);
        CHECK(theme_out[static_cast<std::size_t>(i)] == theme_here);
        CHECK(tile_out[static_cast<std::size_t>(i)] == tile_here);
    }
}

namespace {

[[nodiscard]] std::size_t stored_theme_thumbs(const std::filesystem::path& media_root)
{
    std::size_t count = 0;
    std::error_code ec;
    const auto root = seventhslide::app::media_thumbs_dir(media_root) / "themes";
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        count += it->is_regular_file() ? 1 : 0;
    }
    return count;
}

}  // namespace

// What survives a restart: the memory cache is gone, and the disk serves the
// picture without anything being laid out or painted.
TEST_CASE("A theme thumb painted once comes back from the disk", "[app][previews]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_media_root(tmp.path);
    const QString url = text_theme_url(facade, QStringLiteral("Main"));
    const QSize size(200, 113);

    const QImage painted = finish_elsewhere(facade.themes()->prepare_preview(url, size));
    REQUIRE_FALSE(painted.isNull());
    CHECK(painted.size() == size);
    CHECK(stored_theme_thumbs(tmp.path) == 1);

    // Finishes the job for ``size``; says whether it had to paint.
    const auto painted_for = [&](const QSize& ask) {
        facade.themes()->clear_previews();
        auto job = facade.themes()->prepare_preview(url, ask);
        bool laid_out = false;
        const auto lay_out = job.lay_out;
        REQUIRE(lay_out);
        job.lay_out = [&laid_out, lay_out] {
            laid_out = true;
            return lay_out();
        };
        const QImage out = finish_elsewhere(job);
        REQUIRE_FALSE(out.isNull());
        CHECK(out.size() == ask);
        return laid_out;
    };

    CHECK_FALSE(painted_for(size));
    // A gallery resized within the tier needs nothing painted, or stored.
    CHECK_FALSE(painted_for(QSize(240, 135)));
    CHECK_FALSE(painted_for(QSize(251, 141)));
    CHECK(stored_theme_thumbs(tmp.path) == 1);

    // An edit makes another thumb; the old one is not served.
    const auto theme = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song"))
                           .front()
                           .toMap();
    QVariantMap style = theme.value(QStringLiteral("style")).toMap();
    style.insert(QStringLiteral("balance_wrapped_strength"), 12.0);
    REQUIRE(facade.themes()
                ->update_theme(QStringLiteral("Main"), QStringLiteral("song"),
                         theme.value(QStringLiteral("id")).toString(),
                         theme.value(QStringLiteral("name")).toString(), seventhslide::app::to_json(QVariant(style)), QString(), false)
                .isEmpty());
    CHECK(painted_for(size));
    CHECK(stored_theme_thumbs(tmp.path) == 2);

    // Larger than any tier, it is painted each time and never stored.
    CHECK(painted_for(QSize(960, 540)));
    CHECK(painted_for(QSize(960, 540)));
    CHECK(stored_theme_thumbs(tmp.path) == 2);
}

// A thumb outlives the session, so it must not be painted from a decode that
// has gone stale: the shared cache knows a picture only by its URL.
TEST_CASE("A stored theme thumb shows the picture as it is now", "[app][previews]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::test::TempDir tmp;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_media_root(tmp.path);
    const auto folder = tmp.path / "theme_backgrounds";
    std::filesystem::create_directories(folder);
    const auto save = [](const std::filesystem::path& file, const QColor& color, int side) {
        QImage img(side, side, QImage::Format_RGB32);
        img.fill(color);
        REQUIRE(img.save(QString::fromStdString(file.string())));
    };
    const auto created = facade.themes()->create(QStringLiteral("Main"), QStringLiteral("bg"),
                                                 QStringLiteral("Pictured"));
    const auto id = created.value(QStringLiteral("id")).toString();
    const auto set_picture = [&](const QString& url) {
        QVariantMap style = created.value(QStringLiteral("style")).toMap();
        style.insert(QStringLiteral("background_type"), QStringLiteral("image"));
        style.insert(QStringLiteral("background_image"), url);
        REQUIRE(facade.themes()
                    ->update_theme(QStringLiteral("Main"), QStringLiteral("bg"), id,
                             QStringLiteral("Pictured"), seventhslide::app::to_json(QVariant(style)), QString(), false)
                    .isEmpty());
        return facade.themes()
            ->theme_map(QStringLiteral("Main"), QStringLiteral("bg"), id)
            .value(QStringLiteral("style"))
            .toMap()
            .value(QStringLiteral("background_image"))
            .toString();
    };
    const QString thumb = QStringLiteral("theme/Main/bg/") + id + QStringLiteral("?c=bg");
    const QSize size(320, 180);
    const auto centre = [&] {
        facade.themes()->clear_previews();
        const QImage img = finish_elsewhere(facade.themes()->prepare_preview(thumb, size));
        REQUIRE_FALSE(img.isNull());
        return QColor(img.pixel(size.width() / 2, size.height() / 2));
    };
    // What a painter with only the shared cache would be holding on to.
    const auto shared_decode_of = [&](const QString& url) {
        (void)facade.previews().image_cache()->get(url.toStdString());
    };

    SECTION("replaced under the same name")
    {
        save(folder / "first.png", QColor(220, 30, 30), 64);
        const QString url = set_picture(QStringLiteral("/static/theme_backgrounds/first.png"));
        shared_decode_of(url);
        CHECK(centre().red() > 150);

        const auto file = tmp.path / std::filesystem::path(url.toStdString()).relative_path()
                                         .lexically_relative("static");
        save(file, QColor(30, 30, 220), 128);
        const QColor now = centre();
        CHECK(now.blue() > 150);
        CHECK(now.red() < 100);
    }

    SECTION("arriving after the theme")
    {
        const QString url = set_picture(QStringLiteral("/static/theme_backgrounds/late.png"));
        shared_decode_of(url);  // remembered as missing
        CHECK(centre().green() < 150);

        save(folder / "late.png", QColor(30, 220, 30), 64);
        CHECK(centre().green() > 150);
    }
}

TEST_CASE("A theme thumb's clock shows a set time", "[app][previews]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto job = facade.themes()->prepare_preview(
        text_theme_url(facade, QStringLiteral("Main")), QSize(160, 90));
    REQUIRE(job.lay_out);
    const auto still = job.lay_out();
    REQUIRE(still.has_value());
    CHECK(still->now == seventhslide::app::theme_thumb_clock_time());
}

TEST_CASE("A service's thumbnail is its first slide, kept until what it is drawn from changes",
          "[app][previews][services]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* library = facade.library();
    auto* thumbs = facade.serviceThumbs();
    const QSize size(128, 72);
    const auto key_of = [](const QString& url) {
        return QUrlQuery(QUrl(url)).queryItemValue(QStringLiteral("k"));
    };

    // A service with no row that shows a still has none.
    const qint64 service = library->createService(QStringLiteral("Vespers"));
    REQUIRE(service > 0);
    CHECK(thumbs->thumbUrl(service).isEmpty());
    REQUIRE(library->addDividerToService(QStringLiteral("Welcome")));
    CHECK(thumbs->thumbUrl(service).isEmpty());
    CHECK_FALSE(thumbs->prepare(service, size).lay_out);
    CHECK(thumbs->thumbUrl(-1).isEmpty());

    // A song after the divider: its first slide, in the output's shape,
    // drawn away from the GUI thread and kept under the URL's key.
    auto song = demo_song();
    const auto song_id = library->song_repo()->insert(song);
    REQUIRE(library->addSongToService(song_id));
    const QString url = thumbs->thumbUrl(service);
    REQUIRE(url.startsWith(QStringLiteral("image://servicethumb/")));
    const QString key = key_of(url);
    REQUIRE_FALSE(key.isEmpty());
    CHECK(thumbs->cached(key, size).isNull());
    const auto job = thumbs->prepare(service, size);
    REQUIRE(job.lay_out);
    CHECK_FALSE(job.still.has_value());  // laid out by the worker
    const QImage drawn = finish_elsewhere(job);
    REQUIRE_FALSE(drawn.isNull());
    CHECK(drawn.size() == QSize(128, 72));
    bool varied = false;
    for (int y = 0; y < drawn.height() && !varied; ++y) {
        for (int x = 0; x < drawn.width() && !varied; ++x) {
            varied = drawn.pixel(x, y) != drawn.pixel(0, 0);
        }
    }
    CHECK(varied);  // the lyrics, not an empty plate
    CHECK(thumbs->cached(key, size) == drawn);

    // A new name draws the same; a new row, or an edited theme, does not.
    REQUIRE(library->renameService(service, QStringLiteral("Evening Vespers")).isEmpty());
    CHECK(thumbs->thumbUrl(service) == url);
    REQUIRE(library->addSongToService(song_id, 0));
    const QString moved = thumbs->thumbUrl(service);
    CHECK(key_of(moved) != key);
    facade.previews().bump_theme_revision();
    CHECK(key_of(thumbs->thumbUrl(service)) != key_of(moved));
}
