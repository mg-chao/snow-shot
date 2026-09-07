#include "widgets/button.h"

#include <QApplication>
#include <QPointer>
#include <QWidget>

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void cursorOverlayMayBeDestroyedBeforeButton() {
  auto window = std::make_unique<QWidget>();
  window->resize(320, 200);
  auto* button = new adqt::widgets::AdButton(window.get());
  button->setGeometry(10, 10, 80, 32);
  window->show();
  button->setDisabled(true);
  QApplication::processEvents();
  QPointer<QWidget> overlay =
      window->findChild<QWidget*>(QStringLiteral("ad-button-disabled-cursor-overlay"));
  require(overlay != nullptr, "disabled button must create its cursor overlay");
  button->setEnabled(true);
  button->raise();
  require(window->children().indexOf(overlay) < window->children().indexOf(button),
          "raising the button must place its sibling overlay earlier in destruction order");
  window.reset();
  require(overlay.isNull(), "destroying the window must release the cursor overlay");
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  try {
    cursorOverlayMayBeDestroyedBeforeButton();
    std::cout << "Button lifetime tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
