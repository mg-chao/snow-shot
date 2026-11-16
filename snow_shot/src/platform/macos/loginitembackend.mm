#include "snow_shot/platform/macos/loginitemservice.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/settingsadapters.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
#include <QCoreApplication>
#include <QEventLoop>

namespace snow_shot::platform::macos {
namespace {
QString text(const char* source) {
    return QCoreApplication::translate("LoginItemService", source);
}
bool initialLoginLaunch = false;
id launchObserver = nil;
LoginItemSnapshot query() {
    @autoreleasepool {
        // Let ServiceManagement determine registration eligibility.
        switch (SMAppService.mainAppService.status) {
        case SMAppServiceStatusNotRegistered:
            return {LoginItemStatus::Unregistered, {}};
        case SMAppServiceStatusEnabled:
            return {LoginItemStatus::Enabled, {}};
        case SMAppServiceStatusRequiresApproval:
            return {LoginItemStatus::ApprovalRequired, {}};
        case SMAppServiceStatusNotFound:
            // macOS 26 reports notFound for a main app that has no login item
            // record yet. Registration is the authoritative eligibility check.
            return {LoginItemStatus::Unregistered, {}};
        }
        return {LoginItemStatus::Unavailable, {}};
    }
}
} // namespace
LoginItemOperations nativeLoginItemOperations() {
    return {query,
            [](bool enabled) -> LoginItemResult {
                @autoreleasepool {
                    NSError* error = nil;
                    const BOOL success =
                        enabled ? [SMAppService.mainAppService registerAndReturnError:&error]
                                : [SMAppService.mainAppService unregisterAndReturnError:&error];
                    if (success)
                        return {};
                    return {false, text(QT_TRANSLATE_NOOP("LoginItemService",
                                                          "Could not change launch at login: %1"))
                                       .arg(QString::fromNSString(error.localizedDescription))};
                }
            },
            [](bool enabled) {
                return storage::SystemSettings().setAutoStartAtBoot(enabled) &&
                       storage::ApplicationStorage::instance().configuration().flushNow().success;
            }};
}
LoginItemService& loginItemService() {
    static LoginItemService service(nativeLoginItemOperations());
    return service;
}
void observeNativeLoginItemLaunch() {
    if (launchObserver != nil)
        return;
    initialLoginLaunch = false;
    // Install before QApplication: AppKit may finish launching during Qt setup.
    launchObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:NSApplicationDidFinishLaunchingNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification*) {
                  initialLoginLaunch = isNativeLoginItemLaunch();
                }];
}
bool initialNativeLoginItemLaunch() {
    // Qt initializes AppKit on its first event-loop pass. Capture the launch event
    // while AppKit handles it, before forwarding to an existing instance.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    if (launchObserver != nil) {
        [NSNotificationCenter.defaultCenter removeObserver:launchObserver];
        launchObserver = nil;
    }
    return initialLoginLaunch || isNativeLoginItemLaunch();
}
bool isNativeLoginItemLaunch() {
    NSAppleEventDescriptor* event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
    return event.eventID == kAEOpenApplication &&
           [event paramDescriptorForKeyword:keyAEPropData].enumCodeValue ==
               keyAELaunchedAsLogInItem;
}
} // namespace snow_shot::platform::macos
