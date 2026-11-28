#include <catch2/catch_test_macros.hpp>

#include "service_browser.hpp"
#include "service_list_model.hpp"
#include "test_qt_app.hpp"

#include <QDate>
#include <QSignalSpy>
#include <QLocale>
#include <QStringList>
#include <QTime>
#include <QVariantMap>

#include <string>
#include <vector>

using seventhslide::app::ServiceBrowser;
using seventhslide::app::ServiceListModel;
using seventhslide::model::Service;

namespace {

Service service(std::int64_t id, const char* name, const char* at,
                std::vector<std::string> tags = {})
{
    Service s;
    s.id = id;
    s.name = name;
    s.service_at = at;
    s.tags = std::move(tags);
    return s;
}

/// What row ``row`` holds for ``role``.
QVariant at(const ServiceBrowser& b, int row, ServiceBrowser::Roles role)
{
    return b.data(b.index(row), role);
}

QStringList names(const ServiceBrowser& b)
{
    QStringList out;
    for (int row = 0; row < b.rowCount(); ++row) {
        out.push_back(at(b, row, ServiceBrowser::NameRole).toString());
    }
    return out;
}

struct Fixture {
    ServiceListModel list;
    ServiceBrowser browser;

    Fixture()
    {
        seventhslide::test::ensure_qt_application();
        list.set_services({
            service(1, "Vespers", "2026-03-07T17:30:00", {"Youth"}),
            service(2, "Divine Worship", "2026-03-07T11:00:00"),
            service(3, "Sabbath School", "2026-03-07T09:30:00"),
            service(4, "Baptism Sabbath", "2026-03-21T11:00:00", {"Baptism"}),
            service(5, "Song Service", "", {"youth"}),
            service(6, "Prayer Meeting", "2026-03-04T19:00:00"),
        });
        list.set_open_service_id(2);
        browser.set_today(QDate(2026, 3, 7));
        browser.setServices(&list);
    }
};

}  // namespace

TEST_CASE("Service browser lists services by date, saying which day is today", "[app][services]")
{
    Fixture f;
    // Newest day first; within a day, by time; no date last.
    REQUIRE(names(f.browser) == QStringList{"Baptism Sabbath", "Sabbath School", "Divine Worship",
                                            "Vespers", "Prayer Meeting", "Song Service"});
    using R = ServiceBrowser;
    const auto& b = f.browser;
    CHECK(at(b, 1, R::DayIsTodayRole).toBool());
    CHECK_FALSE(at(b, 1, R::DayIsPastRole).toBool());
    CHECK(at(b, 4, R::DayIsPastRole).toBool());
    CHECK_FALSE(at(b, 0, R::DayIsPastRole).toBool());
    CHECK_FALSE(at(b, 5, R::DayIsPastRole).toBool());
    CHECK(at(b, 1, R::DayLabelRole).toString()
          == QLocale().toString(QDate(2026, 3, 7), QLocale::LongFormat));
    CHECK(at(b, 1, R::DateLabelRole).toString()
          == QLocale().toString(QDate(2026, 3, 7), QStringLiteral("ddd, MMM d, yyyy")));
    CHECK(at(b, 5, R::DateRole).toString().isEmpty());
    CHECK(at(b, 5, R::DateLabelRole).toString().isEmpty());
    CHECK(at(b, 5, R::TimeLabelRole).toString().isEmpty());

    CHECK(at(b, 2, R::DateRole).toString() == QStringLiteral("2026-03-07"));
    CHECK(at(b, 2, R::TimeRole).toString() == QStringLiteral("11:00"));
    CHECK(at(b, 2, R::TimeLabelRole).toString()
          == QLocale().toString(QTime(11, 0), QLocale::ShortFormat));
    CHECK(at(b, 2, R::IsOpenRole).toBool());
    CHECK(b.total() == 6);

    // One row by its id, as the window's picked service reads it.
    const QVariantMap open = b.service(2);
    CHECK(open.value(QStringLiteral("name")).toString() == QStringLiteral("Divine Worship"));
    CHECK(open.value(QStringLiteral("isOpen")).toBool());
    CHECK(b.service(99).isEmpty());
}

TEST_CASE("Service browser sorts by name or by date, either way", "[app][services]")
{
    Fixture f;
    QSignalSpy sorts(&f.browser, &ServiceBrowser::sortChanged);
    // By name, A to Z, case aside.
    f.browser.setSortKey(QStringLiteral("name"));
    f.browser.setSortAscending(true);
    CHECK(sorts.size() == 2);
    CHECK(names(f.browser) == QStringList{"Baptism Sabbath", "Divine Worship", "Prayer Meeting",
                                          "Sabbath School", "Song Service", "Vespers"});
    f.browser.setSortAscending(false);
    CHECK(names(f.browser) == QStringList{"Vespers", "Song Service", "Sabbath School",
                                          "Prayer Meeting", "Divine Worship", "Baptism Sabbath"});

    // Oldest day first, by time within it; no date still last.
    f.browser.setSortKey(QStringLiteral("date"));
    f.browser.setSortAscending(true);
    CHECK(names(f.browser) == QStringList{"Prayer Meeting", "Sabbath School", "Divine Worship",
                                          "Vespers", "Baptism Sabbath", "Song Service"});
    CHECK(f.browser.shownIds().front().toLongLong() == 6);

    // Anything else is by date.
    f.browser.setSortKey(QStringLiteral("size"));
    CHECK(f.browser.sortKey() == QStringLiteral("date"));

    // Numbers by value; one name's services newest first, either way.
    f.list.set_services({
        service(1, "Week 10", "2026-03-14T11:00:00"),
        service(2, "Week 2", "2026-01-10T11:00:00"),
        service(3, "Communion", "2026-01-03T11:00:00"),
        service(4, "communion", "2026-02-07T11:00:00"),
    });
    f.browser.setSortKey(QStringLiteral("name"));
    QStringList dates;
    for (int row = 0; row < f.browser.rowCount(); ++row) {
        dates.push_back(at(f.browser, row, ServiceBrowser::DateRole).toString());
    }
    CHECK(names(f.browser) == QStringList{"communion", "Communion", "Week 2", "Week 10"});
    CHECK(dates.front() == QStringLiteral("2026-02-07"));
    f.browser.setSortAscending(false);
    CHECK(names(f.browser) == QStringList{"Week 10", "Week 2", "communion", "Communion"});
}

TEST_CASE("Service browser changes rows in place while the order holds", "[app][services]")
{
    Fixture f;
    QSignalSpy resets(&f.browser, &QAbstractItemModel::modelReset);
    QSignalSpy updates(&f.browser, &QAbstractItemModel::dataChanged);
    auto services = f.list.services();
    services[1].name = "Divine Worship Hour";  // a rename keeps every place
    f.list.set_services(services);
    CHECK(resets.isEmpty());
    CHECK(updates.size() == 1);
    CHECK(names(f.browser).contains(QStringLiteral("Divine Worship Hour")));

    services[1].service_at = "2026-03-22T11:00:00";  // a new date moves it
    f.list.set_services(services);
    CHECK(resets.isEmpty());
    CHECK(names(f.browser).front() == QStringLiteral("Divine Worship Hour"));

    // A service gone is that row, and the view kept.
    QSignalSpy removed(&f.browser, &QAbstractItemModel::rowsRemoved);
    const auto shown = f.browser.rowCount();
    services.pop_back();
    f.list.set_services(services);
    CHECK(resets.isEmpty());
    CHECK(removed.size() == 1);
    CHECK(f.browser.rowCount() == shown - 1);

    // How they are shown is another view: from the top.
    f.browser.setSearch(QStringLiteral("Divine"));
    CHECK(resets.size() == 1);
}

TEST_CASE("Service browser counts tags and narrows to those picked, or to a search", "[app][services]")
{
    Fixture f;
    // One entry per tag whatever its case, by name.
    const auto tags = f.browser.tags();
    REQUIRE(tags.size() == 2);
    CHECK(tags[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Baptism"));
    CHECK(tags[1].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Youth"));
    CHECK(tags[1].toMap().value(QStringLiteral("count")).toInt() == 2);

    f.browser.setFilterTags({QStringLiteral("YOUTH")});
    CHECK(f.browser.shownIds() == QVariantList{qint64(1), qint64(5)});
    // The counts stay those of the whole library.
    CHECK(f.browser.tags().size() == 2);

    f.browser.setFilterTags({});
    f.browser.setSearch(QStringLiteral("sabbath"));
    CHECK(f.browser.shownIds() == QVariantList{qint64(4), qint64(3)});
    f.browser.setSearch(QStringLiteral("bapt"));  // a tag matches too
    CHECK(f.browser.shownIds() == QVariantList{qint64(4)});

    // It follows the list.
    f.list.set_services({service(9, "New", "2026-03-08T10:00:00")});
    CHECK(f.browser.total() == 1);
    CHECK(f.browser.tags().isEmpty());
}

TEST_CASE("Service browser lists the services a tag's count counted", "[app][services]")
{
    seventhslide::test::ensure_qt_application();
    ServiceListModel list;
    ServiceBrowser browser;
    // Tags are told apart as core tells them apart: letters outside ASCII by
    // their case too.
    list.set_services({service(1, "One", "2026-03-07T11:00:00", {"Église"}),
                       service(2, "Two", "2026-03-07T12:00:00", {"église"})});
    browser.setServices(&list);
    REQUIRE(browser.tags().size() == 2);
    browser.setFilterTags({QStringLiteral("Église")});
    CHECK(browser.shownIds() == QVariantList{qint64(1)});
}

TEST_CASE("Service browser narrowed to several tags lists the services with every one",
          "[app][services]")
{
    seventhslide::test::ensure_qt_application();
    ServiceListModel list;
    ServiceBrowser browser;
    list.set_services({service(1, "One", "2026-03-07T11:00:00", {"Youth", "Communion"}),
                       service(2, "Two", "2026-03-07T12:00:00", {"Youth"}),
                       service(3, "Three", "2026-03-07T13:00:00", {"communion"})});
    browser.setServices(&list);
    browser.setFilterTags({QStringLiteral("youth"), QStringLiteral("Communion")});
    CHECK(browser.shownIds() == QVariantList{qint64(1)});
    // With a search as well, both narrow.
    browser.setSearch(QStringLiteral("two"));
    CHECK(browser.shownIds().isEmpty());
    browser.setSearch({});
    browser.setFilterTags({QStringLiteral("Communion")});
    CHECK(browser.shownIds() == QVariantList{qint64(1), qint64(3)});
    // A tag no service has: none.
    browser.setFilterTags({QStringLiteral("Easter")});
    CHECK(browser.shownIds().isEmpty());
    browser.setFilterTags({});
    CHECK(browser.count() == 3);
}

TEST_CASE("Service browser marks today as the current date unless told", "[app][services]")
{
    seventhslide::test::ensure_qt_application();
    ServiceListModel list;
    ServiceBrowser browser;
    const QString now = QDate::currentDate().toString(Qt::ISODate) + QStringLiteral("T11:00:00");
    list.set_services({service(1, "Now", now.toUtf8().constData())});
    browser.setServices(&list);
    CHECK(browser.today() == QDate::currentDate());
    CHECK(at(browser, 0, ServiceBrowser::DayIsTodayRole).toBool());
    browser.refresh();
    CHECK(at(browser, 0, ServiceBrowser::DayIsTodayRole).toBool());
}
