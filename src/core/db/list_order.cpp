#include "seventhslide/db/list_order.hpp"

#include "seventhslide/db/query.hpp"

#include <unordered_map>

namespace seventhslide::db {
namespace {

/// The positions in ``values`` of a longest run, in order, of rising values.
std::vector<bool> longest_rising(const std::vector<std::int64_t>& values)
{
    const auto n = values.size();
    std::vector<std::size_t> length(n, 1);
    std::vector<std::size_t> before(n, n);
    std::size_t best = n;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (values[j] < values[i] && length[j] + 1 > length[i]) {
                length[i] = length[j] + 1;
                before[i] = j;
            }
        }
        if (best == n || length[i] > length[best]) {
            best = i;
        }
    }
    std::vector<bool> keep(n, false);
    for (auto i = best; i < n; i = before[i]) {
        keep[i] = true;
    }
    return keep;
}

QString name(std::string_view identifier)
{
    return QString::fromUtf8(identifier);
}

}  // namespace

std::optional<std::vector<std::int64_t>> orders_between(std::optional<std::int64_t> low,
                                                        std::optional<std::int64_t> high,
                                                        int count)
{
    std::vector<std::int64_t> out;
    for (int i = 0; i < count; ++i) {
        if (!low && !high) {
            out.push_back(i * k_order_gap);
        } else if (!high) {
            out.push_back(*low + (i + 1) * k_order_gap);
        } else if (!low) {
            out.push_back(*high - (count - i) * k_order_gap);
        } else {
            const auto step = (*high - *low) / (count + 1);
            if (step < 1) {
                return std::nullopt;
            }
            out.push_back(*low + (i + 1) * step);
        }
    }
    return out;
}

std::vector<std::int64_t> reordered(const std::vector<std::int64_t>& now)
{
    const auto keep = longest_rising(now);
    std::vector<std::int64_t> wanted = now;
    for (std::size_t i = 0; i < now.size();) {
        if (keep[i]) {
            ++i;
            continue;
        }
        auto end = i;
        while (end < now.size() && !keep[end]) {
            ++end;
        }
        std::optional<std::int64_t> low;
        std::optional<std::int64_t> high;
        if (i > 0) {
            low = wanted[i - 1];
        }
        if (end < now.size()) {
            high = wanted[end];
        }
        const auto between = orders_between(low, high, static_cast<int>(end - i));
        if (!between) {
            for (std::size_t j = 0; j < now.size(); ++j) {
                wanted[j] = static_cast<std::int64_t>(j) * k_order_gap;
            }
            return wanted;
        }
        for (auto j = i; j < end; ++j) {
            wanted[j] = (*between)[j - i];
        }
        i = end;
    }
    return wanted;
}

std::int64_t next_order(Database& db, const OrderedList& list)
{
    auto q = exec(db,
                  QStringLiteral("SELECT MAX(%1) FROM %2 WHERE %3 IS ?")
                      .arg(name(list.order_column), name(list.table), name(list.parent_column)),
                  {sql_id(list.parent)}, "next order");
    if (!q.next() || q.value(0).isNull()) {
        return 0;
    }
    return q.value(0).toLongLong() + k_order_gap;
}

void reorder_rows(Database& db, const OrderedList& list, const std::vector<std::int64_t>& ids)
{
    if (ids.empty()) {
        return;
    }
    Transaction tx(db);
    std::unordered_map<std::int64_t, std::int64_t> numbers;
    {
        auto q = exec(db,
                      QStringLiteral("SELECT id, %1 FROM %2 WHERE %3 IS ?")
                          .arg(name(list.order_column), name(list.table), name(list.parent_column)),
                      {sql_id(list.parent)}, "reorder");
        while (q.next()) {
            numbers.emplace(q.value(0).toLongLong(), q.value(1).toLongLong());
        }
    }
    std::vector<std::int64_t> rows;
    std::vector<std::int64_t> now;
    for (const auto id : ids) {
        if (const auto it = numbers.find(id); it != numbers.end()) {
            rows.push_back(id);
            now.push_back(it->second);
        }
    }
    const auto wanted = reordered(now);
    auto set = prepare(db,
                       QStringLiteral("UPDATE %1 SET %2 = ? WHERE id = ?")
                           .arg(name(list.table), name(list.order_column)),
                       "reorder");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (wanted[i] != now[i]) {
            exec(set, {static_cast<qint64>(wanted[i]), static_cast<qint64>(rows[i])}, "reorder");
        }
    }
    tx.commit();
}

}  // namespace seventhslide::db
