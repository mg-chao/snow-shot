#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class MessageDocsPage final : public QWidget {
 public:
  explicit MessageDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildTypesDemo();
  QWidget* buildDurationDemo();
  QWidget* buildLoadingDemo();
  QWidget* buildUpdateDemo();
  QWidget* buildManualDemo();
  QWidget* buildMaximumCountDemo();
  QWidget* buildCustomContentDemo();
  QWidget* buildSemanticStyleDemo();
  QWidget* buildCallbackDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
