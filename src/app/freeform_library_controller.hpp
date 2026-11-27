#pragma once

// The Freeform document library.
//
// Owns the stored Freeform items, the folder tree they live in, the browser
// model QML binds to, and their thumbnails, and is the editor's document store.
// It borrows the outputs a thumbnail is rendered for (the session), where the
// files live (the open library), what previews render with, and the rundown
// that "add to service" appends to.
//
// SessionFacade owns one and exposes it as `session.freeformLibrary`.

#include "folder_library.hpp"
#include "freeform_document_store.hpp"
#include "library_models.hpp"
#include "open_library.hpp"
#include "preview_job.hpp"
#include "preview_resources.hpp"
#include "qml_ids.hpp"

#include <seventhslide/db/database.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/freeform/document.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::app {

class IRundown;

class FreeformLibraryController : public FolderLibrary, public IFreeformDocumentStore {
    Q_OBJECT
    QML_ANONYMOUS

public:
    /// ``rundown`` adds the rows "add to service" creates.
    FreeformLibraryController(OpenLibrary& library,
                              session::Session& session,
                              PreviewResources& previews,
                              IRundown& rundown,
                              QObject* parent = nullptr);

    void attach(db::Database& db);
    void detach();
    [[nodiscard]] bool attached() const noexcept override { return repo_ != nullptr; }

    /// Create a blank simple-mode Freeform item. Returns id or -1.
    Q_INVOKABLE qint64 createFreeform(const QString& name = QString());

    Q_INVOKABLE qint64 duplicateItem(qint64 freeform_id);

    Q_INVOKABLE QString deleteItem(qint64 freeform_id);

    /// Add one Freeform document to the open service. ``at_index`` < 0 appends.
    Q_INVOKABLE bool addFreeformToService(qint64 freeform_id, int at_index = -1);
    /// Expand a Freeform folder into one service item per direct-child document.
    Q_INVOKABLE bool addFreeformFolderToService(qint64 folder_id, int at_index = -1);

    /// First-slide library thumb for ``image://freeformthumb/<freeformId>``.
    [[nodiscard]] QImage render_freeform_library_thumb(qint64 freeform_id, const QSize& size);
    /// ``render_freeform_library_thumb``, prepared here and painted (and the
    /// thumbnail stored) by whoever finishes it (see preview_job.hpp). GUI
    /// thread; elsewhere it only reads what is on disk.
    [[nodiscard]] PreviewJob prepare_library_thumb(qint64 freeform_id, const QSize& size);

    void rebuild_freeform_browser();
    /// Times the browser has been built (tests).
    [[nodiscard]] std::uint64_t browser_builds() const noexcept { return browser_builds_; }

    [[nodiscard]] db::FreeformRepository* repo() noexcept { return repo_.get(); }

    // --- IFreeformDocumentStore ------------------------------------------
    /// Says why on ``errorOccurred`` when the stored document cannot be read.
    [[nodiscard]] std::optional<freeform::Document> load_freeform_document(
        std::int64_t id) override;
    /// Every file ``doc`` names is item ``id``'s own once it is stored
    /// (``own_files``).
    [[nodiscard]] QString commit_freeform_document(std::int64_t id,
                                                   const freeform::Document& doc) override;
    [[nodiscard]] std::string freeform_media_owner(std::int64_t id) override;
    void freeform_document_closed(std::int64_t id) override;

    /// Library item ``id`` as live content, tagged with its id. nullopt when
    /// it cannot be loaded.
    [[nodiscard]] std::optional<session::LiveFreeformContent> live_content(std::int64_t id);
    /// ``document``, stored as library item ``id``, as live content.
    [[nodiscard]] static session::LiveFreeformContent live_content(std::int64_t id,
                                                                   freeform::Document document);
    /// Store ``doc`` as a new item in the library root, with its thumbnails,
    /// every file it names its own. ``owner`` is the uid it is stored under
    /// when files were brought into it for it already (freeform/own_media.hpp);
    /// empty, it takes a new one. Returns its id, or -1 without a library.
    [[nodiscard]] qint64 add_document(freeform::Document doc, std::string owner = {});

    /// The files of each owner's folder that no document names any more go
    /// (freeform::remove_unnamed_own_media): those of ``owners``, or with
    /// none given, of every owner. Nothing goes from a library only viewed.
    void prune_own_media(const std::vector<std::string>& owners = {});

signals:
    /// A saved document replaced one already live: the application reloads
    /// what follows live content and repaints.
    void liveChanged(bool rebuildModels);

protected:
    // --- FolderLibrary: items keyed by id ---
    void rebuild() override { rebuild_freeform_browser(); }
    /// With the items in it, and their thumbnails.
    QString delete_folder(std::int64_t folder_id) override;
    QString move_items(const QVariantList& items, std::optional<std::int64_t> folder) override;
    bool reorder_items(std::optional<std::int64_t> folder,
                       const std::vector<std::int64_t>& ids) override;

private:
    void remove_freeform_library_thumbs(std::int64_t freeform_id);
    /// ``doc`` naming only ``owner``'s own files (freeform::take_own_media),
    /// a library file taken in under the title the library gives it. Says
    /// on ``errorOccurred`` when a file could not be taken.
    void own_files(freeform::Document& doc, const std::string& owner);
    /// The uids of every item.
    [[nodiscard]] std::set<std::string> item_uids();

    OpenLibrary& library_;
    session::Session& session_;
    PreviewResources& previews_;
    IRundown& rundown_;
    std::uint64_t browser_builds_ = 0;
    std::unique_ptr<db::FreeformRepository> repo_;
};

} // namespace seventhslide::app
