#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class RadioDocsPage final : public QWidget {
 public:
  explicit RadioDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildQtNativeGroupingDemo();
  QWidget* buildDisabledDemo();
  QWidget* buildRadioGroupDemo();
  QWidget* buildVerticalGroupDemo();
  QWidget* buildBlockGroupDemo();
  QWidget* buildGroupOptionsDemo();
  QWidget* buildRadioButtonDemo();
  QWidget* buildSizeDemo();
  QWidget* buildSolidButtonDemo();
  QWidget* buildStyleClassDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
