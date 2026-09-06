#pragma once

#include <QColor>
#include <QGradient>
#include <QMetaType>
#include <QtGlobal>
#include <QVector>

namespace adqt::widgets {

enum class AdColorSelectionKind {
  Empty,
  Solid,
  Gradient,
};

struct AdColorGradientStop {
  QColor color;
  int percent = 0;

  bool operator==(const AdColorGradientStop& other) const {
    return color == other.color && percent == other.percent;
  }

  bool operator!=(const AdColorGradientStop& other) const { return !(*this == other); }
};

struct AdColorSelection {
  AdColorSelectionKind kind = AdColorSelectionKind::Empty;
  QColor solidColor;
  QVector<AdColorGradientStop> gradientStops;

  bool isEmpty() const { return kind == AdColorSelectionKind::Empty; }
  bool isSolid() const { return kind == AdColorSelectionKind::Solid; }
  bool isGradient() const { return kind == AdColorSelectionKind::Gradient; }

  static AdColorSelection empty(const QColor& baseColor = QColor()) {
    AdColorSelection value;
    value.kind = AdColorSelectionKind::Empty;
    value.solidColor = baseColor;
    if (value.solidColor.isValid()) {
      value.solidColor.setAlpha(0);
    }
    return value;
  }

  static AdColorSelection solid(const QColor& color) {
    AdColorSelection value;
    value.kind = AdColorSelectionKind::Solid;
    value.solidColor = color;
    return value;
  }

  static AdColorSelection gradient(const QVector<AdColorGradientStop>& stops) {
    AdColorSelection value;
    value.kind = AdColorSelectionKind::Gradient;
    value.gradientStops = stops;
    return value;
  }

  bool operator==(const AdColorSelection& other) const {
    return kind == other.kind && solidColor == other.solidColor &&
           gradientStops == other.gradientStops;
  }

  bool operator!=(const AdColorSelection& other) const { return !(*this == other); }
};

enum class AdColorValueKind {
  None,
  Solid,
  Gradient,
};

struct AdColorValue {
  AdColorValueKind kind = AdColorValueKind::None;
  QColor solidColor;
  QGradientStops gradientStops;

  bool isNone() const { return kind == AdColorValueKind::None; }
  bool isSolid() const { return kind == AdColorValueKind::Solid; }
  bool isGradient() const { return kind == AdColorValueKind::Gradient; }

  static AdColorValue none(const QColor& baseColor = QColor()) {
    AdColorValue value;
    value.kind = AdColorValueKind::None;
    value.solidColor = baseColor;
    if (value.solidColor.isValid()) {
      value.solidColor.setAlpha(0);
    }
    return value;
  }

  static AdColorValue solid(const QColor& color) {
    AdColorValue value;
    value.kind = AdColorValueKind::Solid;
    value.solidColor = color;
    return value;
  }

  static AdColorValue gradient(const QGradientStops& stops,
                               const QColor& editableColor = QColor()) {
    AdColorValue value;
    value.kind = AdColorValueKind::Gradient;
    value.solidColor = editableColor;
    value.gradientStops = stops;
    return value;
  }

  bool operator==(const AdColorValue& other) const {
    return kind == other.kind && solidColor == other.solidColor &&
           gradientStops == other.gradientStops;
  }

  bool operator!=(const AdColorValue& other) const { return !(*this == other); }
};

inline AdColorValue toColorValue(const AdColorSelection& selection) {
  if (selection.isEmpty()) {
    return AdColorValue::none(selection.solidColor);
  }
  if (selection.isGradient()) {
    QGradientStops stops;
    stops.reserve(selection.gradientStops.size());
    for (const AdColorGradientStop& stop : selection.gradientStops) {
      stops.append(
          QGradientStop(qBound(0.0, static_cast<double>(stop.percent) / 100.0, 1.0), stop.color));
    }
    return AdColorValue::gradient(stops, selection.solidColor);
  }
  return AdColorValue::solid(selection.solidColor);
}

inline AdColorSelection toColorSelection(const AdColorValue& value) {
  if (value.isNone()) {
    return AdColorSelection::empty(value.solidColor);
  }
  if (value.isGradient()) {
    QVector<AdColorGradientStop> stops;
    stops.reserve(value.gradientStops.size());
    for (const QGradientStop& stop : value.gradientStops) {
      stops.append(AdColorGradientStop{stop.second, qBound(0, qRound(stop.first * 100.0), 100)});
    }
    AdColorSelection selection = AdColorSelection::gradient(stops);
    selection.solidColor = value.solidColor;
    return selection;
  }
  return AdColorSelection::solid(value.solidColor);
}

}  // namespace adqt::widgets

Q_DECLARE_METATYPE(adqt::widgets::AdColorSelectionKind)
Q_DECLARE_METATYPE(adqt::widgets::AdColorGradientStop)
Q_DECLARE_METATYPE(adqt::widgets::AdColorSelection)
Q_DECLARE_METATYPE(adqt::widgets::AdColorValueKind)
Q_DECLARE_METATYPE(adqt::widgets::AdColorValue)
