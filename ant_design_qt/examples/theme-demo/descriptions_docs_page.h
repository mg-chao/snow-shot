#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class DescriptionsDocsPage final : public QWidget {
 public:
  explicit DescriptionsDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);
  QWidget* buildBasicDemo();
  QWidget* buildBorderAndSizeDemo();
  QWidget* buildVerticalDemo();
  QWidget* buildSpanDemo();
  QWidget* buildResponsiveAndCustomDemo();
  QWidget* buildTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
