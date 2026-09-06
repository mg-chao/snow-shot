#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class SelectDocsPage final : public QWidget {
 public:
  explicit SelectDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using Option = adqt::widgets::AdComboBox::Option;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QVector<Option> basicOptions() const;
  QVector<Option> alphaNumericOptions() const;
  QVector<Option> cityOptions() const;

  QWidget* buildBasicDemo();
  QWidget* buildSearchDemo();
  QWidget* buildSearchFilterOptionDemo();
  QWidget* buildSearchMultiFieldDemo();
  QWidget* buildMultipleDemo();
  QWidget* buildSizeDemo();
  QWidget* buildOptionRenderDemo();
  QWidget* buildSearchSortDemo();
  QWidget* buildTagsDemo();
  QWidget* buildOptGroupDemo();
  QWidget* buildCoordinateDemo();
  QWidget* buildSearchBoxDemo();
  QWidget* buildLabelInValueDemo();
  QWidget* buildAutomaticTokenizationDemo();
  QWidget* buildSelectUsersDemo();
  QWidget* buildSuffixDemo();
  QWidget* buildCustomDropdownDemo();
  QWidget* buildHideSelectedDemo();
  QWidget* buildVariantDemo();
  QWidget* buildCustomTagRenderDemo();
  QWidget* buildCustomLabelRenderDemo();
  QWidget* buildResponsiveDemo();
  QWidget* buildStatusDemo();
  QWidget* buildPlacementDemo();
  QWidget* buildPopupLayerModeDemo();
  QWidget* buildMaxCountDemo();
  QWidget* buildStyleClassDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
