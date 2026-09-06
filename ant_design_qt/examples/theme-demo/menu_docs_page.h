#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include "widgets/widgets.h"

class QVBoxLayout;

class MenuDocsPage final : public QWidget {
 public:
  explicit MenuDocsPage(QWidget* parent = nullptr);
  static const QStringList& defaultSectionTitles();

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildHorizontalDemo(bool dark);
  QWidget* buildInlineDemo();
  QWidget* buildInlineCollapsedDemo();
  QWidget* buildTooltipDemo();
  QWidget* buildSiderCurrentDemo();
  QWidget* buildVerticalDemo();
  QWidget* buildContextMenuDemo();
  QWidget* buildColorSchemeDemo();
  QWidget* buildPopupColorSchemeDemo();
  QWidget* buildSwitchModeDemo();
  QWidget* buildStyleClassDemo();
  QWidget* buildStyleDebugDemo();
  QWidget* buildMenuV4Demo();
  QWidget* buildComponentTokenDemo();
  QWidget* buildExtraStyleDemo();
  QWidget* buildCustomPopupRenderDemo();
  QWidget* buildSemanticDemo();
  QWidget* buildApiOverview();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
