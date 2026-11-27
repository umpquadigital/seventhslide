# seventhslide_render - audience windows / paint surfaces.
# Consumes SceneFrame from core. No DB / session business rules.

# Blackmagic DeckLink: vendored SDK headers plus the SDK's own dispatch, which
# loads the driver at run time: libDeckLinkAPI.so (dlopen) on Linux,
# /Library/Frameworks/DeckLinkAPI.framework (CFBundle) on macOS. Build-time
# only - no Blackmagic binary is linked or shipped. See
# third_party/decklink/README.md. Windows has no headers here yet:
# bmd_unavailable.cpp stands in for the parts that talk to the driver, which
# then is never found (Windows needs no dispatch at all: real COM via
# CoCreateInstance). bmd_common.cpp, what needs no driver, is built everywhere.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE)
    if(APPLE)
        set(SEVENTHSLIDE_DECKLINK_DIR ${CMAKE_SOURCE_DIR}/third_party/decklink/mac)
    else()
        set(SEVENTHSLIDE_DECKLINK_DIR ${CMAKE_SOURCE_DIR}/third_party/decklink/linux)
    endif()
    set(SEVENTHSLIDE_DECKLINK_SOURCES
        ${SEVENTHSLIDE_DECKLINK_DIR}/DeckLinkAPIDispatch.cpp
        bmd_output.cpp
        bmd_input.cpp
        bmd_runtime.cpp
    )
    # Vendored SDK source: build it, but don't hold it to our warning set.
    set_source_files_properties(${SEVENTHSLIDE_DECKLINK_DIR}/DeckLinkAPIDispatch.cpp
        PROPERTIES COMPILE_OPTIONS "-w")
else()
    set(SEVENTHSLIDE_DECKLINK_DIR ${CMAKE_SOURCE_DIR}/third_party/decklink/linux)
    set(SEVENTHSLIDE_DECKLINK_SOURCES bmd_unavailable.cpp)
endif()

add_library(seventhslide_render STATIC
    ${SEVENTHSLIDE_DECKLINK_SOURCES}
    background_painter.cpp
    bmd_common.cpp
    canvas_fit.cpp
    cover_fit.cpp
    css_color.cpp
    deinterlace.cpp
    destination.cpp
    destination_registry.cpp
    device_catalog.cpp
    effects_painter.cpp
    element_raster_cache.cpp
    fade.cpp
    image_painter.cpp
    mpv_media_player.cpp
    ndi_output.cpp
    ndi_runtime.cpp
    ndi_receive.cpp
    object_fit.cpp
    offscreen_sink.cpp
    picture_painter.cpp
    table_painter.cpp
    pixel_pack.cpp
    output_window.cpp
    output_surfaces.cpp
    output_window_host.cpp
    presenter_view.cpp
    scene_compositor.cpp
    scene_presenter.cpp
    shape_painter.cpp
    srt_output.cpp
    static_assets.cpp
    text_painter.cpp
    video_painter.cpp
    web_painter.cpp
    live_painter.cpp
    source_frames.cpp
    virtual_camera.cpp
    include/seventhslide/render/background_painter.hpp
    include/seventhslide/render/bmd_output.hpp
    include/seventhslide/render/bmd_input.hpp
    include/seventhslide/render/bmd_runtime.hpp
    include/seventhslide/render/canvas_fit.hpp
    include/seventhslide/render/cover_fit.hpp
    include/seventhslide/render/css_color.hpp
    include/seventhslide/render/deinterlace.hpp
    include/seventhslide/render/destination.hpp
    include/seventhslide/render/destination_registry.hpp
    include/seventhslide/render/device_catalog.hpp
    include/seventhslide/render/effects_painter.hpp
    include/seventhslide/render/element_raster_cache.hpp
    include/seventhslide/render/fade.hpp
    include/seventhslide/render/latest_frame.hpp
    include/seventhslide/render/image_painter.hpp
    include/seventhslide/render/mpv_media_player.hpp
    include/seventhslide/render/ndi_abi.hpp
    include/seventhslide/render/ndi_output.hpp
    include/seventhslide/render/ndi_runtime.hpp
    include/seventhslide/render/ndi_receive.hpp
    include/seventhslide/render/object_fit.hpp
    include/seventhslide/render/offscreen_sink.hpp
    include/seventhslide/render/paint_context.hpp
    include/seventhslide/render/picture_painter.hpp
    include/seventhslide/render/pixel_pack.hpp
    include/seventhslide/render/output_window.hpp
    include/seventhslide/render/output_surfaces.hpp
    include/seventhslide/render/output_window_host.hpp
    include/seventhslide/render/presenter_view.hpp
    include/seventhslide/render/scene_compositor.hpp
    include/seventhslide/render/scene_presenter.hpp
    include/seventhslide/render/shape_painter.hpp
    include/seventhslide/render/srt_output.hpp
    include/seventhslide/render/static_assets.hpp
    include/seventhslide/render/text_painter.hpp
    include/seventhslide/render/video_painter.hpp
    include/seventhslide/render/web_painter.hpp
    include/seventhslide/render/live_painter.hpp
    include/seventhslide/render/source_frames.hpp
    include/seventhslide/render/virtual_camera.hpp
)

add_library(SeventhSlide::render ALIAS seventhslide_render)

# An output window keeps its picture's texture (picture_texture.hpp) through
# QRhi, which is in Qt's GuiPrivate; without it, a texture per picture.
if(TARGET Qt6::GuiPrivate)
    target_sources(seventhslide_render PRIVATE picture_texture.cpp
                                               include/seventhslide/render/picture_texture.hpp)
    target_link_libraries(seventhslide_render PRIVATE Qt6::GuiPrivate)
    target_compile_definitions(seventhslide_render PUBLIC SEVENTHSLIDE_RENDER_RHI=1)
endif()

# macOS: output windows over the menu bar and the Dock, and the Mac kept awake
# while presenting (AppKit); screens known by their display's UUID (ColorSync);
# the DeckLink dispatch's CFBundle and CFString (CoreFoundation).
if(APPLE)
    target_sources(seventhslide_render PRIVATE output_window_mac.mm output_window_mac.hpp)
    target_link_libraries(seventhslide_render PRIVATE "-framework AppKit"
                                                      "-framework ApplicationServices"
                                                      "-framework ColorSync"
                                                      "-framework CoreFoundation")
endif()

target_include_directories(seventhslide_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
# PRIVATE on purpose: bmd_runtime.hpp exposes no SDK type, so nothing downstream
# needs (or gets) the DeckLink headers. SYSTEM: vendored, not held to our warnings.
target_include_directories(seventhslide_render SYSTEM
    PRIVATE
        ${SEVENTHSLIDE_DECKLINK_DIR}/include
)

target_link_libraries(seventhslide_render
    PUBLIC
        SeventhSlide::core
        # NDI and the other destinations carry their output's sound from the
        # Audio Mixer (OutputAudioSink).
        SeventhSlide::audio
        # SRT destinations stream through it.
        SeventhSlide::stream
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        SeventhSlide::mpv
    PRIVATE
        # dlopen/dlsym for the optional NDI runtime and for the DeckLink
        # dispatch. Neither vendor's library is ever linked or shipped -
        # see ndi_runtime.cpp and third_party/decklink/README.md.
        ${CMAKE_DL_LIBS}
)
target_link_libraries(seventhslide_render PRIVATE seventhslide_warnings)

target_compile_definitions(seventhslide_render PUBLIC SEVENTHSLIDE_WITH_MPV=1)

target_compile_features(seventhslide_render PUBLIC cxx_std_20)

set_target_properties(seventhslide_render PROPERTIES
    OUTPUT_NAME seventhslide_render
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
)

# ---------------------------------------------------------------------------
# Optional: prove ndi_abi.hpp matches the real NDI SDK headers.
# Developer-only, opt-in, never part of CI. Requires the SDK to be installed:
#   cmake -B build -DSEVENTHSLIDE_NDI_VERIFY_ABI=ON -DNDI_SDK_DIR=/path/to/sdk
# Compiling the target IS the check; it produces nothing that ships.
# ---------------------------------------------------------------------------
if(SEVENTHSLIDE_NDI_VERIFY_ABI)
    find_path(NDI_SDK_INCLUDE_DIR
        NAMES Processing.NDI.Lib.h
        HINTS
            ${NDI_SDK_DIR}/include
            $ENV{NDI_SDK_DIR}/include
            "/usr/include/ndi-sdk"
            "/usr/include/ndi"
            "/usr/local/include"
            "/Library/NDI SDK for Apple/include"
            "$ENV{ProgramFiles}/NDI/NDI 6 SDK/Include"
    )
    if(NOT NDI_SDK_INCLUDE_DIR)
        message(FATAL_ERROR
            "SEVENTHSLIDE_NDI_VERIFY_ABI=ON but Processing.NDI.Lib.h was not found. "
            "Install the NDI SDK and pass -DNDI_SDK_DIR=/path/to/sdk.")
    endif()
    message(STATUS "SeventhSlide: NDI ABI check against ${NDI_SDK_INCLUDE_DIR}")
    add_library(seventhslide_ndi_abi_check OBJECT ndi_abi_check.cpp)
    target_include_directories(seventhslide_ndi_abi_check
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${NDI_SDK_INCLUDE_DIR}
    )
    target_compile_features(seventhslide_ndi_abi_check PRIVATE cxx_std_20)
    set_target_properties(seventhslide_ndi_abi_check PROPERTIES AUTOMOC OFF)
endif()
