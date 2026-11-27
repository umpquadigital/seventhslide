#pragma once

// Song library summaries for the Songs tab.
// Filter is applied in the model so ListView spacing only exists between hits.
// The search finds a song by its title, authors, songbook or songbook number,
// one of its topics, its key, or a line of its lyrics; titles that match come
// first, then the other fields, then lyrics, each by title (a search that
// reads as a key, "G" or "key Bb", lists the songs in that key before all of
// them). A song found only by its lyrics names the line in ``matchedLine``.

#include <seventhslide/model/content.hpp>

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace seventhslide::app {

class SongListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns its list models; QML reads their roles by name")
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

public:
    enum Roles {
        SongIdRole = Qt::UserRole + 1,
        TitleRole,
        AuthorsRole,
        SongbookRole,
        /// The lyric line the search found, when nothing else matched; else "".
        MatchedLineRole,
    };
    Q_ENUM(Roles)

    explicit SongListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// The library's songs, with their lyrics (for the search) where loaded.
    void set_songs(std::vector<model::Song> songs);
    /// Some songs changed: each of ``changed`` takes the place of the one
    /// with its id, or is added; those with an id in ``gone`` go. Only these
    /// are read for the search again, and only their rows change when the
    /// list stays in the same order.
    void change_songs(const std::vector<model::Song>& changed, const std::vector<std::int64_t>& gone);
    /// The id of every song held, in no order.
    [[nodiscard]] std::vector<std::int64_t> song_ids() const;

    [[nodiscard]] QString filter() const { return filter_; }
    void setFilter(const QString& filter);

    /// A song a search found: what a list of them shows, and the lyric line
    /// that found it ("" when another field did).
    struct Found {
        std::int64_t id = -1;
        QString title;
        QString authors;
        QString matched_line;
    };
    /// Every song ``query`` finds, in the order the list shows them, by the
    /// list's own rules; the list's filter stays as it is (the web remote
    /// searches with this).
    [[nodiscard]] std::vector<Found> search(const QString& query) const;

signals:
    void filterChanged();

private:
    /// A song with the strings the list shows and searches, built once.
    struct Entry {
        std::optional<std::int64_t> id;
        QString title;
        QString authors;
        /// The songbook and its number: searched, not shown.
        QString songbook;
        /// The song's topics: searched, not shown.
        QStringList topics;
        /// The fields above folded for the search; a topic at a time, so a
        /// search cannot match across two of them.
        QString title_folded;
        QString authors_folded;
        QString songbook_folded;
        QStringList topics_folded;
        /// The key as the search compares it (``Bb``, ``f#m``), or "".
        QString key;
        /// The lyric lines as shown (no section headers, chords or markup).
        QStringList lyric_lines;
        /// ``lyric_lines`` folded and joined by spaces, and where each line
        /// starts in it.
        QString lyrics_folded;
        std::vector<int> line_starts;
    };

    friend struct RowSteps;

    /// A row the view shows: a position in songs_, and the lyric line that
    /// found it (or -1).
    struct Hit {
        std::size_t entry = 0;
        int line = -1;
    };

    [[nodiscard]] static Entry make_entry(const model::Song& song);
    /// Show ``songs`` from now on; ``touched`` the positions in it of those
    /// that changed, when only they did (null: any may have).
    void replace(std::vector<Entry> songs, const std::set<std::size_t>* touched);
    [[nodiscard]] const Entry* entry_at(int row) const;
    /// The positions in ``entries`` that search ``filter`` lets through, in
    /// the order the view shows them.
    [[nodiscard]] static std::vector<Hit> visible_rows(const std::vector<Entry>& entries,
                                                       const QString& filter);
    /// The song id at each of ``rows`` (-1 for a song without one): what a
    /// refresh compares to decide whether the view can keep its place.
    [[nodiscard]] static std::vector<qint64> row_ids(const std::vector<Entry>& entries,
                                                     const std::vector<Hit>& rows);

    std::vector<Entry> songs_;
    /// The rows the view shows. Rebuilt by visible_rows whenever songs_ or
    /// the search changes, so it never points past songs_.
    std::vector<Hit> visible_;
    QString filter_;
};

} // namespace seventhslide::app
