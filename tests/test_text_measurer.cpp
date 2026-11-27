#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/layout/qt_font_faces.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/layout/text_measurer.hpp>

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using seventhslide::fonts::FontRegistry;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::layout::FontStyle;
using seventhslide::layout::QtTextMeasurer;
using seventhslide::layout::css_line_height_px;

TEST_CASE("css_line_height_px is ceil(size * factor)", "[layout][metrics]")
{
    REQUIRE(css_line_height_px(48.0, 1.5) == 72);
    REQUIRE(css_line_height_px(10.0) == 12);
    REQUIRE(css_line_height_px(48.0) == 58);  // ceil(57.6)
    REQUIRE(css_line_height_px(11.0) == 14);  // ceil(13.2)
    REQUIRE(css_line_height_px(0.0) == 1);
    REQUIRE(css_line_height_px(-5.0) == 1);
}

TEST_CASE("ApproxTextMeasurer uses half-em char width", "[layout][metrics]")
{
    ApproxTextMeasurer m;
    FontStyle style{.family = "Anything", .size_px = 40.0};
    REQUIRE(m.measure_width("", style) == 0.0);
    REQUIRE(m.measure_width("abcd", style) == Catch::Approx(4 * 20.0));
    REQUIRE(m.line_height_px(style) == 48);  // ceil(40 * 1.2)
    // UTF-8 multi-byte: one code point
    REQUIRE(m.measure_width("é", style) == Catch::Approx(20.0));
}

TEST_CASE("ApproxTextMeasurer adds letter-spacing between code points", "[layout][metrics]")
{
    ApproxTextMeasurer m;
    FontStyle style{.family = "Anything", .size_px = 40.0, .letter_spacing = 2.0};
    REQUIRE(m.measure_width("a", style) == Catch::Approx(20.0));
    REQUIRE(m.measure_width("abcd", style) == Catch::Approx(4 * 20.0 + 3 * 2.0));
}

TEST_CASE("QtTextMeasurer measures real fonts", "[layout][metrics]")
{
    seventhslide::test::ensure_qt_application();

    FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    QtTextMeasurer measurer(fonts);

    const std::string family = "Liberation Sans";

    SECTION("empty and monotonic widths")
    {
        FontStyle style{.family = family, .size_px = 48.0};
        REQUIRE(measurer.measure_width("", style) == 0.0);
        const double a = measurer.measure_width("A", style);
        const double aa = measurer.measure_width("AA", style);
        const double hello = measurer.measure_width("Hello world", style);
        REQUIRE(a > 0.0);
        REQUIRE(aa > a);
        REQUIRE(hello > aa);
        REQUIRE(measurer.line_height_px(style) == css_line_height_px(48.0));
    }

    SECTION("bold face is at least as wide as regular for typical glyphs")
    {
        FontStyle regular{.family = family, .size_px = 48.0, .bold = false};
        FontStyle bold{.family = family, .size_px = 48.0, .bold = true};
        const double w_reg = measurer.measure_width("Hamburg", regular);
        const double w_bold = measurer.measure_width("Hamburg", bold);
        REQUIRE(w_reg > 0.0);
        REQUIRE(w_bold > w_reg);
    }

    SECTION("a cached width is not reused at another letter spacing")
    {
        // The same text at a new spacing used to hit the width cached at the
        // old one, so lines were packed for the wrong width.
        FontStyle plain{.family = family, .size_px = 48.0};
        FontStyle tracked = plain;
        tracked.letter_spacing = 20.0;
        const double w_plain = measurer.measure_width("Amazing grace", plain);
        const double w_tracked = measurer.measure_width("Amazing grace", tracked);
        REQUIRE(w_tracked > w_plain + 100.0);
        REQUIRE(measurer.measure_width("Amazing grace", plain) == w_plain);
    }

    SECTION("missing family falls back to approx")
    {
        FontStyle style{
            .family = "DefinitelyNotARealFontFamily_SeventhSlide_XYZ",
            .size_px = 20.0};
        // No fonts at all, not even a stand-in.
        FontRegistry none;
        QtTextMeasurer with_none(none);

        const double w = with_none.measure_width("abcd", style);
        REQUIRE(w == Catch::Approx(4 * 10.0));
    }
}

TEST_CASE("QtTextMeasurer measures with a font file the index names", "[layout][metrics]")
{
    seventhslide::test::ensure_qt_application();

    FontRegistry fonts;
    seventhslide::fonts::FontIndex index;
    index["testdemo"] = {
        {.subfamily = "Regular",
         .path = seventhslide::test::fixture_font("SSFixtureSans-Regular.ttf")},
    };
    fonts.set_library_fonts(std::move(index));

    QtTextMeasurer measurer(fonts);
    FontStyle style{.family = "TestDemo", .size_px = 36.0};
    const double w = measurer.measure_width("Width", style);
    REQUIRE(w > 0.0);
    REQUIRE(w != Catch::Approx(5 * 18.0));  // not the approx path
}

TEST_CASE("QtFontFaces applies requested weight on the resolved family", "[layout][fonts]")
{
    seventhslide::test::ensure_qt_application();
    using seventhslide::test::k_fixture_plain;
    using seventhslide::test::k_fixture_sans;
    using seventhslide::test::k_fixture_semibold;

    FontRegistry fonts;
    seventhslide::test::use_fixture_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    const auto face = [&](std::string_view family, double size, bool bold) {
        return faces.resolve(
            FontStyle{.family = std::string(family), .size_px = size, .bold = bold});
    };
    const auto width = [](const QFont& font) {
        return QFontMetricsF(font).horizontalAdvance(QLatin1String("Hamburg"));
    };

    const auto regular = face(k_fixture_sans, 72.0, false);
    const auto bold = face(k_fixture_sans, 72.0, true);
    REQUIRE(regular.resolved);
    REQUIRE(bold.resolved);

    const QFontInfo regular_info(regular.font);
    const QFontInfo bold_info(bold.font);
    REQUIRE(regular_info.weight() < static_cast<int>(QFont::DemiBold));
    REQUIRE(bold_info.weight() >= static_cast<int>(QFont::DemiBold));
    REQUIRE(bold_info.weight() > regular_info.weight());

    SECTION("named weight families stay heavier than Regular")
    {
        const auto semi = face(k_fixture_semibold, 48.0, false);
        const auto plain = face(k_fixture_sans, 48.0, false);
        REQUIRE(semi.resolved);
        REQUIRE(plain.resolved);
        const double semi_w = width(semi.font);
        const double reg_w = width(plain.font);
        INFO("semi_w=" << semi_w << " reg_w=" << reg_w
                       << " semi_weight=" << QFontInfo(semi.font).weight()
                       << " reg_weight=" << QFontInfo(plain.font).weight());
        REQUIRE(semi_w > reg_w * 1.02);
    }

    SECTION("a family with a Bold file uses it, not synthesis")
    {
        const auto large_bold = face(k_fixture_sans, 110.0, true);
        REQUIRE(large_bold.resolved);
        REQUIRE(large_bold.face_is_bold);
        REQUIRE(QFontInfo(large_bold.font).weight() >= static_cast<int>(QFont::DemiBold));
        REQUIRE_FALSE(large_bold.needs_synthetic_embolden());
    }

    SECTION("Regular-only families still thicken bold in place")
    {
        const auto plain_reg = face(k_fixture_plain, 110.0, false);
        const auto plain_bold = face(k_fixture_plain, 110.0, true);
        REQUIRE(plain_reg.resolved);
        REQUIRE(plain_bold.resolved);
        // No heavier face exists anywhere, so bold is drawn by thickening the
        // Regular outline.
        REQUIRE(plain_bold.needs_synthetic_embolden());
        const double reg_w = width(plain_reg.font);
        const double bold_w = width(plain_bold.font) + plain_bold.synthetic_embolden_px;
        REQUIRE(bold_w > reg_w);
    }
}

TEST_CASE("QtFontFaces adds each font file to Qt once", "[layout][fonts]")
{
    seventhslide::test::ensure_qt_application();
    // How many times Qt holds a file that names ``family``: the ids Qt hands
    // out are its places in a list, so every one is looked at.
    const auto added = [](std::string_view family) {
        const QString name = QString::fromUtf8(family);
        int n = 0;
        for (int id = 0; id < 4096; ++id) {
            n += QFontDatabase::applicationFontFamilies(id).contains(name) ? 1 : 0;
        }
        return n;
    };
    const auto plain = seventhslide::test::k_fixture_plain;
    const int before = added(plain);

    FontRegistry fonts;
    seventhslide::test::use_fixture_fonts(fonts);
    fonts.set_stand_in_family(std::string(plain));
    seventhslide::layout::QtFontFaces faces(fonts);
    // Its one Regular answers for all four styles, and for the families shown
    // in the stand-in.
    CHECK(faces.painted_family(plain) == std::string(plain));
    CHECK(faces.painted_family("No Such Family") == std::string(plain));
    CHECK(faces.painted_family("Nor This One") == std::string(plain));
    CHECK(faces.resolve({.family = std::string(plain), .size_px = 30.0, .bold = true}).resolved);
    CHECK(added(plain) == before + 1);

    faces.clear_cache();
    CHECK(added(plain) == before);
}

TEST_CASE("Text never takes a face of the family's name from the system", "[layout][fonts]")
{
    seventhslide::test::ensure_qt_application();
    using seventhslide::test::fixture_font;
    const auto shipped = std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR}.parent_path()
                         / "third_party" / "fonts" / "Open Sans";
    // The library has only Open Sans Regular. The system, as far as Qt can tell, has
    // the rest of the family, and a family the library has nothing of.
    const auto as_if_installed = [](const std::filesystem::path& file) {
        const int id = QFontDatabase::addApplicationFont(QString::fromStdString(file.string()));
        REQUIRE(id >= 0);
        return id;
    };
    const std::vector<int> installed{
        as_if_installed(shipped / "OpenSans-Bold.ttf"),
        as_if_installed(shipped / "OpenSans-Italic.ttf"),
        as_if_installed(shipped / "OpenSans-BoldItalic.ttf"),
        as_if_installed(fixture_font("SSFixturePlain-Regular.ttf")),
    };
    const auto system_styles = QFontDatabase::styles(QStringLiteral("Open Sans"));
    REQUIRE(system_styles.contains(QStringLiteral("Bold")));
    REQUIRE(system_styles.contains(QStringLiteral("Italic")));

    FontRegistry fonts;
    seventhslide::fonts::FontIndex library;
    library["open sans"] = {{.subfamily = "Regular", .path = shipped / "OpenSans-Regular.ttf"}};
    fonts.set_library_fonts(library);
    fonts.set_stand_in_family("Open Sans");
    seventhslide::layout::QtFontFaces faces(fonts);
    const auto width = [](const QFont& font) {
        return QFontMetricsF(font).horizontalAdvance(QStringLiteral("Hamburgefonstiv"));
    };
    const auto ink = [](const QFont& font) {
        QImage image(400, 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        QPainter painter(&image);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QPointF(20, 80), QStringLiteral("Hillll"));
        painter.end();
        return image;
    };

    const std::string family = "Open Sans";
    const auto regular = faces.resolve({.family = family, .size_px = 40.0});

    const auto bold = faces.resolve({.family = family, .size_px = 40.0, .bold = true});
    CHECK(bold.resolved);
    CHECK(bold.needs_synthetic_embolden());
    CHECK_FALSE(bold.face_is_bold);
    CHECK(width(bold.font) == Catch::Approx(width(regular.font)));

    // Slanted from the file it has: Open Sans's own Italic has other widths.
    const auto italic = faces.resolve({.family = family, .size_px = 40.0, .italic = true});
    CHECK(italic.resolved);
    INFO("italic " << width(italic.font) << " regular " << width(regular.font));
    CHECK(width(italic.font) == Catch::Approx(width(regular.font)));
    CHECK(ink(italic.font) != ink(regular.font));

    const auto bold_italic =
        faces.resolve({.family = family, .size_px = 40.0, .bold = true, .italic = true});
    CHECK(width(bold_italic.font) == Catch::Approx(width(regular.font)));

    // A family only the system has is shown in the stand-in, by name.
    const auto other = faces.resolve(
        {.family = std::string(seventhslide::test::k_fixture_plain), .size_px = 40.0});
    CHECK(other.resolved);
    CHECK(other.font.family() == QStringLiteral("Open Sans"));

    faces.clear_cache();
    for (const int id : installed) {
        QFontDatabase::removeApplicationFont(id);
    }
}
