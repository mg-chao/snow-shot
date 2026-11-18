#import <AppKit/AppKit.h>
#include <QWidget>

void macPlacePinnedTestUnderlay(QWidget& underlay, QWidget& pin) {
    NSWindow* lower = reinterpret_cast<NSView*>(underlay.winId()).window;
    NSWindow* upper = reinterpret_cast<NSView*>(pin.winId()).window;
    lower.level = upper.level;
    [lower orderWindow:NSWindowBelow relativeTo:upper.windowNumber];
}

bool macPinnedHitTest(QWidget& expected, const QPoint& globalPoint) {
    const NSPoint point =
        NSMakePoint(globalPoint.x(), NSMaxY(NSScreen.screens.firstObject.frame) - globalPoint.y());
    NSWindow* window = reinterpret_cast<NSView*>(expected.winId()).window;
    return window != nil && [NSWindow windowNumberAtPoint:point
                                belowWindowWithWindowNumber:0] == window.windowNumber;
}

qintptr macPinnedWindowNumber(QWidget& pin) {
    return reinterpret_cast<NSView*>(pin.winId()).window.windowNumber;
}
