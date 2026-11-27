#pragma once

// QAbstractListModel over Session outputs, every one or only those switched
// on, with their on / blank / freeze state plus open/screen roles for the
// output strip.

#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <string>
#include <utility>
#include <vector>

namespace seventhslide::app {

class OutputListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns its list models; QML reads their roles by name")
    /// The rows' ids and names, in order: what a view looks a row up by.
    Q_PROPERTY(QStringList ids READ ids NOTIFY rowsReplaced)
    Q_PROPERTY(QStringList names READ names NOTIFY rowsReplaced)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IsBlankRole,
        IsFrozenRole,
        /// Switched on, on this computer (Settings > Outputs).
        IsOnRole,
        IsOpenRole,
        ScreenKeyRole,
        ScreenLabelRole,
        /// Stable id: the key for anything stored per output (theme maps).
        IdRole,
        /// One of model::k_output_icons.
        IconRole,
        /// The canvas's width over its height (16:9 while it has no size):
        /// the shape of the output's previews.
        AspectRole,
        /// The canvas's size in pixels; 0 while it has none.
        CanvasWidthRole,
        CanvasHeightRole,
    };
    Q_ENUM(Roles)

    /// With ``on_only``, the rows are the outputs switched on
    /// (``Session::outputs_on``): what the operator's previews and the remote
    /// show. Otherwise every output.
    explicit OutputListModel(session::Session& session, bool on_only = false,
                             QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] QStringList ids() const;
    [[nodiscard]] QStringList names() const;

    /// Reset when the rows changed (added, removed, renamed, reordered), else
    /// refresh them in place. Returns true when the rows changed.
    bool reload();
    /// Refresh all rows without resetting the model (nav / flag updates).
    void notify_rows_changed();

    /// Host state of the output ``output_id``.
    void set_open(const QString& output_id, bool open);
    void set_screen(const QString& output_id, const QString& key, const QString& label);
    void clear_host_state();

signals:
    /// Rows were added, removed, renamed or reordered (the model was reset).
    void rowsReplaced();

private:
    [[nodiscard]] const std::vector<model::OutputConfig>& rows() const;

    session::Session& session_;
    bool on_only_ = false;
    /// The rows as last reset: their ids and names.
    std::vector<std::pair<std::string, std::string>> rows_;
    QHash<QString, bool> open_;
    QHash<QString, QString> screen_keys_;
    QHash<QString, QString> screen_labels_;
};

} // namespace seventhslide::app
