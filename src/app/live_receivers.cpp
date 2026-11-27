// The app's live receivers (see live_receiver.hpp): NDI, capture devices and
// streams through FFmpeg, and DeckLink inputs.

#include "live_receiver.hpp"

#include "ffmpeg_live_input.hpp"

#include <seventhslide/render/bmd_input.hpp>
#include <seventhslide/render/ndi_receive.hpp>
#include <seventhslide/render/ndi_runtime.hpp>

#include <QElapsedTimer>
#include <QMetaObject>
#include <QTimer>

#include <seventhslide/audio/stream_source.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <utility>

namespace seventhslide::app {
namespace {

/// How often a receiver looks at its source: is it lost, has what it says
/// changed.
constexpr int k_watch_ms = 500;

/// What the app's receivers share. Pictures arrive on a receiving thread;
/// each is announced on the GUI thread (at most one announcement waiting at a
/// time) and kept there, so the last stays when the source stops sending, or
/// when the input it came from is made again. A source that has sent
/// pictures and sends none for k_live_lost_ms is lost until it sends again.
class HeldReceiver : public LiveReceiver {
public:
    HeldReceiver()
    {
        connect(&watch_, &QTimer::timeout, this, [this] { watch(); });
        watch_.start(k_watch_ms);
        since_.start();
    }

    [[nodiscard]] QImage frame() const final { return held_; }
    [[nodiscard]] QString problem() const final { return held_.isNull() ? why() : QString(); }
    [[nodiscard]] bool lost() const final { return lost_; }

protected:
    /// The receiving side's latest picture; null when it has none.
    [[nodiscard]] virtual QImage latest() const = 0;
    /// Why there is no picture.
    [[nodiscard]] virtual QString why() const = 0;

    /// The receiving thread: a new picture.
    void post()
    {
        if (posted_.exchange(true)) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this] {
                posted_ = false;
                take();
            },
            Qt::QueuedConnection);
    }

    /// What ``problem()`` says may have changed: announced if it has.
    void recheck()
    {
        if (const auto now = problem(); now != said_) {
            said_ = now;
            emit problemChanged();
        }
    }

private:
    void take()
    {
        QImage next = latest();
        if (next.isNull()) {
            return;  // announced by an input since made again
        }
        held_ = std::move(next);
        since_.start();
        emit frameChanged();
        set_lost(false);
        recheck();
    }

    void watch()
    {
        if (since_.hasExpired(k_live_lost_ms)) {
            set_lost(true);
        }
        recheck();
    }

    void set_lost(bool lost)
    {
        if (lost_ != lost) {
            lost_ = lost;
            emit lostChanged();
        }
    }

    QTimer watch_;
    std::atomic<bool> posted_{false};
    QImage held_;
    QElapsedTimer since_;
    bool lost_ = false;
    QString said_;
};

/// An NDI source, received on the NdiReceiver's thread. NDI finds the source
/// again by itself when it comes back.
class NdiLiveReceiver final : public HeldReceiver {
public:
    NdiLiveReceiver(const model::LiveSource& source, LiveQuality quality)
        : name_(QString::fromStdString(source.name))
    {
        receiver_ = std::make_unique<render::NdiReceiver>(
            source.name, [this] { post(); },
            quality == LiveQuality::Preview ? render::NdiReceiver::Bandwidth::Preview
                                            : render::NdiReceiver::Bandwidth::Full);
        recheck();
    }

    ~NdiLiveReceiver() override
    {
        // Its thread stops before anything it calls back into goes.
        receiver_.reset();
    }

    [[nodiscard]] audio::AudioSource* sound() override { return &receiver_->sound(); }
    [[nodiscard]] Stats stats() const override
    {
        const auto s = receiver_->stats();
        return {s.size, s.frame_rate};
    }

private:
    [[nodiscard]] QImage latest() const override { return receiver_->frame(); }
    [[nodiscard]] QString why() const override
    {
        if (!render::ndi_runtime_available()) {
            return render::ndi_missing_message();
        }
        if (!receiver_->available()) {
            return tr("This NDI runtime cannot receive sources.");
        }
        return tr("Waiting for “%1” on the network.").arg(name_);
    }

    QString name_;
    std::unique_ptr<render::NdiReceiver> receiver_;
};

/// A capture device on this computer, found by its name and read through
/// FFmpeg. Not plugged in, or its input failing (unplugged, perhaps plugged
/// in again where another device was), it is looked for every two seconds:
/// read again where it is now, and let go of while it is nowhere, so its old
/// place, taken by another device, does not show that one under its name.
class DeviceLiveReceiver final : public HeldReceiver {
public:
    explicit DeviceLiveReceiver(const model::LiveSource& source)
        : name_(source.name)
    {
        connect(&look_, &QTimer::timeout, this, [this] { find(); });
        look_.start(FfmpegLiveInput::k_retry_ms);
        find();
        recheck();
    }

    ~DeviceLiveReceiver() override { input_.reset(); }

    /// The same for the receiver's whole life, whichever input feeds it: a
    /// stack's strip holds it.
    [[nodiscard]] audio::AudioSource* sound() override { return &sound_; }
    [[nodiscard]] Stats stats() const override
    {
        if (!input_) {
            return {};
        }
        const auto s = input_->stats();
        return {s.size, s.frame_rate};
    }

private:
    [[nodiscard]] QImage latest() const override { return input_ ? input_->frame() : QImage(); }
    [[nodiscard]] QString why() const override
    {
        const QString name = QString::fromStdString(name_);
        if (!input_) {
            return tr("Waiting for \u201c%1\u201d to be plugged in.").arg(name);
        }
        const auto problem = QString::fromStdString(input_->problem());
        return problem.isEmpty() ? tr("Waiting for \u201c%1\u201d.").arg(name)
                                 : QStringLiteral("\u201c%1\u201d: %2").arg(name, problem);
    }

    void find()
    {
        if (input_ && input_->problem().empty()) {
            return;  // reading, or opening
        }
        for (const auto& device : capture_devices()) {
            if (device.name == name_) {
                if (!input_ || device.url != url_) {
                    input_.reset();
                    url_ = device.url;
                    input_ = std::make_unique<FfmpegLiveInput>(
                        FfmpegLiveInput::Spec{capture_input_format(), url_, device.options},
                        [this] { post(); },
                        &sound_);
                }
                return;
            }
        }
        input_.reset();
        url_.clear();
    }

    std::string name_;
    std::string url_;
    QTimer look_;
    audio::StreamSource sound_;
    std::unique_ptr<FfmpegLiveInput> input_;
};

/// A DeckLink input, by its device's name. Not connected (or the driver not
/// installed, or the input in use), it is looked for every two seconds until
/// it is.
class DeckLinkLiveReceiver final : public HeldReceiver {
public:
    explicit DeckLinkLiveReceiver(const model::LiveSource& source)
        : name_(QString::fromStdString(source.name))
    {
        connect(&look_, &QTimer::timeout, this, [this] {
            // A receiver that never started is made again; the sound it
            // feeds stays the one the stack's strip holds.
            if (!receiver_->available()) {
                open();
            }
        });
        look_.start(FfmpegLiveInput::k_retry_ms);
        open();
        recheck();
    }

    ~DeckLinkLiveReceiver() override { receiver_.reset(); }

    /// The same for the receiver's whole life, whichever card receiver
    /// feeds it: a stack's strip holds it.
    [[nodiscard]] audio::AudioSource* sound() override { return &sound_; }
    [[nodiscard]] Stats stats() const override
    {
        const auto s = receiver_->stats();
        return {s.size, s.frame_rate};
    }

private:
    [[nodiscard]] QImage latest() const override { return receiver_->frame(); }
    [[nodiscard]] QString why() const override { return receiver_->problem(); }

    void open()
    {
        receiver_.reset();
        receiver_ = std::make_unique<render::BmdReceiver>(name_, [this] { post(); }, &sound_);
    }

    QString name_;
    QTimer look_;
    audio::StreamSource sound_;
    std::unique_ptr<render::BmdReceiver> receiver_;
};

/// A stream (SRT, RTMP, RTSP, HLS, ...), read through FFmpeg from its
/// address; connected again whenever it drops.
class StreamLiveReceiver final : public HeldReceiver {
public:
    explicit StreamLiveReceiver(const model::LiveSource& source)
        : name_(QString::fromStdString(source.name))
    {
        // A stalled stream is noticed within five seconds, and an RTSP
        // camera's pictures come over TCP, which loses none to the network.
        std::map<std::string, std::string> options{{"rw_timeout", "5000000"}};
        if (source.url.starts_with("rtsp")) {
            options.emplace("rtsp_transport", "tcp");
        }
        input_ = std::make_unique<FfmpegLiveInput>(
            FfmpegLiveInput::Spec{{}, source.url, std::move(options)}, [this] { post(); }, &sound_);
        recheck();
    }

    ~StreamLiveReceiver() override { input_.reset(); }

    [[nodiscard]] audio::AudioSource* sound() override { return &sound_; }
    [[nodiscard]] Stats stats() const override
    {
        const auto s = input_->stats();
        return {s.size, s.frame_rate};
    }

private:
    [[nodiscard]] QImage latest() const override { return input_->frame(); }
    [[nodiscard]] QString why() const override
    {
        const auto problem = QString::fromStdString(input_->problem());
        return problem.isEmpty() ? tr("Connecting to \u201c%1\u201d.").arg(name_)
                                 : QStringLiteral("\u201c%1\u201d: %2").arg(name_, problem);
    }

    QString name_;
    audio::StreamSource sound_;
    std::unique_ptr<FfmpegLiveInput> input_;
};

} // namespace

LiveReceiverFactory live_receiver_factory()
{
    return [](const model::LiveSource& source, LiveQuality quality) -> std::unique_ptr<LiveReceiver> {
        if (source.kind == model::k_live_kind_ndi) {
            return std::make_unique<NdiLiveReceiver>(source, quality);
        }
        if (source.kind == model::k_live_kind_device) {
            // A device has no smaller stream: its thumbnail is the picture.
            return std::make_unique<DeviceLiveReceiver>(source);
        }
        if (source.kind == model::k_live_kind_decklink) {
            return std::make_unique<DeckLinkLiveReceiver>(source);
        }
        if (source.kind == model::k_live_kind_stream && !source.url.empty()) {
            return std::make_unique<StreamLiveReceiver>(source);
        }
        return nullptr;
    };
}

} // namespace seventhslide::app
