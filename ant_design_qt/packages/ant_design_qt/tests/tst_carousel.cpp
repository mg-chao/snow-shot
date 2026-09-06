#include <QAbstractButton>
#include <QDir>
#include <QGridLayout>
#include <QImage>
#include <QMetaProperty>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

#include "theme/theme.h"
#include "widgets/carousel.h"
#include "widgets/carousel_style.h"

using adqt::widgets::AdCarousel;

namespace {

class TestSlide final : public QWidget {
 public:
  TestSlide(const QColor& color, const QSize& hint, QWidget* parent = nullptr)
      : QWidget(parent), color_(color), hint_(hint) {
    setAccessibleName(QStringLiteral("Test slide"));
  }

  QSize sizeHint() const override { return hint_; }
  QSize minimumSizeHint() const override { return hint_ / 2; }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), color_);
    painter.setPen(Qt::white);
    QFont display = font();
    display.setPixelSize(24);
    display.setBold(true);
    painter.setFont(display);
    painter.drawText(rect(), Qt::AlignCenter, accessibleName());
  }

 private:
  QColor color_;
  QSize hint_;
};

TestSlide* makeSlide(const QColor& color, const QString& name,
                     const QSize& hint = QSize(320, 180)) {
  auto* slide = new TestSlide(color, hint);
  slide->setAccessibleName(name);
  return slide;
}

QList<QAbstractButton*> arrows(AdCarousel* carousel) {
  return carousel->findChildren<QAbstractButton*>(QStringLiteral("ad-carousel-arrow"),
                                                  Qt::FindDirectChildrenOnly);
}

QList<QAbstractButton*> dots(AdCarousel* carousel) {
  return carousel->findChildren<QAbstractButton*>(QStringLiteral("ad-carousel-dot"),
                                                  Qt::FindDirectChildrenOnly);
}

}  // namespace

class CarouselTest final : public QObject {
  Q_OBJECT

 private slots:
  void defaultsAndPropertyMetadata();
  void initialSlideAppliesAsSlidesArrive();
  void slideOwnershipAndStableCurrentWidget();
  void externallyDestroyedSlidesAreRemoved();
  void navigationSignalsAndInfiniteBoundaries();
  void rapidCommandsRespectWaitForAnimation();
  void autoplayAdvancesAndStopsAtFiniteEnd();
  void autoplayPausesWhileFocusIsWithinCarousel();
  void keyboardDragDotsAndRtlArrowsNavigate();
  void indicatorKeysNavigateAndMoveFocus();
  void draggingPreservesInteractiveSlideControls();
  void placementControlsOrientationAndGeometry();
  void finiteArrowsExposeDisabledBoundaryState();
  void customArrowsFollowQtOwnershipRules();
  void accessibilityTextPreservesApplicationOverrides();
  void sizingAndComponentTokensResolve();
  void cancellingDragPreventsDeferredNavigation();
  void dynamicallyAddedSlideChildrenParticipateInDragging();
  void renderedStatesMatchAntDesignStructure();
};

void CarouselTest::defaultsAndPropertyMetadata() {
  AdCarousel carousel;
  QCOMPARE(carousel.count(), 0);
  QCOMPARE(carousel.initialSlide(), 0);
  QCOMPARE(carousel.currentIndex(), -1);
  QCOMPARE(carousel.currentWidget(), nullptr);
  QVERIFY(!carousel.arrowsVisible());
  QVERIFY(!carousel.autoplay());
  QCOMPARE(carousel.autoplayInterval(), 3000);
  QVERIFY(!carousel.autoplayProgressVisible());
  QVERIFY(!carousel.adaptiveHeight());
  QCOMPARE(carousel.dotPlacement(), AdCarousel::DotPlacement::Bottom);
  QVERIFY(carousel.dotsVisible());
  QVERIFY(!carousel.draggable());
  QCOMPARE(carousel.effect(), AdCarousel::Effect::Scroll);
  QVERIFY(carousel.infinite());
  QCOMPARE(carousel.transitionDuration(), 500);
  QCOMPARE(carousel.easingCurve().type(), QEasingCurve::Linear);
  QVERIFY(!carousel.waitForAnimation());
  QVERIFY(carousel.pauseOnHover());
  QVERIFY(carousel.pauseOnFocus());
  QVERIFY(!carousel.vertical());
  QVERIFY(!carousel.animationRunning());
  QVERIFY(!carousel.accessibleName().isEmpty());
  QVERIFY(carousel.previousArrowButton());
  QVERIFY(carousel.nextArrowButton());

  const QMetaObject* meta = carousel.metaObject();
  for (const char* name : {"count",
                           "initialSlide",
                           "currentIndex",
                           "currentWidget",
                           "arrowsVisible",
                           "autoplay",
                           "autoplayInterval",
                           "autoplayProgressVisible",
                           "adaptiveHeight",
                           "dotPlacement",
                           "dotsVisible",
                           "draggable",
                           "effect",
                           "infinite",
                           "transitionDuration",
                           "easingCurve",
                           "waitForAnimation",
                           "pauseOnHover",
                           "pauseOnFocus",
                           "vertical",
                           "animationRunning",
                           "previousArrowButton",
                           "nextArrowButton"}) {
    const int index = meta->indexOfProperty(name);
    QVERIFY2(index >= 0, name);
    const QMetaProperty property = meta->property(index);
    QVERIFY(property.isReadable());
    QVERIFY(property.hasNotifySignal());
  }
  QVERIFY(!meta->property(meta->indexOfProperty("count")).isWritable());
  QVERIFY(!meta->property(meta->indexOfProperty("currentWidget")).isWritable());
  QVERIFY(!meta->property(meta->indexOfProperty("vertical")).isWritable());
  QVERIFY(!meta->property(meta->indexOfProperty("animationRunning")).isWritable());
}

void CarouselTest::initialSlideAppliesAsSlidesArrive() {
  AdCarousel carousel;
  carousel.setTransitionDuration(0);
  carousel.setInitialSlide(2);
  QCOMPARE(carousel.initialSlide(), 2);

  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  QCOMPARE(carousel.currentIndex(), 0);
  carousel.addSlide(makeSlide(Qt::green, QStringLiteral("Two")));
  QCOMPARE(carousel.currentIndex(), 1);
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Three")));
  QCOMPARE(carousel.currentIndex(), 2);
  carousel.addSlide(makeSlide(Qt::yellow, QStringLiteral("Four")));
  QCOMPARE(carousel.currentIndex(), 2);

  carousel.clear();
  QCOMPARE(carousel.currentIndex(), -1);
  carousel.addSlide(makeSlide(Qt::cyan, QStringLiteral("Five")));
  carousel.addSlide(makeSlide(Qt::magenta, QStringLiteral("Six")));
  carousel.addSlide(makeSlide(Qt::gray, QStringLiteral("Seven")));
  QCOMPARE(carousel.currentIndex(), 2);

  carousel.clear();
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("Eight")));
  carousel.goTo(0, true);
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Nine")));
  QCOMPARE(carousel.currentIndex(), 0);
}

void CarouselTest::slideOwnershipAndStableCurrentWidget() {
  AdCarousel carousel;
  QSignalSpy countSpy(&carousel, &AdCarousel::countChanged);
  QSignalSpy indexSpy(&carousel, &AdCarousel::currentIndexChanged);
  QSignalSpy widgetSpy(&carousel, &AdCarousel::currentWidgetChanged);

  auto* blue = makeSlide(QColor(QStringLiteral("#1677ff")), QStringLiteral("Blue"));
  QCOMPARE(carousel.addSlide(blue), 0);
  QCOMPARE(blue->parentWidget()->objectName(), QStringLiteral("ad-carousel-viewport"));
  QCOMPARE(carousel.currentIndex(), 0);
  QCOMPARE(carousel.currentWidget(), blue);
  QCOMPARE(countSpy.count(), 1);
  QCOMPARE(indexSpy.count(), 1);
  QCOMPARE(widgetSpy.count(), 1);

  auto* green = makeSlide(QColor(QStringLiteral("#52c41a")), QStringLiteral("Green"));
  QCOMPARE(carousel.insertSlide(0, green), 0);
  QCOMPARE(carousel.currentIndex(), 1);
  QCOMPARE(carousel.currentWidget(), blue);
  QCOMPARE(carousel.indexOf(green), 0);
  QCOMPARE(carousel.addSlide(blue), -1);
  QCOMPARE(carousel.addSlide(&carousel), -1);

  QWidget* taken = carousel.takeSlide(0);
  QCOMPARE(taken, green);
  QVERIFY(!taken->parentWidget());
  QCOMPARE(carousel.currentIndex(), 0);
  QCOMPARE(carousel.currentWidget(), blue);
  delete taken;

  QPointer<QWidget> guard = blue;
  carousel.removeSlide(0);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(guard.isNull());
  QCOMPARE(carousel.count(), 0);
  QCOMPARE(carousel.currentIndex(), -1);
}

void CarouselTest::externallyDestroyedSlidesAreRemoved() {
  AdCarousel carousel;
  auto* first = makeSlide(Qt::red, QStringLiteral("First"));
  auto* second = makeSlide(Qt::green, QStringLiteral("Second"));
  auto* third = makeSlide(Qt::blue, QStringLiteral("Third"));
  carousel.addSlide(first);
  carousel.addSlide(second);
  carousel.addSlide(third);
  carousel.goTo(1, true);
  QSignalSpy countSpy(&carousel, &AdCarousel::countChanged);
  QSignalSpy widgetSpy(&carousel, &AdCarousel::currentWidgetChanged);

  delete first;
  QCOMPARE(carousel.count(), 2);
  QCOMPARE(carousel.currentIndex(), 0);
  QCOMPARE(carousel.currentWidget(), second);

  delete second;
  QCOMPARE(carousel.count(), 1);
  QCOMPARE(carousel.currentWidget(), third);
  QCOMPARE(countSpy.count(), 2);
  QCOMPARE(widgetSpy.count(), 1);
}

void CarouselTest::navigationSignalsAndInfiniteBoundaries() {
  AdCarousel carousel;
  carousel.setTransitionDuration(0);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::green, QStringLiteral("Two")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Three")));
  QSignalSpy beforeSpy(&carousel, &AdCarousel::beforeChange);
  QSignalSpy afterSpy(&carousel, &AdCarousel::afterChange);

  carousel.next();
  QCOMPARE(carousel.currentIndex(), 1);
  QCOMPARE(beforeSpy.takeFirst(), QVariantList({0, 1}));
  QCOMPARE(afterSpy.count(), 1);
  QCOMPARE(afterSpy.takeFirst().at(0).toInt(), 1);
  carousel.previous();
  QCOMPARE(carousel.currentIndex(), 0);
  carousel.previous();
  QCOMPARE(carousel.currentIndex(), 2);

  carousel.setInfinite(false);
  carousel.next();
  QCOMPARE(carousel.currentIndex(), 2);
  carousel.goTo(0, true);
  carousel.previous();
  QCOMPARE(carousel.currentIndex(), 0);
  carousel.goTo(50, true);
  QCOMPARE(carousel.currentIndex(), 2);
}

void CarouselTest::rapidCommandsRespectWaitForAnimation() {
  AdCarousel carousel;
  carousel.resize(360, 180);
  carousel.setTransitionDuration(80);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::green, QStringLiteral("Two")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Three")));
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  carousel.setWaitForAnimation(true);
  carousel.goTo(1);
  QVERIFY(carousel.animationRunning());
  carousel.goTo(2);
  QCOMPARE(carousel.currentIndex(), 1);
  QTRY_VERIFY_WITH_TIMEOUT(!carousel.animationRunning(), 250);

  carousel.setWaitForAnimation(false);
  carousel.goTo(0, true);
  carousel.goTo(1);
  carousel.goTo(2);
  QCOMPARE(carousel.currentIndex(), 2);
  QTRY_VERIFY_WITH_TIMEOUT(!carousel.animationRunning(), 250);
}

void CarouselTest::autoplayAdvancesAndStopsAtFiniteEnd() {
  AdCarousel carousel;
  carousel.resize(320, 160);
  carousel.setTransitionDuration(0);
  carousel.setAutoplayInterval(35);
  carousel.setAutoplayProgressVisible(true);
  carousel.setPauseOnHover(false);
  carousel.setPauseOnFocus(false);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::green, QStringLiteral("Two")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Three")));
  carousel.setAutoplay(true);
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));
  QTRY_COMPARE_WITH_TIMEOUT(carousel.currentIndex(), 1, 250);

  carousel.setInfinite(false);
  QTRY_COMPARE_WITH_TIMEOUT(carousel.currentIndex(), 2, 250);
  QTest::qWait(100);
  QCOMPARE(carousel.currentIndex(), 2);
  carousel.setAutoplay(false);
}

void CarouselTest::autoplayPausesWhileFocusIsWithinCarousel() {
  AdCarousel carousel;
  carousel.resize(320, 160);
  carousel.setTransitionDuration(0);
  carousel.setAutoplayInterval(35);
  carousel.setPauseOnHover(false);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Two")));
  carousel.setAutoplay(true);
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  dots(&carousel).first()->setFocus(Qt::TabFocusReason);
  QCOMPARE(QApplication::focusWidget(), dots(&carousel).first());
  QTest::qWait(100);
  QCOMPARE(carousel.currentIndex(), 0);

  dots(&carousel).first()->clearFocus();
  QTRY_COMPARE_WITH_TIMEOUT(carousel.currentIndex(), 1, 250);
  carousel.setAutoplay(false);
}

void CarouselTest::keyboardDragDotsAndRtlArrowsNavigate() {
  AdCarousel carousel;
  carousel.resize(360, 180);
  carousel.setTransitionDuration(0);
  carousel.setArrowsVisible(true);
  carousel.setDraggable(true);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::green, QStringLiteral("Two")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Three")));
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  carousel.setFocus(Qt::TabFocusReason);
  QTest::keyClick(&carousel, Qt::Key_Right);
  QCOMPARE(carousel.currentIndex(), 1);
  QTest::keyClick(&carousel, Qt::Key_Home);
  QCOMPARE(carousel.currentIndex(), 0);

  QWidget* visibleSlide = carousel.currentWidget();
  QVERIFY(visibleSlide);
  const QPoint center = visibleSlide->rect().center();
  QTest::mousePress(visibleSlide, Qt::LeftButton, Qt::NoModifier, center);
  QTest::mouseMove(visibleSlide, center - QPoint(70, 0));
  QTest::mouseRelease(visibleSlide, Qt::LeftButton, Qt::NoModifier, center - QPoint(70, 0));
  QCOMPARE(carousel.currentIndex(), 1);

  const QList<QAbstractButton*> dotButtons = dots(&carousel);
  QCOMPARE(dotButtons.size(), 3);
  QTest::mouseClick(dotButtons.at(2), Qt::LeftButton);
  QCOMPARE(carousel.currentIndex(), 2);

  carousel.goTo(1, true);
  carousel.setLayoutDirection(Qt::RightToLeft);
  QCoreApplication::processEvents();
  const QList<QAbstractButton*> arrowButtons = arrows(&carousel);
  QCOMPARE(arrowButtons.size(), 2);
  QCOMPARE(arrowButtons.at(0)->accessibleName(), QStringLiteral("Next slide"));
  QTest::mouseClick(arrowButtons.at(0), Qt::LeftButton);
  QCOMPARE(carousel.currentIndex(), 2);
}

void CarouselTest::indicatorKeysNavigateAndMoveFocus() {
  AdCarousel carousel;
  carousel.resize(360, 180);
  carousel.setTransitionDuration(0);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::green, QStringLiteral("Two")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Three")));
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  const QList<QAbstractButton*> dotButtons = dots(&carousel);
  dotButtons.at(0)->setFocus(Qt::TabFocusReason);
  QTest::keyClick(dotButtons.at(0), Qt::Key_Right);
  QCOMPARE(carousel.currentIndex(), 1);
  QCOMPARE(QApplication::focusWidget(), dotButtons.at(1));

  QTest::keyClick(dotButtons.at(1), Qt::Key_End);
  QCOMPARE(carousel.currentIndex(), 2);
  QCOMPARE(QApplication::focusWidget(), dotButtons.at(2));

  carousel.setLayoutDirection(Qt::RightToLeft);
  carousel.goTo(1, true);
  dotButtons.at(1)->setFocus(Qt::TabFocusReason);
  QTest::keyClick(dotButtons.at(1), Qt::Key_Left);
  QCOMPARE(carousel.currentIndex(), 2);
  QCOMPARE(QApplication::focusWidget(), dotButtons.at(2));

  carousel.setDotPlacement(AdCarousel::DotPlacement::Start);
  carousel.goTo(1, true);
  dotButtons.at(1)->setFocus(Qt::TabFocusReason);
  QTest::keyClick(dotButtons.at(1), Qt::Key_Up);
  QCOMPARE(carousel.currentIndex(), 0);
  QCOMPARE(QApplication::focusWidget(), dotButtons.at(0));
}

void CarouselTest::draggingPreservesInteractiveSlideControls() {
  AdCarousel carousel;
  carousel.resize(360, 180);
  carousel.setTransitionDuration(0);
  carousel.setDraggable(true);

  auto* first = new QWidget;
  auto* button = new QPushButton(QStringLiteral("Open"), first);
  button->setGeometry(120, 65, 120, 36);
  carousel.addSlide(first);
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Second")));
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  QSignalSpy clickSpy(button, &QPushButton::clicked);
  QTest::mouseClick(button, Qt::LeftButton);
  QCOMPARE(clickSpy.count(), 1);
  QCOMPARE(carousel.currentIndex(), 0);

  const QPoint start(30, first->height() / 2);
  QTest::mousePress(first, Qt::LeftButton, Qt::NoModifier, start);
  QTest::mouseMove(first, start - QPoint(70, 0));
  QTest::mouseRelease(first, Qt::LeftButton, Qt::NoModifier, start - QPoint(70, 0));
  QCOMPARE(carousel.currentIndex(), 1);
  QCOMPARE(clickSpy.count(), 1);
}

void CarouselTest::placementControlsOrientationAndGeometry() {
  AdCarousel carousel;
  carousel.resize(420, 220);
  carousel.setTransitionDuration(0);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::green, QStringLiteral("Two")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Three")));
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  const QList<QAbstractButton*> dotButtons = dots(&carousel);
  QCOMPARE(dotButtons.size(), 3);
  QVERIFY(dotButtons.at(0)->y() > carousel.height() / 2);

  QSignalSpy verticalSpy(&carousel, &AdCarousel::verticalChanged);
  carousel.setDotPlacement(AdCarousel::DotPlacement::Top);
  QVERIFY(dotButtons.at(0)->y() < carousel.height() / 2);
  QVERIFY(!carousel.vertical());
  carousel.setDotPlacement(AdCarousel::DotPlacement::Start);
  QVERIFY(carousel.vertical());
  QCOMPARE(verticalSpy.count(), 1);
  QVERIFY(dotButtons.at(0)->x() < carousel.width() / 2);
  carousel.setDotPlacement(AdCarousel::DotPlacement::End);
  QVERIFY(dotButtons.at(0)->x() > carousel.width() / 2);

  carousel.setLayoutDirection(Qt::RightToLeft);
  QCoreApplication::processEvents();
  QVERIFY(dotButtons.at(0)->x() < carousel.width() / 2);
  carousel.setDotPlacement(AdCarousel::DotPlacement::Bottom);
  QVERIFY(dotButtons.at(0)->x() > dotButtons.at(1)->x());
}

void CarouselTest::finiteArrowsExposeDisabledBoundaryState() {
  AdCarousel carousel;
  carousel.resize(360, 180);
  carousel.setTransitionDuration(0);
  carousel.setArrowsVisible(true);
  carousel.setInfinite(false);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Two")));
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  QCOMPARE(arrows(&carousel).size(), 2);
  QVERIFY(arrows(&carousel).at(0)->isVisible());
  QVERIFY(!arrows(&carousel).at(0)->isEnabled());
  QVERIFY(arrows(&carousel).at(1)->isEnabled());

  carousel.goTo(1, true);
  QVERIFY(arrows(&carousel).at(0)->isEnabled());
  QVERIFY(!arrows(&carousel).at(1)->isEnabled());
}

void CarouselTest::customArrowsFollowQtOwnershipRules() {
  AdCarousel carousel;
  QPointer<QAbstractButton> oldPrevious = carousel.previousArrowButton();
  auto* custom = new QPushButton(QStringLiteral("Back"));
  carousel.setPreviousArrowButton(custom);
  QCOMPARE(carousel.previousArrowButton(), custom);
  QCOMPARE(custom->parentWidget(), &carousel);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QVERIFY(oldPrevious.isNull());

  QSignalSpy changedSpy(&carousel, &AdCarousel::previousArrowButtonChanged);
  QAbstractButton* taken = carousel.takePreviousArrowButton();
  QCOMPARE(taken, custom);
  QVERIFY(!taken->parentWidget());
  QVERIFY(carousel.previousArrowButton());
  QVERIFY(carousel.previousArrowButton() != taken);
  QCOMPARE(changedSpy.count(), 1);
  QCOMPARE(changedSpy.first().at(0).value<QAbstractButton*>(), carousel.previousArrowButton());
  delete taken;

  QPointer<QAbstractButton> next = new QPushButton;
  carousel.setNextArrowButton(next);
  auto* replacement = new QPushButton;
  connect(&carousel, &AdCarousel::nextArrowButtonChanged, &carousel,
          [&carousel, replacement](QAbstractButton* button) {
            if (!button) carousel.setNextArrowButton(replacement);
          });
  delete next;
  QTRY_COMPARE(carousel.nextArrowButton(), replacement);
}

void CarouselTest::accessibilityTextPreservesApplicationOverrides() {
  AdCarousel carousel;
  QCOMPARE(carousel.accessibleName(), QStringLiteral("Carousel"));
  QCOMPARE(carousel.accessibleDescription(), QStringLiteral("No slides"));

  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("First")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Second")));
  QCOMPARE(carousel.accessibleDescription(), QStringLiteral("Slide 1 of 2"));
  QCOMPARE(dots(&carousel).at(1)->accessibleName(), QStringLiteral("Go to slide 2"));

  carousel.setAccessibleName(QStringLiteral("Featured offers"));
  carousel.setAccessibleDescription(QStringLiteral("Application-provided description"));
  carousel.goTo(1, true);
  QCOMPARE(carousel.accessibleName(), QStringLiteral("Featured offers"));
  QCOMPARE(carousel.accessibleDescription(), QStringLiteral("Application-provided description"));

  QEvent languageChange(QEvent::LanguageChange);
  QCoreApplication::sendEvent(&carousel, &languageChange);
  QCOMPARE(carousel.accessibleName(), QStringLiteral("Featured offers"));
  QCOMPARE(dots(&carousel).at(0)->accessibleName(), QStringLiteral("Go to slide 1"));
}

void CarouselTest::sizingAndComponentTokensResolve() {
  AdCarousel carousel;
  carousel.setTransitionDuration(0);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("Short"), QSize(280, 100)));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Tall"), QSize(280, 240)));
  QCOMPARE(carousel.sizeHint(), QSize(280, 240));
  carousel.goTo(1, true);
  QCOMPARE(carousel.sizeHint(), QSize(280, 240));
  carousel.goTo(0, true);
  carousel.setAdaptiveHeight(true);
  QCOMPARE(carousel.sizeHint().height(), 100);
  carousel.goTo(1, true);
  QCOMPARE(carousel.sizeHint().height(), 240);

  AdCarousel::ComponentTokens tokens;
  tokens.colors.arrowColor = QColor(QStringLiteral("#123456"));
  tokens.colors.dotColor = QColor(QStringLiteral("#abcdef"));
  tokens.metrics.arrowSize = 22;
  tokens.metrics.dotWidth = 18;
  tokens.metrics.dotActiveWidth = 30;
  tokens.metrics.dragThreshold = 17;
  carousel.setComponentTokens(tokens);
  const auto appearance =
      adqt::widgets::detail::resolveCarouselAppearance(&carousel, carousel.componentTokens());
  QCOMPARE(appearance.arrow, QColor(QStringLiteral("#123456")));
  QCOMPARE(appearance.dot, QColor(QStringLiteral("#abcdef")));
  QCOMPARE(appearance.metrics.arrowSize, 22);
  QCOMPARE(appearance.metrics.dotWidth, 18);
  QCOMPARE(appearance.metrics.dotActiveWidth, 30);
  QCOMPARE(appearance.metrics.dragThreshold, 17);
}

void CarouselTest::cancellingDragPreventsDeferredNavigation() {
  AdCarousel carousel;
  carousel.resize(360, 180);
  carousel.setTransitionDuration(0);
  carousel.setDraggable(true);
  carousel.addSlide(makeSlide(Qt::red, QStringLiteral("One")));
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Two")));
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  QWidget* slide = carousel.currentWidget();
  const QPoint start = slide->rect().center();
  QTest::mousePress(slide, Qt::LeftButton, Qt::NoModifier, start);
  carousel.setDraggable(false);
  QTest::mouseRelease(slide, Qt::LeftButton, Qt::NoModifier, start - QPoint(80, 0));
  QCOMPARE(carousel.currentIndex(), 0);
}

void CarouselTest::dynamicallyAddedSlideChildrenParticipateInDragging() {
  AdCarousel carousel;
  carousel.resize(360, 180);
  carousel.setTransitionDuration(0);
  carousel.setDraggable(true);
  auto* first = new QWidget;
  carousel.addSlide(first);
  carousel.addSlide(makeSlide(Qt::blue, QStringLiteral("Two")));
  carousel.show();
  QVERIFY(QTest::qWaitForWindowExposed(&carousel));

  auto* addedLater = new QWidget(first);
  addedLater->setGeometry(20, 20, 200, 100);
  addedLater->show();
  QCoreApplication::processEvents();
  const QPoint start = addedLater->rect().center();
  QTest::mousePress(addedLater, Qt::LeftButton, Qt::NoModifier, start);
  QTest::mouseMove(addedLater, start - QPoint(80, 0));
  QTest::mouseRelease(addedLater, Qt::LeftButton, Qt::NoModifier, start - QPoint(80, 0));
  QCOMPARE(carousel.currentIndex(), 1);
}

void CarouselTest::renderedStatesMatchAntDesignStructure() {
  auto& themeManager = adqt::theme::ThemeManager::instance();
  const adqt::theme::ThemeConfig original = themeManager.config();
  themeManager.applyTo(*qApp);

  QWidget showcase;
  showcase.resize(900, 560);
  auto* layout = new QGridLayout(&showcase);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(24);

  auto makeCarousel = [](AdCarousel::DotPlacement placement, AdCarousel::Effect effect) {
    auto* carousel = new AdCarousel;
    carousel->setMinimumSize(390, 230);
    carousel->setArrowsVisible(true);
    carousel->setDotPlacement(placement);
    carousel->setEffect(effect);
    carousel->setTransitionDuration(0);
    carousel->addSlide(makeSlide(QColor(QStringLiteral("#1677ff")), QStringLiteral("1")));
    carousel->addSlide(makeSlide(QColor(QStringLiteral("#13c2c2")), QStringLiteral("2")));
    carousel->addSlide(makeSlide(QColor(QStringLiteral("#722ed1")), QStringLiteral("3")));
    return carousel;
  };
  AdCarousel* horizontal =
      makeCarousel(AdCarousel::DotPlacement::Bottom, AdCarousel::Effect::Scroll);
  AdCarousel* vertical = makeCarousel(AdCarousel::DotPlacement::End, AdCarousel::Effect::Fade);
  vertical->setAutoplay(true);
  vertical->setAutoplayInterval(5000);
  vertical->setAutoplayProgressVisible(true);
  layout->addWidget(horizontal, 0, 0);
  layout->addWidget(vertical, 0, 1);

  const QString snapshotDirectory = qEnvironmentVariable("ADQT_CAROUSEL_SNAPSHOT_DIR");
  auto renderScheme = [&](adqt::theme::ThemeScheme scheme, const QString& fileName) {
    themeManager.setPreset(scheme, adqt::theme::ThemeDensity::Comfortable);
    const auto colors = themeManager.resolveTheme(&showcase);
    QPalette palette = showcase.palette();
    palette.setColor(QPalette::Window, colors.colorBgContainer);
    showcase.setPalette(palette);
    showcase.setAutoFillBackground(true);
    showcase.show();
    QCoreApplication::processEvents();
    QTest::qWait(50);

    const QImage image = showcase.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    QVERIFY(!image.isNull());
    QCOMPARE(image.deviceIndependentSize().toSize(), showcase.size());
    QSet<QRgb> sampledColors;
    int saturatedSurfacePixels = 0;
    for (int y = 0; y < image.height(); y += 8) {
      for (int x = 0; x < image.width(); x += 8) {
        const QRgb pixel = image.pixel(x, y);
        sampledColors.insert(pixel);
        const QColor color = QColor::fromRgba(pixel);
        if (color.blue() > color.red() + 20 || color.green() > color.red() + 20) {
          ++saturatedSurfacePixels;
        }
      }
    }
    QVERIFY2(sampledColors.size() > 3,
             "Carousel snapshot should contain slide surfaces, arrows, dots, and text");
    QVERIFY2(saturatedSurfacePixels > 500,
             "Carousel snapshot should contain a substantial visible slide surface");
    for (QAbstractButton* arrow : arrows(horizontal)) {
      QVERIFY(arrow->isVisible());
      QVERIFY(!arrow->accessibleName().isEmpty());
    }
    QCOMPARE(dots(horizontal).size(), 3);
    QCOMPARE(dots(vertical).size(), 3);
    if (!snapshotDirectory.isEmpty()) {
      QDir().mkpath(snapshotDirectory);
      QVERIFY(image.save(QDir(snapshotDirectory).filePath(fileName)));
    }
  };

  renderScheme(adqt::theme::ThemeScheme::Light, QStringLiteral("carousel-light.png"));
  renderScheme(adqt::theme::ThemeScheme::Dark, QStringLiteral("carousel-dark.png"));
  themeManager.setConfig(original);
}

QTEST_MAIN(CarouselTest)

#include "tst_carousel.moc"
