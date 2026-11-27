#pragma once

// IRemoteBridge - the entire surface src/remote is allowed to touch.
//
// src/app implements this over SessionFacade. Inverting the dependency this way
// keeps the link graph honest (remote never links app, and so never links QML)
// and lets the protocol and command layers be tested against a fake bridge with
// no sockets and no Qt Quick.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::remote {

/// One independently-patchable region of the mirrored state.
///
/// Sections exist so a line-cursor move does not re-send the rundown. The
/// library is deliberately absent - it is paged over REST instead (see
/// IRemoteBridge::library_page).
enum class Section {
    Session,  ///< mode, cursor, blank/freeze, the *Mode flags, video position
    Stacks,   ///< defs, occupied, focus, armed send-to
    Service,  ///< the open rundown
    Lines,    ///< the line map for the focused stack
    Outputs,  ///< per-output blank/freeze status only, of the outputs on
    Images,   ///< image playlist, when an image item is live
    Advance,  ///< per-stack auto-advance settings
    // The tab's Preview, each as its live namesake has the Program:
    Preview,        ///< what is cued: cursor, mode flags, media and page state
    PreviewLines,   ///< its line map
    PreviewImages,  ///< its image playlist
};

/// Every section, in snapshot order.
[[nodiscard]] const std::vector<Section>& all_sections();

[[nodiscard]] std::string_view section_name(Section s) noexcept;

/// Everything a remote may ask the session to do. remote_protocol's table
/// gives each its wire name, its arguments and the sections it invalidates;
/// the bridge implementation answers each one.
enum class Command {
    // Transport
    NextSlide,
    PrevSlide,
    JumpToLine,
    NextSlideOnStack,
    JumpToLineOnStack,
    NextServiceSlide,
    // Blank, freeze
    ToggleGlobalBlank,
    ToggleGlobalFreeze,
    ToggleOutputBlank,
    ToggleOutputFreeze,
    ToggleStackBlank,
    ToggleStackFreeze,
    ClearBlankFreeze,
    // Stacks
    SetFocusedStackId,
    ArmSendTo,
    ClearStack,
    ClearAllStacks,
    // A stack's Preview: the take, dropping what is cued, and how long the
    // take's Fade takes
    Transition,
    ClearPreview,
    SetTakeFade,
    SetServiceItemStack,
    SetServiceItemAlsoPlay,
    LinkServiceItems,
    UnlinkServiceItem,
    // Per-stack auto-advance
    SetAutoAdvance,
    // Media transport
    ImageGoto,
    VideoPlay,
    VideoPause,
    VideoSeek,
    VideoRestart,
    // A web page live: the page before it, and loading it again
    WebBack,
    WebReload,
    // Service rundown
    OpenService,
    CloseService,
    SelectServiceItem,
    SelectServiceItemAtImage,
    AddSongToService,
    AddFreeformToService,
    AddBibleRefToService,
    AddImageToService,
    AddVideoToService,
    AddImageFolderToService,
    AddVideoFolderToService,
    AddFreeformFolderToService,
    AddDividerToService,
    RemoveServiceItem,
    MoveServiceItemTo,
    ReorderServiceItems,
    // Service library: rename and delete only
    RenameService,
    DeleteService,
    // Library: play only, never change
    PlaySong,
    PlayFreeform,
    PlayImage,
    PlayImageFolder,
    PlayVideo,
    PlayBibleRef,
    PlayAudio,
    PlayWebPage,
    PlayLiveSource,
    ResolveBibleRef,
    SuggestBibleBooks,
};

/// How many commands there are: one past the last.
inline constexpr std::size_t k_command_count =
    static_cast<std::size_t>(Command::SuggestBibleBooks) + 1;

/// Outcome of one command. ``data`` carries a result payload for the few
/// commands that answer a question (bible reference resolution, book lookup).
struct CommandResult {
    bool ok = true;
    std::string error;
    nlohmann::json data = nlohmann::json::object();

    [[nodiscard]] static CommandResult good()
    {
        return CommandResult{true, {}, nlohmann::json::object()};
    }
    [[nodiscard]] static CommandResult good(nlohmann::json payload)
    {
        return CommandResult{true, {}, std::move(payload)};
    }
    [[nodiscard]] static CommandResult fail(std::string message)
    {
        return CommandResult{false, std::move(message), nlohmann::json::object()};
    }
};

/// A page of library rows. ``kind`` is one of songs | freeform | bibles |
/// bible-books | bible-chapters | bible-verses | videos | images | audio |
/// web | live. Media, freeform and web honour ``folder_id``; bible-verses also
/// honour ``chapter``.
struct LibraryQuery {
    std::string kind;
    std::string query;
    std::int64_t folder_id = -1;
    int chapter = 0;
    int offset = 0;
    int limit = 50;
};

/// An image the remote wants to display. ``kind`` selects which of the
/// operator UI's QQuickImageProviders this mirrors.
struct ThumbRequest {
    std::string kind;   ///< preview | media | freeform | web | live
    std::string name;   ///< output id, media filename, or live source key
    std::string sub;    ///< media kind (images|videos)
    /// preview of one slide: the stack whose deck it is (empty, or a stack
    /// that is gone: the console's tab).
    std::string stack;
    /// preview of one slide: the side of that stack whose deck it is
    /// ("preview", or empty for the Program's).
    std::string side;
    std::int64_t id = -1;
    int slide_index = -1;
    int width = 320;
};

struct ThumbResult {
    bool ok = false;
    std::string mime;
    std::string bytes;  ///< raw encoded image; std::string keeps this Qt-free
};

/// One remote device's own place in the session: the stack tab it shows.
/// Every connected device has one, so two devices never move each other's
/// tab, and neither moves the console's. The server keeps it with the
/// device; only the bridge reads or changes what is in it.
struct View {
    std::string stack;  ///< empty until settled (IRemoteBridge::settle)
};

/// A thumbnail prepared and not yet made. What it shows is read from live
/// session state on the GUI thread; painting or decoding it and encoding the
/// result are most of the cost and need none of that, so the server runs
/// ``encode`` on a worker.
struct PendingThumb {
    /// Safe to call on any thread. Empty when there is no such thumbnail.
    std::function<ThumbResult()> encode;
};

/**
 * The operator session, as seen from the remote.
 *
 * Commands arrive from remote_protocol's whitelist, which owns the table of
 * what a remote may do and validates arguments before anything reaches here.
 * Implementations should still treat every argument as untrusted.
 */
class IRemoteBridge {
public:
    virtual ~IRemoteBridge() = default;

    IRemoteBridge() = default;
    IRemoteBridge(const IRemoteBridge&) = delete;
    IRemoteBridge& operator=(const IRemoteBridge&) = delete;
    IRemoteBridge(IRemoteBridge&&) = delete;
    IRemoteBridge& operator=(IRemoteBridge&&) = delete;

    /// Bring ``view`` up to date before it is used: a view with no tab yet,
    /// or whose tab was removed, takes the console's.
    virtual void settle(View& view) const = 0;

    /// Current JSON for one section of the mirrored state, as ``view`` sees
    /// it (settled).
    [[nodiscard]] virtual nlohmann::json section(Section s, const View& view) const = 0;

    /// Run ``command`` for the device whose (settled) view this is; picking a
    /// tab, or sending content live to another stack, moves ``view``. Its
    /// arguments have been checked against the protocol's spec for it:
    /// present, and of the right JSON types.
    [[nodiscard]] virtual CommandResult invoke(Command command,
                                               const nlohmann::json& args,
                                               View& view) = 0;

    /// One page of library rows, plus a total for the client's paging.
    [[nodiscard]] virtual nlohmann::json library_page(const LibraryQuery& q) const = 0;

    /// The services for the selector sheet: ``{tag, tags: [{name, count}],
    /// services: [{id, name, serviceAt, createdAt, tags, isOpen}]}``. With no
    /// tag and no query, every tag in use and every service; with ``tag``, its
    /// services; with ``query``, the tags and services it finds (by name or
    /// tag) anywhere. Services come newest first, those with no date last.
    [[nodiscard]] virtual nlohmann::json service_list(std::string_view tag,
                                                      std::string_view query) const = 0;

    /// The app's glyphs as an inline SVG sprite: one ``<symbol id="ic-NAME">``
    /// per glyph, painted in ``currentColor``. The server puts it into
    /// index.html, so the page draws the same art as the console.
    [[nodiscard]] virtual std::string glyph_sprite() const = 0;

    /// Prepare one thumbnail: read what it shows. Called on the GUI thread;
    /// implementations must not block, and leave the paint, decode and encode
    /// to the returned encode, which runs on a worker thread. No encode, or an
    /// encoded result with ok unset, yields a 404.
    [[nodiscard]] virtual PendingThumb thumb(const ThumbRequest& r) const = 0;
};

} // namespace seventhslide::remote
