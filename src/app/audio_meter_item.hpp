#pragma once

// An Audio Mixer strip's level meter: a bar per channel, rising from the
// meter's floor (-60 dB) to full scale, green, then yellow from -20 dB and red
// from -9 dB, as OBS draws them, with each channel's held peak as a line.

#include <seventhslide/audio/mixer.hpp>

#include <QtQml/qqmlregistration.h>
#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>

Q_MOC_INCLUDE("mixer_controller.hpp")

namespace seventhslide::app {

class MixerController;

/**
 * QML: ``AudioMeterItem { mixer: session.mixer; kind: "stack"; channelId: id }``
 *
 * Reads the strip's meter from the engine on the mixer's meter tick (never
 * through model roles), and repaints only when the reading moved. It counts
 * as a meter showing (MixerController::add_meter_view) while it is visible,
 * so the tick stops when the panel is hidden. The colours are the Style's,
 * handed in.
 */
class AudioMeterItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(seventhslide::app::MixerController* mixer READ mixer WRITE setMixer NOTIFY mixerChanged)
    /// "stack" or "output".
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    Q_PROPERTY(QString channelId READ channelId WRITE setChannelId NOTIFY channelIdChanged)
    /// A muted strip: its bars in mutedColor.
    Q_PROPERTY(bool dimmed READ dimmed WRITE setDimmed NOTIFY dimmedChanged)
    Q_PROPERTY(QColor trackColor MEMBER track_color_ NOTIFY colorsChanged)
    Q_PROPERTY(QColor lowColor MEMBER low_color_ NOTIFY colorsChanged)
    Q_PROPERTY(QColor midColor MEMBER mid_color_ NOTIFY colorsChanged)
    Q_PROPERTY(QColor highColor MEMBER high_color_ NOTIFY colorsChanged)
    Q_PROPERTY(QColor mutedColor MEMBER muted_color_ NOTIFY colorsChanged)

public:
    /// Where yellow and red start (OBS's defaults).
    static constexpr double k_mid_db = -20.0;
    static constexpr double k_high_db = -9.0;

    explicit AudioMeterItem(QQuickItem* parent = nullptr);
    ~AudioMeterItem() override;

    [[nodiscard]] MixerController* mixer() const { return mixer_; }
    void setMixer(MixerController* mixer);
    [[nodiscard]] QString kind() const { return kind_; }
    void setKind(const QString& kind);
    [[nodiscard]] QString channelId() const { return channel_id_; }
    void setChannelId(const QString& id);
    [[nodiscard]] bool dimmed() const { return dimmed_; }
    void setDimmed(bool dimmed);

    void paint(QPainter* painter) override;

    /// What it last read (tests).
    [[nodiscard]] const audio::MeterReading& reading() const noexcept { return reading_; }

signals:
    void mixerChanged();
    void kindChanged();
    void channelIdChanged();
    void dimmedChanged();
    void colorsChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    /// Count as a meter showing while visible and given a mixer.
    void follow();
    void read();

    QPointer<MixerController> mixer_;
    /// The mixer this item counts as a meter view of; null when it does not.
    QPointer<MixerController> counted_;
    QString kind_;
    QString channel_id_;
    bool dimmed_ = false;
    audio::MeterReading reading_{};
    QColor track_color_{Qt::black};
    QColor low_color_{Qt::green};
    QColor mid_color_{Qt::yellow};
    QColor high_color_{Qt::red};
    QColor muted_color_{Qt::gray};
};

} // namespace seventhslide::app
