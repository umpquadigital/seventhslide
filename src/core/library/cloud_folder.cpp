#include "seventhslide/library/cloud_folder.hpp"

#include "seventhslide/util/strings.hpp"

#include <array>
#include <string_view>
#include <system_error>

namespace seventhslide::library {
namespace {

namespace fs = std::filesystem;

struct NameRule {
    /// Compared without regard to case.
    std::string_view name;
    /// Match ``name`` as a prefix rather than the whole folder name.
    bool prefix;
    std::string_view service;
};

/// Folder names the services' desktop apps sync, on any platform.
constexpr std::array k_name_rules = {
    NameRule{"Dropbox", false, "Dropbox"},
    NameRule{"Dropbox (", true, "Dropbox"},  // a team's: "Dropbox (Grace Church)"
    NameRule{"Google Drive", false, "Google Drive"},
    NameRule{"GoogleDrive", true, "Google Drive"},  // macOS: "GoogleDrive-<account>"
    NameRule{"My Drive", false, "Google Drive"},    // Windows: G:\My Drive
    NameRule{"Shared drives", false, "Google Drive"},
    NameRule{"google-drive:", true, "Google Drive"},  // GNOME Online Accounts
    NameRule{"OneDrive", true, "OneDrive"},  // "OneDrive - Grace Church", "OneDrive-Personal"
    NameRule{"iCloud Drive", false, "iCloud Drive"},
    NameRule{"iCloudDrive", false, "iCloud Drive"},     // Windows
    NameRule{"Mobile Documents", false, "iCloud Drive"},  // macOS: ~/Library/Mobile Documents
    NameRule{"Nextcloud", false, "Nextcloud"},
    NameRule{"ownCloud", false, "ownCloud"},
    NameRule{"pCloud", true, "pCloud"},  // "pCloud Drive", "pCloudDrive"
    NameRule{"Box Sync", false, "Box"},
};

struct MarkerRule {
    /// A file or folder the service's app keeps in the root of what it syncs.
    std::string_view marker;
    std::string_view service;
};

constexpr std::array k_marker_rules = {
    MarkerRule{".dropbox", "Dropbox"},
    MarkerRule{".stfolder", "Syncthing"},
};

[[nodiscard]] std::string_view service_named(std::string_view folder_name)
{
    for (const auto& rule : k_name_rules) {
        if (rule.prefix ? util::starts_with_ci(folder_name, rule.name)
                        : util::iequals(folder_name, rule.name)) {
            return rule.service;
        }
    }
    return {};
}

/// macOS keeps every File Provider service under ~/Library/CloudStorage, one
/// folder each, named "<Service>-<account>". One we have no rule for is
/// still named by the part before the dash.
[[nodiscard]] std::string file_provider_service(std::string_view folder_name)
{
    if (const auto known = service_named(folder_name); !known.empty()) {
        return std::string(known);
    }
    const auto dash = folder_name.find('-');
    return std::string(dash == std::string_view::npos || dash == 0 ? folder_name
                                                                    : folder_name.substr(0, dash));
}

[[nodiscard]] std::string_view service_marked(const fs::path& dir)
{
    for (const auto& rule : k_marker_rules) {
        std::error_code ec;
        if (fs::exists(dir / rule.marker, ec)) {
            return rule.service;
        }
    }
    return {};
}

}  // namespace

std::string cloud_sync_service(const fs::path& folder)
{
    std::error_code ec;
    fs::path absolute = fs::absolute(folder, ec);
    if (ec) {
        absolute = folder;
    }
    absolute = absolute.lexically_normal();

    fs::path at = absolute.root_path();
    std::string previous;
    std::string before_previous;
    for (const auto& part : absolute.relative_path()) {
        const std::string name = part.string();
        if (name.empty()) {
            continue;  // the trailing separator of "dir/"
        }
        at /= part;
        if (util::iequals(previous, "CloudStorage") && util::iequals(before_previous, "Library")) {
            return file_provider_service(name);
        }
        if (const auto named = service_named(name); !named.empty()) {
            return std::string(named);
        }
        if (const auto marked = service_marked(at); !marked.empty()) {
            return std::string(marked);
        }
        before_previous = std::move(previous);
        previous = name;
    }
    return {};
}

}  // namespace seventhslide::library
