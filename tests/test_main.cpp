// The test binary's main: Catch2's, then the Qt application goes.
//
// ensure_qt_application() creates the application on first use and keeps it
// for the whole run. Left to the end of the process it outlives the
// QThreadStorage slots Qt destroys with the other statics, and the main
// thread's storage, cleaned up after them, warns once per slot. Deleting it
// here, as the app's main() does by leaving scope, closes it while everything
// it uses still exists.
//
// The tests read in one locale, US English, whatever the computer's: dates,
// times and the order of names are the locale's, and CI's runners have none
// (C.UTF-8). A test of another locale sets it, and puts this one back.

#include <catch2/catch_session.hpp>

#ifdef SEVENTHSLIDE_HAS_WEBENGINE
#include "webengine_renderer.hpp"
#endif

#include <QCoreApplication>
#include <QLocale>

int main(int argc, char* argv[])
{
#ifdef SEVENTHSLIDE_HAS_WEBENGINE
    // Qt WebEngine sets up before the application, as in the app's main().
    if (qEnvironmentVariableIsSet("SEVENTHSLIDE_TEST_WEBENGINE")) {
        seventhslide::app::initialize_web_engine();
    }
#endif
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    const int result = Catch::Session().run(argc, argv);
    delete QCoreApplication::instance();
    return result;
}
