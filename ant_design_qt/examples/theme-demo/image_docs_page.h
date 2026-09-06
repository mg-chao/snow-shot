#pragma once

#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class ImageDocsPage final : public QWidget {
 public:
  explicit ImageDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using AdImage = adqt::widgets::AdImage;
  using AdImageItem = adqt::widgets::AdImageItem;
  using AdImageItems = adqt::widgets::AdImageItems;
  using AdImageListModel = adqt::widgets::AdImageListModel;
  using AdImageViewer = adqt::widgets::AdImageViewer;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildFallbackDemo();
  QWidget* buildPlaceholderDemo();
  QWidget* buildPreviewGroupDemo();
  QWidget* buildPreviewItemsDemo();
  QWidget* buildPreviewMaskDemo();
  QWidget* buildStyleDemo();
  QWidget* buildPreviewInfoDemo();
  QWidget* buildNestedDemo();

  static AdImage* createImage(const QUrl& src, const QSize& preferredSize,
                              const QString& alt = QStringLiteral("image"),
                              QWidget* parent = nullptr);

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
