#pragma once

#include <QColor>

#include "image.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct ImageMetrics {
  int borderRadius = 0;
  int focusRingWidth = 2;
  int coverPadding = 8;
  int operationIconSize = 18;
  int operationButtonSize = 42;
  int footerPadding = 12;
  int actionsHorizontalPadding = 24;
  int actionsGap = 12;
  int footerGap = 16;
  int controlOffset = 12;
  int footerBottomOffset = 32;
  int switchButtonSize = 42;
  int closeButtonSize = 42;
  int zIndexPopup = 1080;
};

struct ImageVisualStyle {
  QColor rootBackground = QColor(Qt::transparent);
  QColor rootBorder = QColor(Qt::transparent);
  QColor focusRing = QColor(Qt::transparent);
  QColor placeholderBackground = QColor("#f5f5f5");
  QColor placeholderIcon = QColor("#bfbfbf");
  QColor coverBackground = QColor(0, 0, 0, 76);
  QColor coverText = QColor("#ffffff");
  QColor popupMask = QColor(0, 0, 0, 115);
  QColor popupBodyBackground = QColor(Qt::transparent);
  QColor popupFooterText = QColor("#ffffff");
  QColor popupActionsBackground = QColor(0, 0, 0, 28);
  QColor operationColor = QColor(255, 255, 255, 166);
  QColor operationHoverColor = QColor(255, 255, 255, 217);
  QColor operationDisabledColor = QColor(255, 255, 255, 64);
  QColor operationBorder = QColor(Qt::transparent);
  ImageMetrics metrics;
};

struct ImageStyleInput {
  AdImage::ComponentTokens componentTokens;
  AdImage::SemanticStyles semanticStyles;
};

struct ImageViewerStyleInput {
  AdImageViewer::ComponentTokens componentTokens;
  AdImageViewer::SemanticStyles semanticStyles;
  bool maskVisible = true;
};

ImageVisualStyle resolveImageVisualStyle(const ImageStyleInput& input,
                                         const adqt::theme::ResolvedTheme& resolvedTheme);
ImageVisualStyle resolveImageVisualStyle(const ImageStyleInput& input);
ImageVisualStyle resolveImageViewerVisualStyle(const ImageViewerStyleInput& input,
                                               const adqt::theme::ResolvedTheme& resolvedTheme);
ImageVisualStyle resolveImageViewerVisualStyle(const ImageViewerStyleInput& input);

}  // namespace adqt::widgets::detail
