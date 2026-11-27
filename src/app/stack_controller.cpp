#include "stack_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "qml_ids.hpp"

#include <seventhslide/model/stack.hpp>

#include <nlohmann/json.hpp>

#include <QTimer>

#include <algorithm>
#include <optional>
#include <string>

namespace seventhslide::app {

namespace {

/// A transition as the Controller and the remote name it: ``type``,
/// ``direction`` and ``easing`` as core writes them, at most a minute long.
/// An empty direction or easing is the model's default. None for a name core
/// does not know: a typo must not take as a cut.
[[nodiscard]] std::optional<model::Transition> transition_from(const QString& type,
                                                               int duration_ms,
                                                               const QString& direction,
                                                               const QString& easing)
{
    // Read back as written: core reads an unknown name as its default.
    const auto known = [](const QString& name, auto from_string) {
        const auto s = name.toStdString();
        const auto value = from_string(s);
        return model::to_string(value) == s ? std::optional(value) : std::nullopt;
    };
    model::Transition out;
    const auto t = known(type, &model::transition_type_from_string);
    if (!t) {
        return std::nullopt;
    }
    out.type = *t;
    out.duration_ms = std::clamp(duration_ms, 0, 60000);
    if (!direction.isEmpty()) {
        const auto d = known(direction, &model::transition_direction_from_string);
        if (!d) {
            return std::nullopt;
        }
        out.direction = *d;
    }
    if (!easing.isEmpty()) {
        const auto e = known(easing, &model::transition_easing_from_string);
        if (!e) {
            return std::nullopt;
        }
        out.easing = *e;
    }
    return out;
}

QVariantMap stack_def_map(const model::StackDef& s, bool preview)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromStdString(s.id));
    m.insert(QStringLiteral("name"), QString::fromStdString(s.name));
    m.insert(QStringLiteral("icon"), QString::fromStdString(s.icon));
    m.insert(QStringLiteral("preview"), preview);
    return m;
}

} // namespace

StackController::StackController(session::Session& session, OpenLibrary& library, QObject* parent)
    : QObject(parent)
    , session_(session)
    , library_(library)
{
}

QString StackController::commit(const model::StackMutationResult& result)
{
    if (!result.ok) {
        return QString::fromStdString(result.message);
    }
    return persist();
}

QString StackController::persist()
{
    if (auto* repo = library_.config()) {
        try {
            repo->set_setting(std::string(model::k_setting_stacks),
                              nlohmann::json(library_.settings().stacks));
            repo->set_setting(std::string(model::k_setting_stack_type_defaults),
                              library_.settings().stack_type_defaults.to_json());
        } catch (const std::exception& ex) {
            return report_entry_error(__func__, ex);
        }
    }
    apply_settings();
    emit appSettingsChanged();
    // Paint order, rundown pins and focus can all move with the stack list;
    // the application's reaction to liveChanged refreshes this controller too.
    emit liveChanged(true);
    return {};
}

void StackController::refresh_state()
{
    // Each property is notified only when its value actually moved: these
    // lists drive repeaters, and a spurious change rebuilds every delegate.
    if (auto all = stacks(); all != notified_stacks_) {
        notified_stacks_ = std::move(all);
        emit stacksChanged();
    }
    if (auto occupied = occupiedStacks(); occupied != notified_occupied_) {
        notified_occupied_ = std::move(occupied);
        emit occupiedStacksChanged();
    }
    if (auto armed = armedSendToId(); armed != notified_armed_) {
        notified_armed_ = std::move(armed);
        emit armedSendToChanged();
    }
    if (auto focused = focusedStackId(); focused != notified_focused_) {
        notified_focused_ = std::move(focused);
        emit focusedIdChanged();
    }
    // Type defaults move without the stack list changing, so they get their
    // own notify rather than riding on stacksChanged.
    if (auto defaults = stackTypeDefaults(); defaults != notified_type_defaults_) {
        notified_type_defaults_ = std::move(defaults);
        emit typeDefaultsChanged();
    }
}

void StackController::apply_settings()
{
    session_.set_stacks(library_.settings().stacks,
                        library_.settings().stack_type_defaults);
    apply_previews();
    // A removed stack must not leave a timer running against an id that is gone.
    if (session_.prune_auto_advance()) {
        emit autoAdvanceChanged();
    }
    update_advance_timer();
}

void StackController::apply_previews()
{
    auto* store = library_.console_store();
    const auto library_id = console_library_id();
    if (store == nullptr || library_id.empty()) {
        return;
    }
    library::StackIds stacks;
    for (const auto& def : session_.stack_defs()) {
        stacks.insert(def.id);
    }
    // A stack deleted here or on a computer the library syncs with is
    // forgotten. Should that fail, what is left names no stack: harmless.
    (void)store->keep_only_stacks(library_id, stacks);
    (void)session_.set_preview_stacks(store->preview_stacks(library_id));
}

std::string StackController::console_library_id() const
{
    const auto* registry = library_.registry();
    return registry != nullptr && library_.console_store() != nullptr ? registry->active_id()
                                                                      : std::string{};
}

QVariantList StackController::stacks() const
{
    QVariantList out;
    for (const auto& s : session_.stack_defs()) {
        const auto* stack = session_.find_stack(s.id);
        out.push_back(stack_def_map(s, stack != nullptr && stack->preview.has_value()));
    }
    return out;
}

QVariantList StackController::occupiedStacks() const
{
    QVariantList out;
    const auto& defs = session_.stack_defs();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        // The session keeps its stacks in its defs' order.
        const auto& stack = session_.stacks()[i];
        if (!stack.holds_content()) {
            continue;
        }
        const auto& id = stack.id();
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromStdString(id));
        m.insert(QStringLiteral("name"), QString::fromStdString(defs[i].name));
        m.insert(QStringLiteral("icon"), QString::fromStdString(defs[i].icon));
        m.insert(QStringLiteral("blank"), session_.stack_is_blank(id));
        m.insert(QStringLiteral("frozen"), session_.stack_is_frozen(id));
        m.insert(QStringLiteral("previewBlank"), session_.preview_is_blank(id));
        m.insert(QStringLiteral("live"), stack.program.occupied());
        m.insert(QStringLiteral("cued"), stack.preview && stack.preview->occupied());
        m.insert(QStringLiteral("preview"), stack.preview.has_value());
        out.push_back(m);
    }
    return out;
}

QString StackController::focusedStackId() const
{
    return QString::fromStdString(session_.focused_stack_id());
}

void StackController::setFocusedStackId(const QString& id)
{
    const std::string next = id.toStdString();
    if (next == session_.focused_stack_id() || session_.find_stack(next) == nullptr) {
        return;
    }
    session_.set_focused_stack_id(next);
    // Focus only picks what the controller shows. Outputs, previews, the rundown
    // and video playback are unchanged: no liveChanged. The application
    // follows focusedStackChanged (the line map shows the focused stack).
    refresh_state();
    emit focusedStackChanged();
    emit mediaStateChanged();
}

QString StackController::armedSendToId() const
{
    return QString::fromStdString(session_.armed_send_to());
}

bool StackController::showStackTabs() const
{
    return occupiedStacks().size() > 1;
}

QStringList StackController::iconIds() const
{
    QStringList out;
    for (const std::string_view icon : model::k_stack_icons) {
        out.push_back(QString::fromUtf8(icon));
    }
    return out;
}

void StackController::armSendTo(const QString& stack_id)
{
    session_.arm_send_to(stack_id.trimmed().toStdString());
    refresh_state();
}

QString StackController::clearStack(const QString& stack_id)
try {
    if (!session_.clear_stack(stack_id.toStdString())) {
        return refusal::stack_gone();
    }
    emit liveChanged(true);
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

void StackController::clearAllStacks()
try {
    session_.clear_all_stacks();
    emit liveChanged(true);
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

// Blank and freeze change what outputs paint, not which rows the models hold:
// a rebuild would reset the line map and scroll the controller back to the top.
bool StackController::toggleStackBlank(const QString& stack_id)
{
    return guarded_change(*this, __func__,
                          [&] { return session_.toggle_stack_blank(stack_id.toStdString()); },
                          [this] { emit liveChanged(false); });
}

bool StackController::togglePreviewBlank(const QString& stack_id)
{
    return guarded_change(*this, __func__,
                          [&] { return session_.toggle_preview_blank(stack_id.toStdString()); },
                          [this] { emit liveChanged(false); });
}

bool StackController::toggleStackFreeze(const QString& stack_id)
{
    return guarded_change(*this, __func__,
                          [&] { return session_.toggle_stack_freeze(stack_id.toStdString()); },
                          [this] { emit liveChanged(false); });
}

QString StackController::addStack(const QString& name, const QString& icon)
try {
    return commit(model::add_stack(library_.settings().stacks,
                                   name.isEmpty() ? "Stack" : name.toStdString(),
                                   icon.toStdString()));
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString StackController::removeStack(const QString& stack_id)
try {
    if (!model::find_stack(library_.settings().stacks, stack_id.toStdString())) {
        return refusal::stack_gone();
    }
    return commit(model::remove_stack(library_.settings().stacks, stack_id.toStdString(),
                                      library_.settings().stack_type_defaults));
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString StackController::renameStack(const QString& stack_id, const QString& name)
try {
    if (!model::find_stack(library_.settings().stacks, stack_id.toStdString())) {
        return refusal::stack_gone();
    }
    return commit(model::rename_stack(library_.settings().stacks, stack_id.toStdString(),
                                      name.toStdString()));
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString StackController::setStackIcon(const QString& stack_id, const QString& icon)
try {
    if (!model::find_stack(library_.settings().stacks, stack_id.toStdString())) {
        return refusal::stack_gone();
    }
    return commit(model::set_stack_icon(library_.settings().stacks, stack_id.toStdString(),
                                        icon.toStdString()));
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString StackController::reorderStacks(const QVariantList& ordered_ids)
try {
    const auto ids = ids_from_list(ordered_ids);
    for (const auto& id : ids) {
        if (!model::find_stack(library_.settings().stacks, id)) {
            return refusal::stack_gone();
        }
    }
    return commit(model::reorder_stacks(library_.settings().stacks, ids));
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString StackController::setStackPreview(const QString& stack_id, bool on)
try {
    const auto id = stack_id.trimmed().toStdString();
    const auto* stack = session_.find_stack(id);
    if (stack == nullptr) {
        return refusal::stack_gone();
    }
    if (stack->preview.has_value() == on) {
        return {};
    }
    if (const auto library_id = console_library_id(); !library_id.empty()) {
        auto* store = library_.console_store();
        if (!store->set_stack_preview(library_id, id, on)) {
            return tr("Could not save this computer's Preview stacks to %1")
                .arg(QString::fromStdString(store->file().string()));
        }
    }
    auto ids = session_.preview_stack_ids();
    if (on) {
        ids.insert(id);
    } else {
        ids.erase(id);
    }
    (void)session_.set_preview_stacks(ids);
    // The Controller's wells follow, and anything cued there went with it:
    // the application's reaction refreshes this controller's state too.
    emit liveChanged(true);
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

bool StackController::previewCued(const QString& stack_id) const
{
    const auto* stack = session_.find_stack(stack_id.trimmed().toStdString());
    return stack != nullptr && stack->preview && stack->preview->occupied();
}

QVariantMap StackController::stackTypeDefaults() const
{
    QVariantMap out;
    for (const auto key : model::k_stack_type_keys) {
        out.insert(QString::fromUtf8(key),
                   QString::fromStdString(library_.settings().stack_type_defaults.for_type(key)));
    }
    return out;
}

QString StackController::setStackTypeDefault(const QString& type_key, const QString& stack_id)
try {
    const auto key = type_key.trimmed().toStdString();
    if (std::ranges::find(model::k_stack_type_keys, key) == model::k_stack_type_keys.end()) {
        return tr("That is not a kind of content a stack receives.");
    }
    const auto id = stack_id.trimmed().toStdString();
    if (!model::find_stack(library_.settings().stacks, id)) {
        return refusal::stack_gone();
    }
    if (library_.settings().stack_type_defaults.for_type(key) == id) {
        return {};
    }
    library_.settings().stack_type_defaults.for_type(key) = id;
    return persist();
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString StackController::fallbackStackId() const
{
    return QString::fromStdString(model::fallback_stack_id(library_.settings().stacks));
}

QString StackController::releaseStackTypeDefault(const QString& type_key, const QString& stack_id)
try {
    auto& settings = library_.settings();
    const auto r = model::release_type_default(settings.stack_type_defaults, settings.stacks,
                                               type_key.trimmed().toStdString(),
                                               stack_id.trimmed().toStdString());
    if (!r.ok) {
        return QString::fromStdString(r.message);
    }
    return persist();
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString StackController::transition(const QString& stack_id)
{
    return take(stack_id, std::nullopt);
}

QString StackController::transitionWith(const QString& stack_id,
                                        const QString& type,
                                        int duration_ms,
                                        const QString& direction,
                                        const QString& easing)
{
    const auto how = transition_from(type, duration_ms, direction, easing);
    if (!how) {
        return tr("No such transition.");
    }
    return take(stack_id, how);
}

QString StackController::take(const QString& stack_id, std::optional<model::Transition> how)
try {
    const auto id = stack_id.trimmed().toStdString();
    if (session_.find_stack(id) == nullptr) {
        return refusal::stack_gone();
    }
    if (!session_.transition(id, how)) {
        return tr("Nothing is cued on that stack.");
    }
    // An advancing stack gives what came in its whole time, from the end of
    // its transition.
    if (advance_timer_ != nullptr && session_.auto_advance(id).enabled) {
        advance_due_ms_.insert(stack_id.trimmed(), advance_clock_.elapsed()
                                                       + session_.transition_ms_on(id)
                                                       + session_.auto_advance(id).interval_ms);
    }
    // The service's selected row and Also play followed what went live.
    emit liveChanged(true);
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString StackController::clearPreview(const QString& stack_id)
try {
    if (!session_.clear({stack_id.trimmed().toStdString(), session::StackSide::Preview})) {
        return tr("That stack has no Preview.");
    }
    emit liveChanged(true);
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

int StackController::takeFadeMs() const
{
    return library_.machine_settings().take_fade_ms;
}

QString StackController::setTakeFadeMs(int duration_ms)
try {
    auto settings = library_.machine_settings();
    const int ms = model::normalize_take_fade_ms(duration_ms);
    if (settings.take_fade_ms == ms) {
        return {};
    }
    settings.take_fade_ms = ms;
    if (!library_.save_machine_settings(std::move(settings))) {
        const auto* store = library_.machine_store();
        // Changed from the Controller, which has nowhere of its own to say so.
        const auto why = tr("Could not save this computer's settings to %1")
                             .arg(store ? QString::fromStdString(store->file().string()) : QString());
        emit errorOccurred(why);
        return why;
    }
    emit takeFadeMsChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

bool StackController::next_service_slide()
{
    return guarded_change(*this, __func__,
                          [&] { return session_.next_service_slide(); },
                          [this] { emit liveChanged(false); });
}

QString StackController::setAutoAdvance(const QString& stack_id,
                                        bool enabled,
                                        int interval_sec,
                                        bool loop,
                                        bool through,
                                        bool stop_at_dividers)
try {
    const std::string id = stack_id.trimmed().toStdString();
    if (id.empty() || session_.find_stack(id) == nullptr) {
        return refusal::stack_gone();
    }
    session::AutoAdvanceState state;
    state.enabled = enabled;
    state.interval_ms = std::clamp(interval_sec, 1, 3600) * 1000;
    state.loop = loop;
    state.through = through;
    state.stop_at_dividers = stop_at_dividers;
    session_.set_auto_advance(id, state);
    // Any settings change restarts the cadence rather than firing early against
    // a deadline computed from the old interval.
    advance_due_ms_.remove(stack_id);
    update_advance_timer();
    emit autoAdvanceChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QVariantMap StackController::autoAdvanceFor(const QString& stack_id) const
{
    const auto state = session_.auto_advance(stack_id.toStdString());
    QVariantMap out;
    out.insert(QStringLiteral("enabled"), state.enabled);
    out.insert(QStringLiteral("intervalSec"), state.interval_ms / 1000);
    out.insert(QStringLiteral("loop"), state.loop);
    out.insert(QStringLiteral("through"), state.through);
    out.insert(QStringLiteral("stopAtDividers"), state.stop_at_dividers);
    return out;
}

QStringList StackController::advancingStackIds() const
{
    QStringList out;
    for (const auto& id : session_.advancing_stack_ids()) {
        out.push_back(QString::fromStdString(id));
    }
    return out;
}

void StackController::update_advance_timer()
{
    const bool need = !session_.advancing_stack_ids().empty();
    if (advance_timer_ == nullptr) {
        if (!need) {
            return;
        }
        advance_clock_.start();
        advance_timer_ = new QTimer(this);
        // Coarser than the shortest interval a user can set, so a one-second
        // cadence still lands within a quarter second of its deadline.
        advance_timer_->setInterval(250);
        connect(advance_timer_, &QTimer::timeout, this, &StackController::on_advance_tick);
    }
    if (need && !advance_timer_->isActive()) {
        advance_timer_->start();
    } else if (!need && advance_timer_->isActive()) {
        advance_timer_->stop();
        advance_due_ms_.clear();
    }
}

void StackController::on_advance_tick()
{
    const qint64 now = advance_clock_.elapsed();
    bool advanced = false;
    bool changed_item = false;
    bool exhausted = false;

    for (const auto& raw_id : session_.advancing_stack_ids()) {
        const QString id = QString::fromStdString(raw_id);
        const auto state = session_.auto_advance(raw_id);
        const auto due = advance_due_ms_.constFind(id);
        if (due == advance_due_ms_.cend()) {
            // First sight of this stack - start counting from now.
            advance_due_ms_.insert(id, now + state.interval_ms);
            continue;
        }
        if (now < *due) {
            continue;
        }
        const auto tick = session_.tick_auto_advance(raw_id);
        advanced = advanced || tick.did_advance;
        changed_item = changed_item || tick.changed_item;
        if (tick.exhausted) {
            advance_due_ms_.remove(id);
            exhausted = true;
        } else {
            // The slide's time runs from the end of its transition.
            const int coming_in = tick.did_advance ? session_.transition_ms_on(raw_id) : 0;
            advance_due_ms_.insert(id, now + coming_in + state.interval_ms);
        }
    }

    if (advanced) {
        emit liveChanged(changed_item);
    }
    if (exhausted) {
        update_advance_timer();
        emit autoAdvanceChanged();
    }
}

} // namespace seventhslide::app
