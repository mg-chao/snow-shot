#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>
#include <QWidget>
#include <memory>
#include <optional>

class QEvent;
class QPainter;
class QResizeEvent;

namespace adqt::widgets {

namespace detail {
class SpinSurface;
}

class AdSpin final : public QWidget {
  Q_OBJECT

  Q_PROPERTY(bool spinning READ spinning WRITE setSpinning NOTIFY spinningChanged)
  Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
  Q_PROPERTY(int delayMs READ delayMs WRITE setDelayMs NOTIFY delayMsChanged)
  Q_PROPERTY(SizeClass sizeClass READ sizeClass WRITE setSizeClass NOTIFY sizeClassChanged)
  Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
  Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged)
  Q_PROPERTY(
      ProgressMode progressMode READ progressMode WRITE setProgressMode NOTIFY progressModeChanged)
  Q_PROPERTY(qreal percent READ percent WRITE setPercent NOTIFY percentChanged)
  Q_PROPERTY(qreal displayedPercent READ displayedPercent NOTIFY displayedPercentChanged)
  Q_PROPERTY(
      QWidget* contentWidget READ contentWidget WRITE setContentWidget NOTIFY contentWidgetChanged)
  Q_PROPERTY(QWidget* indicatorWidget READ indicatorWidget WRITE setIndicatorWidget NOTIFY
                 indicatorWidgetChanged)

 public:
  enum class SizeClass {
    Small,
    Medium,
    Large,
  };
  Q_ENUM(SizeClass)

  enum class ProgressMode {
    None,
    Determinate,
    Automatic,
  };
  Q_ENUM(ProgressMode)

  struct ColorTokens {
    std::optional<QColor> indicator;
    std::optional<QColor> description;
    std::optional<QColor> progressTrack;
    std::optional<QColor> containerOverlay;
    std::optional<QColor> fullscreenMask;
    std::optional<QColor> fullscreenIndicator;
    std::optional<QColor> fullscreenDescription;
  };

  struct MetricTokens {
    std::optional<int> dotSize;
    std::optional<int> dotSizeSmall;
    std::optional<int> dotSizeLarge;
    std::optional<int> descriptionGap;
    std::optional<int> animationCycleMs;
    std::optional<int> autoProgressIntervalMs;
  };

  struct ComponentTokens {
    ColorTokens colors;
    MetricTokens metrics;
  };

  struct SemanticSlotStyle {
    std::optional<QColor> textColor;
    std::optional<QColor> backgroundColor;
  };

  struct SemanticStyles {
    SemanticSlotStyle root;
    SemanticSlotStyle section;
    SemanticSlotStyle indicator;
    SemanticSlotStyle description;
    SemanticSlotStyle container;
  };

  explicit AdSpin(QWidget* parent = nullptr);
  AdSpin(QWidget* contentWidget, QWidget* parent);
  ~AdSpin() override;

  bool spinning() const;
  void setSpinning(bool value);
  bool isActive() const;

  int delayMs() const;
  void setDelayMs(int value);

  SizeClass sizeClass() const;
  void setSizeClass(SizeClass value);

  QString description() const;
  void setDescription(const QString& value);

  bool fullscreen() const;
  void setFullscreen(bool value);

  ProgressMode progressMode() const;
  void setProgressMode(ProgressMode mode);
  qreal percent() const;
  void setPercent(qreal value);
  qreal displayedPercent() const;
  void setAutoProgress(bool enabled = true);
  void clearProgress();

  QWidget* contentWidget() const;
  void setContentWidget(QWidget* widget);
  QWidget* takeContentWidget();

  QWidget* indicatorWidget() const;
  void setIndicatorWidget(QWidget* widget);
  QWidget* takeIndicatorWidget();

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& tokens);
  void resetComponentTokens();

  SemanticStyles semanticStyles() const;
  void setSemanticStyles(const SemanticStyles& styles);
  void resetSemanticStyles();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 signals:
  void spinningChanged(bool spinning);
  void activeChanged(bool active);
  void delayMsChanged(int delayMs);
  void sizeClassChanged(SizeClass size);
  void descriptionChanged(const QString& description);
  void fullscreenChanged(bool fullscreen);
  void progressModeChanged(ProgressMode mode);
  void percentChanged(qreal percent);
  void displayedPercentChanged(qreal percent);
  void contentWidgetChanged(QWidget* widget);
  void indicatorWidgetChanged(QWidget* widget);
  void componentTokensChanged();
  void semanticStylesChanged();

 protected:
  bool event(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  friend class detail::SpinSurface;
  struct Private;

  void setActive(bool value);
  void scheduleActivation();
  void refreshAppearance();
  void refreshSurfaces();
  void refreshAnimationSubscription();
  void scheduleAutoProgressStep();
  void cancelAutoProgressStep();
  bool hasVisibleSurface() const;
  bool canAdoptWidget(const QWidget* widget) const;
  bool isInteractionBlockedTarget(const QObject* object) const;
  void syncInteractionFilter();
  void notifyAccessibleValueChange();
  void ensureFullscreenSurface();
  void releaseFullscreenSurface();
  void layoutChildren();
  void layoutIndicator(detail::SpinSurface* surface);
  void paintSurface(detail::SpinSurface* surface, QPainter* painter);
  void animationFrame(qint64 nowMs);

  std::unique_ptr<Private> d_;
};

}  // namespace adqt::widgets

Q_DECLARE_METATYPE(adqt::widgets::AdSpin::SizeClass)
Q_DECLARE_METATYPE(adqt::widgets::AdSpin::ProgressMode)
