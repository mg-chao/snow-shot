// SPDX-License-Identifier: GPL-3.0-or-later
#include "displaywakeobserver.h"

#import <AppKit/AppKit.h>
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    @autoreleasepool {
        QCoreApplication app(argc, argv);
        auto* workspace = NSWorkspace.sharedWorkspace;
        auto* center = workspace.notificationCenter;
        const auto post = [center, workspace](NSNotificationName name) {
            [center postNotificationName:name object:workspace];
        };
        int recoveries = 0;
        auto observer = snow_shot::platform::macos::observeDisplayWake([&] { ++recoveries; });

        post(NSWorkspaceWillSleepNotification);
        post(NSWorkspaceScreensDidSleepNotification);
        [center postNotificationName:NSWorkspaceScreensDidWakeNotification object:nil];
        post(NSWorkspaceDidWakeNotification);
        QCoreApplication::processEvents();
        require(recoveries == 0, "only a screen wake from the shared workspace triggers recovery");

        post(NSWorkspaceScreensDidWakeNotification);
        require(recoveries == 1, "publish the wake before a subsequent hotkey can be handled");

        // A second screen wake can happen without a system sleep or wake.
        post(NSWorkspaceScreensDidWakeNotification);
        require(recoveries == 2, "deliver each screen wake once");

        observer.reset();
        post(NSWorkspaceScreensDidWakeNotification);
        QCoreApplication::processEvents();
        require(recoveries == 2, "stop delivering screen wakes after teardown");
    }
}
