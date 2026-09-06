#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class PopoverDocsPage final : public QWidget {
 public:
  explicit PopoverDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using Placement = adqt::widgets::AdPopover::Placement;
  using Trigger = adqt::widgets::AdPopover::Trigger;
  using Triggers = adqt::widgets::AdPopover::Triggers;
  using VisibilityPolicy = adqt::widgets::AdPopover::VisibilityPolicy;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildTriggerDemo();
  QWidget* buildPlacementDemo();
  QWidget* buildArrowDemo();
  QWidget* buildAutoShiftDemo();
  QWidget* buildPopupLayerModeDemo();
  QWidget* buildControlledDemo();
  QWidget* buildHoverWithClickDemo();
  QWidget* buildVisualStyleDemo();
  QWidget* buildVisualPropertyDemo();
  QWidget* buildApiOverview();

  QWidget* makePopover(const QString& triggerText, const QString& title, const QString& content,
                       Triggers triggers, QWidget* parent = nullptr,
                       adqt::widgets::AdPopover** popoverOut = nullptr);

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
