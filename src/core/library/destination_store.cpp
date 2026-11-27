#include "seventhslide/library/destination_store.hpp"

#include "seventhslide/util/atomic_file.hpp"

#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace seventhslide::library {
namespace {

constexpr int k_store_version = 1;

}  // namespace

DestinationStore::DestinationStore(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir))
{
}

std::filesystem::path DestinationStore::file() const
{
    return data_dir_ / std::string{k_destinations_filename};
}

nlohmann::json DestinationStore::read() const
{
    nlohmann::json out{{"libraries", nlohmann::json::object()},
                       {"outputs_off", nlohmann::json::object()}};
    std::ifstream in(file());
    if (!in) {
        return out;
    }
    const auto doc = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object()) {
        return out;
    }
    for (const char* key : {"libraries", "outputs_off"}) {
        const auto it = doc.find(key);
        if (it != doc.end() && it->is_object()) {
            out[key] = *it;
        }
    }
    return out;
}

bool DestinationStore::write(const nlohmann::json& doc) const
{
    nlohmann::json out{{"version", k_store_version}};
    for (const char* key : {"libraries", "outputs_off"}) {
        if (!doc.at(key).empty()) {
            out[key] = doc.at(key);
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    return util::write_file_atomically(file(), out.dump(2) + '\n');
}

OutputDestinations DestinationStore::load(std::string_view library_id) const
{
    OutputDestinations out;
    const auto libraries = read().at("libraries");
    const auto lib = libraries.find(std::string(library_id));
    if (lib == libraries.end() || !lib->is_object()) {
        return out;
    }
    for (const auto& [output_id, records] : lib->items()) {
        if (records.is_array() && !records.empty()) {
            out.emplace(output_id, records);
        }
    }
    return out;
}

bool DestinationStore::save(std::string_view library_id, const OutputDestinations& outputs) const
{
    auto doc = read();
    auto& libraries = doc.at("libraries");
    auto& lib = libraries[std::string(library_id)];
    if (!lib.is_object()) {
        lib = nlohmann::json::object();
    }
    for (const auto& [output_id, records] : outputs) {
        if (records.is_array() && !records.empty()) {
            lib[output_id] = records;
        } else {
            lib.erase(output_id);
        }
    }
    if (lib.empty()) {
        libraries.erase(std::string(library_id));
    }
    return write(doc);
}

bool DestinationStore::remove_output(std::string_view library_id,
                                     std::string_view output_id) const
{
    return save(library_id, {{std::string(output_id), nlohmann::json::array()}})
        && set_output_on(library_id, output_id, true);
}

std::set<std::string, std::less<>> DestinationStore::outputs_off(std::string_view library_id) const
{
    std::set<std::string, std::less<>> out;
    const auto off = read().at("outputs_off");
    const auto lib = off.find(std::string(library_id));
    if (lib == off.end() || !lib->is_array()) {
        return out;
    }
    for (const auto& id : *lib) {
        if (id.is_string()) {
            out.insert(id.get<std::string>());
        }
    }
    return out;
}

bool DestinationStore::set_output_on(std::string_view library_id,
                                     std::string_view output_id,
                                     bool on) const
{
    auto ids = outputs_off(library_id);
    const bool changed = on ? ids.erase(std::string(output_id)) > 0
                            : ids.insert(std::string(output_id)).second;
    if (!changed) {
        return true;
    }
    auto doc = read();
    auto& off = doc.at("outputs_off");
    if (ids.empty()) {
        off.erase(std::string(library_id));
    } else {
        off[std::string(library_id)] = ids;
    }
    return write(doc);
}

bool DestinationStore::remove_library(std::string_view library_id) const
{
    auto doc = read();
    const auto removed = doc.at("libraries").erase(std::string(library_id))
                       + doc.at("outputs_off").erase(std::string(library_id));
    if (removed == 0) {
        return true;
    }
    return write(doc);
}

}  // namespace seventhslide::library
