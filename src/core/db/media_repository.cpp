#include "seventhslide/db/media_repository.hpp"

#include "seventhslide/db/list_order.hpp"
#include "seventhslide/db/query.hpp"

#include "seventhslide/db/json_codec.hpp"
#include "seventhslide/freeform/media_refs.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/model/audio_fades.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/static_media.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

#include <QSqlQuery>
#include <QVariant>

namespace seventhslide::db {
namespace {

/// ``sql`` with ``kind``'s registry table for ``%1``.
[[nodiscard]] QString in_files(MediaKind kind, const char* sql)
{
    return QString::fromLatin1(sql).arg(QLatin1String(media_tables(kind).files));
}

[[nodiscard]] model::MediaFile file_from(const QSqlQuery& q)
{
    model::MediaFile f;
    f.filename = q.value(0).toString().toStdString();
    f.display_name = q.value(1).toString().toStdString();
    f.library_visible = q.value(2).toInt() != 0;
    return f;
}

template <class Json, class Fn>
void visit_document_images(Json& doc, Fn&& fn);

/// ``fn(path)`` for every library image a service item of ``type`` shows, in
/// its ``data``: an image item's file, an image folder's list, a Freeform
/// item's document (freeform::visit_document_media). ``path`` is the JSON
/// string node, so a caller may read it or point it at another file. A
/// malformed row is walked as far as it goes.
template <class Json, class Fn>
void visit_item_images(const QString& type, Json& data, Fn&& fn)
{
    if (type == QLatin1String("image")) {
        if (auto* file = json::member(data, "filename"); file != nullptr && file->is_string()) {
            fn(*file);
        }
    } else if (type == QLatin1String("image_folder")) {
        if (auto* list = json::member(data, "images"); list != nullptr && list->is_array()) {
            for (auto& el : *list) {
                if (el.is_string()) {
                    fn(el);
                }
            }
        }
    } else if (type == QLatin1String("freeform")) {
        if (auto* doc = json::member(data, "document")) {
            visit_document_images(*doc, fn);
        }
    }
}

/// ``fn(path)`` for every library file of ``kind`` a Freeform document names.
template <class Json, class Fn>
void visit_document_files(Json& doc, canvas::MediaRefKind kind, Fn&& fn)
{
    freeform::visit_document_media(doc, [&](auto& path, canvas::MediaRefKind of) {
        if (of == kind) {
            fn(path);
        }
    });
}

/// ``fn(path)`` for every library image a Freeform document names.
template <class Json, class Fn>
void visit_document_images(Json& doc, Fn&& fn)
{
    visit_document_files(doc, canvas::MediaRefKind::Picture, fn);
}

/// True when any image ``visit`` walks is ``needle``.
template <class Visit>
[[nodiscard]] bool shows_image(std::string_view needle, Visit&& visit)
{
    bool found = false;
    visit([&](const nlohmann::json& path) {
        found = found || model::media_basename(path.get_ref<const std::string&>()) == needle;
    });
    return found;
}

/// ``path`` with its leaf ``old_name`` swapped for ``new_name``, any folder
/// before it kept. False, and ``path`` untouched, when its leaf is another.
bool retarget_leaf(std::string& path, std::string_view old_name, std::string_view new_name)
{
    if (model::media_basename(path) != old_name) {
        return false;
    }
    path = path.substr(0, path.size() - old_name.size()) + std::string(new_name);
    return true;
}

/// Every image ``visit`` walks that is ``old_name``, retargeted. True when
/// one changed.
template <class Visit>
[[nodiscard]] bool retarget_images(Visit&& visit, std::string_view old_name,
                                   std::string_view new_name)
{
    bool changed = false;
    visit([&](nlohmann::json& path) {
        changed = retarget_leaf(path.get_ref<std::string&>(), old_name, new_name) || changed;
    });
    return changed;
}

}  // namespace

void MediaRepository::upsert_file(MediaKind kind,
                                  std::string_view filename,
                                  std::string_view display_name)
{
    exec(db_,
         in_files(kind, "INSERT INTO %1 (filename, display_name, library_visible) "
                        "VALUES (?, ?, 1) "
                        "ON CONFLICT(filename) DO UPDATE SET display_name = excluded.display_name"),
         {sql_text(filename), sql_text(display_name)}, "register media file");
}

void MediaRepository::set_display_name(MediaKind kind,
                                       std::string_view filename,
                                       std::string_view display_name)
{
    exec(db_, in_files(kind, "UPDATE %1 SET display_name = ? WHERE filename = ?"),
         {sql_text(display_name), sql_text(filename)}, "set media display name");
}

nlohmann::json MediaRepository::audio_fades(std::string_view filename)
{
    auto q = exec(db_, QStringLiteral("SELECT audio_fades FROM audio_files WHERE filename = ?"),
                  {sql_text(filename)}, "get audio file fades");
    if (!q.next()) {
        return nlohmann::json::object();
    }
    return model::set_audio_fades(parse_json_text(q.value(0).toString(), nlohmann::json::object()));
}

void MediaRepository::set_audio_fades(std::string_view filename, const nlohmann::json& fades)
{
    exec(db_, QStringLiteral("UPDATE audio_files SET audio_fades = ? WHERE filename = ?"),
         {json_to_qstring(model::set_audio_fades(fades)), sql_text(filename)},
         "set audio file fades");
}

std::map<std::string, nlohmann::json, std::less<>> MediaRepository::audio_fades_by_file()
{
    std::map<std::string, nlohmann::json, std::less<>> out;
    auto q = exec(db_,
                  QStringLiteral("SELECT filename, audio_fades FROM audio_files "
                                 "WHERE audio_fades <> '{}'"),
                  {}, "list audio file fades");
    while (q.next()) {
        auto fades =
            model::set_audio_fades(parse_json_text(q.value(1).toString(), nlohmann::json::object()));
        if (!fades.empty()) {
            out.emplace(q.value(0).toString().toStdString(), std::move(fades));
        }
    }
    return out;
}

std::optional<model::MediaFile> MediaRepository::get_file(MediaKind kind, std::string_view filename)
{
    auto q = exec(db_,
                  in_files(kind, "SELECT filename, display_name, library_visible FROM %1 "
                                 "WHERE filename = ?"),
                  {sql_text(filename)}, "get media file");
    if (!q.next()) {
        return std::nullopt;
    }
    return file_from(q);
}

std::vector<model::MediaFile> MediaRepository::list_files(MediaKind kind, bool visible_only)
{
    auto q = exec(db_,
                  in_files(kind, visible_only
                                     ? "SELECT filename, display_name, library_visible FROM %1 "
                                       "WHERE library_visible != 0 "
                                       "ORDER BY display_name COLLATE NOCASE ASC"
                                     : "SELECT filename, display_name, library_visible FROM %1 "
                                       "ORDER BY display_name COLLATE NOCASE ASC"),
                  {}, "list media files");
    std::vector<model::MediaFile> out;
    while (q.next()) {
        out.push_back(file_from(q));
    }
    return out;
}

void MediaRepository::set_file_visible(MediaKind kind, std::string_view filename, bool visible)
{
    exec(db_, in_files(kind, "UPDATE %1 SET library_visible = ? WHERE filename = ?"),
         {visible ? 1 : 0, sql_text(filename)}, "set media file visible");
}

bool MediaRepository::remove_file(MediaKind kind, std::string_view filename)
{
    auto q = exec(db_, in_files(kind, "DELETE FROM %1 WHERE filename = ?"),
                  {sql_text(filename)}, "remove media file");
    return q.numRowsAffected() > 0;
}

namespace {

/// Calls ``visit(shows)`` once for every row that can show a library image,
/// where ``shows(filename)`` says whether that row shows it. Each row's JSON is
/// parsed once however many filenames are asked about.
template <typename Visit>
void visit_image_ref_rows(Database& db, Visit&& visit)
{
    // Service snapshots keep their filenames in the data JSON.
    const auto sql = QStringLiteral(
        "SELECT item_type, data FROM service_items "
        "WHERE item_type IN ('image', 'image_folder', 'freeform')");
    auto items = exec(db, sql, {}, "count image refs in service items");
    while (items.next()) {
        const auto type = items.value(0).toString();
        const auto data = parse_json_text(items.value(1).toString(), nlohmann::json::object());
        visit([&](std::string_view name) {
            return shows_image(name, [&](auto&& fn) { visit_item_images(type, data, fn); });
        });
    }

    auto docs = exec(db, QStringLiteral("SELECT document FROM freeform_items"),
                     {}, "count image refs in freeform items");
    while (docs.next()) {
        const auto doc = parse_json_text(docs.value(0).toString(), nlohmann::json::object());
        visit([&](std::string_view name) {
            return shows_image(name, [&](auto&& fn) { visit_document_images(doc, fn); });
        });
    }
}

} // namespace

std::unordered_set<std::string> MediaRepository::referenced_images(
    const std::vector<std::string>& filenames)
{
    std::unordered_set<std::string> referenced;
    if (filenames.empty()) {
        return referenced;
    }
    visit_image_ref_rows(db_, [&](const auto& shows) {
        for (const auto& name : filenames) {
            if (!referenced.contains(name) && shows(name)) {
                referenced.insert(name);
            }
        }
    });
    return referenced;
}

namespace {

/// The item_type of the service items that play ``kind``'s files.
[[nodiscard]] QString playback_item_type(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Video:
        return QString::fromUtf8(model::k_item_type_video);
    case MediaKind::Audio:
        return QString::fromUtf8(model::k_item_type_audio);
    case MediaKind::Image:
        break;
    }
    throw std::invalid_argument("images are not played by filename alone");
}

/// Calls ``visit(name)`` with the filename each service item playing a
/// ``kind`` file holds. Rows whose data carries no string ``filename`` are
/// skipped.
template <typename Visit>
void visit_playback_ref_rows(Database& db, MediaKind kind, Visit&& visit)
{
    auto q = exec(db, QStringLiteral("SELECT data FROM service_items WHERE item_type = ?"),
                  {playback_item_type(kind)}, "read playback service refs");
    while (q.next()) {
        const auto data = parse_json_text(q.value(0).toString(), nlohmann::json::object());
        const auto it = data.find("filename");
        if (it != data.end() && it->is_string()) {
            visit(it->get_ref<const std::string&>());
        }
    }
    if (kind != MediaKind::Video) {
        return;
    }
    // A Freeform slide's video objects play their files too (4.7's 6): in the
    // library's items and the services' copies of them.
    const auto each_video = [&](const nlohmann::json& doc) {
        visit_document_files(doc, canvas::MediaRefKind::Video, [&](const nlohmann::json& path) {
            visit(std::string(model::media_basename(path.get_ref<const std::string&>())));
        });
    };
    auto docs = exec(db, QStringLiteral("SELECT document FROM freeform_items"), {},
                     "read freeform video refs");
    while (docs.next()) {
        each_video(parse_json_text(docs.value(0).toString(), nlohmann::json::object()));
    }
    auto items = exec(db, QStringLiteral("SELECT data FROM service_items WHERE item_type = 'freeform'"),
                      {}, "read service freeform video refs");
    while (items.next()) {
        const auto data = parse_json_text(items.value(0).toString(), nlohmann::json::object());
        if (const auto* doc = json::member(data, "document")) {
            each_video(*doc);
        }
    }
}

}  // namespace

int MediaRepository::count_playback_refs(MediaKind kind, std::string_view filename)
{
    const std::string needle(filename);
    int count = 0;
    visit_playback_ref_rows(db_, kind, [&](const std::string& name) {
        if (name == needle) {
            ++count;
        }
    });
    return count;
}

std::unordered_set<std::string> MediaRepository::referenced_playback_files(
    MediaKind kind, const std::vector<std::string>& filenames)
{
    std::unordered_set<std::string> referenced;
    if (filenames.empty()) {
        return referenced;
    }
    const std::unordered_set<std::string> wanted(filenames.begin(), filenames.end());
    visit_playback_ref_rows(db_, kind, [&](const std::string& name) {
        if (wanted.count(name) != 0) {
            referenced.insert(name);
        }
    });
    return referenced;
}

FolderTree MediaRepository::folders(MediaKind kind) const
{
    return FolderTree(db_, media_tables(kind).folders);
}

std::vector<std::string> MediaRepository::list_folder_tree_filenames(MediaKind kind,
                                                                     std::int64_t id)
{
    const auto t = media_tables(kind);
    const auto ids = descendant_folder_ids(db_, t.folders, id);
    std::vector<std::string> filenames;
    const QString sql = QStringLiteral("SELECT filename FROM %1 WHERE folder_id IN %2")
                            .arg(QLatin1String(t.items), id_in_list(ids));
    auto q = exec(db_, sql, {}, "list folder files");
    while (q.next()) {
        filenames.push_back(q.value(0).toString().toStdString());
    }
    return filenames;
}

std::vector<std::string> MediaRepository::delete_folder_tree(MediaKind kind, std::int64_t id)
{
    auto filenames = list_folder_tree_filenames(kind, id);
    // ``parent_id`` cascades to child folders and ``folder_id`` cascades to
    // their items, so dropping the root takes the whole subtree with it.
    (void)folders(kind).remove(id);
    return filenames;
}

std::unordered_set<std::string> MediaRepository::list_linked_filenames(MediaKind kind)
{
    const auto t = media_tables(kind);
    const QString sql =
        QStringLiteral("SELECT DISTINCT filename FROM %1").arg(QLatin1String(t.items));
    auto q = exec(db_, sql, {}, "list linked filenames");
    std::unordered_set<std::string> out;
    while (q.next()) {
        out.insert(q.value(0).toString().toStdString());
    }
    return out;
}

std::int64_t MediaRepository::add_folder_item(MediaKind kind,
                                              std::int64_t folder_id,
                                              std::string_view filename)
{
    const auto t = media_tables(kind);
    const auto sort_order = next_order(db_, {t.items, "sort_order", "folder_id", folder_id});

    const QString sql = QStringLiteral(
                            "INSERT INTO %1 (folder_id, filename, sort_order) VALUES (?, ?, ?)")
                            .arg(QLatin1String(t.items));
    exec(db_, sql,
         {static_cast<qint64>(folder_id), sql_text(filename), static_cast<qint64>(sort_order)}, "add folder item");
    return db_.last_insert_id();
}

std::unordered_map<std::int64_t, int> MediaRepository::folder_item_counts(MediaKind kind)
{
    const auto t = media_tables(kind);
    auto q = exec(db_,
                  QStringLiteral("SELECT folder_id, COUNT(*) FROM %1 GROUP BY folder_id")
                      .arg(QLatin1String(t.items)),
                  {}, "folder item counts");
    std::unordered_map<std::int64_t, int> out;
    while (q.next()) {
        if (q.value(0).isNull()) {
            continue;
        }
        out[q.value(0).toLongLong()] = q.value(1).toInt();
    }
    return out;
}

std::vector<model::MediaFolderItem> MediaRepository::list_folder_items(MediaKind kind,
                                                                       std::int64_t folder_id)
{
    const auto t = media_tables(kind);
    const QString sql = QStringLiteral(
                            "SELECT id, folder_id, filename, sort_order FROM %1 "
                            "WHERE folder_id = ? ORDER BY sort_order ASC, filename ASC")
                            .arg(QLatin1String(t.items));
    auto q = exec(db_, sql, {static_cast<qint64>(folder_id)}, "list folder items");
    std::vector<model::MediaFolderItem> out;
    while (q.next()) {
        model::MediaFolderItem item;
        item.id = q.value(0).toLongLong();
        item.folder_id = q.value(1).toLongLong();
        item.filename = q.value(2).toString().toStdString();
        item.sort_order = q.value(3).toLongLong();
        out.push_back(std::move(item));
    }
    return out;
}

void MediaRepository::reorder_folder_items(MediaKind kind,
                                           std::int64_t folder_id,
                                           const std::vector<std::int64_t>& ordered_item_ids)
{
    reorder_rows(db_, {media_tables(kind).items, "sort_order", "folder_id", folder_id},
                 ordered_item_ids);
}

void MediaRepository::unlink_filename_from_folders(MediaKind kind, std::string_view filename)
{
    const auto t = media_tables(kind);
    const QString sql =
        QStringLiteral("DELETE FROM %1 WHERE filename = ?").arg(QLatin1String(t.items));
    exec(db_, sql, {sql_text(filename)}, "unlink filename");
}

void MediaRepository::move_filename_to_folder(MediaKind kind,
                                              std::string_view filename,
                                              std::optional<std::int64_t> folder_id)
{
    // Unlink + add is one move: a failure between them would drop the file out
    // of every folder.
    Transaction tx(db_);
    unlink_filename_from_folders(kind, filename);
    if (folder_id) {
        (void)add_folder_item(kind, *folder_id, filename);
    }
    tx.commit();
}

void MediaRepository::move_filenames_to_folder(MediaKind kind,
                                               const std::vector<std::string>& filenames,
                                               std::optional<std::int64_t> folder_id)
{
    Transaction tx(db_);
    for (const auto& filename : filenames) {
        move_filename_to_folder(kind, filename, folder_id);
    }
    tx.commit();
}

std::size_t MediaRepository::retarget_file(MediaKind kind,
                                           std::string_view old_name,
                                           std::string_view new_name)
{
    Transaction tx(db_);
    std::size_t changed = 0;
    const auto t = media_tables(kind);
    if (kind == MediaKind::Audio) {
        // A file's own fades go with it to what replaces it.
        auto fades = exec(db_,
                          QStringLiteral("UPDATE audio_files SET audio_fades = (SELECT audio_fades "
                                         "FROM audio_files WHERE filename = ?) WHERE filename = ? "
                                         "AND EXISTS (SELECT 1 FROM audio_files WHERE filename = ?)"),
                          {sql_text(old_name), sql_text(new_name), sql_text(old_name)},
                          "carry audio file fades");
        changed += static_cast<std::size_t>(std::max(0, fades.numRowsAffected()));
    }
    auto links = exec(db_,
                      QStringLiteral("UPDATE %1 SET filename = ? WHERE filename = ?")
                          .arg(QLatin1String(t.items)),
                      {sql_text(new_name), sql_text(old_name)}, "retarget folder links");
    changed += static_cast<std::size_t>(std::max(0, links.numRowsAffected()));

    // Rows are read in full first, then written: no query is open over a
    // table while it changes.
    std::vector<std::pair<qint64, nlohmann::json>> rewrites;
    if (kind == MediaKind::Image) {
        auto items = exec(db_,
                          QStringLiteral("SELECT id, item_type, data FROM service_items "
                                         "WHERE item_type IN ('image', 'image_folder', 'freeform')"),
                          {}, "read image refs to retarget");
        while (items.next()) {
            const auto type = items.value(1).toString();
            auto data = parse_json_text(items.value(2).toString(), nlohmann::json::object());
            const bool hit = retarget_images(
                [&](auto&& fn) { visit_item_images(type, data, fn); }, old_name, new_name);
            if (hit) {
                rewrites.emplace_back(items.value(0).toLongLong(), std::move(data));
            }
        }
    } else {
        auto items = exec(db_, QStringLiteral("SELECT id, data FROM service_items WHERE item_type = ?"),
                          {playback_item_type(kind)}, "read playback refs to retarget");
        while (items.next()) {
            auto data = parse_json_text(items.value(1).toString(), nlohmann::json::object());
            auto* file = json::member(data, "filename");
            if (file != nullptr && file->is_string()
                && retarget_leaf(file->get_ref<std::string&>(), old_name, new_name)) {
                rewrites.emplace_back(items.value(0).toLongLong(), std::move(data));
            }
        }
        if (kind == MediaKind::Video) {
            // A service's copy of a Freeform item, its video objects.
            auto freeform = exec(db_, QStringLiteral("SELECT id, data FROM service_items "
                                                     "WHERE item_type = 'freeform'"),
                                 {}, "read service freeform videos to retarget");
            while (freeform.next()) {
                auto data = parse_json_text(freeform.value(1).toString(), nlohmann::json::object());
                auto* doc = json::member(data, "document");
                if (doc != nullptr
                    && retarget_images(
                        [&](auto&& fn) { visit_document_files(*doc, canvas::MediaRefKind::Video, fn); },
                        old_name, new_name)) {
                    rewrites.emplace_back(freeform.value(0).toLongLong(), std::move(data));
                }
            }
        }
    }
    if (!rewrites.empty()) {
        auto update = prepare(db_, QStringLiteral("UPDATE service_items SET data = ? WHERE id = ?"),
                              "retarget a service item");
        for (const auto& [id, data] : rewrites) {
            exec(update, {json_to_qstring(data), QVariant::fromValue(id)}, "retarget a service item");
        }
    }
    changed += rewrites.size();

    if (kind != MediaKind::Audio) {
        // A Freeform item's pictures, or its video objects' files.
        const auto ref = kind == MediaKind::Image ? canvas::MediaRefKind::Picture
                                                  : canvas::MediaRefKind::Video;
        std::vector<std::pair<qint64, nlohmann::json>> docs;
        auto rows = exec(db_, QStringLiteral("SELECT id, document FROM freeform_items"), {},
                         "read freeform docs to retarget");
        while (rows.next()) {
            auto doc = parse_json_text(rows.value(1).toString(), nlohmann::json::object());
            if (retarget_images([&](auto&& fn) { visit_document_files(doc, ref, fn); }, old_name,
                                new_name)) {
                docs.emplace_back(rows.value(0).toLongLong(), std::move(doc));
            }
        }
        if (!docs.empty()) {
            auto update = prepare(db_,
                                  QStringLiteral("UPDATE freeform_items SET document = ? WHERE id = ?"),
                                  "retarget a freeform item");
            for (const auto& [id, doc] : docs) {
                exec(update, {json_to_qstring(doc), QVariant::fromValue(id)},
                     "retarget a freeform item");
            }
        }
        changed += docs.size();
    }
    tx.commit();
    return changed;
}

}  // namespace seventhslide::db
