# miniaudio, vendored unmodified (README.md): the Listen device and, later,
# the sound devices outputs play on. Built without the project's warning
# flags, and included as SYSTEM so its header raises none in ours. Only device
# I/O is compiled; SeventhSlide decodes with FFmpeg and mixes itself.
add_library(seventhslide_miniaudio STATIC miniaudio.cpp)
target_include_directories(seventhslide_miniaudio SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(seventhslide_miniaudio PUBLIC
    MA_NO_DECODING
    MA_NO_ENCODING
    MA_NO_GENERATION
    MA_NO_RESOURCE_MANAGER
    MA_NO_NODE_GRAPH
    MA_NO_ENGINE
)
if(NOT MSVC)
    target_compile_options(seventhslide_miniaudio PRIVATE -w)
endif()
# Backends (PulseAudio, ALSA, JACK, ...) are dlopen'd at run time; nothing
# else is linked.
find_package(Threads REQUIRED)
target_link_libraries(seventhslide_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(seventhslide_miniaudio PUBLIC m)
endif()
# macOS: Core Audio linked, not dlopen'd. miniaudio's own docs say the
# dlopen of system frameworks can fail notarization and the App Store, and
# linking costs nothing: they are on every Mac.
if(APPLE)
    target_compile_definitions(seventhslide_miniaudio PUBLIC MA_NO_RUNTIME_LINKING)
    target_link_libraries(seventhslide_miniaudio PUBLIC
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()
set_target_properties(seventhslide_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF)
target_compile_features(seventhslide_miniaudio PUBLIC cxx_std_20)
