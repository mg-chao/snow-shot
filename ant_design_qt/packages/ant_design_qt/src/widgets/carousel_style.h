#pragma once

#include "carousel.h"

#include <QColor>

namespace adqt::widgets::detail {

struct CarouselMetrics {
  int arrowSize = 16;
  int arrowOffset = 8;
  int dotWidth = 16;
  int dotHeight = 3;
  int dotGap = 4;
  int dotOffset = 12;
  int dotActiveWidth = 24;
  int hitTargetSize = 24;
  int focusOutlineWidth = 2;
  int dragThreshold = 40;
};

struct CarouselAppearance {
  QColor arrow;
  QColor dot;
  QColor focusOutline;
  CarouselMetrics metrics;
  bool motionEnabled = true;
};

CarouselAppearance resolveCarouselAppearance(const AdCarousel* carousel,
                                             const AdCarousel::ComponentTokens& tokens);

}  // namespace adqt::widgets::detail
