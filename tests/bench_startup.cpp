// What the operator window costs to start. Hidden from the default run, and
// meant to run alone, one process a run, so the first window is the app's:
//   ./seventhslide_tests_dev "[.startup]"
// Prints the time to the first frame, the objects the window holds, and how
// much the process grew; asserts nothing about them.

#include <catch2/catch_test_macros.hpp>

#include "operator_ui.hpp"
#include "sample_library.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/render/output_window_host.hpp>

#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QQuickWindow>
#include <QTest>

#include <iostream>

namespace {

/// The process's resident memory in KiB, or -1 where /proc is not there.
long resident_kib()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly)) {
        return -1;
    }
    for (const QByteArray& line : status.readAll().split('\n')) {
        if (line.startsWith("VmRSS:")) {
            return line.mid(6).trimmed().split(' ').front().toLong();
        }
    }
    return -1;
}

}  // namespace

TEST_CASE("bench: operator window start", "[.startup]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::render::OutputWindowHost host;
    host.set_live(false);
    seventhslide::app::SessionFacade facade;
    facade.set_output_host(&host);
    const seventhslide::test::TempDir dir;
    REQUIRE(facade.open_database(dir.path / "library.db"));
    (void)seventhslide::test::fill_sample_library(facade);

    const long before = resident_kib();
    QElapsedTimer timer;
    timer.start();
    seventhslide::test::OperatorUi ui(facade);
    REQUIRE(ui.window() != nullptr);
    const qint64 created = timer.elapsed();
    bool drawn = false;
    QObject::connect(ui.window(), &QQuickWindow::frameSwapped, [&drawn] { drawn = true; });
    ui.window()->show();
    REQUIRE(QTest::qWaitFor([&drawn] { return drawn; }, 10000));
    const qint64 first_frame = timer.elapsed();
    const long after = resident_kib();
    const auto objects = ui.window()->findChildren<QObject*>().size();

    // What the first Settings costs, wherever it is made.
    timer.restart();
    QObject* settings = ui.find_component("AppSettingsDialog");
    REQUIRE(settings != nullptr);
    REQUIRE(QMetaObject::invokeMethod(settings, "openDialog"));
    drawn = false;
    REQUIRE(QTest::qWaitFor([&drawn] { return drawn; }, 10000));
    const qint64 settings_ms = timer.elapsed();

    std::cout << "startup: created " << created << " ms, first frame " << first_frame
              << " ms, objects " << objects << ", grew " << (after - before) / 1024
              << " MiB, first Settings " << settings_ms << " ms\n";
}
