#include "snow_shot/presentation/components/actionpopupmenu.h"
#include "snow_shot/presentation/components/pointerhoverevents.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEventLoop>
#include <QHoverEvent>
#include <QPointer>
#include <QPushButton>
#include <QRegion>
#include <QScopeGuard>

#include <cstdlib>
#include <iostream>

namespace {
using adqt::widgets::AdContextMenu;
using snow_shot::presentation::ActionPopupMenu;
using snow_shot::presentation::pointerHoverPositionWithin;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void drainRetiredMenus() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void sendPointerHover(QWidget* receiver, QEvent::Type type, const QPoint& local) {
    const QPoint global = receiver->mapToGlobal(local);
    if (type == QEvent::Enter) {
        QEnterEvent event(local, receiver->mapTo(receiver->window(), local), global);
        QApplication::sendEvent(receiver, &event);
    } else if (type == QEvent::MouseMove) {
        QMouseEvent event(type, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(receiver, &event);
    } else {
        QHoverEvent event(type, local, global, QPointF(-1, -1), Qt::NoModifier);
        QApplication::sendEvent(receiver, &event);
    }
}

void advanceDismissal() {
    QEventLoop loop;
    QTimer::singleShot(200, &loop, &QEventLoop::quit);
    loop.exec();
}

void hoverEventsRespectClippingAndEnabledState() {
    QWidget owner;
    owner.resize(360, 240);
    QWidget parent(&owner);
    parent.setGeometry(20, 20, 40, 40);
    QWidget receiver(&parent);
    receiver.setGeometry(30, 0, 30, 40);
    owner.show();
    QCoreApplication::processEvents();
    const auto hoverPosition = [&](const QPoint& local) {
        QHoverEvent event(QEvent::HoverMove, local, receiver.mapToGlobal(local), QPointF(-1, -1),
                          Qt::NoModifier);
        return pointerHoverPositionWithin(&receiver, &event);
    };
    require(hoverPosition(QPoint(5, 5)) == receiver.mapToGlobal(QPoint(5, 5)),
            "delivered hover inside the visible receiver must retain its event position");
    require(!hoverPosition(QPoint(15, 5)) && !hoverPosition(QPoint(-1, 5)),
            "hover outside the receiver or clipped by its parent must be ignored");
    parent.setEnabled(false);
    require(!hoverPosition(QPoint(5, 5)), "disabled receiver ancestry must reject hover");
    parent.setEnabled(true);
    parent.hide();
    require(!hoverPosition(QPoint(5, 5)), "hidden receiver ancestry must reject hover");
    parent.show();
    QWidget child(&receiver);
    child.setGeometry(0, 0, 10, 10);
    child.show();
    require(hoverPosition(QPoint(5, 5)) == receiver.mapToGlobal(QPoint(5, 5)),
            "hover propagated from an input child must belong to its receiver");
    receiver.setAttribute(Qt::WA_TransparentForMouseEvents);
    require(!hoverPosition(QPoint(5, 5)), "click-through receiver subtrees must reject hover");
    receiver.setAttribute(Qt::WA_TransparentForMouseEvents, false);
    const QPoint point(5, 5);
    QMouseEvent press(QEvent::MouseButtonPress, point, receiver.mapToGlobal(point), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    require(!pointerHoverPositionWithin(&receiver, &press),
            "press events must not be interpreted as hover presence");
}

void enteringTriggerUsesTheDeliveredPointerPosition() {
    QWidget owner;
    owner.resize(360, 240);
    QPushButton trigger(QStringLiteral("More"), &owner);
    trigger.setGeometry(100, 80, 90, 32);
    QPointer<AdContextMenu> menu;
    ActionPopupMenu controller(
        &trigger,
        [&]() {
            menu = new AdContextMenu(&owner);
            menu->addItem(QStringLiteral("Action"));
            return menu.data();
        },
        ActionPopupMenu::Placement::BottomLeft, ActionPopupMenu::Surface::Widget);
    owner.show();
    QCoreApplication::processEvents();
    const QPoint previousCursor = QCursor::pos();
    const auto restoreCursor = qScopeGuard([previousCursor] { QCursor::setPos(previousCursor); });
    // The delivered Enter position is authoritative even if the live cursor
    // reports a point outside the trigger during the transition.
    QCursor::setPos(trigger.mapToGlobal(QPoint(trigger.width() / 2, -1)));
    require(QApplication::widgetAt(QCursor::pos()) != &trigger,
            "the hover fixture must place the live cursor above More");
    const QPoint outside(trigger.width() / 2, -1);
    QEnterEvent outsideEnter(outside, trigger.mapTo(&owner, outside), trigger.mapToGlobal(outside));
    QApplication::sendEvent(&trigger, &outsideEnter);
    require(!menu, "an Enter outside the trigger must not open More");
    for (const QPoint& local :
         {QPoint(trigger.width() / 2, 0), QPoint(trigger.width() / 2, trigger.height() - 1),
          QPoint(0, trigger.height() / 2), QPoint(trigger.width() - 1, trigger.height() / 2)}) {
        QEnterEvent enter(local, trigger.mapTo(&owner, local), trigger.mapToGlobal(local));
        QApplication::sendEvent(&trigger, &enter);
        require(menu && menu->isPopupVisible(),
                "entering any edge must open More using the delivered pointer position");
        menu->dismissPopup();
        drainRetiredMenus();
    }
}

void hoverMovementCanOpenWithoutAnEnterEvent() {
    QWidget owner;
    owner.resize(360, 240);
    QPushButton trigger(QStringLiteral("More"), &owner);
    trigger.setGeometry(100, 80, 90, 32);
    owner.show();
    QCoreApplication::processEvents();
    const QPoint previousCursor = QCursor::pos();
    const auto restoreCursor = qScopeGuard([previousCursor] { QCursor::setPos(previousCursor); });
    QCursor::setPos(owner.mapToGlobal(QPoint(10, 10)));
    QPointer<AdContextMenu> menu;
    int creations = 0;
    ActionPopupMenu controller(
        &trigger,
        [&]() {
            ++creations;
            menu = new AdContextMenu(&owner);
            menu->addItem(QStringLiteral("Action"));
            return menu.data();
        },
        ActionPopupMenu::Placement::BottomLeft, ActionPopupMenu::Surface::Widget);
    for (const QEvent::Type type : {QEvent::HoverMove, QEvent::HoverEnter, QEvent::MouseMove}) {
        const int previousCreations = creations;
        sendPointerHover(&trigger, type, QPoint(trigger.width() / 2, -1));
        require(!menu, "hover movement outside More must not open it");
        for (const QPoint& local : {QPoint(trigger.width() / 2, 0), trigger.rect().center()}) {
            sendPointerHover(&trigger, type, local);
            require(menu && menu->isPopupVisible() && creations == previousCreations + 1,
                    "hover movement must open one menu without requiring an Enter event");
        }
        menu->dismissPopup();
        drainRetiredMenus();
        trigger.setEnabled(false);
        sendPointerHover(&trigger, type, trigger.rect().center());
        require(!menu, "disabled triggers must ignore hover movement");
        trigger.setEnabled(true);
    }
}

void hoverWhileMenuIsOpeningKeepsOneSession() {
    QWidget owner;
    owner.resize(360, 240);
    QPushButton trigger(QStringLiteral("More"), &owner);
    trigger.setGeometry(100, 80, 90, 32);
    owner.show();
    QCoreApplication::processEvents();

    const bool generalEffects = QApplication::isEffectEnabled(Qt::UI_General);
    const bool animateMenus = QApplication::isEffectEnabled(Qt::UI_AnimateMenu);
    const bool fadeMenus = QApplication::isEffectEnabled(Qt::UI_FadeMenu);
    const auto restoreEffects = qScopeGuard([=] {
        QApplication::setEffectEnabled(Qt::UI_FadeMenu, fadeMenus);
        QApplication::setEffectEnabled(Qt::UI_AnimateMenu, animateMenus);
        QApplication::setEffectEnabled(Qt::UI_General, generalEffects);
    });
    QApplication::setEffectEnabled(Qt::UI_General, true);
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, true);
    QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);

    QPointer<AdContextMenu> menu;
    int creations = 0;
    ActionPopupMenu controller(
        &trigger,
        [&]() {
            ++creations;
            menu = new AdContextMenu(&owner);
            menu->addItem(QStringLiteral("Action"));
            QObject::connect(menu, &QMenu::aboutToShow, &owner, [&] {
                if (creations == 1)
                    sendPointerHover(&trigger, QEvent::HoverMove, trigger.rect().center());
            });
            return menu.data();
        },
        ActionPopupMenu::Placement::BottomLeft, ActionPopupMenu::Surface::Widget);

    sendPointerHover(&trigger, QEvent::Enter, trigger.rect().center());
    const QPointer<AdContextMenu> first = menu;
    require(first && first->isVisible() && creations == 1,
            "hover during popup creation must leave exactly one visible menu");
    require(QApplication::isEffectEnabled(Qt::UI_AnimateMenu),
            "opening an action popup must restore the application menu effect");
    for (int index = 0; index < 8; ++index) {
        sendPointerHover(&trigger, QEvent::HoverMove, trigger.rect().center() + QPoint(index, 0));
        require(menu == first && creations == 1,
                "hover before the menu's first paint must keep the same popup session");
    }
    menu->dismissPopup();
    drainRetiredMenus();
    require(!first, "dismissing the popup must release the session");
}

void actionMenuHoverRespectsTheVisibleTriggerRegion() {
    QWidget owner;
    owner.resize(360, 240);
    QWidget parent(&owner);
    parent.setGeometry(20, 20, 100, 80);
    QPushButton trigger(QStringLiteral("More"), &parent);
    trigger.setGeometry(70, 20, 60, 32);
    owner.show();
    QCoreApplication::processEvents();
    const QPoint previousCursor = QCursor::pos();
    const auto restoreCursor = qScopeGuard([previousCursor] { QCursor::setPos(previousCursor); });
    QCursor::setPos(owner.mapToGlobal(QPoint(10, 10)));
    QPointer<AdContextMenu> menu;
    int creations = 0;
    ActionPopupMenu controller(
        &trigger,
        [&]() {
            ++creations;
            menu = new AdContextMenu(&owner);
            menu->addItem(QStringLiteral("Action"));
            return menu.data();
        },
        ActionPopupMenu::Placement::BottomLeft, ActionPopupMenu::Surface::Widget);

    for (const QEvent::Type type :
         {QEvent::Enter, QEvent::HoverEnter, QEvent::HoverMove, QEvent::MouseMove}) {
        sendPointerHover(&trigger, type, QPoint(40, 16));
        require(!menu, "hover in the part of More clipped by its parent must not open a menu");
        trigger.setMask(QRegion(0, 0, 10, trigger.height()));
        sendPointerHover(&trigger, type, QPoint(15, 16));
        require(!menu, "hover outside More's mask must not open a menu");
        trigger.clearMask();
        parent.setMask(QRegion(0, 0, 80, parent.height()));
        sendPointerHover(&trigger, type, QPoint(15, 16));
        require(!menu, "hover outside an ancestor's mask must not open a menu");
        parent.clearMask();
        {
            QWidget blocker(&parent);
            blocker.setGeometry(trigger.geometry());
            blocker.show();
            blocker.raise();
            const QPoint covered(5, 16);
            require(QApplication::widgetAt(trigger.mapToGlobal(covered)) == &blocker,
                    "the hover fixture must cover the visible portion of More");
            sendPointerHover(&trigger, type, covered);
            require(!menu, "hover delivered to a covered trigger must not open a menu");
        }
        parent.setEnabled(false);
        sendPointerHover(&trigger, type, QPoint(5, 16));
        require(!menu, "hover in disabled trigger ancestry must not open a menu");
        parent.setEnabled(true);
        parent.hide();
        sendPointerHover(&trigger, type, QPoint(5, 16));
        require(!menu, "hover in hidden trigger ancestry must not open a menu");
        parent.show();

        const int previousCreations = creations;
        sendPointerHover(&trigger, type, QPoint(5, 16));
        sendPointerHover(&trigger, type, QPoint(5, 16));
        require(menu && menu->isPopupVisible() && creations == previousCreations + 1,
                "the visible part of a clipped trigger must open exactly one popup session");
        menu->dismissPopup();
        drainRetiredMenus();
    }
}

void hoverTransitionsPreserveMenuAndKeyboardSessions() {
    QWidget owner;
    owner.resize(360, 180);
    QPushButton trigger(QStringLiteral("More"), &owner);
    trigger.setGeometry(100, 140, 90, 32);
    owner.show();
    QCoreApplication::processEvents();
    const QPoint previousCursor = QCursor::pos();
    const auto restoreCursor = qScopeGuard([previousCursor] { QCursor::setPos(previousCursor); });
    const QPoint outside = owner.mapToGlobal(QPoint(10, 10));
    QCursor::setPos(outside);
    QPointer<AdContextMenu> menu;
    ActionPopupMenu controller(
        &trigger,
        [&]() {
            menu = new AdContextMenu(&owner);
            for (int index = 0; index < 4; ++index)
                menu->addItem(QStringLiteral("Action %1").arg(index));
            return menu.data();
        },
        ActionPopupMenu::Placement::BottomLeft, ActionPopupMenu::Surface::Widget);
    sendPointerHover(&trigger, QEvent::HoverMove, trigger.rect().center());
    // Settle Qt's opening-time mouse move before delivering the transition stream.
    QCoreApplication::processEvents();
    require(menu && menu->isPopupVisible(), "hover must open the transition fixture's menu");
    const QPoint menuPoint = menu->rect().center();
    require(!owner.rect().contains(owner.mapFromGlobal(menu->mapToGlobal(menuPoint))),
            "the popup fixture must extend beyond its QObject owner's window");
    QEvent leave(QEvent::HoverLeave);
    QApplication::sendEvent(&trigger, &leave);
    sendPointerHover(menu, QEvent::HoverMove, menuPoint);
    advanceDismissal();
    require(menu && menu->isPopupVisible(),
            "hover in a popup outside its owner must cancel dismissal using event coordinates");

    const QPoint triggerPosition = trigger.mapToGlobal(trigger.rect().center());
    require(QApplication::widgetAt(triggerPosition) == &trigger,
            "the trigger must remain uncovered for grabbed popup movement");
    for (const QEvent::Type type : {QEvent::HoverMove, QEvent::MouseMove}) {
        QApplication::sendEvent(menu, &leave);
        sendPointerHover(menu, type, menu->mapFromGlobal(triggerPosition));
        advanceDismissal();
        require(menu && menu->isPopupVisible(),
                "moves grabbed by the popup over its trigger must cancel dismissal");
    }

    sendPointerHover(menu, QEvent::MouseMove, menu->mapFromGlobal(outside));
    advanceDismissal();
    require(!menu || !menu->isPopupVisible(),
            "grabbed movement outside both menu and trigger must dismiss the menu");
    drainRetiredMenus();

    controller.open(true);
    require(menu && menu->activeAction(), "keyboard opening must select an action");
    QApplication::sendEvent(menu, &leave);
    advanceDismissal();
    require(menu && menu->isPopupVisible(), "hover leave must preserve a keyboard menu session");
    sendPointerHover(menu, QEvent::MouseMove, menu->mapFromGlobal(outside));
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(menu, &key);
    advanceDismissal();
    require(menu && menu->isPopupVisible(), "keyboard input must cancel pointer dismissal");
    sendPointerHover(menu, QEvent::MouseMove, menu->mapFromGlobal(outside));
    advanceDismissal();
    require(!menu || !menu->isPopupVisible(),
            "pointer movement must resume dismissal after keyboard interaction");
    drainRetiredMenus();
}

void deletingAnOpenMenuEndsTheControllerSession() {
    QWidget owner;
    QPushButton trigger(QStringLiteral("More"), &owner);
    owner.show();
    QPointer<AdContextMenu> menu;
    int creations = 0;
    ActionPopupMenu controller(
        &trigger,
        [&]() {
            ++creations;
            menu = new AdContextMenu(&owner);
            menu->addItem(QStringLiteral("Action"));
            return menu.data();
        },
        ActionPopupMenu::Placement::BottomLeft, ActionPopupMenu::Surface::Widget);
    controller.open();
    require(menu && menu->isPopupVisible(), "the action menu must open");
    QPointer<AdContextMenu> previous = menu;
    // A popup's destruction can synchronously deliver events to its trigger.
    // Reopen before hiding, while the old menu still reports visible, to verify
    // that the controller has relinquished that session before teardown starts.
    QObject::connect(previous, &AdContextMenu::aboutToDestroy, &owner, [&]() {
        require(previous && previous->isPopupVisible(),
                "destruction notification must precede menu state teardown");
        controller.open();
        require(creations == 2 && menu != previous,
                "a destroying menu must no longer own the controller's active session");
    });
    delete previous.data();
    require(!previous && menu && menu->isPopupVisible(),
            "destroying an old session must preserve the newly opened menu");
    menu->dismissPopup();
    drainRetiredMenus();
    require(!menu, "the replacement session must retire normally");
}

void closingAndReopeningMenusPreservesActionDelivery() {
    QWidget owner;
    QPushButton trigger(QStringLiteral("More"), &owner);
    owner.show();
    QPointer<AdContextMenu> menu;
    QPointer<QAction> action;
    int creations = 0;
    int activations = 0;
    ActionPopupMenu controller(
        &trigger,
        [&]() {
            ++creations;
            menu = new AdContextMenu(&owner);
            action = menu->addItem(QStringLiteral("Action"));
            QObject::connect(action, &QAction::triggered, &owner, [&]() { ++activations; });
            return menu.data();
        },
        ActionPopupMenu::Placement::BottomLeft, ActionPopupMenu::Surface::Widget);
    for (int cycle = 1; cycle <= 4; ++cycle) {
        controller.open(true);
        require(menu && menu->activeAction() == action,
                "keyboard opening must select the current session's action");
        QPointer<AdContextMenu> previous = menu;
        QPointer<QAction> previousAction = action;
        menu->dismissPopup();
        // QMenu delivers its selected action after aboutToHide.
        previousAction->trigger();
        require(activations == cycle, "retirement must preserve action delivery");
        controller.open();
        require(menu && menu != previous && creations == 2 * cycle,
                "reopening before deferred deletion must create a fresh session");
        drainRetiredMenus();
        require(!previous && !previousAction && menu && menu->isPopupVisible(),
                "retiring an older menu must leave the new session usable");
        menu->dismissPopup();
        drainRetiredMenus();
        require(!menu && !action, "dismissal must release the menu and its actions");
    }
}

void destroyingTheTriggerDismissesItsMenu() {
    QWidget owner;
    owner.show();
    auto* trigger = new QPushButton(QStringLiteral("More"), &owner);
    trigger->show();
    QPointer<AdContextMenu> menu;
    new ActionPopupMenu(
        trigger,
        [&]() {
            menu = new AdContextMenu(&owner);
            menu->addItem(QStringLiteral("Action"));
            return menu.data();
        },
        ActionPopupMenu::Placement::BottomLeft, ActionPopupMenu::Surface::Widget);
    trigger->click();
    require(menu && menu->isPopupVisible(), "the trigger must open its owned controller's menu");
    delete trigger;
    drainRetiredMenus();
    require(!menu, "trigger destruction must dismiss and release its popup session");
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    hoverEventsRespectClippingAndEnabledState();
    enteringTriggerUsesTheDeliveredPointerPosition();
    hoverMovementCanOpenWithoutAnEnterEvent();
    hoverWhileMenuIsOpeningKeepsOneSession();
    actionMenuHoverRespectsTheVisibleTriggerRegion();
    hoverTransitionsPreserveMenuAndKeyboardSessions();
    deletingAnOpenMenuEndsTheControllerSession();
    closingAndReopeningMenusPreservesActionDelivery();
    destroyingTheTriggerDismissesItsMenu();
    std::cout << "Action popup menu tests passed\n";
    return 0;
}
