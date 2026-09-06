#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class FormDocsPage final : public QWidget {
 public:
  explicit FormDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildLayoutDemo();
  QWidget* buildRequiredMarkDemo();
  QWidget* buildValidateStatusDemo();
  QWidget* buildDynamicValidationDemo();
  QWidget* buildFieldPathDependencyDemo();
  QWidget* buildFormListDemo();
  QWidget* buildControlPropagationDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
