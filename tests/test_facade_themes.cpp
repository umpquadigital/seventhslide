// Themes, the theme designer and its text tools through the facade (ThemeController, SlideEditSession).

#include "canvas_test_helpers.hpp"
#include "font_controller.hpp"
#include "session_facade_fixture.hpp"

#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/model/rich_text.hpp>

#include <QElapsedTimer>
#include <QSignalSpy>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace CanvasAlignQml = seventhslide::app::CanvasAlignQml;
namespace CanvasDistributeQml = seventhslide::app::CanvasDistributeQml;
namespace CanvasFlipQml = seventhslide::app::CanvasFlipQml;
namespace CanvasOrderQml = seventhslide::app::CanvasOrderQml;

using namespace seventhslide::test::facade;

namespace {

/// A text theme on Main built from ``style``, open in the slide editor on
/// its song context.
QString open_text_theme(SessionFacade& facade, const nlohmann::json& style = {})
{
    const auto created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("Test"),
        style.is_null() ? QVariantMap{} : seventhslide::app::to_qvariant(style).toMap());
    const QString id = created.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(facade.slideEdit()->openTheme(QStringLiteral("Main"), QStringLiteral("song"), id));
    facade.slideEdit()->setContext(QStringLiteral("song"));
    return id;
}

/// The sample body's words on the designer stage's frame, a line each.
[[nodiscard]] QString draft_body_words(const seventhslide::app::ThemeController& themes)
{
    QString joined;
    const auto& frame = themes.draft_frame();
    if (frame.text) {
        for (const auto& line : frame.text->lines) {
            joined += QString::fromStdString(seventhslide::model::plain_text(line.runs));
            joined += QLatin1Char('\n');
        }
    }
    return joined;
}

/// How many pixels of ``img`` (a ``canvas``-sized design, scaled) inside the
/// canvas rect ``x``, ``y``, ``w``, ``h`` match ``wanted``.
template <class Wanted>
[[nodiscard]] int pixels_in(const QImage& img,
                            QSize canvas,
                            int x,
                            int y,
                            int w,
                            int h,
                            Wanted&& wanted)
{
    const double sx = static_cast<double>(img.width()) / canvas.width();
    const double sy = static_cast<double>(img.height()) / canvas.height();
    int count = 0;
    for (int py = static_cast<int>(y * sy); py < static_cast<int>((y + h) * sy) && py < img.height(); ++py) {
        for (int px = static_cast<int>(x * sx); px < static_cast<int>((x + w) * sx) && px < img.width();
             ++px) {
            if (wanted(img.pixel(px, py))) {
                ++count;
            }
        }
    }
    return count;
}

/// A red rectangle element at canvas ``x``, ``y``, ``w``, ``h`` and ``z``.
[[nodiscard]] nlohmann::json red_box(const char* id, int x, int y, int w, int h, int z)
{
    return {{"id", id},
            {"type", "shape"},
            {"shape_kind", "rectangle"},
            {"x", x},
            {"y", y},
            {"w", w},
            {"h", h},
            {"z", z},
            {"fill", "#ff0000"},
            {"fill_kind", "solid"},
            {"stroke", ""},
            {"stroke_width", 0},
            {"opacity", 1.0},
            {"binding", "static"}};
}

[[nodiscard]] bool is_red(QRgb px)
{
    return qRed(px) > 200 && qGreen(px) < 60 && qBlue(px) < 60 && qAlpha(px) > 200;
}

[[nodiscard]] bool is_light(QRgb px)
{
    return qGreen(px) > 150 && qBlue(px) > 150 && qAlpha(px) > 150;
}

}  // namespace

// The editor shows what C++ hands it; QML adds no defaults of its own, so
// what a control shows is what playback paints.
TEST_CASE("The editor shows each value as it paints", "[app][editor][defaults]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    SECTION("a stored indicator that leaves its type empty")
    {
        // Paint puts an indicator's empty fields top-left at 30 px; the ribbon
        // used to say centred, 48 and a 1.15 line height.
        const nlohmann::json style = {
            {"elements",
             {{"canvas", {{"width", 1920}, {"height", 1080}}},
              {"song",
               nlohmann::json::array({{{"id", "te1"},
                                       {"type", "indicator"},
                                       {"font_family", ""},
                                       {"font_size", 0},
                                       {"font_color", ""},
                                       {"text_align", ""},
                                       {"vertical_align", ""},
                                       {"line_height", 0}}})}}}};
        open_text_theme(facade, style);
        const auto objects = facade.slideEdit()->objects()->canvasObjects();
        REQUIRE(objects.size() == 1);
        const auto el = objects.front().toMap();
        CHECK(el.value(QStringLiteral("align")).toString() == QStringLiteral("left"));
        CHECK(el.value(QStringLiteral("valign")).toString() == QStringLiteral("top"));
        CHECK(el.value(QStringLiteral("font_size")).toInt() == 30);
        CHECK(el.value(QStringLiteral("font_family")).toString() == QStringLiteral("Liberation Sans"));
        CHECK(el.value(QStringLiteral("font_color")).toString() == QStringLiteral("#ffffff"));
        CHECK(el.value(QStringLiteral("line_height")).toDouble() == Catch::Approx(0.8));
    }
    SECTION("an element's transition: unset is a cut at the default length")
    {
        const nlohmann::json style = {
            {"elements",
             {{"canvas", {{"width", 1920}, {"height", 1080}}},
              {"song",
               nlohmann::json::array(
                   {{{"id", "plain"}, {"type", "text"}},
                    {{"id", "fades"},
                     {"type", "text"},
                     {"transition", {{"type", "fade"}, {"duration_ms", 300}}}}})}}}};
        open_text_theme(facade, style);
        const auto objects = facade.slideEdit()->objects()->canvasObjects();
        REQUIRE(objects.size() == 2);
        for (const auto& o : objects) {
            const auto el = o.toMap();
            const bool fades = el.value(QStringLiteral("id")).toString() == QStringLiteral("fades");
            CHECK(el.value(QStringLiteral("transition_type")).toString()
                  == (fades ? QStringLiteral("fade") : QStringLiteral("cut")));
            CHECK(el.value(QStringLiteral("transition_ms")).toInt() == (fades ? 300 : 500));
        }
    }
    SECTION("nothing selected shows a new object's values")
    {
        open_text_theme(facade);
        facade.slideEdit()->objects()->clearSelection();
        const auto theme_new = facade.slideEdit()->objects()->selection();
        CHECK(theme_new.value(QStringLiteral("font_size")).toInt() == 48);
        CHECK(theme_new.value(QStringLiteral("align")).toString() == QStringLiteral("center"));
        CHECK(theme_new.value(QStringLiteral("width")).toDouble() == 100);
        facade.slideEdit()->cancel();

        auto* edit = facade.slideEdit();
        const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Idle"));
        REQUIRE(edit->openFreeform(id));
        edit->objects()->clearSelection();
        const auto sel = edit->objects()->selection();
        CHECK(sel.value(QStringLiteral("id")).toString().isEmpty());
        CHECK(sel.value(QStringLiteral("font_size")).toInt() == 48);
        CHECK(sel.value(QStringLiteral("font_family")).toString() == QStringLiteral("Liberation Sans"));
        CHECK(sel.value(QStringLiteral("fill")).toString() == QStringLiteral("#ffffff"));
    }
    SECTION("a slide turned to a colour keeps its colour, or starts on black")
    {
        auto* edit = facade.slideEdit();
        const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Colour"));
        REQUIRE(edit->openFreeform(id));
        auto* slides = edit->slides();
        const auto colour = [slides] { return slides->background().value(QStringLiteral("color")).toString(); };
        CHECK(colour().isEmpty());
        REQUIRE(slides->setBackground(QStringLiteral("color"), QString()).isEmpty());
        CHECK(colour() == QStringLiteral("#000000"));
        REQUIRE(slides->setBackground(QStringLiteral("color"), QStringLiteral("#123456")).isEmpty());
        REQUIRE(slides->setBackground(QStringLiteral("color"), QString()).isEmpty());
        CHECK(colour() == QStringLiteral("#123456"));
    }
    SECTION("a theme's settings, each unset one at its default")
    {
        auto* themes = facade.themes();
        const auto unset = themes->style_settings(nlohmann::json::object());
        CHECK(unset.value(QStringLiteral("show_title_slides")).toBool());
        CHECK(unset.value(QStringLiteral("background_type")).toString()
              == QStringLiteral("transparent"));
        // Each content type's transition, a cut where nothing says otherwise.
        const auto transition = [](const QVariantMap& settings, const char* ctx) {
            return settings.value(QStringLiteral("transitions")).toMap().value(QString::fromLatin1(ctx)).toMap();
        };
        CHECK(transition(unset, "video").value(QStringLiteral("type")).toString() == QStringLiteral("cut"));
        CHECK(transition(unset, "song").value(QStringLiteral("ms")).toInt() == 500);
        CHECK(unset.value(QStringLiteral("balance_wrapped_strength")).toInt() == 100);
        const nlohmann::json fade = {{"type", "fade"}, {"duration_ms", 900}};
        const auto set = themes->style_settings({{"background_type", "color"},
                                                 {"show_title_slides", false},
                                                 {"transitions", {{"bible", fade}, {"live", fade}}}});
        CHECK(set.value(QStringLiteral("background_color")).toString() == QStringLiteral("#000000"));
        CHECK_FALSE(set.value(QStringLiteral("show_title_slides")).toBool());
        // Each content type's as it is stored; one it leaves out is a cut.
        CHECK(transition(set, "bible").value(QStringLiteral("type")).toString() == QStringLiteral("fade"));
        CHECK(transition(set, "live").value(QStringLiteral("ms")).toInt() == 900);
        CHECK(transition(set, "song").value(QStringLiteral("type")).toString() == QStringLiteral("cut"));
    }
}

// A click changes the selection and nothing else, so it must not make QML
// rebuild the object list, the stage or the slide models.
TEST_CASE("Selecting in either editor signals only the selection", "[app][editor][signals]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    auto* edit = facade.slideEdit();
    auto* objects = edit->objects();
    const auto check = [&] {
        REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
        const auto id = objects->selectedIds().front();
        QSignalSpy document(edit, &seventhslide::app::SlideEditSession::documentChanged);
        QSignalSpy selection(edit, &seventhslide::app::SlideEditSession::selectionChanged);
        QSignalSpy clipboard(edit, &seventhslide::app::SlideEditSession::clipboardChanged);
        objects->clearSelection();
        objects->selectObject(id);
        objects->selectAll();
        CHECK(selection.size() == 3);
        CHECK(document.isEmpty());
        REQUIRE(objects->copySelected().isEmpty());
        CHECK(clipboard.size() == 1);
        CHECK(document.isEmpty());
        // An edit says both.
        REQUIRE(objects->nudge(4, 0).isEmpty());
        CHECK(document.size() == 1);
        CHECK(selection.size() == 4);
    };
    SECTION("theme designer")
    {
        open_text_theme(facade);
        check();
    }
    SECTION("Freeform editor")
    {
        const auto id = facade.freeformLibrary()->createFreeform(QStringLiteral("Signals"));
        REQUIRE(id >= 0);
        REQUIRE(edit->openFreeform(id));
        check();
    }
}

TEST_CASE("SessionFacade theme designer undoes and redoes element commands", "[app][theme][editor][undo]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});

    // A theme opens with its stock elements, so each section below counts
    // from that baseline.
    open_text_theme(facade);
    auto* edit = facade.slideEdit();
    auto* objects = edit->objects();
    REQUIRE(edit->active());
    const auto baseline = objects->canvasObjects().size();
    REQUIRE_FALSE(edit->dirty());

    auto insert_text = [objects]() {
        REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
        const QString id = objects->selection().value(QStringLiteral("id")).toString();
        REQUIRE_FALSE(id.isEmpty());
        return id;
    };
    auto x_of = [objects](const QString& want) {
        for (const auto& v : objects->canvasObjects()) {
            const auto m = v.toMap();
            if (m.value(QStringLiteral("id")).toString() == want) {
                return m.value(QStringLiteral("x")).toDouble();
            }
        }
        return -1.0;
    };

    SECTION("undo steps a command back and redo puts it forward again")
    {
        insert_text();
        REQUIRE(objects->canvasObjects().size() == baseline + 1);
        REQUIRE(edit->dirty());

        REQUIRE(edit->undo().isEmpty());
        REQUIRE(objects->canvasObjects().size() == baseline);
        REQUIRE_FALSE(edit->dirty());

        REQUIRE(edit->redo().isEmpty());
        REQUIRE(objects->canvasObjects().size() == baseline + 1);
        REQUIRE(edit->dirty());
    }

    SECTION("undo restores geometry a gesture moved, leaving earlier commands alone")
    {
        const QString id = insert_text();
        const double x0 = x_of(id);

        objects->selectObject(id, false);
        objects->beginGesture();
        objects->setGeometries(seventhslide::test::geometry(id, x0 + 120, 40, 400, 200));
        objects->endGesture();
        REQUIRE(x_of(id) != Catch::Approx(x0));

        // One gesture is one undo step: the move goes, the insert stays.
        REQUIRE(edit->undo().isEmpty());
        REQUIRE(x_of(id) == Catch::Approx(x0));
        REQUIRE(objects->canvasObjects().size() == baseline + 1);
        REQUIRE(edit->dirty());
    }

    SECTION("undo with nothing on the stack reports instead of mutating")
    {
        REQUIRE_FALSE(edit->undo().isEmpty());
        REQUIRE(objects->canvasObjects().size() == baseline);
        REQUIRE_FALSE(edit->dirty());
    }

    SECTION("redo is dropped once a new command lands on top of an undo")
    {
        insert_text();
        REQUIRE(edit->undo().isEmpty());
        REQUIRE(objects->insert(QStringLiteral("shape")).isEmpty());
        REQUIRE_FALSE(edit->redo().isEmpty());
        REQUIRE(objects->canvasObjects().size() == baseline + 1);
    }

    edit->cancel();
}

TEST_CASE("A theme edit or a font reload builds the Freeform tiles once and counts once", "[app][theme]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto tid = facade.themes()
                         ->list(QStringLiteral("Main"), QStringLiteral("song"))
                         .value(0)
                         .toMap()
                         .value(QStringLiteral("id"))
                         .toString();
    REQUIRE_FALSE(tid.isEmpty());
    const auto once = [&](const auto& event) {
        QSignalSpy changed(facade.themes(), &seventhslide::app::ThemeController::libraryChanged);
        const auto builds = facade.freeformLibrary()->browser_builds();
        const int revision = facade.themes()->libraryRevision();
        event();
        CHECK(changed.count() == 1);
        CHECK(facade.freeformLibrary()->browser_builds() == builds + 1);
        CHECK(facade.themes()->libraryRevision() == revision + 1);
    };
    once([&] {
        REQUIRE(facade.themes()
                    ->setDefault(QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("theme"), tid)
                    .isEmpty());
    });
    once([&] { emit facade.fonts()->fontsReloaded(); });
}

TEST_CASE("SessionFacade text/bg theme CRUD + defaults", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto texts = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song"));
    const auto bgs = facade.themes()->list(QStringLiteral("Main"), QStringLiteral("bg"));
    REQUIRE(texts.size() >= 1);
    REQUIRE(bgs.size() >= 1);

    const int lib_rev0 = facade.themes()->libraryRevision();

    const auto created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("Stage"));
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto tid = created.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(tid.isEmpty());
    REQUIRE_FALSE(created.value(QStringLiteral("createdAt")).toString().isEmpty());
    REQUIRE(facade.themes()->libraryRevision() > lib_rev0);

    QVariantMap style = created.value(QStringLiteral("style")).toMap();
    style.insert(QStringLiteral("follow_lines"), 3);
    style.insert(QStringLiteral("font_size"), 55);                              // filtered out
    style.insert(QStringLiteral("background_color"), QStringLiteral("#ff0000")); // filtered out
    const int lib_rev1 = facade.themes()->libraryRevision();
    const QString err = facade.themes()->update_theme(QStringLiteral("Main"),
                                           QStringLiteral("song"),
                                           tid,
                                           QStringLiteral("Stage Bold"),
                                           seventhslide::app::to_json(QVariant(style)),
                                           QStringLiteral("foldback"));
    REQUIRE(err.isEmpty());
    REQUIRE(facade.themes()->libraryRevision() > lib_rev1);

    const auto loaded = facade.themes()->theme_map(QStringLiteral("Main"), QStringLiteral("song"), tid);
    REQUIRE(loaded.value(QStringLiteral("name")).toString() == QStringLiteral("Stage Bold"));
    REQUIRE(loaded.value(QStringLiteral("style"))
                .toMap()
                .value(QStringLiteral("follow_lines"))
                .toInt()
            == 3);
    // Geometry and typography live on elements, so a loose font size is dropped
    // the same way a background colour is.
    REQUIRE_FALSE(
        loaded.value(QStringLiteral("style")).toMap().contains(QStringLiteral("font_size")));
    REQUIRE_FALSE(loaded.value(QStringLiteral("style"))
                      .toMap()
                      .contains(QStringLiteral("background_color")));

    REQUIRE(facade.themes()->setDefault(
                QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("theme"), tid)
                .isEmpty());
    const auto defaults = facade.themes()->categoryDefaults(QStringLiteral("Main"));
    REQUIRE(defaults.value(QStringLiteral("song")).toMap().value(QStringLiteral("theme")).toString()
            == tid);

    const auto dup = facade.themes()->create(QStringLiteral("Main"),
                                        QStringLiteral("song"),
                                        QString(),
                                        {},
                                        QString(),
                                        tid);
    REQUIRE_FALSE(dup.contains(QStringLiteral("error")));
    REQUIRE_FALSE(dup.value(QStringLiteral("createdAt")).toString().isEmpty());
    REQUIRE(facade.themes()->list(QStringLiteral("Main"), QStringLiteral("song")).size() >= 3);

    REQUIRE(facade.themes()->remove(QStringLiteral("Main"), QStringLiteral("song"), tid).isEmpty());
    // Defaults rewired away from deleted id.
    const auto after = facade.themes()->categoryDefaults(QStringLiteral("Main"));
    REQUIRE(after.value(QStringLiteral("song")).toMap().value(QStringLiteral("theme")).toString()
            != tid);
}

// The QML asks which choices a theme map and a gallery card offer: they are
// the same rules the stored picks are read by.
TEST_CASE("ThemeController offers each scope's theme choices", "[app][theme_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* themes = facade.themes();

    const auto names = [](const QVariantList& groups) {
        QStringList out;
        for (const auto& group : groups) {
            for (const auto& slot : group.toMap().value(QStringLiteral("slots")).toList()) {
                const auto map = slot.toMap();
                out << map.value(QStringLiteral("category")).toString() + QLatin1Char('.')
                           + map.value(QStringLiteral("field")).toString() + QLatin1Char('@')
                           + map.value(QStringLiteral("kind")).toString();
            }
        }
        return out;
    };
    const auto service = themes->pickSlots(QStringLiteral("service"));
    CHECK(service.size() == 3);
    CHECK(names(service)
          == QStringList{QStringLiteral("song.theme@song"), QStringLiteral("song.bg@bg"),
                         QStringLiteral("bible.theme@bible"), QStringLiteral("bible.bg@bg"),
                         QStringLiteral("media.theme@media")});
    CHECK(names(themes->pickSlots(QStringLiteral("song")))
          == QStringList{QStringLiteral("song.theme@song"), QStringLiteral("song.bg@bg")});
    CHECK(themes->pickSlots(QStringLiteral("freeform")).isEmpty());
    CHECK(themes->pickSlots(QStringLiteral("nonsense")).isEmpty());

    const auto categories = [](const QVariantList& offered) {
        QStringList out;
        for (const auto& slot : offered) {
            out << slot.toMap().value(QStringLiteral("category")).toString();
        }
        return out;
    };
    // A text theme can be its own category's default; a background any that
    // takes one.
    CHECK(categories(themes->defaultSlots(QStringLiteral("song"))) == QStringList{QStringLiteral("song")});
    CHECK(categories(themes->defaultSlots(QStringLiteral("bible")))
          == QStringList{QStringLiteral("bible")});
    CHECK(categories(themes->defaultSlots(QStringLiteral("media")))
          == QStringList{QStringLiteral("media")});
    CHECK(themes->defaultSlots(QStringLiteral("text")).isEmpty());
    CHECK(categories(themes->defaultSlots(QStringLiteral("bg")))
          == QStringList{QStringLiteral("song"), QStringLiteral("bible")});
}

TEST_CASE("SessionFacade theme gallery preview cache hits and invalidates", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("bg"), QStringLiteral("Cached"));
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto tid = created.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(tid.isEmpty());

    QVariantMap style = created.value(QStringLiteral("style")).toMap();
    style.insert(QStringLiteral("background_type"), QStringLiteral("color"));
    style.insert(QStringLiteral("background_color"), QStringLiteral("#112233"));
    REQUIRE(facade.themes()->update_theme(QStringLiteral("Main"),
                               QStringLiteral("bg"),
                               tid,
                               QStringLiteral("Cached"),
                               seventhslide::app::to_json(QVariant(style)),
                               QString(),
                               false)
                .isEmpty());

    const QString id = QStringLiteral("theme/Main/bg/") + tid + QStringLiteral("?c=bg&r=0");
    const QSize size(160, 90);
    const QImage first = facade.themes()->render_preview(id, size);
    REQUIRE_FALSE(first.isNull());
    const QImage second = facade.themes()->render_preview(id, size);
    REQUIRE(second.size() == first.size());
    REQUIRE(second == first);

    style.insert(QStringLiteral("background_color"), QStringLiteral("#ff00aa"));
    REQUIRE(facade.themes()->update_theme(QStringLiteral("Main"),
                               QStringLiteral("bg"),
                               tid,
                               QStringLiteral("Cached"),
                               seventhslide::app::to_json(QVariant(style)),
                               QString(),
                               false)
                .isEmpty());
    REQUIRE(facade.themes()->theme_map(QStringLiteral("Main"), QStringLiteral("bg"), tid)
                .value(QStringLiteral("style"))
                .toMap()
                .value(QStringLiteral("background_color"))
                .toString()
            == QStringLiteral("#ff00aa"));

    const QImage after = facade.themes()->render_preview(
        QStringLiteral("theme/Main/bg/") + tid + QStringLiteral("?c=bg&r=1"), size);
    REQUIRE_FALSE(after.isNull());
    REQUIRE(after.size() == first.size());
    REQUIRE(after != first);
}

TEST_CASE("SessionFacade text theme preview ignores partner background", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto bg_created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("bg"), QStringLiteral("Magenta"));
    REQUIRE_FALSE(bg_created.contains(QStringLiteral("error")));
    const auto bg_id = bg_created.value(QStringLiteral("id")).toString();
    QVariantMap bg_style = bg_created.value(QStringLiteral("style")).toMap();
    bg_style.insert(QStringLiteral("background_type"), QStringLiteral("color"));
    bg_style.insert(QStringLiteral("background_color"), QStringLiteral("#ff00aa"));
    REQUIRE(facade.themes()->update_theme(QStringLiteral("Main"),
                               QStringLiteral("bg"),
                               bg_id,
                               QStringLiteral("Magenta"),
                               seventhslide::app::to_json(QVariant(bg_style)),
                               QString(),
                               false)
                .isEmpty());
    REQUIRE(facade.themes()->setDefault(
                QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("bg"), bg_id)
                .isEmpty());

    const auto text_created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("Lyrics"));
    REQUIRE_FALSE(text_created.contains(QStringLiteral("error")));
    const auto text_id = text_created.value(QStringLiteral("id")).toString();

    const QSize size(160, 90);
    const QImage thumb = facade.themes()->render_preview(
        QStringLiteral("theme/Main/song/") + text_id + QStringLiteral("?c=song&r=0"), size);
    REQUIRE_FALSE(thumb.isNull());
    REQUIRE(thumb.size() == size);
    // A song theme shows its own contexts only: naming none, or another
    // kind's, shows the song design.
    CHECK(facade.themes()->render_preview(
              QStringLiteral("theme/Main/song/") + text_id + QStringLiteral("?r=0"), size)
          == thumb);
    CHECK(facade.themes()->render_preview(
              QStringLiteral("theme/Main/song/") + text_id + QStringLiteral("?c=bible&r=0"), size)
          == thumb);

    QImage grid(size, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&grid);
        seventhslide::render::paint_transparency_checkerboard(
            p, QRectF(0, 0, size.width(), size.height()));
    }
    REQUIRE(thumb.pixel(0, 0) == grid.pixel(0, 0));
    REQUIRE(thumb.pixel(8, 0) == grid.pixel(8, 0));
    REQUIRE(thumb.pixel(0, 0) != thumb.pixel(8, 0));

    QVariantMap draft_style;
    draft_style.insert(QStringLiteral("font_size"), 48);
    draft_style.insert(QStringLiteral("highlight_color"), QStringLiteral("#ffffff"));
    facade.themes()->update_draft_preview(QStringLiteral("Main"),
                                   QStringLiteral("song"),
                                   QStringLiteral("song"),
                                   seventhslide::app::to_json(QVariant(draft_style)));
    const QImage draft = facade.themes()->render_draft_preview(size);
    REQUIRE_FALSE(draft.isNull());
    // Designer stage paints the checkerboard in QML at display size.
    REQUIRE(qAlpha(draft.pixel(0, 0)) == 0);
    REQUIRE(qAlpha(draft.pixel(8, 0)) == 0);

    // The sample lyrics are in the frame the stage paints, at the draft's
    // type, and painted into it: the stage draws no words of its own.
    const auto& frame = facade.themes()->draft_frame();
    REQUIRE(frame.text.has_value());
    REQUIRE(frame.text->box.font_size == 48);
    REQUIRE(draft_body_words(*facade.themes()).contains(QStringLiteral("heavens")));
    const auto& box = frame.text->box;
    const QImage stage = facade.themes()->render_draft_preview(QSize(960, 540));
    CHECK(pixels_in(stage, QSize(frame.canvas_width, frame.canvas_height), box.x, box.y,
                    box.width, box.height, [](QRgb px) { return qAlpha(px) > 100; })
          > 0);
}

TEST_CASE("SessionFacade bg theme preview paints no clock", "[app]")
{
    // The clock is a text-theme element. Freeform's editor uses this bg thumb as
    // its canvas backdrop, so a clock baked in here ghosts underneath the
    // document's own (live, ticking) Clock object.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto bg_created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("bg"), QStringLiteral("Flat"));
    REQUIRE_FALSE(bg_created.contains(QStringLiteral("error")));
    const auto bg_id = bg_created.value(QStringLiteral("id")).toString();
    QVariantMap bg_style = bg_created.value(QStringLiteral("style")).toMap();
    bg_style.insert(QStringLiteral("background_type"), QStringLiteral("color"));
    bg_style.insert(QStringLiteral("background_color"), QStringLiteral("#ff00aa"));
    REQUIRE(facade.themes()->update_theme(QStringLiteral("Main"), QStringLiteral("bg"), bg_id,
                               QStringLiteral("Flat"), seventhslide::app::to_json(QVariant(bg_style)), QString(), false)
                .isEmpty());

    const auto text_created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("Clocked"));
    REQUIRE_FALSE(text_created.contains(QStringLiteral("error")));
    const auto text_id = text_created.value(QStringLiteral("id")).toString();
    // A big clock in the text theme's own design: the thing that must not leak.
    QVariantMap text_style = text_created.value(QStringLiteral("style")).toMap();
    auto design = seventhslide::theme::ElementSet::from_json(nlohmann::json::parse(
        QJsonDocument(QJsonObject::fromVariantMap(
                          text_style.value(QStringLiteral("elements")).toMap()))
            .toJson()
            .toStdString()));
    design.song.push_back(seventhslide::test::clock_element("te_clock", 40, 40, 30, 300));
    text_style.insert(QStringLiteral("elements"),
                      QJsonDocument::fromJson(QByteArray::fromStdString(design.to_json().dump()))
                          .object()
                          .toVariantMap());
    REQUIRE(facade.themes()->update_theme(QStringLiteral("Main"), QStringLiteral("song"), text_id,
                               QStringLiteral("Clocked"), seventhslide::app::to_json(QVariant(text_style)), QString(), false)
                .isEmpty());
    // Guard against this test going quiet: the clock really is in the theme.
    const seventhslide::model::ThemeEntry* stored = nullptr;
    for (const auto& oc : facade.session().outputs()) {
        if (oc.name == "Main") {
            stored = seventhslide::theme::find_theme(oc.song_themes, text_id.toStdString());
        }
    }
    REQUIRE(stored != nullptr);
    REQUIRE(seventhslide::theme::has_clock(
        seventhslide::theme::ElementSet::from_json(seventhslide::theme::element_model(stored->style))));
    REQUIRE(facade.themes()->setDefault(
                QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("theme"), text_id)
                .isEmpty());

    const QSize size(192, 108);
    const QImage thumb = facade.themes()->render_preview(
        QStringLiteral("theme/Main/bg/") + bg_id + QStringLiteral("?c=bg&r=0"), size);
    REQUIRE_FALSE(thumb.isNull());
    REQUIRE(thumb.size() == size);

    // Whole fill, including the band the clock would have occupied.
    const QRgb fill = thumb.pixel(size.width() - 1, size.height() - 1);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            REQUIRE(thumb.pixel(x, y) == fill);
        }
    }
}

TEST_CASE("SessionFacade theme preview grid uses a fixed cell size", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    const QSize size(160, 90);
    const QImage grid = facade.themes()->render_preview(QStringLiteral("grid?cell=24"), size);
    REQUIRE_FALSE(grid.isNull());
    REQUIRE(grid.size() == size);

    QImage expected(size, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&expected);
        seventhslide::render::paint_transparency_checkerboard(
            p, QRectF(0, 0, size.width(), size.height()), 24);
    }
    REQUIRE(grid.pixel(0, 0) == expected.pixel(0, 0));
    REQUIRE(grid.pixel(8, 0) == expected.pixel(8, 0));
    REQUIRE(grid.pixel(8, 0) == grid.pixel(0, 0));
    REQUIRE(grid.pixel(24, 0) != grid.pixel(0, 0));
}

TEST_CASE("SessionFacade theme draft paints the Bible sample", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    QVariantMap draft_style;
    draft_style.insert(QStringLiteral("show_bible_text"), true);
    draft_style.insert(QStringLiteral("bible_text_box_width"), 1280);
    draft_style.insert(QStringLiteral("bible_text_box_height"), 400);
    facade.themes()->update_draft_preview(QStringLiteral("Main"),
                                   QStringLiteral("bible"),
                                   QStringLiteral("bible"),
                                   seventhslide::app::to_json(QVariant(draft_style)));
    REQUIRE(facade.themes()->draft_frame().text.has_value());
    REQUIRE(draft_body_words(*facade.themes()).contains(QStringLiteral("everlasting gospel")));
}

// The designer stage is the draft raster under chrome, so the raster has to
// stack a design as the output does: each element at its own z, the lyrics
// and a media area's stand-in among them.
TEST_CASE("The designer's draft paints the lyrics at their place among the shapes",
          "[app][theme][paint_order]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto draft_with_box = [&](int box_z) {
        auto set = seventhslide::devsupport::elements_from_flat_style(
            {{"font_size", 72}, {"font_color", "#ffffff"}});
        auto song = set.to_json().at("song");
        int lyrics_x = 0;
        int lyrics_y = 0;
        int lyrics_w = 0;
        int lyrics_h = 0;
        for (auto& el : song) {
            if (el.value("binding", "") == "lyrics") {
                el["z"] = 10;
                lyrics_x = static_cast<int>(el.value("x", 0.0));
                lyrics_y = static_cast<int>(el.value("y", 0.0));
                lyrics_w = static_cast<int>(el.value("w", 0.0));
                lyrics_h = static_cast<int>(el.value("h", 0.0));
            }
        }
        song.push_back(red_box("te_red", lyrics_x, lyrics_y, lyrics_w, lyrics_h, box_z));
        auto elements = set.to_json();
        elements["song"] = song;
        nlohmann::json style = {{"font_size", 72}, {"font_color", "#ffffff"},
                                {"elements", elements}};
        facade.themes()->update_draft_preview(QStringLiteral("Main"), QStringLiteral("song"),
                                              QStringLiteral("song"), style);
        const auto& frame = facade.themes()->draft_frame();
        REQUIRE(frame.text.has_value());
        const auto& box = frame.text->box;
        const QImage img = facade.themes()->render_draft_preview(QSize(960, 540));
        const QSize canvas(frame.canvas_width, frame.canvas_height);
        return std::pair{pixels_in(img, canvas, box.x, box.y, box.width, box.height, is_light),
                         pixels_in(img, canvas, box.x, box.y, box.width, box.height, is_red)};
    };

    // A box sent behind the lyrics: the words show over it.
    const auto [words_over, red_under] = draft_with_box(5);
    CHECK(words_over > 0);
    CHECK(red_under > 0);
    // A box brought in front of them: it covers every word.
    const auto [words_under, red_over] = draft_with_box(20);
    CHECK(words_under == 0);
    CHECK(red_over > red_under);
}

TEST_CASE("The designer's draft paints a media area's stand-in at its place among the shapes",
          "[app][theme][paint_order]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    // The image area over the left half; a red box straddling its edge.
    const auto spot_with_box = [&](int box_z) {
        auto set = seventhslide::devsupport::elements_from_flat_style({{"font_size", 36}});
        auto elements = set.to_json();
        auto image = elements.at("image");
        for (auto& el : image) {
            if (el.value("binding", "") == "image_area") {
                el["x"] = 0;
                el["y"] = 0;
                el["w"] = 960;
                el["h"] = 1080;
                el["z"] = 10;
            }
        }
        image.push_back(red_box("te_red", 800, 100, 300, 200, box_z));
        elements["image"] = image;
        nlohmann::json style = {{"font_size", 36}, {"elements", elements}};
        facade.themes()->update_draft_preview(QStringLiteral("Main"), QStringLiteral("media"),
                                              QStringLiteral("image"), style);
        const auto& frame = facade.themes()->draft_frame();
        REQUIRE(frame.media_stand_in.has_value());
        const QImage img = facade.themes()->render_draft_preview(QSize(960, 540));
        const QSize canvas(frame.canvas_width, frame.canvas_height);
        struct Spot {
            int red_inside;
            int bars_inside;
            int red_outside;
        };
        // Inside the area, on its last, blue, bar (823-960), and outside
        // it (960-1100).
        const auto bar_blue = [](QRgb px) {
            return qBlue(px) > 150 && qRed(px) < 40 && qGreen(px) < 40;
        };
        return Spot{pixels_in(img, canvas, 830, 110, 120, 180, is_red),
                    pixels_in(img, canvas, 830, 110, 120, 180, bar_blue),
                    pixels_in(img, canvas, 970, 110, 120, 180, is_red)};
    };

    // Under the area: the stand-in's bars hide it there, as a picture would
    // on the output. Past the area's edge it shows.
    const auto under = spot_with_box(5);
    CHECK(under.red_inside == 0);
    CHECK(under.bars_inside > 0);
    CHECK(under.red_outside > 0);
    // Over the area: it shows on the bars.
    const auto over = spot_with_box(20);
    CHECK(over.red_inside > 0);
    CHECK(over.bars_inside == 0);
    CHECK(over.red_outside > 0);
}

TEST_CASE("QTextDocument applies CSS line-height on block tags, not spans", "[app][text]")
{
    seventhslide::test::ensure_qt_application();

    QTextDocument span_doc;
    span_doc.setHtml(QStringLiteral("<span style=\"line-height:1.15\">Hello<br/>World</span>"));
    REQUIRE(span_doc.begin().blockFormat().lineHeightType() == QTextBlockFormat::SingleHeight);

    QTextDocument unitless_doc;
    unitless_doc.setHtml(
        QStringLiteral("<div style=\"margin:0; line-height:1.15\">Hello<br/>World</div>"));
    const auto unitless = unitless_doc.begin().blockFormat();
    REQUIRE(unitless.lineHeightType() == QTextBlockFormat::ProportionalHeight);
    REQUIRE(unitless.lineHeight() == Catch::Approx(115.0));
    REQUIRE(unitless.topMargin() == Catch::Approx(0.0));
    REQUIRE(unitless.bottomMargin() == Catch::Approx(0.0));

    QTextDocument px_doc;
    px_doc.setHtml(
        QStringLiteral("<div style=\"margin:0; line-height:56px\">Hello</div>"
                       "<div style=\"margin:0; line-height:37px\">World</div>"));
    const auto first = px_doc.begin().blockFormat();
    // Qt maps CSS ``Npx`` to MinimumHeight (not FixedHeight). Preview/edit
    // therefore re-apply FixedHeight via apply_text_edit_line_height.
    REQUIRE(first.lineHeightType() == QTextBlockFormat::MinimumHeight);
    REQUIRE(first.lineHeight() == Catch::Approx(56.0));
    const auto second = px_doc.begin().next().blockFormat();
    REQUIRE(second.lineHeightType() == QTextBlockFormat::MinimumHeight);
    REQUIRE(second.lineHeight() == Catch::Approx(37.0));
}

TEST_CASE("apply_text_edit_line_height uses CSS pixel line boxes", "[app][text]")
{
    seventhslide::test::ensure_qt_application();

    QFont face;
    face.setPixelSize(48);

    QTextDocument doc;
    doc.setDefaultFont(face);
    doc.setHtml(QStringLiteral("<span style=\"font-size:48px\">Hello<br/>World</span>"));
    REQUIRE(doc.documentMargin() == Catch::Approx(4.0));

    seventhslide::app::apply_text_edit_line_height(&doc, 1.15, 0.0, 48.0);
    REQUIRE(doc.documentMargin() == Catch::Approx(0.0));
    const auto fmt = doc.begin().blockFormat();
    REQUIRE(fmt.lineHeightType() == QTextBlockFormat::FixedHeight);
    REQUIRE(fmt.lineHeight()
            == Catch::Approx(seventhslide::layout::css_line_height_px(48.0, 1.15)));
    REQUIRE(fmt.topMargin() == Catch::Approx(0.0));
    REQUIRE(fmt.bottomMargin() == Catch::Approx(0.0));

    QTextDocument mixed;
    mixed.setDefaultFont(face);
    mixed.setHtml(QStringLiteral(
        "<div><span style=\"font-size:48px\">Active</span></div>"
        "<div><span style=\"font-size:32px\">Upcoming</span></div>"));
    seventhslide::app::apply_text_edit_line_height(&mixed, 1.15, 0.0, 48.0);
    REQUIRE(mixed.begin().blockFormat().lineHeight()
            == Catch::Approx(seventhslide::layout::css_line_height_px(48.0, 1.15)));
    REQUIRE(mixed.begin().next().blockFormat().lineHeight()
            == Catch::Approx(seventhslide::layout::css_line_height_px(32.0, 1.15)));
}

TEST_CASE("SessionFacade round-trips a theme's element design", "[app][elements]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("Elements"));
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto tid = created.value(QStringLiteral("id")).toString();

    // A new song theme is born with an element design, one list per context
    // of its own, and none of another kind's.
    auto style = created.value(QStringLiteral("style")).toMap();
    REQUIRE(style.contains(QStringLiteral("elements")));
    auto elements = style.value(QStringLiteral("elements")).toMap();
    for (const auto* ctx : {"song", "titles"}) {
        REQUIRE(elements.contains(QString::fromLatin1(ctx)));
    }
    for (const auto* ctx : {"bible", "image", "video", "web", "live", "audio"}) {
        REQUIRE_FALSE(elements.contains(QString::fromLatin1(ctx)));
    }
    auto song = elements.value(QStringLiteral("song")).toList();
    REQUIRE_FALSE(song.isEmpty());

    // Insert a text box the way the designer does, and move the body.
    QVariantMap licence;
    licence.insert(QStringLiteral("id"), QStringLiteral("te99"));
    licence.insert(QStringLiteral("type"), QStringLiteral("text"));
    licence.insert(QStringLiteral("x"), 60);
    licence.insert(QStringLiteral("y"), 940);
    licence.insert(QStringLiteral("w"), 900);
    licence.insert(QStringLiteral("h"), 80);
    licence.insert(QStringLiteral("z"), 40);
    licence.insert(QStringLiteral("text"), QStringLiteral("CCLI License #{-ccli-license}"));
    licence.insert(QStringLiteral("slide_rule"), QStringLiteral("first"));
    licence.insert(QStringLiteral("slide_count"), 2);
    song.append(licence);

    for (int i = 0; i < song.size(); ++i) {
        auto el = song[i].toMap();
        if (el.value(QStringLiteral("binding")).toString() == QStringLiteral("lyrics")) {
            el.insert(QStringLiteral("x"), 333);
            song[i] = el;
        }
    }
    elements.insert(QStringLiteral("song"), song);
    style.insert(QStringLiteral("elements"), elements);

    REQUIRE(facade.themes()->update_theme(QStringLiteral("Main"),
                               QStringLiteral("song"),
                               tid,
                               QStringLiteral("Elements"),
                               seventhslide::app::to_json(QVariant(style)))
                .isEmpty());

    const auto loaded = facade.themes()->theme_map(QStringLiteral("Main"), QStringLiteral("song"), tid);
    const auto back = loaded.value(QStringLiteral("style"))
                          .toMap()
                          .value(QStringLiteral("elements"))
                          .toMap()
                          .value(QStringLiteral("song"))
                          .toList();
    bool saw_licence = false;
    bool moved_body = false;
    for (const auto& v : back) {
        const auto el = v.toMap();
        if (el.value(QStringLiteral("id")).toString() == QStringLiteral("te99")) {
            saw_licence = true;
            REQUIRE(el.value(QStringLiteral("text")).toString()
                    == QStringLiteral("CCLI License #{-ccli-license}"));
            REQUIRE(el.value(QStringLiteral("slide_rule")).toString()
                    == QStringLiteral("first"));
            REQUIRE(el.value(QStringLiteral("slide_count")).toInt() == 2);
        }
        if (el.value(QStringLiteral("binding")).toString() == QStringLiteral("lyrics")) {
            moved_body = el.value(QStringLiteral("x")).toInt() == 333;
        }
    }
    REQUIRE(saw_licence);
    REQUIRE(moved_body);

    // The designer preview reads the same blob the database now holds.
    facade.themes()->update_draft_preview(QStringLiteral("Main"),
                                   QStringLiteral("song"),
                                   QStringLiteral("song"),
                                   seventhslide::app::to_json(loaded.value(QStringLiteral("style"))));
    REQUIRE(facade.themes()->draft_frame().text.has_value());
    // The inserted box paints in the preview, tokens filled from the fixture
    // song - the same objects the audience frame would carry.
    const QImage img = facade.themes()->render_draft_preview(QSize(960, 540));
    REQUIRE(img.width() == 960);
    int ink = 0;
    for (int y = 460; y < 520 && y < img.height(); ++y) {
        for (int x = 60; x < 500 && x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 8) {
                ++ink;
            }
        }
    }
    REQUIRE(ink > 0);
}

// A theme and a Freeform deck are kept on their output's canvas; the designer
// edits what is stored. When the output changes canvas, both are carried over
// once, with the outputs, and the result says so.
TEST_CASE("Changing an output's canvas carries its themes and Freeform decks over once",
          "[app][theme][freeform][canvas]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const QString main = QStringLiteral("Main");
    const QString text = QStringLiteral("song");
    const auto& output = facade.session().outputs().at(0);
    const std::string output_id = output.id;
    const auto tid = QString::fromStdString(output.song_themes.at(0).id);

    const auto lyrics_of = [](const QVariantMap& theme) {
        const auto style = seventhslide::app::to_json(theme.value(QStringLiteral("style")));
        for (const auto& el : style.at("elements").at("song")) {
            if (el.value("binding", "") == "lyrics") {
                return el;
            }
        }
        return nlohmann::json::object();
    };
    const auto stored_canvas = [&facade, &tid] {
        for (const auto& t : facade.session().outputs().at(0).song_themes) {
            if (t.id == tid.toStdString()) {
                return seventhslide::theme::ElementSet::from_json(t.style.at("elements")).canvas;
            }
        }
        return seventhslide::geom::CanvasSize{};
    };

    // A Freeform item with a deck on Main: a text box and a square.
    auto doc = seventhslide::freeform::make_document("Deck", {output_id});
    auto* deck = seventhslide::freeform::find_deck(doc, output_id);
    REQUIRE(deck != nullptr);
    seventhslide::freeform::Object box;
    box.id = "o_text";
    box.type = seventhslide::freeform::ObjectType::Text;
    box.x = 320;
    box.y = 340;
    box.w = 1280;
    box.h = 400;
    box.font_size = 48;
    seventhslide::freeform::Object square;
    square.id = "o_square";
    square.type = seventhslide::freeform::ObjectType::Shape;
    square.x = 760;
    square.y = 340;
    square.w = 400;
    square.h = 400;
    deck->slides[0].objects = {box, square};
    const qint64 item = facade.freeformLibrary()->createFreeform(QStringLiteral("Deck"));
    REQUIRE(item >= 0);
    REQUIRE(facade.freeformLibrary()->commit_freeform_document(item, doc).isEmpty());
    const auto stored_deck = [&facade, item, &output_id] {
        auto loaded = facade.freeformLibrary()->load_freeform_document(item);
        REQUIRE(loaded.has_value());
        const auto* d = seventhslide::freeform::find_deck(*loaded, output_id);
        REQUIRE(d != nullptr);
        return *d;
    };

    // The designer is handed the stored design, as is.
    auto lyrics = lyrics_of(facade.themes()->theme_map(main, text, tid));
    REQUIRE(lyrics.at("x").get<double>() == Catch::Approx(320));
    REQUIRE(lyrics.at("font_size") == 48);

    QSignalSpy fitted(facade.outputSettings(),
                      &seventhslide::app::OutputSettingsController::canvasFitted);
    const auto resize = [&](int w, int h) {
        const auto result = facade.outputSettings()->updateOutputSettings(
            main, {{QStringLiteral("name"), main},
                   {QStringLiteral("canvasWidth"), w},
                   {QStringLiteral("canvasHeight"), h}});
        REQUIRE(result.value(QStringLiteral("error")).toString().isEmpty());
        return result.value(QStringLiteral("fitted")).toString();
    };

    // On its side: the theme and the deck are carried over, and said so.
    CHECK(resize(1080, 1920)
          == QStringLiteral("3 themes and 1 Freeform item were fitted to 1080 × 1920"));
    CHECK(fitted.count() == 1);
    REQUIRE(stored_canvas() == seventhslide::geom::CanvasSize{1080, 1920});
    lyrics = lyrics_of(facade.themes()->theme_map(main, text, tid));
    CHECK(lyrics.at("x").get<double>() == Catch::Approx(180));
    CHECK(lyrics.at("w").get<double>() == Catch::Approx(720));
    CHECK(lyrics.at("font_size") == 48);
    auto on_side = stored_deck();
    REQUIRE(on_side.canvas == seventhslide::geom::CanvasSize{1080, 1920});
    CHECK(on_side.slides[0].objects[0].x == Catch::Approx(180));
    CHECK(on_side.slides[0].objects[0].font_size == 48);
    CHECK(on_side.slides[0].objects[1].w == Catch::Approx(400));
    CHECK(on_side.slides[0].objects[1].h == Catch::Approx(400));

    // Saving what the designer shows keeps it there.
    const auto theme = facade.themes()->theme_map(main, text, tid);
    REQUIRE(facade
                .themes()->update_theme(main, text, tid, theme.value(QStringLiteral("name")).toString(),
                             seventhslide::app::to_json(QVariant(theme.value(QStringLiteral("style")).toMap())), QString(), false)
                .isEmpty());
    REQUIRE(stored_canvas() == seventhslide::geom::CanvasSize{1080, 1920});
    CHECK(lyrics_of(facade.themes()->theme_map(main, text, tid)).at("font_size") == 48);

    // And back: the originals, within a pixel.
    CHECK(resize(1920, 1080)
          == QStringLiteral("3 themes and 1 Freeform item were fitted to 1920 × 1080"));
    lyrics = lyrics_of(facade.themes()->theme_map(main, text, tid));
    CHECK(lyrics.at("x").get<double>() == Catch::Approx(320).margin(1));
    CHECK(lyrics.at("w").get<double>() == Catch::Approx(1280).margin(1));
    CHECK(lyrics.at("font_size") == 48);
    auto back = stored_deck();
    REQUIRE(back.canvas == seventhslide::geom::CanvasSize{1920, 1080});
    CHECK(back.slides[0].objects[0].x == Catch::Approx(320).margin(1));
    CHECK(back.slides[0].objects[1].x == Catch::Approx(760).margin(1));
    CHECK(back.slides[0].objects[1].y == Catch::Approx(340).margin(1));
    CHECK(back.slides[0].objects[1].w == Catch::Approx(400).margin(1));

    // Twice the size: everything doubled.
    CHECK(resize(3840, 2160)
          == QStringLiteral("3 themes and 1 Freeform item were fitted to 3840 × 2160"));
    lyrics = lyrics_of(facade.themes()->theme_map(main, text, tid));
    CHECK(lyrics.at("x").get<double>() == Catch::Approx(640));
    CHECK(lyrics.at("font_size") == 96);
    CHECK(stored_deck().slides[0].objects[0].font_size == 96);

    // A rename alone moves nothing and says nothing.
    const auto renamed = facade.outputSettings()->updateOutputSettings(
        main, {{QStringLiteral("name"), QStringLiteral("Hall")}});
    CHECK(renamed.value(QStringLiteral("name")).toString() == QStringLiteral("Hall"));
    CHECK_FALSE(renamed.contains(QStringLiteral("fitted")));
    CHECK(fitted.count() == 3);
}

// Changing an output's canvas writes every Freeform item of the output in one
// transaction on the GUI thread, which should take well under 100 ms for a
// library of hundreds.
TEST_CASE("Resizing an output with hundreds of Freeform items is quick", "[app][freeform][canvas]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const QString main = QStringLiteral("Main");
    const std::string output_id = facade.session().outputs().at(0).id;

    constexpr int k_items = 200;
    for (int i = 0; i < k_items; ++i) {
        auto doc = seventhslide::freeform::make_document("Deck " + std::to_string(i), {output_id});
        auto* deck = seventhslide::freeform::find_deck(doc, output_id);
        REQUIRE(deck != nullptr);
        for (int slide = 0; slide < 10; ++slide) {
            seventhslide::freeform::Slide s;
            s.id = "s" + std::to_string(slide);
            for (int o = 0; o < 5; ++o) {
                seventhslide::freeform::Object obj;
                obj.id = "o" + std::to_string(slide * 5 + o);
                obj.type = o % 2 == 0 ? seventhslide::freeform::ObjectType::Text
                                      : seventhslide::freeform::ObjectType::Shape;
                obj.x = 100 + o * 50;
                obj.y = 100 + slide * 20;
                obj.w = 300;
                obj.h = 200;
                obj.text = "Line " + std::to_string(o);
                s.objects.push_back(obj);
            }
            deck->slides.push_back(std::move(s));
        }
        const qint64 item =
            facade.freeformLibrary()->createFreeform(QString::fromStdString(doc.name));
        REQUIRE(item >= 0);
        REQUIRE(facade.freeformLibrary()->commit_freeform_document(item, doc).isEmpty());
    }

    // Where the resize's own time goes: reading the rows, loading the
    // documents, moving them, writing them.
    {
        auto* db = facade.open_library().database();
        REQUIRE(db != nullptr);
        seventhslide::db::FreeformRepository repo(*db);
        QElapsedTimer part;
        part.start();
        auto items = repo.list_items();
        const qint64 list_ms = part.restart();
        std::vector<seventhslide::freeform::Document> docs;
        for (const auto& it : items) {
            docs.push_back(*seventhslide::freeform::load_document(it.document, it.name));
        }
        const qint64 load_ms = part.restart();
        for (auto& d : docs) {
            REQUIRE(seventhslide::freeform::resize_output_deck(d, output_id, {1080, 1920}));
        }
        const qint64 move_ms = part.restart();
        for (std::size_t i = 0; i < docs.size(); ++i) {
            items[i].document = docs[i].to_json();
        }
        const qint64 json_ms = part.restart();
        {
            seventhslide::db::Transaction tx(*db);
            for (const auto& it : items) {
                repo.update_item(it);
            }
            // Rolled back: the real resize below writes.
        }
        const qint64 write_ms = part.elapsed();
        WARN("list " << list_ms << " ms, load " << load_ms << " ms, move " << move_ms
                     << " ms, to_json " << json_ms << " ms, write " << write_ms << " ms");
    }

    // What any edit of the output costs (the save, the rebuild, the Freeform
    // browser), to set against the resize.
    QElapsedTimer clock;
    clock.start();
    REQUIRE(facade.outputSettings()
                ->updateOutputSettings(main, {{QStringLiteral("name"), main}})
                .value(QStringLiteral("error"))
                .toString()
                .isEmpty());
    const qint64 edit_ms = clock.restart();
    const auto result = facade.outputSettings()->updateOutputSettings(
        main, {{QStringLiteral("name"), main},
               {QStringLiteral("canvasWidth"), 1080},
               {QStringLiteral("canvasHeight"), 1920}});
    const qint64 ms = clock.elapsed();
    REQUIRE(result.value(QStringLiteral("error")).toString().isEmpty());
    CHECK(result.value(QStringLiteral("fitted")).toString()
          == QStringLiteral("3 themes and 200 Freeform items were fitted to 1080 × 1920"));
    WARN("resizing " << k_items << " Freeform items took " << ms
                     << " ms, of which any edit of the output costs " << edit_ms << " ms");
    // The number is the point (the WARN shows it); the bound only
    // catches a resize gone quadratic.
    CHECK(ms - edit_ms < 20000);
}

TEST_CASE("Theme designer selection signals only while a theme is open", "[app][theme][editor]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    auto* te = facade.slideEdit();
    auto* objects = te->objects();
    QSignalSpy doc(te, &seventhslide::app::SlideEditSession::documentChanged);
    QSignalSpy sel(te, &seventhslide::app::SlideEditSession::selectionChanged);
    const auto signals_since = [&]() {
        const auto out = std::pair{static_cast<int>(doc.count()), static_cast<int>(sel.count())};
        doc.clear();
        sel.clear();
        return out;
    };

    // Closed: nothing to select, and nothing announced.
    REQUIRE_FALSE(te->active());
    objects->selectObject(QStringLiteral("x"));
    objects->selectIds({QStringLiteral("x")});
    objects->selectAll();
    objects->clearSelection();
    CHECK(signals_since() == std::pair{0, 0});

    open_text_theme(facade);
    REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
    const QString id = objects->selection().value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(id.isEmpty());
    signals_since();

    objects->clearSelection();
    CHECK(signals_since() == std::pair{0, 1});
    CHECK(objects->selectedIds().isEmpty());
    objects->selectIds({id});
    CHECK(signals_since() == std::pair{0, 1});
    CHECK(objects->selectedIds() == QStringList{id});
    objects->selectObject(id);
    CHECK(signals_since() == std::pair{0, 1});
    objects->selectAll();
    CHECK(signals_since() == std::pair{0, 1});
    // A context change swaps the element list too.
    te->setContext(QStringLiteral("titles"));
    CHECK(signals_since() == std::pair{1, 1});
}

TEST_CASE("Both canvas editors refuse an arrange choice outside its enum the same way",
          "[app][theme][freeform][editor]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    // What QML could pass as a bare number.
    const auto align = static_cast<CanvasAlignQml::Align>(99);
    const auto dist = static_cast<CanvasDistributeQml::Distribute>(99);
    const auto axis = static_cast<CanvasFlipQml::Axis>(99);
    const auto order = static_cast<CanvasOrderQml::Order>(99);
    const auto refusals = [&] {
        auto* editor = facade.slideEdit()->objects();
        return QStringList{editor->align(align), editor->distribute(dist), editor->flip(axis),
                           editor->reorderSelected(order)};
    };
    using seventhslide::app::SlideObjectsSession;
    const QStringList expected{SlideObjectsSession::tr("That is not a way to align objects."),
                               SlideObjectsSession::tr("That is not a way to space objects out."),
                               SlideObjectsSession::tr("That is not a way to flip an object."),
                               SlideObjectsSession::tr("That is not a way to order objects.")};
    open_text_theme(facade);
    const QStringList theme = refusals();
    CHECK(theme == expected);
    facade.slideEdit()->cancel();
    REQUIRE(facade.slideEdit()->openFreeform(facade.freeformLibrary()->createFreeform(QStringLiteral("Card"))));
    CHECK(refusals() == theme);
}

TEST_CASE("A text theme's elements copy and paste through the same canvas commands",
          "[app][theme][clipboard]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    auto* edit = facade.slideEdit();
    auto* objects = edit->objects();
    open_text_theme(facade);
    const auto had = objects->canvasObjects().size();
    REQUIRE(objects->insert(QStringLiteral("text")).isEmpty());
    REQUIRE(objects->canvasObjects().size() == had + 1);
    QSignalSpy clips(objects, &seventhslide::app::SlideObjectsSession::clipboardChanged);
    REQUIRE(objects->copySelected().isEmpty());
    CHECK(clips.size() == 1);
    REQUIRE(objects->canPaste());
    REQUIRE(objects->paste().isEmpty());
    CHECK(objects->canvasObjects().size() == had + 2);
    CHECK(objects->selectedIds().size() == 1);
    CHECK(edit->dirty());
    REQUIRE(edit->undo().isEmpty());
    CHECK(objects->canvasObjects().size() == had + 1);
    edit->cancel();

    // The theme's clipboard is not the Freeform editor's.
    REQUIRE(edit->openFreeform(facade.freeformLibrary()->createFreeform(QStringLiteral("Card"))));
    CHECK_FALSE(objects->canPaste());
    CHECK(objects->paste() == QStringLiteral("nothing to paste"));
}

TEST_CASE("Whether a theme is open and saved is the slide editor's to say", "[app][theme]")
{
    // The canvas once said too, and missed a theme's tags and settings.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    auto* edit = facade.slideEdit();
    open_text_theme(facade);
    edit->theme()->setTags({QStringLiteral("Easter")});
    CHECK(edit->active());
    CHECK(edit->dirty());
    const auto* objects = edit->objects()->metaObject();
    CHECK(objects->indexOfProperty("active") < 0);
    CHECK(objects->indexOfProperty("dirty") < 0);
    edit->cancel();
    CHECK_FALSE(edit->active());
    CHECK_FALSE(edit->dirty());
}

TEST_CASE("The box being typed into stays out of the draft through the edits made meanwhile",
          "[app][theme][paint_order]")
{
    // QML draws the box being typed into over the stage. An edit made while
    // typing (a size, bold) paints the draft again, which must still leave it
    // out, or its words show twice.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    facade.set_outputs({make_main()});
    auto* edit = facade.slideEdit();
    open_text_theme(facade);
    auto* objects = edit->objects();
    REQUIRE(objects->insert(QStringLiteral("text"), {{QStringLiteral("binding"), QStringLiteral("static")}})
                .isEmpty());
    const QString id = objects->selection().value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(objects->setText(id, QStringLiteral("Welcome"), false, false, false).isEmpty());
    const auto in_draft = [&] {
        for (const auto& so : facade.themes()->draft_frame().objects) {
            if (QString::fromStdString(so.id) == id) {
                return true;
            }
        }
        return false;
    };
    REQUIRE(in_draft());

    edit->theme()->refreshDraft(id);
    CHECK_FALSE(in_draft());
    REQUIRE(objects->setSelectedProperty(QStringLiteral("font_size"), 50).isEmpty());
    CHECK_FALSE(in_draft());

    // Typing done: the raster paints it again.
    edit->theme()->refreshDraft({});
    CHECK(in_draft());
}
