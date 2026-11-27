#include <seventhslide/model/audio_fades.hpp>

#include "seventhslide/json/read.hpp"
#include "seventhslide/model/theme_keys.hpp"

#include <algorithm>
#include <array>

namespace seventhslide::model {

namespace {

constexpr std::array<const char*, 3> k_fields = {"fade_in_ms", "fade_out_ms", "overlap_ms"};
constexpr const char* k_on_play_pause = "on_play_pause";

[[nodiscard]] int& field_of(AudioFades& fades, std::string_view key) noexcept
{
    if (key == "fade_in_ms") {
        return fades.fade_in_ms;
    }
    if (key == "fade_out_ms") {
        return fades.fade_out_ms;
    }
    return fades.overlap_ms;
}

[[nodiscard]] int clamp_ms(int ms) noexcept
{
    return std::clamp(ms, 0, k_audio_fade_max_ms);
}

} // namespace

nlohmann::json AudioFades::to_json() const
{
    return {{"fade_in_ms", clamp_ms(fade_in_ms)},
            {"fade_out_ms", clamp_ms(fade_out_ms)},
            {"overlap_ms", clamp_ms(overlap_ms)},
            {k_on_play_pause, on_play_pause}};
}

AudioFades AudioFades::from_json(const nlohmann::json& j)
{
    return override_audio_fades({}, j);
}

nlohmann::json set_audio_fades(const nlohmann::json& j)
{
    nlohmann::json out = nlohmann::json::object();
    if (!j.is_object()) {
        return out;
    }
    for (const char* key : k_fields) {
        if (const auto* v = json::find(j, key); v != nullptr && v->is_number()) {
            out[key] = clamp_ms(json::int_or(j, key, 0));
        }
    }
    if (const auto* v = json::find(j, k_on_play_pause); v != nullptr && v->is_boolean()) {
        out[k_on_play_pause] = v->get<bool>();
    }
    return out;
}

AudioFades override_audio_fades(AudioFades under, const nlohmann::json& set)
{
    const auto fields = set_audio_fades(set);
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (it.key() == k_on_play_pause) {
            under.on_play_pause = it.value().get<bool>();
        } else {
            field_of(under, it.key()) = it.value().get<int>();
        }
    }
    return under;
}

AudioFades resolve_audio_fades(const AudioFades& output,
                               const nlohmann::json& service,
                               const nlohmann::json& file,
                               std::span<const std::string> priority)
{
    const auto order = normalize_theme_priority(
        std::vector<std::string>(priority.begin(), priority.end()));
    // The furthest first, each nearer one over it.
    AudioFades out = output;
    for (auto tier = order.rbegin(); tier != order.rend(); ++tier) {
        if (*tier == "global") {
            out = output;
        } else if (*tier == "service") {
            out = override_audio_fades(out, service);
        } else if (*tier == "content") {
            out = override_audio_fades(out, file);
        }
    }
    return out;
}

} // namespace seventhslide::model
