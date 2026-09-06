#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QRect>
#include <QSize>
#include <QVariant>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

#include "icon_core.h"

class QPainter;

namespace adqt::widgets {

class AdSegmented final : public QWidget {
  Q_OBJECT

  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
  Q_PROPERTY(
      QVariant currentValue READ currentValue WRITE setCurrentValue NOTIFY currentValueChanged)
  Q_PROPERTY(
      ControlSize controlSize READ controlSize WRITE setControlSize NOTIFY controlSizeChanged)
  Q_PROPERTY(
      Qt::Orientation orientation READ orientation WRITE setOrientation NOTIFY orientationChanged)
  Q_PROPERTY(
      Distribution distribution READ distribution WRITE setDistribution NOTIFY distributionChanged)
  Q_PROPERTY(Shape shape READ shape WRITE setShape NOTIFY shapeChanged)
  Q_PROPERTY(bool animated READ animated WRITE setAnimated NOTIFY animatedChanged)

 public:
  enum class ControlSize {
    Small,
    Medium,
    Large,
  };
  Q_ENUM(ControlSize)

  enum class Shape {
    Default,
    Round,
  };
  Q_ENUM(Shape)

  enum class Distribution {
    Content,
    Fill,
  };
  Q_ENUM(Distribution)

  struct Option {
    QVariant value;
    QString label;
    adqt::icons::IconRef icon;
    QString tooltip;
    bool enabled = true;
    QVariant data;
  };

  struct ComponentTokenContext {
    ControlSize controlSize = ControlSize::Medium;
    Qt::Orientation orientation = Qt::Horizontal;
    Shape shape = Shape::Default;
    int currentIndex = -1;
    bool enabled = true;
    Distribution distribution = Distribution::Content;
  };

  struct ColorTokens {
    std::optional<QColor> itemColor;
    std::optional<QColor> itemHoverColor;
    std::optional<QColor> itemHoverBackground;
    std::optional<QColor> itemActiveBackground;
    std::optional<QColor> itemSelectedBackground;
    std::optional<QColor> itemSelectedColor;
    std::optional<QColor> itemDisabledColor;
    std::optional<QColor> trackBackground;
    std::optional<QColor> focusOutline;
    std::optional<QColor> thumbShadow;
  };

  struct MetricTokens {
    std::optional<int> trackPadding;
    std::optional<int> horizontalPadding;
    std::optional<int> smallHorizontalPadding;
    std::optional<int> borderRadius;
    std::optional<int> iconSize;
    std::optional<int> iconGap;
    std::optional<int> focusOutlineWidth;
    std::optional<int> focusOutlineOffset;
    std::optional<int> thumbShadowOffsetY;
  };

  struct ComponentTokens {
    ColorTokens colors;
    MetricTokens metrics;
  };

  struct SemanticSlotStyle {
    std::optional<QColor> textColor;
    std::optional<QColor> backgroundColor;
    std::optional<QFont> font;
  };

  struct SemanticStyles {
    SemanticSlotStyle root;
    SemanticSlotStyle item;
    SemanticSlotStyle label;
    SemanticSlotStyle icon;
  };

  struct StyleContext {
    ControlSize controlSize = ControlSize::Medium;
    Qt::Orientation orientation = Qt::Horizontal;
    Shape shape = Shape::Default;
    int currentIndex = -1;
    int hoveredIndex = -1;
    int pressedIndex = -1;
    bool enabled = true;
    Distribution distribution = Distribution::Content;
  };

  struct ItemPaintInfo {
    int index = -1;
    Option option;
    QRect itemRect;
    QRect contentRect;
    QColor foreground;
    QFont font;
    int iconSize = 16;
    int iconGap = 4;
    bool selected = false;
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    bool enabled = true;
  };

  using ComponentTokenResolver = std::function<ComponentTokens(const ComponentTokenContext&)>;
  using SemanticStyleResolver = std::function<SemanticStyles(const StyleContext&)>;
  using ItemPaintCallback = std::function<void(QPainter&, const ItemPaintInfo&)>;
  using ItemSizeHintCallback = std::function<QSize(const Option&, ControlSize, const QFont&)>;

  explicit AdSegmented(QWidget* parent = nullptr);
  ~AdSegmented() override;

  int count() const;
  int currentIndex() const;
  QVariant currentValue() const;

  int addOption(const QString& label, const QVariant& value = QVariant());
  int addOption(const Option& option);
  int insertOption(int index, const Option& option);
  void removeOption(int index);
  void clear();

  Option option(int index) const;
  QList<Option> options() const;
  void setOptions(const QList<Option>& options);
  int indexOfValue(const QVariant& value) const;
  QRect optionRect(int index) const;
  int optionAt(const QPoint& position) const;

  QString optionLabel(int index) const;
  void setOptionLabel(int index, const QString& label);
  adqt::icons::IconRef optionIcon(int index) const;
  void setOptionIcon(int index, const adqt::icons::IconRef& icon);
  QString optionTooltip(int index) const;
  void setOptionTooltip(int index, const QString& tooltip);
  bool isOptionEnabled(int index) const;
  void setOptionEnabled(int index, bool enabled);
  QVariant optionData(int index) const;
  void setOptionData(int index, const QVariant& value);

  ControlSize controlSize() const;
  void setControlSize(ControlSize value);
  Qt::Orientation orientation() const;
  void setOrientation(Qt::Orientation value);
  Distribution distribution() const;
  void setDistribution(Distribution value);
  Shape shape() const;
  void setShape(Shape value);
  bool animated() const;
  void setAnimated(bool value);

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& tokens);
  void resetComponentTokens();
  void setComponentTokenResolver(ComponentTokenResolver resolver);
  void resetComponentTokenResolver();

  SemanticStyles semanticStyles() const;
  void setSemanticStyles(const SemanticStyles& styles);
  void resetSemanticStyles();
  void setSemanticStyleResolver(SemanticStyleResolver resolver);
  void resetSemanticStyleResolver();

  void setItemPaintCallback(ItemPaintCallback callback);
  void resetItemPaintCallback();
  void setItemSizeHintCallback(ItemSizeHintCallback callback);
  void resetItemSizeHintCallback();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 public slots:
  void setCurrentIndex(int index);
  void setCurrentValue(const QVariant& value);

 signals:
  void currentIndexChanged(int index);
  void currentValueChanged(const QVariant& value);
  void currentChanged(int index, const QVariant& value);
  void activated(int index, const QVariant& value);
  void optionsChanged();
  void controlSizeChanged(ControlSize value);
  void orientationChanged(Qt::Orientation value);
  void distributionChanged(Distribution value);
  void shapeChanged(Shape value);
  void animatedChanged(bool value);
  void componentTokensChanged();
  void semanticStylesChanged();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  struct Private;
  std::unique_ptr<Private> d_;
};

}  // namespace adqt::widgets

Q_DECLARE_METATYPE(adqt::widgets::AdSegmented::ControlSize)
Q_DECLARE_METATYPE(adqt::widgets::AdSegmented::Shape)
Q_DECLARE_METATYPE(adqt::widgets::AdSegmented::Distribution)
Q_DECLARE_METATYPE(adqt::widgets::AdSegmented::Option)
