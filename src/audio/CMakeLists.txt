# seventhslide_audio - the Audio Mixer's engine: files decoded by FFmpeg,
# mixed per stack and per output, metered, and played on the Listen device
# (miniaudio). Plain C++20: no Qt in its code, so it is tested headless.
# Depends on core only; render and app link it.

add_library(seventhslide_audio STATIC
    audio_decoder.cpp
    audio_engine.cpp
    audio_file_player.cpp
    device_sink.cpp
    drift_buffer.cpp
    ma_devices.cpp
    mixer.cpp
    pipewire_source.cpp
    stream_source.cpp
    audio_decoder.hpp
    ma_devices.hpp
    include/seventhslide/audio/audio_engine.hpp
    include/seventhslide/audio/audio_file_player.hpp
    include/seventhslide/audio/audio_format.hpp
    include/seventhslide/audio/audio_source.hpp
    include/seventhslide/audio/device_sink.hpp
    include/seventhslide/audio/drift_buffer.hpp
    include/seventhslide/audio/frame_ring.hpp
    include/seventhslide/audio/mixer.hpp
    include/seventhslide/audio/mixer_link.hpp
    include/seventhslide/audio/pipewire_source.hpp
    include/seventhslide/audio/stream_source.hpp
)

add_library(SeventhSlide::audio ALIAS seventhslide_audio)

target_include_directories(seventhslide_audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(seventhslide_audio
    PUBLIC
        SeventhSlide::core
    PRIVATE
        SeventhSlide::ffmpeg
        seventhslide_miniaudio
        seventhslide_warnings
)

if(PIPEWIRE_FOUND)
    target_link_libraries(seventhslide_audio PRIVATE PkgConfig::PIPEWIRE)
    target_compile_definitions(seventhslide_audio PRIVATE SEVENTHSLIDE_WITH_PIPEWIRE=1)
endif()

target_compile_features(seventhslide_audio PUBLIC cxx_std_20)

set_target_properties(seventhslide_audio PROPERTIES
    OUTPUT_NAME seventhslide_audio
    POSITION_INDEPENDENT_CODE ON
    # No Qt here, so nothing for moc.
    AUTOMOC OFF
)
