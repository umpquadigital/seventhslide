#include "ui_font.hpp"

#include <QFontDatabase>
#include <QFontInfo>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QString>
#include <QStringList>

namespace seventhslide::app {

namespace {

/// Compact Arial-metric UI stack (web CSS / XFCE Sans). GNOME's Adwaita Sans
/// and Cantarell have much taller line metrics at the same pixelSize, which
/// crushes QML chrome that sizes rows from implicitHeight + tight padding.
/// Pick a family that is actually installed - QFontInfo can still report a
/// requested name like "Segoe UI" on Linux when that face is absent.
QString operator_ui_family()
{
    // Linux: compact metric-compatible faces first (XFCE/web). Windows/macOS
    // prefer the native UI face. Never start with a name that merely *exists*
    // (this machine has Segoe UI *and* Adwaita Sans).
    QStringList preferred;
#if defined(Q_OS_WIN)
    preferred = {
        QStringLiteral("Segoe UI"),
        QStringLiteral("Arial"),
        QStringLiteral("Helvetica"),
    };
#elif defined(Q_OS_MACOS)
    preferred = {
        QStringLiteral("Helvetica Neue"),
        QStringLiteral("Arial"),
        QStringLiteral("Helvetica"),
    };
#else
    preferred = {
        QStringLiteral("Liberation Sans"),
        QStringLiteral("Noto Sans"),
        QStringLiteral("DejaVu Sans"),
        QStringLiteral("Arial"),
        QStringLiteral("Helvetica"),
    };
#endif
    for (const QString& name : preferred) {
        if (QFontDatabase::hasFamily(name)) {
            return name;
        }
    }
    QFont sans;
    sans.setStyleHint(QFont::SansSerif);
    return QFontInfo(sans).family();
}

}  // namespace

QFont operator_ui_font()
{
    QFont font(operator_ui_family());
    font.setStyleHint(QFont::SansSerif);
    font.setStyleStrategy(QFont::PreferAntialias);
    // QML sets pixelSize per control; do not inherit GNOME 11pt vs XFCE 10pt.
    font.setPixelSize(13);
    return font;
}

void use_operator_ui_style()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
#if !defined(Q_OS_MACOS)
    // The words hinted to the pixel grid by the system's font engine. Qt
    // Quick's own text (distance fields) ignores hinting and is soft at 13 px
    // on a screen at 100%; a Mac's 2x screen hides that, so it keeps Qt's.
    // Text that turns with a slide's box asks for Qt's itself (SlideStage).
    // The outputs draw their text apart from this (render/text_painter).
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
#endif
}

}  // namespace seventhslide::app
