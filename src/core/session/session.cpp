#include "seventhslide/session/session.hpp"

#include "seventhslide/model/content.hpp"
#include "seventhslide/model/stack.hpp"
#include "seventhslide/model/output_library.hpp"
#include "seventhslide/util/find.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace seventhslide::session {

Session::Session(layout::ITextMeasurer& measurer)
    : measurer_(measurer)
{
    theme_priority_ = model::normalize_theme_priority({});
    ensure_default_stack();
}

void Session::ensure_default_stack()
{
    stack_defs_ = model::normalize_stacks(std::move(stack_defs_));
    type_defaults_ = model::normalize_type_defaults(type_defaults_, stack_defs_);
    if (stacks_.empty()) {
        Stack stack;
        stack.program.id = stack_defs_.back().id;
        stacks_.push_back(std::move(stack));
    }
    if (focused_stack_id_.empty()) {
        focused_stack_id_ = stacks_.back().id();
    }
}

StackBuildContext Session::build_context() const
{
    return StackBuildContext{
        measurer_,
        outputs_on(),
        theme_priority_,
        ccli_licence_number_,
        service_themes_,
    };
}

const Stack* Session::find_stack(std::string_view id) const noexcept
{
    return util::find_by(stacks_, id, &Stack::id);
}

Stack* Session::find_stack(std::string_view id) noexcept
{
    return util::find_by(stacks_, id, &Stack::id);
}

const StackRuntime* Session::runtime(const Slot& slot) const noexcept
{
    const auto* stack = find_stack(slot.stack_id);
    return stack != nullptr ? stack->side(slot.side) : nullptr;
}

StackRuntime* Session::runtime(const Slot& slot) noexcept
{
    return const_cast<StackRuntime*>(std::as_const(*this).runtime(slot));
}

const Stack& Session::focused_stack() const
{
    if (const auto* stack = find_stack(focused_stack_id_)) {
        return *stack;
    }
    return stacks_.back();
}

Stack& Session::focused_stack()
{
    return const_cast<Stack&>(std::as_const(*this).focused_stack());
}

std::vector<const Stack*> Session::occupied_stacks() const
{
    std::vector<const Stack*> out;
    for (const auto& stack : stacks_) {
        if (stack.program.occupied()) {
            out.push_back(&stack);
        }
    }
    return out;
}

const Stack* Session::frontmost_holding() const noexcept
{
    const auto it = std::find_if(stacks_.rbegin(), stacks_.rend(),
                                 [](const Stack& s) { return s.holds_content(); });
    return it != stacks_.rend() ? &*it : nullptr;
}

std::string Session::focused_stack_id() const
{
    return focused_stack().id();
}

void Session::set_focused_stack_id(std::string id)
{
    if (find_stack(id)) {
        focused_stack_id_ = std::move(id);
    }
}

void Session::arm_send_to(std::string id)
{
    if (id.empty() || find_stack(id)) {
        armed_send_to_ = std::move(id);
    }
}

std::string Session::resolve_target_stack(std::string_view item_type,
                                          std::string_view item_override) const
{
    return model::resolve_stack_id(stack_defs_, type_defaults_, armed_send_to_, item_override,
                                   item_type);
}

std::string Session::resolve_planned_stack(std::string_view item_type,
                                           std::string_view item_override) const
{
    return model::resolve_stack_id(stack_defs_, type_defaults_, {}, item_override, item_type);
}

const model::ServiceItem* Session::find_service_item(std::int64_t item_id) const noexcept
{
    for (const auto& item : service_items_) {
        if (item.item_id && *item.item_id == item_id) {
            return &item;
        }
    }
    return nullptr;
}

std::string Session::planned_stack_of(const model::ServiceItem& item) const
{
    return resolve_planned_stack(item.item_type, model::service_item_stack_id(item));
}

std::string Session::companion_error(const model::ServiceItem& parent,
                                     const std::string& parent_stack,
                                     std::int64_t companion_id,
                                     const std::vector<std::string>& used) const
{
    if (companion_id < 0) {
        return "No service item";
    }
    if (parent.item_id && companion_id == *parent.item_id) {
        return "An item cannot also play itself";
    }
    const auto* companion = find_service_item(companion_id);
    if (!companion) {
        return "Unknown item";
    }
    if (companion->item_type == model::k_item_type_divider) {
        return "Dividers cannot go live";
    }
    const auto stack = planned_stack_of(*companion);
    if (stack == parent_stack) {
        return "Also play needs a different stack";
    }
    if (std::find(used.begin(), used.end(), stack) != used.end()) {
        return "Another linked item already uses that stack";
    }
    return {};
}

std::string Session::also_play_list_error(std::int64_t parent_id,
                                          const std::vector<std::int64_t>& companion_ids) const
{
    if (parent_id < 0) {
        return "No service item";
    }
    const auto* parent = find_service_item(parent_id);
    if (!parent) {
        return "Unknown item";
    }
    if (parent->item_type == model::k_item_type_divider) {
        return "Dividers cannot go live";
    }
    const auto parent_stack = planned_stack_of(*parent);
    std::vector<std::string> used;
    for (const auto companion_id : companion_ids) {
        if (auto err = companion_error(*parent, parent_stack, companion_id, used); !err.empty()) {
            return err;
        }
        used.push_back(planned_stack_of(*find_service_item(companion_id)));
    }
    return {};
}

std::vector<std::int64_t> Session::also_play_set(std::int64_t item_id) const
{
    // The link list lives on whichever item was focused when the set was made,
    // but the set has no head: walk the links both ways from any member.
    std::vector<std::int64_t> found{item_id};
    const auto add = [&](std::int64_t id) {
        if (std::find(found.begin(), found.end(), id) == found.end()
            && find_service_item(id) != nullptr) {
            found.push_back(id);
        }
    };
    for (std::size_t i = 0; i < found.size(); ++i) {
        const auto id = found[i];
        for (const auto& other : service_items_) {
            if (!other.item_id) {
                continue;
            }
            const auto links = model::service_item_also_play(other);
            if (*other.item_id == id) {
                for (const auto link : links) {
                    add(link);
                }
            } else if (std::find(links.begin(), links.end(), id) != links.end()) {
                add(*other.item_id);
            }
        }
    }
    if (found.size() < 2) {
        return {};
    }
    std::vector<std::int64_t> ordered;
    ordered.reserve(found.size());
    for (const auto& item : service_items_) {
        if (item.item_id
            && std::find(found.begin(), found.end(), *item.item_id) != found.end()) {
            ordered.push_back(*item.item_id);
        }
    }
    return ordered;
}

void Session::fire_also_play(const model::ServiceItem& item,
                             const std::string& item_stack,
                             Companions showing)
{
    if (!item.item_id) {
        return;
    }
    std::vector<std::string> used{item_stack};
    const auto already = [&](const std::string& id) {
        return std::find(used.begin(), used.end(), id) != used.end();
    };
    for (const auto member_id : also_play_set(*item.item_id)) {
        if (member_id == *item.item_id) {
            continue;
        }
        const auto* member = find_service_item(member_id);
        if (!member || member->item_type == model::k_item_type_divider) {
            continue;
        }
        const auto stack = planned_stack_of(*member);
        if (already(stack)) {
            continue;
        }
        used.push_back(stack);
        auto& program = stack_or_focused(stack).program;
        if (showing == Companions::KeepShowing && program.service_item_id() == member_id) {
            continue;
        }
        program.apply_live_from_service_item(*member, build_context());
    }
}

Stack& Session::stack_or_focused(std::string_view stack_id)
{
    if (auto* stack = find_stack(stack_id)) {
        return *stack;
    }
    return focused_stack();
}

void Session::set_stacks(std::vector<model::StackDef> defs, model::StackTypeDefaults defaults)
{
    stack_defs_ = model::normalize_stacks(std::move(defs));
    type_defaults_ = model::normalize_type_defaults(std::move(defaults), stack_defs_);

    std::vector<Stack> next;
    next.reserve(stack_defs_.size());
    for (const auto& def : stack_defs_) {
        if (auto* existing = find_stack(def.id)) {
            next.push_back(std::move(*existing));
        } else {
            Stack stack;
            stack.program.id = def.id;
            next.push_back(std::move(stack));
        }
    }
    stacks_ = std::move(next);
    if (!find_stack(focused_stack_id_)) {
        focused_stack_id_ = stacks_.empty() ? std::string{} : stacks_.back().id();
    }
    if (!find_stack(service_stack_id_)) {
        // The selected row was what that stack showed.
        service_item_index_ = -1;
        service_stack_id_.clear();
    }
    if (!find_stack(armed_send_to_)) {
        armed_send_to_.clear();
    }
    rebuild();
    attach_players();
}

bool Session::set_preview_stacks(const std::set<std::string, std::less<>>& stack_ids)
{
    bool changed = false;
    for (auto& stack : stacks_) {
        const bool wanted = stack_ids.contains(stack.id());
        if (wanted == stack.preview.has_value()) {
            continue;
        }
        changed = true;
        if (wanted) {
            stack.preview.emplace();
            stack.preview->id = stack.id();
        } else {
            stack.preview.reset();
            // What was cued is gone, and its blank with it.
            if (const auto it = stack_display_.find(stack.id()); it != stack_display_.end()) {
                it->second.preview_blank = false;
            }
        }
    }
    return changed;
}

std::set<std::string, std::less<>> Session::preview_stack_ids() const
{
    std::set<std::string, std::less<>> out;
    for (const auto& stack : stacks_) {
        if (stack.preview) {
            out.insert(stack.id());
        }
    }
    return out;
}

void Session::set_outputs(std::vector<model::OutputConfig> outputs)
{
    outputs_ = std::move(outputs);
    // Blank, freeze and held frames follow the output id, so every output
    // needs one before anything plays.
    (void)model::ensure_output_ids(outputs_);
    rebuild();
}

void Session::set_theme_priority(std::vector<std::string> priority)
{
    theme_priority_ = model::normalize_theme_priority(priority);
    rebuild();
}

void Session::set_ccli_licence_number(std::string number)
{
    if (number == ccli_licence_number_) {
        return;
    }
    ccli_licence_number_ = std::move(number);
    // Title overlays fill their tokens when the slides are built, so a live
    // song's title slide needs a rebuild, not just fresh scene extras.
    rebuild();
}

void Session::rebuild()
{
    // Outputs or stacks may have come and gone.
    prune_display_state();
    sync_outputs_on();
    const auto ctx = build_context();
    // Settings, outputs or themes changed under content, live or cued: re-show
    // it where it is rather than jumping every stack back to its first line.
    for_every_side([&](StackRuntime& st) {
        if (st.has_build() || st.occupied()) {
            st.refresh(ctx);
        }
    });
    sync_output_holds();
}

StackSide Session::landing_side(const Stack& stack, Land land) noexcept
{
    return land == Land::Cue && stack.preview ? StackSide::Preview : StackSide::Program;
}

namespace {

/// ``side`` of ``stack``, as landing_side picked it.
StackRuntime& landed_on(Stack& stack, StackSide side)
{
    auto* runtime = stack.side(side);
    // landing_side picks Preview only for a stack that has one.
    assert(runtime != nullptr);
    return *runtime;
}

} // namespace

void Session::after_send_to(Stack& stack, StackSide side, bool from_library)
{
    focused_stack_id_ = stack.id();
    if (side == StackSide::Preview) {
        arrive_cued(landed_on(stack, side));
        lift_preview_blank(stack.id());
        return;
    }
    if (from_library) {
        drop_selected_row(stack.id());
    }
}

void Session::arrive_cued(StackRuntime& preview)
{
    preview.set_media_playing(false);
}

void Session::lift_preview_blank(std::string_view stack_id)
{
    if (const auto it = stack_display_.find(stack_id); it != stack_display_.end()) {
        it->second.preview_blank = false;
    }
}

void Session::drop_selected_row(std::string_view stack_id)
{
    if (service_stack_id_ == stack_id) {
        service_item_index_ = -1;
        service_stack_id_.clear();
    }
}

Stack& Session::library_target(std::string_view item_type, std::string stack_id)
{
    if (stack_id.empty()) {
        stack_id = resolve_target_stack(item_type);
    }
    return stack_or_focused(stack_id);
}

Slot Session::play_song(model::Song song, std::string stack_id, Land land)
{
    auto& stack = library_target(model::k_item_type_song, std::move(stack_id));
    const auto side = landing_side(stack, land);
    landed_on(stack, side).play_song(std::move(song), build_context());
    after_send_to(stack, side, true);
    return {stack.id(), side};
}

Slot Session::play_bible(model::BiblePassage bible, std::string stack_id, Land land)
{
    auto& stack = library_target(model::k_item_type_bible, std::move(stack_id));
    const auto side = landing_side(stack, land);
    landed_on(stack, side).play_bible(std::move(bible), build_context());
    after_send_to(stack, side, true);
    return {stack.id(), side};
}

Slot Session::play_freeform(LiveFreeformContent content, std::string stack_id, Land land)
{
    auto& stack = library_target(model::k_item_type_freeform, std::move(stack_id));
    const auto side = landing_side(stack, land);
    landed_on(stack, side).play_freeform(std::move(content), build_context());
    after_send_to(stack, side, true);
    return {stack.id(), side};
}

Slot Session::play_media(LiveMediaContent media, std::string stack_id, Land land)
{
    auto& stack = library_target(model::to_string(media.kind), std::move(stack_id));
    const auto side = landing_side(stack, land);
    landed_on(stack, side).play_media(std::move(media), build_context());
    after_send_to(stack, side, true);
    return {stack.id(), side};
}

void Session::open_service(std::optional<std::int64_t> service_id,
                           theme::OutputThemeMap service_themes,
                           std::vector<model::ServiceItem> items)
{
    service_id_ = service_id;
    service_themes_ = std::move(service_themes);
    service_items_ = std::move(items);
    service_item_index_ = -1;
    service_stack_id_.clear();
    clear_all_stacks();
    focused_stack_id_ = stacks_.empty() ? std::string{} : stacks_.back().id();
}

std::optional<Slot> Session::select_service_item(int index, bool focus_target, Land land)
{
    if (index < 0 || index >= static_cast<int>(service_items_.size())) {
        return std::nullopt;
    }
    const auto& item = service_items_[static_cast<std::size_t>(index)];
    const auto target = resolve_planned_stack(item.item_type, model::service_item_stack_id(item));
    auto& stack = stack_or_focused(target);
    const auto side = landing_side(stack, land);
    auto& landed = landed_on(stack, side);
    landed.apply_live_from_service_item(item, build_context());
    if (focus_target) {
        focused_stack_id_ = stack.id();
    }
    if (side == StackSide::Preview) {
        // Cued, not live: the selected row is what is live, and the item's
        // Also play set goes live with it at the take.
        arrive_cued(landed);
        lift_preview_blank(stack.id());
        return Slot{stack.id(), side};
    }
    service_item_index_ = index;
    service_stack_id_ = stack.id();
    fire_also_play(item, stack.id(), Companions::Restart);
    return Slot{stack.id(), side};
}

bool Session::patch_service_item_data(std::int64_t item_id, nlohmann::json data)
{
    for (auto& item : service_items_) {
        if (item.item_id && *item.item_id == item_id) {
            item.data = data.is_object() ? std::move(data) : nlohmann::json::object();
            return true;
        }
    }
    return false;
}

void Session::replace_service_items(std::vector<model::ServiceItem> items)
{
    std::optional<std::int64_t> selected_id;
    if (const auto* cur = current_service_item()) {
        selected_id = cur->item_id;
    }
    service_items_ = std::move(items);
    service_item_index_ = -1;
    if (!selected_id) {
        return;
    }
    for (std::size_t i = 0; i < service_items_.size(); ++i) {
        if (service_items_[i].item_id == selected_id) {
            service_item_index_ = static_cast<int>(i);
            return;
        }
    }
}

bool Session::refresh_service_item(std::int64_t item_id)
{
    const auto* item = find_service_item(item_id);
    if (item == nullptr) {
        return false;
    }
    const auto ctx = build_context();
    bool any = false;
    for_every_side([&](StackRuntime& st, StackSide side) {
        if (st.service_item_id() != item_id) {
            return;
        }
        // A row made another kind of item loads anew: in a Preview, cued.
        if (st.refresh_from_service_item(*item, ctx) && side == StackSide::Preview) {
            arrive_cued(st);
        }
        any = true;
    });
    return any;
}

bool Session::refresh_freeform(const LiveFreeformContent& content)
{
    if (!content.library_id) {
        return false;
    }
    const auto ctx = build_context();
    bool any = false;
    for_every_side([&](StackRuntime& st) { any = st.refresh_freeform(content, ctx) || any; });
    return any;
}

void Session::set_service_themes(theme::OutputThemeMap service_themes)
{
    service_themes_ = std::move(service_themes);
    rebuild();
}

bool Session::clear(const Slot& slot)
{
    if (slot.side == StackSide::Program) {
        return clear_stack(slot.stack_id);
    }
    auto* stack = find_stack(slot.stack_id);
    auto* preview = stack != nullptr ? stack->side(StackSide::Preview) : nullptr;
    if (preview == nullptr) {
        return false;
    }
    preview->clear(build_context());
    lift_preview_blank(stack->id());
    refocus_if_emptied(*stack);
    return true;
}

bool Session::transition(std::string_view stack_id, std::optional<model::Transition> how)
{
    auto* stack = find_stack(stack_id);
    if (stack == nullptr || !stack->preview || !stack->preview->occupied()) {
        return false;
    }
    // Where each side stands now: what is live, to go back to; what is cued,
    // to carry on from (played in Preview, it may have reached its end).
    stack->program.sync_playback();
    stack->preview->sync_playback();
    std::swap(stack->program, *stack->preview);
    auto& live = stack->program;
    auto& cued = *stack->preview;
    // Each side's blank goes with it; a freeze holds the outputs, not a side.
    if (const auto it = stack_display_.find(stack->id()); it != stack_display_.end()) {
        std::swap(it->second.blank, it->second.preview_blank);
    }

    live.player().arrive(how);
    // Playing, it plays on; paused, it plays from where it stands, as Play
    // would have it (over again, played to its end in Preview), unless its
    // item waits for Play.
    if (live.playback().is_playing || live.playback().autoplay_intent) {
        (void)live.media_play();
    } else {
        live.set_media_playing(false);
    }
    cued.set_media_playing(false);

    // The selected row is what is live, and its Also play set goes live with
    // it; a companion already showing carries on, as the item itself does.
    const auto item_id = live.service_item_id();
    const auto* item = item_id ? find_service_item(*item_id) : nullptr;
    if (item != nullptr) {
        service_item_index_ = static_cast<int>(item - service_items_.data());
        service_stack_id_ = stack->id();
        fire_also_play(*item, stack->id(), Companions::KeepShowing);
    } else {
        drop_selected_row(stack->id());
    }
    return true;
}

bool Session::clear_stack(std::string_view id)
{
    auto* stack = find_stack(id);
    if (!stack) {
        return false;
    }
    stack->program.clear(build_context());
    // A cleared stack starts over: not blanked or frozen. What is cued keeps
    // its own blank.
    if (const auto it = stack_display_.find(stack->id()); it != stack_display_.end()) {
        const bool cued_blank = it->second.preview_blank;
        it->second = StackDisplay{};
        it->second.preview_blank = cued_blank;
    }
    drop_selected_row(stack->id());
    refocus_if_emptied(*stack);
    return true;
}

void Session::refocus_if_emptied(const Stack& stack)
{
    // The focus stays while the stack still has something on either side,
    // and otherwise goes to the frontmost stack that has.
    if (focused_stack_id_ != stack.id() || stack.holds_content()) {
        return;
    }
    if (const auto* front = frontmost_holding()) {
        focused_stack_id_ = front->id();
    } else {
        focused_stack_id_ = stacks_.empty() ? std::string{} : stacks_.back().id();
    }
}

void Session::clear_all_stacks()
{
    service_item_index_ = -1;
    service_stack_id_.clear();
    const auto ctx = build_context();
    for_every_side([&](StackRuntime& st) { st.clear(ctx); });
    stack_display_.clear();
}

const model::ServiceItem* Session::current_service_item() const noexcept
{
    if (service_item_index_ < 0
        || service_item_index_ >= static_cast<int>(service_items_.size())) {
        return nullptr;
    }
    return &service_items_[static_cast<std::size_t>(service_item_index_)];
}

SessionMode Session::mode() const noexcept
{
    const auto& st = focused_stack().program;
    if (st.mode() != SessionMode::Empty) {
        return st.mode();
    }
    if (service_id_) {
        return SessionMode::Service;
    }
    return SessionMode::Empty;
}

bool Session::has_build() const noexcept
{
    return std::any_of(stacks_.begin(), stacks_.end(),
                       [](const Stack& stack) { return stack.program.has_build(); });
}

void Session::set_players(PlayerFor player_for)
{
    player_for_ = std::move(player_for);
    attach_players();
}

void Session::attach_players()
{
    // Every side holding video or audio plays it: a Program's goes to the
    // outputs, a Preview's only to its well (the app routes each by the side
    // its item is on).
    for_every_side([this](StackRuntime& st, StackSide side) {
        media::IMediaPlayer* player = nullptr;
        if (player_for_ && st.media_instance() != 0) {
            if (st.video_mode_active()) {
                player = player_for_(st.media_instance(), model::ContentKind::Video, side);
            } else if (st.audio_mode_active()) {
                player = player_for_(st.media_instance(), model::ContentKind::Audio, side);
            }
        }
        st.set_media_player(player);
    });
}

std::vector<Session::ObjectVideo> Session::object_videos() const
{
    std::vector<ObjectVideo> out;
    for (auto it = stacks_.rbegin(); it != stacks_.rend(); ++it) {
        for (auto& video : it->program.object_videos()) {
            const auto listed = std::find_if(out.begin(), out.end(), [&](const ObjectVideo& v) {
                return v.video.key == video.key;
            });
            if (listed != out.end()) {
                listed->video.merge(video);
            } else {
                out.push_back({it->id(), std::move(video)});
            }
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

void Session::sync_playback()
{
    for (auto& stack : stacks_) {
        if (stack.program.has_transport()) {
            stack.program.sync_playback();
        }
    }
}

bool Session::next_slide_on(const Slot& slot)
{
    auto* st = runtime(slot);
    return st != nullptr && st->next_slide();
}

bool Session::jump_to_line_on(const Slot& slot, int line_index)
{
    auto* st = runtime(slot);
    return st != nullptr && st->jump_to_line(line_index);
}

bool Session::jump_to_step_on(const Slot& slot, int line_index, int step)
{
    auto* st = runtime(slot);
    return st != nullptr && st->jump_to_step(line_index, step);
}

bool Session::next_service_slide()
{
    if (auto* stack = find_stack(service_stack_id_)) {
        return stack->program.next_slide();
    }
    return focused_stack().program.next_slide();
}

// --- on / off ---

bool Session::output_is_on(std::string_view output_id) const
{
    return find_output(output_id) != nullptr && !off_output_ids_.contains(output_id);
}

bool Session::set_output_on(std::string_view output_id, bool on)
{
    const auto* oc = find_output(output_id);
    if (oc == nullptr) {
        return false;
    }
    if (on == output_is_on(oc->id)) {
        return true;
    }
    if (on) {
        off_output_ids_.erase(off_output_ids_.find(oc->id));
    } else {
        if (outputs_on().size() <= 1) {
            return false;
        }
        off_output_ids_.insert(oc->id);
    }
    rebuild();
    return true;
}

void Session::set_outputs_off(std::set<std::string, std::less<>> output_ids)
{
    if (output_ids == off_output_ids_) {
        return;
    }
    off_output_ids_ = std::move(output_ids);
    rebuild();
}

void Session::sync_outputs_on()
{
    // Every output off would leave nothing to show: the first stays on.
    const bool any_on = std::any_of(outputs_.begin(), outputs_.end(), [this](const auto& oc) {
        return !off_output_ids_.contains(oc.id);
    });
    if (!any_on && !outputs_.empty()) {
        off_output_ids_.erase(off_output_ids_.find(outputs_.front().id));
    }
    outputs_on_.clear();
    if (off_output_ids_.empty()) {
        return;
    }
    for (const auto& oc : outputs_) {
        if (!off_output_ids_.contains(oc.id)) {
            outputs_on_.push_back(oc);
        }
    }
}

// --- blank / freeze ---

const model::OutputConfig* Session::find_output(std::string_view output_id) const
{
    return model::find_output_by_id(outputs_, output_id);
}

bool Session::output_is_blank(const model::OutputConfig& oc) const
{
    const auto it = output_toggles_.find(oc.id);
    return it != output_toggles_.end() && it->second.blank;
}

bool Session::output_is_frozen(const model::OutputConfig& oc) const
{
    const auto it = output_toggles_.find(oc.id);
    return it != output_toggles_.end() && it->second.frozen;
}

bool Session::output_is_blank(std::string_view output_id) const
{
    const auto* oc = find_output(output_id);
    return oc != nullptr && output_is_blank(*oc);
}

bool Session::output_is_frozen(std::string_view output_id) const
{
    const auto* oc = find_output(output_id);
    return oc != nullptr && output_is_frozen(*oc);
}

const model::OutputConfig* Session::primary_output() const noexcept
{
    for (const auto& oc : outputs_) {
        if (output_is_on(oc.id) && !oc.name.empty()) {
            return &oc;
        }
    }
    if (!outputs_.empty() && !outputs_.front().name.empty()) {
        return &outputs_.front();
    }
    return nullptr;
}

bool Session::global_on(bool model::OutputConfig::*exempt, bool OutputToggles::*flag) const
{
    bool any = false;
    for (const auto& oc : outputs_on()) {
        if (oc.*exempt) {
            continue;
        }
        const auto it = output_toggles_.find(oc.id);
        if (it == output_toggles_.end() || !(it->second.*flag)) {
            return false;
        }
        any = true;
    }
    return any;
}

void Session::set_global(bool model::OutputConfig::*exempt, bool OutputToggles::*flag, bool on)
{
    for (const auto& oc : outputs_on()) {
        if (!(oc.*exempt)) {
            output_toggles_[oc.id].*flag = on;
        }
    }
}

bool Session::global_blank() const
{
    return global_on(&model::OutputConfig::exempt_from_global_blank, &OutputToggles::blank);
}

bool Session::global_freeze() const
{
    return global_on(&model::OutputConfig::exempt_from_global_freeze, &OutputToggles::frozen);
}

void Session::toggle_global_blank()
{
    set_global_blank(!global_blank());
}

void Session::toggle_global_freeze()
{
    set_global_freeze(!global_freeze());
}

void Session::set_global_blank(bool on)
{
    for (const auto& oc : outputs_on()) {
        if (!oc.exempt_from_global_blank && output_is_blank(oc) != on) {
            settle_for_blank(oc.id);
        }
    }
    set_global(&model::OutputConfig::exempt_from_global_blank, &OutputToggles::blank, on);
}

void Session::set_global_freeze(bool on)
{
    set_global(&model::OutputConfig::exempt_from_global_freeze, &OutputToggles::frozen, on);
    sync_output_holds();
}

bool Session::toggle_output_blank(std::string_view output_id)
{
    const auto* oc = find_output(output_id);
    if (oc == nullptr) {
        return false;
    }
    auto& toggles = output_toggles_[oc->id];
    toggles.blank = !toggles.blank;
    settle_for_blank(oc->id);
    return true;
}

bool Session::toggle_output_freeze(std::string_view output_id)
{
    const auto* oc = find_output(output_id);
    if (oc == nullptr) {
        return false;
    }
    auto& toggles = output_toggles_[oc->id];
    toggles.frozen = !toggles.frozen;
    sync_output_holds();
    return true;
}

bool Session::toggle_stack_blank(std::string_view stack_id)
{
    auto* stack = find_stack(stack_id);
    if (stack == nullptr || (!stack_is_blank(stack->id()) && !stack->program.occupied())) {
        return false;
    }
    auto& display = stack_display_[stack->id()];
    display.blank = !display.blank;
    stack->program.player().settle();
    return true;
}

bool Session::toggle_preview_blank(std::string_view stack_id)
{
    auto* stack = find_stack(stack_id);
    if (stack == nullptr || !stack->preview
        || (!preview_is_blank(stack->id()) && !stack->preview->occupied())) {
        return false;
    }
    auto& display = stack_display_[stack->id()];
    display.preview_blank = !display.preview_blank;
    stack->preview->player().settle();
    return true;
}

bool Session::toggle_stack_freeze(std::string_view stack_id)
{
    const auto* stack = find_stack(stack_id);
    if (stack == nullptr || (!stack_is_frozen(stack->id()) && !stack->program.occupied())) {
        return false;
    }
    auto& display = stack_display_[stack->id()];
    display.frozen = !display.frozen;
    display.held.clear();
    if (display.frozen) {
        for (auto& frame : stack->program.scene_frames(build_context())) {
            // Copy the key first: moving the frame in the same call would
            // leave the key unsequenced against the move.
            const std::string output_id = frame.output_id;
            display.held.insert_or_assign(output_id, std::move(frame));
        }
    }
    return true;
}

void Session::clear_blank_freeze()
{
    for (const auto& [output_id, toggles] : output_toggles_) {
        if (toggles.blank) {
            settle_for_blank(output_id);
        }
    }
    for (auto& stack : stacks_) {
        if (stack_is_blank(stack.id())) {
            stack.program.player().settle();
        }
        if (stack.preview && preview_is_blank(stack.id())) {
            stack.preview->player().settle();
        }
    }
    output_toggles_.clear();
    stack_display_.clear();
    sync_output_holds();
}

void Session::settle_for_blank(std::string_view output_id)
{
    for (auto& stack : stacks_) {
        stack.program.player().settle(output_id);
    }
}

bool Session::stack_is_blank(std::string_view stack_id) const
{
    const auto it = stack_display_.find(stack_id);
    return it != stack_display_.end() && it->second.blank;
}

bool Session::preview_is_blank(std::string_view stack_id) const
{
    const auto it = stack_display_.find(stack_id);
    return it != stack_display_.end() && it->second.preview_blank;
}

bool Session::stack_is_frozen(std::string_view stack_id) const
{
    const auto it = stack_display_.find(stack_id);
    return it != stack_display_.end() && it->second.frozen;
}

void Session::sync_output_holds()
{
    // An output switched off shows nothing, so holds nothing.
    std::erase_if(output_holds_, [this](const auto& hold) {
        const auto* oc = find_output(hold.first);
        return oc == nullptr || !output_is_on(oc->id) || !output_is_frozen(*oc);
    });
    const auto& on = outputs_on();
    const bool any_new = std::any_of(on.begin(), on.end(), [this](const auto& oc) {
        return output_is_frozen(oc) && !output_holds_.contains(oc.id);
    });
    if (!any_new) {
        return;
    }
    auto frames = stack_frames(build_context(), {});
    for (const auto& oc : on) {
        if (output_is_frozen(oc) && !output_holds_.contains(oc.id)) {
            output_holds_.emplace(oc.id, live_layers(oc, frames));
        }
    }
}

void Session::prune_display_state()
{
    const auto output_gone = [this](const auto& entry) {
        return find_output(entry.first) == nullptr;
    };
    std::erase_if(output_toggles_, output_gone);
    std::erase_if(output_holds_, output_gone);
    std::erase_if(off_output_ids_,
                  [this](const std::string& id) { return find_output(id) == nullptr; });
    std::erase_if(stack_display_,
                  [this](const auto& entry) { return find_stack(entry.first) == nullptr; });
}

namespace {

void clear_media_wipe_if_layered(scene::SceneFrame& frame)
{
    if (frame.mode != scene::SceneMode::Image && frame.mode != scene::SceneMode::Video
        && frame.mode != scene::SceneMode::Web && frame.mode != scene::SceneMode::Live) {
        return;
    }
    frame.background.type = layout::BackgroundType::Transparent;
    frame.background.color.clear();
    frame.background.image.clear();
    frame.background.video.clear();
}

} // namespace

Session::StackFrames Session::stack_frames(const StackBuildContext& ctx,
                                           std::string_view only_output_id) const
{
    // Built once per stack for every output it reaches; a frozen stack shows
    // what it held instead. Audio is sound only: it builds no frame, so it
    // adds no layer, neither covering the stacks behind it nor becoming the
    // frame an output reports. (A stack frozen before audio arrived still
    // shows what it held.)
    StackFrames frames(stacks_.size());
    for (std::size_t i = 0; i < stacks_.size(); ++i) {
        const auto& st = stacks_[i].program;
        if (st.occupied() && !st.audio_mode_active() && !stack_is_frozen(st.id)) {
            frames[i] = st.scene_frames(ctx, only_output_id);
        }
    }
    return frames;
}

std::vector<Session::Layer> Session::live_layers(const model::OutputConfig& oc,
                                                 StackFrames& frames) const
{
    std::vector<Layer> layers;
    for (std::size_t i = 0; i < stacks_.size(); ++i) {
        const auto& st = stacks_[i].program;
        if (!model::output_enables_stack(oc.enabled_stack_ids, st.id)) {
            continue;
        }
        if (const auto display = stack_display_.find(st.id);
            display != stack_display_.end() && display->second.frozen) {
            if (const auto held = display->second.held.find(oc.id);
                held != display->second.held.end()) {
                auto frame = held->second;
                frame.is_frozen = true;
                layers.push_back({st.id, std::move(frame)});
            }
            continue;
        }
        for (auto& frame : frames[i]) {
            if (frame.output_id == oc.id) {
                layers.push_back({st.id, std::move(frame)});
                break;
            }
        }
    }
    return layers;
}

scene::SceneFrame Session::present(const model::OutputConfig& oc, std::vector<Layer> layers) const
{
    // Here, so it holds for what a freeze holds as it does for what is live,
    // as the mixer's leaving out their sound does.
    std::erase_if(layers, [&oc](const Layer& layer) {
        return !model::output_enables_type(oc.disabled_content_types,
                                           scene::to_string(layer.frame.mode));
    });
    const bool frozen = output_is_frozen(oc);
    const bool blank = output_is_blank(oc);
    for (auto& layer : layers) {
        // A held layer may predate a rename.
        layer.frame.output_name = oc.name;
        layer.frame.stack_id = layer.stack_id;
        if (frozen) {
            layer.frame.is_frozen = true;
        }
        // Blanked before the layers below are shared: they are not changed
        // after.
        if (blank || stack_is_blank(layer.stack_id)) {
            scene::apply_scene_blank(layer.frame);
        }
    }

    scene::SceneFrame frame;
    if (layers.empty()) {
        frame.output_id = oc.id;
        frame.output_name = oc.name;
        frame.canvas_width = oc.canvas_width;
        frame.canvas_height = oc.canvas_height;
        frame.mode = scene::SceneMode::Empty;
        frame.is_frozen = frozen;
        if (blank) {
            scene::apply_scene_blank(frame);
        }
    } else {
        for (std::size_t i = 1; i < layers.size(); ++i) {
            clear_media_wipe_if_layered(layers[i].frame);
        }
        frame = std::move(layers.back().frame);
        layers.pop_back();
        frame.stacked_below.reserve(layers.size());
        for (auto& layer : layers) {
            frame.stacked_below.push_back(scene::share_frame(std::move(layer.frame)));
        }
    }
    return frame;
}

std::vector<scene::SceneFrame> Session::compose_frames(std::string_view only_output_id) const
{
    // Built or not, every output on has a frame: with nothing on its stacks
    // it is an empty one (or what it held when frozen), which is what clears
    // an output whose last stack was deleted. One switched off has none.
    auto frames = stack_frames(build_context(), only_output_id);
    const auto& on = outputs_on();
    std::vector<scene::SceneFrame> out;
    out.reserve(on.size());
    for (const auto& oc : on) {
        if (!only_output_id.empty() && oc.id != only_output_id) {
            continue;
        }
        const auto hold = output_holds_.find(oc.id);
        out.push_back(present(oc, hold != output_holds_.end() && output_is_frozen(oc)
                                      ? hold->second
                                      : live_layers(oc, frames)));
    }
    return out;
}

std::vector<scene::SceneFrame> Session::scene_frames() const
{
    return compose_frames({});
}

std::optional<scene::SceneFrame> Session::scene_frame(std::string_view output_id) const
{
    if (output_id.empty()) {
        return std::nullopt;
    }
    auto frames = compose_frames(output_id);
    if (frames.empty()) {
        return std::nullopt;
    }
    return std::move(frames.front());
}

std::optional<scene::SceneFrame> Session::scene_frame_at(const Slot& slot,
                                                         std::string_view output_id,
                                                         int slide_index) const
{
    const auto* st = runtime(slot);
    if (st == nullptr) {
        return std::nullopt;
    }
    return st->scene_frame_at(build_context(), output_id, slide_index);
}

AutoAdvanceState Session::auto_advance(std::string_view stack_id) const
{
    const auto it = auto_advance_.find(stack_id);
    if (it == auto_advance_.end()) {
        return {};
    }
    return it->second;
}

void Session::set_auto_advance(std::string_view stack_id, AutoAdvanceState state)
{
    auto_advance_.insert_or_assign(std::string(stack_id), state);
}

std::vector<std::string> Session::advancing_stack_ids() const
{
    std::vector<std::string> ids;
    for (const auto& [id, state] : auto_advance_) {
        if (state.enabled) {
            ids.push_back(id);
        }
    }
    return ids;
}

namespace {

/// True when ``item`` is a non-divider row whose resolved target is ``stack_id``.
bool item_targets_stack(const Session& session,
                        const model::ServiceItem& item,
                        std::string_view stack_id)
{
    if (item.item_type == model::k_item_type_divider) {
        return false;
    }
    return session.resolve_planned_stack(item.item_type, model::service_item_stack_id(item))
        == stack_id;
}

} // namespace

int Session::transition_ms_on(std::string_view stack_id) const
{
    const auto* stack = find_stack(stack_id);
    if (stack == nullptr) {
        return 0;
    }
    int longest = 0;
    for (const auto& frame : stack->program.scene_frames(build_context())) {
        if (frame.content_transition.plays()) {
            longest = std::max(longest, frame.content_transition.duration_ms);
        }
        // A step's builds play to their end too.
        longest = std::max(longest, frame.builds_end_ms);
        if (frame.background_transition.plays()) {
            longest = std::max(longest, frame.background_transition.duration_ms);
        }
    }
    return longest;
}

ByOutput<int> Session::content_change_ms_by_output(std::string_view stack_id) const
{
    ByOutput<int> out;
    const auto* stack = find_stack(stack_id);
    if (stack == nullptr) {
        return out;
    }
    for (const auto& frame : stack->program.scene_frames(build_context())) {
        out[frame.output_id] =
            frame.content_transition.plays() ? frame.content_transition.duration_ms : 0;
    }
    return out;
}

ByOutput<model::AudioFades> Session::audio_fades_on(std::string_view stack_id) const
{
    ByOutput<model::AudioFades> out;
    const auto* stack = find_stack(stack_id);
    static const nlohmann::json k_none = nlohmann::json::object();
    const nlohmann::json* file = &k_none;
    if (stack != nullptr) {
        if (const auto it = audio_file_fades_.find(stack->program.playback().filename);
            it != audio_file_fades_.end()) {
            file = &it->second;
        }
    }
    for (const auto& oc : outputs_) {
        out[oc.id] = model::resolve_audio_fades(oc.audio_fades, service_audio_fades_, *file,
                                                theme_priority_);
    }
    return out;
}

void Session::set_service_audio_fades(nlohmann::json fades)
{
    service_audio_fades_ = model::set_audio_fades(fades);
}

void Session::set_audio_file_fades(std::map<std::string, nlohmann::json, std::less<>> by_file)
{
    audio_file_fades_ = std::move(by_file);
}

AdvanceTick Session::tick_auto_advance(std::string_view stack_id)
{
    AdvanceTick tick;
    const auto it = auto_advance_.find(stack_id);
    if (it == auto_advance_.end() || !it->second.enabled) {
        return tick;
    }
    const AutoAdvanceState state = it->second;

    // Auto-advance moves what is live, Preview or not.
    const Slot live{std::string(stack_id)};
    // 1. Ordinary case - another slide left on this stack.
    if (next_slide_on(live)) {
        tick.did_advance = true;
        return tick;
    }

    const bool focus_target = focused_stack_id_ == stack_id;

    // 2. Run on into the rundown, when configured to.
    if (state.through) {
        // Start from the selected row only when it belongs to this stack;
        // otherwise scan the whole rundown for rows that do.
        int from = service_item_index_;
        if (from >= 0 && from < static_cast<int>(service_items_.size())
            && !item_targets_stack(*this, service_items_[static_cast<std::size_t>(from)],
                                   stack_id)) {
            from = -1;
        }

        for (int j = from + 1; j < static_cast<int>(service_items_.size()); ++j) {
            const auto& item = service_items_[static_cast<std::size_t>(j)];
            if (item.item_type == model::k_item_type_divider) {
                if (state.stop_at_dividers) {
                    it->second.enabled = false;
                    tick.exhausted = true;
                    return tick;
                }
                continue;
            }
            if (!item_targets_stack(*this, item, stack_id)) {
                continue;
            }
            if (select_service_item(j, focus_target, Land::Live)) {
                tick.did_advance = true;
                tick.changed_item = true;
                return tick;
            }
        }

        if (state.loop) {
            // With stop_at_dividers the loop is bounded to the current section:
            // rewind to the row after the last divider preceding the selection.
            int section_start = 0;
            if (state.stop_at_dividers && from >= 0) {
                for (int i = 0; i < from; ++i) {
                    if (service_items_[static_cast<std::size_t>(i)].item_type == model::k_item_type_divider) {
                        section_start = i + 1;
                    }
                }
                while (section_start < static_cast<int>(service_items_.size())
                       && service_items_[static_cast<std::size_t>(section_start)].item_type
                           == model::k_item_type_divider) {
                    ++section_start;
                }
            }
            for (int k = section_start; k < static_cast<int>(service_items_.size()); ++k) {
                const auto& item = service_items_[static_cast<std::size_t>(k)];
                if (item.item_type == model::k_item_type_divider) {
                    if (state.stop_at_dividers) {
                        break;
                    }
                    continue;
                }
                if (!item_targets_stack(*this, item, stack_id)) {
                    continue;
                }
                if (select_service_item(k, focus_target, Land::Live)) {
                    tick.did_advance = true;
                    tick.changed_item = true;
                    return tick;
                }
            }
        }
    } else if (state.loop) {
        // 3. Loop within the current item.
        if (jump_to_line_on(live, 0)) {
            tick.did_advance = true;
            return tick;
        }
    }

    // 4. Nothing left to do - switch the stack off.
    it->second.enabled = false;
    tick.exhausted = true;
    return tick;
}

bool Session::prune_auto_advance()
{
    bool removed = false;
    for (auto it = auto_advance_.begin(); it != auto_advance_.end();) {
        if (find_stack(it->first) == nullptr) {
            it = auto_advance_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

} // namespace seventhslide::session
