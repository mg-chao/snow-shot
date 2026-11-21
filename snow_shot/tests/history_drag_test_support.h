#pragma once

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QThread>
#include <QWidget>

namespace history_drag_test {

inline void mouse(QWidget* target, QEvent::Type type, QPoint local) {
    const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, local, target->mapToGlobal(local), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

inline void start(QWidget* target, QPoint origin) {
    mouse(target, QEvent::MouseButtonPress, origin);
    mouse(target, QEvent::MouseMove, origin + QPoint(QApplication::startDragDistance(), 0));
}

template <typename Predicate> bool waitFor(Predicate ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 5000) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}

} // namespace history_drag_test
