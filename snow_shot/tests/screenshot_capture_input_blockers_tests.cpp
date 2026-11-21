#include "../src/presentation/capture/screenshotcaptureinputblockers.h"

#include "widgets/modal.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

#include <cstdlib>
#include <iostream>

namespace {
using snow_shot::presentation::prepareCaptureInput;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

class RefusingWindow final : public QWidget {
  protected:
    void closeEvent(QCloseEvent* event) override {
        event->ignore();
    }
};

class MouseTrackingOverlay final : public QWidget {
  public:
    MouseTrackingOverlay() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint) {
        setMouseTracking(true);
        setGeometry(20, 20, 100, 100);
    }

    int mouseMoves = 0;

  protected:
    void mouseMoveEvent(QMouseEvent*) override {
        ++mouseMoves;
    }
};

void sendWindowMouseMove(QWidget& widget) {
    const QPointF local(10, 10);
    QMouseEvent event(QEvent::MouseMove, local, widget.mapToGlobal(local.toPoint()), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    // Exercise Qt's window-to-widget routing, including active popup redirection.
    QApplication::sendEvent(widget.windowHandle(), &event);
}

void popupReleasesOverlayMouseInput() {
    QMenu menu;
    menu.addAction(QStringLiteral("Capture"));
    menu.popup(QPoint(200, 200));
    MouseTrackingOverlay overlay;
    overlay.show();
    QApplication::processEvents();
    require(QApplication::activePopupWidget() == &menu &&
                QApplication::activeModalWidget() == nullptr,
            "a tray-style menu owns popup input without being a modal dialog");

    overlay.mouseMoves = 0;
    sendWindowMouseMove(overlay);
    require(overlay.mouseMoves == 0,
            "an open popup redirects mouse movement away from the overlay");
    require(prepareCaptureInput(), "capture preparation should release popup input");
    sendWindowMouseMove(overlay);
    require(overlay.mouseMoves == 1,
            "capture preparation must restore mouse movement to the screenshot overlay");
    require(!menu.isVisible() && QApplication::activePopupWidget() == nullptr,
            "capture preparation must dismiss the popup before showing overlays");
}

void nestedMenusReleaseInputAndPreserveTheOwnerDialog() {
    QWidget source;
    source.show();
    adqt::widgets::AdModal modal(&source);
    modal.setMode(adqt::widgets::AdModal::Mode::Window);
    auto* draft = new QLineEdit;
    draft->setText(QStringLiteral("unsaved edits"));
    modal.setContentWidget(draft);
    modal.open();
    QWidget* surface = draft->window();
    QMenu menu(surface);
    auto* child = menu.addMenu(QStringLiteral("Child"));
    child->addAction(QStringLiteral("Item"));
    menu.popup(QPoint(200, 200));
    child->popup(QPoint(300, 200));
    require(QApplication::activePopupWidget() == child, "nested menu owns popup input");

    require(prepareCaptureInput(), "an owned modal must allow an independent capture");
    require(!child->isVisible() && !menu.isVisible() && !QApplication::activePopupWidget(),
            "capture dismisses every menu that redirects pointer input");

    MouseTrackingOverlay overlay;
    overlay.show();
    QApplication::processEvents();
    require(adqt::widgets::detail::blockingModalWindow(source.windowHandle()) &&
                !adqt::widgets::detail::blockingModalWindow(overlay.windowHandle()),
            "the dialog continues to block only its source window");
    overlay.mouseMoves = 0;
    sendWindowMouseMove(overlay);
    require(overlay.mouseMoves == 1, "the independent screenshot overlay receives pointer input");
    overlay.close();
    require(modal.isOpen() && surface->isVisible() &&
                draft->text() == QStringLiteral("unsaved edits"),
            "finishing capture preserves the original dialog and its draft");
}

void applicationModalUnderNestedWindowModalPreventsCapture() {
    QWidget global(nullptr, Qt::Dialog);
    global.setWindowModality(Qt::ApplicationModal);
    global.show();
    QWidget nested(&global, Qt::Dialog);
    nested.setWindowModality(Qt::WindowModal);
    nested.show();
    require(QApplication::activeModalWidget() == &nested,
            "the nested window modal is on top of the application modal");
    require(!prepareCaptureInput() && global.isVisible() && nested.isVisible(),
            "capture respects an application-wide blocker anywhere in the modal stack");
    nested.close();
    require(!prepareCaptureInput() && global.isVisible(),
            "capture must leave the application-modal decision to the user");
    global.close();
    require(prepareCaptureInput(), "capture is available after the user closes the dialog");
}

void popupCloseHandlerCanOpenAnApplicationModal() {
    QWindow modal;
    modal.setModality(Qt::ApplicationModal);
    QMenu menu;
    menu.addAction(QStringLiteral("Item"));
    QObject::connect(&menu, &QMenu::aboutToHide, &modal, [&modal] { modal.show(); });
    menu.popup(QPoint(200, 200));
    require(!prepareCaptureInput() && !menu.isVisible() && modal.isVisible(),
            "capture rechecks Qt modality after closing menus, including non-widget dialogs");
}

void refusingPopupPreventsCapture() {
    RefusingWindow popup;
    popup.setWindowFlags(Qt::Popup);
    popup.show();
    require(QApplication::activePopupWidget() == &popup,
            "the refusing window must own popup input");
    require(!prepareCaptureInput(), "capture must not start when a popup refuses to close");
    require(popup.isVisible() && QApplication::activePopupWidget() == &popup,
            "a refused close must leave the popup usable");
    popup.hide();
}
} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    popupReleasesOverlayMouseInput();
    nestedMenusReleaseInputAndPreserveTheOwnerDialog();
    applicationModalUnderNestedWindowModalPreventsCapture();
    popupCloseHandlerCanOpenAnApplicationModal();
    refusingPopupPreventsCapture();
    return 0;
}
