#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QEvent;
class QPaintEvent;
class QResizeEvent;

namespace adqt::widgets {

class AdPagination final : public QWidget {
  Q_OBJECT

  Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
  Q_PROPERTY(int total READ total WRITE setTotal NOTIFY totalChanged)
  Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
  Q_PROPERTY(int pageCount READ pageCount NOTIFY pageCountChanged)
  Q_PROPERTY(QVector<int> pageSizeOptions READ pageSizeOptions WRITE setPageSizeOptions NOTIFY
                 pageSizeOptionsChanged)
  Q_PROPERTY(
      ControlSize controlSize READ controlSize WRITE setControlSize NOTIFY controlSizeChanged)
  Q_PROPERTY(Alignment alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged)
  Q_PROPERTY(SizeChangerMode sizeChangerMode READ sizeChangerMode WRITE setSizeChangerMode NOTIFY
                 sizeChangerModeChanged)
  Q_PROPERTY(int totalBoundaryShowSizeChanger READ totalBoundaryShowSizeChanger WRITE
                 setTotalBoundaryShowSizeChanger NOTIFY totalBoundaryShowSizeChangerChanged)
  Q_PROPERTY(bool disabled READ disabled WRITE setDisabled NOTIFY disabledChanged)
  Q_PROPERTY(bool hideOnSinglePage READ hideOnSinglePage WRITE setHideOnSinglePage NOTIFY
                 hideOnSinglePageChanged)
  Q_PROPERTY(bool showQuickJumper READ showQuickJumper WRITE setShowQuickJumper NOTIFY
                 showQuickJumperChanged)
  Q_PROPERTY(
      bool showLessItems READ showLessItems WRITE setShowLessItems NOTIFY showLessItemsChanged)
  Q_PROPERTY(bool showTitle READ showTitle WRITE setShowTitle NOTIFY showTitleChanged)
  Q_PROPERTY(bool simple READ simple WRITE setSimple NOTIFY simpleChanged)
  Q_PROPERTY(
      bool simpleReadOnly READ simpleReadOnly WRITE setSimpleReadOnly NOTIFY simpleReadOnlyChanged)
  Q_PROPERTY(bool responsive READ responsive WRITE setResponsive NOTIFY responsiveChanged)

 public:
  enum class ControlSize {
    Large,
    Medium,
    Small,
  };
  Q_ENUM(ControlSize)

  enum class Alignment {
    Start,
    Center,
    End,
  };
  Q_ENUM(Alignment)

  enum class SizeChangerMode {
    Auto,
    Always,
    Never,
  };
  Q_ENUM(SizeChangerMode)

  enum class ItemType {
    Page,
    Previous,
    Next,
    JumpPrevious,
    JumpNext,
  };
  Q_ENUM(ItemType)

  struct Range {
    int first = 0;
    int last = 0;
  };

  struct ColorTokens {
    std::optional<QColor> itemBackground;
    std::optional<QColor> itemHoverBackground;
    std::optional<QColor> itemPressedBackground;
    std::optional<QColor> itemText;
    std::optional<QColor> itemActiveBackground;
    std::optional<QColor> itemActiveText;
    std::optional<QColor> itemActiveHoverText;
    std::optional<QColor> itemActiveBorder;
    std::optional<QColor> itemDisabledText;
    std::optional<QColor> itemActiveDisabledBackground;
    std::optional<QColor> focusOutline;
  };

  struct MetricTokens {
    std::optional<int> itemSize;
    std::optional<int> itemSizeSmall;
    std::optional<int> itemSizeLarge;
    std::optional<int> itemSpacing;
    std::optional<int> borderRadius;
    std::optional<int> quickJumperWidth;
    std::optional<int> responsiveBreakpoint;
  };

  struct ComponentTokens {
    ColorTokens colors;
    MetricTokens metrics;
  };

  struct SemanticSlotStyle {
    std::optional<QColor> textColor;
    std::optional<QColor> backgroundColor;
    std::optional<QColor> borderColor;
  };

  struct SemanticStyles {
    SemanticSlotStyle root;
    SemanticSlotStyle item;
  };

  using TotalTextFormatter = std::function<QString(int total, const Range& range)>;
  using ItemTextFormatter = std::function<QString(int page, ItemType type)>;

  explicit AdPagination(QWidget* parent = nullptr);
  ~AdPagination() override;

  int currentPage() const;
  void setCurrentPage(int value);

  int total() const;
  void setTotal(int value);

  int pageSize() const;
  void setPageSize(int value);

  int pageCount() const;
  Range visibleItemRange() const;

  ControlSize controlSize() const;
  void setControlSize(ControlSize value);

  Alignment alignment() const;
  void setAlignment(Alignment value);

  SizeChangerMode sizeChangerMode() const;
  void setSizeChangerMode(SizeChangerMode value);
  bool isSizeChangerVisible() const;
  void setShowSizeChanger(bool value);

  int totalBoundaryShowSizeChanger() const;
  void setTotalBoundaryShowSizeChanger(int value);

  QVector<int> pageSizeOptions() const;
  void setPageSizeOptions(const QVector<int>& values);

  bool disabled() const;
  void setDisabled(bool value);

  bool hideOnSinglePage() const;
  void setHideOnSinglePage(bool value);

  bool showQuickJumper() const;
  void setShowQuickJumper(bool value);

  bool showLessItems() const;
  void setShowLessItems(bool value);

  bool showTitle() const;
  void setShowTitle(bool value);

  bool simple() const;
  void setSimple(bool value);

  bool simpleReadOnly() const;
  void setSimpleReadOnly(bool value);

  bool responsive() const;
  void setResponsive(bool value);

  TotalTextFormatter totalTextFormatter() const;
  void setTotalTextFormatter(TotalTextFormatter formatter);

  ItemTextFormatter itemTextFormatter() const;
  void setItemTextFormatter(ItemTextFormatter formatter);

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& tokens);
  void resetComponentTokens();

  SemanticStyles semanticStyles() const;
  void setSemanticStyles(const SemanticStyles& styles);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 public slots:
  void previousPage();
  void nextPage();
  void jumpToPage(int page);

 signals:
  void currentPageChanged(int page);
  void totalChanged(int total);
  void pageSizeChanged(int pageSize);
  void pageCountChanged(int pageCount);
  void controlSizeChanged(ControlSize size);
  void alignmentChanged(Alignment alignment);
  void sizeChangerModeChanged(SizeChangerMode mode);
  void totalBoundaryShowSizeChangerChanged(int boundary);
  void pageSizeOptionsChanged(const QVector<int>& values);
  void disabledChanged(bool disabled);
  void hideOnSinglePageChanged(bool value);
  void showQuickJumperChanged(bool value);
  void showLessItemsChanged(bool value);
  void showTitleChanged(bool value);
  void simpleChanged(bool value);
  void simpleReadOnlyChanged(bool value);
  void responsiveChanged(bool value);
  void totalTextFormatterChanged();
  void itemTextFormatterChanged();
  void componentTokensChanged();
  void semanticStylesChanged();

  // Emitted only for user-triggered navigation, matching Ant Design's onChange contract.
  void changed(int page, int pageSize);
  void pageSizeSelected(int currentPage, int pageSize);

 protected:
  void changeEvent(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  struct Private;

  void rebuild();
  void applyPageSizeFromUser(int value);
  void navigateFromUser(int page);
  int boundedPage(int page) const;
  ControlSize effectiveControlSize() const;

  std::unique_ptr<Private> d_;
};

}  // namespace adqt::widgets

Q_DECLARE_METATYPE(adqt::widgets::AdPagination::ControlSize)
Q_DECLARE_METATYPE(adqt::widgets::AdPagination::Alignment)
Q_DECLARE_METATYPE(adqt::widgets::AdPagination::SizeChangerMode)
Q_DECLARE_METATYPE(adqt::widgets::AdPagination::ItemType)
Q_DECLARE_METATYPE(adqt::widgets::AdPagination::Range)
