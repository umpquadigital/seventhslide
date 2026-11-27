#pragma once

// Stack list, the armed send-to target, per-stack auto-advance, and a
// Preview's take (Transition), by the theme's way or by a Cut or this
// computer's Fade.
//
// The stacks live in the session; their definitions are part of the open
// library's settings and saved with them. liveChanged tells the application
// when live content moved. SessionFacade owns one and exposes
// it as `session.stacks`.

#include "open_library.hpp"

#include <seventhslide/model/stack.hpp>
#include <seventhslide/model/transition.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <vector>

class QTimer;

namespace seventhslide::app {

class StackController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    /// Every defined stack: ``[{id, name, icon, preview}]`` in paint order,
    /// ``preview`` when it has one on this computer.
    Q_PROPERTY(QVariantList all READ stacks NOTIFY stacksChanged)
    /// Stacks with something live or cued, in paint order: ``[{id, name,
    /// icon, blank, frozen, live, cued, preview}]``. ``live``: on the
    /// outputs; ``cued``: in its Preview; ``preview``: it has one.
    Q_PROPERTY(QVariantList occupied READ occupiedStacks NOTIFY occupiedStacksChanged)
    /// The stack the controller column shows and drives.
    Q_PROPERTY(QString focusedId READ focusedStackId WRITE setFocusedStackId NOTIFY
                   focusedIdChanged)
    /// Where the next "play" lands; empty = each content type's default stack.
    Q_PROPERTY(QString armedSendToId READ armedSendToId NOTIFY armedSendToChanged)
    /// More than one stack has something live or cued, so the controller
    /// shows stack tabs.
    Q_PROPERTY(bool showTabs READ showStackTabs NOTIFY occupiedStacksChanged)
    /// Ids of every stack whose auto-advance timer is running.
    Q_PROPERTY(QStringList advancingIds READ advancingStackIds NOTIFY autoAdvanceChanged)
    /// Content-type -> stack id map ({song, bible, freeform, image, video, audio}).
    Q_PROPERTY(QVariantMap typeDefaults READ stackTypeDefaults NOTIFY typeDefaultsChanged)
    /// The stack a content type goes to when no other stack takes it.
    Q_PROPERTY(QString fallbackId READ fallbackStackId NOTIFY stacksChanged)
    /// How long this computer's Fade takes, in ms (Transition's menu, which
    /// takes by a Cut or by it in place of the theme's transition).
    Q_PROPERTY(int takeFadeMs READ takeFadeMs NOTIFY takeFadeMsChanged)

public:
    StackController(session::Session& session, OpenLibrary& library, QObject* parent = nullptr);

    [[nodiscard]] QVariantList stacks() const;
    [[nodiscard]] QVariantList occupiedStacks() const;
    [[nodiscard]] QString focusedStackId() const;
    void setFocusedStackId(const QString& id);
    [[nodiscard]] QString armedSendToId() const;
    [[nodiscard]] bool showStackTabs() const;

    /// The glyphs a stack may show, in picker order (model::k_stack_icons).
    Q_INVOKABLE [[nodiscard]] QStringList iconIds() const;
    Q_INVOKABLE void armSendTo(const QString& stack_id);
    Q_INVOKABLE [[nodiscard]] QString clearStack(const QString& stack_id);
    Q_INVOKABLE void clearAllStacks();
    Q_INVOKABLE [[nodiscard]] bool toggleStackBlank(const QString& stack_id);
    Q_INVOKABLE [[nodiscard]] bool toggleStackFreeze(const QString& stack_id);
    /// Blank what is cued in stack ``stack_id``'s Preview, to go live blanked
    /// (``Session::toggle_preview_blank``).
    Q_INVOKABLE [[nodiscard]] bool togglePreviewBlank(const QString& stack_id);
    Q_INVOKABLE [[nodiscard]] QString addStack(const QString& name = QString(),
                                               const QString& icon = QString());
    Q_INVOKABLE [[nodiscard]] QString removeStack(const QString& stack_id);
    Q_INVOKABLE [[nodiscard]] QString renameStack(const QString& stack_id, const QString& name);
    Q_INVOKABLE [[nodiscard]] QString setStackIcon(const QString& stack_id, const QString& icon);
    Q_INVOKABLE [[nodiscard]] QString reorderStacks(const QVariantList& ordered_ids);
    /// Give stack ``stack_id`` a Preview on this computer, or take it away
    /// with whatever is cued there. Kept in the console store, not the
    /// library, so it is neither synced nor read-only with it.
    Q_INVOKABLE [[nodiscard]] QString setStackPreview(const QString& stack_id, bool on);
    /// Whether stack ``stack_id`` has something cued in its Preview, which
    /// taking the Preview away would drop.
    Q_INVOKABLE [[nodiscard]] bool previewCued(const QString& stack_id) const;
    [[nodiscard]] QVariantMap stackTypeDefaults() const;
    /// Point one content type at ``stack_id``. Persists immediately, like the
    /// other stack mutators.
    Q_INVOKABLE [[nodiscard]] QString setStackTypeDefault(const QString& type_key,
                                                          const QString& stack_id);
    [[nodiscard]] QString fallbackStackId() const;
    /// Content type ``type_key`` stops playing on ``stack_id`` and goes to the
    /// fallback stack. Refused on the fallback stack itself. Persists at once.
    Q_INVOKABLE [[nodiscard]] QString releaseStackTypeDefault(const QString& type_key,
                                                              const QString& stack_id);
    /// Advance the service-driven stack one slide (the web remote's "next").
    [[nodiscard]] bool next_service_slide();

    // --- a Preview's take ---
    /// Take stack ``stack_id``'s Preview live by the theme's transition
    /// (``Session::transition``). Empty when it went; else why not.
    Q_INVOKABLE [[nodiscard]] QString transition(const QString& stack_id);
    /// The same by a transition of its own: ``type`` as the model names it
    /// (``"fade"``), ``duration_ms``, ``direction`` for one that moves and
    /// ``easing`` (empty: the model's default for either). Refused for a name
    /// the model does not know.
    Q_INVOKABLE [[nodiscard]] QString transitionWith(const QString& stack_id,
                                                     const QString& type,
                                                     int duration_ms,
                                                     const QString& direction,
                                                     const QString& easing = {});
    /// Drop what is cued in stack ``stack_id``'s Preview; what is live stays.
    Q_INVOKABLE [[nodiscard]] QString clearPreview(const QString& stack_id);

    [[nodiscard]] int takeFadeMs() const;
    /// Set how long the Fade takes, brought into its range
    /// (``model::normalize_take_fade_ms``). Kept on this computer at once.
    Q_INVOKABLE [[nodiscard]] QString setTakeFadeMs(int duration_ms);

    // --- per-stack auto-advance ---
    // Owned by Session rather than ControllerColumn.qml so the web remote can
    // both see and drive it, and so a stack keeps its timer across a focus
    // change. Empty return = ok.
    Q_INVOKABLE [[nodiscard]] QString setAutoAdvance(const QString& stack_id,
                                                     bool enabled,
                                                     int interval_sec,
                                                     bool loop,
                                                     bool through,
                                                     bool stop_at_dividers);
    /// {enabled, intervalSec, loop, through, stopAtDividers} for one stack,
    /// always every field: a stack with no settings of its own, or no stack,
    /// has the defaults (``session::AutoAdvanceState``).
    Q_INVOKABLE [[nodiscard]] QVariantMap autoAdvanceFor(const QString& stack_id) const;
    [[nodiscard]] QStringList advancingStackIds() const;

    /// Push app_settings stacks into the session, with this computer's
    /// Previews for them, and drop timers of removed stacks.
    void apply_settings();
    /// The session changed underneath (anything that may add, clear, focus or
    /// arm a stack): notify the properties whose values moved.
    void refresh_state();

signals:
    void stacksChanged();
    void occupiedStacksChanged();
    void armedSendToChanged();
    /// ``focusedId`` changed.
    void focusedIdChanged();
    /// ``typeDefaults`` changed.
    void typeDefaultsChanged();
    void appSettingsChanged();
    void autoAdvanceChanged();
    void takeFadeMsChanged();
    /// Focus moved: everything the controller column shows changed with it.
    /// Nothing went live, so the application only follows the new focus.
    void focusedStackChanged();
    void mediaStateChanged();
    void errorOccurred(const QString& message);
    /// Live content changed (see LibraryController::liveChanged); the
    /// application's reaction refreshes this controller's state too.
    void liveChanged(bool rebuildModels);
    /// A take traded stack ``stackId``'s Program and Preview. Comes just
    /// before the take's liveChanged, before anything shows the sides
    /// traded.
    void sidesSwapped(const QString& stackId);

private:
    [[nodiscard]] QString persist();
    /// A stack edit's result for QML: the model's reason when it refused,
    /// else the settings saved (and why not, if saving failed).
    [[nodiscard]] QString commit(const model::StackMutationResult& result);
    /// Give the session's stacks the Previews this computer keeps for the
    /// open library, forgetting those kept for stacks it no longer has.
    /// Without a console store (an in-memory library in tests) the session's
    /// own last the run.
    void apply_previews();
    /// The open library's id in the console store; empty without one.
    [[nodiscard]] std::string console_library_id() const;
    void update_advance_timer();
    void on_advance_tick();
    /// The take on ``stack_id``, by ``how`` or the theme's way.
    [[nodiscard]] QString take(const QString& stack_id, std::optional<model::Transition> how);

    session::Session& session_;
    OpenLibrary& library_;
    QTimer* advance_timer_ = nullptr;
    /// Monotonic, so a change to the system clock (NTP, a manual set) cannot
    /// advance slides early or hold them.
    QElapsedTimer advance_clock_;
    /// When each advancing stack next moves, on ``advance_clock_``.
    QHash<QString, qint64> advance_due_ms_;
    // What the properties last notified, so refresh_state only signals change.
    QVariantList notified_stacks_;
    QVariantList notified_occupied_;
    QString notified_armed_;
    QString notified_focused_;
    QVariantMap notified_type_defaults_;
};

} // namespace seventhslide::app
