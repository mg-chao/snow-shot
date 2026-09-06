#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

class CheckboxDocsPage final : public QWidget {
 public:
  explicit CheckboxDocsPage(QWidget* parent = nullptr);

  const QVector<QWidget*>& sectionAnchors() const;
  const QStringList& sectionTitles() const;

 private:
  void addSection(QVBoxLayout* root, const QString& title, const QString& description,
                  QWidget* content);

  QWidget* buildBasicDemo();
  QWidget* buildDisabledDemo();
  QWidget* buildControlledDemo();
  QWidget* buildGroupDemo();
  QWidget* buildCheckAllDemo();
  QWidget* buildVerticalDemo();
  QWidget* buildTokenDemo();

  QVector<QWidget*> anchors_;
  QStringList titles_;
};
