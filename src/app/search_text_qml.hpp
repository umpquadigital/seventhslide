#pragma once

// The library search rule (db::SearchQuery: case, accents and punctuation
// aside) for the lists QML filters itself: live sources, the Settings Bibles
// list, the service picker's tags.

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

namespace seventhslide::app {

/// ``SearchText`` in QML.
class SearchText : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// ``text`` holds ``query`` by the library search rule. An empty query
    /// matches everything.
    Q_INVOKABLE [[nodiscard]] bool matches(const QString& text, const QString& query) const;
};

}  // namespace seventhslide::app
