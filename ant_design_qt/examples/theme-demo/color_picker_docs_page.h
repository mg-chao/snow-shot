#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class ColorPickerDocsPage final : public QWidget {
 public:
  explicit ColorPickerDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using ColorValue = adqt::widgets::AdColorValue;
  using GradientStop = adqt::widgets::AdColorGradientStop;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  static ColorValue solid(const QString& value);
  static ColorValue gradient(const QVector<GradientStop>& stops);

  QWidget* buildBaseDemo();
  QWidget* buildSizeDemo();
  QWidget* buildControlledDemo();
  QWidget* buildLineGradientDemo();
  QWidget* buildTextRenderDemo();
  QWidget* buildDisabledDemo();
  QWidget* buildDisabledAlphaDemo();
  QWidget* buildAllowClearDemo();
  QWidget* buildTriggerDemo();
  QWidget* buildTriggerEventDemo();
  QWidget* buildPopupLayerModeDemo();
  QWidget* buildFormatDemo();
  QWidget* buildPresetsDemo();
  QWidget* buildPanelRenderDemo();
  QWidget* buildStyleClassDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
