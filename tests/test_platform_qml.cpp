// Platform (QML): what the interface writes the system's own way.

#include "platform_qml.hpp"
#include "test_qt_app.hpp"

#include <catch2/catch_test_macros.hpp>

using seventhslide::app::PlatformQml;

TEST_CASE("Platform writes a shortcut as the system does", "[app][platform]")
{
    seventhslide::test::ensure_qt_application();
    const PlatformQml platform;
#if defined(Q_OS_MACOS)
    // Ctrl is Command there, shown as its sign.
    CHECK(platform.shortcutText(QStringLiteral("Ctrl+Shift+G")).contains(QChar(0x2318)));
#else
    CHECK(platform.shortcutText(QStringLiteral("Ctrl+Shift+G")) == QStringLiteral("Ctrl+Shift+G"));
    CHECK(platform.shortcutText(QStringLiteral("Ctrl+]")) == QStringLiteral("Ctrl+]"));
#endif
}

TEST_CASE("Platform names a fixed-width font and the file manager", "[app][platform]")
{
    seventhslide::test::ensure_qt_application();
    const PlatformQml platform;
    CHECK_FALSE(platform.monoFamily().isEmpty());
#if defined(Q_OS_MACOS)
    CHECK(platform.showFolderText() == QStringLiteral("Show in Finder"));
#elif defined(Q_OS_WIN)
    CHECK(platform.showFolderText() == QStringLiteral("Show in Explorer"));
#else
    CHECK(platform.showFolderText() == QStringLiteral("Show Folder"));
#endif
}
