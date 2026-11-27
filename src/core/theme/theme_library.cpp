#include <seventhslide/theme/theme_library.hpp>

#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/json/read.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/model/theme_keys.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/theme/theme_elements.hpp>
#include <seventhslide/theme/theme_picks.hpp>
#include <seventhslide/platform/paths.hpp>
#include <seventhslide/util/find.hpp>
#include <seventhslide/util/hash.hpp>
#include <seventhslide/util/random_id.hpp>
#include <seventhslide/util/strings.hpp>
#include <seventhslide/util/word_list.hpp>

#include <algorithm>
#include <iterator>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace seventhslide::theme {
namespace {

struct AssetSlot {
    std::string_view name;
    std::string_view url_key;
    std::string_view type_key;
    bool is_video = false;
};

constexpr AssetSlot k_slots[] = {
    {"bg", "background_image", "background_type", false},
    {"bg_video", "background_video", "background_type", true},
};

const AssetSlot* find_slot(std::string_view slot)
{
    return util::find_by(k_slots, slot, &AssetSlot::name);
}

std::filesystem::path theme_bg_dir(const std::filesystem::path& media_dir)
{
    return media_dir / model::k_theme_backgrounds_subdir;
}

std::string theme_bg_key(std::string_view theme_id)
{
    bool safe = !theme_id.empty();
    for (char c : theme_id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                        || c == '_' || c == '-';
        if (!ok) {
            safe = false;
            break;
        }
    }
    if (safe) {
        return std::string(theme_id);
    }
    // Stable fallback for hostile ids. It names files already on disk, so the
    // hash must never change.
    return util::fnv1a32_hex(theme_id);
}

std::string theme_bg_url(std::string_view filename)
{
    return std::string(k_theme_bg_url_prefix) + std::string(filename);
}

/// Every file a theme owns for ``slot`` is named "<key>.<slot>.<random><ext>".
std::string owned_prefix(std::string_view theme_id, std::string_view slot)
{
    return theme_bg_key(theme_id) + "." + std::string(slot) + ".";
}

bool url_owned_by(std::string_view url, std::string_view theme_id, std::string_view slot)
{
    if (!url.starts_with(k_theme_bg_url_prefix)) {
        return false;
    }
    return url.substr(k_theme_bg_url_prefix.size()).starts_with(owned_prefix(theme_id, slot));
}

/// A background another theme owns (a duplicated theme, say): take a copy so
/// deleting either theme cannot break the other.
bool needs_theme_asset_adopt(std::string_view url,
                             std::string_view theme_id,
                             std::string_view slot)
{
    return url.starts_with(k_theme_bg_url_prefix) && !url_owned_by(url, theme_id, slot);
}

/// A file of its own for ``theme_id``/``slot`` with the bytes of ``src``, an
/// owned file of another theme: a hard link, since owned files never change.
/// The new basename, or nullopt where the filesystem has no hard links.
std::optional<std::string> link_to_owned(const std::filesystem::path& src,
                                         std::string_view theme_id,
                                         std::string_view slot,
                                         const std::filesystem::path& media_dir)
{
    auto filename = new_owned_asset_name(theme_id, slot, src.extension().string());
    std::error_code ec;
    std::filesystem::create_hard_link(src, theme_bg_dir(media_dir) / filename, ec);
    if (ec) {
        return std::nullopt;
    }
    return filename;
}

/// Delete owned files whose name starts with any of ``prefixes``, except
/// ``keep`` (a basename; empty keeps nothing).
void remove_owned_files(const std::filesystem::path& media_dir,
                        const std::vector<std::string>& prefixes,
                        std::string_view keep)
{
    const auto dir = theme_bg_dir(media_dir);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::error_code entry_ec;
        if (!entry.is_regular_file(entry_ec)) {
            continue;
        }
        const auto fn = entry.path().filename().string();
        if (fn == keep) {
            continue;
        }
        const bool owned = std::any_of(prefixes.begin(), prefixes.end(),
                                       [&](const std::string& p) { return fn.starts_with(p); });
        if (owned) {
            // A file that will not go is left behind, not a reason to stop.
            std::filesystem::remove(entry.path(), entry_ec);
        }
    }
}

std::optional<std::filesystem::path> static_url_media_path(std::string_view url,
                                                            const std::filesystem::path& media_dir)
{
    constexpr std::string_view k_static = "/static/";
    if (url.size() < k_static.size() || url.substr(0, k_static.size()) != k_static) {
        return std::nullopt;
    }
    const auto rel = url.substr(k_static.size());
    auto p = (media_dir / std::string(rel)).lexically_normal();
    if (!platform::Paths::path_is_within(p, media_dir)) {
        return std::nullopt;
    }
    return p;
}

/// Delete this theme/slot's owned files except ``keep_filename`` (a basename;
/// empty deletes them all).
void prune_theme_owned_files(std::string_view theme_id,
                             std::string_view slot,
                             const std::filesystem::path& media_dir,
                             std::string_view keep_filename)
{
    if (media_dir.empty() || theme_id.empty() || slot.empty()) {
        return;
    }
    remove_owned_files(media_dir, {owned_prefix(theme_id, slot)}, keep_filename);
}

void apply_tags(model::ThemeEntry& theme, const nlohmann::json& tags_in)
{
    theme.tags = normalize_theme_tags(tags_in);
}

std::string default_theme_name(std::string_view name)
{
    std::string n = util::trim(name);
    return n.empty() ? std::string("Untitled") : n;
}

std::string now_iso_local()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

void stamp_created_at(model::ThemeEntry& theme)
{
    theme.created_at = now_iso_local();
}

} // namespace

std::string new_theme_id()
{
    return util::random_hex_id(2);
}

std::vector<std::string> normalize_theme_tags(const nlohmann::json& raw)
{
    std::vector<std::string> parts;
    if (raw.is_string()) {
        std::string s = raw.get<std::string>();
        std::string cur;
        for (char c : s) {
            if (c == ',') {
                parts.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        parts.push_back(cur);
    } else if (raw.is_array()) {
        parts = json::strings_in(raw);
    } else {
        return {};
    }

    return util::normalize_word_list(parts, {.max_words = k_max_theme_tags,
                                             .max_bytes = k_max_theme_tag_bytes,
                                             .lower_case = true});
}

nlohmann::json own_contexts_only(ThemeKind kind, const nlohmann::json& per_context)
{
    if (!per_context.is_object()) {
        return per_context;
    }
    const auto contexts = contexts_of(kind);
    nlohmann::json out = nlohmann::json::object();
    for (auto it = per_context.begin(); it != per_context.end(); ++it) {
        const bool own_context = std::ranges::any_of(
            contexts, [&it](ElementContext ctx) { return it.key() == to_string(ctx); });
        const bool set_field = it.key() == "canvas" || it.key() == "next_id";
        if (own_context || set_field) {
            out[it.key()] = it.value();
        }
    }
    return out;
}

nlohmann::json filter_theme_style(ThemeKind kind, const nlohmann::json& style_in)
{
    nlohmann::json out = nlohmann::json::object();
    if (!style_in.is_object()) {
        return out;
    }
    for (auto it = style_in.begin(); it != style_in.end(); ++it) {
        if (!is_theme_key(kind, it.key())) {
            continue;
        }
        const bool per_context = it.key() == k_elements_key || it.key() == k_transitions_key;
        out[it.key()] = per_context ? own_contexts_only(kind, it.value()) : it.value();
    }
    return out;
}

namespace {

/// What a new text theme of ``kind`` starts as: its contexts of the stock
/// design laid out for ``canvas``.
void use_stock_design(ThemeKind kind, nlohmann::json& style, geom::CanvasSize canvas)
{
    style[std::string(k_elements_key)] =
        own_contexts_only(kind, default_elements(canvas).to_json());
}

}  // namespace

nlohmann::json first_category_defaults(const model::OutputConfig& oc)
{
    ThemePicks defaults;
    for (const auto category : k_theme_categories) {
        if (const auto& themes = theme_list(oc, text_kind_of(category)); !themes.empty()) {
            (void)defaults.set(category, ThemeField::Theme, themes.front().id);
        }
        if (takes_background(category) && !oc.bg_themes.empty()) {
            (void)defaults.set(category, ThemeField::Bg, oc.bg_themes.front().id);
        }
    }
    return defaults.to_json();
}

namespace {

/// A ``kind`` Base theme's id, made from its output's and the kind's: an
/// output given its Base themes on two computers, or each time a library this
/// computer only views is opened, gets the same ones.
std::string base_theme_id(const model::OutputConfig& oc, ThemeKind kind)
{
    if (oc.id.empty()) {
        return new_theme_id();
    }
    const std::string seed = "base theme " + std::string(to_string(kind)) + " " + oc.id;
    return util::fnv1a64_hex(seed) + util::fnv1a64_hex(seed + " 2");
}

} // namespace

bool seed_default_themes(model::OutputConfig& oc)
{
    bool changed = false;
    for (const auto kind : k_theme_kinds) {
        auto& themes = theme_list(oc, kind);
        if (!themes.empty()) {
            continue;
        }
        model::ThemeEntry t;
        t.id = base_theme_id(oc, kind);
        t.name = "Base";
        t.style = nlohmann::json::object();
        if (kind != ThemeKind::Bg) {
            use_stock_design(kind, t.style, model::output_canvas(oc));
        }
        stamp_created_at(t);
        themes.push_back(std::move(t));
        changed = true;
    }
    if (!oc.category_defaults.is_object() || oc.category_defaults.empty()) {
        oc.category_defaults = first_category_defaults(oc);
        changed = true;
    }
    return changed;
}

model::OutputConfig new_output(std::string_view name, const std::vector<model::OutputConfig>& existing)
{
    model::OutputConfig oc;
    oc.id = model::new_output_id();
    oc.name = model::unique_output_name(name, existing);
    oc.canvas_width = geom::k_reference_canvas.width;
    oc.canvas_height = geom::k_reference_canvas.height;
    seed_default_themes(oc);
    return oc;
}

std::vector<model::ThemeEntry>& theme_list(model::OutputConfig& oc, ThemeKind kind)
{
    switch (kind) {
    case ThemeKind::Song:
        return oc.song_themes;
    case ThemeKind::Bible:
        return oc.bible_themes;
    case ThemeKind::Media:
        return oc.media_themes;
    case ThemeKind::Bg:
        break;
    }
    return oc.bg_themes;
}

const std::vector<model::ThemeEntry>& theme_list(const model::OutputConfig& oc, ThemeKind kind)
{
    return theme_list(const_cast<model::OutputConfig&>(oc), kind);
}

model::ThemeEntry* find_theme(model::OutputConfig& oc, ThemeKind kind, std::string_view theme_id)
{
    return util::find_by(theme_list(oc, kind), theme_id, &model::ThemeEntry::id);
}

const model::ThemeEntry* find_theme(const model::OutputConfig& oc,
                                    ThemeKind kind,
                                    std::string_view theme_id)
{
    return util::find_by(theme_list(oc, kind), theme_id, &model::ThemeEntry::id);
}

ThemeMutationResult create_theme(model::OutputConfig& oc,
                                 ThemeKind kind,
                                 std::string_view name,
                                 const nlohmann::json& style_in,
                                 const nlohmann::json& tags_in,
                                 std::string_view duplicate_from_id)
{
    ThemeMutationResult r;
    model::ThemeEntry theme;
    theme.id = new_theme_id();
    theme.name = default_theme_name(name);

    if (!duplicate_from_id.empty()) {
        const auto* src = find_theme(oc, kind, duplicate_from_id);
        if (src == nullptr) {
            r.message = "Source theme not found";
            return r;
        }
        theme.style = filter_theme_style(kind, src->style);
        theme.tags = src->tags;
        if (theme.name == "Untitled" && !src->name.empty()) {
            theme.name = src->name + " copy";
        }
    } else if (style_in.is_object() && !style_in.empty()) {
        theme.style = filter_theme_style(kind, style_in);
        set_elements_canvas(theme.style, model::output_canvas(oc));
    } else {
        theme.style = nlohmann::json::object();
    }

    if (!tags_in.is_null()) {
        apply_tags(theme, tags_in);
    }
    if (kind != ThemeKind::Bg && !has_elements(theme.style)) {
        use_stock_design(kind, theme.style, model::output_canvas(oc));
    }
    stamp_created_at(theme);

    theme_list(oc, kind).push_back(theme);
    r.ok = true;
    r.theme = theme_list(oc, kind).back();
    return r;
}

ThemeMutationResult update_theme(model::OutputConfig& oc,
                                 ThemeKind kind,
                                 std::string_view theme_id,
                                 std::string_view name,
                                 const nlohmann::json& style_in,
                                 const nlohmann::json& tags_in,
                                 bool apply_tags_flag)
{
    ThemeMutationResult r;
    if (theme_id.empty()) {
        r.message = "Missing theme id";
        return r;
    }
    auto* theme = find_theme(oc, kind, theme_id);
    if (theme == nullptr) {
        r.message = "Theme not found";
        return r;
    }
    theme->name = default_theme_name(name);
    theme->style = filter_theme_style(kind, style_in);
    set_elements_canvas(theme->style, model::output_canvas(oc));
    if (apply_tags_flag) {
        apply_tags(*theme, tags_in);
    }
    r.ok = true;
    r.theme = *theme;
    return r;
}

ThemeMutationResult delete_theme(model::OutputConfig& oc,
                                 ThemeKind kind,
                                 std::string_view theme_id)
{
    ThemeMutationResult r;
    if (theme_id.empty()) {
        r.message = "Missing theme id";
        return r;
    }
    auto& list = theme_list(oc, kind);
    const auto it = std::find_if(
        list.begin(), list.end(), [&](const model::ThemeEntry& t) { return t.id == theme_id; });
    if (it == list.end()) {
        r.message = "Theme not found";
        return r;
    }
    if (list.size() <= 1) {
        r.message = "Cannot delete the last theme of this type";
        return r;
    }
    list.erase(it);

    // A default that named it names the first theme left of its kind.
    const auto field = kind == ThemeKind::Bg ? ThemeField::Bg : ThemeField::Theme;
    auto defaults = ThemePicks::from_json(oc.category_defaults, CategorySet::all());
    for (const auto category : k_theme_categories) {
        const bool holds_kind = kind == ThemeKind::Bg || text_kind_of(category) == kind;
        if (holds_kind && defaults.of(category).id(field) == theme_id) {
            (void)defaults.set(category, field, list.front().id);
        }
    }
    oc.category_defaults = defaults.to_json();

    r.ok = true;
    return r;
}

ThemeMutationResult set_category_default(model::OutputConfig& oc,
                                         std::string_view category,
                                         std::string_view field,
                                         std::string_view theme_id)
{
    ThemeMutationResult r;
    const auto theme_category = theme_category_from_string(category);
    if (!theme_category) {
        r.message = "Invalid category";
        return r;
    }
    const auto theme_field = theme_field_from_string(field);
    if (!theme_field) {
        r.message = "Invalid field";
        return r;
    }
    if (theme_id.empty()) {
        r.message = "Missing theme id";
        return r;
    }
    const auto kind =
        *theme_field == ThemeField::Theme ? text_kind_of(*theme_category) : ThemeKind::Bg;
    if (find_theme(oc, kind, theme_id) == nullptr) {
        r.message = "Theme not found";
        return r;
    }
    auto defaults = ThemePicks::from_json(oc.category_defaults, CategorySet::all());
    if (!defaults.set(*theme_category, *theme_field, std::string(theme_id))) {
        r.message = "This category has no background";
        return r;
    }
    oc.category_defaults = defaults.to_json();
    r.ok = true;
    return r;
}

void delete_theme_owned_files(std::string_view theme_id,
                              const std::filesystem::path& media_dir,
                              std::string_view slot)
{
    if (media_dir.empty() || theme_id.empty()) {
        return;
    }
    std::vector<std::string> prefixes;
    if (!slot.empty()) {
        prefixes.push_back(owned_prefix(theme_id, slot));
    } else {
        for (const auto& s : k_slots) {
            prefixes.push_back(owned_prefix(theme_id, s.name));
        }
    }
    remove_owned_files(media_dir, prefixes, {});
}

std::filesystem::path theme_assets_dir(const std::filesystem::path& media_dir)
{
    return theme_bg_dir(media_dir);
}

bool is_theme_asset_slot(std::string_view slot) noexcept
{
    return find_slot(slot) != nullptr;
}

std::string theme_asset_url(const model::ThemeEntry& theme, std::string_view slot)
{
    const auto* info = find_slot(slot);
    if (info == nullptr || !theme.style.is_object()) {
        return {};
    }
    return theme.style.value(std::string(info->url_key), std::string{});
}

std::string new_owned_asset_name(std::string_view theme_id,
                                 std::string_view slot,
                                 std::string_view extension)
{
    return owned_prefix(theme_id, slot) + new_theme_id().substr(0, 8) + std::string(extension);
}

ThemeMutationResult set_owned_theme_asset(model::ThemeEntry& theme,
                                          std::string_view slot,
                                          std::string_view filename)
{
    ThemeMutationResult r;
    const auto* info = find_slot(slot);
    if (info == nullptr) {
        r.message = "Invalid asset slot";
        return r;
    }
    if (theme.id.empty()) {
        r.message = "Missing theme id";
        return r;
    }
    if (!filename.starts_with(owned_prefix(theme.id, slot))) {
        r.message = "Not an owned asset of this theme";
        return r;
    }

    if (!theme.style.is_object()) {
        theme.style = nlohmann::json::object();
    }
    theme.style[std::string(info->url_key)] = theme_bg_url(filename);
    theme.style[std::string(info->type_key)] = info->is_video ? "video" : "image";

    r.ok = true;
    r.theme = theme;
    return r;
}

void prune_unused_theme_assets(const model::ThemeEntry& theme,
                               const std::filesystem::path& media_dir,
                               std::string_view only_slot)
{
    for (const auto& slot : k_slots) {
        if (!only_slot.empty() && slot.name != only_slot) {
            continue;
        }
        const std::string url = theme.style.is_object()
                                    ? theme.style.value(std::string(slot.url_key), std::string{})
                                    : std::string{};
        const std::string keep = url_owned_by(url, theme.id, slot.name)
                                     ? url.substr(k_theme_bg_url_prefix.size())
                                     : std::string{};
        prune_theme_owned_files(theme.id, slot.name, media_dir, keep);
    }
}

AssetSyncResult sync_theme_owned_assets(model::ThemeEntry& theme,
                                        const std::filesystem::path& media_dir)
{
    AssetSyncResult out;
    if (theme.id.empty() || media_dir.empty() || !theme.style.is_object()) {
        return out;
    }
    for (const auto& slot : k_slots) {
        auto url = util::trim(theme.style.value(std::string(slot.url_key), std::string{}));
        if (theme.style.value(std::string(slot.url_key), std::string{}) != url) {
            theme.style[std::string(slot.url_key)] = url;
            out.changed = true;
        }

        if (!url.empty() && needs_theme_asset_adopt(url, theme.id, slot.name)) {
            std::error_code ec;
            const auto src = static_url_media_path(url, media_dir);
            // A source that is not there leaves the URL as it was, still
            // pointing at the other theme's file.
            if (src && std::filesystem::is_regular_file(*src, ec)) {
                if (const auto filename = link_to_owned(*src, theme.id, slot.name, media_dir)) {
                    url = theme_bg_url(*filename);
                    theme.style[std::string(slot.url_key)] = url;
                    out.changed = true;
                } else {
                    out.copies.push_back({std::string(slot.name), url, *src});
                }
            }
        }
    }
    return out;
}

std::vector<std::string> shared_assets_not_here(const model::ThemeEntry& theme,
                                                const std::filesystem::path& media_dir)
{
    std::vector<std::string> out;
    if (theme.id.empty() || media_dir.empty() || !theme.style.is_object()) {
        return out;
    }
    constexpr std::string_view k_static = "/static/";
    for (const auto& slot : k_slots) {
        const auto url = util::trim(theme.style.value(std::string(slot.url_key), std::string{}));
        if (url.empty() || !needs_theme_asset_adopt(url, theme.id, slot.name)) {
            continue;
        }
        std::error_code ec;
        const auto src = static_url_media_path(url, media_dir);
        if (src && !std::filesystem::is_regular_file(*src, ec)) {
            out.push_back(url.substr(k_static.size()));
        }
    }
    return out;
}

} // namespace seventhslide::theme
