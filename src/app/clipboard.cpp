#include "clipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>

namespace seventhslide::app {

void Clipboard::setText(const QString& text) const
{
    QGuiApplication::clipboard()->setText(text);
}

}  // namespace seventhslide::app
