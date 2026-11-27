cmake_minimum_required(VERSION 3.21)

# macOS 26 at least. libc++'s floating-point std::from_chars (used to read
# numbers that must not follow the locale) is only in macOS 26's system
# library, and Homebrew's libraries are built for the macOS they were built
# on. Before project(), where CMake reads it; ignored on other systems.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING
        "The oldest macOS SeventhSlide runs on (26.0 at least)" FORCE)
endif()

project(SeventhSlide
    VERSION 0.1.0
    DESCRIPTION "SeventhSlide C++/Qt application"
    LANGUAGES CXX
)

# A pre-release is the version above with a label: tag v1.3.0-beta.2 builds
# with SEVENTHSLIDE_VERSION_LABEL=beta.2, and the app calls itself
# 1.3.0-beta.2 (seventhslide::version_string). A release has none. The
# release workflow sets it from the tag (tools/release_version.py).
set(SEVENTHSLIDE_VERSION_LABEL "" CACHE STRING
    "A pre-release's label, after the dash in its tag (beta.2); empty for a release")
if(SEVENTHSLIDE_VERSION_LABEL STREQUAL "")
    set(SEVENTHSLIDE_VERSION "${PROJECT_VERSION}")
elseif(SEVENTHSLIDE_VERSION_LABEL MATCHES "^[0-9A-Za-z]+(\\.[0-9A-Za-z]+)*$")
    set(SEVENTHSLIDE_VERSION "${PROJECT_VERSION}-${SEVENTHSLIDE_VERSION_LABEL}")
else()
    message(FATAL_ERROR "SEVENTHSLIDE_VERSION_LABEL is letters, digits and dots "
        "(beta.2), not \"${SEVENTHSLIDE_VERSION_LABEL}\"")
endif()

# What the build is packaged as, for a release (cmake/SeventhSlidePackaging.cmake):
# deb (Ubuntu), rpm (Fedora, with Fedora's Qt), dmg (macOS) or exe (the
# Windows installer). Empty for a build that is not packaged.
set(SEVENTHSLIDE_PACKAGE "" CACHE STRING "What a release build is packaged as: deb, rpm, dmg, exe or empty")
set_property(CACHE SEVENTHSLIDE_PACKAGE PROPERTY STRINGS "" deb rpm dmg exe)
if(NOT SEVENTHSLIDE_PACKAGE MATCHES "^(|deb|rpm|dmg|exe)$")
    message(FATAL_ERROR "SEVENTHSLIDE_PACKAGE is deb, rpm, dmg, exe or empty, "
        "not \"${SEVENTHSLIDE_PACKAGE}\"")
endif()
# A deb is for one Ubuntu: the one it is built on, whose libraries it names.
# Ubuntu 24.04's Qt is 6.4, below the 6.8 floor, so its deb carries the Qt
# it was built with, in /opt/seventhslide (SEVENTHSLIDE_BUNDLE_QT); a later
# Ubuntu's is new enough, and its deb uses it, in /usr.
set(SEVENTHSLIDE_DEB_FOR "" CACHE STRING "The Ubuntu a deb is for, as its file names it (ubuntu-26.04)")
option(SEVENTHSLIDE_BUNDLE_QT "The deb carries its own Qt, in /opt/seventhslide" OFF)
if(SEVENTHSLIDE_PACKAGE STREQUAL "deb" AND NOT SEVENTHSLIDE_DEB_FOR MATCHES "^ubuntu-[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "A deb needs SEVENTHSLIDE_DEB_FOR, the Ubuntu it is for (ubuntu-26.04), "
        "not \"${SEVENTHSLIDE_DEB_FOR}\"")
endif()
if(SEVENTHSLIDE_BUNDLE_QT AND NOT SEVENTHSLIDE_PACKAGE STREQUAL "deb")
    message(FATAL_ERROR "SEVENTHSLIDE_BUNDLE_QT is for a deb (SEVENTHSLIDE_PACKAGE deb)")
endif()
# The package's file in a release, the same in every release: so
# .../releases/latest/download/<file> is always the newest, and the app knows
# which file of a release is its own. .github/workflows/release.yml and
# packaging/windows/seventhslide.iss name them too.
set(SEVENTHSLIDE_PACKAGE_FILE "")
if(SEVENTHSLIDE_PACKAGE STREQUAL "deb")
    set(SEVENTHSLIDE_PACKAGE_FILE "seventhslide-${SEVENTHSLIDE_DEB_FOR}-amd64.deb")
elseif(SEVENTHSLIDE_PACKAGE STREQUAL "rpm")
    set(SEVENTHSLIDE_PACKAGE_FILE "seventhslide-fedora-x86_64.rpm")
elseif(SEVENTHSLIDE_PACKAGE STREQUAL "dmg")
    set(SEVENTHSLIDE_PACKAGE_FILE "SeventhSlide-macOS-arm64.dmg")
elseif(SEVENTHSLIDE_PACKAGE STREQUAL "exe")
    set(SEVENTHSLIDE_PACKAGE_FILE "SeventhSlide-Windows-x64-Setup.exe")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Export compile commands for IDEs / clangd.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SEVENTHSLIDE_BUILD_TESTS "Build Catch2 unit tests" ON)
option(SEVENTHSLIDE_BUILD_APP "Build the Qt/QML application" ON)
option(SEVENTHSLIDE_BUILD_TOOLS "Build developer CLI tools (layout dump, render smoke, render song)" OFF)
# Developer-only: compile-time check that our hand-written NDI ABI shim matches
# the real SDK headers. Requires the NDI SDK; never needed for a normal build.
option(SEVENTHSLIDE_NDI_VERIFY_ABI "Verify the NDI ABI shim against the NDI SDK headers" OFF)

# Windows, for every target (fetched dependencies too): <windows.h> without
# its min/max macros, which break std::min/std::max, and without winsock 1,
# which clashes with winsock2.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
# MSVC reads a source file in the system's code page unless told otherwise,
# and the sources are UTF-8 (the interface's text has typographic quotes,
# ×, ›). Catch2 and nlohmann/json test files outgrow the object format's
# section limit. Only for C and C++: compile options also reach the resource
# compiler, and rc.exe refuses /utf-8. The CRT's "this function may be
# unsafe" deprecations (C4996: getenv, _wfopen) name its _s versions, which
# no other platform has.
if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8;/bigobj>")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SeventhSlideDependencies)
# Also enables the resource compiler on Windows.
include(SeventhSlideWindows)
if(APPLE)
    # What talks to AppKit is Objective-C++ (src/render/output_window_mac.mm).
    enable_language(OBJCXX)
endif()

# Compiler warnings for our own targets (not for generated QML cache code or
# third-party sources). Designated initializers that leave fields out are
# intentional, so missing-field-initializers stays off. They are given to
# C++ (and Objective-C++) only: a Windows program's resource script is a
# source too, and neither rc.exe nor windres takes them.
add_library(seventhslide_warnings INTERFACE)
set(_ss_warnings_cxx "$<COMPILE_LANGUAGE:CXX,OBJCXX>")
if(MSVC)
    target_compile_options(seventhslide_warnings INTERFACE "$<${_ss_warnings_cxx}:/W4>")
else()
    target_compile_options(seventhslide_warnings INTERFACE
        "$<${_ss_warnings_cxx}:-Wall;-Wextra;-Wshadow;-Wnon-virtual-dtor;-Woverloaded-virtual;-Wimplicit-fallthrough;-Wno-missing-field-initializers>")
endif()
# CI turns this on so a new warning fails the build instead of piling up.
option(SEVENTHSLIDE_WARNINGS_AS_ERRORS "Treat compiler warnings in project targets as errors" OFF)
if(SEVENTHSLIDE_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(seventhslide_warnings INTERFACE "$<${_ss_warnings_cxx}:/WX>")
    else()
        target_compile_options(seventhslide_warnings INTERFACE "$<${_ss_warnings_cxx}:-Werror>")
    endif()
endif()

# 6.8 is a hard floor: src/remote is built on QHttpHeaders and
# QHttpServerWebSocketUpgradeResponse, both introduced in Qt 6.8. Distro Qt is
# usually older (Ubuntu 24.04 ships 6.4), so point CMAKE_PREFIX_PATH at a
# newer Qt there - see docs/BUILDING.md.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickLayouts QuickDialogs2 Sql Network
                                          HttpServer WebSockets Svg)
# Qt's GuiPrivate holds QRhi, through which an output window keeps its
# picture's texture and web pages render off-screen, and the platform window
# the pointer is confined through. Qt's own installer defines it with Gui; a
# distribution's Qt (Fedora's qt6-qtbase-private-devel) only when it is asked
# for. Without it, the windows make a texture per picture, pages are not drawn
# and the pointer is not confined.
find_package(Qt6 6.8 QUIET COMPONENTS GuiPrivate)
# Qt WebEngine draws the Browser tab's pages on the outputs. It is large and
# optional: without it, pages are kept and go into services but show nothing.
option(SEVENTHSLIDE_WEBENGINE "Draw web pages with Qt WebEngine where it is installed" ON)
# A build that is for Qt WebEngine (CI's job that tests it) stops here without
# it, rather than building and testing nothing of it.
option(SEVENTHSLIDE_REQUIRE_WEBENGINE "Fail to configure without Qt WebEngine" OFF)
set(SEVENTHSLIDE_HAS_WEBENGINE OFF)
if(SEVENTHSLIDE_WEBENGINE)
    # Qt WebChannel carries a page's sound to the mixer.
    find_package(Qt6 6.8 QUIET COMPONENTS WebEngineQuick WebChannel)
    if(Qt6WebEngineQuick_FOUND AND Qt6WebChannel_FOUND AND TARGET Qt6::GuiPrivate)
        set(SEVENTHSLIDE_HAS_WEBENGINE ON)
    else()
        message(STATUS "Qt WebEngine not found: web pages will not be drawn")
    endif()
endif()
if(SEVENTHSLIDE_REQUIRE_WEBENGINE AND NOT SEVENTHSLIDE_HAS_WEBENGINE)
    # The find_package calls are quiet, so this names what was missing.
    set(_webengine_missing "")
    if(NOT SEVENTHSLIDE_WEBENGINE)
        list(APPEND _webengine_missing "SEVENTHSLIDE_WEBENGINE is off")
    else()
        foreach(_module WebEngineQuick WebChannel)
            if(NOT Qt6${_module}_FOUND)
                list(APPEND _webengine_missing "Qt's ${_module} was not found")
            endif()
        endforeach()
        if(NOT TARGET Qt6::GuiPrivate)
            list(APPEND _webengine_missing "Qt's GuiPrivate was not found")
        endif()
    endif()
    list(JOIN _webengine_missing ", " _webengine_missing)
    message(FATAL_ERROR "SEVENTHSLIDE_REQUIRE_WEBENGINE is on, and Qt WebEngine is not built in: "
        "${_webengine_missing}")
endif()
# QML modules (src/ui and src/app) use the standard :/qt/qml/ resource prefix.
qt_policy(SET QTP0001 NEW)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
add_subdirectory(src/core)

# Web remote transport - app and tests.
if(SEVENTHSLIDE_BUILD_APP OR SEVENTHSLIDE_BUILD_TESTS)
    add_subdirectory(src/remote)
endif()

# The sync's connections between SeventhSlides - app, server and tests.
if(SEVENTHSLIDE_BUILD_APP OR SEVENTHSLIDE_BUILD_TESTS)
    add_subdirectory(src/link)
    # `seventhslide --server`: the same program, no window.
    add_subdirectory(src/server)
endif()

# The Audio Mixer's engine - app, tests, and tools (the render library's
# destinations carry its sound). miniaudio is vendored.
if(SEVENTHSLIDE_BUILD_APP OR SEVENTHSLIDE_BUILD_TESTS OR SEVENTHSLIDE_BUILD_TOOLS)
    add_subdirectory(third_party/miniaudio)
    add_subdirectory(src/audio)
    # Live streams sent out (SRT): the render library's SRT destinations.
    add_subdirectory(src/stream)
endif()

# Audience windows / paint - app, tests, and render-smoke tool.
if(SEVENTHSLIDE_BUILD_APP OR SEVENTHSLIDE_BUILD_TESTS OR SEVENTHSLIDE_BUILD_TOOLS)
    add_subdirectory(src/render)
endif()

if(SEVENTHSLIDE_BUILD_APP)
    # Vendored: the web remote's QR code (src/app).
    add_subdirectory(third_party/qrcodegen)
    add_subdirectory(src/ui)
    add_subdirectory(src/app)
endif()

# Shared by tests and developer tools; never part of the app.
if(SEVENTHSLIDE_BUILD_TESTS OR SEVENTHSLIDE_BUILD_TOOLS)
    add_subdirectory(src/devsupport)
endif()

if(SEVENTHSLIDE_BUILD_TOOLS)
    add_subdirectory(src/tools/layout_dump)
    add_subdirectory(src/tools/render_smoke)
    add_subdirectory(src/tools/render_song)
endif()

if(SEVENTHSLIDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
# Every rule here is component seventhslide, which is all the packages
# install: what is fetched (pugixml) has install rules of its own.
if(SEVENTHSLIDE_BUILD_APP)
    include(GNUInstallDirs)
    # macOS: the bundle goes in /Applications. One to give another Mac is
    # the macos_app target's (src/app/CMakeLists.txt): this one still uses
    # Homebrew's libraries where they are.
    install(TARGETS seventhslide
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT seventhslide
        BUNDLE DESTINATION /Applications COMPONENT seventhslide
    )
    # Windows: the server's console program (src/server/console_main.cpp).
    if(TARGET seventhslide_server_console)
        install(TARGETS seventhslide_server_console RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT seventhslide)
    endif()

    # Linux desktop launcher + hicolor icon. With its own Qt, the deb's are
    # where the desktop looks, outside its prefix (/opt/seventhslide), as is
    # the seventhslide the launcher starts.
    if(UNIX AND NOT APPLE)
        set(_ss_share "${CMAKE_INSTALL_DATAROOTDIR}")
        if(SEVENTHSLIDE_BUNDLE_QT)
            set(_ss_share "/usr/share")
        endif()
        set(_ss_icon_png "${CMAKE_SOURCE_DIR}/packaging/icons/seventhslide-icon.png")
        if(EXISTS "${_ss_icon_png}")
            install(FILES "${_ss_icon_png}"
                DESTINATION "${_ss_share}/icons/hicolor/1024x1024/apps"
                RENAME seventhslide.png
                COMPONENT seventhslide
            )
        endif()
        install(FILES "${CMAKE_SOURCE_DIR}/packaging/seventhslide.desktop"
            DESTINATION "${_ss_share}/applications"
            COMPONENT seventhslide
        )
        # The licence (GPL 3.0 or later) and the notice of its additional
        # permission and of the other software in it.
        install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/NOTICE.md"
            DESTINATION "${_ss_share}/doc/seventhslide"
            COMPONENT seventhslide
        )
    endif()

    # A deb with its own Qt (Ubuntu 24.04's is below the 6.8 floor):
    # packaging/linux/deploy_qt.cmake copies in the Qt it was built with,
    # under the prefix; the program finds its libraries through its RPATH,
    # and the rest through the qt.conf beside it. /usr/bin/seventhslide is a
    # link to it. Qt writes CONTENT out with its variables filled in, so it
    # names only paths known now.
    if(SEVENTHSLIDE_BUNDLE_QT)
        set_target_properties(seventhslide PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
        qt_generate_deploy_script(
            TARGET seventhslide
            OUTPUT_SCRIPT _ss_deploy_script
            CONTENT "
set(_ss_executable \"$<TARGET_FILE:seventhslide>\")
set(_ss_qml_source \"${CMAKE_SOURCE_DIR}/src/ui\")
set(_ss_cpp_source \"${CMAKE_SOURCE_DIR}/src/app\")
set(_ss_qt_qml \"${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}\")
set(_ss_qmlimportscanner \"${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBEXECS}/qmlimportscanner\")
include(\"${CMAKE_SOURCE_DIR}/packaging/linux/deploy_qt.cmake\")
")
        install(SCRIPT "${_ss_deploy_script}" COMPONENT seventhslide)
        install(CODE "
            file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/usr/bin\")
            file(CREATE_LINK \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/seventhslide\"
                \"\$ENV{DESTDIR}/usr/bin/seventhslide\" SYMBOLIC)
        " COMPONENT seventhslide)
    endif()

    # Every deb: an AppArmor profile that lets Qt WebEngine's sandbox make its
    # user namespaces, as Ubuntu allows only a program a profile names
    # (packaging/linux/apparmor-seventhslide.in). The program, where it is
    # installed; and Qt WebEngine's process too where the deb carries it, as
    # Ubuntu's own profile names only its own.
    if(SEVENTHSLIDE_PACKAGE STREQUAL "deb")
        set(SS_PROGRAM "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/seventhslide")
        set(SS_WEBENGINE_PROFILE "")
        if(SEVENTHSLIDE_BUNDLE_QT)
            set(SS_WEBENGINE_PROFILE "
profile seventhslide-webengine ${CMAKE_INSTALL_PREFIX}/libexec/QtWebEngineProcess flags=(unconfined) {
  userns,

  include if exists <local/seventhslide-webengine>
}
")
        endif()
        configure_file("${CMAKE_SOURCE_DIR}/packaging/linux/apparmor-seventhslide.in"
            "${CMAKE_BINARY_DIR}/packaging/apparmor-seventhslide" @ONLY)
        install(FILES "${CMAKE_BINARY_DIR}/packaging/apparmor-seventhslide"
            DESTINATION /etc/apparmor.d RENAME seventhslide COMPONENT seventhslide)
    endif()
endif()

include(SeventhSlidePackaging)
