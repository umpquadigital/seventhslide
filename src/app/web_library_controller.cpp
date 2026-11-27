#include "web_library_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "rundown.hpp"
#include "media_thumb_cache.hpp"
#include "open_library.hpp"
#include "qvariant_json.hpp"

#include <seventhslide/db/search_text.hpp>
#include <seventhslide/model/content.hpp>
#include <seventhslide/model/web_page.hpp>

#include <nlohmann/json.hpp>

#include <QVariant>

#include <utility>
#include <vector>

namespace seventhslide::app {
namespace {

QVariantMap page_map(const model::WebPage& p)
{
    return {{QStringLiteral("id"), qml_id(p.id)},
            {QStringLiteral("name"), QString::fromStdString(p.name)},
            {QStringLiteral("url"), QString::fromStdString(p.url)},
            {QStringLiteral("width"), p.width},
            {QStringLiteral("height"), p.height},
            {QStringLiteral("zoom"), p.zoom}};
}

/// What savePage answers when it refuses: why, as its "error".
QVariantMap refused(const QString& why)
{
    return {{QStringLiteral("error"), why}};
}

} // namespace

WebLibraryController::WebLibraryController(session::Session& session, IRundown& rundown,
                                           const OpenLibrary& library, QObject* parent)
    : FolderLibrary(parent)
    , session_(session)
    , rundown_(rundown)
    , library_(library)
    , thumbs_([this]() -> std::filesystem::path {
        const auto& root = library_.media_root();
        return root.empty() || !repo_ ? std::filesystem::path() : media_thumbs_dir(root) / "web";
    })
{
    // One rebuild for however many thumbnails land before it runs.
    connect(&thumbs_, &WebThumbs::made, this, [this] {
        if (thumbs_posted_) {
            return;
        }
        thumbs_posted_ = true;
        QMetaObject::invokeMethod(
            this,
            [this] {
                thumbs_posted_ = false;
                rebuild_browser();
            },
            Qt::QueuedConnection);
    });
}

WebLibraryController::~WebLibraryController()
{
    // Renderers go before the session and library they were made for.
    thumbs_.stop();
}

void WebLibraryController::set_renderer_factory(const WebRendererFactory& factory)
{
    thumbs_.set_factory(factory);
}

void WebLibraryController::attach(db::Database& db)
{
    repo_ = std::make_unique<db::WebPageRepository>(db);
    folders_.attach(repo_->folders());
    // Thumbnails of pages since deleted, here or on another computer.
    thumbs_.prune(repo_->all_items());
    rebuild_browser();
    emit changed();
}

void WebLibraryController::detach()
{
    thumbs_.stop();
    repo_.reset();
    folders_.detach();
    browser_model_.set_entries({});
    emit changed();
}

void WebLibraryController::rebuild_browser()
{
    // Not every caller announces a change, and folderId moves with this.
    if (folders_.drop_missing()) {
        emit changed();
    }
    if (!repo_) {
        browser_model_.set_entries({});
        return;
    }
    const db::SearchQuery query(folders_.query());
    const auto matches = [&](const std::string& text) {
        return query.matches(std::string_view(text));
    };
    std::vector<model::Folder> folders = folders_.children();
    std::erase_if(folders, [&](const model::Folder& f) { return !matches(f.name); });
    auto entries = folders_.folder_entries(folders, repo_->folder_item_counts());
    std::vector<model::WebPage> unthumbed;
    for (const auto& page : repo_->list_items_in_folder(folders_.folder())) {
        if (!page.id) {
            continue;
        }
        // A search finds a page by its address as well as its name.
        if (!matches(page.name) && !matches(page.url)) {
            continue;
        }
        MediaBrowserEntry e;
        e.kind = MediaBrowserEntry::Kind::File;
        e.id = *page.id;
        e.name = page.name;
        e.thumb_url = thumbs_.url_for(page).toStdString();
        if (e.thumb_url.empty()) {
            unthumbed.push_back(page);
        }
        entries.push_back(std::move(e));
    }
    browser_model_.set_entries(std::move(entries), folders_.view());
    thumbs_.want(unthumbed);
}

QVariantMap WebLibraryController::page(qint64 page_id)
try {
    if (!repo_ || page_id < 0) {
        return {};
    }
    const auto p = repo_->get_item(page_id);
    return p ? page_map(*p) : QVariantMap{};
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return {};
}

QString WebLibraryController::urlFromText(const QString& text) const
{
    const auto url = model::web_url_from_text(text.toStdString());
    return url ? QString::fromStdString(*url) : QString();
}

QVariantMap WebLibraryController::savePage(qint64 page_id, const QVariantMap& fields)
try {
    if (!repo_) {
        return refused(refusal::no_library());
    }
    const auto url = model::web_url_from_text(fields.value(QStringLiteral("url")).toString().toStdString());
    if (!url) {
        return refused(tr("That is not a web address a page can show."));
    }
    model::WebPage page;
    if (page_id >= 0) {
        const auto stored = repo_->get_item(page_id);
        if (!stored) {
            return refused(refusal::web_page_gone());
        }
        page = *stored;
    } else {
        page.folder_id = folders_.folder();
    }
    page.name = fields.value(QStringLiteral("name")).toString().toStdString();
    page.url = *url;
    page.width = fields.value(QStringLiteral("width"), page.width).toInt();
    page.height = fields.value(QStringLiteral("height"), page.height).toInt();
    page.zoom = fields.value(QStringLiteral("zoom"), page.zoom).toDouble();
    if (page_id >= 0) {
        (void)repo_->update_item(page);
    } else {
        (void)repo_->create_item(page);
    }
    rebuild_browser();
    return {{QStringLiteral("id"), qml_id(page.id)}};
} catch (const std::exception& ex) {
    return refused(app::report_entry_error(__func__, ex));
}

bool WebLibraryController::playPage(qint64 page_id, bool live)
try {
    if (!repo_ || page_id < 0) {
        return false;
    }
    const auto page = repo_->get_item(page_id);
    if (!page) {
        return false;
    }
    session::LiveMediaContent media;
    media.kind = model::ContentKind::Web;
    media.data = model::web_item_data(*page);
    session_.play_media(std::move(media), {},
                        live ? session::Land::Live : session::Land::Cue);
    emit liveChanged(true);
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool WebLibraryController::addPageToService(qint64 page_id, int at_index)
try {
    if (!repo_ || page_id < 0) {
        return false;
    }
    const auto page = repo_->get_item(page_id);
    if (!page) {
        return false;
    }
    return rundown_.append_service_item(model::k_item_type_web, model::web_item_data(*page),
                                        std::nullopt, optional_index(at_index));
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

bool WebLibraryController::addFolderToService(qint64 folder_id, int at_index)
try {
    if (!repo_ || folder_id < 0) {
        return false;
    }
    std::vector<nlohmann::json> items;
    for (const auto& page : repo_->list_items_in_folder(folder_id)) {
        items.push_back(model::web_item_data(page));
    }
    if (items.empty()) {
        return false;
    }
    return rundown_.append_service_items(model::k_item_type_web, items, optional_index(at_index));
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return false;
}

qint64 WebLibraryController::duplicateItem(qint64 page_id)
try {
    if (!repo_ || page_id < 0) {
        return -1;
    }
    auto copy = repo_->get_item(page_id);
    if (!copy) {
        return -1;
    }
    copy->id.reset();
    copy->name += " copy";
    const auto id = repo_->create_item(*copy);
    rebuild_browser();
    return static_cast<qint64>(id);
} catch (const std::exception& ex) {
    emit errorOccurred(app::report_entry_error(__func__, ex));
    return -1;
}

QString WebLibraryController::deleteItem(qint64 page_id)
try {
    if (!repo_) {
        return refusal::no_library();
    }
    if (page_id < 0 || !repo_->remove_item(page_id)) {
        return refusal::web_page_gone();
    }
    rebuild_browser();
    return {};
} catch (const std::exception& ex) {
    return app::report_entry_error(__func__, ex);
}

QString WebLibraryController::delete_folder(std::int64_t folder_id)
{
    (void)repo_->delete_folder_tree(folder_id);
    rebuild_browser();
    return {};
}

QString WebLibraryController::move_items(const QVariantList& items,
                                         std::optional<std::int64_t> folder)
{
    const auto ids = parse_id_list(items);
    if (!ids) {
        return refusal::items_gone();
    }
    (void)repo_->move_items(*ids, folder);
    return {};
}

bool WebLibraryController::reorder_items(std::optional<std::int64_t> folder,
                                         const std::vector<std::int64_t>& ids)
{
    repo_->reorder_items(folder, ids);
    return true;
}

} // namespace seventhslide::app
