#include "output_settings_controller.hpp"

#include "destination_rows.hpp"
#include "entry_error.hpp"
#include "refusals.hpp"
#include "qml_ids.hpp"
#include "qvariant_json.hpp"

#include <seventhslide/db/config_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/geom/canvas_presets.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/theme/theme_elements.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <exception>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace seventhslide::app {

OutputSettingsController::OutputSettingsController(session::Session& session,
                                                   OpenLibrary& library,
                                                   QObject* parent)
    : QObject(parent)
    , session_(session)
    , library_(library)
{
}

QVariantMap OutputSettingsController::getOutputSettings(const QString& output_name)
{
    const auto* oc = model::find_output_by_name(session_.outputs(), output_name.toStdString());
    if (oc == nullptr) {
        return {};
    }
    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromStdString(oc->id));
    m.insert(QStringLiteral("name"), QString::fromStdString(oc->name));
    // As it paints: a size that is no canvas is the reference one.
    const auto canvas = geom::canvas_or_reference(oc->canvas_width, oc->canvas_height);
    m.insert(QStringLiteral("canvasWidth"), canvas.width);
    m.insert(QStringLiteral("canvasHeight"), canvas.height);
    QVariantList enabled;
    for (const auto& id : oc->enabled_stack_ids) {
        enabled.push_back(QString::fromStdString(id));
    }
    m.insert(QStringLiteral("enabledStackIds"), enabled);
    QVariantList disabled_types;
    for (const auto& type : oc->disabled_content_types) {
        disabled_types.push_back(QString::fromStdString(type));
    }
    m.insert(QStringLiteral("disabledContentTypes"), disabled_types);
    m.insert(QStringLiteral("exemptFromGlobalBlank"), oc->exempt_from_global_blank);
    m.insert(QStringLiteral("exemptFromGlobalFreeze"), oc->exempt_from_global_freeze);
    m.insert(QStringLiteral("icon"), QString::fromStdString(oc->icon));
    m.insert(QStringLiteral("audioFades"),
             QVariantMap{{QStringLiteral("fade_in_ms"), oc->audio_fades.fade_in_ms},
                         {QStringLiteral("fade_out_ms"), oc->audio_fades.fade_out_ms},
                         {QStringLiteral("overlap_ms"), oc->audio_fades.overlap_ms},
                         {QStringLiteral("on_play_pause"), oc->audio_fades.on_play_pause}});
    if (windows_ != nullptr) {
        m.insert(QStringLiteral("destinations"),
                 destination_rows(*windows_, QString::fromStdString(oc->id)));
    }
    return m;
}

QVariantMap OutputSettingsController::updateOutputSettings(const QString& output_name,
                                                           const QVariantMap& fields)
try {
    QVariantMap out;
    auto& outputs = session_.outputs();
    const auto found = model::find_output_index(outputs, output_name.toStdString());
    if (!found) {
        out.insert(QStringLiteral("error"), refusal::output_gone());
        return out;
    }
    const std::size_t index = *found;
    auto general = model::output_general(outputs[index]);
    const auto field = [&fields](const char* key) { return fields.value(QLatin1String(key)); };
    if (fields.contains(QStringLiteral("name"))) {
        general.name = field("name").toString().toStdString();
    }
    if (fields.contains(QStringLiteral("canvasWidth"))) {
        general.canvas_width = field("canvasWidth").toInt();
    }
    if (fields.contains(QStringLiteral("canvasHeight"))) {
        general.canvas_height = field("canvasHeight").toInt();
    }
    if (fields.contains(QStringLiteral("enabledStackIds"))) {
        general.enabled_stack_ids = ids_from_list(field("enabledStackIds").toList());
    }
    if (fields.contains(QStringLiteral("disabledContentTypes"))) {
        general.disabled_content_types = ids_from_list(field("disabledContentTypes").toList());
    }
    if (fields.contains(QStringLiteral("exemptFromGlobalBlank"))) {
        general.exempt_from_global_blank = field("exemptFromGlobalBlank").toBool();
    }
    if (fields.contains(QStringLiteral("exemptFromGlobalFreeze"))) {
        general.exempt_from_global_freeze = field("exemptFromGlobalFreeze").toBool();
    }
    if (fields.contains(QStringLiteral("icon"))) {
        general.icon = field("icon").toString().toStdString();
    }
    if (fields.contains(QStringLiteral("audioFades"))) {
        // Each field it sets over the output's own; each clamped.
        general.audio_fades =
            model::override_audio_fades(general.audio_fades, to_json(field("audioFades")));
    }
    const geom::CanvasSize before = model::output_canvas(outputs[index]);
    const auto result = model::update_output_settings(outputs[index], outputs, index,
                                                      std::move(general));
    if (!result.ok) {
        out.insert(QStringLiteral("error"), QString::fromStdString(result.message));
        return out;
    }
    QString fitted;
    if (model::output_canvas(outputs[index]) != before) {
        fitted = fit_to_canvas(outputs, index);
    }
    save_outputs();
    emit outputsEdited();
    out.insert(QStringLiteral("name"), QString::fromStdString(result.name));
    if (!fitted.isEmpty()) {
        out.insert(QStringLiteral("fitted"), fitted);
        emit canvasFitted(fitted);
    }
    return out;
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), report_entry_error(__func__, ex)}};
}

void OutputSettingsController::save_outputs() const
{
    if (save_outputs_) {
        save_outputs_();
    }
}

QString OutputSettingsController::fit_to_canvas(std::vector<model::OutputConfig>& outputs,
                                                std::size_t index)
{
    auto& oc = outputs[index];
    const geom::CanvasSize canvas = model::output_canvas(oc);
    int themes = 0;
    for (const auto kind : theme::k_theme_kinds) {
        if (kind == theme::ThemeKind::Bg) {
            continue;
        }
        for (auto& entry : theme::theme_list(oc, kind)) {
            if (theme::resize_elements(entry.style, canvas)) {
                ++themes;
            }
        }
    }
    // Every deck keyed to the output, in one transaction with the outputs:
    // a failure part-way leaves the library as it was.
    int items = 0;
    if (auto* db = library_.database()) {
        db::Transaction tx(*db);
        db::FreeformRepository repo(*db);
        for (auto& item : repo.list_items()) {
            if (!freeform::document_has_deck(item.document, oc.id)) {
                continue;
            }
            auto doc = freeform::load_document(item.document, item.name);
            if (!doc || !freeform::resize_output_deck(*doc, oc.id, canvas)) {
                continue;
            }
            item.document = doc->to_json();
            repo.update_item(item);
            ++items;
        }
        if (auto* config = library_.config()) {
            config->save_outputs(outputs);
        }
        tx.commit();
    }
    if (themes == 0 && items == 0) {
        return {};
    }
    QStringList what;
    if (themes > 0) {
        what << (themes == 1 ? tr("1 theme") : tr("%1 themes").arg(themes));
    }
    if (items > 0) {
        what << (items == 1 ? tr("1 Freeform item") : tr("%1 Freeform items").arg(items));
    }
    const QString size = QStringLiteral("%1 × %2").arg(canvas.width).arg(canvas.height);
    return (themes + items == 1 ? tr("%1 was fitted to %2") : tr("%1 were fitted to %2"))
        .arg(what.join(tr(" and ")), size);
}

QStringList OutputSettingsController::iconIds() const
{
    QStringList out;
    for (const auto icon : model::k_output_icons) {
        out.push_back(QString::fromUtf8(icon));
    }
    return out;
}

QVariantList OutputSettingsController::canvasPresetGroups() const
{
    const auto& all = geom::k_canvas_presets;
    QVariantList groups;
    QVariantList presets;
    for (std::size_t i = 0; i < std::size(all); ++i) {
        const auto& preset = all[i];
        presets.push_back(QVariantMap{
            {QStringLiteral("name"), QString::fromUtf8(preset.name)},
            {QStringLiteral("title"), QString::fromUtf8(geom::canvas_preset_title(preset))},
            {QStringLiteral("width"), preset.size.width},
            {QStringLiteral("height"), preset.size.height}});
        // Each shape's presets are together: its group ends where the next
        // preset's shape differs, or the list does.
        if (i + 1 == std::size(all) || all[i + 1].shape != preset.shape) {
            groups.push_back(QVariantMap{
                {QStringLiteral("heading"),
                 QString::fromUtf8(geom::canvas_shape_heading(preset.shape))},
                {QStringLiteral("presets"), presets}});
            presets.clear();
        }
    }
    return groups;
}

QVariantMap OutputSettingsController::describeCanvas(int width, int height) const
{
    const geom::CanvasSize size{width, height};
    const auto* preset = geom::find_canvas_preset(size);
    return {{QStringLiteral("preset"),
             preset != nullptr ? QString::fromUtf8(geom::canvas_preset_title(*preset))
                               : QString()},
            {QStringLiteral("aspect"), QString::fromUtf8(geom::canvas_aspect_label(size))},
            {QStringLiteral("problem"), QString::fromUtf8(geom::canvas_size_problem(size))}};
}

QVariantMap OutputSettingsController::createOutput(const QString& name)
try {
    auto& outputs = session_.outputs();
    outputs.push_back(theme::new_output(name.toStdString(), outputs));
    const auto created = QString::fromStdString(outputs.back().name);
    save_outputs();
    emit outputsEdited();
    return {{QStringLiteral("name"), created}};
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), report_entry_error(__func__, ex)}};
}

QString OutputSettingsController::deleteOutput(const QString& output_name)
try {
    auto& outputs = session_.outputs();
    const auto found = model::find_output_index(outputs, output_name.toStdString());
    if (!found) {
        return refusal::output_gone();
    }
    const std::size_t index = *found;
    // Its background themes' files go only once the outputs without it are
    // saved: a failure before then would leave stored themes naming files
    // that are gone.
    const std::string output_id = outputs[index].id;
    // The only output on going leaves the rest off; the rebuild switches the
    // first of them on.
    const bool last_on = session_.output_is_on(output_id) && session_.outputs_on().size() == 1;
    std::vector<std::string> bg_theme_ids;
    bg_theme_ids.reserve(outputs[index].bg_themes.size());
    for (const auto& theme : outputs[index].bg_themes) {
        bg_theme_ids.push_back(theme.id);
    }
    auto before = outputs;
    const auto result = model::remove_output(outputs, index);
    if (!result.ok) {
        return QString::fromStdString(result.message);
    }
    try {
        save_outputs();
    } catch (const std::exception& ex) {
        // Not saved: the output stays, with its windows, destinations and files.
        outputs = std::move(before);
        return report_entry_error(__func__, ex);
    }
    const auto media_dir = library_.media_root();
    for (const auto& theme_id : bg_theme_ids) {
        theme::delete_theme_owned_files(theme_id, media_dir);
    }
    if (windows_ != nullptr) {
        windows_->remove_output(QString::fromStdString(output_id));
    }
    auto* destinations = library_.destination_store();
    const auto& library_id = library_.destinations_library_id();
    if (destinations != nullptr && !library_id.empty()
        && !destinations->remove_output(library_id, output_id)) {
        emit errorOccurred(tr("Could not update %1")
                               .arg(QString::fromStdString(destinations->file().string())));
    }
    emit outputsEdited();
    if (last_on) {
        const auto& on = session_.outputs_on();
        if (destinations != nullptr && !library_id.empty() && !on.empty()) {
            (void)destinations->set_output_on(library_id, on.front().id, true);
        }
        emit outputsSwitched();
    }
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString OutputSettingsController::reorderOutputs(const QVariantList& ordered_names)
try {
    const auto result = model::reorder_outputs(session_.outputs(), ids_from_list(ordered_names));
    if (!result.ok) {
        return QString::fromStdString(result.message);
    }
    save_outputs();
    emit outputsEdited();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString OutputSettingsController::setOutputOn(const QString& output_name, bool on)
try {
    const auto* oc = model::find_output_by_name(session_.outputs(), output_name.toStdString());
    if (oc == nullptr) {
        return refusal::output_gone();
    }
    const std::string output_id = oc->id;
    if (session_.output_is_on(output_id) == on) {
        return {};
    }
    if (!session_.set_output_on(output_id, on)) {
        return tr("One output is always on");
    }
    auto* store = library_.destination_store();
    const auto& library_id = library_.destinations_library_id();
    if (store != nullptr && !library_id.empty()
        && !store->set_output_on(library_id, output_id, on)) {
        emit errorOccurred(tr("Could not update %1")
                               .arg(QString::fromStdString(store->file().string())));
    }
    emit outputsSwitched();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

} // namespace seventhslide::app
