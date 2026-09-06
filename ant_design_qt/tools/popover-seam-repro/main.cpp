#include "widgets/button.h"
#include "widgets/combo_box.h"
#include "widgets/popover.h"
#include "widgets/popup_placement.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <optional>

namespace {

struct SeamRunConfig {
  int seamX = 0;
  QString surfaceObjectName;
  QString label;
};

struct SeamRunResult {
  bool sawSurface = false;
  bool sawJump = false;
  int maxStepDelta = 0;
  int maxPositionError = 0;
};

void pumpEvents(int durationMs = 80) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  } while (timer.elapsed() < durationMs);
}

std::optional<int> adjacentScreenSeamX() {
  QList<QScreen*> screens = QGuiApplication::screens();
  std::sort(screens.begin(), screens.end(), [](const QScreen* lhs, const QScreen* rhs) {
    return lhs->geometry().left() < rhs->geometry().left();
  });

  for (int i = 0; i + 1 < screens.size(); ++i) {
    const QRect left = screens.at(i)->geometry();
    const QRect right = screens.at(i + 1)->geometry();
    if (left.right() + 1 == right.left()) {
      return right.left();
    }
  }
  return std::nullopt;
}

void printScreens() {
  const QList<QScreen*> screens = QGuiApplication::screens();
  for (QScreen* screen : screens) {
    std::cout << "screen name=" << screen->name().toStdString()
              << " geometry=" << screen->geometry().x() << "," << screen->geometry().y() << " "
              << screen->geometry().width() << "x" << screen->geometry().height()
              << " available=" << screen->availableGeometry().x() << ","
              << screen->availableGeometry().y() << " " << screen->availableGeometry().width()
              << "x" << screen->availableGeometry().height()
              << " dpr=" << screen->devicePixelRatio() << '\n';
  }
}

QRect widgetGlobalRect(const QWidget* widget) {
  if (!widget) {
    return QRect();
  }
  return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
}

QWidget* findVisibleSurface(const QString& objectName) {
  QWidget* hiddenCandidate = nullptr;
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget* widget : widgets) {
    if (!widget || widget->objectName() != objectName) {
      continue;
    }
    if (widget->isVisible()) {
      return widget;
    }
    if (!hiddenCandidate) {
      hiddenCandidate = widget;
    }
  }
  return hiddenCandidate;
}

bool intersectsAnyScreen(const QRect& globalRect) {
  if (!globalRect.isValid()) {
    return false;
  }
  const QList<QScreen*> screens = QGuiApplication::screens();
  for (QScreen* screen : screens) {
    if (screen && screen->availableGeometry().intersects(globalRect)) {
      return true;
    }
  }
  return false;
}

adqt::widgets::detail::OverlayPopupPlacement toOverlayPlacement(
    adqt::widgets::AdPopover::Placement placement) {
  using Placement = adqt::widgets::AdPopover::Placement;
  using OverlayPlacement = adqt::widgets::detail::OverlayPopupPlacement;
  switch (placement) {
    case Placement::Top:
      return OverlayPlacement::Top;
    case Placement::TopLeft:
      return OverlayPlacement::TopLeft;
    case Placement::TopRight:
      return OverlayPlacement::TopRight;
    case Placement::Bottom:
      return OverlayPlacement::Bottom;
    case Placement::BottomLeft:
      return OverlayPlacement::BottomLeft;
    case Placement::BottomRight:
      return OverlayPlacement::BottomRight;
    case Placement::Left:
      return OverlayPlacement::Left;
    case Placement::LeftTop:
      return OverlayPlacement::LeftTop;
    case Placement::LeftBottom:
      return OverlayPlacement::LeftBottom;
    case Placement::Right:
      return OverlayPlacement::Right;
    case Placement::RightTop:
      return OverlayPlacement::RightTop;
    case Placement::RightBottom:
      return OverlayPlacement::RightBottom;
  }
  return OverlayPlacement::Top;
}

SeamRunResult openClosePopoverAcrossSeam(QWidget* window, QWidget* trigger,
                                         adqt::widgets::AdPopover* popover,
                                         const SeamRunConfig& config) {
  const int y = std::max(320, QGuiApplication::primaryScreen()->availableGeometry().top() + 420);
  const int startX = config.seamX + 220;
  const int endX = config.seamX - 720;
  const int step = -20;
  constexpr int kPositionTolerancePx = 8;

  SeamRunResult result;

  for (int x = startX; x >= endX; x += step) {
    window->move(x, y);
    window->show();
    window->raise();
    pumpEvents(120);

    popover->setVisible(true);
    pumpEvents(140);

    QWidget* surface = findVisibleSurface(config.surfaceObjectName);
    const QRect triggerGlobalRect = widgetGlobalRect(trigger);
    if (!surface || !surface->isVisible()) {
      std::cout << config.label.toStdString() << " x=" << x
                << " surface=missing triggerGlobal=" << triggerGlobalRect.x() << ","
                << triggerGlobalRect.y() << " " << triggerGlobalRect.width() << "x"
                << triggerGlobalRect.height() << '\n';
      result.sawJump = true;
      popover->setVisible(false);
      pumpEvents(80);
      continue;
    }

    result.sawSurface = true;
    const QRect popupGlobalRect = widgetGlobalRect(surface);

    adqt::widgets::detail::OverlayPopupPlacementInput placementInput;
    placementInput.anchorRect = triggerGlobalRect;
    placementInput.popupSize = surface->size();
    placementInput.bounds =
        adqt::widgets::detail::popupScreenBoundsInGlobal(trigger, triggerGlobalRect);
    placementInput.preferredPlacement = toOverlayPlacement(popover->placement());
    placementInput.popupOffset = std::max(0, popover->popupOffset());
    placementInput.allowFallback = popover->autoAdjustOverflow();
    placementInput.pointAtCenter = popover->arrowPointAtCenter();
    const adqt::widgets::detail::OverlayPopupPlacementOutput expectedPlacement =
        adqt::widgets::detail::resolveOverlayPopupPlacement(placementInput);

    const QPoint delta = popupGlobalRect.topLeft() - expectedPlacement.topLeft;
    const int positionError = std::max(std::abs(delta.x()), std::abs(delta.y()));
    result.maxPositionError = std::max(result.maxPositionError, positionError);

    const bool visibleOnScreen = intersectsAnyScreen(popupGlobalRect);
    const bool positionOk = positionError <= kPositionTolerancePx;
    if (!visibleOnScreen || !positionOk) {
      result.sawJump = true;
    }

    std::cout << config.label.toStdString() << " x=" << x << " popupGlobal=" << popupGlobalRect.x()
              << "," << popupGlobalRect.y() << " popupSize=" << surface->width() << "x"
              << surface->height() << " expectedGlobal=" << expectedPlacement.topLeft.x() << ","
              << expectedPlacement.topLeft.y() << " delta=" << delta.x() << "," << delta.y()
              << " triggerGlobal=" << triggerGlobalRect.x() << "," << triggerGlobalRect.y() << " "
              << triggerGlobalRect.width() << "x" << triggerGlobalRect.height()
              << " surfaceIsWindow=" << (surface->isWindow() ? "true" : "false")
              << " visibleOnScreen=" << (visibleOnScreen ? "true" : "false")
              << " positionError=" << positionError << '\n';

    popover->setVisible(false);
    pumpEvents(100);

    QWidget* stillVisible = findVisibleSurface(config.surfaceObjectName);
    if (stillVisible && stillVisible->isVisible()) {
      std::cout << config.label.toStdString() << " x=" << x << " close=still-visible\n";
      result.sawJump = true;
    }
  }

  window->hide();
  pumpEvents(20);
  return result;
}

SeamRunResult moveWindowAcrossSeam(QWidget* window, QWidget* trigger, const SeamRunConfig& config,
                                   const std::function<void()>& keepOpen) {
  const int y = std::max(80, QGuiApplication::primaryScreen()->availableGeometry().top() + 180);
  const int startX = config.seamX - 720;
  const int endX = config.seamX + 220;
  const int step = 20;

  SeamRunResult result;
  QPoint previousPopupLocal;
  bool havePrevious = false;

  for (int x = startX; x <= endX; x += step) {
    window->move(x, y);
    window->show();
    window->raise();
    if (keepOpen) {
      keepOpen();
    }
    pumpEvents();

    QWidget* surface = window->findChild<QWidget*>(config.surfaceObjectName);
    if ((!surface || !surface->isVisible()) && keepOpen) {
      keepOpen();
      pumpEvents();
      surface = window->findChild<QWidget*>(config.surfaceObjectName);
    }
    const QRect triggerLocalRect(trigger->mapTo(window, QPoint(0, 0)), trigger->size());
    if (!surface || !surface->isVisible()) {
      std::cout << config.label.toStdString() << " x=" << x
                << " surface=missing triggerLocal=" << triggerLocalRect.x() << ","
                << triggerLocalRect.y() << " " << triggerLocalRect.width() << "x"
                << triggerLocalRect.height() << '\n';
      result.sawJump = true;
      havePrevious = false;
      continue;
    }

    result.sawSurface = true;
    const QPoint popupLocal = surface->pos();
    const QPoint popupGlobal = surface->mapToGlobal(QPoint(0, 0));
    const QRect popupLocalRect(popupLocal, surface->size());
    const int anchorDeltaX = popupLocalRect.center().x() - triggerLocalRect.center().x();
    const int anchorGapY = triggerLocalRect.top() - popupLocalRect.bottom() - 1;

    int stepDelta = 0;
    if (havePrevious) {
      stepDelta = std::max(std::abs(popupLocal.x() - previousPopupLocal.x()),
                           std::abs(popupLocal.y() - previousPopupLocal.y()));
      result.maxStepDelta = std::max(result.maxStepDelta, stepDelta);
      if (stepDelta > 3) {
        result.sawJump = true;
      }
    }
    previousPopupLocal = popupLocal;
    havePrevious = true;

    std::cout << config.label.toStdString() << " x=" << x << " popupLocal=" << popupLocal.x() << ","
              << popupLocal.y() << " popupGlobal=" << popupGlobal.x() << "," << popupGlobal.y()
              << " popupSize=" << surface->width() << "x" << surface->height()
              << " triggerLocal=" << triggerLocalRect.x() << "," << triggerLocalRect.y() << " "
              << triggerLocalRect.width() << "x" << triggerLocalRect.height()
              << " anchorDeltaX=" << anchorDeltaX << " anchorGapY=" << anchorGapY
              << " stepDelta=" << stepDelta << '\n';
  }

  window->hide();
  pumpEvents(20);
  return result;
}

int resultCodeForScenario(const QString& label, const SeamRunResult& result) {
  std::cout << label.toStdString()
            << " result sawSurface=" << (result.sawSurface ? "true" : "false")
            << " maxStepDelta=" << result.maxStepDelta
            << " maxPositionError=" << result.maxPositionError << '\n';
  if (!result.sawSurface) {
    std::cout << "FAIL " << label.toStdString() << " surface was never visible\n";
    return 2;
  }
  if (result.sawJump) {
    std::cout << "FAIL " << label.toStdString()
              << " position or visibility was incorrect while crossing the monitor seam\n";
    return 1;
  }
  std::cout << "PASS " << label.toStdString()
            << " position and visibility stayed correct across the monitor seam\n";
  return 0;
}

int runPartiallyOffscreenAnchorScenario() {
  constexpr int kAnchorWidth = 200;
  constexpr int kAnchorHeight = 40;
  constexpr int kVisiblePixels = 4;

  for (QScreen* screen : QGuiApplication::screens()) {
    if (!screen) {
      continue;
    }
    const QRect screenGeometry = screen->geometry();
    const int centeredX = screenGeometry.center().x() - kAnchorWidth / 2;
    const int centeredY = screenGeometry.center().y() - kAnchorHeight / 2;
    const QList<QRect> candidates = {
        QRect(screenGeometry.left() - kAnchorWidth + kVisiblePixels, centeredY, kAnchorWidth,
              kAnchorHeight),
        QRect(screenGeometry.right() - kVisiblePixels + 1, centeredY, kAnchorWidth, kAnchorHeight),
        QRect(centeredX, screenGeometry.top() - kAnchorHeight + kVisiblePixels, kAnchorWidth,
              kAnchorHeight),
        QRect(centeredX, screenGeometry.bottom() - kVisiblePixels + 1, kAnchorWidth, kAnchorHeight),
    };

    for (const QRect& anchorRect : candidates) {
      if (QGuiApplication::screenAt(anchorRect.center()) ||
          !screenGeometry.intersects(anchorRect)) {
        continue;
      }
      QScreen* selectedScreen =
          adqt::widgets::detail::popupScreenForGlobalRect(nullptr, anchorRect);
      std::cout << "partially-offscreen-anchor anchor=" << anchorRect.x() << "," << anchorRect.y()
                << " " << anchorRect.width() << "x" << anchorRect.height()
                << " selected=" << (selectedScreen ? selectedScreen->name().toStdString() : "none")
                << " expected=" << screen->name().toStdString() << '\n';
      if (selectedScreen != screen) {
        std::cout << "FAIL partially-offscreen anchor selected the wrong screen\n";
        return 1;
      }
      std::cout << "PASS partially-offscreen anchor selected its intersecting screen\n";
      return 0;
    }
  }

  std::cout << "SKIP no screen edge with an off-screen center was found\n";
  return 0;
}

int runPopoverScenario(int seamX) {
  QWidget window;
  window.setWindowTitle(QStringLiteral("Popover seam repro"));
  window.resize(560, 360);

  auto* root = new QVBoxLayout(&window);
  root->setContentsMargins(40, 40, 40, 40);
  root->setSpacing(16);
  root->addWidget(
      new QLabel(QStringLiteral("The button below opens a QtTool popover after each move.")));

  auto* row = new QHBoxLayout();
  row->addSpacing(180);
  auto* trigger = new adqt::widgets::AdButton(QStringLiteral("Open popover"));
  trigger->setButtonStyle(adqt::widgets::AdButton::ButtonStyle::Outline);
  row->addWidget(trigger, 0, Qt::AlignLeft);
  row->addStretch();
  root->addLayout(row);
  root->addStretch();

  adqt::widgets::AdPopover popover(trigger);
  popover.setSourceWidget(trigger);
  popover.setTitle(QStringLiteral("Placement"));
  popover.setText(
      QStringLiteral("Moving across adjacent monitors should keep this popup visible."));
  popover.setPlacement(adqt::widgets::AdPopover::Placement::Top);
  popover.setPopupLayerMode(adqt::widgets::AdPopover::PopupLayerMode::QtTool);
  popover.setAutoAdjustOverflow(true);

  const SeamRunResult result = openClosePopoverAcrossSeam(
      &window, trigger, &popover,
      SeamRunConfig{seamX, QStringLiteral("adpopover-surface"), QStringLiteral("popover-qttool")});
  return resultCodeForScenario(QStringLiteral("popover"), result);
}

int runSelectScenario(int seamX) {
  QWidget window;
  window.setWindowTitle(QStringLiteral("Select seam repro"));
  window.resize(560, 360);

  auto* root = new QVBoxLayout(&window);
  root->setContentsMargins(40, 40, 40, 40);
  root->setSpacing(16);
  root->addWidget(
      new QLabel(QStringLiteral("The combo box below owns a visible in-window popup.")));

  auto* row = new QHBoxLayout();
  row->addSpacing(180);
  auto* select = new adqt::widgets::AdComboBox();
  select->setMinimumWidth(180);
  select->setPopupLayerMode(adqt::widgets::AdComboBox::PopupLayerMode::InWindow);

  QVector<adqt::widgets::AdComboBox::Option> options;
  for (const QString& label : {QStringLiteral("Alpha"), QStringLiteral("Beta"),
                               QStringLiteral("Gamma"), QStringLiteral("Delta")}) {
    adqt::widgets::AdComboBox::Option option;
    option.value = label.toLower();
    option.label = label;
    options.append(option);
  }
  select->setOptions(options);
  select->setCurrentIndex(0);

  row->addWidget(select, 0, Qt::AlignLeft);
  row->addStretch();
  root->addLayout(row);
  root->addStretch();

  const SeamRunResult result = moveWindowAcrossSeam(
      &window, select,
      SeamRunConfig{seamX, QStringLiteral("adselect-popup"), QStringLiteral("select")},
      [select]() { select->setPopupVisible(true); });
  return resultCodeForScenario(QStringLiteral("select"), result);
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  printScreens();
  int exitCode = runPartiallyOffscreenAnchorScenario();
  const std::optional<int> seamX = adjacentScreenSeamX();
  if (!seamX.has_value()) {
    std::cout << "SKIP no adjacent horizontal screen seam found\n";
    return exitCode;
  }

  exitCode = std::max(exitCode, runPopoverScenario(seamX.value()));
  exitCode = std::max(exitCode, runSelectScenario(seamX.value()));
  return exitCode;
}
