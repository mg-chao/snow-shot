#pragma once

#include <QMargins>
#include <QPointer>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>

class QEnterEvent;
class QHideEvent;

namespace adqt::widgets {

class AdScrollBar final : public QScrollBar {
  Q_OBJECT

  Q_PROPERTY(int scrollBarThickness READ scrollBarThickness WRITE setScrollBarThickness NOTIFY
                 scrollBarThicknessChanged)
  Q_PROPERTY(int scrollBarRadius READ scrollBarRadius WRITE setScrollBarRadius NOTIFY
                 scrollBarRadiusChanged)
  Q_PROPERTY(int collapsedVisualThickness READ collapsedVisualThickness WRITE
                 setCollapsedVisualThickness NOTIFY collapsedVisualThicknessChanged)
  Q_PROPERTY(QMargins overlayMargins READ overlayMargins WRITE setOverlayMargins NOTIFY
                 overlayMarginsChanged)
  Q_PROPERTY(
      QRect overlayBounds READ overlayBounds WRITE setOverlayBounds NOTIFY overlayBoundsChanged)
  Q_PROPERTY(bool expanded READ isExpanded NOTIFY expandedChanged)
  Q_PROPERTY(bool hoverExpansionEnabled READ hoverExpansionEnabled WRITE setHoverExpansionEnabled
                 NOTIFY hoverExpansionEnabledChanged)
  Q_PROPERTY(bool overlayColorsEnabled READ overlayColorsEnabled WRITE setOverlayColorsEnabled
                 NOTIFY overlayColorsEnabledChanged)
  Q_PROPERTY(bool embedded READ isEmbedded WRITE setEmbedded NOTIFY embeddedChanged)

 public:
  explicit AdScrollBar(Qt::Orientation orientation, QWidget* parent = nullptr);
  ~AdScrollBar() override = default;

  int scrollBarThickness() const;
  void setScrollBarThickness(int value);

  int scrollBarRadius() const;
  void setScrollBarRadius(int value);

  int collapsedVisualThickness() const;
  void setCollapsedVisualThickness(int value);

  QMargins overlayMargins() const;
  void setOverlayMargins(const QMargins& margins);

  QRect overlayBounds() const;
  void setOverlayBounds(const QRect& bounds);

  bool isExpanded() const;

  bool hoverExpansionEnabled() const;
  void setHoverExpansionEnabled(bool enabled);

  bool overlayColorsEnabled() const;
  void setOverlayColorsEnabled(bool enabled);

  bool isEmbedded() const;
  void setEmbedded(bool embedded);

 signals:
  void scrollBarThicknessChanged(int value);
  void scrollBarRadiusChanged(int value);
  void collapsedVisualThicknessChanged(int value);
  void overlayMarginsChanged(const QMargins& margins);
  void overlayBoundsChanged(const QRect& bounds);
  void expandedChanged(bool expanded);
  void hoverExpansionEnabledChanged(bool enabled);
  void overlayColorsEnabledChanged(bool enabled);
  void embeddedChanged(bool embedded);

 protected:
  void changeEvent(QEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void hideEvent(QHideEvent* event) override;

 private:
  void setExpanded(bool expanded);
  void applyScrollBarStyle();
  void updateOverlayGeometry();

  int scrollBarThickness_ = 8;
  int scrollBarRadius_ = 4;
  int collapsedVisualThickness_ = 3;
  QMargins overlayMargins_{2, 2, 2, 2};
  QRect overlayBounds_;
  bool expanded_ = false;
  bool hoverExpansionEnabled_ = true;
  bool overlayColorsEnabled_ = true;
  bool embedded_ = false;
  bool applyingStyle_ = false;
};

class AdScrollArea final : public QScrollArea {
  Q_OBJECT

  Q_PROPERTY(bool fitToWidth READ fitToWidth WRITE setFitToWidth NOTIFY fitToWidthChanged)
  Q_PROPERTY(int scrollBarThickness READ scrollBarThickness WRITE setScrollBarThickness NOTIFY
                 scrollBarThicknessChanged)
  Q_PROPERTY(int scrollBarRadius READ scrollBarRadius WRITE setScrollBarRadius NOTIFY
                 scrollBarRadiusChanged)

 public:
  explicit AdScrollArea(QWidget* parent = nullptr);
  ~AdScrollArea() override = default;

  static void applyThemedScrollBar(QScrollBar* bar, int extent = 8, int radius = 4, int inset = 0,
                                   int marginStart = 0, int marginEnd = 0);

  void setContentWidget(QWidget* widget);
  QWidget* contentWidget() const;
  AdScrollBar* overlayVerticalScrollBar() const;
  AdScrollBar* overlayHorizontalScrollBar() const;

  bool fitToWidth() const;
  void setFitToWidth(bool value);

  int scrollBarThickness() const;
  void setScrollBarThickness(int value);

  int scrollBarRadius() const;
  void setScrollBarRadius(int value);

 signals:
  void fitToWidthChanged(bool value);
  void scrollBarThicknessChanged(int value);
  void scrollBarRadiusChanged(int value);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void applyScrollBarStyle();
  void syncContentSize();
  void syncOverlayScrollBars();
  void updateOverlayGeometry();

  QPointer<QWidget> contentWidget_;
  QPointer<AdScrollBar> overlayVerticalScrollBar_;
  QPointer<AdScrollBar> overlayHorizontalScrollBar_;
  bool fitToWidth_ = true;
  int scrollBarThickness_ = 8;
  int scrollBarRadius_ = 4;
  bool syncingContentSize_ = false;
};

}  // namespace adqt::widgets
