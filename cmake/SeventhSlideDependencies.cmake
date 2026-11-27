# Shared dependency helpers: FetchContent version pins and system package
# lookups. Included once from the top-level CMakeLists.txt.

# Preferred Catch2 version for FetchContent (tests/).
set(SEVENTHSLIDE_CATCH2_VERSION "v3.8.0" CACHE STRING "Catch2 git tag")

# Preferred nlohmann/json version for FetchContent (core/, when needed).
set(SEVENTHSLIDE_JSON_VERSION "v3.11.3" CACHE STRING "nlohmann/json git tag")

# Preferred pugixml version for FetchContent (core/parse/).
set(SEVENTHSLIDE_PUGIXML_VERSION "v1.14" CACHE STRING "pugixml git tag")

# Preferred FTXUI version for FetchContent (server/: its settings screens).
set(SEVENTHSLIDE_FTXUI_VERSION "v7.0.3" CACHE STRING "FTXUI git tag")

# OpenSSL's libcrypto signs and checks what synced libraries trade and locks
# the admin key (src/core/crypto). Qt's TLS loads the same library at run
# time. Install openssl-devel / libssl-dev.
# macOS: Homebrew's openssl is keg-only, so the macos preset points at it
# through $HOMEBREW_PREFIX. A configure started without that set (an IDE
# opened from the Dock) gets /opt/openssl; look where Homebrew puts it.
if(APPLE AND (NOT OPENSSL_ROOT_DIR OR NOT EXISTS "${OPENSSL_ROOT_DIR}"))
    find_program(_ss_brew brew HINTS /opt/homebrew/bin /usr/local/bin)
    if(_ss_brew)
        execute_process(COMMAND "${_ss_brew}" --prefix openssl
                        OUTPUT_VARIABLE _ss_brew_openssl OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET RESULT_VARIABLE _ss_brew_failed)
    endif()
    if(_ss_brew AND NOT _ss_brew_failed AND EXISTS "${_ss_brew_openssl}")
        set(OPENSSL_ROOT_DIR "${_ss_brew_openssl}")
    elseif(EXISTS /opt/homebrew/opt/openssl)
        set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl)
    elseif(EXISTS /usr/local/opt/openssl)
        set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
    endif()
    if(OPENSSL_ROOT_DIR)
        message(STATUS "SeventhSlide: OpenSSL from ${OPENSSL_ROOT_DIR}")
    endif()
endif()
find_package(OpenSSL 3.0 REQUIRED)
message(STATUS "SeventhSlide: OpenSSL ${OPENSSL_VERSION}")

# libmpv and FFmpeg are found by pkg-config where there is one (Linux,
# Homebrew, MSYS2, vcpkg's pkgconf), else by their headers and libraries
# under CMAKE_PREFIX_PATH (the usual Windows builds ship no .pc files: point
# CMAKE_PREFIX_PATH, or SEVENTHSLIDE_MPV_ROOT / SEVENTHSLIDE_FFMPEG_ROOT, at
# the folder holding include/ and lib/). Either way the rest of the build
# links SeventhSlide::mpv and SeventhSlide::ffmpeg.
find_package(PkgConfig QUIET)

# Each name in ``libs`` as one library found under ``hint``, else a fatal
# error that says what was looked for.
function(seventhslide_find_libraries target hint)
    foreach(lib IN LISTS ARGN)
        find_library(SEVENTHSLIDE_${lib}_LIBRARY NAMES ${lib} lib${lib}
            HINTS "${hint}" PATH_SUFFIXES lib)
        if(NOT SEVENTHSLIDE_${lib}_LIBRARY)
            message(FATAL_ERROR "SeventhSlide: library ${lib} not found. Set "
                "CMAKE_PREFIX_PATH (or the dependency's SEVENTHSLIDE_*_ROOT) to "
                "the folder that holds its include/ and lib/.")
        endif()
        target_link_libraries(${target} INTERFACE "${SEVENTHSLIDE_${lib}_LIBRARY}")
    endforeach()
endfunction()

# MSVC's libmpv: an import library under ``hint`` (an mpv.lib, as vcpkg's or
# one made by hand), else one made here from the mpv-dev archive's mpv.def,
# as mpv says to for MSVC. CMake's MSVC search never matches the archive's
# libmpv.dll.a, the MinGW import library, which is the last resort.
function(seventhslide_mpv_msvc_library out hint)
    find_library(SEVENTHSLIDE_mpv_LIBRARY NAMES mpv libmpv
        HINTS "${hint}" PATH_SUFFIXES lib)
    if(SEVENTHSLIDE_mpv_LIBRARY)
        set(${out} "${SEVENTHSLIDE_mpv_LIBRARY}" PARENT_SCOPE)
        return()
    endif()
    find_file(SEVENTHSLIDE_MPV_DEF NAMES mpv.def libmpv.def
        HINTS "${hint}" PATH_SUFFIXES lib NO_DEFAULT_PATH)
    if(SEVENTHSLIDE_MPV_DEF)
        # The import library names the DLL it stands for: the one beside the
        # .def (libmpv-2.dll in current archives, mpv-2.dll or mpv-1.dll in
        # older ones).
        get_filename_component(def_dir "${SEVENTHSLIDE_MPV_DEF}" DIRECTORY)
        file(GLOB dlls "${def_dir}/libmpv-*.dll" "${def_dir}/mpv-*.dll"
            "${def_dir}/../bin/libmpv-*.dll" "${def_dir}/../bin/mpv-*.dll")
        if(NOT dlls)
            message(FATAL_ERROR "SeventhSlide: found ${SEVENTHSLIDE_MPV_DEF} but no "
                "libmpv-*.dll beside it to make mpv.lib for.")
        endif()
        list(GET dlls 0 dll)
        get_filename_component(dll_name "${dll}" NAME)
        set(lib "${CMAKE_BINARY_DIR}/mpv/mpv.lib")
        # Made again when the .def changes, or another archive is pointed at.
        set(made_from "${SEVENTHSLIDE_MPV_DEF} ${dll_name}")
        set(stamp "${lib}.from")
        set(was_made_from "")
        if(EXISTS "${stamp}")
            file(READ "${stamp}" was_made_from)
        endif()
        if(NOT EXISTS "${lib}" OR "${SEVENTHSLIDE_MPV_DEF}" IS_NEWER_THAN "${lib}"
           OR NOT was_made_from STREQUAL made_from)
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/mpv")
            # CMAKE_AR is MSVC's lib.exe.
            execute_process(
                COMMAND "${CMAKE_AR}" /nologo "/def:${SEVENTHSLIDE_MPV_DEF}"
                        "/name:${dll_name}" "/machine:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}"
                        "/out:${lib}"
                RESULT_VARIABLE result
                OUTPUT_VARIABLE output
                ERROR_VARIABLE output)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "SeventhSlide: making mpv.lib from "
                    "${SEVENTHSLIDE_MPV_DEF} failed:\n${output}")
            endif()
            file(WRITE "${stamp}" "${made_from}")
        endif()
        message(STATUS "SeventhSlide: mpv.lib made from ${SEVENTHSLIDE_MPV_DEF} (for ${dll_name})")
        set(${out} "${lib}" PARENT_SCOPE)
        return()
    endif()
    # find_file: find_library takes a name as given only when it ends in one
    # of the platform's library suffixes, and .dll.a is not MSVC's.
    find_file(SEVENTHSLIDE_mpv_GNU_LIBRARY NAMES libmpv.dll.a
        HINTS "${hint}" PATH_SUFFIXES lib NO_DEFAULT_PATH)
    if(SEVENTHSLIDE_mpv_GNU_LIBRARY)
        set(${out} "${SEVENTHSLIDE_mpv_GNU_LIBRARY}" PARENT_SCOPE)
        return()
    endif()
    message(FATAL_ERROR "SeventhSlide: no import library for libmpv under ${hint}: "
        "no mpv.lib, no mpv.def to make one from, no libmpv.dll.a. Set "
        "SEVENTHSLIDE_MPV_ROOT to the unpacked mpv-dev archive.")
endfunction()

# libmpv is required for video. Install mpv-devel / libmpv-dev; on Windows
# the mpv-dev builds (include/mpv, libmpv-2.dll, mpv.def and libmpv.dll.a):
# MSVC links an mpv.lib made from the .def (above), MinGW libmpv.dll.a.
add_library(seventhslide_mpv INTERFACE)
add_library(SeventhSlide::mpv ALIAS seventhslide_mpv)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(MPV QUIET IMPORTED_TARGET mpv)
endif()
# pkg-config can know mpv's version yet give none of its libraries, when a
# Requires.private package is missing: Homebrew's mpv.pc names libarchive,
# which Homebrew keeps out of the search path. That would link no libmpv at
# all, so mpv is then looked for directly, as without pkg-config.
if(MPV_FOUND AND NOT MPV_LIBRARIES)
    message(STATUS "SeventhSlide: pkg-config gives no libraries for mpv; looking for it directly")
    set(MPV_FOUND FALSE)
endif()
if(MPV_FOUND)
    target_link_libraries(seventhslide_mpv INTERFACE PkgConfig::MPV)
    message(STATUS "SeventhSlide: libmpv ${MPV_VERSION}")
else()
    set(SEVENTHSLIDE_MPV_ROOT "" CACHE PATH "Folder holding libmpv's include/ and lib/")
    find_path(SEVENTHSLIDE_MPV_INCLUDE_DIR NAMES mpv/client.h
        HINTS "${SEVENTHSLIDE_MPV_ROOT}" PATH_SUFFIXES include)
    if(NOT SEVENTHSLIDE_MPV_INCLUDE_DIR)
        message(FATAL_ERROR "SeventhSlide: libmpv not found (no pkg-config 'mpv', "
            "no mpv/client.h). Install mpv-devel / libmpv-dev, or set "
            "SEVENTHSLIDE_MPV_ROOT.")
    endif()
    target_include_directories(seventhslide_mpv SYSTEM INTERFACE "${SEVENTHSLIDE_MPV_INCLUDE_DIR}")
    # Where the library is: the root given, else the folder above include/.
    # The mpv-dev archives keep it there rather than in lib/, which a
    # CMAKE_PREFIX_PATH search alone does not look in.
    if(SEVENTHSLIDE_MPV_ROOT)
        set(_ss_mpv_hint "${SEVENTHSLIDE_MPV_ROOT}")
    else()
        get_filename_component(_ss_mpv_hint "${SEVENTHSLIDE_MPV_INCLUDE_DIR}" DIRECTORY)
    endif()
    if(MSVC)
        seventhslide_mpv_msvc_library(_ss_mpv_lib "${_ss_mpv_hint}")
        target_link_libraries(seventhslide_mpv INTERFACE "${_ss_mpv_lib}")
    else()
        seventhslide_find_libraries(seventhslide_mpv "${_ss_mpv_hint}" mpv)
    endif()
    message(STATUS "SeventhSlide: libmpv from ${SEVENTHSLIDE_MPV_INCLUDE_DIR}")
endif()

# FFmpeg decodes the sound the Audio Mixer plays (src/audio), the frame a
# video's thumbnail shows (src/app/video_frame.cpp), and live capture devices
# and streams (src/app/ffmpeg_live_input.cpp, through libavdevice). 5.1 is the
# floor: the AVChannelLayout API (swr_alloc_set_opts2). Install ffmpeg-devel /
# libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev
# libavdevice-dev; on Windows a shared ("-shared") build.
add_library(seventhslide_ffmpeg INTERFACE)
add_library(SeventhSlide::ffmpeg ALIAS seventhslide_ffmpeg)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavdevice>=59.7.100
        libavformat>=59.27.100
        libavcodec>=59.37.100
        libavutil>=57.28.100
        libswresample>=4.7.100
        libswscale>=6.7.100
    )
endif()
if(FFMPEG_FOUND)
    target_link_libraries(seventhslide_ffmpeg INTERFACE PkgConfig::FFMPEG)
    message(STATUS "SeventhSlide: FFmpeg libavcodec ${FFMPEG_libavcodec_VERSION}")
else()
    set(SEVENTHSLIDE_FFMPEG_ROOT "" CACHE PATH "Folder holding FFmpeg's include/ and lib/")
    # libavcodec/version_major.h is new in FFmpeg 5.1, so finding it is the
    # floor's check here (pkg-config checks each library's version itself).
    find_path(SEVENTHSLIDE_FFMPEG_INCLUDE_DIR NAMES libavcodec/version_major.h
        HINTS "${SEVENTHSLIDE_FFMPEG_ROOT}" PATH_SUFFIXES include)
    if(NOT SEVENTHSLIDE_FFMPEG_INCLUDE_DIR)
        message(FATAL_ERROR "SeventhSlide: FFmpeg 5.1 or later not found (no "
            "pkg-config, no libavcodec/version_major.h). Install the FFmpeg "
            "development files, or set SEVENTHSLIDE_FFMPEG_ROOT.")
    endif()
    target_include_directories(seventhslide_ffmpeg SYSTEM INTERFACE "${SEVENTHSLIDE_FFMPEG_INCLUDE_DIR}")
    seventhslide_find_libraries(seventhslide_ffmpeg "${SEVENTHSLIDE_FFMPEG_ROOT}"
        avdevice avformat avcodec avutil swresample swscale)
    message(STATUS "SeventhSlide: FFmpeg from ${SEVENTHSLIDE_FFMPEG_INCLUDE_DIR}")
endif()

# Conda / some cross toolchains list /usr/include as "implicit" so CMake drops
# it from -I/-isystem, yet the compiler does not search it. Force a real -I for
# <mpv/client.h>. (Only pkg-config's mpv can be one of those.) Not on Windows:
# MSYS2's mpv is in the compiler's own include folder, and a -I for that one
# breaks libstdc++'s #include_next <stdlib.h>.
if(MPV_FOUND AND NOT WIN32)
    find_path(SEVENTHSLIDE_MPV_CLIENT_INCLUDE_DIR
        NAMES mpv/client.h
        HINTS ${MPV_INCLUDE_DIRS}
        REQUIRED
    )
    target_compile_options(seventhslide_mpv INTERFACE
        "-I${SEVENTHSLIDE_MPV_CLIENT_INCLUDE_DIR}")
endif()

# What follows is Linux's (PipeWire, Wayland, X11), and only through
# pkg-config.
if(NOT PKG_CONFIG_FOUND)
    set(PIPEWIRE_FOUND OFF)
    set(WAYLAND_CLIENT_FOUND OFF)
    set(SEVENTHSLIDE_WAYLAND_CONFINEMENT_DEPS OFF)
    set(XFIXES_FOUND OFF)
    return()
endif()

# PipeWire: optional. With it, a virtual camera also offers its output's
# sound as a microphone (src/audio/pipewire_source.cpp); without it, it does
# not. 0.3.49 is the floor: pw_buffer::requested (Ubuntu 22.04's 0.3.48 builds
# without the microphone).
pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3>=0.3.49)
if(PIPEWIRE_FOUND)
    message(STATUS "SeventhSlide: PipeWire ${PIPEWIRE_VERSION} (virtual camera microphones)")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "SeventhSlide: no PipeWire; virtual cameras carry no microphone")
endif()

# The pointer kept on a web page worked from the Controller
# (src/app/pointer_confinement.hpp): each windowing system's own way, built
# where its library is. All optional; without one, that system keeps no
# pointer. Wayland: the compositor's pointer constraints, from
# wayland-protocols, generated by wayland-scanner (libwayland-dev
# wayland-protocols / wayland-devel wayland-protocols-devel). X11: XFixes'
# pointer barriers (libxfixes-dev / libXfixes-devel).
pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
pkg_get_variable(SEVENTHSLIDE_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_get_variable(SEVENTHSLIDE_WAYLAND_SCANNER wayland-scanner wayland_scanner)
set(SEVENTHSLIDE_POINTER_CONSTRAINTS_XML
    "${SEVENTHSLIDE_WAYLAND_PROTOCOLS_DIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")
if(WAYLAND_CLIENT_FOUND AND SEVENTHSLIDE_WAYLAND_SCANNER
   AND EXISTS "${SEVENTHSLIDE_POINTER_CONSTRAINTS_XML}")
    set(SEVENTHSLIDE_WAYLAND_CONFINEMENT_DEPS ON)
else()
    set(SEVENTHSLIDE_WAYLAND_CONFINEMENT_DEPS OFF)
endif()
pkg_check_modules(XFIXES QUIET IMPORTED_TARGET xfixes>=5.0 x11)
