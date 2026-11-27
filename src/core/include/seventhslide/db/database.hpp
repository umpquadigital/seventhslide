#pragma once

// SQLite access via Qt SQL (QSQLITE).
// Seam: Qt Core + Qt Sql only - documented boundary for seventhslide_core.
// No QML. Repositories in this module own domain table access.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <QSqlDatabase>
#include <QSqlError>
#include <QString>

#include "seventhslide/util/error_text.hpp"

namespace seventhslide::db {

/// A statement or a connection SQLite refused. ``what()`` is what was being
/// done and the driver's words, for the log; ``for_people`` says it plainly,
/// by SQLite's error code (a busy library, a full disk, a damaged file).
class DatabaseError : public util::InternalError {
public:
    DatabaseError(const QSqlError& error, std::string_view context);
    /// SQLite's primary result code (``SQLITE_BUSY`` is 5), or -1.
    [[nodiscard]] int code() const noexcept { return code_; }
    /// The write was refused because the database is open read-only (a
    /// viewer's library: ``SessionFacade::set_read_only``).
    [[nodiscard]] bool read_only() const noexcept { return code_ == 8; }

private:
    DatabaseError(const QSqlError& error, std::string_view context, int code);
    int code_ = -1;
};

inline constexpr int k_busy_timeout_ms = 5000;
inline constexpr int k_wal_autocheckpoint_pages = 400;

/// A column of a table, as SQLite describes it (``pragma_table_info``).
struct TableColumn {
    std::string name;
    bool not_null = false;
    bool primary = false;
};

/**
 * RAII wrapper around a named ``QSqlDatabase`` connection.
 *
 * Connection names are unique per instance so tests can open several DBs at once.
 * Destroy the ``Database`` before the process tears down Qt SQL (tests: keep instances
 * in scope until assertions finish).
 *
 * File-backed opens enable WAL + busy_timeout.
 * In-memory opens skip WAL (not meaningful) but still set foreign_keys + busy_timeout.
 */
class Database {
public:
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    ~Database();

    /// Open (or create) a file-backed SQLite database.
    [[nodiscard]] static Database open_file(const std::filesystem::path& path);

    /// Open a private in-memory database (useful for Catch2).
    [[nodiscard]] static Database open_memory();

    [[nodiscard]] const QString& connection_name() const noexcept { return connection_name_; }

    /// Empty for in-memory databases.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] bool is_memory() const noexcept { return path_.empty(); }

    [[nodiscard]] QSqlDatabase handle() const;

    /// Execute a statement that returns no rows. Throws ``std::runtime_error`` on failure.
    void exec(std::string_view sql);


    /// ``SELECT`` a single integer column from the first row; ``fallback`` if no row.
    [[nodiscard]] int query_int(std::string_view sql, int fallback = 0);

    /// ``SELECT`` a 64-bit integer column from the first row; ``fallback`` if no row.
    [[nodiscard]] std::int64_t query_int64(std::string_view sql, std::int64_t fallback = 0);

    /// ``last_insert_rowid()`` for this connection (after an INSERT).
    [[nodiscard]] std::int64_t last_insert_id();

    /// ``table``'s columns, in order; none for a table that is not there.
    /// Read once per table: the schema is made in one step (``create_schema``,
    /// which calls ``forget_columns``) and does not change while it is open.
    [[nodiscard]] const std::vector<TableColumn>& table_columns(std::string_view table);
    /// The schema changed: read each table's columns again when next asked.
    void forget_columns() noexcept { columns_.clear(); }

    /// How many statements ``db::prepare`` has prepared on this connection,
    /// which tests hold a loop to.
    [[nodiscard]] std::int64_t statements_prepared() const noexcept { return prepared_; }
    void count_prepared() noexcept { ++prepared_; }

    /// Begin a transaction, or a savepoint inside the one already running.
    void begin();
    /// Commit the innermost transaction / release its savepoint.
    void commit();
    /// Roll the innermost transaction / savepoint back.
    void rollback();
    /// Depth of nested ``begin`` calls (0 = no transaction running).
    [[nodiscard]] int transaction_depth() const noexcept { return tx_depth_; }
    /// Begin a read (``ReadTransaction``): false, and nothing begun, inside
    /// a transaction or a read already.
    [[nodiscard]] bool begin_read();
    /// End the read ``begin_read`` began.
    void end_read();

private:
    explicit Database(QString connection_name);

    static Database open_with_name(const QString& database_name,
                                   std::filesystem::path path);

    void apply_connection_pragmas();
    /// Close and unregister the connection this instance owns, if any.
    void close() noexcept;

    QString connection_name_;
    std::filesystem::path path_;
    bool active_ = false;
    int tx_depth_ = 0;
    bool reading_ = false;
    std::map<std::string, std::vector<TableColumn>, std::less<>> columns_;
    std::int64_t prepared_ = 0;
};

/**
 * RAII transaction: ``BEGIN IMMEDIATE`` on construction, ``ROLLBACK`` on
 * destruction unless ``commit()`` ran.
 *
 * Use it for any loop of writes: one commit instead of one per row, and a throw
 * part-way through leaves the database as it was. To read one consistent
 * state without the write lock, use ``ReadTransaction``.
 */
class Transaction {
public:
    explicit Transaction(Database& db) : db_(&db) { db_->begin(); }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    ~Transaction()
    {
        if (db_ == nullptr) {
            return;
        }
        try {
            db_->rollback();
        } catch (...) {  // NOLINT(bugprone-empty-catch): nothing to do while unwinding
        }
    }

    /// A COMMIT that fails leaves the transaction open; the destructor then
    /// rolls it back instead of leaving the connection mid-transaction.
    void commit()
    {
        db_->commit();
        db_ = nullptr;
    }

private:
    Database* db_ = nullptr;
};

/**
 * RAII read: every statement in it sees the database as it was at the first,
 * so a record of several rows is read whole however another connection
 * writes meanwhile. A file-backed database (WAL) holds no lock for it that a
 * write on another connection waits for. Inside a transaction it does
 * nothing: that one's reads are one already. No transaction may begin in it.
 */
class ReadTransaction {
public:
    explicit ReadTransaction(Database& db) : db_(db.begin_read() ? &db : nullptr) {}

    ReadTransaction(const ReadTransaction&) = delete;
    ReadTransaction& operator=(const ReadTransaction&) = delete;
    ReadTransaction(ReadTransaction&&) = delete;
    ReadTransaction& operator=(ReadTransaction&&) = delete;

    ~ReadTransaction()
    {
        if (db_ == nullptr) {
            return;
        }
        try {
            db_->end_read();
        } catch (...) {  // NOLINT(bugprone-empty-catch): nothing to do while unwinding
        }
    }

private:
    Database* db_ = nullptr;
};

}  // namespace seventhslide::db
