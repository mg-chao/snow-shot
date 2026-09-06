#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTFLOATINGTOOLPALETTENATIVE_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTFLOATINGTOOLPALETTENATIVE_H

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QtGui/qwindowdefs.h>
#include <Qt>
#include <QtGlobal>

class QWidget;

namespace screenshot_floating_palette_native {
[[nodiscard]] Qt::WindowFlags windowFlags();
[[nodiscard]] bool currentPhysicalCursorPosition(QPointF* position);
[[nodiscard]] bool currentWindowGeometry(WId windowId, QRect* geometry);
[[nodiscard]] bool moveWindowTo(WId windowId, const QPoint& topLeft);
[[nodiscard]] bool setKeyboardFocusEnabled(WId windowId, bool enabled);
[[nodiscard]] bool activateWindow(WId windowId);
void setNativePaletteOwner(WId windowId, QWidget* owner);
} // namespace screenshot_floating_palette_native

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTFLOATINGTOOLPALETTENATIVE_H
