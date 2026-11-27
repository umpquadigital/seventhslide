#pragma once

// RemoteController - owns the web remote's lifecycle and exposes its
// status to the settings dialog.
//
// Keeping this out of main.cpp gives QML something to bind to: whether the
// server is up, which port it took, and why a start failed.

#include "session_remote_bridge.hpp"
#include "throttle.hpp"

#include <seventhslide/remote/remote_server.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>

namespace seventhslide::app {

class SessionFacade;

class RemoteController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("main() creates the one remote controller")

    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(int port READ port NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(QStringList joinUrls READ joinUrls NOTIFY statusChanged)

public:
    explicit RemoteController(SessionFacade& facade, QObject* parent = nullptr);

    [[nodiscard]] bool running() const { return server_.running(); }
    [[nodiscard]] int port() const { return server_.port(); }
    [[nodiscard]] QString lastError() const { return server_.last_error(); }
    [[nodiscard]] QStringList joinUrls() const;

Q_SIGNALS:
    void statusChanged();

private:
    /// Bring the server in line with the current app settings. Safe to call on
    /// every settings save: it restarts only when the port actually moved.
    void apply_settings();
    void wire_facade_signals();

    SessionFacade& facade_;
    SessionRemoteBridge bridge_;
    remote::RemoteServer server_;
    int started_port_ = 0;
    /// The playing clock, sent to the remote four times a second rather than
    /// with every tick.
    Throttle position_pushes_;
    /// A web page's or live source's new picture, likewise.
    Throttle source_pushes_;
};

} // namespace seventhslide::app
