// The tests' application in the app's own font and style, not the system's,
// as the app has them (test_qt_app.hpp).

#include "test_qt_app.hpp"
#include "ui_font.hpp"

namespace seventhslide::test {

void use_app_look()
{
    QGuiApplication::setFont(app::operator_ui_font());
    app::use_operator_ui_style();
}

}  // namespace seventhslide::test
