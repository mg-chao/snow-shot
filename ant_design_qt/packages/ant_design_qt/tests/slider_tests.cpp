#include <QApplication>
#include <QImage>
#include <QTest>

#include <algorithm>

#include "widgets/slider.h"
#include "widgets/tooltip.h"

using adqt::widgets::AdMultiSlider;
using adqt::widgets::AdRangeSlider;
using adqt::widgets::AdSlider;

namespace {

constexpr int kAxisInset = 16;
constexpr int kMinimumThumbSize = 10;

int visualRailStartPixel(int extent) {
  const qreal maximumInset = std::max<qreal>(0.0, (extent - 1.0) / 2.0);
  const qreal handleInset = std::min<qreal>(kAxisInset, maximumInset);
  return qCeil(std::max<qreal>(0.0, handleInset - kMinimumThumbSize / 2.0));
}

int visualRailEndPixel(int extent) {
  const qreal maximumInset = std::max<qreal>(0.0, (extent - 1.0) / 2.0);
  const qreal handleInset = std::min<qreal>(kAxisInset, maximumInset);
  const qreal visualEnd = std::min<qreal>(extent, extent - handleInset + kMinimumThumbSize / 2.0);
  return qFloor(visualEnd) - 1;
}

void applyTestStyle(AdMultiSlider* slider) {
  AdMultiSlider::ComponentTokens tokens;
  tokens.railSize = 6;
  tokens.handleSize = kMinimumThumbSize;
  tokens.handleSizeHover = kMinimumThumbSize;
  tokens.handleLineWidth = 2;
  tokens.handleLineWidthHover = 2;
  tokens.focusOutlineSize = 0;
  tokens.marginMain = kAxisInset;
  tokens.railBg = QColor(0, 220, 0);
  tokens.railHoverBg = tokens.railBg;
  tokens.trackBg = QColor(230, 0, 0);
  tokens.trackHoverBg = tokens.trackBg;
  tokens.handleColor = QColor(0, 0, 230);
  tokens.handleActiveColor = tokens.handleColor;
  slider->setComponentTokens(tokens);

  AdMultiSlider::SemanticStyles styles;
  styles.root.backgroundColor = QColor(Qt::white);
  styles.handle.backgroundColor = QColor(250, 220, 0);
  slider->setSemanticStyles(styles);
  slider->setTooltipEnabled(false);
}

void hideTestHandles(AdMultiSlider* slider) {
  AdMultiSlider::SemanticStyles styles = slider->semanticStyles();
  styles.handle.backgroundColor = QColor(Qt::transparent);
  styles.handle.borderColor = QColor(Qt::transparent);
  slider->setSemanticStyles(styles);
}

QWidget* topLevelTooltipSurface() {
  for (QWidget* widget : QApplication::topLevelWidgets()) {
    if (widget && widget->objectName() == QStringLiteral("adtooltip-surface")) {
      return widget;
    }
  }
  return nullptr;
}

QImage renderSlider(QWidget* slider) {
  slider->show();
  QCoreApplication::processEvents();
  return slider->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

bool isRailPixel(const QColor& color) {
  return color.green() > color.red() + 50 && color.green() > color.blue() + 50;
}

bool isTrackPixel(const QColor& color) {
  return color.red() > color.green() + 50 && color.red() > color.blue() + 50;
}

bool isHandleFillPixel(const QColor& color) {
  return color.red() > 180 && color.green() > 150 && color.blue() < 80;
}

}  // namespace

class SliderTest final : public QObject {
  Q_OBJECT

 private slots:
  void horizontalRailIsVisualAndMovementIsLogical();
  void verticalRailIsVisualAndMovementIsLogical();
  void reversedMaximumFillsVisualRail();
  void rangeTrackUsesExplicitVisualCaps();
  void rightToLeftAxisPreservesVisualCapsAndInputDirection();
  void rightToLeftSelectionDragFollowsValueDirection();
  void tinyControlClampsHandleInset();
  void hoverTooltipUsesInputTransparentTopLevelSurface();
};

void SliderTest::horizontalRailIsVisualAndMovementIsLogical() {
  AdSlider slider;
  slider.resize(200, 40);
  applyTestStyle(&slider);
  slider.setValue(slider.minimum());

  hideTestHandles(&slider);
  QImage image = renderSlider(&slider);
  const int centerY = image.height() / 2;
  const int railStart = visualRailStartPixel(image.width());
  const int railEnd = visualRailEndPixel(image.width());
  QVERIFY(!isRailPixel(image.pixelColor(0, centerY)));
  QVERIFY(isRailPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isRailPixel(image.pixelColor(railEnd, centerY)));
  QVERIFY(!isRailPixel(image.pixelColor(image.width() - 1, centerY)));

  applyTestStyle(&slider);
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(kAxisInset, centerY)));
  QVERIFY(!isHandleFillPixel(image.pixelColor(0, centerY)));

  QTest::mouseClick(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(railEnd, slider.height() / 2));
  QCOMPARE(slider.value(), slider.maximum());
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(slider.width() - kAxisInset, centerY)));
  hideTestHandles(&slider);
  image = renderSlider(&slider);
  QVERIFY(!isTrackPixel(image.pixelColor(0, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railEnd, centerY)));
  QVERIFY(!isTrackPixel(image.pixelColor(image.width() - 1, centerY)));

  QTest::mouseClick(&slider, Qt::LeftButton, Qt::NoModifier,
                    QPoint(railStart, slider.height() / 2));
  QCOMPARE(slider.value(), slider.minimum());
}

void SliderTest::verticalRailIsVisualAndMovementIsLogical() {
  AdSlider slider;
  slider.setOrientation(Qt::Vertical);
  slider.resize(40, 200);
  applyTestStyle(&slider);
  slider.setValue(slider.minimum());

  hideTestHandles(&slider);
  QImage image = renderSlider(&slider);
  const int centerX = image.width() / 2;
  const int railStart = visualRailStartPixel(image.height());
  const int railEnd = visualRailEndPixel(image.height());
  QVERIFY(!isRailPixel(image.pixelColor(centerX, 0)));
  QVERIFY(isRailPixel(image.pixelColor(centerX, railStart)));
  QVERIFY(isRailPixel(image.pixelColor(centerX, railEnd)));
  QVERIFY(!isRailPixel(image.pixelColor(centerX, image.height() - 1)));

  applyTestStyle(&slider);
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(centerX, slider.height() - kAxisInset)));
  QVERIFY(!isHandleFillPixel(image.pixelColor(centerX, image.height() - 1)));

  QTest::mouseClick(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(centerX, railStart));
  QCOMPARE(slider.value(), slider.maximum());
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(centerX, kAxisInset)));
  hideTestHandles(&slider);
  image = renderSlider(&slider);
  QVERIFY(!isTrackPixel(image.pixelColor(centerX, 0)));
  QVERIFY(isTrackPixel(image.pixelColor(centerX, railStart)));
  QVERIFY(isTrackPixel(image.pixelColor(centerX, railEnd)));
  QVERIFY(!isTrackPixel(image.pixelColor(centerX, image.height() - 1)));

  QTest::mouseClick(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(centerX, railEnd));
  QCOMPARE(slider.value(), slider.minimum());
}

void SliderTest::reversedMaximumFillsVisualRail() {
  AdSlider slider;
  slider.resize(200, 40);
  slider.setInvertedAppearance(true);
  applyTestStyle(&slider);
  slider.setValue(slider.maximum());

  hideTestHandles(&slider);
  QImage image = renderSlider(&slider);
  const int centerY = image.height() / 2;
  const int railStart = visualRailStartPixel(image.width());
  const int railEnd = visualRailEndPixel(image.width());
  QVERIFY(!isTrackPixel(image.pixelColor(0, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railEnd, centerY)));
  QVERIFY(!isTrackPixel(image.pixelColor(image.width() - 1, centerY)));

  applyTestStyle(&slider);
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(kAxisInset, centerY)));
}

void SliderTest::rangeTrackUsesExplicitVisualCaps() {
  AdRangeSlider slider;
  slider.resize(200, 40);
  applyTestStyle(&slider);

  slider.setValues(slider.minimum(), slider.maximum());
  hideTestHandles(&slider);
  QImage image = renderSlider(&slider);
  const int centerY = image.height() / 2;
  const int railStart = visualRailStartPixel(image.width());
  const int railEnd = visualRailEndPixel(image.width());
  QVERIFY(!isTrackPixel(image.pixelColor(0, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railEnd, centerY)));
  QVERIFY(!isTrackPixel(image.pixelColor(image.width() - 1, centerY)));

  applyTestStyle(&slider);
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(kAxisInset, centerY)));
  QVERIFY(isHandleFillPixel(image.pixelColor(slider.width() - kAxisInset, centerY)));

  slider.setValues(25, 75);
  image = renderSlider(&slider);
  QVERIFY(isRailPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(image.width() / 2, centerY)));
  QVERIFY(isRailPixel(image.pixelColor(railEnd, centerY)));

  slider.setValues(slider.minimum(), 75);
  hideTestHandles(&slider);
  image = renderSlider(&slider);
  QVERIFY(isTrackPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isRailPixel(image.pixelColor(railEnd, centerY)));

  slider.setValues(25, slider.maximum());
  image = renderSlider(&slider);
  QVERIFY(isRailPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railEnd, centerY)));
}

void SliderTest::rightToLeftAxisPreservesVisualCapsAndInputDirection() {
  AdSlider slider;
  slider.resize(200, 40);
  slider.setLayoutDirection(Qt::RightToLeft);
  applyTestStyle(&slider);
  slider.setValue(slider.minimum());

  hideTestHandles(&slider);
  QImage image = renderSlider(&slider);
  const int centerY = image.height() / 2;
  const int railStart = visualRailStartPixel(image.width());
  const int railEnd = visualRailEndPixel(image.width());
  QVERIFY(!isRailPixel(image.pixelColor(0, centerY)));
  QVERIFY(isRailPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isRailPixel(image.pixelColor(railEnd, centerY)));
  QVERIFY(!isRailPixel(image.pixelColor(image.width() - 1, centerY)));

  applyTestStyle(&slider);
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(slider.width() - kAxisInset, centerY)));

  QTest::mouseClick(&slider, Qt::LeftButton, Qt::NoModifier,
                    QPoint(railStart, slider.height() / 2));
  QCOMPARE(slider.value(), slider.maximum());
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(kAxisInset, centerY)));
  hideTestHandles(&slider);
  image = renderSlider(&slider);
  QVERIFY(isTrackPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railEnd, centerY)));

  QTest::mouseClick(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(railEnd, slider.height() / 2));
  QCOMPARE(slider.value(), slider.minimum());
}

void SliderTest::rightToLeftSelectionDragFollowsValueDirection() {
  AdRangeSlider slider;
  slider.resize(200, 40);
  slider.setLayoutDirection(Qt::RightToLeft);
  slider.setSelectionDragEnabled(true);
  slider.setValues(20, 40);
  applyTestStyle(&slider);
  renderSlider(&slider);

  const QPoint trackCenter(134, slider.height() / 2);
  QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, trackCenter);
  QTest::mouseMove(&slider, trackCenter - QPoint(17, 0));
  QTest::mouseRelease(&slider, Qt::LeftButton, Qt::NoModifier, trackCenter - QPoint(17, 0));

  QCOMPARE(slider.lowerValue(), 30.0);
  QCOMPARE(slider.upperValue(), 50.0);
}

void SliderTest::tinyControlClampsHandleInset() {
  AdSlider slider;
  slider.resize(24, 40);
  applyTestStyle(&slider);
  slider.setValue(slider.minimum());

  hideTestHandles(&slider);
  QImage image = renderSlider(&slider);
  const int centerY = image.height() / 2;
  const int railStart = visualRailStartPixel(image.width());
  const int railEnd = visualRailEndPixel(image.width());
  QVERIFY(!isRailPixel(image.pixelColor(0, centerY)));
  QVERIFY(isRailPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isRailPixel(image.pixelColor(railEnd, centerY)));
  QVERIFY(!isRailPixel(image.pixelColor(image.width() - 1, centerY)));

  applyTestStyle(&slider);
  image = renderSlider(&slider);
  QVERIFY(isHandleFillPixel(image.pixelColor(image.width() / 2, centerY)));
  QVERIFY(!isHandleFillPixel(image.pixelColor(0, centerY)));
  QVERIFY(!isHandleFillPixel(image.pixelColor(image.width() - 1, centerY)));

  const QPoint minimumHandleCenter(slider.width() / 2, slider.height() / 2);
  const QPoint maximumDragTarget(slider.width() - 1, slider.height() / 2);
  QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, minimumHandleCenter);
  QTest::mouseMove(&slider, maximumDragTarget);
  QTest::mouseRelease(&slider, Qt::LeftButton, Qt::NoModifier, maximumDragTarget);
  QCOMPARE(slider.value(), slider.maximum());
  hideTestHandles(&slider);
  image = renderSlider(&slider);
  QVERIFY(!isTrackPixel(image.pixelColor(0, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railStart, centerY)));
  QVERIFY(isTrackPixel(image.pixelColor(railEnd, centerY)));
  QVERIFY(!isTrackPixel(image.pixelColor(image.width() - 1, centerY)));
}

void SliderTest::hoverTooltipUsesInputTransparentTopLevelSurface() {
  AdSlider slider;
  slider.resize(200, 40);
  slider.setValue(50);
  renderSlider(&slider);

  const QPoint handleCenter(slider.width() / 2, slider.height() / 2);
  QTest::mouseMove(&slider, handleCenter);

  auto* tooltip =
      slider.findChild<adqt::widgets::AdTooltip*>(QStringLiteral("ad-slider-tooltip-host"));
  QVERIFY(tooltip);
  QTRY_VERIFY(tooltip->isVisible());
  QCOMPARE(tooltip->layerMode(), adqt::widgets::AdTooltip::LayerMode::TopLevelTransient);
  QTRY_VERIFY(topLevelTooltipSurface());
  QVERIFY(topLevelTooltipSurface()->windowFlags().testFlag(Qt::WindowTransparentForInput));
}

QTEST_MAIN(SliderTest)

#include "slider_tests.moc"
