#pragma once

// Downloading media with yt-dlp: what a link holds (``ytdlp_lookup_args``,
// ``parse_ytdlp_info``), the command that downloads it for a choice, how far
// it is (``YtdlpProgress``), the files it made, and which release file the
// app keeps up to date on each platform. Nothing here runs a program or
// opens a connection: the app does, with these.
//
// A link always follows ``--`` on yt-dlp's command line, so what was pasted
// is never read as one of its options; and yt-dlp reads no config file, so
// one of the user's cannot move or rename what it writes.
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::media {

/// What yt-dlp says a link holds (``yt-dlp -J``).
struct YtdlpInfo {
    struct Entry {
        std::string title;
        /// Seconds; 0 when unknown.
        double duration_s = 0.0;

        [[nodiscard]] bool operator==(const Entry&) const = default;
    };

    /// A playlist (or a channel): ``entries`` are its items.
    bool playlist = false;
    std::string title;
    std::string uploader;
    /// Seconds; 0 when unknown, and for a playlist.
    double duration_s = 0.0;
    std::string thumbnail;
    /// The picture heights a video comes in, highest first; empty for a
    /// playlist (each item has its own) and for sound alone.
    std::vector<int> heights;
    /// A video has a picture at all; false for sound alone.
    bool has_video = false;
    std::vector<Entry> entries;

    [[nodiscard]] bool operator==(const YtdlpInfo&) const = default;
};

/// yt-dlp's arguments to describe ``url`` as JSON on stdout, without
/// downloading; a playlist's items are listed, not looked into.
[[nodiscard]] std::vector<std::string> ytdlp_lookup_args(std::string_view url);

/// Read what ``ytdlp_lookup_args`` made yt-dlp print. Nullopt when it is not
/// a description yt-dlp writes.
[[nodiscard]] std::optional<YtdlpInfo> parse_ytdlp_info(std::string_view json_text);

/// What to download: the sound alone, or the picture and sound at a height.
struct DownloadChoice {
    bool audio_only = false;
    /// The highest picture wanted; 0 for the best there is.
    int max_height = 0;
    /// Every item of a playlist; otherwise only the one the link names.
    bool playlist = false;
    /// A video's container: ``mp4`` (H.264 and AAC first), ``webm`` (VP9
    /// and Opus first) or ``mkv`` (whatever is best); anything else is mp4.
    std::string container = "mp4";
    /// Sound alone: ``m4a``, ``mp3``, ``opus``, ``flac`` or ``wav``; anything
    /// else is m4a.
    std::string audio_format = "m4a";

    [[nodiscard]] bool operator==(const DownloadChoice&) const = default;
};

/// What a download line marks: a progress line, and a finished file.
inline constexpr std::string_view k_ytdlp_progress_tag = "SSPROG";
inline constexpr std::string_view k_ytdlp_file_tag = "SSFILE";

/// yt-dlp's arguments to download ``url`` as ``choice`` says into
/// ``out_dir`` (each file named by its id), merging picture and sound with
/// the ffmpeg at ``ffmpeg`` (empty: yt-dlp's own search). Video comes in
/// ``choice.container`` where the site allows, sound alone as
/// ``choice.audio_format``. It
/// prints a ``k_ytdlp_progress_tag`` line as it goes (``YtdlpProgress``) and
/// a ``k_ytdlp_file_tag`` line for each file done (``parse_ytdlp_file``).
[[nodiscard]] std::vector<std::string> ytdlp_download_args(std::string_view url,
                                                           const DownloadChoice& choice,
                                                           const std::filesystem::path& out_dir,
                                                           const std::filesystem::path& ffmpeg);

/// How far a download is, from its progress lines: 0 to 1 across every item
/// of a playlist. A video's picture and sound arrive one after the other;
/// where yt-dlp knows the size of both together, the two read as one, so
/// the fraction only moves forward within an item.
class YtdlpProgress {
public:
    /// One line of output: the fraction so far, or nullopt for any other
    /// line.
    [[nodiscard]] std::optional<double> read(std::string_view line);

private:
    int item_ = 0;
    double base_bytes_ = 0.0;
    double last_bytes_ = 0.0;
    double last_total_ = 0.0;
    double best_ = 0.0;
};

/// A ``k_ytdlp_file_tag`` line: the item's title and the file yt-dlp left.
[[nodiscard]] std::optional<std::pair<std::string, std::filesystem::path>> parse_ytdlp_file(
    std::string_view line);

/// Whether a yt-dlp error reads as the site having changed under it - which a
/// newer yt-dlp usually fixes - rather than the link or the network.
[[nodiscard]] bool ytdlp_error_suggests_update(std::string_view error);

/// The release file the app keeps for ``platform_id`` (``linux``, ``macos``,
/// ``windows``) on CPU ``arch`` (``x86_64``, ``arm64``/``aarch64``): the
/// standalone build, which needs no Python. Empty where there is none.
[[nodiscard]] std::string ytdlp_release_asset(std::string_view platform_id,
                                              std::string_view arch);

/// A release's ``SHA2-256SUMS``: file name to lower-case hex digest.
[[nodiscard]] std::map<std::string, std::string> parse_sha256sums(std::string_view text);

} // namespace seventhslide::media
