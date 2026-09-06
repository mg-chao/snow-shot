#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QMetaType>
#include <QPoint>
#include <QRectF>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

#include "icon_core.h"

class QPainter;
class QEnterEvent;
class QEvent;
class QFocusEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QMoveEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

namespace adqt::widgets {
namespace detail {
class AnimatedScalar;
class FrameLoop;
struct SwitchAppearanceInput;
struct SwitchAppearance;
}  // namespace detail

class AdSwitch final : public QAbstractButton {
  Q_OBJECT

  Q_PROPERTY(
      ControlSize controlSize READ controlSize WRITE setControlSize NOTIFY controlSizeChanged)
  Q_PROPERTY(bool loading READ loading WRITE setLoading NOTIFY loadingChanged)
  Q_PROPERTY(QString checkedText READ checkedText WRITE setCheckedText NOTIFY checkedTextChanged)
  Q_PROPERTY(
      QString uncheckedText READ uncheckedText WRITE setUncheckedText NOTIFY uncheckedTextChanged)
  Q_PROPERTY(adqt::icons::IconRef checkedIconRef READ checkedIconRef WRITE setCheckedIconRef NOTIFY
                 checkedIconRefChanged)
  Q_PROPERTY(adqt::icons::IconRef uncheckedIconRef READ uncheckedIconRef WRITE setUncheckedIconRef
                 NOTIFY uncheckedIconRefChanged)

 public:
  enum class ControlSize {
    Medium,
    Small,
  };
  Q_ENUM(ControlSize)

  struct ComponentTokenContext {
    ControlSize controlSize = ControlSize::Medium;
    bool checked = false;
    bool loading = false;
    bool disabled = false;
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
  };

  struct ColorTokens {
    std::optional<QColor> uncheckedTrack;
    std::optional<QColor> uncheckedTrackHover;
    std::optional<QColor> checkedTrack;
    std::optional<QColor> checkedTrackHover;
    std::optional<QColor> thumb;
    std::optional<QColor> thumbBorder;
    std::optional<QColor> thumbShadow;
    std::optional<QColor> content;
    std::optional<QColor> loadingIndicator;
    std::optional<QColor> checkedLoadingIndicator;
    std::optional<QColor> focusRing;
    std::optional<QColor> wave;
  };

  struct MetricTokens {
    std::optional<int> trackHeight;
    std::optional<int> smallTrackHeight;
    std::optional<int> trackMinWidth;
    std::optional<int> smallTrackMinWidth;
    std::optional<int> trackPadding;
    std::optional<int> thumbSize;
    std::optional<int> smallThumbSize;
    std::optional<int> loadingIndicatorSize;
    std::optional<qreal> disabledOpacity;
  };

  struct ComponentTokens {
    ColorTokens colors;
    MetricTokens metrics;
  };

  using ComponentTokenResolver = std::function<ComponentTokens(const ComponentTokenContext&)>;

  explicit AdSwitch(QWidget* parent = nullptr);
  ~AdSwitch() override;

  ControlSize controlSize() const;
  void setControlSize(ControlSize value);

  bool loading() const;
  void setLoading(bool value);

  QString checkedText() const;
  void setCheckedText(const QString& value);

  QString uncheckedText() const;
  void setUncheckedText(const QString& value);

  adqt::icons::IconRef checkedIconRef() const;
  void setCheckedIconRef(const adqt::icons::IconRef& value);

  adqt::icons::IconRef uncheckedIconRef() const;
  void setUncheckedIconRef(const adqt::icons::IconRef& value);

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& options);
  void resetComponentTokens();
  void setComponentTokenResolver(ComponentTokenResolver resolver);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 signals:
  void controlSizeChanged(ControlSize value);
  void loadingChanged(bool value);
  void checkedTextChanged(const QString& value);
  void uncheckedTextChanged(const QString& value);
  void checkedIconRefChanged(const adqt::icons::IconRef& value);
  void uncheckedIconRefChanged(const adqt::icons::IconRef& value);
  void componentTokensChanged();

 protected:
  bool event(QEvent* event) override;
  bool hitButton(const QPoint& pos) const override;
  void paintEvent(QPaintEvent* event) override;
  void nextCheckState() override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void moveEvent(QMoveEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  struct Private;

  bool interactionBlocked() const;
  detail::SwitchAppearanceInput buildAppearanceInput() const;
  detail::SwitchAppearance resolvedAppearance() const;
  QRect indicatorRect() const;
  ComponentTokens resolvedComponentTokens() const;
  QString effectiveAccessibleName() const;
  QString effectiveAccessibleValue() const;
  void invalidateLayoutCache(bool invalidateSizeHint = true) const;
  void invalidateAppearanceCache(bool invalidateSizeHint = true) const;
  void invalidateResolvedTokensCache() const;
  void refreshAccessibleState();
  void refreshAutomaticCursor();
  void applyAutomaticCursor(std::optional<Qt::CursorShape> cursorShape);
  void setPressedState(bool value, bool immediate = false);
  void refreshThumbAnimation(bool immediate);
  void refreshPressAnimation(bool immediate);
  void refreshSpinnerLoop();
  void refreshFocusOverlay();
  void triggerWaveOverlay();
  void stopAnimations();
  void resetDragState();
  qreal visualThumbPosition() const;
  void drawSpinner(QPainter* painter, const QRectF& rect, const QColor& color,
                   qreal preferredSize) const;
  int contentWidthForState(bool checkedState, const detail::SwitchAppearance& appearance) const;

  std::unique_ptr<Private> d_;
};

}  // namespace adqt::widgets

Q_DECLARE_METATYPE(adqt::widgets::AdSwitch::ControlSize)
