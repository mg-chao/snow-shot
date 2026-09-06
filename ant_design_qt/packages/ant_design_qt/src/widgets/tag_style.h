#pragma once

#include <QColor>
#include <QFont>
#include <Qt>

#include "tag.h"

namespace adqt::theme {
struct ResolvedTheme;
}

namespace adqt::widgets::detail {

struct TagMetrics {
  int height = 20;
  int borderRadius = 4;
  int borderWidth = 1;
  int paddingHorizontal = 7;
  int iconSize = 12;
  int contentGap = 4;
  int closeGap = 3;
  QFont font;
  QColor focusOutlineColor;
  qreal focusOutlineWidth = 3.0;
  qreal focusOutlineOffset = 1.0;
  QColor waveColor;
};

struct TagVisualStyle {
  QColor backgroundColor;
  QColor borderColor;
  QColor contentColor;
  QColor iconColor;
  QColor closeColor;
  QColor closeHoverColor;
  QColor closeHoverBackground;
  QColor focusOutlineColor;
  Qt::PenStyle borderPenStyle = Qt::SolidLine;
  TagMetrics metrics;
};

struct TagStyleInput {
  AdTag::Variant variant = AdTag::Variant::Filled;
  AdTag::BorderStyle borderStyle = AdTag::BorderStyle::Solid;
  AdTag::ColorScheme colorScheme = AdTag::ColorScheme::Default;
  QColor customColor;
  bool checkable = false;
  bool checked = false;
  bool closable = false;
  bool disabled = false;
  bool hovered = false;
  bool pressed = false;
  bool closeHovered = false;
  QFont baseFont;
  AdTag::ComponentTokens componentTokens;
  AdTag::SemanticStyles semanticStyles;
};

TagVisualStyle resolveTagVisualStyle(const TagStyleInput& input,
                                     const adqt::theme::ResolvedTheme& resolvedTheme);
TagVisualStyle resolveTagVisualStyle(const TagStyleInput& input);

}  // namespace adqt::widgets::detail
