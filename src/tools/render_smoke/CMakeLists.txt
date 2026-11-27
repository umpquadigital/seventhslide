cmake_minimum_required(VERSION 3.21)

add_executable(seventhslide_render_smoke
    main.cpp
)

target_include_directories(seventhslide_render_smoke
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(seventhslide_render_smoke
    PRIVATE
        SeventhSlide::core
        SeventhSlide::devsupport
        SeventhSlide::render
        Qt6::Core
        Qt6::Gui
)
target_link_libraries(seventhslide_render_smoke PRIVATE seventhslide_warnings)

target_compile_features(seventhslide_render_smoke PRIVATE cxx_std_20)

set_target_properties(seventhslide_render_smoke PROPERTIES
    OUTPUT_NAME seventhslide-render-smoke
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Windows: the manifest, so paths read as the app reads them.
seventhslide_windows_executable(seventhslide_render_smoke)
