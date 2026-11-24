// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/storage/applicationstorage.h"
#include <QApplication>
#include <QMouseEvent>

class QWidget;

namespace snow_shot::mouse {
inline const Bindings& defaultBindings() {
    static const Bindings bindings;
    return bindings;
}
inline std::optional<Action> match(Category category, Binding binding) {
    auto& storage = storage::ApplicationStorage::instance();
    if (storage.isInitialized())
        return storage.configuration().mouseAction(category, binding);
    return defaultBindings().match(category, binding);
}
#ifdef Q_OS_MACOS
void initializeNativeButtonInput();
Qt::MouseButton physicalButton(const QMouseEvent& event, const QWidget& surface);
#else
inline Qt::MouseButton physicalButton(const QMouseEvent& event, const QWidget&) {
    return event.button();
}
#endif

// A press owns its initiating button until release, including after modifier changes.
struct Press {
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButton reportedButton = Qt::NoButton;
    Qt::KeyboardModifiers keys{};
    QPointF origin;
    std::optional<Action> click, drag;
    bool crossed = false;
    bool updateThreshold(const QPointF& point) {
        if (!crossed && (point - origin).manhattanLength() >= QApplication::startDragDistance())
            crossed = true;
        return crossed;
    }
};
// Qt detects double-clicks by button and timing. A binding also requires both
// presses to use the same modifiers, and the first press to have completed a click.
class ClickSequence {
  public:
    void observe(const QMouseEvent& event, const QWidget& surface) {
        const auto type = event.type();
        if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
            const Binding click{Kind::Click, physicalButton(event, surface),
                                modifiers(event.modifiers())};
            // A canvas fallback or QWidget propagation can see the same press again.
            if (m_pressed && m_pressType == type && m_timestamp == event.timestamp() &&
                m_pressed->origin == event.globalPosition() && m_pressed->button == click.button &&
                m_pressed->keys == click.modifiers)
                return;
            m_kind = type == QEvent::MouseButtonDblClick && m_lastClick == click ? Kind::DoubleClick
                                                                                 : Kind::Click;
            m_lastClick.reset();
            m_pressed = Press{.button = click.button,
                              .reportedButton = event.button(),
                              .keys = click.modifiers,
                              .origin = event.globalPosition(),
                              .click = {},
                              .drag = {}};
            m_pressType = type;
            m_timestamp = event.timestamp();
        } else if (m_pressed && type == QEvent::MouseMove) {
            if (event.buttons().testFlag(m_pressed->reportedButton))
                m_pressed->updateThreshold(event.globalPosition());
            else
                m_pressed.reset();
        } else if (m_pressed && type == QEvent::MouseButtonRelease &&
                   event.button() == m_pressed->reportedButton) {
            if (m_kind == Kind::Click && !m_pressed->updateThreshold(event.globalPosition()))
                m_lastClick = Binding{Kind::Click, m_pressed->button, m_pressed->keys};
            m_pressed.reset();
        }
    }

    Kind kind() const {
        return m_kind;
    }
    void cancelPress() {
        m_pressed.reset();
    }
    void reset() {
        cancelPress();
        m_lastClick.reset();
        m_kind = Kind::Click;
    }

  private:
    std::optional<Press> m_pressed;
    std::optional<Binding> m_lastClick;
    Kind m_kind = Kind::Click;
    QEvent::Type m_pressType = QEvent::None;
    quint64 m_timestamp = 0;
};

inline Press resolvePress(Category category, const QMouseEvent& event, Kind kind,
                          const QWidget& surface) {
    Press p;
    p.button = physicalButton(event, surface);
    p.reportedButton = event.button();
    p.keys = modifiers(event.modifiers());
    p.origin = event.globalPosition();
    // Both candidates must come from the same configuration revision.
    auto& storage = storage::ApplicationStorage::instance();
    const auto bindings =
        storage.isInitialized() ? storage.configuration().mouseBindings() : defaultBindings();
    p.click = bindings.match(category, {kind, p.button, p.keys});
    if (category == Category::Pinned)
        p.drag = bindings.match(category, {Kind::Drag, p.button, p.keys});
    return p;
}
} // namespace snow_shot::mouse
