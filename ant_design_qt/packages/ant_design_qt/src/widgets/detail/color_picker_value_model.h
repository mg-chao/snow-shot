#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QVector>

#include "../color_picker.h"

namespace adqt::widgets::detail {

class ColorPickerValueModel {
 public:
  using Mode = AdColorPicker::Mode;
  using Format = AdColorPicker::Format;

  struct State {
    AdColorSelection selection;
    Mode mode = Mode::Solid;
    QVector<Mode> modeOptions = {Mode::Solid};
    int activeStopIndex = 0;
  };

  static QVector<Mode> normalizeModeOptions(const QVector<Mode>& options);
  static QVector<AdColorGradientStop> normalizeStops(const QVector<AdColorGradientStop>& stops,
                                                     const QColor& fallbackColor = QColor());
  static State normalizedState(const State& state);
  static State stateFromSelection(const AdColorSelection& selection,
                                  const QVector<Mode>& modeOptions, Mode preferredMode,
                                  int activeStopIndex = 0);
  static State stateFromCssValue(const QString& cssValue, const QVector<Mode>& modeOptions,
                                 Mode preferredMode, int activeStopIndex = 0, bool* ok = nullptr);
  static State withMode(const State& state, Mode mode);
  static State withModeOptions(const State& state, const QVector<Mode>& modeOptions);
  static State withEditableColor(const State& state, const QColor& color);
  static State withGradientStopPositions(const State& state, const QList<double>& values);
  static State clearedState(const State& state);

  static QColor editableColor(const State& state);
  static QString cssValue(const State& state);
  static QString cssValue(const AdColorSelection& selection);
  static QString formattedValue(const State& state, Format format);
  static QString formattedValue(const AdColorSelection& selection, Format format,
                                int activeStopIndex = 0);

  static QColor parseCssColor(const QString& value, bool* ok = nullptr);
  static AdColorSelection parseCssValue(const QString& value, bool* ok = nullptr);
  static QString colorToString(const QColor& color, Format format);
  static QString colorToCss(const QColor& color);
  static QString formattedColorString(const QColor& color, Format format);
};

}  // namespace adqt::widgets::detail
