#include "seventhslide/library/console_store.hpp"

#include "seventhslide/util/atomic_file.hpp"

#include <fstream>
#include <system_error>
#include <utility>

namespace seventhslide::library {
namespace {

constexpr int k_store_version = 1;
constexpr const char* k_preview_stacks = "preview_stacks";

}  // namespace

ConsoleStore::ConsoleStore(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir))
{
}

std::filesystem::path ConsoleStore::file() const
{
    return data_dir_ / std::string{k_console_filename};
}

nlohmann::json ConsoleStore::read() const
{
    std::ifstream in(file());
    if (!in) {
        return nlohmann::json::object();
    }
    const auto doc = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object()) {
        return nlohmann::json::object();
    }
    const auto it = doc.find("libraries");
    return it != doc.end() && it->is_object() ? *it : nlohmann::json::object();
}

bool ConsoleStore::write(nlohmann::json libraries) const
{
    nlohmann::json out{{"version", k_store_version}};
    if (!libraries.empty()) {
        out["libraries"] = std::move(libraries);
    }
    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    return util::write_file_atomically(file(), out.dump(2) + '\n');
}

StackIds ConsoleStore::preview_stacks(std::string_view library_id) const
{
    StackIds out;
    const auto libraries = read();
    const auto lib = libraries.find(std::string(library_id));
    if (lib == libraries.end() || !lib->is_object()) {
        return out;
    }
    const auto ids = lib->find(k_preview_stacks);
    if (ids == lib->end() || !ids->is_array()) {
        return out;
    }
    for (const auto& id : *ids) {
        if (id.is_string() && !id.get_ref<const std::string&>().empty()) {
            out.insert(id.get<std::string>());
        }
    }
    return out;
}

bool ConsoleStore::save_preview_stacks(std::string_view library_id, const StackIds& ids) const
{
    auto libraries = read();
    const std::string key(library_id);
    auto& lib = libraries[key];
    if (!lib.is_object()) {
        lib = nlohmann::json::object();
    }
    if (ids.empty()) {
        lib.erase(k_preview_stacks);
    } else {
        lib[k_preview_stacks] = ids;
    }
    if (lib.empty()) {
        libraries.erase(key);
    }
    return write(std::move(libraries));
}

bool ConsoleStore::set_stack_preview(std::string_view library_id,
                                     std::string_view stack_id,
                                     bool on) const
{
    auto ids = preview_stacks(library_id);
    const auto it = ids.find(stack_id);
    if (on == (it != ids.end())) {
        return true;
    }
    if (on) {
        ids.emplace(stack_id);
    } else {
        ids.erase(it);
    }
    return save_preview_stacks(library_id, ids);
}

bool ConsoleStore::keep_only_stacks(std::string_view library_id, const StackIds& stacks) const
{
    auto ids = preview_stacks(library_id);
    const auto gone = std::erase_if(ids, [&](const std::string& id) { return !stacks.contains(id); });
    return gone == 0 || save_preview_stacks(library_id, ids);
}

bool ConsoleStore::remove_library(std::string_view library_id) const
{
    auto libraries = read();
    if (libraries.erase(std::string(library_id)) == 0) {
        return true;
    }
    return write(std::move(libraries));
}

}  // namespace seventhslide::library
