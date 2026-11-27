#pragma once

// Opening outputs in tests the way the output dialog does.

#include "destination_rows.hpp"
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/render/output_window_host.hpp>

#include <QString>
#include <QVariant>

namespace seventhslide::test {

/// Key of the first display ``host`` can see.
inline QString first_display_key(const render::OutputWindowHost& host)
{
    const auto displays = seventhslide::app::display_rows(host);
    REQUIRE_FALSE(displays.isEmpty());
    return displays.front().toMap().value(QStringLiteral("key")).toString();
}

/// Show ``output`` on the first display: add a display destination, point it
/// at that screen and enable it. Returns the destination id.
inline QString open_on_first_display(render::OutputWindowHost& host, const QString& output)
{
    const QString id =
        host.add_destination(output, render::destination_kind_name(render::DestinationKind::Display));
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(host.set_destination_screen(id, first_display_key(host)));
    REQUIRE(host.set_destination_enabled(id, true));
    REQUIRE(host.is_open(output));
    return id;
}

}  // namespace seventhslide::test
