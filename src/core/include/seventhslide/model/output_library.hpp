#pragma once

// Output library helpers - name sanitize/unique, structural CRUD
// helpers for OutputConfig lists. Plain C++20.

#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/model/output_config.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::model {

/// A fresh random output id.
[[nodiscard]] std::string new_output_id();

/// Give every output a non-empty id that no other output uses: an output with
/// none, or with one already taken earlier in the list, gets a new one. The
/// new id comes from the output's place in the list and the id it had, so a
/// list put right again gets the same ids: a library this computer only views
/// is put right on every open and never written, and an editor that writes it
/// gives the ids the viewer used. Returns true when any id changed (the caller
/// persists the list).
bool ensure_output_ids(std::vector<OutputConfig>& outputs);

/// Output with ``id``, or null.
[[nodiscard]] const OutputConfig* find_output_by_id(const std::vector<OutputConfig>& outputs,
                                                    std::string_view id) noexcept;

/// Output named ``name`` (exact match), or null. Names are display-only; use
/// this only to translate a name from the UI or the remote into an output.
[[nodiscard]] const OutputConfig* find_output_by_name(const std::vector<OutputConfig>& outputs,
                                                      std::string_view name) noexcept;
[[nodiscard]] OutputConfig* find_output_by_name(std::vector<OutputConfig>& outputs,
                                                std::string_view name) noexcept;

/// ``oc``'s canvas, or the reference canvas when it has no valid size.
[[nodiscard]] geom::CanvasSize output_canvas(const OutputConfig& oc) noexcept;
/// Canvas of output ``id``, or the reference canvas when there is no such
/// output or it has no valid size.
[[nodiscard]] geom::CanvasSize output_canvas(const std::vector<OutputConfig>& outputs,
                                             std::string_view id) noexcept;

/// Position of the output named ``name``, or nullopt.
[[nodiscard]] std::optional<std::size_t> find_output_index(
    const std::vector<OutputConfig>& outputs,
    std::string_view name) noexcept;

/// Strip path/unsafe filename chars; trim; cap 64; fallback ``Output``.
[[nodiscard]] std::string sanitize_output_name(std::string_view raw);

/// ``name`` sanitised, or, when another output has it (ignoring case), its
/// first 60 characters (less any spaces and dots that leaves at the end) +
/// " 2", " 3", ..., so the result still fits in 64. The output at
/// ``exclude_index`` (one being renamed) keeps its own name.
[[nodiscard]] std::string unique_output_name(std::string_view name,
                                            const std::vector<OutputConfig>& outputs,
                                            std::optional<std::size_t> exclude_index = std::nullopt);

struct OutputMutationResult {
    bool ok = false;
    std::string message;
    std::string name; // resulting / created name
    std::string id;   // id of the created / updated / removed output
};

/// An output's own settings: what the Output window's General page edits.
struct OutputGeneral {
    std::string name;
    int canvas_width = geom::k_reference_canvas.width;
    int canvas_height = geom::k_reference_canvas.height;
    /// Empty = every stack shows on this output.
    std::vector<std::string> enabled_stack_ids;
    /// Empty = every content type shows on this output.
    std::vector<std::string> disabled_content_types;
    bool exempt_from_global_blank = false;
    bool exempt_from_global_freeze = false;
    std::string icon{k_default_output_icon};
    AudioFades audio_fades{};
};

/// ``oc``'s general settings, to change and pass back to update_output_settings.
[[nodiscard]] OutputGeneral output_general(const OutputConfig& oc);

/// Apply ``general`` to ``oc`` (at ``index`` in ``all``), leaving its themes.
/// The name is sanitised and made unique; a canvas out of range is refused and
/// changes nothing; an unknown icon becomes the default.
[[nodiscard]] OutputMutationResult update_output_settings(OutputConfig& oc,
                                                          const std::vector<OutputConfig>& all,
                                                          std::size_t index,
                                                          OutputGeneral general);

/// Remove by index, the last one too: a library may have no outputs. Caller
/// deletes owned theme assets.
[[nodiscard]] OutputMutationResult remove_output(std::vector<OutputConfig>& outputs,
                                                 std::size_t index);

/// Reorder to match ``names``, which must name every output exactly once
/// (case-insensitive). Any other list is refused and ``outputs`` is unchanged.
[[nodiscard]] OutputMutationResult reorder_outputs(std::vector<OutputConfig>& outputs,
                                                   const std::vector<std::string>& names);

} // namespace seventhslide::model
