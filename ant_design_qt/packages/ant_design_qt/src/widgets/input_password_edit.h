#pragma once

#include "input_line_edit.h"

namespace adqt::widgets {

class AdPasswordEdit final : public AdLineEdit {
  Q_OBJECT

  Q_PROPERTY(bool revealActionVisible READ revealActionVisible WRITE setRevealActionVisible NOTIFY
                 revealActionVisibleChanged)
  Q_PROPERTY(bool textVisible READ textVisible WRITE setTextVisible NOTIFY textVisibleChanged)

 public:
  explicit AdPasswordEdit(QWidget* parent = nullptr);
  ~AdPasswordEdit() override;

  bool revealActionVisible() const;
  void setRevealActionVisible(bool value);

  bool textVisible() const;
  void setTextVisible(bool value);

  adqt::icons::IconRef visibleIconRef() const;
  void setVisibleIconRef(const adqt::icons::IconRef& value);

  adqt::icons::IconRef hiddenIconRef() const;
  void setHiddenIconRef(const adqt::icons::IconRef& value);

 signals:
  void revealActionVisibleChanged(bool value);
  void textVisibleChanged(bool value);

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void updateToggleVisual();

  bool revealActionVisible_ = true;
  bool textVisible_ = false;
  adqt::icons::IconRef visibleIconRef_;
  adqt::icons::IconRef hiddenIconRef_;
};

}  // namespace adqt::widgets
