#include "mixer_channel_model.hpp"

#include "list_model_replace.hpp"

#include <seventhslide/media/audio_levels.hpp>

#include <algorithm>
#include <utility>

namespace seventhslide::app {

MixerChannelModel::MixerChannelModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MixerChannelModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant MixerChannelModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || static_cast<std::size_t>(index.row()) >= rows_.size()) {
        return {};
    }
    const Row& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ChannelIdRole:
        return QString::fromStdString(row.id);
    case NameRole:
        return row.name;
    case IconRole:
        return row.icon;
    case GainDbRole:
        return row.mix.gain_db;
    case GainTextRole:
        return QString::fromStdString(media::format_gain_db(row.mix.gain_db));
    case FaderPositionRole:
        return media::fader_position(row.mix.gain_db);
    case MutedRole:
        return row.mix.muted;
    case ListenRole:
        return row.mix.listen;
    default:
        return {};
    }
}

QHash<int, QByteArray> MixerChannelModel::roleNames() const
{
    return {
        {ChannelIdRole, "channelId"},
        {NameRole, "name"},
        {IconRole, "icon"},
        {GainDbRole, "gainDb"},
        {GainTextRole, "gainText"},
        {FaderPositionRole, "faderPosition"},
        {MutedRole, "muted"},
        {ListenRole, "listen"},
    };
}

void MixerChannelModel::set_rows(std::vector<Row> rows)
{
    if (rows == rows_) {
        return;
    }
    switch (classify_list_replace(rows_, rows, [](const Row& r) { return r.id; })) {
    case ListReplace::Noop:
        return;
    case ListReplace::DataChanged:
        rows_ = std::move(rows);
        emit dataChanged(index(0), index(static_cast<int>(rows_.size()) - 1));
        return;
    case ListReplace::Reset:
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
        return;
    }
}

void MixerChannelModel::set_mix(std::string_view id, const library::ChannelMix& mix)
{
    const auto it = std::ranges::find(rows_, id, &Row::id);
    if (it == rows_.end() || it->mix == mix) {
        return;
    }
    it->mix = mix;
    const auto at = index(static_cast<int>(it - rows_.begin()));
    emit dataChanged(at, at, {GainDbRole, GainTextRole, FaderPositionRole, MutedRole, ListenRole});
}

bool MixerChannelModel::contains(std::string_view id) const
{
    return std::ranges::find(rows_, id, &Row::id) != rows_.end();
}

} // namespace seventhslide::app
