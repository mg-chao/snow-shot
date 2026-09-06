#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QEasingCurve>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

namespace adqt::widgets {

class AdCarousel final : public QWidget {
  Q_OBJECT

  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(int initialSlide READ initialSlide WRITE setInitialSlide NOTIFY initialSlideChanged)
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
  Q_PROPERTY(QWidget* currentWidget READ currentWidget NOTIFY currentWidgetChanged)
  Q_PROPERTY(
      bool arrowsVisible READ arrowsVisible WRITE setArrowsVisible NOTIFY arrowsVisibleChanged)
  Q_PROPERTY(bool autoplay READ autoplay WRITE setAutoplay NOTIFY autoplayChanged)
  Q_PROPERTY(int autoplayInterval READ autoplayInterval WRITE setAutoplayInterval NOTIFY
                 autoplayIntervalChanged)
  Q_PROPERTY(bool autoplayProgressVisible READ autoplayProgressVisible WRITE
                 setAutoplayProgressVisible NOTIFY autoplayProgressVisibleChanged)
  Q_PROPERTY(
      bool adaptiveHeight READ adaptiveHeight WRITE setAdaptiveHeight NOTIFY adaptiveHeightChanged)
  Q_PROPERTY(
      DotPlacement dotPlacement READ dotPlacement WRITE setDotPlacement NOTIFY dotPlacementChanged)
  Q_PROPERTY(bool dotsVisible READ dotsVisible WRITE setDotsVisible NOTIFY dotsVisibleChanged)
  Q_PROPERTY(bool draggable READ draggable WRITE setDraggable NOTIFY draggableChanged)
  Q_PROPERTY(Effect effect READ effect WRITE setEffect NOTIFY effectChanged)
  Q_PROPERTY(bool infinite READ infinite WRITE setInfinite NOTIFY infiniteChanged)
  Q_PROPERTY(int transitionDuration READ transitionDuration WRITE setTransitionDuration NOTIFY
                 transitionDurationChanged)
  Q_PROPERTY(
      QEasingCurve easingCurve READ easingCurve WRITE setEasingCurve NOTIFY easingCurveChanged)
  Q_PROPERTY(bool waitForAnimation READ waitForAnimation WRITE setWaitForAnimation NOTIFY
                 waitForAnimationChanged)
  Q_PROPERTY(bool pauseOnHover READ pauseOnHover WRITE setPauseOnHover NOTIFY pauseOnHoverChanged)
  Q_PROPERTY(bool pauseOnFocus READ pauseOnFocus WRITE setPauseOnFocus NOTIFY pauseOnFocusChanged)
  Q_PROPERTY(bool vertical READ vertical NOTIFY verticalChanged)
  Q_PROPERTY(bool animationRunning READ animationRunning NOTIFY animationRunningChanged)
  Q_PROPERTY(QAbstractButton* previousArrowButton READ previousArrowButton WRITE
                 setPreviousArrowButton NOTIFY previousArrowButtonChanged)
  Q_PROPERTY(QAbstractButton* nextArrowButton READ nextArrowButton WRITE setNextArrowButton NOTIFY
                 nextArrowButtonChanged)

 public:
  enum class DotPlacement {
    Top,
    Bottom,
    Start,
    End,
  };
  Q_ENUM(DotPlacement)

  enum class Effect {
    Scroll,
    Fade,
  };
  Q_ENUM(Effect)

  struct ComponentTokenContext {
    DotPlacement dotPlacement = DotPlacement::Bottom;
    Effect effect = Effect::Scroll;
    bool enabled = true;
    bool vertical = false;
  };

  struct ColorTokens {
    std::optional<QColor> arrowColor;
    std::optional<QColor> dotColor;
    std::optional<QColor> focusOutline;
  };

  struct MetricTokens {
    std::optional<int> arrowSize;
    std::optional<int> arrowOffset;
    std::optional<int> dotWidth;
    std::optional<int> dotHeight;
    std::optional<int> dotGap;
    std::optional<int> dotOffset;
    std::optional<int> dotActiveWidth;
    std::optional<int> hitTargetSize;
    std::optional<int> focusOutlineWidth;
    std::optional<int> dragThreshold;
  };

  struct ComponentTokens {
    ColorTokens colors;
    MetricTokens metrics;
  };

  using ComponentTokenResolver = std::function<ComponentTokens(const ComponentTokenContext&)>;

  explicit AdCarousel(QWidget* parent = nullptr);
  ~AdCarousel() override;

  int count() const;
  int initialSlide() const;
  void setInitialSlide(int index);
  int currentIndex() const;
  QWidget* currentWidget() const;

  // Successful insertion transfers ownership to the carousel. Invalid widgets are rejected.
  int addSlide(QWidget* slide);
  int insertSlide(int index, QWidget* slide);
  // removeSlide schedules the slide for deletion; takeSlide transfers ownership to the caller.
  void removeSlide(int index);
  QWidget* takeSlide(int index);
  void clear();
  QWidget* widget(int index) const;
  int indexOf(const QWidget* slide) const;

  bool arrowsVisible() const;
  void setArrowsVisible(bool value);
  bool autoplay() const;
  void setAutoplay(bool value);
  int autoplayInterval() const;
  void setAutoplayInterval(int milliseconds);
  bool autoplayProgressVisible() const;
  void setAutoplayProgressVisible(bool value);
  bool adaptiveHeight() const;
  void setAdaptiveHeight(bool value);
  DotPlacement dotPlacement() const;
  void setDotPlacement(DotPlacement value);
  bool dotsVisible() const;
  void setDotsVisible(bool value);
  bool draggable() const;
  void setDraggable(bool value);
  Effect effect() const;
  void setEffect(Effect value);
  bool infinite() const;
  void setInfinite(bool value);
  int transitionDuration() const;
  void setTransitionDuration(int milliseconds);
  QEasingCurve easingCurve() const;
  void setEasingCurve(const QEasingCurve& value);
  bool waitForAnimation() const;
  void setWaitForAnimation(bool value);
  bool pauseOnHover() const;
  void setPauseOnHover(bool value);
  bool pauseOnFocus() const;
  void setPauseOnFocus(bool value);
  bool vertical() const;
  bool animationRunning() const;

  // Custom arrows are reparented to the carousel. Passing nullptr restores the default arrow.
  QAbstractButton* previousArrowButton() const;
  void setPreviousArrowButton(QAbstractButton* button);
  // Taking an arrow transfers ownership and immediately installs a new default arrow.
  QAbstractButton* takePreviousArrowButton();
  QAbstractButton* nextArrowButton() const;
  void setNextArrowButton(QAbstractButton* button);
  QAbstractButton* takeNextArrowButton();

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& value);
  void resetComponentTokens();
  void setComponentTokenResolver(ComponentTokenResolver resolver);
  void resetComponentTokenResolver();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;

 public slots:
  void setCurrentIndex(int index);
  void goTo(int index, bool withoutAnimation = false);
  void next();
  void previous();

 signals:
  void countChanged(int count);
  void initialSlideChanged(int index);
  void currentIndexChanged(int index);
  void currentWidgetChanged(QWidget* widget);
  void beforeChange(int current, int next);
  void afterChange(int current);
  void arrowsVisibleChanged(bool value);
  void autoplayChanged(bool value);
  void autoplayIntervalChanged(int milliseconds);
  void autoplayProgressVisibleChanged(bool value);
  void adaptiveHeightChanged(bool value);
  void dotPlacementChanged(DotPlacement value);
  void dotsVisibleChanged(bool value);
  void draggableChanged(bool value);
  void effectChanged(Effect value);
  void infiniteChanged(bool value);
  void transitionDurationChanged(int milliseconds);
  void easingCurveChanged(const QEasingCurve& value);
  void waitForAnimationChanged(bool value);
  void pauseOnHoverChanged(bool value);
  void pauseOnFocusChanged(bool value);
  void verticalChanged(bool value);
  void animationRunningChanged(bool value);
  void previousArrowButtonChanged(QAbstractButton* button);
  void nextArrowButtonChanged(QAbstractButton* button);
  void componentTokensChanged();

 protected:
  bool event(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  struct Private;
  std::unique_ptr<Private> d_;
};

}  // namespace adqt::widgets

Q_DECLARE_METATYPE(adqt::widgets::AdCarousel::DotPlacement)
Q_DECLARE_METATYPE(adqt::widgets::AdCarousel::Effect)
