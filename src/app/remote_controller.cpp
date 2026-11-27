#include "remote_controller.hpp"

#include "line_map_model.hpp"
#include "live_controller.hpp"
#include "live_sources.hpp"
#include "session_facade.hpp"
#include "stack_controller.hpp"
#include "side_view.hpp"
#include "web_sources.hpp"

#include <QAbstractItemModel>
#include <QList>
#include <QModelIndex>

#include <utility>
#include <vector>

namespace seventhslide::app {

RemoteController::RemoteController(SessionFacade& facade, QObject* parent)
    : QObject(parent)
    , facade_(facade)
    , bridge_(facade)
    , server_(bridge_, this)
    , position_pushes_(std::chrono::milliseconds(250),
                       [this] {
                           server_.mark_dirty(remote::Section::Session);
                           server_.mark_dirty(remote::Section::Preview);
                       })
    , source_pushes_(std::chrono::milliseconds(250), [this] {
        bridge_.note_source_frame();
        server_.mark_dirty(remote::Section::Session);
        server_.mark_dirty(remote::Section::Preview);
    })
{
    connect(&server_, &remote::RemoteServer::runningChanged, this,
            &RemoteController::statusChanged);
    wire_facade_signals();
    connect(&facade_, &SessionFacade::appSettingsChanged, this,
            &RemoteController::apply_settings);
    apply_settings();
}

void RemoteController::wire_facade_signals()
{
    // Most mutations only emit the broad sessionChanged. Mark every section and
    // let RemoteServer's per-section diff drop the ones that did not change.
    connect(&facade_, &SessionFacade::sessionChanged, this,
            [this] { server_.mark_all_dirty(); });
    const auto mark_stacks = [this] { server_.mark_dirty(remote::Section::Stacks); };
    StackController* stacks = facade_.stacks();
    connect(stacks, &StackController::stacksChanged, this, mark_stacks);
    connect(stacks, &StackController::armedSendToChanged, this, mark_stacks);
    connect(stacks, &StackController::takeFadeMsChanged, this, mark_stacks);
    // A focus switch changes the controller's view, not the rundown or outputs.
    connect(&facade_, &SessionFacade::focusedStackChanged, this, [this] {
        for (const auto s : {remote::Section::Session, remote::Section::Stacks,
                             remote::Section::Lines, remote::Section::Images,
                             remote::Section::Preview, remote::Section::PreviewLines,
                             remote::Section::PreviewImages}) {
            server_.mark_dirty(s);
        }
    });
    // Playback position rides in the session section, and a Preview's in the
    // preview section. Play state and length go at once; the clock, which
    // moves with every tick, goes at most four times a second (a phone's seek
    // bar and clock need no more).
    connect(&facade_, &SessionFacade::mediaStateChanged, this, [this] {
        server_.mark_dirty(remote::Section::Session);
        server_.mark_dirty(remote::Section::Preview);
    });
    // Each side's section: the session's for the Program, the preview's for
    // the Preview.
    for (const auto& [view, section] :
         {std::pair{facade_.program(), remote::Section::Session},
          std::pair{facade_.preview(), remote::Section::Preview}}) {
        connect(view, &SideView::mediaPositionChanged, this,
                [this] { position_pushes_.poke(); });
        // A web page or a live source: what it says of itself (loading, a
        // problem, the page it is on) at once; its picture, as the clock, at
        // most four times a second.
        connect(view, &SideView::webStateChanged, this,
                [this, section] { server_.mark_dirty(section); });
        connect(view, &SideView::liveSourceStateChanged, this,
                [this, section] { server_.mark_dirty(section); });
    }
    if (WebSources* web = facade_.web_sources()) {
        connect(web, &WebSources::framesChanged, this, [this] { source_pushes_.poke(); });
    }
    if (LiveSources* live = facade_.live_sources()) {
        connect(live, &LiveSources::framesChanged, this, [this] { source_pushes_.poke(); });
    }
    // Switched on or off, or run out at the end: the settings, and which
    // stacks are advancing (the session section's advancingStackIds).
    connect(stacks, &StackController::autoAdvanceChanged, this, [this] {
        server_.mark_dirty(remote::Section::Session);
        server_.mark_dirty(remote::Section::Advance);
    });
    // Line-map updates fire *before* present_frames. Flush now so the remote
    // sees the new cursor without waiting for the output render to finish.
    using Sections = std::vector<remote::Section>;
    for (const auto& [view, sections] :
         {std::pair{facade_.program(), Sections{remote::Section::Session, remote::Section::Lines,
                                                remote::Section::Images}},
          std::pair{facade_.preview(),
                    Sections{remote::Section::Preview, remote::Section::PreviewLines,
                             remote::Section::PreviewImages}}}) {
        LineMapModel* lines = view->lines();
        if (lines == nullptr) {
            continue;
        }
        const auto push_lines = [this, sections] {
            for (const auto s : sections) {
                server_.mark_dirty(s);
            }
            server_.flush_now();
        };
        connect(lines, &QAbstractItemModel::dataChanged, this,
                [push_lines](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                    push_lines();
                });
        connect(lines, &QAbstractItemModel::modelReset, this, push_lines);
    }
}

void RemoteController::apply_settings()
{
    const bool want = facade_.remoteEnabled();
    const int want_port = facade_.remotePort();

    if (!want) {
        if (server_.running()) {
            server_.stop();
            started_port_ = 0;
            Q_EMIT statusChanged();
        }
        return;
    }

    if (server_.running() && started_port_ == want_port) {
        // Only the PIN can have moved; no need to drop the listening socket.
        server_.set_pin(facade_.remotePin());
        return;
    }

    remote::RemoteServer::Config cfg;
    cfg.port = static_cast<quint16>(want_port);
    cfg.pin = facade_.remotePin();
    if (server_.start(cfg)) {
        started_port_ = server_.port();
    } else {
        started_port_ = 0;
    }
    Q_EMIT statusChanged();
}

QStringList RemoteController::joinUrls() const
{
    return facade_.list_remote_candidate_urls(server_.running() ? server_.port()
                                                             : facade_.remotePort());
}

} // namespace seventhslide::app
