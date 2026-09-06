#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class PopconfirmDocsPage final : public QWidget {
 public:
  explicit PopconfirmDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using Placement = adqt::widgets::AdPopconfirm::Placement;
  using Trigger = adqt::widgets::AdPopconfirm::Trigger;
  using Triggers = adqt::widgets::AdPopconfirm::Triggers;
  using VisibilityMode = adqt::widgets::AdPopconfirm::VisibilityMode;
  using StandardButton = adqt::widgets::AdPopconfirm::StandardButton;
  using StandardButtons = adqt::widgets::AdPopconfirm::StandardButtons;
  using Icon = adqt::widgets::AdPopconfirm::Icon;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* makePopconfirm(const QString& triggerText, const QString& title,
                          const QString& description, Triggers triggers = Trigger::Click,
                          QWidget* parent = nullptr,
                          adqt::widgets::AdPopconfirm** popconfirmOut = nullptr);

  QWidget* buildBasicDemo();
  QWidget* buildLocaleDemo();
  QWidget* buildPlacementDemo();
  QWidget* buildAutoShiftDemo();
  QWidget* buildDynamicTriggerDemo();
  QWidget* buildIconDemo();
  QWidget* buildAsyncDemo();
  QWidget* buildPromiseDemo();
  QWidget* buildStyleClassDemo();
  QWidget* buildRenderPanelDemo();
  QWidget* buildWireframeDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
