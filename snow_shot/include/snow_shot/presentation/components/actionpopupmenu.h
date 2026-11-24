#pragma once
#include "snow_shot/shortcuts/shortcutbinding.h"
#include "snow_shot/presentation/components/pointerhoverevents.h"

#include "theme/theme_manager.h"
#include "widgets/context_menu.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCursor>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>
#include <functional>
#include <optional>
#include <utility>

namespace snow_shot::presentation {

// Shared hover/click menu behavior for compact action triggers.
class ActionPopupMenu final : public QObject {
  public:
    enum class Placement { BottomLeft, TopRight };
    // Platform keeps the macOS native menu. Widget uses the shared hover menu on every platform.
    enum class Surface { Platform, Widget };
    // The factory returns a fresh heap-owned menu for each popup session.
    ActionPopupMenu(QAbstractButton* trigger,
                    std::function<adqt::widgets::AdContextMenu*()> createMenu,
                    Placement placement = Placement::BottomLeft,
                    Surface surface = Surface::Platform)
        : QObject(trigger), m_trigger(trigger), m_createMenu(std::move(createMenu)),
          m_placement(placement), m_nativeMenu(false) {
#ifdef Q_OS_MACOS
        m_nativeMenu = surface != Surface::Widget;
#else
        (void)surface;
#endif
        if (!m_nativeMenu)
            trigger->setAttribute(Qt::WA_Hover);
        m_closeTimer.setSingleShot(true);
        m_closeTimer.setInterval(150);
        connect(&m_closeTimer, &QTimer::timeout, this, [this] {
            if (m_menu && !pointerOverMenuOrTrigger(QCursor::pos()))
                dismissMenu();
        });
        trigger->installEventFilter(this);
        connect(trigger, &QAbstractButton::clicked, this, [this] { open(); });
    }

    ~ActionPopupMenu() override {
        dismissMenu();
    }

    void open(bool keyboard = false, std::optional<QPoint> globalPosition = std::nullopt) {
        if (m_opening)
            return;
        m_closeTimer.stop();
        m_keyboard = keyboard;
        if (!m_trigger->isVisible() || !m_trigger->isEnabled())
            return;
        if (!m_menu || !m_menu->isPopupVisible()) {
            m_opening = true;
            const QPointer<ActionPopupMenu> controller(this);
            const auto finishOpening = qScopeGuard([controller] {
                if (controller)
                    controller->m_opening = false;
            });
            dismissMenu();
            m_menu = m_createMenu();
            if (!m_menu)
                return;
            if (!m_nativeMenu)
                m_menu->setNativeMenuEnabled(false);
            m_menu->setDeleteOnHide();
            m_menu->setTriggerWidget(m_trigger);
            m_menu->installEventFilter(this);
            auto* menu = m_menu.data();
            const auto detachSession = [this, menu] {
                if (m_menu == menu)
                    detachMenu();
            };
            connect(menu, &QMenu::aboutToHide, this, detachSession);
            connect(menu, &adqt::widgets::AdContextMenu::aboutToDestroy, this, detachSession);
            const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(m_trigger);
            auto tokens = m_menu->componentTokens();
            tokens.minimumWidth = 0;
            tokens.text = theme.colorPrimary;
            tokens.hoverText = theme.colorPrimary;
            m_menu->setComponentTokens(tokens);
            QPoint position(0, m_trigger->height());
            if (m_placement == Placement::TopRight) {
                const QSize size = m_menu->sizeHint();
                position = QPoint(m_trigger->width() - size.width(), -size.height());
            }
            if (!keyboard)
                m_menu->setActiveAction(nullptr);

            // Qt's menu effect shows a proxy window before the real popup becomes visible.
            // Pointer events during that transition can reopen a hover menu on the same trigger.
            const bool animateMenu =
                !m_nativeMenu && QApplication::isEffectEnabled(Qt::UI_AnimateMenu);
            if (animateMenu)
                QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
            const auto restoreMenuEffect = qScopeGuard([animateMenu] {
                if (animateMenu)
                    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, true);
            });
            m_menu->popupAt(globalPosition.value_or(m_trigger->mapToGlobal(position)));
            if (!controller)
                return;
        }
        if (keyboard && m_menu) {
            for (auto* action : m_menu->actions()) {
                if (action->isEnabled() && action->isVisible() && !action->isSeparator()) {
                    m_menu->setActiveAction(action);
                    break;
                }
            }
            if (!m_nativeMenu)
                m_menu->setFocus(Qt::PopupFocusReason);
        }
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_trigger && event->type() == QEvent::ContextMenu) {
            auto* context = static_cast<QContextMenuEvent*>(event);
            open(false, context->globalPos());
            context->accept();
            return true;
        }
        const bool pointerMoved =
            event->type() == QEvent::HoverMove || event->type() == QEvent::MouseMove;
        if (!m_nativeMenu) {
            const auto* widget = qobject_cast<QWidget*>(watched);
            const bool insideReceiver = pointerHoverPositionWithin(widget, event).has_value();
            // Popup grabs also deliver moves outside the receiver. Resolve those
            // against the actual target, using the same event-coordinate mapping.
            const auto grabbedPosition =
                pointerMoved && !insideReceiver
                    ? adqt::widgets::detail::pointerEventGlobalPosition(widget, event)
                    : std::nullopt;
            if (pointerMoved)
                m_keyboard = false;
            if (insideReceiver || (grabbedPosition && pointerOverMenuOrTrigger(*grabbedPosition))) {
                m_closeTimer.stop();
                if (watched == m_trigger && insideReceiver &&
                    (!m_menu || !m_menu->isPopupVisible()))
                    open();
            } else if (pointerMoved && m_menu && m_menu->isPopupVisible() &&
                       !m_closeTimer.isActive()) {
                m_closeTimer.start();
            }
        }
        if (watched == m_trigger && event->type() == QEvent::Hide && m_menu)
            dismissMenu();
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (watched == m_trigger && (snow_shot::shortcuts::commandKey(*key) == Qt::Key_Return ||
                                         snow_shot::shortcuts::commandKey(*key) == Qt::Key_Enter ||
                                         snow_shot::shortcuts::commandKey(*key) == Qt::Key_Space ||
                                         snow_shot::shortcuts::commandKey(*key) == Qt::Key_Up ||
                                         snow_shot::shortcuts::commandKey(*key) == Qt::Key_Down)) {
                open(true);
                return true;
            }
            if (watched == m_menu) {
                m_keyboard = true;
                m_closeTimer.stop();
            }
            if (watched == m_menu && snow_shot::shortcuts::commandKey(*key) == Qt::Key_Escape) {
                dismissMenu();
                m_trigger->setFocus(Qt::PopupFocusReason);
                return true;
            }
        }
        if (!m_nativeMenu && m_menu && m_menu->isPopupVisible()) {
            if ((event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave) &&
                !m_keyboard) {
                m_closeTimer.start();
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    adqt::widgets::AdContextMenu* detachMenu() {
        m_closeTimer.stop();
        auto* menu = m_menu.data();
        m_menu.clear();
        if (menu) {
            // QPointer survives the derived destructor. Stop observing before
            // QWidget teardown can send events after the menu's private state is gone.
            menu->removeEventFilter(this);
            disconnect(menu, nullptr, this, nullptr);
        }
        return menu;
    }

    void dismissMenu() {
        if (auto* menu = detachMenu())
            menu->dismissPopup();
    }

    bool pointerOverMenuOrTrigger(const QPoint& position) const {
        // A popup can grab moves outside its bounds; rectangles also count covered widgets.
        const QWidget* target = QApplication::widgetAt(position);
        return target && (target == m_trigger || m_trigger->isAncestorOf(target) ||
                          (m_menu && (target == m_menu || m_menu->isAncestorOf(target))));
    }
    QAbstractButton* m_trigger;
    QPointer<adqt::widgets::AdContextMenu> m_menu;
    std::function<adqt::widgets::AdContextMenu*()> m_createMenu;
    Placement m_placement;
    QTimer m_closeTimer;
    bool m_keyboard = false;
    bool m_nativeMenu = false;
    bool m_opening = false;
};
} // namespace snow_shot::presentation
