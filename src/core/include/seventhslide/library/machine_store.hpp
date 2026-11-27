#pragma once

// What belongs to this computer rather than to any library: its id, and its
// settings (model::MachineSettings).
//
// Lives beside the library registry in the app data dir, like the output
// destinations, so a library copied (or, one day, synced) to another
// computer never brings this one's setup along. The id names this computer
// to the others a library is shared with; nothing reads it yet. What the
// settings object holds is the app's business; this only files it.
//
// Seam: plain C++20 + std::filesystem + nlohmann/json (no Qt).

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::library {

/// ``machine.json`` inside the app data dir.
inline constexpr std::string_view k_machine_filename = "machine.json";

class MachineStore {
public:
    explicit MachineStore(std::filesystem::path data_dir);

    [[nodiscard]] std::filesystem::path file() const;

    /// Read the file, and give this computer its id the first time, writing
    /// it at once. A missing or unreadable file reads as a computer never
    /// seen before.
    void load();

    /// 32 lower-case hex digits, made once per computer and kept. Empty
    /// before ``load``.
    [[nodiscard]] const std::string& machine_id() const noexcept { return machine_id_; }

    /// The settings object last saved, or ``std::nullopt`` when none ever
    /// was on this computer.
    [[nodiscard]] const std::optional<nlohmann::json>& settings() const noexcept
    {
        return settings_;
    }
    /// Replace the settings object. False on I/O failure, and then nothing
    /// changes.
    bool save_settings(nlohmann::json settings);

private:
    [[nodiscard]] bool write(const std::optional<nlohmann::json>& settings) const;

    std::filesystem::path data_dir_;
    std::string machine_id_;
    std::optional<nlohmann::json> settings_;
};

}  // namespace seventhslide::library
