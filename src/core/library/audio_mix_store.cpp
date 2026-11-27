#include "seventhslide/library/audio_mix_store.hpp"

#include "seventhslide/media/audio_levels.hpp"
#include "seventhslide/util/atomic_file.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace seventhslide::library {
namespace {

constexpr int k_store_version = 1;
constexpr std::string_view k_silence = "-inf";

using Strips = std::map<std::string, ChannelMix, std::less<>>;

nlohmann::json strips_to_json(const Strips& strips)
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [id, strip] : strips) {
        if (strip != ChannelMix{}) {
            out[id] = strip.to_json();
        }
    }
    return out;
}

Strips strips_from_json(const nlohmann::json& j, std::string_view key)
{
    Strips out;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_object()) {
        return out;
    }
    for (const auto& [id, strip] : it->items()) {
        if (!id.empty()) {
            out.emplace(id, ChannelMix::from_json(strip));
        }
    }
    return out;
}

ChannelMix strip_or_default(const Strips& strips, std::string_view id)
{
    const auto it = strips.find(id);
    return it == strips.end() ? ChannelMix{} : it->second;
}

}  // namespace

nlohmann::json ChannelMix::to_json() const
{
    const double db = media::normalize_gain_db(gain_db);
    nlohmann::json level = std::isinf(db) ? nlohmann::json(k_silence) : nlohmann::json(db);
    return {{"gain_db", std::move(level)}, {"muted", muted}, {"listen", listen}};
}

ChannelMix ChannelMix::from_json(const nlohmann::json& j)
{
    ChannelMix out;
    if (!j.is_object()) {
        return out;
    }
    if (const auto it = j.find("gain_db"); it != j.end()) {
        if (it->is_number()) {
            out.gain_db = media::normalize_gain_db(it->get<double>());
        } else if (it->is_string() && it->get<std::string>() == k_silence) {
            out.gain_db = -std::numeric_limits<double>::infinity();
        }
    }
    if (const auto it = j.find("muted"); it != j.end() && it->is_boolean()) {
        out.muted = it->get<bool>();
    }
    if (const auto it = j.find("listen"); it != j.end() && it->is_boolean()) {
        out.listen = it->get<bool>();
    }
    return out;
}

ChannelMix LibraryMix::stack(std::string_view id) const
{
    return strip_or_default(stacks, id);
}

ChannelMix LibraryMix::output(std::string_view id) const
{
    return strip_or_default(outputs, id);
}

nlohmann::json LibraryMix::to_json() const
{
    return {{"stacks", strips_to_json(stacks)}, {"outputs", strips_to_json(outputs)}};
}

LibraryMix LibraryMix::from_json(const nlohmann::json& j)
{
    LibraryMix out;
    if (!j.is_object()) {
        return out;
    }
    out.stacks = strips_from_json(j, "stacks");
    out.outputs = strips_from_json(j, "outputs");
    return out;
}

LibraryMix LibraryMix::first_use(const std::vector<std::string>& output_ids)
{
    LibraryMix out;
    if (!output_ids.empty() && !output_ids.front().empty()) {
        out.outputs[output_ids.front()].listen = true;
    }
    return out;
}

AudioMixStore::AudioMixStore(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir))
{
}

std::filesystem::path AudioMixStore::file() const
{
    return data_dir_ / std::string{k_audio_mix_filename};
}

nlohmann::json AudioMixStore::read() const
{
    std::ifstream in(file());
    if (!in) {
        return nlohmann::json::object();
    }
    auto doc = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    return doc.is_object() ? doc : nlohmann::json::object();
}

bool AudioMixStore::write(const nlohmann::json& doc) const
{
    nlohmann::json out = doc;
    out["version"] = k_store_version;
    if (!out.contains("libraries") || !out["libraries"].is_object()) {
        out["libraries"] = nlohmann::json::object();
    }
    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    return util::write_file_atomically(file(), out.dump(2) + '\n');
}

std::optional<LibraryMix> AudioMixStore::load(std::string_view library_id) const
{
    const auto doc = read();
    const auto libraries = doc.find("libraries");
    if (libraries == doc.end() || !libraries->is_object()) {
        return std::nullopt;
    }
    const auto lib = libraries->find(std::string(library_id));
    if (lib == libraries->end() || !lib->is_object()) {
        return std::nullopt;
    }
    return LibraryMix::from_json(*lib);
}

bool AudioMixStore::save(std::string_view library_id, const LibraryMix& mix) const
{
    auto doc = read();
    if (!doc.contains("libraries") || !doc["libraries"].is_object()) {
        doc["libraries"] = nlohmann::json::object();
    }
    doc["libraries"][std::string(library_id)] = mix.to_json();
    return write(doc);
}

bool AudioMixStore::remove_stack(std::string_view library_id, std::string_view stack_id) const
{
    auto mix = load(library_id);
    if (!mix) {
        return true;
    }
    const auto it = mix->stacks.find(stack_id);
    if (it == mix->stacks.end()) {
        return true;
    }
    mix->stacks.erase(it);
    return save(library_id, *mix);
}

bool AudioMixStore::remove_output(std::string_view library_id, std::string_view output_id) const
{
    auto mix = load(library_id);
    if (!mix) {
        return true;
    }
    const auto it = mix->outputs.find(output_id);
    if (it == mix->outputs.end()) {
        return true;
    }
    mix->outputs.erase(it);
    return save(library_id, *mix);
}

bool AudioMixStore::remove_library(std::string_view library_id) const
{
    auto doc = read();
    const auto libraries = doc.find("libraries");
    if (libraries == doc.end() || !libraries->is_object()
        || libraries->erase(std::string(library_id)) == 0) {
        return true;
    }
    return write(doc);
}

}  // namespace seventhslide::library
