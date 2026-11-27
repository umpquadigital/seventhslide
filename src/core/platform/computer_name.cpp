#include "seventhslide/platform/computer_name.hpp"

#include <QString>
#include <QSysInfo>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#endif

namespace seventhslide::platform {

std::string computer_name()
{
#if defined(__APPLE__)
    if (CFStringRef name = SCDynamicStoreCopyComputerName(nullptr, nullptr); name != nullptr) {
        const QString text = QString::fromCFString(name).trimmed();
        CFRelease(name);
        if (!text.isEmpty()) {
            return text.toStdString();
        }
    }
#endif
    return QSysInfo::machineHostName().toStdString();
}

}  // namespace seventhslide::platform
