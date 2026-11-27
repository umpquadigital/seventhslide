#include <seventhslide/model/output_library.hpp>

#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/geom/canvas_presets.hpp>
#include <seventhslide/util/find.hpp>
#include <seventhslide/util/hash.hpp>
#include <seventhslide/util/random_id.hpp>
#include <seventhslide/util/reorder.hpp>
#include <seventhslide/util/strings.hpp>
#include <seventhslide/util/unique_name.hpp>

#include <unordered_set>

namespace seventhslide::model {
namespace {

bool unsafe_name_char(unsigned char c)
{
    if (c < 0x20) {
        return true;
    }
    switch (c) {
    case '/':
    case '\\':
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
        return true;
    default:
        return false;
    }
}

/// Drop the spaces and dots at the end of ``name``.
void trim_end_space_dot(std::string& name)
{
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
}

} // namespace

std::string sanitize_output_name(std::string_view raw)
{
    std::string name;
    name.reserve(raw.size());
    for (char c : raw) {
        if (unsafe_name_char(static_cast<unsigned char>(c))) {
            continue;
        }
        name.push_back(c);
    }
    // Collapse ".."
    for (;;) {
        const auto pos = name.find("..");
        if (pos == std::string::npos) {
            break;
        }
        name.erase(pos, 2);
    }
    while (!name.empty() && (name.front() == ' ' || name.front() == '.')) {
        name.erase(name.begin());
    }
    trim_end_space_dot(name);
    if (name.size() > 64) {
        name.resize(64);
        trim_end_space_dot(name);
    }
    return name.empty() ? std::string("Output") : name;
}

std::string unique_output_name(std::string_view name,
                               const std::vector<OutputConfig>& outputs,
                               std::optional<std::size_t> exclude_index)
{
    std::string candidate = sanitize_output_name(name);
    // A taken name is numbered from its first 60 characters, so the number
    // still fits in 64. The cut keeps at least the first character, which the
    // sanitiser never leaves as a space or a dot.
    std::string base = candidate.substr(0, 60);
    trim_end_space_dot(base);
    return util::unique_name(std::move(candidate), base,
                             util::taken_names(outputs, &OutputConfig::name, exclude_index));
}

std::string new_output_id()
{
    return util::random_hex_id(1);
}

bool ensure_output_ids(std::vector<OutputConfig>& outputs)
{
    bool changed = false;
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        auto& oc = outputs[i];
        // From its place and the id it had, so the same list gets the same
        // ids each time it is put right.
        const auto had = oc.id;
        for (int attempt = 0; oc.id.empty() || seen.contains(oc.id); ++attempt) {
            oc.id = util::fnv1a64_hex("output " + std::to_string(i) + " "
                                      + std::to_string(attempt) + " " + had);
            changed = true;
        }
        seen.insert(oc.id);
    }
    return changed;
}

const OutputConfig* find_output_by_id(const std::vector<OutputConfig>& outputs,
                                      std::string_view id) noexcept
{
    return id.empty() ? nullptr : util::find_by(outputs, id, &OutputConfig::id);
}

const OutputConfig* find_output_by_name(const std::vector<OutputConfig>& outputs,
                                        std::string_view name) noexcept
{
    return util::find_by(outputs, name, &OutputConfig::name);
}

OutputConfig* find_output_by_name(std::vector<OutputConfig>& outputs,
                                  std::string_view name) noexcept
{
    return util::find_by(outputs, name, &OutputConfig::name);
}

geom::CanvasSize output_canvas(const OutputConfig& oc) noexcept
{
    return geom::canvas_or_reference(oc.canvas_width, oc.canvas_height);
}

geom::CanvasSize output_canvas(const std::vector<OutputConfig>& outputs,
                               std::string_view id) noexcept
{
    const auto* oc = find_output_by_id(outputs, id);
    return oc != nullptr ? output_canvas(*oc) : geom::k_reference_canvas;
}

std::optional<std::size_t> find_output_index(const std::vector<OutputConfig>& outputs,
                                             std::string_view name) noexcept
{
    const auto* oc = find_output_by_name(outputs, name);
    if (oc == nullptr) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(oc - outputs.data());
}

OutputGeneral output_general(const OutputConfig& oc)
{
    return {.name = oc.name,
            .canvas_width = oc.canvas_width,
            .canvas_height = oc.canvas_height,
            .enabled_stack_ids = oc.enabled_stack_ids,
            .disabled_content_types = oc.disabled_content_types,
            .exempt_from_global_blank = oc.exempt_from_global_blank,
            .exempt_from_global_freeze = oc.exempt_from_global_freeze,
            .icon = oc.icon,
            .audio_fades = oc.audio_fades};
}

OutputMutationResult update_output_settings(OutputConfig& oc,
                                            const std::vector<OutputConfig>& all,
                                            std::size_t index,
                                            OutputGeneral general)
{
    OutputMutationResult r;
    r.message = geom::canvas_size_problem({general.canvas_width, general.canvas_height});
    if (!r.message.empty()) {
        return r;
    }
    oc.name = unique_output_name(general.name, all, index);
    oc.canvas_width = general.canvas_width;
    oc.canvas_height = general.canvas_height;
    oc.enabled_stack_ids = std::move(general.enabled_stack_ids);
    oc.disabled_content_types = std::move(general.disabled_content_types);
    oc.exempt_from_global_blank = general.exempt_from_global_blank;
    oc.exempt_from_global_freeze = general.exempt_from_global_freeze;
    oc.icon = normalize_output_icon(general.icon);
    oc.audio_fades = AudioFades::from_json(general.audio_fades.to_json());
    r.ok = true;
    r.name = oc.name;
    r.id = oc.id;
    return r;
}

OutputMutationResult remove_output(std::vector<OutputConfig>& outputs, std::size_t index)
{
    OutputMutationResult r;
    if (index >= outputs.size()) {
        r.message = "Invalid output index";
        return r;
    }
    r.name = outputs[index].name;
    r.id = outputs[index].id;
    outputs.erase(outputs.begin() + static_cast<std::ptrdiff_t>(index));
    r.ok = true;
    return r;
}

OutputMutationResult reorder_outputs(std::vector<OutputConfig>& outputs,
                                     const std::vector<std::string>& names)
{
    // Names match without regard to case, as they are unique that way.
    std::vector<std::string> keys;
    keys.reserve(names.size());
    for (const auto& name : names) {
        keys.push_back(util::to_lower(name));
    }
    OutputMutationResult r;
    switch (util::reorder_by_key(outputs, keys,
                                 [](const OutputConfig& oc) { return util::to_lower(oc.name); })) {
    case util::ReorderError::None:
        r.ok = true;
        break;
    case util::ReorderError::WrongCount:
        r.message = "Name list must include every output";
        break;
    case util::ReorderError::DuplicateKey:
        r.message = "Duplicate name in order list";
        break;
    case util::ReorderError::UnknownKey:
        r.message = "Unknown output name";
        break;
    }
    return r;
}

} // namespace seventhslide::model
