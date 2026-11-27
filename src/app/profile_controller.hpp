#pragma once

// Saved style profiles in the open library.
//
// The repository and the service that switches profiles are the open
// library's. A switch rewrites settings, theme maps and output defaults
// underneath everything, so it ends in profileSwitched and the application
// re-reads all of it. SessionFacade owns one and exposes it as
// `session.profiles`.

#include "open_library.hpp"

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace seventhslide::app {

class ProfileController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    /// ``[{id, name, active}]`` for the current library.
    Q_PROPERTY(QVariantList all READ styleProfiles NOTIFY profilesChanged)
    Q_PROPERTY(qint64 activeId READ activeProfileId NOTIFY profilesChanged)
    /// The active profile's name (the bottom bar shows it).
    Q_PROPERTY(QString activeName READ activeProfileName NOTIFY profilesChanged)

public:
    explicit ProfileController(OpenLibrary& library, QObject* parent = nullptr);

    [[nodiscard]] QVariantList styleProfiles() const;
    [[nodiscard]] qint64 activeProfileId() const;
    [[nodiscard]] QString activeProfileName() const;
    /// Write the live assignments back into the active profile (done on quit,
    /// and before the library closes). Nothing is written to a read-only
    /// library (``OpenLibrary::read_only``).
    void capture_active_profile();
    /// Capture the live assignments into the current profile, then apply ``id``.
    /// Empty return = ok, as for every mutator here.
    Q_INVOKABLE [[nodiscard]] QString activateProfile(qint64 id);
    /// New profile keeping today's output defaults but no per-item overrides.
    Q_INVOKABLE [[nodiscard]] QString createProfile(const QString& name);
    Q_INVOKABLE [[nodiscard]] QString duplicateProfile(qint64 id, const QString& name = QString());
    Q_INVOKABLE [[nodiscard]] QString renameProfile(qint64 id, const QString& name);
    /// Refuses the last profile; deleting the active one switches first.
    Q_INVOKABLE [[nodiscard]] QString deleteProfile(qint64 id);

    /// The profile set may have changed underneath (a library opened or a
    /// profile was applied): re-read it.
    void refresh() { emit profilesChanged(); }

signals:
    void profilesChanged();
    /// Another profile was applied: the application re-reads what it rewrote
    /// and repaints (which ends in ``refresh``).
    void profileSwitched();

private:
    OpenLibrary& library_;
};

} // namespace seventhslide::app
