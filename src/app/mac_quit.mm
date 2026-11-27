#include "mac_quit.hpp"

#include <QCoreApplication>
#include <QEvent>

#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

namespace {

/// The quit request being held until the app has quit; null for none.
NSAppleEventManagerSuspensionID g_held = nullptr;

}  // namespace

@interface SeventhSlideQuitHandler : NSObject
- (void)handleQuit:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation SeventhSlideQuitHandler

- (void)handleQuit:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply
{
    (void)event;
    QCoreApplication* app = QCoreApplication::instance();
    if (app == nullptr || g_held != nullptr) {
        // Not running, or quitting already: answered at once.
        return;
    }
    // What Command+Q does: every window asked to close (the outputs first,
    // OutputWindowHost's filter), then the event loop told to end.
    QEvent quit(QEvent::Quit);
    QCoreApplication::sendEvent(app, &quit);
    if (!quit.isAccepted()) {
        [reply setParamDescriptor:[NSAppleEventDescriptor descriptorWithInt32:userCanceledErr]
                       forKeyword:keyErrorNumber];
        return;
    }
    g_held = [[NSAppleEventManager sharedAppleEventManager] suspendCurrentAppleEvent];
}

@end

namespace seventhslide::app {

void handle_mac_quit_events()
{
    static SeventhSlideQuitHandler* handler = [[SeventhSlideQuitHandler alloc] init];
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:handler
            andSelector:@selector(handleQuit:withReplyEvent:)
          forEventClass:kCoreEventClass
             andEventID:kAEQuitApplication];
}

MacQuitAnswer::~MacQuitAnswer()
{
    if (g_held != nullptr) {
        [[NSAppleEventManager sharedAppleEventManager] resumeWithSuspensionID:g_held];
        g_held = nullptr;
    }
}

}  // namespace seventhslide::app
