#pragma once

#include <QFlags>
#include <QObject>
#include <Qt>

namespace adqt::widgets {

Q_NAMESPACE

enum class AdPopupPlacement {
  Top,
  TopLeft,
  TopRight,
  Bottom,
  BottomLeft,
  BottomRight,
  Left,
  LeftTop,
  LeftBottom,
  Right,
  RightTop,
  RightBottom,
};
Q_ENUM_NS(AdPopupPlacement)

enum class AdPopupTrigger {
  Hover = 0x1,
  Focus = 0x2,
  Click = 0x4,
  ContextMenu = 0x8,
};
Q_ENUM_NS(AdPopupTrigger)
Q_DECLARE_FLAGS(AdPopupTriggers, AdPopupTrigger)
Q_FLAG_NS(AdPopupTriggers)

enum class AdPopupActivationMode {
  Automatic,
  Manual,
};
Q_ENUM_NS(AdPopupActivationMode)

enum class AdPopupLifetime {
  Retained,
  RecreateOnOpen,
};
Q_ENUM_NS(AdPopupLifetime)

enum class AdPopupLayerMode {
  InWindow,
  QtTool,
};
Q_ENUM_NS(AdPopupLayerMode)

inline Qt::WindowFlags adQtToolWindowFlags() {
  return Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
}

}  // namespace adqt::widgets

Q_DECLARE_OPERATORS_FOR_FLAGS(adqt::widgets::AdPopupTriggers)
Q_DECLARE_METATYPE(adqt::widgets::AdPopupPlacement)
Q_DECLARE_METATYPE(adqt::widgets::AdPopupTrigger)
Q_DECLARE_METATYPE(adqt::widgets::AdPopupTriggers)
Q_DECLARE_METATYPE(adqt::widgets::AdPopupActivationMode)
Q_DECLARE_METATYPE(adqt::widgets::AdPopupLifetime)
Q_DECLARE_METATYPE(adqt::widgets::AdPopupLayerMode)
