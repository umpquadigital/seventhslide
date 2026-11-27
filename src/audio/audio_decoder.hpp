#pragma once

// FFmpeg, kept to this file and its .cpp: a file's audio track decoded to the
// engine's format (audio_format.hpp). Used on one thread at a time - the
// player's decode thread.

#include <string>
#include <vector>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace seventhslide::audio {

class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    /// Open ``path`` and its best audio track. False when FFmpeg cannot read
    /// it or it has no sound; then nothing else may be called.
    bool open(const std::string& path);

    /// The file's length; 0 when it does not say.
    [[nodiscard]] double duration_sec() const noexcept { return duration_sec_; }

    enum class Status {
        /// Frames were appended.
        Data,
        /// The file has ended: every frame was delivered.
        End,
        /// The file cannot be decoded further.
        Failed,
    };

    /// Decode what comes next and append it to ``out`` (interleaved stereo
    /// frames at the engine's rate). Mono plays at full level on both sides.
    /// A sound track that starts after the file does (a video's, often) is
    /// preceded by silence, so its frames sit where the picture's do.
    Status decode(std::vector<float>& out);

    /// The next frames start at ``position_sec`` (0 is the start of the file,
    /// which may come before the first sound), to the frame where the file
    /// allows. A file that cannot seek is opened
    /// again and decoded up to there. False when it cannot even be opened
    /// again; then nothing else may be called.
    bool seek(double position_sec);

private:
    void close();
    bool ensure_resampler(const AVFrame* frame);
    bool convert(const AVFrame* frame, std::vector<float>& out);
    void flush_resampler(std::vector<float>& out);
    void append(const float* frames, int count, std::vector<float>& out);

    std::string path_;
    AVFormatContext* format_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    SwrContext* resampler_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    int stream_ = -1;
    double time_base_ = 0.0;
    double start_sec_ = 0.0;
    double duration_sec_ = 0.0;
    bool draining_ = false;

    // What the resampler was set up for; a frame that differs sets it up again.
    int in_format_ = -1;
    int in_rate_ = 0;
    int in_channels_ = 0;
    /// The resampler writes one channel and append() doubles it.
    bool mono_ = false;
    std::vector<float> converted_;

    /// After opening or a seek, the first frames are placed at this position:
    /// what decodes before it is dropped, and silence fills up to a sound
    /// that starts after it.
    bool placing_ = false;
    double place_at_sec_ = 0.0;
};

} // namespace seventhslide::audio
