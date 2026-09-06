#pragma once

class QObject;
class QString;
class QWidget;

namespace adqt::widgets::detail {

inline constexpr char kTooltipManagerProperty[] = "adqt.tooltip.manager";

void installQtTooltipBridge();

void showQtTooltip(QWidget* target, const QString& text, int displayTimeMs = -1);

void syncTopLevelPopupTooltipRoute(QObject* owner, QWidget* triggerRoot, QWidget* popupSurface,
                                   bool active);

}  // namespace adqt::widgets::detail
