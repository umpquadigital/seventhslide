# The Ubuntu 24.04 deb's Qt: run at install, from Qt's deploy script for the
# app (the top-level CMakeLists.txt, SEVENTHSLIDE_BUNDLE_QT), which sets the
# __QT_DEPLOY_* variables and, from the configure, _ss_executable (the
# program as built), _ss_qml_source (src/ui), _ss_cpp_source (src/app),
# _ss_qt_qml (Qt's QML modules) and _ss_qmlimportscanner.
#
# Qt's deploy copies in every library the program needs that is not the
# system's (Qt's own), the plugins and QML modules it uses, Qt WebEngine's
# process and resources, and a qt.conf beside the program that points at
# them, all under the install prefix (/opt/seventhslide).
#
# The QML modules are those src/ui imports, and those the QML written in
# src/app's C++ imports (Qt WebEngine's, webengine_renderer.cpp). Qt's own
# scan looks only in the program's folder (src/app), where the QML is in
# strings: the app's QML is a module linked in. So qmlimportscanner is run
# here on src/ui and on a file of the import lines in src/app's sources, and
# its list given to Qt's QML deploy. The app's own modules are not found in
# Qt's folder, so are left out: they are in the program.
#
# The plugins are those of the modules the app links, less the QML debugger,
# the eglfs devices (no eglfs platform is carried), the serial-port position
# source (Qt Serial Port is not carried) and the SQL drivers but SQLite,
# whose client libraries apt would otherwise install; and with the
# Wayland platform (the app links no Qt Wayland module, so Qt would not count
# it), the input methods (IBus, compose keys) and the platform themes the
# system's file dialogs come through. Qt's deploy takes them from
# __QT_DEPLOY_PLUGINS, its own list, which is edited here: Qt 6.8 has no
# option for it.
#
# Qt 6.8's WebEngine step adds DESTDIR to a path that has it already (CPack
# installs with a DESTDIR), so the deploy runs with the whole destination as
# its prefix and no DESTDIR, which is put back after.

set(_ss_qt_plugins "${__QT_DEPLOY_QT_INSTALL_PREFIX}/${__QT_DEPLOY_QT_INSTALL_PLUGINS}")
list(FILTER __QT_DEPLOY_PLUGINS EXCLUDE REGEX
    "/(qmltooling|egldeviceintegrations)/|/position/libqtposition_nmea|/sqldrivers/libqsql(mysql|psql|odbc|mimer)")
file(GLOB _ss_more_plugins
    "${_ss_qt_plugins}/platforms/libqwayland-*.so"
    "${_ss_qt_plugins}/wayland-*/*.so"
    "${_ss_qt_plugins}/platforminputcontexts/*.so"
    "${_ss_qt_plugins}/platformthemes/*.so")
list(APPEND __QT_DEPLOY_PLUGINS ${_ss_more_plugins})

set(_ss_destdir "$ENV{DESTDIR}")
set(ENV{DESTDIR} "")
set(CMAKE_INSTALL_PREFIX "${QT_DEPLOY_PREFIX}")
set(_ss_cpp_imports "")
file(GLOB _ss_cpp_files "${_ss_cpp_source}/*.cpp")
foreach(_ss_file IN LISTS _ss_cpp_files)
    file(STRINGS "${_ss_file}" _ss_lines REGEX "^import [A-Za-z][A-Za-z0-9.]*$")
    list(APPEND _ss_cpp_imports ${_ss_lines})
endforeach()
list(REMOVE_DUPLICATES _ss_cpp_imports)
list(JOIN _ss_cpp_imports "\n" _ss_cpp_imports)
set(_ss_cpp_qml "${__QT_DEPLOY_IMPL_DIR}/seventhslide_cpp_imports.qml")
file(WRITE "${_ss_cpp_qml}" "${_ss_cpp_imports}\nQtObject {}\n")

set(_ss_qml_imports "${__QT_DEPLOY_IMPL_DIR}/seventhslide_qml_imports.cmake")
execute_process(
    COMMAND "${_ss_qmlimportscanner}" -rootPath "${_ss_qml_source}" -qmlFiles "${_ss_cpp_qml}"
            -importPath "${_ss_qt_qml}" -cmake-output -output-file "${_ss_qml_imports}"
    RESULT_VARIABLE _ss_scan_result
)
if(_ss_scan_result)
    message(FATAL_ERROR "qmlimportscanner failed on ${_ss_qml_source}: ${_ss_scan_result}")
endif()
_qt_internal_deploy_qml_imports_for_target(
    IMPORTS_FILE "${_ss_qml_imports}"
    PLUGINS_FOUND _ss_qml_plugins
    QML_DIR "${QT_DEPLOY_QML_DIR}"
    PLUGINS_DIR "${QT_DEPLOY_PLUGINS_DIR}"
)
qt6_deploy_runtime_dependencies(
    EXECUTABLE "${_ss_executable}"
    ADDITIONAL_MODULES ${_ss_qml_plugins}
    GENERATE_QT_CONF
)
set(ENV{DESTDIR} "${_ss_destdir}")
