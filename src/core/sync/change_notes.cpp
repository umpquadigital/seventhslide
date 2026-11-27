#include "seventhslide/sync/change_notes.hpp"

#include "seventhslide/db/query.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

namespace seventhslide::sync {
namespace {

using nlohmann::json;

/// What each table's records are to people; the tables left out are not noted.
constexpr std::array<std::pair<std::string_view, std::string_view>, 16> k_kinds{{
    {"songs", "song"},
    {"services", "service"},
    {"service_items", "service"},
    {"freeform_items", "freeform"},
    {"freeform_folders", "folder"},
    {"image_folders", "folder"},
    {"video_folders", "folder"},
    {"audio_folders", "folder"},
    {"web_folders", "folder"},
    {"web_pages", "web_page"},
    {"image_files", "image"},
    {"video_files", "video"},
    {"audio_files", "audio"},
    {"font_files", "font"},
    {"bibles", "bible"},
    {"outputs", "output"},
}};

std::optional<std::string_view> kind_of(std::string_view table)
{
    for (const auto& [t, kind] : k_kinds) {
        if (t == table) {
            return kind;
        }
    }
    if (table == "style_profiles") {
        return "style_profile";
    }
    return std::nullopt;
}

std::string text_at(const json& doc, const char* name)
{
    if (!doc.is_object()) {
        return {};
    }
    const auto it = doc.find(name);
    return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/// A JSON column's value, read: the object it holds, or null. A column that
/// holds a reference travels as the JSON itself, any other as its text.
json json_column(const json& doc, const char* name)
{
    if (!doc.is_object()) {
        return json();
    }
    const auto it = doc.find(name);
    if (it == doc.end()) {
        return json();
    }
    if (it->is_object()) {
        return *it;
    }
    if (!it->is_string()) {
        return json();
    }
    auto j = json::parse(it->get<std::string>(), nullptr, /*allow_exceptions=*/false);
    return j.is_object() ? j : json();
}

std::string title_of(std::string_view table, const json& doc)
{
    if (table == "songs") {
        return text_at(doc, "title");
    }
    if (table == "image_files" || table == "video_files" || table == "audio_files") {
        return text_at(doc, "display_name");
    }
    if (table == "font_files") {
        return text_at(doc, "family");
    }
    if (table == "outputs") {
        return text_at(json_column(doc, "data"), "name");
    }
    return text_at(doc, "name");
}

/// The members of ``before``'s and ``after``'s objects at ``name`` that differ.
void differing(const json& before, const json& after, const char* name, std::set<std::string>& out)
{
    const auto at = [name](const json& j) {
        if (!j.is_object()) {
            return json::object();
        }
        const auto it = j.find(name);
        return it != j.end() && it->is_object() ? *it : json::object();
    };
    const auto b = at(before);
    const auto a = at(after);
    for (const auto& [key, value] : a.items()) {
        if (!b.contains(key) || b.at(key) != value) {
            out.insert(key);
        }
    }
    for (const auto& [key, value] : b.items()) {
        if (!a.contains(key)) {
            out.insert(key);
        }
    }
}

/// Only moved among its neighbours: what it is did not change.
bool only_moved(const SentRecord& r)
{
    return !r.added && !r.removed && r.table != "service_items" && !r.fields.empty()
           && std::ranges::all_of(r.fields, [](const std::string& f) { return f == "sort_order" || f == "order_num"; });
}

std::string lookup(db::Database& db, const char* sql, const std::string& uid)
{
    if (uid.empty()) {
        return {};
    }
    auto q = db::exec(db, QString::fromLatin1(sql), {db::sql_text(uid)}, "read a title for the shared log");
    return q.next() ? q.value(0).toString().toStdString() : std::string();
}

}  // namespace

SentRecord sent_record(const Change& change, const json& before)
{
    SentRecord out;
    out.table = change.table;
    out.key = change.key;
    out.removed = change.deleted;
    out.added = !change.deleted && before.is_null();
    const auto& doc = change.deleted ? before : change.document;
    out.title = title_of(change.table, doc);
    if (!out.added && !out.removed) {
        for (const auto& [field, version] : change.base) {
            out.fields.push_back(field);
        }
    }
    if (change.table == "service_items") {
        out.service = text_at(doc, "service_id");
    }
    if (change.table == "style_profiles" && !out.added && !out.removed
        && std::ranges::find(out.fields, "data") != out.fields.end()) {
        const auto was = json_column(before, "data");
        const auto now = json_column(change.document, "data");
        differing(was, now, "song_verse_orders", out.verse_orders);
        differing(was, now, "songs", out.themes);
        // What else the profile holds: when that is as it was, the profile
        // itself did not change, only its songs.
        auto rest_was = was.is_object() ? was : json::object();
        auto rest_now = now.is_object() ? now : json::object();
        for (auto* j : {&rest_was, &rest_now}) {
            j->erase("song_verse_orders");
            j->erase("songs");
        }
        if (rest_was == rest_now) {
            std::erase(out.fields, "data");
        }
    }
    return out;
}

ChangeNotes change_notes(db::Database& db, const std::vector<SentRecord>& sent)
{
    ChangeNotes out;
    std::map<std::pair<std::string, std::string>, std::size_t> at;
    const auto note = [&](std::string kind, const std::string& key, std::string title, const char* action,
                          const std::vector<std::string>& fields) {
        const auto [it, added] = at.try_emplace({kind, key}, out.items.size());
        if (added) {
            out.items.push_back({std::move(kind), key, std::move(title), action, {}});
        }
        auto& item = out.items[it->second];
        if (item.title.empty()) {
            item.title = std::move(title);
        }
        // Added or removed says it all; a change adds its fields.
        const std::string_view now = action;
        if (now == "removed" || (now == "added" && item.action != "removed")) {
            item.action = std::string(now);
        }
        for (const auto& f : fields) {
            if (std::ranges::find(item.fields, f) == item.fields.end()) {
                item.fields.push_back(f);
            }
        }
    };
    for (const auto& r : sent) {
        const auto kind = kind_of(r.table);
        if (!kind || only_moved(r)) {
            continue;
        }
        if (r.table == "service_items") {
            if (!r.service.empty()) {
                note("service", r.service, {}, "changed", {"items"});
            }
            continue;
        }
        if (r.table == "style_profiles") {
            for (const auto& song : r.verse_orders) {
                note("song", song, {}, "changed", {"verse_order"});
            }
            for (const auto& song : r.themes) {
                note("song", song, {}, "changed", {"theme_map"});
            }
            if (!r.added && !r.removed && r.fields.empty()) {
                continue;
            }
        }
        note(std::string(*kind), r.key, r.title, r.added ? "added" : r.removed ? "removed" : "changed", r.fields);
    }
    for (auto& item : out.items) {
        if (item.title.empty() && item.kind == "song") {
            item.title = lookup(db, "SELECT title FROM songs WHERE uid = ?", item.key);
        } else if (item.title.empty() && item.kind == "service") {
            item.title = lookup(db, "SELECT name FROM services WHERE uid = ?", item.key);
        }
        ++out.counts[item.kind][item.action];
    }
    return out;
}

}  // namespace seventhslide::sync
