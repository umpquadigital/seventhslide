// SessionFacade: the outputs.
//
// One slice of SessionFacade (declared in session_facade.hpp): the output list
// and its settings, and keeping the output host in step with it. The
// destinations each output paints to are destinations_controller.hpp; the
// operator previews output_preview_controller.hpp.

#include "session_facade.hpp"

#include "destinations_controller.hpp"
#include "output_list_model.hpp"
#include "qvariant_json.hpp"

#include <seventhslide/model/output_library.hpp>
#include <seventhslide/render/output_window_host.hpp>

#include <QSet>

#include <initializer_list>
#include <string>
#include <utility>

namespace seventhslide::app {

void SessionFacade::save_outputs()
{
    if (auto* config = open_library_.config()) {
        config->save_outputs(session_.outputs());
    }
}

void SessionFacade::rebuild_after_outputs_edited()
{
    // Rebuild slides so live outputs pick up theme edits.
    session_.rebuild();
    // One deleted is forgotten as off.
    push_outputs_off();
    after_mutation(true);
}

void SessionFacade::load_outputs_off()
{
    auto* store = open_library_.destination_store();
    if (open_library_.registry() == nullptr || store == nullptr) {
        // Nowhere to keep them: every output is on, whatever was off before.
        session_.set_outputs_off({});
        return;
    }
    session_.set_outputs_off(store->outputs_off(open_library_.destinations_library_id()));
}

void SessionFacade::apply_outputs_on()
{
    push_outputs_off();
    after_mutation(true);
    // Opens what an output switched back on paints to.
    restore_output_windows();
}

void SessionFacade::push_outputs_off()
{
    if (host_ == nullptr) {
        return;
    }
    QSet<QString> off;
    for (const auto& id : session_.outputs_off()) {
        off.insert(QString::fromStdString(id));
    }
    host_->set_outputs_off(std::move(off));
}

void SessionFacade::set_outputs(std::vector<model::OutputConfig> outputs)
{
    session_.set_outputs(std::move(outputs));
    ready_ = true;
    after_mutation(true);
    emit readyChanged();
}

QString SessionFacade::primary_output_name() const
{
    const auto* primary = session_.primary_output();
    return primary ? QString::fromStdString(primary->name) : QString();
}

void SessionFacade::push_output_names()
{
    if (host_ == nullptr) {
        return;
    }
    QHash<QString, QString> names;
    for (const auto& oc : session_.outputs()) {
        names.insert(QString::fromStdString(oc.id), QString::fromStdString(oc.name));
    }
    host_->set_output_names(std::move(names));
}

void SessionFacade::sync_output_host_state()
{
    destinations_->refreshDisplays();
    push_output_names();
    const std::initializer_list<OutputListModel*> models{outputs_model_.get(),
                                                         outputs_on_model_.get()};
    for (const auto& oc : session_.outputs()) {
        const QString id = QString::fromStdString(oc.id);
        const bool open = host_ != nullptr && host_->is_open(id);
        // The output strip names the target of the output's first destination.
        const render::Destination* first = nullptr;
        if (host_ != nullptr) {
            for (const auto& d : host_->registry().all()) {
                if (d.output_id == id) {
                    first = &d;
                    break;
                }
            }
        }
        for (auto* model : models) {
            model->set_open(id, open);
            if (first != nullptr) {
                model->set_screen(id, first->target_key, host_->devices().target_label(*first));
            }
        }
    }
    for (auto* model : models) {
        model->notify_rows_changed();
    }
}

void SessionFacade::load_output_destinations()
{
    if (host_ == nullptr || open_library_.registry() == nullptr) {
        return;
    }
    const auto stored =
        open_library_.destination_store()->load(open_library_.destinations_library_id());
    auto records = nlohmann::json::array();
    // Records of an output this library no longer has stay stored, unused.
    for (const auto& oc : session_.outputs()) {
        const auto it = stored.find(oc.id);
        if (it == stored.end()) {
            continue;
        }
        for (auto record : it->second) {
            record["output"] = oc.id;
            records.push_back(std::move(record));
        }
    }
    // Names first, so windows the destinations open are titled from the start.
    push_output_names();
    host_->replace_destinations(records);
    push_outputs_off();
}

void SessionFacade::save_output_destinations()
{
    auto* store = open_library_.destination_store();
    const auto& library_id = open_library_.destinations_library_id();
    if (host_ == nullptr || store == nullptr || library_id.empty()) {
        return;
    }
    // Every current output gets an entry, so one whose last destination was
    // removed is cleared rather than left holding its old records.
    library::OutputDestinations by_output;
    for (const auto& oc : session_.outputs()) {
        by_output[oc.id] = nlohmann::json::array();
    }
    for (auto record : host_->destination_records()) {
        const auto it = by_output.find(record.value("output", std::string{}));
        if (it == by_output.end()) {
            continue;
        }
        record.erase("output");
        it->second.push_back(std::move(record));
    }
    if (!store->save(library_id, by_output)) {
        emit errorOccurred(tr("Could not save output destinations to %1")
                               .arg(QString::fromStdString(store->file().string())));
    }
}

void SessionFacade::restore_output_windows()
{
    if (host_ == nullptr || !audience_outputs_enabled_) {
        return;
    }
    host_->restore();
    destinations_->notify_changed();
    sync_output_host_state();
    present_frames();
}

} // namespace seventhslide::app
