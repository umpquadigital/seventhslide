#pragma once

// SyncAdmin - the admin work on the open library, for the Sync page
// (``SyncController::admin``, Main.qml's ``sync.admin``): invites made and
// withdrawn, roles and a viewer's copy of the locked admin key, removing a
// computer, the size limit, the password and the recovery code. Each takes the admin password,
// unlocked on the library's thread for that one change, and ends with the
// controller's ``finished(action, result)``, as the controller's own work
// does: ``result.ok``, or ``result.error`` in words, and what the action
// gives (an invite's ``code``, a ``recoveryCode`` to write down).
//
// What it works on is the controller's (the open library's sync, the
// node); it keeps nothing of its own.
//
// Seam: the link (the library's sync, the node).

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

namespace seventhslide::app {

class SyncController;

class SyncAdmin : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("the sync controller makes its admin")

public:
    explicit SyncAdmin(SyncController& sync);

    // Each ends with ``SyncController::finished`` (or returns why it cannot
    // start); empty return = started.
    /// An invite to the open library for ``role`` ("editor" / "viewer"),
    /// good for ``days``; gives its ``code`` and ``expires``. It shows among the
    /// computers, waiting, until its computer joins.
    Q_INVOKABLE QString invite(const QString& role, int days, const QString& password);
    /// Invite ``invite`` (its id) lets no computer in from now on.
    Q_INVOKABLE QString withdrawInvite(const QString& invite, const QString& password);
    /// Make ``machine`` an editor or a viewer (``role``) and, a viewer, one
    /// that holds a copy of the locked admin key from now on or not: whatever
    /// of those differs. A copy it has already stays until the password changes.
    Q_INVOKABLE QString setRole(const QString& machine, const QString& role, bool holdsAdminKey,
                                const QString& password);
    Q_INVOKABLE QString removeComputer(const QString& machine, const QString& password);
    /// ``gigabytes`` (decimal) the open library's media may take; 0 for no
    /// limit.
    Q_INVOKABLE QString setSizeLimit(double gigabytes, const QString& password);
    /// A new admin password, with the current one or (``withRecoveryCode``)
    /// the recovery code; gives a new ``recoveryCode`` when the code was used.
    Q_INVOKABLE QString changePassword(const QString& currentOrCode, const QString& fresh,
                                       bool withRecoveryCode);
    /// A new recovery code (the paper was lost); gives ``recoveryCode``.
    Q_INVOKABLE QString newRecoveryCode(const QString& password);

private:
    /// ``request`` for the open library's admin work (``link::admin``):
    /// ``finished(action, reply)``.
    QString run(const QString& action, nlohmann::json request);

    SyncController& sync_;
};

}  // namespace seventhslide::app
