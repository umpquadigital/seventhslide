#pragma once

// Where each library's outputs are shown: screens, NDI names, capture cards,
// virtual cameras; and which of its outputs are switched off here.
//
// These are this machine's own: what they name is its hardware, and an output
// switched off at home stays on at church. So they live beside the library registry
// in the app data dir rather than in the library: a library folder copied to
// another machine does not bring this one's screens along, and opening one
// library never touches another's setup. Records are keyed by output id, which
// survives a rename. What a record holds is the output host's business; this
// only files them.
//
// Seam: plain C++20 + std::filesystem + nlohmann/json (no Qt).

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace seventhslide::library {

/// ``destinations.json`` inside the app data dir.
inline constexpr std::string_view k_destinations_filename = "destinations.json";

/// One library's destination records: output id -> array of records.
using OutputDestinations = std::map<std::string, nlohmann::json>;

class DestinationStore {
public:
    explicit DestinationStore(std::filesystem::path data_dir);

    [[nodiscard]] std::filesystem::path file() const;

    /// ``library_id``'s records. A missing or unreadable file reads as none.
    [[nodiscard]] OutputDestinations load(std::string_view library_id) const;

    /// Replace the records of every output named in ``outputs`` (an empty array
    /// clears one). The library's other outputs, and other libraries, keep
    /// theirs. False on I/O failure.
    bool save(std::string_view library_id, const OutputDestinations& outputs) const;

    /// Forget a deleted output's records, and that it was off.
    bool remove_output(std::string_view library_id, std::string_view output_id) const;

    /// The ids of ``library_id``'s outputs switched off on this machine.
    [[nodiscard]] std::set<std::string, std::less<>> outputs_off(std::string_view library_id) const;
    /// Switch ``library_id``'s output ``output_id`` on or off here. False on
    /// I/O failure.
    bool set_output_on(std::string_view library_id, std::string_view output_id, bool on) const;

    /// Forget a library removed from the registry, so a later library given the
    /// same id starts clean.
    bool remove_library(std::string_view library_id) const;

private:
    /// The file: ``libraries`` (library id -> output id -> records) and
    /// ``outputs_off`` (library id -> output ids), each an object.
    [[nodiscard]] nlohmann::json read() const;
    [[nodiscard]] bool write(const nlohmann::json& doc) const;

    std::filesystem::path data_dir_;
};

}  // namespace seventhslide::library
