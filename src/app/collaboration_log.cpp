#include "collaboration_log.hpp"

#include <seventhslide/link/off_thread.hpp>
#include <seventhslide/util/atomic_file.hpp>
#include <seventhslide/util/read_file.hpp>

#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QLocale>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace seventhslide::app {
namespace {

/// A message this soon after the last of the same computer's goes under it,
/// without its name again.
constexpr qint64 k_follow_on_s = 5 * 60;

constexpr int k_read_format = 1;
constexpr const char* k_read_filename = "collaboration_read.json";

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

/// ``text`` in the operator's words, ``n`` for its plurals.
/// ``text`` in the operator's words.
QString say(const char* text)
{
    return QCoreApplication::translate("CollaborationLog", text);
}

/// "3 songs", for records of ``kind``.
QString count_of(std::string_view kind, int n)
{
    struct Noun {
        std::string_view kind;
        const char* one;
        const char* many;
    };
    static constexpr std::array<Noun, 13> k_nouns{{
        {"song", QT_TRANSLATE_NOOP("CollaborationLog", "1 song"), QT_TRANSLATE_NOOP("CollaborationLog", "%1 songs")},
        {"service", QT_TRANSLATE_NOOP("CollaborationLog", "1 service"),
         QT_TRANSLATE_NOOP("CollaborationLog", "%1 services")},
        {"freeform", QT_TRANSLATE_NOOP("CollaborationLog", "1 Freeform item"),
         QT_TRANSLATE_NOOP("CollaborationLog", "%1 Freeform items")},
        {"web_page", QT_TRANSLATE_NOOP("CollaborationLog", "1 web page"),
         QT_TRANSLATE_NOOP("CollaborationLog", "%1 web pages")},
        {"image", QT_TRANSLATE_NOOP("CollaborationLog", "1 image"), QT_TRANSLATE_NOOP("CollaborationLog", "%1 images")},
        {"video", QT_TRANSLATE_NOOP("CollaborationLog", "1 video"), QT_TRANSLATE_NOOP("CollaborationLog", "%1 videos")},
        {"audio", QT_TRANSLATE_NOOP("CollaborationLog", "1 audio file"),
         QT_TRANSLATE_NOOP("CollaborationLog", "%1 audio files")},
        {"font", QT_TRANSLATE_NOOP("CollaborationLog", "1 font"), QT_TRANSLATE_NOOP("CollaborationLog", "%1 fonts")},
        {"bible", QT_TRANSLATE_NOOP("CollaborationLog", "1 Bible"), QT_TRANSLATE_NOOP("CollaborationLog", "%1 Bibles")},
        {"folder", QT_TRANSLATE_NOOP("CollaborationLog", "1 folder"), QT_TRANSLATE_NOOP("CollaborationLog", "%1 folders")},
        {"output", QT_TRANSLATE_NOOP("CollaborationLog", "1 output"), QT_TRANSLATE_NOOP("CollaborationLog", "%1 outputs")},
        {"style_profile", QT_TRANSLATE_NOOP("CollaborationLog", "1 style profile"),
         QT_TRANSLATE_NOOP("CollaborationLog", "%1 style profiles")},
        {"", QT_TRANSLATE_NOOP("CollaborationLog", "1 item"), QT_TRANSLATE_NOOP("CollaborationLog", "%1 items")},
    }};
    const auto* noun = &k_nouns.back();
    for (const auto& each : k_nouns) {
        if (each.kind == kind) {
            noun = &each;
            break;
        }
    }
    return n == 1 ? say(noun->one) : say(noun->many).arg(QLocale().toString(n));
}

/// The glyph a record of ``kind`` is drawn with (``glyphs.cpp``).
QString glyph_of(std::string_view kind)
{
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 12> k_glyphs{{
        {"song", "song"},
        {"service", "list"},
        {"freeform", "freeform"},
        {"web_page", "globe"},
        {"image", "image"},
        {"video", "video"},
        {"audio", "audio"},
        {"font", "textBox"},
        {"bible", "bible"},
        {"folder", "folder"},
        {"output", "display"},
        {"style_profile", "themes"},
    }};
    for (const auto& [k, glyph] : k_glyphs) {
        if (k == kind) {
            return QString::fromUtf8(glyph);
        }
    }
    return QStringLiteral("edit");
}

/// What a changed field is, in words; empty for one not worth saying.
QString field_words(std::string_view kind, std::string_view field)
{
    if (field == "sort_order" || field == "order_num" || field == "folder_id" || field == "parent_id") {
        return field == "folder_id" || field == "parent_id" ? say("folder") : QString();
    }
    if (kind == "song") {
        if (field == "lyrics") return say("lyrics");
        if (field == "verse_order") return say("verse order");
        if (field == "theme_map") return say("theme");
        if (field == "authors") return say("authors");
        if (field == "copyright" || field == "show_copyright") return say("copyright");
        if (field == "ccli_song_number") return say("CCLI number");
        if (field == "songbook_name" || field == "songbook_entry") return say("songbook");
        if (field == "key") return say("key");
        if (field == "topics") return say("topics");
        if (field == "comments") return say("comments");
    }
    if (kind == "service") {
        if (field == "items") return say("items");
        if (field == "service_at") return say("date");
        if (field == "theme_map") return say("theme");
    }
    if (field == "title" || field == "name" || field == "display_name") return say("title");
    if (field == "tags") return say("tags");
    if (field == "document") return say("slides");
    if (field == "url") return say("address");
    if (field == "data") return say("settings");
    if (field == "library_visible" || field == "enabled") return say("visibility");
    QString words = q(std::string(field));
    words.replace(QLatin1Char('_'), QLatin1Char(' '));
    return words;
}

QString day_of(const QDate& day)
{
    const auto today = QDate::currentDate();
    if (day == today) {
        return say("Today");
    }
    if (day == today.addDays(-1)) {
        return say("Yesterday");
    }
    const QLocale locale;
    return day.year() == today.year() ? locale.toString(day, QStringLiteral("dddd, MMMM d"))
                                      : locale.toString(day, QLocale::LongFormat);
}

QDateTime local_time(const sync::LogEntry& entry)
{
    return QDateTime::fromString(q(entry.time), Qt::ISODate).toLocalTime();
}

/// ``next`` goes in the row ``last`` ends: both notes of one computer's, the
/// same day.
bool joins(const sync::LogEntry& last, const sync::LogEntry& next)
{
    return last.kind == sync::k_log_changes && next.kind == sync::k_log_changes && last.machine == next.machine
           && local_time(last).date() == local_time(next).date();
}

}  // namespace

QVariantMap picture_map(const sync::Picture& picture)
{
    QVariantMap out;
    if (!picture.image.empty()) {
        out.insert(QStringLiteral("image"), q(picture.image));
    } else if (!picture.glyph.empty()) {
        out.insert(QStringLiteral("glyph"), q(picture.glyph));
        out.insert(QStringLiteral("colour"), q(picture.colour));
    }
    return out;
}

CollaborationLog::CollaborationLog(Post post, std::filesystem::path data, QObject* parent)
    : QAbstractListModel(parent), post_(std::move(post)), data_(std::move(data))
{
    pool_.setMaxThreadCount(1);
    const auto text = util::read_file(data_ / k_read_filename);
    if (!text) {
        return;
    }
    const auto j = nlohmann::json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("libraries") || !j["libraries"].is_object()) {
        return;
    }
    for (const auto& [uid, writers] : j["libraries"].items()) {
        if (!writers.is_object()) {
            continue;
        }
        for (const auto& [machine, sequences] : writers.items()) {
            if (!sequences.is_array()) {
                continue;
            }
            for (const auto& sequence : sequences) {
                if (sequence.is_number_integer()) {
                    read_[uid][machine].insert(sequence.get<std::int64_t>());
                }
            }
        }
    }
}

CollaborationLog::~CollaborationLog()
{
    pool_.waitForDone();
}

int CollaborationLog::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QHash<int, QByteArray> CollaborationLog::roleNames() const
{
    return {
        {EntryIdRole, "entryId"},     {KindRole, "kind"},         {MachineRole, "machine"},
        {NameRole, "name"},           {PictureRole, "picture"},   {MineRole, "mine"},
        {TimeTextRole, "timeText"},   {TextRole, "text"},         {SummaryRole, "summary"},
        {ItemsRole, "items"},         {ShowDayRole, "showDay"},   {DayRole, "day"},
        {ShowSenderRole, "showSender"}, {ExpandedRole, "expanded"},
    };
}

QVariant CollaborationLog::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto at = static_cast<std::size_t>(index.row());
    const auto& row = rows_[at];
    const auto& e = entries_[row.first];
    const auto* before = at > 0 ? &entries_[rows_[at - 1].last] : nullptr;
    switch (role) {
    case EntryIdRole:
        return q(entry_id(e));
    case KindRole:
        return q(e.kind);
    case MachineRole:
        return q(e.machine);
    case NameRole:
        return who(e.machine).name;
    case PictureRole:
        return picture_map(who(e.machine).picture);
    case MineRole:
        return e.machine == me_;
    case TimeTextRole: {
        const QLocale locale;
        const auto from = locale.toString(local_time(e).time(), QLocale::ShortFormat);
        const auto to = locale.toString(local_time(entries_[row.last]).time(), QLocale::ShortFormat);
        return from == to ? from : say("%1 – %2").arg(from, to);
    }
    case TextRole:
        return q(e.text);
    case SummaryRole:
        return e.kind == sync::k_log_changes ? summary(who(e.machine).name, row_entry(row).counts) : QString();
    case ItemsRole: {
        QVariantList items;
        for (const auto& item : row_entry(row).items) {
            items.push_back(item_map(item));
        }
        return items;
    }
    case ShowDayRole:
        return before == nullptr || local_time(*before).date() != local_time(e).date();
    case DayRole:
        return day_of(local_time(e).date());
    case ShowSenderRole: {
        if (e.kind != sync::k_log_message || before == nullptr || before->kind != sync::k_log_message
            || before->machine != e.machine) {
            return true;
        }
        const auto was = local_time(*before);
        const auto now = local_time(e);
        return was.date() != now.date() || was.secsTo(now) > k_follow_on_s;
    }
    case ExpandedRole:
        for (auto n = row.first; n <= row.last; ++n) {
            if (expanded_.contains(entry_id(entries_[n]))) {
                return true;
            }
        }
        return false;
    default:
        return {};
    }
}

std::string CollaborationLog::entry_id(const sync::LogEntry& entry) const
{
    return entry.machine + "/" + std::to_string(entry.sequence);
}

std::vector<CollaborationLog::Row> CollaborationLog::rows_of() const
{
    std::vector<Row> rows;
    for (std::size_t n = 0; n < entries_.size(); ++n) {
        if (!rows.empty() && joins(entries_[rows.back().last], entries_[n])) {
            rows.back().last = n;
        } else {
            rows.push_back({n, n});
        }
    }
    return rows;
}

sync::LogEntry CollaborationLog::row_entry(const Row& row) const
{
    if (row.first == row.last) {
        return entries_[row.first];
    }
    return merge_notes(std::span(entries_).subspan(row.first, row.last - row.first + 1));
}

CollaborationLog::Who CollaborationLog::who(const std::string& machine) const
{
    const auto it = members_.find(machine);
    if (it != members_.end()) {
        return it->second;
    }
    return {say("Another computer"), {}};
}

void CollaborationLog::follow(const std::string& uid, std::shared_ptr<sync::SyncStore> store, std::string me)
{
    if (uid == uid_ && store == store_) {
        return;
    }
    ++generation_;
    reading_ = false;
    read_again_ = false;
    beginResetModel();
    uid_ = uid;
    store_ = uid.empty() ? nullptr : std::move(store);
    me_ = std::move(me);
    entries_.clear();
    rows_.clear();
    expanded_.clear();
    members_.clear();
    can_post_ = false;
    reader_ = uid_.empty() ? nullptr : std::make_shared<sync::LogReader>(uid_);
    endResetModel();
    emit availableChanged();
    update_unread();
    refresh();
}

void CollaborationLog::set_members(const sync::Members& members)
{
    std::map<std::string, Who> named;
    for (const auto& [machine, m] : members.members) {
        named.emplace(machine, Who{m.name.empty() ? say("Another computer") : q(m.name), m.picture});
    }
    const auto* self = members.find(me_);
    const bool can_post = self != nullptr && !self->removed && self->role == sync::Role::Editor;
    if (can_post != can_post_) {
        can_post_ = can_post;
        emit availableChanged();
    }
    if (named != members_) {
        members_ = std::move(named);
        if (!rows_.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1), {NameRole, PictureRole, SummaryRole});
        }
    }
}

void CollaborationLog::refresh()
{
    if (!reader_ || !store_) {
        return;
    }
    if (reading_) {
        read_again_ = true;
        return;
    }
    reading_ = true;
    link::run_off_thread(
        this, pool_, [reader = reader_, store = store_] { return reader->read_new(*store); },
        [this, generation = generation_](std::vector<sync::LogEntry> entries) {
            take(generation, std::move(entries));
        });
}

void CollaborationLog::take(std::uint64_t generation, std::vector<sync::LogEntry> entries)
{
    if (generation != generation_) {
        return;  // read for the library shown before
    }
    reading_ = false;
    if (!entries.empty()) {
        // Most often each comes after all shown: added at the end, so the view
        // keeps its place, the last row going on when the first is more of
        // its notes. One that sorts earlier (it waited somewhere) puts the
        // whole log in order again.
        const bool at_end = entries_.empty() || !sync::log_before(entries.front(), entries_.back());
        if (at_end) {
            const auto had = rows_.size();
            std::ranges::move(entries, std::back_inserter(entries_));
            auto rows = rows_of();
            const bool grew = had > 0 && rows[had - 1].last != rows_[had - 1].last;
            if (rows.size() > had) {
                beginInsertRows({}, static_cast<int>(had), static_cast<int>(rows.size()) - 1);
                rows_ = std::move(rows);
                endInsertRows();
            } else {
                rows_ = std::move(rows);
            }
            if (grew) {
                const auto last = index(static_cast<int>(had) - 1);
                emit dataChanged(last, last, {TimeTextRole, SummaryRole, ItemsRole});
            }
        } else {
            beginResetModel();
            std::ranges::move(entries, std::back_inserter(entries_));
            std::ranges::sort(entries_, sync::log_before);
            rows_ = rows_of();
            endResetModel();
        }
        update_unread();
    }
    if (std::exchange(read_again_, false)) {
        refresh();
    }
}

QString CollaborationLog::send(const QString& text)
{
    if (uid_.empty()) {
        return say("This library does not sync, so there is no one to send to.");
    }
    if (!can_post_) {
        return say("This computer only views this library, so it cannot send messages.");
    }
    const auto trimmed = text.trimmed().toStdString();
    if (trimmed.empty()) {
        return say("Type a message to send.");
    }
    if (trimmed.size() > sync::k_longest_message) {
        return say("That message is too long to send. Try splitting it in two.");
    }
    QPointer<CollaborationLog> self(this);
    post_(uid_, trimmed, [self](const std::string& error) {
        if (self && !error.empty()) {
            emit self->failed(q(error));
        }
    });
    return {};
}

void CollaborationLog::setExpanded(const QString& entryId, bool expanded)
{
    const auto id = entryId.toStdString();
    for (std::size_t at = 0; at < rows_.size(); ++at) {
        const auto& row = rows_[at];
        bool here = false;
        bool open = false;
        for (auto n = row.first; n <= row.last; ++n) {
            const auto each = entry_id(entries_[n]);
            here = here || each == id;
            open = open || expanded_.contains(each);
        }
        if (!here) {
            continue;
        }
        if (open == expanded) {
            return;
        }
        if (expanded) {
            expanded_.insert(id);
        } else {
            for (auto n = row.first; n <= row.last; ++n) {
                expanded_.erase(entry_id(entries_[n]));
            }
        }
        emit dataChanged(index(static_cast<int>(at)), index(static_cast<int>(at)), {ExpandedRole});
        return;
    }
    if (expanded) {
        expanded_.insert(id);
    } else {
        expanded_.erase(id);
    }
}

void CollaborationLog::setShowing(bool showing)
{
    showing_ = showing;
    update_unread();
}

bool CollaborationLog::is_unread(const sync::LogEntry& entry) const
{
    if (entry.kind != sync::k_log_message || entry.machine == me_) {
        return false;
    }
    const auto library = read_.find(uid_);
    if (library == read_.end()) {
        return true;
    }
    const auto writer = library->second.find(entry.machine);
    return writer == library->second.end() || !writer->second.contains(entry.sequence);
}

void CollaborationLog::update_unread()
{
    if (showing_) {
        bool moved = false;
        for (const auto& entry : entries_) {
            if (is_unread(entry)) {
                read_[uid_][entry.machine].insert(entry.sequence);
                moved = true;
            }
        }
        if (moved) {
            // Off the window's thread, in turn with the reads. Not saved, the
            // worst of it is the dot back at the next start.
            const nlohmann::json j{{"format", k_read_format}, {"libraries", read_}};
            pool_.start([path = data_ / k_read_filename, text = j.dump()] {
                (void)util::write_file_atomically(path, text);
            });
        }
    }
    const bool unread = std::ranges::any_of(entries_, [this](const auto& e) { return is_unread(e); });
    if (unread != unread_) {
        unread_ = unread;
        emit unreadChanged();
    }
}

QString CollaborationLog::summary(const QString& who, const sync::LogCounts& counts)
{
    // How many of each kind, and what was done to them.
    std::map<std::string, int> by_kind;
    std::set<std::string> actions;
    int total = 0;
    for (const auto& [kind, by_action] : counts) {
        for (const auto& [action, n] : by_action) {
            by_kind[kind] += n;
            total += n;
            if (n > 0) {
                actions.insert(action);
            }
        }
    }
    std::vector<std::pair<std::string, int>> kinds(by_kind.begin(), by_kind.end());
    std::ranges::stable_sort(kinds, [](const auto& a, const auto& b) { return a.second > b.second; });
    QString what;
    if (kinds.size() == 1) {
        what = count_of(kinds[0].first, kinds[0].second);
    } else if (kinds.size() == 2) {
        what = say("%1 and %2").arg(count_of(kinds[0].first, kinds[0].second),
                                   count_of(kinds[1].first, kinds[1].second));
    } else {
        what = count_of("", total);
    }
    if (actions.size() == 1 && *actions.begin() == "added") {
        return say("%1 added %2").arg(who, what);
    }
    if (actions.size() == 1 && *actions.begin() == "removed") {
        return say("%1 removed %2").arg(who, what);
    }
    if (actions.size() == 1) {
        return say("%1 updated %2").arg(who, what);
    }
    return say("%1 changed %2").arg(who, what);
}

sync::LogEntry CollaborationLog::merge_notes(std::span<const sync::LogEntry> notes)
{
    if (notes.empty()) {
        return {};
    }
    // The first's writer, number and time; the records of all of them.
    auto out = notes.front();
    out.items.clear();
    out.counts.clear();
    std::map<std::pair<std::string, std::string>, std::size_t> named_at;
    sync::LogCounts unnamed;
    for (const auto& note : notes) {
        sync::LogCounts named;
        for (const auto& item : note.items) {
            ++named[item.kind][item.action];
            const auto found = item.key.empty() ? named_at.end() : named_at.find({item.kind, item.key});
            if (found == named_at.end()) {
                if (!item.key.empty()) {
                    named_at.emplace(std::pair(item.kind, item.key), out.items.size());
                }
                out.items.push_back(item);
                continue;
            }
            auto& was = out.items[found->second];
            if (!item.title.empty()) {
                was.title = item.title;
            }
            if (item.action == "changed" && was.action == "changed") {
                for (const auto& field : item.fields) {
                    if (std::ranges::find(was.fields, field) == was.fields.end()) {
                        was.fields.push_back(field);
                    }
                }
            } else if (item.action != "changed" || was.action != "added") {
                was.action = item.action;
                was.fields = item.fields;
            }
        }
        // Past ``k_most_log_items`` a note counts what it does not name.
        for (const auto& [kind, by_action] : note.counts) {
            for (const auto& [action, n] : by_action) {
                const int rest = n - named[kind][action];
                if (rest > 0) {
                    unnamed[kind][action] += rest;
                }
            }
        }
    }
    out.counts = std::move(unnamed);
    for (const auto& item : out.items) {
        ++out.counts[item.kind][item.action];
    }
    return out;
}

QVariantMap CollaborationLog::item_map(const sync::LogItem& item)
{
    QString detail;
    if (item.action == "added") {
        detail = say("Added");
    } else if (item.action == "removed") {
        detail = say("Removed");
    } else {
        QStringList words;
        for (const auto& field : item.fields) {
            const auto w = field_words(item.kind, field);
            if (!w.isEmpty() && !words.contains(w)) {
                words.push_back(w);
            }
        }
        detail = words.isEmpty() ? say("Changed") : words.join(QStringLiteral(", "));
        detail[0] = detail[0].toUpper();
    }
    return {{QStringLiteral("glyph"), glyph_of(item.kind)},
            {QStringLiteral("title"), item.title.empty() ? count_of(item.kind, 1) : q(item.title)},
            {QStringLiteral("detail"), detail}};
}

}  // namespace seventhslide::app
