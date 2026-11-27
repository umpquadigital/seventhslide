#include <seventhslide/render/srt_output.hpp>

#include <QMetaObject>

#include <utility>

namespace seventhslide::render {

SrtSink::SrtSink(QObject* parent)
    : OffscreenSink(parent)
{
    // Told on the streaming thread; said on this one. Dropped if the sink
    // has gone by then, and the streamer stops before it does.
    streamer_.set_status_listener([this] {
        QMetaObject::invokeMethod(this, [this] { emit statusChanged(); }, Qt::QueuedConnection);
    });
}

SrtSink::~SrtSink()
{
    stop();
}

OffscreenSink::Status SrtSink::status() const
{
    const stream::StreamStatus s = streamer_.status();
    Status out;
    out.stream_state = QString::fromUtf8(stream::state_name(s.state));
    out.encoder = QString::fromStdString(s.encoder.name);
    out.hardware_encoder = s.encoder.hardware;
    out.bitrate_kbps = s.bitrate_kbps;
    out.error = QString::fromStdString(s.error);
    return out;
}

void SrtSink::set_audio(audio::Mixer* mixer, std::string output_id, bool on)
{
    if (link_.set(mixer, std::move(output_id), on) && is_live()) {
        link_.join();
    }
}

void SrtSink::push(const float* frames, std::size_t count)
{
    streamer_.push_audio(frames, count);
}

bool SrtSink::on_start(QSize size)
{
    std::string error;
    if (!streamer_.start(settings_, size.width(), size.height(), error)) {
        set_last_error(QString::fromStdString(error));
        return false;
    }
    link_.join();
    return true;
}

void SrtSink::on_stop()
{
    // The mixer first: after this nothing pushes into the streamer.
    link_.leave();
    streamer_.stop();
}

bool SrtSink::on_resize(QSize size)
{
    // The encoder and the stream's header are fixed at a size: start again.
    std::string error;
    if (!streamer_.start(settings_, size.width(), size.height(), error)) {
        set_last_error(QString::fromStdString(error));
        return false;
    }
    set_output_size(size);
    mark_dirty();
    return true;
}

void SrtSink::deliver(const QImage& image, bool changed)
{
    if (!changed) {
        return;  // the streamer repeats the last picture
    }
    // Opaque B,G,R,X in memory, as the composite already is on an opaque
    // letterbox; anything else is converted first.
    const bool direct = image.format() == QImage::Format_RGB32
                        || image.format() == QImage::Format_ARGB32
                        || image.format() == QImage::Format_ARGB32_Premultiplied;
    const QImage pixels = direct ? image : image.convertToFormat(QImage::Format_RGB32);
    streamer_.submit_picture(pixels.constBits(), pixels.width(), pixels.height(),
                             static_cast<int>(pixels.bytesPerLine()));
}

}  // namespace seventhslide::render
