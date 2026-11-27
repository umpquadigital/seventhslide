// The Convert dialog's menus: what it offers, from ConvertText, for what
// the probe read.

#include <catch2/catch_test_macros.hpp>

#include "test_qml.hpp"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace {

QObject* convert_dialog(seventhslide::test::QmlScene& scene)
{
    auto* window = qobject_cast<QQuickWindow*>(scene.create(R"QML(
import QtQuick
import SeventhSlide as Ui

Window {
    width: 600; height: 600
    property alias dialog: dialog
    Ui.ConvertDialog { id: dialog }
}
)QML",
        QUrl::fromLocalFile(QStringLiteral(SEVENTHSLIDE_UI_QML_DIR "/"))));
    REQUIRE(window != nullptr);
    QCoreApplication::processEvents();
    auto* dialog = window->property("dialog").value<QObject*>();
    REQUIRE(dialog != nullptr);
    return dialog;
}

QVariantList options(QObject* dialog, const char* function)
{
    QVariant out;
    REQUIRE(QMetaObject::invokeMethod(dialog, function, Q_RETURN_ARG(QVariant, out)));
    return out.toList();
}

QStringList strings(const QVariantList& options, const QString& key)
{
    QStringList out;
    for (const QVariant& option : options) {
        out << option.toMap().value(key).toString();
    }
    return out;
}

QVariantMap one_video()
{
    return {{QStringLiteral("title"), QStringLiteral("Sunrise")},
            {QStringLiteral("filename"), QStringLiteral("sunrise.mov")}};
}

} // namespace

TEST_CASE("Convert offers a video sound formats only when there is sound", "[ui][convert]")
{
    seventhslide::test::QmlScene scene;
    QObject* dialog = convert_dialog(scene);
    const QString value = QStringLiteral("value");
    const QStringList video{"mp4", "webm", "mov", "mkv"};
    const QStringList audio{"mp3", "m4a", "wav", "flac", "ogg"};

    dialog->setProperty("kind", QStringLiteral("video"));
    dialog->setProperty("sources", QVariantList{one_video()});
    dialog->setProperty("probe", QVariantMap{{QStringLiteral("hasVideo"), true},
                                             {QStringLiteral("hasAudio"), false}});
    CHECK(strings(options(dialog, "formatOptions"), value) == video);

    // Until the probe says, and with sound, it can become sound alone.
    dialog->setProperty("probing", true);
    CHECK(strings(options(dialog, "formatOptions"), value) == video + audio);
    dialog->setProperty("probing", false);
    dialog->setProperty("probe", QVariantMap{{QStringLiteral("hasVideo"), true},
                                             {QStringLiteral("hasAudio"), true}});
    CHECK(strings(options(dialog, "formatOptions"), value) == video + audio);

    // Several files: the first one's probe does not speak for the rest.
    dialog->setProperty("probe", QVariantMap{{QStringLiteral("hasAudio"), false}});
    dialog->setProperty("sources", QVariantList{one_video(), one_video()});
    CHECK(strings(options(dialog, "formatOptions"), value) == video + audio);

    dialog->setProperty("kind", QStringLiteral("audio"));
    CHECK(strings(options(dialog, "formatOptions"), value) == audio);
    dialog->setProperty("kind", QStringLiteral("image"));
    CHECK(strings(options(dialog, "formatOptions"), value) == QStringList{"jpg", "png", "webp"});
    CHECK(strings(options(dialog, "formatOptions"), QStringLiteral("label"))
          == QStringList{"JPEG", "PNG", "WebP"});
}

TEST_CASE("Convert offers Keep at the source's frame rate, then each rate", "[ui][convert]")
{
    seventhslide::test::QmlScene scene;
    QObject* dialog = convert_dialog(scene);
    dialog->setProperty("kind", QStringLiteral("video"));
    dialog->setProperty("sources", QVariantList{one_video()});
    dialog->setProperty("probe", QVariantMap{{QStringLiteral("fps"), 29.97002997}});

    const QVariantList fps = options(dialog, "fpsOptions");
    CHECK(strings(fps, QStringLiteral("value")) == QStringList{"0", "24", "25", "30", "50", "60"});
    CHECK(strings(fps, QStringLiteral("label"))
          == QStringList{"Keep (29.97 fps)", "24 fps", "25 fps", "30 fps", "50 fps", "60 fps"});

    // Several files have no one rate to keep.
    dialog->setProperty("sources", QVariantList{one_video(), one_video()});
    CHECK(options(dialog, "fpsOptions").front().toMap().value(QStringLiteral("label"))
          == QStringLiteral("Keep"));
}
