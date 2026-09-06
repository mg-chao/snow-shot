#include "themed_scrollbar.h"

#include "theme/theme.h"

#include <QPalette>
#include <QPainter>
#include <QProxyStyle>
#include <QScrollBar>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>

namespace adqt::widgets::detail {

namespace {

QColor toColor(const QColor& value, const QColor& fallback) {
  return value.isValid() ? value : fallback;
}

QColor compositeOn(const QColor& foreground, const QColor& background) {
  if (!foreground.isValid()) {
    return background;
  }
  if (!background.isValid()) {
    QColor opaque = foreground;
    opaque.setAlpha(255);
    return opaque;
  }

  const float alpha = std::clamp(foreground.alphaF(), 0.0F, 1.0F);
  if (alpha >= 0.999F) {
    return foreground;
  }

  QColor mixed;
  mixed.setRedF(foreground.redF() * alpha + background.redF() * (1.0F - alpha));
  mixed.setGreenF(foreground.greenF() * alpha + background.greenF() * (1.0F - alpha));
  mixed.setBlueF(foreground.blueF() * alpha + background.blueF() * (1.0F - alpha));
  mixed.setAlpha(255);
  return mixed;
}

QColor withAlpha(const QColor& color, float alpha) {
  QColor updated = color;
  updated.setAlphaF(std::clamp(alpha, 0.0F, 1.0F));
  return updated;
}

class OverlayScrollBarStyle final : public QProxyStyle {
 public:
  explicit OverlayScrollBarStyle(QStyle* baseStyle = nullptr) : QProxyStyle(baseStyle) {}

  void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                          QPainter* painter, const QWidget* widget) const override {
    if (control != CC_ScrollBar || !option || !painter) {
      QProxyStyle::drawComplexControl(control, option, painter, widget);
      return;
    }

    const auto* sliderOption = qstyleoption_cast<const QStyleOptionSlider*>(option);
    if (!sliderOption) {
      QProxyStyle::drawComplexControl(control, option, painter, widget);
      return;
    }

    const QRect grooveRect = scrollBarGrooveRect(sliderOption, widget);
    if (!grooveRect.isValid()) {
      return;
    }

    const QRect sliderRect = scrollBarSliderRect(sliderOption, widget);
    const QColor trackColor = propertyColor(widget, "_adqt_track_color", QColor("#f0f0f0"));
    const QColor handleBaseColor = propertyColor(widget, "_adqt_handle_color", QColor("#8c8c8c"));
    const QColor handleHoverColor =
        propertyColor(widget, "_adqt_handle_hover_color", handleBaseColor);
    const QColor handlePressedColor =
        propertyColor(widget, "_adqt_handle_pressed_color", handleHoverColor);

    QColor handleColor = handleBaseColor;
    const bool sliderActive = (sliderOption->activeSubControls & SC_ScrollBarSlider) != 0;
    if (sliderActive && (sliderOption->state & State_Sunken)) {
      handleColor = handlePressedColor;
    } else if (sliderActive && (sliderOption->state & State_MouseOver)) {
      handleColor = handleHoverColor;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);

    const int grooveRadius = scrollBarRadius(widget, grooveRect);
    if (trackColor.alpha() > 0) {
      painter->setBrush(trackColor);
      painter->drawRoundedRect(grooveRect.adjusted(0, 0, -1, -1), grooveRadius, grooveRadius);
    }

    if (sliderRect.isValid() && handleColor.alpha() > 0) {
      const int handleRadius = scrollBarRadius(widget, sliderRect);
      painter->setBrush(handleColor);
      painter->drawRoundedRect(sliderRect.adjusted(0, 0, -1, -1), handleRadius, handleRadius);
    }

    painter->restore();
  }

  int pixelMetric(PixelMetric metric, const QStyleOption* option,
                  const QWidget* widget) const override {
    if (metric == PM_ScrollBarExtent && widget) {
      const int extent = widget->property("_adqt_extent").toInt();
      if (extent > 0) {
        return extent;
      }
    }
    if (metric == PM_ScrollBarSliderMin) {
      return 24;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
  }

  SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                                   const QPoint& pos, const QWidget* widget) const override {
    if (control != CC_ScrollBar || !option) {
      return QProxyStyle::hitTestComplexControl(control, option, pos, widget);
    }

    const auto* sliderOption = qstyleoption_cast<const QStyleOptionSlider*>(option);
    if (!sliderOption) {
      return QProxyStyle::hitTestComplexControl(control, option, pos, widget);
    }

    const QRect grooveRect = scrollBarGrooveRect(sliderOption, widget);
    if (!grooveRect.contains(pos)) {
      return SC_None;
    }

    const QRect sliderRect = scrollBarSliderRect(sliderOption, widget);
    if (sliderRect.isValid() && sliderRect.contains(pos)) {
      return SC_ScrollBarSlider;
    }

    const bool vertical = sliderOption->orientation == Qt::Vertical;
    if (vertical) {
      return pos.y() < sliderRect.top() ? SC_ScrollBarSubPage : SC_ScrollBarAddPage;
    }
    return pos.x() < sliderRect.left() ? SC_ScrollBarSubPage : SC_ScrollBarAddPage;
  }

  QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                       SubControl subControl, const QWidget* widget) const override {
    if (control != CC_ScrollBar) {
      return QProxyStyle::subControlRect(control, option, subControl, widget);
    }

    const auto* sliderOption = qstyleoption_cast<const QStyleOptionSlider*>(option);
    if (!sliderOption) {
      return QProxyStyle::subControlRect(control, option, subControl, widget);
    }

    if (subControl == SC_ScrollBarAddLine || subControl == SC_ScrollBarSubLine) {
      return QRect();
    }

    const QRect grooveRect = scrollBarGrooveRect(sliderOption, widget);
    const QRect sliderRect = scrollBarSliderRect(sliderOption, widget);
    if (subControl == SC_ScrollBarGroove) {
      return grooveRect;
    }
    if (subControl == SC_ScrollBarSlider) {
      return sliderRect;
    }
    if (subControl == SC_ScrollBarSubPage) {
      if (sliderOption->orientation == Qt::Vertical) {
        return QRect(grooveRect.left(), grooveRect.top(), grooveRect.width(),
                     std::max(0, sliderRect.top() - grooveRect.top()));
      }
      return QRect(grooveRect.left(), grooveRect.top(),
                   std::max(0, sliderRect.left() - grooveRect.left()), grooveRect.height());
    }
    if (subControl == SC_ScrollBarAddPage) {
      if (sliderOption->orientation == Qt::Vertical) {
        const int top = sliderRect.bottom() + 1;
        return QRect(grooveRect.left(), top, grooveRect.width(),
                     std::max(0, grooveRect.bottom() - sliderRect.bottom()));
      }
      const int left = sliderRect.right() + 1;
      return QRect(left, grooveRect.top(), std::max(0, grooveRect.right() - sliderRect.right()),
                   grooveRect.height());
    }

    return QRect();
  }

 private:
  static QColor propertyColor(const QWidget* widget, const char* key, const QColor& fallback) {
    if (!widget) {
      return fallback;
    }
    const QVariant value = widget->property(key);
    if (value.canConvert<QColor>()) {
      return value.value<QColor>();
    }
    return fallback;
  }

  static int scrollBarRadius(const QWidget* widget, const QRect& rect) {
    if (!widget) {
      return std::max(1, std::min(rect.width(), rect.height()) / 2);
    }
    const QVariant value = widget->property("_adqt_radius");
    if (value.isValid()) {
      const int configured = std::max(0, value.toInt());
      return std::min(configured, std::min(rect.width(), rect.height()) / 2);
    }
    return std::max(1, std::min(rect.width(), rect.height()) / 2);
  }

  static QRect scrollBarGrooveRect(const QStyleOptionSlider* option, const QWidget* widget) {
    if (!option) {
      return QRect();
    }
    QRect groove = option->rect;
    if (!widget) {
      return groove;
    }
    const int inset = std::max(0, widget->property("_adqt_inset").toInt());
    const int marginStart = std::max(0, widget->property("_adqt_margin_start").toInt());
    const int marginEnd = std::max(0, widget->property("_adqt_margin_end").toInt());
    if (option->orientation == Qt::Vertical) {
      const int boundedInset = std::min(inset, std::max(0, groove.width() - 1));
      const int boundedTop = std::min(marginStart, std::max(0, groove.height() - 1));
      const int remainingHeight = std::max(0, groove.height() - boundedTop);
      const int boundedBottom = std::min(marginEnd, std::max(0, remainingHeight - 1));
      groove.adjust(boundedInset, boundedTop, 0, -boundedBottom);
    } else {
      const int boundedInset = std::min(inset, std::max(0, groove.height() - 1));
      const int boundedLeft = std::min(marginStart, std::max(0, groove.width() - 1));
      const int remainingWidth = std::max(0, groove.width() - boundedLeft);
      const int boundedRight = std::min(marginEnd, std::max(0, remainingWidth - 1));
      groove.adjust(boundedLeft, boundedInset, -boundedRight, 0);
    }
    return groove;
  }

  QRect scrollBarSliderRect(const QStyleOptionSlider* option, const QWidget* widget) const {
    if (!option) {
      return QRect();
    }
    const QRect groove = scrollBarGrooveRect(option, widget);
    if (!groove.isValid()) {
      return QRect();
    }

    const bool vertical = option->orientation == Qt::Vertical;
    const int trackLength = vertical ? groove.height() : groove.width();
    if (trackLength <= 0) {
      return QRect();
    }

    const int minSliderLength = pixelMetric(PM_ScrollBarSliderMin, option, widget);
    const int minValue = option->minimum;
    const int maxValue = option->maximum;
    const int pageStep = std::max(0, option->pageStep);
    const int range = std::max(0, maxValue - minValue);

    int sliderLength = trackLength;
    if (range > 0) {
      const qreal denominator = static_cast<qreal>(range + pageStep);
      const qreal ratio = denominator > 0.0 ? static_cast<qreal>(pageStep) / denominator : 0.0;
      const int boundedMinSliderLength = std::min(minSliderLength, trackLength);
      sliderLength = qBound(boundedMinSliderLength,
                            static_cast<int>(std::round(trackLength * ratio)), trackLength);
    }

    const int available = std::max(0, trackLength - sliderLength);
    const int sliderPos = QStyle::sliderPositionFromValue(
        minValue, maxValue, option->sliderPosition, available, option->upsideDown);
    if (vertical) {
      return QRect(groove.left(), groove.top() + sliderPos, groove.width(), sliderLength);
    }
    return QRect(groove.left() + sliderPos, groove.top(), sliderLength, groove.height());
  }
};

void ensureOverlayStyle(QScrollBar* bar) {
  if (!bar || bar->property("_adqt_overlay_style_applied").toBool()) {
    return;
  }

  QStyle* fusionStyle = QStyleFactory::create(QStringLiteral("Fusion"));
  if (!fusionStyle) {
    fusionStyle = bar->style();
  }

  auto* overlayStyle = new OverlayScrollBarStyle(fusionStyle);
  overlayStyle->setParent(bar);
  bar->setStyle(overlayStyle);
  bar->setProperty("_adqt_overlay_style_applied", true);
}

bool setIntPropertyIfChanged(QWidget* widget, const char* key, int value) {
  if (!widget || !key) {
    return false;
  }
  const QVariant current = widget->property(key);
  if (current.isValid() && current.toInt() == value) {
    return false;
  }
  widget->setProperty(key, value);
  return true;
}

bool setColorPropertyIfChanged(QWidget* widget, const char* key, const QColor& color) {
  if (!widget || !key) {
    return false;
  }
  const QVariant current = widget->property(key);
  if (current.canConvert<QColor>() && qvariant_cast<QColor>(current) == color) {
    return false;
  }
  widget->setProperty(key, color);
  return true;
}

bool applyScrollBarPalette(QScrollBar* bar, const QColor& trackColor, const QColor& handleColor,
                           const QColor& handlePressedColor) {
  if (!bar) {
    return false;
  }

  const QPalette currentPalette = bar->palette();
  QPalette nextPalette = currentPalette;
  nextPalette.setColor(QPalette::Window, trackColor);
  nextPalette.setColor(QPalette::Base, trackColor);
  nextPalette.setColor(QPalette::Button, handleColor);
  nextPalette.setColor(QPalette::Mid, handleColor);
  nextPalette.setColor(QPalette::Midlight, handleColor);
  nextPalette.setColor(QPalette::Light, handleColor);
  nextPalette.setColor(QPalette::Dark, handlePressedColor);
  nextPalette.setColor(QPalette::Shadow, handlePressedColor);
  nextPalette.setColor(QPalette::Highlight, handleColor);
  nextPalette.setColor(QPalette::ButtonText, handleColor);
  nextPalette.setColor(QPalette::Text, handleColor);
  nextPalette.setColor(QPalette::Disabled, QPalette::Button, handleColor);
  nextPalette.setColor(QPalette::Disabled, QPalette::Window, trackColor);

  if (nextPalette == currentPalette) {
    return false;
  }
  bar->setPalette(nextPalette);
  return true;
}

}  // namespace

void applyThemedScrollBar(QScrollBar* bar, int extent, int radius, int inset, int marginStart,
                          int marginEnd) {
  if (!bar) {
    return;
  }

  const adqt::theme::ThemeMapToken map = adqt::theme::ThemeManager::instance().resolveTheme(bar);
  const QColor baseColor = toColor(map.colorBgContainer, QColor("#ffffff"));
  const QColor trackColor =
      compositeOn(withAlpha(toColor(map.colorTextQuaternary, QColor("#8c8c8c")), 0.18F), baseColor);
  const QColor handleColor =
      compositeOn(withAlpha(toColor(map.colorTextSecondary, QColor("#595959")), 0.45F), trackColor);
  const QColor handleHoverColor =
      compositeOn(withAlpha(toColor(map.colorText, QColor("#141414")), 0.60F), trackColor);
  const QColor handlePressedColor =
      compositeOn(withAlpha(toColor(map.colorText, QColor("#141414")), 0.75F), trackColor);

  const int normalizedExtent = std::max(6, extent);
  const int normalizedInset = std::max(0, inset);
  const int normalizedMarginStart = std::max(0, marginStart);
  const int normalizedMarginEnd = std::max(0, marginEnd);
  const int visibleThickness = std::max(1, normalizedExtent - normalizedInset);
  const int fallbackRadius = std::max(1, (visibleThickness + 1) / 2);
  const int normalizedRadius = radius > 0 ? radius : fallbackRadius;

  bool changed = false;
  ensureOverlayStyle(bar);
  changed |= setIntPropertyIfChanged(bar, "_adqt_extent", normalizedExtent);
  changed |= setIntPropertyIfChanged(bar, "_adqt_inset", normalizedInset);
  changed |= setIntPropertyIfChanged(bar, "_adqt_radius", std::max(0, normalizedRadius));
  changed |= setIntPropertyIfChanged(bar, "_adqt_margin_start", normalizedMarginStart);
  changed |= setIntPropertyIfChanged(bar, "_adqt_margin_end", normalizedMarginEnd);
  changed |= setColorPropertyIfChanged(bar, "_adqt_track_color", trackColor);
  changed |= setColorPropertyIfChanged(bar, "_adqt_handle_color", handleColor);
  changed |= setColorPropertyIfChanged(bar, "_adqt_handle_hover_color", handleHoverColor);
  changed |= setColorPropertyIfChanged(bar, "_adqt_handle_pressed_color", handlePressedColor);

  if (!bar->hasMouseTracking()) {
    bar->setMouseTracking(true);
    changed = true;
  }

  if (bar->orientation() == Qt::Vertical) {
    if (bar->minimumWidth() != normalizedExtent || bar->maximumWidth() != normalizedExtent) {
      bar->setFixedWidth(normalizedExtent);
      changed = true;
    }
  } else {
    if (bar->minimumHeight() != normalizedExtent || bar->maximumHeight() != normalizedExtent) {
      bar->setFixedHeight(normalizedExtent);
      changed = true;
    }
  }

  changed |= applyScrollBarPalette(bar, trackColor, handleColor, handlePressedColor);
  if (changed) {
    bar->update();
  }
}

}  // namespace adqt::widgets::detail
