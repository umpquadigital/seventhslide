#pragma once

// What the interface shows the system's own way, for QML: the fixed-width
// font, what showing a folder is called, and a shortcut as the system writes it (⌘⇧] on macOS, Ctrl+Shift+]
// elsewhere). QML can name neither itself: "monospace" is fontconfig's word,
// unknown on Windows and macOS.

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

namespace seventhslide::app {

/// ``Platform`` in QML.
class PlatformQml : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Platform)
    QML_SINGLETON
    /// The system's fixed-width family: Consolas, Menlo, or fontconfig's
    /// monospace.
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)
    /// What a button that shows a folder in the system's file manager is
    /// called: "Show in Finder", "Show in Explorer", "Show Folder".
    Q_PROPERTY(QString showFolderText READ showFolderText CONSTANT)

public:
    using QObject::QObject;

    [[nodiscard]] QString monoFamily() const;
    [[nodiscard]] QString showFolderText() const;

    /// ``portable`` ("Ctrl+Shift+]", Qt's portable form, where Ctrl is
    /// Command on macOS) as this system shows it.
    Q_INVOKABLE [[nodiscard]] QString shortcutText(const QString& portable) const;
};

}  // namespace seventhslide::app
