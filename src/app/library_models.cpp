#include "library_models.hpp"
#include "list_model_replace.hpp"
#include "qml_ids.hpp"

#include <tuple>

namespace seventhslide::app {

// --- BibleListModel ---

BibleListModel::BibleListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &BibleListModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &BibleListModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &BibleListModel::countChanged);
}

int BibleListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(bibles_.size());
}

QVariant BibleListModel::data(const QModelIndex& index, int role) const
{
    const auto* b = index.isValid() ? bible_at(index.row()) : nullptr;
    if (b == nullptr) {
        return {};
    }
    switch (role) {
    case BibleIdRole:
        return qml_id(b->id);
    case NameRole:
        return QString::fromStdString(b->name);
    case AbbreviationRole:
        return QString::fromStdString(b->abbreviation);
    case EnabledRole:
        return b->enabled;
    default:
        return {};
    }
}

QHash<int, QByteArray> BibleListModel::roleNames() const
{
    return {{BibleIdRole, "bibleId"},
            {NameRole, "name"},
            {AbbreviationRole, "abbreviation"},
            {EnabledRole, "enabled"}};
}

void BibleListModel::set_bibles(std::vector<model::Bible> bibles)
{
    const auto kind = classify_list_replace(bibles_, bibles, [](const model::Bible& b) {
        return b.id.value_or(-1);
    });
    if (kind == ListReplace::Noop) {
        return;
    }
    if (kind == ListReplace::DataChanged) {
        bibles_ = std::move(bibles);
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    beginResetModel();
    bibles_ = std::move(bibles);
    endResetModel();
}

const model::Bible* BibleListModel::bible_at(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= bibles_.size()) {
        return nullptr;
    }
    return &bibles_[static_cast<std::size_t>(row)];
}

QString BibleListModel::nameAt(int row) const
{
    const auto* b = bible_at(row);
    return b ? QString::fromStdString(b->name) : QString();
}

QString BibleListModel::abbreviationAt(int row) const
{
    const auto* b = bible_at(row);
    return b ? QString::fromStdString(b->abbreviation) : QString();
}

qint64 BibleListModel::idAt(int row) const
{
    // A Bible with no id yet reads as -1, the same as no row.
    const auto* b = bible_at(row);
    return b ? qml_id(b->id) : qint64(-1);
}

bool BibleListModel::enabledAt(int row) const
{
    const auto* b = bible_at(row);
    return b != nullptr && b->enabled;
}

// --- BibleVerseListModel ---

BibleVerseListModel::BibleVerseListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int BibleVerseListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(verses_.size());
}

QVariant BibleVerseListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= verses_.size()) {
        return {};
    }
    const auto& v = verses_[row];
    switch (role) {
    case VerseNumRole:
        return v.verse_num;
    case TextRole:
        return QString::fromStdString(v.text);
    default:
        return {};
    }
}

QHash<int, QByteArray> BibleVerseListModel::roleNames() const
{
    return {{VerseNumRole, "verseNum"}, {TextRole, "text"}};
}

void BibleVerseListModel::set_verses(std::vector<model::BibleVerse> verses)
{
    const auto kind = classify_list_replace(verses_, verses, [](const model::BibleVerse& v) {
        return v.verse_num;
    });
    if (kind == ListReplace::Noop) {
        return;
    }
    if (kind == ListReplace::DataChanged) {
        verses_ = std::move(verses);
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    beginResetModel();
    verses_ = std::move(verses);
    endResetModel();
}

void BibleVerseListModel::clear()
{
    if (verses_.empty()) {
        return;
    }
    beginResetModel();
    verses_.clear();
    endResetModel();
}

// --- ImageListModel ---

ImageListModel::ImageListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ImageListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(images_.size());
}

QVariant ImageListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= images_.size()) {
        return {};
    }
    const auto& img = images_[row];
    switch (role) {
    case FilenameRole:
        return QString::fromStdString(img.filename);
    case TitleRole:
        return QString::fromStdString(img.display_name.empty() ? img.filename : img.display_name);
    default:
        return {};
    }
}

QHash<int, QByteArray> ImageListModel::roleNames() const
{
    return {{FilenameRole, "filename"}, {TitleRole, "title"}};
}

void ImageListModel::set_images(std::vector<model::MediaFile> images)
{
    const auto kind = classify_list_replace(images_, images, [](const model::MediaFile& img) {
        return img.filename;
    });
    if (kind == ListReplace::Noop) {
        return;
    }
    if (kind == ListReplace::DataChanged) {
        images_ = std::move(images);
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    beginResetModel();
    images_ = std::move(images);
    endResetModel();
}

// --- MediaBrowserModel ---

MediaBrowserModel::MediaBrowserModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MediaBrowserModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant MediaBrowserModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= entries_.size()) {
        return {};
    }
    const auto& e = entries_[row];
    switch (role) {
    case KindRole:
        switch (e.kind) {
        case MediaBrowserEntry::Kind::Parent:
            return QStringLiteral("parent");
        case MediaBrowserEntry::Kind::Folder:
            return QStringLiteral("folder");
        case MediaBrowserEntry::Kind::File:
            return QStringLiteral("file");
        }
        return {};
    case IdRole:
        return static_cast<qint64>(e.id);
    case NameRole:
        return QString::fromStdString(e.name);
    case FilenameRole:
        return QString::fromStdString(e.filename);
    case CountRole:
        return e.count;
    case ThumbUrlRole:
        return QString::fromStdString(e.thumb_url);
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaBrowserModel::roleNames() const
{
    return {{KindRole, "kind"},
            {IdRole, "entryId"},
            {NameRole, "name"},
            {FilenameRole, "filename"},
            {CountRole, "count"},
            {ThumbUrlRole, "thumbUrl"}};
}

void MediaBrowserModel::set_entries(std::vector<MediaBrowserEntry> entries, const QString& view)
{
    if (view != view_) {
        view_ = view;
        beginResetModel();
        entries_ = std::move(entries);
        endResetModel();
        return;
    }
    RowSteps::replace(*this, entries_, std::move(entries), [](const MediaBrowserEntry& e) {
        return std::tuple<int, std::int64_t, std::string>{static_cast<int>(e.kind), e.id, e.filename};
    });
}

QVariantList MediaBrowserModel::entryIds(const QString& kind_filter) const
{
    QVariantList out;
    const auto want = kind_filter.trimmed().toLower();
    for (const auto& e : entries_) {
        if (want == QLatin1String("folder")) {
            if (e.kind != MediaBrowserEntry::Kind::Folder || e.id < 0) {
                continue;
            }
            out.push_back(static_cast<qint64>(e.id));
        } else if (want == QLatin1String("file")) {
            if (e.kind != MediaBrowserEntry::Kind::File || e.id < 0) {
                continue;
            }
            out.push_back(static_cast<qint64>(e.id));
        }
    }
    return out;
}

} // namespace seventhslide::app
