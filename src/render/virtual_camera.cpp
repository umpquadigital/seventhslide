#include <seventhslide/render/virtual_camera.hpp>

#include <seventhslide/render/pixel_pack.hpp>

#include <QColor>
#include <QDir>

#include <algorithm>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace seventhslide::render {
namespace {

// Everything in here is the v4l2 side; off Linux it would be unused, which
// warnings-as-errors refuses.
#if defined(__linux__)

[[nodiscard]] QString card_label(const QString& card, const QString& path)
{
    QString c = card.trimmed();
    if (c.isEmpty() || c.startsWith(QLatin1String("Dummy video device"))) {
        c = QStringLiteral("Virtual Camera");
    }
    return QStringLiteral("%1 · %2").arg(c, path);
}

[[nodiscard]] bool query_loopback(int fd, QString* card_out)
{
    v4l2_capability cap {};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        return false;
    }
    const QByteArray driver(reinterpret_cast<const char*>(cap.driver));
    if (driver != "v4l2 loopback" && driver != "v4l2loopback") {
        return false;
    }
    if (card_out) {
        *card_out = QString::fromUtf8(reinterpret_cast<const char*>(cap.card));
    }
    return true;
}

bool write_all(int fd, const char* data, qsizetype size)
{
    qsizetype left = size;
    while (left > 0) {
        const ssize_t n = ::write(fd, data, static_cast<size_t>(left));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        data += n;
        left -= n;
    }
    return true;
}

#endif  // __linux__

}  // namespace

QSize clamp_camera_size(QSize size)
{
    int w = even_dim(size.width() > 0 ? size.width() : 1920);
    int h = even_dim(size.height() > 0 ? size.height() : 1080);
    constexpr int k_max_w = 1920;
    constexpr int k_max_h = 1080;
    if (w > k_max_w || h > k_max_h) {
        const qreal s = std::min(qreal(k_max_w) / qreal(w), qreal(k_max_h) / qreal(h));
        w = even_dim(qRound(qreal(w) * s));
        h = even_dim(qRound(qreal(h) * s));
    }
    return {w, h};
}

QString virtual_camera_missing_message()
{
#if defined(__linux__)
    return QStringLiteral(
        "No virtual camera device found. Install and load the v4l2loopback "
        "kernel module (for example: sudo modprobe v4l2loopback "
        "exclusive_caps=1 card_label=SeventhSlide), then switch this destination on.");
#else
    return QStringLiteral(
        "Virtual cameras are not supported on this system yet: SeventhSlide makes "
        "them on Linux only so far. Send this output over NDI to reach other apps.");
#endif
}

bool is_virtual_camera_device(const QString& path)
{
#if defined(__linux__)
    if (path.isEmpty()) {
        return false;
    }
    const int fd = ::open(path.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    const bool ok = query_loopback(fd, nullptr);
    ::close(fd);
    return ok;
#else
    Q_UNUSED(path);
    return false;
#endif
}

std::vector<VirtualCameraDeviceInfo> list_virtual_camera_devices()
{
    std::vector<VirtualCameraDeviceInfo> out;
#if defined(__linux__)
    QDir dir(QStringLiteral("/dev"));
    const auto names =
        dir.entryList({QStringLiteral("video*")}, QDir::System, QDir::Name);
    out.reserve(static_cast<std::size_t>(names.size()));
    for (const QString& name : names) {
        const QString path = dir.filePath(name);
        const int fd = ::open(path.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        QString card;
        const bool ok = query_loopback(fd, &card);
        ::close(fd);
        if (!ok) {
            continue;
        }
        VirtualCameraDeviceInfo info;
        info.path = path;
        info.card = card;
        info.label = card_label(card, path);
        out.push_back(std::move(info));
    }
#endif
    return out;
}

VirtualCameraSink::VirtualCameraSink(QObject* parent)
    : OffscreenSink(parent)
{
}

VirtualCameraSink::~VirtualCameraSink()
{
    stop();
}

QSize VirtualCameraSink::negotiate_size(QSize want) const
{
    return clamp_camera_size(want);
}

bool VirtualCameraSink::on_start(QSize size)
{
    // One branch per platform, decided here rather than by
    // virtual_camera_implemented() at run time: a check that is always false
    // leaves the rest unreachable, which MSVC warns about (C4702).
#if defined(__linux__)
    const QString device_path = target();
    if (device_path.isEmpty()) {
        set_last_error(virtual_camera_missing_message());
        return false;
    }
    if (!is_virtual_camera_device(device_path)) {
        set_last_error(virtual_camera_missing_message());
        return false;
    }
    const int fd = ::open(device_path.toUtf8().constData(), O_WRONLY);
    if (fd < 0) {
        set_last_error(QStringLiteral("Could not open virtual camera %1.").arg(device_path));
        return false;
    }
    fd_ = fd;
    if (!configure_format(size)) {
        set_last_error(QStringLiteral("Could not configure virtual camera %1.").arg(device_path));
        on_stop();
        return false;
    }
    ensure_writer();
    frames_.put({});
    // Live once this returns: the microphone comes with the picture.
    sync_microphone(true);
    return true;
#else
    Q_UNUSED(size);
    set_last_error(virtual_camera_missing_message());
    return false;
#endif
}

void VirtualCameraSink::set_audio(audio::Mixer* mixer, std::string output_id, bool on)
{
    // Unchanged, the microphone stays as it is: an app recording from it
    // keeps its connection.
    if (mixer == mixer_ && output_id == output_id_ && on == audio_on_) {
        return;
    }
    mixer_ = mixer;
    output_id_ = std::move(output_id);
    audio_on_ = on;
    sync_microphone(is_live());
}

void VirtualCameraSink::sync_microphone(bool live)
{
    mic_.stop();
    if (!live || mixer_ == nullptr || !audio_on_ || output_id_.empty()) {
        return;
    }
    const QString name = mic_name_.isEmpty() ? QStringLiteral("SeventhSlide") : mic_name_;
    (void)mic_.start(*mixer_, output_id_, name.toStdString());
}

void VirtualCameraSink::on_stop()
{
    // The sound first; it does not need the device, but goes with it.
    mic_.stop();
    stop_writer();
#if defined(__linux__)
    if (fd_ >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(fd_, VIDIOC_STREAMOFF, &type);
        ::close(fd_);
    }
#endif
    fd_ = -1;
    yuyv_.clear();
}

bool VirtualCameraSink::on_resize(QSize size)
{
    return configure_format(size);
}

void VirtualCameraSink::deliver(const QImage& image, bool changed)
{
    // Packed on the writer's thread: a keepalive sends what it packed last.
    frames_.put(changed ? image : QImage());
}

bool VirtualCameraSink::configure_format(QSize size)
{
#if defined(__linux__)
    if (fd_ < 0) {
        return false;
    }
    const bool running = writer_.joinable();
    if (running) {
        stop_writer();
    }
    int off = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(fd_, VIDIOC_STREAMOFF, &off);

    const QSize candidates[] = {
        clamp_camera_size(size),
        QSize(1280, 720),
        QSize(640, 480),
    };
    for (QSize want : candidates) {
        v4l2_format fmt {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        fmt.fmt.pix.width = static_cast<__u32>(want.width());
        fmt.fmt.pix.height = static_cast<__u32>(want.height());
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        fmt.fmt.pix.bytesperline = static_cast<__u32>(want.width() * 2);
        fmt.fmt.pix.sizeimage = static_cast<__u32>(want.width() * want.height() * 2);
        fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
            continue;
        }
        if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
            continue;
        }
        set_output_size(QSize(even_dim(int(fmt.fmt.pix.width)),
                              even_dim(int(fmt.fmt.pix.height))));

        v4l2_streamparm parm {};
        parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        parm.parm.output.timeperframe.numerator = 1;
        parm.parm.output.timeperframe.denominator = 30;
        ioctl(fd_, VIDIOC_S_PARM, &parm);

        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(fd_, VIDIOC_STREAMON, &type);
        fill_black_yuyv();
        mark_dirty();
        if (running) {
            ensure_writer();
        }
        return true;
    }
    if (running) {
        ensure_writer();
    }
    return false;
#else
    Q_UNUSED(size);
    return false;
#endif
}

void VirtualCameraSink::fill_black_yuyv()
{
    QImage black(output_size(), QImage::Format_RGB32);
    black.fill(Qt::black);
    yuyv_ = encode_yuyv(black);
}

void VirtualCameraSink::ensure_writer()
{
    if (writer_.joinable()) {
        return;
    }
    frames_.reset();
    writer_ = std::thread([this]() { writer_loop(); });
}

void VirtualCameraSink::stop_writer()
{
    frames_.stop();
    if (writer_.joinable()) {
        writer_.join();
    }
    frames_.reset();
}

void VirtualCameraSink::writer_loop()
{
    for (;;) {
        auto next = frames_.wait();
        if (next.stopped) {
            return;
        }
        if (!next.image.isNull()) {
            yuyv_ = encode_yuyv(next.image);
            next.image = QImage();  // the presenter's buffer, given back at once
        }
#if defined(__linux__)
        if (fd_ >= 0 && !yuyv_.isEmpty()) {
            write_all(fd_, yuyv_.constData(), yuyv_.size());
        }
#endif
    }
}

}  // namespace seventhslide::render
