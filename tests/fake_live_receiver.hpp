#pragma once

// A live receiver for the tests: it "receives" a source as one solid colour,
// at once, and keeps a sound a test can push to. The real one (NDI) needs the
// runtime and a source on the network; test_ndi_receive.cpp and
// test_live_sources.cpp's [receive] case use it with this machine's own.

#include "live_receiver.hpp"

#include <seventhslide/audio/stream_source.hpp>

#include <QColor>
#include <QImage>
#include <QPointer>

#include <memory>
#include <vector>

namespace seventhslide::test {

class FakeLiveReceiver final : public app::LiveReceiver {
public:
    FakeLiveReceiver(model::LiveSource source, bool on, app::LiveQuality quality = app::LiveQuality::Full)
        : source_(std::move(source))
        , quality_(quality)
    {
        if (on) {
            draw(QColor(Qt::blue));
        }
    }

    [[nodiscard]] QImage frame() const override { return frame_; }
    [[nodiscard]] audio::AudioSource* sound() override { return &sound_; }
    [[nodiscard]] QString problem() const override
    {
        return frame_.isNull() ? QStringLiteral("Waiting for %1.").arg(QString::fromStdString(source_.name))
                               : QString();
    }

    [[nodiscard]] Stats stats() const override
    {
        return {frame_.size(), frame_.isNull() ? 0.0 : 29.97};
    }

    void draw(const QColor& colour)
    {
        // As NDI's preview stream is: smaller.
        QImage next = quality_ == app::LiveQuality::Preview ? QImage(640, 360, QImage::Format_RGB32)
                                                            : QImage(1280, 720, QImage::Format_RGB32);
        next.fill(colour);
        const bool first = frame_.isNull();
        frame_ = std::move(next);
        emit frameChanged();
        if (first) {
            emit problemChanged();
        }
    }

    model::LiveSource source_;
    app::LiveQuality quality_;
    QImage frame_;
    audio::StreamSource sound_;
};

/// Makes FakeLiveReceivers and keeps a weak pointer to each.
struct FakeLiveReceivers {
    std::vector<QPointer<FakeLiveReceiver>> made;
    /// What each was made for, in order (they may be gone).
    std::vector<app::LiveQuality> qualities;
    /// False: each source stays off the network until the test draws it.
    bool on = true;

    [[nodiscard]] app::LiveReceiverFactory factory()
    {
        return [this](const model::LiveSource& source, app::LiveQuality quality) {
            auto r = std::make_unique<FakeLiveReceiver>(source, on, quality);
            made.emplace_back(r.get());
            qualities.push_back(quality);
            return std::unique_ptr<app::LiveReceiver>(std::move(r));
        };
    }
    [[nodiscard]] int running() const
    {
        int n = 0;
        for (const auto& r : made) {
            n += r ? 1 : 0;
        }
        return n;
    }
    [[nodiscard]] FakeLiveReceiver* last() const { return made.empty() ? nullptr : made.back().data(); }
};

} // namespace seventhslide::test
