#include "session_remote_bridge.hpp"

#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "library_models.hpp"
#include "line_map_model.hpp"
#include "live_controller.hpp"
#include "live_library_controller.hpp"
#include "media_controller.hpp"
#include "media_thumb_provider.hpp"
#include "output_list_model.hpp"
#include "preview_job.hpp"
#include "qml_ids.hpp"
#include "qvariant_json.hpp"
#include "refusals.hpp"
#include "service_item_model.hpp"
#include "song_list_model.hpp"
#include "session_facade.hpp"
#include "stack_controller.hpp"
#include "side_view.hpp"
#include "web_library_controller.hpp"
#include "web_thumbs.hpp"
#include "glyphs.hpp"

#include <seventhslide/db/search_text.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/bible_lookup.hpp>
#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/media_listing.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/web_page_repository.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/model/transition.hpp>
#include <seventhslide/parse/bible.hpp>
#include <seventhslide/util/strings.hpp>

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::app {

using remote::CommandResult;
using remote::Section;

namespace {

/// Tints the stack strip paints its glyphs with: Style.textMuted and
/// Style.textStrong, as the desktop controller's stack tabs use them. Copied
/// because C++ cannot read the QML Style; keep the two in step.
constexpr auto k_stack_icon_idle = "#8a8a8a";
constexpr auto k_stack_icon_active = "#ffffff";

QString qs(const nlohmann::json& args, const char* key, const QString& fallback = {})
{
    const auto it = args.find(key);
    return it != args.end() && it->is_string()
        ? QString::fromStdString(it->get<std::string>())
        : fallback;
}

std::int64_t qi(const nlohmann::json& args, const char* key, std::int64_t fallback = -1)
{
    const auto it = args.find(key);
    return it != args.end() && it->is_number_integer() ? it->get<std::int64_t>() : fallback;
}

double qd(const nlohmann::json& args, const char* key, double fallback = 0.0)
{
    const auto it = args.find(key);
    return it != args.end() && it->is_number() ? it->get<double>() : fallback;
}

bool qb(const nlohmann::json& args, const char* key, bool fallback = false)
{
    const auto it = args.find(key);
    return it != args.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

/// While alive, the session's focused stack is the remote's own tab, so
/// everything that reads "the focused stack" describes what the remote shows.
/// Emits nothing, and puts the console's focus back when it goes.
class TabLens {
public:
    TabLens(session::Session& session, const std::string& view)
        : session_(session)
        , console_(session.focused_stack_id())
        , active_(!view.empty() && view != console_ && session.find_stack(view) != nullptr)
    {
        if (active_) {
            session_.set_focused_stack_id(view);
        }
    }
    ~TabLens()
    {
        if (active_) {
            session_.set_focused_stack_id(console_);
        }
    }
    TabLens(const TabLens&) = delete;
    TabLens& operator=(const TabLens&) = delete;

private:
    session::Session& session_;
    std::string console_;
    bool active_;
};

/// A command the remote refused, and why (the phone shows it as it is).
CommandResult refused(const QString& why)
{
    return CommandResult::fail(why.toStdString());
}

/// Facade calls that answer bool: false means "the session refused", for
/// ``why``.
CommandResult from_bool(bool ok, const QString& why)
{
    return ok ? CommandResult::good() : refused(why);
}

/// Facade calls that answer an error string, empty on success.
CommandResult from_error(const QString& error)
{
    return error.isEmpty() ? CommandResult::good() : refused(error);
}

/// Encode for the wire. Photographic content goes out as JPEG; rendered slides
/// keep PNG so text edges stay crisp on a phone screen. Reads only ``image``,
/// so it may run on any thread.
remote::ThumbResult encode(const QImage& image, bool photographic)
{
    if (image.isNull()) {
        return {};
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    const bool ok = photographic ? image.save(&buffer, "JPEG", 82)
                                 : image.save(&buffer, "PNG");
    if (!ok) {
        return {};
    }
    remote::ThumbResult out;
    out.ok = true;
    out.mime = photographic ? "image/jpeg" : "image/png";
    out.bytes.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    return out;
}

/// ``job`` waiting to be painted and encoded on the server's pool; none when
/// there is nothing to show.
remote::PendingThumb pending(PreviewJob job, bool photographic)
{
    if (job.image.isNull() && !job.still && !job.lay_out && !job.then) {
        return {};
    }
    return {[job = std::move(job), photographic] {
        return encode(finish_preview(job), photographic);
    }};
}

// What each model row sends, under the names the web client reads. Listed,
// not taken from roleNames(): QML-only roles (the line map's cursor flags and
// styled text, the output list's screen fields) stay off the wire, and
// renaming a role for QML cannot change what a phone receives.
constexpr WireField k_service_item_fields[] = {
    {"title", ServiceItemModel::TitleRole},
    {"itemType", ServiceItemModel::ItemTypeRole},
    {"itemIndex", ServiceItemModel::ItemIndexRole},
    {"itemId", ServiceItemModel::ItemIdRole},
    {"selected", ServiceItemModel::SelectedRole},
    {"hasOverrides", ServiceItemModel::HasOverridesRole},
    {"displayNumber", ServiceItemModel::DisplayNumberRole},
    {"folderImages", ServiceItemModel::FolderImagesRole},
    {"imageFile", ServiceItemModel::ImageFileRole},
    {"stackId", ServiceItemModel::StackIdRole},
    {"stackIcon", ServiceItemModel::StackIconRole},
    {"alsoPlayIds", ServiceItemModel::AlsoPlayIdsRole},
    {"alsoPlayWith", ServiceItemModel::AlsoPlayWithRole},
    {"cued", ServiceItemModel::CuedRole},
};
// No current / next / visible flags: the client derives them from the cursor
// and the slide maps, and per-row flags would change this section (and re-send
// every lyric) on each next/prev. ``text`` is plain; the styled copy is the
// desktop list's.
constexpr WireField k_line_fields[] = {
    {"lineIndex", LineMapModel::LineIndexRole},
    {"text", LineMapModel::TextRole},
    {"label", LineMapModel::LabelRole},
};
// Status only, of the outputs on. Screen assignment and open state are
// operator concerns, and configuration is out of scope for the remote.
constexpr WireField k_output_fields[] = {
    {"id", OutputListModel::IdRole},
    {"name", OutputListModel::NameRole},
    {"isBlank", OutputListModel::IsBlankRole},
    {"isFrozen", OutputListModel::IsFrozenRole},
};

/// Slice ``rows`` to the requested window, reporting the pre-slice total.
nlohmann::json page(nlohmann::json rows, int offset, int limit)
{
    const auto total = static_cast<int>(rows.size());
    nlohmann::json window = nlohmann::json::array();
    for (int i = offset; i < total && i < offset + limit; ++i) {
        window.push_back(rows[static_cast<std::size_t>(i)]);
    }
    return {{"total", total}, {"offset", offset}, {"rows", std::move(window)}};
}

/// Resolve a scripture reference against a specific bible without touching the
/// operator console's browse selection.
CommandResult resolve_bible_ref(db::BibleRepository& repo, std::int64_t bible_id,
                                const QString& reference)
{
    const auto found = db::resolve_reference(repo, bible_id, reference.toStdString());
    if (!found.ok) {
        return CommandResult::good(nlohmann::json{
            {"success", false}, {"message", found.message}});
    }

    nlohmann::json verse_rows = nlohmann::json::array();
    for (const auto& v : found.verses) {
        verse_rows.push_back({{"verse_num", v.verse_num}, {"text", v.text}});
    }

    return CommandResult::good(nlohmann::json{
        {"success", true},
        {"bible_id", bible_id},
        {"book", found.book},
        {"chapter", found.chapter},
        {"verse_start", found.verse_start},
        {"verse_end", found.verse_end},
        {"ref", found.ref},
        {"verses", std::move(verse_rows)},
    });
}

} // namespace

SessionRemoteBridge::SessionRemoteBridge(SessionFacade& facade) noexcept
    : facade_(facade)
    , media_thumbs_(std::make_unique<MediaThumbProvider>(
          [&facade](const QString& subdir, const QString& filename) {
              return facade.media()->resolve_media_path(subdir, filename);
          },
          [&facade]() { return facade.open_library().media_root_any_thread(); }))
{
}

SessionRemoteBridge::~SessionRemoteBridge() = default;

// ---------------------------------------------------------------- sections --

void SessionRemoteBridge::settle(remote::View& view) const
{
    // No tab yet, or it was removed: start from the console's, then keep it
    // separately from here on.
    const auto& session = facade_.session();
    if (view.stack.empty() || session.find_stack(view.stack) == nullptr) {
        view.stack = session.focused_stack_id();
    }
}

std::string SessionRemoteBridge::primary_output_id() const
{
    const auto* oc = facade_.session().primary_output();
    return oc != nullptr ? oc->id : std::string();
}

std::string SessionRemoteBridge::shown_stack(const remote::View& view) const
{
    const auto& session = facade_.session();
    const auto* stack = session.find_stack(view.stack);
    if (stack == nullptr || stack->holds_content()) {
        return view.stack;
    }
    // The tab was left with nothing live or cued: show what the console
    // would move to after a clear.
    const auto* front = session.frontmost_holding();
    return front != nullptr ? front->id() : view.stack;
}

nlohmann::json SessionRemoteBridge::section(Section s, const remote::View& view) const
{
    const TabLens lens(facade_.session(), shown_stack(view));
    return section_for(s);
}

nlohmann::json SessionRemoteBridge::section_for(Section s) const
{
    const auto& stack = facade_.session().focused_stack();
    const auto* cued = stack.side(session::StackSide::Preview);
    switch (s) {
    case Section::Session: return section_session();
    case Section::Stacks: return section_stacks();
    case Section::Service: return section_service();
    case Section::Lines: return side_lines(*facade_.program(), &stack.program);
    case Section::Outputs: return section_outputs();
    case Section::Images: return side_images(&stack.program);
    case Section::Advance: return section_advance();
    case Section::Preview: return section_preview();
    case Section::PreviewLines: return side_lines(*facade_.preview(), cued);
    case Section::PreviewImages: return side_images(cued);
    }
    return nlohmann::json::object();
}

nlohmann::json SessionRemoteBridge::side_fields(SideView& side,
                                                const session::StackRuntime* st) const
{
    // The rundown row the side shows, as on the console.
    int row = -1;
    if (const auto id = st != nullptr ? st->service_item_id() : std::nullopt) {
        const auto& items = facade_.session().service_items();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].item_id == id) {
                row = static_cast<int>(i);
                break;
            }
        }
    }
    const QString title = st != nullptr ? facade_.live()->title_of_stack(*st) : QString();
    nlohmann::json body = {
        {"lineCursor", side.lineCursor()},
        {"totalLines", side.totalLines()},
        // The build step on the slide: "step 2 of 3".
        {"stepCursor", side.currentStep()},
        {"totalSteps", side.step_count()},
        {"serviceItemIndex", row},
        {"currentTitle", title.toStdString()},
        {"imageMode", side.imageMode()},
        {"videoMode", side.videoMode()},
        // The transport fields cover audio as well; the wire keeps their names.
        {"mediaTransport", side.mediaTransport()},
        {"videoPlaying", side.mediaPlaying()},
        {"videoPositionSec", side.mediaPositionSec()},
        {"videoDurationSec", side.mediaDurationSec()},
        {"currentImageFilename", side.currentImageFilename().toStdString()},
        {"freeformGallery", side.freeformGallery()},
        // A web page: where it is now, and why it does not show.
        {"webMode", side.webMode()},
        {"webUrl", side.webUrl().toStdString()},
        {"webLoading", side.webLoading()},
        {"webProblem", side.webProblem().toStdString()},
        {"webCanGoBack", side.webCanGoBack()},
        // A live source: why it does not show, and whether its picture
        // stopped coming.
        {"liveSourceMode", side.liveSourceMode()},
        {"liveSourceProblem", side.liveSourceProblem().toStdString()},
        {"liveSourceLost", side.liveSourceLost()},
    };
    // Preview revision only belongs on a moving picture: video, a web page,
    // a live source. Putting it on every frame of the section would
    // hash-change it on clock ticks and make the remote rebuild thumbs that
    // have not actually changed.
    if (side.videoMode()) {
        body["previewRevision"] = facade_.preview_revision();
    } else if (side.webMode() || side.liveSourceMode()) {
        body["previewRevision"] = source_frames_;
    }
    nlohmann::json slides = nlohmann::json::array();
    if (st != nullptr) {
        for (const auto& oc : st->player().outputs()) {
            slides.push_back(oc.index);
        }
    }
    body["outputSlides"] = std::move(slides);
    return body;
}

nlohmann::json SessionRemoteBridge::section_session() const
{
    nlohmann::json body = side_fields(*facade_.program(), &facade_.session().focused_stack().program);
    body.update({
        {"mode", facade_.live()->mode().toStdString()},
        {"hasBuild", facade_.has_build()},
        {"globalBlank", facade_.live()->global_blank()},
        {"globalFreeze", facade_.live()->global_freeze()},
        {"openServiceId", facade_.openServiceId()},
        {"openServiceName", facade_.library()->openServiceName().toStdString()},
        // Outputs are named by id on the wire (preview URLs, the output
        // toggles); the name is for showing.
        {"primaryOutputId", primary_output_id()},
        {"primaryOutputName", facade_.primary_output_name().toStdString()},
        {"advancingStackIds", to_json(QVariant::fromValue(facade_.stacks()->advancingStackIds()))},
    });
    return body;
}

nlohmann::json SessionRemoteBridge::section_preview() const
{
    const auto& stack = facade_.session().focused_stack();
    const auto* cued = stack.side(session::StackSide::Preview);
    nlohmann::json body = side_fields(*facade_.preview(), cued);
    // Whose Preview it is: the stack a take or a Clear from it names, which
    // may arrive before the stacks section says the tab moved.
    body["stackId"] = stack.id();
    // The tab's stack has a Preview, and something is cued in it.
    body["available"] = cued != nullptr;
    body["occupied"] = cued != nullptr && cued->occupied();
    body["hasBuild"] = cued != nullptr && cued->has_build();
    // Blanked, what is cued goes live blanked.
    body["blank"] = facade_.session().preview_is_blank(stack.id());
    return body;
}

nlohmann::json SessionRemoteBridge::section_stacks() const
{
    // Stack icons are inline SVG data: URIs, so the client can render them
    // without another round trip. The strip tints its glyph by state and a
    // data: URI cannot be recoloured once it is in an <img>, so each tint
    // travels as its own URL rather than as a CSS filter approximating one.
    // The untinted iconUrl is a service row's mark of its stack.
    const auto attach_icons = [](nlohmann::json& list) {
        if (!list.is_array()) {
            return;
        }
        for (auto& d : list) {
            if (!d.is_object() || !d.contains("icon") || !d["icon"].is_string()) {
                continue;
            }
            const std::string icon = d["icon"].get<std::string>();
            const auto url = [&icon](const QString& ink) {
                return glyphs::data_url(glyphs::svg(icon, ink, 2)).toStdString();
            };
            d["iconUrl"] = url(QString());
            d["iconUrlIdle"] = url(QLatin1String(k_stack_icon_idle));
            d["iconUrlActive"] = url(QLatin1String(k_stack_icon_active));
        }
    };

    nlohmann::json defs = to_json(QVariant::fromValue(facade_.stacks()->stacks()));
    attach_icons(defs);
    // Every stack with something live or cued, as the console's strip lists
    // them.
    nlohmann::json occupied = to_json(QVariant::fromValue(facade_.stacks()->occupiedStacks()));
    attach_icons(occupied);

    return {
        {"defs", std::move(defs)},
        {"occupied", std::move(occupied)},
        {"focusedStackId", facade_.stacks()->focusedStackId().toStdString()},
        {"armedSendToId", facade_.stacks()->armedSendToId().toStdString()},
        {"showStackTabs", facade_.stacks()->showStackTabs()},
        // How long this computer's Fade takes, as Transition's menu offers it.
        {"takeFadeMs", facade_.stacks()->takeFadeMs()},
    };
}

nlohmann::json SessionRemoteBridge::section_service() const
{
    return {{"items", model_rows(*facade_.serviceItems(), k_service_item_fields)}};
}

nlohmann::json SessionRemoteBridge::side_lines(SideView& side,
                                               const session::StackRuntime* st) const
{
    nlohmann::json rows = model_rows(*side.lines(), k_line_fields);
    nlohmann::json maps = nlohmann::json::array();
    // Where next / prev go from each line, by the rule the session moves by.
    // The client looks its targets up here, from a cursor the server may not
    // have reached yet, instead of working them out a second way. Per line,
    // so the section still does not change when the cursor moves.
    nlohmann::json next = nlohmann::json::array();
    nlohmann::json prev = nlohmann::json::array();
    if (st != nullptr) {
        const auto& player = st->player();
        for (const auto& oc : player.outputs()) {
            maps.push_back(oc.line_to_slide);
        }
        for (int line = 0; line < player.total_lines(); ++line) {
            next.push_back(player.find_next_change_line(line, +1).value_or(-1));
            prev.push_back(line > 0 ? player.find_next_change_line(line, -1).value_or(0) : -1);
        }
    }
    // Which deck the rows are, and which build of it: slide thumbnails name
    // both, so they change exactly when the slides do.
    return {{"stackId", facade_.session().focused_stack_id()},
            {"build", st != nullptr ? st->build_id() : 0},
            {"rows", std::move(rows)},
            {"slideMaps", std::move(maps)},
            {"nextChange", std::move(next)},
            {"prevChange", std::move(prev)}};
}

nlohmann::json SessionRemoteBridge::section_outputs() const
{
    return {{"list", model_rows(*facade_.outputsOn(), k_output_fields)}};
}

nlohmann::json SessionRemoteBridge::side_images(const session::StackRuntime* st)
{
    if (st == nullptr || !st->image_mode_active()) {
        return {{"active", false}, {"images", nlohmann::json::array()}, {"index", -1}};
    }
    const auto& playlist = st->image_playlist();
    nlohmann::json names = nlohmann::json::array();
    for (const std::string& f : playlist.images) {
        names.push_back(f);
    }
    return {
        {"active", true},
        {"folderName", playlist.folder_name},
        {"images", std::move(names)},
        {"index", playlist.index},
    };
}

nlohmann::json SessionRemoteBridge::section_advance() const
{
    nlohmann::json byStack = nlohmann::json::object();
    const auto& session = facade_.session();
    for (const auto& def : session.stack_defs()) {
        const auto state = session.auto_advance(def.id);
        byStack[def.id] = {
            {"enabled", state.enabled},
            {"intervalSec", state.interval_ms / 1000},
            {"loop", state.loop},
            {"through", state.through},
            {"stopAtDividers", state.stop_at_dividers},
        };
    }
    return {{"byStack", std::move(byStack)}};
}

// ---------------------------------------------------------------- commands --

namespace {

/// The ids in ``args[key]``, an array of integers (the protocol checked).
QVariantList id_list(const nlohmann::json& args, const char* key)
{
    QVariantList ids;
    const auto it = args.find(key);
    if (it != args.end() && it->is_array()) {
        for (const auto& e : *it) {
            if (e.is_number_integer()) {
                ids.push_back(static_cast<qint64>(e.get<std::int64_t>()));
            }
        }
    }
    return ids;
}

int arg_int(const nlohmann::json& args, const char* key, int fallback = -1)
{
    return static_cast<int>(qi(args, key, fallback));
}

/// ``key`` was sent (the protocol reads a null as not sent).
bool sent(const nlohmann::json& args, const char* key)
{
    const auto it = args.find(key);
    return it != args.end() && !it->is_null();
}

} // namespace

CommandResult SessionRemoteBridge::invoke(remote::Command command,
                                          const nlohmann::json& a,
                                          remote::View& view)
{
    // Picking a tab on a remote changes only that remote's tab.
    if (command == remote::Command::SetFocusedStackId) {
        const auto id = qs(a, "stackId").toStdString();
        if (facade_.session().find_stack(id) == nullptr) {
            return refused(refusal::stack_gone());
        }
        view.stack = id;
        return CommandResult::good();
    }
    // Everything else runs against the remote's tab and leaves the console's
    // focus, and every other remote's tab, where it was. Content sent live to
    // another stack takes this remote's tab with it.
    std::string stack = shown_stack(view);
    CommandResult result = CommandResult::good();
    facade_.run_in_view(stack, [&] { result = invoke_command(command, a); });
    view.stack = std::move(stack);
    return result;
}

CommandResult SessionRemoteBridge::invoke_command(remote::Command command, const nlohmann::json& a)
{
    using remote::Command;
    auto* library = facade_.library();
    auto* stacks = facade_.stacks();
    auto* media = facade_.media();
    auto* freeform = facade_.freeformLibrary();
    const int at_index = arg_int(a, "atIndex");
    // The side a transport command works: the Program, unless it names the
    // Preview.
    SideView* side = facade_.program();
    if (const QString name = qs(a, "side", QStringLiteral("program"));
        name == QLatin1String("preview")) {
        if (!facade_.session().focused_stack().preview) {
            return refused(tr("This stack has no Preview."));
        }
        side = facade_.preview();
    } else if (name != QLatin1String("program")) {
        return refused(tr("A side is “program” or “preview”."));
    }

    switch (command) {
    // Transport
    // Running off either end does nothing, quietly, as on the console: the
    // remote must not toast at the end of every song. (The client predicts
    // from the same per-line targets the server moves by, see side_lines,
    // so it never waits on a move that did not happen.)
    case Command::NextSlide:
        (void)side->nextSlide();
        return CommandResult::good();
    case Command::PrevSlide:
        (void)side->prevSlide();
        return CommandResult::good();
    case Command::JumpToLine:
        return from_bool(side->jumpToLine(arg_int(a, "lineIndex")),
                         tr("That line is not in what is showing."));
    case Command::NextSlideOnStack:
        return from_bool(facade_.live()->next_slide_on_stack(qs(a, "stackId")),
                         tr("There is no next slide."));
    case Command::JumpToLineOnStack:
        return from_bool(facade_.live()->jump_to_line_on_stack(qs(a, "stackId"), arg_int(a, "lineIndex")),
                         tr("That line is not in what is showing."));
    case Command::NextServiceSlide:
        return from_bool(stacks->next_service_slide(), tr("There is nothing to go on to."));

    // Blank, freeze
    case Command::ToggleGlobalBlank:
        facade_.live()->toggleGlobalBlank();
        return CommandResult::good();
    case Command::ToggleGlobalFreeze:
        facade_.live()->toggleGlobalFreeze();
        return CommandResult::good();
    case Command::ToggleOutputBlank:
        return from_bool(facade_.live()->toggleOutputBlank(qs(a, "outputId")),
                         refusal::output_gone());
    case Command::ToggleOutputFreeze:
        return from_bool(facade_.live()->toggleOutputFreeze(qs(a, "outputId")),
                         refusal::output_gone());
    case Command::ToggleStackBlank:
        if (side == facade_.preview()) {
            return from_bool(stacks->togglePreviewBlank(qs(a, "stackId")),
                             tr("That stack is no longer there, or nothing is cued in its Preview."));
        }
        return from_bool(stacks->toggleStackBlank(qs(a, "stackId")),
                         tr("That stack is no longer there, or nothing is live on it."));
    case Command::ToggleStackFreeze:
        return from_bool(stacks->toggleStackFreeze(qs(a, "stackId")),
                         tr("That stack is no longer there, or nothing is live on it."));
    case Command::ClearBlankFreeze:
        facade_.live()->clearBlankFreeze();
        return CommandResult::good();

    // Stacks
    case Command::SetFocusedStackId:
        return CommandResult::good();  // handled in invoke()
    case Command::ArmSendTo:
        stacks->armSendTo(qs(a, "stackId"));
        return CommandResult::good();
    case Command::ClearStack:
        return from_error(stacks->clearStack(qs(a, "stackId")));
    case Command::ClearAllStacks:
        stacks->clearAllStacks();
        return CommandResult::good();
    case Command::Transition: {
        const QString stack = qs(a, "stackId");
        if (!sent(a, "type")) {
            if (sent(a, "durationMs") || sent(a, "direction") || sent(a, "easing")) {
                return refused(tr("A transition’s length, direction and easing go with its type."));
            }
            return from_error(stacks->transition(stack));
        }
        // Named as core names them; one it does not know is refused.
        return from_error(stacks->transitionWith(
            stack, qs(a, "type"), arg_int(a, "durationMs", model::k_default_transition_ms),
            qs(a, "direction"), qs(a, "easing")));
    }
    case Command::ClearPreview:
        return from_error(stacks->clearPreview(qs(a, "stackId")));
    case Command::SetTakeFade:
        return from_error(stacks->setTakeFadeMs(arg_int(a, "durationMs")));
    case Command::SetServiceItemStack:
        return from_error(library->set_service_item_stack(qi(a, "itemId"), qs(a, "stackId")));
    case Command::SetServiceItemAlsoPlay:
        return from_error(
            library->set_service_item_also_play(qi(a, "itemId"), id_list(a, "companionIds")));
    case Command::LinkServiceItems:
        return from_error(library->linkServiceItems(id_list(a, "itemIds")));
    case Command::UnlinkServiceItem:
        return from_error(library->unlinkServiceItem(qi(a, "itemId")));

    // Per-stack auto-advance: what the arguments leave out stays as it is.
    case Command::SetAutoAdvance: {
        const QString stack = qs(a, "stackId");
        const auto current = facade_.session().auto_advance(stack.toStdString());
        return from_error(stacks->setAutoAdvance(
            stack, qb(a, "enabled", current.enabled),
            arg_int(a, "intervalSec", current.interval_ms / 1000), qb(a, "loop", current.loop),
            qb(a, "through", current.through),
            qb(a, "stopAtDividers", current.stop_at_dividers)));
    }

    // Media transport
    case Command::ImageGoto:
        return from_bool(side->jumpToLine(arg_int(a, "index")),
                         tr("That picture is not in what is showing."));
    case Command::VideoPlay:
        return from_bool(side->mediaPlay(), tr("No video or sound is playing."));
    case Command::VideoPause:
        return from_bool(side->mediaPause(), tr("No video or sound is playing."));
    case Command::VideoSeek:
        return from_bool(side->mediaSeek(qd(a, "positionSec")),
                         tr("No video or sound is playing."));
    case Command::VideoRestart:
        return from_bool(side->mediaRestart(), tr("No video or sound is playing."));
    case Command::WebBack:
        return from_bool(facade_.program()->webGoBack(), tr("There is no page before this one."));
    case Command::WebReload:
        return from_bool(facade_.program()->reloadWeb(), tr("No web page is live."));

    // Service rundown
    case Command::OpenService:
        return from_bool(library->openService(qi(a, "serviceId")), refusal::service_gone());
    case Command::CloseService:
        return from_error(library->closeService());
    case Command::SelectServiceItem:
        return from_bool(facade_.live()->selectServiceItem(arg_int(a, "index")),
                         refusal::service_item_gone());
    case Command::SelectServiceItemAtImage:
        return from_bool(
            facade_.live()->selectServiceItemAtImage(arg_int(a, "index"), arg_int(a, "imageIndex")),
            refusal::service_item_gone());
    case Command::AddSongToService:
        return from_bool(library->addSongToService(qi(a, "songId"), at_index),
                         tr("The song could not be added to the service."));
    case Command::AddFreeformToService:
        return from_bool(freeform->addFreeformToService(qi(a, "freeformId"), at_index),
                         tr("The item could not be added to the service."));
    case Command::AddBibleRefToService:
        return from_bool(library->add_bible_ref_to_service(qi(a, "bibleId"), qs(a, "book"),
                                                       arg_int(a, "chapter"),
                                                       arg_int(a, "verseStart"),
                                                       arg_int(a, "verseEnd"), at_index),
                         refusal::passage_not_found());
    case Command::AddImageToService:
        return from_bool(media->addImageToService(qs(a, "filename"), at_index),
                         tr("The picture could not be added to the service."));
    case Command::AddVideoToService:
        return from_bool(media->addVideoToService(qs(a, "filename"), at_index),
                         tr("The video could not be added to the service."));
    case Command::AddImageFolderToService:
        return from_bool(media->addImageFolderToService(qi(a, "folderId"), at_index),
                         tr("The folder could not be added to the service."));
    case Command::AddVideoFolderToService:
        return from_bool(media->addVideoFolderToService(qi(a, "folderId"), at_index),
                         tr("The folder could not be added to the service."));
    case Command::AddFreeformFolderToService:
        return from_bool(freeform->addFreeformFolderToService(qi(a, "folderId"), at_index),
                         tr("The folder could not be added to the service."));
    case Command::AddDividerToService:
        return from_bool(library->addDividerToService(qs(a, "title"), at_index),
                         tr("The divider could not be added to the service."));
    case Command::RemoveServiceItem:
        return from_bool(library->remove_service_item(qi(a, "itemId")),
                         refusal::service_item_gone());
    case Command::MoveServiceItemTo:
        return from_bool(
            library->moveServiceItemTo(arg_int(a, "fromIndex"), arg_int(a, "toIndex")),
            tr("The item could not be moved."));
    case Command::ReorderServiceItems:
        return from_bool(library->reorderServiceItems(id_list(a, "orderedItemIds")),
                         tr("The service could not be put in that order."));

    // Service library: rename and delete only
    case Command::RenameService:
        return from_error(library->renameService(qi(a, "serviceId"), qs(a, "name")));
    case Command::DeleteService:
        return from_error(library->deleteService(qi(a, "serviceId")));

    // Library: play only, never change
    case Command::PlaySong:
        return from_bool(facade_.live()->playSong(qi(a, "songId")), refusal::song_gone());
    case Command::PlayFreeform:
        return from_bool(facade_.live()->playFreeform(qi(a, "freeformId")),
                         refusal::freeform_gone());
    case Command::PlayImage:
        return from_bool(facade_.live()->playImage(qs(a, "filename")), refusal::media_file_gone());
    case Command::PlayImageFolder:
        return from_bool(media->playImageFolder(qi(a, "folderId"), arg_int(a, "startIndex", 0)),
                         refusal::folder_gone());
    case Command::PlayVideo:
        return from_bool(facade_.live()->playVideo(qs(a, "filename"), qb(a, "loop")),
                         refusal::media_file_gone());
    case Command::PlayBibleRef:
        return from_bool(facade_.live()->play_bible_ref(qi(a, "bibleId"), qs(a, "book"),
                                              arg_int(a, "chapter"), arg_int(a, "verseStart"),
                                              arg_int(a, "verseEnd")),
                         refusal::passage_not_found());
    case Command::PlayAudio:
        return from_bool(facade_.live()->playAudio(qs(a, "filename"), qb(a, "loop")),
                         refusal::media_file_gone());
    case Command::PlayWebPage:
        return from_bool(facade_.webLibrary()->playPage(qi(a, "pageId")), refusal::web_page_gone());
    case Command::PlayLiveSource:
        return from_bool(facade_.liveLibrary()->playSource(qs(a, "kind"), qs(a, "name")),
                         tr("That live source is no longer there."));
    case Command::ResolveBibleRef: {
        const std::int64_t bible_id = qi(a, "bibleId");
        if (bible_id < 0 || !facade_.bible_repo()) {
            return refused(refusal::no_bible_chosen());
        }
        // Stateless: a phone resolving a passage must not move the operator's
        // own bible browse selection on the console.
        return resolve_bible_ref(*facade_.bible_repo(), bible_id, qs(a, "reference"));
    }
    case Command::SuggestBibleBooks: {
        const std::int64_t bible_id = qi(a, "bibleId");
        if (bible_id < 0 || !facade_.bible_repo()) {
            return refused(refusal::no_bible_chosen());
        }
        const auto books = facade_.bible_repo()->list_books(bible_id);
        const auto hits = parse::suggest_bible_books(books, qs(a, "prefix").toStdString());
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& book : hits) {
            arr.push_back(book);
            if (arr.size() >= 12) {
                break;
            }
        }
        return CommandResult::good(nlohmann::json{{"books", std::move(arr)}});
    }
    }
    return refused(tr("SeventhSlide does not know that command."));
}

std::string SessionRemoteBridge::glyph_sprite() const
{
    return glyphs::sprite().toStdString();
}

// ----------------------------------------------------------------- library --

nlohmann::json SessionRemoteBridge::library_page(const remote::LibraryQuery& q) const
{
    nlohmann::json rows = nlohmann::json::array();
    // The rule every library search here follows (db::SearchQuery).
    const db::SearchQuery search(std::string_view(q.query));

    if (q.kind == "songs") {
        // The console's own search: title, authors, songbook, topics, key
        // and lyrics, ranked as its list ranks them; a song found by its
        // lyrics names the line.
        const QString query = QString::fromStdString(q.query);
        for (const auto& song : facade_.library()->songs()->search(query)) {
            nlohmann::json row{{"id", song.id},
                               {"title", song.title.toStdString()},
                               {"authors", song.authors.toStdString()}};
            if (!song.matched_line.isEmpty()) {
                row["matchedLine"] = song.matched_line.toStdString();
            }
            rows.push_back(std::move(row));
        }
    } else if (q.kind == "freeform") {
        if (auto* repo = facade_.freeform_repo()) {
            const std::optional<std::int64_t> parent =
                optional_id(q.folder_id);
            for (const auto& folder : repo->folders().children(parent)) {
                if (folder.id && search.matches(std::string_view(folder.name))) {
                    rows.push_back({{"folderId", *folder.id}, {"name", folder.name}});
                }
            }
            // The browser rows: a name needs no document.
            for (const auto& item : repo->list_browser_rows(parent)) {
                if (search.matches(std::string_view(item.name))) {
                    rows.push_back({{"id", item.id}, {"name", item.name}});
                }
            }
        }
    } else if (q.kind == "bibles") {
        if (auto* repo = facade_.bible_repo()) {
            for (const auto& bible : repo->list_all()) {
                // A Bible switched off in Settings is not offered anywhere.
                if (!bible.id || !bible.enabled || !search.matches(std::string_view(bible.name))) {
                    continue;
                }
                rows.push_back({{"id", *bible.id}, {"name", bible.name}});
            }
        }
    } else if (q.kind == "bible-books") {
        if (auto* repo = facade_.bible_repo(); repo && q.folder_id >= 0) {
            for (const auto& book : repo->list_books(q.folder_id)) {
                if (search.matches(std::string_view(book))) {
                    rows.push_back({{"book", book}});
                }
            }
        }
    } else if (q.kind == "bible-chapters") {
        if (auto* repo = facade_.bible_repo(); repo && q.folder_id >= 0) {
            for (const int chapter : repo->list_chapters(q.folder_id, q.query)) {
                rows.push_back({{"chapter", chapter}});
            }
        }
    } else if (q.kind == "bible-verses") {
        if (auto* repo = facade_.bible_repo(); repo && q.folder_id >= 0 && q.chapter > 0) {
            for (const auto& verse : repo->get_chapter(q.folder_id, q.query, q.chapter)) {
                rows.push_back({{"verse", verse.verse_num}, {"text", verse.text}});
            }
        }
    } else if (q.kind == "images" || q.kind == "videos" || q.kind == "audio") {
        const auto kind = q.kind == "images"   ? db::MediaKind::Image
                          : q.kind == "videos" ? db::MediaKind::Video
                                               : db::MediaKind::Audio;
        if (auto* repo = facade_.media_repo()) {
            const std::optional<std::int64_t> parent =
                optional_id(q.folder_id);
            // Same listing the console browser shows, so the two never drift.
            const auto listing = db::list_media(*repo, kind, parent, q.query);
            for (const auto& folder : listing.folders) {
                if (folder.id) {
                    rows.push_back(nlohmann::json{{"folderId", *folder.id}, {"name", folder.name}});
                }
            }
            for (const auto& file : listing.files) {
                rows.push_back(nlohmann::json{{"filename", file.filename},
                                              {"title", file.title}});
            }
        }
    } else if (q.kind == "web") {
        auto* web = facade_.webLibrary();
        if (auto* repo = web->repo()) {
            const std::optional<std::int64_t> parent = optional_id(q.folder_id);
            for (const auto& folder : repo->folders().children(parent)) {
                if (folder.id && search.matches(std::string_view(folder.name))) {
                    rows.push_back({{"folderId", *folder.id}, {"name", folder.name}});
                }
            }
            for (const auto& page : repo->list_items_in_folder(parent)) {
                if (page.id && search.matches(std::string_view(page.name))) {
                    // The still is named by its look, so it is asked for again
                    // exactly when the page changes.
                    rows.push_back({{"id", *page.id},
                                    {"name", page.name},
                                    {"url", page.url},
                                    {"thumb", WebThumbs::key_of(page)}});
                }
            }
        }
    } else if (q.kind == "live") {
        auto* live = facade_.liveLibrary();
        // Read again while the remote's Live tab shows; looked for until then.
        live->keep_searching_for(k_live_lease);
        const auto thumbs = live->thumbnails_revision();
        for (const QVariant& source : live->sources()) {
            const std::string name =
                source.toMap().value(QStringLiteral("name")).toString().toStdString();
            if (!search.matches(std::string_view(name))) {
                continue;
            }
            nlohmann::json row = to_json(source);
            row["thumbRev"] = thumbs;
            rows.push_back(std::move(row));
        }
    } else {
        return {{"error", tr("That is not one of the library’s lists.").toStdString()},
                {"rows", nlohmann::json::array()},
                {"total", 0}};
    }

    return page(std::move(rows), q.offset, q.limit);
}

nlohmann::json SessionRemoteBridge::service_list(std::string_view tag,
                                                 std::string_view query) const
{
    // Create and tagging stay on the console. The remote may list, open,
    // rename, and delete.
    nlohmann::json tags = nlohmann::json::array();
    nlohmann::json services = nlohmann::json::array();
    const db::SearchQuery q(query);
    const std::string t = util::trim(tag);
    if (auto* repo = facade_.service_repo()) {
        const auto all = repo->list_services();
        const std::int64_t open_id = facade_.openServiceId();
        const auto has_tag = [](const model::Service& s, const std::string& want) {
            return std::ranges::any_of(s.tags, [&want](const std::string& x) {
                return util::iequals(x, want);
            });
        };
        const auto tag_hit = [&q](const model::Service& s) {
            return std::ranges::any_of(s.tags,
                                       [&q](const std::string& x) { return q.matches(std::string_view(x)); });
        };
        if (t.empty()) {
            for (const auto& c : model::service_tag_counts(all)) {
                if (q.matches(std::string_view(c.name))) {
                    tags.push_back({{"name", c.name}, {"count", c.count}});
                }
            }
        }
        for (const auto& s : all) {
            if (!s.id) {
                continue;
            }
            const bool shown = !q.empty() ? (q.matches(std::string_view(s.name)) || tag_hit(s))
                                          : (t.empty() || has_tag(s, t));
            if (shown) {
                services.push_back({{"id", *s.id},
                                    {"name", s.name},
                                    {"serviceAt", s.service_at},
                                    {"createdAt", s.created_at},
                                    {"tags", s.tags},
                                    {"isOpen", open_id >= 0 && *s.id == open_id}});
            }
        }
    }
    return {{"tag", t}, {"tags", std::move(tags)}, {"services", std::move(services)}};
}

// -------------------------------------------------------------- thumbnails --

remote::PendingThumb SessionRemoteBridge::thumb(const remote::ThumbRequest& r) const
{
    if (r.kind == "preview") {
        const QString output_id =
            QString::fromStdString(r.name.empty() ? primary_output_id() : r.name);
        // Take the aspect from the output's own canvas rather than assuming 16:9.
        int height = r.width * 9 / 16;
        if (const auto* oc =
                model::find_output_by_id(facade_.session().outputs(), output_id.toStdString());
            oc != nullptr && oc->canvas_width > 0) {
            height = r.width * oc->canvas_height / oc->canvas_width;
        }
        // A slide is one of its stack's deck: the stack the request names,
        // else the console's; and of its Program's, unless it names the
        // Preview, whose picture without a slide is its current slide's.
        const bool named = !r.stack.empty() && facade_.session().find_stack(r.stack) != nullptr;
        const TabLens lens(facade_.session(), named ? r.stack : std::string());
        const auto side =
            r.side == "preview" ? session::StackSide::Preview : session::StackSide::Program;
        int slide = r.slide_index;
        if (side == session::StackSide::Preview && slide < 0) {
            // Its current slide on that output: lines share slides, each
            // output its own way.
            const auto* cued = facade_.session().focused_stack().side(side);
            const auto* nav = cued != nullptr ? cued->player().find_output(output_id.toStdString())
                                              : nullptr;
            slide = nav != nullptr ? nav->index : 0;
        }
        return pending(facade_.outputPreviews()->prepare_output_preview(
                           output_id, QSize(r.width, height), slide, side),
                       /*photographic=*/false);
    }

    if (r.kind == "media") {
        const QString id = QString::fromStdString(r.sub) + QLatin1Char('/')
            + QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdString(r.name)));
        // Needs no session state: the decode (and a thumbnail write on a
        // miss) runs on the pool with the rest of the encode.
        return {[thumbs = media_thumbs_.get(), id, width = r.width] {
            QSize produced;
            return encode(thumbs->requestImage(id, &produced, QSize(width, width)),
                          /*photographic=*/true);
        }};
    }

    if (r.kind == "freeform") {
        // A bounding box: the tile keeps its deck's canvas shape, whatever
        // it is.
        return pending(facade_.freeformLibrary()->prepare_library_thumb(
                           r.id, QSize(r.width, r.width)),
                       /*photographic=*/false);
    }

    if (r.kind == "web") {
        auto* web = facade_.webLibrary();
        auto* repo = web->repo();
        const auto page = repo != nullptr ? repo->get_item(r.id) : std::nullopt;
        if (!page) {
            return {};
        }
        // The still the library keeps, read and scaled on the pool.
        return {[path = web->thumbs().path_of(*page), width = r.width] {
            const QImage still(QString::fromStdString(path.string()));
            return encode(still.isNull() ? still
                                         : still.scaledToWidth(std::min(width, still.width()),
                                                               Qt::SmoothTransformation),
                          /*photographic=*/true);
        }};
    }

    if (r.kind == "live") {
        const QImage still = facade_.liveLibrary()->thumbnail(QString::fromStdString(r.name));
        if (still.isNull()) {
            return {};
        }
        return {[still, width = r.width] {
            return encode(still.scaledToWidth(std::min(width, still.width()),
                                              Qt::SmoothTransformation),
                          /*photographic=*/true);
        }};
    }

    return {};
}

} // namespace seventhslide::app
