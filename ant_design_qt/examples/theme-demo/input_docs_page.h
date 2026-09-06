#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class InputDocsPage final : public QWidget {
 public:
  explicit InputDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildSizeDemo();
  QWidget* buildVariantDemo();
  QWidget* buildCompactStyleDemo();
  QWidget* buildSearchInputDemo();
  QWidget* buildSearchLoadingDemo();
  QWidget* buildTextAreaDemo();
  QWidget* buildAutoSizeTextAreaDemo();
  QWidget* buildOtpDemo();
  QWidget* buildTooltipDemo();
  QWidget* buildPreSuffixDemo();
  QWidget* buildPasswordDemo();
  QWidget* buildClearButtonDemo();
  QWidget* buildShowCountDemo();
  QWidget* buildAdvanceCountDemo();
  QWidget* buildStatusDemo();
  QWidget* buildFocusDemo();
  QWidget* buildStyleClassDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
