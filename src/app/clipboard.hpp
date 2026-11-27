#pragma once

// The system clipboard for QML, which has no API of its own for it. Text only:
// the remote's address, for now.

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

namespace seventhslide::app {

/// ``Clipboard`` in QML.
class Clipboard : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// Put ``text`` on the clipboard.
    Q_INVOKABLE void setText(const QString& text) const;
};

}  // namespace seventhslide::app
