#include "widgets/color_picker.h"
#include "widgets/slider.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPixmap>
#include <QTextStream>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

using adqt::widgets::AdColorPicker;
using adqt::widgets::AdSlider;

int firstShowIterations() {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue("ADQT_PERF_FIRST_SHOW_ITERATIONS", &ok);
  return ok ? std::max(1, value) : 40;
}

int dragFrames() {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue("ADQT_PERF_DRAG_FRAMES", &ok);
  return ok ? std::max(1, value) : 240;
}

int minimumDragFps() {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue("ADQT_PERF_MIN_DRAG_FPS", &ok);
  return ok ? std::max(1, value) : 0;
}

void flushPaints() {
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
  QCoreApplication::processEvents(QEventLoop::AllEvents);
}

void forceRender(QWidget* widget) {
  if (!widget || widget->size().isEmpty()) {
    return;
  }
  QPixmap pixmap(widget->size() * widget->devicePixelRatioF());
  pixmap.setDevicePixelRatio(widget->devicePixelRatioF());
  pixmap.fill(Qt::transparent);
  widget->render(&pixmap);
}

QWidget* findVisibleWidget(const QString& objectName, QWidget* expectedWindow = nullptr) {
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget* widget : widgets) {
    if (widget && widget->objectName() == objectName && widget->isVisible() &&
        (!expectedWindow || widget->window() == expectedWindow->window())) {
      return widget;
    }
  }
  return nullptr;
}

struct Samples {
  QString name;
  QVector<double> milliseconds;
};

double percentile(QVector<double> values, double ratio) {
  if (values.isEmpty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const int index =
      std::clamp(static_cast<int>(std::ceil(ratio * static_cast<double>(values.size()))) - 1, 0,
                 static_cast<int>(values.size()) - 1);
  return values.at(index);
}

double meanFps(const Samples& samples) {
  if (samples.milliseconds.isEmpty()) {
    return 0.0;
  }

  double total = 0.0;
  for (double value : samples.milliseconds) {
    total += value;
  }
  const double mean = total / static_cast<double>(samples.milliseconds.size());
  return mean > 0.0 ? 1000.0 / mean : 0.0;
}

void printSamples(QTextStream& out, const Samples& samples) {
  const double median = percentile(samples.milliseconds, 0.50);
  const double p95 = percentile(samples.milliseconds, 0.95);
  const double worst = percentile(samples.milliseconds, 1.0);
  double total = 0.0;
  for (double value : samples.milliseconds) {
    total += value;
  }
  const double mean = samples.milliseconds.isEmpty()
                          ? 0.0
                          : total / static_cast<double>(samples.milliseconds.size());
  const double fps = meanFps(samples);
  out << samples.name << ": samples=" << samples.milliseconds.size()
      << " mean_ms=" << QString::number(mean, 'f', 3)
      << " median_ms=" << QString::number(median, 'f', 3)
      << " p95_ms=" << QString::number(p95, 'f', 3)
      << " worst_ms=" << QString::number(worst, 'f', 3)
      << " mean_fps=" << QString::number(fps, 'f', 1) << Qt::endl;
}

void sendMouse(QWidget* widget, QEvent::Type type, const QPointF& localPosition,
               Qt::MouseButton button, Qt::MouseButtons buttons);

struct FirstShowSamples {
  Samples total;
  Samples openCall;
  Samples eventPaint;
  Samples diagnosticRender;
  int prewarmed = 0;
  int failures = 0;
};

FirstShowSamples benchmarkFirstShow(AdColorPicker::PopupLayerMode mode, const QString& name) {
  FirstShowSamples result{{name, {}},
                          {name + QStringLiteral("_open_call"), {}},
                          {name + QStringLiteral("_event_paint"), {}},
                          {name + QStringLiteral("_diagnostic_render"), {}},
                          0,
                          0};
  const int iterations = firstShowIterations();
  result.total.milliseconds.reserve(iterations);
  result.openCall.milliseconds.reserve(iterations);
  result.eventPaint.milliseconds.reserve(iterations);
  result.diagnosticRender.milliseconds.reserve(iterations);

  for (int i = 0; i < iterations + 3; ++i) {
    QWidget host;
    host.resize(640, 480);
    auto* layout = new QHBoxLayout(&host);
    auto* picker = new AdColorPicker(&host);
    picker->setPopupLayerMode(mode);
    layout->addWidget(picker);
    const QWidgetList widgetsBeforeShow = QApplication::allWidgets();
    host.show();
    flushPaints();

    auto findOwnedPanel = [&widgetsBeforeShow]() -> QWidget* {
      const QWidgetList widgets = QApplication::allWidgets();
      for (QWidget* widget : widgets) {
        if (widget && widget->objectName() == QStringLiteral("ad-color-picker-panel-host") &&
            !widgetsBeforeShow.contains(widget)) {
          return widget;
        }
      }
      return nullptr;
    };
    QWidget* panelHost = findOwnedPanel();
    const bool prewarmed = panelHost != nullptr;
    QWidget* trigger = picker->findChild<QWidget*>(QStringLiteral("ad-color-picker-trigger-frame"));
    const QPointF triggerCenter = trigger ? QPointF(trigger->rect().center()) : QPointF();
    sendMouse(trigger, QEvent::MouseButtonPress, triggerCenter, Qt::LeftButton, Qt::LeftButton);
    flushPaints();

    QElapsedTimer callTimer;
    callTimer.start();
    sendMouse(trigger, QEvent::MouseButtonRelease, triggerCenter, Qt::LeftButton, Qt::NoButton);
    const double callMs = static_cast<double>(callTimer.nsecsElapsed()) / 1000000.0;

    QElapsedTimer paintTimer;
    paintTimer.start();
    flushPaints();
    const double paintMs = static_cast<double>(paintTimer.nsecsElapsed()) / 1000000.0;

    if (!panelHost) {
      panelHost = findOwnedPanel();
    }
    const bool openedCorrectly = picker->popupVisible() && panelHost && panelHost->isVisible() &&
                                 !panelHost->size().isEmpty();
    const QString screenshotDir = qEnvironmentVariable("ADQT_PERF_SCREENSHOT_DIR");
    if (i == 3 && openedCorrectly && !screenshotDir.isEmpty()) {
      QDir().mkpath(screenshotDir);
      panelHost->window()->grab().save(QDir(screenshotDir).filePath(name + QStringLiteral(".png")));
    }
    QElapsedTimer renderTimer;
    renderTimer.start();
    forceRender(panelHost);
    const double renderMs = static_cast<double>(renderTimer.nsecsElapsed()) / 1000000.0;

    picker->setPopupVisible(false);
    flushPaints();
    if (i >= 3) {
      result.total.milliseconds.append(callMs + paintMs);
      result.openCall.milliseconds.append(callMs);
      result.eventPaint.milliseconds.append(paintMs);
      result.diagnosticRender.milliseconds.append(renderMs);
      result.prewarmed += prewarmed ? 1 : 0;
      result.failures += openedCorrectly ? 0 : 1;
    }
  }
  return result;
}

void printFirstShowSamples(QTextStream& out, const FirstShowSamples& samples) {
  printSamples(out, samples.total);
  printSamples(out, samples.openCall);
  printSamples(out, samples.eventPaint);
  printSamples(out, samples.diagnosticRender);
  out << samples.total.name << "_prewarmed: " << samples.prewarmed << "/"
      << samples.total.milliseconds.size() << Qt::endl;
  out << samples.total.name << "_correctness_failures: " << samples.failures << Qt::endl;
}

void sendMouse(QWidget* widget, QEvent::Type type, const QPointF& localPosition,
               Qt::MouseButton button, Qt::MouseButtons buttons) {
  if (!widget) {
    return;
  }
  const QPointF globalPosition = widget->mapToGlobal(localPosition.toPoint());
  QMouseEvent event(type, localPosition, globalPosition, button, buttons, Qt::NoModifier);
  QCoreApplication::sendEvent(widget, &event);
}

Samples benchmarkDrag(QWidget* target, const QString& name,
                      const std::function<QPointF(int, const QSize&)>& positionForFrame) {
  constexpr int kWarmupFrames = 20;
  Samples result{name, {}};
  if (!target || target->size().isEmpty()) {
    return result;
  }
  const int frames = dragFrames();
  result.milliseconds.reserve(frames);

  const QPointF start = target->rect().center();
  sendMouse(target, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  flushPaints();

  for (int i = 0; i < frames + kWarmupFrames; ++i) {
    const QPointF position =
        i < kWarmupFrames ? start : positionForFrame(i - kWarmupFrames, target->size());
    QElapsedTimer timer;
    timer.start();
    sendMouse(target, QEvent::MouseMove, position, Qt::NoButton, Qt::LeftButton);
    flushPaints();
    const double elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    if (i >= kWarmupFrames) {
      result.milliseconds.append(elapsedMs);
    }
  }

  const QPointF end = positionForFrame(frames - 1, target->size());
  sendMouse(target, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
  flushPaints();
  return result;
}

enum class DragSurface {
  Saturation,
  Hue,
  Alpha,
};

struct DragResult {
  Samples samples;
  int valueChanges = 0;
};

DragResult benchmarkDragSurface(DragSurface surface, const QString& name,
                                const std::function<QPointF(int, const QSize&)>& positionForFrame) {
  QWidget host;
  host.resize(640, 480);
  auto* layout = new QHBoxLayout(&host);
  auto* picker = new AdColorPicker(&host);
  picker->setCssText(QStringLiteral("#1677ff"));
  picker->setPopupLayerMode(AdColorPicker::PopupLayerMode::InWindow);
  layout->addWidget(picker);
  host.show();
  flushPaints();
  picker->setPopupVisible(true);
  flushPaints();

  QWidget* panelHost = findVisibleWidget(QStringLiteral("ad-color-picker-panel-host"), &host);
  QWidget* saturation =
      panelHost ? panelHost->findChild<QWidget*>(QStringLiteral("ad-color-picker-saturation-panel"))
                : nullptr;
  QWidget* alphaSection =
      panelHost ? panelHost->findChild<QWidget*>(QStringLiteral("ad-color-picker-alpha-section"))
                : nullptr;
  AdSlider* alphaSlider = alphaSection ? alphaSection->findChild<AdSlider*>() : nullptr;
  AdSlider* hueSlider = nullptr;
  const QList<AdSlider*> sliders =
      panelHost ? panelHost->findChildren<AdSlider*>() : QList<AdSlider*>();
  for (AdSlider* slider : sliders) {
    if (slider != alphaSlider) {
      hueSlider = slider;
      break;
    }
  }

  QWidget* target = saturation;
  if (surface == DragSurface::Hue) {
    target = hueSlider;
  } else if (surface == DragSurface::Alpha) {
    target = alphaSlider;
  }

  DragResult result{{name, {}}, 0};
  QObject::connect(picker, &AdColorPicker::valueChanged, picker,
                   [&result](const adqt::widgets::AdColorValue&) { ++result.valueChanges; });
  result.samples = benchmarkDrag(target, name, positionForFrame);
  picker->setPopupVisible(false);
  flushPaints();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
  QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);

  QTextStream out(stdout);
  const FirstShowSamples inWindowFirstShow = benchmarkFirstShow(
      AdColorPicker::PopupLayerMode::InWindow, QStringLiteral("first_show_in_window"));
  const FirstShowSamples qtToolFirstShow = benchmarkFirstShow(AdColorPicker::PopupLayerMode::QtTool,
                                                              QStringLiteral("first_show_qt_tool"));
  printFirstShowSamples(out, inWindowFirstShow);
  printFirstShowSamples(out, qtToolFirstShow);

  const auto sweep2d = [](int frame, const QSize& size) {
    const int period = 120;
    const double phase = static_cast<double>(frame % period) / static_cast<double>(period - 1);
    const double xRatio = (frame / period) % 2 == 0 ? phase : 1.0 - phase;
    const double yRatio = 0.5 + 0.4 * std::sin(static_cast<double>(frame) * 0.13);
    return QPointF(1.0 + xRatio * std::max(1, size.width() - 2),
                   1.0 + yRatio * std::max(1, size.height() - 2));
  };
  const auto sweep1d = [](int frame, const QSize& size) {
    const int period = 120;
    const double phase = static_cast<double>(frame % period) / static_cast<double>(period - 1);
    const double ratio = (frame / period) % 2 == 0 ? phase : 1.0 - phase;
    return QPointF(1.0 + ratio * std::max(1, size.width() - 2), size.height() / 2.0);
  };

  const DragResult saturationDrag =
      benchmarkDragSurface(DragSurface::Saturation, QStringLiteral("drag_saturation"), sweep2d);
  const DragResult hueDrag =
      benchmarkDragSurface(DragSurface::Hue, QStringLiteral("drag_hue"), sweep1d);
  const DragResult alphaDrag =
      benchmarkDragSurface(DragSurface::Alpha, QStringLiteral("drag_alpha"), sweep1d);
  printSamples(out, saturationDrag.samples);
  printSamples(out, hueDrag.samples);
  printSamples(out, alphaDrag.samples);
  out << "drag_value_changes: saturation=" << saturationDrag.valueChanges
      << " hue=" << hueDrag.valueChanges << " alpha=" << alphaDrag.valueChanges << Qt::endl;

  const bool correctnessPassed = inWindowFirstShow.failures == 0 && qtToolFirstShow.failures == 0 &&
                                 saturationDrag.samples.milliseconds.size() == dragFrames() &&
                                 hueDrag.samples.milliseconds.size() == dragFrames() &&
                                 alphaDrag.samples.milliseconds.size() == dragFrames() &&
                                 saturationDrag.valueChanges > 0 && hueDrag.valueChanges > 0 &&
                                 alphaDrag.valueChanges > 0;
  const int minimumFps = minimumDragFps();
  const bool performancePassed =
      minimumFps == 0 ||
      (meanFps(saturationDrag.samples) >= minimumFps && meanFps(hueDrag.samples) >= minimumFps &&
       meanFps(alphaDrag.samples) >= minimumFps);
  out << "drag_performance_min_fps=" << minimumFps << Qt::endl;
  out << "drag_performance_status="
      << (minimumFps == 0     ? "disabled"
          : performancePassed ? "pass"
                              : "fail")
      << Qt::endl;
  out << "correctness_status=" << (correctnessPassed ? "pass" : "fail") << Qt::endl;
  return correctnessPassed && performancePassed ? 0 : 2;
}
