#pragma once

// The song, Bible and service library.
//
// Owns the repositories over the open database and the list models QML binds
// to. The service the operator currently has OPEN - the live rundown - lives
// in the session, so edits to it reach the session as well as the database,
// and liveChanged tells the application to follow.
//
// SessionFacade owns one and exposes it as `session.library`.

#include "library_models.hpp"
#include "rundown.hpp"
#include "service_list_model.hpp"
#include "shipped_bibles.hpp"
#include "song_list_model.hpp"

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QThreadPool>
#include <QVariantMap>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace seventhslide::app {

class LibraryController : public QObject, public IRundown {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(seventhslide::app::ServiceListModel* services READ services CONSTANT)
    Q_PROPERTY(seventhslide::app::SongListModel* songs READ songs CONSTANT)
    /// Every imported Bible, for Settings.
    Q_PROPERTY(seventhslide::app::BibleListModel* bibles READ bibles CONSTANT)
    /// The Bibles switched on: the ones offered where a translation is picked.
    Q_PROPERTY(seventhslide::app::BibleListModel* enabledBibles READ enabledBibles CONSTANT)
    Q_PROPERTY(seventhslide::app::BibleVerseListModel* bibleVerses READ bibleVerses CONSTANT)
    Q_PROPERTY(qint64 selectedBibleId READ selectedBibleId NOTIFY bibleBrowseChanged)
    Q_PROPERTY(QString selectedBibleName READ selectedBibleName NOTIFY bibleBrowseChanged)
    /// The selected Bible's abbreviation; empty when it has none.
    Q_PROPERTY(QString selectedBibleAbbreviation READ selectedBibleAbbreviation
                   NOTIFY bibleBrowseChanged)
    Q_PROPERTY(QString selectedBibleBook READ selectedBibleBook NOTIFY bibleBrowseChanged)
    Q_PROPERTY(int selectedBibleChapter READ selectedBibleChapter NOTIFY bibleBrowseChanged)
    Q_PROPERTY(QString openServiceName READ openServiceName NOTIFY changed)
    /// The Library panel's tab showing (its id: ``songs``, ``browser``, ...).
    /// Kept per computer, like the Audio Mixer's: it is how this operator
    /// works at this screen, not part of the library.
    Q_PROPERTY(QString shownTab READ shownTab WRITE setShownTab NOTIFY shownTabChanged)
    /// The Library panel's tabs as Settings arranged them (their ids), kept
    /// per computer for the same reason. See LibraryTabs.js for what is made
    /// of ids that are missing or no longer exist.
    Q_PROPERTY(QStringList tabOrder READ tabOrder WRITE setTabOrder NOTIFY tabLayoutChanged)
    Q_PROPERTY(QStringList hiddenTabs READ hiddenTabs WRITE setHiddenTabs NOTIFY tabLayoutChanged)
    /// The most tabs its strip shows at once; a narrow panel shows fewer.
    Q_PROPERTY(int tabsShown READ tabsShown WRITE setTabsShown NOTIFY tabLayoutChanged)

public:
    explicit LibraryController(session::Session& session, QObject* parent = nullptr);
    ~LibraryController() override;

    /// Point the library at a newly opened database, or let go of one that is
    /// closing. The repositories live exactly as long as the database does.
    /// Once an open has settled, the library takes its copy of the shipped
    /// Bibles if it has not had it.
    void attach(db::Database& db);
    void detach();

    /// Whether the library takes writes here (a viewer's does not).
    void set_may_write(std::function<bool()> may_write) { may_write_ = std::move(may_write); }
    /// Whether the library may take its copy of the shipped Bibles here: only
    /// where it was made, as FontController's shipped fonts. Yes when unset.
    void set_may_take_shipped(std::function<bool()> may_take) { may_take_shipped_ = std::move(may_take); }
    /// Where the shipped Bibles are (``k_shipped_bibles_dir`` unless set).
    void set_shipped_bibles_dir(QString dir) { shipped_bibles_dir_ = std::move(dir); }
    /// Copy the shipped Bibles into the library, the first time only, so one
    /// removed stays removed. One the library has a Bible of that name for
    /// keeps its own. They are read off this thread, then imported.
    void take_shipped_bibles();
    [[nodiscard]] bool attached() const noexcept { return service_repo_ != nullptr; }

    // --- Service panel ---
    void refresh_services();

    /// Re-read every song.
    void refresh_songs();
    /// Re-read only the songs with these uids (sync changed them), and let
    /// go of those no longer here.
    void refresh_songs(const std::set<std::string>& uids);
    /// Re-read the Bible lists and keep the browse selection on a Bible that
    /// is switched on.
    void refresh_bibles();

    Q_INVOKABLE bool openService(qint64 service_id);
    /// Close the open service: no service open, so no rundown, and every
    /// stack cleared, as opening another service clears them. The service
    /// stays in the library. Empty = ok.
    Q_INVOKABLE QString closeService();

    /// Create a service dated now, and open it. -1 when it could not be.
    Q_INVOKABLE qint64 createService(const QString& name);

    Q_INVOKABLE QString renameService(qint64 service_id, const QString& name);

    /// Delete service (and items). Empty = ok. Clears session if it was open.
    Q_INVOKABLE QString deleteService(qint64 service_id);

    /// Set when the service is held (local ISO ``yyyy-MM-ddTHH:mm:ss``; "" for
    /// no date). Empty = ok.
    Q_INVOKABLE QString setServiceDateTime(qint64 service_id, const QString& iso);

    /// Replace the service's tags (tidied, without repeats). Empty = ok.
    Q_INVOKABLE QString setServiceTags(qint64 service_id, const QStringList& tags);

    /// The audio fades the service sets for its audio: ``fades`` holds any of
    /// fade_in_ms, fade_out_ms and overlap_ms (ms, each clamped); a field it
    /// does not hold follows each output's own. The open service's take
    /// effect at once. Empty = ok.
    Q_INVOKABLE QString setServiceAudioFades(qint64 service_id, const QVariantMap& fades);

    /// Service ``service_id``'s details, for Service Settings: ``{name, date,
    /// time, timeLabel, tags, audioFades}`` (``date`` ``yyyy-MM-dd`` and
    /// ``time`` ``HH:mm``, both "" for no date; ``timeLabel`` as the locale
    /// writes it; ``audioFades`` only the fields it sets). Empty when it is
    /// gone.
    Q_INVOKABLE QVariantMap serviceDetails(qint64 service_id);

    /// How many items service ``service_id`` holds, its dividers aside (the
    /// Services window's line under each name); 0 when it is gone.
    Q_INVOKABLE int serviceItemCount(qint64 service_id);

    /// Per-output service theme overrides (``{outputId: {text?, bg?}}``). Empty map if missing.
    Q_INVOKABLE QVariantMap getServiceThemeMap(qint64 service_id);

    /// Replace service theme overrides. ``{}`` clears them. Empty return = ok.
    /// Reloads the open service so live slides pick up the new map.
    Q_INVOKABLE QString setServiceThemeMap(qint64 service_id, const QVariant& theme_map);

    /// ``at_index`` < 0 appends (toolbar Add); >= 0 inserts at that service order.
    Q_INVOKABLE bool addSongToService(qint64 song_id, int at_index = -1);

    /// Insert a labelled section divider (``item_type = divider``). Empty title -> "Section".
    Q_INVOKABLE bool addDividerToService(const QString& title = QString(), int at_index = -1);

    /// Rename a divider's label. Empty return = ok.
    Q_INVOKABLE QString renameServiceDivider(qint64 item_id, const QString& title);

    /// Take row ``item_id`` out of the open service: the web remote's. A view
    /// takes rows out with removeServiceItems.
    bool remove_service_item(qint64 item_id);
    /// Take these rows out of the open service, all or none. Empty return = ok.
    Q_INVOKABLE QString removeServiceItems(const QVariantList& item_ids);

    /// Move item from ``from_index`` to ``to_index`` (absolute, for drag-drop).
    Q_INVOKABLE bool moveServiceItemTo(int from_index, int to_index);

    /// Full reorder by durable item ids (must list every item).
    Q_INVOKABLE bool reorderServiceItems(const QVariantList& ordered_item_ids);

    /// Library song fields for the editor; empty map if missing.
    /// Includes authors, songbook, copyright, ccli, key, showCopyright, themeMap,
    /// topics and comments.
    Q_INVOKABLE QVariantMap getSong(qint64 song_id);

    /// Create library song. Optional ``meta``: authors (list), songbookName,
    /// songbookEntry, copyright, ccliSongNumber, key, showCopyright, themeMap
    /// (``{outputId: {text?, bg?}}``; empty ids omitted), topics (list) and
    /// comments.
    /// Returns {songId} or {error}.
    Q_INVOKABLE QVariantMap createSong(const QString& title,
                                              const QString& lyrics,
                                              const QString& verse_order,
                                              const QVariantMap& meta = {});

    /// Update library song (+ optional meta, same keys as create). Empty = ok.
    /// ``themeMap`` in meta replaces overrides when present; omitted leaves prior map.
    Q_INVOKABLE QString updateSong(qint64 song_id,
                                          const QString& title,
                                          const QString& lyrics,
                                          const QString& verse_order,
                                          const QVariantMap& meta = {});

    /// Delete library song. Empty return = ok.
    Q_INVOKABLE QString deleteSong(qint64 song_id);

    Q_INVOKABLE bool selectBible(qint64 bible_id);

    /// Snapshot a passage (loads verse texts) into the open service.
    Q_INVOKABLE bool addBibleToService(int verse_start, int verse_end, int at_index = -1);

    /// Rundown counterpart of play_bible_ref.
    bool add_bible_ref_to_service(qint64 bible_id,
                                          const QString& book_name,
                                          int chapter,
                                          int verse_start,
                                          int verse_end,
                                          int at_index = -1);

    /// Resolve free-text ref (``John 3:16``) against the selected bible. On
    /// success: updates browse selection + verse
    /// list and returns {success, book, chapter, verse_start, verse_end, ref, ...}.
    /// Empty / invalid refs clear the verse list and say why in "message". A
    /// library that cannot be read answers {success: false} and says so by
    /// ``errorOccurred``.
    Q_INVOKABLE QVariantMap resolveBibleRef(const QString& reference);

    /// Prefix/alias book suggestions for the selected bible (max 12).
    Q_INVOKABLE QStringList suggestBibleBooks(const QString& prefix);

    /// Rename a bible translation. Empty return = ok.
    Q_INVOKABLE QString renameBible(qint64 bible_id, const QString& name);

    /// Set a translation's short name. Empty return = ok.
    Q_INVOKABLE QString setBibleAbbreviation(qint64 bible_id, const QString& abbreviation);
    /// Switch a translation on or off. Empty return = ok.
    Q_INVOKABLE QString setBibleEnabled(qint64 bible_id, bool enabled);

    /// Delete a bible (+ verses). Empty return = ok.
    Q_INVOKABLE QString deleteBible(qint64 bible_id);

    // --- Song editor mid-service ---
    /// Song service-item fields for the editor; empty map if missing / not a song.
    /// Includes ``themeMap`` (``{outputId: {text?, bg?}}``).
    Q_INVOKABLE QVariantMap getServiceSongItem(qint64 item_id);

    /// Persist title/lyrics/verse_order onto the service snapshot (not the library).
    /// Optional ``themeMap`` (valid QVariantMap, including {}) replaces overrides;
    /// omitted / invalid leaves the prior snapshot map. Empty return = ok.
    Q_INVOKABLE QString updateServiceSongItem(qint64 item_id,
                                             const QString& title,
                                             const QString& lyrics,
                                             const QString& verse_order,
                                             const QVariant& theme_map = QVariant());

    /// Pin row ``item_id`` to stack ``stack_id`` (empty: its type's). Empty
    /// return = ok. The web remote's; a view pins rows with setServiceItemsStack.
    QString set_service_item_stack(qint64 item_id, const QString& stack_id);
    /// Pin these rows to stack ``stack_id`` (empty: their type's), all or
    /// none. Empty return = ok.
    Q_INVOKABLE QString setServiceItemsStack(const QVariantList& item_ids, const QString& stack_id);

    /// Replace the Also-play companion list on a rundown row. Empty list
    /// clears. The web remote's; the console links rows with linkServiceItems.
    QString set_service_item_also_play(qint64 item_id, const QVariantList& companion_ids);

    /// Make these rows play together. A row that is already linked brings its
    /// whole set along, so the result is one set. Empty return = ok.
    Q_INVOKABLE QString linkServiceItems(const QVariantList& item_ids);

    /// Take this one row out of its Also-play set, whichever row holds the
    /// link. A set left with a single row is dissolved. Empty return = ok.
    Q_INVOKABLE QString unlinkServiceItem(qint64 item_id);

    // --- Library import ---
    /// Import song files of any kind parse::read_song_file reads (OpenLyrics,
    /// OpenSong, ChordPro, SongSelect, plain text). Returns {count, ok,
    /// errors: ["file: why"], warnings: ["title: what was dropped"]}.
    Q_INVOKABLE QVariantMap importSongs(const QStringList& local_paths);

    /// Read song files and hold their songs, saving nothing yet: a row each
    /// for the import window, files that gave no song first and the rest by
    /// title. ``{row, title, format, note, importable}``; a file that gave
    /// none has its name as ``title`` and why as ``note``.
    Q_INVOKABLE QVariantList readSongFiles(const QStringList& local_paths);

    /// Save the songs ``readSongFiles`` holds at these ``row``s and let the
    /// rest go. Returns {count, ok, errors: [..]}.
    Q_INVOKABLE QVariantMap importReadSongs(const QVariantList& rows);

    /// Let go of the songs ``readSongFiles`` holds.
    Q_INVOKABLE void discardReadSongs();

    /// Import one Zefania/OSIS/OpenSong bible XML. Returns {id, name} or {error}.
    Q_INVOKABLE QVariantMap importBible(const QString& local_path);

    // The rundown, as the library tabs add to it (IRundown). The defaults
    // are the interface's alone.
    [[nodiscard]] bool append_service_item(std::string_view item_type,
                                           const nlohmann::json& data,
                                           std::optional<std::int64_t> song_id,
                                           std::optional<int> at_index) override;
    bool append_service_items(std::string_view item_type,
                              const std::vector<nlohmann::json>& items,
                              std::optional<int> at_index) override;
    bool sync_open_service_items(std::optional<std::int64_t> edited_item_id) override;

    [[nodiscard]] ServiceListModel* services() noexcept { return &services_model_; }
    [[nodiscard]] SongListModel* songs() noexcept { return &songs_model_; }
    [[nodiscard]] BibleListModel* bibles() noexcept { return &bible_model_; }
    [[nodiscard]] BibleListModel* enabledBibles() noexcept { return &enabled_bible_model_; }
    [[nodiscard]] BibleVerseListModel* bibleVerses() noexcept { return &bible_verses_model_; }

    [[nodiscard]] qint64 selectedBibleId() const noexcept
    {
        return selected_bible_id_.value_or(-1);
    }
    [[nodiscard]] QString selectedBibleName() const { return selected_bible_name_; }
    [[nodiscard]] QString selectedBibleAbbreviation() const
    {
        return selected_bible_abbreviation_;
    }
    [[nodiscard]] QString selectedBibleBook() const { return selected_bible_book_; }
    [[nodiscard]] int selectedBibleChapter() const noexcept { return selected_bible_chapter_; }
    [[nodiscard]] QString openServiceName() const { return open_service_name_; }
    [[nodiscard]] QString shownTab() const { return shown_tab_; }
    void setShownTab(const QString& tab);
    [[nodiscard]] QStringList tabOrder() const { return tab_order_; }
    void setTabOrder(const QStringList& order);
    [[nodiscard]] QStringList hiddenTabs() const { return hidden_tabs_; }
    void setHiddenTabs(const QStringList& hidden);
    [[nodiscard]] int tabsShown() const noexcept { return tabs_shown_; }
    void setTabsShown(int count);

    /// C++ access for the rest of the application (profile capture reads songs;
    /// media and the Freeform library append rundown rows).
    [[nodiscard]] db::SongRepository* song_repo() noexcept { return song_repo_.get(); }
    [[nodiscard]] db::ServiceRepository* service_repo() noexcept { return service_repo_.get(); }
    [[nodiscard]] db::BibleRepository* bible_repo() noexcept { return bible_repo_.get(); }
    /// Verses ``verse_start``..``verse_end`` of ``book_name`` ``chapter`` in
    /// Bible ``bible_id``; nullopt without a library or such a passage.
    [[nodiscard]] std::optional<model::BiblePassage> bible_passage(qint64 bible_id,
                                                                 const QString& book_name,
                                                                 int chapter,
                                                                 int verse_start,
                                                                 int verse_end);

    /// Make ``items`` the live rundown of service ``service_id`` (none: close
    /// it), with ``service_theme_map`` its theme picks as stored. The rundown
    /// is named from the database.
    void open_rundown(std::optional<std::int64_t> service_id,
                      nlohmann::json service_theme_map,
                      std::vector<model::ServiceItem> items);

signals:
    /// The library's own lists changed.
    void changed();
    /// The Bible browse selection moved.
    void bibleBrowseChanged();
    void shownTabChanged();
    void tabLayoutChanged();
    /// An entry point failed and has no return value to carry the reason:
    /// the shell shows ``message`` to the operator (see entry_error.hpp).
    void errorOccurred(const QString& message);
    /// Live content or the rundown around it changed: the application reloads
    /// what follows it and repaints. ``rebuildModels`` is false for a change
    /// that only moved a value inside a row.
    void liveChanged(bool rebuildModels);

private:
    /// Song ``song_id`` was saved or deleted here: its row read again, or
    /// let go of, and no other song read.
    void song_changed(std::int64_t song_id);
    QString persist_service_item_data(qint64 item_id, nlohmann::json data);
    /// Store each row's new Also-play list in one transaction.
    QString write_also_play_lists(
        const std::vector<std::pair<std::int64_t, std::vector<std::int64_t>>>& lists);

    void set_open_service_name(QString name);
    /// Mark the open service in the service list.
    void sync_service_list_selection();

    [[nodiscard]] bool may_write() const { return !may_write_ || may_write_(); }
    [[nodiscard]] bool may_take_shipped() const { return !may_take_shipped_ || may_take_shipped_(); }
    /// The shipped Bibles, read, into the library ``generation`` was.
    void finish_taking_bibles(std::uint64_t generation, const std::vector<ShippedBible>& bibles);

    session::Session& session_;
    db::Database* db_ = nullptr;
    std::unique_ptr<db::SongRepository> song_repo_;
    /// Songs ``readSongFiles`` read, until they are saved or let go.
    std::vector<model::Song> read_songs_;
    std::unique_ptr<db::ServiceRepository> service_repo_;
    std::unique_ptr<db::BibleRepository> bible_repo_;

    SongListModel songs_model_;
    BibleListModel bible_model_;
    BibleListModel enabled_bible_model_;
    BibleVerseListModel bible_verses_model_;
    ServiceListModel services_model_;
    QString shown_tab_;
    QStringList tab_order_;
    QStringList hidden_tabs_;
    int tabs_shown_;

    std::optional<std::int64_t> selected_bible_id_;
    QString selected_bible_name_;
    QString selected_bible_abbreviation_;
    QString selected_bible_book_;
    int selected_bible_chapter_ = 0;
    QString open_service_name_;

    std::function<bool()> may_write_;
    std::function<bool()> may_take_shipped_;
    QString shipped_bibles_dir_ = QLatin1String(k_shipped_bibles_dir);
    /// Counts each database attached or let go: the shipped Bibles read for
    /// one are not imported into another.
    std::uint64_t generation_ = 0;
    /// Reads the shipped Bibles, one job at a time.
    QThreadPool workers_;
};

} // namespace seventhslide::app
