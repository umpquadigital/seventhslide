// The theme library and its previews: theme CRUD and category defaults per
// output, owned background assets, and the gallery and designer-draft images.
// The designer (the element editor) is the slide editor's session,
// slide_edit_session.hpp; drafts it sends are previewed here.

#include "theme_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "local_file_path.hpp"
#include "media_thumb_cache.hpp"
#include "qvariant_json.hpp"
#include "requested_size.hpp"
#include "element_draft_source.hpp"
#include "theme_thumb_cache.hpp"

#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/model/content_kind.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/scene/theme_draft_preview.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/theme/theme_library.hpp>
#include <seventhslide/theme/theme_picks.hpp>

#include <QMetaObject>
#include <QPainter>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace seventhslide::app {

namespace {

/// A theme kind the view named that is not one ("song", "bible", "bg", ...).
QString unknown_theme_kind()
{
    return ThemeController::tr("That is not a kind of theme SeventhSlide has.");
}

/// ``t`` for QML, its style as stored: a theme is kept on its output's
/// canvas, and the designer edits what is stored.
QVariantMap theme_entry_to_map(const model::ThemeEntry& t)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromStdString(t.id));
    m.insert(QStringLiteral("name"), QString::fromStdString(t.name));
    m.insert(QStringLiteral("style"), to_qvariant(t.style));
    QStringList tags;
    tags.reserve(static_cast<int>(t.tags.size()));
    for (const auto& tag : t.tags) {
        tags.push_back(QString::fromStdString(tag));
    }
    m.insert(QStringLiteral("tags"), tags);
    m.insert(QStringLiteral("createdAt"), QString::fromStdString(t.created_at));
    return m;
}

[[nodiscard]] QImage paint_theme_preview_grid(const QSize& size, int cell_px)
{
    const QSize want = size_or(size, QSize(960, 540));
    const int cell = cell_px > 0 ? cell_px : 24;
    QImage out(want, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&out);
    render::paint_transparency_checkerboard(
        painter, QRectF(QPointF(0, 0), QSizeF(want)), cell);
    painter.end();
    return out;
}

/// The designer context a request names: a background theme's fill, or the
/// text-theme context QML asks for. A theme shows only its own kind's
/// contexts, so one it names none of (as a gallery thumbnail does), or
/// another kind's, is the kind's first.
[[nodiscard]] scene::DraftContext draft_context_for(const QString& kind, const QString& context)
{
    const auto parsed = theme::parse_theme_kind(kind.toStdString());
    if (!parsed || *parsed == theme::ThemeKind::Bg) {
        return scene::DraftContext::Background;
    }
    if (const auto named = scene::draft_context_from_string(context.toStdString())) {
        const auto design = scene::element_context_of(*named);
        if (design && theme::kind_of(*design) == *parsed) {
            return *named;
        }
    }
    return scene::draft_context_from_string(
               theme::to_string(theme::primary_context(*parsed)))
        .value_or(scene::DraftContext::Song);
}

/// A still of ``frame`` at ``size`` over a transparent letterbox, with the
/// preview resources. Any thread.
[[nodiscard]] render::StillPaint still_of(scene::SceneFrame frame,
                                          const QSize& size,
                                          bool checkerboard,
                                          PreviewResources& previews)
{
    render::StillPaint still;
    still.frame = scene::share_frame(std::move(frame));
    still.size = size;
    still.letterbox = QColor(Qt::transparent);
    still.checkerboard = checkerboard;
    still.cache = previews.image_cache();
    // Paint APIs take const faces* - resolve mutates an internal cache under mutex.
    still.faces = &previews.measurer().faces();
    return still;
}

/// Why an upload's file was not used, as the dialog shows it alone: a
/// sentence with the file's name ("“sunset.png”: the file could not be
/// found."). ``why`` is one of file_importer.hpp's reasons.
[[nodiscard]] QString asset_not_imported(const ImportFile& file, const std::string& why)
{
    return ThemeController::tr("“%1”: %2.")
        .arg(QString::fromStdString(file.display), QString::fromStdString(why));
}

}  // namespace

ThemeController::ThemeController(session::Session& session,
                                 OpenLibrary& library,
                                 PreviewResources& previews,
                                 QObject* parent)
    : QObject(parent)
    , session_(session)
    , library_(library)
    , previews_(previews)
    , asset_copier_([this] { emit assetCopyChanged(); })
{
}

void ThemeController::add_designer(const ElementDraftSource* designer)
{
    if (designer != nullptr) {
        designers_.push_back(designer);
    }
}

void ThemeController::apply_designer_elements(nlohmann::json& style) const
{
    for (const auto* designer : designers_) {
        designer->apply_elements(style);
    }
}

const model::OutputConfig* ThemeController::find_output(const QString& name) const
{
    return model::find_output_by_name(session_.outputs(), name.toStdString());
}

model::OutputConfig* ThemeController::find_output_mutable(const QString& name)
{
    return model::find_output_by_name(session_.outputs(), name.toStdString());
}

void ThemeController::save_outputs() const
{
    if (save_outputs_) {
        save_outputs_();
    }
}

QString ThemeController::save_or_put_back(model::OutputConfig& oc, model::OutputConfig before)
{
    try {
        save_outputs();
        return {};
    } catch (const std::exception& ex) {
        oc = std::move(before);
        return report_entry_error("save_outputs", ex);
    }
}

void ThemeController::library_changed()
{
    previews_.bump_theme_revision();
    preview_cache_.clear();
    emit libraryChanged();
}

QVariantList ThemeController::list(const QString& output_name, const QString& kind)
try {
    QVariantList out;
    const auto* oc = find_output(output_name);
    const auto parsed = theme::parse_theme_kind(kind.toStdString());
    if (oc == nullptr || !parsed) {
        return out;
    }
    for (const auto& t : theme::theme_list(*oc, *parsed)) {
        out.push_back(theme_entry_to_map(t));
    }
    return out;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return {};
}

QVariantMap ThemeController::theme_map(const QString& output_name,
                                    const QString& kind,
                                    const QString& theme_id)
try {
    const auto* oc = find_output(output_name);
    const auto parsed = theme::parse_theme_kind(kind.toStdString());
    if (oc == nullptr || !parsed) {
        return {};
    }
    const auto* t = theme::find_theme(*oc, *parsed, theme_id.toStdString());
    if (t == nullptr) {
        return {};
    }
    return theme_entry_to_map(*t);
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), report_entry_error(__func__, ex)}};
}

QVariantMap ThemeController::categoryDefaults(const QString& output_name)
{
    const auto* oc = find_output(output_name);
    if (oc == nullptr) {
        return {};
    }
    const auto v = to_qvariant(oc->category_defaults);
    return v.toMap();
}

QVariantMap ThemeController::create(const QString& output_name,
                                       const QString& kind,
                                       const QString& name,
                                       const QVariantMap& style,
                                       const QString& tags,
                                       const QString& duplicate_from_id)
try {
    QVariantMap err;
    auto* oc = find_output_mutable(output_name);
    const auto parsed = theme::parse_theme_kind(kind.toStdString());
    if (oc == nullptr || !parsed) {
        err.insert(QStringLiteral("error"),
                   oc == nullptr ? refusal::output_gone() : unknown_theme_kind());
        return err;
    }
    const auto style_json = to_json(style);
    // Put back if the theme cannot be kept yet, or is not saved.
    auto before = *oc;
    auto result = theme::create_theme(*oc,
                                      *parsed,
                                      name.toStdString(),
                                      style_json.is_object() ? style_json : nlohmann::json::object(),
                                      tags.isEmpty() ? nlohmann::json() : nlohmann::json(tags.toStdString()),
                                      duplicate_from_id.toStdString());
    if (!result.ok) {
        err.insert(QStringLiteral("error"), QString::fromStdString(result.message));
        return err;
    }
    std::vector<theme::PendingAssetCopy> copies;
    if (*parsed == theme::ThemeKind::Bg) {
        if (auto* t = theme::find_theme(*oc, *parsed, result.theme.id)) {
            if (const auto why = why_still_coming(*t, {}); !why.isEmpty()) {
                *oc = std::move(before);
                err.insert(QStringLiteral("error"), why);
                return err;
            }
            copies = theme::sync_theme_owned_assets(*t, library_.media_root()).copies;
            result.theme = *t;
        }
    }
    // Not saved: no theme, and none of the files made for it.
    if (auto why = save_or_put_back(*oc, std::move(before)); !why.isEmpty()) {
        if (*parsed == theme::ThemeKind::Bg) {
            theme::delete_theme_owned_files(result.theme.id, library_.media_root());
        }
        err.insert(QStringLiteral("error"), why);
        return err;
    }
    // Saved: any file of its own it does not name can go.
    if (const auto* t = *parsed == theme::ThemeKind::Bg
                            ? theme::find_theme(*oc, *parsed, result.theme.id)
                            : nullptr) {
        theme::prune_unused_theme_assets(*t, library_.media_root());
    }
    emit outputsEdited();
    library_changed();
    copy_shared_assets(output_name, result.theme.id, copies);
    return theme_entry_to_map(result.theme);
} catch (const std::exception& ex) {
    return {{QStringLiteral("error"), report_entry_error(__func__, ex)}};
}

QString ThemeController::update_theme(const QString& output_name,
                                      const QString& kind,
                                      const QString& theme_id,
                                      const QString& name,
                                      nlohmann::json style,
                                      const QString& tags,
                                      bool apply_tags)
try {
    auto* oc = find_output_mutable(output_name);
    const auto parsed = theme::parse_theme_kind(kind.toStdString());
    if (oc == nullptr) {
        return refusal::output_gone();
    }
    if (!parsed) {
        return unknown_theme_kind();
    }
    auto style_json = std::move(style);
    if (!style_json.is_object()) {
        style_json = nlohmann::json::object();
    }
    apply_designer_elements(style_json);
    // Put back if the theme cannot be kept yet, or is not saved. What it
    // shared before stays as it was.
    auto before = *oc;
    std::vector<std::string> held;
    if (*parsed == theme::ThemeKind::Bg) {
        if (const auto* t = theme::find_theme(*oc, *parsed, theme_id.toStdString())) {
            held = theme::shared_assets_not_here(*t, library_.media_root());
        }
    }
    auto result = theme::update_theme(
        *oc,
        *parsed,
        theme_id.toStdString(),
        name.toStdString(),
        style_json,
        tags.toStdString(),
        apply_tags);
    if (!result.ok) {
        return QString::fromStdString(result.message);
    }
    std::vector<theme::PendingAssetCopy> copies;
    if (*parsed == theme::ThemeKind::Bg) {
        if (auto* t = theme::find_theme(*oc, *parsed, theme_id.toStdString())) {
            if (const auto why = why_still_coming(*t, held); !why.isEmpty()) {
                *oc = std::move(before);
                return why;
            }
            copies = theme::sync_theme_owned_assets(*t, library_.media_root()).copies;
        }
    }
    const bool bg = *parsed == theme::ThemeKind::Bg;
    const auto id = theme_id.toStdString();
    if (auto why = save_or_put_back(*oc, std::move(before)); !why.isEmpty()) {
        // Not saved: the theme is as it was, and keeps the files it named;
        // those just made for it go.
        if (const auto* t = bg ? theme::find_theme(*oc, *parsed, id) : nullptr) {
            theme::prune_unused_theme_assets(*t, library_.media_root());
        }
        return why;
    }
    // Saved: the files it no longer names can go.
    if (const auto* t = bg ? theme::find_theme(*oc, *parsed, id) : nullptr) {
        theme::prune_unused_theme_assets(*t, library_.media_root());
    }
    emit outputsEdited();
    library_changed();
    copy_shared_assets(output_name, theme_id.toStdString(), copies);
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString ThemeController::why_still_coming(const model::ThemeEntry& theme,
                                          const std::vector<std::string>& held) const
{
    if (!on_its_way_) {
        return {};
    }
    for (const auto& path : theme::shared_assets_not_here(theme, library_.media_root())) {
        if (std::ranges::find(held, path) == held.end() && on_its_way_(path)) {
            return tr("Its background is still on its way from another computer. Try again once it "
                      "has come, so this theme gets a copy of its own.");
        }
    }
    return {};
}

QString ThemeController::remove(const QString& output_name,
                                   const QString& kind,
                                   const QString& theme_id)
try {
    auto* oc = find_output_mutable(output_name);
    const auto parsed = theme::parse_theme_kind(kind.toStdString());
    if (oc == nullptr) {
        return refusal::output_gone();
    }
    if (!parsed) {
        return unknown_theme_kind();
    }
    auto before = *oc;
    const auto result = theme::delete_theme(*oc, *parsed, theme_id.toStdString());
    if (!result.ok) {
        return QString::fromStdString(result.message);
    }
    // Not saved: the theme stays, and so do its files.
    if (auto why = save_or_put_back(*oc, std::move(before)); !why.isEmpty()) {
        return why;
    }
    // Saved without it: its files can go.
    if (*parsed == theme::ThemeKind::Bg) {
        theme::delete_theme_owned_files(theme_id.toStdString(), library_.media_root());
    }
    emit outputsEdited();
    library_changed();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString ThemeController::setDefault(const QString& output_name,
                                       const QString& category,
                                       const QString& field,
                                       const QString& theme_id)
try {
    auto* oc = find_output_mutable(output_name);
    if (oc == nullptr) {
        return refusal::output_gone();
    }
    const auto result = theme::set_category_default(
        *oc, category.toStdString(), field.toStdString(), theme_id.toStdString());
    if (!result.ok) {
        return QString::fromStdString(result.message);
    }
    save_outputs();
    emit outputsEdited();
    library_changed();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

namespace {

/// The theme library a slot picks from: the category's text themes, or the
/// backgrounds.
[[nodiscard]] QString library_kind(const theme::PickSlot& slot)
{
    const auto kind = slot.field == theme::ThemeField::Bg ? theme::ThemeKind::Bg
                                                          : theme::text_kind_of(slot.category);
    return QString::fromUtf8(theme::to_string(kind));
}

/// What a category is called on its own: a default's chip.
[[nodiscard]] QString category_label(theme::ThemeCategory category)
{
    switch (category) {
    case theme::ThemeCategory::Song:
        return ThemeController::tr("Song");
    case theme::ThemeCategory::Bible:
        return ThemeController::tr("Bible");
    case theme::ThemeCategory::Media:
        return ThemeController::tr("Media");
    }
    return {};
}

/// What a category's group of slots is headed.
[[nodiscard]] QString category_heading(theme::ThemeCategory category)
{
    switch (category) {
    case theme::ThemeCategory::Song:
        return ThemeController::tr("Songs");
    case theme::ThemeCategory::Bible:
        return ThemeController::tr("Bible");
    case theme::ThemeCategory::Media:
        return ThemeController::tr("Pictures, video and web pages");
    }
    return {};
}

[[nodiscard]] QString field_label(theme::ThemeField field)
{
    return field == theme::ThemeField::Bg ? ThemeController::tr("Background")
                                          : ThemeController::tr("Text");
}

[[nodiscard]] QVariantMap slot_map(const theme::PickSlot& slot, QString label)
{
    return {
        {QStringLiteral("category"), QString::fromUtf8(theme::to_string(slot.category))},
        {QStringLiteral("field"), QString::fromUtf8(theme::to_string(slot.field))},
        {QStringLiteral("kind"), library_kind(slot)},
        {QStringLiteral("label"), std::move(label)},
    };
}

}  // namespace

QVariantList ThemeController::pickSlots(const QString& scope) const
{
    const auto name = scope.toStdString();
    std::vector<theme::PickSlot> offered;
    if (name == "service") {
        offered = theme::pick_slots(theme::CategorySet::all());
    } else if (const auto kind = model::content_kind_from_string(name);
               model::to_string(kind) == name) {
        offered = theme::pick_slots(kind);
    }
    QVariantList groups;
    for (const auto& slot : offered) {
        const auto category = QString::fromUtf8(theme::to_string(slot.category));
        if (groups.isEmpty()
            || groups.back().toMap().value(QStringLiteral("category")).toString() != category) {
            groups.push_back(QVariantMap{
                {QStringLiteral("category"), category},
                {QStringLiteral("label"), category_heading(slot.category)},
                {QStringLiteral("slots"), QVariantList{}},
            });
        }
        auto group = groups.back().toMap();
        auto list = group.value(QStringLiteral("slots")).toList();
        list.push_back(slot_map(slot, field_label(slot.field)));
        group.insert(QStringLiteral("slots"), list);
        groups.back() = group;
    }
    return groups;
}

QVariantList ThemeController::kinds() const
{
    QVariantList out;
    for (const auto kind : theme::k_theme_kinds) {
        QString label;
        switch (kind) {
        case theme::ThemeKind::Song:
            label = tr("Song Themes");
            break;
        case theme::ThemeKind::Bible:
            label = tr("Bible Themes");
            break;
        case theme::ThemeKind::Media:
            label = tr("Media Themes");
            break;
        case theme::ThemeKind::Bg:
            label = tr("Backgrounds");
            break;
        }
        out.push_back(QVariantMap{{QStringLiteral("kind"), QString::fromUtf8(theme::to_string(kind))},
                                  {QStringLiteral("label"), label}});
    }
    return out;
}

QVariantList ThemeController::defaultSlots(const QString& kind) const
{
    const auto parsed = theme::parse_theme_kind(kind.toStdString());
    if (!parsed) {
        return {};
    }
    QVariantList out;
    for (const auto& slot : theme::pick_slots(theme::CategorySet::all())) {
        // A background can be any category's that takes one; a text theme
        // only its own category's.
        const bool fits = *parsed == theme::ThemeKind::Bg
                              ? slot.field == theme::ThemeField::Bg
                              : slot.field == theme::ThemeField::Theme
                                    && theme::text_kind_of(slot.category) == *parsed;
        if (fits) {
            // A text theme's one chip says what it does; a background's say
            // whose default it is.
            out.push_back(slot_map(slot, *parsed == theme::ThemeKind::Bg
                                             ? category_label(slot.category)
                                             : ThemeController::tr("Default")));
        }
    }
    return out;
}

qint64 ThemeController::upload_asset(const QString& output_name,
                                   const QString& theme_id,
                                   const QString& slot,
                                   const QString& local_path,
                                   bool draft)
{
    const qint64 id = ++last_upload_id_;
    // Reported like an upload that ran, once the caller holds the id.
    const auto refuse = [this, id](const QString& error) {
        QMetaObject::invokeMethod(
            this, [this, id, error] { emit assetUploadFinished(id, error); },
            Qt::QueuedConnection);
        return id;
    };
    try {
        auto* oc = find_output_mutable(output_name);
        if (oc == nullptr) {
            return refuse(refusal::output_gone());
        }
        if (theme::find_theme(*oc, theme::ThemeKind::Bg, theme_id.toStdString()) == nullptr) {
            return refuse(refusal::theme_gone());
        }
        if (!theme::is_theme_asset_slot(slot.toStdString())) {
            return refuse(refusal::unknown_asset_slot());
        }
        const auto media_dir = library_.media_root();
        if (media_dir.empty()) {
            return refuse(refusal::no_library());
        }
        ImportBatch batch;
        batch.id = id;
        batch.dir = theme::theme_assets_dir(media_dir);
        ImportFile file;
        file.source = local_path_from_qml(local_path);
        file.display = file.source.filename().string();
        batch.files.push_back(std::move(file));
        asset_copier_.start(
            std::move(batch),
            [this, output_name, theme_id = theme_id.toStdString(), slot = slot.toStdString(),
             draft](ImportBatch done) {
                if (dropped_drafts_.erase(done.id) > 0) {
                    std::error_code ec;
                    std::filesystem::remove(done.files.front().staged, ec);
                    emit assetUploadFinished(
                        done.id, asset_not_imported(done.files.front(), import_stopped()));
                    return;
                }
                QString error;
                std::string drafted;
                try {
                    error = commit_asset(output_name, theme_id, slot, {}, done,
                                         draft ? &drafted : nullptr);
                } catch (const std::exception& ex) {
                    error = report_entry_error("uploadAsset", ex);
                }
                if (error.isEmpty() && draft) {
                    emit assetDrafted(done.id, QString::fromStdString(drafted));
                }
                emit assetUploadFinished(done.id, error);
            });
        return id;
    } catch (const std::exception& ex) {
        return refuse(report_entry_error(__func__, ex));
    }
}

void ThemeController::copy_shared_assets(const QString& output_name,
                                         const std::string& theme_id,
                                         const std::vector<theme::PendingAssetCopy>& copies)
{
    const auto media_dir = library_.media_root();
    if (copies.empty() || media_dir.empty()) {
        return;
    }
    for (const auto& copy : copies) {
        ImportBatch batch;
        batch.dir = theme::theme_assets_dir(media_dir);
        ImportFile file;
        file.source = copy.source;
        file.display = copy.source.filename().string();
        batch.files.push_back(std::move(file));
        asset_copier_.start(std::move(batch), [this, output_name, theme_id, copy](ImportBatch done) {
            // A copy that fails leaves the theme on the shared file, as it was.
            try {
                (void)commit_asset(output_name, theme_id, copy.slot, copy.url, done);
            } catch (const std::exception& ex) {
                emit errorOccurred(report_entry_error("copy_shared_assets", ex));
            }
        });
    }
}

QString ThemeController::commit_asset(const QString& output_name,
                                      const std::string& theme_id,
                                      const std::string& slot,
                                      const std::string& expected_url,
                                      const ImportBatch& batch,
                                      std::string* drafted)
{
    // Theme uploads and shared-file copies are one file per batch.
    const auto& file = batch.files.front();
    // Whatever happens below, the staged file does not outlive this call.
    struct DiscardStaged {
        std::filesystem::path path;
        ~DiscardStaged()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } staged{file.staged};
    if (!file.error.empty()) {
        return asset_not_imported(file, file.error);
    }
    // The library the copy was for is no longer the open one.
    const auto media_dir = library_.media_root();
    if (media_dir.empty() || theme::theme_assets_dir(media_dir) != batch.dir) {
        return asset_not_imported(file, import_stopped());
    }
    auto* oc = find_output_mutable(output_name);
    auto* theme =
        oc != nullptr ? theme::find_theme(*oc, theme::ThemeKind::Bg, theme_id) : nullptr;
    if (theme == nullptr) {
        return refusal::theme_gone();
    }
    if (!expected_url.empty() && theme::theme_asset_url(*theme, slot) != expected_url) {
        // The slot moved on while the file copied; this copy is not needed.
        return {};
    }

    const auto filename =
        theme::new_owned_asset_name(theme_id, slot, file.source.extension().string());
    const auto owned = batch.dir / filename;
    std::error_code ec;
    std::filesystem::rename(file.staged, owned, ec);
    if (ec) {
        return asset_not_imported(file, import_not_copied());
    }
    staged.path.clear();
    if (drafted != nullptr) {
        *drafted = std::string(theme::k_theme_bg_url_prefix) + filename;
        return {};
    }
    auto before = *oc;
    const auto result = theme::set_owned_theme_asset(*theme, slot, filename);
    if (!result.ok) {
        std::filesystem::remove(owned, ec);
        return QString::fromStdString(result.message);
    }
    // Not saved: the theme keeps the background it had, and the new file goes.
    if (auto why = save_or_put_back(*oc, std::move(before)); !why.isEmpty()) {
        std::filesystem::remove(owned, ec);
        return why;
    }
    // Saved with the new one: the slot's others can go.
    theme::prune_unused_theme_assets(*theme, media_dir, slot);
    emit outputsEdited();
    library_changed();
    return {};
}

void ThemeController::prune_assets(const QString& output_name, const QString& theme_id)
try {
    const auto* oc = find_output_mutable(output_name);
    const auto* theme =
        oc != nullptr ? theme::find_theme(*oc, theme::ThemeKind::Bg, theme_id.toStdString()) : nullptr;
    if (theme != nullptr) {
        theme::prune_unused_theme_assets(*theme, library_.media_root());
    }
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

void ThemeController::drop_draft_upload(qint64 request_id)
{
    if (request_id > 0 && request_id <= last_upload_id_) {
        dropped_drafts_.insert(request_id);
    }
}

QStringList ThemeController::normalizeTags(const QVariant& raw) const
{
    QStringList out;
    for (const auto& tag : theme::normalize_theme_tags(to_json(raw))) {
        out.push_back(QString::fromStdString(tag));
    }
    return out;
}

QVariantMap ThemeController::style_settings(const nlohmann::json& style) const
{
    const auto b = theme::read_theme_behavior(style);
    const auto bg = theme::read_theme_background(style);
    // Each content context's transition, by the context's name.
    QVariantMap transitions;
    for (const auto ctx : theme::k_content_contexts) {
        const auto& spec = b.transition(ctx);
        transitions.insert(QString::fromUtf8(theme::to_string(ctx)),
                           QVariantMap{{QStringLiteral("type"),
                                        QString::fromUtf8(model::to_string(spec.type))},
                                       {QStringLiteral("ms"), spec.duration_ms},
                                       {QStringLiteral("direction"),
                                        QString::fromUtf8(model::to_string(spec.direction))}});
    }
    return {
        {QStringLiteral("transitions"), transitions},
        {QStringLiteral("balance_wrapped_lines"), b.balance_wrapped_lines},
        {QStringLiteral("balance_wrapped_strength"), b.balance_wrapped_strength},
        {QStringLiteral("bible_fluid_verses"), b.bible_fluid_verses},
        {QStringLiteral("fluid_slides"), b.fluid_slides},
        {QStringLiteral("follow_lines"), b.follow_lines},
        {QStringLiteral("prevent_mixed_active"), b.prevent_mixed_active},
        {QStringLiteral("show_title_slides"), b.show_title_slides},
        {QStringLiteral("title_slide_show_lines"), b.title_slide_show_lines},
        {QStringLiteral("verse_gap"), b.verse_gap},
        {QStringLiteral("background_type"), QString::fromUtf8(layout::to_string(bg.type))},
        {QStringLiteral("background_color"), QString::fromUtf8(bg.color)},
        {QStringLiteral("background_image"), QString::fromUtf8(bg.image)},
        {QStringLiteral("background_video"), QString::fromUtf8(bg.video)},
        {QStringLiteral("bg_enable_fade"), bg.enable_fade},
        {QStringLiteral("bg_fade_duration"), bg.fade_duration},
    };
}

void ThemeController::update_draft_preview(const QString& output_name,
                                           const QString& kind,
                                           const QString& context,
                                           nlohmann::json style,
                                           const QString& suppress_id)
try {
    const auto* oc = find_output(output_name);
    const geom::CanvasSize canvas = oc != nullptr ? model::output_canvas(*oc) : geom::k_reference_canvas;
    const int cw = canvas.width;
    const int ch = canvas.height;
    nlohmann::json draft = std::move(style);
    if (!draft.is_object()) {
        draft = nlohmann::json::object();
    }
    apply_designer_elements(draft);

    const scene::DraftContext ctx = draft_context_for(kind, context);

    // A text draft previews on transparency, without a background surface. A
    // bg draft previews its own fill only: the body and every chrome element
    // (clock included) belong to the partner text theme.
    if (kind != QLatin1String("bg")) {
        draft = scene::isolate_text_preview_style(std::move(draft));
    }

    // The designer sends its draft in the output's pixels.
    theme::set_elements_canvas(draft, geom::CanvasSize{cw, ch});

    const std::string output = oc != nullptr ? oc->name : std::string("preview");
    nlohmann::json inputs = {{"draft", draft},       {"context", static_cast<int>(ctx)},
                             {"w", cw},              {"h", ch},
                             {"output", output},     {"suppress", suppress_id.toStdString()},
                             {"revision", previews_.theme_revision()}};
    if (inputs == draft_inputs_) {
        return;
    }
    draft_inputs_ = std::move(inputs);
    draft_canvas_ = canvas;
    draft_frame_ = scene::build_theme_draft_frame(
        draft, ctx, cw, ch, output, &previews_.measurer(), true, suppress_id.toStdString());
    ++draft_revision_;
    emit draftChanged();
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

QImage ThemeController::render_draft_preview(const QSize& size) const
{
    return finish_preview(prepare_draft_preview(size));
}

PreviewJob ThemeController::prepare_draft_preview(const QSize& size) const
{
    // The whole design, the sample body and stand-ins included, each at its
    // own place in the stack: the stage draws only chrome over it.
    const QSize want = size_or(
        size, QSize(640, std::max(1, 640 * draft_canvas_.height / std::max(1, draft_canvas_.width))));
    return {.still = still_of(draft_frame_, want, false, previews_)};
}

QImage ThemeController::render_preview(const QString& id, const QSize& size) const
{
    return finish_preview(prepare_preview(id, size));
}

PreviewJob ThemeController::prepare_preview(const QString& id, const QSize& size) const
{
    QString path = id;
    QString query;
    const int qi = path.indexOf(QLatin1Char('?'));
    if (qi >= 0) {
        query = path.mid(qi + 1);
        path = path.left(qi);
    }

    if (path.isEmpty() || path == QLatin1String("draft")) {
        return prepare_draft_preview(size);
    }
    if (path == QLatin1String("grid")) {
        QUrlQuery gq(query);
        const int cell = gq.queryItemValue(QStringLiteral("cell")).toInt();
        return {.image = paint_theme_preview_grid(size, cell)};
    }

    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 4 || parts.at(0) != QLatin1String("theme")) {
        return {};
    }

    const QString output_name = QUrl::fromPercentEncoding(parts.at(1).toUtf8());
    const QString kind = parts.at(2);
    const QString theme_id = QUrl::fromPercentEncoding(parts.at(3).toUtf8());

    QUrlQuery q(query);
    const scene::DraftContext context = draft_context_for(kind, q.queryItemValue(QStringLiteral("c")));

    const auto* oc = find_output(output_name);
    const auto parsed = theme::parse_theme_kind(kind.toStdString());
    if (oc == nullptr || !parsed) {
        return {};
    }
    const auto* theme = theme::find_theme(*oc, *parsed, theme_id.toStdString());
    if (theme == nullptr) {
        return {};
    }

    const auto [cw, ch] = model::output_canvas(*oc);
    const QSize want = size_or(size, QSize(320, std::max(1, 320 * ch / std::max(1, cw))));

    const QString cache_key =
        output_name + QLatin1Char('\n') + kind + QLatin1Char('\n') + theme_id + QLatin1Char('\n')
        + QString::number(static_cast<int>(context)) + QLatin1Char('\n') + QString::number(cw)
        + QLatin1Char('x')
        + QString::number(ch) + QLatin1Char('@') + QString::number(want.width())
        + QLatin1Char('x') + QString::number(want.height());
    if (QImage hit = preview_cache_.get(cache_key); !hit.isNull()) {
        return {.image = std::move(hit)};
    }

    nlohmann::json draft = theme->style.is_object() ? theme->style : nlohmann::json::object();
    if (*parsed != theme::ThemeKind::Bg) {
        draft = scene::isolate_text_preview_style(std::move(draft));
    }
    // Bg thumbs stay surface-only - this same image is the Freeform editor's
    // canvas backdrop, and any chrome baked in here ghosts under the
    // document's own objects.

    // With a media folder a small thumb is painted at its tier and kept on disk
    // under what it shows, with pictures of its own (see theme_thumb_pictures).
    const auto& root = library_.media_root();
    const std::optional<QSize> stored_size =
        root.empty() ? std::nullopt : theme_thumb_stored_size(want);
    const QSize paint_size = stored_size.value_or(want);
    const auto pictures =
        stored_size ? theme_thumb_pictures(root, paint_size) : nullptr;
    const auto draft_shared = std::make_shared<const nlohmann::json>(std::move(draft));
    const int generation = preview_cache_.generation();

    // Laid out and painted by whoever finishes the job, from copies.
    PreviewJob job{
        .lay_out = [draft = draft_shared, context, width = cw, height = ch, name = oc->name,
                    paint_size, pictures,
                    previews = &previews_]() -> std::optional<render::StillPaint> {
            render::StillPaint still = still_of(
                scene::build_theme_draft_frame(*draft, context, width, height, name,
                                               &previews->measurer()),
                paint_size, true, *previews);
            still.now = theme_thumb_clock_time();
            if (pictures) {
                still.cache = pictures.get();
            }
            return still;
        },
        .then = [cache = &preview_cache_, cache_key, want, generation](QImage img) {
            img = fit_theme_thumb(img, want);
            cache->put_if_current(cache_key, img, generation);
            return img;
        },
    };
    if (stored_size) {
        // On the finishing thread, since it reads files. A clear means another
        // library or a changed theme: what was painted is not stored.
        job.store = [draft = draft_shared, context, width = cw, height = ch, name = oc->name, root,
                     want, paint_size, cache = &preview_cache_,
                     generation](const std::function<QImage()>& paint) {
            const auto thumb_path = theme_thumb_path(
                root,
                theme_thumb_identity(
                    *draft, ThemeThumbView{static_cast<int>(context), width, height, name, paint_size},
                    root),
                paint_size);
            if (QImage kept = load_theme_thumb(thumb_path, want); !kept.isNull()) {
                return kept;
            }
            QImage painted = paint();
            if (!painted.isNull() && cache->generation() == generation) {
                (void)write_thumb_jpeg(painted, std::max(painted.width(), painted.height()), thumb_path);
            }
            return painted;
        };
    }
    return job;
}

} // namespace seventhslide::app
