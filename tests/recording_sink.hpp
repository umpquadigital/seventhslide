#pragma once

// An offscreen sink with no transport, for tests of what the OffscreenSink
// base does (fades, sizes, sound alone) without a runtime CI does not have.

#include <seventhslide/render/offscreen_sink.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QSize>

namespace seventhslide::test {

/// A sink with no transport: it just keeps whatever the base class hands it.
/// The base asks for each picture without waiting, so every read below first
/// waits for the picture asked for to be delivered.
class RecordingSink final : public render::OffscreenSink {
public:
    RecordingSink() = default;
    ~RecordingSink() override { stop(); }

    /// Wait for the picture asked for to be delivered.
    void settle() const { (void)settled(); }

    [[nodiscard]] int deliveries() const { return settled().deliveries_; }
    [[nodiscard]] int changed_deliveries() const { return settled().changed_deliveries_; }
    [[nodiscard]] const QImage& last() const { return settled().last_; }
    /// Pixel buffer of the image that was handed over, before any copy. Two
    /// destinations sharing a composite are handed the very same buffer.
    [[nodiscard]] const uchar* last_bits() const { return settled().last_bits_; }

    bool keyed = false;  // transparent letterbox, as NDI/Blackmagic alpha does
    int tick_budget_ms = 33;  // the cadence a transition has to fit inside

protected:
    bool on_start(QSize /*size*/) override { return true; }
    void on_stop() override {}
    void deliver(const QImage& image, bool changed) override
    {
        ++deliveries_;
        last_bits_ = image.constBits();
        if (changed) {
            ++changed_deliveries_;
            // Deep copy, as every real transport does on its way to a wire
            // format - and only when the picture changed, so a keepalive tick
            // stays as cheap here as it is in production.
            last_ = image.copy();
        }
    }
    [[nodiscard]] QColor letterbox_color() const override
    {
        return keyed ? QColor(Qt::transparent) : QColor(Qt::black);
    }
    [[nodiscard]] int tick_interval_ms() const override { return tick_budget_ms; }

private:
    /// This sink, once the picture in flight has been delivered.
    const RecordingSink& settled() const
    {
        QElapsedTimer waited;
        waited.start();
        while (delivering() && waited.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return *this;
    }

    int deliveries_ = 0;
    int changed_deliveries_ = 0;
    QImage last_;
    const uchar* last_bits_ = nullptr;
};

}  // namespace seventhslide::test
