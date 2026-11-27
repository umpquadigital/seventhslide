#pragma once

// QML hands paths over as either a plain path or a file:// URL, depending on
// whether they came from a text field or a file dialog.

#include <QString>
#include <QUrl>

#include <filesystem>

namespace seventhslide::app {

[[nodiscard]] inline std::filesystem::path local_path_from_qml(const QString& raw)
{
    QString path = raw;
    if (path.startsWith(QStringLiteral("file://"))) {
        path = QUrl(path).toLocalFile();
    }
    return std::filesystem::path(path.toStdString());
}

}  // namespace seventhslide::app
