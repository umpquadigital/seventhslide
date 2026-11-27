#include "output_window_mac.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QtGui/qscreen_platform.h>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

namespace seventhslide::render {

namespace {

/// Qt's NSWindow for ``window``; nil until it is shown, or not under Cocoa.
[[nodiscard]] NSWindow* ns_window(QWindow& window);

/// Only Qt's Cocoa platform is the real desktop. Under another (offscreen, as
/// the tests and a headless run use) winId() is not an NSView, and nothing is
/// on a screen to keep awake.
[[nodiscard]] bool on_cocoa()
{
    return QGuiApplication::platformName() == QLatin1String("cocoa");
}

NSWindow* ns_window(QWindow& window)
{
    if (!on_cocoa()) {
        return nil;
    }
    // Qt's NSView for the window; its NSWindow once shown.
    auto* view = reinterpret_cast<NSView*>(window.winId());
    return view != nil ? [view window] : nil;
}

}  // namespace

void keep_above_menu_bar(QWindow& window)
{
    NSWindow* ns = ns_window(window);
    if (ns == nil) {
        return;
    }
    // The status level is above the menu bar's and the Dock's.
    [ns setLevel:NSStatusWindowLevel];
    [ns setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces
                              | NSWindowCollectionBehaviorStationary
                              | NSWindowCollectionBehaviorIgnoresCycle
                              | NSWindowCollectionBehaviorFullScreenAuxiliary];
    // Stays up while the operator works in another app, or hides this one.
    [ns setHidesOnDeactivate:NO];
    [ns setCanHide:NO];
}

void keep_as_window(QWindow& window)
{
    NSWindow* ns = ns_window(window);
    if (ns == nil) {
        return;
    }
    [ns setLevel:NSNormalWindowLevel];
    [ns setCollectionBehavior:NSWindowCollectionBehaviorDefault];
    [ns setHidesOnDeactivate:NO];
    [ns setCanHide:YES];
}

QString display_uuid(const QScreen& screen)
{
    if (!on_cocoa()) {
        return {};
    }
    const auto* cocoa = screen.nativeInterface<QNativeInterface::QCocoaScreen>();
    NSScreen* ns = cocoa != nullptr ? cocoa->nativeScreen() : nil;
    NSNumber* number = ns != nil ? ns.deviceDescription[@"NSScreenNumber"] : nil;
    if (number == nil) {
        return {};
    }
    CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(number.unsignedIntValue);
    if (uuid == nullptr) {
        return {};
    }
    CFStringRef text = CFUUIDCreateString(kCFAllocatorDefault, uuid);
    CFRelease(uuid);
    if (text == nullptr) {
        return {};
    }
    const QString out = QString::fromCFString(text);
    CFRelease(text);
    return out;
}

void set_presenting(bool presenting)
{
    // The activity token, retained (CFBridgingRetain works with ARC and
    // without); null when not presenting.
    static CFTypeRef activity = nullptr;
    if (presenting == (activity != nullptr) || !on_cocoa()) {
        return;
    }
    NSProcessInfo* info = [NSProcessInfo processInfo];
    if (presenting) {
        const NSActivityOptions options = NSActivityUserInitiated
                                          | NSActivityIdleDisplaySleepDisabled
                                          | NSActivityLatencyCritical;
        activity = CFBridgingRetain([info beginActivityWithOptions:options
                                                            reason:@"Presenting to an audience"]);
    } else {
        id<NSObject> token = CFBridgingRelease(activity);
        activity = nullptr;
        [info endActivity:token];
    }
}

}  // namespace seventhslide::render
