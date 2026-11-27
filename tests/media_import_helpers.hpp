#pragma once

// Wait for a media import to report.
//
// MediaController::import_files copies off the GUI thread and
// report through importFinished from the event loop, so a test runs the loop
// until the request it started is over.

#include "media_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QTest>
#include <QVariantMap>

namespace seventhslide::test {

inline QVariantMap wait_for_import(app::MediaController& media, qint64 request_id)
{
    QVariantMap result;
    bool done = false;
    const auto connection = QObject::connect(
        &media, &app::MediaController::importFinished,
        [&](qint64 id, const QVariantMap& r) {
            if (id == request_id) {
                result = r;
                done = true;
            }
        });
    (void)QTest::qWaitFor([&] { return done; }, 30000);
    QObject::disconnect(connection);
    REQUIRE(done);
    return result;
}

}  // namespace seventhslide::test
