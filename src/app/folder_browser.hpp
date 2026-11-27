#pragma once

// One enter-folder library browser: the folder it shows, its search text, and
// the folder edits it offers, over one folder tree.
//
// Every library tab has one, in its FolderLibrary (images, videos, audio,
// Freeform, web pages), which gives QML its calls and rebuilds the browser
// when one reports a change. What a folder holds, how a search matches it,
// and what deleting a folder does to its contents stay with each library.
//
// Ids arrive as QML sends them, where -1 (any negative) means none: the root.

#include "library_models.hpp"

#include <seventhslide/db/folder_tree.hpp>
#include <seventhslide/model/library.hpp>

#include <QCoreApplication>
#include <QString>
#include <QVariantList>
#include <QtTypes>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace seventhslide::app {

class FolderBrowser {
    Q_DECLARE_TR_FUNCTIONS(FolderBrowser)

public:
    /// Browse ``tree``, from the root with an empty search.
    void attach(db::FolderTree tree);
    /// Nothing to browse: back to the root, search cleared.
    void detach();

    /// The folder shown; nullopt for the root.
    [[nodiscard]] std::optional<std::int64_t> folder() const noexcept { return folder_; }
    /// The search box as typed; ``query`` is what filters.
    [[nodiscard]] const QString& search() const noexcept { return search_; }
    [[nodiscard]] QString query() const { return search_.trimmed(); }
    /// What is shown, for ``MediaBrowserModel::set_entries``: another folder or
    /// search is another view.
    [[nodiscard]] QString view() const
    {
        return QString::number(folder_.value_or(-1)) + QLatin1Char('/') + query();
    }

    /// True when ``id`` is a folder of the tree.
    [[nodiscard]] bool contains(std::int64_t id) const;

    // --- Navigation. True when the browser must be rebuilt. ---
    /// Show ``folder_id`` (-1: the root). False when there is no such folder.
    bool enter(qint64 folder_id);
    /// Show the current folder's parent. False at the root.
    bool up();
    /// Filter by ``text``. False when that is already the search.
    bool set_search(const QString& text);
    /// Back to the root when the folder shown no longer exists; true when it
    /// moved, so ``folder()`` changed. Every rebuild calls this first: a
    /// folder can go with an ancestor, or with a library.
    bool drop_missing();

    /// The current folder's parent for QML: -1 for the root, or at the root.
    [[nodiscard]] qint64 parent_id() const;

    // --- Folder edits. The caller rebuilds after one succeeds. ---
    /// A new folder inside the one shown. -1 with nothing to browse or a
    /// blank name.
    qint64 create(const QString& name);
    /// Empty on success, else why not.
    QString rename(qint64 folder_id, const QString& name);
    /// Move ``folder_id`` to the end of ``parent_id`` (-1: the root). False
    /// when either is missing or the move would put a folder inside itself.
    bool move(qint64 folder_id, qint64 parent_id);
    /// Put ``parent_id``'s children (-1: the root's) in ``ordered_ids`` order.
    /// False when the list is not a list of ids.
    bool reorder(qint64 parent_id, const QVariantList& ordered_ids);
    /// ``{id, name, path}`` for every folder, sorted by path: the Move-to
    /// pickers' list.
    [[nodiscard]] QVariantList rows() const;

    /// The shown folder's children, or nothing with nothing to browse.
    [[nodiscard]] std::vector<model::Folder> children() const;

    /// The browser's leading rows: ".." inside a folder, except while
    /// searching (it is navigation, not a match), then ``folders`` with their
    /// item counts from ``counts``.
    [[nodiscard]] std::vector<MediaBrowserEntry> folder_entries(
        const std::vector<model::Folder>& folders,
        const std::unordered_map<std::int64_t, int>& counts) const;

private:
    std::optional<db::FolderTree> tree_;
    std::optional<std::int64_t> folder_;
    QString search_;
};

}  // namespace seventhslide::app
