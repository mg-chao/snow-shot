#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class DatePickerDocsPage final : public QWidget {
 public:
  explicit DatePickerDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildModeDemo();
  QWidget* buildShowWeekDemo();
  QWidget* buildLocaleDemo();
  QWidget* buildBuddhistEraDemo();
  QWidget* buildPanelViewDemo();
  QWidget* buildRangeDemo();
  QWidget* buildRangeSeparatorDemo();
  QWidget* buildStartEndDemo();
  QWidget* buildMinMaxDemo();
  QWidget* buildRangeConstraintDemo();
  QWidget* buildAllowEmptyDemo();
  QWidget* buildFormatDemo();
  QWidget* buildMaskFormatDemo();
  QWidget* buildInputBehaviorDemo();
  QWidget* buildCellRenderDemo();
  QWidget* buildMultipleDemo();
  QWidget* buildNeedConfirmDemo();
  QWidget* buildShowTimeDemo();
  QWidget* buildTimePickerDemo();
  QWidget* buildPrefixSuffixDemo();
  QWidget* buildNavigationIconDemo();
  QWidget* buildSemanticStyleDemo();
  QWidget* buildComponentTokenDemo();
  QWidget* buildSizeDemo();
  QWidget* buildVariantDemo();
  QWidget* buildStatusDisabledDemo();
  QWidget* buildPresetFooterDemo();
  QWidget* buildPlacementDemo();
  QWidget* buildPopupLayerModeDemo();
  QWidget* buildPopupContentWrapperDemo();
  QWidget* buildPanelComponentDemo();
  QWidget* buildPanelDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
