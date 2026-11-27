#pragma once

// A song's authors as the song editor edits them: one row per author, with
// the name and the credits (``[{type, label, lang}]``) it carries. The song
// editor's state creates one (``AuthorListModel {}``) and saves ``authors()``;
// core validates what is saved, so these rules only shape the editing.

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariantList>

#include <vector>

namespace seventhslide::app {

class AuthorListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        /// The author's credits, ``[{type, label, lang}]``.
        CreditsRole,
    };
    Q_ENUM(Roles)

    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const noexcept { return static_cast<int>(rows_.size()); }

    /// Replace the rows with ``authors`` (``[{name, roles}]`` as a song stores
    /// them): names trimmed, unnamed authors and credits without a type left
    /// out.
    Q_INVOKABLE void load(const QVariantList& authors);
    Q_INVOKABLE void clear();
    /// A new row named ``name``, with no credits.
    Q_INVOKABLE void append(const QString& name);
    Q_INVOKABLE void remove(int row);

    Q_INVOKABLE [[nodiscard]] QString nameAt(int row) const;
    Q_INVOKABLE void setName(int row, const QString& name);
    [[nodiscard]] QVariantList credits_at(int row) const;

    // Credits are a set: one person can be words *and* arrangement.
    /// Credit ``type`` (``words``, ``music``, ...), or take it away again.
    Q_INVOKABLE void toggleCredit(int row, const QString& type);
    /// A free-text credit (``other`` with ``label``), unless it is there already.
    Q_INVOKABLE void addCustomCredit(int row, const QString& label);
    /// A free-text credit is told apart by its wording, so that is what toggles.
    Q_INVOKABLE void toggleCustomCredit(int row, const QString& label);

    /// What Save stores: ``[{name, roles}]``, names trimmed, unnamed rows left out.
    Q_INVOKABLE [[nodiscard]] QVariantList authors() const;

signals:
    void countChanged();

private:
    struct Row {
        QString name;
        QVariantList credits;
    };

    [[nodiscard]] bool has_row(int row) const noexcept
    {
        return row >= 0 && row < count();
    }
    /// The first credit ``matches`` picks, or -1.
    template <class Match>
    [[nodiscard]] int find_credit(int row, Match matches) const;
    void set_credits(int row, QVariantList credits);

    std::vector<Row> rows_;
};

} // namespace seventhslide::app
