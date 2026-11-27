#pragma once

// The Browser tab's web pages: the stored pages, the folder tree they live
// in, and the browser model QML binds to.
//
// SessionFacade owns one and exposes it as `session.webLibrary`. Its folder
// calls are every library's (FolderLibrary); its pages are keyed by id.

#include "folder_library.hpp"
#include "library_models.hpp"
#include "qml_ids.hpp"
#include "web_renderer.hpp"
#include "web_thumbs.hpp"

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/web_page_repository.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/model/web_page.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace seventhslide::app {

class IRundown;
class OpenLibrary;

class WebLibraryController : public FolderLibrary {
    Q_OBJECT
    QML_ANONYMOUS

    /// The least and most a page's width or height may be.
    Q_PROPERTY(int minSide READ minSide CONSTANT)
    Q_PROPERTY(int maxSide READ maxSide CONSTANT)
    /// More pixels than this (4K) and the page dialog warns of the cost.
    Q_PROPERTY(int heavyPixels READ heavyPixels CONSTANT)

public:
    /// Pages go live on ``session`` and into ``rundown``'s open service;
    /// their thumbnails are kept in ``library``'s thumbs folder.
    WebLibraryController(session::Session& session, IRundown& rundown,
                         const OpenLibrary& library, QObject* parent = nullptr);
    ~WebLibraryController() override;

    /// How pages are drawn for their thumbnails.
    void set_renderer_factory(const WebRendererFactory& factory);
    [[nodiscard]] WebThumbs& thumbs() noexcept { return thumbs_; }

    void attach(db::Database& db);
    void detach();

    [[nodiscard]] db::WebPageRepository* repo() noexcept { return repo_.get(); }
    [[nodiscard]] static int minSide() noexcept { return model::k_web_min_side; }
    [[nodiscard]] static int maxSide() noexcept { return model::k_web_max_side; }
    [[nodiscard]] static int heavyPixels() noexcept { return model::k_web_heavy_pixels; }

    // --- pages ---
    /// A page as ``{id, name, url, width, height, zoom}``; empty when gone.
    Q_INVOKABLE QVariantMap page(qint64 page_id);
    /// Save ``fields`` (``name``, ``url``, ``width``, ``height``, ``zoom``) as
    /// a new page in the folder showing, or over page ``page_id`` (>= 0).
    /// Returns ``{id}``, or ``{error}`` saying why not.
    Q_INVOKABLE QVariantMap savePage(qint64 page_id, const QVariantMap& fields);
    /// The URL ``text`` stands for, as it would be saved; empty when it is not
    /// one a page can show.
    Q_INVOKABLE QString urlFromText(const QString& text) const;
    /// Show page ``page_id`` on the stack web pages play on: cued in its
    /// Preview when it has one, unless ``live``.
    Q_INVOKABLE bool playPage(qint64 page_id, bool live = false);
    /// Add page ``page_id`` to the open service, at ``at_index`` (< 0: the end).
    Q_INVOKABLE bool addPageToService(qint64 page_id, int at_index = -1);
    /// Add each page of folder ``folder_id`` to the open service, in order.
    Q_INVOKABLE bool addFolderToService(qint64 folder_id, int at_index = -1);
    Q_INVOKABLE qint64 duplicateItem(qint64 page_id);
    Q_INVOKABLE QString deleteItem(qint64 page_id);

    void rebuild_browser();

signals:
    /// A page went live (see LibraryController::liveChanged).
    void liveChanged(bool rebuildModels);

protected:
    // --- FolderLibrary: pages keyed by id ---
    [[nodiscard]] bool attached() const override { return repo_ != nullptr; }
    void rebuild() override { rebuild_browser(); }
    QString delete_folder(std::int64_t folder_id) override;
    QString move_items(const QVariantList& items, std::optional<std::int64_t> folder) override;
    bool reorder_items(std::optional<std::int64_t> folder,
                       const std::vector<std::int64_t>& ids) override;

private:
    session::Session& session_;
    IRundown& rundown_;
    const OpenLibrary& library_;
    std::unique_ptr<db::WebPageRepository> repo_;
    WebThumbs thumbs_;
    /// A rebuild for new thumbnails is queued.
    bool thumbs_posted_ = false;
};

} // namespace seventhslide::app
