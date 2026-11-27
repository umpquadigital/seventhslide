#pragma once

// How this computer's console is set up for each library: which of its stacks
// have a Preview.
//
// An operator at the front desk cues on a stack that the one in the booth
// sends straight live, so this is the computer's choice, not the library's.
// It lives beside the destinations and the Audio Mixer's levels in the app
// data dir, is never synced, and a library folder copied to another computer
// does not bring it along. Stacks are keyed by id, which survives a rename.
//
// Seam: plain C++20 + std::filesystem + nlohmann/json (no Qt).

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace seventhslide::library {

/// ``console.json`` inside the app data dir.
inline constexpr std::string_view k_console_filename = "console.json";

using StackIds = std::set<std::string, std::less<>>;

class ConsoleStore {
public:
    explicit ConsoleStore(std::filesystem::path data_dir);

    [[nodiscard]] std::filesystem::path file() const;

    /// The ids of ``library_id``'s stacks with a Preview here. A missing or
    /// unreadable file reads as none.
    [[nodiscard]] StackIds preview_stacks(std::string_view library_id) const;
    /// Give ``library_id``'s stack ``stack_id`` a Preview here, or take it
    /// away. False on I/O failure.
    bool set_stack_preview(std::string_view library_id, std::string_view stack_id, bool on) const;

    /// Forget what is kept for ``library_id``'s stacks other than ``stacks``:
    /// those deleted, here or on a computer the library syncs with. Writes
    /// only when something goes. False on I/O failure.
    bool keep_only_stacks(std::string_view library_id, const StackIds& stacks) const;

    /// Forget a library removed from the registry, so a later library given the
    /// same id starts clean.
    bool remove_library(std::string_view library_id) const;

private:
    /// The file's ``libraries``: library id -> ``{"preview_stacks": [ids]}``.
    [[nodiscard]] nlohmann::json read() const;
    [[nodiscard]] bool write(nlohmann::json libraries) const;
    /// Replace ``library_id``'s Preview stacks with ``ids``. False on I/O
    /// failure.
    [[nodiscard]] bool save_preview_stacks(std::string_view library_id, const StackIds& ids) const;

    std::filesystem::path data_dir_;
};

}  // namespace seventhslide::library
