#include "carousel_style.h"

#include "theme/theme.h"

#include <algorithm>

namespace adqt::widgets::detail {

namespace {

QColor colorOr(const std::optional<QColor>& value, const QColor& fallback) {
  return value && value->isValid() ? *value : fallback;
}

int intOr(const std::optional<int>& value, int fallback, int minimum = 0) {
  return std::max(minimum, value.value_or(fallback));
}

}  // namespace

CarouselAppearance resolveCarouselAppearance(const AdCarousel* carousel,
                                             const AdCarousel::ComponentTokens& tokens) {
  const adqt::theme::ResolvedTheme theme = adqt::theme::ThemeManager::instance().resolve(carousel);

  CarouselAppearance result;
  result.arrow = colorOr(tokens.colors.arrowColor, theme.values.colorBgContainer);
  result.dot = colorOr(tokens.colors.dotColor, theme.values.colorBgContainer);
  result.focusOutline = colorOr(tokens.colors.focusOutline, theme.values.colorPrimaryBorder);

  result.metrics.arrowSize = intOr(tokens.metrics.arrowSize, 16, 4);
  result.metrics.arrowOffset = intOr(tokens.metrics.arrowOffset, qRound(theme.values.sizeXS));
  result.metrics.dotWidth = intOr(tokens.metrics.dotWidth, 16, 1);
  result.metrics.dotHeight = intOr(tokens.metrics.dotHeight, 3, 1);
  result.metrics.dotGap = intOr(tokens.metrics.dotGap, qRound(theme.values.sizeXXS));
  result.metrics.dotOffset = intOr(tokens.metrics.dotOffset, 12);
  result.metrics.dotActiveWidth = intOr(tokens.metrics.dotActiveWidth, 24, result.metrics.dotWidth);
  result.metrics.hitTargetSize = intOr(tokens.metrics.hitTargetSize, 24, result.metrics.arrowSize);
  result.metrics.focusOutlineWidth = intOr(tokens.metrics.focusOutlineWidth, 2, 1);
  result.metrics.dragThreshold = intOr(tokens.metrics.dragThreshold, 40, 1);
  result.motionEnabled = theme.values.motion;
  return result;
}

}  // namespace adqt::widgets::detail
