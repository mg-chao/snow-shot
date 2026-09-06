#pragma once

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QRectF>

#include "pagination.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct PaginationVisualStyle {
  QColor rootBackground;
  QColor rootBorder;
  bool hasRootBorder = false;
  QColor text;
  QColor itemBackground;
  QColor itemHoverBackground;
  QColor itemPressedBackground;
  QColor activeBackground;
  QColor activeText;
  QColor activeHoverText;
  QColor activeBorder;
  QColor disabledText;
  QColor activeDisabledBackground;
  QColor focusOutline;
  int itemSize = 32;
  int spacing = 8;
  int radius = 6;
  int borderWidth = 1;
  int quickJumperWidth = 50;
  QFont font;
};

struct PaginationStyleInput {
  AdPagination::ControlSize controlSize = AdPagination::ControlSize::Medium;
  QFont baseFont;
  AdPagination::ComponentTokens componentTokens;
  AdPagination::SemanticStyles semanticStyles;
};

PaginationVisualStyle resolvePaginationVisualStyle(const PaginationStyleInput& input,
                                                   const adqt::theme::ResolvedTheme& resolvedTheme);
QRectF deviceAlignedPaginationBorderRect(const QRectF& bounds, qreal borderWidth, qreal dpr,
                                         const QPointF& origin = QPointF());

}  // namespace adqt::widgets::detail
