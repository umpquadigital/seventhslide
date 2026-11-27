#pragma once

// Library list models for bibles, images, videos, and strings.

#include <seventhslide/model/library.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seventhslide::app {

class BibleListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    /// The number of Bibles, for bindings: a ``rowCount()`` call in a binding
    /// is not re-evaluated when the list changes.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { BibleIdRole = Qt::UserRole + 1, NameRole, AbbreviationRole, EnabledRole };
    explicit BibleListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    void set_bibles(std::vector<model::Bible> bibles);
    Q_INVOKABLE [[nodiscard]] QString nameAt(int row) const;
    /// "" when ``row`` is out of range or the Bible has no abbreviation.
    Q_INVOKABLE [[nodiscard]] QString abbreviationAt(int row) const;
    /// -1 when ``row`` is out of range. Pairs with ``nameAt`` so QML can
    /// look a translation up by id without reaching for a role constant.
    Q_INVOKABLE [[nodiscard]] qint64 idAt(int row) const;
    /// Offered for lookup; false when ``row`` is out of range.
    Q_INVOKABLE [[nodiscard]] bool enabledAt(int row) const;

signals:
    void countChanged();

private:
    /// The Bible at ``row``, or null past either end.
    [[nodiscard]] const model::Bible* bible_at(int row) const;

    std::vector<model::Bible> bibles_;
};

/// Per-verse rows for the library Bibles tab (search results / chapter browse).
class BibleVerseListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns its list models; QML reads their roles by name")
public:
    enum Roles { VerseNumRole = Qt::UserRole + 1, TextRole };
    Q_ENUM(Roles)
    explicit BibleVerseListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    void set_verses(std::vector<model::BibleVerse> verses);
    void clear();

private:
    std::vector<model::BibleVerse> verses_;
};

class ImageListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Roles { FilenameRole = Qt::UserRole + 1, TitleRole };
    explicit ImageListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    void set_images(std::vector<model::MediaFile> images);

private:
    std::vector<model::MediaFile> images_;
};

/// One row in the enter-folder media browser (parent / folder / file).
struct MediaBrowserEntry {
    enum class Kind { Parent, Folder, File };
    Kind kind = Kind::File;
    std::int64_t id = -1;       ///< Folder id (Folder) or item id (File); unused for Parent.
    std::string name;
    std::string filename;       ///< File only.
    int count = 0;              ///< Folder direct item count.
    std::string thumb_url;      ///< ``image://mediathumb`` thumb for images and videos; empty for sound/folder.
};

class MediaBrowserModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns its list models; QML reads their roles by name")
public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        IdRole,
        NameRole,
        FilenameRole,
        CountRole,
        ThumbUrlRole
    };
    Q_ENUM(Roles)
    explicit MediaBrowserModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    /// The rows now. ``view`` names what they are a view of (a folder and a
    /// search): the same one changes by the rows that changed, and stays
    /// where it is scrolled; another starts over, from the top.
    void set_entries(std::vector<MediaBrowserEntry> entries, const QString& view = {});
    /// Ordered entry ids for ``folder`` or ``file`` kinds (skips parent / id < 0).
    Q_INVOKABLE QVariantList entryIds(const QString& kind_filter) const;

private:
    friend struct RowSteps;

    std::vector<MediaBrowserEntry> entries_;
    QString view_;
};

} // namespace seventhslide::app
