#include "../src/platform/macos/windowshortcutinput_p.h"
#include "snow_shot/presentation/mouseoperationinput.h"

#import <AppKit/AppKit.h>

#include <QApplication>
#include <QAbstractEventDispatcher>
#include <QKeyEvent>
#include <QWidget>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool filterKey(QAbstractNativeEventFilter& filter, NSEventType type, NSEventModifierFlags flags,
               bool repeat = false, unsigned short key = 48) {
    NSEvent* event = [NSEvent keyEventWithType:type
                                      location:NSZeroPoint
                                 modifierFlags:flags
                                     timestamp:1
                                  windowNumber:0
                                       context:nil
                                    characters:@"\t"
                   charactersIgnoringModifiers:@"\t"
                                     isARepeat:repeat
                                       keyCode:key];
    return filter.nativeEventFilter(QByteArrayLiteral("mac_generic_NSEvent"), event, nullptr);
}

void nativeInputPreservesDispatchAndPressOwnership() {
    QWidget first;
    QWidget second;
    QWidget* focused = &first;
    QWidget* deliveredTo = nullptr;
    bool enabled = true;
    int presses = 0;
    int releases = 0;
    Qt::KeyboardModifiers deliveredModifiers;
    auto filter = snow_shot::platform::macos::makeWindowShortcutInputFilter(
        [&](QWidget* receiver, QKeyEvent& event) {
            deliveredTo = receiver;
            deliveredModifiers = event.modifiers();
            require(event.nativeVirtualKey() == 48 && event.timestamp() == 1000,
                    "native Tab identity and timestamp must survive input routing");
            if (event.type() == QEvent::KeyRelease) {
                ++releases;
                return false;
            }
            require(event.key() == (event.modifiers().testFlag(Qt::ShiftModifier) ? Qt::Key_Backtab
                                                                                  : Qt::Key_Tab),
                    "shifted native Tab must retain backward-cycling identity");
            if (!enabled || event.isAutoRepeat())
                return false;
            ++presses;
            return true;
        },
        [&](quintptr) { return focused; });

    require(!filterKey(*filter, NSEventTypeKeyDown, 0) &&
                !filterKey(*filter, NSEventTypeKeyDown, NSEventModifierFlagOption) &&
                !filterKey(*filter, NSEventTypeKeyDown, NSEventModifierFlagCommand) &&
                !filterKey(*filter, NSEventTypeKeyDown, NSEventModifierFlagControl, false, 0) &&
                presses == 0,
            "ordinary keys must retain their normal Cocoa delivery path");
    require(filterKey(*filter, NSEventTypeKeyDown, NSEventModifierFlagControl) && presses == 1 &&
                deliveredModifiers == Qt::MetaModifier && deliveredTo == &first,
            "physical Control+Tab must dispatch once through the focused receiver");
    require(filterKey(*filter, NSEventTypeKeyDown, NSEventModifierFlagControl, true) &&
                presses == 1,
            "a consumed non-repeating shortcut must retain ownership of native repeats");
    focused = &second;
    enabled = false;
    require(filterKey(*filter, NSEventTypeKeyUp, 0) && releases == 1 && deliveredTo == &first,
            "native release must reach its press owner after focus and bindings change");
    require(!filterKey(*filter, NSEventTypeKeyUp, NSEventModifierFlagControl) && releases == 1 &&
                !filterKey(*filter, NSEventTypeKeyDown, NSEventModifierFlagControl) && presses == 1,
            "unowned releases and unavailable shortcuts must remain unconsumed");
    enabled = true;
    require(filterKey(*filter, NSEventTypeKeyDown,
                      NSEventModifierFlagControl | NSEventModifierFlagShift |
                          NSEventModifierFlagOption | NSEventModifierFlagCommand) &&
                presses == 2 && deliveredTo == &second &&
                deliveredModifiers ==
                    (Qt::MetaModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::ControlModifier),
            "configured Tab shortcuts must preserve all physical modifier identities");
    require(filterKey(*filter, NSEventTypeKeyUp, 0) && releases == 2,
            "backward cycling must preserve its release ownership");
    focused = nullptr;
    require(!filterKey(*filter, NSEventTypeKeyDown, NSEventModifierFlagControl) && presses == 2,
            "native input without a matching focused window must remain unconsumed");
}

void activationCanDestroyTheFilter() {
    QWidget receiver;
    std::unique_ptr<QAbstractNativeEventFilter> filter;
    filter = snow_shot::platform::macos::makeWindowShortcutInputFilter(
        [&](QWidget*, QKeyEvent&) {
            filter.reset();
            return true;
        },
        [&](quintptr) { return &receiver; });
    require(filterKey(*filter, NSEventTypeKeyDown, NSEventModifierFlagControl) && filter == nullptr,
            "shortcut activation may destroy the owning manager and native filter safely");
}

void queuedMouseInputRetainsItsPhysicalButton() {
    snow_shot::mouse::initializeNativeButtonInput();
    const auto observe = [](NSEventType type, double timestamp, NSInteger windowNumber) {
        NSEvent* native = [NSEvent mouseEventWithType:type
                                             location:NSZeroPoint
                                        modifierFlags:NSEventModifierFlagControl
                                            timestamp:timestamp
                                         windowNumber:windowNumber
                                              context:nil
                                          eventNumber:0
                                           clickCount:1
                                             pressure:1];
        require(!QAbstractEventDispatcher::instance()->filterNativeEvent(
                    QByteArrayLiteral("mac_generic_NSEvent"), native, nullptr),
                "native mouse observation leaves Qt's event delivery in place");
    };
    observe(NSEventTypeLeftMouseDown, 1, 42);
    observe(NSEventTypeRightMouseDown, 1, 43);
    observe(NSEventTypeRightMouseDown, 2, 42);

    QMouseEvent translated(QEvent::MouseButtonPress, QPointF{}, QPointF{}, Qt::RightButton,
                           Qt::RightButton, Qt::MetaModifier);
    using snow_shot::platform::macos::physicalMousePressButton;
    translated.setTimestamp(1000);
    require(physicalMousePressButton(translated, 42) == Qt::LeftButton &&
                physicalMousePressButton(translated, 43) == Qt::RightButton,
            "queued Qt presses keep their physical button even after later native events");
    translated.setTimestamp(2000);
    require(physicalMousePressButton(translated, 42) == Qt::RightButton,
            "a physical Control-right press retains its own binding");
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    nativeInputPreservesDispatchAndPressOwnership();
    activationCanDestroyTheFilter();
    queuedMouseInputRetainsItsPhysicalButton();
    return 0;
}
