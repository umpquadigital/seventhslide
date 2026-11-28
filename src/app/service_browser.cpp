#include "service_browser.hpp"

#include "list_model_replace.hpp"
#include "qml_ids.hpp"

#include <seventhslide/db/search_text.hpp>
#include <seventhslide/util/strings.hpp>

#include <QCollator>
#include <QDateTime>
#include <QLocale>
#include <QStringList>
#include <QVariantMap>

#include <utility>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace seventhslide::app {

namespace {

/// ``iso`` (``yyyy-MM-ddTHH:mm:ss``) as a date-time; invalid when empty.
QDateTime when(const std::string& iso)
{
    return QDateTime::fromString(QString::fromStdString(iso), QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

/// ``s`` carries ``tag``, by the rule that counts tags (service_tag_counts),
/// so a tag lists the services its count counted.
bool has_tag(const model::Service& s, const std::string& tag)
{
    return std::ranges::any_of(s.tags, [&tag](const std::string& t) {
        return util::iequals(t, tag);
    });
}

bool matches(const model::Service& s, const db::SearchQuery& query)
{
    if (query.matches(std::string_view(s.name))) {
        return true;
    }
    return std::ranges::any_of(s.tags, [&query](const std::string& t) {
        return query.matches(std::string_view(t));
    });
}

/// The locale's long date with its day and month names shortened: "Fri, Apr
/// 3, 2026" where the long one is "Friday, April 3, 2026".
QString medium_date_format(const QLocale& locale)
{
    // The language's own long date, not the system's: macOS's long style has
    // no weekday ("March 7, 2026"), and the day is the point here.
    const QLocale named(locale.language(), locale.script(), locale.territory());
    QString format = named.dateFormat(QLocale::LongFormat);
    format.replace(QStringLiteral("dddd"), QStringLiteral("ddd"));
    format.replace(QStringLiteral("MMMM"), QStringLiteral("MMM"));
    return format;
}

}  // namespace

ServiceBrowser::ServiceBrowser(QObject* parent)
    : QAbstractListModel(parent)
{
}

QDate ServiceBrowser::today() const
{
    return today_.isValid() ? today_ : QDate::currentDate();
}

void ServiceBrowser::refresh()
{
    rebuild();
}

void ServiceBrowser::setServices(ServiceListModel* services)
{
    if (services_ == services) {
        return;
    }
    if (services_) {
        disconnect(services_, nullptr, this, nullptr);
    }
    services_ = services;
    if (services_) {
        connect(services_, &ServiceListModel::servicesChanged, this, [this] { rebuild(Rebuild::SameView); });
    }
    emit servicesChanged();
    rebuild();
}

void ServiceBrowser::setFilterTags(const QStringList& tags)
{
    if (filter_tags_ == tags) {
        return;
    }
    filter_tags_ = tags;
    emit filterTagsChanged();
    rebuild();
}

void ServiceBrowser::setSearch(const QString& search)
{
    if (search_ == search) {
        return;
    }
    search_ = search;
    emit searchChanged();
    rebuild();
}

void ServiceBrowser::set_today(const QDate& today)
{
    if (today_ == today) {
        return;
    }
    today_ = today;
    rebuild();
}

void ServiceBrowser::setSortKey(const QString& key)
{
    const QString k = key == QStringLiteral("name") ? key : QStringLiteral("date");
    if (sort_key_ == k) {
        return;
    }
    sort_key_ = k;
    emit sortChanged();
    rebuild();
}

void ServiceBrowser::setSortAscending(bool ascending)
{
    if (sort_ascending_ == ascending) {
        return;
    }
    sort_ascending_ = ascending;
    emit sortChanged();
    rebuild();
}

void ServiceBrowser::componentComplete()
{
    complete_ = true;
    rebuild();
}

int ServiceBrowser::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ServiceBrowser::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= rows_.size()) {
        return {};
    }
    const Row& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ServiceIdRole:
        return QVariant::fromValue<qint64>(r.id);
    case NameRole:
        return r.name;
    case DateRole:
        return r.date;
    case TimeRole:
        return r.time;
    case TimeLabelRole:
        return r.time_label;
    case TagsRole:
        return r.tags;
    case IsOpenRole:
        return r.open;
    case DayLabelRole:
        return r.day_label;
    case DateLabelRole:
        return r.date_label;
    case DayIsTodayRole:
        return r.today;
    case DayIsPastRole:
        return r.past;
    default:
        return {};
    }
}

QVariantMap ServiceBrowser::service(qint64 id) const
{
    const auto it = std::ranges::find(rows_, static_cast<std::int64_t>(id), &Row::id);
    if (it == rows_.end()) {
        return {};
    }
    const QModelIndex at = index(static_cast<int>(it - rows_.begin()));
    QVariantMap out;
    const auto names = roleNames();
    for (auto role = names.cbegin(); role != names.cend(); ++role) {
        out.insert(QString::fromLatin1(role.value()), data(at, role.key()));
    }
    return out;
}

QHash<int, QByteArray> ServiceBrowser::roleNames() const
{
    return {
        {ServiceIdRole, "serviceId"},
        {NameRole, "name"},
        {DateRole, "date"},
        {TimeRole, "time"},
        {TimeLabelRole, "timeLabel"},
        {TagsRole, "tags"},
        {IsOpenRole, "isOpen"},
        {DayLabelRole, "dayLabel"},
        {DateLabelRole, "dateLabel"},
        {DayIsTodayRole, "dayIsToday"},
        {DayIsPastRole, "dayIsPast"},
    };
}

QVariantList ServiceBrowser::shownIds() const
{
    QVariantList out;
    for (const auto& r : rows_) {
        out.push_back(QVariant::fromValue<qint64>(r.id));
    }
    return out;
}

void ServiceBrowser::rebuild(Rebuild what)
{
    if (!complete_) {
        return;
    }
    tags_.clear();
    total_ = 0;
    std::vector<Row> next;
    if (services_) {
        const auto& all = services_->services();
        total_ = static_cast<int>(all.size());
        for (const auto& t : model::service_tag_counts(all)) {
            tags_.push_back(QVariantMap{{QStringLiteral("name"), QString::fromStdString(t.name)},
                                        {QStringLiteral("count"), t.count}});
        }

        const db::SearchQuery query(search_);
        std::vector<std::string> wanted;
        for (const auto& t : filter_tags_) {
            if (!t.isEmpty()) {
                wanted.push_back(t.toStdString());
            }
        }
        const auto has_every_tag = [&wanted](const model::Service& s) {
            return std::ranges::all_of(wanted, [&s](const std::string& t) { return has_tag(s, t); });
        };
        std::vector<const model::Service*> shown;
        for (const auto& s : all) {
            if (s.id && has_every_tag(s) && matches(s, query)) {
                shown.push_back(&s);
            }
        }
        // By day (newest first unless ascending), earliest time first within
        // it, no date last; or by name, one name's services newest first.
        // Ties keep the library's order.
        const bool up = sort_ascending_;
        const auto by_day = [](const model::Service* a, const model::Service* b, bool ascending) {
            const QDateTime wa = when(a->service_at);
            const QDateTime wb = when(b->service_at);
            if (wa.isValid() != wb.isValid()) {
                return wa.isValid() ? -1 : 1;
            }
            if (!wa.isValid()) {
                return 0;
            }
            if (wa.date() != wb.date()) {
                return (wa.date() < wb.date()) == ascending ? -1 : 1;
            }
            return wa.time() < wb.time() ? -1 : (wb.time() < wa.time() ? 1 : 0);
        };
        if (sort_key_ == QStringLiteral("name")) {
            const QCollator& c = db::name_collator();
            std::ranges::stable_sort(shown, [&](const model::Service* a, const model::Service* b) {
                const int by_name = c.compare(QString::fromStdString(a->name),
                                              QString::fromStdString(b->name));
                if (by_name != 0) {
                    return up ? by_name < 0 : by_name > 0;
                }
                return by_day(a, b, false) < 0;
            });
        } else {
            std::ranges::stable_sort(shown, [&](const model::Service* a, const model::Service* b) {
                return by_day(a, b, up) < 0;
            });
        }

        const QLocale locale;
        const QString medium_date = medium_date_format(locale);
        const QDate today = this->today();
        const std::optional<std::int64_t> open = services_->open_service_id();
        for (const auto* s : shown) {
            const QDateTime w = when(s->service_at);
            Row r;
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): shown holds only services with an id.
            r.id = *s->id;
            r.name = QString::fromStdString(s->name);
            r.date = w.isValid() ? w.date().toString(Qt::ISODate) : QString();
            r.time = w.isValid() ? w.time().toString(QStringLiteral("HH:mm")) : QString();
            r.time_label = w.isValid() ? locale.toString(w.time(), QLocale::ShortFormat) : QString();
            for (const auto& t : s->tags) {
                r.tags.push_back(QString::fromStdString(t));
            }
            r.open = open && *open == r.id;
            r.day_label = w.isValid() ? locale.toString(w.date(), QLocale::LongFormat) : tr("No date");
            r.date_label = w.isValid() ? locale.toString(w.date(), medium_date) : QString();
            r.today = w.isValid() && w.date() == today;
            r.past = w.isValid() && w.date() < today;
            next.push_back(std::move(r));
        }
    }

    // The same services in the same order: change them where they are.
    const bool same = next.size() == rows_.size()
                      && std::ranges::equal(next, rows_, {}, &Row::id, &Row::id);
    if (same) {
        rows_ = std::move(next);
        if (!rows_.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1));
        }
    } else if (what == Rebuild::SameView) {
        RowSteps::replace(*this, rows_, std::move(next), [](const Row& row) { return row.id; });
    } else {
        beginResetModel();
        rows_ = std::move(next);
        endResetModel();
    }
    emit changed();
}

} // namespace seventhslide::app
