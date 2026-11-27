#pragma once

// Helpers every repository needs: running a statement, binding values,
// turning a failed QSqlQuery into an exception, and walking a self-referencing
// folder table.
//
// Qt SQL seam.

#include "seventhslide/db/database.hpp"

#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

/// An optional id as a bind value: NULL when absent.
[[nodiscard]] inline QVariant sql_id(std::optional<std::int64_t> id)
{
    return id ? QVariant(static_cast<qint64>(*id)) : QVariant();
}

/// UTF-8 text as a bind value.
[[nodiscard]] inline QString sql_text(std::string_view text)
{
    return QString::fromUtf8(text);
}

/// Throw a DatabaseError: the driver's message, prefixed by what was being
/// attempted.
[[noreturn]] inline void throw_query(const QSqlQuery& q, std::string_view context)
{
    throw DatabaseError(q.lastError(), context);
}

/// ``sql`` prepared on ``db``, for a statement run several times (see the
/// ``exec`` that takes a query). Throws when it does not compile.
[[nodiscard]] inline QSqlQuery prepare(Database& db, const QString& sql, std::string_view context)
{
    db.count_prepared();
    QSqlQuery q(db.handle());
    if (!q.prepare(sql)) {
        throw_query(q, std::string("prepare ") + std::string(context));
    }
    return q;
}

/// As ``prepare``, for rows read once, in order: Qt keeps none of those
/// read, so a table of any size is read in little memory.
[[nodiscard]] inline QSqlQuery prepare_forward_only(Database& db, const QString& sql, std::string_view context)
{
    db.count_prepared();
    QSqlQuery q(db.handle());
    q.setForwardOnly(true);
    if (!q.prepare(sql)) {
        throw_query(q, std::string("prepare ") + std::string(context));
    }
    return q;
}

/// Run the prepared ``q`` with ``binds`` for its ``?`` placeholders, in order.
/// Throws with ``context`` when it fails.
inline void exec(QSqlQuery& q, std::initializer_list<QVariant> binds, std::string_view context)
{
    int i = 0;
    for (const auto& value : binds) {
        q.bindValue(i++, value);
    }
    if (!q.exec()) {
        throw_query(q, context);
    }
}

/// Prepare ``sql`` on ``db`` and run it with ``binds``: the one-statement
/// case. Returns the query, before its first row, for the caller to read
/// rows, ``numRowsAffected`` or ``lastInsertId`` from. Throws with
/// ``context`` when either step fails.
inline QSqlQuery exec(Database& db,
                      const QString& sql,
                      std::initializer_list<QVariant> binds,
                      std::string_view context)
{
    auto q = prepare(db, sql, context);
    exec(q, binds, context);
    return q;
}


/**
 * ``root`` plus every folder under it in ``folder_table``.
 *
 * The table must have ``id`` and ``parent_id`` columns. One recursive query
 * rather than a SELECT per node, which would cost a prepare + exec per folder.
 * ``UNION`` (not ``UNION ALL``) de-duplicates, so a cycle - which the move
 * guards prevent, but a hand-edited database could hold - terminates the walk
 * instead of hanging.
 *
 * ``root`` comes back whether or not it exists; the callers rely on that.
 * Order is SQLite's queue order (breadth first) and no caller depends on it.
 */
[[nodiscard]] inline std::vector<std::int64_t> descendant_folder_ids(Database& db,
                                                                     std::string_view folder_table,
                                                                     std::int64_t root)
{
    const QString sql = QStringLiteral(
                            "WITH RECURSIVE tree(id) AS ("
                            "  SELECT ?"
                            "  UNION"
                            "  SELECT f.id FROM %1 f JOIN tree ON f.parent_id = tree.id"
                            ") SELECT id FROM tree")
                            .arg(sql_text(folder_table));
    auto q = exec(db, sql, {static_cast<qint64>(root)}, "folder descendants");
    std::vector<std::int64_t> out;
    while (q.next()) {
        out.push_back(q.value(0).toLongLong());
    }
    return out;
}

/// ``(1, 2, 3)`` for an SQL ``IN`` list. Integers straight from the database,
/// so there is nothing to escape; an empty list yields ``(NULL)``, which
/// matches nothing.
[[nodiscard]] inline QString id_in_list(const std::vector<std::int64_t>& ids)
{
    if (ids.empty()) {
        return QStringLiteral("(NULL)");
    }
    QString out = QStringLiteral("(");
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) {
            out += QLatin1Char(',');
        }
        out += QString::number(static_cast<qlonglong>(ids[i]));
    }
    out += QLatin1Char(')');
    return out;
}

}  // namespace seventhslide::db
