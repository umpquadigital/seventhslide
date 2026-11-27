#include "output_list_model.hpp"

namespace seventhslide::app {

OutputListModel::OutputListModel(session::Session& session, bool on_only, QObject* parent)
    : QAbstractListModel(parent)
    , session_(session)
    , on_only_(on_only)
{
}

const std::vector<model::OutputConfig>& OutputListModel::rows() const
{
    return on_only_ ? session_.outputs_on() : session_.outputs();
}

int OutputListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(rows().size());
}

QVariant OutputListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto& outputs = rows();
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= outputs.size()) {
        return {};
    }
    const auto& oc = outputs[row];
    const QString qid = QString::fromStdString(oc.id);

    switch (role) {
    case IdRole:
        return qid;
    case NameRole:
        return QString::fromStdString(oc.name);
    case IconRole:
        return QString::fromStdString(oc.icon);
    case AspectRole:
        return oc.canvas_width > 0 && oc.canvas_height > 0
                   ? static_cast<double>(oc.canvas_width) / oc.canvas_height
                   : 16.0 / 9.0;
    case CanvasWidthRole:
        return oc.canvas_width;
    case CanvasHeightRole:
        return oc.canvas_height;
    case IsBlankRole:
        return session_.output_is_blank(oc.id);
    case IsFrozenRole:
        return session_.output_is_frozen(oc.id);
    case IsOnRole:
        return session_.output_is_on(oc.id);
    case IsOpenRole:
        return open_.value(qid, false);
    case ScreenKeyRole:
        return screen_keys_.value(qid);
    case ScreenLabelRole: {
        const auto label = screen_labels_.value(qid);
        if (!label.isEmpty()) {
            return label;
        }
        const auto key = screen_keys_.value(qid);
        return key.isEmpty() ? QStringLiteral("Primary") : key;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> OutputListModel::roleNames() const
{
    return {
        {IdRole, "outputId"},
        {NameRole, "name"},
        {IsBlankRole, "isBlank"},
        {IsFrozenRole, "isFrozen"},
        {IsOnRole, "isOn"},
        {IsOpenRole, "isOpen"},
        {ScreenKeyRole, "screenKey"},
        {ScreenLabelRole, "screenLabel"},
        {IconRole, "icon"},
        {AspectRole, "aspect"},
        {CanvasWidthRole, "canvasWidth"},
        {CanvasHeightRole, "canvasHeight"},
    };
}

QStringList OutputListModel::ids() const
{
    QStringList out;
    for (const auto& oc : rows()) {
        out.push_back(QString::fromStdString(oc.id));
    }
    return out;
}

QStringList OutputListModel::names() const
{
    QStringList out;
    for (const auto& oc : rows()) {
        out.push_back(QString::fromStdString(oc.name));
    }
    return out;
}

bool OutputListModel::reload()
{
    std::vector<std::pair<std::string, std::string>> now;
    now.reserve(rows().size());
    for (const auto& oc : rows()) {
        now.emplace_back(oc.id, oc.name);
    }
    if (now == rows_) {
        notify_rows_changed();
        return false;
    }
    beginResetModel();
    rows_ = std::move(now);
    endResetModel();
    emit rowsReplaced();
    return true;
}

void OutputListModel::notify_rows_changed()
{
    if (rowCount() <= 0) {
        return;
    }
    const auto top = index(0);
    const auto bottom = index(rowCount() - 1);
    emit dataChanged(top, bottom);
}

void OutputListModel::set_open(const QString& output_id, bool open)
{
    open_.insert(output_id, open);
}

void OutputListModel::set_screen(const QString& output_id, const QString& key, const QString& label)
{
    if (key.isEmpty()) {
        screen_keys_.remove(output_id);
        screen_labels_.remove(output_id);
        return;
    }
    screen_keys_.insert(output_id, key);
    screen_labels_.insert(output_id, label);
}

void OutputListModel::clear_host_state()
{
    open_.clear();
    screen_keys_.clear();
    screen_labels_.clear();
}

} // namespace seventhslide::app
