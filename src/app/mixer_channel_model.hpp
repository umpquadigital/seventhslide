#pragma once

// The Audio Mixer's strips of one tab: a row per stack (and under a stack with
// a Preview, its cue strip), or per output, in the order the app lists them,
// with each strip's level, mute and Listen.
//
// Meter levels are not roles: they move 60 times a second, and AudioMeterItem
// reads them from the engine itself.

#include <seventhslide/library/audio_mix_store.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::app {

class MixerChannelModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Roles {
        ChannelIdRole = Qt::UserRole + 1,
        NameRole,
        IconRole,
        /// At most 0; -Infinity at the fader's bottom.
        GainDbRole,
        /// "0.0 dB", "-inf dB".
        GainTextRole,
        /// Where the fader sits for the level, 0 to 1 (OBS's taper).
        FaderPositionRole,
        MutedRole,
        ListenRole,
    };

    struct Row {
        std::string id;
        QString name;
        QString icon;
        library::ChannelMix mix;
        /// A stack's cue strip: what is cued in its Preview, heard on Listen
        /// alone.
        bool cue = false;

        [[nodiscard]] bool operator==(const Row&) const = default;
    };

    explicit MixerChannelModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Show ``rows``. The same strips (a rename, a new level) change in place,
    /// so a view keeps its scroll; others reset.
    void set_rows(std::vector<Row> rows);
    /// One strip's level, mute and Listen.
    void set_mix(std::string_view id, const library::ChannelMix& mix);

    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    [[nodiscard]] bool contains(std::string_view id) const;

private:
    std::vector<Row> rows_;
};

} // namespace seventhslide::app
