#include "platform_qml.hpp"

#include <QFontDatabase>
#include <QKeySequence>

namespace seventhslide::app {

QString PlatformQml::monoFamily() const
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QString PlatformQml::showFolderText() const
{
#if defined(Q_OS_MACOS)
    return tr("Show in Finder");
#elif defined(Q_OS_WIN)
    return tr("Show in Explorer");
#else
    return tr("Show Folder");
#endif
}

QString PlatformQml::shortcutText(const QString& portable) const
{
    return QKeySequence::fromString(portable, QKeySequence::PortableText)
        .toString(QKeySequence::NativeText);
}

}  // namespace seventhslide::app
