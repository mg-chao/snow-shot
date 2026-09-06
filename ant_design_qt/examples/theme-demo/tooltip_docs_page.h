#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class TooltipDocsPage final : public QWidget {
 public:
  explicit TooltipDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using Placement = adqt::widgets::AdTooltip::Placement;
  using Trigger = adqt::widgets::AdTooltip::Trigger;
  using Triggers = adqt::widgets::AdTooltip::Triggers;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* makeTooltip(const QString& triggerText, const QString& title,
                       Triggers triggers = Trigger::Hover, QWidget* parent = nullptr,
                       adqt::widgets::AdTooltip** tooltipOut = nullptr);

  QWidget* buildBasicDemo();
  QWidget* buildSmoothTransitionDemo();
  QWidget* buildPlacementDemo();
  QWidget* buildArrowDemo();
  QWidget* buildShiftDemo();
  QWidget* buildAutoAdjustOverflowDemo();
  QWidget* buildDestroyOnHiddenDemo();
  QWidget* buildColorfulDemo();
  QWidget* buildDisabledDemo();
  QWidget* buildDisabledChildrenDemo();
  QWidget* buildWrapCustomComponentDemo();
  QWidget* buildStyleClassDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
