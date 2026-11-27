#include "seventhslide/db/sync_records.hpp"

#include "seventhslide/db/query.hpp"
#include "seventhslide/db/sync_tables.hpp"

#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <set>
#include <utility>

namespace seventhslide::db {
namespace {

using nlohmann::json;

/// A table that syncs record by record.
const SyncTable& record_table(std::string_view table)
{
    if (!syncs_as_records(table)) {
        throw std::invalid_argument("No records of their own in " + std::string(table));
    }
    return *sync_table(table);
}

template <typename Range, typename Value, typename Proj = std::identity>
bool has(Range&& range, const Value& value, Proj proj = {})
{
    return std::ranges::find(range, value, proj) != std::ranges::end(range);
}

/// ``"name"``: a column or table name in SQL.
QString ident(std::string_view name)
{
    return QLatin1Char('"') + QString::fromUtf8(name) + QLatin1Char('"');
}

/// ``t.key`` split at its commas: a key table's key columns.
std::vector<std::string> key_columns(const SyncTable& t)
{
    std::vector<std::string> out;
    std::string_view rest = t.key;
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        auto column = rest.substr(0, comma);
        while (column.starts_with(' ')) {
            column.remove_prefix(1);
        }
        out.emplace_back(column);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
    }
    return out;
}

/// The member of a JSON column that names a JSON id ("data.id" -> "data", "id"),
/// or that a reference inside JSON is kept in.
std::pair<std::string, std::string> json_member(std::string_view field)
{
    const auto dot = field.find('.');
    return {std::string(field.substr(0, dot)), std::string(field.substr(dot + 1))};
}

/// A column the document leaves out: the local id, the uid, a key column.
bool left_out(const SyncTable& t, const TableColumn& c)
{
    if (c.name == "id" && c.primary) {
        return true;
    }
    if (t.as == SyncAs::Record && c.name == "uid") {
        return true;
    }
    return t.as == SyncAs::Key && has(key_columns(t), c.name);
}

/// Local ids turned to uids and back, for one call: each statement is
/// prepared once, however many references the call turns.
class Uids {
public:
    explicit Uids(Database& db) : db_(db) {}

    Database& db() { return db_; }

    std::optional<std::string> uid_of(std::string_view target, std::int64_t id)
    {
        auto& q = statement(uid_of_, target, "SELECT uid FROM %1 WHERE id = ?", "find a record's uid");
        exec(q, {static_cast<qint64>(id)}, "find a record's uid");
        auto out = q.next() ? std::optional(q.value(0).toString().toStdString()) : std::nullopt;
        q.finish();
        return out;
    }

    std::optional<std::int64_t> id_of(std::string_view target, std::string_view uid)
    {
        auto& q = statement(id_of_, target, "SELECT id FROM %1 WHERE uid = ?", "find a record by uid");
        exec(q, {sql_text(uid)}, "find a record by uid");
        auto out = q.next() ? std::optional(q.value(0).toLongLong()) : std::nullopt;
        q.finish();
        return out;
    }

private:
    using Statements = std::map<std::string, QSqlQuery, std::less<>>;

    QSqlQuery& statement(Statements& kept, std::string_view target, const char* sql, std::string_view context)
    {
        if (const auto it = kept.find(target); it != kept.end()) {
            return it->second;
        }
        auto q = prepare(db_, QString::fromLatin1(sql).arg(ident(target)), context);
        return kept.emplace(std::string(target), std::move(q)).first->second;
    }

    Database& db_;
    Statements uid_of_;
    Statements id_of_;
};

json to_json(const QVariant& v)
{
    if (v.isNull()) {
        return nullptr;
    }
    switch (v.typeId()) {
    case QMetaType::LongLong:
    case QMetaType::Int:
    case QMetaType::ULongLong:
    case QMetaType::UInt:
        return v.toLongLong();
    case QMetaType::Double:
        return v.toDouble();
    case QMetaType::QString:
        return v.toString().toStdString();
    default:
        throw std::runtime_error(std::string("A value sync cannot carry: ") + v.typeName());
    }
}

QVariant to_variant(const json& j, std::string_view field)
{
    if (j.is_null()) {
        return {};
    }
    if (j.is_number_integer()) {
        return QVariant(static_cast<qint64>(j.get<std::int64_t>()));
    }
    if (j.is_number_float()) {
        return QVariant(j.get<double>());
    }
    if (j.is_string()) {
        return sql_text(j.get_ref<const std::string&>());
    }
    throw std::invalid_argument("Not a value a column holds, for " + std::string(field));
}

/// Which way a reference is being turned, and what could not be.
struct Direction {
    bool to_uids = true;
    std::vector<std::string>* unresolved = nullptr;

    void missed(std::string_view field) const
    {
        if (unresolved != nullptr) {
            unresolved->emplace_back(field);
        }
    }
};

/// A local id as the target's uid, or a uid as the local id: null when the
/// record is not here.
json turn_value(Uids& uids, const SyncReference& r, const json& value, const Direction& dir)
{
    if (value.is_null()) {
        return nullptr;
    }
    if (dir.to_uids) {
        if (value.is_number_integer()) {
            if (auto uid = uids.uid_of(r.target, value.get<std::int64_t>())) {
                return *uid;
            }
        }
        return nullptr;
    }
    if (value.is_string()) {
        if (auto id = uids.id_of(r.target, value.get_ref<const std::string&>())) {
            return *id;
        }
    }
    dir.missed(r.field);
    return nullptr;
}

/// An object keyed by local ids as one keyed by uids, or back. An entry whose
/// record is not here is left out.
json turn_keys(Uids& uids, const SyncReference& r, const json& object, const Direction& dir)
{
    if (!object.is_object()) {
        return object;
    }
    json out = json::object();
    for (const auto& [key, value] : object.items()) {
        std::optional<std::string> turned;
        if (dir.to_uids) {
            std::int64_t id = 0;
            const auto end = key.data() + key.size();
            if (const auto [at, ec] = std::from_chars(key.data(), end, id);
                ec == std::errc{} && at == end) {
                turned = uids.uid_of(r.target, id);
            }
        } else if (const auto id = uids.id_of(r.target, key)) {
            turned = std::to_string(*id);
        } else {
            dir.missed(r.field);
        }
        if (turned) {
            out[*turned] = value;
        }
    }
    return out;
}

/// A list of local ids (or a single one) as uids, or back. An entry whose
/// record is not here is left out; a single id becomes null.
json turn_list(Uids& uids, const SyncReference& r, const json& value, const Direction& dir)
{
    if (!value.is_array()) {
        return turn_value(uids, r, value, dir);
    }
    json out = json::array();
    for (const auto& entry : value) {
        if (auto turned = turn_value(uids, r, entry, dir); !turned.is_null()) {
            out.push_back(std::move(turned));
        }
    }
    return out;
}

/// The text of ``table``'s JSON column ``column`` with the references inside
/// it turned; the text as it was when it holds none. ``setting`` is the key of
/// the ``app_settings`` row being turned.
std::string turn_json(Uids& uids, std::string_view table, std::string_view column,
                      std::string_view setting, const std::string& text, const Direction& dir)
{
    std::optional<json> parsed;
    bool turned = false;
    for (const auto& r : k_sync_references) {
        if (r.table != table || r.in == RefIn::Column) {
            continue;
        }
        if (r.in == RefIn::Setting) {
            if (column != "value" || setting != r.field) {
                continue;
            }
            parsed = json::parse(text, nullptr, false);
            if (parsed->is_discarded()) {
                return text;
            }
            *parsed = turn_value(uids, r, *parsed, dir);
            turned = true;
            continue;
        }
        const auto [in_column, member] = json_member(r.field);
        if (in_column != column) {
            continue;
        }
        if (!parsed) {
            parsed = json::parse(text, nullptr, false);
        }
        if (!parsed->is_object()) {
            return text;
        }
        const auto it = parsed->find(member);
        if (it == parsed->end()) {
            continue;
        }
        switch (r.in) {
        case RefIn::JsonKeys:
            *it = turn_keys(uids, r, *it, dir);
            break;
        case RefIn::JsonList:
            *it = turn_list(uids, r, *it, dir);
            break;
        default:
            *it = turn_value(uids, r, *it, dir);
            break;
        }
        turned = true;
    }
    return turned ? parsed->dump() : text;
}

std::vector<std::string> part_columns(Database& db, const SyncPart& part)
{
    std::vector<std::string> out;
    for (const auto& c : db.table_columns(part.table)) {
        if (!(c.name == "id" && c.primary) && c.name != part.owner_column) {
            out.push_back(c.name);
        }
    }
    return out;
}

/// The values of a key table's key columns for ``key``, references turned to
/// local ids; nullopt when a record the key names is not here.
std::optional<std::vector<std::pair<std::string, QVariant>>>
key_values(Uids& uids, const SyncTable& t, std::string_view key)
{
    const auto columns = key_columns(t);
    std::vector<std::pair<std::string, QVariant>> out;
    std::string_view rest = key;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        std::string_view part = rest;
        if (i + 1 < columns.size()) {
            const auto slash = rest.find('/');
            if (slash == std::string_view::npos) {
                return std::nullopt;
            }
            part = rest.substr(0, slash);
            rest = rest.substr(slash + 1);
        }
        if (const auto* ref = column_reference(t.table, columns[i])) {
            const auto id = uids.id_of(ref->target, part);
            if (!id) {
                return std::nullopt;
            }
            out.emplace_back(columns[i], QVariant(static_cast<qint64>(*id)));
        } else {
            out.emplace_back(columns[i], sql_text(part));
        }
    }
    return out;
}

/// What a key table's key is read from, a column for each part: a column
/// holding a reference reads as the uid it names, "" when that is not here.
QStringList key_parts(const SyncTable& t)
{
    QStringList out;
    for (const auto& c : key_columns(t)) {
        if (const auto* ref = column_reference(t.table, c)) {
            out << QStringLiteral("(SELECT uid FROM %1 WHERE id = %2.%3)")
                       .arg(ident(ref->target), ident(t.table), ident(c));
        } else {
            out << ident(c);
        }
    }
    return out;
}

/// The key of a key table's row, read by ``key_parts`` from ``first`` on.
std::string key_of_row(const SyncTable& t, const QSqlQuery& q, int first)
{
    std::string key;
    const auto parts = static_cast<int>(key_columns(t).size());
    for (int i = 0; i < parts; ++i) {
        key += (i == 0 ? "" : "/") + q.value(first + i).toString().toStdString();
    }
    return key;
}

/// The JSON id of an output-like row: its JSON column's member.
std::string json_id(const SyncTable& t, const QString& text)
{
    const auto [column, member] = json_member(t.key);
    const auto data = json::parse(text.toStdString(), nullptr, false);
    if (!data.is_object()) {
        return {};
    }
    const auto it = data.find(member);
    return it != data.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

/// The rowid of ``t``'s record ``key``, if it is here.
std::optional<std::int64_t> find_rowid(Uids& uids, const SyncTable& t, std::string_view key)
{
    auto& db = uids.db();
    if (t.as == SyncAs::Record) {
        auto q = exec(db, QStringLiteral("SELECT rowid FROM %1 WHERE uid = ?").arg(ident(t.table)),
                      {sql_text(key)}, "find a record");
        return q.next() ? std::optional(q.value(0).toLongLong()) : std::nullopt;
    }
    if (t.as == SyncAs::JsonId) {
        const auto column = json_member(t.key).first;
        auto q = exec(db,
                      QStringLiteral("SELECT rowid, %1 FROM %2 ORDER BY rowid")
                          .arg(ident(column), ident(t.table)),
                      {}, "find a record");
        while (q.next()) {
            if (json_id(t, q.value(1).toString()) == key) {
                return q.value(0).toLongLong();
            }
        }
        return std::nullopt;
    }
    const auto values = key_values(uids, t, key);
    if (!values) {
        return std::nullopt;
    }
    QStringList where;
    for (const auto& [column, value] : *values) {
        where << ident(column) + QStringLiteral(" = ?");
    }
    auto q = prepare(db,
                     QStringLiteral("SELECT rowid FROM %1 WHERE %2")
                         .arg(ident(t.table), where.join(QStringLiteral(" AND "))),
                     "find a record");
    for (int i = 0; i < static_cast<int>(values->size()); ++i) {
        q.bindValue(i, (*values)[static_cast<std::size_t>(i)].second);
    }
    if (!q.exec()) {
        throw_query(q, "find a record");
    }
    return q.next() ? std::optional(q.value(0).toLongLong()) : std::nullopt;
}

json read_part(Database& db, const SyncPart& part, std::int64_t owner)
{
    const auto columns = part_columns(db, part);
    QStringList names;
    for (const auto& c : columns) {
        names << ident(c);
    }
    auto q = exec(db,
                  QStringLiteral("SELECT %1 FROM %2 WHERE %3 = ? ORDER BY rowid")
                      .arg(names.join(QStringLiteral(", ")), ident(part.table),
                           ident(part.owner_column)),
                  {static_cast<qint64>(owner)}, "read a record's parts");
    json rows = json::array();
    while (q.next()) {
        json row = json::array();
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            row.push_back(to_json(q.value(i)));
        }
        rows.push_back(std::move(row));
    }
    return json{{"columns", columns}, {"rows", std::move(rows)}};
}

void write_part(Database& db, const SyncPart& part, std::int64_t owner, const json& value)
{
    const auto known = part_columns(db, part);
    if (!value.is_object() || !value.contains("columns") || !value.contains("rows")
        || !value["columns"].is_array() || !value["rows"].is_array()) {
        throw std::invalid_argument("Not a record's " + std::string(part.table));
    }
    std::vector<std::string> columns;
    QStringList names;
    QStringList marks;
    for (const auto& c : value["columns"]) {
        if (!c.is_string() || !has(known, c.get<std::string>())) {
            throw std::invalid_argument("No such column of " + std::string(part.table));
        }
        columns.push_back(c.get<std::string>());
        names << ident(columns.back());
        marks << QStringLiteral("?");
    }
    exec(db,
         QStringLiteral("DELETE FROM %1 WHERE %2 = ?")
             .arg(ident(part.table), ident(part.owner_column)),
         {static_cast<qint64>(owner)}, "replace a record's parts");
    auto insert = prepare(db,
                          QStringLiteral("INSERT INTO %1 (%2, %3) VALUES (?, %4)")
                              .arg(ident(part.table), ident(part.owner_column),
                                   names.join(QStringLiteral(", ")),
                                   marks.join(QStringLiteral(", "))),
                          "write a record's parts");
    for (const auto& row : value["rows"]) {
        if (!row.is_array() || row.size() != columns.size()) {
            throw std::invalid_argument("A row of " + std::string(part.table)
                                        + " that does not match its columns");
        }
        insert.bindValue(0, static_cast<qint64>(owner));
        for (std::size_t i = 0; i < columns.size(); ++i) {
            insert.bindValue(static_cast<int>(i) + 1, to_variant(row[i], columns[i]));
        }
        if (!insert.exec()) {
            throw_query(insert, "write a record's parts");
        }
    }
}

}  // namespace

std::vector<std::string_view> record_tables_in_order()
{
    std::vector<std::string_view> out;
    std::vector<std::string_view> left;
    for (const auto& t : k_sync_tables) {
        if (syncs_as_records(t.table)) {
            left.push_back(t.table);
        }
    }
    while (!left.empty()) {
        // The first table, in k_sync_tables order, all of whose targets are out.
        const auto ready = std::ranges::find_if(left, [&](std::string_view table) {
            return std::ranges::all_of(k_sync_references, [&](const SyncReference& r) {
                return owner_of(r.table) != table || r.target == table || !has(left, r.target);
            });
        });
        if (ready == left.end()) {
            throw std::logic_error("The references between synced tables go round in a circle");
        }
        out.push_back(*ready);
        left.erase(ready);
    }
    return out;
}

std::vector<std::string> record_keys(Database& db, std::string_view table)
{
    const auto& t = record_table(table);
    std::vector<std::string> out;
    if (t.as == SyncAs::Record) {
        auto q = exec(db, QStringLiteral("SELECT uid FROM %1 ORDER BY rowid").arg(ident(table)), {},
                      "list records");
        while (q.next()) {
            out.push_back(q.value(0).toString().toStdString());
        }
    } else if (t.as == SyncAs::JsonId) {
        auto q = exec(db,
                      QStringLiteral("SELECT %1 FROM %2 ORDER BY rowid")
                          .arg(ident(json_member(t.key).first), ident(table)),
                      {}, "list records");
        while (q.next()) {
            if (auto id = json_id(t, q.value(0).toString()); !id.empty()) {
                out.push_back(std::move(id));
            }
        }
    } else {
        auto q = exec(db,
                      QStringLiteral("SELECT %1 FROM %2 ORDER BY rowid")
                          .arg(key_parts(t).join(QStringLiteral(", ")), ident(table)),
                      {}, "list records");
        while (q.next()) {
            out.push_back(key_of_row(t, q, 0));
        }
    }
    return out;
}

bool has_records(Database& db, std::string_view table)
{
    if (record_table(table).as == SyncAs::JsonId) {
        return !record_keys(db, table).empty();  // a row without an id is none
    }
    auto q = exec(db, QStringLiteral("SELECT EXISTS (SELECT 1 FROM %1)").arg(ident(table)), {},
                  "find whether there are records");
    return q.next() && q.value(0).toBool();
}

std::optional<json> read_record(Database& db, std::string_view table, std::string_view key)
{
    const auto& t = record_table(table);
    Uids uids(db);
    const auto rowid = find_rowid(uids, t, key);
    if (!rowid) {
        return std::nullopt;
    }
    std::vector<const TableColumn*> columns;
    QStringList names;
    for (const auto& c : db.table_columns(table)) {
        if (!left_out(t, c)) {
            names << ident(c.name);
            columns.push_back(&c);
        }
    }
    json doc = json::object();
    if (!columns.empty()) {
        auto q = exec(db,
                      QStringLiteral("SELECT %1 FROM %2 WHERE rowid = ?")
                          .arg(names.join(QStringLiteral(", ")), ident(table)),
                      {static_cast<qint64>(*rowid)}, "read a record");
        if (!q.next()) {
            return std::nullopt;
        }
        const Direction to_uids{true, nullptr};
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            const auto& name = columns[static_cast<std::size_t>(i)]->name;
            json value = to_json(q.value(i));
            if (const auto* ref = column_reference(table, name)) {
                value = turn_value(uids, *ref, value, to_uids);
            } else if (value.is_string()) {
                value = turn_json(uids, table, name, key, value.get<std::string>(), to_uids);
            }
            doc[name] = std::move(value);
        }
    }
    for (const auto& part : parts_of(table)) {
        doc[std::string(part.table)] = read_part(db, part, *rowid);
    }
    return doc;
}

RecordWrite write_record(Database& db, std::string_view table, std::string_view key,
                         const json& document, std::optional<std::int64_t> id_here)
{
    const auto& t = record_table(table);
    if (!document.is_object()) {
        throw std::invalid_argument("A record's document is a JSON object");
    }
    const auto& all_columns = db.table_columns(table);
    auto parts = parts_of(table);

    Transaction tx(db);
    Uids uids(db);
    RecordWrite out;
    std::vector<std::pair<std::string, QVariant>> keys;
    if (t.as == SyncAs::Key) {
        auto values = key_values(uids, t, key);
        if (!values) {
            for (const auto& column : key_columns(t)) {
                if (column_reference(table, column) != nullptr) {
                    out.unresolved.push_back(column);
                }
            }
            return out;
        }
        keys = std::move(*values);
    } else if (t.as == SyncAs::Record) {
        keys.emplace_back("uid", sql_text(key));
    }
    const auto rowid = find_rowid(uids, t, key);

    const Direction to_ids{false, &out.unresolved};
    std::vector<std::pair<std::string, QVariant>> values;
    for (const auto& [name, value] : document.items()) {
        if (has(parts, name, &SyncPart::table)) {
            continue;
        }
        const auto column = std::ranges::find(all_columns, name, &TableColumn::name);
        if (column == all_columns.end() || left_out(t, *column)) {
            throw std::invalid_argument(std::string(table) + " has no field " + name);
        }
        json turned = value;
        if (const auto* ref = column_reference(table, name)) {
            turned = turn_value(uids, *ref, value, to_ids);
            if (turned.is_null() && !value.is_null() && column->not_null) {
                return {false, {name}};
            }
        } else if (value.is_string()) {
            turned = turn_json(uids, table, name, key, value.get<std::string>(), to_ids);
        }
        if (t.as == SyncAs::JsonId && name == json_member(t.key).first
            && json_id(t, sql_text(turned.is_string() ? turned.get<std::string>() : "")) != key) {
            throw std::invalid_argument("An output's id is not its key");
        }
        values.emplace_back(name, to_variant(turned, name));
    }

    std::int64_t owner = 0;
    if (rowid) {
        owner = *rowid;
        if (!values.empty()) {
            QStringList sets;
            for (const auto& [name, value] : values) {
                sets << ident(name) + QStringLiteral(" = ?");
            }
            auto q = prepare(db,
                             QStringLiteral("UPDATE %1 SET %2 WHERE rowid = ?")
                                 .arg(ident(table), sets.join(QStringLiteral(", "))),
                             "write a record");
            int i = 0;
            for (const auto& [name, value] : values) {
                q.bindValue(i++, value);
            }
            q.bindValue(i, static_cast<qint64>(*rowid));
            if (!q.exec()) {
                throw_query(q, "write a record");
            }
        }
    } else {
        QStringList names;
        QStringList marks;
        for (const auto& [name, value] : keys) {
            names << ident(name);
            marks << QStringLiteral("?");
        }
        for (const auto& [name, value] : values) {
            names << ident(name);
            marks << QStringLiteral("?");
        }
        if (names.isEmpty()) {
            throw std::invalid_argument("A new " + std::string(table) + " record with nothing in it");
        }
        QVariant old_id;  // null: a new id
        if (id_here && t.as == SyncAs::Record) {
            auto taken = exec(db, QStringLiteral("SELECT 1 FROM %1 WHERE id = ?").arg(ident(table)),
                              {static_cast<qint64>(*id_here)}, "find a record's old id");
            if (!taken.next()) {
                old_id = static_cast<qint64>(*id_here);
                names << QStringLiteral("id");
                marks << QStringLiteral("?");
            }
        }
        auto q = prepare(db,
                         QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                             .arg(ident(table), names.join(QStringLiteral(", ")),
                                  marks.join(QStringLiteral(", "))),
                         "add a record");
        int i = 0;
        for (const auto& [name, value] : keys) {
            q.bindValue(i++, value);
        }
        for (const auto& [name, value] : values) {
            q.bindValue(i++, value);
        }
        if (!old_id.isNull()) {
            q.bindValue(i, old_id);
        }
        if (!q.exec()) {
            throw_query(q, "add a record");
        }
        owner = q.lastInsertId().toLongLong();
    }
    for (const auto& part : parts) {
        if (const auto it = document.find(part.table); it != document.end()) {
            write_part(db, part, owner, *it);
        }
    }
    tx.commit();
    out.written = true;
    return out;
}

std::optional<std::int64_t> record_id(Database& db, std::string_view table, std::string_view key)
{
    const auto& t = record_table(table);
    if (t.as != SyncAs::Record) {
        return std::nullopt;
    }
    Uids uids(db);
    return find_rowid(uids, t, key);
}

bool record_exists(Database& db, std::string_view table, std::string_view key)
{
    Uids uids(db);
    return find_rowid(uids, record_table(table), key).has_value();
}

namespace {

bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/// Every run of exactly 32 lower-case hex digits in ``text``: a uid.
void add_uids(std::string_view text, std::set<std::string>& out)
{
    for (std::size_t at = 0; at < text.size();) {
        if (!is_hex_digit(text[at])) {
            ++at;
            continue;
        }
        auto end = at;
        while (end < text.size() && is_hex_digit(text[end])) {
            ++end;
        }
        if (end - at == 32) {
            out.emplace(text.substr(at, 32));
        }
        at = end;
    }
}

/// ``text`` as a file's name: itself, and what follows its last "/" (a
/// path or a URL, any query left off).
void add_file_names(std::string_view text, std::set<std::string>& out)
{
    out.emplace(text);
    auto name = text.substr(text.find_last_of('/') + 1);
    name = name.substr(0, name.find_first_of("?#"));
    if (!name.empty()) {
        out.emplace(name);
    }
}

void add_names(const json& value, bool files, std::set<std::string>& out)
{
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        add_uids(text, out);
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first != std::string::npos && (text[first] == '{' || text[first] == '[')) {
            // JSON held as text: what it holds, each in its place.
            if (const auto inner = json::parse(text, nullptr, false); !inner.is_discarded()) {
                add_names(inner, files, out);
                return;
            }
        }
        if (files) {
            add_file_names(text, out);
        }
    } else if (value.is_object()) {
        for (const auto& [member, v] : value.items()) {
            add_uids(member, out);  // an object keyed by uid
            add_names(v, files, out);
        }
    } else if (value.is_array()) {
        for (const auto& v : value) {
            add_names(v, files, out);
        }
    }
}

}  // namespace

std::set<std::string> names_in(std::string_view table, std::string_view key, const json& document)
{
    std::set<std::string> out;
    if (document.is_object()) {
        for (const auto& [column, value] : document.items()) {
            add_names(value, names_files(table, column), out);
        }
    }
    if (const auto* t = sync_table(table);
        t != nullptr && t->as == SyncAs::Key && t->key.find(',') != std::string_view::npos) {
        for (std::size_t at = 0; at <= key.size();) {
            const auto slash = std::min(key.find('/', at), key.size());
            out.emplace(key.substr(at, slash - at));
            at = slash + 1;
        }
    }
    return out;
}

bool delete_record(Database& db, std::string_view table, std::string_view key)
{
    const auto& t = record_table(table);
    Uids uids(db);
    const auto rowid = find_rowid(uids, t, key);
    if (!rowid) {
        return false;
    }
    exec(db, QStringLiteral("DELETE FROM %1 WHERE rowid = ?").arg(ident(table)),
         {static_cast<qint64>(*rowid)}, "delete a record");
    return true;
}

}  // namespace seventhslide::db
