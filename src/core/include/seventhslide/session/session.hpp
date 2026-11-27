#pragma once

// Session - live presentation command API; no global APP_STATE.
// Owns N StackRuntimes composited per output, and what the operator does to
// each output's picture (blank / freeze), and which outputs are on. Seam:
// plain C++20.

#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/model/audio_fades.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/model/stack.hpp"
#include "seventhslide/scene/scene_frame.hpp"
#include "seventhslide/session/auto_advance.hpp"
#include "seventhslide/session/stack_runtime.hpp"
#include "seventhslide/theme/resolved_theme.hpp"
#include "seventhslide/theme/theme_resolver.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace seventhslide::session {

/// Something each output has its own of, by output id.
template <class T>
using ByOutput = std::map<std::string, T, std::less<>>;

/// Where content sent to a stack goes.
enum class Land {
    /// Its Preview, when it has one, else its Program: anything the operator
    /// sends (the library, a service row, the web remote, Send to).
    Cue,
    /// Its Program, live at once, Preview or not (auto-advance, Also play).
    Live,
};

/**
 * Explicit live session - the Sunday-capable command surface for QML / remote.
 *
 * Owns outputs, stack runtimes, and the open service rundown. Does not own a
 * global singleton, HTTP, or DB connections.
 */
class Session {
public:
    explicit Session(layout::ITextMeasurer& measurer);

    // --- configuration ---
    /// Takes the outputs, giving any without a unique id a fresh one.
    void set_outputs(std::vector<model::OutputConfig> outputs);
    [[nodiscard]] const std::vector<model::OutputConfig>& outputs() const noexcept
    {
        return outputs_;
    }
    [[nodiscard]] std::vector<model::OutputConfig>& outputs() noexcept { return outputs_; }
    /// The outputs switched on, in order: all the show builds, plays and
    /// paints. An output switched off keeps its settings and is left out of
    /// everything else. Read again after ``rebuild``.
    [[nodiscard]] const std::vector<model::OutputConfig>& outputs_on() const noexcept
    {
        return off_output_ids_.empty() ? outputs_ : outputs_on_;
    }
    /// Switch output ``output_id`` on or off (rebuilds the live stacks). False
    /// when there is no such output, or when it is the last one on: one
    /// always is.
    [[nodiscard]] bool set_output_on(std::string_view output_id, bool on);
    /// The outputs to switch off, by id, all at once (a library opening);
    /// the rest are on. Should that leave none on, the first is on.
    void set_outputs_off(std::set<std::string, std::less<>> output_ids);
    [[nodiscard]] bool output_is_on(std::string_view output_id) const;
    [[nodiscard]] const std::set<std::string, std::less<>>& outputs_off() const noexcept
    {
        return off_output_ids_;
    }

    void set_theme_priority(std::vector<std::string> priority);
    [[nodiscard]] const std::vector<std::string>& theme_priority() const noexcept
    {
        return theme_priority_;
    }

    void set_ccli_licence_number(std::string number);
    [[nodiscard]] const std::string& ccli_licence_number() const noexcept
    {
        return ccli_licence_number_;
    }

    void set_stacks(std::vector<model::StackDef> defs, model::StackTypeDefaults defaults);
    [[nodiscard]] const std::vector<model::StackDef>& stack_defs() const noexcept
    {
        return stack_defs_;
    }
    [[nodiscard]] const model::StackTypeDefaults& type_defaults() const noexcept
    {
        return type_defaults_;
    }
    /// The stacks with a Preview, by id: each gets an empty one, and every
    /// other stack loses its own, with whatever was cued in it. Ids naming no
    /// stack are passed over. This computer's choice (library::ConsoleStore),
    /// so a stack the library adds later has none until it is given one.
    /// True when any stack gained or lost a Preview.
    bool set_preview_stacks(const std::set<std::string, std::less<>>& stack_ids);
    /// The ids of the stacks with a Preview.
    [[nodiscard]] std::set<std::string, std::less<>> preview_stack_ids() const;

    [[nodiscard]] std::string focused_stack_id() const;
    void set_focused_stack_id(std::string id);
    [[nodiscard]] std::string armed_send_to() const { return armed_send_to_; }
    void arm_send_to(std::string id);

    /// Library / ad-hoc send: armed target, then item override, then type default.
    [[nodiscard]] std::string resolve_target_stack(std::string_view item_type,
                                                   std::string_view item_override = {}) const;
    /// Rundown plan: item pin, then type default. Ignores the armed target button.
    [[nodiscard]] std::string resolve_planned_stack(std::string_view item_type,
                                                    std::string_view item_override = {}) const;
    /// Empty when ``companion_ids`` would be a valid also-play list for
    /// ``parent_id``; otherwise a short operator-facing reason. Reads the
    /// candidate list rather than the stored one, so a caller can validate
    /// before it writes anything.
    [[nodiscard]] std::string also_play_list_error(
        std::int64_t parent_id,
        const std::vector<std::int64_t>& companion_ids) const;
    /// Every item linked to ``item_id`` by Also play, ``item_id`` included,
    /// in rundown order; empty when it is not linked. Links are followed both
    /// ways, so the set is the same whichever member asks.
    [[nodiscard]] std::vector<std::int64_t> also_play_set(std::int64_t item_id) const;

    /// Every stack, in ``stack_defs`` order: paint order, back to front.
    [[nodiscard]] const std::vector<Stack>& stacks() const noexcept { return stacks_; }
    [[nodiscard]] const Stack* find_stack(std::string_view id) const noexcept;
    [[nodiscard]] Stack* find_stack(std::string_view id) noexcept;
    /// ``slot``'s runtime; null when there is no such stack, or it has no
    /// Preview and ``slot`` names one.
    [[nodiscard]] const StackRuntime* runtime(const Slot& slot) const noexcept;
    [[nodiscard]] StackRuntime* runtime(const Slot& slot) noexcept;
    [[nodiscard]] const Stack& focused_stack() const;
    [[nodiscard]] Stack& focused_stack();
    /// Stacks with something live (on their Program), in paint order.
    [[nodiscard]] std::vector<const Stack*> occupied_stacks() const;
    /// The frontmost stack with something live or cued (``holds_content``):
    /// where the focus goes when the one it was on is left with nothing.
    /// Null when no stack holds anything.
    [[nodiscard]] const Stack* frontmost_holding() const noexcept;

    // --- content commands (rebuild) ---
    // Each sends to ``stack_id`` (empty: the armed or default stack for the
    // content), focuses that stack, and returns the side it landed on. Landed
    // in a Preview, its video or audio waits, paused, for the take.
    Slot play_song(model::Song song, std::string stack_id = {}, Land land = Land::Cue);
    Slot play_bible(model::BiblePassage bible, std::string stack_id = {}, Land land = Land::Cue);
    Slot play_freeform(LiveFreeformContent content,
                       std::string stack_id = {},
                       Land land = Land::Cue);
    Slot play_media(LiveMediaContent media, std::string stack_id = {}, Land land = Land::Cue);

    void open_service(std::optional<std::int64_t> service_id,
                      theme::OutputThemeMap service_themes,
                      std::vector<model::ServiceItem> items);
    /// Select a rundown item onto its resolved stack.
    /// When ``focus_target`` is false, live content still updates but the
    /// operator focus tab is left alone (background auto-advance). Landed in
    /// a Preview, it is cued: the selected row and its Also play set wait for
    /// the take. Returns the side it landed on; none when ``index`` names no
    /// row.
    std::optional<Slot> select_service_item(int index,
                                            bool focus_target = true,
                                            Land land = Land::Cue);
    /// Patch a rundown row's snapshot blob without clearing live stacks.
    bool patch_service_item_data(std::int64_t item_id, nlohmann::json data);
    /// Swap in the open service's rows after an add / remove / reorder / edit.
    /// Live stacks, focus and the armed target are untouched; the selected row
    /// follows its item id (-1 if that item is gone).
    void replace_service_items(std::vector<model::ServiceItem> items);
    /// Re-show the edited content of rundown item ``item_id`` on every stack
    /// currently showing it, keeping each stack's place. A row made another
    /// kind of item loads anew, and in a Preview is cued as anything sent
    /// there is. Returns true if any stack was showing it.
    bool refresh_service_item(std::int64_t item_id);
    /// Re-show a library Freeform that was just saved on every stack playing
    /// that library item (``content.library_id``), keeping each stack's place.
    /// Stacks showing other content, including rundown snapshots of the same
    /// document, are untouched. Returns true if any stack was showing it.
    bool refresh_freeform(const LiveFreeformContent& content);
    /// Replace the open service's theme picks and refresh live stacks in place.
    void set_service_themes(theme::OutputThemeMap service_themes);
    /// Clear what stack ``id`` shows live, and its blank and freeze. Focused,
    /// it keeps the focus while its Preview holds something; otherwise the
    /// focus goes to the frontmost stack with something on either side
    /// (``frontmost_holding``).
    bool clear_stack(std::string_view id);
    /// Clear one side. A Program clears as ``clear_stack``; a Preview only
    /// drops what is cued, and its blank, and leaves what is live and its
    /// blank and freeze.
    /// The focus moves by the same rule as ``clear_stack``'s: only off a
    /// stack left with nothing on either side.
    bool clear(const Slot& slot);
    void clear_all_stacks();

    /// The take: stack ``stack_id``'s Preview goes live where it was cued, and
    /// what was live goes back to its Preview where it was. It changes by
    /// ``how`` (the content and the background alike) or, without one, as
    /// the theme says, until the stack next moves. Media that was playing
    /// plays on; paused, it plays from where it stands, unless its item waits
    /// for Play (Autoplay off); played to its end in Preview, it starts over.
    /// What went back is paused (the app lets it play on over the change
    /// first). A blank goes with its side: what was cued blanked goes live
    /// blanked, and what was live blanked comes back blanked; a freeze stays
    /// on the stack's outputs. The service's selected row follows what is
    /// live, and the rest
    /// of its Also play set goes live with it, but for a member already
    /// showing, which carries on. Refused (false) unless the stack has a
    /// Preview with something cued: an empty one cannot take the stack off
    /// the air.
    bool transition(std::string_view stack_id, std::optional<model::Transition> how = std::nullopt);

    /// Re-show live content after outputs, themes or settings changed: each
    /// built stack keeps its place rather than starting over.
    void rebuild();

    // --- navigation (no rebuild) ---
    /// Next / previous / jump on one stack. The focused stack is
    /// ``focused_stack()``; callers name it rather than going through a
    /// wrapper here.
    [[nodiscard]] bool next_slide_on(const Slot& slot);
    [[nodiscard]] bool jump_to_line_on(const Slot& slot, int line_index);
    /// ``line_index`` at build step ``step``, shown as built.
    [[nodiscard]] bool jump_to_step_on(const Slot& slot, int line_index, int step);
    /// Auto-advance: next on the stack that last received a service item.
    [[nodiscard]] bool next_service_slide();

    // --- per-stack auto-advance ---
    /// Configuration for one stack; a stack with no entry reports the defaults.
    [[nodiscard]] AutoAdvanceState auto_advance(std::string_view stack_id) const;
    void set_auto_advance(std::string_view stack_id, AutoAdvanceState state);
    /// Ids of every stack whose timer should currently be running.
    [[nodiscard]] std::vector<std::string> advancing_stack_ids() const;
    /// Run one step for ``stack_id``. Switches the stack off when exhausted.
    [[nodiscard]] AdvanceTick tick_auto_advance(std::string_view stack_id);
    /// How long what ``stack_id`` shows now takes to come in, on the output
    /// it takes longest on: its content or background transition, whichever
    /// is longer; 0 for a cut. Auto-advance counts a slide's time from the
    /// end of it, so a five-second slide is five seconds at full
    /// strength.
    [[nodiscard]] int transition_ms_on(std::string_view stack_id) const;
    /// How long what ``stack_id`` shows now takes to come in on each output
    /// that builds it, by output id: its content transition, 0 for a cut. A
    /// video's sound comes and goes with its picture by it, output by output
    /// (an output's Media theme's Video transition, or a take's own). An
    /// output that builds nothing of the stack is not named.
    [[nodiscard]] ByOutput<int> content_change_ms_by_output(std::string_view stack_id) const;
    /// How the audio item ``stack_id`` plays comes and goes on each output,
    /// by output id: the output's own fades, the open service's and the audio
    /// file's, in the theme priority's order (``model::resolve_audio_fades``).
    /// Every output is named; each hears the item fade by its own.
    [[nodiscard]] ByOutput<model::AudioFades> audio_fades_on(std::string_view stack_id) const;
    /// The fades the open service sets (only those fields), for every audio
    /// item while it is open; the library's to say as it opens a service and
    /// as they change. An empty object sets none.
    void set_service_audio_fades(nlohmann::json fades);
    /// The fades each audio file sets for itself, by filename: the library's
    /// to say as it opens and as they change.
    void set_audio_file_fades(std::map<std::string, nlohmann::json, std::less<>> by_file);
    /// Forget entries for stacks that no longer exist. Returns true if any went.
    bool prune_auto_advance();

    /// Hands out the player for one piece of video or audio content
    /// (``StackRuntime::media_instance``), of ``kind`` (Video or Audio), on
    /// ``side`` of its stack: the same one each time while that content stays.
    /// The app owns the players, and may hand a side none for now (a Preview
    /// whose item still plays out on the outputs after a take): its runtime
    /// then keeps what it is told until it is given one.
    using PlayerFor = std::function<media::IMediaPlayer*(
        std::uint64_t media_instance, model::ContentKind kind, StackSide side)>;

    /// Where the players come from; hands them out at once. Which content gets
    /// one is the session's to say (``attach_players``), which player the app's.
    void set_players(PlayerFor player_for);
    /// Hand the players out again: one for each video or audio item on
    /// either side of a stack, so every stack's plays at once and a Preview's
    /// can be played there; none to an item with no file (instance 0). A take
    /// swaps the sides, and their items keep their players until this runs.
    /// Call whenever live content changes. A player no runtime holds any
    /// more is not touched again, so the app may release it after this
    /// returns.
    void attach_players();
    /// Read every Program's playing file's position back from its player:
    /// what the outputs' frames say. A Preview's runtime keeps what it was
    /// told (paused where it was cued, or played there); its well reads the
    /// player itself.
    void sync_playback();
    /// A video object a stack shows, and the stack (4.7's 6).
    struct ObjectVideo {
        std::string stack_id;
        scene::SceneObjectVideo video;

        [[nodiscard]] bool operator==(const ObjectVideo&) const noexcept = default;
    };
    /// Every video object the stacks show now, back to front, blank or not:
    /// a blank hides a video, it does not stop it. One clip on two stacks
    /// is listed for the front one.
    [[nodiscard]] std::vector<ObjectVideo> object_videos() const;

    // --- blank / freeze ---
    // Two kinds, each with its own toggle: an output's (every layer on that
    // output) and a stack's (that stack's layer on every output). Held here,
    // for every stack, so content arriving on any stack (one that has never
    // played included) shows through the same state.
    //
    // Freeze holds what the output shows, layer by layer: content sent to any
    // stack, or a stack cleared, leaves it alone until it is unfrozen. Blank
    // beats freeze at both levels: a blanked stack blanks its layer, and a
    // blanked output blanks every layer, frozen or not. Blank hides the
    // picture only: media keeps playing under it. A blank goes in and lifts
    // by the slide's own transition, and the slide comes back as built.
    //
    // Global is not a third kind: it sets the output toggle of every output
    // on that is not exempt from it, so each output's own toggle is all there
    // is to show for it.
    /// Blank (or freeze) every output on that is not exempt, or, when every
    /// one already is, lift it from all of them.
    void toggle_global_blank();
    void toggle_global_freeze();
    void set_global_blank(bool on);
    void set_global_freeze(bool on);
    /// Every output on that is not exempt is blank (frozen); false when there
    /// is no such output.
    [[nodiscard]] bool global_blank() const;
    [[nodiscard]] bool global_freeze() const;
    /// Per output, by ``OutputConfig::id``. False if there is no such output.
    [[nodiscard]] bool toggle_output_blank(std::string_view output_id);
    [[nodiscard]] bool toggle_output_freeze(std::string_view output_id);
    [[nodiscard]] bool output_is_blank(std::string_view output_id) const;
    [[nodiscard]] bool output_is_frozen(std::string_view output_id) const;
    /// The output the operator's previews default to: the first named output
    /// that is on, else the first output if it is named. Null when there is
    /// none.
    [[nodiscard]] const model::OutputConfig* primary_output() const noexcept;
    /// Per stack: its layer on every output. False if there is no such stack,
    /// or the stack is empty and the toggle would turn on: an empty stack has
    /// nothing to hide or hold, and holding nothing would hide whatever is
    /// sent to it next. Turning off always works.
    [[nodiscard]] bool toggle_stack_blank(std::string_view stack_id);
    [[nodiscard]] bool toggle_stack_freeze(std::string_view stack_id);
    [[nodiscard]] bool stack_is_blank(std::string_view stack_id) const;
    [[nodiscard]] bool stack_is_frozen(std::string_view stack_id) const;
    /// A Preview's own blank: what is cued goes live blanked at the take,
    /// where the Program's blank comes back with what was live. It shows on
    /// no output, and goes with what is cued: something new cued, or the
    /// Preview cleared, it is lifted. False if the stack has no Preview, or
    /// nothing is cued and the toggle would turn on. Turning off always works.
    [[nodiscard]] bool toggle_preview_blank(std::string_view stack_id);
    [[nodiscard]] bool preview_is_blank(std::string_view stack_id) const;
    /// Lift every blank and freeze, of every output and every stack.
    void clear_blank_freeze();

    // --- accessors ---
    [[nodiscard]] SessionMode mode() const noexcept;
    [[nodiscard]] int service_item_index() const noexcept { return service_item_index_; }
    [[nodiscard]] const std::vector<model::ServiceItem>& service_items() const noexcept
    {
        return service_items_;
    }
    [[nodiscard]] std::optional<std::int64_t> service_id() const noexcept { return service_id_; }
    [[nodiscard]] const model::ServiceItem* current_service_item() const noexcept;

    [[nodiscard]] bool has_build() const noexcept;

    /// What every output shows: its stacks composited, blank and freeze
    /// applied. One frame per output, an empty one when nothing is live.
    [[nodiscard]] std::vector<scene::SceneFrame> scene_frames() const;
    /// The same for the output with ``OutputConfig::id`` ``output_id``.
    [[nodiscard]] std::optional<scene::SceneFrame> scene_frame(
        std::string_view output_id) const;
    /// ``slot``'s slide ``slide_index`` on that output, as content only (a
    /// thumbnail): no other stack, no blank or freeze, and its builds all
    /// played. Nothing for a side there is not.
    [[nodiscard]] std::optional<scene::SceneFrame> scene_frame_at(
        const Slot& slot,
        std::string_view output_id,
        int slide_index) const;

private:
    /// One stack's picture on one output: a layer of that output's frame.
    struct Layer {
        std::string stack_id;
        scene::SceneFrame frame;
    };
    /// The operator's own blank / freeze for one output.
    struct OutputToggles {
        bool blank = false;
        bool frozen = false;
    };
    /// One stack's blank / freeze. While frozen, ``held`` is the frame it
    /// keeps showing on each output, by output id; an output with none had
    /// nothing from this stack when it froze, and still has nothing.
    struct StackDisplay {
        bool blank = false;
        /// The Preview's blank, which swaps with ``blank`` at the take.
        bool preview_blank = false;
        bool frozen = false;
        std::map<std::string, scene::SceneFrame, std::less<>> held;
    };
    /// Each stack's Program's content frames, by stack index (empty when
    /// unoccupied).
    using StackFrames = std::vector<std::vector<scene::SceneFrame>>;

    /// Call ``fn`` with every side of every stack: each Program, and each
    /// Preview there is. For what follows content wherever it is (a rebuild,
    /// an edit, a clear); what the outputs show walks the Programs alone.
    /// ``fn`` takes the runtime, and the side it is on when it asks for it.
    template <typename Fn>
    void for_every_side(Fn&& fn)
    {
        const auto call = [&fn](StackRuntime& st, StackSide side) {
            if constexpr (std::is_invocable_v<Fn&, StackRuntime&, StackSide>) {
                fn(st, side);
            } else {
                fn(st);
            }
        };
        for (auto& stack : stacks_) {
            call(stack.program, StackSide::Program);
            if (stack.preview) {
                call(*stack.preview, StackSide::Preview);
            }
        }
    }

    /// Composited frames for every output, or only ``only_output_id`` when given.
    [[nodiscard]] std::vector<scene::SceneFrame> compose_frames(
        std::string_view only_output_id) const;
    [[nodiscard]] StackFrames stack_frames(const StackBuildContext& ctx,
                                           std::string_view only_output_id) const;
    /// ``oc``'s layers back to front before any blank: each enabled stack's
    /// content, or its held frame while that stack is frozen. Moves the
    /// frames it uses out of ``frames``.
    [[nodiscard]] std::vector<Layer> live_layers(const model::OutputConfig& oc,
                                                 StackFrames& frames) const;
    /// ``oc``'s frame from its layers, with stack and output blank / freeze applied.
    [[nodiscard]] scene::SceneFrame present(const model::OutputConfig& oc,
                                            std::vector<Layer> layers) const;
    [[nodiscard]] bool output_is_blank(const model::OutputConfig& oc) const;
    [[nodiscard]] bool output_is_frozen(const model::OutputConfig& oc) const;
    /// Whether every output on that ``exempt`` leaves to the global toggle
    /// has ``flag`` set; false when there is none.
    [[nodiscard]] bool global_on(bool model::OutputConfig::*exempt,
                                 bool OutputToggles::*flag) const;
    /// Set ``flag`` to ``on`` for every output on that ``exempt`` leaves to
    /// the global toggle.
    void set_global(bool model::OutputConfig::*exempt, bool OutputToggles::*flag, bool on);
    /// Hold the picture of every output that has just become frozen, and
    /// drop the hold of every output that no longer is (or no longer exists).
    void sync_output_holds();
    /// A blank came to or went from output ``output_id``: every stack's
    /// slide there settles (PlayerController::settle).
    void settle_for_blank(std::string_view output_id);
    /// Forget toggles and holds of outputs and stacks that no longer exist.
    void prune_display_state();
    /// Keep one output on, and ``outputs_on_`` in step with ``outputs_``.
    void sync_outputs_on();
    [[nodiscard]] StackBuildContext build_context() const;
    void ensure_default_stack();
    /// Where content sent to ``stack`` goes, by ``land``.
    [[nodiscard]] static StackSide landing_side(const Stack& stack, Land land) noexcept;
    /// After content landed on ``side`` of ``stack``: the stack has the
    /// focus, and its video or audio waits in a Preview. Library content
    /// live on the stack the service last played on drops the selected row.
    void after_send_to(Stack& stack, StackSide side, bool from_library);
    /// Content has just loaded in ``preview``: cued, nothing starts by
    /// landing there. Every way into a Preview comes through here.
    static void arrive_cued(StackRuntime& preview);
    /// Something new was cued on stack ``stack_id``: it is not blanked, as
    /// what it replaced may have been.
    void lift_preview_blank(std::string_view stack_id);
    /// The selected row was what stack ``stack_id`` showed, and is no longer:
    /// nothing is selected. Another stack's row is left alone.
    void drop_selected_row(std::string_view stack_id);
    /// ``stack`` was just cleared on a side: focused and left with nothing
    /// on either, the focus goes to ``frontmost_holding`` (the last stack
    /// with none).
    void refocus_if_emptied(const Stack& stack);
    [[nodiscard]] const model::OutputConfig* find_output(std::string_view output_id) const;
    [[nodiscard]] Stack& stack_or_focused(std::string_view stack_id);
    /// Where library content of ``item_type`` goes: ``stack_id`` when given,
    /// else ``resolve_target_stack``; the focused stack if that names none.
    [[nodiscard]] Stack& library_target(std::string_view item_type, std::string stack_id);
    [[nodiscard]] const model::ServiceItem* find_service_item(std::int64_t item_id) const noexcept;
    /// The stack ``item`` plays on by plan (its pin, else its type default).
    [[nodiscard]] std::string planned_stack_of(const model::ServiceItem& item) const;
    /// Why ``companion_id`` cannot also play with ``parent`` (planned on
    /// ``parent_stack``) while ``used`` stacks are taken; empty when it can.
    [[nodiscard]] std::string companion_error(const model::ServiceItem& parent,
                                              const std::string& parent_stack,
                                              std::int64_t companion_id,
                                              const std::vector<std::string>& used) const;
    /// What ``fire_also_play`` does with a member its stack already shows.
    enum class Companions {
        Restart,      ///< shown again from its start, as the item it goes with is
        KeepShowing,  ///< left as it is (a take, where nothing starts over)
    };
    /// Send the rest of this item's Also-play set onto their own stacks. Never
    /// steals operator focus or the selected rundown row.
    void fire_also_play(const model::ServiceItem& item,
                        const std::string& item_stack,
                        Companions showing);

    layout::ITextMeasurer& measurer_;
    std::vector<model::OutputConfig> outputs_;
    std::vector<std::string> theme_priority_;
    std::string ccli_licence_number_;

    std::vector<model::StackDef> stack_defs_;
    model::StackTypeDefaults type_defaults_;
    std::vector<Stack> stacks_;
    std::string focused_stack_id_;
    std::string service_stack_id_;
    std::string armed_send_to_;
    PlayerFor player_for_;

    /// By output id.
    std::map<std::string, OutputToggles, std::less<>> output_toggles_;
    /// The outputs switched off, by id, and (while there are any) a copy of
    /// the rest, which ``outputs_on`` reads.
    std::set<std::string, std::less<>> off_output_ids_;
    std::vector<model::OutputConfig> outputs_on_;
    /// What each frozen output keeps showing, by output id.
    std::map<std::string, std::vector<Layer>, std::less<>> output_holds_;
    /// By stack id.
    std::map<std::string, StackDisplay, std::less<>> stack_display_;

    std::optional<std::int64_t> service_id_;
    theme::OutputThemeMap service_themes_;
    nlohmann::json service_audio_fades_ = nlohmann::json::object();
    std::map<std::string, nlohmann::json, std::less<>> audio_file_fades_;
    std::vector<model::ServiceItem> service_items_;
    int service_item_index_ = -1;
    /// stack id -> auto-advance config. Absent means "off, defaults".
    std::map<std::string, AutoAdvanceState, std::less<>> auto_advance_;
};

} // namespace seventhslide::session
