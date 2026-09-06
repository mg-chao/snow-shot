#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class SliderDocsPage final : public QWidget {
 public:
  explicit SliderDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using Mark = adqt::widgets::AdSliderMark;
  using MarkMap = adqt::widgets::AdSliderMarkMap;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  MarkMap temperatureMarks() const;

  QWidget* buildBasicDemo();
  QWidget* buildInputNumberDemo();
  QWidget* buildIconSliderDemo();
  QWidget* buildTipFormatterDemo();
  QWidget* buildEventDemo();
  QWidget* buildMarkDemo();
  QWidget* buildVerticalDemo();
  QWidget* buildShowTooltipDemo();
  QWidget* buildReverseDemo();
  QWidget* buildDraggableTrackDemo();
  QWidget* buildMultipleDemo();
  QWidget* buildEditableDemo();
  QWidget* buildStyleClassDemo();
  QWidget* buildComponentTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
