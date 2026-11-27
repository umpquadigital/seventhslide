#pragma once

// A QML engine for one test and the objects the test creates in it.
//
// Teardown matches the app: the objects go first, newest first, then the
// engine. Destroying a view (ListView, Repeater, ...) only queues its delegates
// with deleteLater, so the scene delivers those deletes while their engine, and
// everything declared before the scene, still exists.

#include "gui_thread.hpp"
#include "icon_image_provider.hpp"
#include "test_qt_app.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAnyStringView>
#include <QByteArray>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace seventhslide::test {

class QmlScene {
public:
    /// Every view draws its icons through ``image://ssicon``, which reads no
    /// application state, so every scene has it, as the app's engine does.
    QmlScene()
    {
        engine_->addImageProvider(QStringLiteral("ssicon"), new app::IconImageProvider());
    }
    QmlScene(const QmlScene&) = delete;
    QmlScene& operator=(const QmlScene&) = delete;

    ~QmlScene()
    {
        while (!objects_.empty()) {
            objects_.pop_back();
        }
        flush_deferred_deletes();
        // The engine joins Qt's pixmap-reader thread as it goes, and a
        // provider there may be waiting on this thread (call_on_gui_thread).
        // Release such waits for the teardown, as main() does on quit.
        app::g_gui_calls_closed = true;
        engine_.reset();
        app::g_gui_calls_closed = false;
        flush_deferred_deletes();
    }

    [[nodiscard]] QQmlEngine& engine() { return *engine_; }

    /// Create ``qml``, resolving relative imports against ``base``, with
    /// ``properties`` set first (its required properties among them). The
    /// scene owns the result.
    QObject* create(const QByteArray& qml, const QUrl& base = {},
                    const QVariantMap& properties = {})
    {
        QQmlComponent component(engine_.get());
        component.setData(qml, base);
        return finish(component, properties);
    }

    /// Create the type ``type`` from the module ``uri``, as the app loads it.
    QObject* create_from_module(QAnyStringView uri, QAnyStringView type,
                                const QVariantMap& properties = {})
    {
        QQmlComponent component(engine_.get());
        component.loadFromModule(uri, type);
        return finish(component, properties);
    }

private:
    QObject* finish(QQmlComponent& component, const QVariantMap& properties)
    {
        QObject* object =
            component.isReady() ? component.createWithInitialProperties(properties) : nullptr;
        if (object == nullptr) {
            FAIL(component.errorString().toStdString());
        }
        objects_.emplace_back(object);
        return object;
    }

    // First member: the engine needs the application to exist.
    bool app_ = (ensure_qt_application(), true);
    std::unique_ptr<QQmlEngine> engine_ = std::make_unique<QQmlEngine>();
    std::vector<std::unique_ptr<QObject>> objects_;
};

}  // namespace seventhslide::test
