#pragma once

// What the Services window lists: the library's services by date (by time
// within a day, a service with no date last) or by name, narrowed to the
// tags picked and a search; and the tags in use, with counts.
//
// A row per service shown, with its day written out long and short, and
// whether it is today or past. While the same services stay in the same order, a change
// updates rows in place: a row being edited keeps its delegate, and the field
// being typed in.
//
// QML: ``ServiceBrowser { services: session.library.services; filterTags: [...];
// search: ...; sortKey: "name" }``.

#include "service_list_model.hpp"

#include <QtQml/qqmlregistration.h>
#include <QAbstractListModel>
#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QPointer>
#include <QQmlParserStatus>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <vector>

namespace seventhslide::app {

class ServiceBrowser : public QAbstractListModel, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(seventhslide::app::ServiceListModel* services READ services WRITE setServices
                   NOTIFY servicesChanged)
    /// Only services with every one of these tags (any case); none, all.
    Q_PROPERTY(QStringList filterTags READ filterTags WRITE setFilterTags NOTIFY filterTagsChanged)
    /// Services whose name or a tag contains this (any case).
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    /// The day marked today, and before which days are past: the current
    /// date, as of the last rebuild or refresh(), unless set (tests set it).
    Q_PROPERTY(QDate today READ today WRITE setToday NOTIFY todayChanged)
    /// "date" (the default: by day, then time) or "name".
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
    /// A to Z, or oldest day first; the default, false, is newest first.
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortChanged)
    /// ``[{name, count}]`` over every service, by name.
    Q_PROPERTY(QVariantList tags READ tags NOTIFY changed)
    /// Every service in the library.
    Q_PROPERTY(int total READ total NOTIFY changed)
    /// The services shown, in the order shown.
    Q_PROPERTY(QVariantList shownIds READ shownIds NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    enum Roles {
        ServiceIdRole = Qt::UserRole + 1,
        NameRole,
        /// ``yyyy-MM-dd``; "" for no date.
        DateRole,
        /// ``HH:mm``; "" for no date.
        TimeRole,
        /// The time as the locale writes it; "" for no date.
        TimeLabelRole,
        TagsRole,
        IsOpenRole,
        /// The day written out ("Saturday, March 7, 2026"), or "No date".
        DayLabelRole,
        /// The day shortened ("Sat, Mar 7, 2026"); "" for no date.
        DateLabelRole,
        DayIsTodayRole,
        DayIsPastRole,
    };
    Q_ENUM(Roles)

    explicit ServiceBrowser(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] ServiceListModel* services() const noexcept { return services_; }
    void setServices(ServiceListModel* services);
    [[nodiscard]] QStringList filterTags() const { return filter_tags_; }
    void setFilterTags(const QStringList& tags);
    [[nodiscard]] QString search() const { return search_; }
    void setSearch(const QString& search);
    [[nodiscard]] QDate today() const;
    void setToday(const QDate& today);
    [[nodiscard]] QString sortKey() const { return sort_key_; }
    void setSortKey(const QString& key);
    [[nodiscard]] bool sortAscending() const noexcept { return sort_ascending_; }
    void setSortAscending(bool ascending);

    [[nodiscard]] QVariantList tags() const { return tags_; }
    [[nodiscard]] int total() const noexcept { return total_; }
    [[nodiscard]] QVariantList shownIds() const;
    [[nodiscard]] int count() const noexcept { return static_cast<int>(rows_.size()); }

    /// Service ``id``'s row as a map keyed by role name (``name``,
    /// ``dateLabel``...), or an empty map when it is not shown.
    Q_INVOKABLE QVariantMap service(qint64 id) const;

    /// List again as of now: a window kept between showings may have been
    /// built yesterday.
    Q_INVOKABLE void refresh();

    // Made by QML, it reads its inputs once they are all set: a binding that
    // first reads it must not see it change while it reads.
    void classBegin() override { complete_ = false; }
    void componentComplete() override;

signals:
    void servicesChanged();
    void filterTagsChanged();
    void searchChanged();
    void todayChanged();
    void sortChanged();
    void changed();

private:
    struct Row {
        std::int64_t id = -1;
        QString name;
        QString date;
        QString time;
        QString time_label;
        QStringList tags;
        bool open = false;
        QString day_label;
        QString date_label;
        bool today = false;
        bool past = false;
    };

    /// The services changed, shown as they were; or how they are shown, which
    /// starts over from the top unless the rows are the same.
    enum class Rebuild { SameView, NewView };
    void rebuild(Rebuild what = Rebuild::NewView);
    friend struct RowSteps;

    QPointer<ServiceListModel> services_;
    QStringList filter_tags_;
    QString search_;
    QDate today_;  // invalid: the current date
    QString sort_key_ = QStringLiteral("date");
    bool sort_ascending_ = false;
    std::vector<Row> rows_;
    QVariantList tags_;
    int total_ = 0;
    bool complete_ = true;
};

} // namespace seventhslide::app
