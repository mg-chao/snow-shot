#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class TagDocsPage final : public QWidget {
 public:
  explicit TagDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildColorfulDemo();
  QWidget* buildStatusDemo();
  QWidget* buildCheckableDemo();
  QWidget* buildControlDemo();
  QWidget* buildIconDemo();
  QWidget* buildDisabledDemo();
  QWidget* buildComponentTokenDemo();
  QWidget* buildStyleClassDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
