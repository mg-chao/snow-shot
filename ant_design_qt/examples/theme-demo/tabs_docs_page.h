#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class TabsDocsPage final : public QWidget {
 public:
  explicit TabsDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildPlacementDemo();
  QWidget* buildSizeDemo();
  QWidget* buildCardDemo();
  QWidget* buildEditableDemo();
  QWidget* buildOverflowDemo();
  QWidget* buildTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
