#pragma once

// The open library's settings, persisted in its ``app_settings`` table.
//
// They travel with the library: every computer that opens it reads the same
// ones. A setting that describes one computer instead (its network, its
// hardware) does not belong here but in MachineSettings, which stays on the
// computer. A new setting picks one of the two.
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include "seventhslide/model/stack.hpp"
#include "seventhslide/model/theme_keys.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::model {

/// The church's or organization's name. Nothing shows it yet; it is kept for
/// the slide editors to insert.
inline constexpr std::string_view k_setting_organization_name = "organization_name";
inline constexpr std::string_view k_setting_ccli_licence = "ccli_licence_number";
inline constexpr std::string_view k_setting_theme_priority = "theme_priority";

struct AppSettings {
    std::string organization_name;
    std::string ccli_licence_number;
    std::vector<std::string> theme_priority = normalize_theme_priority({});
    std::vector<StackDef> stacks = normalize_stacks({});
    StackTypeDefaults stack_type_defaults{};

    /// Setting key -> JSON value, the shape ``ConfigRepository::save_settings``
    /// stores and ``load_settings`` returns.
    [[nodiscard]] nlohmann::json to_json() const;
    static AppSettings from_json(const nlohmann::json& j);
};

} // namespace seventhslide::model
