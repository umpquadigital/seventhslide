#include "seventhslide/db/sync_outbox.hpp"

#include "seventhslide/db/query.hpp"

#include <QSqlQuery>

namespace seventhslide::db {
namespace {

/// The marks ``q`` reads: table, then key.
std::vector<MarkedRecord> marks_of(QSqlQuery& q)
{
    std::vector<MarkedRecord> out;
    while (q.next()) {
        out.push_back({q.value(0).toString().toStdString(), q.value(1).toString().toStdString()});
    }
    return out;
}

}  // namespace

std::vector<MarkedRecord> marked_records(Database& db)
{
    auto q = exec(db,
                  QStringLiteral("SELECT record_table, record_key FROM sync_outbox "
                                 "ORDER BY record_table, record_key"),
                  {}, "read the sync outbox");
    return marks_of(q);
}

std::vector<MarkedRecord> marked_records(Database& db, std::string_view table)
{
    auto q = exec(db,
                  QStringLiteral("SELECT record_table, record_key FROM sync_outbox WHERE record_table = ? "
                                 "ORDER BY record_key"),
                  {sql_text(table)}, "read the sync outbox");
    return marks_of(q);
}

}  // namespace seventhslide::db
