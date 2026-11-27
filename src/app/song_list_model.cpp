#include "song_list_model.hpp"
#include "list_model_replace.hpp"
#include "qml_ids.hpp"

#include <seventhslide/db/search_text.hpp>
#include <seventhslide/layout/chords.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/util/strings.hpp>

#include <QCollator>
#include <QRegularExpression>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace seventhslide::app {
namespace {

QString authors_joined(const model::Song& song)
{
    return QString::fromStdString(model::join_author_names(song.authors));
}

QString songbook_label(const model::Song& song)
{
    QString sb = QString::fromStdString(song.songbook_name).trimmed();
    const auto entry = QString::fromStdString(song.songbook_entry).trimmed();
    if (!entry.isEmpty()) {
        if (sb.isEmpty()) {
            sb = QStringLiteral("#%1").arg(entry);
        } else {
            sb += QStringLiteral(" #%1").arg(entry);
        }
    }
    return sb;
}

/// The lines of ``lyrics`` as the operator reads them: no section headers,
/// chords, markup or forced breaks, and no blank lines.
QStringList lyric_lines(const std::string& lyrics)
{
    QStringList lines;
    for (const auto& verse : parse::labelled_verses(lyrics)) {
        const auto content = QString::fromStdString(verse.content);
        for (const auto& line : content.split(QLatin1Char('\n'))) {
            const std::string raw = line.toStdString();
            if (model::is_forced_split_marker(util::trim_view(raw))) {
                continue;
            }
            const auto plain = QString::fromStdString(
                                   model::strip_markup_tags(layout::lyric_without_chords(raw)))
                                   .simplified();
            if (!plain.isEmpty()) {
                lines.push_back(plain);
            }
        }
    }
    return lines;
}

/// ``key`` as the search compares it: lower case, ``♭`` and ``♯`` as ``b``
/// and ``#``, no spaces. "B♭ m" and "bbm" compare alike.
QString key_form(QString key)
{
    key = key.toLower();
    key.replace(QChar(0x266D), QLatin1Char('b'));
    key.replace(QChar(0x266F), QLatin1Char('#'));
    key.remove(QLatin1Char(' '));
    return key;
}

/// Whether ``hay`` holds ``needle`` with a number in it whole: "45" is in
/// "hymnal 45" and "45a" but not in "145" or "450".
bool contains_whole_numbers(const QString& hay, const QString& needle)
{
    if (needle.isEmpty()) {
        return false;
    }
    const bool digit_first = needle.front().isDigit();
    const bool digit_last = needle.back().isDigit();
    for (qsizetype at = hay.indexOf(needle); at >= 0; at = hay.indexOf(needle, at + 1)) {
        const qsizetype end = at + needle.size();
        if (digit_first && at > 0 && hay.at(at - 1).isDigit()) {
            continue;
        }
        if (digit_last && end < hay.size() && hay.at(end).isDigit()) {
            continue;
        }
        return true;
    }
    return false;
}

} // namespace

SongListModel::SongListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int SongListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(visible_.size());
}

const SongListModel::Entry* SongListModel::entry_at(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= visible_.size()) {
        return nullptr;
    }
    return &songs_[visible_[static_cast<std::size_t>(row)].entry];
}

QVariant SongListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto* entry = entry_at(index.row());
    if (!entry) {
        return {};
    }
    switch (role) {
    case SongIdRole:
        return qml_id(entry->id);
    case TitleRole:
        return entry->title;
    case AuthorsRole:
        return entry->authors;
    case SongbookRole:
        return entry->songbook;
    case MatchedLineRole: {
        const int line = visible_[static_cast<std::size_t>(index.row())].line;
        return line >= 0 ? entry->lyric_lines.at(line) : QString();
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> SongListModel::roleNames() const
{
    return {
        {SongIdRole, "songId"},
        {TitleRole, "title"},
        {AuthorsRole, "authors"},
        {SongbookRole, "songbook"},
        {MatchedLineRole, "matchedLine"},
    };
}

std::vector<SongListModel::Hit> SongListModel::visible_rows(const std::vector<Entry>& entries,
                                                            const QString& filter)
{
    const db::SearchQuery search(filter);
    const QString& query = search.typed();
    const QString& folded_query = search.folded();
    // "G", "key G", "key: bb".
    QString key_query = key_form(query);
    if (key_query.startsWith(QLatin1String("key"))) {
        key_query = key_query.mid(3);
        if (key_query.startsWith(QLatin1Char(':'))) {
            key_query = key_query.mid(1);
        }
    }
    const auto has = [&](const QString& field, const QString& field_folded) {
        return search.matches(field, field_folded);
    };

    // A search that reads as a key ("G", "Bb", "F#m") lists the songs in it
    // first, before the titles that merely hold the letter.
    static const QRegularExpression key_shape(QStringLiteral("^[a-g][#b]?m?$"));
    const bool key_search = key_shape.match(key_query).hasMatch();

    // tier: 0 title, 1 authors / songbook / topics / key, 2 lyrics only;
    // -1 the key, when the search reads as one.
    struct Ranked {
        Hit hit;
        int tier = 0;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (key_search && e.key == key_query) {
            ranked.push_back({{i, -1}, -1});
            continue;
        }
        if (query.isEmpty() || has(e.title, e.title_folded)) {
            ranked.push_back({{i, -1}, 0});
            continue;
        }
        bool in_topics = false;
        for (qsizetype t = 0; t < e.topics.size() && !in_topics; ++t) {
            in_topics = has(e.topics.at(t), e.topics_folded.at(t));
        }
        // A songbook number whole: "45" is not hymn 145.
        if (in_topics || has(e.authors, e.authors_folded)
            || contains_whole_numbers(e.songbook_folded, folded_query)
            || (!e.key.isEmpty() && e.key == key_query)) {
            ranked.push_back({{i, -1}, 1});
            continue;
        }
        if (folded_query.isEmpty()) {
            continue;
        }
        const qsizetype at = e.lyrics_folded.indexOf(folded_query);
        if (at < 0) {
            continue;
        }
        const auto line = std::ranges::upper_bound(e.line_starts, static_cast<int>(at));
        ranked.push_back({{i, static_cast<int>(line - e.line_starts.begin()) - 1}, 2});
    }

    const QCollator& c = db::name_collator();
    std::ranges::stable_sort(ranked, [&](const Ranked& x, const Ranked& y) {
        if (x.tier != y.tier) {
            return x.tier < y.tier;
        }
        const Entry& a = entries[x.hit.entry];
        const Entry& b = entries[y.hit.entry];
        const int t = c.compare(a.title, b.title);
        if (t != 0) {
            return t < 0;
        }
        return a.id.value_or(-1) < b.id.value_or(-1);
    });

    std::vector<Hit> rows;
    rows.reserve(ranked.size());
    for (const auto& r : ranked) {
        rows.push_back(r.hit);
    }
    return rows;
}

std::vector<SongListModel::Found> SongListModel::search(const QString& query) const
{
    std::vector<Found> found;
    for (const Hit& hit : visible_rows(songs_, query)) {
        const Entry& e = songs_[hit.entry];
        if (!e.id) {
            continue;
        }
        found.push_back({*e.id, e.title, e.authors,
                         hit.line >= 0 ? e.lyric_lines.at(hit.line) : QString()});
    }
    return found;
}

std::vector<qint64> SongListModel::row_ids(const std::vector<Entry>& entries,
                                           const std::vector<Hit>& rows)
{
    std::vector<qint64> ids;
    ids.reserve(rows.size());
    for (const auto& row : rows) {
        ids.push_back(entries[row.entry].id.value_or(-1));
    }
    return ids;
}

SongListModel::Entry SongListModel::make_entry(const model::Song& song)
{
    Entry entry;
    entry.id = song.id;
    entry.title = QString::fromStdString(song.title);
    entry.authors = authors_joined(song);
    entry.songbook = songbook_label(song);
    for (const auto& topic : song.topics) {
        entry.topics.push_back(QString::fromStdString(topic));
        entry.topics_folded.push_back(db::search_fold(entry.topics.back()));
    }
    entry.title_folded = db::search_fold(entry.title);
    entry.authors_folded = db::search_fold(entry.authors);
    entry.songbook_folded = db::search_fold(entry.songbook);
    entry.key = key_form(QString::fromStdString(song.key).trimmed());
    entry.lyric_lines = lyric_lines(song.lyrics);
    for (const auto& line : entry.lyric_lines) {
        if (!entry.lyrics_folded.isEmpty()) {
            entry.lyrics_folded.append(QLatin1Char(' '));
        }
        entry.line_starts.push_back(static_cast<int>(entry.lyrics_folded.size()));
        entry.lyrics_folded.append(db::search_fold(line));
    }
    return entry;
}

void SongListModel::set_songs(std::vector<model::Song> song_list)
{
    std::vector<Entry> songs;
    songs.reserve(song_list.size());
    for (const auto& song : song_list) {
        songs.push_back(make_entry(song));
    }
    replace(std::move(songs), nullptr);
}

void SongListModel::change_songs(const std::vector<model::Song>& changed,
                                 const std::vector<std::int64_t>& gone)
{
    if (changed.empty() && gone.empty()) {
        return;
    }
    // A copy, as the view reads songs_ until replace swaps it: its strings
    // are shared, not copied.
    auto songs = songs_;
    if (!gone.empty()) {
        const std::unordered_set<std::int64_t> going(gone.begin(), gone.end());
        std::erase_if(songs, [&](const Entry& e) { return e.id && going.contains(*e.id); });
    }
    std::unordered_map<std::int64_t, std::size_t> at;
    for (std::size_t i = 0; i < songs.size(); ++i) {
        if (const auto& id = songs[i].id) {
            at.emplace(*id, i);
        }
    }
    std::set<std::size_t> touched;
    for (const auto& song : changed) {
        auto entry = make_entry(song);
        const auto it = entry.id ? at.find(*entry.id) : at.end();
        if (it != at.end()) {
            songs[it->second] = std::move(entry);
            touched.insert(it->second);
            continue;
        }
        if (entry.id) {
            at.emplace(*entry.id, songs.size());
        }
        touched.insert(songs.size());
        songs.push_back(std::move(entry));
    }
    replace(std::move(songs), &touched);
}

std::vector<std::int64_t> SongListModel::song_ids() const
{
    std::vector<std::int64_t> ids;
    ids.reserve(songs_.size());
    for (const auto& e : songs_) {
        if (e.id) {
            ids.push_back(*e.id);
        }
    }
    return ids;
}

void SongListModel::replace(std::vector<Entry> songs, const std::set<std::size_t>* touched)
{
    const auto before = row_ids(songs_, visible_);
    auto next_visible = visible_rows(songs, filter_);
    const auto after = row_ids(songs, next_visible);
    const auto kind = classify_identity_keys(before, after);
    // Some of the rows stay: a view of them stays where it is scrolled.
    const auto kept = kind == ListReplace::Reset ? kept_rows(before, after) : std::nullopt;
    if (kept) {
        RowSteps::apply(
            *this, *kept,
            [&](int first, int last) { visible_.erase(visible_.begin() + first, visible_.begin() + last + 1); },
            [&] {
                songs_ = std::move(songs);
                visible_.clear();
                for (std::size_t i = 0; i < next_visible.size(); ++i) {
                    if (kept->after[i]) {
                        visible_.push_back(next_visible[i]);
                    }
                }
            },
            [&](int first, int last) {
                visible_.insert(visible_.begin() + first, next_visible.begin() + first,
                                next_visible.begin() + last + 1);
            });
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    if (kind == ListReplace::Reset) {
        beginResetModel();
    }
    songs_ = std::move(songs);
    visible_ = std::move(next_visible);
    if (kind == ListReplace::Reset) {
        endResetModel();
        return;
    }
    if (kind != ListReplace::DataChanged) {
        return;
    }
    if (touched == nullptr || before != after) {
        emit dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    // The same rows in the same order: only those that changed, in runs.
    const int rows = rowCount();
    for (int row = 0; row < rows;) {
        if (!touched->contains(visible_[static_cast<std::size_t>(row)].entry)) {
            ++row;
            continue;
        }
        int last = row;
        while (last + 1 < rows && touched->contains(visible_[static_cast<std::size_t>(last + 1)].entry)) {
            ++last;
        }
        emit dataChanged(index(row), index(last));
        row = last + 1;
    }
}

void SongListModel::setFilter(const QString& filter)
{
    if (filter_ == filter) {
        return;
    }
    beginResetModel();
    filter_ = filter;
    visible_ = visible_rows(songs_, filter_);
    endResetModel();
    emit filterChanged();
}

} // namespace seventhslide::app
