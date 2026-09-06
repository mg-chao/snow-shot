#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class CarouselDocsPage final : public QWidget {
 public:
  explicit CarouselDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildPlacementDemo();
  QWidget* buildAutoplayDemo();
  QWidget* buildFadeDemo();
  QWidget* buildArrowsDemo();
  QWidget* buildTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
