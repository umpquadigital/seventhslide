include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        ${SEVENTHSLIDE_JSON_VERSION}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        ${SEVENTHSLIDE_PUGIXML_VERSION}
    GIT_SHALLOW    TRUE
)
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pugixml)

# seventhslide_core - UI-agnostic domain library.
# Rules (see docs/CODING.md):
#   - No QML / Qt Quick imports
#   - Qt Core/Gui only at documented boundaries
#   - No HTTP / remote serving code

add_library(seventhslide_core STATIC
    util/atomic_file.cpp
    util/crc32.cpp
    util/error_text.cpp
    util/file_copy.cpp
    util/file_name.cpp
    util/hash.cpp
    util/open_files.cpp
    util/read_file.cpp
    util/text_encoding.cpp
    util/zip_reader.cpp
    util/zip_writer.cpp

    geom/canvas_presets.cpp
    geom/shape_path.cpp

    platform/computer_name.cpp
    platform/paths.cpp
    platform/tools.cpp

    media/audio_levels.cpp
    media/convert.cpp
    media/media_player.cpp
    media/ytdlp.cpp

    crypto/encoding.cpp
    crypto/password_box.cpp
    crypto/random.cpp
    crypto/sha256.cpp
    crypto/signing.cpp

    exchange/adapt.cpp
    exchange/bundle.cpp
    exchange/bundle_io.cpp

    fonts/font_file.cpp
    fonts/font_registry.cpp

    library/audio_mix_store.cpp
    library/cloud_folder.cpp
    library/computer_key.cpp
    library/console_store.cpp
    library/destination_store.cpp
    library/library.cpp
    library/machine_store.cpp

    update/releases.cpp

    model/app_settings.cpp
    model/audio_fades.cpp
    model/author.cpp
    model/content.cpp
    model/library.cpp
    model/live_source.cpp
    model/machine_settings.cpp
    model/output_config.cpp
    model/output_library.cpp
    model/rich_text.cpp
    model/rich_text_edit.cpp
    model/rich_text_import.cpp
    model/stack.cpp
    model/static_media.cpp
    model/transition.cpp
    model/web_page.cpp

    parse/bible.cpp
    parse/bible_osis.cpp
    parse/lyrics_blocks.cpp
    parse/song_chordpro.cpp
    parse/song_import.cpp
    parse/song_opensong.cpp
    parse/song_sections.cpp
    parse/song_text.cpp
    parse/song_usr.cpp
    parse/song_xml.cpp
    parse/song_xml_write.cpp
    parse/xml_safe.cpp

    canvas/interact.cpp
    canvas/layout.cpp
    canvas/object.cpp
    canvas/parts.cpp

    layout/background_fill.cpp
    layout/chords.cpp
    layout/editor_html.cpp
    layout/overlay_layout.cpp
    layout/overlays.cpp
    layout/qt_font_faces.cpp
    layout/qt_text_measurer.cpp
    layout/slide_grouper.cpp
    layout/text_measurer.cpp
    layout/word_wrap.cpp

    freeform/background_copy.cpp
    freeform/builds.cpp
    freeform/compile.cpp
    freeform/doc_cues.cpp
    freeform/doc_edit.cpp
    freeform/doc_linked.cpp
    freeform/doc_objects.cpp
    freeform/doc_paste.cpp
    freeform/doc_slides.cpp
    freeform/document.cpp
    freeform/editor.cpp
    freeform/editor_cues.cpp
    freeform/editor_objects.cpp
    freeform/editor_properties.cpp
    freeform/object_properties.cpp
    freeform/own_media.cpp
    freeform/paint.cpp
    freeform/tokens.cpp

    theme/element_editor.cpp
    theme/element_properties.cpp
    theme/resolved_theme.cpp
    theme/song_theme_pack.cpp
    theme/theme_category.cpp
    theme/theme_elements.cpp
    theme/theme_kind.cpp
    theme/theme_library.cpp
    theme/theme_picks.cpp
    theme/theme_resolver.cpp

    db/bible_lookup.cpp
    db/bible_repository.cpp
    db/config_repository.cpp
    db/database.cpp
    db/folder_tree.cpp
    db/foldered_rows.cpp
    db/font_repository.cpp
    db/freeform_repository.cpp
    db/list_order.cpp
    db/media_kind.cpp
    db/media_listing.cpp
    db/media_repository.cpp
    db/schema.cpp
    db/search_text.cpp
    db/service_repository.cpp
    db/song_repository.cpp
    db/style_profile_repository.cpp
    db/style_profiles.cpp
    db/sync_bookkeeping.cpp
    db/sync_outbox.cpp
    db/sync_records.cpp
    db/web_page_repository.cpp

    build/bible_layout.cpp
    build/even_wrap.cpp
    build/slide_builder.cpp
    build/song_layout.cpp
    build/title_slides.cpp

    sync/admin_key.cpp
    sync/change_file.cpp
    sync/change_notes.cpp
    sync/envelope.cpp
    sync/folder_store.cpp
    sync/gate.cpp
    sync/housekeeping.cpp
    sync/media_pieces.cpp
    sync/member_list.cpp
    sync/shared_log.cpp
    sync/snapshot.cpp
    sync/sync_engine.cpp
    sync/sync_engine_apply.cpp
    sync/sync_engine_catch_up.cpp
    sync/sync_engine_marks.cpp
    sync/sync_engine_media.cpp
    sync/sync_engine_merge.cpp
    sync/sync_engine_send.cpp
    sync/sync_engine_snapshot.cpp

    scene/chrome_text.cpp
    scene/freeform_convert.cpp
    scene/paint_order.cpp
    scene/scene_builder.cpp
    scene/scene_frame.cpp
    scene/text_metrics.cpp
    scene/theme_draft_preview.cpp

    session/player_controller.cpp
    session/session.cpp
    session/stack_runtime.cpp
    session/still_frames.cpp
)

add_library(SeventhSlide::core ALIAS seventhslide_core)

# The version lives once, in the project() call, with a beta's label
# (SEVENTHSLIDE_VERSION, the top-level CMakeLists.txt).
configure_file(version.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/include/seventhslide/version.hpp @ONLY)

target_include_directories(seventhslide_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(seventhslide_core
    PUBLIC
        nlohmann_json::nlohmann_json
        Qt6::Core
        Qt6::Gui
        Qt6::Sql
)
target_link_libraries(seventhslide_core PRIVATE seventhslide_warnings pugixml::pugixml OpenSSL::Crypto)
# macOS: the Computer Name (platform/computer_name.cpp).
if(APPLE)
    target_link_libraries(seventhslide_core PRIVATE
        "-framework CoreFoundation" "-framework SystemConfiguration")
endif()

target_compile_features(seventhslide_core PUBLIC cxx_std_20)

# Core stays free of Qt Quick. Documented seams: docs/CODING.md.

set_target_properties(seventhslide_core PROPERTIES
    OUTPUT_NAME seventhslide_core
    POSITION_INDEPENDENT_CODE ON
)
