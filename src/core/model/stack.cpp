#include <seventhslide/model/stack.hpp>

#include "seventhslide/model/content.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/util/find.hpp"
#include "seventhslide/util/random_id.hpp"
#include "seventhslide/util/reorder.hpp"
#include "seventhslide/util/strings.hpp"
#include "seventhslide/util/unique_name.hpp"

#include <algorithm>
#include <iterator>
#include <utility>
#include <unordered_set>

namespace seventhslide::model {
namespace {
using seventhslide::json::str_or;


std::string trim_name(std::string_view raw)
{
    std::string name = util::trim(raw);
    if (name.size() > 40) {
        name = util::trim(std::string_view(name).substr(0, 40));
    }
    return name;
}

} // namespace

bool is_known_stack_icon(std::string_view icon) noexcept
{
    return std::ranges::find(k_stack_icons, icon) != std::end(k_stack_icons);
}

std::string normalize_stack_icon(std::string_view icon)
{
    return std::string(is_known_stack_icon(icon) ? icon : k_default_stack_icon);
}

std::string new_stack_id()
{
    return util::random_hex_id(1);
}

StackDef make_default_stack()
{
    StackDef s;
    s.id = std::string(k_default_stack_id);
    s.name = std::string(k_default_stack_name);
    s.icon = std::string(k_default_stack_icon);
    return s;
}

nlohmann::json StackDef::to_json() const
{
    return {
        {"id", id},
        {"name", name},
        {"icon", icon},
    };
}

StackDef StackDef::from_json(const nlohmann::json& j)
{
    StackDef s;
    if (!j.is_object()) {
        return s;
    }
    s.id = str_or(j, "id");
    s.name = str_or(j, "name");
    s.icon = normalize_stack_icon(str_or(j, "icon", k_default_stack_icon));
    return s;
}

namespace {

/// Index of ``key`` in ``k_stack_type_keys``. Compile-time only, so a key that
/// is not in the list is a build error rather than an index past the end.
consteval std::size_t slot_of(std::string_view key)
{
    for (std::size_t i = 0; i < k_stack_type_keys.size(); ++i) {
        if (k_stack_type_keys[i] == key) {
            return i;
        }
    }
    throw "not a stack type key";
}

/// Index into ``k_stack_type_keys`` of the type ``item_type`` plays as.
[[nodiscard]] std::size_t type_slot(std::string_view item_type)
{
    switch (content_kind_from_string(item_type)) {
    case ContentKind::Bible:
        return slot_of("bible");
    case ContentKind::Freeform:
        return slot_of("freeform");
    case ContentKind::Image:
    case ContentKind::ImageFolder:
        return slot_of("image");
    case ContentKind::Video:
        return slot_of("video");
    case ContentKind::Audio:
        return slot_of("audio");
    case ContentKind::Web:
        return slot_of("web");
    case ContentKind::Live:
        return slot_of("live");
    case ContentKind::Song:
    case ContentKind::Divider:
    case ContentKind::Empty:
        break;
    }
    return slot_of("song");
}

}  // namespace

StackTypeDefaults::StackTypeDefaults()
{
    ids_.fill(std::string(k_default_stack_id));
}

nlohmann::json StackTypeDefaults::to_json() const
{
    nlohmann::json j = nlohmann::json::object();
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        j[std::string(k_stack_type_keys[i])] = ids_[i];
    }
    return j;
}

StackTypeDefaults StackTypeDefaults::from_json(const nlohmann::json& j)
{
    StackTypeDefaults d;
    if (!j.is_object()) {
        return d;
    }
    for (std::size_t i = 0; i < d.ids_.size(); ++i) {
        d.ids_[i] = str_or(j, k_stack_type_keys[i].data(), k_default_stack_id);
    }
    // Web pages came after the others: until a stack is chosen for them they
    // play where videos do.
    if (!j.contains("web")) {
        d.ids_[slot_of("web")] = d.ids_[slot_of("video")];
    }
    // Live sources too.
    if (!j.contains("live")) {
        d.ids_[slot_of("live")] = d.ids_[slot_of("video")];
    }
    return d;
}

std::string& StackTypeDefaults::for_type(std::string_view item_type)
{
    return ids_[type_slot(item_type)];
}

const std::string& StackTypeDefaults::for_type(std::string_view item_type) const
{
    return ids_[type_slot(item_type)];
}

const StackDef* find_stack(const std::vector<StackDef>& stacks, std::string_view id) noexcept
{
    return util::find_by(stacks, id, &StackDef::id);
}

StackDef* find_stack(std::vector<StackDef>& stacks, std::string_view id) noexcept
{
    return util::find_by(stacks, id, &StackDef::id);
}

std::string unique_stack_name(std::string_view name,
                              const std::vector<StackDef>& stacks,
                              std::optional<std::size_t> exclude_index)
{
    std::string candidate = trim_name(name);
    if (candidate.empty()) {
        candidate = "Stack";
    }
    const std::string base = candidate;
    return util::unique_name(std::move(candidate), base,
                             util::taken_names(stacks, &StackDef::name, exclude_index));
}

std::vector<StackDef> normalize_stacks(std::vector<StackDef> stacks)
{
    std::vector<StackDef> out;
    std::unordered_set<std::string> seen_ids;
    out.reserve(stacks.size());
    for (auto& s : stacks) {
        s.id = trim_name(s.id);
        s.name = trim_name(s.name);
        s.icon = normalize_stack_icon(s.icon);
        if (s.id.empty()) {
            s.id = new_stack_id();
        }
        if (seen_ids.count(s.id)) {
            s.id = new_stack_id();
        }
        seen_ids.insert(s.id);
        if (s.name.empty()) {
            s.name = s.id == k_default_stack_id ? std::string(k_default_stack_name) : "Stack";
        }
        s.name = unique_stack_name(s.name, out);
        out.push_back(std::move(s));
    }
    if (out.empty()) {
        out.push_back(make_default_stack());
    }
    return out;
}

std::string fallback_stack_id(const std::vector<StackDef>& stacks)
{
    if (stacks.empty() || find_stack(stacks, k_default_stack_id) != nullptr) {
        return std::string(k_default_stack_id);
    }
    return stacks.back().id;
}

StackTypeDefaults normalize_type_defaults(StackTypeDefaults defaults,
                                          const std::vector<StackDef>& stacks)
{
    const std::string fallback = fallback_stack_id(stacks);
    for (auto& id : defaults.ids()) {
        if (!find_stack(stacks, id)) {
            id = fallback;
        }
    }
    return defaults;
}

StackMutationResult add_stack(std::vector<StackDef>& stacks,
                              std::string_view name_in,
                              std::string_view icon)
{
    StackMutationResult r;
    StackDef s;
    s.id = new_stack_id();
    s.name = unique_stack_name(name_in, stacks);
    s.icon = normalize_stack_icon(icon);
    stacks.push_back(std::move(s));
    r.ok = true;
    r.id = stacks.back().id;
    return r;
}

StackMutationResult remove_stack(std::vector<StackDef>& stacks,
                                 std::string_view id,
                                 StackTypeDefaults& defaults)
{
    StackMutationResult r;
    if (stacks.size() <= 1) {
        r.message = "Cannot delete the last stack";
        return r;
    }
    auto it = std::find_if(stacks.begin(), stacks.end(), [&](const StackDef& s) {
        return s.id == id;
    });
    if (it == stacks.end()) {
        r.message = "Unknown stack";
        return r;
    }
    r.id = it->id;
    stacks.erase(it);
    defaults = normalize_type_defaults(std::move(defaults), stacks);
    r.ok = true;
    return r;
}

StackMutationResult release_type_default(StackTypeDefaults& defaults,
                                         const std::vector<StackDef>& stacks,
                                         std::string_view item_type,
                                         std::string_view id)
{
    StackMutationResult r;
    if (std::ranges::find(k_stack_type_keys, item_type) == k_stack_type_keys.end()) {
        r.message = "Unknown content type";
        return r;
    }
    std::string& on = defaults.for_type(item_type);
    if (on != id) {
        r.message = "That content type does not play on this stack";
        return r;
    }
    const std::string fallback = fallback_stack_id(stacks);
    if (fallback == id) {
        r.message = "Everything not sent to another stack plays here";
        return r;
    }
    on = fallback;
    r.ok = true;
    r.id = fallback;
    return r;
}

StackMutationResult reorder_stacks(std::vector<StackDef>& stacks,
                                   const std::vector<std::string>& ids)
{
    StackMutationResult r;
    switch (util::reorder_by_key(stacks, ids, [](const StackDef& s) { return s.id; })) {
    case util::ReorderError::None:
        r.ok = true;
        break;
    case util::ReorderError::WrongCount:
        r.message = "Id list must include every stack";
        break;
    case util::ReorderError::DuplicateKey:
        r.message = "Duplicate stack id";
        break;
    case util::ReorderError::UnknownKey:
        r.message = "Unknown stack";
        break;
    }
    return r;
}

StackMutationResult rename_stack(std::vector<StackDef>& stacks,
                                 std::string_view id,
                                 std::string_view name_in)
{
    StackMutationResult r;
    for (std::size_t i = 0; i < stacks.size(); ++i) {
        if (stacks[i].id != id) {
            continue;
        }
        stacks[i].name = unique_stack_name(name_in, stacks, i);
        r.ok = true;
        r.id = stacks[i].id;
        return r;
    }
    r.message = "Unknown stack";
    return r;
}

StackMutationResult set_stack_icon(std::vector<StackDef>& stacks,
                                   std::string_view id,
                                   std::string_view icon)
{
    StackMutationResult r;
    auto* s = find_stack(stacks, id);
    if (!s) {
        r.message = "Unknown stack";
        return r;
    }
    s->icon = normalize_stack_icon(icon);
    r.ok = true;
    r.id = s->id;
    return r;
}

std::string resolve_stack_id(const std::vector<StackDef>& stacks,
                             const StackTypeDefaults& defaults,
                             std::string_view armed_send_to,
                             std::string_view item_override,
                             std::string_view item_type)
{
    const std::string fallback =
        stacks.empty() ? std::string(k_default_stack_id) : stacks.back().id;
    auto pick = [&](std::string_view id) -> std::string {
        if (!id.empty() && find_stack(stacks, id)) {
            return std::string(id);
        }
        return {};
    };
    if (auto id = pick(armed_send_to); !id.empty()) {
        return id;
    }
    if (auto id = pick(item_override); !id.empty()) {
        return id;
    }
    if (auto id = pick(defaults.for_type(item_type)); !id.empty()) {
        return id;
    }
    return fallback;
}

bool output_enables_stack(const std::vector<std::string>& enabled_stack_ids,
                          std::string_view stack_id) noexcept
{
    if (enabled_stack_ids.empty()) {
        return true;
    }
    return std::find(enabled_stack_ids.begin(), enabled_stack_ids.end(), stack_id)
        != enabled_stack_ids.end();
}

bool output_enables_type(const std::vector<std::string>& disabled_content_types,
                         std::string_view item_type)
{
    const auto kind = content_kind_from_string(item_type);
    if (kind == ContentKind::Empty || kind == ContentKind::Divider) {
        return true;
    }
    return std::find(disabled_content_types.begin(), disabled_content_types.end(),
                     k_stack_type_keys[type_slot(item_type)])
        == disabled_content_types.end();
}

void to_json(nlohmann::json& j, const StackDef& v) { j = v.to_json(); }
void from_json(const nlohmann::json& j, StackDef& v) { v = StackDef::from_json(j); }

} // namespace seventhslide::model
