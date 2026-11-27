#pragma once

// Shared Qt application bootstrap for Catch2 tests.
// Prefer QGuiApplication so layout font metrics work in-process
// alongside Qt Sql tests. Idempotent across translation units.

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>

namespace seventhslide::test {

/// The app's interface font and controls' style, in a program with the app
/// (qt_app_look.cpp); nothing in one without it (qt_plain_look.cpp).
void use_app_look();

inline void ensure_qt_application()
{
    if (QGuiApplication::instance() != nullptr) {
        return;
    }
    // Headless CI / no DISPLAY: offscreen platform plugin. A run of the real
    // web-page renderer's test (test_webengine.cpp) needs the GPU, so the
    // display's own platform.
    if (!qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // Settings the UI saves (the media view mode) go to a directory that
    // lives as long as this process, so no run reads what an earlier one
    // left behind and none writes to the user's own config. As INI files on
    // every system: the native format is the registry on Windows and
    // macOS's preferences, which no path redirects.
    static QTemporaryDir settings_dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    static int argc = 1;
    static char arg0[] = "seventhslide_tests";
    static char* argv[] = {arg0, nullptr};
    new QGuiApplication(argc, argv);
    use_app_look();
}

/// The first member of a fixture whose other members need the application
/// (a SessionFacade measures text as it is built): members are constructed
/// before the fixture's own constructor runs, so calling
/// ``ensure_qt_application`` there is too late when the test runs alone.
struct QtBootstrap {
    QtBootstrap() { ensure_qt_application(); }
};

/// Deliver every queued deleteLater now. The tests seldom run an event loop,
/// so without this a queued delete waits for whatever runs one next.
inline void flush_deferred_deletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

}  // namespace seventhslide::test
