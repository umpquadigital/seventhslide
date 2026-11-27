#include "seventhslide/db/style_profiles.hpp"

#include "seventhslide/json/read.hpp"
#include "seventhslide/model/app_settings.hpp"
#include "seventhslide/model/theme_keys.hpp"

#include <algorithm>
#include <functional>

namespace seventhslide::db {
namespace {

[[nodiscard]] bool theme_map_nonempty(const nlohmann::json& tm)
{
    return tm.is_object() && !tm.empty();
}

[[nodiscard]] nlohmann::json as_object_or_empty(const nlohmann::json& v)
{
    return v.is_object() ? v : nlohmann::json::object();
}

/**
 * The rows of ``current`` (song id, live value) whose value differs from the
 * one ``snapshot`` keeps under the id as a string, each paired with the kept
 * value. ``kept`` reads a snapshot entry, or null for a song the snapshot
 * does not list, as a value; ``live`` puts a current value in the same terms.
 * A snapshot that is not an object lists nothing (``find`` on it is ``end``).
 */
template <class Value, class Kept, class Live>
[[nodiscard]] std::vector<std::pair<std::int64_t, Value>> snapshot_deltas(
    const std::vector<std::pair<std::int64_t, Value>>& current,
    const nlohmann::json& snapshot,
    Kept kept,
    Live live)
{
    std::vector<std::pair<std::int64_t, Value>> deltas;
    for (const auto& [id, value] : current) {
        const auto it = snapshot.find(std::to_string(id));
        Value want = kept(it != snapshot.end() ? &*it : nullptr);
        if (live(value) != want) {
            deltas.emplace_back(id, std::move(want));
        }
    }
    return deltas;
}

} // namespace

nlohmann::json normalize_profile_data(const nlohmann::json& data)
{
    nlohmann::json out = model::StyleProfile::empty_data();
    out[std::string{k_profile_key_song_verse_orders}] = nlohmann::json::object();
    out[std::string{k_profile_key_theme_priority}] = profile_theme_priority(data);
    if (!data.is_object()) {
        return out;
    }
    for (const auto key : {std::string_view{"outputs"}, std::string_view{"songs"},
                           k_profile_key_song_verse_orders}) {
        if (const auto it = data.find(key); it != data.end() && it->is_object()) {
            out[std::string{key}] = *it;
        }
    }
    return out;
}

nlohmann::json snapshot_live(const std::vector<model::OutputConfig>& outputs,
                             const LiveProfileState& live)
{
    nlohmann::json out = model::StyleProfile::empty_data();
    for (const auto& oc : outputs) {
        out["outputs"][oc.id] = oc.category_defaults.is_object()
                                      ? oc.category_defaults
                                      : nlohmann::json::object();
    }
    for (const auto& [id, tm] : live.song_theme_maps) {
        if (theme_map_nonempty(tm)) {
            out["songs"][std::to_string(id)] = tm;
        }
    }
    auto verse_orders = nlohmann::json::object();
    for (const auto& [id, order] : live.song_verse_orders) {
        if (!order.empty()) {
            verse_orders[std::to_string(id)] = order;
        }
    }
    out[std::string{k_profile_key_song_verse_orders}] = std::move(verse_orders);
    out[std::string{k_profile_key_theme_priority}] =
        model::normalize_theme_priority(live.theme_priority);
    return out;
}

nlohmann::json blank_profile_data(const nlohmann::json& live_snapshot)
{
    auto live = normalize_profile_data(live_snapshot);
    // Empty verse orders (every song follows its own lyric order) and today's
    // theme priority.
    return nlohmann::json{
        {"outputs", live["outputs"]},
        {"songs", nlohmann::json::object()},
        {std::string{k_profile_key_song_verse_orders}, nlohmann::json::object()},
        {std::string{k_profile_key_theme_priority}, live[std::string{k_profile_key_theme_priority}]},
    };
}

void apply_output_defaults(std::vector<model::OutputConfig>& outputs,
                           const nlohmann::json& profile_outputs)
{
    if (!profile_outputs.is_object()) {
        return;
    }
    for (auto& oc : outputs) {
        const auto it = profile_outputs.find(oc.id);
        if (it != profile_outputs.end() && it->is_object()) {
            oc.category_defaults = *it;
        }
    }
}

std::vector<std::string> profile_theme_priority(const nlohmann::json& data)
{
    // The key names a string literal, so ``data()`` is NUL-terminated.
    return model::normalize_theme_priority(
        json::strings_or_empty(data, k_profile_key_theme_priority.data()));
}

std::vector<std::pair<std::int64_t, std::string>> verse_order_deltas(
    const std::vector<std::pair<std::int64_t, std::string>>& current,
    const nlohmann::json& snapshot_by_str_id)
{
    // Anything but a string is no verse order.
    return snapshot_deltas(
        current, snapshot_by_str_id,
        [](const nlohmann::json* kept) {
            return kept != nullptr && kept->is_string() ? kept->get<std::string>() : std::string{};
        },
        std::identity{});
}

std::vector<std::pair<std::int64_t, nlohmann::json>> theme_map_deltas(
    const std::vector<std::pair<std::int64_t, nlohmann::json>>& current,
    const nlohmann::json& snapshot_by_str_id)
{
    // Anything but an object is no theme map, on either side: a live null and
    // a live {} are the same map.
    return snapshot_deltas(
        current, snapshot_by_str_id,
        [](const nlohmann::json* kept) {
            return kept != nullptr && kept->is_object() ? *kept : nlohmann::json::object();
        },
        as_object_or_empty);
}

StyleProfileService::StyleProfileService(StyleProfileRepository& profiles,
                                         SongRepository& songs,
                                         ConfigRepository& config,
                                         std::vector<model::OutputConfig>& outputs)
    : profiles_(profiles)
    , songs_(songs)
    , config_(config)
    , outputs_(outputs)
{
}

LiveProfileState StyleProfileService::read_live_state() const
{
    LiveProfileState live;
    for (const auto& song : songs_.list_summaries()) {
        if (!song.id) {
            continue;
        }
        live.song_theme_maps.emplace_back(*song.id, song.theme_map);
        live.song_verse_orders.emplace_back(*song.id, song.verse_order);
    }
    live.theme_priority = live_theme_priority();
    return live;
}

nlohmann::json StyleProfileService::snapshot_live() const
{
    return db::snapshot_live(outputs_, read_live_state());
}

void StyleProfileService::apply_snapshot(const nlohmann::json& data)
{
    const auto blob = normalize_profile_data(data);
    apply_output_defaults(outputs_, blob["outputs"]);

    const auto live = read_live_state();
    songs_.set_theme_maps(theme_map_deltas(live.song_theme_maps, blob["songs"]));

    songs_.set_verse_orders(
        verse_order_deltas(live.song_verse_orders, blob[std::string{k_profile_key_song_verse_orders}]));
    if (const auto priority = profile_theme_priority(blob); priority != live.theme_priority) {
        config_.set_setting(model::k_setting_theme_priority, priority);
    }
}

std::vector<std::string> StyleProfileService::live_theme_priority() const
{
    const auto stored = config_.get_setting(model::k_setting_theme_priority);
    return model::normalize_theme_priority(stored ? json::strings_in(*stored)
                                                  : std::vector<std::string>{});
}

void StyleProfileService::capture_active()
{
    const auto aid = active_profile_id();
    if (!aid) {
        return;
    }
    // Most often nothing has changed (this runs on every quit and every
    // library close): the profile is then left as it is.
    auto live = snapshot_live();
    if (const auto stored = profiles_.get(*aid); stored && stored->data != live) {
        profiles_.save_data(*aid, live);
    }
}

bool StyleProfileService::activate(std::int64_t target_id)
{
    const auto prof = profiles_.get(target_id);
    if (!prof) {
        return false;
    }
    // Capture, apply and persist are one switch: a failure half-way must not
    // leave the outgoing profile's overrides on the incoming profile. The
    // database rolls back on its own; the live outputs, changed first, are
    // put back here, so the screens never show a switch that did not happen.
    const auto outputs_before = outputs_;
    try {
        db::Transaction tx(profiles_.database());
        const auto current = active_profile_id();
        if (!current || *current != target_id) {
            capture_active();
        }
        apply_snapshot(prof->data);
        set_active_profile_id(target_id);
        persist_active_and_outputs();
        tx.commit();
    } catch (...) {
        outputs_ = outputs_before;
        throw;
    }
    return true;
}

std::int64_t StyleProfileService::create_blank(std::string_view name)
{
    return profiles_.create(name, blank_profile_data(snapshot_live()));
}

std::optional<std::int64_t> StyleProfileService::duplicate(std::int64_t source_id,
                                                           std::string_view name)
{
    const auto active = active_profile_id();
    if (active && *active == source_id) {
        capture_active();
    }
    const auto src = profiles_.get(source_id);
    if (!src) {
        return std::nullopt;
    }
    return profiles_.create(name, normalize_profile_data(src->data));
}

void StyleProfileService::ensure_seeded()
{
    const auto profs = profiles_.list_summaries();
    if (profs.empty()) {
        const auto pid = profiles_.create("Default", snapshot_live());
        set_active_profile_id(pid);
        persist_active_and_outputs();
        return;
    }
    const auto active = active_profile_id();
    const bool valid = active && std::any_of(profs.begin(), profs.end(), [&](const auto& p) {
        return p.id && *p.id == *active;
    });
    if (!valid) {
        // Repairing a stale id does NOT apply the blob, so the live tables do not
        // mirror the newly chosen profile - capturing here would clobber it.
        set_active_profile_id(profs.front().id);
        persist_active_and_outputs();
    }
}

bool StyleProfileService::remove(std::int64_t id)
{
    const auto profs = profiles_.list_summaries();
    const bool found = std::any_of(profs.begin(), profs.end(), [&](const auto& p) {
        return p.id && *p.id == id;
    });
    if (!found) {
        return false;
    }
    if (profs.size() <= 1) {
        return false;
    }

    const auto active = active_profile_id();
    if (active && *active == id) {
        std::optional<std::int64_t> other;
        for (const auto& p : profs) {
            if (p.id && *p.id != id) {
                other = *p.id;
                break;
            }
        }
        if (!other || !activate(*other)) {
            return false;
        }
    }
    return profiles_.remove(id);
}

std::optional<std::int64_t> StyleProfileService::active_profile_id() const
{
    const auto raw = config_.get_setting(k_active_profile_setting);
    if (!raw || raw->is_null()) {
        return std::nullopt;
    }
    if (raw->is_number_integer()) {
        return raw->get<std::int64_t>();
    }
    if (raw->is_number_unsigned()) {
        return static_cast<std::int64_t>(raw->get<std::uint64_t>());
    }
    return std::nullopt;
}

void StyleProfileService::set_active_profile_id(std::optional<std::int64_t> id)
{
    if (id) {
        config_.set_setting(k_active_profile_setting, *id);
    } else {
        config_.set_setting(k_active_profile_setting, nullptr);
    }
}

void StyleProfileService::persist_active_and_outputs()
{
    // active_profile_id already written via set_active_profile_id when needed;
    // always flush outputs so category_defaults land with the profile switch.
    config_.save_outputs(outputs_);
}

} // namespace seventhslide::db
