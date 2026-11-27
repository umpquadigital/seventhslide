// On-disk thumbnail caches for library images and Freeform decks.
//
// Both are persistent caches, so the interesting behaviour is not "does it make
// a JPEG" but "does it know when the one on disk is stale". They answer that
// question two different ways - media compares mtimes against the source, while
// freeform writes a stamp of the preview key and theme revision - and a cache
// that wrongly reports fresh serves the operator a picture of the old slide
// until someone clears the folder by hand.

#include "file_time_string.hpp"
#include "test_qt_app.hpp"

#include <catch2/catch_test_macros.hpp>

#include "test_temp_dir.hpp"

#include "freeform_thumb_cache.hpp"
#include "freeform_thumb_provider.hpp"
#include "media_thumb_cache.hpp"
#include "theme_thumb_cache.hpp"
#include "video_frame.hpp"

#include <QColor>
#include <QImage>
#include <QString>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using namespace seventhslide::app;

using seventhslide::test::TempDir;

namespace {


/// A real on-disk PNG, since the cache decodes through QImageReader.
fs::path write_source_image(const fs::path& dir, const std::string& name, int w, int h)
{
    seventhslide::test::ensure_qt_application();
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(QColor(20, 90, 160));
    const fs::path out = dir / name;
    fs::create_directories(dir);
    REQUIRE(img.save(QString::fromStdString(out.string()), "PNG"));
    return out;
}

/// Push a file's mtime into the past so freshness comparisons are decidable.
void age_by(const fs::path& p, std::chrono::seconds seconds)
{
    std::error_code ec;
    const auto now = fs::last_write_time(p, ec);
    REQUIRE_FALSE(ec);
    fs::last_write_time(p, now - seconds, ec);
    REQUIRE_FALSE(ec);
}

} // namespace

// --- media tiers ---------------------------------------------------------

TEST_CASE("media_thumb_tier_for_request picks the smallest covering tier", "[app][thumbs]")
{
    // Serving a 512px file to a 64px grid cell is the waste this exists to
    // avoid; serving a 160px file to a 500px cell is the blur.
    REQUIRE(media_thumb_tier_for_request(1, 1) == k_media_thumb_tier_sm);
    REQUIRE(media_thumb_tier_for_request(160, 160) == k_media_thumb_tier_sm);
    REQUIRE(media_thumb_tier_for_request(161, 100) == k_media_thumb_tier_md);
    REQUIRE(media_thumb_tier_for_request(320, 320) == k_media_thumb_tier_md);
    REQUIRE(media_thumb_tier_for_request(321, 1) == k_media_thumb_tier_lg);
    REQUIRE(media_thumb_tier_for_request(512, 512) == k_media_thumb_tier_lg);

    // Past the largest tier there is nothing bigger to choose.
    REQUIRE(media_thumb_tier_for_request(4096, 4096) == k_media_thumb_tier_lg);

    // The longest edge decides, whichever axis it is on.
    REQUIRE(media_thumb_tier_for_request(500, 10) == k_media_thumb_tier_lg);
    REQUIRE(media_thumb_tier_for_request(10, 500) == k_media_thumb_tier_lg);

    // Degenerate requests floor at the smallest tier rather than returning 0.
    REQUIRE(media_thumb_tier_for_request(0, 0) == k_media_thumb_tier_sm);
    REQUIRE(media_thumb_tier_for_request(-100, -100) == k_media_thumb_tier_sm);
}

TEST_CASE("media_thumb_path lays out thumbs by tier and stem", "[app][thumbs]")
{
    const fs::path root = "/media";

    REQUIRE(media_thumb_path(root, "photo.png", 320)
            == root / "thumbs" / "320" / "photo.jpg");
    // The extension is always .jpg regardless of the source format.
    REQUIRE(media_thumb_path(root, "photo.JPEG", 160)
            == root / "thumbs" / "160" / "photo.jpg");
    REQUIRE(media_thumb_path(root, "two.dots.png", 512)
            == root / "thumbs" / "512" / "two.dots.jpg");

    // No media folder means no cache location, not a path relative to the cwd.
    REQUIRE(media_thumb_path({}, "photo.png", 320).empty());

    // A filename that is only dots has no usable stem.
    REQUIRE(media_thumb_path(root, ".", 320).empty());
    REQUIRE(media_thumb_path(root, "..", 320).empty());
}

TEST_CASE("media_thumb_path keeps a directory-laden name inside the cache", "[app][thumbs]")
{
    // Filenames arrive from the database and, through the remote's thumb route,
    // from a URL. Only the leaf's stem is used, so a name carrying separators
    // cannot walk the thumb write out of the media folder.
    const fs::path root = "/media";
    const fs::path expected = root / "thumbs" / "320" / "passwd.jpg";

    REQUIRE(media_thumb_path(root, "../../etc/passwd", 320) == expected);
    REQUIRE(media_thumb_path(root, "/etc/passwd", 320) == expected);
    REQUIRE(media_thumb_path(root, "a/b/c/passwd.png", 320) == expected);
}

// --- media generation and staleness --------------------------------------

TEST_CASE("ensure_media_thumb writes, reuses, and regenerates", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";
    const fs::path source = write_source_image(tmp.path / "media", "pic.png", 800, 600);

    const fs::path dest = media_thumb_path(root, "pic.png", k_media_thumb_tier_md);
    REQUIRE_FALSE(fs::exists(dest));

    QString error = QStringLiteral("untouched");
    REQUIRE(ensure_media_thumb(root, source, "pic.png", k_media_thumb_tier_md, &error));
    REQUIRE(fs::is_regular_file(dest));

    // The thumb is a real JPEG, scaled so its longest edge fits the tier.
    QImage written(QString::fromStdString(dest.string()));
    REQUIRE_FALSE(written.isNull());
    REQUIRE(std::max(written.width(), written.height()) <= k_media_thumb_tier_md);
    // 800x600 at 320 -> 320x240: aspect is preserved, not letterboxed.
    REQUIRE(written.width() == k_media_thumb_tier_md);
    REQUIRE(written.height() == 240);

    // A second call with an up-to-date thumb must not rewrite the file.
    const auto stamp = fs::last_write_time(dest);
    REQUIRE(ensure_media_thumb(root, source, "pic.png", k_media_thumb_tier_md));
    REQUIRE(fs::last_write_time(dest) == stamp);

    // Re-importing an image under the same name makes the source newer than the
    // thumb. This is the case that decides whether the operator sees the new
    // picture or the old one.
    age_by(dest, std::chrono::seconds(120));
    const auto aged = fs::last_write_time(dest);
    REQUIRE(aged < fs::last_write_time(source));

    REQUIRE(ensure_media_thumb(root, source, "pic.png", k_media_thumb_tier_md));
    // The thumb was rewritten, and is once again at least as new as its source.
    REQUIRE(fs::last_write_time(dest) > aged);
    REQUIRE(fs::last_write_time(dest) >= fs::last_write_time(source));
}

TEST_CASE("ensure_media_thumb refuses bad input with a message", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";
    const fs::path source = write_source_image(tmp.path / "media", "pic.png", 40, 40);

    QString error;
    REQUIRE_FALSE(ensure_media_thumb({}, source, "pic.png", 320, &error));
    REQUIRE_FALSE(error.isEmpty());

    error.clear();
    REQUIRE_FALSE(ensure_media_thumb(root, source, "pic.png", 0, &error));
    REQUIRE_FALSE(error.isEmpty());

    // A row pointing at a file that has since been deleted must fail cleanly
    // rather than write a zero-byte thumb that then looks fresh forever.
    error.clear();
    REQUIRE_FALSE(
        ensure_media_thumb(root, tmp.path / "gone.png", "gone.png", 320, &error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(fs::exists(media_thumb_path(root, "gone.png", 320)));

    // The error pointer is optional.
    REQUIRE_FALSE(ensure_media_thumb(root, tmp.path / "gone.png", "gone.png", 320));
}

TEST_CASE("ensure_media_thumbs_all_tiers writes every tier once", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";
    const fs::path source = write_source_image(tmp.path / "media", "wide.png", 1200, 400);

    REQUIRE(ensure_media_thumbs_all_tiers(root, source, "wide.png"));

    for (const int tier :
         {k_media_thumb_tier_sm, k_media_thumb_tier_md, k_media_thumb_tier_lg}) {
        INFO("tier " << tier);
        const fs::path dest = media_thumb_path(root, "wide.png", tier);
        REQUIRE(fs::is_regular_file(dest));
        QImage img(QString::fromStdString(dest.string()));
        REQUIRE_FALSE(img.isNull());
        REQUIRE(std::max(img.width(), img.height()) <= tier);
    }

    // All fresh: the fast path leaves the files alone.
    const auto stamp = fs::last_write_time(media_thumb_path(root, "wide.png",
                                                            k_media_thumb_tier_sm));
    REQUIRE(ensure_media_thumbs_all_tiers(root, source, "wide.png"));
    REQUIRE(fs::last_write_time(media_thumb_path(root, "wide.png",
                                                 k_media_thumb_tier_sm))
            == stamp);

    // A partial cache is repaired rather than left half-written.
    fs::remove(media_thumb_path(root, "wide.png", k_media_thumb_tier_md));
    REQUIRE(ensure_media_thumbs_all_tiers(root, source, "wide.png"));
    REQUIRE(fs::is_regular_file(media_thumb_path(root, "wide.png",
                                                 k_media_thumb_tier_md)));
}

TEST_CASE("remove_media_thumbs clears every tier and tolerates absence", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";
    const fs::path source = write_source_image(tmp.path / "media", "gone.png", 200, 200);

    REQUIRE(ensure_media_thumbs_all_tiers(root, source, "gone.png"));
    remove_media_thumbs(root, "gone.png");

    for (const int tier :
         {k_media_thumb_tier_sm, k_media_thumb_tier_md, k_media_thumb_tier_lg}) {
        INFO("tier " << tier);
        REQUIRE_FALSE(fs::exists(media_thumb_path(root, "gone.png", tier)));
    }

    // Deleting an image whose thumbs were never generated is not an error.
    REQUIRE_NOTHROW(remove_media_thumbs(root, "never-existed.png"));
    REQUIRE_NOTHROW(remove_media_thumbs({}, "gone.png"));
}

// --- video thumbs --------------------------------------------------------

namespace {

/// One of the solid #3366cc 64x36 test videos.
fs::path fixture_video(const std::string& name)
{
    return fs::path(SEVENTHSLIDE_FIXTURES_DIR) / "video" / name;
}

/// ``c`` within JPEG / YUV rounding of #3366cc.
bool near_fixture_blue(const QColor& c)
{
    return std::abs(c.red() - 0x33) <= 12 && std::abs(c.green() - 0x66) <= 12
           && std::abs(c.blue() - 0xcc) <= 12;
}

} // namespace

TEST_CASE("grab_video_frame decodes a frame at its display size", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();

    // Already smaller than the edge asked for: not scaled up.
    const QImage full = grab_video_frame(fixture_video("solid_3366cc_64x36_20s.mp4"), 512);
    REQUIRE_FALSE(full.isNull());
    REQUIRE(full.size() == QSize(64, 36));
    REQUIRE(near_fixture_blue(full.pixelColor(32, 18)));

    // Scaled so the longest edge fits, shape kept.
    const QImage small = grab_video_frame(fixture_video("solid_3366cc_64x36.mp4"), 32);
    REQUIRE(small.size() == QSize(32, 18));
    REQUIRE(near_fixture_blue(small.pixelColor(16, 9)));
}

TEST_CASE("grab_video_frame is null for a file with no picture", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    REQUIRE(grab_video_frame(tmp.path / "missing.mp4", 512).isNull());

    {
        std::ofstream junk(tmp.path / "junk.mp4", std::ios::binary);
        junk << "not a video";
    }
    REQUIRE(grab_video_frame(tmp.path / "junk.mp4", 512).isNull());
    REQUIRE(grab_video_frame(fixture_video("solid_3366cc_64x36.mp4"), 0).isNull());
}

TEST_CASE("video_thumb_path keeps the whole filename apart from image thumbs", "[app][thumbs]")
{
    const fs::path root = "/media";
    REQUIRE(video_thumb_path(root, "clip.mp4", 320)
            == root / "thumbs" / "videos" / "320" / "clip.mp4.jpg");
    // clip.mp4, clip.mov and an image clip.png each have their own.
    REQUIRE(video_thumb_path(root, "clip.mov", 320) != video_thumb_path(root, "clip.mp4", 320));
    REQUIRE(video_thumb_path(root, "clip.mp4", 320) != media_thumb_path(root, "clip.png", 320));
    // Only the leaf, so a name with separators stays inside the cache.
    REQUIRE(video_thumb_path(root, "../../etc/passwd", 320)
            == root / "thumbs" / "videos" / "320" / "passwd.jpg");

    REQUIRE(video_thumb_path({}, "clip.mp4", 320).empty());
    REQUIRE(video_thumb_path(root, "..", 320).empty());
}

TEST_CASE("ensure_video_thumbs writes every tier from one frame, then removes them",
          "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";
    const fs::path source = root / "videos" / "clip.mp4";
    fs::create_directories(source.parent_path());
    fs::copy_file(fixture_video("solid_3366cc_64x36_20s.mp4"), source);

    QString error;
    REQUIRE(ensure_video_thumbs(root, source, "clip.mp4", &error));
    for (const int tier : k_media_thumb_tiers) {
        INFO("tier " << tier);
        const QImage img(QString::fromStdString(video_thumb_path(root, "clip.mp4", tier).string()));
        REQUIRE_FALSE(img.isNull());
        REQUIRE(near_fixture_blue(img.pixelColor(img.width() / 2, img.height() / 2)));
    }

    // Fresh: left alone.
    const fs::path sm = video_thumb_path(root, "clip.mp4", k_media_thumb_tier_sm);
    const auto stamp = fs::last_write_time(sm);
    REQUIRE(ensure_video_thumbs(root, source, "clip.mp4"));
    REQUIRE(fs::last_write_time(sm) == stamp);

    // The video replaced: the thumbs are made again.
    age_by(sm, std::chrono::seconds(120));
    const auto aged = fs::last_write_time(sm);
    REQUIRE(ensure_video_thumbs(root, source, "clip.mp4"));
    REQUIRE(fs::last_write_time(sm) > aged);

    // Removing a video's thumbs leaves an image's of the same stem alone.
    const fs::path pic = write_source_image(root / "images", "clip.png", 80, 60);
    REQUIRE(ensure_media_thumbs_all_tiers(root, pic, "clip.png"));
    remove_video_thumbs(root, "clip.mp4");
    for (const int tier : k_media_thumb_tiers) {
        INFO("tier " << tier);
        REQUIRE_FALSE(fs::exists(video_thumb_path(root, "clip.mp4", tier)));
        REQUIRE(fs::exists(media_thumb_path(root, "clip.png", tier)));
    }
}

TEST_CASE("ensure_video_thumbs fails cleanly for a file with no picture", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";
    const fs::path source = root / "videos" / "junk.mp4";
    fs::create_directories(source.parent_path());
    {
        std::ofstream junk(source, std::ios::binary);
        junk << "not a video";
    }
    QString error;
    REQUIRE_FALSE(ensure_video_thumbs(root, source, "junk.mp4", &error));
    REQUIRE_FALSE(error.isEmpty());
    // Nothing written that could later look fresh.
    REQUIRE_FALSE(fs::exists(video_thumb_path(root, "junk.mp4", k_media_thumb_tier_sm)));
    REQUIRE_FALSE(ensure_video_thumbs(root, root / "videos" / "gone.mp4", "gone.mp4"));
}

// --- freeform stamps -----------------------------------------------------

TEST_CASE("freeform_thumb_path is keyed by id and tier", "[app][thumbs]")
{
    const fs::path root = "/media";
    REQUIRE(freeform_thumb_path(root, 42, 320)
            == root / "thumbs" / "freeform" / "320" / "42.jpg");

    REQUIRE(freeform_thumb_path({}, 42, 320).empty());
    REQUIRE(freeform_thumb_path(root, -1, 320).empty());
    REQUIRE(freeform_thumb_path(root, 42, 0).empty());
}

TEST_CASE("freeform thumbs are stamped by preview key and theme revision", "[app][thumbs]")
{
    // Freeform slides are rendered, not read from disk, so there is no source
    // mtime to compare. The stamp is the whole freshness mechanism: edit the
    // deck (new preview key) or restyle the theme (new revision) and every tier
    // must be considered stale.
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";

    QImage rendered(640, 360, QImage::Format_RGB32);
    rendered.fill(QColor(200, 40, 40));

    REQUIRE_FALSE(freeform_thumb_fresh(root, 7, "key-a", "t1"));

    QString error;
    REQUIRE(write_freeform_thumbs(root, 7, "key-a", "t1", rendered, &error));
    REQUIRE(freeform_thumb_fresh(root, 7, "key-a", "t1"));

    // A different deck revision invalidates.
    REQUIRE_FALSE(freeform_thumb_fresh(root, 7, "key-b", "t1"));
    // So does a theme change under the same deck.
    REQUIRE_FALSE(freeform_thumb_fresh(root, 7, "key-a", "t2"));
    // And another item's id is simply not cached.
    REQUIRE_FALSE(freeform_thumb_fresh(root, 8, "key-a", "t1"));

    for (const int tier :
         {k_media_thumb_tier_sm, k_media_thumb_tier_md, k_media_thumb_tier_lg}) {
        INFO("tier " << tier);
        REQUIRE(fs::is_regular_file(freeform_thumb_path(root, 7, tier)));
        const QImage loaded = load_freeform_thumb(root, 7, tier);
        REQUIRE_FALSE(loaded.isNull());
        REQUIRE(std::max(loaded.width(), loaded.height()) <= tier);
    }

    // A missing tier makes the whole entry stale, even with a matching stamp -
    // otherwise the gallery would ask for a file that is not there.
    fs::remove(freeform_thumb_path(root, 7, k_media_thumb_tier_md));
    REQUIRE_FALSE(freeform_thumb_fresh(root, 7, "key-a", "t1"));

    REQUIRE(write_freeform_thumbs(root, 7, "key-a", "t1", rendered));
    REQUIRE(freeform_thumb_fresh(root, 7, "key-a", "t1"));

    // A tier that cannot be written leaves no stamp: the old one would call
    // the stale or missing tier current.
    const auto md = freeform_thumb_path(root, 7, k_media_thumb_tier_md);
    fs::remove(md);
    fs::create_directories(md);
    REQUIRE_FALSE(write_freeform_thumbs(root, 7, "key-a", "t1", rendered));
    fs::remove(md);
    REQUIRE(write_freeform_thumbs(root, 7, "key-c", "t1", rendered));
    REQUIRE(freeform_thumb_fresh(root, 7, "key-c", "t1"));
    fs::remove(md);
    fs::create_directories(md);
    REQUIRE_FALSE(write_freeform_thumbs(root, 7, "key-c", "t1", rendered));
    fs::remove(md);
    REQUIRE(write_thumb_jpeg(rendered, k_media_thumb_tier_md, md));
    REQUIRE_FALSE(freeform_thumb_fresh(root, 7, "key-c", "t1"));

    remove_freeform_thumbs(root, 7);
    REQUIRE_FALSE(freeform_thumb_fresh(root, 7, "key-a", "t1"));
    REQUIRE_NOTHROW(remove_freeform_thumbs(root, 999));
}

TEST_CASE("A tile painted from what the disk held is stored only while it still holds it",
          "[app][thumbs]")
{
    // A worker paints a stale tile from a document read on the GUI thread. If
    // anything stored a tile in between, the worker's picture is the older one
    // (or a duplicate) and must not replace it.
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";
    QImage old_pic(640, 360, QImage::Format_RGB32);
    old_pic.fill(QColor(200, 40, 40));
    QImage new_pic(640, 360, QImage::Format_RGB32);
    new_pic.fill(QColor(40, 40, 200));

    SECTION("nothing on disk, nothing stored since: stored")
    {
        const auto seen = freeform_thumb_stamp(root, 7);
        CHECK_FALSE(seen.has_value());
        CHECK(write_freeform_thumbs_unless_changed(root, 7, seen, "key-a", "t1", old_pic));
        CHECK(freeform_thumb_fresh(root, 7, "key-a", "t1"));
    }

    SECTION("a stale stamp, unchanged since: replaced")
    {
        REQUIRE(write_freeform_thumbs(root, 7, "key-a", "t1", old_pic));
        const auto seen = freeform_thumb_stamp(root, 7);
        REQUIRE(seen.has_value());
        CHECK(write_freeform_thumbs_unless_changed(root, 7, seen, "key-b", "t1", new_pic));
        CHECK(freeform_thumb_fresh(root, 7, "key-b", "t1"));
    }

    SECTION("a save stored a newer tile since: kept")
    {
        REQUIRE(write_freeform_thumbs(root, 7, "key-a", "t1", old_pic));
        const auto seen = freeform_thumb_stamp(root, 7);
        REQUIRE(write_freeform_thumbs(root, 7, "key-b", "t1", new_pic));  // the save
        CHECK_FALSE(write_freeform_thumbs_unless_changed(root, 7, seen, "key-a", "t1", old_pic));
        CHECK(freeform_thumb_fresh(root, 7, "key-b", "t1"));
        const QImage kept = load_freeform_thumb(root, 7, k_media_thumb_tier_lg);
        REQUIRE_FALSE(kept.isNull());
        CHECK(QColor(kept.pixel(kept.width() / 2, kept.height() / 2)).blue() > 150);
    }

    SECTION("the item was removed since: nothing comes back")
    {
        REQUIRE(write_freeform_thumbs(root, 7, "key-a", "t1", old_pic));
        const auto seen = freeform_thumb_stamp(root, 7);
        remove_freeform_thumbs(root, 7);
        CHECK_FALSE(write_freeform_thumbs_unless_changed(root, 7, seen, "key-b", "t1", new_pic));
        CHECK_FALSE(fs::exists(freeform_thumb_path(root, 7, k_media_thumb_tier_lg)));
    }
}

TEST_CASE("write_freeform_thumbs refuses a null image", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const fs::path root = tmp.path / "media";

    QString error;
    REQUIRE_FALSE(write_freeform_thumbs(root, 1, "k", "t0", QImage(), &error));
    REQUIRE_FALSE(error.isEmpty());

    // A failed render must leave no stamp behind, or the empty result would be
    // treated as cached.
    REQUIRE_FALSE(freeform_thumb_fresh(root, 1, "k", "t0"));

    QImage ok(10, 10, QImage::Format_RGB32);
    ok.fill(Qt::white);
    REQUIRE_FALSE(write_freeform_thumbs(root, -1, "k", "t0", ok));
    REQUIRE_FALSE(write_freeform_thumbs({}, 1, "k", "t0", ok));
}

TEST_CASE("load_freeform_thumb answers null for a miss", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    REQUIRE(load_freeform_thumb(tmp.path / "media", 1234, 320).isNull());
    REQUIRE(load_freeform_thumb({}, 1234, 320).isNull());
}

TEST_CASE("The Freeform thumb provider hands on the URL's stamp key and themes",
          "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    qint64 got_id = -1;
    QString got_key;
    QString got_themes;
    FreeformThumbProvider provider(
        [&](qint64 id, const QSize& size, const QString& key, const QString& themes) {
            got_id = id;
            got_key = key;
            got_themes = themes;
            QImage img(size, QImage::Format_RGB32);
            img.fill(Qt::white);
            return img;
        });
    QSize size;
    const QImage img =
        provider.requestImage(QStringLiteral("42?k=deck%401920x1080&t=0a1b"), &size, QSize(64, 36));
    REQUIRE_FALSE(img.isNull());
    CHECK(got_id == 42);
    CHECK(got_key == QStringLiteral("deck@1920x1080"));
    CHECK(got_themes == QStringLiteral("0a1b"));
}

namespace {

ThemeThumbView thumb_view(int width = 320)
{
    return {.context = 0,
            .canvas_width = 1920,
            .canvas_height = 1080,
            .output_name = "Main",
            .size = QSize(width, width * 9 / 16)};
}

}  // namespace

TEST_CASE("theme_thumb_identity changes with whatever the thumb shows", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const nlohmann::json style = {{"background_color", "#112233"}};
    const auto base = theme_thumb_identity(style, thumb_view(), tmp.path);
    REQUIRE_FALSE(base.empty());

    SECTION("the same theme asked for again names the same thumb")
    {
        CHECK(theme_thumb_identity(style, thumb_view(), tmp.path) == base);
    }

    SECTION("its style")
    {
        const nlohmann::json edited = {{"background_color", "#445566"}};
        CHECK(theme_thumb_identity(edited, thumb_view(), tmp.path) != base);
    }

    SECTION("its size and canvas")
    {
        CHECK(theme_thumb_identity(style, thumb_view(160), tmp.path) != base);
        auto portrait = thumb_view();
        portrait.canvas_width = 1080;
        portrait.canvas_height = 1920;
        CHECK(theme_thumb_identity(style, portrait, tmp.path) != base);
    }

    SECTION("a picture replaced under the same name")
    {
        const auto picture = tmp.path / "theme_backgrounds" / "a.jpg";
        fs::create_directories(picture.parent_path());
        std::ofstream(picture) << "first";
        const nlohmann::json pictured = {{"background_image", "/static/theme_backgrounds/a.jpg"}};
        const auto first = theme_thumb_identity(pictured, thumb_view(), tmp.path);
        std::ofstream(picture) << "a longer second";
        CHECK(theme_thumb_identity(pictured, thumb_view(), tmp.path) != first);
    }
}

TEST_CASE("theme_thumb_stored_size paints a request at its tier, keeping its shape",
          "[app][thumbs]")
{
    CHECK(theme_thumb_stored_size(QSize(160, 90)) == QSize(160, 90));
    CHECK(theme_thumb_stored_size(QSize(200, 113)) == QSize(320, 180));
    CHECK(theme_thumb_stored_size(QSize(400, 225)) == QSize(512, 288));
    // Another shape keeps its own.
    CHECK(theme_thumb_stored_size(QSize(200, 200)) == QSize(320, 320));
    // Every width a gallery card can take, its height rounded, is one thumb per
    // tier: resizing the window paints nothing new.
    for (int width = 161; width <= 512; ++width) {
        const int height = static_cast<int>(std::lround(width * 9.0 / 16.0));
        INFO(width << "x" << height);
        CHECK(theme_thumb_stored_size(QSize(width, height))
              == (width <= 320 ? QSize(320, 180) : QSize(512, 288)));
    }
    // The Freeform editor's backdrop is too large to keep.
    CHECK_FALSE(theme_thumb_stored_size(QSize(960, 540)).has_value());
    CHECK_FALSE(theme_thumb_stored_size(QSize()).has_value());
}

TEST_CASE("A stored theme thumb loads at the size asked for", "[app][thumbs]")
{
    seventhslide::test::ensure_qt_application();
    TempDir tmp;
    const auto path = theme_thumb_path(tmp.path, "abc", QSize(320, 180));
    CHECK(path == media_thumbs_dir(tmp.path) / "themes" / "320" / "abc.jpg");
    CHECK(load_theme_thumb(path, QSize(200, 113)).isNull());

    QImage img(320, 180, QImage::Format_RGB32);
    img.fill(QColor(20, 90, 160));
    REQUIRE(write_thumb_jpeg(img, 320, path));
    CHECK(load_theme_thumb(path, QSize(200, 113)).size() == QSize(200, 113));
    CHECK(load_theme_thumb(path, QSize(320, 180)).size() == QSize(320, 180));

    // A hit counts as use, so pruning spares it.
    const auto long_ago = fs::file_time_type::clock::now() - std::chrono::hours(24 * 365);
    fs::last_write_time(path, long_ago);
    REQUIRE_FALSE(load_theme_thumb(path, QSize(320, 180)).isNull());
    CHECK(fs::last_write_time(path) > long_ago);

    // A file that cannot be read is a miss, so the thumb is painted again.
    std::ofstream(path) << "not a jpeg";
    CHECK(load_theme_thumb(path, QSize(200, 113)).isNull());
}

TEST_CASE("prune_theme_thumbs keeps the most recently written", "[app][thumbs]")
{
    TempDir tmp;
    CHECK_NOTHROW(prune_theme_thumbs(tmp.path, 1));  // nothing there yet

    const auto dir = media_thumbs_dir(tmp.path) / "themes" / "320";
    fs::create_directories(dir);
    const auto now = fs::file_time_type::clock::now();
    for (int i = 0; i < 4; ++i) {
        const auto file = dir / (std::to_string(i) + ".jpg");
        std::ofstream(file) << "x";
        fs::last_write_time(file, now - std::chrono::hours(i));
    }

    prune_theme_thumbs(tmp.path, 4);
    CHECK(fs::exists(dir / "3.jpg"));

    prune_theme_thumbs(tmp.path, 2);
    CHECK(fs::exists(dir / "0.jpg"));
    CHECK(fs::exists(dir / "1.jpg"));
    CHECK_FALSE(fs::exists(dir / "2.jpg"));
    CHECK_FALSE(fs::exists(dir / "3.jpg"));
}

TEST_CASE("A thumb's picture is decoded just large enough to cover it", "[app][thumbs]")
{
    TempDir tmp;
    const QSize paint(320, 180);
    const auto decoded = [&](int w, int h) {
        const auto file = write_source_image(tmp.path, "picture.png", w, h);
        return decode_theme_thumb_picture(file, paint).size();
    };
    CHECK(decoded(5184, 3456) == QSize(320, 213));
    // Shapes far from the box's still cover it, so nothing is stretched.
    CHECK(decoded(1000, 4000) == QSize(320, 1280));
    CHECK(decoded(5000, 1000) == QSize(900, 180));
    // Never enlarged.
    CHECK(decoded(64, 64) == QSize(64, 64));
    CHECK(decode_theme_thumb_picture(tmp.path / "gone.png", paint).isNull());
}
