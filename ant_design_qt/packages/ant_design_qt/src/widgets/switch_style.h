#pragma once

#include "switch.h"

#include <QColor>
#include <QFont>
#include <QRectF>

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct SwitchMetrics {
  int trackHeight = 22;
  int trackHeightSmall = 16;
  int trackMinWidth = 44;
  int trackMinWidthSmall = 28;
  int trackPadding = 2;
  int thumbSize = 18;
  int thumbSizeSmall = 12;
  int contentInsetNear = 9;
  int contentInsetFar = 24;
  int contentInsetNearSmall = 6;
  int contentInsetFarSmall = 18;
  int loadingIndicatorSize = 10;
  int fontSize = 12;
  int contentGap = 4;
  int animationDurationMs = 200;
  qreal disabledOpacity = 0.65;
  qreal focusRingWidth = 3.0;
  qreal focusRingOffset = 1.0;
  QColor thumbShadowColor;
  qreal thumbShadowOffsetY = 2.0;
  qreal thumbActiveStretchRatio = 0.3;
  int contentPressOffset = 4;
  int contentPressOffsetSmall = 2;
};

struct SwitchAppearance {
  QColor uncheckedTrackColor;
  QColor uncheckedTrackHoverColor;
  QColor checkedTrackColor;
  QColor checkedTrackHoverColor;
  QColor trackBorderColor;
  QColor thumbColor;
  QColor thumbBorderColor;
  QColor contentColor;
  QColor loadingIndicatorColor;
  QColor checkedLoadingIndicatorColor;
  QColor waveColor;
  QColor focusRingColor;
  SwitchMetrics metrics;
};

struct SwitchAppearanceInput {
  AdSwitch::ControlSize controlSize = AdSwitch::ControlSize::Medium;
  bool checked = false;
  bool loading = false;
  bool disabled = false;
  bool hovered = false;
  bool pressed = false;
  bool focused = false;
  AdSwitch::ComponentTokens componentTokens;
};

struct SwitchGeometry {
  QRectF trackRect;
  QRectF thumbRect;
  QRectF thumbVisualRect;
  qreal trackRadius = 0.0;
};

SwitchAppearance resolveSwitchAppearance(const SwitchAppearanceInput& input,
                                         const adqt::theme::ResolvedTheme& resolvedTheme);
SwitchAppearance resolveSwitchAppearance(const SwitchAppearanceInput& input);

SwitchGeometry buildSwitchGeometry(const QRectF& bounds, Qt::LayoutDirection direction,
                                   const SwitchAppearance& appearance,
                                   AdSwitch::ControlSize controlSize, qreal thumbProgress,
                                   qreal pressProgress, qreal pressDirection,
                                   qreal devicePixelRatio = 1.0);

int switchContentWidth(const QString& text, bool hasIcon, const SwitchAppearance& appearance,
                       const QFont& baseFont);

}  // namespace adqt::widgets::detail
