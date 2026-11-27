// Every library search follows one rule (db::SearchQuery): case, accents and
// punctuation aside. One table of searches runs against the rule itself, the
// QML singleton and each tab's own filter, and the web remote's library.

#include <catch2/catch_test_macros.hpp>

#include "facade_all.hpp"
#include "folder_library.hpp"
#include "media_library.hpp"
#include "search_text_qml.hpp"
#include "service_browser.hpp"
#include "session_remote_bridge.hpp"
#include "song_list_model.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/search_text.hpp>
#include <seventhslide/db/web_page_repository.hpp>
#include <seventhslide/model/library.hpp>
#include <seventhslide/model/web_page.hpp>
#include <seventhslide/remote/remote_bridge.hpp>

#include <nlohmann/json.hpp>

#include <QStringList>

#include <functional>
#include <string>
#include <vector>

using seventhslide::app::FolderLibrary;
using seventhslide::app::MediaBrowserModel;
using seventhslide::app::SessionFacade;
using seventhslide::db::MediaKind;
using seventhslide::db::SearchQuery;
namespace MediaKindQml = seventhslide::app::MediaKindQml;

namespace {

/// The titles every list holds, and what each search must find among them.
const QStringList k_titles{"Église Saint-Pierre", "'Twas Grace, Sweet-Sounding", "Hymn #45",
                           "Amazing Grace"};

struct Case {
    const char* query;
    QStringList found;
};

const std::vector<Case> k_cases{
    {"", k_titles},
    {"eglise", {"Église Saint-Pierre"}},           // accents aside
    {"ÉGLISE", {"Église Saint-Pierre"}},           // case aside
    {"saint pierre", {"Église Saint-Pierre"}},     // a dash is a space
    {"twas grace sweet sounding", {"'Twas Grace, Sweet-Sounding"}},  // punctuation aside
    {"grace", {"'Twas Grace, Sweet-Sounding", "Amazing Grace"}},
    {"#45", {"Hymn #45"}},                         // folds to "45", and is there as typed
    {"#", {"Hymn #45"}},                           // folds to nothing: as typed
    {"zzz", {}},
};

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

/// Each case against ``shown``: the titles a list shows for a search.
void check_every_case(const char* where, const std::function<QStringList(const QString&)>& shown)
{
    for (const auto& c : k_cases) {
        const auto got = sorted(shown(QString::fromUtf8(c.query)));
        INFO(where << ", searching \"" << c.query << "\": found "
                   << got.join(QStringLiteral(" | ")).toStdString());
        CHECK(got == sorted(c.found));
    }
}

/// The file rows a library tab's browser shows.
QStringList listed(FolderLibrary& library)
{
    QStringList out;
    const MediaBrowserModel& model = *library.browser();
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row);
        if (model.data(index, MediaBrowserModel::KindRole).toString() == QStringLiteral("file")) {
            out << model.data(index, MediaBrowserModel::NameRole).toString();
        }
    }
    return out;
}

}  // namespace

TEST_CASE("One search rule: case, accents and punctuation aside", "[search]")
{
    seventhslide::test::ensure_qt_application();
    check_every_case("db::SearchQuery", [](const QString& q) {
        QStringList out;
        const SearchQuery search(q);
        for (const auto& t : k_titles) {
            if (search.matches(t)) {
                out << t;
            }
        }
        return out;
    });
    const seventhslide::app::SearchText qml;
    check_every_case("SearchText (QML)", [&qml](const QString& q) {
        QStringList out;
        for (const auto& t : k_titles) {
            if (qml.matches(t, q)) {
                out << t;
            }
        }
        return out;
    });
}

TEST_CASE("Every library tab and the remote search by the one rule", "[search][app]")
{
    seventhslide::test::ensure_qt_application();
    SessionFacade facade;
    REQUIRE(facade.open_memory_database());
    auto* library = facade.library();
    for (qsizetype i = 0; i < k_titles.size(); ++i) {
        const QString& title = k_titles.at(i);
        const auto name = title.toStdString();
        REQUIRE(library->createSong(title, QStringLiteral("[V1]\nLine"), {})
                    .contains(QStringLiteral("songId")));
        REQUIRE(library->createService(title) >= 0);
        seventhslide::model::FreeformItem item;
        item.name = name;
        (void)facade.freeform_repo()->create_item(item);
        seventhslide::model::WebPage page;
        page.name = name;
        page.url = "https://example.com/";
        (void)facade.webLibrary()->repo()->create_item(page);
        // A file each, named apart (titled as the others are).
        for (const auto kind : {MediaKind::Image, MediaKind::Video, MediaKind::Audio}) {
            const auto filename = "file" + std::to_string(i)
                                  + std::string(kind == MediaKind::Audio ? ".mp3" : ".png");
            facade.media_repo()->upsert_file(kind, filename, name);
        }
    }

    // Written behind the browsers' backs: shown once the library is read again.
    facade.refresh_library();

    check_every_case("Songs", [&](const QString& q) {
        auto* songs = library->songs();
        songs->setFilter(q);
        QStringList out;
        for (int row = 0; row < songs->rowCount(); ++row) {
            out << songs->data(songs->index(row), seventhslide::app::SongListModel::TitleRole).toString();
        }
        return out;
    });
    seventhslide::app::ServiceBrowser services;
    services.setServices(library->services());
    check_every_case("Services", [&](const QString& q) {
        services.setSearch(q);
        QStringList out;
        for (int row = 0; row < services.rowCount(); ++row) {
            out << services.data(services.index(row), seventhslide::app::ServiceBrowser::NameRole)
                       .toString();
        }
        return out;
    });
    const std::vector<std::pair<const char*, FolderLibrary*>> tabs{
        {"Images", facade.media()->library(MediaKindQml::Image)},
        {"Videos", facade.media()->library(MediaKindQml::Video)},
        {"Audio", facade.media()->library(MediaKindQml::Audio)},
        {"Freeform", facade.freeformLibrary()},
        {"Web pages", facade.webLibrary()},
    };
    for (const auto& [name, tab] : tabs) {
        check_every_case(name, [library = tab](const QString& q) {
            library->setSearch(q);
            return listed(*library);
        });
    }

    // The web remote's library and service list.
    seventhslide::app::SessionRemoteBridge bridge(facade);
    for (const char* kind : {"songs", "freeform", "images"}) {
        check_every_case(kind, [&bridge, kind](const QString& q) {
            seventhslide::remote::LibraryQuery query;
            query.kind = kind;
            query.query = q.toStdString();
            query.limit = 100;
            QStringList out;
            const auto page = bridge.library_page(query);
            for (const auto& row : page["rows"]) {
                const auto key = row.contains("title") ? "title" : "name";
                out << QString::fromStdString(row[key].get<std::string>());
            }
            return out;
        });
    }
    check_every_case("remote services", [&bridge](const QString& q) {
        QStringList out;
        const auto list = bridge.service_list({}, q.toStdString());
        for (const auto& s : list["services"]) {
            out << QString::fromStdString(s["name"].get<std::string>());
        }
        return out;
    });
}
