// Converting media: what ffprobe says, the ffmpeg command for each choice,
// its progress, and the new file's name. Pure, but for one run of the real
// ffmpeg where it is installed.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/media/convert.hpp>
#include <seventhslide/platform/paths.hpp>
#include <seventhslide/platform/tools.hpp>

#include <QProcess>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <variant>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace seventhslide::media;

namespace {

constexpr std::string_view k_video_json = R"({
    "streams": [
        {"codec_type": "video", "codec_name": "h264", "width": 1920, "height": 1080,
         "r_frame_rate": "30000/1001", "avg_frame_rate": "30000/1001"},
        {"codec_type": "audio", "codec_name": "aac", "sample_rate": "48000", "channels": 2}
    ],
    "format": {"duration": "12.500000", "size": "4500000", "bit_rate": "2880000"}
})";

constexpr std::string_view k_audio_json = R"({
    "streams": [
        {"codec_type": "video", "codec_name": "mjpeg", "width": 600, "height": 600,
         "disposition": {"attached_pic": 1}},
        {"codec_type": "audio", "codec_name": "mp3"}
    ],
    "format": {"duration": "200.0", "size": "3200000", "bit_rate": "128000"}
})";

constexpr std::string_view k_image_json = R"({
    "streams": [{"codec_type": "video", "codec_name": "png", "width": 800, "height": 600,
                 "avg_frame_rate": "0/0", "r_frame_rate": "25/1"}],
    "format": {"size": "120000"}
})";

MediaProbe hd_video()
{
    return *parse_probe(k_video_json);
}

bool has(const std::vector<std::string>& args, std::string_view a, std::string_view b)
{
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == a && args[i + 1] == b) {
            return true;
        }
    }
    return false;
}

bool has(const std::vector<std::string>& args, std::string_view a)
{
    return std::find(args.begin(), args.end(), a) != args.end();
}

} // namespace

TEST_CASE("ffprobe's JSON reads as a probe", "[convert]")
{
    const auto video = parse_probe(k_video_json);
    REQUIRE(video.has_value());
    CHECK(video->has_video);
    CHECK(video->has_audio);
    CHECK(video->width == 1920);
    CHECK(video->height == 1080);
    CHECK(video->fps > 29.9);
    CHECK(video->fps < 30.0);
    CHECK(video->duration_s == 12.5);
    CHECK(video->size_bytes == 4500000);
    CHECK(video->bit_rate == 2880000);
    CHECK(video->video_codec == "h264");
    CHECK(video->audio_codec == "aac");
    CHECK(video->sample_rate == 48000);
    CHECK(video->channels == 2);

    // Cover art is not a picture to convert.
    const auto audio = parse_probe(k_audio_json);
    REQUIRE(audio.has_value());
    CHECK_FALSE(audio->has_video);
    CHECK(audio->has_audio);
    CHECK(audio->duration_s == 200.0);
    CHECK(audio->audio_codec == "mp3");

    const auto image = parse_probe(k_image_json);
    REQUIRE(image.has_value());
    CHECK(image->has_video);
    CHECK_FALSE(image->has_audio);
    CHECK(image->height == 600);
    CHECK(image->duration_s == 0.0);
    // 0/0 is unknown; the nominal rate stands in.
    CHECK(image->fps == 25.0);

    CHECK_FALSE(parse_probe("not json").has_value());
    CHECK_FALSE(parse_probe("{}").has_value());
    CHECK_FALSE(parse_probe("[]").has_value());

    const auto args = ffprobe_args("/m/a.mp4");
    CHECK(args.back() == "/m/a.mp4");
    CHECK(has(args, "-print_format", "json"));
    CHECK(has(args, "-show_streams"));
}

TEST_CASE("A video's ffmpeg command follows its choices", "[convert]")
{
    const MediaProbe source = hd_video();
    VideoConvert choice;
    choice.height = 720;
    choice.format = VideoFormat::Mp4;
    choice.quality = Quality::Balanced;
    auto args = ffmpeg_args(choice, source, "/m/in.mov", "/m/.import-x.part");
    CHECK(args.front() == "-hide_banner");
    CHECK(args.back() == "/m/.import-x.part");
    CHECK(has(args, "-i", "/m/in.mov"));
    CHECK(has(args, "-progress", "pipe:1"));
    CHECK(has(args, "-y"));
    CHECK_FALSE(has(args, "-nostdin"));
    CHECK(has(args, "-vf", "scale=-2:720"));
    CHECK(has(args, "-c:v", "libx264"));
    CHECK(has(args, "-crf", "23"));
    CHECK(has(args, "-movflags", "+faststart"));
    CHECK(has(args, "-c:a", "aac"));
    CHECK_FALSE(has(args, "-an"));
    CHECK_FALSE(has(args, "-r"));

    // A preset never scales up; a size typed in is used as it is.
    choice.height = 2160;
    choice.fps = 30;
    choice.quality = Quality::Smaller;
    args = ffmpeg_args(choice, source, "/m/in.mov", "/m/out");
    CHECK_FALSE(has(args, "-vf"));
    CHECK(has(args, "-r", "30"));
    CHECK(has(args, "-crf", "28"));
    choice.exact_height = true;
    args = ffmpeg_args(choice, source, "/m/in.mov", "/m/out");
    CHECK(has(args, "-vf", "scale=-2:2160"));
    CHECK_FALSE(scaled_size(2160, source, false, true).has_value());
    CHECK(scaled_size(2160, source, true, true) == std::pair{3840, 2160});
    CHECK_FALSE(scaled_size(1080, source, true, true).has_value());
    CHECK(scaled_size(480, source, false, true) == std::pair{852, 480});
    CHECK_FALSE(scaled_size(0, source, true, true).has_value());

    choice = VideoConvert{};
    choice.format = VideoFormat::Webm;
    choice.quality = Quality::Better;
    args = ffmpeg_args(choice, source, "/m/in.mov", "/m/out");
    CHECK(has(args, "-c:v", "libvpx-vp9"));
    CHECK(has(args, "-crf", "26"));
    CHECK(has(args, "-c:a", "libopus"));

    choice.format = VideoFormat::Mov;
    args = ffmpeg_args(choice, source, "/m/in.mov", "/m/out");
    CHECK(has(args, "-c:v", "prores_ks"));
    CHECK(has(args, "-profile:v", "3"));
    CHECK(has(args, "-c:a", "pcm_s16le"));

    choice.format = VideoFormat::Mkv;
    args = ffmpeg_args(choice, source, "/m/in.mov", "/m/out");
    CHECK(has(args, "-c:v", "libx264"));
    CHECK_FALSE(has(args, "-movflags"));

    // A source with no sound gets none, whatever the box says.
    MediaProbe silent = source;
    silent.has_audio = false;
    choice = VideoConvert{};
    args = ffmpeg_args(choice, silent, "/m/in.mov", "/m/out");
    CHECK(has(args, "-an"));
}

TEST_CASE("A tall video is named, and scaled, by its shorter side", "[convert]")
{
    // A phone's 1080p, held upright.
    MediaProbe tall = hd_video();
    std::swap(tall.width, tall.height);
    CHECK_FALSE(scaled_size(1080, tall, false, true).has_value());
    CHECK(scaled_size(720, tall, false, true) == std::pair{720, 1280});
    VideoConvert choice;
    choice.height = 720;
    auto args = ffmpeg_args(choice, tall, "/m/in.mp4", "/m/out");
    CHECK(has(args, "-vf", "scale=720:-2"));
    CHECK(converted_title("Testimony.mp4", choice, tall, ".mp4") == "Testimony (720p).mp4");
    // Its own 1080 is no change; 1920 is not offered, and would keep it.
    choice.height = 1080;
    CHECK_FALSE(has(ffmpeg_args(choice, tall, "/m/in.mp4", "/m/out"), "-vf"));
    choice.height = 2160;
    CHECK_FALSE(has(ffmpeg_args(choice, tall, "/m/in.mp4", "/m/out"), "-vf"));
    // A size typed in is the height, as it is.
    choice.height = 1280;
    choice.exact_height = true;
    args = ffmpeg_args(choice, tall, "/m/in.mp4", "/m/out");
    CHECK(has(args, "-vf", "scale=-2:1280"));
    CHECK(converted_title("Testimony.mp4", choice, tall, ".mp4") == "Testimony (720p).mp4");

    // A tall image: its shorter side, and the size it comes out at.
    MediaProbe portrait;
    portrait.has_video = true;
    portrait.width = 3024;
    portrait.height = 4032;
    ImageConvert image;
    image.height = 1080;
    CHECK(has(ffmpeg_args(image, portrait, "/m/in.png", "/m/out"), "-vf", "scale=1080:-1"));
    CHECK(converted_title("Choir.png", image, portrait, ".png") == "Choir (1080 \u00d7 1440, JPEG).jpg");
}

TEST_CASE("A video stored sideways reads as it plays", "[convert]")
{
    // An older phone: a rotate tag.
    auto probe = parse_probe(R"({"streams": [{"codec_type": "video", "width": 1920,
        "height": 1080, "tags": {"rotate": "90"}}], "format": {}})");
    REQUIRE(probe.has_value());
    CHECK(probe->rotation == 90);
    CHECK(probe->width == 1080);
    CHECK(probe->height == 1920);
    // A newer one: a display matrix, turning the other way.
    probe = parse_probe(R"({"streams": [{"codec_type": "video", "width": 1920,
        "height": 1080, "side_data_list": [{"side_data_type": "Display Matrix",
        "rotation": -90}]}], "format": {}})");
    REQUIRE(probe.has_value());
    CHECK(probe->rotation == 270);
    CHECK(probe->width == 1080);
    // Upside down is the same shape.
    probe = parse_probe(R"({"streams": [{"codec_type": "video", "width": 1920,
        "height": 1080, "side_data_list": [{"rotation": 180}]}], "format": {}})");
    REQUIRE(probe.has_value());
    CHECK(probe->rotation == 180);
    CHECK(probe->width == 1920);
}

TEST_CASE("An audio file's and an image's ffmpeg commands follow their choices", "[convert]")
{
    const MediaProbe song = *parse_probe(k_audio_json);
    AudioConvert audio;
    audio.format = AudioFormat::Mp3;
    auto args = ffmpeg_args(audio, song, "/m/in.flac", "/m/out");
    CHECK(has(args, "-vn"));
    CHECK(has(args, "-c:a", "libmp3lame"));
    CHECK(has(args, "-b:a", "192k"));
    audio.bitrate_kbps = 128;
    args = ffmpeg_args(audio, song, "/m/in.flac", "/m/out");
    CHECK(has(args, "-b:a", "128k"));
    audio.format = AudioFormat::Ogg;
    audio.bitrate_kbps = 0;
    args = ffmpeg_args(audio, song, "/m/in.flac", "/m/out");
    CHECK(has(args, "-c:a", "libopus"));
    CHECK(has(args, "-b:a", "128k"));
    audio.format = AudioFormat::Flac;
    audio.bitrate_kbps = 320;
    args = ffmpeg_args(audio, song, "/m/in.wav", "/m/out");
    CHECK(has(args, "-c:a", "flac"));
    CHECK_FALSE(has(args, "-b:a"));
    audio.format = AudioFormat::Wav;
    args = ffmpeg_args(audio, song, "/m/in.wav", "/m/out");
    CHECK(has(args, "-c:a", "pcm_s16le"));
    audio.format = AudioFormat::M4a;
    args = ffmpeg_args(audio, song, "/m/in.wav", "/m/out");
    CHECK(has(args, "-c:a", "aac"));

    const MediaProbe picture = *parse_probe(k_image_json);
    ImageConvert image;
    image.format = ImageFormat::Webp;
    image.height = 480;
    args = ffmpeg_args(image, picture, "/m/in.png", "/m/out");
    CHECK(has(args, "-vf", "scale=-1:480"));
    CHECK(has(args, "-frames:v", "1"));
    CHECK(has(args, "-c:v", "libwebp"));
    image.format = ImageFormat::Jpeg;
    image.height = 720;
    args = ffmpeg_args(image, picture, "/m/in.png", "/m/out");
    CHECK_FALSE(has(args, "-vf"));
    CHECK(has(args, "-c:v", "mjpeg"));
    image.format = ImageFormat::Png;
    args = ffmpeg_args(image, picture, "/m/in.jpg", "/m/out");
    CHECK(has(args, "-c:v", "png"));
}

TEST_CASE("The format is given with -f, so a staging name will do", "[convert]")
{
    const MediaProbe source = hd_video();
    VideoConvert video;
    video.format = VideoFormat::Mkv;
    auto args = ffmpeg_args(video, source, "/m/in.mov", "/m/.import-x.part");
    REQUIRE(args.size() >= 3);
    CHECK(args[args.size() - 3] == "-f");
    CHECK(args[args.size() - 2] == "matroska");
    VideoConvert mp4;
    mp4.format = VideoFormat::Mp4;
    CHECK(muxer_of(mp4) == "mp4");
    AudioConvert audio;
    audio.format = AudioFormat::M4a;
    CHECK(muxer_of(audio) == "ipod");
    audio.format = AudioFormat::Ogg;
    CHECK(muxer_of(audio) == "ogg");
    ImageConvert image;
    CHECK(muxer_of(image) == "image2");
    image.format = ImageFormat::Webp;
    CHECK(muxer_of(image) == "webp");
}

TEST_CASE("ffmpeg's progress lines read as a fraction of the duration", "[convert]")
{
    CHECK(ffmpeg_progress("out_time_us=5000000", 10.0) == 0.5);
    CHECK(ffmpeg_progress("out_time_ms=2500000", 10.0) == 0.25);
    CHECK(ffmpeg_progress("out_time_us=15000000", 10.0) == 1.0);
    CHECK(ffmpeg_progress("out_time_us=-1", 10.0) == 0.0);
    CHECK(ffmpeg_progress("progress=end", 10.0) == 1.0);
    CHECK(ffmpeg_progress("progress=end", 0.0) == 1.0);
    CHECK_FALSE(ffmpeg_progress("progress=continue", 10.0).has_value());
    CHECK_FALSE(ffmpeg_progress("out_time_us=N/A", 10.0).has_value());
    CHECK_FALSE(ffmpeg_progress("out_time_us=5000000", 0.0).has_value());
    CHECK_FALSE(ffmpeg_progress("frame=12", 10.0).has_value());
    CHECK_FALSE(ffmpeg_progress("", 10.0).has_value());
}

TEST_CASE("The new file is titled after the source and what changed", "[convert]")
{
    const MediaProbe source = hd_video();
    VideoConvert video;
    video.height = 720;
    video.format = VideoFormat::Webm;
    CHECK(converted_title("Sunrise.mp4", video, source, ".mp4") == "Sunrise (720p, WebM).webm");
    // A codec that is not the format's own says so.
    video.format = VideoFormat::Mp4;
    video.video_codec = VideoCodec::H265;
    CHECK(converted_title("Sunrise.mp4", video, source, ".mp4") == "Sunrise (720p, H.265).mp4");

    // The same format and nothing scaled: only the encoding changed.
    video = VideoConvert{};
    CHECK(converted_title("Sunrise.MP4", video, source, ".MP4") == "Sunrise (converted).mp4");
    CHECK(convert_tags(video, source, ".mp4").empty());

    video.fps = 30;  // the source's own rate, near enough
    CHECK(convert_tags(video, source, ".mp4").empty());
    video.fps = 60;
    CHECK(convert_tags(video, source, ".mp4") == std::vector<std::string>{"60 fps"});

    // A title with no extension, and a title whose dot is not one.
    CHECK(converted_title("Sunrise", video, source, ".mp4") == "Sunrise (60 fps).mp4");
    CHECK(converted_title("Vol. 2 Highlights", video, source, ".mp4")
          == "Vol. 2 Highlights (60 fps).mp4");
    CHECK(converted_title("v2.0", video, source, ".mp4") == "v2.0 (60 fps).mp4");

    AudioConvert audio;
    audio.format = AudioFormat::Mp3;
    audio.bitrate_kbps = 128;
    const MediaProbe song = *parse_probe(k_audio_json);
    CHECK(converted_title("Prelude in G.flac", audio, song, ".flac")
          == "Prelude in G (MP3, 128 kbps).mp3");
    audio.format = AudioFormat::Flac;
    CHECK(converted_title("Prelude in G.flac", audio, song, ".flac")
          == "Prelude in G (converted).flac");

    ImageConvert image;
    image.format = ImageFormat::Jpeg;
    image.height = 480;
    const MediaProbe picture = *parse_probe(k_image_json);
    CHECK(converted_title("sunrise.jpeg", image, picture, ".jpeg") == "sunrise (640 \u00d7 480).jpg");
    CHECK(converted_title("sunrise.png", image, picture, ".png") == "sunrise (640 \u00d7 480, JPEG).jpg");
    CHECK(extension_of(ConvertChoice{image}) == ".jpg");
    CHECK(format_label(ImageFormat::Webp) == "WebP");
    CHECK(format_label(VideoFormat::Webm) == "WebM");
    CHECK(format_label(AudioFormat::Ogg) == "Ogg");
}

TEST_CASE("A dialog's choice reads from JSON", "[convert]")
{
    using nlohmann::json;
    auto video = convert_choice_from_json(
        "video", json{{"format", "WebM"}, {"height", 720}, {"quality", "better"}, {"fps", 30},
                      {"videoCodec", "av1"}, {"audioCodec", "vorbis"}, {"audioBitrate", 160}});
    REQUIRE(video.has_value());
    REQUIRE(std::holds_alternative<VideoConvert>(*video));
    const auto& v = std::get<VideoConvert>(*video);
    CHECK(v.format == VideoFormat::Webm);
    CHECK(v.height == 720);
    CHECK(v.quality == Quality::Better);
    CHECK(v.fps == 30);
    CHECK(v.video_codec == VideoCodec::Av1);
    CHECK(v.audio_codec == AudioCodec::Vorbis);
    CHECK(v.audio_bitrate_kbps == 160);
    CHECK_FALSE(v.exact_height);

    // Defaults for what is missing.
    video = convert_choice_from_json("video", json::object());
    REQUIRE(video.has_value());
    CHECK(std::get<VideoConvert>(*video) == VideoConvert{});

    auto audio = convert_choice_from_json("audio", json{{"format", "ogg"}, {"bitrate", 96}});
    REQUIRE(audio.has_value());
    CHECK(std::get<AudioConvert>(*audio).format == AudioFormat::Ogg);
    CHECK(std::get<AudioConvert>(*audio).bitrate_kbps == 96);

    auto image = convert_choice_from_json("image", json{{"format", "jpeg"}, {"height", 1080}});
    REQUIRE(image.has_value());
    CHECK(std::get<ImageConvert>(*image).format == ImageFormat::Jpeg);
    CHECK(std::get<ImageConvert>(*image).height == 1080);
    image = convert_choice_from_json("image", json{{"format", "webp"}});
    REQUIRE(image.has_value());
    CHECK(std::get<ImageConvert>(*image).format == ImageFormat::Webp);

    CHECK_FALSE(convert_choice_from_json("video", json{{"format", "avi"}}).has_value());
    CHECK_FALSE(convert_choice_from_json("video", json{{"quality", "best"}}).has_value());
    CHECK_FALSE(convert_choice_from_json("audio", json{{"format", "mp4"}}).has_value());
    CHECK_FALSE(convert_choice_from_json("song", json::object()).has_value());
    CHECK_FALSE(convert_choice_from_json("video", json::array()).has_value());
}

TEST_CASE("A replacement keeps the title with the new extension", "[convert]")
{
    VideoConvert video;
    video.format = VideoFormat::Webm;
    CHECK(replacement_title("Sunrise.mp4", video) == "Sunrise.webm");
    CHECK(replacement_title("Sunrise", video) == "Sunrise.webm");
    CHECK(replacement_title("Vol. 2 Highlights", video) == "Vol. 2 Highlights.webm");
    ImageConvert image;
    CHECK(replacement_title("logo.PNG", image) == "logo.jpg");
}

TEST_CASE("Sizes and lengths read as the dialog shows them", "[convert]")
{
    CHECK(format_file_size(0) == "0 KB");
    CHECK(format_file_size(-5) == "0 KB");
    CHECK(format_file_size(820'000) == "820 KB");
    CHECK(format_file_size(4'500'000) == "4.5 MB");
    CHECK(format_file_size(45'000'000) == "45 MB");
    CHECK(format_file_size(1'200'000'000) == "1.2 GB");
    CHECK(format_file_size(3'000'000'000'000) == "3000 GB");

    CHECK(format_duration(0).empty());
    CHECK(format_duration(-1).empty());
    CHECK(format_duration(12.4) == "0:12");
    CHECK(format_duration(185) == "3:05");
    CHECK(format_duration(3723) == "1:02:03");
}

TEST_CASE("Each codec has its encoder, and only what its format holds is taken", "[convert]")
{
    const MediaProbe source = hd_video();
    VideoConvert v;
    v.video_codec = VideoCodec::H265;
    auto args = ffmpeg_args(v, source, "/m/in", "/m/out");
    CHECK(has(args, "-c:v", "libx265"));
    CHECK(has(args, "-crf", "28"));
    CHECK(has(args, "-tag:v", "hvc1"));
    v.format = VideoFormat::Mkv;
    CHECK_FALSE(has(ffmpeg_args(v, source, "/m/in", "/m/out"), "-tag:v"));
    v.video_codec = VideoCodec::Av1;
    args = ffmpeg_args(v, source, "/m/in", "/m/out");
    CHECK(has(args, "-c:v", "libsvtav1"));
    CHECK(has(args, "-crf", "34"));
    v.video_codec = VideoCodec::Copy;
    v.audio_codec = AudioCodec::Flac;
    args = ffmpeg_args(v, source, "/m/in", "/m/out");
    CHECK(has(args, "-c:v", "copy"));
    CHECK_FALSE(has(args, "-pix_fmt"));
    CHECK(has(args, "-c:a", "flac"));
    v.audio_codec = AudioCodec::Opus;
    v.audio_bitrate_kbps = 96;
    CHECK(has(ffmpeg_args(v, source, "/m/in", "/m/out"), "-b:a", "96k"));

    CHECK(video_codec_fits(VideoFormat::Mp4, VideoCodec::H265));
    CHECK_FALSE(video_codec_fits(VideoFormat::Mp4, VideoCodec::Vp9));
    CHECK_FALSE(video_codec_fits(VideoFormat::Webm, VideoCodec::H264));
    CHECK(video_codec_fits(VideoFormat::Mov, VideoCodec::ProRes));
    CHECK(video_codec_fits(VideoFormat::Mkv, VideoCodec::Vp9));
    CHECK(audio_codec_fits(VideoFormat::Webm, AudioCodec::Vorbis));
    CHECK_FALSE(audio_codec_fits(VideoFormat::Webm, AudioCodec::Aac));
    CHECK(audio_codec_fits(VideoFormat::Mov, AudioCodec::Alac));
    CHECK_FALSE(audio_codec_fits(VideoFormat::Mkv, AudioCodec::Alac));
    CHECK(audio_codec_fits(AudioFormat::M4a, AudioCodec::Alac));
    CHECK_FALSE(audio_codec_fits(AudioFormat::Mp3, AudioCodec::Aac));
    CHECK(audio_codec_fits(AudioFormat::Ogg, AudioCodec::Vorbis));
    CHECK(audio_codec_fits(AudioFormat::Wav, AudioCodec::Copy));

    using nlohmann::json;
    CHECK_FALSE(convert_choice_from_json("video", json{{"format", "webm"}, {"videoCodec", "h264"}})
                    .has_value());
    CHECK_FALSE(convert_choice_from_json("video", json{{"videoCodec", "divx"}}).has_value());
    // A picture kept as it is cannot be resized or retimed.
    CHECK_FALSE(convert_choice_from_json("video", json{{"videoCodec", "copy"}, {"height", 720}})
                    .has_value());
    CHECK(convert_choice_from_json("video", json{{"videoCodec", "copy"}}).has_value());
    // Heights: even for video, and within reason.
    CHECK(std::get<VideoConvert>(*convert_choice_from_json("video", json{{"height", 721}})).height
          == 720);
    CHECK_FALSE(convert_choice_from_json("video", json{{"height", 4}}).has_value());
    CHECK_FALSE(convert_choice_from_json("image", json{{"height", 99999}}).has_value());
    for (const VideoCodec c : k_video_codecs) {
        CHECK_FALSE(codec_key(c).empty());
    }
}

TEST_CASE("A video can become a sound file, with the sound's own choices", "[convert]")
{
    using nlohmann::json;
    const auto choice = convert_choice_from_json(
        "video", json{{"format", "m4a"}, {"codec", "alac"}, {"sampleRate", 48000},
                      {"channels", 1}});
    REQUIRE(choice.has_value());
    REQUIRE(std::holds_alternative<AudioConvert>(*choice));
    const auto& a = std::get<AudioConvert>(*choice);
    CHECK(a.format == AudioFormat::M4a);
    CHECK(a.codec == AudioCodec::Alac);
    CHECK(a.sample_rate == 48000);
    CHECK(a.channels == 1);
    CHECK(is_audio_format_key("mp3"));
    CHECK_FALSE(is_audio_format_key("mp4"));

    const MediaProbe source = hd_video();
    const auto args = ffmpeg_args(*choice, source, "/m/in.mp4", "/m/out");
    CHECK(has(args, "-vn"));
    CHECK(has(args, "-c:a", "alac"));
    CHECK_FALSE(has(args, "-b:a"));
    CHECK(has(args, "-ar", "48000"));
    CHECK(has(args, "-ac", "1"));
    CHECK(converted_title("Sunrise.mp4", *choice, source, ".mp4") == "Sunrise (M4A, ALAC, mono).m4a");

    CHECK_FALSE(convert_choice_from_json("video", json{{"format", "mp3"}, {"codec", "aac"}})
                    .has_value());
    CHECK_FALSE(convert_choice_from_json("audio", json{{"channels", 6}}).has_value());
    CHECK_FALSE(convert_choice_from_json("audio", json{{"sampleRate", 100}}).has_value());
}

TEST_CASE("Codecs and containers read as people call them", "[convert]")
{
    CHECK(codec_display_name("h264") == "H.264");
    CHECK(codec_display_name("hevc") == "H.265");
    CHECK(codec_display_name("pcm_s24le") == "PCM");
    CHECK(codec_display_name("opus") == "Opus");
    CHECK(codec_display_name("wmv3") == "WMV3");
    CHECK(codec_display_name("").empty());
    CHECK(format_display_name(".mp4") == "MP4");
    CHECK(format_display_name("webm") == "WebM");
    CHECK(format_display_name(".JPEG") == "JPEG");
    CHECK(format_display_name(".oga") == "Ogg");
    CHECK(format_display_name("").empty());

    const auto args = frame_args("/m/in.mp4", 4.25, 180, "/tmp/f.jpg");
    CHECK(has(args, "-ss", "4.250"));
    CHECK(has(args, "-frames:v", "1"));
    CHECK(has(args, "-vf", "scale=-2:180"));
    CHECK(args.back() == "/tmp/f.jpg");
}

TEST_CASE("A size is estimated only where a bit rate fixes it", "[convert]")
{
    const MediaProbe song = *parse_probe(k_audio_json);  // 200 s
    AudioConvert audio;
    audio.format = AudioFormat::Mp3;
    audio.bitrate_kbps = 128;
    CHECK(estimated_bytes(audio, song) == 3200000);
    audio.bitrate_kbps = 0;
    CHECK(estimated_bytes(audio, song) == 4800000);
    audio.format = AudioFormat::Flac;
    CHECK_FALSE(estimated_bytes(audio, song).has_value());
    audio.format = AudioFormat::M4a;
    audio.codec = AudioCodec::Alac;
    CHECK_FALSE(estimated_bytes(audio, song).has_value());
    audio.codec = AudioCodec::Copy;
    CHECK_FALSE(estimated_bytes(audio, song).has_value());
    audio.codec = AudioCodec::Auto;
    MediaProbe unknown = song;
    unknown.duration_s = 0.0;
    audio.format = AudioFormat::Mp3;
    CHECK_FALSE(estimated_bytes(audio, unknown).has_value());
    CHECK_FALSE(estimated_bytes(VideoConvert{}, hd_video()).has_value());
    CHECK_FALSE(estimated_bytes(ImageConvert{}, hd_video()).has_value());
}

TEST_CASE("The real ffmpeg accepts the commands", "[convert]")
{
    seventhslide::test::ensure_qt_application();
    const seventhslide::platform::Paths paths;
    const auto search = seventhslide::platform::ToolSearch::from(paths, {});
    const auto ffmpeg = seventhslide::platform::find_tool("ffmpeg", search);
    const auto ffprobe = seventhslide::platform::find_tool("ffprobe", search);
    if (!ffmpeg || !ffprobe) {
        SKIP("no ffmpeg and ffprobe here");
    }
    const auto run = [](const fs::path& program, const std::vector<std::string>& args,
                        std::string* out = nullptr) {
        QProcess p;
        QStringList list;
        for (const auto& a : args) {
            list << QString::fromStdString(a);
        }
        p.start(QString::fromStdString(program.string()), list);
        REQUIRE(p.waitForFinished(60000));
        if (out != nullptr) {
            *out = p.readAllStandardOutput().toStdString();
        }
        INFO(p.readAllStandardError().toStdString());
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    };
    const seventhslide::test::TempDir tmp;
    // A second of test pattern with a tone.
    const fs::path source = tmp.path / "src.mp4";
    REQUIRE(run(ffmpeg->path,
                {"-hide_banner", "-v", "error", "-y", "-f", "lavfi", "-i",
                 "testsrc=duration=1:size=128x96:rate=10", "-f", "lavfi", "-i",
                 "sine=duration=1", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac",
                 "-shortest", source.string()}));

    std::string json;
    REQUIRE(run(ffprobe->path, ffprobe_args(source), &json));
    const auto probe = parse_probe(json);
    REQUIRE(probe.has_value());
    CHECK(probe->has_video);
    CHECK(probe->has_audio);
    CHECK(probe->height == 96);
    CHECK(probe->duration_s > 0.5);

    VideoConvert video;
    video.format = VideoFormat::Mkv;
    video.height = 120;  // above the source, typed in: used as it is
    video.exact_height = true;
    const fs::path mkv = tmp.path / ".import-mkv.part";
    REQUIRE(run(ffmpeg->path, ffmpeg_args(video, *probe, source, mkv)));
    std::string mkv_json;
    REQUIRE(run(ffprobe->path, ffprobe_args(mkv), &mkv_json));
    const auto converted = parse_probe(mkv_json);
    REQUIRE(converted.has_value());
    CHECK(converted->has_video);
    CHECK(converted->has_audio);
    CHECK(converted->height == 120);

    // Stored sideways with a note to turn it: read upright, and scaled as it
    // plays (ffmpeg turns it before the scale).
    const fs::path sideways = tmp.path / "sideways.mp4";
    REQUIRE(run(ffmpeg->path,
                {"-hide_banner", "-v", "error", "-y", "-display_rotation", "90", "-i",
                 source.string(), "-c", "copy", sideways.string()}));
    std::string sideways_json;
    REQUIRE(run(ffprobe->path, ffprobe_args(sideways), &sideways_json));
    const auto upright = parse_probe(sideways_json);
    REQUIRE(upright.has_value());
    CHECK(upright->width == 96);
    CHECK(upright->height == 128);
    VideoConvert smaller;
    smaller.format = VideoFormat::Mp4;
    smaller.height = 48;
    const fs::path turned = tmp.path / ".import-turned.part";
    REQUIRE(run(ffmpeg->path, ffmpeg_args(smaller, *upright, sideways, turned)));
    std::string turned_json;
    REQUIRE(run(ffprobe->path, ffprobe_args(turned), &turned_json));
    const auto made = parse_probe(turned_json);
    REQUIRE(made.has_value());
    CHECK(made->width == 48);
    CHECK(made->height == 64);

    // A video's sound, as it is, into M4A: no picture, no re-encoding.
    AudioConvert extract;
    extract.format = AudioFormat::M4a;
    extract.codec = AudioCodec::Copy;
    const fs::path m4a = tmp.path / ".import-m4a.part";
    REQUIRE(run(ffmpeg->path, ffmpeg_args(extract, *probe, source, m4a)));
    std::string m4a_json;
    REQUIRE(run(ffprobe->path, ffprobe_args(m4a), &m4a_json));
    const auto extracted = parse_probe(m4a_json);
    REQUIRE(extracted.has_value());
    CHECK_FALSE(extracted->has_video);
    CHECK(extracted->has_audio);
    CHECK(extracted->audio_codec == "aac");

    AudioConvert audio;
    audio.format = AudioFormat::Wav;
    const fs::path wav = tmp.path / ".import-wav.part";
    REQUIRE(run(ffmpeg->path, ffmpeg_args(audio, *probe, source, wav)));
    std::string wav_json;
    REQUIRE(run(ffprobe->path, ffprobe_args(wav), &wav_json));
    const auto sound = parse_probe(wav_json);
    REQUIRE(sound.has_value());
    CHECK_FALSE(sound->has_video);
    CHECK(sound->has_audio);

    ImageConvert image;
    image.format = ImageFormat::Png;
    image.height = 48;
    const fs::path png = tmp.path / ".import-png.part";
    REQUIRE(run(ffmpeg->path, ffmpeg_args(image, *probe, source, png)));
    std::string png_json;
    REQUIRE(run(ffprobe->path, ffprobe_args(png), &png_json));
    const auto still = parse_probe(png_json);
    REQUIRE(still.has_value());
    CHECK(still->height == 48);
    CHECK(still->width == 64);
}
