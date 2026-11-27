#include "seventhslide/exchange/bundle.hpp"

#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace seventhslide::exchange {

namespace {

using nlohmann::json;

constexpr std::string_view k_damaged =
    "The file cannot be read: it is damaged, or was not made by SeventhSlide.";

[[nodiscard]] std::string_view to_string(canvas::MediaRefKind kind) noexcept
{
    return kind == canvas::MediaRefKind::Video ? "video" : "picture";
}

/// Reads the fields of one object, noting the first that is missing or of
/// another type. Every read is checked; nothing throws.
class Fields {
public:
    explicit Fields(const json& j) : j_(j), ok_(j.is_object()) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    std::string text(const char* key, bool required = true, std::size_t max = 4096)
    {
        const json* v = find(key);
        if (v == nullptr) {
            ok_ = ok_ && !required;
            return {};
        }
        if (!v->is_string() || v->get_ref<const std::string&>().size() > max) {
            ok_ = false;
            return {};
        }
        return v->get<std::string>();
    }

    /// A name or title: cut at ``max`` bytes (between characters) rather
    /// than refused, so what a library let someone type always reads back.
    std::string label(const char* key, bool required = true, std::size_t max = 400)
    {
        const json* v = find(key);
        if (v == nullptr) {
            ok_ = ok_ && !required;
            return {};
        }
        if (!v->is_string()) {
            ok_ = false;
            return {};
        }
        std::string text = v->get<std::string>();
        if (text.size() > max) {
            std::size_t cut = max;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u) {
                --cut;  // not inside a character
            }
            text.resize(cut);
        }
        return text;
    }

    std::vector<std::string> labels(const char* key, std::size_t max = 200)
    {
        std::vector<std::string> out;
        for (const auto& v : list(key)) {
            if (!v.is_string()) {
                ok_ = false;
                return {};
            }
            std::string text = v.get<std::string>();
            if (text.size() > max) {
                std::size_t cut = max;
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u) {
                    --cut;
                }
                text.resize(cut);
            }
            out.push_back(std::move(text));
        }
        return out;
    }

    std::int64_t number(const char* key, std::int64_t lo, std::int64_t hi, bool required = true)
    {
        const json* v = find(key);
        if (v == nullptr) {
            ok_ = ok_ && !required;
            return lo;
        }
        if (!v->is_number_integer() || v->get<std::int64_t>() < lo || v->get<std::int64_t>() > hi) {
            ok_ = false;
            return lo;
        }
        return v->get<std::int64_t>();
    }

    std::uint64_t size(const char* key)
    {
        const json* v = find(key);
        if (v == nullptr || !v->is_number_unsigned()) {
            ok_ = false;
            return 0;
        }
        return v->get<std::uint64_t>();
    }

    bool flag(const char* key)
    {
        const json* v = find(key);
        if (v == nullptr) {
            return false;
        }
        if (!v->is_boolean()) {
            ok_ = false;
            return false;
        }
        return v->get<bool>();
    }

    /// The array at ``key`` (empty when absent), at most ``k_max_listed`` long.
    const json& list(const char* key)
    {
        static const json empty = json::array();
        const json* v = find(key);
        if (v == nullptr) {
            return empty;
        }
        if (!v->is_array() || v->size() > k_max_listed) {
            ok_ = false;
            return empty;
        }
        return *v;
    }

    std::vector<std::string> texts(const char* key, std::size_t max = 4096)
    {
        std::vector<std::string> out;
        for (const auto& v : list(key)) {
            if (!v.is_string() || v.get_ref<const std::string&>().size() > max) {
                ok_ = false;
                return {};
            }
            out.push_back(v.get<std::string>());
        }
        return out;
    }

    const json* object(const char* key)
    {
        const json* v = find(key);
        if (v != nullptr && !v->is_object()) {
            ok_ = false;
            return nullptr;
        }
        return v;
    }

    void fail() noexcept { ok_ = false; }

private:
    [[nodiscard]] const json* find(const char* key) const
    {
        if (!j_.is_object()) {
            return nullptr;
        }
        const auto it = j_.find(key);
        return it == j_.end() || it->is_null() ? nullptr : &*it;
    }

    const json& j_;
    bool ok_;
};

[[nodiscard]] bool is_sha256_hex(std::string_view s) noexcept
{
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

/// ASCII letters and digits only: whatever the locale says a letter is.
[[nodiscard]] constexpr bool ascii_alnum(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// ``ext`` lower-cased when it is one an entry name may end in: a dot and one
/// to ten letters or digits. Anything else is left off, so every name written
/// is one ``is_entry_in`` takes.
[[nodiscard]] std::string entry_extension(std::string_view ext)
{
    if (ext.size() < 2 || ext.size() > 11 || ext.front() != '.'
        || !std::all_of(ext.begin() + 1, ext.end(), ascii_alnum)) {
        return {};
    }
    return util::to_lower(ext);
}

[[nodiscard]] std::string numbered(std::string_view dir, std::size_t n, std::string_view ext)
{
    char digits[24];
    std::snprintf(digits, sizeof digits, "%04zu", n);
    return std::string(dir) + digits + std::string(ext);
}

}  // namespace

// ---- names ------------------------------------------------------------------

std::string item_entry(std::size_t n)
{
    return numbered(k_items_dir, n, ".json");
}

std::string preview_entry(std::size_t n)
{
    return numbered(k_previews_dir, n, ".png");
}

std::string media_entry(std::string_view sha256, std::string_view ext)
{
    return std::string(k_media_dir) + std::string(sha256) + entry_extension(ext);
}

std::string font_entry(std::string_view sha256, std::string_view ext)
{
    return std::string(k_fonts_dir) + std::string(sha256) + entry_extension(ext);
}

bool is_entry_in(std::string_view name, std::string_view dir)
{
    if (!name.starts_with(dir) || name.size() == dir.size() || name.size() > dir.size() + 128) {
        return false;
    }
    const auto leaf = name.substr(dir.size());
    if (leaf.front() == '.') {
        return false;
    }
    return std::all_of(leaf.begin(), leaf.end(), [](char c) {
        return ascii_alnum(c) || c == '.' || c == '-' || c == '_';
    });
}

bool is_package_file(std::string_view name)
{
    if (!name.starts_with(k_package_media_dir) || name.size() > 1024) {
        return false;
    }
    if (name.find('\\') != std::string_view::npos || name.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t at = 0;
    while (at <= name.size()) {
        const auto slash = name.find('/', at);
        const auto part = name.substr(at, slash == std::string_view::npos ? std::string_view::npos : slash - at);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        at = slash + 1;
    }
    return true;
}

std::string_view bundle_extension_for(std::size_t items, bool any_folder) noexcept
{
    return items == 1 && !any_folder ? k_single_extension : k_bundle_extension;
}

// ---- JSON -------------------------------------------------------------------

std::optional<json> parse_json_limited(std::string_view text)
{
    // Measure the nesting first, so a deep file is refused before a parser
    // sees it.
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char c : text) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '[' || c == '{') {
            if (++depth > k_max_json_depth) {
                return std::nullopt;
            }
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    auto j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        return std::nullopt;
    }
    return j;
}

// ---- manifest ---------------------------------------------------------------

json to_json(const Manifest& m)
{
    json outputs = json::array();
    for (const auto& o : m.outputs) {
        outputs.push_back({{"id", o.id}, {"name", o.name}, {"width", o.canvas.width}, {"height", o.canvas.height}});
    }
    json folders = json::array();
    for (const auto& f : m.folders) {
        json j{{"key", f.key}, {"name", f.name}};
        j["parent"] = f.parent.empty() ? json(nullptr) : json(f.parent);
        folders.push_back(std::move(j));
    }
    json items = json::array();
    for (const auto& i : m.items) {
        json j{{"entry", i.entry}, {"name", i.name}, {"tags", i.tags}};
        if (!i.preview.empty()) {
            j["preview"] = i.preview;
        }
        j["folder"] = i.folder.empty() ? json(nullptr) : json(i.folder);
        items.push_back(std::move(j));
    }
    json media = json::array();
    for (const auto& f : m.media) {
        media.push_back({{"entry", f.entry},
                         {"kind", to_string(f.kind)},
                         {"title", f.title},
                         {"size", f.size},
                         {"sha256", f.sha256}});
    }
    json fonts = json::array();
    for (const auto& f : m.fonts) {
        fonts.push_back({{"entry", f.entry},
                         {"family", f.family},
                         {"style", f.style},
                         {"bold", f.bold},
                         {"italic", f.italic},
                         {"variable", f.variable},
                         {"size", f.size},
                         {"sha256", f.sha256}});
    }
    json j{{"format", k_bundle_format},
           {"format_version", m.format_version},
           {"freeform_schema_version", m.freeform_schema_version},
           {"made_by", m.made_by},
           {"made_at", m.made_at},
           {"source_library", m.source_library},
           {"outputs", std::move(outputs)},
           {"folders", std::move(folders)},
           {"items", std::move(items)},
           {"media", std::move(media)},
           {"fonts", std::move(fonts)},
           {"not_included", {{"media", m.media_not_included}, {"fonts", m.fonts_not_included}}},
           {"has_rundown", m.has_rundown}};
    return j;
}

std::optional<Manifest> read_manifest(const json& j, std::string& refusal)
{
    const auto damaged = [&refusal]() -> std::optional<Manifest> {
        refusal = std::string(k_damaged);
        return std::nullopt;
    };
    if (!j.is_object() || !j.contains("format") || j["format"] != k_bundle_format) {
        return damaged();
    }
    Fields top(j);
    Manifest m;
    m.format_version = static_cast<int>(top.number("format_version", 1, 1'000'000));
    if (!top.ok()) {
        return damaged();
    }
    if (m.format_version > k_bundle_format_version) {
        refusal = "The file was made by a newer SeventhSlide. Update SeventhSlide to import it.";
        return std::nullopt;
    }
    m.freeform_schema_version = static_cast<int>(top.number("freeform_schema_version", 1, 1'000'000));
    m.made_by = top.label("made_by", false, 200);
    m.made_at = top.text("made_at", false, 64);
    m.source_library = top.text("source_library", false, 200);
    m.has_rundown = top.flag("has_rundown");

    std::set<std::string> entries;
    const auto entry_ok = [&entries](const std::string& e, std::string_view dir) {
        return is_entry_in(e, dir) && entries.insert(e).second;
    };

    std::set<std::string> output_ids;
    for (const auto& o : top.list("outputs")) {
        Fields f(o);
        BundleOutput out;
        out.id = f.text("id", true, 200);
        out.name = f.label("name", false, 200);
        out.canvas.width = static_cast<int>(f.number("width", 1, 100000));
        out.canvas.height = static_cast<int>(f.number("height", 1, 100000));
        if (!f.ok() || out.id.empty() || !output_ids.insert(out.id).second) {
            return damaged();
        }
        m.outputs.push_back(std::move(out));
    }

    std::map<std::string, std::string> parents;
    for (const auto& o : top.list("folders")) {
        Fields f(o);
        BundleFolder folder;
        folder.key = f.text("key", true, 64);
        folder.name = f.label("name", true, 400);
        folder.parent = f.text("parent", false, 64);
        if (!f.ok() || folder.key.empty() || parents.contains(folder.key)) {
            return damaged();
        }
        parents[folder.key] = folder.parent;
        m.folders.push_back(std::move(folder));
    }
    // Every parent is a folder of the file, and no folder is its own
    // ancestor. Each folder is walked once: a walk stops at one already
    // known to reach the top, and meeting one on its own path is a loop.
    {
        enum class Seen { Not, OnPath, Reaches };
        std::map<std::string, Seen> seen;
        for (const auto& [key, parent] : parents) {
            std::vector<std::string> path;
            std::string at = key;
            while (!at.empty() && seen[at] != Seen::Reaches) {
                if (seen[at] == Seen::OnPath || !parents.contains(at)) {
                    return damaged();
                }
                seen[at] = Seen::OnPath;
                path.push_back(at);
                at = parents.at(at);
            }
            for (const auto& done : path) {
                seen[done] = Seen::Reaches;
            }
        }
    }

    for (const auto& o : top.list("items")) {
        Fields f(o);
        BundleItem item;
        item.entry = f.text("entry");
        item.preview = f.text("preview", false);
        item.name = f.label("name", true, 400);
        item.folder = f.text("folder", false, 64);
        item.tags = f.labels("tags", 200);
        if (!f.ok() || !entry_ok(item.entry, k_items_dir)
            || (!item.preview.empty() && !entry_ok(item.preview, k_previews_dir))
            || (!item.folder.empty() && !parents.contains(item.folder))) {
            return damaged();
        }
        m.items.push_back(std::move(item));
    }

    for (const auto& o : top.list("media")) {
        Fields f(o);
        BundleMedia media;
        media.entry = f.text("entry");
        const auto kind = f.text("kind", true, 16);
        media.title = f.label("title", false, 400);
        media.size = f.size("size");
        media.sha256 = f.text("sha256", true, 64);
        if (kind == "video") {
            media.kind = canvas::MediaRefKind::Video;
        } else if (kind != "picture") {
            f.fail();
        }
        if (!f.ok() || !entry_ok(media.entry, k_media_dir) || !is_sha256_hex(media.sha256)) {
            return damaged();
        }
        m.media.push_back(std::move(media));
    }

    for (const auto& o : top.list("fonts")) {
        Fields f(o);
        BundleFont font;
        font.entry = f.text("entry");
        font.family = f.label("family", true, 200);
        font.style = f.label("style", false, 200);
        font.bold = f.flag("bold");
        font.italic = f.flag("italic");
        font.variable = f.flag("variable");
        font.size = f.size("size");
        font.sha256 = f.text("sha256", true, 64);
        if (!f.ok() || font.family.empty() || !entry_ok(font.entry, k_fonts_dir)
            || !is_sha256_hex(font.sha256)) {
            return damaged();
        }
        m.fonts.push_back(std::move(font));
    }

    if (const json* missing = top.object("not_included")) {
        Fields f(*missing);
        m.media_not_included = f.labels("media", 400);
        m.fonts_not_included = f.labels("fonts", 200);
        if (!f.ok()) {
            return damaged();
        }
    }
    if (!top.ok()) {
        return damaged();
    }
    if (m.items.empty() && !m.has_rundown) {
        refusal = "The file holds nothing to import.";
        return std::nullopt;
    }
    return m;
}

// ---- rundown ----------------------------------------------------------------

json to_json(const Rundown& r)
{
    json rows = json::array();
    for (const auto& row : r.rows) {
        json j{{"kind", model::to_string(row.kind)}, {"title", row.title}};
        if (!row.item.empty()) {
            j["item"] = row.item;
        }
        if (!row.files.empty()) {
            j["files"] = row.files;
        }
        if (row.data.is_object() && !row.data.empty()) {
            j["data"] = row.data;
        }
        if (!row.stack_id.empty() || !row.stack_name.empty()) {
            j["stack"] = {{"id", row.stack_id}, {"name", row.stack_name}};
        }
        if (!row.also_play.empty()) {
            j["also_play"] = row.also_play;
        }
        rows.push_back(std::move(j));
    }
    return json{{"name", r.name},
                {"tags", r.tags},
                {"service_at", r.service_at},
                {"audio_fades", r.audio_fades.is_object() ? r.audio_fades : json::object()},
                {"rows", std::move(rows)}};
}

json row_data(model::ContentKind kind, const json& data)
{
    json out = json::object();
    if (!data.is_object()) {
        return out;
    }
    const auto keep = [&](const char* key, auto is_type) {
        const auto it = data.find(key);
        if (it != data.end() && is_type(*it)) {
            out[key] = *it;
        }
    };
    const auto boolean = [](const json& v) { return v.is_boolean(); };
    const auto text = [](const json& v) { return v.is_string() && v.get_ref<const std::string&>().size() <= 4096; };
    const auto whole = [](const json& v) { return v.is_number_integer(); };
    const auto number = [](const json& v) { return v.is_number(); };
    switch (kind) {
    case model::ContentKind::Video:
    case model::ContentKind::Audio:
        keep("autoplay", boolean);
        keep("loop", boolean);
        break;
    case model::ContentKind::ImageFolder:
        keep("folder_name", text);
        break;
    case model::ContentKind::Web:
        keep("name", text);
        keep("url", text);
        keep("width", whole);
        keep("height", whole);
        keep("zoom", number);
        break;
    case model::ContentKind::Live:
        keep("kind", text);
        keep("name", text);
        keep("url", text);
        break;
    default:
        break;
    }
    return out;
}

std::optional<Rundown> read_rundown(const json& j, const std::vector<BundleItem>& items,
                                    std::string& refusal)
{
    const auto damaged = [&refusal]() -> std::optional<Rundown> {
        refusal = std::string(k_damaged);
        return std::nullopt;
    };
    std::set<std::string> item_entries;
    for (const auto& i : items) {
        item_entries.insert(i.entry);
    }
    Fields top(j);
    Rundown r;
    r.name = top.label("name", true, 400);
    r.tags = top.labels("tags", 200);
    r.service_at = top.text("service_at", false, 64);
    if (const json* fades = top.object("audio_fades")) {
        r.audio_fades = *fades;
    }
    const auto& rows = top.list("rows");
    for (const auto& o : rows) {
        Fields f(o);
        RundownRow row;
        row.kind = model::content_kind_from_string(f.text("kind", true, 32));
        row.title = f.label("title", false, 400);
        row.item = f.text("item", false);
        row.files = f.texts("files", 1024);
        if (const json* data = f.object("data")) {
            row.data = row_data(row.kind, *data);
        }
        if (const json* stack = f.object("stack")) {
            Fields s(*stack);
            row.stack_id = s.text("id", false, 200);
            row.stack_name = s.label("name", false, 200);
            if (!s.ok()) {
                f.fail();
            }
        }
        for (const auto& v : f.list("also_play")) {
            if (!v.is_number_integer() || v.get<std::int64_t>() < 0
                || static_cast<std::uint64_t>(v.get<std::int64_t>()) >= rows.size()) {
                f.fail();
                break;
            }
            // Another row, each once.
            const auto other = static_cast<std::size_t>(v.get<std::int64_t>());
            if (other != r.rows.size() && std::ranges::find(row.also_play, other) == row.also_play.end()) {
                row.also_play.push_back(other);
            }
        }
        if (!f.ok()) {
            return damaged();
        }
        switch (row.kind) {
        case model::ContentKind::Song:
        case model::ContentKind::Bible:
        case model::ContentKind::Freeform:
            if (!item_entries.contains(row.item)) {
                return damaged();
            }
            break;
        case model::ContentKind::Image:
        case model::ContentKind::Video:
        case model::ContentKind::Audio:
            // None: its file was not on the computer it came from.
            if (row.files.size() > 1) {
                return damaged();
            }
            [[fallthrough]];
        case model::ContentKind::ImageFolder:
            for (const auto& file : row.files) {
                if (!is_package_file(file)) {
                    return damaged();
                }
            }
            break;
        case model::ContentKind::Web:
        case model::ContentKind::Live:
        case model::ContentKind::Divider:
            break;
        case model::ContentKind::Empty:
            return damaged();
        }
        r.rows.push_back(std::move(row));
    }
    if (!top.ok()) {
        return damaged();
    }
    return r;
}

}  // namespace seventhslide::exchange
