// SPDX-License-Identifier: GPL-3.0-or-later
#include "displaywakeobserver.h"

#import <AppKit/AppKit.h>

#include <QObject>
#include <QPointer>
#include <utility>

namespace snow_shot::platform::macos {
namespace {

class DisplayWakeObserver final : public QObject {
  public:
    explicit DisplayWakeObserver(std::function<void()> onWake) : m_onWake(std::move(onWake)) {
        auto* workspace = NSWorkspace.sharedWorkspace;
        m_center = workspace.notificationCenter;
        const QPointer<DisplayWakeObserver> observer(this);
        m_token = [[m_center addObserverForName:NSWorkspaceScreensDidWakeNotification
                                         object:workspace
                                          queue:NSOperationQueue.mainQueue
                                     usingBlock:^(NSNotification*) {
                                       // Publish the invalidation in this main-thread callback.
                                       if (observer)
                                           observer->m_onWake();
                                     }] retain];
    }

    ~DisplayWakeObserver() override {
        [m_center removeObserver:m_token];
        [m_token release];
    }

  private:
    std::function<void()> m_onWake;
    NSNotificationCenter* m_center;
    id m_token;
};

} // namespace

std::unique_ptr<QObject> observeDisplayWake(std::function<void()> onWake) {
    return std::make_unique<DisplayWakeObserver>(std::move(onWake));
}

} // namespace snow_shot::platform::macos
