// Services, songs and Bibles through the facade (LibraryController).

#include "session_facade_fixture.hpp"
#include "refusals.hpp"
#include "song_text.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/sync/changed.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MediaKindQml = seventhslide::app::MediaKindQml;

using namespace seventhslide::test::facade;
using seventhslide::db::MediaKind;
namespace refusal = seventhslide::app::refusal;

TEST_CASE("SessionFacade service create / add song / reorder", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    Song a;
    a.title = "Alpha";
    a.lyrics = "[V1]\nOne\nTwo";
    a.verse_order = "v1";
    const auto id_a = facade.song_repo()->insert(a);

    Song b;
    b.title = "Beta";
    b.lyrics = "[V1]\nThree\nFour";
    b.verse_order = "v1";
    const auto id_b = facade.song_repo()->insert(b);

    facade.library()->refresh_songs();
    REQUIRE(facade.library()->songs()->rowCount() == 2);

    const auto svc_id = facade.library()->createService(QStringLiteral("Sunday"));
    REQUIRE(svc_id > 0);
    REQUIRE(facade.openServiceId() == svc_id);
    REQUIRE(facade.library()->openServiceName() == QStringLiteral("Sunday"));
    REQUIRE(facade.library()->services()->rowCount() == 1);
    REQUIRE(facade.library()->services()
                ->data(facade.library()->services()->index(0),
                       seventhslide::app::ServiceListModel::IsOpenRole)
                .toBool());

    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_a)));
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_b)));
    REQUIRE(facade.serviceItems()->rowCount() == 2);
    REQUIRE(facade.live()->service_item_index() == -1); // adding only adds; nothing goes live

    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.live()->current_title() == QStringLiteral("Alpha"));

    REQUIRE(facade.library()->moveServiceItemTo(0, 1));
    REQUIRE(facade.serviceItems()->rowCount() == 2);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(0),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("Beta"));
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(1),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("Alpha"));

    const auto item_id = facade.serviceItems()
                             ->data(facade.serviceItems()->index(0),
                                    seventhslide::app::ServiceItemModel::ItemIdRole)
                             .toLongLong();
    REQUIRE(facade.library()->remove_service_item(item_id));
    REQUIRE(facade.serviceItems()->rowCount() == 1);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(0),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("Alpha"));
}

TEST_CASE("SessionFacade addSongToService at_index inserts", "[app][dnd]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Order")) > 0);

    Song a;
    a.title = "First";
    a.lyrics = "[V1]\nA";
    a.verse_order = "v1";
    const auto id_a = facade.song_repo()->insert(a);
    Song b;
    b.title = "Second";
    b.lyrics = "[V1]\nB";
    b.verse_order = "v1";
    const auto id_b = facade.song_repo()->insert(b);
    Song c;
    c.title = "Insert";
    c.lyrics = "[V1]\nC";
    c.verse_order = "v1";
    const auto id_c = facade.song_repo()->insert(c);
    facade.library()->refresh_songs();

    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_a)));
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_b)));
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_c), 1));
    REQUIRE(facade.serviceItems()->rowCount() == 3);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(0),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("First"));
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(1),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("Insert"));
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(2),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("Second"));
}

TEST_CASE("SessionFacade service dividers add / rename / numbering", "[app][divider]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Sections")) > 0);

    Song a;
    a.title = "Alpha";
    a.lyrics = "[V1]\nOne";
    a.verse_order = "v1";
    const auto id_a = facade.song_repo()->insert(a);
    Song b;
    b.title = "Beta";
    b.lyrics = "[V1]\nTwo";
    b.verse_order = "v1";
    const auto id_b = facade.song_repo()->insert(b);
    facade.library()->refresh_songs();

    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_a)));
    REQUIRE(facade.library()->addDividerToService(QStringLiteral("Worship")));
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_b)));
    REQUIRE(facade.serviceItems()->rowCount() == 3);

    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(1),
                       seventhslide::app::ServiceItemModel::ItemTypeRole)
                .toString()
            == QStringLiteral("divider"));
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(1),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("Worship"));
    // Dividers are not numbered; songs keep 1-based ordinals.
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(0),
                       seventhslide::app::ServiceItemModel::DisplayNumberRole)
                .toInt()
            == 1);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(1),
                       seventhslide::app::ServiceItemModel::DisplayNumberRole)
                .toInt()
            == 0);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(2),
                       seventhslide::app::ServiceItemModel::DisplayNumberRole)
                .toInt()
            == 2);

    const auto div_id = facade.serviceItems()
                            ->data(facade.serviceItems()->index(1),
                                   seventhslide::app::ServiceItemModel::ItemIdRole)
                            .toLongLong();
    REQUIRE(facade.library()->renameServiceDivider(div_id, QStringLiteral("Message")).isEmpty());
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(1),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("Message"));

    // Empty title falls back to "Section".
    REQUIRE(facade.library()->addDividerToService(QString()));
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(3),
                       seventhslide::app::ServiceItemModel::TitleRole)
                .toString()
            == QStringLiteral("Section"));

    REQUIRE(facade.live()->selectServiceItem(1));
    REQUIRE(facade.live()->mode() == QStringLiteral("service"));
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(1),
                       seventhslide::app::ServiceItemModel::SelectedRole)
                .toBool());
}

TEST_CASE("SessionFacade library tabs add bible / Freeform / media", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Lib svc")) > 0);

    using seventhslide::db::MediaKind;
    using seventhslide::model::FreeformItem;
    using seventhslide::model::BibleVerse;

    FreeformItem card;
    card.name = "Welcome";
    const auto freeform_id = facade.freeform_repo()->create_item(card);

    std::vector<BibleVerse> verses{
        {.book = "John", .chapter = 3, .verse_num = 16, .text = "For God so loved"},
        {.book = "John", .chapter = 3, .verse_num = 17, .text = "For God sent not"},
    };
    const auto bible_id = facade.bible_repo()->import_bible("KJV", "", verses);

    facade.media_repo()->upsert_file(seventhslide::db::MediaKind::Image, "slide.png", "Slide");
    const auto vfolder = facade.media_repo()->folders(MediaKind::Video).create("Clips");
    (void)facade.media_repo()->add_folder_item(MediaKind::Video, vfolder, "clip.mp4");

    facade.refresh_library();
    REQUIRE(facade.freeform_repo()->list_items_in_folder(std::nullopt).size() == 1);
    REQUIRE(facade.library()->bibles()->rowCount() == 1);
    REQUIRE(facade.media()->images()->rowCount() == 1);
    REQUIRE(facade.media()->browser(MediaKind::Video)->rowCount() == 1);

    REQUIRE(facade.freeformLibrary()->addFreeformToService(static_cast<qint64>(freeform_id)));
    REQUIRE(facade.media()->addImageToService(QStringLiteral("slide.png")));
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("clip.mp4")));

    REQUIRE(facade.library()->selectBible(static_cast<qint64>(bible_id)));
    REQUIRE(facade.library()->resolveBibleRef(QStringLiteral("John 3")).value(QStringLiteral("success")).toBool());
            REQUIRE(facade.library()->bibleVerses()->rowCount() == 2);
            REQUIRE(facade.library()->addBibleToService(16, 17));

    REQUIRE(facade.serviceItems()->rowCount() == 4);
    // Adding only adds; nothing goes live.
    REQUIRE(facade.live()->service_item_index() == -1);
    REQUIRE(facade.live()->current_title().isEmpty());
}

TEST_CASE("SessionFacade resolveBibleRef + rename/delete", "[app][bible]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    using seventhslide::model::BibleVerse;
    std::vector<BibleVerse> verses{
        {.book = "John", .chapter = 3, .verse_num = 16, .text = "For God so loved"},
        {.book = "John", .chapter = 3, .verse_num = 17, .text = "For God sent not"},
        {.book = "Jude", .chapter = 1, .verse_num = 1, .text = "Jude, a servant"},
        {.book = "Jude", .chapter = 1, .verse_num = 2, .text = "Mercy unto you"},
        {.book = "Revelation", .chapter = 1, .verse_num = 1, .text = "The Revelation of Jesus Christ"},
        {.book = "Revelation", .chapter = 1, .verse_num = 11, .text = "Saying, I am Alpha"},
    };
    const auto bible_id = facade.bible_repo()->import_bible("KJV", "", verses);
    facade.refresh_library();
    REQUIRE(facade.library()->selectBible(static_cast<qint64>(bible_id)));

    auto miss = facade.library()->resolveBibleRef(QStringLiteral("Nope 1:1"));
    REQUIRE_FALSE(miss.value(QStringLiteral("success")).toBool());

    auto hit = facade.library()->resolveBibleRef(QStringLiteral("John 3:16-17"));
    REQUIRE(hit.value(QStringLiteral("success")).toBool());
    REQUIRE(hit.value(QStringLiteral("book")).toString() == QStringLiteral("John"));
    REQUIRE(hit.value(QStringLiteral("chapter")).toInt() == 3);
    REQUIRE(hit.value(QStringLiteral("verse_start")).toInt() == 16);
    REQUIRE(hit.value(QStringLiteral("verse_end")).toInt() == 17);
    REQUIRE(facade.library()->bibleVerses()->rowCount() == 2);
    REQUIRE(facade.live()->playBible(16, 17));
    REQUIRE(facade.session().focused_stack().program.bible_mode_active());

    auto chapter = facade.library()->resolveBibleRef(QStringLiteral("Revelation 1"));
    REQUIRE(chapter.value(QStringLiteral("success")).toBool());
    REQUIRE(facade.library()->bibleVerses()->rowCount() == 2);
    auto verse = facade.library()->resolveBibleRef(QStringLiteral("Revelation 1:1"));
    REQUIRE(verse.value(QStringLiteral("success")).toBool());
    REQUIRE(facade.library()->bibleVerses()->rowCount() == 1);
    auto verse11 = facade.library()->resolveBibleRef(QStringLiteral("Revelation 1:11"));
    REQUIRE(verse11.value(QStringLiteral("success")).toBool());
    REQUIRE(verse11.value(QStringLiteral("verse_start")).toInt() == 11);
    REQUIRE(facade.library()->bibleVerses()->rowCount() == 1);
    auto bad_ch = facade.library()->resolveBibleRef(QStringLiteral("Revelation 99"));
    REQUIRE_FALSE(bad_ch.value(QStringLiteral("success")).toBool());
    REQUIRE(facade.library()->bibleVerses()->rowCount() == 0);
    auto punct = facade.library()->resolveBibleRef(QStringLiteral("Revelation 1:"));
    REQUIRE(punct.value(QStringLiteral("success")).toBool());
    REQUIRE(facade.library()->bibleVerses()->rowCount() == 2);
    auto empty = facade.library()->resolveBibleRef(QStringLiteral(""));
    REQUIRE_FALSE(empty.value(QStringLiteral("success")).toBool());
    REQUIRE(facade.library()->bibleVerses()->rowCount() == 0);

    auto suggested = facade.library()->suggestBibleBooks(QStringLiteral("Reve"));
    REQUIRE(suggested.size() == 1);
    REQUIRE(suggested.front() == QStringLiteral("Revelation"));
    REQUIRE(facade.library()->suggestBibleBooks(QStringLiteral("")).isEmpty());

    // Single-chapter book: bare number is the verse.
    auto jude = facade.library()->resolveBibleRef(QStringLiteral("Jude 2"));
    REQUIRE(jude.value(QStringLiteral("success")).toBool());
    REQUIRE(jude.value(QStringLiteral("chapter")).toInt() == 1);
    REQUIRE(jude.value(QStringLiteral("verse_start")).toInt() == 2);

    REQUIRE(facade.library()->renameBible(static_cast<qint64>(bible_id), QStringLiteral("Authorized")).isEmpty());
    REQUIRE(facade.library()->bibles()->data(facade.library()->bibles()->index(0),
                                   seventhslide::app::BibleListModel::NameRole)
                .toString()
            == QStringLiteral("Authorized"));
    REQUIRE(facade.library()->deleteBible(static_cast<qint64>(bible_id)).isEmpty());
    REQUIRE(facade.library()->bibles()->rowCount() == 0);
    REQUIRE(facade.library()->selectedBibleId() < 0);
}

TEST_CASE("Only Bibles switched on are offered, and the selection follows", "[app][bible]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* library = facade.library();
    const auto amp = facade.bible_repo()->import_bible("Amplified", "", {});
    const auto kjv = facade.bible_repo()->import_bible("King James", "", {});
    library->refresh_bibles();
    REQUIRE(library->enabledBibles()->rowCount() == 2);
    REQUIRE(library->selectedBibleId() == amp);

    REQUIRE(library->setBibleAbbreviation(amp, QStringLiteral(" AMP ")).isEmpty());
    CHECK(library->selectedBibleAbbreviation() == QStringLiteral("AMP"));
    CHECK(library->bibles()->abbreviationAt(0) == QStringLiteral("AMP"));

    // Switching off the selected one moves the selection to one still on;
    // Settings still lists both.
    REQUIRE(library->setBibleEnabled(amp, false).isEmpty());
    CHECK(library->enabledBibles()->rowCount() == 1);
    CHECK(library->bibles()->rowCount() == 2);
    CHECK(library->selectedBibleId() == kjv);
    CHECK(library->selectedBibleAbbreviation().isEmpty());

    REQUIRE(library->setBibleEnabled(kjv, false).isEmpty());
    CHECK(library->enabledBibles()->rowCount() == 0);
    CHECK(library->selectedBibleId() < 0);

    REQUIRE(library->setBibleEnabled(kjv, true).isEmpty());
    CHECK(library->selectedBibleId() == kjv);

    CHECK(library->setBibleEnabled(kjv + 100, true) == refusal::bible_gone());
    CHECK(library->setBibleAbbreviation(-1, QStringLiteral("X")) == refusal::bible_gone());
}

TEST_CASE("LibraryController runs on a database and a session", "[app][library]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::layout::ApproxTextMeasurer measurer;
    seventhslide::session::Session session(measurer);
    seventhslide::app::LibraryController library(session);
    QSignalSpy live(&library, &seventhslide::app::LibraryController::liveChanged);

    SECTION("without a database it is inert rather than crashing")
    {
        REQUIRE_FALSE(library.attached());
        REQUIRE(library.songs()->rowCount() == 0);
        REQUIRE(library.createService(QStringLiteral("Sunday")) < 0);
    }

    auto db = seventhslide::db::Database::open_memory();
    seventhslide::db::ensure_schema(db);
    library.attach(db);
    REQUIRE(library.attached());

    SECTION("a song round-trips through the repository and the model")
    {
        const auto created = library.createSong(QStringLiteral("Amazing Grace"),
                                                QStringLiteral("Verse 1\nline"), QString(), {});
        const auto id = created.value(QStringLiteral("songId")).toLongLong();
        REQUIRE(id > 0);
        library.refresh_songs();
        REQUIRE(library.songs()->rowCount() == 1);
        REQUIRE(library.getSong(id).value(QStringLiteral("title")).toString()
                == QStringLiteral("Amazing Grace"));
    }

    SECTION("opening a service makes it the session's rundown, and says so once")
    {
        // Creating a service opens it; open it again from the list.
        const auto svc = library.createService(QStringLiteral("Easter"));
        REQUIRE(svc > 0);
        live.clear();
        REQUIRE(library.openService(svc));
        REQUIRE(session.service_id() == svc);
        CHECK(library.openServiceName() == QStringLiteral("Easter"));
        CHECK(live.count() == 1);
    }

    SECTION("the open service's name is announced whenever it changes, and only then")
    {
        const auto svc = library.createService(QStringLiteral("Easter"));
        REQUIRE(svc > 0);
        QSignalSpy changed(&library, &seventhslide::app::LibraryController::changed);

        // A rundown edit rereads the service: a name changed underneath (by
        // another writer of the database) reaches openServiceName's bindings.
        seventhslide::db::ServiceRepository(db).rename_service(svc, "Easter Sunday");
        REQUIRE(library.addDividerToService(QStringLiteral("Welcome")));
        CHECK(library.openServiceName() == QStringLiteral("Easter Sunday"));
        CHECK(changed.count() >= 1);

        // Renaming to the name it already has announces nothing.
        changed.clear();
        CHECK(library.renameService(svc, QStringLiteral(" Easter Sunday ")).isEmpty());
        CHECK(changed.count() == 0);
        CHECK(library.renameService(svc, QStringLiteral("Easter Day")).isEmpty());
        CHECK(library.openServiceName() == QStringLiteral("Easter Day"));
        CHECK(changed.count() == 1);
    }

    SECTION("detaching lets go of the database and clears what it showed")
    {
        (void)library.createService(QStringLiteral("Easter"));
        library.refresh_services();
        REQUIRE(library.services()->rowCount() == 1);
        library.detach();
        REQUIRE_FALSE(library.attached());
    }
}

TEST_CASE("Song credits round-trip through the song editor", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const QVariantMap meta{
        {QStringLiteral("authors"),
         QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("Ada")},
                                  {QStringLiteral("roles"),
                                   QVariantList{QVariantMap{{QStringLiteral("type"),
                                                             QStringLiteral("words")}}}}}}},
        {QStringLiteral("songbookName"), QStringLiteral(" SDA Hymnal ")},
        {QStringLiteral("songbookEntry"), QStringLiteral(" 12 ")},
        {QStringLiteral("copyright"), QStringLiteral(" Public domain ")},
        {QStringLiteral("ccliSongNumber"), QStringLiteral(" 7201044 ")},
        {QStringLiteral("key"), QStringLiteral(" G ")},
        {QStringLiteral("showCopyright"), true},
    };
    const auto created = facade.library()->createSong(
        QStringLiteral(" Credited "), QStringLiteral("---[Verse:1]---\nLine\n"),
        QStringLiteral(" v1 "), meta);
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto song_id = created.value(QStringLiteral("songId")).toLongLong();

    auto loaded = facade.library()->getSong(song_id);
    CHECK(loaded.value(QStringLiteral("songId")).toLongLong() == song_id);
    CHECK(loaded.value(QStringLiteral("title")).toString() == QStringLiteral("Credited"));
    CHECK(loaded.value(QStringLiteral("verseOrder")).toString() == QStringLiteral("v1"));
    CHECK(loaded.value(QStringLiteral("songbookName")).toString() == QStringLiteral("SDA Hymnal"));
    CHECK(loaded.value(QStringLiteral("songbookEntry")).toString() == QStringLiteral("12"));
    CHECK(loaded.value(QStringLiteral("copyright")).toString() == QStringLiteral("Public domain"));
    CHECK(loaded.value(QStringLiteral("ccliSongNumber")).toString() == QStringLiteral("7201044"));
    CHECK(loaded.value(QStringLiteral("key")).toString() == QStringLiteral("G"));
    CHECK(loaded.value(QStringLiteral("showCopyright")).toBool());
    const auto authors = loaded.value(QStringLiteral("authors")).toList();
    REQUIRE(authors.size() == 1);
    CHECK(authors.front().toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Ada"));

    // Meta is a patch: what the editor leaves out keeps its value.
    REQUIRE(facade.library()
                ->updateSong(song_id, QStringLiteral("Credited"),
                             QStringLiteral("---[Verse:1]---\nLine\n"), QStringLiteral("v1"),
                             {{QStringLiteral("key"), QStringLiteral("A")}})
                .isEmpty());
    loaded = facade.library()->getSong(song_id);
    CHECK(loaded.value(QStringLiteral("key")).toString() == QStringLiteral("A"));
    CHECK(loaded.value(QStringLiteral("songbookName")).toString() == QStringLiteral("SDA Hymnal"));
    CHECK(loaded.value(QStringLiteral("authors")).toList().size() == 1);
    CHECK(loaded.value(QStringLiteral("showCopyright")).toBool());
}

TEST_CASE("SessionFacade library song CRUD", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto created = facade.library()->createSong(
        QStringLiteral("Brand New"),
        QStringLiteral("---[Verse:1]---\nFirst line\nSecond line\n"),
        QStringLiteral("v1"));
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto song_id = created.value(QStringLiteral("songId")).toLongLong();
    REQUIRE(song_id > 0);
    REQUIRE(facade.library()->songs()->rowCount() == 1);

    const auto loaded = facade.library()->getSong(song_id);
    REQUIRE(loaded.value(QStringLiteral("title")).toString() == QStringLiteral("Brand New"));
    REQUIRE(loaded.value(QStringLiteral("lyrics")).toString().contains(QStringLiteral("First line")));

    const QString err = facade.library()->updateSong(
        song_id,
        QStringLiteral("Renamed"),
        QStringLiteral("---[Verse:1]---\nUpdated line\nAnother\n"),
        QStringLiteral("v1"));
    REQUIRE(err.isEmpty());
    REQUIRE(facade.library()->getSong(song_id).value(QStringLiteral("title")).toString()
            == QStringLiteral("Renamed"));
    REQUIRE(facade.library()->songs()
                ->data(facade.library()->songs()->index(0), seventhslide::app::SongListModel::TitleRole)
                .toString()
            == QStringLiteral("Renamed"));

    REQUIRE(facade.library()->deleteSong(song_id).isEmpty());
    REQUIRE(facade.library()->songs()->rowCount() == 0);
    REQUIRE(facade.library()->getSong(song_id).isEmpty());
}

namespace {

/// How many times ``model`` has said anything of its rows changed.
std::shared_ptr<int> count_changes(QAbstractItemModel* model)
{
    auto n = std::make_shared<int>(0);
    const auto bump = [n] { ++*n; };
    QObject::connect(model, &QAbstractItemModel::modelReset, bump);
    QObject::connect(model, &QAbstractItemModel::dataChanged, bump);
    QObject::connect(model, &QAbstractItemModel::rowsInserted, bump);
    QObject::connect(model, &QAbstractItemModel::rowsRemoved, bump);
    QObject::connect(model, &QAbstractItemModel::layoutChanged, bump);
    return n;
}

std::string first_text(SessionFacade& facade, const QString& sql)
{
    QSqlQuery q(facade.open_library().database()->handle());
    REQUIRE(q.exec(sql));
    REQUIRE(q.next());
    return q.value(0).toString().toStdString();
}

std::string song_uid(SessionFacade& facade, qint64 id)
{
    return first_text(facade, QStringLiteral("SELECT uid FROM songs WHERE id = %1").arg(id));
}

}  // namespace

TEST_CASE("A song saved here, or changed by sync, is read again alone", "[app][library][sync]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto& db = *facade.open_library().database();
    auto* library = facade.library();
    auto* songs = library->songs();
    const auto add = [&](const char* title) {
        const auto made = library->createSong(QString::fromLatin1(title),
                                              QStringLiteral("---[Verse:1]---\nA line\n"),
                                              QStringLiteral("v1"));
        REQUIRE_FALSE(made.contains(QStringLiteral("error")));
        return made.value(QStringLiteral("songId")).toLongLong();
    };
    const auto amazing = add("Amazing Grace");
    const auto vision = add("Be Thou My Vision");
    const auto crown = add("Crown Him");
    REQUIRE(songs->rowCount() == 3);
    std::vector<std::pair<int, int>> rows;
    QObject::connect(songs, &QAbstractItemModel::dataChanged,
                     [&](const QModelIndex& from, const QModelIndex& to) {
                         rows.emplace_back(from.row(), to.row());
                     });
    const auto song_changes = count_changes(songs);

    // Saved here: its row alone.
    REQUIRE(library
                ->updateSong(vision, QStringLiteral("Be Thou My Vision"),
                             QStringLiteral("---[Verse:1]---\nHigh King of Heaven\n"),
                             QStringLiteral("v1"))
                .isEmpty());
    CHECK(rows == std::vector<std::pair<int, int>>{{1, 1}});
    CHECK(*song_changes == 1);

    // Sync wrote other computers' changes on its own connection.
    db.exec("UPDATE songs SET lyrics = '---[Verse:1]---\nThe crowns upon His head' WHERE id = "
            + std::to_string(crown));
    seventhslide::sync::Changed one_song;
    one_song.add("songs", song_uid(facade, crown));
    rows.clear();
    facade.reload_after_sync(one_song);
    CHECK(rows == std::vector<std::pair<int, int>>{{2, 2}});
    CHECK(*song_changes == 2);

    // A song gone and one come: read by uid, the gone one by its absence.
    const auto amazing_uid = song_uid(facade, amazing);
    db.exec("DELETE FROM songs WHERE id = " + std::to_string(amazing));
    db.exec("INSERT INTO songs (title, lyrics) VALUES ('All Creatures', '---[Verse:1]---\nOf our God')");
    seventhslide::sync::Changed two_songs;
    two_songs.add("songs", amazing_uid);
    two_songs.add("songs", first_text(facade, QStringLiteral("SELECT uid FROM songs WHERE title = 'All Creatures'")));
    facade.reload_after_sync(two_songs);
    REQUIRE(songs->rowCount() == 3);
    CHECK(songs->data(songs->index(0), seventhslide::app::SongListModel::TitleRole).toString()
          == QStringLiteral("All Creatures"));
    songs->setFilter(QStringLiteral("crowns upon"));
    CHECK(songs->rowCount() == 1);
    songs->setFilter({});
}

TEST_CASE("Sync reads again only the lists that show what it changed", "[app][library][sync]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto& db = *facade.open_library().database();
    auto* media = facade.media();
    REQUIRE_FALSE(facade.library()
                      ->createSong(QStringLiteral("Amazing Grace"),
                                   QStringLiteral("---[Verse:1]---\nA line\n"), QStringLiteral("v1"))
                      .contains(QStringLiteral("error")));
    REQUIRE(facade.freeformLibrary()->createFreeform(QStringLiteral("Welcome")) > 0);
    // A list with nothing in it says nothing however it is read: each has a row.
    db.exec("INSERT INTO image_folders (name) VALUES ('Backgrounds')");
    db.exec("INSERT INTO audio_folders (name) VALUES ('Hymns')");
    db.exec("INSERT INTO web_folders (name) VALUES ('Sites')");
    seventhslide::sync::Changed filled;
    for (const auto* table : {"image_folders", "audio_folders", "web_folders"}) {
        filled.add_all(table);
    }
    facade.reload_after_sync(filled);
    REQUIRE(media->browser(MediaKind::Image)->rowCount() == 1);
    REQUIRE(media->browser(MediaKind::Audio)->rowCount() == 1);
    REQUIRE(facade.webLibrary()->browser()->rowCount() == 1);

    const auto songs = count_changes(facade.library()->songs());
    const auto images = count_changes(media->browser(MediaKind::Image));
    const auto videos = count_changes(media->browser(MediaKind::Video));
    const auto audio = count_changes(media->browser(MediaKind::Audio));
    const auto freeform = count_changes(facade.freeformLibrary()->browser());
    const auto web = count_changes(facade.webLibrary()->browser());

    // A setting: only the Freeform tiles, drawn in the outputs' themes
    // (once).
    seventhslide::sync::Changed setting;
    setting.add("app_settings", "ccli_licence_number");
    facade.reload_after_sync(setting);
    CHECK(*songs == 0);
    CHECK(*images == 0);
    CHECK(*videos == 0);
    CHECK(*audio == 0);
    CHECK(*freeform == 1);
    CHECK(*web == 0);

    // A setting and a Freeform item together: the tiles are still built once.
    seventhslide::sync::Changed both;
    both.add("app_settings", "ccli_licence_number");
    both.add("freeform_items", "any");
    const auto builds = facade.freeformLibrary()->browser_builds();
    facade.reload_after_sync(both);
    CHECK(facade.freeformLibrary()->browser_builds() == builds + 1);
    const int freeform_now = *freeform;

    // A video folder: the video list alone.
    db.exec("INSERT INTO video_folders (name) VALUES ('Clips')");
    seventhslide::sync::Changed folder;
    folder.add("video_folders", first_text(facade, QStringLiteral("SELECT uid FROM video_folders")));
    facade.reload_after_sync(folder);
    CHECK(media->browser(MediaKind::Video)->rowCount() == 1);
    CHECK(*videos == 1);
    CHECK(*songs == 0);
    CHECK(*images == 0);
    CHECK(*audio == 0);
    CHECK(*freeform == freeform_now);
    CHECK(*web == 0);
}

TEST_CASE("SessionFacade round-trips author roles for the editor", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    // Exactly the shape SongEditDialog hands back: {name, roles: [{type,label,lang}]}.
    const auto role = [](const QString& type, const QString& label = {},
                         const QString& lang = {}) {
        QVariantMap m;
        m.insert(QStringLiteral("type"), type);
        m.insert(QStringLiteral("label"), label);
        m.insert(QStringLiteral("lang"), lang);
        return QVariant::fromValue(m);
    };
    const auto author = [](const QString& name, const QVariantList& roles) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("roles"), roles);
        return QVariant::fromValue(m);
    };

    QVariantMap meta;
    meta.insert(QStringLiteral("authors"),
                QVariantList{
                    author(QStringLiteral("Ada"),
                           QVariantList{role(QStringLiteral("words")),
                                        role(QStringLiteral("arrangement"))}),
                    author(QStringLiteral("Bo"),
                           QVariantList{role(QStringLiteral("translation"), {},
                                             QStringLiteral("es"))}),
                    author(QStringLiteral("Cy"),
                           QVariantList{role(QStringLiteral("other"),
                                             QStringLiteral("Paraphrase"))}),
                    author(QStringLiteral("Di"), QVariantList{}),
                });

    const auto created = facade.library()->createSong(
        QStringLiteral("Credited"),
        QStringLiteral("---[Verse:1]---\nA line\n"),
        QStringLiteral("v1"), meta);
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto song_id = created.value(QStringLiteral("songId")).toLongLong();

    const auto loaded = facade.library()->getSong(song_id);
    const auto authors = loaded.value(QStringLiteral("authors")).toList();
    REQUIRE(authors.size() == 4);

    const auto roles_of = [&authors](int i) { return authors[i].toMap()
                                                  .value(QStringLiteral("roles")).toList(); };
    const auto type_at = [&roles_of](int author_i, int role_i) {
        return roles_of(author_i)[role_i].toMap().value(QStringLiteral("type")).toString();
    };
    REQUIRE(authors[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Ada"));
    REQUIRE(roles_of(0).size() == 2);
    REQUIRE(type_at(0, 0) == QStringLiteral("words"));
    REQUIRE(type_at(0, 1) == QStringLiteral("arrangement"));
    REQUIRE(type_at(1, 0) == QStringLiteral("translation"));
    REQUIRE(roles_of(1)[0].toMap().value(QStringLiteral("lang")).toString()
            == QStringLiteral("es"));
    REQUIRE(type_at(2, 0) == QStringLiteral("other"));
    REQUIRE(roles_of(2)[0].toMap().value(QStringLiteral("label")).toString()
            == QStringLiteral("Paraphrase"));
    // An author with no credits still comes back with an empty roles list, so the
    // editor never has to tell "no roles" from "no roles key".
    REQUIRE(roles_of(3).isEmpty());

    // A role the editor should not have sent is dropped rather than stored.
    QVariantMap bad;
    bad.insert(QStringLiteral("authors"),
               QVariantList{author(QStringLiteral("Ed"),
                                   QVariantList{role(QStringLiteral("")),
                                                role(QStringLiteral("other"))})});
    REQUIRE(facade.library()->updateSong(song_id, QStringLiteral("Credited"),
                                     QStringLiteral("---[Verse:1]---\nA line\n"),
                                     QStringLiteral("v1"), bad)
                .isEmpty());
    const auto after = facade.library()->getSong(song_id).value(QStringLiteral("authors")).toList();
    REQUIRE(after.size() == 1);
    REQUIRE(after[0].toMap().value(QStringLiteral("roles")).toList().isEmpty());
}

TEST_CASE("SessionFacade mid-service song edit persists snapshot only", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    Song lib;
    lib.title = "Library Title";
    lib.lyrics = "[V1]\nOriginal line\nSecond line";
    lib.verse_order = "v1";
    const auto song_id = facade.song_repo()->insert(lib);

    REQUIRE(facade.library()->createService(QStringLiteral("Edit svc")) > 0);
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(song_id)));
    REQUIRE(facade.live()->selectServiceItem(0));
    REQUIRE(facade.live()->current_title() == QStringLiteral("Library Title"));
    const int lines_before = facade.program()->totalLines();
    REQUIRE(lines_before > 0);

    const auto item_id = facade.serviceItems()
                             ->data(facade.serviceItems()->index(0),
                                    seventhslide::app::ServiceItemModel::ItemIdRole)
                             .toLongLong();
    REQUIRE(item_id > 0);

    const auto loaded = facade.library()->getServiceSongItem(item_id);
    REQUIRE(loaded.value(QStringLiteral("title")).toString() == QStringLiteral("Library Title"));
    REQUIRE(loaded.value(QStringLiteral("canReset")).toBool());

    const QString err = facade.library()->updateServiceSongItem(
        item_id,
        QStringLiteral("Service Title"),
        QStringLiteral(
            "---[Verse:1]---\nChanged line\nAnother line\n\n---[Verse:2]---\nBridge line\n"),
        QStringLiteral("v1 v2"));
    REQUIRE(err.isEmpty());

    REQUIRE(facade.live()->current_title() == QStringLiteral("Service Title"));
    REQUIRE(facade.program()->totalLines() > lines_before);
    REQUIRE(facade.serviceItems()
                ->data(facade.serviceItems()->index(0),
                       seventhslide::app::ServiceItemModel::HasOverridesRole)
                .toBool());

    // Library row unchanged.
    const auto still = facade.song_repo()->get(song_id);
    REQUIRE(still);
    REQUIRE(still->title == "Library Title");
    REQUIRE(still->lyrics.find("Original line") != std::string::npos);
    REQUIRE(still->lyrics.find("Changed line") == std::string::npos);

    // Snapshot in DB has the edit.
    const auto item = facade.service_repo()->get_item(static_cast<std::int64_t>(item_id));
    REQUIRE(item);
    REQUIRE(item->data.at("title") == "Service Title");
    REQUIRE(item->data.at("user_modified") == true);
}

TEST_CASE("SessionFacade song edit rejects bad verse order", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    Song lib;
    lib.title = "T";
    lib.lyrics = "[V1]\nOne";
    lib.verse_order = "v1";
    const auto song_id = facade.song_repo()->insert(lib);
    REQUIRE(facade.library()->createService(QStringLiteral("Bad")) > 0);
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(song_id)));
    const auto item_id = facade.serviceItems()
                             ->data(facade.serviceItems()->index(0),
                                    seventhslide::app::ServiceItemModel::ItemIdRole)
                             .toLongLong();

    const QString err = facade.library()->updateServiceSongItem(
        item_id,
        QStringLiteral("T"),
        QStringLiteral("---[Verse:1]---\nOne\n"),
        QStringLiteral("v9"));
    REQUIRE_FALSE(err.isEmpty());
}

TEST_CASE("SessionFacade library song themeMap round-trip", "[app][theme_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    QVariantMap main;
    main.insert(QStringLiteral("theme"), QStringLiteral("tid-1"));
    main.insert(QStringLiteral("bg"), QStringLiteral(""));  // omitted on compact
    QVariantMap lower;
    lower.insert(QStringLiteral("bg"), QStringLiteral("bid-2"));
    QVariantMap theme_map;
    theme_map.insert(QStringLiteral("Main"), in_category(QStringLiteral("song"), main));
    theme_map.insert(QStringLiteral("Lower"), in_category(QStringLiteral("song"), lower));
    theme_map.insert(QStringLiteral("Empty"), QVariantMap{});  // dropped

    QVariantMap meta;
    meta.insert(QStringLiteral("themeMap"), song_theme_map_with_chip(theme_map));

    const auto created = facade.library()->createSong(
        QStringLiteral("Hope"),
        QStringLiteral("---[Verse:1]---\nLine one\nLine two\n"),
        QStringLiteral("v1"),
        meta);
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto song_id = created.value(QStringLiteral("songId")).toLongLong();

    const auto loaded = facade.library()->getSong(song_id);
    const auto got = first_chip(loaded.value(QStringLiteral("themeMap")));
    REQUIRE(got.contains(QStringLiteral("Main")));
    REQUIRE(got.value(QStringLiteral("Main")).toMap().value(QStringLiteral("song")).toMap().value(QStringLiteral("theme")).toString()
            == QStringLiteral("tid-1"));
    REQUIRE_FALSE(got.value(QStringLiteral("Main")).toMap().value(QStringLiteral("song")).toMap().contains(QStringLiteral("bg")));
    REQUIRE(got.value(QStringLiteral("Lower")).toMap().value(QStringLiteral("song")).toMap().value(QStringLiteral("bg")).toString()
            == QStringLiteral("bid-2"));
    REQUIRE_FALSE(got.contains(QStringLiteral("Empty")));

    // Update without themeMap leaves prior overrides.
    REQUIRE(facade.library()
                ->updateSong(song_id,
                                   QStringLiteral("Hope"),
                                   QStringLiteral("---[Verse:1]---\nLine one\nLine two\n"),
                                   QStringLiteral("v1"))
                .isEmpty());
    REQUIRE(first_chip(facade.library()->getSong(song_id).value(QStringLiteral("themeMap")))
                .value(QStringLiteral("Main"))
                .toMap()
                .value(QStringLiteral("song")).toMap()
                .value(QStringLiteral("theme"))
                .toString()
            == QStringLiteral("tid-1"));

    // Explicit empty themeMap clears overrides.
    QVariantMap clear_meta;
    clear_meta.insert(QStringLiteral("themeMap"), QVariantMap{});
    REQUIRE(facade.library()
                ->updateSong(song_id,
                                   QStringLiteral("Hope"),
                                   QStringLiteral("---[Verse:1]---\nLine one\nLine two\n"),
                                   QStringLiteral("v1"),
                                   clear_meta)
                .isEmpty());
    REQUIRE(facade.library()->getSong(song_id).value(QStringLiteral("themeMap")).toMap().isEmpty());
}

TEST_CASE("SessionFacade library song theme pack round-trip", "[app][theme_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    QVariantMap chip;
    chip.insert(QStringLiteral("theme"), QStringLiteral("tid-pack"));
    QVariantMap main;
    main.insert(QStringLiteral("Main"), in_category(QStringLiteral("song"), chip));
    QVariantMap chips;
    chips.insert(QStringLiteral("TH1"), main);
    QVariantMap arr;
    arr.insert(QStringLiteral("id"), QStringLiteral("a1"));
    arr.insert(QStringLiteral("name"), QStringLiteral("Sunday"));
    arr.insert(QStringLiteral("order"), QStringLiteral("TH1 V1"));
    arr.insert(QStringLiteral("chips"), chips);
    QVariantList arrs;
    arrs.push_back(arr);
    QVariantMap theme_map;
    theme_map.insert(QStringLiteral("active"), QStringLiteral("a1"));
    theme_map.insert(QStringLiteral("arrangements"), arrs);

    QVariantMap meta;
    meta.insert(QStringLiteral("themeMap"), theme_map);

    const auto created = facade.library()->createSong(
        QStringLiteral("Packed"),
        QStringLiteral("---[Verse:1]---\nLine one\nLine two\n"),
        QStringLiteral("TH1 V1"),
        meta);
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto song_id = created.value(QStringLiteral("songId")).toLongLong();

    const auto got = facade.library()->getSong(song_id).value(QStringLiteral("themeMap")).toMap();
    REQUIRE(got.value(QStringLiteral("active")).toString() == QStringLiteral("a1"));
    const auto loaded_arrs = got.value(QStringLiteral("arrangements")).toList();
    REQUIRE(loaded_arrs.size() == 1);
    const auto loaded_chips = loaded_arrs.front().toMap().value(QStringLiteral("chips")).toMap();
    REQUIRE(loaded_chips.value(QStringLiteral("TH1")).toMap()
                .value(QStringLiteral("Main")).toMap()
                .value(QStringLiteral("song")).toMap()
                .value(QStringLiteral("theme")).toString()
            == QStringLiteral("tid-pack"));
}

// Above is stored as itself through every save a song's themes go through:
// the library's, and the service's copy of the song.
TEST_CASE("SessionFacade keeps a theme change's Above through library and service saves",
          "[app][theme_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const QString above = seventhslide::app::SongText::inheritAboveId();
    QVariantMap main;
    main.insert(QStringLiteral("theme"), QStringLiteral("tid-1"));
    main.insert(QStringLiteral("bg"), above);
    QVariantMap theme_map;
    theme_map.insert(QStringLiteral("Main"), in_category(QStringLiteral("song"), main));
    const auto bg_of = [](const QVariant& stored) {
        return first_chip(stored)
            .value(QStringLiteral("Main"))
            .toMap()
            .value(QStringLiteral("song"))
            .toMap()
            .value(QStringLiteral("bg"))
            .toString();
    };

    QVariantMap meta;
    meta.insert(QStringLiteral("themeMap"), song_theme_map_with_chip(theme_map));
    const auto created = facade.library()->createSong(
        QStringLiteral("Hope"), QStringLiteral("---[Verse:1]---\nLine one\n"),
        QStringLiteral("TH1 v1"), meta);
    REQUIRE_FALSE(created.contains(QStringLiteral("error")));
    const auto song_id = created.value(QStringLiteral("songId")).toLongLong();
    CHECK(bg_of(facade.library()->getSong(song_id).value(QStringLiteral("themeMap"))) == above);

    REQUIRE(facade.library()->createService(QStringLiteral("Theme svc")) > 0);
    REQUIRE(facade.library()->addSongToService(song_id));
    const auto item_id = facade.serviceItems()
                             ->data(facade.serviceItems()->index(0),
                                    seventhslide::app::ServiceItemModel::ItemIdRole)
                             .toLongLong();
    CHECK(bg_of(facade.library()->getServiceSongItem(item_id).value(QStringLiteral("themeMap")))
          == above);
    REQUIRE(facade.library()
                ->updateServiceSongItem(item_id, QStringLiteral("Hope"),
                                        QStringLiteral("---[Verse:1]---\nLine one\n"),
                                        QStringLiteral("TH1 v1"),
                                        song_theme_map_with_chip(theme_map))
                .isEmpty());
    CHECK(bg_of(facade.library()->getServiceSongItem(item_id).value(QStringLiteral("themeMap")))
          == above);
}

TEST_CASE("SessionFacade service song themeMap round-trip", "[app][theme_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    Song lib;
    lib.title = "Library Theme";
    lib.lyrics = "[V1]\nOne\nTwo";
    lib.verse_order = "v1";
    lib.theme_map = song_theme_json_with_chip({{"Main", {{"song", {{"theme", "lib-text"}}}}}});
    const auto song_id = facade.song_repo()->insert(lib);

    REQUIRE(facade.library()->createService(QStringLiteral("Theme svc")) > 0);
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(song_id)));
    const auto item_id = facade.serviceItems()
                             ->data(facade.serviceItems()->index(0),
                                    seventhslide::app::ServiceItemModel::ItemIdRole)
                             .toLongLong();

    const auto loaded = facade.library()->getServiceSongItem(item_id);
    REQUIRE(first_chip(loaded.value(QStringLiteral("themeMap")))
                .value(QStringLiteral("Main"))
                .toMap()
                .value(QStringLiteral("song")).toMap()
                .value(QStringLiteral("theme"))
                .toString()
            == QStringLiteral("lib-text"));

    QVariantMap main;
    main.insert(QStringLiteral("theme"), QStringLiteral("svc-text"));
    main.insert(QStringLiteral("bg"), QStringLiteral("svc-bg"));
    QVariantMap theme_map;
    theme_map.insert(QStringLiteral("Main"), in_category(QStringLiteral("song"), main));

    REQUIRE(facade.library()
                ->updateServiceSongItem(item_id,
                                       QStringLiteral("Library Theme"),
                                       QStringLiteral("---[Verse:1]---\nOne\nTwo\n"),
                                       QStringLiteral("v1"),
                                       song_theme_map_with_chip(theme_map))
                .isEmpty());

    const auto after = facade.library()->getServiceSongItem(item_id);
    REQUIRE(first_chip(after.value(QStringLiteral("themeMap")))
                .value(QStringLiteral("Main"))
                .toMap()
                .value(QStringLiteral("song")).toMap()
                .value(QStringLiteral("theme"))
                .toString()
            == QStringLiteral("svc-text"));
    REQUIRE(first_chip(after.value(QStringLiteral("themeMap")))
                .value(QStringLiteral("Main"))
                .toMap()
                .value(QStringLiteral("song")).toMap()
                .value(QStringLiteral("bg"))
                .toString()
            == QStringLiteral("svc-bg"));

    // Omitted themeMap preserves snapshot overrides.
    REQUIRE(facade.library()
                ->updateServiceSongItem(item_id,
                                       QStringLiteral("Renamed"),
                                       QStringLiteral("---[Verse:1]---\nOne\nTwo\n"),
                                       QStringLiteral("v1"))
                .isEmpty());
    REQUIRE(first_chip(facade.library()->getServiceSongItem(item_id).value(QStringLiteral("themeMap")))
                .value(QStringLiteral("Main"))
                .toMap()
                .value(QStringLiteral("song")).toMap()
                .value(QStringLiteral("bg"))
                .toString()
            == QStringLiteral("svc-bg"));

    // Library row unchanged.
    const auto still = facade.song_repo()->get(song_id);
    REQUIRE(still);
    REQUIRE(still->theme_map["arrangements"][0]["chips"]["TH1"]["Main"]["song"]["theme"] == "lib-text");
    REQUIRE_FALSE(still->theme_map["arrangements"][0]["chips"]["TH1"]["Main"]["song"].contains("bg"));
}

TEST_CASE("SessionFacade service-level themeMap round-trip", "[app][service_settings][theme_map]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto service_id = facade.library()->createService(QStringLiteral("Sunday"));
    REQUIRE(service_id > 0);
    REQUIRE(facade.library()->getServiceThemeMap(service_id).isEmpty());

    QVariantMap main;
    main.insert(QStringLiteral("theme"), QStringLiteral("svc-text"));
    main.insert(QStringLiteral("bg"), QStringLiteral(""));  // omitted on compact
    QVariantMap lower;
    lower.insert(QStringLiteral("bg"), QStringLiteral("svc-bg"));
    QVariantMap theme_map;
    theme_map.insert(QStringLiteral("Main"), in_category(QStringLiteral("song"), main));
    theme_map.insert(QStringLiteral("Lower"), in_category(QStringLiteral("song"), lower));
    theme_map.insert(QStringLiteral("Empty"), QVariantMap{});  // dropped

    REQUIRE(facade.library()->setServiceThemeMap(service_id, theme_map).isEmpty());

    const auto loaded = facade.library()->getServiceThemeMap(service_id);
    REQUIRE(loaded.value(QStringLiteral("Main"))
                .toMap()
                .value(QStringLiteral("song")).toMap()
                .value(QStringLiteral("theme"))
                .toString()
            == QStringLiteral("svc-text"));
    REQUIRE_FALSE(loaded.value(QStringLiteral("Main")).toMap().value(QStringLiteral("song")).toMap().contains(QStringLiteral("bg")));
    REQUIRE(loaded.value(QStringLiteral("Lower"))
                .toMap()
                .value(QStringLiteral("song")).toMap()
                .value(QStringLiteral("bg"))
                .toString()
            == QStringLiteral("svc-bg"));
    REQUIRE_FALSE(loaded.contains(QStringLiteral("Empty")));

    REQUIRE(facade.library()->setServiceThemeMap(service_id, QVariantMap{}).isEmpty());
    REQUIRE(facade.library()->getServiceThemeMap(service_id).isEmpty());

    REQUIRE(facade.library()->setServiceThemeMap(-1, theme_map) == refusal::service_gone());
    REQUIRE(facade.library()->setServiceThemeMap(99999, theme_map) == refusal::service_gone());
}

TEST_CASE("SessionFacade round-trips the title-slide switch", "[app][title]")
{
    // The Titles context's Slide tab writes ``show_title_slides`` into the theme
    // style and saves through updateTheme. It is a text-theme key, so it has to
    // survive the style filter that drops geometry and background keys - if it
    // ever falls out of that set the chip silently stops sticking.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const auto created = facade.themes()->create(
        QStringLiteral("Main"), QStringLiteral("song"), QStringLiteral("Titles"));
    const auto tid = created.value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(tid.isEmpty());

    // A new theme starts with title slides on.
    QVariantMap style = created.value(QStringLiteral("style")).toMap();
    REQUIRE(style.value(QStringLiteral("show_title_slides"), true).toBool());

    style.insert(QStringLiteral("show_title_slides"), false);
    REQUIRE(facade
                .themes()->update_theme(QStringLiteral("Main"), QStringLiteral("song"), tid,
                             QStringLiteral("Titles"), seventhslide::app::to_json(QVariant(style)), QString())
                .isEmpty());

    const auto off = facade.themes()->theme_map(QStringLiteral("Main"), QStringLiteral("song"), tid);
    REQUIRE(off.value(QStringLiteral("style")).toMap()
                .value(QStringLiteral("show_title_slides"))
                .toBool()
            == false);

    style.insert(QStringLiteral("show_title_slides"), true);
    REQUIRE(facade
                .themes()->update_theme(QStringLiteral("Main"), QStringLiteral("song"), tid,
                             QStringLiteral("Titles"), seventhslide::app::to_json(QVariant(style)), QString())
                .isEmpty());
    const auto on = facade.themes()->theme_map(QStringLiteral("Main"), QStringLiteral("song"), tid);
    REQUIRE(on.value(QStringLiteral("style")).toMap()
                .value(QStringLiteral("show_title_slides"))
                .toBool());
}

TEST_CASE("importSongs takes every song format and says what it could not", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp_dir;
    const auto unreadable = tmp_dir.path / "Sunday.pro";
    {
        std::ofstream out(unreadable, std::ios::binary);
        out.write("\x0a\x05hi\0\x12", 6);
    }
    const auto dir = std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "songs" / "import";
    const auto result = facade.library()->importSongs({
        QString::fromStdString((dir / "environments.cho").string()),
        QString::fromStdString((dir / "songselect_2023.txt").string()),
        QString::fromStdString((dir / "holy_holy.usr").string()),
        QString::fromStdString(unreadable.string()),
    });
    CHECK(result.value(QStringLiteral("count")).toInt() == 3);
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK(result.value(QStringLiteral("errors")).toStringList()
          == QStringList{QStringLiteral("Sunday.pro (ProPresenter 7): Format not supported")});
    CHECK(result.value(QStringLiteral("warnings")).toStringList()
          == QStringList{QStringLiteral("Swing Low Sweet Chariot: Dropped tabs and chord grids")});
    CHECK(facade.library()->songs()->rowCount() == 3);
}

TEST_CASE("readSongFiles holds songs until importReadSongs saves those asked for", "[app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp_dir;
    const auto unreadable = tmp_dir.path / "Sunday.pro";
    {
        std::ofstream out(unreadable, std::ios::binary);
        out.write("\x0a\x05hi\0\x12", 6);
    }
    const auto dir = std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "songs" / "import";
    const QStringList paths{
        QString::fromStdString((dir / "environments.cho").string()),
        QString::fromStdString((dir / "songselect_2023.txt").string()),
        QString::fromStdString(unreadable.string()),
        QString::fromStdString((dir / "holy_holy.usr").string()),
    };
    auto* library = facade.library();
    const auto rows = library->readSongFiles(paths);
    REQUIRE(rows.size() == 4);
    // Nothing is saved yet.
    CHECK(library->songs()->rowCount() == 0);

    // The file that gave no song first, then the songs by title.
    const auto bad = rows[0].toMap();
    CHECK(bad.value(QStringLiteral("title")).toString() == QStringLiteral("Sunday.pro"));
    CHECK(bad.value(QStringLiteral("format")).toString() == QStringLiteral("ProPresenter 7"));
    CHECK(bad.value(QStringLiteral("note")).toString() == QStringLiteral("Format not supported"));
    CHECK_FALSE(bad.value(QStringLiteral("importable")).toBool());
    const auto holy = rows[1].toMap();
    CHECK(holy.value(QStringLiteral("title")).toString() == QStringLiteral("Holy, Holy, Holy"));
    CHECK(holy.value(QStringLiteral("format")).toString() == QStringLiteral("SongSelect file"));
    const auto well = rows[2].toMap();
    CHECK(well.value(QStringLiteral("title")).toString() == QStringLiteral("It Is Well With My Soul"));
    const auto swing = rows[3].toMap();
    CHECK(swing.value(QStringLiteral("title")).toString() == QStringLiteral("Swing Low Sweet Chariot"));
    CHECK(swing.value(QStringLiteral("format")).toString() == QStringLiteral("ChordPro"));
    CHECK(swing.value(QStringLiteral("note")).toString()
          == QStringLiteral("Dropped tabs and chord grids"));

    // Only the rows asked for are saved, once.
    const QVariantList chosen{holy.value(QStringLiteral("row")), swing.value(QStringLiteral("row"))};
    const auto result = library->importReadSongs(chosen);
    CHECK(result.value(QStringLiteral("count")).toInt() == 2);
    CHECK(result.value(QStringLiteral("ok")).toBool());
    CHECK(library->songs()->rowCount() == 2);
    CHECK(library->importReadSongs(chosen).value(QStringLiteral("count")).toInt() == 0);
    CHECK(library->songs()->rowCount() == 2);

    // Let go without saving.
    (void)library->readSongFiles(paths);
    library->discardReadSongs();
    CHECK(library->importReadSongs({QVariant(0)}).value(QStringLiteral("count")).toInt() == 0);
    CHECK(library->songs()->rowCount() == 2);
}

TEST_CASE("SessionFacade imports songs bible images videos", "[app]")
{
    seventhslide::test::ensure_qt_application();
    // Before the facade, so it goes after it: the facade holds the library's
    // fonts open, and Windows removes no file that is open.
    const seventhslide::test::TempDir tmp_dir;
    const auto& tmp = tmp_dir.path;
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    std::filesystem::create_directories(tmp / "images");
    std::filesystem::create_directories(tmp / "videos");
    facade.set_media_root(tmp);

    const auto song_xml =
        std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "songs" / "openlyrics_basic.xml";
    const auto song_result =
        facade.library()->importSongs({QString::fromStdString(song_xml.string())});
    REQUIRE(song_result.value(QStringLiteral("count")).toInt() >= 1);
    REQUIRE(facade.library()->songs()->rowCount() >= 1);

    const auto bible_xml =
        std::filesystem::path{SEVENTHSLIDE_FIXTURES_DIR} / "bibles" / "zefania_sample.xml";
    const auto bible_result =
        facade.library()->importBible(QString::fromStdString(bible_xml.string()));
    REQUIRE_FALSE(bible_result.contains(QStringLiteral("error")));
    REQUIRE(bible_result.value(QStringLiteral("id")).toLongLong() > 0);
    REQUIRE(facade.library()->bibles()->rowCount() >= 1);

    const auto img = tmp / "src.png";
    {
        std::ofstream out(img, std::ios::binary);
        out << "PNG";
    }
    const auto img_result = seventhslide::test::wait_for_import(
        *facade.media(),
        facade.media()->import_files(MediaKind::Image, {QString::fromStdString(img.string())}, -1));
    REQUIRE(img_result.value(QStringLiteral("count")).toInt() == 1);
    REQUIRE(img_result.value(QStringLiteral("filenames")).toStringList().size() == 1);
    REQUIRE_FALSE(img_result.value(QStringLiteral("filenames")).toStringList().front().isEmpty());
    REQUIRE(facade.media()->images()->rowCount() >= 1);
    REQUIRE(std::filesystem::is_directory(tmp / "images"));

    const auto vid = tmp / "clip.mp4";
    {
        std::ofstream out(vid, std::ios::binary);
        out << "mp4";
    }
    const auto vid_result = seventhslide::test::wait_for_import(
        *facade.media(),
        facade.media()->import_files(MediaKind::Video, {QString::fromStdString(vid.string())}, -1));
    REQUIRE(vid_result.value(QStringLiteral("count")).toInt() == 1);
    REQUIRE(facade.media()->browser(MediaKind::Video)->rowCount() >= 1);
    const auto clip = vid_result.value(QStringLiteral("filenames")).toStringList().front();
    REQUIRE(std::filesystem::exists(tmp / "videos" / clip.toStdString()));

    const auto img_filename =
        facade.media()->images()
            ->data(facade.media()->images()->index(0), seventhslide::app::ImageListModel::FilenameRole)
            .toString();
    REQUIRE_FALSE(img_filename.isEmpty());
    REQUIRE(facade.media()->library(MediaKindQml::Image)->deleteItem(img_filename).isEmpty());
    REQUIRE(facade.media()->images()->rowCount() == 0);
    REQUIRE_FALSE(std::filesystem::exists(tmp / "images" / img_filename.toStdString()));

    REQUIRE(facade.media()->library(MediaKindQml::Video)->deleteItem(clip).isEmpty());
    REQUIRE(facade.media()->browser(MediaKind::Video)->rowCount() == 0);
    REQUIRE_FALSE(std::filesystem::exists(tmp / "videos" / clip.toStdString()));
    REQUIRE_FALSE(facade.media_repo()->get_file(MediaKind::Video, clip.toStdString()));

    // Refuse deleting a video still referenced by a service.
    {
        std::ofstream out(tmp / "keep.mp4", std::ios::binary);
        out << "mp4";
    }
    const auto keep = seventhslide::test::wait_for_import(
                          *facade.media(),
                          facade.media()->import_files(
                              MediaKind::Video,
                              {QString::fromStdString((tmp / "keep.mp4").string())}, -1))
                          .value(QStringLiteral("filenames"))
                          .toStringList()
                          .front();
    const auto sid = facade.library()->createService(QStringLiteral("Media svc"));
    REQUIRE(sid >= 0);
    REQUIRE(facade.library()->openService(sid));
    REQUIRE(facade.media()->addVideoToService(keep));
    const auto refused = facade.media()->library(MediaKindQml::Video)->deleteItem(keep);
    REQUIRE_FALSE(refused.isEmpty());
    REQUIRE(std::filesystem::exists(tmp / "videos" / keep.toStdString()));
    REQUIRE(facade.media_repo()->get_file(MediaKind::Video, keep.toStdString()));
}

TEST_CASE("SessionFacade renaming a video retitles it and leaves its file alone",
          "[app][services][media]")
{
    // A rename used to rename the file and rewrite every row that played it.
    // The file's name never changes now, so nothing that plays it can break.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());

    const seventhslide::test::TempDir tmp_dir;
    const auto& tmp = tmp_dir.path;
    std::filesystem::create_directories(tmp / "videos");
    facade.set_media_root(tmp);
    std::ofstream(tmp / "videos" / "a1b2.mp4") << "v";
    facade.media_repo()->upsert_file(MediaKind::Video, "a1b2.mp4", "clip.mp4");

    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("a1b2.mp4")));
    REQUIRE(facade.serviceItems()->rowCount() == 1);
    // The row is titled as the library titles the file, not by its name.
    CHECK(facade.session().service_items().front().title() == "clip.mp4");

    const auto renamed = facade.media()->renameMediaFile(MediaKindQml::Video,
                                                         QStringLiteral("a1b2.mp4"),
                                                         QStringLiteral("outro"));
    REQUIRE_FALSE(renamed.contains(QStringLiteral("error")));
    CHECK(renamed.value(QStringLiteral("filename")).toString() == QStringLiteral("a1b2.mp4"));
    CHECK(facade.media()->title_of(MediaKind::Video, "a1b2.mp4") == "outro.mp4");
    CHECK(std::filesystem::exists(tmp / "videos" / "a1b2.mp4"));

    // The row still plays the same file, under the title it was added with.
    const auto& items = facade.session().service_items();
    REQUIRE(items.size() == 1);
    CHECK(items.front().data.value("filename", "") == "a1b2.mp4");
    CHECK(items.front().title() == "clip.mp4");
}

TEST_CASE("SessionFacade titles media by the library's titles, not the file names",
          "[app][services][media]")
{
    // Files are stored under opaque names; what the operator reads, in the
    // rundown and on the live controls, is the title the library gives them.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const seventhslide::test::TempDir tmp_dir;
    facade.set_media_root(tmp_dir.path);
    std::filesystem::create_directories(tmp_dir.path / "videos");
    std::ofstream(tmp_dir.path / "videos" / "77c1.mp4") << "v";
    auto* repo = facade.media_repo();
    repo->upsert_file(MediaKind::Image, "3f2a.png", "Sunrise.png");
    repo->upsert_file(MediaKind::Video, "77c1.mp4", "Countdown.mp4");
    repo->upsert_file(MediaKind::Audio, "9b0e.mp3", "Prelude.mp3");

    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
    REQUIRE(facade.media()->addImageToService(QStringLiteral("3f2a.png")));
    REQUIRE(facade.media()->addVideoToService(QStringLiteral("77c1.mp4")));
    REQUIRE(facade.media()->addAudioToService(QStringLiteral("9b0e.mp3")));
    const auto& items = facade.session().service_items();
    REQUIRE(items.size() == 3);
    CHECK(items[0].title() == "Sunrise.png");
    CHECK(items[1].title() == "Countdown.mp4");
    CHECK(items[2].title() == "Prelude.mp3");

    REQUIRE(facade.live()->playVideo(QStringLiteral("77c1.mp4")));
    CHECK(facade.session().focused_stack().program.playback().title == "Countdown.mp4");
    CHECK(facade.session().focused_stack().program.playback().filename == "77c1.mp4");
}

TEST_CASE("SessionFacade rejects an also-play list without touching the rundown", "[app][services][alsoplay]")
{
    // Validation used to run by mutating the live rundown item and undoing it
    // afterwards, so a rejected companion (or a failed write) could leave the
    // wrong companions on the row.
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);

    seventhslide::model::Song a;
    a.title = "First";
    a.lyrics = "v1\nOne";
    seventhslide::model::Song b;
    b.title = "Second";
    b.lyrics = "v1\nTwo";
    const auto id_a = facade.song_repo()->insert(a);
    const auto id_b = facade.song_repo()->insert(b);
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_a)));
    REQUIRE(facade.library()->addSongToService(static_cast<qint64>(id_b)));

    auto* model = facade.serviceItems();
    REQUIRE(model->rowCount() == 2);
    const auto first_id =
        model->data(model->index(0), seventhslide::app::ServiceItemModel::ItemIdRole).toLongLong();
    const auto second_id =
        model->data(model->index(1), seventhslide::app::ServiceItemModel::ItemIdRole).toLongLong();

    // Two songs resolve to the same stack, so this pairing is not allowed.
    const auto err = facade.library()->set_service_item_also_play(first_id, QVariantList{second_id});
    REQUIRE_FALSE(err.isEmpty());

    const auto& items = facade.session().service_items();
    REQUIRE(seventhslide::model::service_item_also_play(items.front()).empty());
    REQUIRE(model->data(model->index(0), seventhslide::app::ServiceItemModel::AlsoPlayIdsRole)
                .toList()
                .isEmpty());
}

TEST_CASE("SessionFacade links rows into one set and unlinks any one of them", "[app][services][alsoplay]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    REQUIRE(facade.library()->createService(QStringLiteral("Sunday")) > 0);
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lobby"), QStringLiteral("info")).isEmpty());
    REQUIRE(facade.stacks()->addStack(QStringLiteral("Lower"), QStringLiteral("text")).isEmpty());
    std::vector<QString> stack_ids;
    for (const auto& row : facade.stacks()->stacks()) {
        stack_ids.push_back(row.toMap().value(QStringLiteral("id")).toString());
    }
    REQUIRE(stack_ids.size() == 3);

    std::vector<qint64> ids;
    for (int i = 0; i < 3; ++i) {
        seventhslide::model::Song song;
        song.title = "Song " + std::to_string(i);
        song.lyrics = "v1\nLine";
        const auto song_id = facade.song_repo()->insert(song);
        REQUIRE(facade.library()->addSongToService(static_cast<qint64>(song_id)));
    }
    auto* model = facade.serviceItems();
    REQUIRE(model->rowCount() == 3);
    for (int row = 0; row < 3; ++row) {
        ids.push_back(model->data(model->index(row), seventhslide::app::ServiceItemModel::ItemIdRole)
                          .toLongLong());
        REQUIRE(facade.library()->set_service_item_stack(ids.back(), stack_ids[static_cast<std::size_t>(row)])
                    .isEmpty());
    }
    const auto& session = facade.session();
    const auto set_of = [&](qint64 id) { return session.also_play_set(id); };
    const std::vector<std::int64_t> all{ids[0], ids[1], ids[2]};

    // Linking a row to one that is already linked joins the existing set.
    REQUIRE(facade.library()->linkServiceItems(QVariantList{ids[1], ids[2]}).isEmpty());
    REQUIRE(facade.library()->linkServiceItems(QVariantList{ids[0], ids[1]}).isEmpty());
    REQUIRE(set_of(ids[0]) == all);
    REQUIRE(set_of(ids[2]) == all);

    // Unlinking the row that holds the list leaves the other two together.
    REQUIRE(facade.library()->unlinkServiceItem(ids[0]).isEmpty());
    REQUIRE(set_of(ids[0]).empty());
    REQUIRE(set_of(ids[1]) == std::vector<std::int64_t>{ids[1], ids[2]});

    // Unlinking either row of a pair takes the pair apart.
    REQUIRE(facade.library()->unlinkServiceItem(ids[2]).isEmpty());
    REQUIRE(set_of(ids[1]).empty());
    REQUIRE(set_of(ids[2]).empty());
    for (const auto& item : session.service_items()) {
        REQUIRE(seventhslide::model::service_item_also_play(item).empty());
        const auto stored = facade.service_repo()->get_item(*item.item_id);
        REQUIRE(stored);
        REQUIRE(seventhslide::model::service_item_also_play(*stored).empty());
    }
}

TEST_CASE("SessionFacade services: rename, tags, date and delete", "[app][services]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* library = facade.library();
    auto* list = library->services();
    const auto row_of = [list](qint64 id) {
        for (int row = 0; row < list->rowCount(); ++row) {
            if (list->data(list->index(row), seventhslide::app::ServiceListModel::ServiceIdRole)
                    .toLongLong()
                == id) {
                return row;
            }
        }
        return -1;
    };
    const auto role = [&](qint64 id, int r) { return list->data(list->index(row_of(id)), r); };

    const auto sid = library->createService(QStringLiteral("Worship"));
    REQUIRE(sid > 0);
    REQUIRE(facade.openServiceId() == sid);
    CHECK(role(sid, seventhslide::app::ServiceListModel::ServiceAtRole).toString()
          == role(sid, seventhslide::app::ServiceListModel::CreatedAtRole).toString());

    REQUIRE(library->renameService(sid, QStringLiteral("Worship AM")).isEmpty());
    CHECK(role(sid, seventhslide::app::ServiceListModel::NameRole).toString()
          == QStringLiteral("Worship AM"));

    REQUIRE(library->setServiceTags(sid, {QStringLiteral("Communion"), QStringLiteral(" communion "),
                                          QStringLiteral("Youth")})
                .isEmpty());
    CHECK(role(sid, seventhslide::app::ServiceListModel::TagsRole).toStringList()
          == QStringList({QStringLiteral("Communion"), QStringLiteral("Youth")}));
    CHECK(library->setServiceTags(-1, {}) == refusal::service_gone());

    REQUIRE(library->setServiceDateTime(sid, QStringLiteral("2026-08-17T09:30:00")).isEmpty());
    CHECK(role(sid, seventhslide::app::ServiceListModel::ServiceAtRole).toString()
          == QStringLiteral("2026-08-17T09:30:00"));
    REQUIRE_FALSE(library->setServiceDateTime(sid, QStringLiteral("nope")).isEmpty());
    REQUIRE(library->setServiceDateTime(sid, QString()).isEmpty());
    CHECK(role(sid, seventhslide::app::ServiceListModel::ServiceAtRole).toString().isEmpty());

    REQUIRE(library->deleteService(sid).isEmpty());
    CHECK(facade.openServiceId() < 0);
    CHECK(list->rowCount() == 0);
    CHECK(library->deleteService(sid) == refusal::service_gone());
}

// A stored service item with a field of the wrong type used to throw
// nlohmann::type_error out of selectServiceItem and end the app. Stored data is
// now read tolerantly, and the entry point is guarded besides.
TEST_CASE("SessionFacade survives service items with malformed stored data", "[app][bug11]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    const auto sid = facade.service_repo()->create_service("Sunday");
    using namespace seventhslide::model;
    const std::vector<std::pair<std::string_view, nlohmann::json>> items{
        {k_item_type_video, {{"filename", nullptr}, {"loop", "yes"}, {"autoplay", 3.5}}},
        {k_item_type_image, {{"filename", 42}}},
        {k_item_type_image_folder, {{"images", 5}, {"folder_name", 7}, {"folder_id", "x"}}},
        {"bible", {{"chapter", "three"}, {"ref", 16}, {"verses", {{{"verse_num", "1"}, {"text", 2}}}}}},
    };
    for (const auto& [type, data] : items) {
        (void)facade.service_repo()->add_item(sid, type, data);
    }
    REQUIRE(facade.library()->openService(static_cast<qint64>(sid)));
    REQUIRE(facade.serviceItems()->rowCount() == static_cast<int>(items.size()));
    QStringList errors;
    QObject::connect(&facade, &SessionFacade::errorOccurred,
                     [&errors](const QString& message) { errors << message; });
    for (int row = 0; row < facade.serviceItems()->rowCount(); ++row) {
        INFO("row " << row);
        REQUIRE_NOTHROW((void)facade.live()->selectServiceItem(row));
        REQUIRE_NOTHROW((void)facade.program()->nextSlide());
    }
    // Not even a caught failure: the bad fields simply read as absent.
    INFO(errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(errors.isEmpty());
}

TEST_CASE("An entry point says what is missing, not that the database is", "[app][errors]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade closed;
    CHECK(closed.library()->deleteSong(1) == refusal::no_library());

    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* library = facade.library();
    CHECK(library->deleteSong(-1) == refusal::song_gone());
    CHECK(library->renameBible(-1, QStringLiteral("KJV")) == refusal::bible_gone());
    CHECK(library->renameService(-1, QStringLiteral("Easter")) == refusal::service_gone());
    CHECK(library->setServiceTags(-1, {}) == refusal::service_gone());
    CHECK(library->updateServiceSongItem(-1, {}, {}, {}, {}) == refusal::service_item_gone());
    CHECK(facade.freeformLibrary()->deleteFolder(-1) == refusal::folder_gone());
    CHECK(facade.media()->library(MediaKindQml::Image)->deleteFolder(-1)
          == refusal::folder_gone());

    // A kind outside the enum (a bare number from QML) has no library, and
    // is named as such where a call takes one.
    const auto bogus = static_cast<MediaKindQml::Kind>(99);
    CHECK(facade.media()->library(bogus) == nullptr);
    CHECK(facade.media()->renameMediaFile(bogus, QStringLiteral("a.png"), QStringLiteral("b"))
              .value(QStringLiteral("error"))
          == refusal::unknown_media_kind());
}

namespace {

/// The titles the Songs tab lists, in order.
QStringList song_titles(SessionFacade& facade)
{
    auto* songs = facade.library()->songs();
    QStringList out;
    for (int row = 0; row < songs->rowCount(); ++row) {
        out << songs->data(songs->index(row), seventhslide::app::SongListModel::TitleRole).toString();
    }
    return out;
}

}  // namespace

TEST_CASE("Saving a song updates its row, not the whole library", "[app][songs]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* library = facade.library();
    const auto lyrics = QStringLiteral("[V1]\nOne");
    REQUIRE(library->createSong(QStringLiteral("Amazing Grace"), lyrics, {}).contains(QStringLiteral("songId")));
    const auto well = library->createSong(QStringLiteral("it is well"), lyrics, {})
                          .value(QStringLiteral("songId")).toLongLong();
    // Written behind the window's back, as a sync does: no save shows it.
    facade.open_library().database()->exec(
        "INSERT INTO songs (title, lyrics) VALUES ('Behind', '[V1]\nOne')");

    // Each where the list's title order puts it.
    REQUIRE(library->createSong(QStringLiteral("Be Thou My Vision"), lyrics, {}).contains(QStringLiteral("songId")));
    CHECK(song_titles(facade) == QStringList{"Amazing Grace", "Be Thou My Vision", "it is well"});
    REQUIRE(library->updateSong(well, QStringLiteral("Abide With Me"), lyrics, {}, {}).isEmpty());
    CHECK(song_titles(facade) == QStringList{"Abide With Me", "Amazing Grace", "Be Thou My Vision"});
    REQUIRE(library->deleteSong(well).isEmpty());
    CHECK(song_titles(facade) == QStringList{"Amazing Grace", "Be Thou My Vision"});
    // A search finds a song saved since.
    library->songs()->setFilter(QStringLiteral("vision"));
    CHECK(song_titles(facade) == QStringList{"Be Thou My Vision"});
    library->songs()->setFilter({});

    // A sync reads again only what it changed.
    seventhslide::sync::Changed setting;
    setting.add("app_settings", "ccli_licence_number");
    facade.reload_after_sync(setting);
    CHECK_FALSE(song_titles(facade).contains(QStringLiteral("Behind")));
    seventhslide::sync::Changed song;
    song.add("songs", first_text(facade, QStringLiteral("SELECT uid FROM songs WHERE title = 'Behind'")));
    facade.reload_after_sync(song);
    CHECK(song_titles(facade).contains(QStringLiteral("Behind")));
}
