#pragma once

// The operator window as main() builds it, for tests: Main.qml from the
// SeventhSlide module with a session and a remote controller handed over.

#include "dock_layout.hpp"
#include "image_providers.hpp"
#include "remote_controller.hpp"
#include "sync_controller.hpp"
#include "facade_all.hpp"
#include "test_qml.hpp"

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <string>

namespace seventhslide::test {

// Main.qml with the context main() gives it. The remote stays off: a fresh
// library has it disabled.
class OperatorUi {
public:
    /// With the image providers main() adds, so every image the window asks
    /// for resolves (the ones reading live state wait on this thread, which
    /// the tests' event loops serve).
    /// ``library_tab`` is the Library tab remembered from before (its id).
    /// ``sync``, when given, is the window's (main() makes one; most tests
    /// run without).
    explicit OperatorUi(app::SessionFacade& facade,
                        const QString& library_tab = QStringLiteral("songs"),
                        app::SyncController* sync = nullptr)
        : remote_(facade)
    {
        // Each window starts from the default layout: one test moving a
        // panel must not move it for the next (QSettings outlives a test).
        QSettings().remove(QString::fromLatin1(app::DockLayout::k_settings_key.data()));
        // And the Audio Mixer on its first tab, which is kept the same way.
        facade.mixer()->setShownTab(QStringLiteral("stacks"));
        // And the Library on Songs, unless the test says otherwise.
        facade.library()->setShownTab(library_tab);
        app::add_image_providers(scene_.engine(), facade);
        QObject::connect(&scene_.engine(), &QQmlEngine::warnings,
                         [this](const QList<QQmlError>& errors) {
                             for (const QQmlError& e : errors) {
                                 warnings_ += e.toString().toStdString() + '\n';
                             }
                         });
        window_ = qobject_cast<QQuickWindow*>(scene_.create_from_module(
            "SeventhSlide", "Main",
            {{QStringLiteral("session"), QVariant::fromValue(&facade)},
             {QStringLiteral("remote"), QVariant::fromValue(&remote_)},
             {QStringLiteral("sync"), QVariant::fromValue(sync)}}));
    }

    OperatorUi(const OperatorUi&) = delete;
    OperatorUi& operator=(const OperatorUi&) = delete;

    [[nodiscard]] QQuickWindow* window() const { return window_; }

    [[nodiscard]] const std::string& warnings() const { return warnings_; }

    /// The window's dock layout (where its panels sit).
    [[nodiscard]] app::DockLayout* dock_layout() const
    {
        return window_->findChild<app::DockLayout*>();
    }

    // The first instance of a QML-defined component, by its file name. One
    // made only when first asked for (a LazyLoader named for it) is made now,
    // as opening it would. Nothing else is made: what is inside a window not
    // made yet is there once the test opens that window, as the app does.
    [[nodiscard]] QObject* find_component(const char* name) const
    {
        return component_under(window(), name);
    }

    // The same under ``root``.
    static QObject* component_under(QObject* root, const char* name)
    {
        if (QObject* found = built_under(root, name)) {
            return found;
        }
        for (QObject* o : root->findChildren<QObject*>(QString::fromLatin1(name))) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(
                    QStringLiteral("LazyLoader_QMLTYPE_"))) {
                QObject* made = nullptr;
                QMetaObject::invokeMethod(o, "load", Q_RETURN_ARG(QObject*, made));
                return made;
            }
        }
        return nullptr;
    }

    // The first instance of a QML-defined component made so far.
    [[nodiscard]] QObject* built_component(const char* name) const
    {
        return built_under(window(), name);
    }

    static QObject* built_under(QObject* root, const char* name)
    {
        const QString prefix = QString::fromLatin1(name) + QStringLiteral("_QMLTYPE_");
        for (QObject* o : root->findChildren<QObject*>()) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(prefix)) {
                return o;
            }
        }
        return nullptr;
    }

private:
    // Created before the engine so they outlive it: the remote as in main(),
    // and the warning log the engine writes to.
    seventhslide::app::RemoteController remote_;
    std::string warnings_;
    seventhslide::test::QmlScene scene_;
    QQuickWindow* window_ = nullptr;
};

}  // namespace seventhslide::test
