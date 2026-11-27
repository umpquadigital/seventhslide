#include <seventhslide/model/app_settings.hpp>

#include "seventhslide/json/read.hpp"
#include "seventhslide/util/strings.hpp"

namespace seventhslide::model {

// The JSON object is also the ``app_settings`` table: one row per top-level
// key, which is why the keys are the ``k_setting_*`` names.

nlohmann::json AppSettings::to_json() const
{
    nlohmann::json j = nlohmann::json::object();
    j[std::string(k_setting_organization_name)] = organization_name;
    j[std::string(k_setting_ccli_licence)] = ccli_licence_number;
    j[std::string(k_setting_theme_priority)] = theme_priority;
    j[std::string(k_setting_stacks)] = stacks;
    j[std::string(k_setting_stack_type_defaults)] = stack_type_defaults.to_json();
    return j;
}

AppSettings AppSettings::from_json(const nlohmann::json& j)
{
    AppSettings s;
    if (!j.is_object()) {
        return s;
    }
    // The constants name string literals, so ``data()`` is NUL-terminated.
    s.organization_name = util::trim(json::str_or(j, k_setting_organization_name.data()));
    s.ccli_licence_number = util::trim(json::str_or(j, k_setting_ccli_licence.data()));
    s.theme_priority =
        normalize_theme_priority(json::strings_or_empty(j, k_setting_theme_priority.data()));
    if (const auto* stacks = json::find(j, k_setting_stacks.data());
        stacks != nullptr && stacks->is_array()) {
        s.stacks = stacks->get<std::vector<StackDef>>();
    }
    s.stacks = normalize_stacks(std::move(s.stacks));
    if (const auto* defaults = json::find(j, k_setting_stack_type_defaults.data());
        defaults != nullptr && defaults->is_object()) {
        s.stack_type_defaults = StackTypeDefaults::from_json(*defaults);
    }
    s.stack_type_defaults = normalize_type_defaults(s.stack_type_defaults, s.stacks);
    return s;
}

} // namespace seventhslide::model
