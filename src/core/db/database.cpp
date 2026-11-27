#include "seventhslide/db/database.hpp"

#include "seventhslide/db/query.hpp"

#include <atomic>
#include <stdexcept>
#include <utility>

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace seventhslide::db {
namespace {

std::atomic<std::uint64_t> g_connection_serial{0};

[[noreturn]] void throw_sql(const QSqlError& err, std::string_view context)
{
    throw DatabaseError(err, context);
}

QString make_connection_name()
{
    const auto n = g_connection_serial.fetch_add(1, std::memory_order_relaxed);
    return QStringLiteral("seventhslide_%1").arg(n);
}

/// SQLite's primary result code in ``error`` (Qt may give an extended one,
/// whose low byte it is), or -1.
int primary_code(const QSqlError& error)
{
    bool ok = false;
    const int code = error.nativeErrorCode().toInt(&ok);
    return ok ? code & 0xff : -1;
}

std::string said_plainly(int code)
{
    switch (code) {
    case 5:  // SQLITE_BUSY
    case 6:  // SQLITE_LOCKED
        return "The library is busy with another change. Try again in a moment.";
    case 8:  // SQLITE_READONLY
        return "This library cannot be changed on this computer.";
    case 13:  // SQLITE_FULL
        return "The disk is full, so the library could not be saved.";
    case 11:  // SQLITE_CORRUPT
    case 26:  // SQLITE_NOTADB
        return "The library's database file is damaged.";
    case 10:  // SQLITE_IOERR
    case 14:  // SQLITE_CANTOPEN
        return "The library's database file could not be read or written.";
    default:
        return "The library could not do that.";
    }
}

}  // namespace

DatabaseError::DatabaseError(const QSqlError& error, std::string_view context)
    : DatabaseError(error, context, primary_code(error))
{
}

DatabaseError::DatabaseError(const QSqlError& error, std::string_view context, int code)
    : util::InternalError(std::string(context) + ": " + error.text().toStdString(), said_plainly(code))
    , code_(code)
{
}

Database::Database(QString connection_name)
    : connection_name_(std::move(connection_name))
    , active_(true)
{
}

Database Database::open_with_name(const QString& database_name, std::filesystem::path path)
{
    Database db(make_connection_name());
    db.path_ = std::move(path);
    QSqlError open_error;
    {
        QSqlDatabase handle = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), db.connection_name_);
        handle.setDatabaseName(database_name);
        if (!handle.open()) {
            open_error = handle.lastError();
        }
    }  // the handle has to be gone before its connection can be removed
    if (open_error.isValid()) {
        db.active_ = false;
        QSqlDatabase::removeDatabase(db.connection_name_);
        throw_sql(open_error, "open SQLite database");
    }
    db.apply_connection_pragmas();
    return db;
}

Database Database::open_file(const std::filesystem::path& path)
{
    return open_with_name(QString::fromStdString(path.string()), path);
}

Database Database::open_memory()
{
    // Distinct QSqlDatabase connection names give each ":memory:" its own DB.
    return open_with_name(QStringLiteral(":memory:"), {});
}

Database::Database(Database&& other) noexcept
    : connection_name_(std::move(other.connection_name_))
    , path_(std::move(other.path_))
    , active_(other.active_)
    , tx_depth_(other.tx_depth_)
    , reading_(other.reading_)
    , columns_(std::move(other.columns_))
    , prepared_(other.prepared_)
{
    other.active_ = false;
    other.tx_depth_ = 0;
    other.reading_ = false;
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    close();
    connection_name_ = std::move(other.connection_name_);
    path_ = std::move(other.path_);
    active_ = other.active_;
    tx_depth_ = other.tx_depth_;
    reading_ = other.reading_;
    columns_ = std::move(other.columns_);
    prepared_ = other.prepared_;
    other.active_ = false;
    other.tx_depth_ = 0;
    other.reading_ = false;
    return *this;
}

Database::~Database()
{
    close();
}

void Database::close() noexcept
{
    if (!active_) {
        return;
    }
    {
        // The handle has to be gone before its connection can be removed.
        QSqlDatabase handle = QSqlDatabase::database(connection_name_);
        if (handle.isValid() && handle.isOpen()) {
            handle.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name_);
    active_ = false;
}

QSqlDatabase Database::handle() const
{
    QSqlDatabase h = QSqlDatabase::database(connection_name_, /*open=*/false);
    if (!h.isValid()) {
        throw std::runtime_error("Database connection is not valid");
    }
    return h;
}

void Database::apply_connection_pragmas()
{
    exec("PRAGMA foreign_keys = ON");
    exec("PRAGMA busy_timeout = " + std::to_string(k_busy_timeout_ms));

    if (is_memory()) {
        return;
    }

    // WAL is a persistent journal-mode choice for the file; ignore return text.
    exec("PRAGMA journal_mode = WAL");
    exec("PRAGMA wal_autocheckpoint = " + std::to_string(k_wal_autocheckpoint_pages));
}

void Database::exec(std::string_view sql)
{
    QSqlQuery q(handle());
    if (!q.exec(sql_text(sql))) {
        throw_sql(q.lastError(), sql);
    }
}

int Database::query_int(std::string_view sql, int fallback)
{
    QSqlQuery q(handle());
    if (!q.exec(sql_text(sql))) {
        throw_sql(q.lastError(), sql);
    }
    if (!q.next()) {
        return fallback;
    }
    return q.value(0).toInt();
}

std::int64_t Database::query_int64(std::string_view sql, std::int64_t fallback)
{
    QSqlQuery q(handle());
    if (!q.exec(sql_text(sql))) {
        throw_sql(q.lastError(), sql);
    }
    if (!q.next()) {
        return fallback;
    }
    return q.value(0).toLongLong();
}

std::int64_t Database::last_insert_id()
{
    return query_int64("SELECT last_insert_rowid()", 0);
}

const std::vector<TableColumn>& Database::table_columns(std::string_view table)
{
    if (const auto it = columns_.find(table); it != columns_.end()) {
        return it->second;
    }
    auto q = db::exec(*this, QStringLiteral("SELECT name, \"notnull\", pk FROM pragma_table_info(?) ORDER BY cid"),
                  {sql_text(table)}, "read a table's columns");
    std::vector<TableColumn> out;
    while (q.next()) {
        out.push_back({q.value(0).toString().toStdString(), q.value(1).toInt() != 0, q.value(2).toInt() != 0});
    }
    return columns_.emplace(std::string(table), std::move(out)).first->second;
}

namespace {

/// Savepoint name for one nesting level.
[[nodiscard]] std::string savepoint_name(int depth)
{
    return "ss_sp_" + std::to_string(depth);
}

}  // namespace

void Database::begin()
{
    if (reading_) {
        // It would take the write lock part-way through a read, which SQLite
        // refuses once another connection has written since the read began.
        throw std::logic_error("A transaction cannot begin inside a read transaction");
    }
    if (tx_depth_ == 0) {
        exec("BEGIN IMMEDIATE");
    } else {
        exec("SAVEPOINT " + savepoint_name(tx_depth_));
    }
    ++tx_depth_;
}

void Database::commit()
{
    // The depth goes down only once the statement has run: one that fails
    // leaves the transaction open, and the rollback that follows
    // (``Transaction``'s) then ends this one, not the one around it.
    if (tx_depth_ <= 1) {
        exec("COMMIT");
        tx_depth_ = 0;
        return;
    }
    exec("RELEASE " + savepoint_name(tx_depth_ - 1));
    --tx_depth_;
}

bool Database::begin_read()
{
    if (reading_ || tx_depth_ > 0) {
        return false;
    }
    exec("BEGIN DEFERRED");
    reading_ = true;
    return true;
}

void Database::end_read()
{
    reading_ = false;
    // It wrote nothing. Not ROLLBACK, which would end any query still
    // reading on this connection.
    exec("COMMIT");
}

void Database::rollback()
{
    if (tx_depth_ <= 1) {
        tx_depth_ = 0;
        exec("ROLLBACK");
        return;
    }
    --tx_depth_;
    const auto name = savepoint_name(tx_depth_);
    exec("ROLLBACK TO " + name);
    exec("RELEASE " + name);
}

}  // namespace seventhslide::db
