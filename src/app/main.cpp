#include "gui_thread.hpp"
#include "image_providers.hpp"
#include "library_registry_controller.hpp"
#include "pointer_confinement.hpp"
#include "profile_controller.hpp"
#include "remote_controller.hpp"
#include "sync_controller.hpp"
#include "session_facade.hpp"
#include "ui_font.hpp"
#ifdef SEVENTHSLIDE_HAS_WEBENGINE
#include "webengine_renderer.hpp"
#endif
#if defined(__APPLE__)
#include "mac_quit.hpp"
#endif

#include <seventhslide/library/library.hpp>
#include <seventhslide/link/tls.hpp>
#include <seventhslide/platform/paths.hpp>
#include <seventhslide/render/device_catalog.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/server/command_line.hpp>
#include <seventhslide/server/run.hpp>
#include <seventhslide/util/atomic_file.hpp>
#include <seventhslide/util/open_files.hpp>
#include <seventhslide/version.hpp>

#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QSettings>
#include <QtQml/QQmlExtensionPlugin>

#include <chrono>
#include <clocale>
#include <filesystem>
#include <iostream>
#include <memory>

// Static QML module plugin (src/ui SeventhSlide).
Q_IMPORT_QML_PLUGIN(SeventhSlidePlugin)
Q_IMPORT_QML_PLUGIN(SeventhSlide_NativePlugin)

int main(int argc, char* argv[])
{
#if defined(__APPLE__)
    // First, so it goes last: a log out or restart that asked the app to quit
    // hears it has once everything here is closed.
    const seventhslide::app::MacQuitAnswer answer_quit;
#endif
    // Before any library or socket opens, the server's included: macOS starts
    // an app from Finder with room for only 256 open files.
    seventhslide::util::raise_open_file_limit();

    // The server first, before anything of the interface is set up: no
    // QGuiApplication, no window, no video (docs/SERVER.md).
    if (seventhslide::server::asks_for_server(argc, argv)) {
        return seventhslide::server::run(argc, argv);
    }

    // libmpv requires the C numeric locale (decimal point = '.'). Fedora/GNOME
    // defaults (en_US.UTF-8) make mpv refuse to initialize and video load fails.
    std::setlocale(LC_NUMERIC, "C");

#ifdef SEVENTHSLIDE_HAS_WEBENGINE
    // Web pages on the outputs: Qt WebEngine sets up before the application.
    seventhslide::app::initialize_web_engine();
#endif
    // SEVENTHSLIDE_SETTINGS_DIR: this computer's settings (the panel layout,
    // the media views) as INI files there, as SEVENTHSLIDE_DATA_DIR moves
    // the libraries; the browser tests give each app its own. The native
    // format would be the registry on Windows and macOS's preferences,
    // which no folder redirects.
    if (const QString dir = qEnvironmentVariable("SEVENTHSLIDE_SETTINGS_DIR"); !dir.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    }
    QGuiApplication app(argc, argv);
    // Qt may reset locales during construction - force C numeric again for mpv.
    std::setlocale(LC_NUMERIC, "C");

    // gtk3/GNOME platform theme stamps Cantarell/Adwaita onto the app font
    // during QGuiApplication construction. Pin chrome to a compact stack so
    // XFCE, GNOME, Windows, and macOS share the same metrics.
    const QFont ui_font = seventhslide::app::operator_ui_font();
    QGuiApplication::setFont(ui_font);
    seventhslide::app::use_operator_ui_style();
    // Before anything uses TLS, a yt-dlp update's HTTPS included: sync takes
    // only Qt's OpenSSL backend, and Qt keeps to the first one used.
    if (!seventhslide::link::use_openssl_tls()) {
        qWarning("Qt has no OpenSSL TLS backend: sync cannot connect to other computers");
    }

    QGuiApplication::setApplicationName(QStringLiteral("SeventhSlide"));
    QGuiApplication::setOrganizationName(QStringLiteral("SeventhSlide"));
    QGuiApplication::setApplicationVersion(
        QString::fromUtf8(seventhslide::version_string()));
    // Match packaging/seventhslide.desktop so Wayland/X11 pick up the launcher icon.
    QGuiApplication::setDesktopFileName(QStringLiteral("seventhslide"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/seventhslide-icon.png")));

    std::cout << "SeventhSlide " << seventhslide::version_string()
              << " (ui font=" << QFontInfo(ui_font).family().toStdString() << ")\n"
              << std::flush;

    // The app's long-lived objects live on this stack, after ``app`` and before
    // ``engine``, so they are destroyed in reverse: the engine (and the QML
    // holding them) first, then the remote, the session and the output host,
    // all while the application object still exists. The database connection
    // needs it to close; parenting them to ``app`` destroyed them after it, and
    // the session before the remote that refers to it.

    // Audience output windows. SessionFacade presents SceneFrames here.
    seventhslide::render::OutputWindowHost output_host;
    // The destinations wait for the operator to turn Live on, all but those
    // that ignore it.
    output_host.set_live(false);
    const seventhslide::platform::Paths paths;
    if (!paths.ensure_data_dir()) {
        std::cerr << "Warning: could not create data directory\n";
    }
    // The files this computer keeps (the library list, destinations, keys)
    // are at the top of the data folder: the temps a crash left there go.
    // The folders under it, which hold the libraries and their media, are
    // not looked through.
    seventhslide::util::remove_old_temps(paths.data_dir(), std::chrono::hours(1),
                                         seventhslide::util::Sweep::Folder);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &output_host, [&output_host]() {
        output_host.shutdown();
    });

    // Operator session facade.
    // The library registry owns the database + media roots (see seventhslide::library);
    // the facade re-points both whenever the operator switches library.
    seventhslide::app::SessionFacade session;
    session.set_output_host(&output_host);
#ifdef SEVENTHSLIDE_HAS_WEBENGINE
    // What pages store (sign-ins, cookies) is kept with the app's data.
    session.set_web_renderer_factory(seventhslide::app::webengine_renderer_factory(
        QString::fromStdString((paths.data_dir() / "web").string())));
#endif
    // A web page worked from the Controller keeps the pointer as the
    // windowing system holds it.
    session.set_pointer_confinement(seventhslide::app::make_native_pointer_confinement());
    session.load_libraries(paths.data_dir());

    // Sync: every synced library kept in step with the other computers,
    // open or not. A computer whose key cannot be read still runs, unsynced.
    // It starts before the first library opens, so a library this computer
    // only views opens read-only, before anything is written to it.
    std::unique_ptr<seventhslide::app::SyncController> sync_controller;
    try {
        sync_controller = std::make_unique<seventhslide::app::SyncController>(
            session, seventhslide::app::SyncController::Options{paths.data_dir()});
    } catch (const std::exception& ex) {
        std::cerr << "Sync is off: " << ex.what() << '\n';
    }

    // A library that will not open is reported, never papered over: the shell
    // shows databaseError and the session stays not ready, with no library to
    // read or write. An in-memory stand-in would look normal and silently lose
    // everything entered that session.
    if (!session.open_active_library()) {
        const auto root = session.libraries()->active_library_path().toStdString();
        if (root.empty()) {
            // No library to open: the library list was refused.
            std::cerr << "No library opened: " << session.databaseError().toStdString() << '\n';
        } else {
            std::cerr << "Failed to open library database at "
                      << seventhslide::library::database_file(std::filesystem::path{root}) << ": "
                      << session.databaseError().toStdString() << '\n';
        }
    }
    // The active style profile mirrors the live assignment tables - flush on exit.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &session, [&session]() {
        session.profiles()->capture_active_profile();
    });
    // Release image-provider workers blocked on the GUI thread before the
    // engine joins them on teardown.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [] { seventhslide::app::g_gui_calls_closed = true; });

    // Web remote. Owns its own lifecycle from app settings; when
    // the remote is disabled this binds nothing and listens on no port.
    seventhslide::app::RemoteController remote_controller(session);

    QQmlApplicationEngine engine;
    seventhslide::app::add_image_providers(engine, session);
    // Main.qml's required properties: the operator window gets its inputs
    // typed, not as names looked up at run time.
    engine.setInitialProperties({
        {QStringLiteral("session"), QVariant::fromValue(&session)},
        {QStringLiteral("remote"), QVariant::fromValue(&remote_controller)},
        {QStringLiteral("sync"), QVariant::fromValue(sync_controller.get())},
    });

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // Surface the actual QML errors. Without this a bad property assignment
    // fails the whole load while printing nothing but "Failed to load module",
    // which is close to undebuggable.
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError>& errors) {
                         for (const QQmlError& e : errors) {
                             std::cerr << "QML: " << e.toString().toStdString() << '\n';
                         }
                     });

    engine.loadFromModule("SeventhSlide", "Main");

    if (engine.rootObjects().isEmpty()) {
        std::cerr << "Failed to load QML module SeventhSlide/Main\n";
        return 1;
    }

    // Audience windows share the process with the operator QML window. Opening
    // them before the engine exists, or before the event loop is running, left
    // those surfaces black while in-app previews (which paint inside the QML
    // window) looked fine. Wait until exec() has started.
    QTimer::singleShot(0, &session, [&session]() { session.set_audience_outputs_enabled(true); });
#if defined(__APPLE__)
    QTimer::singleShot(0, &app, [] { seventhslide::app::handle_mac_quit_events(); });
#endif
    // Sound to this computer's speakers. Tests never make this call, so they
    // run the mixer silently on its timer.
    session.start_listen_device();

    // Audience windows are independent QQuickWindows. Closing the operator UI
    // would otherwise leave them open and keep the process alive
    // (quitOnLastWindowClosed waits for every top-level window).
    for (QObject* obj : engine.rootObjects()) {
        auto* win = qobject_cast<QQuickWindow*>(obj);
        if (!win) {
            continue;
        }
        // Outputs go to a screen it is not on first, and one on its screen
        // stays an ordinary window (macOS), so the operator is never shut out.
        seventhslide::render::DeviceCatalog::set_operator_window(win);
        QObject::connect(win, &QWindow::screenChanged, &output_host,
                         [&output_host] { output_host.restack(); });
        QObject::connect(win, &QQuickWindow::closing, &output_host, [&output_host]() {
            output_host.shutdown();
            QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit,
                                      Qt::QueuedConnection);
        });
    }

    return app.exec();
}
