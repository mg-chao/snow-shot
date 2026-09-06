#pragma once

#include <QColor>
#include <QFlags>
#include <QFont>
#include <QMargins>
#include <QObject>
#include <QRect>
#include <QScopedPointer>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

#include "popup_types.h"

namespace adqt::widgets {

class AdTooltipPrivate;

class AdTooltip final : public QObject {
  Q_OBJECT

  Q_PROPERTY(adqt::widgets::AdPopupPlacement placement READ placement WRITE setPlacement NOTIFY
                 placementChanged)
  Q_PROPERTY(adqt::widgets::AdPopupTriggers triggers READ triggers WRITE setTriggers NOTIFY
                 triggersChanged)
  Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)
  Q_PROPERTY(adqt::widgets::AdPopupActivationMode activationMode READ activationMode WRITE
                 setActivationMode NOTIFY activationModeChanged)
  Q_PROPERTY(adqt::widgets::AdPopupLifetime popupLifetime READ popupLifetime WRITE setPopupLifetime
                 NOTIFY popupLifetimeChanged)
  Q_PROPERTY(adqt::widgets::AdTooltip::LayerMode layerMode READ layerMode WRITE setLayerMode NOTIFY
                 layerModeChanged)
  Q_PROPERTY(bool initiallyVisible READ initiallyVisible WRITE setInitiallyVisible NOTIFY
                 initiallyVisibleChanged)
  Q_PROPERTY(bool autoAdjustOverflow READ autoAdjustOverflow WRITE setAutoAdjustOverflow NOTIFY
                 autoAdjustOverflowChanged)
  Q_PROPERTY(bool arrowVisible READ arrowVisible WRITE setArrowVisible NOTIFY arrowVisibleChanged)
  Q_PROPERTY(bool arrowPointAtCenter READ arrowPointAtCenter WRITE setArrowPointAtCenter NOTIFY
                 arrowPointAtCenterChanged)
  Q_PROPERTY(int hoverOpenDelayMs READ hoverOpenDelayMs WRITE setHoverOpenDelayMs NOTIFY
                 hoverOpenDelayMsChanged)
  Q_PROPERTY(int hoverCloseDelayMs READ hoverCloseDelayMs WRITE setHoverCloseDelayMs NOTIFY
                 hoverCloseDelayMsChanged)
  Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
  Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
  Q_PROPERTY(
      QWidget* targetWidget READ targetWidget WRITE setTargetWidget NOTIFY targetWidgetChanged)
  Q_PROPERTY(
      QWidget* anchorWidget READ anchorWidget WRITE setAnchorWidget NOTIFY anchorWidgetChanged)
  Q_PROPERTY(bool hasTriggerRect READ hasTriggerRect NOTIFY triggerRectChanged)
  Q_PROPERTY(QRect triggerRect READ triggerRect WRITE setTriggerRect RESET clearTriggerRect NOTIFY
                 triggerRectChanged)
  Q_PROPERTY(bool hasAnchorRect READ hasAnchorRect NOTIFY anchorRectChanged)
  Q_PROPERTY(QRect anchorRect READ anchorRect WRITE setAnchorRect RESET clearAnchorRect NOTIFY
                 anchorRectChanged)

 public:
  using Placement = AdPopupPlacement;
  using Trigger = AdPopupTrigger;
  using Triggers = AdPopupTriggers;
  using ActivationMode = AdPopupActivationMode;
  using PopupLifetime = AdPopupLifetime;

  enum class LayerMode {
    InWindow,
    TopLevelTransient,
  };
  Q_ENUM(LayerMode)

  struct ComponentTokens {
    std::optional<int> popupMaximumWidth;
    std::optional<int> popupMinimumHeight;
    std::optional<int> borderRadius;
    std::optional<int> borderWidth;
    std::optional<int> arrowSize;
    std::optional<int> popupOffset;
    std::optional<QMargins> padding;
    std::optional<QColor> popupBg;
    std::optional<QColor> popupBorderColor;
    std::optional<QColor> textColor;
    std::optional<QFont> textFont;
  };

  struct SemanticSlotStyle {
    std::optional<QColor> textColor;
    std::optional<QColor> backgroundColor;
    std::optional<QColor> borderColor;
  };

  struct SemanticStyles {
    SemanticSlotStyle surface;
    SemanticSlotStyle content;
    SemanticSlotStyle arrow;
  };

  struct StyleContext {
    Placement placement = Placement::Top;
    Triggers triggers = Trigger::Hover;
    bool visible = false;
    bool enabled = true;
    bool arrowVisible = true;
  };

  using SemanticStyleResolver = std::function<SemanticStyles(const StyleContext&)>;

  explicit AdTooltip(QObject* parent = nullptr);
  ~AdTooltip() override;

  static void installApplicationTooltips();
  static void showText(QWidget* target, const QString& text, int displayTimeMs = -1);

  static void resetSyncPopupGeometryCountersForTesting();
  static qint64 syncPopupGeometryCallCountForTesting();
  static qint64 syncPopupGeometryShortCircuitCountForTesting();

  Placement placement() const;
  void setPlacement(Placement value);

  Triggers triggers() const;
  void setTriggers(Triggers value);

  bool isVisible() const;
  void setVisible(bool value);
  void show();
  void hide();
  void toggle();

  ActivationMode activationMode() const;
  void setActivationMode(ActivationMode value);

  PopupLifetime popupLifetime() const;
  void setPopupLifetime(PopupLifetime value);

  LayerMode layerMode() const;
  void setLayerMode(LayerMode value);

  bool initiallyVisible() const;
  void setInitiallyVisible(bool value);

  bool autoAdjustOverflow() const;
  void setAutoAdjustOverflow(bool value);

  bool arrowVisible() const;
  void setArrowVisible(bool value);

  bool arrowPointAtCenter() const;
  void setArrowPointAtCenter(bool value);

  int hoverOpenDelayMs() const;
  void setHoverOpenDelayMs(int value);

  int hoverCloseDelayMs() const;
  void setHoverCloseDelayMs(int value);

  bool isEnabled() const;
  void setEnabled(bool value);

  QString text() const;
  void setText(const QString& value);

  QWidget* targetWidget() const;
  void setTargetWidget(QWidget* widget);

  QWidget* anchorWidget() const;
  void setAnchorWidget(QWidget* widget);

  bool hasTriggerRect() const;
  QRect triggerRect() const;
  void setTriggerRect(const QRect& rect);
  void clearTriggerRect();

  bool hasAnchorRect() const;
  QRect anchorRect() const;
  void setAnchorRect(const QRect& rect);
  void clearAnchorRect();

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& tokens);
  void resetComponentTokens();

  SemanticStyles semanticStyles() const;
  void setSemanticStyles(const SemanticStyles& styles);
  void setSemanticStyleResolver(SemanticStyleResolver resolver);

 signals:
  void placementChanged(Placement value);
  void triggersChanged(Triggers value);
  void visibleChanged(bool value);
  void visibilityRequested(bool value);
  void activationModeChanged(ActivationMode value);
  void popupLifetimeChanged(PopupLifetime value);
  void layerModeChanged(LayerMode value);
  void initiallyVisibleChanged(bool value);
  void autoAdjustOverflowChanged(bool value);
  void arrowVisibleChanged(bool value);
  void arrowPointAtCenterChanged(bool value);
  void hoverOpenDelayMsChanged(int value);
  void hoverCloseDelayMsChanged(int value);
  void enabledChanged(bool value);
  void textChanged(const QString& value);
  void targetWidgetChanged(QWidget* value);
  void anchorWidgetChanged(QWidget* value);
  void triggerRectChanged();
  void anchorRectChanged();
  void componentTokensChanged();
  void semanticStylesChanged();

 private:
  Q_DECLARE_PRIVATE(AdTooltip)

  QScopedPointer<AdTooltipPrivate> d_ptr;
};

}  // namespace adqt::widgets
