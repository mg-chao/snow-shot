#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class SegmentedDocsPage final : public QWidget {
 public:
  explicit SegmentedDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildLayoutDemo();
  QWidget* buildStateAndSizeDemo();
  QWidget* buildIconDemo();
  QWidget* buildDynamicDemo();
  QWidget* buildCustomAndTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
