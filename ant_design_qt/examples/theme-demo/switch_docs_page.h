#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class SwitchDocsPage final : public QWidget {
 public:
  explicit SwitchDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using AdSwitch = adqt::widgets::AdSwitch;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildDisabledDemo();
  QWidget* buildTextDemo();
  QWidget* buildSizeDemo();
  QWidget* buildLoadingDemo();
  QWidget* buildComponentTokenDemo();
  QWidget* buildStyleResolverDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
