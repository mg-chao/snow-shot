#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class ModalDocsPage final : public QWidget {
 public:
  explicit ModalDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using Modal = adqt::widgets::AdModal;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildControlledDemo();
  QWidget* buildAsyncDemo();
  QWidget* buildFooterDemo();
  QWidget* buildFooterRenderDemo();
  QWidget* buildLocaleDemo();
  QWidget* buildLoadingDemo();
  QWidget* buildMaskDemo();
  QWidget* buildWindowModeDemo();
  QWidget* buildPositionDemo();
  QWidget* buildWidthDemo();
  QWidget* buildButtonPropsDemo();
  QWidget* buildStaticInfoDemo();
  QWidget* buildStaticConfirmDemo();
  QWidget* buildManualDemo();
  QWidget* buildDestroyAllDemo();
  QWidget* buildStyleClassDemo();
  QWidget* buildComponentTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
