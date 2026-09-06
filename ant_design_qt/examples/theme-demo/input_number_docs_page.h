#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class InputNumberDocsPage final : public QWidget {
 public:
  explicit InputNumberDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildSizeDemo();
  QWidget* buildAddonDemo();
  QWidget* buildDisabledDemo();
  QWidget* buildDigitDemo();
  QWidget* buildFormatterDemo();
  QWidget* buildKeyboardDemo();
  QWidget* buildWheelDemo();
  QWidget* buildVariantDemo();
  QWidget* buildSpinnerDemo();
  QWidget* buildFilledDebugDemo();
  QWidget* buildOutOfRangeDemo();
  QWidget* buildPreSuffixDemo();
  QWidget* buildStatusDemo();
  QWidget* buildFocusDemo();
  QWidget* buildStyleClassDemo();
  QWidget* buildControlsDemo();
  QWidget* buildRenderPanelDemo();
  QWidget* buildDebugTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
