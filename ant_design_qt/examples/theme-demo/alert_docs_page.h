#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include <optional>

#include "widgets/widgets.h"

class QVBoxLayout;

class AlertDocsPage final : public QWidget {
 public:
  explicit AlertDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  using AdAlert = adqt::widgets::AdAlert;

  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  AdAlert* makeAlert(const QString& text, std::optional<AdAlert::Severity> severity = std::nullopt,
                     std::optional<AdAlert::IconMode> iconMode = std::nullopt,
                     bool closable = false, const QString& informativeText = QString(),
                     QWidget* parent = nullptr);

  QWidget* buildBasicDemo();
  QWidget* buildStyleDemo();
  QWidget* buildClosableDemo();
  QWidget* buildDescriptionDemo();
  QWidget* buildIconDemo();
  QWidget* buildBannerDemo();
  QWidget* buildLoopBannerDemo();
  QWidget* buildLeadingWidgetDemo();
  QWidget* buildLifecycleDemo();
  QWidget* buildActionDemo();
  QWidget* buildPaletteDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
