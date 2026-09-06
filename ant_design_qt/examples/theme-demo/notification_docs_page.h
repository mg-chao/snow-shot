#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class NotificationDocsPage final : public QWidget {
 public:
  explicit NotificationDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);
  QWidget* buildBasicDemo();
  QWidget* buildTypesDemo();
  QWidget* buildPlacementDemo();
  QWidget* buildDurationDemo();
  QWidget* buildProgressDemo();
  QWidget* buildStackDemo();
  QWidget* buildUpdateDemo();
  QWidget* buildActionsDemo();
  QWidget* buildCustomStyleDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
