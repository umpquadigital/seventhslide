// Theme background files are copied off the GUI thread.

#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "refusals.hpp"
#include "media_controller.hpp"
#include "qvariant_json.hpp"
#include "theme_controller.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QUrl>
#include <QVariantMap>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using seventhslide::app::OutputSettingsController;
using seventhslide::app::SessionFacade;
using seventhslide::app::ThemeController;
namespace fs = std::filesystem;
namespace refusal = seventhslide::app::refusal;

namespace {

const QString k_output = QStringLiteral("Main");
const QString k_bg = QStringLiteral("bg");

void write_bytes(const fs::path& path, std::size_t size)
{
    std::ofstream out(path, std::ios::binary);
    const std::string block(64u * 1024u, 'b');
    for (std::size_t done = 0; done < size; done += block.size()) {
        out.write(block.data(),
                  static_cast<std::streamsize>(std::min(block.size(), size - done)));
    }
}

QString file_url(const fs::path& path)
{
    return QUrl::fromLocalFile(QString::fromStdString(path.string())).toString();
}

QString first_bg_theme(ThemeController& themes)
{
    const auto list = themes.list(k_output, k_bg);
    REQUIRE_FALSE(list.isEmpty());
    return list.front().toMap().value(QStringLiteral("id")).toString();
}

QString bg_image(ThemeController& themes, const QString& theme_id, const QString& output = k_output)
{
    return themes.theme_map(output, k_bg, theme_id)
        .value(QStringLiteral("style"))
        .toMap()
        .value(QStringLiteral("background_image"))
        .toString();
}

/// Run the event loop until ``request_id`` reports; its error.
QString wait_for_upload(ThemeController& themes, qint64 request_id)
{
    QString error;
    bool done = false;
    const auto connection = QObject::connect(
        &themes, &ThemeController::assetUploadFinished,
        [&](qint64 id, const QString& e) {
            if (id == request_id) {
                error = e;
                done = true;
            }
        });
    (void)QTest::qWaitFor([&] { return done; }, 30000);
    QObject::disconnect(connection);
    REQUIRE(done);
    return error;
}

std::vector<fs::path> files_in(const fs::path& dir)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        out.push_back(entry.path());
    }
    return out;
}

fs::path owned_file(const fs::path& media_root, const QString& url)
{
    return seventhslide::theme::theme_assets_dir(media_root)
           / fs::path(url.toStdString()).filename();
}

/// Run ``sql`` on the library at ``path`` through a connection of its own.
void run_sql(const fs::path& path, const QString& sql)
{
    const auto name = QStringLiteral("theme-assets-sql");
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(QString::fromStdString(path.string()));
        REQUIRE(db.open());
        QSqlQuery query(db);
        REQUIRE(query.exec(sql));
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

/// The background picture theme ``theme_id`` of ``output`` has in the
/// outputs as saved; nothing when the saved outputs have no such theme.
std::optional<std::string> stored_bg_image(SessionFacade& facade,
                                           const QString& output,
                                           const QString& theme_id)
{
    const auto outputs = facade.open_library().config()->load_outputs();
    const auto* oc = seventhslide::model::find_output_by_name(outputs, output.toStdString());
    const auto* theme = oc != nullptr ? seventhslide::theme::find_theme(
                                            *oc, seventhslide::theme::ThemeKind::Bg,
                                            theme_id.toStdString())
                                      : nullptr;
    if (theme == nullptr) {
        return std::nullopt;
    }
    return seventhslide::theme::theme_asset_url(*theme, "bg");
}

/// Rename the outputs table (``aside``) through a connection of the test's
/// own, so the outputs cannot be saved, or put it back.
void set_outputs_aside(const fs::path& library, bool aside)
{
    run_sql(library, aside ? QStringLiteral("ALTER TABLE outputs RENAME TO outputs_aside")
                           : QStringLiteral("ALTER TABLE outputs_aside RENAME TO outputs"));
}

}  // namespace

TEST_CASE("A theme background upload copies off the GUI thread", "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& themes = *facade.themes();
    const auto theme_id = first_bg_theme(themes);
    const auto before = bg_image(themes, theme_id);
    const auto src = dir.path / "loop.png";
    write_bytes(src, 2u * 1024u * 1024u);

    const auto id = themes.upload_asset(k_output, theme_id, k_bg, file_url(src));

    // Nothing about the theme changes until the copy is in place.
    REQUIRE(id > 0);
    REQUIRE(themes.copyingAsset());
    REQUIRE(bg_image(themes, theme_id) == before);

    REQUIRE(wait_for_upload(themes, id).isEmpty());
    const auto url = bg_image(themes, theme_id);
    REQUIRE(url.startsWith(QStringLiteral("/static/theme_backgrounds/")));
    REQUIRE(fs::file_size(owned_file(facade.media_root(), url)) == 2u * 1024u * 1024u);
    REQUIRE_FALSE(themes.copyingAsset());
    // Only the owned file: no staging file left behind.
    REQUIRE(files_in(seventhslide::theme::theme_assets_dir(facade.media_root())).size() == 1);
}

TEST_CASE("An upload refused before copying still reports from the event loop", "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& themes = *facade.themes();
    QSignalSpy finished(&themes, &ThemeController::assetUploadFinished);

    const auto bad_slot = themes.upload_asset(k_output, first_bg_theme(themes),
                                             QStringLiteral("nope"), file_url(dir.path / "x.png"));
    const auto no_theme = themes.upload_asset(k_output, QStringLiteral("missing"), k_bg,
                                             file_url(dir.path / "x.png"));
    const auto no_file = themes.upload_asset(k_output, first_bg_theme(themes), k_bg,
                                            file_url(dir.path / "missing.png"));
    REQUIRE(finished.isEmpty());

    REQUIRE(QTest::qWaitFor([&] { return finished.size() >= 3; }, 30000));
    QTest::qWait(20);
    REQUIRE(finished.size() == 3);
    const auto error_of = [&](qint64 id) {
        for (const auto& args : finished) {
            if (args.at(0).toLongLong() == id) {
                return args.at(1).toString();
            }
        }
        return QStringLiteral("<no report>");
    };
    REQUIRE(error_of(bad_slot) == refusal::unknown_asset_slot());
    REQUIRE(error_of(no_theme) == refusal::theme_gone());
    // Shown alone, so a sentence that names the file.
    REQUIRE(error_of(no_file) == QStringLiteral("“missing.png”: the file could not be found."));
}

TEST_CASE("An upload to a theme deleted while it copied leaves nothing", "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& themes = *facade.themes();
    const auto created = themes.create(k_output, k_bg, QStringLiteral("Doomed"));
    const auto theme_id = created.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(theme_id.isEmpty());
    const auto src = dir.path / "bg.png";
    write_bytes(src, 1000);

    const auto id = themes.upload_asset(k_output, theme_id, k_bg, file_url(src));
    REQUIRE(themes.remove(k_output, k_bg, theme_id).isEmpty());

    REQUIRE(wait_for_upload(themes, id) == refusal::theme_gone());
    REQUIRE(files_in(seventhslide::theme::theme_assets_dir(facade.media_root())).empty());
}

TEST_CASE("Switching library stops a theme background upload", "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.init_libraries(dir.path / "data"));
    REQUIRE(facade.libraries()->createLibrary(QStringLiteral("Other")).isEmpty());
    auto& themes = *facade.themes();
    const auto old_dir = seventhslide::theme::theme_assets_dir(facade.media_root());
    const auto src = dir.path / "loop.mp4";
    write_bytes(src, 32u * 1024u * 1024u);

    const auto id = themes.upload_asset(k_output, first_bg_theme(themes),
                                       QStringLiteral("bg_video"), file_url(src));
    REQUIRE(facade.libraries()->switchLibrary(QStringLiteral("other")).isEmpty());

    REQUIRE(wait_for_upload(themes, id)
            == QStringLiteral("“loop.mp4”: the library was closed before the file was copied in."));
    REQUIRE(files_in(old_dir).empty());
    REQUIRE_FALSE(themes.copyingAsset());
}

TEST_CASE("A duplicated theme gets its own background file at once", "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& themes = *facade.themes();
    const auto original = first_bg_theme(themes);
    const auto src = dir.path / "bg.png";
    write_bytes(src, 4u * 1024u * 1024u);
    REQUIRE(wait_for_upload(themes, themes.upload_asset(k_output, original, k_bg, file_url(src)))
                .isEmpty());
    const auto original_url = bg_image(themes, original);

    const auto copy = themes.create(k_output, k_bg, QStringLiteral("Copy"), {}, {}, original);

    // No copy is queued: the file is linked before create returns.
    REQUIRE_FALSE(themes.copyingAsset());
    const auto copy_url = copy.value(QStringLiteral("style"))
                              .toMap()
                              .value(QStringLiteral("background_image"))
                              .toString();
    REQUIRE(copy_url.startsWith(QStringLiteral("/static/theme_backgrounds/")));
    REQUIRE(copy_url != original_url);
    const auto copy_file = owned_file(facade.media_root(), copy_url);
    REQUIRE(fs::file_size(copy_file) == 4u * 1024u * 1024u);

    // Deleting the original leaves the copy's background in place.
    REQUIRE(themes.remove(k_output, k_bg, original).isEmpty());
    REQUIRE_FALSE(fs::exists(owned_file(facade.media_root(), original_url)));
    REQUIRE(fs::file_size(copy_file) == 4u * 1024u * 1024u);
}

TEST_CASE("A theme waits for a background it would share that is on its way",
          "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    SessionFacade facade;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    auto& themes = *facade.themes();
    const auto original = first_bg_theme(themes);
    const auto src = dir.path / "bg.png";
    write_bytes(src, 1024);
    REQUIRE(wait_for_upload(themes, themes.upload_asset(k_output, original, k_bg, file_url(src)))
                .isEmpty());
    const auto original_url = bg_image(themes, original);
    const auto plain = themes.create(k_output, k_bg, QStringLiteral("Plain"), {}, {}, {})
                           .value(QStringLiteral("id"))
                           .toString();
    REQUIRE_FALSE(plain.isEmpty());

    // The original's row came from another computer ahead of its file.
    const auto file = owned_file(facade.media_root(), original_url);
    const auto aside = dir.path / "aside.png";
    fs::rename(file, aside);
    const auto path = "theme_backgrounds/" + file.filename().string();
    using seventhslide::app::FileArrival;
    facade.media()->set_arrivals({{path, {FileArrival::State::Downloading, 10, 1024}}});
    const auto count = [&] { return themes.list(k_output, k_bg).size(); };
    const auto before = count();

    // A copy of it is refused, and nothing is added.
    const auto copy = themes.create(k_output, k_bg, QStringLiteral("Copy"), {}, {}, original);
    CHECK(copy.value(QStringLiteral("error")).toString().contains(QStringLiteral("on its way")));
    CHECK(count() == before);
    // So is another theme taking it up, which is left as it was.
    auto style = themes.theme_map(k_output, k_bg, plain).value(QStringLiteral("style")).toMap();
    style.insert(QStringLiteral("background_image"), original_url);
    style.insert(QStringLiteral("background_type"), QStringLiteral("image"));
    CHECK(themes.update_theme(k_output, k_bg, plain, QStringLiteral("Plain"), seventhslide::app::to_json(QVariant(style)), {}, false)
              .contains(QStringLiteral("on its way")));
    CHECK(bg_image(themes, plain) != original_url);
    // Its own file it may be edited with.
    auto own = themes.theme_map(k_output, k_bg, original).value(QStringLiteral("style")).toMap();
    CHECK(themes.update_theme(k_output, k_bg, original, QStringLiteral("Renamed"), seventhslide::app::to_json(QVariant(own)), {}, false).isEmpty());

    // On no computer connected: nothing to wait for, so it is shared as before,
    // and may be edited after.
    facade.media()->set_arrivals({{path, {FileArrival::State::Nowhere, 0, 1024}}});
    CHECK(themes.update_theme(k_output, k_bg, plain, QStringLiteral("Plain"), seventhslide::app::to_json(QVariant(style)), {}, false).isEmpty());
    CHECK(bg_image(themes, plain) == original_url);
    facade.media()->set_arrivals({{path, {FileArrival::State::Queued, 0, 1024}}});
    CHECK(themes.update_theme(k_output, k_bg, plain, QStringLiteral("Still plain"), seventhslide::app::to_json(QVariant(style)), {}, false)
              .isEmpty());

    // Once it is here, a copy gets its own, before sync has looked again.
    fs::rename(aside, file);
    facade.media()->set_arrivals({{path, {FileArrival::State::Downloading, 1024, 1024}}});
    const auto later = themes.create(k_output, k_bg, QStringLiteral("Copy"), {}, {}, original);
    CHECK(later.value(QStringLiteral("error")).toString().isEmpty());
    const auto later_url = later.value(QStringLiteral("style"))
                               .toMap()
                               .value(QStringLiteral("background_image"))
                               .toString();
    CHECK(later_url.startsWith(QStringLiteral("/static/theme_backgrounds/")));
    CHECK(later_url != original_url);
}

TEST_CASE("A deleted background theme's files go only once the outputs are saved",
          "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto path = dir.path / "library.db";
    SessionFacade facade;
    REQUIRE(facade.open_database(path));
    auto& themes = *facade.themes();
    const auto theme_id = themes.create(k_output, k_bg, QStringLiteral("Doomed"))
                              .value(QStringLiteral("id"))
                              .toString();
    REQUIRE_FALSE(theme_id.isEmpty());
    const auto src = dir.path / "bg.png";
    write_bytes(src, 1000);
    REQUIRE(wait_for_upload(themes, themes.upload_asset(k_output, theme_id, k_bg, file_url(src)))
                .isEmpty());
    const auto url = bg_image(themes, theme_id);
    const auto file = owned_file(facade.media_root(), url);
    REQUIRE(fs::is_regular_file(file));
    QSignalSpy rebuilt(&themes, &ThemeController::outputsEdited);

    // The outputs cannot be saved: the theme and its file stay.
    set_outputs_aside(path, true);
    CHECK_FALSE(themes.remove(k_output, k_bg, theme_id).isEmpty());
    set_outputs_aside(path, false);
    CHECK(fs::is_regular_file(file));
    CHECK(bg_image(themes, theme_id) == url);
    CHECK(stored_bg_image(facade, k_output, theme_id) == url.toStdString());
    CHECK(rebuilt.count() == 0);

    // Saved without it, its file goes, and what is live is rebuilt once.
    REQUIRE(themes.remove(k_output, k_bg, theme_id).isEmpty());
    CHECK_FALSE(fs::exists(file));
    CHECK(themes.theme_map(k_output, k_bg, theme_id).isEmpty());
    CHECK_FALSE(stored_bg_image(facade, k_output, theme_id).has_value());
    CHECK(rebuilt.count() == 1);
}

TEST_CASE("A theme's replaced picture goes only once the outputs are saved",
          "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto path = dir.path / "library.db";
    SessionFacade facade;
    REQUIRE(facade.open_database(path));
    auto& themes = *facade.themes();
    const auto theme_id = themes.create(k_output, k_bg, QStringLiteral("Pictured"))
                              .value(QStringLiteral("id"))
                              .toString();
    REQUIRE_FALSE(theme_id.isEmpty());
    const auto first = dir.path / "first.png";
    write_bytes(first, 1000);
    REQUIRE(wait_for_upload(themes, themes.upload_asset(k_output, theme_id, k_bg, file_url(first)))
                .isEmpty());
    const auto url = bg_image(themes, theme_id);
    const auto file = owned_file(facade.media_root(), url);
    REQUIRE(fs::is_regular_file(file));
    const auto assets = seventhslide::theme::theme_assets_dir(facade.media_root());
    const auto files_before = files_in(assets).size();
    const auto second = dir.path / "second.png";
    write_bytes(second, 2000);
    QSignalSpy rebuilt(&themes, &ThemeController::outputsEdited);

    // The outputs cannot be saved: the theme keeps its picture and its file,
    // and the new file is not kept.
    set_outputs_aside(path, true);
    CHECK_FALSE(
        wait_for_upload(themes, themes.upload_asset(k_output, theme_id, k_bg, file_url(second)))
            .isEmpty());
    set_outputs_aside(path, false);
    CHECK(bg_image(themes, theme_id) == url);
    CHECK(fs::is_regular_file(file));
    CHECK(files_in(assets).size() == files_before);
    CHECK(stored_bg_image(facade, k_output, theme_id) == url.toStdString());
    CHECK(rebuilt.count() == 0);

    // Saved with the new picture, the old file goes, and what is live is
    // rebuilt once.
    REQUIRE(
        wait_for_upload(themes, themes.upload_asset(k_output, theme_id, k_bg, file_url(second)))
            .isEmpty());
    const auto replaced = bg_image(themes, theme_id);
    CHECK(replaced != url);
    CHECK(fs::file_size(owned_file(facade.media_root(), replaced)) == 2000);
    CHECK_FALSE(fs::exists(file));
    CHECK(stored_bg_image(facade, k_output, theme_id) == replaced.toStdString());
    CHECK(rebuilt.count() == 1);
}

TEST_CASE("A theme's background changed by an edit goes only once the outputs are saved",
          "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto path = dir.path / "library.db";
    SessionFacade facade;
    REQUIRE(facade.open_database(path));
    auto& themes = *facade.themes();
    // Two themes, each with a picture of its own.
    const auto make = [&](const QString& name, std::size_t bytes) {
        const auto id = themes.create(k_output, k_bg, name).value(QStringLiteral("id")).toString();
        REQUIRE_FALSE(id.isEmpty());
        const auto src = dir.path / (name.toStdString() + ".png");
        write_bytes(src, bytes);
        REQUIRE(wait_for_upload(themes, themes.upload_asset(k_output, id, k_bg, file_url(src)))
                    .isEmpty());
        return id;
    };
    const auto edited = make(QStringLiteral("Edited"), 1000);
    const auto other = make(QStringLiteral("Other"), 2000);
    const auto url = bg_image(themes, edited);
    const auto file = owned_file(facade.media_root(), url);
    const auto other_file = owned_file(facade.media_root(), bg_image(themes, other));
    const auto assets = seventhslide::theme::theme_assets_dir(facade.media_root());
    const auto files_before = files_in(assets).size();
    // The edit points it at the other theme's picture, which it gets a file
    // of its own for; its own picture is then no longer named.
    auto style = themes.theme_map(k_output, k_bg, edited).value(QStringLiteral("style")).toMap();
    style.insert(QStringLiteral("background_image"), bg_image(themes, other));
    const auto edit = [&] {
        return themes.update_theme(k_output, k_bg, edited, QStringLiteral("Edited"),
                                   seventhslide::app::to_json(QVariant(style)), {}, false);
    };
    QSignalSpy rebuilt(&themes, &ThemeController::outputsEdited);

    // The outputs cannot be saved: the theme keeps its picture and its file,
    // and the file made for the new one is not kept.
    set_outputs_aside(path, true);
    CHECK_FALSE(edit().isEmpty());
    set_outputs_aside(path, false);
    CHECK(bg_image(themes, edited) == url);
    CHECK(fs::is_regular_file(file));
    CHECK(files_in(assets).size() == files_before);
    CHECK(stored_bg_image(facade, k_output, edited) == url.toStdString());
    CHECK(rebuilt.count() == 0);

    // Saved: it has its own copy of the other picture, its old file goes,
    // and the other theme's stays.
    REQUIRE(edit().isEmpty());
    const auto now = bg_image(themes, edited);
    CHECK(now != url);
    CHECK(fs::file_size(owned_file(facade.media_root(), now)) == 2000);
    CHECK_FALSE(fs::exists(file));
    CHECK(fs::file_size(other_file) == 2000);
    CHECK(stored_bg_image(facade, k_output, edited) == now.toStdString());
    CHECK(rebuilt.count() == 1);
}

TEST_CASE("A theme made while the outputs cannot be saved leaves no file", "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto path = dir.path / "library.db";
    SessionFacade facade;
    REQUIRE(facade.open_database(path));
    auto& themes = *facade.themes();
    const auto original = first_bg_theme(themes);
    const auto src = dir.path / "bg.png";
    write_bytes(src, 1000);
    REQUIRE(wait_for_upload(themes, themes.upload_asset(k_output, original, k_bg, file_url(src)))
                .isEmpty());
    const auto assets = seventhslide::theme::theme_assets_dir(facade.media_root());
    const auto files_before = files_in(assets).size();
    const auto themes_before = themes.list(k_output, k_bg).size();

    // A copy of it would get a file of its own; not saved, it gets neither.
    set_outputs_aside(path, true);
    const auto copy = themes.create(k_output, k_bg, QStringLiteral("Copy"), {}, {}, original);
    set_outputs_aside(path, false);
    CHECK(copy.contains(QStringLiteral("error")));
    CHECK(themes.list(k_output, k_bg).size() == themes_before);
    CHECK(files_in(assets).size() == files_before);
}

TEST_CASE("A deleted output's theme files go only once the outputs are saved",
          "[app][theme][import]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::test::TempDir dir;
    const auto path = dir.path / "library.db";
    SessionFacade facade;
    REQUIRE(facade.open_database(path));
    auto& themes = *facade.themes();
    auto& settings = *facade.outputSettings();
    const auto stage =
        settings.createOutput(QStringLiteral("Stage")).value(QStringLiteral("name")).toString();
    REQUIRE(stage == QStringLiteral("Stage"));
    const auto theme_id =
        themes.create(stage, k_bg, QStringLiteral("Stage picture")).value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(theme_id.isEmpty());
    const auto src = dir.path / "bg.png";
    write_bytes(src, 1000);
    REQUIRE(wait_for_upload(themes, themes.upload_asset(stage, theme_id, k_bg, file_url(src)))
                .isEmpty());
    const auto url = bg_image(themes, theme_id, stage);
    const auto file = owned_file(facade.media_root(), url);
    REQUIRE(fs::is_regular_file(file));
    QSignalSpy rebuilt(&settings, &OutputSettingsController::outputsEdited);

    // The outputs cannot be saved: the output, its theme and its file stay.
    set_outputs_aside(path, true);
    CHECK_FALSE(settings.deleteOutput(stage).isEmpty());
    set_outputs_aside(path, false);
    CHECK(fs::is_regular_file(file));
    CHECK(bg_image(themes, theme_id, stage) == url);
    CHECK(stored_bg_image(facade, stage, theme_id) == url.toStdString());
    CHECK(rebuilt.count() == 0);

    // Saved without it, its theme's file goes, and what is live is rebuilt
    // once.
    REQUIRE(settings.deleteOutput(stage).isEmpty());
    CHECK_FALSE(fs::exists(file));
    CHECK(settings.getOutputSettings(stage).isEmpty());
    const auto stored = facade.open_library().config()->load_outputs();
    CHECK(seventhslide::model::find_output_by_name(stored, stage.toStdString()) == nullptr);
    CHECK(rebuilt.count() == 1);
}
