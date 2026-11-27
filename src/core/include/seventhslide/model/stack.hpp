#pragma once

// App-wide Stacks: named live slots composited back-to-front onto outputs.

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::model {

inline constexpr std::string_view k_setting_stacks = "stacks";
inline constexpr std::string_view k_setting_stack_type_defaults = "stack_type_defaults";

inline constexpr std::string_view k_default_stack_id = "main";
inline constexpr std::string_view k_default_stack_name = "Main";
inline constexpr std::string_view k_default_stack_icon = "display";

/// The glyphs a stack may show (its tab, the Send to menu, Settings), in
/// picker order. Values are stable ids; the art is the app's glyph table.
inline constexpr std::string_view k_stack_icons[] = {
    "display", "titles",  "captions", "image",     "video", "song",
    "bible",   "quote",   "freeform", "textBox",   "info",  "clock",
    "timer",   "audio",   "mic",      "languages", "users", "church",
    "heart",   "star",    "globe",    "layers",    "target", "phone",
};

/// The content types a stack can be the default for. Every other item type
/// shares one of these (an image folder plays where images do; anything else
/// where songs do).
inline constexpr std::array<std::string_view, 8> k_stack_type_keys = {
    "song",
    "bible",
    "freeform",
    "image",
    "video",
    "audio",
    "web",
    "live",
};

struct StackDef {
    std::string id;
    std::string name;
    std::string icon{std::string(k_default_stack_icon)};

    [[nodiscard]] bool operator==(const StackDef&) const = default;

    [[nodiscard]] nlohmann::json to_json() const;
    static StackDef from_json(const nlohmann::json& j);
};

/// Content-type -> stack id. Empty / unknown ids are remapped by ``normalize_stacks``.
/// The stack each content type plays on unless something says otherwise.
class StackTypeDefaults {
public:
    /// Every type on the default stack.
    StackTypeDefaults();

    /// ``{"song": id, "bible": id, ...}``, one entry per ``k_stack_type_keys``.
    [[nodiscard]] nlohmann::json to_json() const;
    static StackTypeDefaults from_json(const nlohmann::json& j);

    [[nodiscard]] bool operator==(const StackTypeDefaults&) const = default;

    /// The stack id for ``item_type``, which may be any item type.
    [[nodiscard]] std::string& for_type(std::string_view item_type);
    [[nodiscard]] const std::string& for_type(std::string_view item_type) const;

    /// Every stack id, one per ``k_stack_type_keys``.
    [[nodiscard]] std::array<std::string, k_stack_type_keys.size()>& ids() noexcept { return ids_; }

private:
    std::array<std::string, k_stack_type_keys.size()> ids_;
};

[[nodiscard]] bool is_known_stack_icon(std::string_view icon) noexcept;
/// ``icon`` when it is one of ``k_stack_icons``, otherwise the default.
[[nodiscard]] std::string normalize_stack_icon(std::string_view icon);
[[nodiscard]] std::string new_stack_id();
[[nodiscard]] StackDef make_default_stack();

/// Always at least one stack. First-in-list is the back (paint order).
[[nodiscard]] std::vector<StackDef> normalize_stacks(std::vector<StackDef> stacks);

/// The stack a content type plays on when no other stack takes it: Main while
/// it exists, otherwise the front-most stack (the last in paint order).
[[nodiscard]] std::string fallback_stack_id(const std::vector<StackDef>& stacks);

/// Every type whose stack is gone goes to ``fallback_stack_id``.
[[nodiscard]] StackTypeDefaults normalize_type_defaults(StackTypeDefaults defaults,
                                                        const std::vector<StackDef>& stacks);


[[nodiscard]] const StackDef* find_stack(const std::vector<StackDef>& stacks,
                                         std::string_view id) noexcept;
[[nodiscard]] StackDef* find_stack(std::vector<StackDef>& stacks, std::string_view id) noexcept;

/// ``name`` trimmed and capped at 40 ("Stack" when that leaves nothing), or,
/// when another stack has it (ignoring case), that name + " 2", " 3", ....
/// The stack at ``exclude_index`` (one being renamed) keeps its own name.
[[nodiscard]] std::string unique_stack_name(std::string_view name,
                                            const std::vector<StackDef>& stacks,
                                            std::optional<std::size_t> exclude_index = std::nullopt);

struct StackMutationResult {
    bool ok = false;
    std::string message;
    std::string id;
};

[[nodiscard]] StackMutationResult add_stack(std::vector<StackDef>& stacks,
                                            std::string_view name_in = "Stack",
                                            std::string_view icon = k_default_stack_icon);

/// Blocks deleting the last stack. Remaps ``defaults`` ids that pointed at it.
[[nodiscard]] StackMutationResult remove_stack(std::vector<StackDef>& stacks,
                                               std::string_view id,
                                               StackTypeDefaults& defaults);

/// Reorder to match ``ids``, which must name every stack exactly once. Any
/// other list is refused and ``stacks`` is unchanged.
[[nodiscard]] StackMutationResult reorder_stacks(std::vector<StackDef>& stacks,
                                                 const std::vector<std::string>& ids);

[[nodiscard]] StackMutationResult rename_stack(std::vector<StackDef>& stacks,
                                               std::string_view id,
                                               std::string_view name_in);

[[nodiscard]] StackMutationResult set_stack_icon(std::vector<StackDef>& stacks,
                                                 std::string_view id,
                                                 std::string_view icon);

/// ``item_type`` no longer plays on stack ``id``: it goes to the fallback
/// stack. Refused for the fallback stack itself, which has nowhere to send it,
/// and for a type that is not on ``id``.
[[nodiscard]] StackMutationResult release_type_default(StackTypeDefaults& defaults,
                                                       const std::vector<StackDef>& stacks,
                                                       std::string_view item_type,
                                                       std::string_view id);

/// Caller supplies ``armed_send_to`` (library / ad-hoc) or leaves it empty
/// (service rundown plan: item override, then type default).
[[nodiscard]] std::string resolve_stack_id(const std::vector<StackDef>& stacks,
                                           const StackTypeDefaults& defaults,
                                           std::string_view armed_send_to,
                                           std::string_view item_override,
                                           std::string_view item_type);

/// Empty ``enabled_stack_ids`` means every stack is on (the default).
[[nodiscard]] bool output_enables_stack(const std::vector<std::string>& enabled_stack_ids,
                                        std::string_view stack_id) noexcept;

/// Whether an output leaving out ``disabled_content_types`` paints ``item_type``,
/// which may be any item type or scene mode (an image folder is an image).
/// Nothing is left out for having no content.
[[nodiscard]] bool output_enables_type(const std::vector<std::string>& disabled_content_types,
                                       std::string_view item_type);

void to_json(nlohmann::json& j, const StackDef& v);
void from_json(const nlohmann::json& j, StackDef& v);

} // namespace seventhslide::model
