#pragma once

// The set of content libraries and which one is open.
//
// The registry and the destination store are the open library's (OpenLibrary
// loads them). Switching closes one library and opens another through
// ILibrarySwitcher, which only the application can do. SessionFacade owns one
// and exposes it as `session.libraries`.

#include "library_switcher.hpp"
#include "open_library.hpp"

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>
#include <string>
#include <utility>

namespace seventhslide::app {

class LibraryRegistryController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    /// ``[{id, name, path, active, missing, ownsFolder, cloudService}]`` ordered
    /// as registered; ``missing`` when the library's folder is not there,
    /// ``ownsFolder`` when ``deleteLibrary`` may delete it, ``cloudService`` the
    /// service syncing it ("Dropbox"), empty for none.
    Q_PROPERTY(QVariantList all READ libraries NOTIFY librariesChanged)
    Q_PROPERTY(QString activeId READ activeLibraryId NOTIFY librariesChanged)
    /// The open library's name (the bottom bar shows it).
    Q_PROPERTY(QString activeName READ activeLibraryName NOTIFY librariesChanged)

public:
    LibraryRegistryController(OpenLibrary& library,
                              ILibrarySwitcher& switcher,
                              QObject* parent = nullptr);

    [[nodiscard]] QVariantList libraries() const;
    [[nodiscard]] QString activeLibraryId() const;
    [[nodiscard]] QString activeLibraryName() const;
    /// Root folder of the active library (empty when no registry is loaded).
    [[nodiscard]] QString active_library_path() const;
    /// Close the current library and open ``id``. Empty return = ok, as for
    /// every mutator here.
    Q_INVOKABLE [[nodiscard]] QString switchLibrary(const QString& id);
    /// Create an empty library under the data dir and register it (does not switch).
    Q_INVOKABLE [[nodiscard]] QString createLibrary(const QString& name);
    /// Register an existing folder as a library. ``folder`` may be a file:// URL.
    Q_INVOKABLE [[nodiscard]] QString attachLibrary(const QString& folder,
                                                    const QString& name = QString());
    /// The cloud service whose app syncs ``folder`` (``library::cloud_sync_service``),
    /// empty for none: a library there can be damaged, so attaching it asks first.
    /// ``folder`` may be a file:// URL.
    Q_INVOKABLE [[nodiscard]] QString cloudServiceFor(const QString& folder) const;
    Q_INVOKABLE [[nodiscard]] QString renameLibrary(const QString& id, const QString& name);
    /// Forget a library, and its destinations and Audio Mixer levels on this
    /// computer. Files are left on disk; the active library cannot be removed.
    Q_INVOKABLE [[nodiscard]] QString removeLibrary(const QString& id);
    /// ``removeLibrary``, and its folder deleted for good, when ``typedName`` is
    /// the library's name and the folder is the library's alone
    /// (``LibraryRegistry::owns_folder``). Sync lets go of it first.
    Q_INVOKABLE [[nodiscard]] QString deleteLibrary(const QString& id, const QString& typedName);
    /// Open library ``id``'s folder in the system's file manager.
    Q_INVOKABLE [[nodiscard]] QString showFolder(const QString& id) const;

    /// The registry was (re)loaded underneath: re-read it.
    void refresh() { emit librariesChanged(); }

    /// Why library ``id`` may not be opened now, in words; empty when it
    /// may. Sync's: a library still arriving from the other computers.
    using OpenCheck = std::function<QString(const std::string& id)>;
    /// ``switchLibrary`` asks ``check`` first; an empty one asks nothing.
    void set_open_check(OpenCheck check) { open_check_ = std::move(check); }
    /// ``deleteLibrary`` calls ``wait`` once the library is off the list, for
    /// what may still be writing in its folder (sync's); an empty one waits
    /// for nothing.
    void set_delete_wait(std::function<void()> wait) { delete_wait_ = std::move(wait); }

signals:
    void librariesChanged();
    /// Opening another library changes the profile set; the facade relays
    /// this to the profile controller.
    void profilesChanged();
    void errorOccurred(const QString& message);
    /// One library has closed and another is about to open: anything that
    /// caches the outgoing library's media lets go of it.
    void librarySwitching();

private:
    OpenLibrary& library_;
    ILibrarySwitcher& switcher_;
    OpenCheck open_check_;
    std::function<void()> delete_wait_;
};

} // namespace seventhslide::app
