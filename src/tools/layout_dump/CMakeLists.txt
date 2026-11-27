cmake_minimum_required(VERSION 3.21)

add_executable(seventhslide_layout_dump
    main.cpp
)

target_link_libraries(seventhslide_layout_dump
    PRIVATE
        SeventhSlide::core
        SeventhSlide::devsupport
        Qt6::Core
)
target_link_libraries(seventhslide_layout_dump PRIVATE seventhslide_warnings)

target_compile_features(seventhslide_layout_dump PRIVATE cxx_std_20)

set_target_properties(seventhslide_layout_dump PROPERTIES
    OUTPUT_NAME seventhslide-layout-dump
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Windows: the manifest, so paths read as the app reads them.
seventhslide_windows_executable(seventhslide_layout_dump)
