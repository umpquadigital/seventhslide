#include "folder_library.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "qvariant_json.hpp"

namespace seventhslide::app {

FolderLibrary::FolderLibrary(QObject* parent)
    : QObject(parent)
    , browser_model_(this)
{
}

void FolderLibrary::setFolder(qint64 folder_id)
try {
    if (folders_.enter(folder_id)) {
        rebuild();
        announce();
    }
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

void FolderLibrary::folderUp()
try {
    if (folders_.up()) {
        rebuild();
        announce();
    }
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

qint64 FolderLibrary::folderParentId()
try {
    return folders_.parent_id();
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return -1;
}

qint64 FolderLibrary::createFolder(const QString& name)
try {
    const auto id = folders_.create(name);
    if (id >= 0) {
        rebuild();
        announce();
    }
    return id;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return -1;
}

QString FolderLibrary::renameFolder(qint64 folder_id, const QString& name)
try {
    auto error = folders_.rename(folder_id, name);
    if (error.isEmpty()) {
        rebuild();
        announce();
    }
    return error;
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString FolderLibrary::deleteFolder(qint64 folder_id)
try {
    if (!attached()) {
        return refusal::no_library();
    }
    if (folder_id < 0) {
        return refusal::folder_gone();
    }
    return delete_folder(folder_id);
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

bool FolderLibrary::moveFolder(qint64 folder_id, qint64 new_parent_id)
try {
    if (!folders_.move(folder_id, new_parent_id)) {
        return false;
    }
    rebuild();
    announce();
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

QString FolderLibrary::moveItems(const QVariantList& items, qint64 folder_id)
try {
    if (!attached()) {
        return refusal::no_library();
    }
    const auto dest = optional_id(folder_id);
    if (dest && !folders_.contains(*dest)) {
        return refusal::folder_gone();
    }
    auto error = move_items(items, dest);
    if (error.isEmpty()) {
        rebuild();
        announce();
    }
    return error;
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

bool FolderLibrary::reorderItems(qint64 folder_id, const QVariantList& ordered_ids)
try {
    if (!attached()) {
        return false;
    }
    const auto ids = parse_id_list(ordered_ids);
    if (!ids || !reorder_items(optional_id(folder_id), *ids)) {
        return false;
    }
    rebuild();
    announce();
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

bool FolderLibrary::reorderFolders(qint64 parent_id, const QVariantList& ordered_folder_ids)
try {
    if (!folders_.reorder(parent_id, ordered_folder_ids)) {
        return false;
    }
    rebuild();
    announce();
    return true;
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return false;
}

QVariantList FolderLibrary::listFoldersFlat()
try {
    return folders_.rows();
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
    return {};
}

void FolderLibrary::setSearch(const QString& query)
try {
    if (folders_.set_search(query)) {
        rebuild();
        announce();
    }
} catch (const std::exception& ex) {
    emit errorOccurred(report_entry_error(__func__, ex));
}

} // namespace seventhslide::app
