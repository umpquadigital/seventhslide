// The Freeform document library.
//
// Owns the stored items, their folder tree and the browser model, and stores
// the documents the Freeform editor opens (see the header).

#include "freeform_library_controller.hpp"

#include "rundown.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"


#include "qvariant_json.hpp"
#include "freeform_slide_render.hpp"
#include "freeform_thumb_cache.hpp"
#include "media_thumb_cache.hpp"
#include "qml_ids.hpp"
#include "requested_size.hpp"

#include <seventhslide/db/search_text.hpp>
#include <seventhslide/freeform/compile.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/model/output_library.hpp>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QVariantList>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::app {

namespace {

/// Stamp / URL identity so a canvas-aspect change invalidates square or stale thumbs.
[[nodiscard]] std::string library_thumb_identity(std::string_view preview_key, int canvas_w,
                                                 int canvas_h)
{
    return std::string(preview_key) + "@" + std::to_string(canvas_w) + "x"
           + std::to_string(canvas_h);
}

/// The outputs' themes, named for a thumbnail stamp: unlike
/// PreviewResources::theme_revision, which counts edits in this run, it is the
/// same text in every run while they are unchanged. It covers each output's
/// whole saved config (themes, defaults, canvas) and the theme priority.
[[nodiscard]] std::string theme_identity(const session::Session& session)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const auto& oc : session.outputs()) {
        hash.addData(QByteArray::fromStdString(oc.to_persist_dict().dump()));
    }
    for (const auto& name : session.theme_priority()) {
        hash.addData(QByteArray::fromStdString(name + '\n'));
    }
    return hash.result().toHex().left(16).toStdString();
}

/// What a tile drawn without one of its pictures is stamped with in place of
/// the theme identity: a stamp no URL matches, so it is drawn again when next
/// asked for, and is kept once nothing is missing.
constexpr std::string_view k_drawn_without = "+missing";

/// ``k`` is the item's stamp key and ``t`` the theme identity: the provider
/// checks the disk thumb's stamp against both before serving it. ``p`` asks
/// again once a picture arrives (``PreviewResources::pictures_revision``).
[[nodiscard]] QString browser_thumb_url(const std::vector<model::OutputConfig>& outputs,
                                        std::string_view themes, int pictures,
                                        std::int64_t freeform_id,
                                        std::string_view output_id, std::string_view preview_key)
{
    if (preview_key.empty() || freeform_id < 0) {
        return {};
    }
    const auto [cw, ch] = model::output_canvas(outputs, output_id);
    const auto key = library_thumb_identity(preview_key, cw, ch);
    const auto encode = [](std::string_view s) {
        return QString::fromUtf8(QUrl::toPercentEncoding(QString::fromUtf8(s)));
    };
    // In one pass: a second arg() would take the "%40" an encoded "@" leaves
    // for a place marker.
    return QStringLiteral("image://freeformthumb/%1?k=%2&t=%3&p=%4")
        .arg(QString::number(freeform_id), encode(key), encode(themes), QString::number(pictures));
}

/// ``box`` is a bounding box; the image size always matches the output canvas aspect.
[[nodiscard]] QSize fit_freeform_thumb_size(int canvas_w, int canvas_h, const QSize& box)
{
    const QSize bound = size_or(box, QSize(k_media_thumb_tier_lg, k_media_thumb_tier_lg));
    QSize want = QSize(std::max(1, canvas_w), std::max(1, canvas_h))
                     .scaled(bound, Qt::KeepAspectRatio);
    if (want.width() < 1) {
        want.setWidth(1);
    }
    if (want.height() < 1) {
        want.setHeight(1);
    }
    const int max_edge = std::max(want.width(), want.height());
    if (max_edge > k_media_thumb_tier_lg) {
        want = want.scaled(k_media_thumb_tier_lg, k_media_thumb_tier_lg, Qt::KeepAspectRatio);
    }
    return want;
}

/// Slide ``slide_index`` of ``doc`` as ``oc`` paints it, fitted into ``box``,
/// ready to paint; nothing when it has no slides. Lays out that output only,
/// from what it is given: any thread.
[[nodiscard]] std::optional<render::StillPaint> freeform_document_still(
    const freeform::Document& doc,
    const model::OutputConfig& oc,
    const std::vector<std::string>& theme_priority,
    int slide_index,
    const QSize& box,
    PreviewResources& previews)
{
    const auto build = build_freeform_slides(doc, {oc}, theme_priority, previews);
    if (build.outputs.empty()) {
        return std::nullopt;
    }
    const build::OutputRuntime& rt = build.outputs.front();
    if (rt.slide_count() <= 0) {
        return std::nullopt;
    }
    const int resolved = slide_index >= 0 && slide_index < rt.slide_count() ? slide_index : 0;
    const auto canvas = model::output_canvas(oc);
    return freeform_slide_still(rt, canvas, resolved,
                                fit_freeform_thumb_size(canvas.width, canvas.height, box),
                                previews);
}

}  // namespace

FreeformLibraryController::FreeformLibraryController(OpenLibrary& library,
                                                     session::Session& session,
                                                     PreviewResources& previews,
                                                     IRundown& rundown,
                                                     QObject* parent)
    : FolderLibrary(parent)
    , library_(library)
    , session_(session)
    , previews_(previews)
    , rundown_(rundown)
{
}

std::optional<freeform::Document> FreeformLibraryController::load_freeform_document(std::int64_t id)
{
    if (!repo_ || id < 0) {
        return std::nullopt;
    }
    const auto item = repo_->get_item(id);
    if (!item) {
        return std::nullopt;
    }
    auto doc = freeform::load_document(item->document, item->name);
    if (!doc) {
        // Opening and playing both come here; say why nothing happens.
        emit errorOccurred(tr("Cannot open \"%1\". %2")
                               .arg(QString::fromStdString(item->name),
                                    QString::fromStdString(
                                        freeform::document_refusal(item->document))));
    }
    return doc;
}

QString FreeformLibraryController::commit_freeform_document(std::int64_t id,
                                                            const freeform::Document& doc)
try {
    if (!repo_) {
        return refusal::no_library();
    }
    if (id < 0) {
        return refusal::freeform_gone();
    }
    auto item = repo_->get_item(id);
    if (!item) {
        return refusal::freeform_gone();
    }
    item->name = doc.name;
    item->document = doc.to_json();
    repo_->update_item(*item);
    rebuild_freeform_browser();
    // Stacks playing this library item show the saved version in place. The
    // saved item is never sent anywhere it was not already live, and rundown
    // rows keep their own snapshot.
    if (session_.refresh_freeform(live_content(id, doc))) {
        emit liveChanged(true);
    }
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

std::optional<session::LiveFreeformContent> FreeformLibraryController::live_content(std::int64_t id)
{
    auto document = load_freeform_document(id);
    if (!document) {
        return std::nullopt;
    }
    return live_content(id, std::move(*document));
}

session::LiveFreeformContent FreeformLibraryController::live_content(std::int64_t id,
                                                                     freeform::Document document)
{
    if (document.name.empty()) {
        document.name = "Freeform";
    }
    session::LiveFreeformContent live;
    live.name = document.name;
    live.document = std::move(document);
    live.library_id = id;
    return live;
}

qint64 FreeformLibraryController::add_document(freeform::Document doc)
{
    if (!repo_) {
        return -1;
    }
    model::FreeformItem item;
    item.name = doc.name.empty() ? "Untitled" : doc.name;
    freeform::normalize(doc);
    item.document = doc.to_json();
    const auto id = repo_->create_item(item);
    rebuild_freeform_browser();
    return static_cast<qint64>(id);
}

void FreeformLibraryController::attach(db::Database& db)
{
    repo_ = std::make_unique<db::FreeformRepository>(db);
    folders_.attach(repo_->folders());
}

void FreeformLibraryController::detach()
{
    repo_.reset();
    folders_.detach();
    browser_model_.set_entries({});
    emit changed();
}

qint64 FreeformLibraryController::createFreeform(const QString& name)
try {
    if (!repo_) {
        return -1;
    }
    std::vector<std::string> outputs;
    for (const auto& oc : session_.outputs()) {
        outputs.push_back(oc.id);
    }
    const auto trimmed = name.trimmed();
    model::FreeformItem item;
    item.name = trimmed.isEmpty() ? "Untitled" : trimmed.toStdString();
    item.folder_id = folders_.folder();
    auto doc = freeform::make_document(item.name, outputs);
    freeform::normalize(doc);
    item.document = doc.to_json();
    const auto id = repo_->create_item(item);
    rebuild_freeform_browser();
    return static_cast<qint64>(id);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return -1;
}

qint64 FreeformLibraryController::duplicateItem(qint64 freeform_id)
try {
    if (!repo_ || freeform_id < 0) {
        return -1;
    }
    auto src = repo_->get_item(freeform_id);
    if (!src) {
        return -1;
    }
    model::FreeformItem copy = *src;
    copy.id.reset();
    if (copy.name.empty()) {
        copy.name = "Untitled copy";
    } else {
        copy.name += " copy";
    }
    const auto id = repo_->create_item(copy);
    rebuild_freeform_browser();
    return static_cast<qint64>(id);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return -1;
}

QString FreeformLibraryController::deleteItem(qint64 freeform_id)
try {
    if (!repo_) {
        return refusal::no_library();
    }
    if (freeform_id < 0) {
        return refusal::freeform_gone();
    }
    if (!repo_->remove_item(freeform_id)) {
        return refusal::freeform_gone();
    }
    remove_freeform_library_thumbs(freeform_id);
    rebuild_freeform_browser();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

void FreeformLibraryController::rebuild_freeform_browser()
{
    ++browser_builds_;
    // Not every caller announces a change, and folderId moves with this.
    if (folders_.drop_missing()) {
        emit changed();
    }
    if (!repo_) {
        browser_model_.set_entries({});
        return;
    }

    const db::SearchQuery query(folders_.query());
    const auto name_matches = [&](const std::string& name) {
        return query.matches(std::string_view(name));
    };

    std::vector<model::Folder> folders = folders_.children();
    std::erase_if(folders, [&](const model::Folder& f) { return !name_matches(f.name); });
    auto entries = folders_.folder_entries(folders, repo_->folder_item_counts());

    const auto themes = theme_identity(session_);
    for (const auto& item : repo_->list_browser_rows(folders_.folder())) {
        if (!name_matches(item.name)) {
            continue;
        }
        MediaBrowserEntry e;
        e.kind = MediaBrowserEntry::Kind::File;
        e.id = item.id;
        e.name = item.name;
        // The URL only. The image provider renders the thumb when the browser
        // asks for it (see render_freeform_library_thumb).
        e.thumb_url = browser_thumb_url(session_.outputs(), themes, previews_.pictures_revision(), item.id,
                                        item.thumb_output_id, item.thumb_key)
                          .toStdString();
        entries.push_back(std::move(e));
    }

    browser_model_.set_entries(std::move(entries), folders_.view());
}

QString FreeformLibraryController::delete_folder(std::int64_t folder_id)
{
    for (const auto id : repo_->delete_folder_tree(folder_id)) {
        remove_freeform_library_thumbs(id);
    }
    rebuild_freeform_browser();
    return {};
}

QString FreeformLibraryController::move_items(const QVariantList& items,
                                              std::optional<std::int64_t> folder)
{
    const auto ids = parse_id_list(items);
    if (!ids) {
        return refusal::items_gone();
    }
    (void)repo_->move_items(*ids, folder);
    return {};
}

bool FreeformLibraryController::reorder_items(std::optional<std::int64_t> folder,
                                              const std::vector<std::int64_t>& ids)
{
    repo_->reorder_items(folder, ids);
    return true;
}

bool FreeformLibraryController::addFreeformToService(qint64 freeform_id, int at_index)
try {
    if (!repo_ || freeform_id < 0) {
        return false;
    }
    const auto item = repo_->get_item(freeform_id);
    if (!item) {
        return false;
    }
    const auto idx = optional_index(at_index);
    return rundown_.append_service_item(model::k_item_type_freeform,
                                        model::freeform_item_data(*item),
                                        std::nullopt, idx);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool FreeformLibraryController::addFreeformFolderToService(qint64 folder_id, int at_index)
try {
    if (!repo_ || folder_id < 0) {
        return false;
    }
    std::vector<nlohmann::json> items;
    for (const auto& item :
         repo_->list_items_in_folder(folder_id)) {
        if (item.id) {
            items.push_back(model::freeform_item_data(item));
        }
    }
    return rundown_.append_service_items(model::k_item_type_freeform, items, optional_index(at_index));
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

void FreeformLibraryController::remove_freeform_library_thumbs(std::int64_t freeform_id)
{
    remove_freeform_thumbs(library_.media_root(), freeform_id);
}

QImage FreeformLibraryController::render_freeform_library_thumb(qint64 freeform_id, const QSize& size)
{
    return finish_preview(prepare_library_thumb(freeform_id, size));
}

PreviewJob FreeformLibraryController::prepare_library_thumb(qint64 freeform_id, const QSize& size)
{
    if (freeform_id < 0) {
        return {};
    }
    const std::int64_t id = freeform_id;
    const auto root = library_.media_root();
    const auto from_disk = [root, id, size](const QImage&) {
        return load_freeform_thumb_fitted(root, id, size);
    };

    // Rendering needs the library and the outputs, which only the GUI thread may
    // read (see gui_thread.hpp). Off-thread, serve whatever is on disk.
    const auto* app = QCoreApplication::instance();
    const bool can_render =
        repo_ != nullptr && (app == nullptr || QThread::currentThread() == app->thread());
    if (!can_render) {
        return {.then = from_disk};
    }

    const auto item = repo_->get_item(id);
    if (!item) {
        return {};
    }
    const auto loaded = freeform::load_document(item->document, item->name);
    if (!loaded) {
        return {};
    }
    const auto& doc = *loaded;

    // The stamp says whether the slide on disk still matches the document, the
    // output canvas and the theme library; a stale one is made again (below)
    // rather than on every library refresh.
    const auto ref = freeform::library_thumb_ref(doc);
    if (!ref) {
        return {.then = from_disk};
    }
    const auto [cw, ch] = model::output_canvas(session_.outputs(), ref->output_id);
    auto key = library_thumb_identity(ref->preview_key, cw, ch);
    auto themes = theme_identity(session_);
    if (freeform_thumb_fresh(root, id, key, themes)) {
        return {.then = from_disk};
    }
    // A deck for an output that is gone draws on the first output instead.
    const auto& outputs = session_.outputs();
    const auto* oc = model::find_output_by_id(outputs, ref->output_id);
    if (oc == nullptr && !outputs.empty()) {
        oc = &outputs.front();
    }
    if (oc == nullptr) {
        return {.then = from_disk};
    }
    // Laid out and painted by whoever finishes the job, from copies. Stored
    // only if nothing else has stored a tile since this document was read: a
    // save made while this one paints holds the newer picture. With no slide
    // to show, whatever is on disk is served. A picture missing meanwhile (one
    // still on its way from another computer) marks the tile to be drawn
    // again; another preview missing one at the same moment only costs that.
    const auto misses_before = std::make_shared<std::uint64_t>(0);
    return {
        .lay_out = [doc = *loaded, oc = *oc, priority = session_.theme_priority(),
                    previews = &previews_, misses_before] {
            if (const auto* cache = previews->image_cache()) {
                *misses_before = cache->misses();
            }
            // 512x512 is a bounding box; the JPEG keeps the output canvas aspect.
            return freeform_document_still(
                doc, oc, priority, 0, QSize(k_media_thumb_tier_lg, k_media_thumb_tier_lg),
                *previews);
        },
        .then = [root, id, size, key = std::move(key), themes = std::move(themes),
                 seen = freeform_thumb_stamp(root, id), previews = &previews_,
                 misses_before](QImage img) {
            if (!img.isNull()) {
                const auto* cache = previews->image_cache();
                const bool whole = cache == nullptr || cache->misses() == *misses_before;
                write_freeform_thumbs_unless_changed(
                    root, id, seen, key, whole ? themes : themes + std::string(k_drawn_without), img);
            }
            return load_freeform_thumb_fitted(root, id, size);
        },
    };
}

} // namespace seventhslide::app
