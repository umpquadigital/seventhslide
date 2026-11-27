// Screenshots of the operator UI in fixed states, for checking that a QML
// change leaves every pixel where it was (tools/ui_shots_compare.py).
//
//     SEVENTHSLIDE_SHOTS_DIR=out ./seventhslide_tests_dev "[.shots]"
//
// SEVENTHSLIDE_SHOTS_ONLY=<part of a name> shoots only the scenes it names;
// SEVENTHSLIDE_SHOTS_SIZE=1280x560 shoots every one at that size.
//
// Each shot builds the same small library at the same path, opens a fresh
// operator window on it, puts the UI in one state and saves the settled
// window. The software renderer draws it, so a run is repeatable on one
// machine. What a shot sets up is free to change with the QML (a property
// renamed, a function moved): what has to stay is the picture.

#include "fake_tool.hpp"
#include "font_controller.hpp"
#include "font_fixtures.hpp"
#include "font_store.hpp"
#include "slide_edit_session.hpp"
#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "media_controller.hpp"
#include "service_item_model.hpp"
#include "operator_ui.hpp"
#include "sample_library.hpp"
#include "facade_all.hpp"
#include "test_qt_app.hpp"
#include "ui_test_helpers.hpp"
#include "wav_fixture.hpp"
#include "theme_controller.hpp"
#include "tools_controller.hpp"
#include "convert_controller.hpp"
#include "download_controller.hpp"
#include "live_library_controller.hpp"
#include "live_receiver.hpp"
#include "web_library_controller.hpp"
#include "web_renderer.hpp"
#include "web_thumbs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/font_repository.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/platform/tools.hpp>
#include <seventhslide/render/output_window_host.hpp>

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QUrl>
#include <QLinearGradient>
#include <QPainter>
#include <QStyleHints>
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QSharedPointer>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>
#include <QTest>
#include <QVariant>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using seventhslide::app::MediaBrowserModel;
using seventhslide::app::SessionFacade;
using seventhslide::test::OperatorUi;

namespace {

namespace fs = std::filesystem;

/// A camera for the shots, which have no NDI source to receive: a stage's
/// look with the camera's name on it.
class ShotCamera final : public seventhslide::app::LiveReceiver {
public:
    explicit ShotCamera(const seventhslide::model::LiveSource& source)
    {
        QImage picture(1280, 720, QImage::Format_RGB32);
        QPainter p(&picture);
        QLinearGradient stage(0, 0, 0, 720);
        stage.setColorAt(0, QColor(0x14, 0x14, 0x1c));
        stage.setColorAt(1, QColor(0x3a, 0x2e, 0x24));
        p.fillRect(picture.rect(), stage);
        p.setPen(QColor(0xf0, 0xe6, 0xd2));
        QFont font = p.font();
        font.setPixelSize(64);
        p.setFont(font);
        p.drawText(picture.rect(), Qt::AlignCenter, QString::fromStdString(source.name));
        p.end();
        frame_ = std::move(picture);
    }
    [[nodiscard]] QImage frame() const override { return frame_; }
    [[nodiscard]] seventhslide::audio::AudioSource* sound() override { return nullptr; }
    [[nodiscard]] QString problem() const override { return {}; }
    [[nodiscard]] Stats stats() const override { return {frame_.size(), 29.97}; }

private:
    QImage frame_;
};

/// A web page for the shots, which cannot run Qt WebEngine: a countdown
/// page's look, painted at the page's size, titled and coloured by its
/// address so pages can be told apart.
class ShotWebPage final : public seventhslide::app::WebRenderer {
public:
    void load(const QUrl& url, QSize size, double) override
    {
        url_ = url;
        const QString title = url.path().contains(QStringLiteral("countdown"))
                                  ? QStringLiteral("Countdown")
                                  : QStringLiteral("Welcome");
        const bool warm = title == QStringLiteral("Countdown");
        QImage page(size, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&page);
        QLinearGradient sky(0, 0, 0, size.height());
        sky.setColorAt(0, warm ? QColor(0x5a, 0x23, 0x2b) : QColor(0x1d, 0x35, 0x57));
        sky.setColorAt(1, warm ? QColor(0xc0, 0x6b, 0x3a) : QColor(0x45, 0x7b, 0x9d));
        p.fillRect(page.rect(), sky);
        p.setPen(Qt::white);
        QFont font = p.font();
        font.setPixelSize(size.height() / 9);
        font.setBold(true);
        p.setFont(font);
        p.drawText(QRect(0, 0, size.width(), size.height() * 55 / 100),
                   Qt::AlignHCenter | Qt::AlignBottom, title);
        font.setPixelSize(size.height() / 6);
        font.setBold(false);
        p.setFont(font);
        p.drawText(QRect(0, size.height() * 55 / 100, size.width(), size.height() / 4),
                   Qt::AlignCenter, QStringLiteral("04:59"));
        p.end();
        frame_ = std::move(page);
        emit frameChanged();
        emit loaded();
    }
    void reload() override {}
    [[nodiscard]] QImage frame() const override { return frame_; }
    [[nodiscard]] QString problem() const override { return {}; }
    [[nodiscard]] QUrl url() const override { return url_; }
    [[nodiscard]] bool can_go_back() const override { return false; }
    void go_back() override {}
    bool navigate(const QUrl&) override { return true; }

private:
    QImage frame_;
    QUrl url_;
};

/// The same path every run, so a path the UI shows reads the same.
fs::path fixture_dir()
{
    return fs::path(QDir::tempPath().toStdString()) / "seventhslide-ui-shots";
}

using Library = seventhslide::test::SampleLibrary;

/// The sample library at the fixed path, with the first song of the open
/// service live on its third line.
Library build_library(SessionFacade& facade)
{
    const fs::path dir = fixture_dir();
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    REQUIRE(facade.open_database(dir / "library.db"));
    const Library lib = seventhslide::test::fill_sample_library(facade);
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.program()->jumpToLine(2));
    return lib;
}

/// Saves settled grabs of the window into the output directory.
class Shooter {
public:
    explicit Shooter(fs::path out) : out_(std::move(out)) { fs::create_directories(out_); }

    struct Settled {
        QImage last;
        QImage before_last;
        bool settled = false;
    };

    /// Grab until two grabs in a row agree: an animation or a deferred
    /// layout (a page's scrollbar gutter, kept while it is built) has
    /// finished. A scene settles before it opens a menu, so the menu finds
    /// its button where it stays.
    static Settled settle(QQuickWindow& window)
    {
        Settled r;
        for (int i = 0; i < 60 && !r.settled; ++i) {
            QTest::qWait(i == 0 ? 100 : 50);
            QImage now = grab(window);
            r.settled = !r.last.isNull() && now == r.last;
            r.before_last = std::move(r.last);
            r.last = std::move(now);
        }
        return r;
    }

    /// Settle, then save. Notes a window that never settles.
    void shoot(QQuickWindow& window, const std::string& name)
    {
        Settled settled = settle(window);
        if (!settled.settled) {
            unsettled_.push_back(name + " ("
                                 + describe_change(settled.before_last, settled.last) + ")");
        }
        QImage last = std::move(settled.last);
        // The software renderer draws a few anti-aliased edge pixels of
        // rounded rectangles differently from one render to the next; each
        // pixel's most common value over several renders is the stable one.
        std::vector<QImage> grabs{std::move(last)};
        for (int i = 1; i < k_votes; ++i) {
            QTest::qWait(20);
            grabs.push_back(grab(window));
        }
        last = majority(grabs);
        REQUIRE(last.save(QString::fromStdString((out_ / (name + ".png")).string()), "PNG"));
        ++count_;
    }

    [[nodiscard]] const std::vector<std::string>& unsettled() const { return unsettled_; }
    [[nodiscard]] int count() const { return count_; }

private:
    static constexpr int k_votes = 7;

    /// Each pixel's most frequent value across ``images`` (all one size).
    static QImage majority(const std::vector<QImage>& images)
    {
        QImage out = images.front();
        for (int y = 0; y < out.height(); ++y) {
            for (int x = 0; x < out.width(); ++x) {
                QRgb best = images.front().pixel(x, y);
                int best_count = 0;
                for (const QImage& candidate : images) {
                    const QRgb value = candidate.pixel(x, y);
                    int count = 0;
                    for (const QImage& other : images) {
                        count += other.pixel(x, y) == value ? 1 : 0;
                    }
                    if (count > best_count) {
                        best = value;
                        best_count = count;
                    }
                }
                out.setPixel(x, y, best);
            }
        }
        return out;
    }

    /// How many pixels differ between ``a`` and ``b``, and where.
    static std::string describe_change(const QImage& a, const QImage& b)
    {
        if (a.size() != b.size()) {
            return "size changed";
        }
        int count = 0;
        QRect box;
        for (int y = 0; y < a.height(); ++y) {
            for (int x = 0; x < a.width(); ++x) {
                if (a.pixel(x, y) != b.pixel(x, y)) {
                    ++count;
                    box |= QRect(x, y, 1, 1);
                }
            }
        }
        return std::to_string(count) + " px in " + std::to_string(box.x()) + "," +
               std::to_string(box.y()) + " " + std::to_string(box.width()) + "x" +
               std::to_string(box.height());
    }

    /// The window's content (popups included: their overlay is in it), drawn
    /// afresh. grabWindow() is not used: the software renderer repaints only
    /// what changed, and a pixel it skipped (the anti-aliased corner of a card
    /// over a scrim) can keep whatever the previous frame left there. The grab
    /// is RGB32, whose fourth byte the renderer does not always set; RGB888
    /// drops it.
    static QImage grab(QQuickWindow& window)
    {
        QSharedPointer<QQuickItemGrabResult> result = window.contentItem()->grabToImage();
        REQUIRE(result);
        QImage image;
        QObject::connect(result.data(), &QQuickItemGrabResult::ready,
                         [&image, result] { image = result->image(); });
        REQUIRE(QTest::qWaitFor([&image] { return !image.isNull(); }, 5000));
        return image.convertToFormat(QImage::Format_RGB888);
    }

    fs::path out_;
    std::vector<std::string> unsettled_;
    int count_ = 0;
};

QObject* component(OperatorUi& ui, const char* name)
{
    QObject* found = ui.find_component(name);
    INFO("component " << name);
    REQUIRE(found != nullptr);
    return found;
}

/// The first object of QML type ``state`` under the component ``dialog``: a
/// dialog's view-model, or a view's own prompt.
QObject* state_of(OperatorUi& ui, const char* dialog, const char* state)
{
    const QString prefix = QString::fromLatin1(state) + QStringLiteral("_QMLTYPE_");
    for (QObject* o : component(ui, dialog)->findChildren<QObject*>()) {
        if (QString::fromLatin1(o->metaObject()->className()).startsWith(prefix)) {
            return o;
        }
    }
    INFO("state " << state);
    FAIL("no such state");
    return nullptr;
}

/// Whether ``o`` is an instance of the QML component ``name``, or of one
/// built on it (a SidebarColor is a RibbonSwatch).
bool is_component(const QObject* o, const QString& prefix)
{
    for (const QMetaObject* m = o->metaObject(); m != nullptr; m = m->superClass()) {
        if (QString::fromLatin1(m->className()).startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

/// The first instance of the QML component ``name`` (or of one built on it)
/// under ``root`` that is on screen and enabled.
QObject* shown_component(QObject* root, const char* name)
{
    const QString prefix = QString::fromLatin1(name) + QStringLiteral("_QMLTYPE_");
    for (QQuickItem* item : root->findChildren<QQuickItem*>()) {
        if (is_component(item, prefix) && item->isVisible() && item->isEnabled()) {
            return item;
        }
    }
    INFO("component " << name);
    FAIL("no visible, enabled instance");
    return nullptr;
}

/// The first visible instance of the QML component ``name`` drawn under
/// ``root``, walking the visual tree: a view's delegates are not its QObject
/// children.
QQuickItem* drawn_component(QQuickItem* root, const QString& prefix)
{
    for (QQuickItem* item : root->childItems()) {
        if (!item->isVisible()) {
            continue;
        }
        if (QString::fromLatin1(item->metaObject()->className()).startsWith(prefix)) {
            return item;
        }
        if (QQuickItem* found = drawn_component(item, prefix)) {
            return found;
        }
    }
    return nullptr;
}

QQuickItem* drawn_component(QObject* root, const char* name)
{
    auto* item = qobject_cast<QQuickItem*>(root);
    REQUIRE(item != nullptr);
    QQuickItem* found =
        drawn_component(item, QString::fromLatin1(name) + QStringLiteral("_QMLTYPE_"));
    INFO("component " << name);
    REQUIRE(found != nullptr);
    return found;
}

void set(QObject* object, const char* property, const QVariant& value)
{
    INFO("property " << property);
    REQUIRE(object->setProperty(property, value));
}

void call(QObject* object, const char* method, const QVariant& a = {}, const QVariant& b = {})
{
    INFO("method " << method);
    if (!a.isValid()) {
        REQUIRE(QMetaObject::invokeMethod(object, method));
    } else if (!b.isValid()) {
        REQUIRE(QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, a)));
    } else {
        REQUIRE(QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, a), Q_ARG(QVariant, b)));
    }
}

/// One shot: what it is called, the window size, and the state it shows.
struct Scene {
    std::string name;
    std::function<void(SessionFacade&, OperatorUi&, const Library&)> setup;
    int width = 1280;
    int height = 720;
};

QString first_theme_id(SessionFacade& facade, const char* kind)
{
    const auto themes = facade.themes()->list(QStringLiteral("Main"), QString::fromLatin1(kind));
    REQUIRE_FALSE(themes.isEmpty());
    return themes.front().toMap().value(QStringLiteral("id")).toString();
}

/// A few more themes of ``kind`` on Main, tagged, so a gallery shows names,
/// tags and a long name cut short.
void add_tagged_themes(SessionFacade& facade, const char* kind)
{
    const QString k = QString::fromLatin1(kind);
    for (const auto& [name, tags] : {std::pair{"Hymn Lyrics", "hymn, worship"},
                                     std::pair{"Lower Third", "stage"},
                                     std::pair{"Youth Night Large Print", "youth, large print, events"}}) {
        const QVariantMap made = facade.themes()->create(QStringLiteral("Main"), k,
                                                         QString::fromUtf8(name), {},
                                                         QString::fromUtf8(tags));
        REQUIRE_FALSE(made.contains(QStringLiteral("error")));
    }
}

/// A season's services, tagged: some to come (in 2030, so they stay
/// upcoming), several past. Returns the first to come, Christmas Eve.
qint64 add_season_services(SessionFacade& facade)
{
    auto* library = facade.library();
    const auto add = [library](const char* name, const char* at, const QStringList& tags) {
        const qint64 id = library->createService(QString::fromUtf8(name));
        REQUIRE(id > 0);
        REQUIRE(library->setServiceDateTime(id, QString::fromLatin1(at)).isEmpty());
        REQUIRE(library->setServiceTags(id, tags).isEmpty());
        return id;
    };
    const qint64 christmas = add("Christmas Eve Candlelight", "2030-12-24T19:00:00",
                                 {QStringLiteral("Christmas")});
    add("Advent Vespers", "2030-12-07T17:30:00", {QStringLiteral("Vespers")});
    add("Thanksgiving Service", "2026-03-26T10:00:00", {});
    add("Youth Vespers", "2026-03-14T17:30:00", {QStringLiteral("Vespers"), QStringLiteral("Youth")});
    add("Communion Sabbath", "2026-03-07T11:00:00", {QStringLiteral("Communion")});
    add("Prayer Meeting", "2026-03-04T19:00:00", {});
    add("Revival Week", "", {QStringLiteral("Revival"), QStringLiteral("Evangelism")});
    return christmas;
}

std::vector<Scene> scenes()
{
    using F = SessionFacade;
    using L = Library;
    std::vector<Scene> s;
    const auto library_tab = [](int tab) {
        return [tab](F&, OperatorUi& ui, const L&) {
            set(component(ui, "LibraryPanel"), "tabIndex", tab);
        };
    };

    // --- the operator window ---
    s.push_back({"main_songs", [](F&, OperatorUi&, const L&) {}});
    s.push_back({"main_songs_large", [](F&, OperatorUi&, const L&) {}, 1600, 1000});
    s.push_back({"main_live_on", [](F& facade, OperatorUi&, const L&) {
                     facade.destinations()->setLiveOn(true);
                 }});
    s.push_back({"main_songs_selected", [](F&, OperatorUi& ui, const L& lib) {
                     QObject* songs = component(ui, "SongsTab");
                     const QVariant first = QVariant::fromValue<qint64>(lib.first_song);
                     const QVariantList both{first, QVariant::fromValue<qint64>(lib.first_song + 1)};
                     REQUIRE(QMetaObject::invokeMethod(
                         songs->property("songSelection").value<QObject*>(), "set",
                         Q_ARG(QVariant, both), Q_ARG(QVariant, first), Q_ARG(QVariant, first)));
                 }});
    s.push_back({"main_songs_lyrics_search", [](F&, OperatorUi& ui, const L&) {
                     set(component(ui, "SongsTab"), "search", QStringLiteral("taught my heart"));
                 }});
    s.push_back({"main_freeform", library_tab(1)});
    // A Freeform live with builds: its first slide's objects come in on two
    // clicks, and one has been taken; the thumbnail's marks say so.
    s.push_back({"main_freeform_steps", [](F& facade, OperatorUi&, const L& lib) {
                     auto doc = facade.freeformLibrary()->load_freeform_document(lib.freeform);
                     REQUIRE(doc);
                     REQUIRE_FALSE(doc->decks.empty());
                     auto& objects = doc->decks[0].slides[0].objects;
                     REQUIRE_FALSE(objects.empty());
                     int order = 0;
                     for (auto& obj : objects) {
                         seventhslide::canvas::Animation in;
                         in.order = ++order;
                         in.effect = seventhslide::canvas::AnimationEffect::Fade;
                         obj.animations = {in};
                     }
                     REQUIRE(facade.freeformLibrary()->commit_freeform_document(lib.freeform, *doc).isEmpty());
                     REQUIRE(facade.live()->playFreeform(lib.freeform));
                     REQUIRE(facade.program()->nextSlide());
                     REQUIRE(facade.program()->currentStep() == 1);
                 }});
    s.push_back({"main_bibles", [](F& facade, OperatorUi& ui, const L& lib) {
                     REQUIRE(facade.library()->selectBible(lib.bible));
                     (void)facade.library()->resolveBibleRef(QStringLiteral("John 3:16-17"));
                     set(component(ui, "LibraryPanel"), "tabIndex", 2);
                 }});
    // The translation picker open: each Bible, its abbreviation, a check on the one in use.
    s.push_back({"main_bibles_translation_menu", [](F& facade, OperatorUi& ui, const L& lib) {
                     REQUIRE(facade.library()->selectBible(lib.bible));
                     set(component(ui, "LibraryPanel"), "tabIndex", 2);
                     Shooter::settle(*ui.window());
                     QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("translationMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }});
    // A reference being typed: the books it could mean, under the search field.
    s.push_back({"main_bibles_suggestions", [](F& facade, OperatorUi& ui, const L& lib) {
                     REQUIRE(facade.library()->selectBible(lib.bible));
                     set(component(ui, "LibraryPanel"), "tabIndex", 2);
                     Shooter::settle(*ui.window());
                     set(component(ui, "BiblesTab"), "suggestions",
                         QVariantList{QStringLiteral("John")});
                 }});
    s.push_back({"main_videos", library_tab(3)});
    s.push_back({"main_images", library_tab(4)});
    s.push_back({"main_audio", library_tab(5)});
    s.push_back({"main_images_in_folder", [](F& facade, OperatorUi& ui, const L& lib) {
                     facade.media()->library(MediaKindQml::Image)->setFolder(lib.image_folder);
                     set(component(ui, "LibraryPanel"), "tabIndex", 4);
                 }});
    s.push_back({"main_images_icons", [](F& facade, OperatorUi& ui, const L&) {
                     facade.media()->setMediaViewMode(QStringLiteral("icons"));
                     set(component(ui, "LibraryPanel"), "tabIndex", 4);
                 }});
    s.push_back({"main_videos_icons", [](F& facade, OperatorUi& ui, const L&) {
                     facade.media()->setMediaViewMode(QStringLiteral("icons"));
                     set(component(ui, "LibraryPanel"), "tabIndex", 3);
                 }});
    s.push_back({"main_service_item_selected",[](F& facade, OperatorUi& ui, const L&) {
                     const auto id = facade.serviceItems()->data(
                         facade.serviceItems()->index(1, 0),
                         seventhslide::app::ServiceItemModel::ItemIdRole);
                     REQUIRE(QMetaObject::invokeMethod(
                         component(ui, "ServicePanel")->property("itemSelection").value<QObject*>(),
                         "set", Q_ARG(QVariant, QVariantList({id})), Q_ARG(QVariant, id),
                         Q_ARG(QVariant, id)));
                 }});
    s.push_back({"main_two_outputs", [](F& facade, OperatorUi&, const L&) {
                     auto outputs = facade.session().outputs();
                     auto stage = outputs.front();
                     stage.id = "stage";
                     stage.name = "Stage";
                     outputs.push_back(stage);
                     facade.set_outputs(outputs);
                 }});
    // A portrait sign as the primary output beside a landscape stage: its
    // card and the slide gallery take its shape.
    s.push_back({"main_portrait_output", [](F& facade, OperatorUi&, const L&) {
                     auto outputs = facade.session().outputs();
                     auto stage = outputs.front();
                     stage.id = "stage";
                     stage.name = "Stage";
                     outputs.front().canvas_width = 1080;
                     outputs.front().canvas_height = 1920;
                     outputs.push_back(stage);
                     facade.set_outputs(outputs);
                 }});
    // The Audio Mixer: a tone playing on Main, its meter lit, with Lobby
    // muted and turned down; then the Outputs tab, two outputs, one on Listen.
    s.push_back({"main_mixer_playing", [](F& facade, OperatorUi&, const L&) {
                     const auto audio = facade.media_root() / "audio";
                     fs::create_directories(audio);
                     seventhslide::test::write_tone_wav(audio / "Tone.wav", 60.0);
                     facade.refresh_library();
                     auto& mixer = *facade.mixer();
                     REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info"))
                                 .isEmpty());
                     const auto lobby = facade.session().stack_defs().back().id;
                     REQUIRE(mixer.setGainDb(QStringLiteral("stack"), QString::fromStdString(lobby), -18.0)
                                 .isEmpty());
                     REQUIRE(mixer.setMuted(QStringLiteral("stack"), QString::fromStdString(lobby), true)
                                 .isEmpty());
                     REQUIRE(facade.live()->playAudio(QStringLiteral("Tone.wav")));
                     REQUIRE(QTest::qWaitFor(
                         [&] {
                             return mixer.meter(seventhslide::audio::StripKind::Stack, "main")
                                        .level_db[0] > -30.0f;
                         },
                         5000));
                 }});
    s.push_back({"main_mixer_outputs", [](F& facade, OperatorUi&, const L&) {
                     auto outputs = facade.session().outputs();
                     auto stage = outputs.front();
                     stage.id = "stage";
                     stage.name = "Stage";
                     outputs.push_back(stage);
                     facade.set_outputs(outputs);
                     REQUIRE(facade.mixer()
                                 ->setGainDb(QStringLiteral("output"), QStringLiteral("stage"), -6.0)
                                 .isEmpty());
                     facade.mixer()->setShownTab(QStringLiteral("outputs"));
                 }});
    s.push_back({"main_library_hidden", [](F&, OperatorUi& ui, const L&) {
                     ui.dock_layout()->setShown(QStringLiteral("library"), false);
                 }});
    s.push_back({"main_service_collapsed", [](F&, OperatorUi& ui, const L&) {
                     ui.dock_layout()->setCollapsed(QStringLiteral("service"), true);
                 }});
    s.push_back({"main_layout_edit", [](F&, OperatorUi& ui, const L&) {
                     ui.dock_layout()->setEditMode(true);
                 }});
    s.push_back({"main_layout_menu", [](F&, OperatorUi& ui, const L&) {
                     Shooter::settle(*ui.window());
                     QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("layoutMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }});
    // An audio file's Edit: its name and the fades it sets for itself, one
    // set and the rest left to the service and the outputs.
    s.push_back({"main_audio_file_edit", [](F& facade, OperatorUi& ui, const L&) {
                     const auto files = facade.media_repo()->list_files(
                         seventhslide::db::MediaKind::Audio, true);
                     REQUIRE_FALSE(files.empty());
                     const auto filename = QString::fromStdString(files.front().filename);
                     REQUIRE(facade.media()
                                 ->setAudioFileFades(filename, {{QStringLiteral("fade_out_ms"), 2500}})
                                 .isEmpty());
                     set(component(ui, "LibraryPanel"), "tabIndex", 5);
                     Shooter::settle(*ui.window());
                     QObject* browser = nullptr;
                     for (QObject* o : ui.window()->findChildren<QObject*>()) {
                         if (QString::fromLatin1(o->metaObject()->className())
                                 .startsWith(QStringLiteral("MediaBrowser_QMLTYPE_"))
                             && o->property("isAudio").toBool()) {
                             browser = o;
                         }
                     }
                     REQUIRE(browser != nullptr);
                     call(browser, "selectFile", filename, false);
                     call(browser, "promptRename");
                 }});
    // The toolbar's Send to menu, with a second stack to send to.
    s.push_back({"main_send_to_menu", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby")).isEmpty());
                     Shooter::settle(*ui.window());
                     QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("sendToMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }});
    // The controller's options menu (the ⋯ button).
    s.push_back({"main_controller_options", [](F&, OperatorUi& ui, const L&) {
                     Shooter::settle(*ui.window());
                     QObject* menu =
                         ui.window()->findChild<QObject*>(QStringLiteral("controllerOptionsMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }});
    // Two videos converting: the ring in the bottom bar, and its list open.
    // A stand-in ffmpeg reports 42% and holds there.
    s.push_back({"main_converting", [](F& facade, OperatorUi& ui, const L&) {
                     const fs::path bin = fs::temp_directory_path() / "seventhslide-shots-convert";
                     fs::remove_all(bin);
                     fs::create_directories(bin);
                     const auto script = [&bin](const char* name, const char* body) {
                         (void)seventhslide::test::write_fake_tool(bin, name, body);
                     };
                     script("ffprobe",
                            "echo '{\"streams\":[{\"codec_type\":\"video\",\"width\":1920,"
                            "\"height\":1080}],\"format\":{\"duration\":\"2.0\"}}'");
                     script("ffmpeg", "[ \"$1\" = -version ] && exit 0\n"
                                      "echo out_time_us=840000\nsleep 60");
                     seventhslide::platform::ToolSearch search;
                     search.platform_id = seventhslide::test::tool_platform();
                     search.path_env = bin.string();
                     facade.tools()->set_search(search);
                     QStringList videos;
                     for (const auto& f : facade.media()->media_repo()->list_files(
                              seventhslide::db::MediaKind::Video)) {
                         videos << QString::fromStdString(f.filename);
                     }
                     REQUIRE(videos.size() >= 2);
                     auto& convert = *facade.convert();
                     convert.convertMedia(MediaKindQml::Video, videos.mid(0, 2),
                                          {{QStringLiteral("format"), QStringLiteral("webm")},
                                           {QStringLiteral("height"), 720}},
                                          -1, false);
                     REQUIRE(QTest::qWaitFor([&convert] { return convert.progress() > 0.0; }, seventhslide::test::k_tool_wait_ms));
                     Shooter::settle(*ui.window());
                     QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("jobsMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }});
    // Import on Videos, as a menu: from here, or from the internet.
    s.push_back({"main_import_menu", [](F&, OperatorUi& ui, const L&) {
                     set(component(ui, "LibraryPanel"), "tabIndex", 3);
                     Shooter::settle(*ui.window());
                     QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("createMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }});
    // New on Songs, as a menu: write one, or import one.
    s.push_back({"main_songs_create_menu", [](F&, OperatorUi& ui, const L&) {
                     set(component(ui, "LibraryPanel"), "tabIndex", 0);
                     Shooter::settle(*ui.window());
                     QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("createMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }});
    // The tab strip's second page, Browser and Live, behind the arrow after Audio.
    s.push_back({"main_browser", library_tab(6)});
    // A page's dialog, editing the first.
    s.push_back({"main_browser_page", [](F& facade, OperatorUi& ui, const L&) {
                     set(component(ui, "LibraryPanel"), "tabIndex", 6);
                     Shooter::settle(*ui.window());
                     QObject* browser = nullptr;
                     for (QObject* o : ui.window()->findChildren<QObject*>()) {
                         if (QByteArray(o->metaObject()->className()).startsWith("MediaBrowser_QMLTYPE_")
                             && o->property("isWeb").toBool()) {
                             browser = o;
                         }
                     }
                     REQUIRE(browser != nullptr);
                     QObject* dialog = OperatorUi::component_under(browser, "WebPageDialog");
                     REQUIRE(dialog != nullptr);
                     auto* pages = facade.webLibrary()->browser();
                     qint64 id = -1;
                     for (int r = 0; r < pages->rowCount() && id < 0; ++r) {
                         if (pages->data(pages->index(r), MediaBrowserModel::KindRole).toString()
                             == QStringLiteral("file")) {
                             id = pages->data(pages->index(r), MediaBrowserModel::IdRole).toLongLong();
                         }
                     }
                     REQUIRE(QMetaObject::invokeMethod(
                         dialog, "openFor",
                         Q_ARG(QVariant, QVariant(facade.webLibrary()->page(id)))));
                 }});
    // A page live: the Controller shows it, with Reload.
    s.push_back({"main_browser_live", [](F& facade, OperatorUi& ui, const L&) {
                     set(component(ui, "LibraryPanel"), "tabIndex", 6);
                     facade.set_web_renderer_factory(
                         [] { return std::make_unique<ShotWebPage>(); });
                     auto* pages = facade.webLibrary()->browser();
                     for (int r = 0; r < pages->rowCount(); ++r) {
                         if (pages->data(pages->index(r), MediaBrowserModel::KindRole).toString()
                             == QStringLiteral("file")) {
                             REQUIRE(facade.webLibrary()->playPage(
                                 pages->data(pages->index(r), MediaBrowserModel::IdRole)
                                     .toLongLong()));
                             break;
                         }
                     }
                 }});
    // The pages' thumbnails, as icons.
    const auto browser_drawn = [](F& facade, OperatorUi& ui) {
        set(component(ui, "LibraryPanel"), "tabIndex", 6);
        facade.webLibrary()->thumbs().set_settle_ms(0);
        facade.set_web_renderer_factory([] { return std::make_unique<ShotWebPage>(); });
        for (int i = 0; i < 200 && facade.webLibrary()->thumbs().pending() > 0; ++i) {
            QTest::qWait(10);
        }
        QTest::qWait(50);
    };
    s.push_back({"main_browser_icons", [browser_drawn](F& facade, OperatorUi& ui, const L&) {
                     facade.media()->setMediaViewMode(QStringLiteral("icons"));
                     browser_drawn(facade, ui);
                 }});
    // Sources found on the network; one live in the Controller.
    s.push_back({"main_live", [](F& facade, OperatorUi& ui, const L&) {
                     facade.liveLibrary()->set_finder([] {
                         return std::vector<seventhslide::model::LiveSource>{
                             {"ndi", "STAGE-PC (Camera 1)"},
                             {"ndi", "STAGE-PC (Camera 2)"},
                             {"ndi", "LOBBY (Slides)"}};
                     });
                     facade.set_live_receiver_factory([](const seventhslide::model::LiveSource& src,
                                                         seventhslide::app::LiveQuality) {
                         return std::unique_ptr<seventhslide::app::LiveReceiver>(
                             std::make_unique<ShotCamera>(src));
                     });
                     REQUIRE(facade.liveLibrary()
                                 ->saveStream({}, QStringLiteral("Overflow room"),
                                              QStringLiteral("srt://overflow.local:9000"))
                                 .contains(QStringLiteral("key")));
                     set(component(ui, "LibraryPanel"), "tabIndex", 7);
                     REQUIRE(facade.liveLibrary()->playSource(QStringLiteral("ndi"),
                                                              QStringLiteral("STAGE-PC (Camera 1)")));
                     QTest::qWait(50);
                 }});
    // A conversion and a download at once: both in the list.
    s.push_back({"main_working", [](F& facade, OperatorUi& ui, const L&) {
                     const fs::path bin = fs::temp_directory_path() / "seventhslide-shots-work";
                     fs::remove_all(bin);
                     fs::create_directories(bin);
                     const auto script = [&bin](const char* name, const char* body) {
                         (void)seventhslide::test::write_fake_tool(bin, name, body);
                     };
                     script("ffprobe",
                            "echo '{\"streams\":[{\"codec_type\":\"video\",\"width\":1920,"
                            "\"height\":1080}],\"format\":{\"duration\":\"2.0\"}}'");
                     script("ffmpeg", "[ \"$1\" = -version ] && exit 0\n"
                                      "echo out_time_us=1200000\nsleep 60");
                     script("yt-dlp", "[ \"$1\" = --version ] && exit 0\n"
                                      "echo 'SSPROG 30 100 NA NA NA NA'\nsleep 60");
                     seventhslide::platform::ToolSearch search;
                     search.platform_id = seventhslide::test::tool_platform();
                     search.path_env = bin.string();
                     search.tools_dir = bin / "tools";
                     facade.tools()->set_search(search);
                     const auto videos = facade.media()->media_repo()->list_files(
                         seventhslide::db::MediaKind::Video);
                     REQUIRE_FALSE(videos.empty());
                     facade.convert()->convertMedia(
                         MediaKindQml::Video, {QString::fromStdString(videos.front().filename)},
                         {{QStringLiteral("format"), QStringLiteral("mp4")},
                          {QStringLiteral("height"), 720}},
                         -1, false);
                     facade.download()->downloadUrl(QStringLiteral("https://v.example/abc"), {},
                                                    QStringLiteral("Sunday Highlights"), -1, -1);
                     REQUIRE(QTest::qWaitFor(
                         [&facade] {
                             return facade.convert()->progress() > 0.0
                                    && facade.download()->progress() > 0.0;
                         },
                         5000));
                     Shooter::settle(*ui.window());
                     QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("jobsMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }});
    s.push_back({"main_outputs_left", [](F&, OperatorUi& ui, const L&) {
                     ui.dock_layout()->movePanel(QStringLiteral("outputs"), QStringLiteral("left"),
                                                 QStringLiteral("library"));
                 }});
    // A stack with Preview on: one song live, the next cued beside it, and
    // Transition between them.
    s.push_back({"main_preview", [](F& facade, OperatorUi&, const L&) {
                     const auto main = facade.stacks()->focusedStackId();
                     REQUIRE(facade.stacks()->setStackPreview(main, true).isEmpty());
                     REQUIRE(facade.live()->selectServiceItem(0, /*live=*/true));
                     REQUIRE(facade.live()->selectServiceItem(1));
                     REQUIRE(facade.preview()->nextSlide());
                 }, 1600, 1000});
    // The same with Transition's menu open (Cut, Fade, the Fade's length),
    // and what is live blanked, to go back to the Preview so at the take.
    s.push_back({"main_preview_take_menu", [](F& facade, OperatorUi& ui, const L&) {
                     const auto main = facade.stacks()->focusedStackId();
                     REQUIRE(facade.stacks()->setStackPreview(main, true).isEmpty());
                     REQUIRE(facade.live()->selectServiceItem(0, /*live=*/true));
                     REQUIRE(facade.live()->selectServiceItem(1));
                     REQUIRE(facade.stacks()->toggleStackBlank(main));
                     Shooter::settle(*ui.window());
                     QObject* menu = ui.window()->findChild<QObject*>(QStringLiteral("takeMenu"));
                     REQUIRE(menu != nullptr);
                     call(menu, "openAt");
                 }, 1600, 1000});
    s.push_back({"live_bible", [](F& facade, OperatorUi&, const L&) {
                     REQUIRE(facade.live()->selectServiceItem(3));
                 }});
    s.push_back({"live_image", [](F& facade, OperatorUi&, const L&) {
                     REQUIRE(facade.live()->selectServiceItem(4));
                 }});
    s.push_back({"live_image_folder", [](F& facade, OperatorUi&, const L&) {
                     REQUIRE(facade.live()->selectServiceItem(5));
                 }});
    s.push_back({"live_freeform", [](F& facade, OperatorUi&, const L&) {
                     REQUIRE(facade.live()->selectServiceItem(6));
                 }});
    s.push_back({"live_audio", [](F& facade, OperatorUi&, const L&) {
                     REQUIRE(facade.live()->playAudio(QStringLiteral("Prelude in G.mp3")));
                 }});
    s.push_back({"live_blank", [](F& facade, OperatorUi&, const L&) {
                     facade.live()->toggleGlobalBlank();
                 }});
    s.push_back({"live_freeze", [](F& facade, OperatorUi&, const L&) {
                     facade.live()->toggleGlobalFreeze();
                 }});
    s.push_back({"empty_rundown", [](F& facade, OperatorUi&, const L&) {
                     REQUIRE(facade.library()->createService(QStringLiteral("New")) > 0);
                 }});

    // --- dialogs on the service side ---
    s.push_back({"service_picker", [](F&, OperatorUi& ui, const L&) {
                     call(component(ui, "ServicePickerDialog"), "show");
                 }});
    s.push_back({"service_picker_other", [](F&, OperatorUi& ui, const L& lib) {
                     QObject* dialog = component(ui, "ServicePickerDialog");
                     call(dialog, "show");
                     QObject* state = seventhslide::test::qml_object(dialog, "ServicesState");
                     REQUIRE(state != nullptr);
                     call(state, "select", QVariant::fromValue<qint64>(lib.other_service));
                 }});
    // A library of a season's services, the one open (and so picked)
    // heading the list.
    s.push_back({"service_picker_many", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.library()->openService(add_season_services(facade)));
                     call(component(ui, "ServicePickerDialog"), "show");
                 }});
    // The same, by name, A to Z from a click on Name.
    s.push_back({"service_picker_by_name", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.library()->openService(add_season_services(facade)));
                     QObject* dialog = component(ui, "ServicePickerDialog");
                     call(dialog, "show");
                     QObject* state = seventhslide::test::qml_object(dialog, "ServicesState");
                     REQUIRE(state != nullptr);
                     call(state, "sortBy", QStringLiteral("name"));
                 }});
    // The tag menu, its search narrowing the tags to those with "ve".
    s.push_back({"service_picker_tag_search", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.library()->openService(add_season_services(facade)));
                     QObject* dialog = component(ui, "ServicePickerDialog");
                     call(dialog, "show");
                     call(dialog, "toggleTagMenu");
                     set(dialog, "tagSearch", QStringLiteral("ve"));
                 }});
    // Two tags picked: the services with both, the menu open on their chips.
    s.push_back({"service_picker_two_tags", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.library()->openService(add_season_services(facade)));
                     QObject* dialog = component(ui, "ServicePickerDialog");
                     call(dialog, "show");
                     QObject* state = seventhslide::test::qml_object(dialog, "ServicesState");
                     REQUIRE(state != nullptr);
                     call(state, "toggleTag", QStringLiteral("Vespers"));
                     call(state, "toggleTag", QStringLiteral("Youth"));
                     call(dialog, "toggleTagMenu");
                 }});
    // Dozens of tags: the tag menu scrolls.
    s.push_back({"service_picker_many_tags", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.library()->openService(add_season_services(facade)));
                     auto* library = facade.library();
                     const QStringList names = {
                         "Advent", "Anniversary", "Baby Dedication", "Baptism", "Camp Meeting",
                         "Children", "Choir", "Communion", "Community", "Concert", "Conference",
                         "Dedication", "Evangelism", "Family", "Funeral", "Graduation", "Guest Speaker",
                         "Harvest", "Health", "Holy Week", "Lent", "Men", "Mission", "Music",
                         "New Year", "Ordination", "Pathfinders", "Pentecost", "Prayer", "Seniors",
                         "Stewardship", "Thanksgiving", "Wedding", "Women", "Worship Night",
                         "Young Adults"};
                     const qint64 id = library->createService(QStringLiteral("Everything Sabbath"));
                     REQUIRE(id > 0);
                     REQUIRE(library->setServiceTags(id, names).isEmpty());
                     QObject* dialog = component(ui, "ServicePickerDialog");
                     call(dialog, "show");
                     call(dialog, "toggleTagMenu");
                 }});
    // Dozens of tags, several picked: the chips wrap, and the list scrolls
    // with the keys on one.
    s.push_back({"service_picker_many_tags_picked", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.library()->openService(add_season_services(facade)));
                     auto* library = facade.library();
                     const QStringList names = {
                         "Advent", "Anniversary", "Baby Dedication", "Baptism", "Camp Meeting",
                         "Children", "Choir", "Communion", "Community", "Concert", "Conference",
                         "Dedication", "Evangelism", "Family", "Funeral", "Graduation", "Guest Speaker",
                         "Harvest", "Health", "Holy Week", "Lent", "Men", "Mission", "Music",
                         "New Year", "Ordination", "Pathfinders", "Pentecost", "Prayer", "Seniors",
                         "Stewardship", "Thanksgiving", "Wedding", "Women", "Worship Night",
                         "Young Adults"};
                     const qint64 id = library->createService(QStringLiteral("Everything Sabbath"));
                     REQUIRE(id > 0);
                     REQUIRE(library->setServiceTags(id, names).isEmpty());
                     QObject* dialog = component(ui, "ServicePickerDialog");
                     call(dialog, "show");
                     QObject* state = seventhslide::test::qml_object(dialog, "ServicesState");
                     REQUIRE(state != nullptr);
                     // Of the first 24 (a service's most), so all are its.
                     for (const auto* name : {"Advent", "Baptism", "Children", "Communion", "Family",
                                              "Guest Speaker", "Harvest", "Holy Week", "Mission",
                                              "Music"}) {
                         call(state, "toggleTag", QString::fromLatin1(name));
                     }
                     call(dialog, "toggleTagMenu");
                     call(dialog, "stepTagCursor", 1);
                     call(dialog, "stepTagCursor", 4);
                 }});
    s.push_back({"service_picker_empty", [](F& facade, OperatorUi& ui, const L& lib) {
                     REQUIRE(facade.library()->deleteService(lib.service).isEmpty());
                     REQUIRE(facade.library()->deleteService(lib.other_service).isEmpty());
                     call(component(ui, "ServicePickerDialog"), "show");
                 }});
    s.push_back({"service_picker_tag", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "ServicePickerDialog");
                     call(dialog, "show");
                     QObject* state = seventhslide::test::qml_object(dialog, "ServicesState");
                     REQUIRE(state != nullptr);
                     call(state, "toggleTag", QStringLiteral("Easter"));
                 }});
    // Settings over the Services window, on the service picked there.
    s.push_back({"service_picker_settings", [](F&, OperatorUi& ui, const L& lib) {
                     call(component(ui, "ServicePickerDialog"), "show");
                     call(component(ui, "ServiceSettingsDialog"), "openFor",
                          QVariant::fromValue<qint64>(lib.other_service), false);
                 }});
    s.push_back({"service_settings_general", [](F&, OperatorUi& ui, const L&) {
                     call(component(ui, "ServiceSettingsDialog"), "openDialog");
                 }});
    // Its General page at its foot: the service's own audio fades, one set.
    s.push_back({"service_settings_audio", [](F& facade, OperatorUi& ui, const L& lib) {
                     REQUIRE(facade.library()
                                 ->setServiceAudioFades(lib.service,
                                                        {{QStringLiteral("fade_in_ms"), 1000}})
                                 .isEmpty());
                     call(component(ui, "ServiceSettingsDialog"), "openDialog");
                     Shooter::settle(*ui.window());
                     QObject* page = seventhslide::test::qml_object(
                         component(ui, "ServiceSettingsDialog"), "ServiceGeneralPage");
                     REQUIRE(page != nullptr);
                     set(page, "contentY", std::max(0.0, page->property("contentHeight").toReal()
                                                             - page->property("height").toReal()));
                 }});
    s.push_back({"service_settings_themes", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "ServiceSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("themes"));
                 }});
    s.push_back({"service_settings_export", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "ServiceSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("export"));
                 }});
    for (const char* tab : {"lyrics", "authors", "topics", "themes"}) {
        s.push_back({std::string("song_edit_") + tab, [tab](F&, OperatorUi& ui, const L& lib) {
                         QObject* dialog = component(ui, "SongEditDialog");
                         call(dialog, "openLibrary", QVariant::fromValue<qint64>(lib.first_song));
                         set(dialog, "tab", QString::fromLatin1(tab));
                     }});
    }
    // A song too long for either lyrics pane: both scroll, their bars alike.
    s.push_back({"song_edit_lyrics_long", [](F& facade, OperatorUi& ui, const L&) {
                     seventhslide::model::Song song;
                     song.title = "Long Song";
                     std::string order;
                     for (int v = 1; v <= 8; ++v) {
                         song.lyrics += "---[Verse:" + std::to_string(v) + "]---\n";
                         for (int line = 1; line <= (v == 1 ? 24 : 4); ++line) {
                             song.lyrics += "Verse " + std::to_string(v) + ", line "
                                            + std::to_string(line) + "\n";
                         }
                         order += (v > 1 ? " v" : "v") + std::to_string(v);
                     }
                     song.verse_order = order;
                     const auto id = facade.song_repo()->insert(song);
                     QObject* dialog = component(ui, "SongEditDialog");
                     call(dialog, "openLibrary", QVariant::fromValue<qint64>(id));
                 }});
    // The order's menus: its arrangements, and a title or theme change to add.
    for (const char* name : {"arrangementMenu", "addChipMenu"}) {
        s.push_back({std::string("song_edit_") + name, [name](F&, OperatorUi& ui, const L& lib) {
                         QObject* dialog = component(ui, "SongEditDialog");
                         call(dialog, "openLibrary", QVariant::fromValue<qint64>(lib.first_song));
                         Shooter::settle(*ui.window());
                         QObject* menu = ui.window()->findChild<QObject*>(QString::fromLatin1(name));
                         REQUIRE(menu != nullptr);
                         call(menu, "openAt");
                     }});
    }
    s.push_back({"song_edit_author_open", [](F&, OperatorUi& ui, const L& lib) {
                     QObject* dialog = component(ui, "SongEditDialog");
                     call(dialog, "openLibrary", QVariant::fromValue<qint64>(lib.first_song));
                     set(dialog, "tab", QStringLiteral("authors"));
                     QObject* state = seventhslide::test::qml_object(dialog, "SongEditState");
                     REQUIRE(state != nullptr);
                     call(state, "selectAuthor", 0);
                     call(state, "toggleSelectedAuthorRole", QStringLiteral("words"));
                 }});
    s.push_back({"song_edit_theme_change", [](F&, OperatorUi& ui, const L& lib) {
                     QObject* dialog = component(ui, "SongEditDialog");
                     call(dialog, "openLibrary", QVariant::fromValue<qint64>(lib.first_song));
                     QObject* state = seventhslide::test::qml_object(dialog, "SongEditState");
                     REQUIRE(state != nullptr);
                     call(state, "addThemeChip");
                     call(dialog, "showTab", QStringLiteral("themes"));
                 }});
    s.push_back({"song_edit_xml", [](F&, OperatorUi& ui, const L& lib) {
                     QObject* dialog = component(ui, "SongEditDialog");
                     call(dialog, "openLibrary", QVariant::fromValue<qint64>(lib.first_song));
                     QObject* state = seventhslide::test::qml_object(dialog, "SongEditState");
                     REQUIRE(state != nullptr);
                     call(state, "setLyricsMode", QStringLiteral("raw"));
                 }});
    s.push_back({"song_edit_chords", [](F&, OperatorUi& ui, const L& lib) {
                     QObject* dialog = component(ui, "SongEditDialog");
                     call(dialog, "openLibrary", QVariant::fromValue<qint64>(lib.first_song));
                     QObject* state = seventhslide::test::qml_object(dialog, "SongEditState");
                     REQUIRE(state != nullptr);
                     // A chord on the first word, the cursor in it, once the
                     // verse editor has loaded the verse.
                     QTest::qWait(50);
                     QObject* editor = seventhslide::test::qml_object(dialog, "SongVerseEditor");
                     QObject* panel = seventhslide::test::qml_object(dialog, "SongLyricsPanel");
                     REQUIRE(editor != nullptr);
                     REQUIRE(panel != nullptr);
                     auto* text = editor->property("textEdit").value<QObject*>();
                     REQUIRE(text != nullptr);
                     set(text, "cursorPosition", 2);
                     call(panel, "applyVerseChord", QStringLiteral("G"));
                 }});
    s.push_back({"song_edit_rundown_item", [](F& facade, OperatorUi& ui, const L&) {
                     const auto id = facade.serviceItems()->data(
                         facade.serviceItems()->index(0, 0),
                         seventhslide::app::ServiceItemModel::ItemIdRole);
                     call(component(ui, "SongEditDialog"), "openItem", id);
                 }});
    s.push_back({"song_edit_new", [](F&, OperatorUi& ui, const L&) {
                     call(component(ui, "SongEditDialog"), "openNewLibrary");
                 }});

    // --- Convert, on files a probe has read ---
    // The card's picture: a sunrise of sorts, the same on every run.
    const QString frame_url = [] {
        const fs::path path = fs::temp_directory_path() / "seventhslide-shots-frame.png";
        QImage img(320, 180, QImage::Format_RGB32);
        QPainter p(&img);
        QLinearGradient sky(0, 0, 0, 180);
        sky.setColorAt(0.0, QColor(36, 52, 104));
        sky.setColorAt(0.65, QColor(226, 132, 72));
        sky.setColorAt(1.0, QColor(58, 36, 38));
        p.fillRect(img.rect(), sky);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 214, 150));
        p.drawEllipse(QPointF(160, 128), 26, 26);
        p.fillRect(QRect(0, 138, 320, 42), QColor(40, 30, 34));
        p.end();
        REQUIRE(img.save(QString::fromStdString(path.string()), "PNG"));
        return QUrl::fromLocalFile(QString::fromStdString(path.string())).toString();
    }();
    const auto convert_scene = [](const char* kind, QVariantList files, QVariantMap probe,
                                  QVariantMap choices) {
        return [kind, files, probe, choices](F&, OperatorUi& ui, const L&) {
            QObject* dialog = component(ui, "ConvertDialog");
            call(dialog, "openFor", QString::fromLatin1(kind), files);
            call(dialog, "setProbe", probe, QString{});
            for (auto it = choices.cbegin(); it != choices.cend(); ++it) {
                set(dialog, it.key().toLatin1().constData(), it.value());
            }
        };
    };
    const auto file = [](const char* title, const char* filename) {
        return QVariantMap{{QStringLiteral("title"), QString::fromUtf8(title)},
                           {QStringLiteral("filename"), QString::fromLatin1(filename)}};
    };
    const QVariantMap hd_video{{QStringLiteral("width"), 1920},
                               {QStringLiteral("height"), 1080},
                               {QStringLiteral("fps"), 29.97},
                               {QStringLiteral("duration"), 12.5},
                               {QStringLiteral("size"), 4500000},
                               {QStringLiteral("hasVideo"), true},
                               {QStringLiteral("hasAudio"), true},
                               {QStringLiteral("videoCodec"), QStringLiteral("H.264")},
                               {QStringLiteral("audioCodec"), QStringLiteral("AAC")},
                               {QStringLiteral("sampleRate"), 48000},
                               {QStringLiteral("channels"), 2},
                               {QStringLiteral("frame"), frame_url}};
    s.push_back({"convert_video",
                 convert_scene("video", {file("Sunrise.mp4", "3f2a.mp4")}, hd_video,
                               {{QStringLiteral("targetHeight"), 720}})});
    s.push_back({"convert_video_replace",
                 convert_scene("video", {file("Sunrise.mp4", "3f2a.mp4")}, hd_video,
                               {{QStringLiteral("targetHeight"), 720},
                                {QStringLiteral("format"), QStringLiteral("webm")},
                                {QStringLiteral("replaceOriginal"), true}})});
    s.push_back({"convert_videos",
                 convert_scene("video",
                               {file("Sunrise.mp4", "3f2a.mp4"), file("Ocean.mov", "77b1.mov"),
                                file("Countdown.mp4", "c09e.mp4")},
                               hd_video, {{QStringLiteral("targetHeight"), 720}})});
    // A phone's video, upright: 1080p still, made 720p.
    QVariantMap tall_video = hd_video;
    tall_video[QStringLiteral("width")] = 1080;
    tall_video[QStringLiteral("height")] = 1920;
    tall_video[QStringLiteral("duration")] = 48.0;
    tall_video[QStringLiteral("size")] = 61000000;
    s.push_back({"convert_video_tall",
                 convert_scene("video", {file("Testimony.mp4", "b72e.mp4")}, tall_video,
                               {{QStringLiteral("targetHeight"), 720}})});
    // An exact size, and the codecs, open under Advanced.
    s.push_back({"convert_video_custom", [file, hd_video](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "ConvertDialog");
                     call(dialog, "openFor", QStringLiteral("video"),
                          QVariantList{file("Sunrise.mp4", "3f2a.mp4")});
                     call(dialog, "setProbe", hd_video, QString{});
                     call(dialog, "chooseCustom");
                     call(dialog, "setCustomWidth", 1600);
                     set(dialog, "advanced", true);
                     set(dialog, "videoCodec", QStringLiteral("h265"));
                 }});
    // A video made into sound: to Audio.
    s.push_back({"convert_video_to_audio",
                 convert_scene("video", {file("Sunday Sermon.mp4", "9a1c.mp4")}, hd_video,
                               {{QStringLiteral("format"), QStringLiteral("m4a")},
                                {QStringLiteral("advanced"), true},
                                {QStringLiteral("audioCodec"), QStringLiteral("copy")}})});
    s.push_back({"convert_audio",
                 convert_scene("audio", {file("Prelude in G.flac", "a1d4.flac")},
                               {{QStringLiteral("duration"), 200.0},
                                {QStringLiteral("size"), 24000000},
                                {QStringLiteral("hasAudio"), true},
                                {QStringLiteral("audioCodec"), QStringLiteral("FLAC")},
                                {QStringLiteral("sampleRate"), 44100},
                                {QStringLiteral("channels"), 2}},
                               {{QStringLiteral("advanced"), true},
                                {QStringLiteral("channels"), 1}})});
    s.push_back({"convert_image",
                 convert_scene("image",
                               {QVariantMap{{QStringLiteral("title"),
                                             QStringLiteral("Easter Sunrise.png")},
                                            {QStringLiteral("filename"),
                                             QStringLiteral("e5c2.png")},
                                            {QStringLiteral("thumb"), frame_url}}},
                               {{QStringLiteral("width"), 4032},
                                {QStringLiteral("height"), 3024},
                                {QStringLiteral("size"), 9800000},
                                {QStringLiteral("hasVideo"), true},
                                {QStringLiteral("videoCodec"), QStringLiteral("PNG")}},
                               {{QStringLiteral("targetHeight"), 1080}})});

    // --- Download from the internet, in each state its dialog can be in ---
    const auto download_scene = [](QVariantMap state) {
        return [state](F&, OperatorUi& ui, const L&) {
            QObject* dialog = component(ui, "DownloadDialog");
            call(dialog, "openDialog", false);
            for (auto it = state.cbegin(); it != state.cend(); ++it) {
                set(dialog, it.key().toLatin1().constData(), it.value());
            }
        };
    };
    const QVariantMap looked_up{
        {QStringLiteral("ok"), true},
        {QStringLiteral("playlist"), false},
        {QStringLiteral("title"), QStringLiteral("Sunday Highlights")},
        {QStringLiteral("uploader"), QStringLiteral("Grace Church")},
        {QStringLiteral("duration"), 245.0},
        {QStringLiteral("heights"), QVariantList{1080, 720, 480, 360}},
        {QStringLiteral("hasVideo"), true},
        {QStringLiteral("entries"), QVariantList{}}};
    s.push_back({"download_video",
                 download_scene({{QStringLiteral("url"),
                                  QStringLiteral("https://www.youtube.com/watch?v=abc123")},
                                 {QStringLiteral("info"), looked_up}})});
    QVariantMap playlist = looked_up;
    playlist[QStringLiteral("playlist")] = true;
    playlist[QStringLiteral("title")] = QStringLiteral("Easter 2026");
    playlist[QStringLiteral("heights")] = QVariantList{};
    playlist[QStringLiteral("entries")] =
        QVariantList{QVariantMap{{QStringLiteral("title"), QStringLiteral("Prelude")}},
                     QVariantMap{{QStringLiteral("title"), QStringLiteral("Sermon")}},
                     QVariantMap{{QStringLiteral("title"), QStringLiteral("Postlude")}}};
    s.push_back({"download_advanced",
                 download_scene({{QStringLiteral("url"),
                                  QStringLiteral("https://www.youtube.com/watch?v=abc123")},
                                 {QStringLiteral("info"), looked_up},
                                 {QStringLiteral("get"), QStringLiteral("audio")},
                                 {QStringLiteral("advanced"), true},
                                 {QStringLiteral("audioFormat"), QStringLiteral("mp3")}})});
    s.push_back({"download_playlist",
                 download_scene({{QStringLiteral("url"),
                                  QStringLiteral("https://www.youtube.com/playlist?list=PL1")},
                                 {QStringLiteral("info"), playlist},
                                 {QStringLiteral("get"), 720}})});
    s.push_back({"download_missing",
                 download_scene({{QStringLiteral("url"), QStringLiteral("https://v.example/abc")},
                                 {QStringLiteral("error"),
                                  QStringLiteral("Downloading needs yt-dlp, which is not "
                                                 "installed. See Settings \u203a Tools.")},
                                 {QStringLiteral("missing"), true}})});
    s.push_back({"download_update",
                 download_scene({{QStringLiteral("url"), QStringLiteral("https://v.example/abc")},
                                 {QStringLiteral("error"),
                                  QStringLiteral("[youtube] abc123: Unable to extract uploader id")},
                                 {QStringLiteral("updateSuggested"), true}})});

    // --- settings ---
    for (const char* tab : {"general", "library", "outputs", "stacks", "remote", "bibles", "fonts"}) {
        s.push_back({std::string("settings_") + tab, [tab](F&, OperatorUi& ui, const L&) {
                         QObject* dialog = component(ui, "AppSettingsDialog");
                         call(dialog, "openDialog");
                         set(dialog, "tab", QString::fromLatin1(tab));
                     }});
    }

    // Fonts: one the library names and has none for, one it holds, and what
    // files added came to. Nothing live, as Add Fonts would refuse. The library
    // takes its copy of the shipped fonts on a worker: the page waits for it,
    // or a shot shows them or not by how the race went.
    const auto font_page = [](F& facade, OperatorUi& ui) {
        facade.fonts()->set_may_reload([] { return true; });
        auto& db = *facade.open_library().database();
        REQUIRE(QTest::qWaitFor([&db] { return seventhslide::db::FontRepository(db).bundled_taken(); },
                                20000));
        db.exec(R"(INSERT INTO freeform_items (name, document)
                   VALUES ('one', '{"objects":[{"font_family":"Gotham"}]}'))");
        seventhslide::app::FontStore store(db, facade.media_root());
        REQUIRE(store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf"))
                    .error.empty());
        REQUIRE(store.import_file(seventhslide::test::fixture_font("SSFixtureSans-Bold.ttf"))
                    .error.empty());
        facade.fonts()->library_fonts_changed();
        QObject* dialog = component(ui, "AppSettingsDialog");
        call(dialog, "openDialog");
        set(dialog, "tab", QStringLiteral("fonts"));
    };
    s.push_back({"settings_fonts_library", [font_page](F& facade, OperatorUi& ui, const L&) {
                     font_page(facade, ui);
                     call(state_of(ui, "AppSettingsDialog", "SettingsFontsState"), "selectFamily",
                          QStringLiteral("SSFixture Sans"));
                 }});
    s.push_back({"settings_fonts_selected", [font_page](F& facade, OperatorUi& ui, const L&) {
                     font_page(facade, ui);
                     auto* state = state_of(ui, "AppSettingsDialog", "SettingsFontsState");
                     call(state, "selectFamily", QStringLiteral("SSFixture Sans"));
                     const auto faces = state->property("shownFaces").toList();
                     REQUIRE(faces.size() == 2);
                     auto* selection = state->property("faceSelection").value<QObject*>();
                     REQUIRE(selection != nullptr);
                     REQUIRE(QMetaObject::invokeMethod(
                         selection, "selectOnly",
                         Q_ARG(QVariant, faces.back().toMap().value(QStringLiteral("filename")))));
                 }});
    // The missing family, and where it is used: one of each kind.
    s.push_back({"settings_fonts_missing", [font_page](F& facade, OperatorUi& ui, const L&) {
                     auto& db = *facade.open_library().database();
                     REQUIRE_FALSE(facade.themes()
                                       ->create(QStringLiteral("Main"), QStringLiteral("song"),
                                                QStringLiteral("Lower Third"),
                                                QVariantMap{{QStringLiteral("elements"),
                                                             QVariantMap{{QStringLiteral("body"),
                                                                          QVariantMap{{QStringLiteral(
                                                                                           "verse_font_family"),
                                                                                       QStringLiteral("Gotham")}}}}}})
                                       .value(QStringLiteral("id"))
                                       .toString()
                                       .isEmpty());
                     db.exec(R"(INSERT INTO songs (title, lyrics)
                                VALUES ('Morning Has Broken', '<font=Gotham>Morning</font> has broken'))");
                     db.exec("INSERT INTO services (name) VALUES ('Easter Sunday')");
                     db.exec(R"(INSERT INTO service_items (service_id, order_num, item_type, data)
                                VALUES (last_insert_rowid(), 1, 'song',
                                        '{"title":"Morning Has Broken","lyrics":"<font=Gotham>Morning</font>"}'))");
                     font_page(facade, ui);
                 }});
    s.push_back({"settings_fonts_results", [font_page](F& facade, OperatorUi& ui, const L&) {
                     font_page(facade, ui);
                     const QVariantList results{
                         QVariantMap{{QStringLiteral("file"), QStringLiteral("Lato-Regular.ttf")},
                                     {QStringLiteral("ok"), true},
                                     {QStringLiteral("text"), QStringLiteral("Added Lato (Regular).")}},
                         QVariantMap{{QStringLiteral("file"), QStringLiteral("Damaged.ttf")},
                                     {QStringLiteral("ok"), false},
                                     {QStringLiteral("text"), QStringLiteral("This font file is damaged.")}}};
                     REQUIRE(QMetaObject::invokeMethod(
                         state_of(ui, "AppSettingsDialog", "SettingsFontsState"), "resultsShown",
                         Q_ARG(QVariant, results), Q_ARG(QString, QString())));
                 }});

    // The media tools: two found, with their versions, and one not. Stand-ins
    // at a fixed path, so the page reads the same on every run.
    s.push_back({"settings_tools", [](F& facade, OperatorUi& ui, const L&) {
                     const fs::path root = fs::temp_directory_path() / "seventhslide-shots-tools";
                     fs::remove_all(root);
                     const auto script = [&root](const char* name, const char* body) {
                         (void)seventhslide::test::write_fake_tool(root / "bin", name, body);
                     };
                     script("ffmpeg", "echo 'ffmpeg version 7.1.1 Copyright (c) 2000-2025 "
                                      "the FFmpeg developers'");
                     script("yt-dlp", "echo 2025.10.22");
                     seventhslide::platform::ToolSearch search;
                     search.platform_id = seventhslide::test::tool_platform();
                     search.path_env = (root / "bin").string();
                     search.tools_dir = root / "tools";
                     facade.tools()->set_search(search);
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("tools"));
                     REQUIRE(QTest::qWaitFor(
                         [&facade] {
                             return facade.tools()->refreshed() && !facade.tools()->probing();
                         },
                         5000));
                 }});

    // The web remote running: its address and QR code.
    s.push_back({"settings_remote_running", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.updateAppSettings({{QStringLiteral("remoteEnabled"), true},
                                                       {QStringLiteral("remotePort"), 18977}})
                                 .isEmpty());
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("remote"));
                     QObject* state = state_of(ui, "AppSettingsDialog", "SettingsRemoteState");
                     REQUIRE(QTest::qWaitFor(
                         [&] { return !state->property("address").toString().isEmpty(); }, 5000));
                 }});
    // Rows opened in place: a stack with what it receives and a Preview, and
    // a Bible.
    s.push_back({"settings_stacks_open", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower Thirds"),
                                                       QStringLiteral("titles")).isEmpty());
                     const QString id = facade.stacks()->stacks().back().toMap()
                                            .value(QStringLiteral("id")).toString();
                     REQUIRE(facade.stacks()->setStackTypeDefault(QStringLiteral("bible"), id)
                                 .isEmpty());
                     REQUIRE(facade.stacks()->setStackPreview(id, true).isEmpty());
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("stacks"));
                     set(state_of(ui, "AppSettingsDialog", "SettingsStacksState"),
                         "selectedStackId", id);
                 }});
    // Several outputs, the middle one picked (it can move either way), the
    // last switched off.
    s.push_back({"settings_outputs_open", [](F& facade, OperatorUi& ui, const L&) {
                     for (const char* name : {"Stage", "Livestream"}) {
                         REQUIRE(facade.outputSettings()
                                     ->createOutput(QString::fromLatin1(name))
                                     .contains(QStringLiteral("name")));
                     }
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("outputs"));
                     REQUIRE(facade.outputSettings()
                                 ->setOutputOn(QStringLiteral("Livestream"), false)
                                 .isEmpty());
                     set(state_of(ui, "AppSettingsDialog", "SettingsOutputsState"),
                         "selectedId", facade.outputSettings()
                                           ->getOutputSettings(QStringLiteral("Stage"))
                                           .value(QStringLiteral("id")));
                 }});
    s.push_back({"settings_bibles_open", [](F&, OperatorUi& ui, const L& lib) {
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("bibles"));
                     set(state_of(ui, "AppSettingsDialog", "SettingsBiblesState"),
                         "selectedBibleId", QVariant::fromValue(static_cast<qint64>(lib.bible)));
                 }});

    // Bibles with several translations, long names among them, one off.
    s.push_back({"settings_bibles_many", [](F& facade, OperatorUi& ui, const L& lib) {
                     auto* library = facade.library();
                     REQUIRE(library->setBibleAbbreviation(lib.bible, QStringLiteral("SB")).isEmpty());
                     seventhslide::model::BibleVerse verse;
                     verse.book = "John";
                     verse.chapter = 3;
                     verse.verse_num = 16;
                     verse.text = "For God so loved the world";
                     struct Bible {
                         const char* name;
                         const char* abbreviation;
                         bool on;
                     };
                     const Bible more[] = {
                         {"King James Version", "KJV", true},
                         {"English Standard Version", "ESV", true},
                         {"The Holy Bible, Berean Standard Bible", "BSB", true},
                         {"Reina-Valera 1960 (Spanish)", "RVR1960", false},
                     };
                     for (const auto& b : more) {
                         const auto id = facade.bible_repo()->import_bible(b.name, "", {verse});
                         REQUIRE(id >= 0);
                         REQUIRE(library->setBibleAbbreviation(id, QString::fromLatin1(b.abbreviation))
                                     .isEmpty());
                         if (!b.on)
                             REQUIRE(library->setBibleEnabled(id, false).isEmpty());
                     }
                     library->refresh_bibles();
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("bibles"));
                 }});
    // Bibles with a name open for editing in place.
    s.push_back({"settings_bibles_edit", [](F&, OperatorUi& ui, const L& lib) {
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("bibles"));
                     // The page is made as it first shows: its rows first.
                     Shooter::settle(*ui.window());
                     call(drawn_component(dialog, "SettingsBiblesPanel"), "beginEdit",
                          QVariant::fromValue(static_cast<qint64>(lib.bible)), QStringLiteral("name"));
                 }});

    // A stack's icon picker, open: the stacks' glyphs.
    s.push_back({"settings_stacks_icons", [](F& facade, OperatorUi& ui, const L&) {
                     REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower Thirds"),
                                                       QStringLiteral("titles")).isEmpty());
                     const QString id = facade.stacks()->stacks().back().toMap()
                                            .value(QStringLiteral("id")).toString();
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("stacks"));
                     set(state_of(ui, "AppSettingsDialog", "SettingsStacksState"),
                         "selectedStackId", id);
                     Shooter::settle(*ui.window());
                     call(drawn_component(dialog, "IconPickerButton"), "clicked");
                 }});

    // A stack's Default Reception checklist, open: on the fallback stack, what it has
    // is greyed and cannot be unticked.
    s.push_back({"settings_stacks_receives", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "AppSettingsDialog");
                     call(dialog, "openDialog");
                     set(dialog, "tab", QStringLiteral("stacks"));
                     Shooter::settle(*ui.window());
                     call(drawn_component(dialog, "ChecklistField"), "clicked");
                 }});

    // --- outputs and the theme designer ---
    for (const char* tab : {"general", "song", "bible", "media", "bg", "destinations"}) {
        s.push_back({std::string("output_") + tab, [tab](F&, OperatorUi& ui, const L&) {
                         call(component(ui, "OutputEditDialog"), "openFor",
                              QStringLiteral("Main"), QString::fromLatin1(tab));
                     }});
    }
    // The galleries with tagged themes; on the song one, the pointer on a
    // card, so its Default chip shows.
    for (const char* kind : {"song", "bg"}) {
        s.push_back({std::string("output_") + kind + "_gallery", [kind](F& facade, OperatorUi& ui, const L&) {
                         add_tagged_themes(facade, kind);
                         call(component(ui, "OutputEditDialog"), "openFor",
                              QStringLiteral("Main"), QString::fromLatin1(kind));
                         if (std::string_view(kind) != "song")
                             return;
                         Shooter::settle(*ui.window());
                         QQuickItem* name = seventhslide::test::visible_text_under(
                             ui.window()->contentItem(), QStringLiteral("Hymn Lyrics"));
                         REQUIRE(name != nullptr);
                         QTest::mouseMove(ui.window(), seventhslide::test::centre_of(name)
                                                           - QPoint(0, 40));
                     }});
    }
    // The output's icon picker, open: the outputs' glyphs.
    s.push_back({"output_icons", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("general"));
                     Shooter::settle(*ui.window());
                     call(drawn_component(dialog, "IconPickerButton"), "clicked");
                 }});
    // The canvas menu, open: the presets by shape, the canvas's own checked. A
    // taller window, so it opens under its field as on a desktop.
    s.push_back({"output_canvas_menu", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("general"));
                     Shooter::settle(*ui.window());
                     call(drawn_component(dialog, "DropdownField"), "clicked");
                 }, 1280, 900});
    // The widest preset name.
    s.push_back({"output_general_vertical", [](F&, OperatorUi& ui, const L&) {
                     call(component(ui, "OutputEditDialog"), "openFor", QStringLiteral("Main"),
                          QStringLiteral("general"));
                     QObject* state = state_of(ui, "OutputEditDialog", "OutputEditState");
                     set(state, "canvasW", QStringLiteral("1080"));
                     set(state, "canvasH", QStringLiteral("1920"));
                 }});
    // A canvas no preset is (a wide LED wall): Custom, its shape beside it.
    // The General page scrolled to its foot: the output's audio fades.
    s.push_back({"output_general_audio", [](F& facade, OperatorUi& ui, const L&) {
                     auto outputs = facade.session().outputs();
                     outputs.front().audio_fades = {500, 1500, 800};
                     facade.set_outputs(outputs);
                     call(component(ui, "OutputEditDialog"), "openFor", QStringLiteral("Main"),
                          QStringLiteral("general"));
                     Shooter::settle(*ui.window());
                     QObject* page = seventhslide::test::qml_object(component(ui, "OutputEditDialog"),
                                                                    "OutputGeneralPanel");
                     REQUIRE(page != nullptr);
                     set(page, "contentY", std::max(0.0, page->property("contentHeight").toReal()
                                                             - page->property("height").toReal()));
                 }});
    s.push_back({"output_general_custom", [](F&, OperatorUi& ui, const L&) {
                     call(component(ui, "OutputEditDialog"), "openFor", QStringLiteral("Main"),
                          QStringLiteral("general"));
                     QObject* state = state_of(ui, "OutputEditDialog", "OutputEditState");
                     set(state, "canvasW", QStringLiteral("2560"));
                     set(state, "canvasH", QStringLiteral("1080"));
                 }});
    // A destination opened in place.
    s.push_back({"output_destinations_open", [](F& facade, OperatorUi& ui, const L&) {
                     const QString out = QString::fromStdString(facade.session().outputs().front().id);
                     const QVariantMap added = facade.destinations()->add(out, QStringLiteral("display"));
                     REQUIRE_FALSE(added.contains(QStringLiteral("error")));
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
                     QObject* dests = state_of(ui, "OutputEditDialog", "OutputEditState")
                                          ->property("destState").value<QObject*>();
                     REQUIRE(dests != nullptr);
                     set(dests, "selectedDestId", added.value(QStringLiteral("id")));
                 }});
    // A desktop window, its window closed while it stays on.
    s.push_back({"output_destinations_window", [](F& facade, OperatorUi& ui, const L&) {
                     const QString out = QString::fromStdString(facade.session().outputs().front().id);
                     const QVariantMap added = facade.destinations()->add(out, QStringLiteral("window"));
                     REQUIRE_FALSE(added.contains(QStringLiteral("error")));
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
                     QObject* dests = state_of(ui, "OutputEditDialog", "OutputEditState")
                                          ->property("destState").value<QObject*>();
                     REQUIRE(dests != nullptr);
                     auto rows = facade.destinations()->list(out);
                     REQUIRE(rows.size() == 1);
                     QVariantMap row = rows.front().toMap();
                     row.insert(QStringLiteral("enabled"), true);
                     row.insert(QStringLiteral("closed"), true);
                     set(dests, "destinations", QVariantList{row});
                     set(dests, "selectedDestId", added.value(QStringLiteral("id")));
                 }});
    // The New menu, with every type this computer can have.
    s.push_back({"output_destinations_new_menu", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
                     Shooter::settle(*ui.window());
                     QObject* button = nullptr;
                     for (QObject* b : dialog->findChildren<QObject*>()) {
                         if (b->property("label").toString() == QStringLiteral("New")
                             && b->metaObject()->indexOfSignal("clicked()") >= 0) {
                             button = b;
                             break;
                         }
                     }
                     REQUIRE(button != nullptr);
                     QMetaObject::invokeMethod(button, "clicked");
                 }});
    // A destination sent smaller than the output, its size menu open.
    s.push_back({"output_destinations_size", [](F& facade, OperatorUi& ui, const L&) {
                     const QString out = QString::fromStdString(facade.session().outputs().front().id);
                     const QVariantMap added = facade.destinations()->add(out, QStringLiteral("display"));
                     REQUIRE_FALSE(added.contains(QStringLiteral("error")));
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
                     QObject* dests = state_of(ui, "OutputEditDialog", "OutputEditState")
                                          ->property("destState").value<QObject*>();
                     REQUIRE(dests != nullptr);
                     const QVariant id = added.value(QStringLiteral("id"));
                     set(dests, "selectedDestId", id);
                     call(dests, "setDestinationSize", id, 720);
                     Shooter::settle(*ui.window());
                     call(drawn_component(dialog, "DropdownField"), "clicked");
                 }});
    // A display's Monitor menu, open: this computer's screens, its own checked.
    s.push_back({"output_destinations_monitor_menu", [](F& facade, OperatorUi& ui, const L&) {
                     const QString out = QString::fromStdString(facade.session().outputs().front().id);
                     const QVariantMap added = facade.destinations()->add(out, QStringLiteral("display"));
                     REQUIRE_FALSE(added.contains(QStringLiteral("error")));
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
                     QObject* dests = state_of(ui, "OutputEditDialog", "OutputEditState")
                                          ->property("destState").value<QObject*>();
                     REQUIRE(dests != nullptr);
                     set(dests, "selectedDestId", added.value(QStringLiteral("id")));
                     Shooter::settle(*ui.window());
                     call(drawn_component(dialog, "DestinationPicker"), "clicked");
                 }});
    // A Blackmagic destination's Device menu, open on a computer without a
    // card: what it says when there is none. Blackmagic cannot be added
    // without Desktop Video, so the state is given one to show.
    s.push_back({"output_destinations_bmd_menu", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
                     QObject* dests = state_of(ui, "OutputEditDialog", "OutputEditState")
                                          ->property("destState").value<QObject*>();
                     REQUIRE(dests != nullptr);
                     set(dests, "shown", false);
                     const QVariantMap bmd{{QStringLiteral("id"), QStringLiteral("b1")},
                                           {QStringLiteral("type"), QStringLiteral("bmd")},
                                           {QStringLiteral("name"), QStringLiteral("DeckLink")},
                                           {QStringLiteral("enabled"), true}};
                     set(dests, "destinations", QVariantList{bmd});
                     set(dests, "selectedDestId", QStringLiteral("b1"));
                     Shooter::settle(*ui.window());
                     call(drawn_component(dialog, "DestinationPicker"), "clicked");
                 }});
    // An NDI source sent as sound alone. NDI cannot be added without its
    // runtime, so the state is given one to show.
    s.push_back({"output_destinations_ndi_audio", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
                     QObject* dests = state_of(ui, "OutputEditDialog", "OutputEditState")
                                          ->property("destState").value<QObject*>();
                     REQUIRE(dests != nullptr);
                     set(dests, "shown", false);
                     const QVariantMap ndi{{QStringLiteral("id"), QStringLiteral("n1")},
                                           {QStringLiteral("type"), QStringLiteral("ndi")},
                                           {QStringLiteral("name"), QStringLiteral("Stream")},
                                           {QStringLiteral("enabled"), true},
                                           {QStringLiteral("targetKey"), QStringLiteral("SeventhSlide Main")},
                                           // As a real NDI row says: it can send sound,
                                           // and does, alone.
                                           {QStringLiteral("audioCapable"), true},
                                           {QStringLiteral("audio"), true},
                                           {QStringLiteral("sizable"), true},
                                           {QStringLiteral("soundAloneCapable"), true},
                                           {QStringLiteral("maxHeight"), 1080},
                                           {QStringLiteral("video"), false}};
                     set(dests, "destinations", QVariantList{ndi});
                     set(dests, "selectedDestId", QStringLiteral("n1"));
                 }});
    // An SRT stream sending, as a real row says it.
    s.push_back({"output_destinations_srt", [](F&, OperatorUi& ui, const L&) {
                     QObject* dialog = component(ui, "OutputEditDialog");
                     call(dialog, "openFor", QStringLiteral("Main"), QStringLiteral("destinations"));
                     QObject* dests = state_of(ui, "OutputEditDialog", "OutputEditState")
                                          ->property("destState").value<QObject*>();
                     REQUIRE(dests != nullptr);
                     set(dests, "shown", false);
                     const QVariantMap stream{{QStringLiteral("mode"), QStringLiteral("listener")},
                                              {QStringLiteral("port"), 9000},
                                              {QStringLiteral("preset"), QStringLiteral("low_delay")},
                                              {QStringLiteral("codec"), QStringLiteral("h265")},
                                              {QStringLiteral("bitrateKbps"), 0},
                                              {QStringLiteral("encrypted"), false},
                                              {QStringLiteral("address"), QStringLiteral("srt://:9000")}};
                     const QVariantMap srt{{QStringLiteral("id"), QStringLiteral("s1")},
                                           {QStringLiteral("type"), QStringLiteral("srt")},
                                           {QStringLiteral("name"), QStringLiteral("Stream")},
                                           {QStringLiteral("enabled"), true},
                                           {QStringLiteral("live"), true},
                                           {QStringLiteral("targetKey"), QStringLiteral("srt://:9000")},
                                           {QStringLiteral("audioCapable"), true},
                                           {QStringLiteral("audio"), true},
                                           {QStringLiteral("sizable"), true},
                                           {QStringLiteral("video"), true},
                                           {QStringLiteral("stream"), stream},
                                           {QStringLiteral("streamState"), QStringLiteral("sending")},
                                           {QStringLiteral("encoder"), QStringLiteral("hevc_vaapi")},
                                           {QStringLiteral("hardwareEncoder"), true},
                                           {QStringLiteral("bitrateKbps"), 10000}};
                     set(dests, "streamCodecs",
                         QVariantList{QVariantMap{{QStringLiteral("key"), QStringLiteral("h265")},
                                                  {QStringLiteral("available"), true},
                                                  {QStringLiteral("hardware"), true}},
                                      QVariantMap{{QStringLiteral("key"), QStringLiteral("h264")},
                                                  {QStringLiteral("available"), true},
                                                  {QStringLiteral("hardware"), true}}});
                     set(dests, "destinations", QVariantList{srt});
                     set(dests, "selectedDestId", QStringLiteral("s1"));
                 }});
    // --- the slide editor on a theme ---
    const auto open_theme = [](F& facade, OperatorUi& ui, const char* kind) {
        QObject* dialog = component(ui, "OutputEditDialog");
        call(dialog, "openFor", QStringLiteral("Main"), QString::fromLatin1(kind));
        call(state_of(ui, "OutputEditDialog", "OutputEditState"), "enterDesign", first_theme_id(facade, kind));
        REQUIRE(facade.slideEdit()->active());
    };
    s.push_back({"designer_text", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                 }});
    s.push_back({"designer_text_selected", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     REQUIRE(facade.slideEdit()->objects()->insert(QStringLiteral("shape")).isEmpty());
                 }});
    for (const char* tab : {"insert", "design", "transitions"}) {
        s.push_back({std::string("designer_tab_") + tab, [tab, open_theme](F& facade, OperatorUi& ui, const L&) {
                         open_theme(facade, ui, "song");
                         REQUIRE(facade.slideEdit()->objects()->insert(QStringLiteral("text")).isEmpty());
                         set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "ribbonTab",
                             QString::fromLatin1(tab));
                     }});
    }
    // The lyrics box with chords on: the sidebar's Chords section, and the
    // preview paints the sample song's chords.
    s.push_back({"designer_chords", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     facade.slideEdit()->objects()->selectObject(QStringLiteral("te_lyrics"));
                     REQUIRE(facade.slideEdit()
                                 ->objects()
                                 ->setSelectedProperty(QStringLiteral("show_chords"), true)
                                 .isEmpty());
                 }});
    // Follow-lines on, the lyrics box selected: the Active line and Other
    // lines sections, and the preview paints the first line in its own type
    // (bold, larger) over the following two (smaller, slanted, grey).
    s.push_back({"designer_active_lines", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     facade.slideEdit()->theme()->setSetting(QStringLiteral("follow_lines"), 2);
                     facade.slideEdit()->objects()->selectObject(QStringLiteral("te_lyrics"));
                     const auto set = [&facade](const char* name, const QVariant& value) {
                         REQUIRE(facade.slideEdit()
                                     ->objects()
                                     ->setSelectedProperty(QString::fromLatin1(name), value)
                                     .isEmpty());
                     };
                     set("active_line_bold", true);
                     set("active_line_size", 64);
                     set("other_lines_size", 40);
                     set("other_lines_italic", true);
                     set("other_lines_color", "#9aa5b1");
                 }});
    // The Lyrics box selected: the most sections the sidebar shows.
    s.push_back({"designer_behavior_lyrics", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     facade.slideEdit()->objects()->selectObject(QStringLiteral("te_lyrics"));
                 }});
    s.push_back({"designer_indicator", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     REQUIRE(facade.slideEdit()
                                 ->objects()
                                 ->insert(QStringLiteral("indicator"),
                                          {{QStringLiteral("binding"), QStringLiteral("indicator")}})
                                 .isEmpty());
                     REQUIRE(facade.slideEdit()
                                 ->objects()
                                 ->setSelectedProperty(QStringLiteral("indicator_gap"), 2.5)
                                 .isEmpty());
                 }});
    // A video's design: its area filling the canvas.
    s.push_back({"designer_media", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     facade.slideEdit()->setContext(QStringLiteral("video"));
                 }});
    // A media design: its area as the output paints it before the picture
    // comes, under a shape placed over it.
    s.push_back({"designer_media_image", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "media");
                     facade.slideEdit()->setContext(QStringLiteral("image"));
                     REQUIRE(facade.slideEdit()->objects()->insert(QStringLiteral("shape")).isEmpty());
                 }});
    // Audio's: nothing to design, its slide change on the Transitions tab;
    // the other tabs' controls greyed.
    s.push_back({"designer_audio", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     facade.slideEdit()->setContext(QStringLiteral("audio"));
                     set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "ribbonTab",
                         QStringLiteral("transitions"));
                 }});
    s.push_back({"designer_bg", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "bg");
                 }});
    s.push_back({"designer_large", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                 },
                 1600, 1000});

    // --- the slide editor on a Freeform item ---
    s.push_back({"freeform_editor", [](F& facade, OperatorUi&, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                 }});
    s.push_back({"freeform_editor_selected", [](F& facade, OperatorUi&, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                     facade.slideEdit()->objects()->selectAll();
                 }});
    for (const char* tab : {"home", "insert", "design", "transitions", "cues"}) {
        s.push_back({std::string("freeform_tab_") + tab, [tab](F& facade, OperatorUi& ui,
                                                                const L& lib) {
                         REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                         facade.slideEdit()->objects()->selectAll();
                         set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "ribbonTab",
                             QString::fromLatin1(tab));
                     }});
    }
    // The Animations tab with the slide's builds in the Build pane: every
    // object fades in on one click, the first one picked.
    s.push_back({"freeform_animations", [](F& facade, OperatorUi& ui, const L& lib) {
                     auto* fe = facade.slideEdit();
                     REQUIRE(fe->openFreeform(lib.freeform));
                     fe->objects()->selectAll();
                     REQUIRE(fe->animations()->add(QStringLiteral("fade")).isEmpty());
                     QObject* state = state_of(ui, "SlideEditorWindow", "SlideEditState");
                     set(state, "ribbonTab", QStringLiteral("animations"));
                     set(state, "sidebarTab", QStringLiteral("build"));
                 }});
    // A video object, selected: the stage shows the library's picture of the
    // clip, the sidebar its Video section.
    s.push_back({"freeform_video", [](F& facade, OperatorUi&, const L& lib) {
                     auto* fe = facade.slideEdit();
                     REQUIRE(fe->openFreeform(lib.freeform));
                     REQUIRE(fe->objects()
                                 ->insert(QStringLiteral("video"),
                                          {{QStringLiteral("filename"), QStringLiteral("welcome loop.mp4")}})
                                 .isEmpty());
                     REQUIRE(fe->objects()
                                 ->setSelectedProperty(QStringLiteral("video_start"), QStringLiteral("on_click"))
                                 .isEmpty());
                 }});
    s.push_back({"freeform_drawn", [](F& facade, OperatorUi&, const L& lib) {
                     auto* fe = facade.slideEdit();
                     REQUIRE(fe->openFreeform(lib.freeform));
                     REQUIRE(fe->objects()
                                 ->insert(QStringLiteral("shape"), {{QStringLiteral("shapeKind"), QStringLiteral("cloud")}})
                                 .isEmpty());
                     REQUIRE(fe->objects()
                                 ->draw({QVariantList{1150, 650}, QVariantList{1450, 820}, QVariantList{1750, 640},
                                         QVariantList{1600, 950}},
                                        true, true)
                                 .isEmpty());
                     REQUIRE(fe->objects()
                                 ->draw({QVariantList{200, 900}, QVariantList{700, 760}}, false, false)
                                 .isEmpty());
                     REQUIRE(fe->objects()->setSelectedProperty(QStringLiteral("line_end"), QStringLiteral("arrow"))
                                 .isEmpty());
                 }});
    // A gradient shape, words in two columns, and a table (4.7's 9).
    const auto fills_tables = [](F& facade, const L& lib, bool table_selected) {
        auto* fe = facade.slideEdit();
        REQUIRE(fe->openFreeform(lib.freeform));
        auto* objects = fe->objects();
        REQUIRE(objects->insert(QStringLiteral("shape")).isEmpty());
        const auto shape = objects->selection().value(QStringLiteral("id")).toString();
        for (const auto& [name, value] : std::vector<std::pair<QString, QVariant>>{
                 {QStringLiteral("x"), 80}, {QStringLiteral("y"), 620}, {QStringLiteral("width"), 520},
                 {QStringLiteral("height"), 380}, {QStringLiteral("fill_kind"), QStringLiteral("gradient")},
                 {QStringLiteral("gradient_from"), QStringLiteral("#2a6fdb")},
                 {QStringLiteral("gradient_to"), QStringLiteral("#7b2cbf")},
                 {QStringLiteral("gradient_angle"), 45}}) {
            REQUIRE(objects->setSelectedProperty(name, value).isEmpty());
        }
        REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
        const auto words = objects->selection().value(QStringLiteral("id")).toString();
        REQUIRE(objects->setText(words, QStringLiteral("Welcome\nFamily time\nPrayer\nOffering\nSermon\nBenediction"),
                                 false, false, false)
                    .isEmpty());
        for (const auto& [name, value] : std::vector<std::pair<QString, QVariant>>{
                 {QStringLiteral("x"), 660}, {QStringLiteral("y"), 620}, {QStringLiteral("width"), 520},
                 {QStringLiteral("height"), 180}, {QStringLiteral("font_size"), 40},
                 {QStringLiteral("columns"), 2}, {QStringLiteral("column_gap"), 30}}) {
            REQUIRE(objects->setSelectedProperty(name, value).isEmpty());
        }
        REQUIRE(objects->insert(QStringLiteral("table")).isEmpty());
        for (const auto& [name, value] : std::vector<std::pair<QString, QVariant>>{
                 {QStringLiteral("x"), 1240}, {QStringLiteral("y"), 620}, {QStringLiteral("width"), 600},
                 {QStringLiteral("height"), 300}, {QStringLiteral("table_banding"), true},
                 {QStringLiteral("table_cells"),
                  QVariantList{QStringLiteral("Role"), QStringLiteral("Name"), QStringLiteral("Week"),
                               QStringLiteral("Reader"), QStringLiteral("Ana"), QStringLiteral("1"),
                               QStringLiteral("Prayer"), QStringLiteral("Ben"), QStringLiteral("1")}}}) {
            REQUIRE(objects->setSelectedProperty(name, value).isEmpty());
        }
        if (!table_selected) {
            objects->selectObject(shape);
        }
    };
    s.push_back({"freeform_table", [fills_tables](F& facade, OperatorUi&, const L& lib) {
                     fills_tables(facade, lib, true);
                 }});
    s.push_back({"freeform_gradient", [fills_tables](F& facade, OperatorUi&, const L& lib) {
                     fills_tables(facade, lib, false);
                 }});
    // Words turned with their box, on the slide's thumbnail as on the stage.
    s.push_back({"freeform_rotated", [](F& facade, OperatorUi&, const L& lib) {
                     auto* fe = facade.slideEdit();
                     REQUIRE(fe->openFreeform(lib.freeform));
                     const auto objects = fe->objects()->canvasObjects();
                     REQUIRE_FALSE(objects.isEmpty());
                     fe->objects()->selectObject(objects.front().toMap().value(QStringLiteral("id")).toString());
                     REQUIRE(fe->objects()->setSelectedProperty(QStringLiteral("rotation"), 330).isEmpty());
                 }});
    // Paragraphs with settings of their own, and a radial gradient off centre,
    // selected to show its Centre row.
    s.push_back({"freeform_own_paragraphs", [](F& facade, OperatorUi&, const L& lib) {
                     auto* fe = facade.slideEdit();
                     REQUIRE(fe->openFreeform(lib.freeform));
                     auto* objects = fe->objects();
                     REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
                     const auto words = objects->selection().value(QStringLiteral("id")).toString();
                     REQUIRE(objects
                                 ->setText(words,
                                           QStringLiteral("Welcome\n<p=align:center,before:40>Family time\n"
                                                          "<p=align:right,before:40>Prayer"),
                                           false, false, false)
                                 .isEmpty());
                     for (const auto& [name, value] : std::vector<std::pair<QString, QVariant>>{
                              {QStringLiteral("x"), 760}, {QStringLiteral("y"), 620}, {QStringLiteral("width"), 1000},
                              {QStringLiteral("height"), 340}, {QStringLiteral("font_size"), 60}}) {
                         REQUIRE(objects->setSelectedProperty(name, value).isEmpty());
                     }
                     REQUIRE(objects->insert(QStringLiteral("shape")).isEmpty());
                     for (const auto& [name, value] : std::vector<std::pair<QString, QVariant>>{
                              {QStringLiteral("x"), 120}, {QStringLiteral("y"), 620}, {QStringLiteral("width"), 560},
                              {QStringLiteral("height"), 340}, {QStringLiteral("fill_kind"), QStringLiteral("gradient")},
                              {QStringLiteral("gradient_kind"), QStringLiteral("radial")},
                              {QStringLiteral("gradient_from"), QStringLiteral("#ffd166")},
                              {QStringLiteral("gradient_to"), QStringLiteral("#7b2cbf")},
                              {QStringLiteral("gradient_center_x"), 0.2},
                              {QStringLiteral("gradient_center_y"), 0.25}}) {
                         REQUIRE(objects->setSelectedProperty(name, value).isEmpty());
                     }
                 }});
    s.push_back({"freeform_indicator", [](F& facade, OperatorUi&, const L& lib) {
                     auto* fe = facade.slideEdit();
                     REQUIRE(fe->openFreeform(lib.freeform));
                     REQUIRE(fe->cues()->setLineControl(true).isEmpty());
                     fe->cues()->select(0);
                     REQUIRE(fe->cues()->setVerse(QStringLiteral("Verse"), 1).isEmpty());
                     REQUIRE(fe->cues()->insert(QStringLiteral("Chorus line")).isEmpty());
                     fe->cues()->select(1);
                     REQUIRE(fe->cues()->setVerse(QStringLiteral("Chorus"), 1).isEmpty());
                     REQUIRE(fe->objects()->insert(QStringLiteral("indicator")).isEmpty());
                     REQUIRE(fe->objects()->setSelectedProperty(QStringLiteral("indicator_gap"), 0.5)
                                 .isEmpty());
                 }});
    // A song saved as Freeform: cues with chords, a linked box that shows
    // them with the next cue following. Selected, on the Cues tab.
    const auto open_chord_doc = [](F& facade) {
        using namespace seventhslide::freeform;
        const auto main_id = facade.session().outputs().front().id;
        auto doc = make_document("How Deep", {main_id});
        doc.line_control = true;
        doc.cues = {
            Cue{.id = "c1", .text = "How[E2] deep the Father's[F#m7] love[E/G#] for [A2]us,",
                .verse_type = "Verse", .verse_number = 1},
            Cue{.id = "c2", .text = "How[E] vast beyond all[C#m7] mea[B2]sure,",
                .verse_type = "Verse", .verse_number = 1},
            Cue{.id = "c3", .text = "That[E2] He should give[F#m7] His [E/G#]only[A] Son",
                .verse_type = "Verse", .verse_number = 1},
        };
        auto& deck = doc.decks.front();
        deck.slides.resize(2);
        deck.slides[0].id = "s1";
        deck.slides[1].id = "s2";
        deck.line_to_slide = {0, 0, 1};
        Object box;
        box.id = "t1";
        box.type = ObjectType::Text;
        box.x = 160;
        box.y = 140;
        box.w = 1600;
        box.h = 800;
        box.font_size = 64;
        box.cue_linked = true;
        box.show_upcoming = true;
        box.show_chords = true;
        deck.slides[0].objects.push_back(box);
        normalize(doc);
        const auto id = facade.freeformLibrary()->add_document(std::move(doc));
        REQUIRE(id >= 0);
        REQUIRE(facade.slideEdit()->openFreeform(id));
        facade.slideEdit()->objects()->selectObject(QStringLiteral("t1"));
    };
    s.push_back({"freeform_chords", [open_chord_doc](F& facade, OperatorUi& ui, const L&) {
                     open_chord_doc(facade);
                     set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "ribbonTab",
                         QStringLiteral("cues"));
                 }});
    // Editing that box: chord labels over the words, the follow cue's too,
    // and the chord at the caret (just after "How": E2) in the Chord field.
    s.push_back({"freeform_chords_editing", [open_chord_doc](F& facade, OperatorUi& ui, const L&) {
                     open_chord_doc(facade);
                     QObject* state = state_of(ui, "SlideEditorWindow", "SlideEditState");
                     set(state, "ribbonTab", QStringLiteral("cues"));
                     QVariant box;
                     for (const auto& obj : facade.slideEdit()->objects()->canvasObjects()) {
                         if (obj.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("t1")) {
                             box = obj;
                         }
                     }
                     REQUIRE(box.isValid());
                     // As a double-click on the selected box starts it.
                     QTest::qWait(50);
                     call(state, "beginTextEdit", box);
                     QTest::qWait(50);
                     set(state, "editSelStart", 3);
                     set(state, "editSelEnd", 3);
                     set(state, "editSelTick", state->property("editSelTick").toInt() + 1);
                 }});
    // A text box with a fill and a dashed line: the Fill and Line sections,
    // and the frame on the stage and in the sorter's thumbnail.
    s.push_back({"freeform_fill_line", [](F& facade, OperatorUi&, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                     auto* objects = facade.slideEdit()->objects();
                     for (const auto& obj : objects->canvasObjects()) {
                         if (obj.toMap().value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                             objects->selectObject(obj.toMap().value(QStringLiteral("id")).toString());
                         }
                     }
                     REQUIRE(objects->hasSelection());
                     REQUIRE(objects->setSelectedProperty(QStringLiteral("fill_kind"), QStringLiteral("solid")).isEmpty());
                     REQUIRE(objects->setSelectedProperty(QStringLiteral("fill"), QStringLiteral("#2a3f5f")).isEmpty());
                     REQUIRE(objects->setSelectedProperty(QStringLiteral("stroke"), QStringLiteral("#ffd166")).isEmpty());
                     REQUIRE(objects->setSelectedProperty(QStringLiteral("stroke_width"), 6).isEmpty());
                     REQUIRE(objects->setSelectedProperty(QStringLiteral("line_dash"), QStringLiteral("dash")).isEmpty());
                 }});
    // Effects: the words with an outline and a shadow over a glowing frame,
    // and the ellipse with a shadow; the Effects section open on the text.
    s.push_back({"freeform_effects", [](F& facade, OperatorUi&, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                     auto* objects = facade.slideEdit()->objects();
                     QString text_id;
                     QString shape_id;
                     for (const auto& obj : objects->canvasObjects()) {
                         const auto m = obj.toMap();
                         if (m.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                             text_id = m.value(QStringLiteral("id")).toString();
                         } else if (m.value(QStringLiteral("type")).toString() == QStringLiteral("shape")) {
                             shape_id = m.value(QStringLiteral("id")).toString();
                         }
                     }
                     REQUIRE(!text_id.isEmpty());
                     REQUIRE(!shape_id.isEmpty());
                     const auto set = [objects](const char* name, const QVariant& value) {
                         REQUIRE(objects->setSelectedProperty(QString::fromLatin1(name), value).isEmpty());
                     };
                     objects->selectObject(shape_id);
                     set("shadow_on", true);
                     set("shadow_blur", 16);
                     set("shadow_distance", 14);
                     set("soft_edge", 6);
                     objects->selectObject(text_id);
                     set("fill_kind", QStringLiteral("solid"));
                     set("fill", QStringLiteral("#1f2a44"));
                     set("glow_on", true);
                     set("glow_color", QStringLiteral("#80ffd166"));
                     set("glow_size", 18);
                     set("text_outline_width", 3);
                     set("text_outline_color", QStringLiteral("#000000"));
                     set("text_shadow_on", true);
                     set("text_shadow_distance", 6);
                 }});
    // Paragraphs: a numbered list with an indent and margins in a framed box.
    s.push_back({"freeform_paragraphs", [](F& facade, OperatorUi&, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                     auto* objects = facade.slideEdit()->objects();
                     QString text_id;
                     for (const auto& obj : objects->canvasObjects()) {
                         const auto m = obj.toMap();
                         if (m.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                             text_id = m.value(QStringLiteral("id")).toString();
                         }
                     }
                     REQUIRE(!text_id.isEmpty());
                     objects->selectObject(text_id);
                     REQUIRE(objects->setText(text_id, QStringLiteral("Welcome and announcements\nThe offering, with thanks\nPrayer requests"), false, false, false).isEmpty());
                     const auto set = [objects](const char* name, const QVariant& value) {
                         REQUIRE(objects->setSelectedProperty(QString::fromLatin1(name), value).isEmpty());
                     };
                     set("align", QStringLiteral("left"));
                     set("height", 480);
                     set("fill_kind", QStringLiteral("solid"));
                     set("fill", QStringLiteral("#2a3f5f"));
                     set("bullets", QStringLiteral("number"));
                     set("space_after", 16);
                     set("margin_left", 40);
                     set("margin_top", 24);
                 }});
    // Pictures: a cropped, tinted, see-through picture beside the ellipse
    // filled with another; the Picture section open.
    s.push_back({"freeform_pictures", [](F& facade, OperatorUi&, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                     auto* objects = facade.slideEdit()->objects();
                     const auto set = [objects](const char* name, const QVariant& value) {
                         REQUIRE(objects->setSelectedProperty(QString::fromLatin1(name), value).isEmpty());
                     };
                     for (const auto& obj : objects->canvasObjects()) {
                         const auto m = obj.toMap();
                         if (m.value(QStringLiteral("type")).toString() == QStringLiteral("shape")) {
                             objects->selectObject(m.value(QStringLiteral("id")).toString());
                             set("fill_picture_file", QStringLiteral("ocean.png"));
                             set("x", 1100);
                             set("width", 600);
                             set("height", 500);
                         }
                     }
                     REQUIRE(objects->insert(QStringLiteral("image"), {{QStringLiteral("filename"), QStringLiteral("sunrise.png")}}).isEmpty());
                     set("x", 200);
                     set("y", 500);
                     set("width", 700);
                     set("height", 400);
                     set("crop_left", 0.3);
                     set("recolor", QStringLiteral("tint"));
                     set("tint", QStringLiteral("#ffd166"));
                     set("picture_transparency", 0.3);
                     set("shadow_on", true);
                 }});
    // The sidebar's Notes tab, with the slide's speaker notes.
    s.push_back({"freeform_notes", [](F& facade, OperatorUi& ui, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                     REQUIRE(facade.slideEdit()
                                 ->slides()
                                 ->setNotes(QStringLiteral("Welcome the visitors by name, then hand over to the worship team."))
                                 .isEmpty());
                     set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "sidebarTab",
                         QStringLiteral("notes"));
                 }});
    s.push_back({"freeform_editor_large", [](F& facade, OperatorUi&, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                 },
                 1600, 1000});

    // --- the sidebar's sections whole: every section open, the Properties
    // column shot a page at a time down to its foot, each selection with the
    // switches on that show the most rows ---
    using Setup = std::function<void(F&, OperatorUi&, const L&)>;
    using Props = std::vector<std::pair<const char*, QVariant>>;
    const auto set_all = [](F& facade, const Props& props) {
        for (const auto& [name, value] : props) {
            INFO("property " << name);
            REQUIRE(facade.slideEdit()
                        ->objects()
                        ->setSelectedProperty(QString::fromLatin1(name), value)
                        .isEmpty());
        }
    };
    const auto freeform_with = [set_all](const char* type, const QVariantMap& options, Props props) -> Setup {
        return [type, options, props = std::move(props), set_all](F& facade, OperatorUi&, const L& lib) {
            REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
            REQUIRE(facade.slideEdit()->objects()->insert(QString::fromLatin1(type), options).isEmpty());
            set_all(facade, props);
        };
    };
    const auto sidebar_pages = [&s](const std::string& name, Setup setup) {
        for (int page = 0; page < 3; ++page) {
            s.push_back({name + "_" + std::to_string(page), [setup, page](F& facade, OperatorUi& ui, const L& lib) {
                             setup(facade, ui, lib);
                             set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "sectionClosed",
                                 QVariantMap{});
                             QObject* properties = nullptr;
                             for (QObject* o : component(ui, "SlideEditorWindow")->findChildren<QObject*>()) {
                                 if (o->objectName() == QStringLiteral("sideProperties")) {
                                     properties = o;
                                 }
                             }
                             REQUIRE(properties != nullptr);
                             // The opened sections lay out on the next polish.
                             QTest::qWait(100);
                             const double room = properties->property("height").toDouble();
                             const double last = std::max(
                                 0.0, properties->property("contentHeight").toDouble() - room);
                             set(properties, "contentY", std::min(last, page * (room - 40)));
                         },
                         1280, 1000});
        }
    };
    sidebar_pages("sidebar_text", freeform_with("text", {},
                                                {{"columns", 2},
                                                 {"fill_kind", QStringLiteral("solid")},
                                                 {"fill", QStringLiteral("#2a3f5f")},
                                                 {"shadow_on", true},
                                                 {"glow_on", true},
                                                 {"text_outline_width", 3},
                                                 {"text_shadow_on", true}}));
    sidebar_pages("sidebar_shape_radial",
                  freeform_with("shape", {{QStringLiteral("shapeKind"), QStringLiteral("star")}},
                                {{"fill_kind", QStringLiteral("gradient")},
                                 {"gradient_kind", QStringLiteral("radial")},
                                 {"gradient_from", QStringLiteral("#2a6fdb")},
                                 {"gradient_to", QStringLiteral("#7b2cbf")}}));
    sidebar_pages("sidebar_shape_arrow",
                  freeform_with("shape", {{QStringLiteral("shapeKind"), QStringLiteral("arrow")}},
                                {{"fill_kind", QStringLiteral("gradient")}, {"gradient_angle", 45}}));
    sidebar_pages("sidebar_shape_rounded",
                  freeform_with("shape", {{QStringLiteral("shapeKind"), QStringLiteral("rounded_rect")}},
                                {{"fill_picture_file", QStringLiteral("ocean.png")}}));
    sidebar_pages("sidebar_line_open", [](F& facade, OperatorUi&, const L& lib) {
        auto* fe = facade.slideEdit();
        REQUIRE(fe->openFreeform(lib.freeform));
        REQUIRE(fe->objects()->draw({QVariantList{200, 900}, QVariantList{700, 760}}, false, false).isEmpty());
    });
    sidebar_pages("sidebar_picture",
                  freeform_with("image", {{QStringLiteral("filename"), QStringLiteral("sunrise.png")}},
                                {{"recolor", QStringLiteral("tint")}, {"tint", QStringLiteral("#ffd166")}}));
    sidebar_pages("sidebar_table", freeform_with("table", {}, {}));
    sidebar_pages("sidebar_video",
                  freeform_with("video", {{QStringLiteral("filename"), QStringLiteral("welcome loop.mp4")}}, {}));
    // The two whose first Text options row is not line spacing.
    sidebar_pages("sidebar_indicator", freeform_with("indicator", {}, {}));
    sidebar_pages("sidebar_clock", freeform_with("clock", {}, {}));
    sidebar_pages("sidebar_linked", [open_chord_doc](F& facade, OperatorUi&, const L&) { open_chord_doc(facade); });
    // A theme's lyrics box, lines not following: fluid, balanced, chords on.
    sidebar_pages("sidebar_designer_lyrics", [open_theme, set_all](F& facade, OperatorUi& ui, const L&) {
        open_theme(facade, ui, "song");
        auto* theme = facade.slideEdit()->theme();
        theme->setSetting(QStringLiteral("fluid_slides"), true);
        theme->setSetting(QStringLiteral("balance_wrapped_lines"), true);
        facade.slideEdit()->objects()->selectObject(QStringLiteral("te_lyrics"));
        set_all(facade, {{"show_chords", true}});
    });
    sidebar_pages("sidebar_designer_follow", [open_theme](F& facade, OperatorUi& ui, const L&) {
        open_theme(facade, ui, "song");
        facade.slideEdit()->theme()->setSetting(QStringLiteral("follow_lines"), 2);
        facade.slideEdit()->objects()->selectObject(QStringLiteral("te_lyrics"));
    });
    sidebar_pages("sidebar_designer_passage", [open_theme, set_all](F& facade, OperatorUi& ui, const L&) {
        open_theme(facade, ui, "bible");
        facade.slideEdit()->objects()->selectObject(QStringLiteral("te_bible_text"));
        set_all(facade, {{"verse_numbers", true}});
    });
    // Shown on the first N slides, only with the song's copyright.
    sidebar_pages("sidebar_designer_rule", [open_theme, set_all](F& facade, OperatorUi& ui, const L&) {
        open_theme(facade, ui, "song");
        facade.slideEdit()->objects()->selectObject(QStringLiteral("te_copyright"));
        set_all(facade, {{"rule", QStringLiteral("first")}, {"count", 2}});
    });
    s.push_back({"freeform_fields", [](F& facade, OperatorUi& ui, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                     REQUIRE(facade.slideEdit()->fields()->add(QStringLiteral("speaker")).isEmpty());
                     REQUIRE(facade.slideEdit()->fields()->set(QStringLiteral("speaker"), QStringLiteral("Pastor Dan")).isEmpty());
                     call(state_of(ui, "SlideEditorWindow", "FieldsWindow"), "show");
                 }});
    // A box being typed into behind the window: the + per row and From
    // selection are live, and one token has gone in at the caret.
    s.push_back({"freeform_fields_typing", [](F& facade, OperatorUi& ui, const L& lib) {
                     REQUIRE(facade.slideEdit()->openFreeform(lib.freeform));
                     auto* fields = facade.slideEdit()->fields();
                     REQUIRE(fields->add(QStringLiteral("speaker")).isEmpty());
                     REQUIRE(fields->set(QStringLiteral("speaker"), QStringLiteral("Pastor Dan")).isEmpty());
                     REQUIRE(fields->add(QStringLiteral("sermon-title")).isEmpty());
                     REQUIRE(fields->set(QStringLiteral("sermon-title"), QStringLiteral("Steadfast")).isEmpty());
                     QVariant box;
                     for (const auto& obj : facade.slideEdit()->objects()->canvasObjects()) {
                         if (obj.toMap().value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
                             box = obj;
                         }
                     }
                     REQUIRE(box.isValid());
                     QObject* state = state_of(ui, "SlideEditorWindow", "SlideEditState");
                     QTest::qWait(50);
                     call(state, "beginTextEdit", box);
                     QTest::qWait(50);
                     QObject* window = state_of(ui, "SlideEditorWindow", "FieldsWindow");
                     call(window, "show");
                     call(window, "insertToken", QStringLiteral("speaker"), false);
                 }});
    // --- the slide editor with a deck open ---
    // Its deck: four slides, the second open with its title box selected.
    const auto mock_deck = [](F& facade) {
        using namespace seventhslide::freeform;
        const auto main_id = facade.session().outputs().front().id;
        auto doc = make_document("Welcome", {main_id});
        auto& deck = doc.decks.front();
        deck.slides.resize(4);
        const auto text_box = [](const char* id, const char* text, int y, int h, int size) {
            Object box;
            box.id = id;
            box.type = ObjectType::Text;
            box.x = 300;
            box.y = y;
            box.w = 1320;
            box.h = h;
            box.font_size = size;
            box.text = text;
            return box;
        };
        deck.slides[0].id = "s1";
        deck.slides[0].objects.push_back(text_box("o1", "Welcome to worship", 440, 200, 64));
        deck.slides[1].id = "s2";
        deck.slides[1].objects.push_back(text_box("title", "Announcements", 200, 240, 64));
        deck.slides[1].objects.push_back(
            text_box("body", "Potluck after the service\nPrayer meeting Wednesday 7 pm", 620, 240, 44));
        deck.slides[2].id = "s3";
        deck.slides[2].objects.push_back(text_box("o2", "Prayer requests", 440, 200, 64));
        deck.slides[3].id = "s4";
        normalize(doc);
        const auto id = facade.freeformLibrary()->add_document(std::move(doc));
        REQUIRE(id >= 0);
        REQUIRE(facade.slideEdit()->openFreeform(id));
        auto* fe = facade.slideEdit();
        fe->slides()->select(1);
        auto* objects = fe->objects();
        objects->selectObject(QStringLiteral("body"));
        REQUIRE(objects->setSelectedProperty(QStringLiteral("fill_kind"), QStringLiteral("solid")).isEmpty());
        REQUIRE(objects->setSelectedProperty(QStringLiteral("fill"), QStringLiteral("#1a1a1a")).isEmpty());
        REQUIRE(objects->setSelectedProperty(QStringLiteral("stroke"), QStringLiteral("#555555")).isEmpty());
        REQUIRE(objects->setSelectedProperty(QStringLiteral("stroke_width"), 2).isEmpty());
        objects->selectObject(QStringLiteral("title"));
        REQUIRE(objects->setSelectedProperty(QStringLiteral("fill_kind"), QStringLiteral("solid")).isEmpty());
        REQUIRE(objects->setSelectedProperty(QStringLiteral("fill"), QStringLiteral("#0a1420")).isEmpty());
    };
    for (const char* tab : {"home", "insert", "design", "transitions", "animations", "cues"}) {
        s.push_back({std::string("ed_tab_") + tab, [tab, mock_deck](F& facade, OperatorUi& ui, const L&) {
                         mock_deck(facade);
                         set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "ribbonTab",
                             QString::fromLatin1(tab));
                     }, 1600, 1000});
    }
    // With a build on it: the Animations tab's fields live.
    s.push_back({"ed_tab_animations_build", [mock_deck](F& facade, OperatorUi& ui, const L&) {
                     mock_deck(facade);
                     REQUIRE(facade.slideEdit()->animations()->add(QStringLiteral("fly_in")).isEmpty());
                     facade.slideEdit()->objects()->selectObject(QStringLiteral("body"));
                     REQUIRE(facade.slideEdit()->animations()->add(QStringLiteral("fade")).isEmpty());
                     QObject* state = state_of(ui, "SlideEditorWindow", "SlideEditState");
                     set(state, "ribbonTab", QStringLiteral("animations"));
                     set(state, "sidebarTab", QStringLiteral("build"));
                 }, 1600, 1000});
    s.push_back({"ed_sidebar_notes", [mock_deck](F& facade, OperatorUi& ui, const L&) {
                     mock_deck(facade);
                     REQUIRE(facade.slideEdit()
                                 ->slides()
                                 ->setNotes(QStringLiteral("Potluck is in the fellowship hall, not the gym this week."))
                                 .isEmpty());
                     set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "sidebarTab",
                         QStringLiteral("notes"));
                 }, 1600, 1000});
    // The menus: the output picker, a font list, a split button's, a row's.
    for (const auto& menu : {std::pair{"output", "slideEditorOutput"},
                             std::pair{"fontFamily", "ribbonFontFamily"},
                             std::pair{"align", "ribbonAlign"}, std::pair{"order", "ribbonOrder"}}) {
        const char* control = menu.second;
        s.push_back({std::string("ed_menu_") + menu.first, [control, mock_deck](F& facade, OperatorUi& ui, const L&) {
                         mock_deck(facade);
                         // A menu places itself by what it hangs from, laid out by now.
                         QTest::qWait(100);
                         QObject* opener = ui.window()->findChild<QObject*>(QString::fromLatin1(control));
                         REQUIRE(opener != nullptr);
                         set(opener, "open", true);
                     }, 1600, 1000});
    }
    s.push_back({"ed_fields", [mock_deck](F& facade, OperatorUi& ui, const L&) {
                     mock_deck(facade);
                     auto* fields = facade.slideEdit()->fields();
                     for (const auto& [name, value] : {std::pair{"speaker", "Pastor Dan Williams"},
                                                       std::pair{"sermon-title", "Steadfast"},
                                                       std::pair{"verse-ref", "Hebrews 10:23"}}) {
                         REQUIRE(fields->add(QString::fromLatin1(name)).isEmpty());
                         REQUIRE(fields->set(QString::fromLatin1(name), QString::fromLatin1(value)).isEmpty());
                     }
                     call(state_of(ui, "SlideEditorWindow", "FieldsWindow"), "show");
                 }, 1600, 1000});
    // Line control: cues under their slides, one being dragged is not shot.
    s.push_back({"ed_line_control", [](F& facade, OperatorUi& ui, const L&) {
                     using namespace seventhslide::freeform;
                     const auto main_id = facade.session().outputs().front().id;
                     auto doc = make_document("Amazing Grace", {main_id});
                     doc.line_control = true;
                     doc.cues = {
                         Cue{.id = "c1", .text = "Amazing grace how sweet the sound", .verse_type = "Verse", .verse_number = 1},
                         Cue{.id = "c2", .text = "That saved a wretch like me", .verse_type = "Verse", .verse_number = 1},
                         Cue{.id = "c3", .text = "I once was lost but now am found", .verse_type = "Verse", .verse_number = 1},
                         Cue{.id = "c4", .text = "Was blind but now I see", .verse_type = "Verse", .verse_number = 1},
                     };
                     auto& deck = doc.decks.front();
                     deck.slides.resize(2);
                     deck.slides[0].id = "s1";
                     deck.slides[1].id = "s2";
                     deck.line_to_slide = {0, 0, 1, 1};
                     Object box;
                     box.id = "t1";
                     box.type = ObjectType::Text;
                     box.x = 160;
                     box.y = 300;
                     box.w = 1600;
                     box.h = 480;
                     box.font_size = 64;
                     box.cue_linked = true;
                     deck.slides[0].objects.push_back(box);
                     deck.slides[1].objects.push_back(box);
                     normalize(doc);
                     const auto id = facade.freeformLibrary()->add_document(std::move(doc));
                     REQUIRE(id >= 0);
                     REQUIRE(facade.slideEdit()->openFreeform(id));
                     facade.slideEdit()->slides()->select(1);
                     facade.slideEdit()->cues()->select(2);
                     set(state_of(ui, "SlideEditorWindow", "SlideEditState"), "ribbonTab",
                         QStringLiteral("cues"));
                 }, 1600, 1000});
    // A theme's: the tokens a service fills in, name only.
    s.push_back({"designer_tokens", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     call(state_of(ui, "SlideEditorWindow", "FieldsWindow"), "show");
                 }});

    // --- small dialogs ---
    s.push_back({"error_dialog", [](F& facade, OperatorUi&, const L&) {
                     emit facade.errorOccurred(QStringLiteral("The file could not be read."));
                 }});
    // The picker lives in the dialog that uses it: the song editor's here,
    // open on its Themes page.
    s.push_back({"theme_picker", [](F& facade, OperatorUi& ui, const L&) {
                     QObject* song = component(ui, "SongEditDialog");
                     const auto id = facade.serviceItems()->data(
                         facade.serviceItems()->index(0, 0),
                         seventhslide::app::ServiceItemModel::ItemIdRole);
                     call(song, "openItem", id);
                     set(song, "tab", QStringLiteral("themes"));
                     // Opened as a theme slot opens it, Inherit tile and all.
                     QObject* map = nullptr;
                     for (QObject* o : song->findChildren<QObject*>()) {
                         if (QByteArray(o->metaObject()->className()).startsWith("ThemeMapEditor_QMLTYPE"))
                             map = o;
                     }
                     REQUIRE(map != nullptr);
                     const auto& out = facade.session().outputs().front();
                     // The chip's song theme slot.
                     const QVariantMap slot{{QStringLiteral("category"), QStringLiteral("song")},
                                            {QStringLiteral("field"), QStringLiteral("theme")},
                                            {QStringLiteral("kind"), QStringLiteral("song")}};
                     REQUIRE(QMetaObject::invokeMethod(
                         map, "openPicker", Q_ARG(QVariant, QString::fromStdString(out.id)),
                         Q_ARG(QVariant, QString::fromStdString(out.name)),
                         Q_ARG(QVariant, QVariant(slot))));
                 }});
    // Insert › Image in the slide editor, over a document.
    s.push_back({"image_picker", [mock_deck](F& facade, OperatorUi& ui, const L&) {
                     mock_deck(facade);
                     call(state_of(ui, "SlideEditorWindow", "SlideEditState"), "insert",
                          QStringLiteral("image"), QString());
                 }, 1600, 1000});
    s.push_back({"color_picker", [open_theme](F& facade, OperatorUi& ui, const L&) {
                     open_theme(facade, ui, "song");
                     REQUIRE(facade.slideEdit()->objects()->insert(QStringLiteral("shape")).isEmpty());
                     Shooter::settle(*ui.window());
                     // A swatch's picker stays hidden; only its dialog shows.
                     QObject* swatch = shown_component(component(ui, "SlideEditorWindow"), "RibbonSwatch");
                     for (QObject* o : swatch->findChildren<QObject*>()) {
                         if (QString::fromLatin1(o->metaObject()->className())
                                 .startsWith(QStringLiteral("ColorPicker_QMLTYPE_"))) {
                             call(o, "openDialog");
                             break;
                         }
                     }
                 }});
    s.push_back({"song_import_dialog", [](F&, OperatorUi& ui, const L&) {
                     const auto row = [](int at, const char* title, const char* format,
                                         const char* note, bool importable = true) {
                         return QVariantMap{{"row", at},
                                            {"title", QString::fromLatin1(title)},
                                            {"format", QString::fromLatin1(format)},
                                            {"note", QString::fromLatin1(note)},
                                            {"importable", importable}};
                     };
                     QObject* dialog = component(ui, "SongImportDialog");
                     set(dialog, "rows", QVariantList{
                         row(-1, "Scan 12.pdf", "", "Format not recognized", false),
                         row(-1, "Sunday set.pro", "ProPresenter 7", "Format not supported", false),
                         row(0, "Be Thou My Vision", "OpenSong", ""),
                         row(1, "Blessed Assurance", "SongSelect text", ""),
                         row(2, "Come Thou Fount", "SongSelect text", ""),
                         row(3, "Great Is Thy Faithfulness", "ChordPro",
                             "Dropped tabs and chord grids"),
                         row(4, "Holy, Holy, Holy", "SongSelect file", ""),
                         row(5, "How Great Thou Art", "OpenLyrics",
                             "Dropped languages: DE"),
                         row(6, "It Is Well", "SongSelect text", ""),
                         row(7, "Rock of Ages", "ChordPro", ""),
                         row(8, "Swing Low Sweet Chariot", "ChordPro", ""),
                         row(9, "What a Friend", "Plain text", ""),
                         row(10, "When I Survey", "SongSelect text", ""),
                     });
                     call(dialog, "toggle", 2);
                     set(dialog, "open", true);
                 }});
    s.push_back({"prompt_dialog", [](F&, OperatorUi& ui, const L&) {
                     // The Freeform browser's folder-name prompt.
                     call(state_of(ui, "MediaBrowser", "AppPromptDialog"), "ask",
                          QStringLiteral("Sunday Service"));
                 }});
    return s;
}

}  // namespace

TEST_CASE("UI shots", "[.shots]")
{
    const char* out = std::getenv("SEVENTHSLIDE_SHOTS_DIR");
    REQUIRE(out != nullptr);
    // Before any window exists: the software renderer draws the same pixels
    // every run.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    seventhslide::test::ensure_qt_application();
    // A caret that blinks would never let a grab settle.
    QGuiApplication::styleHints()->setCursorFlashTime(0);

    const char* only = std::getenv("SEVENTHSLIDE_SHOTS_ONLY");
    // Every scene at one size instead ("1280x560"): short, a window's pages
    // scroll, to see their scroll bars.
    int force_w = 0;
    int force_h = 0;
    if (const char* size = std::getenv("SEVENTHSLIDE_SHOTS_SIZE")) {
        const QStringList wh = QString::fromLatin1(size).split(QLatin1Char('x'));
        REQUIRE(wh.size() == 2);
        force_w = wh[0].toInt();
        force_h = wh[1].toInt();
    }
    Shooter shooter{fs::path(out)};
    for (const Scene& scene : scenes()) {
        if (only != nullptr && scene.name.find(only) == std::string::npos) {
            continue;
        }
        INFO("scene " << scene.name);
        // As main() has one: the output cards' previews draw its presenters.
        seventhslide::render::OutputWindowHost host;
        host.set_live(false);
        SessionFacade facade;
        facade.set_output_host(&host);
        const Library lib = build_library(facade);
        {
            OperatorUi ui(facade);
            REQUIRE(ui.window() != nullptr);
            ui.window()->resize(force_w > 0 ? force_w : scene.width,
                                force_h > 0 ? force_h : scene.height);
            scene.setup(facade, ui, lib);
            shooter.shoot(*ui.window(), scene.name);
            if (!ui.warnings().empty()) {
                std::cout << "warnings in " << scene.name << ":\n" << ui.warnings();
            }
        }
        facade.slideEdit()->cancel();
    }
    std::cout << "shots: " << shooter.count() << "\n";
    for (const auto& name : shooter.unsettled()) {
        std::cout << "never settled: " << name << "\n";
    }
}
