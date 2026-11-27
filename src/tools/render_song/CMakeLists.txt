cmake_minimum_required(VERSION 3.21)

add_executable(seventhslide_render_song
    main.cpp
)

target_link_libraries(seventhslide_render_song
    PRIVATE
        SeventhSlide::core
        SeventhSlide::devsupport
        SeventhSlide::render
        Qt6::Core
        Qt6::Gui
)
target_link_libraries(seventhslide_render_song PRIVATE seventhslide_warnings)

target_compile_features(seventhslide_render_song PRIVATE cxx_std_20)
target_compile_definitions(seventhslide_render_song PRIVATE
    SEVENTHSLIDE_FONTS_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts")

set_target_properties(seventhslide_render_song PROPERTIES
    OUTPUT_NAME seventhslide-render-song
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Windows: the manifest, so paths read as the app reads them.
seventhslide_windows_executable(seventhslide_render_song)
