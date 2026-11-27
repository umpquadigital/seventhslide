#include "css_color_qml.hpp"

#include <seventhslide/render/css_color.hpp>

namespace seventhslide::app {

QString CssColor::canonical(const QString& css) const
{
    const auto hex = render::css_color_hex(css.toStdString());
    return QString::fromStdString(hex);
}

}  // namespace seventhslide::app
