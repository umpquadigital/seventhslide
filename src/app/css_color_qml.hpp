#pragma once

// QML access to render::css_color.
//
// The swatch and the string ColorPicker stores both come from the painter's
// parser. The field does not keep a second rgb() reader that can drift.

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

namespace seventhslide::app {

class CssColor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    /// ``#rrggbb``, or ``#aarrggbb`` when the color is translucent.
    /// Empty when ``css`` is not a color.
    Q_INVOKABLE QString canonical(const QString& css) const;
};

}  // namespace seventhslide::app
