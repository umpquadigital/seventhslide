#include "seventhslide/library/machine_store.hpp"

#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/random_id.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

namespace seventhslide::library {
namespace {

constexpr int k_store_version = 1;

[[nodiscard]] bool is_machine_id(const std::string& id)
{
    return id.size() == 32 && std::ranges::all_of(id, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

}  // namespace

MachineStore::MachineStore(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir))
{
}

std::filesystem::path MachineStore::file() const
{
    return data_dir_ / std::string{k_machine_filename};
}

void MachineStore::load()
{
    machine_id_.clear();
    settings_.reset();

    if (std::ifstream in(file()); in) {
        const auto doc = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (doc.is_object()) {
            if (const auto it = doc.find("machine_id"); it != doc.end() && it->is_string()) {
                machine_id_ = it->get<std::string>();
            }
            if (const auto it = doc.find("settings"); it != doc.end() && it->is_object()) {
                settings_ = *it;
            }
        }
    }

    if (!is_machine_id(machine_id_)) {
        machine_id_ = util::random_hex_id(2);
        // Unwritten, the id lasts this run and the next run makes another;
        // nothing has been told this one yet, so that loses nothing.
        (void)write(settings_);
    }
}

bool MachineStore::save_settings(nlohmann::json settings)
{
    std::optional<nlohmann::json> next = std::move(settings);
    if (!write(next)) {
        return false;
    }
    settings_ = std::move(next);
    return true;
}

bool MachineStore::write(const std::optional<nlohmann::json>& settings) const
{
    nlohmann::json doc{{"version", k_store_version}, {"machine_id", machine_id_}};
    if (settings) {
        doc["settings"] = *settings;
    }
    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    return util::write_file_atomically(file(), doc.dump(2) + '\n');
}

}  // namespace seventhslide::library
