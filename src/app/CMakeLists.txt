# seventhslide - Qt/QML operator application.

# Everything but main(): the operator application as a library, so the tests
# link the same objects the app runs instead of compiling them a second time.
set(SEVENTHSLIDE_APP_SOURCES
    session_facade.cpp
    stack_controller.cpp
    profile_controller.cpp
    library_registry_controller.cpp
    output_settings_controller.cpp
    playback_controller.cpp
    mixer_controller.cpp
    mixer_channel_model.cpp
    audio_meter_item.cpp
    video_with_sound.cpp
    session_facade_present.cpp
    library_controller.cpp
    shipped_bibles.cpp
    live_controller.cpp
    side_view.cpp
    session_facade_outputs.cpp
    theme_controller.cpp
    destination_rows.cpp
    lan_addresses.cpp
    destinations_controller.cpp
    slide_edit_session.cpp
    slide_objects_canvas.cpp
    background_theme_copier.cpp
    freeform_library_controller.cpp
    freeform_slide_render.cpp
    session_facade_freeform_convert.cpp
    media_controller.cpp
    font_store.cpp
    font_controller.cpp
    ui_font.cpp
    media_kind_qml.hpp
    folder_browser.cpp
    folder_library.cpp
    media_library.cpp
    file_importer.cpp
    tool_jobs.cpp
    tools_controller.cpp
    convert_text.cpp
    convert_controller.cpp
    export_controller.cpp
    freeform_import_controller.cpp
    ytdlp_updater.cpp
    app_updater.cpp
    download_controller.cpp
    service_item_model.cpp
    service_list_model.cpp
    service_browser.cpp
    service_thumbs.cpp
    service_thumb_provider.cpp
    dock_layout.cpp
    glyphs.cpp
    song_list_model.cpp
    library_models.cpp
    output_list_model.cpp
    output_preview_controller.cpp
    display_list_model.cpp
    output_preview_item.cpp
    build_preview_item.cpp
    output_preview_provider.cpp
    session_bound_item.cpp
    live_frame_item.cpp
    live_video_item.cpp
    web_page_item.cpp
    live_web_item.cpp
    live_source_item.cpp
    live_thumb_item.cpp
    theme_preview_provider.cpp
    media_thumb_provider.cpp
    media_thumb_cache.cpp
    video_frame.cpp
    web_library_controller.cpp
    source_routing.cpp
    web_sources.cpp
    web_thumbs.cpp
    pointer_confinement.cpp
    live_receiver.hpp
    live_receivers.cpp
    ffmpeg_live_input.cpp
    live_sources.cpp
    live_library_controller.cpp
    live_source_list_model.cpp
    web_renderer.hpp
    freeform_thumb_provider.cpp
    icon_image_provider.cpp
    image_providers.cpp
    freeform_thumb_cache.cpp
    theme_thumb_cache.cpp
    line_map_model.cpp
    freeform_editor_models.cpp
    canvas_ops_qml.hpp
    canvas_interact.cpp
    canvas_rich_edit.cpp
    freeform_picture_item.cpp
    freeform_shape_item.cpp
    overlay_text_item.cpp
    verse_editor.cpp
    text_edit_tools.cpp
    css_color_qml.cpp
    open_library.cpp
    preview_resources.cpp
    song_text.cpp
    author_list_model.cpp
    clipboard.cpp
    platform_qml.cpp
    search_text_qml.cpp
    qr_code.cpp
    qvariant_json.cpp
    session_remote_bridge.cpp
    remote_controller.cpp
    sync_controller.cpp
    collaboration_log.cpp
    sync_admin.cpp
)

# Brand art ships with the packaging files.
set(SEVENTHSLIDE_ICONS_DIR "${CMAKE_SOURCE_DIR}/packaging/icons")
set(SEVENTHSLIDE_ICON_PNG "${SEVENTHSLIDE_ICONS_DIR}/seventhslide-icon.png")
set(SEVENTHSLIDE_ICON_ICO "${SEVENTHSLIDE_ICONS_DIR}/seventhslide.ico")
set(SEVENTHSLIDE_ICON_ICNS "${SEVENTHSLIDE_ICONS_DIR}/seventhslide.icns")

set(SEVENTHSLIDE_APP_EXTRA_SOURCES)
if(APPLE AND EXISTS "${SEVENTHSLIDE_ICON_ICNS}")
    list(APPEND SEVENTHSLIDE_APP_EXTRA_SOURCES "${SEVENTHSLIDE_ICON_ICNS}")
    set_source_files_properties("${SEVENTHSLIDE_ICON_ICNS}"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

add_library(seventhslide_applib STATIC ${SEVENTHSLIDE_APP_SOURCES})
add_library(SeventhSlide::applib ALIAS seventhslide_applib)

# The fonts every library can take a copy of (font_store.hpp): a folder per
# family, named for it, each with its licence, which goes wherever the font goes.
set(SEVENTHSLIDE_FONTS_DIR "${CMAKE_SOURCE_DIR}/third_party/fonts")
file(GLOB_RECURSE SEVENTHSLIDE_BUNDLED_FONTS CONFIGURE_DEPENDS
    "${SEVENTHSLIDE_FONTS_DIR}/*/*.ttf"
    "${SEVENTHSLIDE_FONTS_DIR}/*/*.otf"
    "${SEVENTHSLIDE_FONTS_DIR}/*/OFL.txt")
qt_add_resources(seventhslide_applib "bundled_fonts"
    PREFIX "/bundled_fonts"
    BASE "${SEVENTHSLIDE_FONTS_DIR}"
    FILES ${SEVENTHSLIDE_BUNDLED_FONTS}
    # rcc leaves a file as it is unless it shrinks by 70%, and fonts do not:
    # compiled that way these take 6 GB of memory to build rather than 2.
    OPTIONS --threshold 5 --compress 19
)

target_include_directories(seventhslide_applib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(seventhslide_applib
    PUBLIC
        SeventhSlide::core
        SeventhSlide::link
        SeventhSlide::audio
        SeventhSlide::render
        SeventhSlide::remote
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Sql
        Qt6::Network
        Qt6::Svg
)
target_link_libraries(seventhslide_applib PRIVATE seventhslide_warnings)
# A video thumbnail's frame (video_frame.cpp).
target_link_libraries(seventhslide_applib PRIVATE SeventhSlide::ffmpeg)
target_link_libraries(seventhslide_applib PRIVATE seventhslide_qrcodegen)
# The controls' style, the same on every system (ui_font.cpp).
target_link_libraries(seventhslide_applib PRIVATE Qt6::QuickControls2)
# macOS: the Live tab's cameras, listed by AVFoundation (FFmpeg's
# avfoundation opens them but cannot list them), and the system's quit
# request answered once the app has quit (mac_quit.hpp).
if(APPLE)
    target_sources(seventhslide_applib PRIVATE capture_devices_mac.mm mac_quit.mm mac_quit.hpp)
    target_link_libraries(seventhslide_applib PRIVATE
        "-framework AVFoundation" "-framework CoreMedia" "-framework Foundation")
endif()
target_compile_features(seventhslide_applib PUBLIC cxx_std_20)
if(SEVENTHSLIDE_HAS_WEBENGINE)
    target_sources(seventhslide_applib PRIVATE webengine_renderer.cpp web_page_sound.hpp)
    # QRhi (the off-screen render target) is in Qt's GuiPrivate.
    target_link_libraries(seventhslide_applib PUBLIC Qt6::WebEngineQuick
                          PRIVATE Qt6::GuiPrivate Qt6::WebChannel)
    target_compile_definitions(seventhslide_applib PUBLIC SEVENTHSLIDE_HAS_WEBENGINE=1)
endif()

# The pointer kept on a web page worked from the Controller: each windowing
# system's own way (pointer_confinement_native.hpp), where Qt's platform
# window (GuiPrivate) and that system's library are at hand.
set(SEVENTHSLIDE_POINTER_CONFINEMENT "")
if(TARGET Qt6::GuiPrivate)
    if(QT_FEATURE_wayland AND SEVENTHSLIDE_WAYLAND_CONFINEMENT_DEPS)
        set(_constraints_dir "${CMAKE_CURRENT_BINARY_DIR}/wayland")
        set(_constraints_h "${_constraints_dir}/pointer-constraints-unstable-v1-client-protocol.h")
        set(_constraints_c "${_constraints_dir}/pointer-constraints-unstable-v1-protocol.c")
        add_custom_command(
            OUTPUT "${_constraints_h}" "${_constraints_c}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_constraints_dir}"
            COMMAND "${SEVENTHSLIDE_WAYLAND_SCANNER}" client-header
                    "${SEVENTHSLIDE_POINTER_CONSTRAINTS_XML}" "${_constraints_h}"
            COMMAND "${SEVENTHSLIDE_WAYLAND_SCANNER}" private-code
                    "${SEVENTHSLIDE_POINTER_CONSTRAINTS_XML}" "${_constraints_c}"
            DEPENDS "${SEVENTHSLIDE_POINTER_CONSTRAINTS_XML}"
            COMMENT "Generating the Wayland pointer-constraints protocol"
            VERBATIM)
        # The protocol's interface tables are generated C, built on their
        # own: the project's warnings are C++'s.
        enable_language(C)
        add_library(seventhslide_wayland_protocols STATIC "${_constraints_h}" "${_constraints_c}")
        target_include_directories(seventhslide_wayland_protocols PUBLIC "${_constraints_dir}")
        target_link_libraries(seventhslide_wayland_protocols PUBLIC PkgConfig::WAYLAND_CLIENT)
        target_sources(seventhslide_applib PRIVATE pointer_confinement_wayland.cpp)
        target_link_libraries(seventhslide_applib PRIVATE seventhslide_wayland_protocols)
        target_compile_definitions(seventhslide_applib PRIVATE SEVENTHSLIDE_HAS_WAYLAND_CONFINEMENT=1)
        list(APPEND SEVENTHSLIDE_POINTER_CONFINEMENT "Wayland")
    endif()
    if(QT_FEATURE_xcb AND XFIXES_FOUND)
        target_sources(seventhslide_applib PRIVATE pointer_confinement_x11.cpp)
        target_link_libraries(seventhslide_applib PRIVATE PkgConfig::XFIXES)
        target_compile_definitions(seventhslide_applib PRIVATE SEVENTHSLIDE_HAS_X11_CONFINEMENT=1)
        list(APPEND SEVENTHSLIDE_POINTER_CONFINEMENT "X11")
    endif()
    if(WIN32)
        target_sources(seventhslide_applib PRIVATE pointer_confinement_windows.cpp)
        target_compile_definitions(seventhslide_applib PRIVATE SEVENTHSLIDE_HAS_WINDOWS_CONFINEMENT=1)
        list(APPEND SEVENTHSLIDE_POINTER_CONFINEMENT "Windows")
    endif()
endif()
if(SEVENTHSLIDE_POINTER_CONFINEMENT)
    target_sources(seventhslide_applib PRIVATE pointer_confinement_native.cpp)
    target_link_libraries(seventhslide_applib PRIVATE Qt6::GuiPrivate)
    target_compile_definitions(seventhslide_applib PUBLIC SEVENTHSLIDE_HAS_NATIVE_POINTER_CONFINEMENT=1)
    list(JOIN SEVENTHSLIDE_POINTER_CONFINEMENT ", " _confinement)
    message(STATUS "SeventhSlide: pointer confinement on ${_confinement}")
else()
    message(STATUS "SeventhSlide: no pointer confinement (a worked web page cannot keep the pointer)")
endif()

# The C++ item types the QML uses declare themselves with QML_ELEMENT, so
# qmllint and the QML compiler see them. The SeventhSlide module imports this
# one, so its files use them unqualified.
qt_add_qml_module(seventhslide_applib
    URI SeventhSlide.Native
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/SeventhSlide/Native"
)

qt_add_executable(seventhslide
    main.cpp
    ${SEVENTHSLIDE_APP_EXTRA_SOURCES}
)

# The Bibles a library takes a copy of (shipped_bibles.hpp): the program's own,
# not applib's, so a test's library takes none. BIG_RESOURCES puts them in an
# object file as they are, not in C++ source the compiler has to read.
file(GLOB SEVENTHSLIDE_BUNDLED_BIBLES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/third_party/bibles/*.xml")
qt_add_resources(seventhslide "bundled_bibles"
    PREFIX "/bundled_bibles"
    BASE "${CMAKE_SOURCE_DIR}/third_party/bibles"
    BIG_RESOURCES
    FILES ${SEVENTHSLIDE_BUNDLED_BIBLES}
)


# Embed the square PNG so window / taskbar icons work without install.
if(EXISTS "${SEVENTHSLIDE_ICON_PNG}")
    qt_add_resources(seventhslide "app_icon"
        PREFIX "/icons"
        BASE "${SEVENTHSLIDE_ICONS_DIR}"
        FILES "${SEVENTHSLIDE_ICON_PNG}"
    )
else()
    message(WARNING "App icon not found at ${SEVENTHSLIDE_ICON_PNG}")
endif()

target_include_directories(seventhslide
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(seventhslide
    PRIVATE
        SeventhSlide::applib
        SeventhSlide::server
        SeventhSlide::core
        SeventhSlide::render
        SeventhSlide::remote
        SeventhSlide::ui
        seventhslide_uiplugin
        seventhslide_applibplugin
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickLayouts
        Qt6::QuickDialogs2
        Qt6::Sql
        Qt6::Network
        Qt6::Svg
)
target_link_libraries(seventhslide PRIVATE seventhslide_warnings)

# Ensure static QML plugin registration for loadFromModule.
qt_import_qml_plugins(seventhslide)

set_target_properties(seventhslide PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

seventhslide_windows_executable(seventhslide ICON "${SEVENTHSLIDE_ICON_ICO}")

# macOS: the bundle's Info.plist, with what the app must ask the person for
# (camera, microphone, local network) - see packaging/macos/Info.plist.in.
if(APPLE)
    set_target_properties(seventhslide PROPERTIES
        # SeventhSlide.app, as Finder and the Dock show it.
        OUTPUT_NAME "SeventhSlide"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.seventhslide.app"
        MACOSX_BUNDLE_BUNDLE_NAME "SeventhSlide"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    if(EXISTS "${SEVENTHSLIDE_ICON_ICNS}")
        set_target_properties(seventhslide PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "seventhslide.icns"
        )
    endif()

    # `cmake --build build --target macos_app`: a SeventhSlide.app that runs
    # on a Mac without Homebrew, and a disk image of it, in build/deploy.
    # macdeployqt copies in Qt (its QML modules, its plugins: the TLS backend
    # sync needs among them) and every Homebrew library (OpenSSL, mpv,
    # FFmpeg); finish_bundle.sh points WebEngine's helper at the copies and
    # drops plugins of modules the app does not use, which came without their
    # frameworks. Not signed with a Developer ID, so not notarized: the copy
    # changes the binaries, and an Apple Silicon Mac runs only signed code, so
    # it is signed ad hoc (no identity). See docs/BUILDING.md, "macOS".
    get_target_property(_ss_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_ss_qt_bin "${_ss_qmake}" DIRECTORY)
    find_program(SEVENTHSLIDE_MACDEPLOYQT macdeployqt HINTS "${_ss_qt_bin}")
    if(SEVENTHSLIDE_MACDEPLOYQT)
        set(_ss_deploy "${CMAKE_BINARY_DIR}/deploy")
        set(_ss_app "${_ss_deploy}/SeventhSlide.app")
        add_custom_target(macos_app
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${_ss_app}" "${_ss_deploy}/SeventhSlide.dmg"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_ss_deploy}"
            COMMAND ditto "$<TARGET_BUNDLE_DIR:seventhslide>" "${_ss_app}"
            COMMAND "${SEVENTHSLIDE_MACDEPLOYQT}" "${_ss_app}"
                    "-qmldir=${CMAKE_SOURCE_DIR}/src/ui" "-qmldir=${CMAKE_SOURCE_DIR}/src/app"
                    -always-overwrite
            COMMAND sh "${CMAKE_SOURCE_DIR}/packaging/macos/finish_bundle.sh" "${_ss_app}"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/LICENSE"
                    "${CMAKE_SOURCE_DIR}/NOTICE.md" "${_ss_app}/Contents/Resources"
            COMMAND codesign --force --deep --sign - "${_ss_app}"
            COMMAND hdiutil create -quiet -volname SeventhSlide -srcfolder "${_ss_app}"
                    -ov -format UDZO "${_ss_deploy}/SeventhSlide.dmg"
            DEPENDS seventhslide
            COMMENT "SeventhSlide.app and SeventhSlide.dmg in ${_ss_deploy}"
            VERBATIM
        )
    endif()
endif()
