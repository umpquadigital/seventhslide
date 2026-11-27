// Seam between the glyph table (glyphs.cpp, ``Icons`` in QML) and
// IconImageProvider: every kind still rasterises, and an Image is Ready in the
// frame its source is assigned (icon_image_provider.hpp says why).

#include <catch2/catch_test_macros.hpp>

#include "glyphs.hpp"
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/stack.hpp>
#include "icon_image_provider.hpp"
#include "test_qml.hpp"
#include "test_qt_app.hpp"

#include <QColor>
#include <QSet>
#include <QImage>
#include <QQmlEngine>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

using seventhslide::app::IconImageProvider;
namespace glyphs = seventhslide::app::glyphs;

namespace {

/// Icons.url() as QML calls it: the module's singleton, through a real engine.
class IconUrls {
public:
    IconUrls()
        : object_(scene_.create("import QtQuick\n"
                                "import SeventhSlide\n"
                                "QtObject {\n"
                                "    function url(kind, ink) { return Icons.url(kind, ink) }\n"
                                "    function styled(kind) { return Icons.url(kind, Style.textSoft, 2) }\n"
                                "}\n"))
    {
        REQUIRE(object_ != nullptr);
    }

    QString url(const QString& kind, const QString& ink) const
    {
        QVariant out;
        const bool ok = QMetaObject::invokeMethod(
            object_, "url", Q_RETURN_ARG(QVariant, out),
            Q_ARG(QVariant, kind), Q_ARG(QVariant, ink));
        REQUIRE(ok);
        return out.toString();
    }

    QString styled(const QString& kind) const
    {
        QVariant out;
        REQUIRE(QMetaObject::invokeMethod(object_, "styled", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, kind)));
        return out.toString();
    }

    seventhslide::test::QmlScene& scene() { return scene_; }

private:
    seventhslide::test::QmlScene scene_;
    QObject* object_;
};

bool has_ink(const QImage& img)
{
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) != 0) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("icon urls are provider-backed, not data: URLs", "[icons]")
{
    seventhslide::test::ensure_qt_application();
    IconUrls urls;

    const QString gear = urls.url(QStringLiteral("settings"), QStringLiteral("#888888"));
    CHECK(gear.startsWith(QStringLiteral("image://ssicon/")));
    CHECK_FALSE(gear.contains(QStringLiteral("data:")));

    // Ink is still part of the id, so the cache keys one entry per colour.
    const QString hovered = urls.url(QStringLiteral("settings"), QStringLiteral("#dddddd"));
    CHECK(hovered != gear);

    // A Style colour is written as SVG reads it; the stroke width as given.
    const QString soft = glyphs::svg("settings", QStringLiteral("#a8a8a8"), 2);
    CHECK(urls.styled(QStringLiteral("settings")) == seventhslide::app::svg_icon_url(soft));
    CHECK(soft.contains(QStringLiteral("stroke-width=\"2\"")));
    // A see-through colour keeps its alpha.
    CHECK(seventhslide::app::Icons::ink_name(QVariant::fromValue(QColor(255, 0, 0, 128)))
          == QStringLiteral("#80ff0000"));
    // An unknown kind draws nothing rather than failing.
    CHECK_FALSE(glyphs::svg("no-such-glyph", {}).contains(QStringLiteral("<path")));
}

TEST_CASE("every glyph rasterises", "[icons]")
{
    seventhslide::test::ensure_qt_application();
    IconUrls urls;
    IconImageProvider provider;

    QStringList kinds;
    for (const auto& glyph : glyphs::all()) {
        kinds.push_back(QString::fromUtf8(glyph.name));
    }
    REQUIRE(kinds.size() > 20);
    // Each name once.
    CHECK(QSet<QString>(kinds.begin(), kinds.end()).size() == kinds.size());

    for (const QString& kind : kinds) {
        INFO("glyph: " << kind.toStdString());
        const QString url = urls.url(kind, QStringLiteral("#888888"));
        REQUIRE(url.startsWith(QStringLiteral("image://ssicon/")));

        const QString id = url.mid(QStringLiteral("image://ssicon/").size());
        QSize size;
        const QImage img = provider.requestImage(id, &size, QSize(13, 13));

        CHECK(size == QSize(13, 13));
        CHECK(img.size() == QSize(13, 13));
        // A glyph that decoded wrong (or not at all) comes back fully transparent.
        CHECK(has_ink(img));
    }
}

TEST_CASE("icon provider falls back cleanly on a bad id", "[icons]")
{
    seventhslide::test::ensure_qt_application();
    IconImageProvider provider;

    QSize size;
    const QImage img = provider.requestImage(QStringLiteral("not-base64-svg"), &size, QSize(13, 13));
    CHECK(img.size() == QSize(13, 13));
    CHECK_FALSE(has_ink(img));

    // No sourceSize: render at the 24x24 design size rather than 0x0.
    QSize unsized;
    const QImage dflt = provider.requestImage(QString(), &unsized, QSize(-1, -1));
    CHECK(dflt.size() == QSize(24, 24));
}

TEST_CASE("icon images load synchronously", "[icons]")
{
    seventhslide::test::ensure_qt_application();
    IconUrls urls;

    QObject* img = urls.scene().create(
        "import QtQuick\n"
        "Image { sourceSize: Qt.size(13, 13) }\n");

    // The whole point of the provider: Ready before control returns, so the old
    // glyph is never dropped for a frame while a recoloured copy is fetched.
    img->setProperty("source", urls.url(QStringLiteral("settings"), QStringLiteral("#888888")));
    CHECK(img->property("status").toInt() == 1 /* Image.Ready */);

    img->setProperty("source", urls.url(QStringLiteral("settings"), QStringLiteral("#dddddd")));
    CHECK(img->property("status").toInt() == 1 /* Image.Ready */);
}

TEST_CASE("Every stack and output icon has a glyph; the remote gets them inline", "[icons]")
{
    seventhslide::test::ensure_qt_application();
    IconImageProvider provider;
    QStringList ids;
    for (const auto icon : seventhslide::model::k_stack_icons) {
        ids.push_back(QString::fromUtf8(icon));
    }
    for (const auto icon : seventhslide::model::k_output_icons) {
        ids.push_back(QString::fromUtf8(icon));
    }
    ids.push_back(QStringLiteral("sendTo"));
    for (const QString& id : ids) {
        INFO("icon: " << id.toStdString());
        REQUIRE(glyphs::find(id.toStdString()) != nullptr);
        const QString url = seventhslide::app::svg_icon_url(glyphs::svg(id.toStdString(), "#ffffff", 2));
        QSize size;
        CHECK(has_ink(provider.requestImage(url.mid(QStringLiteral("image://ssicon/").size()),
                                            &size, QSize(14, 14))));
    }

    // The web remote cannot load image:// sources, so it gets the same glyph inline.
    const QString remote = glyphs::data_url(glyphs::svg("video", {}, 2));
    CHECK(remote.startsWith(QStringLiteral("data:image/svg+xml")));
}
