#pragma once

// One library tab as QML sees it: a browser over one folder tree, with the
// same folder calls on every tab (images, videos and audio, Freeform, web
// pages), so MediaBrowser asks whichever library it shows without asking
// which one it is.
//
// The folder half is written once here, over a FolderBrowser. Each library
// says what is its own: how its browser is built, how its items are keyed
// (media files by name, Freeform items and web pages by id) and moved, and
// what deleting a folder does to what is in it.
//
// Ids arrive as QML sends them, where -1 (any negative) means none: the root.

#include "folder_browser.hpp"
#include "library_models.hpp"
#include "qml_ids.hpp"

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <optional>
#include <vector>

namespace seventhslide::app {

class FolderLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A library tab's folders: session.media.library(kind), session.freeformLibrary, session.webLibrary")

    Q_PROPERTY(seventhslide::app::MediaBrowserModel* browser READ browser CONSTANT)
    Q_PROPERTY(qint64 folderId READ folderId NOTIFY changed)

public:
    [[nodiscard]] MediaBrowserModel* browser() noexcept { return &browser_model_; }
    [[nodiscard]] qint64 folderId() const noexcept { return qml_id(folders_.folder()); }
    /// The folder shown and the search, for the library's own code.
    [[nodiscard]] FolderBrowser& folder_browser() noexcept { return folders_; }
    [[nodiscard]] const FolderBrowser& folder_browser() const noexcept { return folders_; }

    /// Show ``folder_id`` (-1: the root).
    Q_INVOKABLE void setFolder(qint64 folder_id);
    Q_INVOKABLE void folderUp();
    /// The shown folder's parent: -1 for the root, or at the root.
    Q_INVOKABLE qint64 folderParentId();
    /// A new folder inside the one shown; its id, or -1.
    Q_INVOKABLE qint64 createFolder(const QString& name);
    /// Empty on success, else why not.
    Q_INVOKABLE QString renameFolder(qint64 folder_id, const QString& name);
    /// The folder, the folders under it and what is in them. Empty on
    /// success, else why not.
    Q_INVOKABLE QString deleteFolder(qint64 folder_id);
    Q_INVOKABLE bool moveFolder(qint64 folder_id, qint64 new_parent_id);
    /// ``items`` (as this library keys them: see ``move_items``) into
    /// ``folder_id`` (-1: the root), all or none. Empty on success, else why
    /// not.
    Q_INVOKABLE QString moveItems(const QVariantList& items, qint64 folder_id);
    /// The rows of ``folder_id`` in this order (their browser entry ids).
    Q_INVOKABLE bool reorderItems(qint64 folder_id, const QVariantList& ordered_ids);
    Q_INVOKABLE bool reorderFolders(qint64 parent_id, const QVariantList& ordered_folder_ids);
    /// ``{id, name, path}`` for every folder, sorted by path.
    Q_INVOKABLE QVariantList listFoldersFlat();
    Q_INVOKABLE void setSearch(const QString& query);

signals:
    /// The folder shown, the search or the rows moved.
    void changed();
    /// An entry point failed and has no return value to carry the reason:
    /// the shell shows ``message`` to the operator (see entry_error.hpp).
    void errorOccurred(const QString& message);

protected:
    explicit FolderLibrary(QObject* parent = nullptr);

    /// A library is open to browse.
    [[nodiscard]] virtual bool attached() const = 0;
    /// Build the browser again for the folder shown, the search and the rows.
    virtual void rebuild() = 0;
    /// Say that ``folderId`` or the rows may have moved.
    virtual void announce() { emit changed(); }
    /// Delete folder ``folder_id`` (>= 0) with the folders under it and what
    /// is in them, and rebuild. Empty on success, else why not.
    virtual QString delete_folder(std::int64_t folder_id) = 0;
    /// Move ``items`` into ``folder`` (which exists; nullopt: the root), all
    /// or none. Empty on success, else why not.
    virtual QString move_items(const QVariantList& items, std::optional<std::int64_t> folder) = 0;
    /// Put the rows ``ids`` of ``folder`` in that order. False when this
    /// library cannot.
    virtual bool reorder_items(std::optional<std::int64_t> folder,
                               const std::vector<std::int64_t>& ids) = 0;

    FolderBrowser folders_;
    MediaBrowserModel browser_model_;
};

} // namespace seventhslide::app
