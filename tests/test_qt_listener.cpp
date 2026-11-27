// Deletes the objects a test case queued with deleteLater when that test case
// ends.
//
// The test application lives for the whole process and is never destroyed, so
// a delete still queued at the end of a test case would otherwise run during a
// later test case, or from the exit handlers once Qt's own globals are torn
// down (where QML popups crash). Delivering it here means it runs at the same
// point whether ctest runs the test case alone or the whole suite runs in one
// process, and any problem it causes shows up in the test case that queued it.

#include "test_qt_app.hpp"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

class DeferredDeleteFlush : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseEnded(const Catch::TestCaseStats& /*stats*/) override
    {
        if (QCoreApplication::instance() != nullptr) {
            seventhslide::test::flush_deferred_deletes();
        }
    }
};

}  // namespace

CATCH_REGISTER_LISTENER(DeferredDeleteFlush)
