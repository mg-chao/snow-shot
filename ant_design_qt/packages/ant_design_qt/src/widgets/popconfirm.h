#pragma once

#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#include <QHash>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

#include "button.h"
#include "icon_core.h"
#include "popover.h"

class QEvent;

namespace adqt::widgets {

class AdPopconfirmPrivate;

class AdPopconfirm final : public QObject {
  Q_OBJECT

  Q_PROPERTY(adqt::widgets::AdPopupPlacement placement READ placement WRITE setPlacement NOTIFY
                 placementChanged)
  Q_PROPERTY(adqt::widgets::AdPopupTriggers triggers READ triggers WRITE setTriggers NOTIFY
                 triggersChanged)
  Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)
  Q_PROPERTY(adqt::widgets::AdPopupActivationMode visibilityMode READ visibilityMode WRITE
                 setVisibilityMode NOTIFY visibilityModeChanged)
  Q_PROPERTY(adqt::widgets::AdPopupLifetime popupLifetime READ popupLifetime WRITE setPopupLifetime
                 NOTIFY popupLifetimeChanged)
  Q_PROPERTY(adqt::widgets::AdPopupLayerMode popupLayerMode READ popupLayerMode WRITE
                 setPopupLayerMode NOTIFY popupLayerModeChanged)
  Q_PROPERTY(
      bool defaultVisible READ defaultVisible WRITE setDefaultVisible NOTIFY defaultVisibleChanged)
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
  Q_PROPERTY(QString informativeText READ informativeText WRITE setInformativeText NOTIFY
                 informativeTextChanged)
  Q_PROPERTY(QDialogButtonBox::StandardButtons standardButtons READ standardButtons WRITE
                 setStandardButtons NOTIFY standardButtonsChanged)
  Q_PROPERTY(QDialogButtonBox::StandardButton defaultButton READ defaultButton WRITE
                 setDefaultButton NOTIFY defaultButtonChanged)
  Q_PROPERTY(QDialogButtonBox::StandardButton escapeButton READ escapeButton WRITE setEscapeButton
                 NOTIFY escapeButtonChanged)
  Q_PROPERTY(QDialogButtonBox::StandardButtons autoCloseButtons READ autoCloseButtons WRITE
                 setAutoCloseButtons NOTIFY autoCloseButtonsChanged)
  Q_PROPERTY(QMessageBox::Icon icon READ icon WRITE setIcon NOTIFY iconChanged)
  Q_PROPERTY(adqt::icons::IconRef customIconRef READ customIconRef WRITE setCustomIconRef NOTIFY
                 customIconRefChanged)
  Q_PROPERTY(
      QWidget* sourceWidget READ sourceWidget WRITE setSourceWidget NOTIFY sourceWidgetChanged)

 public:
  using Placement = AdPopover::Placement;
  using Trigger = AdPopover::Trigger;
  using Triggers = AdPopover::Triggers;
  using VisibilityMode = AdPopover::VisibilityPolicy;
  using PopupLifetime = AdPopover::PopupLifetime;
  using PopupLayerMode = AdPopover::PopupLayerMode;
  using StandardButton = QDialogButtonBox::StandardButton;
  using StandardButtons = QDialogButtonBox::StandardButtons;
  using Icon = QMessageBox::Icon;

  struct ComponentTokens {
    std::optional<int> titleMinWidth;
    std::optional<int> popupMaximumWidth;
    std::optional<int> zIndexPopup;
    std::optional<int> messageGap;
    std::optional<int> messageBottom;
    std::optional<int> descriptionGap;
    std::optional<int> buttonGap;
    std::optional<int> iconSize;
    std::optional<QColor> iconColor;
  };

  struct SemanticSlotStyle {
    std::optional<QColor> textColor;
    std::optional<QColor> backgroundColor;
    std::optional<QColor> borderColor;
  };

  struct SemanticStyles {
    SemanticSlotStyle root;
    SemanticSlotStyle container;
    SemanticSlotStyle title;
    SemanticSlotStyle description;
    SemanticSlotStyle icon;
    SemanticSlotStyle arrow;
  };

  struct StyleContext {
    Placement placement = Placement::Top;
    Triggers triggers = Trigger::Click;
    VisibilityMode visibilityMode = VisibilityMode::Automatic;
    bool visible = false;
    bool enabled = true;
    bool arrowVisible = true;
    StandardButtons standardButtons = StandardButton::Ok | StandardButton::Cancel;
    Icon icon = Icon::Warning;
  };

  using SemanticStyleResolver = std::function<SemanticStyles(const StyleContext&)>;

  explicit AdPopconfirm(QObject* parent = nullptr);
  ~AdPopconfirm() override;

  Placement placement() const;
  void setPlacement(Placement value);

  Triggers triggers() const;
  void setTriggers(Triggers value);

  bool isVisible() const;
  void setVisible(bool value);
  void show();
  void hide();
  void toggle();

  VisibilityMode visibilityMode() const;
  void setVisibilityMode(VisibilityMode value);

  PopupLifetime popupLifetime() const;
  void setPopupLifetime(PopupLifetime value);

  PopupLayerMode popupLayerMode() const;
  void setPopupLayerMode(PopupLayerMode value);

  bool defaultVisible() const;
  void setDefaultVisible(bool value);

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

  QString informativeText() const;
  void setInformativeText(const QString& value);

  StandardButtons standardButtons() const;
  void setStandardButtons(StandardButtons value);

  StandardButton defaultButton() const;
  void setDefaultButton(StandardButton value);

  StandardButton escapeButton() const;
  void setEscapeButton(StandardButton value);

  StandardButtons autoCloseButtons() const;
  void setAutoCloseButtons(StandardButtons value);

  Icon icon() const;
  void setIcon(Icon value);

  adqt::icons::IconRef customIconRef() const;
  void setCustomIconRef(const adqt::icons::IconRef& value);
  void clearCustomIconRef();
  bool hasCustomIconRef() const;

  QString buttonText(StandardButton button) const;
  void setButtonText(StandardButton button, const QString& value);

  AdButton::AccentRole buttonAccentRole(StandardButton button) const;
  void setButtonAccentRole(StandardButton button, AdButton::AccentRole value);

  AdButton::ButtonStyle buttonStyle(StandardButton button) const;
  void setButtonStyle(StandardButton button, AdButton::ButtonStyle value);

  bool buttonBusy(StandardButton button) const;
  void setButtonBusy(StandardButton button, bool value);

  QWidget* sourceWidget() const;
  void setSourceWidget(QWidget* widget);

  AdButton* button(StandardButton button) const;

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
  void visibilityModeChanged(VisibilityMode value);
  void popupLifetimeChanged(PopupLifetime value);
  void popupLayerModeChanged(PopupLayerMode value);
  void defaultVisibleChanged(bool value);
  void autoAdjustOverflowChanged(bool value);
  void arrowVisibleChanged(bool value);
  void arrowPointAtCenterChanged(bool value);
  void hoverOpenDelayMsChanged(int value);
  void hoverCloseDelayMsChanged(int value);
  void enabledChanged(bool value);
  void textChanged(const QString& value);
  void informativeTextChanged(const QString& value);
  void standardButtonsChanged(StandardButtons value);
  void defaultButtonChanged(StandardButton value);
  void escapeButtonChanged(StandardButton value);
  void autoCloseButtonsChanged(StandardButtons value);
  void iconChanged(Icon value);
  void customIconRefChanged(const adqt::icons::IconRef& value);
  void sourceWidgetChanged(QWidget* value);
  void componentTokensChanged();
  void semanticStylesChanged();
  void accepted();
  void rejected();
  void helpRequested();
  void clicked(StandardButton button);
  void popupClicked();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  std::unique_ptr<AdPopconfirmPrivate> d_;
};

}  // namespace adqt::widgets
