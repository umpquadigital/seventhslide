#include "seventhslide/freeform/own_media.hpp"

#include "seventhslide/freeform/media_refs.hpp"
#include "seventhslide/library/library.hpp"
#include "seventhslide/model/static_media.hpp"
#include "seventhslide/util/file_copy.hpp"
#include "seventhslide/util/random_id.hpp"

#include <algorithm>
#include <map>

namespace seventhslide::freeform {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t k_tag_digits = 8;
/// The bytes of a title a name keeps, so the name stays well inside what
/// every filesystem takes.
constexpr std::size_t k_title_bytes = 80;

[[nodiscard]] std::string own_prefix()
{
    return "/static/" + std::string(model::k_freeform_media_subdir) + "/";
}

/// ASCII only, whatever the locale: a name must read the same on every
/// computer, and ``std::isalnum`` takes Latin-1 bytes for letters in some.
[[nodiscard]] constexpr bool is_ascii_alnum(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

[[nodiscard]] constexpr bool is_ascii_hex(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] bool is_part_char(char c) noexcept
{
    return is_ascii_alnum(c) || c == '.' || c == '_' || c == '-';
}

[[nodiscard]] std::string lower(std::string_view s)
{
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return out;
}

/// ``extension`` as a name ends: lower case, with its dot, safe; empty for
/// one that is not.
[[nodiscard]] std::string clean_extension(std::string_view extension)
{
    if (extension.starts_with('.')) {
        extension.remove_prefix(1);
    }
    if (extension.empty() || !std::ranges::all_of(extension, is_ascii_alnum)) {
        return {};
    }
    return "." + lower(extension);
}

/// A file still being written: an import's staging copy, a write's temp
/// file, or a file sync is still fetching (``<name>.part``). No document
/// names one yet, and none is the cleanup's to take.
[[nodiscard]] bool is_in_progress(std::string_view name) noexcept
{
    return (name.size() > library::k_staging_prefix.size() + library::k_staging_suffix.size()
            && name.starts_with(library::k_staging_prefix) && name.ends_with(library::k_staging_suffix))
           || name.ends_with(".part") || name.ends_with(".tmp");
}

/// ``from`` at ``to``: a hard link, or a copy where there are none. A copy is
/// made under a staging name beside ``to`` and only then named, so a copy cut
/// short (a crash, a full disk) never sits under the name of the whole file.
[[nodiscard]] bool link_or_copy(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    fs::create_hard_link(from, to, ec);
    if (!ec) {
        return true;
    }
    const auto staging = to.parent_path()
                         / (std::string(library::k_staging_prefix) + util::random_hex_id(1)
                            + std::string(library::k_staging_suffix));
    if (util::copy_file_chunked(from, staging, [](std::uintmax_t) { return true; })
        != util::CopyOutcome::Copied) {
        fs::remove(staging, ec);
        return false;
    }
    fs::rename(staging, to, ec);
    if (ec) {
        fs::remove(staging, ec);
        return false;
    }
    return true;
}

[[nodiscard]] std::string fallback_title(canvas::MediaRefKind kind)
{
    return kind == canvas::MediaRefKind::Video ? "Video" : "Picture";
}

}  // namespace

bool is_own_media_part(std::string_view part) noexcept
{
    return !part.empty() && part != "." && part != ".." && std::ranges::all_of(part, is_part_char);
}

std::optional<OwnMedia> own_media_at(std::string_view place)
{
    const auto slash = place.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    OwnMedia file{std::string(place.substr(0, slash)), std::string(place.substr(slash + 1))};
    if (!is_own_media_part(file.owner) || !is_own_media_part(file.name)) {
        return std::nullopt;
    }
    return file;
}

std::optional<OwnMedia> own_media_of(std::string_view ref)
{
    const auto prefix = own_prefix();
    if (!ref.starts_with(prefix)) {
        return std::nullopt;
    }
    return own_media_at(ref.substr(prefix.size()));
}

std::string own_media_url(const OwnMedia& file)
{
    return own_prefix() + file.place();
}

std::string new_own_media_name(std::string_view title, std::string_view extension)
{
    const auto ext = clean_extension(extension);
    // The title without its own extension, when it has the file's.
    if (!ext.empty() && title.size() > ext.size()
        && lower(title.substr(title.size() - ext.size())) == ext) {
        title.remove_suffix(ext.size());
    }
    std::string safe;
    for (const char c : title) {
        if (safe.size() >= k_title_bytes) {
            break;
        }
        const bool keep = is_ascii_alnum(c) || c == '-';
        // Anything else is a gap, one however many there are in a row.
        if (keep) {
            safe.push_back(c);
        } else if (!safe.empty() && safe.back() != '_') {
            safe.push_back('_');
        }
    }
    while (!safe.empty() && safe.back() == '_') {
        safe.pop_back();
    }
    std::string name = util::random_hex_id(1).substr(0, k_tag_digits);
    if (!safe.empty()) {
        name += "-" + safe;
    }
    return name + ext;
}

std::string own_media_title(std::string_view name)
{
    // "<8 hex>-<title>.<ext>" or "<8 hex>.<ext>"; a name not made here is
    // shown as it is.
    if (name.size() <= k_tag_digits
        || !std::all_of(name.begin(), name.begin() + k_tag_digits,
                        is_ascii_hex)) {
        return std::string(name);
    }
    auto rest = name.substr(k_tag_digits);
    if (rest.starts_with('-')) {
        rest.remove_prefix(1);
    }
    std::string title(rest);
    std::ranges::replace(title, '_', ' ');
    return title;
}

std::string new_own_media_owner()
{
    return util::random_hex_id(2);
}

fs::path own_media_dir(const fs::path& media_root, std::string_view owner)
{
    return media_root / std::string(model::k_freeform_media_subdir) / std::string(owner);
}

fs::path own_media_path(const fs::path& media_root, const OwnMedia& file)
{
    return own_media_dir(media_root, file.owner) / file.name;
}

fs::path media_ref_file(const fs::path& media_root, std::string_view ref, canvas::MediaRefKind kind)
{
    if (media_root.empty() || ref.empty()) {
        return {};
    }
    fs::path path;
    if (const auto own = own_media_of(ref)) {
        path = own_media_path(media_root, *own);
    } else if (ref.starts_with("/static/")) {
        // A library file or a theme's background by URL: one folder, one name.
        const auto rest = ref.substr(std::string_view("/static/").size());
        const auto slash = rest.find('/');
        if (slash == std::string_view::npos) {
            return {};
        }
        const auto folder = rest.substr(0, slash);
        const auto name = rest.substr(slash + 1);
        if (name.empty() || name == "." || name == ".."
            || name.find_first_of("/\\") != std::string_view::npos) {
            return {};
        }
        if (folder != model::k_images_subdir && folder != model::k_videos_subdir
            && folder != model::k_theme_backgrounds_subdir) {
            return {};
        }
        path = media_root / std::string(folder) / std::string(name);
    } else {
        const auto name = model::media_basename(ref);
        if (name.empty() || name == "." || name == "..") {
            return {};
        }
        const auto folder =
            kind == canvas::MediaRefKind::Video ? model::k_videos_subdir : model::k_images_subdir;
        path = media_root / std::string(folder) / std::string(name);
    }
    std::error_code ec;
    return fs::is_regular_file(path, ec) ? path : fs::path();
}

TakenMedia take_own_media(Document& doc, std::string_view owner, const fs::path& media_root,
                          const MediaTitle& title_of)
{
    TakenMedia out;
    if (media_root.empty() || !is_own_media_part(owner)) {
        return out;
    }
    const auto dir = own_media_dir(media_root, owner);
    // Each file once, however often it is named.
    std::map<std::string, std::string, std::less<>> taken;
    visit_document_media(doc, [&](std::string& ref, canvas::MediaRefKind kind) {
        if (ref.empty()) {
            return;
        }
        const auto own = own_media_of(ref);
        if (own && own->owner == owner) {
            return;
        }
        if (const auto it = taken.find(ref); it != taken.end()) {
            ref = it->second;
            return;
        }
        const auto from = media_ref_file(media_root, ref, kind);
        if (from.empty()) {
            ++out.missing;
            return;
        }
        std::string name;
        if (own) {
            name = own->name;
        } else {
            auto title = title_of ? title_of(ref, kind) : std::string();
            if (title.empty()) {
                title = fallback_title(kind);
            }
            name = new_own_media_name(title, from.extension().string());
        }
        std::error_code ec;
        fs::create_directories(dir, ec);
        const OwnMedia mine{std::string(owner), name};
        const auto to = own_media_path(media_root, mine);
        // A file of another's already taken (the same bytes, by its name),
        // or a new name, which is never one already here.
        if (!fs::exists(to, ec) && !link_or_copy(from, to)) {
            ++out.failed;
            return;
        }
        const auto url = own_media_url(mine);
        taken.emplace(ref, url);
        ref = url;
        ++out.taken;
    });
    return out;
}

void note_own_media(const nlohmann::json& doc, std::set<std::string, std::less<>>& out)
{
    visit_document_media(doc, [&out](const nlohmann::json& path, canvas::MediaRefKind) {
        if (const auto own = own_media_of(path.get_ref<const std::string&>())) {
            out.insert(own->place());
        }
    });
}

int remove_unnamed_own_media(const fs::path& media_root, std::string_view owner,
                             const std::set<std::string, std::less<>>& named)
{
    if (media_root.empty() || !is_own_media_part(owner)) {
        return 0;
    }
    const auto dir = own_media_dir(media_root, owner);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return 0;
    }
    int removed = 0;
    const std::string place_prefix = std::string(owner) + "/";
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (is_in_progress(name) || named.contains(place_prefix + name)) {
            continue;
        }
        std::error_code remove_ec;
        if (it->is_regular_file(remove_ec) && fs::remove(it->path(), remove_ec)) {
            ++removed;
        }
    }
    // Gone once nothing is in it; a folder still holding something stays.
    fs::remove(dir, ec);
    return removed;
}

}  // namespace seventhslide::freeform
