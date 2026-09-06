#pragma once

#include <QPointer>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QEvent;

namespace adqt::widgets {

class AdFieldGroup final : public QWidget {
  Q_OBJECT

 public:
  explicit AdFieldGroup(QWidget* parent = nullptr);
  ~AdFieldGroup() override;

  void addControl(QWidget* control, int stretch = 0);
  void insertControl(int index, QWidget* control, int stretch = 0);
  int controlCount() const;
  QWidget* controlAt(int index) const;

 protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct ControlEntry {
    QPointer<QWidget> control;
    int stretch = 0;
  };

  void refreshJoinedEdges();
  void rebuildLayout();
  int overlapWidth() const;

  QVector<ControlEntry> controls_;
  QHBoxLayout* layout_ = nullptr;
};

}  // namespace adqt::widgets
