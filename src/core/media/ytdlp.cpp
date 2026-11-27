#include "seventhslide/media/ytdlp.hpp"

#include "seventhslide/util/strings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <set>

namespace seventhslide::media {
namespace {

namespace fs = std::filesystem;

std::string string_or(const nlohmann::json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

double number_or(const nlohmann::json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : 0.0;
}

/// A number yt-dlp's template printed, or nullopt for "NA" and the like.
std::optional<double> number_of(std::string_view word)
{
    double value = 0.0;
    const auto result = std::from_chars(word.data(), word.data() + word.size(), value);
    if (result.ec != std::errc{} || result.ptr != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::string_view> words(std::string_view line)
{
    std::vector<std::string_view> out;
    while (!line.empty()) {
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string_view::npos) {
            break;
        }
        line.remove_prefix(start);
        const auto end = line.find_first_of(" \t");
        out.push_back(line.substr(0, end));
        if (end == std::string_view::npos) {
            break;
        }
        line.remove_prefix(end);
    }
    return out;
}

/// ``value`` when it is one of ``allowed``, else ``fallback``: nothing
/// else reaches yt-dlp's command line.
std::string one_of(const std::string& value, std::initializer_list<const char*> allowed,
                   const char* fallback)
{
    for (const char* a : allowed) {
        if (value == a) {
            return value;
        }
    }
    return fallback;
}

} // namespace

std::vector<std::string> ytdlp_lookup_args(std::string_view url)
{
    return {"--ignore-config", "-J",    "--flat-playlist", "--no-warnings",
            "--color",         "never", "--",              std::string{url}};
}

std::optional<YtdlpInfo> parse_ytdlp_info(std::string_view json_text)
{
    const nlohmann::json j = nlohmann::json::parse(json_text, nullptr, false);
    if (!j.is_object() || (!j.contains("title") && !j.contains("id"))) {
        return std::nullopt;
    }
    YtdlpInfo info;
    info.title = string_or(j, "title");
    info.uploader = string_or(j, "uploader");
    if (info.uploader.empty()) {
        info.uploader = string_or(j, "channel");
    }
    info.thumbnail = string_or(j, "thumbnail");
    const std::string type = string_or(j, "_type");
    if (type == "playlist" || type == "multi_video") {
        info.playlist = true;
        const auto entries = j.find("entries");
        if (entries != j.end() && entries->is_array()) {
            for (const auto& e : *entries) {
                if (!e.is_object()) {
                    continue;
                }
                info.entries.push_back({string_or(e, "title"), number_or(e, "duration")});
            }
        }
        return info;
    }
    info.duration_s = number_or(j, "duration");
    std::set<int, std::greater<>> heights;
    const auto formats = j.find("formats");
    if (formats != j.end() && formats->is_array()) {
        for (const auto& f : *formats) {
            if (!f.is_object()) {
                continue;
            }
            const std::string vcodec = string_or(f, "vcodec");
            const int height = static_cast<int>(number_or(f, "height"));
            // A picture: one with a video codec, or a height where the
            // codec is not said (some sites).
            if (vcodec != "none" && height > 0) {
                heights.insert(height);
            }
        }
    } else if (const int height = static_cast<int>(number_or(j, "height")); height > 0) {
        heights.insert(height);
    }
    info.heights.assign(heights.begin(), heights.end());
    info.has_video = !info.heights.empty();
    return info;
}

std::vector<std::string> ytdlp_download_args(std::string_view url,
                                             const DownloadChoice& choice,
                                             const fs::path& out_dir,
                                             const fs::path& ffmpeg)
{
    std::vector<std::string> args = {
        // A config file of the user's could rename or move what it writes.
        "--ignore-config", "--newline", "--color", "never", "--no-warnings", "--no-simulate",
        // --print makes it quiet, which would hide the progress.
        "--progress",
        choice.playlist ? "--yes-playlist" : "--no-playlist",
        "-o", (out_dir / "%(id)s.%(ext)s").string(),
        // Picture and sound together, as many bytes as yt-dlp expects in
        // all, and which item of how many.
        "--progress-template",
        "download:" + std::string(k_ytdlp_progress_tag)
            + " %(progress.downloaded_bytes)s %(progress.total_bytes)s"
              " %(progress.total_bytes_estimate)s %(info.filesize_approx)s"
              " %(info.playlist_index)s %(info.n_entries)s",
        "--print", "after_move:" + std::string(k_ytdlp_file_tag) + "\t%(title)s\t%(filepath)s",
    };
    if (!ffmpeg.empty()) {
        args.emplace_back("--ffmpeg-location");
        args.push_back(ffmpeg.string());
    }
    if (choice.audio_only) {
        const std::string format = one_of(choice.audio_format,
                                          {"m4a", "mp3", "opus", "flac", "wav"}, "m4a");
        // As it comes where the site has it so; otherwise made so.
        const std::string pick = format == "m4a"    ? "ba[ext=m4a]/ba/b"
                                 : format == "opus" ? "ba[acodec=opus]/ba/b"
                                                    : "ba/b";
        for (const std::string& a : {std::string{"-f"}, pick, std::string{"-x"},
                                    std::string{"--audio-format"}, format}) {
            args.push_back(a);
        }
    } else {
        const std::string container = one_of(choice.container, {"mp4", "webm", "mkv"}, "mp4");
        std::string sort =
            choice.max_height > 0 ? "res:" + std::to_string(choice.max_height) : "res";
        if (container == "mp4") {
            sort += ",vcodec:h264,acodec:aac";
        } else if (container == "webm") {
            sort += ",vcodec:vp9,acodec:opus";
        }
        for (const std::string& a : {std::string{"-f"}, std::string{"bv*+ba/b"}, std::string{"-S"},
                                    sort, std::string{"--merge-output-format"}, container}) {
            args.push_back(a);
        }
    }
    args.emplace_back("--");
    args.emplace_back(url);
    return args;
}

std::optional<double> YtdlpProgress::read(std::string_view line)
{
    const auto parts = words(line);
    if (parts.size() < 7 || parts[0] != k_ytdlp_progress_tag) {
        return std::nullopt;
    }
    const auto downloaded = number_of(parts[1]);
    if (!downloaded) {
        return std::nullopt;
    }
    const double stream_total = number_of(parts[2]).value_or(number_of(parts[3]).value_or(0.0));
    const double item_total = number_of(parts[4]).value_or(0.0);
    const int index = static_cast<int>(number_of(parts[5]).value_or(1.0));
    const int count = std::max(1, static_cast<int>(number_of(parts[6]).value_or(1.0)));

    if (index != item_) {
        item_ = index;
        base_bytes_ = 0.0;
        last_bytes_ = 0.0;
        last_total_ = 0.0;
    } else if (*downloaded + 1.0 < last_bytes_) {
        // The next stream of the same item (sound after picture) began.
        base_bytes_ += last_total_ > 0.0 ? last_total_ : last_bytes_;
    }
    last_bytes_ = *downloaded;
    last_total_ = stream_total;

    double within = 0.0;
    if (item_total > 0.0) {
        within = (base_bytes_ + *downloaded) / item_total;
    } else if (stream_total > 0.0) {
        within = *downloaded / stream_total;
    }
    within = std::clamp(within, 0.0, 1.0);
    const double fraction =
        std::clamp((std::clamp(index, 1, count) - 1 + within) / count, 0.0, 1.0);
    // Never backwards: a stream whose size was not known can only be
    // guessed at.
    best_ = std::max(best_, fraction);
    return best_;
}

std::optional<std::pair<std::string, fs::path>> parse_ytdlp_file(std::string_view line)
{
    const std::string tag = std::string(k_ytdlp_file_tag) + "\t";
    if (!line.starts_with(tag)) {
        return std::nullopt;
    }
    line.remove_prefix(tag.size());
    // The path is last; a title may hold a tab of its own.
    const auto tab = line.rfind('\t');
    if (tab == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view path = line.substr(tab + 1);
    if (path.empty()) {
        return std::nullopt;
    }
    return std::pair{std::string{line.substr(0, tab)}, fs::path{std::string{path}}};
}

bool ytdlp_error_suggests_update(std::string_view error)
{
    const std::string e = util::to_lower(error);
    for (const char* sign : {"unable to extract", "please report this issue",
                             "update to the latest version", "unsupported url",
                             "signature extraction failed", "nsig extraction failed",
                             "requested format is not available"}) {
        if (e.find(sign) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string ytdlp_release_asset(std::string_view platform_id, std::string_view arch)
{
    const bool arm = arch == "arm64" || arch == "aarch64";
    if (platform_id == "windows") {
        return arm ? "yt-dlp_arm64.exe" : "yt-dlp.exe";
    }
    if (platform_id == "macos") {
        return "yt-dlp_macos";
    }
    if (platform_id == "linux") {
        return arm ? "yt-dlp_linux_aarch64" : "yt-dlp_linux";
    }
    return {};
}

std::map<std::string, std::string> parse_sha256sums(std::string_view text)
{
    std::map<std::string, std::string> out;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto parts = words(line);
        if (parts.size() == 2 && parts[0].size() == 64) {
            std::string name{parts[1]};
            // "*name" marks a binary file in sha256sum's own output.
            if (!name.empty() && name.front() == '*') {
                name.erase(0, 1);
            }
            out.emplace(std::move(name), util::to_lower(parts[0]));
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return out;
}

} // namespace seventhslide::media
