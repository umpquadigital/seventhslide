#pragma once

// Converting media with ffmpeg: what ffprobe says about a file, the choices
// Convert offers for each kind, the ffmpeg command for a choice, and how the
// result is named. Nothing here runs a program: the app's ToolJobs does, with
// these arguments, and reads its progress through ``ffmpeg_progress``.
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace seventhslide::media {

/// What ffprobe reports for a file (``parse_probe``). A field it did not
/// give is 0 or empty.
struct MediaProbe {
    bool has_video = false;
    bool has_audio = false;
    /// The picture as it plays: a phone video stored sideways with a note to
    /// turn it a quarter (``rotation``) is given here turned, as tall as it
    /// shows.
    int width = 0;
    int height = 0;
    /// Degrees the stored picture is turned to play (0, 90, 180, 270).
    int rotation = 0;
    /// Frames per second; 0 when unknown.
    double fps = 0.0;
    /// Seconds; 0 when unknown (an image).
    double duration_s = 0.0;
    std::int64_t size_bytes = 0;
    /// Bits per second over the whole file; 0 when unknown.
    std::int64_t bit_rate = 0;
    std::string video_codec;
    std::string audio_codec;
    /// The sound's samples a second and channels; 0 when unknown.
    int sample_rate = 0;
    int channels = 0;

    [[nodiscard]] bool operator==(const MediaProbe&) const = default;
};

/// A codec as people call it, from ffmpeg's name for it: ``h264`` is
/// "H.264", ``hevc`` "H.265", ``pcm_s16le`` "PCM"; one it does not know comes
/// back in capitals. Empty for empty.
[[nodiscard]] std::string codec_display_name(std::string_view ffmpeg_name);

/// A container as people call it, from a file's extension (``.mp4``,
/// ``webm``): "MP4", "WebM", "MOV"...; empty for none.
[[nodiscard]] std::string format_display_name(std::string_view extension);

/// The arguments for ffmpeg to save one frame of ``source``, ``at_s``
/// seconds in, ``height`` pixels tall, as a JPEG at ``dest``: the picture
/// the Convert dialog shows.
[[nodiscard]] std::vector<std::string> frame_args(const std::filesystem::path& source,
                                                  double at_s,
                                                  int height,
                                                  const std::filesystem::path& dest);

/// ffprobe's arguments: JSON for ``parse_probe`` on stdout, errors only on
/// stderr.
[[nodiscard]] std::vector<std::string> ffprobe_args(const std::filesystem::path& source);

/// Read what ``ffprobe_args`` made ffprobe print. Nullopt when it is not
/// ffprobe's JSON.
[[nodiscard]] std::optional<MediaProbe> parse_probe(std::string_view json_text);

enum class VideoFormat { Mp4, Webm, Mov, Mkv };
enum class AudioFormat { Mp3, M4a, Wav, Flac, Ogg };
enum class ImageFormat { Jpeg, Png, Webp };
/// Smaller file, a balance, or a better picture: the encoder's quality.
enum class Quality { Smaller, Balanced, Better };
/// A video's picture codec. Auto is its format's own: H.264 for MP4 and
/// MKV, VP9 for WebM, ProRes for MOV. Copy keeps the source's picture as it
/// is, so nothing about it (its size, its frame rate) can change.
enum class VideoCodec { Auto, Copy, H264, H265, Vp9, Av1, ProRes };
/// A sound codec, in a video or on its own. Auto is the format's own; Copy
/// keeps the source's sound as it is.
enum class AudioCodec { Auto, Copy, Aac, Mp3, Opus, Vorbis, Flac, Pcm, Alac };

struct VideoConvert {
    /// The size to scale to, or 0 to keep (see ``scaled_size``). A preset
    /// names the picture's shorter side, as "1080p" does for a tall picture
    /// as for a wide one, and is never larger than the source's. An
    /// ``exact_height`` (typed in) is the height, used as it is, larger or
    /// not. The other side follows the picture's shape.
    int height = 0;
    bool exact_height = false;
    VideoFormat format = VideoFormat::Mp4;
    Quality quality = Quality::Balanced;
    /// Frames a second, or 0 to keep.
    int fps = 0;
    VideoCodec video_codec = VideoCodec::Auto;
    AudioCodec audio_codec = AudioCodec::Auto;
    /// kbit/s for a lossy sound codec; 0 is the codec's default.
    int audio_bitrate_kbps = 0;

    [[nodiscard]] bool operator==(const VideoConvert&) const = default;
};

struct AudioConvert {
    AudioFormat format = AudioFormat::Mp3;
    /// kbit/s for a lossy codec; 0 is its default. Ignored for a lossless
    /// one.
    int bitrate_kbps = 0;
    /// Where the format has a choice (M4A: AAC or ALAC; Ogg: Opus or Vorbis),
    /// or Copy; Auto is the format's own.
    AudioCodec codec = AudioCodec::Auto;
    /// Samples a second, or 0 to keep.
    int sample_rate = 0;
    /// 1 (mono) or 2 (stereo), or 0 to keep.
    int channels = 0;

    [[nodiscard]] bool operator==(const AudioConvert&) const = default;
};

struct ImageConvert {
    ImageFormat format = ImageFormat::Jpeg;
    /// As VideoConvert::height.
    int height = 0;
    bool exact_height = false;

    [[nodiscard]] bool operator==(const ImageConvert&) const = default;
};

using ConvertChoice = std::variant<VideoConvert, AudioConvert, ImageConvert>;

/// The choice a dialog sends, as JSON, for a source of ``kind`` (``video``,
/// ``audio`` or ``image``): ``format`` by its extension without the dot
/// ("webm", "mp3", "jpg"; "jpeg" too), and the other fields of what it
/// makes - a video's ``height``, ``exactHeight``, ``quality`` ("smaller",
/// "balanced", "better"), ``fps``, ``videoCodec``, ``audioCodec`` and
/// ``audioBitrate``; a sound's ``bitrate``, ``codec``, ``sampleRate`` and
/// ``channels``; an image's ``height`` and ``exactHeight``. Codecs go by
/// ``codec_key``. A video may become a sound file (an audio format: the
/// picture is left out). A missing field is the default. Nullopt for an
/// unknown kind, format or codec, a codec its format cannot hold, and a
/// picture kept as is (Copy) that is also asked to change.
[[nodiscard]] std::optional<ConvertChoice> convert_choice_from_json(std::string_view kind,
                                                                    const nlohmann::json& j);

/// The extension the converted file gets, lower case with the dot.
[[nodiscard]] std::string_view extension_of(VideoFormat format) noexcept;
[[nodiscard]] std::string_view extension_of(AudioFormat format) noexcept;
[[nodiscard]] std::string_view extension_of(ImageFormat format) noexcept;
[[nodiscard]] std::string_view extension_of(const ConvertChoice& choice) noexcept;

/// What the format is called: "MP4", "WebM", "MOV", "MKV"; "MP3", "M4A",
/// "WAV", "FLAC", "Ogg"; "JPEG", "PNG", "WebP".
[[nodiscard]] std::string_view format_label(VideoFormat format) noexcept;
[[nodiscard]] std::string_view format_label(AudioFormat format) noexcept;
[[nodiscard]] std::string_view format_label(ImageFormat format) noexcept;

/// The picture's size after the conversion, as (width, height), or nullopt
/// when it keeps its own. A preset (not ``exact``) names the shorter side:
/// 720 makes 1920 × 1080 into 1280 × 720 and 1080 × 1920 into 720 × 1280;
/// at or above the source's shorter side, or with the source's size unknown,
/// it keeps its own. An ``exact`` size is the height, used as it is unless it
/// is the source's own. The other side follows the shape, rounded to even
/// with ``even`` (video), and is 0 where the shape is unknown.
[[nodiscard]] std::optional<std::pair<int, int>> scaled_size(int chosen,
                                                             const MediaProbe& source,
                                                             bool exact,
                                                             bool even) noexcept;

/// Whether ``format`` can hold ``codec``. Auto and Copy always fit (a copy
/// that does not is ffmpeg's to refuse).
[[nodiscard]] bool video_codec_fits(VideoFormat format, VideoCodec codec) noexcept;
[[nodiscard]] bool audio_codec_fits(VideoFormat format, AudioCodec codec) noexcept;
[[nodiscard]] bool audio_codec_fits(AudioFormat format, AudioCodec codec) noexcept;

/// A codec as JSON names it: "auto", "copy", "h264", "h265", "vp9", "av1",
/// "prores"; "aac", "mp3", "opus", "vorbis", "flac", "pcm", "alac".
[[nodiscard]] std::string_view codec_key(VideoCodec codec) noexcept;
[[nodiscard]] std::string_view codec_key(AudioCodec codec) noexcept;

/// Every format of each kind, in the order a menu lists them.
inline constexpr std::array<VideoFormat, 4> k_video_formats = {
    VideoFormat::Mp4, VideoFormat::Webm, VideoFormat::Mov, VideoFormat::Mkv};
inline constexpr std::array<AudioFormat, 5> k_audio_formats = {
    AudioFormat::Mp3, AudioFormat::M4a, AudioFormat::Wav, AudioFormat::Flac, AudioFormat::Ogg};
inline constexpr std::array<ImageFormat, 3> k_image_formats = {
    ImageFormat::Jpeg, ImageFormat::Png, ImageFormat::Webp};

/// Every video codec, then every sound codec, in the order a menu lists them.
inline constexpr std::array<VideoCodec, 7> k_video_codecs = {
    VideoCodec::Auto, VideoCodec::Copy, VideoCodec::H264, VideoCodec::H265,
    VideoCodec::Vp9,  VideoCodec::Av1,  VideoCodec::ProRes};
inline constexpr std::array<AudioCodec, 9> k_audio_codecs = {
    AudioCodec::Auto, AudioCodec::Copy, AudioCodec::Aac,  AudioCodec::Mp3, AudioCodec::Opus,
    AudioCodec::Vorbis, AudioCodec::Flac, AudioCodec::Pcm, AudioCodec::Alac};

/// Whether ``kind``'s ``format`` key is a sound format (a video converted to
/// one becomes a sound file).
[[nodiscard]] bool is_audio_format_key(std::string_view format) noexcept;

/// The ffmpeg muxer that writes ``choice``'s format: given with ``-f``, so
/// the output's own name (a staging name) does not have to say it.
[[nodiscard]] std::string_view muxer_of(const ConvertChoice& choice) noexcept;

/// ffmpeg's arguments to read ``source_path`` and write ``dest_path``
/// (overwriting it: the caller names a staging file, whatever its
/// extension; the format is given with ``-f``), with its progress on
/// stdout for ``ffmpeg_progress`` and only errors on stderr. ffmpeg reads
/// stdin, so ``q`` stops it cleanly.
[[nodiscard]] std::vector<std::string> ffmpeg_args(const ConvertChoice& choice,
                                                   const MediaProbe& source,
                                                   const std::filesystem::path& source_path,
                                                   const std::filesystem::path& dest_path);

/// One line of ffmpeg's ``-progress`` output, as a fraction of
/// ``duration_s``: ``out_time_us=`` (or the older ``out_time_ms=``, which
/// is microseconds too) gives how far it is, ``progress=end`` gives 1.
/// Nullopt for any other line, and for a time when the duration is unknown.
[[nodiscard]] std::optional<double> ffmpeg_progress(std::string_view line, double duration_s);

/// What the conversion changed, for the new file's title: "720p" (an
/// image's "1440 × 1080"), the format when the extension changes, a codec
/// that is not the format's own ("H.265"), "30 fps", "128 kbps", "mono".
/// Empty when nothing changed but the encoding.
[[nodiscard]] std::vector<std::string> convert_tags(const ConvertChoice& choice,
                                                    const MediaProbe& source,
                                                    std::string_view source_extension);

/// The new file's title: the source's title with its extension taken off,
/// the tags in brackets ("converted" when there are none), then the new
/// extension: ``Sunrise.mp4`` to ``Sunrise (720p, WebM).webm``.
[[nodiscard]] std::string converted_title(std::string_view source_title,
                                          const ConvertChoice& choice,
                                          const MediaProbe& source,
                                          std::string_view source_extension);

/// The title the new file takes when it replaces the source: the source's
/// title with the new extension (``Sunrise.mp4`` to ``Sunrise.webm``).
[[nodiscard]] std::string replacement_title(std::string_view source_title,
                                            const ConvertChoice& choice);

/// A file size as the dialog shows it: "820 KB", "4.5 MB", "1.2 GB"
/// (1000s, one decimal under 10). "0 KB" for 0 or less.
[[nodiscard]] std::string format_file_size(std::int64_t bytes);

/// A length as the dialog shows it: "0:12", "3:05", "1:02:03". Empty for 0
/// or less.
[[nodiscard]] std::string format_duration(double seconds);

/// How big the result will be, where the choice fixes a bit rate (sound
/// alone, lossy, with a known duration). Nullopt where it depends on the
/// picture, or on a sound kept as it is.
[[nodiscard]] std::optional<std::int64_t> estimated_bytes(const ConvertChoice& choice,
                                                          const MediaProbe& source);

} // namespace seventhslide::media
