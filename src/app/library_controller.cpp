// The song, Bible and service library.
//
// Owns the repositories and the list models over them; the service that is
// currently OPEN is the session's (see library_controller.hpp).

#include "library_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"

#include "qvariant_json.hpp"
#include "local_file_path.hpp"
#include "qml_ids.hpp"

#include <seventhslide/db/bible_lookup.hpp>
#include <seventhslide/db/search_text.hpp>
#include <seventhslide/model/audio_fades.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/parse/bible.hpp>
#include <seventhslide/parse/lyrics_blocks.hpp>
#include <seventhslide/parse/song_import.hpp>
#include <seventhslide/theme/song_theme_pack.hpp>
#include <seventhslide/theme/theme_picks.hpp>
#include <seventhslide/util/strings.hpp>

#include <QCollator>
#include <QDateTime>
#include <QLocale>
#include <QMetaObject>
#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <exception>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::app {

namespace {

const QString k_tab_setting = QStringLiteral("library/tab");
const QString k_tab_order_setting = QStringLiteral("library/tabOrder");
const QString k_hidden_tabs_setting = QStringLiteral("library/hiddenTabs");
const QString k_tabs_shown_setting = QStringLiteral("library/tabsShown");

/// How many tabs the strip shows at once, as stored; unset or nonsense is six.
int normalized_tabs_shown(int count)
{
    return count > 0 ? count : 6;
}

/// A tab's id as stored: trimmed and lower case; unset is ``songs``. Which
/// ids exist is the panel's to know, and it shows Songs for one it has not.
QString normalized_tab(const QString& tab)
{
    const auto id = tab.trimmed().toLower();
    return id.isEmpty() ? QStringLiteral("songs") : id;
}

/// A song's (or a song rundown item's) theme arrangements from QML, as stored.
nlohmann::json song_theme_map_from_qvariant(const QVariant& v)
{
    if (!v.isValid() || v.isNull()) {
        return nlohmann::json::object();
    }
    return theme::SongThemeMap::from_json(to_json(v)).to_json();
}

QVariantMap json_object_to_qvariant(const nlohmann::json& object)
{
    const auto v = to_qvariant(object);
    return v.isValid() ? v.toMap() : QVariantMap{};
}

QVariantMap song_theme_map_to_qvariant(const nlohmann::json& raw)
{
    return json_object_to_qvariant(theme::SongThemeMap::from_json(raw).to_json());
}

/// A song as the song editor reads and writes it, by QML name.
constexpr QmlField k_song_fields[] = {
    {"title", "title"},
    {"lyrics", "lyrics"},
    {"verseOrder", "verse_order"},
    {"authors", "authors"},
    {"songbookName", "songbook_name"},
    {"songbookEntry", "songbook_entry"},
    {"copyright", "copyright"},
    {"ccliSongNumber", "ccli_song_number"},
    {"key", "key"},
    {"showCopyright", "show_copyright"},
    {"themeMap", "theme_map"},
    {"topics", "topics"},
    {"comments", "comments"},
};

/// ``song`` for the song editor. The theme map is shown compacted.
QVariantMap song_to_qml(const model::Song& song)
{
    auto out = to_qml_fields(song.to_json(), k_song_fields);
    out.insert(QStringLiteral("themeMap"), song_theme_map_to_qvariant(song.theme_map));
    return out;
}

/// ``song`` with the fields ``fields`` names replaced. Core reads them, so it
/// normalises the credits and the author roles, not QML. Callers then set the
/// words from ``read_song_text``, so ``fields`` cannot skip its checks.
model::Song song_with_qml_fields(const model::Song& song, const QVariantMap& fields)
{
    auto merged = song.to_json();
    apply_qml_fields(merged, fields, k_song_fields);
    auto out = model::Song::from_json(merged);
    out.id = song.id;
    if (fields.contains(QStringLiteral("themeMap"))) {
        out.theme_map = theme::SongThemeMap::from_json(out.theme_map).to_json();
    }
    return out;
}

/// The words of a song as the editor sends them: the title and verse order
/// trimmed, the lyrics as typed.
struct SongText {
    std::string title;
    std::string lyrics;
    std::string verse_order;
};

/// ``SongText`` from the editor, or why it cannot be saved.
[[nodiscard]] std::optional<QString> read_song_text(const QString& title,
                                                    const QString& lyrics,
                                                    const QString& verse_order,
                                                    SongText& out)
{
    out.title = title.trimmed().toStdString();
    out.lyrics = lyrics.toStdString();
    out.verse_order = verse_order.trimmed().toStdString();
    if (out.title.empty()) {
        return LibraryController::tr("Type a title.");
    }
    if (const auto err = parse::validate_no_blank_lines_in_verse(out.lyrics)) {
        return QString::fromStdString(*err);
    }
    if (const auto err = parse::validate_verse_order(out.verse_order, out.lyrics)) {
        return QString::fromStdString(*err);
    }
    return std::nullopt;
}

}  // namespace

void LibraryController::attach(db::Database& db)
{
    db_ = &db;
    song_repo_ = std::make_unique<db::SongRepository>(db);
    service_repo_ = std::make_unique<db::ServiceRepository>(db);
    bible_repo_ = std::make_unique<db::BibleRepository>(db);
    ++generation_;
    // Once the open has settled, so whether this computer only views the
    // library, or joined it, is known.
    QMetaObject::invokeMethod(
        this,
        [this, generation = generation_] {
            if (generation == generation_) {
                take_shipped_bibles();
            }
        },
        Qt::QueuedConnection);
}

void LibraryController::detach()
{
    ++generation_;
    bible_repo_.reset();
    service_repo_.reset();
    song_repo_.reset();
    db_ = nullptr;
    selected_bible_id_.reset();
    selected_bible_name_.clear();
    selected_bible_abbreviation_.clear();
    selected_bible_book_.clear();
    selected_bible_chapter_ = 0;
    open_service_name_.clear();
    emit changed();
}

LibraryController::LibraryController(session::Session& session, QObject* parent)
    : QObject(parent)
    , session_(session)
    , songs_model_(this)
    , bible_model_(this)
    , enabled_bible_model_(this)
    , bible_verses_model_(this)
    , services_model_(this)
    , shown_tab_(normalized_tab(QSettings().value(k_tab_setting).toString()))
    , tab_order_(QSettings().value(k_tab_order_setting).toStringList())
    , hidden_tabs_(QSettings().value(k_hidden_tabs_setting).toStringList())
    , tabs_shown_(normalized_tabs_shown(QSettings().value(k_tabs_shown_setting).toInt()))
{
    workers_.setMaxThreadCount(1);
}

LibraryController::~LibraryController()
{
    // A read still running posts to this object: it is done before it goes.
    workers_.waitForDone();
}

void LibraryController::setShownTab(const QString& tab)
{
    const auto next = normalized_tab(tab);
    if (shown_tab_ == next) {
        return;
    }
    shown_tab_ = next;
    QSettings().setValue(k_tab_setting, shown_tab_);
    emit shownTabChanged();
}

void LibraryController::setTabOrder(const QStringList& order)
{
    if (tab_order_ == order) {
        return;
    }
    tab_order_ = order;
    QSettings().setValue(k_tab_order_setting, tab_order_);
    emit tabLayoutChanged();
}

void LibraryController::setHiddenTabs(const QStringList& hidden)
{
    if (hidden_tabs_ == hidden) {
        return;
    }
    hidden_tabs_ = hidden;
    QSettings().setValue(k_hidden_tabs_setting, hidden_tabs_);
    emit tabLayoutChanged();
}

void LibraryController::setTabsShown(int count)
{
    const int next = normalized_tabs_shown(count);
    if (tabs_shown_ == next) {
        return;
    }
    tabs_shown_ = next;
    QSettings().setValue(k_tabs_shown_setting, tabs_shown_);
    emit tabLayoutChanged();
}

void LibraryController::refresh_services()
{
    if (!service_repo_) {
        services_model_.set_services({});
        return;
    }
    services_model_.set_services(service_repo_->list_services());
    sync_service_list_selection();
}

void LibraryController::set_open_service_name(QString name)
{
    if (open_service_name_ == name) {
        return;
    }
    open_service_name_ = std::move(name);
    emit changed();
}

void LibraryController::refresh_bibles()
{
    if (!bible_repo_) {
        bible_model_.set_bibles({});
        enabled_bible_model_.set_bibles({});
        return;
    }
    auto all = bible_repo_->list_all();
    std::vector<model::Bible> enabled;
    std::ranges::copy_if(all, std::back_inserter(enabled),
                         [](const model::Bible& b) { return b.enabled && b.id; });
    const auto selected = std::ranges::find_if(enabled, [this](const model::Bible& b) {
        return selected_bible_id_ && b.id == selected_bible_id_;
    });
    if (selected != enabled.end()) {
        // Still offered: its name or abbreviation may have changed.
        selected_bible_name_ = QString::fromStdString(selected->name);
        selected_bible_abbreviation_ = QString::fromStdString(selected->abbreviation);
        emit bibleBrowseChanged();
    } else if (!enabled.empty()) {
        // None yet, or the selected one was deleted or switched off.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): enabled holds only Bibles with an id.
        selectBible(*enabled.front().id);
    } else if (selected_bible_id_) {
        selectBible(-1);
    }
    bible_model_.set_bibles(std::move(all));
    enabled_bible_model_.set_bibles(std::move(enabled));
}

void LibraryController::take_shipped_bibles()
{
    if (!bible_repo_ || !may_write() || !may_take_shipped()
        || shipped_bible_abbreviations(shipped_bibles_dir_).empty()) {
        return;
    }
    try {
        if (bible_repo_->bundled_taken()) {
            return;
        }
    } catch (const std::exception& ex) {
        qWarning("SeventhSlide: could not read whether the shipped Bibles were copied in: %s",
                 ex.what());
        return;
    }
    workers_.start([this, dir = shipped_bibles_dir_, generation = generation_] {
        auto bibles = read_shipped_bibles(dir);
        QMetaObject::invokeMethod(
            this,
            [this, generation, bibles = std::move(bibles)] {
                finish_taking_bibles(generation, bibles);
            },
            Qt::QueuedConnection);
    });
}

void LibraryController::finish_taking_bibles(std::uint64_t generation,
                                             const std::vector<ShippedBible>& bibles)
{
    // For a library that has gone, or turned view only here: it is copied in
    // the next time it opens where it may be.
    if (generation != generation_ || !bible_repo_ || !may_write() || !may_take_shipped()) {
        return;
    }
    try {
        // A sync may have brought the note since the read began.
        if (bible_repo_->bundled_taken()) {
            return;
        }
        std::set<std::string> held;
        for (const auto& bible : bible_repo_->list_all()) {
            held.insert(util::to_lower(bible.name));
        }
        for (const auto& shipped : bibles) {
            if (held.contains(util::to_lower(shipped.bible.name))) {
                continue;
            }
            const auto id = bible_repo_->import_bible(shipped.bible.name, shipped.bible.copyright,
                                                      shipped.bible.verses);
            (void)bible_repo_->set_abbreviation(id, shipped.abbreviation);
        }
        bible_repo_->note_bundled_taken();
    } catch (const std::exception& ex) {
        // Those taken stay, and are not taken again; the rest are tried the
        // next time the library opens.
        qWarning("SeventhSlide: could not copy the shipped Bibles in: %s", ex.what());
    }
    refresh_bibles();
}

void LibraryController::refresh_songs()
{
    if (!song_repo_) {
        songs_model_.set_songs({});
        return;
    }
    songs_model_.set_songs(song_repo_->list_searchable());
}

void LibraryController::refresh_songs(const std::set<std::string>& uids)
{
    if (!song_repo_) {
        songs_model_.set_songs({});
        return;
    }
    // A song deleted has no uid left to find it by: gone is what the list
    // holds and the library does not.
    const auto here = song_repo_->list_ids();
    std::vector<std::int64_t> gone;
    for (const auto id : songs_model_.song_ids()) {
        if (!std::ranges::binary_search(here, id)) {
            gone.push_back(id);
        }
    }
    songs_model_.change_songs(song_repo_->list_searchable(uids), gone);
}

void LibraryController::song_changed(std::int64_t song_id)
{
    if (auto song = song_repo_->get(song_id)) {
        songs_model_.change_songs({std::move(*song)}, {});
    } else {
        songs_model_.change_songs({}, {song_id});
    }
}

bool LibraryController::append_service_item(std::string_view item_type,
                                        const nlohmann::json& data,
                                        std::optional<std::int64_t> song_id,
                                        std::optional<int> at_index)
{
    const auto service_id = session_.service_id();
    if (!service_repo_ || !service_id) {
        return false;
    }
    (void)service_repo_->add_item(*service_id, item_type, data, song_id, at_index);
    return sync_open_service_items(std::nullopt);
}

bool LibraryController::append_service_items(std::string_view item_type,
                                         const std::vector<nlohmann::json>& items,
                                         std::optional<int> at_index)
{
    const auto service_id = session_.service_id();
    if (!service_repo_ || !db_ || !service_id || items.empty()) {
        return false;
    }
    {
        db::Transaction tx(*db_);
        auto insert_at = at_index;
        for (const auto& data : items) {
            (void)service_repo_->add_item(*service_id, item_type, data, std::nullopt, insert_at);
            if (insert_at) {
                ++*insert_at;
            }
        }
        tx.commit();
    }
    return sync_open_service_items(std::nullopt);
}

QVariantMap LibraryController::getSong(qint64 song_id)
try {
    if (!song_repo_ || song_id < 0) {
        return {};
    }
    const auto song = song_repo_->get(song_id);
    if (!song) {
        return {};
    }
    auto out = song_to_qml(*song);
    out.insert(QStringLiteral("songId"), song_id);
    return out;
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {};
}

QVariantMap LibraryController::createSong(const QString& title,
                                             const QString& lyrics,
                                             const QString& verse_order,
                                             const QVariantMap& meta)
try {
    QVariantMap out;
    if (!song_repo_) {
        out.insert(QStringLiteral("error"), refusal::no_library());
        return out;
    }

    SongText text;
    if (const auto err = read_song_text(title, lyrics, verse_order, text)) {
        out.insert(QStringLiteral("error"), *err);
        return out;
    }
    auto song = song_with_qml_fields({}, meta);
    song.title = std::move(text.title);
    song.lyrics = std::move(text.lyrics);
    song.verse_order = std::move(text.verse_order);
    const auto id = song_repo_->insert(song);
    song_changed(id);
    out.insert(QStringLiteral("songId"), static_cast<qint64>(id));
    return out;
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), app::report_entry_error(__func__, ex)}};
}

QString LibraryController::updateSong(qint64 song_id,
                                         const QString& title,
                                         const QString& lyrics,
                                         const QString& verse_order,
                                         const QVariantMap& meta)
try {
    if (!song_repo_) {
        return refusal::no_library();
    }
    if (song_id < 0) {
        return refusal::song_gone();
    }
    auto song = song_repo_->get(song_id);
    if (!song) {
        return refusal::song_gone();
    }

    SongText text;
    if (const auto err = read_song_text(title, lyrics, verse_order, text)) {
        return *err;
    }
    auto edited = song_with_qml_fields(*song, meta);
    edited.title = std::move(text.title);
    edited.lyrics = std::move(text.lyrics);
    edited.verse_order = std::move(text.verse_order);
    song_repo_->update(edited);
    song_changed(song_id);
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::deleteSong(qint64 song_id)
try {
    if (!song_repo_) {
        return refusal::no_library();
    }
    if (song_id < 0) {
        return refusal::song_gone();
    }
    if (!song_repo_->remove(song_id)) {
        return refusal::song_gone();
    }
    song_changed(song_id);
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

bool LibraryController::selectBible(qint64 bible_id)
try {
    selected_bible_id_.reset();
    selected_bible_name_.clear();
    selected_bible_abbreviation_.clear();
    selected_bible_book_.clear();
    selected_bible_chapter_ = 0;
    bible_verses_model_.clear();
    if (!bible_repo_ || bible_id < 0) {
        emit bibleBrowseChanged();
        return false;
    }
    selected_bible_id_ = bible_id;
    if (const auto bible = bible_repo_->get(*selected_bible_id_)) {
        selected_bible_name_ = QString::fromStdString(bible->name);
        selected_bible_abbreviation_ = QString::fromStdString(bible->abbreviation);
    }
    emit bibleBrowseChanged();
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool LibraryController::addBibleToService(int verse_start, int verse_end, int at_index)
{
    // Forwards the operator UI's browse selection into the explicit-coordinate
    // implementation the remote also uses, so there is one copy of the logic
    // (and of its error handling).
    if (!selected_bible_id_) {
        return false;
    }
    return add_bible_ref_to_service(*selected_bible_id_, selected_bible_book_,
                                    selected_bible_chapter_, verse_start, verse_end, at_index);
}

bool LibraryController::add_bible_ref_to_service(qint64 bible_id,
                                         const QString& book_name,
                                         int chapter,
                                         int verse_start,
                                         int verse_end,
                                         int at_index)
try {
    if (!bible_repo_ || bible_id < 0) {
        return false;
    }
    const auto passage = bible_repo_->passage(bible_id, book_name.toStdString(), chapter,
                                              verse_start, verse_end);
    if (!passage) {
        return false;
    }
    return append_service_item(model::k_item_type_bible, passage->to_json(), std::nullopt,
                               optional_index(at_index));
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

QVariantMap LibraryController::resolveBibleRef(const QString& reference)
try {
    QVariantMap out;
    out.insert(QStringLiteral("success"), false);

    auto fail = [&](const QString& message) -> QVariantMap {
        bible_verses_model_.clear();
        out.insert(QStringLiteral("message"), message);
        return out;
    };

    if (!bible_repo_ || !selected_bible_id_) {
        return fail(refusal::no_bible_chosen());
    }

    const auto found = db::resolve_reference(*bible_repo_, *selected_bible_id_,
                                             reference.toStdString());
    if (!found.ok) {
        return fail(QString::fromStdString(found.message));
    }

    // Align browse selection so playBible / addBibleToService work afterward.
    selected_bible_book_ = QString::fromStdString(found.book);
    selected_bible_chapter_ = found.chapter;
    bible_verses_model_.set_verses(found.verses);

    QVariantList verse_rows;
    verse_rows.reserve(static_cast<int>(found.verses.size()));
    for (const auto& v : found.verses) {
        QVariantMap row;
        row.insert(QStringLiteral("verse_num"), v.verse_num);
        row.insert(QStringLiteral("text"), QString::fromStdString(v.text));
        verse_rows.push_back(row);
    }

    out.insert(QStringLiteral("success"), true);
    out.insert(QStringLiteral("bible_id"), static_cast<qint64>(*selected_bible_id_));
    out.insert(QStringLiteral("book"), selected_bible_book_);
    out.insert(QStringLiteral("chapter"), found.chapter);
    out.insert(QStringLiteral("verse_start"), found.verse_start);
    out.insert(QStringLiteral("verse_end"), found.verse_end);
    out.insert(QStringLiteral("ref"), QString::fromStdString(found.ref));
    out.insert(QStringLiteral("verses"), verse_rows);
    emit bibleBrowseChanged();
    return out;
} catch (const std::exception& ex) {
    // Runs on every keystroke of the search field, which answers a reference
    // that does not resolve with books to choose from, not with its
    // "message". A library that cannot be read is not such a reference, so
    // the shell says so, once; the field carries on as for one that did not
    // resolve.
    bible_verses_model_.clear();
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {{QStringLiteral("success"), false}};
}

QStringList LibraryController::suggestBibleBooks(const QString& prefix)
try {
    QStringList out;
    if (!bible_repo_ || !selected_bible_id_) {
        return out;
    }
    const QString trimmed = prefix.trimmed();
    if (trimmed.isEmpty()) {
        return out;
    }
    const auto books = bible_repo_->list_books(*selected_bible_id_);
    const auto hits = parse::suggest_bible_books(books, trimmed.toStdString());
    out.reserve(static_cast<qsizetype>(std::min<std::size_t>(hits.size(), 12)));
    for (const auto& book : hits) {
        out.push_back(QString::fromStdString(book));
        if (out.size() >= 12) {
            break;
        }
    }
    return out;
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {};
}

QString LibraryController::renameBible(qint64 bible_id, const QString& name)
try {
    if (!bible_repo_) {
        return refusal::no_library();
    }
    if (bible_id < 0) {
        return refusal::bible_gone();
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return refusal::name_required();
    }
    if (!bible_repo_->rename(bible_id, trimmed.toStdString())) {
        return refusal::bible_gone();
    }
    refresh_bibles();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::setBibleAbbreviation(qint64 bible_id, const QString& abbreviation)
try {
    if (!bible_repo_) {
        return refusal::no_library();
    }
    if (bible_id < 0 || !bible_repo_->set_abbreviation(bible_id, abbreviation.toStdString())) {
        return refusal::bible_gone();
    }
    refresh_bibles();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::setBibleEnabled(qint64 bible_id, bool enabled)
try {
    if (!bible_repo_) {
        return refusal::no_library();
    }
    if (bible_id < 0 || !bible_repo_->set_enabled(bible_id, enabled)) {
        return refusal::bible_gone();
    }
    refresh_bibles();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::deleteBible(qint64 bible_id)
try {
    if (!bible_repo_) {
        return refusal::no_library();
    }
    if (bible_id < 0) {
        return refusal::bible_gone();
    }
    if (!bible_repo_->remove(bible_id)) {
        return refusal::bible_gone();
    }
    if (selected_bible_id_ && *selected_bible_id_ == bible_id) {
        selectBible(-1);
    }
    refresh_bibles();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QVariantMap LibraryController::getServiceSongItem(qint64 item_id)
try {
    if (!service_repo_ || item_id < 0) {
        return {};
    }
    auto item = service_repo_->get_item(item_id);
    if (!item || item->item_type != model::k_item_type_song) {
        return {};
    }
    auto out = song_to_qml(item->song());
    out.insert(QStringLiteral("itemId"), item_id);
    out.insert(QStringLiteral("songId"), qml_id(item->song_id));
    out.insert(QStringLiteral("hasOverrides"), item->has_overrides());
    out.insert(QStringLiteral("canReset"), item->song_id.has_value());
    return out;
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {};
}

QString LibraryController::updateServiceSongItem(qint64 item_id,
                                             const QString& title,
                                             const QString& lyrics,
                                             const QString& verse_order,
                                             const QVariant& theme_map)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (item_id < 0) {
        return refusal::service_item_gone();
    }
    auto item = service_repo_->get_item(item_id);
    if (!item || item->item_type != model::k_item_type_song) {
        return tr("That item is not a song.");
    }

    SongText text;
    if (const auto err = read_song_text(title, lyrics, verse_order, text)) {
        return *err;
    }

    nlohmann::json data = item->data.is_object() ? item->data : nlohmann::json::object();
    data["title"] = std::move(text.title);
    data["lyrics"] = std::move(text.lyrics);
    data["verse_order"] = std::move(text.verse_order);
    data["user_modified"] = true;
    // Valid themeMap (including {}) replaces overrides; omitted/invalid leaves prior map.
    if (theme_map.isValid() && !theme_map.isNull()) {
        data["theme_map"] = song_theme_map_from_qvariant(theme_map);
    }

    service_repo_->replace_item_data(item_id, data);

    // Library song must stay unchanged even when song_id is set.
    if (!sync_open_service_items(item_id)) {
        return tr("The song was saved, but its service is no longer open.");
    }
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::set_service_item_stack(qint64 item_id, const QString& stack_id)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (item_id < 0) {
        return refusal::service_item_gone();
    }
    auto item = service_repo_->get_item(item_id);
    if (!item) {
        return refusal::service_item_gone();
    }
    auto data = item->data.is_object() ? item->data : nlohmann::json::object();
    const auto id = stack_id.trimmed().toStdString();
    if (id.empty()) {
        data.erase("stack_id");
    } else {
        data["stack_id"] = id;
    }
    return persist_service_item_data(item_id, std::move(data));
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::persist_service_item_data(qint64 item_id, nlohmann::json data)
{
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (item_id < 0) {
        return refusal::service_item_gone();
    }
    // Throws on a failed write; the entry points calling this report it.
    service_repo_->replace_item_data(item_id, data);
    session_.patch_service_item_data(item_id, std::move(data));
    emit liveChanged(true);
    return {};
}

QString LibraryController::set_service_item_also_play(qint64 item_id,
                                                      const QVariantList& companion_ids)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (item_id < 0) {
        return refusal::service_item_gone();
    }
    auto item = service_repo_->get_item(item_id);
    if (!item) {
        return refusal::service_item_gone();
    }
    auto ids = parse_id_list(companion_ids);
    if (!ids) {
        return refusal::items_gone();
    }
    // Validate the whole list first: the live rundown must not be left holding
    // companions that were rejected, or that the write below failed to store.
    const auto err = session_.also_play_list_error(item_id, *ids);
    if (!err.empty()) {
        return QString::fromStdString(err);
    }
    model::ServiceItem probe = *item;
    probe.data = item->data.is_object() ? item->data : nlohmann::json::object();
    model::set_service_item_also_play(probe, std::move(*ids));
    return persist_service_item_data(item_id, probe.data);
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

namespace {

/// ``ids`` in the order their rows sit in the open rundown.
std::vector<std::int64_t> in_rundown_order(const session::Session& session,
                                           const std::vector<std::int64_t>& ids)
{
    std::vector<std::int64_t> out;
    for (const auto& item : session.service_items()) {
        if (item.item_id && std::find(ids.begin(), ids.end(), *item.item_id) != ids.end()) {
            out.push_back(*item.item_id);
        }
    }
    return out;
}

} // namespace

QString LibraryController::setServiceItemsStack(const QVariantList& item_ids, const QString& stack_id)
try {
    if (!service_repo_ || !db_) {
        return refusal::no_library();
    }
    const auto ids = parse_id_list(item_ids);
    if (!ids || in_rundown_order(session_, *ids).size() != ids->size()) {
        return refusal::items_gone();
    }
    const auto stack = stack_id.trimmed().toStdString();
    std::vector<std::pair<std::int64_t, nlohmann::json>> changed;
    for (const auto id : *ids) {
        auto item = service_repo_->get_item(id);
        if (!item) {
            return refusal::items_gone();
        }
        auto data = item->data.is_object() ? item->data : nlohmann::json::object();
        if (stack.empty()) {
            data.erase("stack_id");
        } else {
            data["stack_id"] = stack;
        }
        changed.emplace_back(id, std::move(data));
    }
    {
        db::Transaction tx(*db_);
        for (const auto& [id, data] : changed) {
            service_repo_->replace_item_data(id, data);
        }
        tx.commit();
    }
    for (auto& [id, data] : changed) {
        session_.patch_service_item_data(id, std::move(data));
    }
    emit liveChanged(true);
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::linkServiceItems(const QVariantList& item_ids)
try {
    if (!service_repo_ || !db_) {
        return refusal::no_library();
    }
    const auto ids = parse_id_list(item_ids);
    if (!ids) {
        return refusal::items_gone();
    }
    std::vector<std::int64_t> members;
    for (const auto id : *ids) {
        auto set = session_.also_play_set(id);
        if (set.empty()) {
            set.push_back(id);
        }
        for (const auto m : set) {
            if (std::find(members.begin(), members.end(), m) == members.end()) {
                members.push_back(m);
            }
        }
    }
    const auto ordered = in_rundown_order(session_, members);
    if (ordered.size() != members.size()) {
        return refusal::items_gone();
    }
    if (ordered.size() < 2) {
        return tr("Select at least two items to play together.");
    }
    // The topmost row holds the list for the whole set. Validating it checks
    // that every member plays on a stack of its own.
    const auto holder = ordered.front();
    const std::vector<std::int64_t> rest(ordered.begin() + 1, ordered.end());
    const auto err = session_.also_play_list_error(holder, rest);
    if (!err.empty()) {
        return QString::fromStdString(err);
    }
    std::vector<std::pair<std::int64_t, std::vector<std::int64_t>>> lists;
    for (const auto m : ordered) {
        lists.emplace_back(m, m == holder ? rest : std::vector<std::int64_t>{});
    }
    return write_also_play_lists(lists);
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::unlinkServiceItem(qint64 item_id)
try {
    if (!service_repo_ || !db_) {
        return refusal::no_library();
    }
    if (item_id < 0) {
        return refusal::service_item_gone();
    }
    const std::int64_t id = item_id;
    const auto set = session_.also_play_set(id);
    if (set.empty()) {
        return {};
    }
    std::vector<std::int64_t> rest;
    for (const auto m : set) {
        if (m != id) {
            rest.push_back(m);
        }
    }
    // The rows that stay are rewritten with the topmost holding the list, so
    // it no longer matters which row held it before. One row cannot play
    // together with itself, so a pair comes apart entirely.
    std::vector<std::pair<std::int64_t, std::vector<std::int64_t>>> lists;
    for (const auto m : set) {
        if (rest.size() >= 2 && m == rest.front()) {
            lists.emplace_back(m, std::vector<std::int64_t>(rest.begin() + 1, rest.end()));
        } else {
            lists.emplace_back(m, std::vector<std::int64_t>{});
        }
    }
    return write_also_play_lists(lists);
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::write_also_play_lists(
    const std::vector<std::pair<std::int64_t, std::vector<std::int64_t>>>& lists)
{
    std::vector<std::pair<std::int64_t, nlohmann::json>> changed;
    for (const auto& [id, list] : lists) {
        auto item = service_repo_->get_item(id);
        if (!item) {
            return refusal::items_gone();
        }
        if (model::service_item_also_play(*item) == list) {
            continue;
        }
        if (!item->data.is_object()) {
            item->data = nlohmann::json::object();
        }
        model::set_service_item_also_play(*item, list);
        changed.emplace_back(id, std::move(item->data));
    }
    if (changed.empty()) {
        return {};
    }
    {
        db::Transaction tx(*db_);
        for (const auto& [id, data] : changed) {
            service_repo_->replace_item_data(id, data);
        }
        tx.commit();
    }
    for (auto& [id, data] : changed) {
        session_.patch_service_item_data(id, std::move(data));
    }
    emit liveChanged(true);
    return {};
}

namespace {

/// The song file at ``raw``. The user is told a file is damaged; what the
/// parser said of it stays in the log.
parse::SongFile read_song_file_logged(const QString& raw)
{
    auto file = parse::read_song_file(local_path_from_qml(raw));
    if (!file.detail.empty()) {
        qWarning("SeventhSlide: %s: %s", file.name.c_str(), file.detail.c_str());
    }
    return file;
}

}  // namespace

QVariantMap LibraryController::importSongs(const QStringList& local_paths)
try {
    QVariantMap out;
    QStringList errors;
    QStringList warnings;
    int count = 0;
    if (!song_repo_) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("count"), 0);
        out.insert(QStringLiteral("errors"), QStringList{refusal::no_library()});
        return out;
    }
    {
        // One commit for the whole import rather than one per song.
        db::Transaction tx(*db_);
        for (const auto& raw : local_paths) {
            auto file = read_song_file_logged(raw);
            const auto name = QString::fromStdString(file.name);
            if (!file.error.empty()) {
                errors.push_back(
                    (file.format_name.empty()
                         ? name
                         : name + " (" + QString::fromStdString(file.format_name) + ")")
                    + ": " + QString::fromStdString(file.error));
                continue;
            }
            try {
                for (auto& imported : file.songs) {
                    (void)song_repo_->insert(imported.song);
                    ++count;
                    for (const auto& warning : imported.warnings) {
                        warnings.push_back(QString::fromStdString(imported.song.title) + ": "
                                           + QString::fromStdString(warning));
                    }
                }
            } catch (const std::exception& ex) {
                errors.push_back(name + ": " + report_entry_error(__func__, ex));
            }
        }
        tx.commit();
    }
    refresh_songs();
    out.insert(QStringLiteral("ok"), errors.isEmpty());
    out.insert(QStringLiteral("count"), count);
    out.insert(QStringLiteral("errors"), errors);
    out.insert(QStringLiteral("warnings"), warnings);
    return out;
} catch (const std::exception& ex) {
    QVariantMap out;
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("count"), 0);
    out.insert(QStringLiteral("errors"), QStringList{app::report_entry_error(__func__, ex)});
    return out;
}

QVariantList LibraryController::readSongFiles(const QStringList& local_paths)
try {
    read_songs_.clear();
    QVariantList unreadable;
    QVariantList rows;
    for (const auto& raw : local_paths) {
        const auto file = read_song_file_logged(raw);
        if (!file.error.empty()) {
            unreadable.push_back(QVariantMap{
                {QStringLiteral("row"), -1},
                {QStringLiteral("title"), QString::fromStdString(file.name)},
                {QStringLiteral("format"), QString::fromStdString(file.format_name)},
                {QStringLiteral("note"), QString::fromStdString(file.error)},
                {QStringLiteral("importable"), false},
            });
            continue;
        }
        for (const auto& imported : file.songs) {
            QStringList notes;
            for (const auto& warning : imported.warnings) {
                notes.push_back(QString::fromStdString(warning));
            }
            rows.push_back(QVariantMap{
                {QStringLiteral("row"), static_cast<int>(read_songs_.size())},
                {QStringLiteral("title"), QString::fromStdString(imported.song.title)},
                {QStringLiteral("format"),
                 QString::fromUtf8(parse::song_format_name(imported.format))},
                {QStringLiteral("note"), notes.join(QStringLiteral("; "))},
                {QStringLiteral("importable"), true},
            });
            read_songs_.push_back(imported.song);
        }
    }
    const QCollator& collator = db::name_collator();
    std::stable_sort(rows.begin(), rows.end(), [&](const QVariant& a, const QVariant& b) {
        return collator.compare(a.toMap().value(QStringLiteral("title")).toString(),
                                b.toMap().value(QStringLiteral("title")).toString())
            < 0;
    });
    return unreadable + rows;
} catch (const std::exception& ex) {
    read_songs_.clear();
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {};
}

QVariantMap LibraryController::importReadSongs(const QVariantList& rows)
try {
    QVariantMap out;
    if (!song_repo_) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("count"), 0);
        out.insert(QStringLiteral("errors"), QStringList{refusal::no_library()});
        return out;
    }
    auto songs = std::exchange(read_songs_, {});
    int count = 0;
    {
        db::Transaction tx(*db_);
        for (const auto& row : rows) {
            const auto at = row.toInt();
            if (at >= 0 && static_cast<std::size_t>(at) < songs.size()) {
                (void)song_repo_->insert(songs[static_cast<std::size_t>(at)]);
                ++count;
            }
        }
        tx.commit();
    }
    refresh_songs();
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("count"), count);
    out.insert(QStringLiteral("errors"), QStringList{});
    return out;
} catch (const std::exception& ex) {
    QVariantMap out;
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("count"), 0);
    out.insert(QStringLiteral("errors"), QStringList{app::report_entry_error(__func__, ex)});
    return out;
}

void LibraryController::discardReadSongs()
{
    read_songs_.clear();
}

QVariantMap LibraryController::importBible(const QString& local_path)
try {
    QVariantMap out;
    if (!bible_repo_) {
        out.insert(QStringLiteral("error"), refusal::no_library());
        return out;
    }
    const auto path = local_path_from_qml(local_path);
    auto parsed = parse::parse_bible_file(path);
    if (parsed.verses.empty()) {
        out.insert(QStringLiteral("error"), tr("That file has no verses SeventhSlide can read."));
        return out;
    }
    const auto id =
        bible_repo_->import_bible(parsed.name, parsed.copyright, parsed.verses);
    refresh_bibles();
    out.insert(QStringLiteral("id"), static_cast<qint64>(id));
    out.insert(QStringLiteral("name"), QString::fromStdString(parsed.name));
    out.insert(QStringLiteral("verseCount"), static_cast<int>(parsed.verses.size()));
    return out;
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), app::report_entry_error(__func__, ex)}};
}

bool LibraryController::openService(qint64 service_id)
try {
    if (!service_repo_ || service_id < 0) {
        return false;
    }
    const auto svc = service_repo_->get_service(service_id);
    if (!svc) {
        return false;
    }
    open_rundown(svc->id, svc->theme_map, service_repo_->list_items(service_id));
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

QString LibraryController::closeService()
try {
    if (!session_.service_id()) {
        return refusal::no_service_open();
    }
    open_rundown(std::nullopt, nlohmann::json::object(), {});
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

qint64 LibraryController::createService(const QString& name)
try {
    if (!service_repo_) {
        return -1;
    }
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return -1;
    }
    const auto id = service_repo_->create_service(trimmed.toStdString());
    refresh_services();
    if (!openService(static_cast<qint64>(id))) {
        return -1;
    }
    return static_cast<qint64>(id);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return -1;
}

QString LibraryController::renameService(qint64 service_id, const QString& name)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (service_id < 0) {
        return refusal::service_gone();
    }
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return refusal::name_required();
    }
    service_repo_->rename_service(service_id,
                                  trimmed.toStdString());
    if (session_.service_id() && *session_.service_id() == service_id) {
        set_open_service_name(trimmed);
    }
    refresh_services();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QVariantMap LibraryController::serviceDetails(qint64 service_id)
try {
    if (!service_repo_ || service_id < 0) {
        return {};
    }
    const auto svc = service_repo_->get_service(service_id);
    if (!svc) {
        return {};
    }
    const QDateTime at = QDateTime::fromString(QString::fromStdString(svc->service_at),
                                               QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    QStringList tags;
    for (const auto& t : svc->tags) {
        tags.push_back(QString::fromStdString(t));
    }
    return {
        {QStringLiteral("name"), QString::fromStdString(svc->name)},
        {QStringLiteral("date"), at.isValid() ? at.date().toString(Qt::ISODate) : QString()},
        {QStringLiteral("time"), at.isValid() ? at.time().toString(QStringLiteral("HH:mm")) : QString()},
        {QStringLiteral("timeLabel"),
         at.isValid() ? QLocale().toString(at.time(), QLocale::ShortFormat) : QString()},
        {QStringLiteral("tags"), tags},
        {QStringLiteral("audioFades"), json_object_to_qvariant(svc->audio_fades)},
    };
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {};
}

int LibraryController::serviceItemCount(qint64 service_id)
try {
    if (!service_repo_ || service_id < 0) {
        return 0;
    }
    return service_repo_->item_count(service_id);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return 0;
}

QVariantMap LibraryController::getServiceThemeMap(qint64 service_id)
try {
    if (!service_repo_ || service_id < 0) {
        return {};
    }
    const auto svc = service_repo_->get_service(service_id);
    if (!svc) {
        return {};
    }
    return json_object_to_qvariant(theme::service_themes_from_json(svc->theme_map).to_json());
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {};
}

QString LibraryController::setServiceThemeMap(qint64 service_id, const QVariant& theme_map)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (service_id < 0) {
        return refusal::service_gone();
    }
    const auto svc = service_repo_->get_service(service_id);
    if (!svc) {
        return refusal::service_gone();
    }
    auto themes = theme_map.isValid() && !theme_map.isNull()
                      ? theme::service_themes_from_json(to_json(theme_map))
                      : theme::OutputThemeMap{};
    service_repo_->set_service_theme_map(service_id, themes.to_json());
    if (session_.service_id()
        && *session_.service_id() == service_id) {
        session_.set_service_themes(std::move(themes));
        emit liveChanged(true);
    }
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::deleteService(qint64 service_id)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (service_id < 0) {
        return refusal::service_gone();
    }
    const bool was_open = session_.service_id()
                          && *session_.service_id() == service_id;
    if (!service_repo_->delete_service(service_id)) {
        return refusal::service_gone();
    }
    refresh_services();
    if (was_open) {
        open_rundown(std::nullopt, nlohmann::json::object(), {});
    }
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::setServiceDateTime(qint64 service_id, const QString& iso)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (service_id < 0) {
        return refusal::service_gone();
    }
    service_repo_->set_service_at(service_id,
                                  iso.toStdString());
    refresh_services();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::setServiceTags(qint64 service_id, const QStringList& tags)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (service_id < 0 || !service_repo_->get_service(service_id)) {
        return refusal::service_gone();
    }
    std::vector<std::string> list;
    list.reserve(static_cast<std::size_t>(tags.size()));
    for (const auto& t : tags) {
        list.push_back(t.toStdString());
    }
    service_repo_->set_service_tags(service_id, list);
    refresh_services();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString LibraryController::setServiceAudioFades(qint64 service_id, const QVariantMap& fades)
try {
    if (!service_repo_) {
        return refusal::no_library();
    }
    if (service_id < 0 || !service_repo_->get_service(service_id)) {
        return refusal::service_gone();
    }
    const nlohmann::json given = to_json(QVariant(fades));
    const auto set = model::set_audio_fades(given);
    service_repo_->set_service_audio_fades(service_id, set);
    if (session_.service_id() == service_id) {
        session_.set_service_audio_fades(set);
    }
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

bool LibraryController::addSongToService(qint64 song_id, int at_index)
try {
    const auto service_id = session_.service_id();
    if (!service_repo_ || !service_id || song_id < 0) {
        return false;
    }
    const auto idx = optional_index(at_index);
    if (!service_repo_->add_song_to_service(*service_id, song_id, idx)) {
        return false;
    }
    return sync_open_service_items(std::nullopt);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool LibraryController::addDividerToService(const QString& title, int at_index)
try {
    if (!service_repo_ || !session_.service_id()) {
        return false;
    }
    auto label = title.trimmed().toStdString();
    if (label.empty()) {
        label = "Section";
    }
    const auto idx = optional_index(at_index);
    return append_service_item(model::k_item_type_divider, nlohmann::json{{"title", label}},
                               std::nullopt, idx);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

QString LibraryController::renameServiceDivider(qint64 item_id, const QString& title)
try {
    if (!service_repo_ || !session_.service_id()) {
        return refusal::no_service_open();
    }
    if (item_id < 0) {
        return refusal::service_item_gone();
    }
    auto item = service_repo_->get_item(item_id);
    if (!item || item->item_type != model::k_item_type_divider) {
        return tr("That item is not a divider.");
    }
    auto label = title.trimmed().toStdString();
    if (label.empty()) {
        label = "Section";
    }
    nlohmann::json data = item->data.is_object() ? item->data : nlohmann::json::object();
    data["title"] = label;
    service_repo_->replace_item_data(item_id, data);
    if (!sync_open_service_items(item_id)) {
        return tr("The divider was renamed, but its service is no longer open.");
    }
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

bool LibraryController::remove_service_item(qint64 item_id)
try {
    if (!service_repo_ || !session_.service_id() || item_id < 0) {
        return false;
    }
    if (!service_repo_->remove_item(item_id)) {
        return false;
    }
    return sync_open_service_items(std::nullopt);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

QString LibraryController::removeServiceItems(const QVariantList& item_ids)
try {
    if (!service_repo_ || !db_ || !session_.service_id()) {
        return refusal::no_service_open();
    }
    const auto ids = parse_id_list(item_ids);
    if (!ids || in_rundown_order(session_, *ids).size() != ids->size()) {
        return refusal::items_gone();
    }
    {
        db::Transaction tx(*db_);
        for (const auto id : *ids) {
            if (!service_repo_->remove_item(id)) {
                return refusal::items_gone();
            }
        }
        tx.commit();
    }
    (void)sync_open_service_items(std::nullopt);
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

bool LibraryController::moveServiceItemTo(int from_index, int to_index)
try {
    const auto service_id = session_.service_id();
    if (!service_repo_ || !service_id) {
        return false;
    }
    const auto& items = session_.service_items();
    const int n = static_cast<int>(items.size());
    if (from_index < 0 || from_index >= n || to_index < 0 || to_index >= n
        || from_index == to_index) {
        return false;
    }
    std::vector<std::int64_t> ids;
    ids.reserve(static_cast<std::size_t>(n));
    for (const auto& it : items) {
        if (!it.item_id) {
            return false;
        }
        ids.push_back(*it.item_id);
    }
    const auto moved = ids[static_cast<std::size_t>(from_index)];
    ids.erase(ids.begin() + from_index);
    ids.insert(ids.begin() + to_index, moved);
    service_repo_->reorder_items(*service_id, ids);
    return sync_open_service_items(std::nullopt);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool LibraryController::reorderServiceItems(const QVariantList& ordered_item_ids)
try {
    const auto service_id = session_.service_id();
    if (!service_repo_ || !service_id) {
        return false;
    }
    const auto parsed = parse_id_list(ordered_item_ids);
    if (!parsed) {
        return false;
    }
    const std::vector<std::int64_t>& ids = *parsed;
    if (ids.size() != session_.service_items().size()) {
        return false;
    }
    service_repo_->reorder_items(*service_id, ids);
    return sync_open_service_items(std::nullopt);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool LibraryController::sync_open_service_items(std::optional<std::int64_t> edited_item_id)
{
    const auto open = session_.service_id();
    if (!service_repo_ || !open) {
        return false;
    }
    const auto service_id = *open;
    const auto svc = service_repo_->get_service(service_id);
    if (!svc) {
        return false;
    }
    set_open_service_name(QString::fromStdString(svc->name));
    // Rundown edits only change the rundown. What is live stays live, where it
    // is; the one exception is the edited item itself, re-shown in place.
    session_.replace_service_items(service_repo_->list_items(service_id));
    if (edited_item_id) {
        (void)session_.refresh_service_item(*edited_item_id);
    }
    sync_service_list_selection();
    emit liveChanged(true);
    return true;
}

void LibraryController::open_rundown(std::optional<std::int64_t> service_id,
                                     nlohmann::json service_theme_map,
                                     std::vector<model::ServiceItem> items)
{
    session_.open_service(service_id, theme::service_themes_from_json(service_theme_map),
                          std::move(items));
    session_.set_service_audio_fades(nlohmann::json::object());
    if (!service_id) {
        set_open_service_name({});
    } else if (const auto svc = service_repo_ ? service_repo_->get_service(*service_id)
                                              : std::nullopt) {
        set_open_service_name(QString::fromStdString(svc->name));
        session_.set_service_audio_fades(svc->audio_fades);
    } else {
        set_open_service_name(tr("Service %1").arg(static_cast<qint64>(*service_id)));
    }
    sync_service_list_selection();
    emit liveChanged(true);
}

void LibraryController::sync_service_list_selection()
{
    services_model_.set_open_service_id(session_.service_id());
}

std::optional<model::BiblePassage> LibraryController::bible_passage(qint64 bible_id,
                                                                   const QString& book_name,
                                                                   int chapter,
                                                                   int verse_start,
                                                                   int verse_end)
{
    if (!bible_repo_ || bible_id < 0) {
        return std::nullopt;
    }
    return bible_repo_->passage(bible_id, book_name.toStdString(), chapter, verse_start,
                                verse_end);
}

} // namespace seventhslide::app
