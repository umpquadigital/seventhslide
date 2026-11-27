#include "seventhslide/media/convert.hpp"

#include "seventhslide/util/strings.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <charconv>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <utility>

namespace seventhslide::media {
namespace {

namespace fs = std::filesystem;

/// A JSON number, or a number ffprobe wrote as a string ("1.5").
double number_or(const nlohmann::json& j, const char* key, double fallback)
{
    const auto it = j.find(key);
    if (it == j.end()) {
        return fallback;
    }
    if (it->is_number()) {
        return it->get<double>();
    }
    if (it->is_string()) {
        const std::string s = it->get<std::string>();
        double value = fallback;
        const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
        return result.ec == std::errc{} ? value : fallback;
    }
    return fallback;
}

std::string string_or(const nlohmann::json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

/// The degrees a stream's picture is turned to play, from its rotate tag
/// (older files) or its display matrix (newer ones), as 0, 90, 180 or 270.
int stream_rotation(const nlohmann::json& stream)
{
    double degrees = 0.0;
    if (const auto tags = stream.find("tags"); tags != stream.end() && tags->is_object()) {
        degrees = number_or(*tags, "rotate", 0.0);
    }
    if (const auto side = stream.find("side_data_list");
        side != stream.end() && side->is_array()) {
        for (const auto& data : *side) {
            if (data.is_object() && data.contains("rotation")) {
                degrees = number_or(data, "rotation", 0.0);
            }
        }
    }
    const long quarter = std::lround(degrees / 90.0);
    return static_cast<int>(((quarter % 4) + 4) % 4) * 90;
}

/// ffprobe's "30000/1001" (or "25/1", or "0/0" for unknown).
double parse_rate(std::string_view text)
{
    const auto slash = text.find('/');
    double num = 0.0;
    double den = 1.0;
    const std::string_view a = text.substr(0, slash);
    std::from_chars(a.data(), a.data() + a.size(), num);
    if (slash != std::string_view::npos) {
        const std::string_view b = text.substr(slash + 1);
        std::from_chars(b.data(), b.data() + b.size(), den);
    }
    if (den <= 0.0 || num <= 0.0) {
        return 0.0;
    }
    return num / den;
}

/// A set of codecs, one bit each.
template <typename Codec>
constexpr std::uint32_t codec_set(std::initializer_list<Codec> list)
{
    std::uint32_t bits = 0;
    for (const Codec c : list) {
        bits |= std::uint32_t{1} << static_cast<unsigned>(c);
    }
    return bits;
}

constexpr std::uint32_t k_any_codec = ~std::uint32_t{0};

template <typename Codec>
constexpr bool holds(std::uint32_t set, Codec codec)
{
    return ((set >> static_cast<unsigned>(codec)) & 1U) != 0;
}

// What each format and codec is, in one place: a format or codec added is a
// row added. Rows are in their enum's order (checked below), so a value is
// its row's index.

struct VideoFormatFacts {
    VideoFormat id;
    std::string_view key;        ///< as a choice names it
    std::string_view extension;
    std::string_view label;      ///< as a title tags it
    std::string_view muxer;      ///< ffmpeg's -f
    VideoCodec video;            ///< what Auto is
    AudioCodec audio;
    std::uint32_t video_codecs;  ///< what it holds (Auto and Copy always)
    std::uint32_t audio_codecs;
};

constexpr std::array<VideoFormatFacts, 4> k_video_format_facts = {{
    {VideoFormat::Mp4, "mp4", ".mp4", "MP4", "mp4", VideoCodec::H264, AudioCodec::Aac,
     codec_set({VideoCodec::H264, VideoCodec::H265, VideoCodec::Av1}),
     codec_set({AudioCodec::Aac, AudioCodec::Mp3, AudioCodec::Opus, AudioCodec::Alac})},
    {VideoFormat::Webm, "webm", ".webm", "WebM", "webm", VideoCodec::Vp9, AudioCodec::Opus,
     codec_set({VideoCodec::Vp9, VideoCodec::Av1}), codec_set({AudioCodec::Opus, AudioCodec::Vorbis})},
    {VideoFormat::Mov, "mov", ".mov", "MOV", "mov", VideoCodec::ProRes, AudioCodec::Pcm,
     codec_set({VideoCodec::H264, VideoCodec::H265, VideoCodec::ProRes}),
     codec_set({AudioCodec::Aac, AudioCodec::Pcm, AudioCodec::Alac})},
    {VideoFormat::Mkv, "mkv", ".mkv", "MKV", "matroska", VideoCodec::H264, AudioCodec::Aac, k_any_codec,
     k_any_codec & ~codec_set({AudioCodec::Alac})},
}};

struct AudioFormatFacts {
    AudioFormat id;
    std::string_view key;
    std::string_view extension;
    std::string_view label;
    std::string_view muxer;
    AudioCodec codec;            ///< what Auto is
    std::uint32_t codecs;
};

constexpr std::array<AudioFormatFacts, 5> k_audio_format_facts = {{
    {AudioFormat::Mp3, "mp3", ".mp3", "MP3", "mp3", AudioCodec::Mp3, codec_set({AudioCodec::Mp3})},
    {AudioFormat::M4a, "m4a", ".m4a", "M4A", "ipod", AudioCodec::Aac,
     codec_set({AudioCodec::Aac, AudioCodec::Alac})},
    {AudioFormat::Wav, "wav", ".wav", "WAV", "wav", AudioCodec::Pcm, codec_set({AudioCodec::Pcm})},
    {AudioFormat::Flac, "flac", ".flac", "FLAC", "flac", AudioCodec::Flac, codec_set({AudioCodec::Flac})},
    {AudioFormat::Ogg, "ogg", ".ogg", "Ogg", "ogg", AudioCodec::Opus,
     codec_set({AudioCodec::Opus, AudioCodec::Vorbis})},
}};

struct ImageFormatFacts {
    ImageFormat id;
    std::string_view key;        ///< "jpeg" is read as "jpg"
    std::string_view extension;
    std::string_view label;
    std::string_view muxer;
};

constexpr std::array<ImageFormatFacts, 3> k_image_format_facts = {{
    {ImageFormat::Jpeg, "jpg", ".jpg", "JPEG", "image2"},
    {ImageFormat::Png, "png", ".png", "PNG", "image2"},
    {ImageFormat::Webp, "webp", ".webp", "WebP", "webp"},
}};

struct VideoCodecFacts {
    VideoCodec id;
    std::string_view key;
    std::string_view tag;        ///< as a title tags it
};

constexpr std::array<VideoCodecFacts, 7> k_video_codec_facts = {{
    {VideoCodec::Auto, "auto", "H.264"},
    {VideoCodec::Copy, "copy", "H.264"},
    {VideoCodec::H264, "h264", "H.264"},
    {VideoCodec::H265, "h265", "H.265"},
    {VideoCodec::Vp9, "vp9", "VP9"},
    {VideoCodec::Av1, "av1", "AV1"},
    {VideoCodec::ProRes, "prores", "ProRes"},
}};

struct AudioCodecFacts {
    AudioCodec id;
    std::string_view key;
    std::string_view tag;
    int bitrate_kbps;            ///< a lossy codec's when none is chosen; 0 for the others
};

constexpr std::array<AudioCodecFacts, 9> k_audio_codec_facts = {{
    {AudioCodec::Auto, "auto", "", 0},
    {AudioCodec::Copy, "copy", "", 0},
    {AudioCodec::Aac, "aac", "AAC", 192},
    {AudioCodec::Mp3, "mp3", "", 192},
    {AudioCodec::Opus, "opus", "Opus", 128},
    {AudioCodec::Vorbis, "vorbis", "Vorbis", 160},
    {AudioCodec::Flac, "flac", "", 0},
    {AudioCodec::Pcm, "pcm", "", 0},
    {AudioCodec::Alac, "alac", "ALAC", 0},
}};

template <typename Table>
constexpr bool in_enum_order(const Table& table)
{
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (static_cast<std::size_t>(table[i].id) != i) {
            return false;
        }
    }
    return true;
}

static_assert(in_enum_order(k_video_format_facts) && in_enum_order(k_audio_format_facts)
                  && in_enum_order(k_image_format_facts) && in_enum_order(k_video_codec_facts)
                  && in_enum_order(k_audio_codec_facts),
              "a format or codec without its row, or out of its enum's order");
static_assert(k_video_codec_facts.size() == k_video_codecs.size()
                  && k_audio_codec_facts.size() == k_audio_codecs.size(),
              "a codec without its row");
static_assert(k_video_format_facts.size() == k_video_formats.size()
                  && k_audio_format_facts.size() == k_audio_formats.size()
                  && k_image_format_facts.size() == k_image_formats.size(),
              "a format without its row");

constexpr const VideoFormatFacts& facts(VideoFormat f) { return k_video_format_facts[static_cast<std::size_t>(f)]; }
constexpr const AudioFormatFacts& facts(AudioFormat f) { return k_audio_format_facts[static_cast<std::size_t>(f)]; }
constexpr const ImageFormatFacts& facts(ImageFormat f) { return k_image_format_facts[static_cast<std::size_t>(f)]; }
constexpr const VideoCodecFacts& facts(VideoCodec c) { return k_video_codec_facts[static_cast<std::size_t>(c)]; }
constexpr const AudioCodecFacts& facts(AudioCodec c) { return k_audio_codec_facts[static_cast<std::size_t>(c)]; }

/// The row of ``table`` whose key is ``key``, or null.
template <typename Table>
const typename Table::value_type* row_named(const Table& table, std::string_view key)
{
    for (const auto& row : table) {
        if (row.key == key) {
            return &row;
        }
    }
    return nullptr;
}

/// Each picture codec's quality setting for smaller / balanced / better:
/// their scales differ, and these look alike across them.
std::string crf_for(VideoCodec codec, Quality quality)
{
    const auto pick = [quality](const char* smaller, const char* balanced, const char* better) {
        return std::string{quality == Quality::Smaller  ? smaller
                           : quality == Quality::Better ? better
                                                        : balanced};
    };
    switch (codec) {
    case VideoCodec::H265:
        return pick("32", "28", "23");
    case VideoCodec::Vp9:
        return pick("36", "31", "26");
    case VideoCodec::Av1:
        return pick("40", "34", "28");
    default:
        break;
    }
    return pick("28", "23", "18");
}

/// ProRes profiles: proxy, standard, HQ.
std::string prores_profile(Quality quality)
{
    return quality == Quality::Smaller ? "0" : quality == Quality::Better ? "3" : "2";
}

void push(std::vector<std::string>& args, std::initializer_list<std::string_view> more)
{
    for (const auto a : more) {
        args.emplace_back(a);
    }
}

std::string with_k(int kbps)
{
    return std::to_string(kbps) + "k";
}

VideoCodec resolved(VideoFormat format, VideoCodec codec)
{
    return codec == VideoCodec::Auto ? facts(format).video : codec;
}

AudioCodec resolved(VideoFormat format, AudioCodec codec)
{
    return codec == AudioCodec::Auto ? facts(format).audio : codec;
}

AudioCodec resolved(AudioFormat format, AudioCodec codec)
{
    return codec == AudioCodec::Auto ? facts(format).codec : codec;
}

/// A lossy sound codec's bit rate when none is chosen; 0 for the others.
int default_bitrate(AudioCodec codec)
{
    return facts(codec).bitrate_kbps;
}

/// ``codec`` (resolved) at ``kbps`` (0: its default).
void push_audio_codec(std::vector<std::string>& args, AudioCodec codec, int kbps)
{
    const int rate = kbps > 0 ? kbps : default_bitrate(codec);
    switch (codec) {
    case AudioCodec::Copy:
        push(args, {"-c:a", "copy"});
        return;
    case AudioCodec::Aac:
        push(args, {"-c:a", "aac", "-b:a", with_k(rate)});
        return;
    case AudioCodec::Mp3:
        push(args, {"-c:a", "libmp3lame", "-b:a", with_k(rate)});
        return;
    case AudioCodec::Opus:
        push(args, {"-c:a", "libopus", "-b:a", with_k(rate)});
        return;
    case AudioCodec::Vorbis:
        push(args, {"-c:a", "libvorbis", "-b:a", with_k(rate)});
        return;
    case AudioCodec::Flac:
        push(args, {"-c:a", "flac"});
        return;
    case AudioCodec::Alac:
        push(args, {"-c:a", "alac"});
        return;
    case AudioCodec::Pcm:
    case AudioCodec::Auto:
        break;
    }
    push(args, {"-c:a", "pcm_s16le"});
}

/// The scale filter for a chosen size, or empty to keep the picture's own.
/// The side that does not follow the choice is -2 (even, for video) or -1.
std::string scale_filter(int chosen, bool exact, const MediaProbe& source, bool even)
{
    const auto size = scaled_size(chosen, source, exact, even);
    if (!size) {
        return {};
    }
    const std::string follow = even ? "-2" : "-1";
    // A tall picture's preset is its width; anything else, its height.
    if (!exact && source.width > 0 && source.width < source.height) {
        return "scale=" + std::to_string(size->first) + ":" + follow;
    }
    return "scale=" + follow + ":" + std::to_string(size->second);
}

std::vector<std::string> video_args(const VideoConvert& choice, const MediaProbe& source)
{
    std::vector<std::string> args;
    const VideoCodec codec = resolved(choice.format, choice.video_codec);
    if (codec != VideoCodec::Copy) {
        if (const auto filter = scale_filter(choice.height, choice.exact_height, source, true);
            !filter.empty()) {
            push(args, {"-vf", filter});
        }
        if (choice.fps > 0) {
            push(args, {"-r", std::to_string(choice.fps)});
        }
    }
    const std::string crf = crf_for(codec, choice.quality);
    switch (codec) {
    case VideoCodec::Copy:
        push(args, {"-c:v", "copy"});
        break;
    case VideoCodec::H265:
        push(args, {"-c:v", "libx265", "-preset", "medium", "-crf", crf, "-pix_fmt", "yuv420p"});
        if (choice.format == VideoFormat::Mp4 || choice.format == VideoFormat::Mov) {
            // The tag Apple's players look for.
            push(args, {"-tag:v", "hvc1"});
        }
        break;
    case VideoCodec::Vp9:
        push(args, {"-c:v", "libvpx-vp9", "-crf", crf, "-b:v", "0", "-row-mt", "1", "-pix_fmt",
                    "yuv420p"});
        break;
    case VideoCodec::Av1:
        push(args, {"-c:v", "libsvtav1", "-preset", "8", "-crf", crf, "-pix_fmt", "yuv420p"});
        break;
    case VideoCodec::ProRes:
        push(args, {"-c:v", "prores_ks", "-profile:v", prores_profile(choice.quality),
                    "-pix_fmt", "yuv422p10le"});
        break;
    case VideoCodec::H264:
    case VideoCodec::Auto:
        push(args, {"-c:v", "libx264", "-preset", "medium", "-crf", crf, "-pix_fmt", "yuv420p"});
        break;
    }
    if (choice.format == VideoFormat::Mp4) {
        // Playable before it has fully loaded.
        push(args, {"-movflags", "+faststart"});
    }
    if (!source.has_audio) {
        push(args, {"-an"});
    } else {
        push_audio_codec(args, resolved(choice.format, choice.audio_codec),
                         choice.audio_bitrate_kbps);
    }
    return args;
}

std::vector<std::string> audio_args(const AudioConvert& choice)
{
    std::vector<std::string> args;
    // No picture: not even a cover art stream.
    push(args, {"-vn"});
    push_audio_codec(args, resolved(choice.format, choice.codec), choice.bitrate_kbps);
    if (choice.sample_rate > 0) {
        push(args, {"-ar", std::to_string(choice.sample_rate)});
    }
    if (choice.channels > 0) {
        push(args, {"-ac", std::to_string(choice.channels)});
    }
    return args;
}

std::vector<std::string> image_args(const ImageConvert& choice, const MediaProbe& source)
{
    std::vector<std::string> args;
    if (const auto filter = scale_filter(choice.height, choice.exact_height, source, false);
        !filter.empty()) {
        push(args, {"-vf", filter});
    }
    // One picture out, written as a file and not an image sequence.
    push(args, {"-frames:v", "1", "-update", "1"});
    switch (choice.format) {
    case ImageFormat::Jpeg:
        push(args, {"-c:v", "mjpeg", "-q:v", "3"});
        break;
    case ImageFormat::Png:
        push(args, {"-c:v", "png"});
        break;
    case ImageFormat::Webp:
        push(args, {"-c:v", "libwebp", "-quality", "85"});
        break;
    }
    return args;
}

std::optional<VideoCodec> video_codec_from(std::string_view key)
{
    for (const VideoCodec c : k_video_codecs) {
        if (codec_key(c) == key) {
            return c;
        }
    }
    return std::nullopt;
}

std::optional<AudioCodec> audio_codec_from(std::string_view key)
{
    for (const AudioCodec c : k_audio_codecs) {
        if (codec_key(c) == key) {
            return c;
        }
    }
    return std::nullopt;
}

/// The codec ``key`` names in ``j``; Auto when it is missing, nullopt when it
/// names none.
template <typename Codec, typename From>
std::optional<Codec> codec_or_auto(const nlohmann::json& j, const char* key, From from)
{
    const std::string name = util::to_lower(string_or(j, key));
    if (name.empty()) {
        return Codec::Auto;
    }
    return from(name);
}

/// A sound-only choice from ``j``, whatever kind the source is.
std::optional<ConvertChoice> audio_choice(const std::string& format, const nlohmann::json& j)
{
    AudioConvert c;
    if (!format.empty()) {
        const auto* row = row_named(k_audio_format_facts, format);
        if (row == nullptr) {
            return std::nullopt;
        }
        c.format = row->id;
    }
    c.bitrate_kbps = std::max(0, static_cast<int>(number_or(j, "bitrate", 0)));
    const auto codec = codec_or_auto<AudioCodec>(j, "codec", audio_codec_from);
    if (!codec || !audio_codec_fits(c.format, *codec)) {
        return std::nullopt;
    }
    c.codec = *codec;
    c.sample_rate = static_cast<int>(number_or(j, "sampleRate", 0));
    c.channels = static_cast<int>(number_or(j, "channels", 0));
    if (c.sample_rate < 0 || (c.sample_rate > 0 && (c.sample_rate < 8000 || c.sample_rate > 192000))
        || c.channels < 0 || c.channels > 2) {
        return std::nullopt;
    }
    return c;
}

/// A height from ``j``: 0, or a sensible picture height. Video heights are
/// even, as the codecs want them.
std::optional<int> height_from(const nlohmann::json& j, bool even)
{
    int height = static_cast<int>(number_or(j, "height", 0));
    if (height == 0) {
        return 0;
    }
    if (height < 16 || height > 8640) {
        return std::nullopt;
    }
    if (even) {
        height &= ~1;
    }
    return height;
}

/// A codec as a title calls it.
std::string_view codec_tag(VideoCodec codec)
{
    return facts(codec).tag;
}

std::string_view codec_tag(AudioCodec codec)
{
    return facts(codec).tag;
}

bool bool_or_false(const nlohmann::json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

/// ``title`` without a trailing extension (a dot and 2 to 5 letters or
/// digits), which a library title keeps from the file it was imported as.
std::string_view title_stem(std::string_view title)
{
    const auto dot = title.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return title;
    }
    const std::string_view ext = title.substr(dot + 1);
    if (ext.size() < 2 || ext.size() > 5) {
        return title;
    }
    for (const char c : ext) {
        const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
                           || (c >= 'A' && c <= 'Z');
        if (!alnum) {
            return title;
        }
    }
    return title.substr(0, dot);
}

} // namespace

std::vector<std::string> ffprobe_args(const fs::path& source)
{
    return {"-v", "error", "-print_format", "json", "-show_format", "-show_streams",
            source.string()};
}

std::optional<MediaProbe> parse_probe(std::string_view json_text)
{
    const nlohmann::json j = nlohmann::json::parse(json_text, nullptr, false);
    if (!j.is_object()) {
        return std::nullopt;
    }
    const auto streams = j.find("streams");
    const auto format = j.find("format");
    if (streams == j.end() || !streams->is_array() || format == j.end()
        || !format->is_object()) {
        return std::nullopt;
    }
    MediaProbe probe;
    for (const auto& stream : *streams) {
        if (!stream.is_object()) {
            continue;
        }
        const std::string type = string_or(stream, "codec_type");
        if (type == "video" && !probe.has_video) {
            // A music file's cover art is a video stream too: a picture that
            // is not a film, which ffprobe marks as attached.
            const auto disposition = stream.find("disposition");
            if (disposition != stream.end() && disposition->is_object()
                && number_or(*disposition, "attached_pic", 0) != 0) {
                continue;
            }
            probe.has_video = true;
            probe.width = static_cast<int>(number_or(stream, "width", 0));
            probe.height = static_cast<int>(number_or(stream, "height", 0));
            probe.rotation = stream_rotation(stream);
            if (probe.rotation == 90 || probe.rotation == 270) {
                // Stored sideways, played upright: give it as it plays.
                std::swap(probe.width, probe.height);
            }
            probe.video_codec = string_or(stream, "codec_name");
            probe.fps = parse_rate(string_or(stream, "avg_frame_rate"));
            if (probe.fps <= 0.0) {
                probe.fps = parse_rate(string_or(stream, "r_frame_rate"));
            }
        } else if (type == "audio" && !probe.has_audio) {
            probe.has_audio = true;
            probe.audio_codec = string_or(stream, "codec_name");
            probe.sample_rate = static_cast<int>(number_or(stream, "sample_rate", 0));
            probe.channels = static_cast<int>(number_or(stream, "channels", 0));
        }
    }
    probe.duration_s = number_or(*format, "duration", 0.0);
    probe.size_bytes = static_cast<std::int64_t>(number_or(*format, "size", 0.0));
    probe.bit_rate = static_cast<std::int64_t>(number_or(*format, "bit_rate", 0.0));
    return probe;
}

std::optional<ConvertChoice> convert_choice_from_json(std::string_view kind,
                                                      const nlohmann::json& j)
{
    if (!j.is_object()) {
        return std::nullopt;
    }
    const std::string format = util::to_lower(string_or(j, "format"));
    if (kind == "audio" || (kind == "video" && is_audio_format_key(format))) {
        return audio_choice(format, j);
    }
    if (kind == "video") {
        VideoConvert c;
        if (!format.empty()) {
            const auto* row = row_named(k_video_format_facts, format);
            if (row == nullptr) {
                return std::nullopt;
            }
            c.format = row->id;
        }
        const auto height = height_from(j, true);
        if (!height) {
            return std::nullopt;
        }
        c.height = *height;
        c.exact_height = bool_or_false(j, "exactHeight");
        c.fps = static_cast<int>(number_or(j, "fps", 0));
        if (c.fps < 0 || c.fps > 240) {
            return std::nullopt;
        }
        const std::string quality = util::to_lower(string_or(j, "quality"));
        if (quality == "smaller") {
            c.quality = Quality::Smaller;
        } else if (quality == "better") {
            c.quality = Quality::Better;
        } else if (!quality.empty() && quality != "balanced") {
            return std::nullopt;
        }
        const auto video = codec_or_auto<VideoCodec>(j, "videoCodec", video_codec_from);
        const auto audio = codec_or_auto<AudioCodec>(j, "audioCodec", audio_codec_from);
        if (!video || !audio || !video_codec_fits(c.format, *video)
            || !audio_codec_fits(c.format, *audio)) {
            return std::nullopt;
        }
        // A picture kept as it is cannot be resized or retimed.
        if (*video == VideoCodec::Copy && (c.height > 0 || c.fps > 0)) {
            return std::nullopt;
        }
        c.video_codec = *video;
        c.audio_codec = *audio;
        c.audio_bitrate_kbps = std::max(0, static_cast<int>(number_or(j, "audioBitrate", 0)));
        return c;
    }
    if (kind == "image") {
        ImageConvert c;
        if (!format.empty()) {
            const auto* row = row_named(k_image_format_facts, format == "jpeg" ? "jpg" : format);
            if (row == nullptr) {
                return std::nullopt;
            }
            c.format = row->id;
        }
        const auto height = height_from(j, false);
        if (!height) {
            return std::nullopt;
        }
        c.height = *height;
        c.exact_height = bool_or_false(j, "exactHeight");
        return c;
    }
    return std::nullopt;
}

bool is_audio_format_key(std::string_view format) noexcept
{
    return row_named(k_audio_format_facts, format) != nullptr;
}

bool video_codec_fits(VideoFormat format, VideoCodec codec) noexcept
{
    return codec == VideoCodec::Auto || codec == VideoCodec::Copy
           || holds(facts(format).video_codecs, codec);
}

bool audio_codec_fits(VideoFormat format, AudioCodec codec) noexcept
{
    return codec == AudioCodec::Auto || codec == AudioCodec::Copy
           || holds(facts(format).audio_codecs, codec);
}

bool audio_codec_fits(AudioFormat format, AudioCodec codec) noexcept
{
    return codec == AudioCodec::Auto || codec == AudioCodec::Copy || holds(facts(format).codecs, codec);
}

std::string_view codec_key(VideoCodec codec) noexcept
{
    return facts(codec).key;
}

std::string_view codec_key(AudioCodec codec) noexcept
{
    return facts(codec).key;
}

std::string codec_display_name(std::string_view ffmpeg_name)
{
    static constexpr std::pair<std::string_view, std::string_view> k_names[] = {
        {"h264", "H.264"},     {"hevc", "H.265"},      {"vp8", "VP8"},
        {"vp9", "VP9"},        {"av1", "AV1"},         {"prores", "ProRes"},
        {"mpeg4", "MPEG-4"},   {"mpeg2video", "MPEG-2"}, {"mjpeg", "JPEG"},
        {"png", "PNG"},        {"webp", "WebP"},       {"gif", "GIF"},
        {"bmp", "BMP"},        {"tiff", "TIFF"},       {"aac", "AAC"},
        {"mp3", "MP3"},        {"opus", "Opus"},       {"vorbis", "Vorbis"},
        {"flac", "FLAC"},      {"alac", "ALAC"},       {"ac3", "AC-3"},
        {"eac3", "E-AC-3"},
    };
    if (ffmpeg_name.empty()) {
        return {};
    }
    for (const auto& [key, name] : k_names) {
        if (ffmpeg_name == key) {
            return std::string{name};
        }
    }
    if (ffmpeg_name.starts_with("pcm_")) {
        return "PCM";
    }
    std::string out{ffmpeg_name};
    for (char& ch : out) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return out;
}

std::string format_display_name(std::string_view extension)
{
    if (!extension.empty() && extension.front() == '.') {
        extension.remove_prefix(1);
    }
    const std::string e = util::to_lower(extension);
    if (e == "webm") {
        return "WebM";
    }
    if (e == "webp") {
        return "WebP";
    }
    if (e == "ogg" || e == "oga") {
        return "Ogg";
    }
    if (e == "opus") {
        return "Opus";
    }
    if (e == "jpg" || e == "jpeg") {
        return "JPEG";
    }
    std::string out = e;
    for (char& ch : out) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return out;
}

std::vector<std::string> frame_args(const fs::path& source,
                                    double at_s,
                                    int height,
                                    const fs::path& dest)
{
    char at[32];
    std::snprintf(at, sizeof at, "%.3f", at_s > 0.0 ? at_s : 0.0);
    return {"-hide_banner", "-y", "-v", "error", "-ss", at, "-i", source.string(),
            "-frames:v", "1", "-vf", "scale=-2:" + std::to_string(height),
            "-update", "1", "-f", "image2", "-c:v", "mjpeg", "-q:v", "4", dest.string()};
}

std::string_view extension_of(VideoFormat format) noexcept
{
    return facts(format).extension;
}

std::string_view extension_of(AudioFormat format) noexcept
{
    return facts(format).extension;
}

std::string_view extension_of(ImageFormat format) noexcept
{
    return facts(format).extension;
}

std::string_view extension_of(const ConvertChoice& choice) noexcept
{
    return std::visit([](const auto& c) { return extension_of(c.format); }, choice);
}

std::string_view format_label(VideoFormat format) noexcept
{
    return facts(format).label;
}

std::string_view format_label(AudioFormat format) noexcept
{
    return facts(format).label;
}

std::string_view format_label(ImageFormat format) noexcept
{
    return facts(format).label;
}

std::optional<std::pair<int, int>> scaled_size(int chosen,
                                               const MediaProbe& source,
                                               bool exact,
                                               bool even) noexcept
{
    const bool known = source.width > 0 && source.height > 0;
    const auto follow = [&](double side) {
        long v = std::lround(side);
        if (even) {
            v -= v % 2;
        }
        return static_cast<int>(v);
    };
    if (chosen <= 0) {
        return std::nullopt;
    }
    if (exact) {
        if (chosen == source.height) {
            return std::nullopt;
        }
        const int width = known ? follow(static_cast<double>(source.width) * chosen / source.height)
                                : 0;
        return std::pair{width, chosen};
    }
    if (!known || chosen >= std::min(source.width, source.height)) {
        return std::nullopt;
    }
    if (source.width < source.height) {
        // Tall: the preset is its width.
        return std::pair{chosen, follow(static_cast<double>(source.height) * chosen / source.width)};
    }
    return std::pair{follow(static_cast<double>(source.width) * chosen / source.height), chosen};
}

std::vector<std::string> ffmpeg_args(const ConvertChoice& choice,
                                     const MediaProbe& source,
                                     const fs::path& source_path,
                                     const fs::path& dest_path)
{
    // -y: the staging file is ours to overwrite. No -nostdin: q on stdin
    // stops it cleanly. -v error: stderr is only what went wrong.
    std::vector<std::string> args = {"-hide_banner", "-y", "-v", "error", "-nostats",
                                     "-progress", "pipe:1", "-i", source_path.string()};
    std::vector<std::string> coding = std::visit(
        [&source](const auto& c) -> std::vector<std::string> {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, VideoConvert>) {
                return video_args(c, source);
            } else if constexpr (std::is_same_v<T, AudioConvert>) {
                return audio_args(c);
            } else {
                return image_args(c, source);
            }
        },
        choice);
    args.insert(args.end(), std::make_move_iterator(coding.begin()),
                std::make_move_iterator(coding.end()));
    args.emplace_back("-f");
    args.emplace_back(muxer_of(choice));
    args.push_back(dest_path.string());
    return args;
}

std::string_view muxer_of(const ConvertChoice& choice) noexcept
{
    return std::visit([](const auto& c) { return facts(c.format).muxer; }, choice);
}

std::optional<double> ffmpeg_progress(std::string_view line, double duration_s)
{
    if (line == "progress=end") {
        return 1.0;
    }
    std::string_view value;
    if (line.starts_with("out_time_us=")) {
        value = line.substr(12);
    } else if (line.starts_with("out_time_ms=")) {
        value = line.substr(12);
    } else {
        return std::nullopt;
    }
    if (duration_s <= 0.0) {
        return std::nullopt;
    }
    std::int64_t us = 0;
    if (std::from_chars(value.data(), value.data() + value.size(), us).ec != std::errc{}) {
        return std::nullopt;  // "N/A"
    }
    if (us < 0) {
        return 0.0;
    }
    const double fraction = static_cast<double>(us) / 1'000'000.0 / duration_s;
    return fraction > 1.0 ? 1.0 : fraction;
}

std::vector<std::string> convert_tags(const ConvertChoice& choice,
                                      const MediaProbe& source,
                                      std::string_view source_extension)
{
    std::vector<std::string> tags;
    const std::string from = util::to_lower(source_extension);
    const bool new_format = from != extension_of(choice)
                            // .jpeg and .jpg are one format.
                            && !(from == ".jpeg" && extension_of(choice) == ".jpg");
    std::visit(
        [&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, VideoConvert>) {
                if (const auto size = scaled_size(c.height, source, c.exact_height, true)) {
                    // Named by its shorter side, as 1080p is.
                    const int shorter = size->first > 0 ? std::min(size->first, size->second)
                                                        : size->second;
                    tags.push_back(std::to_string(shorter) + "p");
                }
                if (new_format) {
                    tags.emplace_back(format_label(c.format));
                }
                if (c.video_codec != VideoCodec::Auto && c.video_codec != VideoCodec::Copy) {
                    tags.emplace_back(codec_tag(c.video_codec));
                }
                if (c.fps > 0 && std::lround(source.fps) != c.fps) {
                    tags.push_back(std::to_string(c.fps) + " fps");
                }
            } else if constexpr (std::is_same_v<T, AudioConvert>) {
                if (new_format) {
                    tags.emplace_back(format_label(c.format));
                }
                const AudioCodec codec = resolved(c.format, c.codec);
                if (c.codec != AudioCodec::Auto && c.codec != AudioCodec::Copy
                    && codec != resolved(c.format, AudioCodec::Auto)) {
                    tags.emplace_back(codec_tag(codec));
                }
                if (c.bitrate_kbps > 0 && default_bitrate(codec) > 0) {
                    tags.push_back(std::to_string(c.bitrate_kbps) + " kbps");
                }
                if (c.channels == 1) {
                    tags.emplace_back("mono");
                }
            } else {
                // A picture's size is its pixels, as the dialog offers it.
                if (const auto size = scaled_size(c.height, source, c.exact_height, false)) {
                    tags.push_back(size->first > 0 ? std::to_string(size->first) + " \u00d7 "
                                                         + std::to_string(size->second)
                                                   : std::to_string(size->second) + "p");
                }
                if (new_format) {
                    tags.emplace_back(format_label(c.format));
                }
            }
        },
        choice);
    return tags;
}

std::string converted_title(std::string_view source_title,
                            const ConvertChoice& choice,
                            const MediaProbe& source,
                            std::string_view source_extension)
{
    std::string title{title_stem(source_title)};
    title += " (";
    const auto tags = convert_tags(choice, source, source_extension);
    if (tags.empty()) {
        title += "converted";
    }
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) {
            title += ", ";
        }
        title += tags[i];
    }
    title += ")";
    title += extension_of(choice);
    return title;
}

std::string replacement_title(std::string_view source_title, const ConvertChoice& choice)
{
    std::string title{title_stem(source_title)};
    title += extension_of(choice);
    return title;
}

std::string format_file_size(std::int64_t bytes)
{
    constexpr std::array<std::string_view, 3> k_units = {"KB", "MB", "GB"};
    double value = bytes > 0 ? static_cast<double>(bytes) / 1000.0 : 0.0;
    std::size_t unit = 0;
    while (value >= 1000.0 && unit + 1 < k_units.size()) {
        value /= 1000.0;
        ++unit;
    }
    char buffer[32];
    if (value < 10.0 && unit > 0) {
        std::snprintf(buffer, sizeof buffer, "%.1f %s", value, k_units[unit].data());
    } else {
        std::snprintf(buffer, sizeof buffer, "%.0f %s", value, k_units[unit].data());
    }
    return buffer;
}

std::string format_duration(double seconds)
{
    if (!(seconds > 0.0)) {
        return {};
    }
    const auto total = static_cast<long long>(std::llround(seconds));
    const long long h = total / 3600;
    const long long m = (total / 60) % 60;
    const long long s = total % 60;
    char buffer[32];
    if (h > 0) {
        std::snprintf(buffer, sizeof buffer, "%lld:%02lld:%02lld", h, m, s);
    } else {
        std::snprintf(buffer, sizeof buffer, "%lld:%02lld", m, s);
    }
    return buffer;
}

std::optional<std::int64_t> estimated_bytes(const ConvertChoice& choice,
                                            const MediaProbe& source)
{
    const auto* audio = std::get_if<AudioConvert>(&choice);
    if (audio == nullptr || source.duration_s <= 0.0) {
        return std::nullopt;
    }
    const AudioCodec codec = resolved(audio->format, audio->codec);
    const int lossy = default_bitrate(codec);
    const int kbps = lossy == 0 ? 0 : audio->bitrate_kbps > 0 ? audio->bitrate_kbps : lossy;
    if (kbps <= 0) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(kbps * 1000.0 / 8.0 * source.duration_s));
}

} // namespace seventhslide::media
