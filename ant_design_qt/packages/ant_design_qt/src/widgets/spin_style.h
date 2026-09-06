#pragma once

#include <QColor>
#include <QFont>

#include "spin.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct SpinVisualStyle {
  QColor rootBackground;
  QColor indicator;
  QColor description;
  QColor progressTrack;
  QColor containerOverlay;
  QColor fullscreenMask;
  QColor fullscreenIndicator;
  QColor fullscreenDescription;
  QColor sectionBackground;
  QColor indicatorBackground;
  QColor descriptionBackground;
  QFont font;
  int dotSize = 20;
  int descriptionGap = 8;
  int animationCycleMs = 1200;
  int autoProgressIntervalMs = 200;
  bool hasRootBackground = false;
  bool hasSectionBackground = false;
  bool hasIndicatorBackground = false;
  bool hasDescriptionBackground = false;
};

struct SpinStyleInput {
  AdSpin::SizeClass sizeClass = AdSpin::SizeClass::Medium;
  QFont baseFont;
  AdSpin::ComponentTokens componentTokens;
  AdSpin::SemanticStyles semanticStyles;
};

SpinVisualStyle resolveSpinVisualStyle(const SpinStyleInput& input,
                                       const adqt::theme::ResolvedTheme& resolvedTheme);

}  // namespace adqt::widgets::detail
