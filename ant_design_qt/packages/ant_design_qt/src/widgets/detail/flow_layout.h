#pragma once

#include <QLayout>
#include <QRect>
#include <QStyle>

#include <functional>

namespace adqt::widgets::detail {

class FlowLayout final : public QLayout {
 public:
  using ItemEndSpacingProvider = std::function<int(const QLayoutItem* item)>;

  explicit FlowLayout(QWidget* parent = nullptr, int margin = 0, int hSpacing = -1,
                      int vSpacing = -1);
  ~FlowLayout() override;

  void addItem(QLayoutItem* item) override;
  int horizontalSpacing() const;
  int verticalSpacing() const;
  void setHorizontalSpacing(int spacing);
  void setVerticalSpacing(int spacing);
  void setItemEndSpacingProvider(ItemEndSpacingProvider provider);
  Qt::Orientations expandingDirections() const override;
  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;
  int count() const override;
  QLayoutItem* itemAt(int index) const override;
  QLayoutItem* takeAt(int index) override;
  QSize minimumSize() const override;
  QSize sizeHint() const override;
  void setGeometry(const QRect& rect) override;

 private:
  int doLayout(const QRect& rect, bool testOnly) const;
  int smartSpacing(QStyle::PixelMetric pm) const;

  QList<QLayoutItem*> itemList_;
  int hSpacing_ = -1;
  int vSpacing_ = -1;
  ItemEndSpacingProvider itemEndSpacingProvider_;
};

}  // namespace adqt::widgets::detail
