#include "library_registry_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "local_file_path.hpp"

#include <seventhslide/library/cloud_folder.hpp>

#include <QDesktopServices>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace seventhslide::app {

LibraryRegistryController::LibraryRegistryController(OpenLibrary& library,
                                                     ILibrarySwitcher& switcher,
                                                     QObject* parent)
    : QObject(parent)
    , library_(library)
    , switcher_(switcher)
{
}

QVariantList LibraryRegistryController::libraries() const
{
    QVariantList out;
    auto* registry = library_.registry();
    if (registry == nullptr) {
        return out;
    }
    const auto& active = registry->active_id();
    for (const auto& entry : registry->entries()) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), QString::fromStdString(entry.id));
        row.insert(QStringLiteral("name"), QString::fromStdString(entry.name));
        row.insert(QStringLiteral("path"), QString::fromStdString(entry.root.string()));
        row.insert(QStringLiteral("active"), entry.id == active);
        // A folder moved or deleted outside the app: say so rather than let
        // Open fail.
        std::error_code ec;
        row.insert(QStringLiteral("missing"), !std::filesystem::is_directory(entry.root, ec));
        row.insert(QStringLiteral("ownsFolder"), registry->owns_folder(entry.id));
        row.insert(QStringLiteral("cloudService"),
                   QString::fromStdString(library::cloud_sync_service(entry.root)));
        out.push_back(row);
    }
    return out;
}

QString LibraryRegistryController::activeLibraryId() const
{
    auto* registry = library_.registry();
    return registry ? QString::fromStdString(registry->active_id()) : QString();
}

QString LibraryRegistryController::activeLibraryName() const
{
    auto* registry = library_.registry();
    const auto* entry = registry ? registry->active() : nullptr;
    return entry ? QString::fromStdString(entry->name) : QString();
}

QString LibraryRegistryController::active_library_path() const
{
    auto* registry = library_.registry();
    if (registry == nullptr) {
        return {};
    }
    const auto* entry = registry->active();
    return entry ? QString::fromStdString(entry->root.string()) : QString();
}

QString LibraryRegistryController::switchLibrary(const QString& id)
try {
    auto* registry = library_.registry();
    if (registry == nullptr) {
        return tr("Libraries are not available");
    }
    const auto key = id.toStdString();
    if (key == registry->active_id()) {
        return {};
    }
    if (registry->find(key) == nullptr) {
        return refusal::library_gone();
    }
    if (open_check_) {
        if (auto why = open_check_(key); !why.isEmpty()) {
            return why;
        }
    }

    const auto previous_id = registry->active_id();
    switcher_.close_library();
    emit librarySwitching();

    const auto result = registry->set_active(key);
    if (!result.ok()) {
        if (const auto* previous = registry->active()) {
            (void)switcher_.open_library(*previous);
        }
        emit librariesChanged();
        emit profilesChanged();
        return QString::fromStdString(result.error);
    }

    const auto* target = registry->active();
    if (target != nullptr && switcher_.open_library(*target)) {
        emit librariesChanged();
        emit profilesChanged();
        return {};
    }

    // Never leave the registry pointing at a library that will not open - the
    // next launch would hit the same failure with no way back through the UI.
    const QString failure = library_.last_error().isEmpty()
                                ? tr("Could not open that library")
                                : library_.last_error();
    switcher_.close_library();
    if (registry->set_active(previous_id).ok()) {
        if (const auto* fallback = registry->active()) {
            (void)switcher_.open_library(*fallback);
        }
    }
    emit librariesChanged();
    emit profilesChanged();
    return failure;
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString LibraryRegistryController::createLibrary(const QString& name)
try {
    auto* registry = library_.registry();
    if (registry == nullptr) {
        return tr("Libraries are not available");
    }
    const auto result = registry->create(name.trimmed().toStdString());
    if (!result.ok()) {
        return QString::fromStdString(result.error);
    }
    emit librariesChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString LibraryRegistryController::attachLibrary(const QString& folder, const QString& name)
try {
    auto* registry = library_.registry();
    if (registry == nullptr) {
        return tr("Libraries are not available");
    }
    if (folder.trimmed().isEmpty()) {
        return tr("Choose a folder for the library");
    }
    const auto result =
        registry->attach(local_path_from_qml(folder), name.trimmed().toStdString());
    if (!result.ok()) {
        return QString::fromStdString(result.error);
    }
    emit librariesChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString LibraryRegistryController::cloudServiceFor(const QString& folder) const
try {
    if (folder.trimmed().isEmpty()) {
        return {};
    }
    return QString::fromStdString(library::cloud_sync_service(local_path_from_qml(folder)));
} catch (const std::exception& ex) {
    // Only a warning hangs on this; failing to look must not stop the attach.
    (void)report_entry_error(__func__, ex);
    return {};
}

QString LibraryRegistryController::renameLibrary(const QString& id, const QString& name)
try {
    auto* registry = library_.registry();
    if (registry == nullptr) {
        return tr("Libraries are not available");
    }
    const auto result = registry->rename(id.toStdString(), name.trimmed().toStdString());
    if (!result.ok()) {
        return QString::fromStdString(result.error);
    }
    emit librariesChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString LibraryRegistryController::showFolder(const QString& id) const
try {
    auto* registry = library_.registry();
    const auto* entry = registry ? registry->find(id.toStdString()) : nullptr;
    if (entry == nullptr) {
        return refusal::library_gone();
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(entry->root, ec)) {
        return tr("The folder is not there: %1").arg(QString::fromStdString(entry->root.string()));
    }
    const QUrl url = QUrl::fromLocalFile(QString::fromStdString(entry->root.string()));
    if (!QDesktopServices::openUrl(url)) {
        return tr("Could not open %1").arg(url.toLocalFile());
    }
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString LibraryRegistryController::removeLibrary(const QString& id)
try {
    auto* registry = library_.registry();
    if (registry == nullptr) {
        return tr("Libraries are not available");
    }
    const auto result = registry->remove(id.toStdString());
    if (!result.ok()) {
        return QString::fromStdString(result.error);
    }
    auto* destinations = library_.destination_store();
    if (destinations != nullptr && !destinations->remove_library(id.toStdString())) {
        // The library is gone either way; its stale destinations would only
        // come back if a new library took the same id.
        emit errorOccurred(tr("The library was removed, but its output destinations could not "
                              "be cleared from %1")
                               .arg(QString::fromStdString(destinations->file().string())));
    }
    auto* console = library_.console_store();
    if (console != nullptr && !console->remove_library(id.toStdString())) {
        emit errorOccurred(tr("The library was removed, but which of its stacks have a Preview "
                              "could not be cleared from %1")
                               .arg(QString::fromStdString(console->file().string())));
    }
    auto* mixes = library_.audio_mix_store();
    if (mixes != nullptr && !mixes->remove_library(id.toStdString())) {
        emit errorOccurred(tr("The library was removed, but its Audio Mixer levels could not "
                              "be cleared from %1")
                               .arg(QString::fromStdString(mixes->file().string())));
    }
    emit librariesChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString LibraryRegistryController::deleteLibrary(const QString& id, const QString& typedName)
try {
    auto* registry = library_.registry();
    if (registry == nullptr) {
        return tr("Libraries are not available");
    }
    const auto* entry = registry->find(id.toStdString());
    if (entry == nullptr) {
        return refusal::library_gone();
    }
    if (typedName.trimmed().toStdString() != entry->name) {
        return tr("Type the library’s name to delete it.");
    }
    if (!registry->owns_folder(entry->id)) {
        return tr("SeventhSlide did not make this library’s folder, so it will not delete it. "
                  "Remove the library from the list, and delete the folder yourself.");
    }
    const auto folder = entry->root;
    if (auto why = removeLibrary(id); !why.isEmpty()) {
        return why;
    }
    if (delete_wait_) {
        delete_wait_();
    }
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    if (ec) {
        return tr("The library was removed from the list, but its folder could not be fully "
                  "deleted (%1). What is left is in %2.")
            .arg(QString::fromStdString(ec.message()), QString::fromStdString(folder.string()));
    }
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

} // namespace seventhslide::app
