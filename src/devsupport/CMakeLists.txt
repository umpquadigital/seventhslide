# seventhslide_devsupport - what tests and developer tools share and the app
# does not ship: the flat style keys fixtures describe looks with, and the
# layout dump compared against the oracle fixtures.

add_library(seventhslide_devsupport STATIC
    flat_style.cpp
    scene_dump.cpp
)

add_library(SeventhSlide::devsupport ALIAS seventhslide_devsupport)

target_include_directories(seventhslide_devsupport
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(seventhslide_devsupport PUBLIC SeventhSlide::core)
target_link_libraries(seventhslide_devsupport PRIVATE seventhslide_warnings)

target_compile_features(seventhslide_devsupport PRIVATE cxx_std_20)
