// The Live tab's rows: each new list of sources taken in by key, so a row
// that stays keeps its place and its delegate.

#include "test_qt_app.hpp"

#include <catch2/catch_test_macros.hpp>

#include "live_source_list_model.hpp"

#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QString>
#include <QStringList>

#include <vector>

namespace {

using seventhslide::app::LiveSourceListModel;
using Row = LiveSourceListModel::Row;

Row source(const QString& name, const QString& detail = {})
{
    return Row{.key = QStringLiteral("device:") + name,
               .kind = QStringLiteral("device"),
               .kind_label = QStringLiteral("Capture"),
               .name = name,
               .url = {},
               .detail = detail,
               .signal = {}};
}

QStringList names(const LiveSourceListModel& model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i) {
        out.push_back(model.data(model.index(i), LiveSourceListModel::NameRole).toString());
    }
    return out;
}

struct Spies {
    explicit Spies(LiveSourceListModel& model)
        : inserted(&model, &QAbstractItemModel::rowsInserted)
        , removed(&model, &QAbstractItemModel::rowsRemoved)
        , moved(&model, &QAbstractItemModel::rowsMoved)
        , changed(&model, &QAbstractItemModel::dataChanged)
        , reset(&model, &QAbstractItemModel::modelReset)
    {
    }
    QSignalSpy inserted;
    QSignalSpy removed;
    QSignalSpy moved;
    QSignalSpy changed;
    QSignalSpy reset;
};

}  // namespace

TEST_CASE("The Live tab's rows: sources come, move, change and go", "[app][live_rows]")
{
    seventhslide::test::ensure_qt_application();
    LiveSourceListModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.set_rows({source(QStringLiteral("A")), source(QStringLiteral("B"))});
    CHECK(names(model) == QStringList{QStringLiteral("A"), QStringLiteral("B")});

    SECTION("a new source is inserted where it comes")
    {
        Spies spies(model);
        model.set_rows({source(QStringLiteral("A")), source(QStringLiteral("C")),
                        source(QStringLiteral("B"))});
        CHECK(names(model) == QStringList{QStringLiteral("A"), QStringLiteral("C"), QStringLiteral("B")});
        CHECK(spies.inserted.count() == 1);
        CHECK(spies.removed.count() == 0);
        CHECK(spies.reset.count() == 0);
    }
    SECTION("a source that moves keeps its row")
    {
        const QPersistentModelIndex b(model.index(1));
        Spies spies(model);
        model.set_rows({source(QStringLiteral("B")), source(QStringLiteral("A"))});
        CHECK(names(model) == QStringList{QStringLiteral("B"), QStringLiteral("A")});
        CHECK(spies.moved.count() == 1);
        CHECK(spies.inserted.count() == 0);
        CHECK(spies.removed.count() == 0);
        CHECK(b.row() == 0);
    }
    SECTION("a source that changes is refreshed in place")
    {
        const QPersistentModelIndex b(model.index(1));
        Spies spies(model);
        model.set_rows({source(QStringLiteral("A")), source(QStringLiteral("B"), QStringLiteral("1080p"))});
        CHECK(spies.changed.count() == 1);
        CHECK(spies.inserted.count() == 0);
        CHECK(spies.removed.count() == 0);
        CHECK(b.row() == 1);
        CHECK(model.data(b, LiveSourceListModel::DetailRole).toString() == QStringLiteral("1080p"));
    }
    SECTION("a source that goes is removed")
    {
        Spies spies(model);
        model.set_rows({source(QStringLiteral("B"))});
        CHECK(names(model) == QStringList{QStringLiteral("B")});
        CHECK(spies.removed.count() == 1);
        model.set_rows({});
        CHECK(model.rowCount() == 0);
    }
    SECTION("the same list again changes nothing")
    {
        Spies spies(model);
        model.set_rows({source(QStringLiteral("A")), source(QStringLiteral("B"))});
        CHECK(spies.inserted.count() + spies.removed.count() + spies.moved.count()
                  + spies.changed.count() + spies.reset.count()
              == 0);
    }
}

// Two capture dongles of one make are both "device:USB Video".
TEST_CASE("The Live tab's rows: two sources with one key each have a row", "[app][live_rows]")
{
    seventhslide::test::ensure_qt_application();
    LiveSourceListModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    const Row usb = source(QStringLiteral("USB Video"));

    model.set_rows({usb, usb, source(QStringLiteral("B"))});
    CHECK(names(model)
          == QStringList{QStringLiteral("USB Video"), QStringLiteral("USB Video"), QStringLiteral("B")});

    SECTION("one unplugged leaves one row")
    {
        model.set_rows({usb, source(QStringLiteral("B"))});
        CHECK(names(model) == QStringList{QStringLiteral("USB Video"), QStringLiteral("B")});
    }
    SECTION("one unplugged, with nothing after it, leaves one row")
    {
        model.set_rows({usb});
        CHECK(names(model) == QStringList{QStringLiteral("USB Video")});
    }
    SECTION("both unplugged leaves none")
    {
        model.set_rows({source(QStringLiteral("B"))});
        CHECK(names(model) == QStringList{QStringLiteral("B")});
    }
}
