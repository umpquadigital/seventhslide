# The Linux packages a release carries, made by CPack from the install rules
# (the top-level CMakeLists.txt): `cpack --config build-package/CPackConfig.cmake`
# after building with an ubuntu-*-deb or the fedora-rpm preset. The macOS disk image
# is the macos_app target's (src/app/CMakeLists.txt) and the Windows
# installer packaging/windows/seventhslide.iss's; they need nothing here.
#
# Each file has the same name in every release (SEVENTHSLIDE_PACKAGE_FILE),
# so https://github.com/<owner>/<repo>/releases/latest/download/<name> is
# always the newest one (.github/workflows/release.yml lists them all).
#
# deb: one Ubuntu (SEVENTHSLIDE_DEB_FOR) and what is built on it (Mint).
#      dpkg-shlibdeps names the system libraries the program needs (mpv,
#      FFmpeg, OpenSSL, X11 and Wayland, NSS for WebEngine) as dependencies,
#      so apt installs them. With its own Qt (SEVENTHSLIDE_BUNDLE_QT, Ubuntu
#      24.04), the program and that Qt are in /opt/seventhslide; otherwise it
#      is in /usr and uses Ubuntu's, whose QML modules and plugins it names
#      too, as no library says it needs them. An AppArmor profile lets Qt
#      WebEngine's sandbox run (packaging/linux/apparmor-seventhslide.in).
# rpm: the current Fedora, built against Fedora's Qt and FFmpeg in /usr; rpm
#      reads the libraries the program needs itself. FFmpeg is ffmpeg-free's or,
#      swapped in, RPM Fusion's: both give the same libraries.
#
# A pre-release's label (SEVENTHSLIDE_VERSION_LABEL) sorts before its release
# in both: 1.3.0~beta.2 for apt, release 0.beta.2 of 1.3.0 for rpm.

if(NOT SEVENTHSLIDE_BUILD_APP OR NOT SEVENTHSLIDE_PACKAGE MATCHES "^(deb|rpm)$")
    return()
endif()

set(CPACK_PACKAGE_NAME "seventhslide")
set(CPACK_PACKAGE_VENDOR "SeventhSlide")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Presentation software for churches, schools and events")
set(CPACK_PACKAGE_DESCRIPTION "Songs, Bible passages, slides, images and video, sent to \
any number of outputs: displays, desktop windows, virtual cameras, NDI, Blackmagic and SRT.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://seventhslide.org")
# Who answers for the packages: the deb's Maintainer field.
set(SEVENTHSLIDE_PACKAGE_CONTACT "SeventhSlide <webmaster@seventhslide.org>" CACHE STRING
    "Who answers for the Linux packages, as Name <email>")
set(CPACK_PACKAGE_CONTACT "${SEVENTHSLIDE_PACKAGE_CONTACT}")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
# The app's install rules alone (component seventhslide), in one package,
# not those of what is fetched: pugixml would add its headers.
set(CPACK_COMPONENTS_ALL seventhslide)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/deploy")
set(CPACK_STRIP_FILES ON)
# Nothing in it writable but by root (packaging/linux/package_modes.cmake).
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_SOURCE_DIR}/packaging/linux/package_modes.cmake")

if(SEVENTHSLIDE_PACKAGE STREQUAL "deb")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_FILE_NAME "${SEVENTHSLIDE_PACKAGE_FILE}")
    if(SEVENTHSLIDE_VERSION_LABEL STREQUAL "")
        set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
    else()
        set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}~${SEVENTHSLIDE_VERSION_LABEL}")
    endif()
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    if(SEVENTHSLIDE_BUNDLE_QT)
        # The libraries it carries are found there, not looked for in a package.
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${CMAKE_INSTALL_PREFIX}/lib")
    else()
        # Ubuntu's Qt: what the program loads, not links. The QML modules
        # src/ui and webengine_renderer.cpp import; the plugins for the
        # Wayland platform, SVG, SQLite and WebEngine's process; and, if
        # there, the platform themes the system's file dialogs come through.
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "qml6-module-qtqml, qml6-module-qtqml-models, qml6-module-qtqml-workerscript, \
qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-dialogs, \
qml6-module-qtquick-effects, qml6-module-qtquick-layouts, qml6-module-qtquick-shapes, \
qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qt-labs-folderlistmodel, \
qml6-module-qtwebchannel, qml6-module-qtwebengine, libqt6webenginecore6-bin, \
libqt6waylandclient6, libqt6wlshellintegration6, qt6-svg-plugins, libqt6sql6-sqlite")
        set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
            "qt6-xdgdesktopportal-platformtheme, qt6-gtk-platformtheme")
    endif()
    # Its AppArmor profile, a conffile, loaded once installed and unloaded once
    # removed (packaging/linux/debian).
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_SOURCE_DIR}/packaging/linux/debian/postinst"
        "${CMAKE_SOURCE_DIR}/packaging/linux/debian/postrm"
        "${CMAKE_SOURCE_DIR}/packaging/linux/debian/conffiles")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
else()
    set(CPACK_GENERATOR "RPM")
    set(CPACK_RPM_FILE_NAME "${SEVENTHSLIDE_PACKAGE_FILE}")
    if(SEVENTHSLIDE_VERSION_LABEL STREQUAL "")
        set(CPACK_RPM_PACKAGE_RELEASE "1")
    else()
        set(CPACK_RPM_PACKAGE_RELEASE "0.${SEVENTHSLIDE_VERSION_LABEL}")
    endif()
    # .fc43 and so on after the release: the Fedora it was built for.
    set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_PACKAGE_URL "https://seventhslide.org")
    # SPDX, as Fedora writes it. NOTICE.md's additional permission (NDI,
    # DeckLink) has no SPDX name, and only adds to what the GPL allows.
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
    # What rpm cannot read from the libraries: Qt's QML modules (Qt Quick's
    # controls, layouts and dialogs) and its Wayland platform.
    set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtsvg, qt6-qtwayland")
    # The folders the desktop already owns.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications
        /usr/share/icons
        /usr/share/icons/hicolor
        /usr/share/icons/hicolor/1024x1024
        /usr/share/icons/hicolor/1024x1024/apps
    )
endif()

include(CPack)
