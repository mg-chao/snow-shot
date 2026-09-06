#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class SpinDocsPage final : public QWidget {
 public:
  explicit SpinDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildSizeDemo();
  QWidget* buildNestedDemo();
  QWidget* buildDescriptionDemo();
  QWidget* buildDelayDemo();
  QWidget* buildCustomIndicatorDemo();
  QWidget* buildProgressDemo();
  QWidget* buildFullscreenDemo();
  QWidget* buildTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
