include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        ${SEVENTHSLIDE_CATCH2_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Key presses in UI tests go through QTest so they reach window shortcuts the
# way a real key does. Only the tests need it.
find_package(Qt6 REQUIRED COMPONENTS Test)

# Windows: the program a test's stand-in tool is (fake_tool.hpp), as Windows
# runs no sh script as one.
if(WIN32)
    add_executable(seventhslide_fake_tool fake_tool.cpp)
    if(MINGW)
        target_link_options(seventhslide_fake_tool PRIVATE -municode)
    endif()
endif()

include(Catch)
# Windows finds a program's DLLs on PATH, and none of Qt's, FFmpeg's, mpv's or
# OpenSSL's are beside the tests: their folders go on PATH to list the tests
# and to run each (0xc0000135 otherwise). Add others here.
if(WIN32)
    set(_ss_dll_dirs "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    foreach(_ss_root IN ITEMS "${SEVENTHSLIDE_FFMPEG_ROOT}" "${SEVENTHSLIDE_MPV_ROOT}"
                              "${OPENSSL_ROOT_DIR}")
        if(_ss_root)
            list(APPEND _ss_dll_dirs "${_ss_root}/bin")
        endif()
    endforeach()
    foreach(_ss_lib IN ITEMS SEVENTHSLIDE_avcodec_LIBRARY SEVENTHSLIDE_mpv_LIBRARY)
        if(${_ss_lib})
            get_filename_component(_ss_lib_dir "${${_ss_lib}}" DIRECTORY)
            list(APPEND _ss_dll_dirs "${_ss_lib_dir}" "${_ss_lib_dir}/../bin")
        endif()
    endforeach()
    set(SEVENTHSLIDE_TEST_DLL_PATHS "${_ss_dll_dirs}" CACHE STRING
        "Folders the tests' DLLs are in, put on PATH when ctest runs them (Windows)")
    if(CMAKE_VERSION VERSION_LESS 3.22)
        message(WARNING "CMake 3.22 or later puts the DLLs' folders on PATH for ctest; "
            "put them on PATH yourself before running it.")
    endif()
endif()

# The tests are a program per layer, each linking only the libraries its
# tests use, so a change to one area rebuilds and relinks only the programs
# above it. Each test case is a ctest test labelled with its program: `ctest`
# runs them all, `ctest -L sync` one program's. What no ctest run needs (the
# benchmarks, the screenshots, the deck check) is seventhslide_tests_dev,
# built only when asked for.

# What a program without the app has: the Qt application's bootstrap
# (test_qt_app.hpp), in Qt's own look.
add_library(seventhslide_test_base STATIC qt_plain_look.cpp)
target_link_libraries(seventhslide_test_base
    PUBLIC Catch2::Catch2 Qt6::Gui Qt6::Test
    PRIVATE seventhslide_warnings)
target_compile_features(seventhslide_test_base PRIVATE cxx_std_20)

# What a program with the app has: the bootstrap in the app's look, and the
# sample library (sample_library.hpp).
add_library(seventhslide_test_support STATIC qt_app_look.cpp sample_library.cpp)
target_link_libraries(seventhslide_test_support
    PUBLIC SeventhSlide::applib Catch2::Catch2 Qt6::Test
    PRIVATE seventhslide_warnings)
target_compile_features(seventhslide_test_support PRIVATE cxx_std_20)
target_compile_definitions(seventhslide_test_support
    PRIVATE SEVENTHSLIDE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures")

# The program seventhslide_tests_<name>: test_main.cpp and the files after
# SOURCES, linking the libraries after LIBS. APP: it has the app (applib, and
# the sample library). QML: it loads the QML module as the app does, and has
# the app. DEV: built only when asked for, and not a ctest test.
function(seventhslide_add_tests name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "APP;QML;DEV" "" "LIBS;SOURCES")
    set(target seventhslide_tests_${name})
    add_executable(${target} test_main.cpp ${ARG_SOURCES})
    if(ARG_APP OR ARG_QML)
        target_link_libraries(${target} PRIVATE seventhslide_test_support SeventhSlide::applib)
    else()
        target_link_libraries(${target} PRIVATE seventhslide_test_base)
    endif()
    target_link_libraries(${target}
        PRIVATE
            ${ARG_LIBS}
            SeventhSlide::core
            Catch2::Catch2
            Qt6::Core
            Qt6::Gui
            Qt6::Sql
            Qt6::Test
            seventhslide_warnings
    )
    if(ARG_QML)
        target_link_libraries(${target}
            PRIVATE SeventhSlide::ui seventhslide_uiplugin seventhslide_applibplugin)
        qt_import_qml_plugins(${target})
    endif()
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target}
        PRIVATE
            SEVENTHSLIDE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/fixtures"
            SEVENTHSLIDE_UI_QML_DIR="${CMAKE_SOURCE_DIR}/src/ui"
    )
    if(WIN32)
        add_dependencies(${target} seventhslide_fake_tool)
        target_compile_definitions(${target}
            PRIVATE SEVENTHSLIDE_FAKE_TOOL="$<TARGET_FILE:seventhslide_fake_tool>")
    endif()
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    # Windows: the manifest, so paths read as the app reads them.
    seventhslide_windows_executable(${target})
    if(ARG_DEV)
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        return()
    endif()
    # PRE_TEST avoids running the binary at build time (conda Qt + system
    # libmpv can fail OpenSSL symbol checks during CatchAddTests discovery).
    if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.22)
        catch_discover_tests(${target} DISCOVERY_MODE PRE_TEST
            DL_PATHS ${SEVENTHSLIDE_TEST_DLL_PATHS} PROPERTIES LABELS ${name})
    else()
        catch_discover_tests(${target} DISCOVERY_MODE PRE_TEST PROPERTIES LABELS ${name})
    endif()
endfunction()

# util, model, parse, layout, canvas, freeform, theme, db, build, scene and
# session.
seventhslide_add_tests(core LIBS SeventhSlide::devsupport SOURCES
        test_app_settings.cpp
        test_atomic_file.cpp
        test_audio_levels.cpp
        test_audio_mix_store.cpp
        test_author.cpp
        test_auto_advance.cpp
        test_background_fill.cpp
        test_bible_fit.cpp
        test_bible_fit_audit.cpp
        test_bible_layout.cpp
        test_bible_lookup.cpp
        test_bible_parse.cpp
        test_canvas_editor.cpp
        test_canvas_interact.cpp
        test_canvas_layout.cpp
        test_canvas_mapping.cpp
        test_canvas_presets.cpp
        test_chords.cpp
        test_chrome_text.cpp
        test_content.cpp
        test_console_store.cpp
        test_convert_schema_14.cpp
        test_convert_schema_15.cpp
        test_core_db.cpp
        test_destination_store.cpp
        test_edit_history.cpp
        test_element_projection.cpp
        test_even_wrap.cpp
        test_file_copy.cpp
        test_folder_tree.cpp
        test_font_file.cpp
        test_font_registry.cpp
        test_freeform_chords.cpp
        test_freeform_compile.cpp
        test_freeform_convert.cpp
        test_freeform_document.cpp
        test_freeform_files.cpp
        test_freeform_editor.cpp
        test_freeform_paste.cpp
        test_freeform_slide_copy.cpp
        test_hash.cpp
        test_json_read.cpp
        test_library_db.cpp
        test_library_registry.cpp
        test_lyrics_blocks.cpp
        test_media_listing.cpp
        test_object_properties.cpp
        test_output_config.cpp
        test_output_display.cpp
        test_output_library.cpp
        test_overlay_layout.cpp
        test_overlays.cpp
        test_paint_order.cpp
        test_paths.cpp
        test_player_controller.cpp
        test_reorder.cpp
        test_rich_text.cpp
        test_scene_builder.cpp
        test_scene_dump.cpp
        test_scene_frame.cpp
        test_schema.cpp
        test_session.cpp
        test_session_preview.cpp
        test_slide_builder.cpp
        test_song_chord_rows.cpp
        test_song_import.cpp
        test_song_layout.cpp
        test_song_theme_pack.cpp
        test_song_xml.cpp
        test_stack.cpp
        test_strings.cpp
        test_style_profiles.cpp
        test_text_measurer.cpp
        test_theme_draft_preview.cpp
        test_theme_editor.cpp
        test_theme_elements.cpp
        test_theme_keys.cpp
        test_theme_library.cpp
        test_theme_picks.cpp
        test_theme_resolver.cpp
        test_title_slides.cpp
        test_tools_locate.cpp
        test_transition.cpp
        test_version.cpp
        test_word_wrap.cpp
        test_zip.cpp
)

# render, audio and stream: painters, outputs, sinks, players. The stream
# tests' receiver reads through FFmpeg, as OBS would; test_render_smoke.cpp
# draws as the render_smoke tool does.
seventhslide_add_tests(media
    LIBS SeventhSlide::render SeventhSlide::audio SeventhSlide::stream SeventhSlide::devsupport
         SeventhSlide::ffmpeg
    SOURCES
        test_audio_engine.cpp
        test_background_image_cache.cpp
        test_background_painter.cpp
        test_bmd_input.cpp
        test_bmd_output.cpp
        test_bmd_runtime.cpp
        test_builds.cpp
        test_canvas_fit.cpp
        test_effects_painter.cpp
        test_element_order.cpp
        test_element_raster_cache.cpp
        test_fade.cpp
        test_fills_tables_columns.cpp
        test_image_painter.cpp
        test_mixer_plan.cpp
        test_ndi_output.cpp
        test_ndi_receive.cpp
        test_ndi_runtime.cpp
        test_offscreen_fade.cpp
        test_picture_texture.cpp
        test_pixel_pack.cpp
        test_presenter_view.cpp
        test_render_smoke.cpp
        test_song_fit_audit.cpp
        test_stream.cpp
        test_stream_source.cpp
        test_text_painter.cpp
        test_video_painter.cpp
        test_video_player.cpp
        test_virtual_camera.cpp
        test_web_painter.cpp
)
target_include_directories(seventhslide_tests_media PRIVATE ${CMAKE_SOURCE_DIR}/src/tools/render_smoke)
# test_picture_texture.cpp drives the window's texture through QRhi, in Qt's
# GuiPrivate.
if(TARGET Qt6::GuiPrivate)
    target_link_libraries(seventhslide_tests_media PRIVATE Qt6::GuiPrivate)
endif()

# The app's controllers, models and SessionFacade, without QML, and its
# outputs' destinations and inputs. test_qr_code.cpp reads the vendored
# encoder directly; the SRT tests' receiver reads through FFmpeg.
seventhslide_add_tests(app APP
    LIBS SeventhSlide::devsupport SeventhSlide::ffmpeg seventhslide_qrcodegen
    SOURCES
        test_app_list_models.cpp
        test_author_list_model.cpp
        test_convert_controller.cpp
        test_destination_format.cpp
        test_dock_layout.cpp
        test_download_controller.cpp
        test_error_text.cpp
        test_export.cpp
        test_facade_freeform.cpp
        test_freeform_files_app.cpp
        test_facade_library.cpp
        test_facade_media.cpp
        test_facade_mixer.cpp
        test_facade_playback.cpp
        test_facade_preview.cpp
        test_facade_themes.cpp
        test_ffmpeg_live_input.cpp
        test_folder_browser.cpp
        test_folder_libraries.cpp
        test_foldered_rows.cpp
        test_font_controller.cpp
        test_font_store.cpp
        test_freeform_edit_refresh.cpp
        test_freeform_editor_models.cpp
        test_gui_thread.cpp
        test_library_models.cpp
        test_live_source_list_model.cpp
        test_live_sources.cpp
        test_media_convert.cpp
        test_media_import.cpp
        test_media_references.cpp
        test_output_window_host.cpp
        test_platform_qml.cpp
        test_pointer_confinement.cpp
        test_preview_jobs.cpp
        test_qr_code.cpp
        test_qt_listener.cpp
        test_qvariant_json.cpp
        test_rundown_edits.cpp
        test_search_text.cpp
        test_service_browser.cpp
        test_service_order.cpp
        test_session_facade.cpp
        test_shape_path.cpp
        test_shipped_bibles.cpp
        test_slide_edit_session.cpp
        test_song_list_model.cpp
        test_song_text.cpp
        test_srt_destination.cpp
        test_text_offsets.cpp
        test_theme_assets.cpp
        test_throttle.cpp
        test_thumb_caches.cpp
        test_tool_jobs.cpp
        test_verse_editor.cpp
        test_video_with_sound.cpp
        test_web_pages.cpp
        test_web_sources.cpp
        test_web_thumbs.cpp
        test_webengine.cpp
        test_ytdlp.cpp
        test_ytdlp_updater.cpp
)

# The web remote's server and bridge, and the library its browser tests run
# against (remote_web_fixture.cpp).
seventhslide_add_tests(remote APP LIBS SeventhSlide::devsupport SOURCES
        remote_web_fixture.cpp
        test_remote_auth.cpp
        test_remote_http_util.cpp
        test_remote_protocol.cpp
        test_remote_server.cpp
        test_remote_thumbs.cpp
        test_session_remote_bridge.cpp
)

# The sync engine, its connections and the server.
seventhslide_add_tests(sync
    LIBS SeventhSlide::link SeventhSlide::server ftxui::component Qt6::Network
    SOURCES
        test_cloud_folder.cpp
        test_computer_key.cpp
        test_crypto.cpp
        test_discovery.cpp
        test_lan.cpp
        test_library_sync.cpp
        test_link.cpp
        test_machine_store.cpp
        test_routes.cpp
        test_server.cpp
        test_server_config.cpp
        test_shared_log.cpp
        test_snapshot_parts.cpp
        test_sync_engine.cpp
        test_sync_members.cpp
        test_sync_outbox.cpp
        test_sync_records.cpp
        test_sync_tables.cpp
)
# test_server_config.cpp drives the settings' form (FTXUI); its headers are
# not held to this project's warnings.
FetchContent_GetProperties(ftxui)
target_include_directories(seventhslide_tests_sync SYSTEM PRIVATE ${ftxui_SOURCE_DIR}/include)
# The server tests run the program itself as a server, when it is built.
if(TARGET seventhslide)
    add_dependencies(seventhslide_tests_sync seventhslide)
    target_compile_definitions(seventhslide_tests_sync
        PRIVATE SEVENTHSLIDE_APP_PROGRAM="$<TARGET_FILE:seventhslide>")
endif()

# The QML: views, dialogs, editors, and the window with sync on.
seventhslide_add_tests(ui QML SOURCES
        test_color_picker.cpp
        test_convert_dialog.cpp
        test_dialog_field.cpp
        test_icon_provider.cpp
        test_modal_scrim.cpp
        test_reorder_js.cpp
        test_rich_edit_bridge.cpp
        test_slide_editor_ui.cpp
        test_sync_app.cpp
        test_theme_gallery.cpp
        test_theme_gallery_view.cpp
        test_theme_map_editor.cpp
        test_ui_destinations.cpp
        test_ui_dock.cpp
        test_ui_library_tools.cpp
        test_ui_loads.cpp
        test_ui_mixer.cpp
        test_ui_output_general.cpp
        test_ui_selection.cpp
        test_ui_unified_controls.cpp
        test_ui_view_reads.cpp
)

# Run by hand: the benchmarks ("[.bench]", "[.startup]"), the screenshots
# ("[.shots]") and the deck check ("[.deck-check]").
seventhslide_add_tests(dev QML DEV LIBS SeventhSlide::devsupport Qt6::Svg SOURCES
        bench_hot_paths.cpp
        bench_present.cpp
        bench_presenter_view.cpp
        bench_startup.cpp
        deck_check.cpp
        ui_shots.cpp
)
# The view's texture benchmark goes through QRhi, in Qt's GuiPrivate.
if(TARGET Qt6::GuiPrivate)
    target_link_libraries(seventhslide_tests_dev PRIVATE Qt6::GuiPrivate)
endif()

# The memory joining and catching up take (bench_sync_memory.cpp): a program
# of its own, with only core in it, so little else counts.
add_executable(seventhslide_bench_sync_memory bench_sync_memory.cpp)
target_link_libraries(seventhslide_bench_sync_memory PRIVATE SeventhSlide::core Catch2::Catch2WithMain Qt6::Core
                                                             Qt6::Sql seventhslide_warnings)
target_compile_features(seventhslide_bench_sync_memory PRIVATE cxx_std_20)
set_target_properties(seventhslide_bench_sync_memory PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# The web remote in a real browser (tests/remote_web, see its README): the app
# and seventhslide_tests_remote, driven by Playwright. Skipped where Node or the
# Playwright packages are not installed. It runs its own workers, so alone;
# `ctest -LE browser` leaves it out.
find_program(SEVENTHSLIDE_NODE node)
if(SEVENTHSLIDE_NODE)
    # The programs by their real paths: an .exe, a macOS bundle, a
    # multi-config generator's bin/<Config>/.
    set(_ss_web_programs SEVENTHSLIDE_TESTS_BINARY=$<TARGET_FILE:seventhslide_tests_remote>)
    if(TARGET seventhslide)
        list(APPEND _ss_web_programs SEVENTHSLIDE_APP_BINARY=$<TARGET_FILE:seventhslide>)
    endif()
    add_test(NAME remote_web_browser
        COMMAND ${CMAKE_COMMAND} -E env SEVENTHSLIDE_BUILD_DIR=${CMAKE_BINARY_DIR}
                ${_ss_web_programs}
                ${SEVENTHSLIDE_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/remote_web/run.mjs
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/remote_web)
    set_tests_properties(remote_web_browser PROPERTIES
        SKIP_RETURN_CODE 77
        RUN_SERIAL TRUE
        TIMEOUT 900
        LABELS browser)
    # ESLint over the bundled client and the browser tests; skipped where it
    # is not installed (the same npm ci).
    add_test(NAME remote_web_lint
        COMMAND ${SEVENTHSLIDE_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/remote_web/lint.mjs
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/remote_web)
    set_tests_properties(remote_web_lint PROPERTIES SKIP_RETURN_CODE 77)
endif()
