# seventhslide_stream - live streams sent out over the network: pictures and
# sound encoded with FFmpeg (H.264 / H.265 and AAC, hardware encoders first)
# and sent as MPEG-TS over SRT. Plain C++20: no Qt in its code, so it is
# tested headless. Depends on core and audio (the sound comes from the mixer
# through a DriftBuffer); render links it.

add_library(seventhslide_stream STATIC
    audio_encoder.cpp
    encoder_catalog.cpp
    ffmpeg_util.cpp
    srt_streamer.cpp
    stream_settings.cpp
    video_encoder.cpp
    audio_encoder.hpp
    ffmpeg_util.hpp
    video_encoder.hpp
    include/seventhslide/stream/encoder_catalog.hpp
    include/seventhslide/stream/srt_streamer.hpp
    include/seventhslide/stream/stream_settings.hpp
)

add_library(SeventhSlide::stream ALIAS seventhslide_stream)

target_include_directories(seventhslide_stream
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(seventhslide_stream
    PUBLIC
        SeventhSlide::core
        SeventhSlide::audio
    PRIVATE
        SeventhSlide::ffmpeg
        seventhslide_warnings
)

target_compile_features(seventhslide_stream PUBLIC cxx_std_20)

set_target_properties(seventhslide_stream PROPERTIES
    OUTPUT_NAME seventhslide_stream
    POSITION_INDEPENDENT_CODE ON
    # No Qt here, so nothing for moc.
    AUTOMOC OFF
)
