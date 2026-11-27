#pragma once

// The size an image request asks for, or a default when it asks for none.

#include <QSize>

namespace seventhslide::app {

/// ``requested`` when it has an area, else ``fallback``. An image provider is
/// asked for an empty or invalid size when QML sets no ``sourceSize``.
[[nodiscard]] inline QSize size_or(const QSize& requested, const QSize& fallback)
{
    return requested.isEmpty() ? fallback : requested;
}

}  // namespace seventhslide::app
