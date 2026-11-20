#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QImage>
#include <QPointer>
#include <QTest>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "widgets/form.h"
#include "widgets/slider.h"
#include "widgets/tooltip.h"

using adqt::widgets::AdMultiSlider;
using adqt::widgets::AdRangeSlider;
using adqt::widgets::AdSlider;

namespace {

constexpr int kAxisInset = 16;
constexpr int kMinimumThumbSize = 10;

class ModeSlider final : public AdMultiSlider {
 public:
  using AdMultiSlider::setMode;
};

class SliderPropertyObserver final : public QObject {
 public:
  std::function<void()> propertyChanged;

 protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::DynamicPropertyChange) {
      const auto callback = propertyChanged;
      if (callback) callback();
    }
    return false;
  }
};

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
  void markedSliderFitsItsLabelsInVerticalForm();
  void verticalRailIsVisualAndMovementIsLogical();
  void reversedMaximumFillsVisualRail();
  void rangeTrackUsesExplicitVisualCaps();
  void rightToLeftAxisPreservesVisualCapsAndInputDirection();
  void rightToLeftSelectionDragFollowsValueDirection();
  void tinyControlClampsHandleInset();
  void hoverTooltipUsesInputTransparentTopLevelSurface();
  void boundUpdatesCommitBeforeNotificationsAndPreserveOrder();
  void boundUpdatesWithUnchangedHandlesPublishNotifications();
  void nestedBoundNotificationsKeepLatestRanges();
  void boundNotificationsCoalesceTransientChanges();
  void boundNotificationsCanReturnToDeliveredRange();
  void boundsChangedFromHandleNotificationsSettleBeforeCombinedRange();
  void boundUpdatesDeferPublicationDuringAccessibilityRefresh();
  void nonFiniteBoundNotificationsDoNotRepeat();
  void combinedRangeObserversCanReplaceBounds();
  void boundNotificationsSurviveOwnerDeletion();
  void handleUpdatesCommitBeforeNotifications();
  void nestedHandleNotificationsPreserveIndependentChanges();
  void nestedHandleNotificationsCoalesceSupersededValues();
  void nestedFocusNotificationsKeepLatestState();
  void userHandleNotificationsPreserveIndependentChanges();
  void supersededUserMovementDoesNotPublishStaleValues();
  void deferredCommitNotificationsKeepLatestValues();
  void nestedHandleNotificationsSurviveOwnerDeletion();
  void nestedRangeNotificationsKeepLatestValues();
  void nestedLowerNotificationPreservesUpperUpdate();
  void nestedRangeNotificationsAvoidDuplicateCombinedValues();
  void nestedUpperAndCombinedNotificationsKeepLatestValues();
  void pendingRangeNotificationSurvivesOwnerDeletion();
  void valueNotificationsSurviveOwnerDeletion();
  void outerSettersSurviveOwnerDeletion();
  void interactionNotificationsSurviveOwnerDeletion();
};

void SliderTest::boundUpdatesCommitBeforeNotificationsAndPreserveOrder() {
  for (const int setter : {0, 1, 2}) {
    AdSlider slider;
    const double expectedMinimum = setter == 0 ? 200.0 : setter == 1 ? -10.0 : 10.0;
    const double expectedMaximum = setter == 0 ? 200.0 : setter == 1 ? -10.0 : 200.0;
    QStringList order;
    connect(&slider, &AdMultiSlider::valueChanged, this, [&](double value) {
      QCOMPARE(value, slider.value());
      order.append(QStringLiteral("value"));
    });
    connect(&slider, &AdMultiSlider::handleValuesChanged, this, [&](const QList<double>& values) {
      QCOMPARE(values, static_cast<const AdMultiSlider&>(slider).handleValues());
      order.append(QStringLiteral("handles"));
    });
    connect(&slider, &AdMultiSlider::minimumChanged, this, [&](double minimum) {
      QCOMPARE(minimum, expectedMinimum);
      QCOMPARE(slider.minimum(), expectedMinimum);
      QCOMPARE(slider.maximum(), expectedMaximum);
      QCOMPARE(slider.value(), expectedMinimum);
      order.append(QStringLiteral("minimum"));
    });
    connect(&slider, &AdMultiSlider::maximumChanged, this, [&](double maximum) {
      QCOMPARE(maximum, expectedMaximum);
      QCOMPARE(slider.minimum(), expectedMinimum);
      QCOMPARE(slider.maximum(), expectedMaximum);
      QCOMPARE(slider.value(), expectedMinimum);
      order.append(QStringLiteral("maximum"));
    });
    connect(&slider, &AdMultiSlider::rangeChanged, this, [&](double minimum, double maximum) {
      QCOMPARE(minimum, expectedMinimum);
      QCOMPARE(maximum, expectedMaximum);
      order.append(QStringLiteral("range"));
    });

    if (setter == 0)
      slider.setMinimum(200);
    else if (setter == 1)
      slider.setMaximum(-10);
    else
      slider.setRange(10, 200);

    const QStringList expectedOrder =
        setter == 0 ? QStringList{QStringLiteral("value"), QStringLiteral("handles"),
                                  QStringLiteral("maximum"), QStringLiteral("minimum"),
                                  QStringLiteral("range")}
                    : QStringList{QStringLiteral("value"), QStringLiteral("handles"),
                                  QStringLiteral("minimum"), QStringLiteral("maximum"),
                                  QStringLiteral("range")};
    QCOMPARE(order, expectedOrder);
  }
}

void SliderTest::nestedBoundNotificationsKeepLatestRanges() {
  for (const int setter : {0, 1, 2, 3}) {
    AdSlider slider;
    QList<double> minimums;
    QList<double> maximums;
    QList<QList<double>> ranges;
    connect(&slider, &AdMultiSlider::minimumChanged, this, [&](double minimum) {
      minimums.append(minimum);
      if (setter == 0 && minimum == 10)
        slider.setMaximum(50);
      else if (setter == 2 && minimum == -10)
        slider.setMaximum(20);
    });
    connect(&slider, &AdMultiSlider::maximumChanged, this, [&](double maximum) {
      maximums.append(maximum);
      if (setter == 1 && maximum == 200)
        slider.setMinimum(150);
      else if (setter == 3 && maximum == 200)
        slider.setRange(20, 80);
    });
    connect(&slider, &AdMultiSlider::rangeChanged, this, [&](double minimum, double maximum) {
      QCOMPARE(minimum, slider.minimum());
      QCOMPARE(maximum, slider.maximum());
      ranges.append({minimum, maximum});
    });

    if (setter == 1)
      slider.setMinimum(200);
    else if (setter == 2)
      slider.setMaximum(-10);
    else
      slider.setRange(10, 200);

    const double expectedMinimum = setter == 0   ? 10.0
                                   : setter == 1 ? 150.0
                                   : setter == 2 ? -10.0
                                                 : 20.0;
    const double expectedMaximum = setter == 0   ? 50.0
                                   : setter == 1 ? 200.0
                                   : setter == 2 ? 20.0
                                                 : 80.0;
    QCOMPARE(slider.minimum(), expectedMinimum);
    QCOMPARE(slider.maximum(), expectedMaximum);
    const QList<double> expectedMinimums =
        setter == 3 ? QList<double>{10, 20} : QList<double>{expectedMinimum};
    const QList<double> expectedMaximums =
        setter == 3 ? QList<double>{200, 80} : QList<double>{expectedMaximum};
    QCOMPARE(minimums, expectedMinimums);
    QCOMPARE(maximums, expectedMaximums);
    QCOMPARE(ranges, QList<QList<double>>({{expectedMinimum, expectedMaximum}}));
  }
}

void SliderTest::boundUpdatesWithUnchangedHandlesPublishNotifications() {
  AdSlider slider;
  slider.setValue(50);
  QStringList order;
  connect(&slider, &AdMultiSlider::minimumChanged, this,
          [&](double) { order.append(QStringLiteral("minimum")); });
  connect(&slider, &AdMultiSlider::maximumChanged, this,
          [&](double) { order.append(QStringLiteral("maximum")); });
  connect(&slider, &AdMultiSlider::rangeChanged, this, [&](double minimum, double maximum) {
    QCOMPARE(minimum, 10.0);
    QCOMPARE(maximum, 90.0);
    order.append(QStringLiteral("range"));
  });
  slider.setRange(10, 90);
  QCOMPARE(slider.value(), 50.0);
  QCOMPARE(order, QStringList({QStringLiteral("minimum"), QStringLiteral("maximum"),
                               QStringLiteral("range")}));
}

void SliderTest::boundNotificationsCoalesceTransientChanges() {
  for (const bool restoreMinimum : {false, true}) {
    AdSlider slider;
    QList<double> minimums;
    QList<double> maximums;
    QList<QList<double>> ranges;
    connect(&slider, &AdMultiSlider::minimumChanged, this, [&](double minimum) {
      minimums.append(minimum);
      if (minimum != 10) return;
      if (restoreMinimum) {
        slider.setRange(0, 150);
      } else {
        slider.setMaximum(50);
        slider.setMaximum(100);
        slider.setMinimum(10);
      }
    });
    connect(&slider, &AdMultiSlider::maximumChanged, this,
            [&](double maximum) { maximums.append(maximum); });
    connect(&slider, &AdMultiSlider::rangeChanged, this,
            [&](double minimum, double maximum) { ranges.append({minimum, maximum}); });

    slider.setRange(10, 200);

    QCOMPARE(minimums, restoreMinimum ? QList<double>({10, 0}) : QList<double>({10}));
    QCOMPARE(maximums, restoreMinimum ? QList<double>({150}) : QList<double>());
    const QList<double> expectedRange =
        restoreMinimum ? QList<double>{0, 150} : QList<double>{10, 100};
    QCOMPARE(ranges, QList<QList<double>>({expectedRange}));
    QCOMPARE(slider.minimum(), expectedRange.at(0));
    QCOMPARE(slider.maximum(), expectedRange.at(1));
  }
}

void SliderTest::boundNotificationsCanReturnToDeliveredRange() {
  for (const bool reverseFromCombined : {false, true}) {
    AdSlider slider;
    QList<double> minimums;
    QList<double> maximums;
    QList<QList<double>> ranges;
    connect(&slider, &AdMultiSlider::minimumChanged, this, [&](double minimum) {
      minimums.append(minimum);
      if (!reverseFromCombined && minimum == 10) slider.setRange(0, 100);
    });
    connect(&slider, &AdMultiSlider::maximumChanged, this,
            [&](double maximum) { maximums.append(maximum); });
    connect(&slider, &AdMultiSlider::rangeChanged, this, [&](double minimum, double maximum) {
      ranges.append({minimum, maximum});
      if (reverseFromCombined && minimum == 10 && maximum == 200) slider.setRange(0, 100);
    });

    slider.setRange(10, 200);

    QCOMPARE(slider.minimum(), 0.0);
    QCOMPARE(slider.maximum(), 100.0);
    QCOMPARE(minimums, QList<double>({10, 0}));
    QCOMPARE(maximums, reverseFromCombined ? QList<double>({200, 100}) : QList<double>());
    const QList<QList<double>> expectedRanges =
        reverseFromCombined ? QList<QList<double>>{{10, 200}, {0, 100}} : QList<QList<double>>{};
    QCOMPARE(ranges, expectedRanges);
  }
}

void SliderTest::boundsChangedFromHandleNotificationsSettleBeforeCombinedRange() {
  for (const int source : {0, 1, 2, 3, 4, 5}) {
    AdRangeSlider slider;
    slider.setValues(10, 80);
    QList<double> minimums;
    QList<double> maximums;
    QList<QList<double>> ranges;
    QList<QList<double>> handles;
    const auto replaceBounds = [&] { slider.setRange(30, 60); };
    if (source == 0) {
      connect(&slider, &AdMultiSlider::sliderPositionChanged, this, [&](double position) {
        if (position == 20) replaceBounds();
      });
    } else if (source == 1) {
      connect(&slider, &AdMultiSlider::valueChanged, this, [&](double value) {
        if (value == 20) replaceBounds();
      });
    } else if (source == 2) {
      connect(&slider, &AdMultiSlider::handleValuesChanged, this, [&](const QList<double>& values) {
        if (values == QList<double>{20, 70}) replaceBounds();
      });
    } else if (source == 3) {
      connect(&slider, &AdRangeSlider::lowerValueChanged, this, [&](double lower) {
        if (lower == 20) replaceBounds();
      });
    } else if (source == 4) {
      connect(&slider, &AdRangeSlider::upperValueChanged, this, [&](double upper) {
        if (upper == 70) replaceBounds();
      });
    } else {
      connect(&slider, &AdRangeSlider::valuesChanged, this, [&](double lower, double upper) {
        if (lower == 20 && upper == 70) replaceBounds();
      });
    }
    connect(&slider, &AdMultiSlider::handleValuesChanged, this,
            [&](const QList<double>& values) { handles.append(values); });
    connect(&slider, &AdMultiSlider::minimumChanged, this,
            [&](double minimum) { minimums.append(minimum); });
    connect(&slider, &AdMultiSlider::maximumChanged, this,
            [&](double maximum) { maximums.append(maximum); });
    connect(&slider, &AdMultiSlider::rangeChanged, this, [&](double minimum, double maximum) {
      QCOMPARE(minimum, slider.minimum());
      QCOMPARE(maximum, slider.maximum());
      QVERIFY(!minimums.isEmpty());
      QVERIFY(!maximums.isEmpty());
      QVERIFY(!handles.isEmpty());
      QCOMPARE(minimums.constLast(), minimum);
      QCOMPARE(maximums.constLast(), maximum);
      QCOMPARE(handles.constLast(), static_cast<const AdMultiSlider&>(slider).handleValues());
      QCOMPARE(slider.lowerValue(), 30.0);
      QCOMPARE(slider.upperValue(), 60.0);
      ranges.append({minimum, maximum});
    });

    slider.setRange(20, 70);

    QCOMPARE(minimums, QList<double>({30}));
    QCOMPARE(maximums, QList<double>({60}));
    QCOMPARE(ranges, QList<QList<double>>({{30, 60}}));
    QCOMPARE(handles.constLast(), QList<double>({30, 60}));
  }
}

void SliderTest::boundUpdatesDeferPublicationDuringAccessibilityRefresh() {
  for (const bool replaceRange : {false, true}) {
    AdMultiSlider slider;
    slider.setHandleValues({10, 80});
    QList<QList<double>> handles;
    QList<QList<double>> ranges;
    bool observed = false;
    SliderPropertyObserver observer;
    observer.propertyChanged = [&] {
      if (observed || slider.minimum() != 20 || slider.maximum() != 70) return;
      observed = true;
      slider.setCurrentHandle(1);
      if (replaceRange) slider.setRange(30, 60);
    };
    slider.installEventFilter(&observer);
    connect(&slider, &AdMultiSlider::handleValuesChanged, this,
            [&](const QList<double>& values) { handles.append(values); });
    connect(&slider, &AdMultiSlider::rangeChanged, this, [&](double minimum, double maximum) {
      const QList<double> expectedHandles =
          replaceRange ? QList<double>{30, 60} : QList<double>{20, 70};
      QCOMPARE(slider.handleValues(), expectedHandles);
      QVERIFY(!handles.isEmpty());
      QCOMPARE(handles.constLast(), expectedHandles);
      QCOMPARE(minimum, slider.minimum());
      QCOMPARE(maximum, slider.maximum());
      ranges.append({minimum, maximum});
    });

    slider.setRange(20, 70);

    QVERIFY(observed);
    QCOMPARE(slider.currentHandle(), 1);
    const QList<double> expectedRange =
        replaceRange ? QList<double>{30, 60} : QList<double>{20, 70};
    QCOMPARE(slider.handleValues(), expectedRange);
    QCOMPARE(ranges, QList<QList<double>>({expectedRange}));
  }
}

void SliderTest::nonFiniteBoundNotificationsDoNotRepeat() {
  for (const double requestedMaximum :
       {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    AdMultiSlider slider;
    int maximumChanges = 0;
    int rangeChanges = 0;
    const auto isRequestedMaximum = [&](double maximum) {
      return maximum == requestedMaximum || (std::isnan(maximum) && std::isnan(requestedMaximum));
    };
    connect(&slider, &AdMultiSlider::maximumChanged, this, [&](double maximum) {
      if (!isRequestedMaximum(maximum)) return;
      ++maximumChanges;
      // Restore a finite bound if a broken drain repeats, keeping the regression
      // deterministic instead of leaving the test stuck in its publication loop.
      if (maximumChanges > 1) slider.setMaximum(100);
    });
    connect(&slider, &AdMultiSlider::rangeChanged, this, [&](double minimum, double maximum) {
      QCOMPARE(minimum, 0.0);
      if (!isRequestedMaximum(maximum)) return;
      ++rangeChanges;
      if (rangeChanges > 1) slider.setMaximum(100);
    });

    slider.setMaximum(requestedMaximum);

    QCOMPARE(maximumChanges, 1);
    QCOMPARE(rangeChanges, 1);
    QVERIFY(isRequestedMaximum(slider.maximum()));
    QCOMPARE(slider.handleValues(), QList<double>({0, 0}));
  }
}

void SliderTest::combinedRangeObserversCanReplaceBounds() {
  AdRangeSlider slider;
  slider.setValues(10, 80);
  QList<double> minimums;
  QList<double> maximums;
  QList<QList<double>> ranges;
  connect(&slider, &AdMultiSlider::minimumChanged, this,
          [&](double minimum) { minimums.append(minimum); });
  connect(&slider, &AdMultiSlider::maximumChanged, this,
          [&](double maximum) { maximums.append(maximum); });
  connect(&slider, &AdMultiSlider::rangeChanged, this, [&](double minimum, double maximum) {
    QCOMPARE(minimum, slider.minimum());
    QCOMPARE(maximum, slider.maximum());
    QVERIFY(!minimums.isEmpty());
    QVERIFY(!maximums.isEmpty());
    QCOMPARE(minimums.constLast(), minimum);
    QCOMPARE(maximums.constLast(), maximum);
    ranges.append({minimum, maximum});
    if (minimum == 20 && maximum == 70) slider.setRange(30, 60);
  });

  slider.setRange(20, 70);

  QCOMPARE(minimums, QList<double>({20, 30}));
  QCOMPARE(maximums, QList<double>({70, 60}));
  QCOMPARE(ranges, QList<QList<double>>({{20, 70}, {30, 60}}));
  QCOMPARE(slider.lowerValue(), 30.0);
  QCOMPARE(slider.upperValue(), 60.0);
}

void SliderTest::boundNotificationsSurviveOwnerDeletion() {
  for (const int setter : {0, 1, 2}) {
    for (const int signal : {0, 1, 2}) {
      QPointer<AdSlider> slider = new AdSlider;
      int notifications = 0;
      const auto deleteOwner = [&] {
        ++notifications;
        delete slider.data();
      };
      if (signal == 0)
        connect(slider, &AdMultiSlider::minimumChanged, this, deleteOwner);
      else if (signal == 1)
        connect(slider, &AdMultiSlider::maximumChanged, this, deleteOwner);
      else
        connect(slider, &AdMultiSlider::rangeChanged, this, deleteOwner);

      if (setter == 0)
        slider->setMinimum(200);
      else if (setter == 1)
        slider->setMaximum(-10);
      else
        slider->setRange(10, 200);

      QCOMPARE(notifications, 1);
      QVERIFY(slider.isNull());
    }
  }

  QPointer<AdSlider> slider = new AdSlider;
  connect(slider, &AdMultiSlider::minimumChanged, this, [&](double minimum) {
    if (minimum == 10) slider->setMaximum(50);
  });
  connect(slider, &AdMultiSlider::maximumChanged, this, [&] { delete slider.data(); });
  slider->setRange(10, 200);
  QVERIFY(slider.isNull());
}

void SliderTest::handleUpdatesCommitBeforeNotifications() {
  AdRangeSlider slider;
  const AdMultiSlider& state = slider;
  bool observed = false;
  QList<double> lastNotifiedValues;
  int lowerChanges = 0;
  connect(&slider, &AdMultiSlider::handleValuesChanged, this,
          [&](const QList<double>& values) { lastNotifiedValues = values; });
  connect(&slider, &AdRangeSlider::lowerValueChanged, this, [&] { ++lowerChanges; });
  connect(&slider, &AdMultiSlider::sliderPositionChanged, this, [&](double position) {
    if (position != 20.0) return;
    observed = true;
    QCOMPARE(state.value(), 20.0);
    QCOMPARE(state.handleValues(), QList<double>({20, 40}));
    slider.setValues(60, 80);
  });
  slider.setValues(20, 40);
  QVERIFY(observed);
  QCOMPARE(state.value(), 60.0);
  QCOMPARE(state.sliderPosition(), 60.0);
  QCOMPARE(state.handleValues(), QList<double>({60, 80}));
  QCOMPARE(lastNotifiedValues, state.handleValues());
  QCOMPARE(lowerChanges, 1);
  slider.setValues(60, 90);
  QCOMPARE(lowerChanges, 1);
}

void SliderTest::nestedHandleNotificationsPreserveIndependentChanges() {
  AdRangeSlider slider;
  slider.setValues(10, 20);
  QList<double> values;
  QList<QList<double>> handles;
  QList<QList<double>> endpoints;
  connect(&slider, &AdMultiSlider::valueChanged, this, [&](double value) {
    QCOMPARE(value, static_cast<const AdMultiSlider&>(slider).value());
    values.append(value);
  });
  connect(&slider, &AdMultiSlider::handleValuesChanged, this,
          [&](const QList<double>& next) { handles.append(next); });
  connect(&slider, &AdRangeSlider::valuesChanged, this,
          [&](double lower, double upper) { endpoints.append({lower, upper}); });
  connect(&slider, &AdMultiSlider::sliderPositionChanged, this, [&](double position) {
    if (position == 30) slider.setUpperValue(50);
  });

  slider.setValues(30, 40);

  QCOMPARE(slider.lowerValue(), 30.0);
  QCOMPARE(slider.upperValue(), 50.0);
  QCOMPARE(values, QList<double>({30}));
  QCOMPARE(handles, QList<QList<double>>({{30, 50}}));
  QCOMPARE(endpoints, QList<QList<double>>({{30, 50}}));
  slider.setValues(30, 50);
  QCOMPARE(values.size(), 1);
  QCOMPARE(handles.size(), 1);
  QCOMPARE(endpoints.size(), 1);
}

void SliderTest::nestedHandleNotificationsCoalesceSupersededValues() {
  for (const bool restoreLower : {false, true}) {
    AdRangeSlider slider;
    slider.setValues(10, 20);
    QList<double> values;
    QList<double> positions;
    QList<QList<double>> handles;
    connect(&slider, &AdMultiSlider::valueChanged, this,
            [&](double value) { values.append(value); });
    connect(&slider, &AdMultiSlider::handleValuesChanged, this,
            [&](const QList<double>& next) { handles.append(next); });
    connect(&slider, &AdMultiSlider::sliderPositionChanged, this, [&](double position) {
      positions.append(position);
      if (position != 30) return;
      if (restoreLower) {
        slider.setValues(10, 50);
        slider.setValues(30, 50);
      } else {
        slider.setValues(60, 80);
      }
    });

    slider.setValues(30, 40);

    const QList<double> finalHandles = restoreLower ? QList<double>{30, 50} : QList<double>{60, 80};
    QCOMPARE(static_cast<const AdMultiSlider&>(slider).handleValues(), finalHandles);
    QCOMPARE(values, QList<double>({finalHandles.constFirst()}));
    QCOMPARE(handles, QList<QList<double>>({finalHandles}));
    const QList<double> expectedPositions =
        restoreLower ? QList<double>{30} : QList<double>{30, 60};
    QCOMPARE(positions, expectedPositions);
  }
}

void SliderTest::nestedFocusNotificationsKeepLatestState() {
  for (const bool redirectFocus : {false, true}) {
    ModeSlider slider;
    slider.setHandleValues({10, 20, 30});
    QList<int> active;
    QList<int> current;
    QList<double> positions;
    QList<double> values;
    QList<QList<double>> handles;
    connect(&slider, &AdMultiSlider::activeHandleIndexChanged, this, [&](int index) {
      active.append(index);
      if (index != 1) return;
      if (redirectFocus)
        slider.setCurrentHandle(2);
      else
        slider.setHandleValues({15, 25, 35});
    });
    connect(&slider, &AdMultiSlider::currentHandleChanged, this, [&](int index) {
      QCOMPARE(index, slider.currentHandle());
      current.append(index);
    });
    connect(&slider, &AdMultiSlider::sliderPositionChanged, this, [&](double position) {
      QCOMPARE(position, slider.sliderPosition());
      positions.append(position);
    });
    connect(&slider, &AdMultiSlider::valueChanged, this,
            [&](double value) { values.append(value); });
    connect(&slider, &AdMultiSlider::handleValuesChanged, this,
            [&](const QList<double>& next) { handles.append(next); });

    slider.setCurrentHandle(1);

    const QList<int> expectedActive = redirectFocus ? QList<int>{1, 2} : QList<int>{1};
    QCOMPARE(active, expectedActive);
    QCOMPARE(current, QList<int>({redirectFocus ? 2 : 1}));
    QCOMPARE(positions, QList<double>({redirectFocus ? 30.0 : 25.0}));
    QCOMPARE(values, redirectFocus ? QList<double>{} : QList<double>{15});
    const QList<QList<double>> expectedHandles =
        redirectFocus ? QList<QList<double>>{} : QList<QList<double>>{{15, 25, 35}};
    QCOMPARE(handles, expectedHandles);
  }
}

void SliderTest::userHandleNotificationsPreserveIndependentChanges() {
  for (const int source : {0, 1, 2}) {
    AdRangeSlider slider;
    slider.setTooltipEnabled(false);
    slider.setWheelEnabled(true);
    slider.setValues(10, 40);
    QList<double> values;
    QList<QList<double>> handles;
    QList<double> moved;
    QStringList order;
    connect(&slider, &AdMultiSlider::sliderPositionChanged, this, [&](double position) {
      if (position == 11) slider.setUpperValue(60);
    });
    connect(&slider, &AdMultiSlider::valueChanged, this, [&](double value) {
      values.append(value);
      order.append(QStringLiteral("value"));
    });
    connect(&slider, &AdMultiSlider::handleValuesChanged, this, [&](const QList<double>& next) {
      handles.append(next);
      order.append(QStringLiteral("handles"));
    });
    connect(&slider, &AdMultiSlider::handleMoved, this, [&](int index, double value) {
      QCOMPARE(index, 0);
      QCOMPARE(value, slider.lowerValue());
      moved.append(value);
      order.append(QStringLiteral("moved"));
    });

    if (source == 0) {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
      QCoreApplication::sendEvent(&slider, &press);
      QVERIFY(press.isAccepted());
    } else if (source == 1) {
      QWheelEvent wheel(QPointF(100, 20), QPointF(100, 20), {}, QPoint(0, 120), Qt::NoButton,
                        Qt::NoModifier, Qt::NoScrollPhase, false);
      QCoreApplication::sendEvent(&slider, &wheel);
      QVERIFY(wheel.isAccepted());
    } else {
      slider.triggerAction(QAbstractSlider::SliderSingleStepAdd);
    }

    QCOMPARE(values, QList<double>({11}));
    QCOMPARE(handles, QList<QList<double>>({{11, 60}}));
    QCOMPARE(moved, QList<double>({11}));
    QCOMPARE(order, QStringList({QStringLiteral("value"), QStringLiteral("handles"),
                                 QStringLiteral("moved")}));
  }
}

void SliderTest::supersededUserMovementDoesNotPublishStaleValues() {
  AdRangeSlider slider;
  slider.setTooltipEnabled(false);
  slider.setValues(10, 40);
  QList<double> values;
  QList<QList<double>> handles;
  int moves = 0;
  connect(&slider, &AdMultiSlider::sliderPositionChanged, this, [&](double position) {
    if (position == 11) slider.setValues(20, 60);
  });
  connect(&slider, &AdMultiSlider::valueChanged, this, [&](double value) { values.append(value); });
  connect(&slider, &AdMultiSlider::handleValuesChanged, this,
          [&](const QList<double>& next) { handles.append(next); });
  connect(&slider, &AdMultiSlider::handleMoved, this, [&] { ++moves; });

  QKeyEvent press(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  QCoreApplication::sendEvent(&slider, &press);

  QCOMPARE(values, QList<double>({20}));
  QCOMPARE(handles, QList<QList<double>>({{20, 60}}));
  QCOMPARE(moves, 0);
}

void SliderTest::deferredCommitNotificationsKeepLatestValues() {
  for (const int source : {0, 1, 2, 3}) {
    for (const bool supersedeLower : {false, true}) {
      AdRangeSlider slider;
      slider.resize(200, 40);
      applyTestStyle(&slider);
      slider.setTracking(false);
      slider.setWheelEnabled(true);
      slider.setValues(10, 40);
      QList<double> values;
      QList<QList<double>> handles;
      QList<QList<double>> endpoints;
      int completions = 0;
      const double committedLower = source == 3 ? 20.0 : 11.0;
      connect(&slider, &AdMultiSlider::valueChanged, this, [&](double value) {
        values.append(value);
        if (value != committedLower) return;
        if (supersedeLower)
          slider.setValues(30, 60);
        else
          slider.setUpperValue(60);
      });
      connect(&slider, &AdMultiSlider::handleValuesChanged, this,
              [&](const QList<double>& next) { handles.append(next); });
      connect(&slider, &AdRangeSlider::valuesChanged, this,
              [&](double lower, double upper) { endpoints.append({lower, upper}); });
      connect(&slider, &AdMultiSlider::editingFinished, this, [&] { ++completions; });

      if (source == 1) {
        QWheelEvent wheel(QPointF(100, 20), QPointF(100, 20), {}, QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&slider, &wheel);
        QVERIFY(wheel.isAccepted());
      } else if (source == 3) {
        const QPointF start(33, 20);
        const QPointF end(50, 20);
        QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(&slider, &press);
        QMouseEvent move(QEvent::MouseMove, end, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&slider, &move);
        QCOMPARE(slider.lowerValue(), 10.0);
        QCOMPARE(static_cast<const AdMultiSlider&>(slider).sliderPosition(), committedLower);
        QVERIFY(values.isEmpty());
        QVERIFY(handles.isEmpty());
        QMouseEvent release(QEvent::MouseButtonRelease, end, end, Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(&slider, &release);
        QVERIFY(release.isAccepted());
      } else {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QCoreApplication::sendEvent(&slider, &press);
        QCOMPARE(slider.lowerValue(), 10.0);
        QCOMPARE(static_cast<const AdMultiSlider&>(slider).sliderPosition(), committedLower);
        QVERIFY(values.isEmpty());
        QVERIFY(handles.isEmpty());
        if (source == 0) {
          QKeyEvent release(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
          QCoreApplication::sendEvent(&slider, &release);
          QVERIFY(release.isAccepted());
        } else {
          QFocusEvent focusOut(QEvent::FocusOut, Qt::OtherFocusReason);
          QCoreApplication::sendEvent(&slider, &focusOut);
        }
      }

      const double finalLower = supersedeLower ? 30.0 : committedLower;
      QCOMPARE(slider.lowerValue(), finalLower);
      QCOMPARE(slider.upperValue(), 60.0);
      const QList<double> expectedValues =
          supersedeLower ? QList<double>{committedLower, 30} : QList<double>{committedLower};
      QCOMPARE(values, expectedValues);
      QCOMPARE(handles, QList<QList<double>>({{finalLower, 60}}));
      QCOMPARE(endpoints, QList<QList<double>>({{finalLower, 60}}));
      QCOMPARE(completions, 1);
    }
  }
}

void SliderTest::nestedHandleNotificationsSurviveOwnerDeletion() {
  for (const bool deferredCommit : {false, true}) {
    QPointer<AdRangeSlider> slider = new AdRangeSlider;
    slider->setTooltipEnabled(false);
    slider->setValues(10, 40);
    int handles = 0;
    int completions = 0;
    connect(slider, &AdMultiSlider::sliderPositionChanged, this, [&](double position) {
      if (!deferredCommit && position == 30) slider->setUpperValue(60);
    });
    connect(slider, &AdMultiSlider::valueChanged, this, [&](double value) {
      if (deferredCommit) {
        QCOMPARE(value, 11.0);
        slider->setUpperValue(60);
      }
      delete slider.data();
    });
    connect(slider, &AdMultiSlider::handleValuesChanged, this, [&] { ++handles; });
    connect(slider, &AdMultiSlider::editingFinished, this, [&] { ++completions; });

    if (deferredCommit) {
      slider->setTracking(false);
      QKeyEvent press(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
      QCoreApplication::sendEvent(slider, &press);
      QVERIFY(slider);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
      QCoreApplication::sendEvent(slider, &release);
      QVERIFY(release.isAccepted());
    } else {
      slider->setValues(30, 40);
    }

    QVERIFY(slider.isNull());
    QCOMPARE(handles, 0);
    QCOMPARE(completions, 0);
  }
}

void SliderTest::valueNotificationsSurviveOwnerDeletion() {
  for (const int signal : {0, 1, 2, 3, 4, 5}) {
    QPointer<AdRangeSlider> slider = new AdRangeSlider;
    const auto deleteOwner = [&] { delete slider.data(); };
    if (signal == 0) {
      connect(slider, &AdMultiSlider::sliderPositionChanged, this, deleteOwner);
    } else if (signal == 1) {
      connect(slider, &AdMultiSlider::valueChanged, this, deleteOwner);
    } else if (signal == 2) {
      connect(slider, &AdMultiSlider::handleValuesChanged, this, deleteOwner);
    } else if (signal == 3) {
      connect(slider, &AdRangeSlider::lowerValueChanged, this, deleteOwner);
    } else if (signal == 4) {
      connect(slider, &AdRangeSlider::upperValueChanged, this, deleteOwner);
    } else {
      connect(slider, &AdRangeSlider::valuesChanged, this, deleteOwner);
    }
    slider->setValues(20, 40);
    QVERIFY(slider.isNull());
  }
}

void SliderTest::nestedRangeNotificationsKeepLatestValues() {
  AdRangeSlider slider;
  double lastUpper = -1;
  QList<double> lastValues;
  connect(&slider, &AdRangeSlider::upperValueChanged, this,
          [&](double value) { lastUpper = value; });
  connect(&slider, &AdRangeSlider::valuesChanged, this,
          [&](double lower, double upper) { lastValues = {lower, upper}; });
  connect(&slider, &AdRangeSlider::lowerValueChanged, this, [&](double value) {
    if (value == 20) slider.setValues(60, 80);
  });
  slider.setValues(20, 40);
  QCOMPARE(slider.lowerValue(), 60.0);
  QCOMPARE(slider.upperValue(), 80.0);
  QCOMPARE(lastValues, QList<double>({60, 80}));
  QCOMPARE(lastUpper, 80.0);
}

void SliderTest::nestedLowerNotificationPreservesUpperUpdate() {
  for (const bool useEndpointSetter : {false, true}) {
    AdRangeSlider slider;
    QList<double> lowerNotifications;
    QList<double> upperNotifications;
    QList<QList<double>> combinedNotifications;
    connect(&slider, &AdRangeSlider::lowerValueChanged, this, [&](double value) {
      lowerNotifications.append(value);
      if (value == 20) {
        if (useEndpointSetter) {
          slider.setLowerValue(10);
        } else {
          slider.setValues(10, 40);
        }
      }
    });
    connect(&slider, &AdRangeSlider::upperValueChanged, this,
            [&](double value) { upperNotifications.append(value); });
    connect(&slider, &AdRangeSlider::valuesChanged, this, [&](double lower, double upper) {
      combinedNotifications.append({lower, upper});
      QCOMPARE(slider.lowerValue(), lower);
      QCOMPARE(slider.upperValue(), upper);
    });

    slider.setValues(20, 40);

    QCOMPARE(slider.lowerValue(), 10.0);
    QCOMPARE(slider.upperValue(), 40.0);
    QCOMPARE(lowerNotifications, QList<double>({20, 10}));
    QCOMPARE(upperNotifications, QList<double>({40}));
    QCOMPARE(combinedNotifications, QList<QList<double>>({{10, 40}}));
  }
}

void SliderTest::nestedRangeNotificationsAvoidDuplicateCombinedValues() {
  AdRangeSlider slider;
  QList<double> upperNotifications;
  QList<QList<double>> combinedNotifications;
  bool restored = false;
  connect(&slider, &AdRangeSlider::lowerValueChanged, this, [&](double value) {
    if (value != 20 || restored) return;
    restored = true;
    slider.setValues(10, 40);
    slider.setValues(20, 40);
  });
  connect(&slider, &AdRangeSlider::upperValueChanged, this,
          [&](double value) { upperNotifications.append(value); });
  connect(&slider, &AdRangeSlider::valuesChanged, this,
          [&](double lower, double upper) { combinedNotifications.append({lower, upper}); });

  slider.setValues(20, 40);

  QVERIFY(restored);
  QCOMPARE(slider.lowerValue(), 20.0);
  QCOMPARE(slider.upperValue(), 40.0);
  QCOMPARE(upperNotifications, QList<double>({40}));
  QCOMPARE(combinedNotifications, QList<QList<double>>({{20, 40}}));
}

void SliderTest::nestedUpperAndCombinedNotificationsKeepLatestValues() {
  for (const bool mutateFromCombined : {false, true}) {
    AdRangeSlider slider;
    QList<double> lowerNotifications;
    QList<double> upperNotifications;
    QList<QList<double>> combinedNotifications;
    connect(&slider, &AdRangeSlider::lowerValueChanged, this,
            [&](double value) { lowerNotifications.append(value); });
    connect(&slider, &AdRangeSlider::upperValueChanged, this, [&](double value) {
      upperNotifications.append(value);
      if (!mutateFromCombined && value == 40) slider.setLowerValue(10);
    });
    connect(&slider, &AdRangeSlider::valuesChanged, this, [&](double lower, double upper) {
      combinedNotifications.append({lower, upper});
      if (mutateFromCombined && upper == 40) {
        slider.setValues(lower, upper);
        slider.setUpperValue(60);
      }
    });

    slider.setValues(20, 40);

    if (mutateFromCombined) {
      QCOMPARE(slider.lowerValue(), 20.0);
      QCOMPARE(slider.upperValue(), 60.0);
      QCOMPARE(lowerNotifications, QList<double>({20}));
      QCOMPARE(upperNotifications, QList<double>({40, 60}));
      QCOMPARE(combinedNotifications, QList<QList<double>>({{20, 40}, {20, 60}}));
    } else {
      QCOMPARE(slider.lowerValue(), 10.0);
      QCOMPARE(slider.upperValue(), 40.0);
      QCOMPARE(lowerNotifications, QList<double>({20, 10}));
      QCOMPARE(upperNotifications, QList<double>({40}));
      QCOMPARE(combinedNotifications, QList<QList<double>>({{10, 40}}));
    }
  }
}

void SliderTest::pendingRangeNotificationSurvivesOwnerDeletion() {
  QPointer<AdRangeSlider> slider = new AdRangeSlider;
  int combinedNotifications = 0;
  connect(slider, &AdRangeSlider::lowerValueChanged, this, [&](double value) {
    if (value == 20) slider->setLowerValue(10);
  });
  connect(slider, &AdRangeSlider::upperValueChanged, this, [&] { delete slider.data(); });
  connect(slider, &AdRangeSlider::valuesChanged, this, [&] { ++combinedNotifications; });

  slider->setValues(20, 40);

  QVERIFY(slider.isNull());
  QCOMPARE(combinedNotifications, 0);
}

void SliderTest::outerSettersSurviveOwnerDeletion() {
  for (const int setter : {0, 1, 2, 3, 4, 5, 6}) {
    QPointer<ModeSlider> slider = new ModeSlider;
    slider->setHandleValues({20, 40});
    AdMultiSlider::MarkMap marks;
    marks[0].label = QStringLiteral("Zero");
    marks[50].label = QStringLiteral("Half");
    marks[100].label = QStringLiteral("Full");
    slider->setMarks(marks);
    connect(slider, &AdMultiSlider::handleValuesChanged, this, [&] { delete slider.data(); });
    if (setter == 0) {
      slider->setRange(30, 100);
    } else if (setter == 1) {
      slider->setMinimum(30);
    } else if (setter == 2) {
      slider->setMaximum(10);
    } else if (setter == 3) {
      slider->setSingleStep(30);
    } else if (setter == 4) {
      slider->setMarkSnapEnabled(true);
    } else if (setter == 5) {
      slider->setMarkStepSnapEnabled(true);
    } else {
      slider->setMode(AdMultiSlider::Mode::Single);
    }
    QVERIFY(slider.isNull());
  }
}

void SliderTest::interactionNotificationsSurviveOwnerDeletion() {
  for (const int signal : {0, 1, 2, 3, 4}) {
    QPointer<AdSlider> slider = new AdSlider;
    slider->setTooltipEnabled(false);
    slider->resize(200, 40);
    slider->setValue(50);
    const auto deleteOwner = [&] { delete slider.data(); };
    if (signal == 0) {
      connect(slider, &AdSlider::actionTriggered, this, deleteOwner);
    } else if (signal == 1) {
      connect(slider, &AdMultiSlider::valueChanged, this, deleteOwner);
    } else if (signal == 2) {
      connect(slider, &AdSlider::sliderMoved, this, deleteOwner);
    } else if (signal == 3) {
      connect(slider, &AdSlider::sliderPressed, this, deleteOwner);
    } else {
      connect(slider, &AdMultiSlider::sliderDownChanged, this, deleteOwner);
    }
    if (signal < 3) {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
      QCoreApplication::sendEvent(slider, &event);
      QVERIFY(event.isAccepted());
    } else {
      const QPointF point(100, 20);
      QMouseEvent event(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(slider, &event);
      QVERIFY(event.isAccepted());
    }
    QVERIFY(slider.isNull());
  }
  for (const int signal : {0, 1, 2}) {
    QPointer<AdSlider> slider = new AdSlider;
    slider->setTooltipEnabled(false);
    slider->setWheelEnabled(true);
    slider->setValue(50);
    const auto deleteOwner = [&] { delete slider.data(); };
    if (signal == 0)
      connect(slider, &AdSlider::actionTriggered, this, deleteOwner);
    else if (signal == 1)
      connect(slider, &AdMultiSlider::valueChanged, this, deleteOwner);
    else
      connect(slider, &AdSlider::sliderMoved, this, deleteOwner);
    QWheelEvent event(QPointF(100, 20), QPointF(100, 20), QPoint(), QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(slider, &event);
    QVERIFY(slider.isNull());
    QVERIFY(event.isAccepted());
  }
  {
    QPointer<AdSlider> slider = new AdSlider;
    slider->setTooltipEnabled(false);
    slider->setValue(50);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(slider, &press);
    connect(slider, &AdSlider::editingFinished, this, [&] { delete slider.data(); });
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(slider, &release);
    QVERIFY(slider.isNull());
    QVERIFY(release.isAccepted());
  }
}

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

void SliderTest::markedSliderFitsItsLabelsInVerticalForm() {
  adqt::widgets::AdForm form;
  form.setFormLayout(adqt::widgets::AdForm::FormLayout::Vertical);
  auto* slider = new AdSlider(&form);
  applyTestStyle(slider);
  constexpr int markGap = 24;
  auto tokens = slider->componentTokens();
  tokens.markGap = markGap;
  slider->setComponentTokens(tokens);
  form.addField(QStringLiteral("Quality"), slider);

  const int railHeight = slider->sizeHint().height();
  AdSlider::Mark mark;
  mark.label = QStringLiteral("100%");
  QFont markFont = slider->font();
  markFont.setPixelSize(24);
  mark.font = markFont;
  mark.color = QColor(Qt::magenta);
  slider->setMarks({{slider->maximum(), mark}});

  form.show();
  form.adjustSize();
  const QImage image = renderSlider(slider);
  const int markTop = qCeil(railHeight / 2.0) + markGap;
  QCOMPARE(slider->height(), markTop + QFontMetrics(markFont).height());
  const qreal dpr = image.devicePixelRatio();
  QVERIFY(isRailPixel(image.pixelColor(image.width() / 2, qFloor(railHeight / 2.0 * dpr))));

  bool labelPainted = false;
  for (int y = qFloor(markTop * dpr); y < image.height(); ++y) {
    for (int x = image.width() / 2; x < image.width(); ++x) {
      const QColor pixel = image.pixelColor(x, y);
      labelPainted |= pixel.red() > 180 && pixel.green() < 100 && pixel.blue() > 180;
    }
  }
  QVERIFY(labelPainted);
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
