#pragma once

// SessionRemoteBridge - IRemoteBridge over SessionFacade.
//
// This is the only place src/remote's contract meets the operator session. The
// remote library never links src/app, so every capability the web remote has
// arrives through here and nowhere else.

#include <seventhslide/remote/remote_bridge.hpp>

#include <QCoreApplication>
#include <QObject>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace seventhslide::session {
class StackRuntime;
}

namespace seventhslide::app {

class SessionFacade;
class SideView;

class MediaThumbProvider;

class SessionRemoteBridge final : public remote::IRemoteBridge {
    // The bridge's own refusals, which a phone shows as they are, are
    // translated as the console's are.
    Q_DECLARE_TR_FUNCTIONS(SessionRemoteBridge)

public:
    explicit SessionRemoteBridge(SessionFacade& facade) noexcept;
    ~SessionRemoteBridge() override;

    void settle(remote::View& view) const override;
    [[nodiscard]] nlohmann::json section(remote::Section s,
                                         const remote::View& view) const override;
    [[nodiscard]] remote::CommandResult invoke(remote::Command command,
                                               const nlohmann::json& args,
                                               remote::View& view) override;
    [[nodiscard]] nlohmann::json library_page(const remote::LibraryQuery& q) const override;
    [[nodiscard]] nlohmann::json service_list(std::string_view tag,
                                              std::string_view query) const override;
    [[nodiscard]] std::string glyph_sprite() const override;
    [[nodiscard]] remote::PendingThumb thumb(const remote::ThumbRequest& r) const override;

    /// A web page or a live source has drawn a new picture: the session
    /// section's previewRevision moves while one is live on a tab
    /// (RemoteController calls this at most four times a second).
    void note_source_frame() noexcept { ++source_frames_; }

    /// How long a read of the Live tab keeps sources looked for: the client
    /// reads it again well within it while its tab shows.
    static constexpr std::chrono::milliseconds k_live_lease{5000};

private:
    /// The stack ``view`` shows: its tab, or, when that tab was left with
    /// nothing live or cued, the frontmost stack that has something
    /// (``Session::frontmost_holding``), as the console moves after a clear.
    [[nodiscard]] std::string shown_stack(const remote::View& view) const;
    /// The primary output's id; empty with no outputs.
    [[nodiscard]] std::string primary_output_id() const;
    [[nodiscard]] nlohmann::json section_for(remote::Section s) const;
    [[nodiscard]] remote::CommandResult invoke_command(remote::Command command,
                                                       const nlohmann::json& args);
    [[nodiscard]] nlohmann::json section_session() const;
    [[nodiscard]] nlohmann::json section_stacks() const;
    [[nodiscard]] nlohmann::json section_service() const;
    [[nodiscard]] nlohmann::json section_outputs() const;
    [[nodiscard]] nlohmann::json section_advance() const;
    [[nodiscard]] nlohmann::json section_preview() const;

    // One side of the tab's stack, as the session, lines and images sections
    // have the Program and their preview namesakes the Preview. ``st`` is the
    // side's runtime: null for a Preview the stack does not have.
    /// What a well shows of the side: cursor, mode flags, media, page, source.
    [[nodiscard]] nlohmann::json side_fields(SideView& side,
                                             const session::StackRuntime* st) const;
    /// Its line map.
    [[nodiscard]] nlohmann::json side_lines(SideView& side,
                                            const session::StackRuntime* st) const;
    /// Its image playlist.
    [[nodiscard]] static nlohmann::json side_images(const session::StackRuntime* st);

    SessionFacade& facade_;
    /// Counts note_source_frame.
    std::uint64_t source_frames_ = 0;
    /// Reuses the operator UI's provider, which already knows about the on-disk
    /// JPEG pyramids written at import time. Owned rather than function-static
    /// so it cannot outlive, or be bound to, the wrong facade.
    std::unique_ptr<MediaThumbProvider> media_thumbs_;
};

} // namespace seventhslide::app
