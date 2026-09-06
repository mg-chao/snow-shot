#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class PaginationDocsPage final : public QWidget {
 public:
  explicit PaginationDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);
  QWidget* buildBasicDemo();
  QWidget* buildMoreDemo();
  QWidget* buildChangerDemo();
  QWidget* buildJumperDemo();
  QWidget* buildSizeDemo();
  QWidget* buildSimpleDemo();
  QWidget* buildTotalDemo();
  QWidget* buildControlledDemo();
  QWidget* buildAlignmentDemo();
  QWidget* buildTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
