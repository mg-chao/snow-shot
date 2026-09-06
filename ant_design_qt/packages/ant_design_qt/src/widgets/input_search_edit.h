#pragma once

#include <QString>
#include <QWidget>

#include "input_line_edit.h"

class QHBoxLayout;
class QResizeEvent;
class QSpacerItem;

namespace adqt::widgets {

class AdButton;

class AdSearchEdit final : public QWidget {
  Q_OBJECT

  Q_PROPERTY(AdLineEdit::ControlSize controlSize READ controlSize WRITE setControlSize NOTIFY
                 controlSizeChanged)
  Q_PROPERTY(AdLineEdit::Variant variant READ variant WRITE setVariant NOTIFY variantChanged)
  Q_PROPERTY(AdLineEdit::Status status READ status WRITE setStatus NOTIFY statusChanged)
  Q_PROPERTY(bool allowClear READ allowClear WRITE setAllowClear NOTIFY allowClearChanged)
  Q_PROPERTY(QString placeholderText READ placeholderText WRITE setPlaceholderText NOTIFY
                 placeholderTextChanged)
  Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
  Q_PROPERTY(bool busy READ busy WRITE setBusy NOTIFY busyChanged)
  Q_PROPERTY(QString searchButtonText READ searchButtonText WRITE setSearchButtonText NOTIFY
                 searchButtonTextChanged)
  Q_PROPERTY(bool joinedLeft READ joinedLeft WRITE setJoinedLeft)
  Q_PROPERTY(bool joinedRight READ joinedRight WRITE setJoinedRight)
  Q_PROPERTY(adqt::icons::IconRef prefixIconRef READ prefixIconRef WRITE setPrefixIconRef NOTIFY
                 prefixIconRefChanged)
  Q_PROPERTY(adqt::icons::IconRef suffixIconRef READ suffixIconRef WRITE setSuffixIconRef NOTIFY
                 suffixIconRefChanged)
  Q_PROPERTY(adqt::icons::IconRef feedbackIconRef READ feedbackIconRef WRITE setFeedbackIconRef
                 NOTIFY feedbackIconRefChanged)

 public:
  enum class SearchReason {
    ReturnKey,
    ButtonClick,
    ClearAction,
  };
  Q_ENUM(SearchReason)

  using ControlSize = AdLineEdit::ControlSize;
  using Variant = AdLineEdit::Variant;
  using Status = AdLineEdit::Status;
  using FocusSelection = AdLineEdit::FocusSelection;

  explicit AdSearchEdit(QWidget* parent = nullptr);
  ~AdSearchEdit() override;

  ControlSize controlSize() const;
  void setControlSize(ControlSize value);

  Variant variant() const;
  void setVariant(Variant value);

  Status status() const;
  void setStatus(Status value);

  bool allowClear() const;
  void setAllowClear(bool value);

  QString placeholderText() const;
  void setPlaceholderText(const QString& value);

  QString text() const;
  void setText(const QString& value);
  void clear();

  bool busy() const;
  void setBusy(bool value);

  QString searchButtonText() const;
  void setSearchButtonText(const QString& value);

  adqt::icons::IconRef prefixIconRef() const;
  void setPrefixIconRef(const adqt::icons::IconRef& value);

  adqt::icons::IconRef suffixIconRef() const;
  void setSuffixIconRef(const adqt::icons::IconRef& value);

  adqt::icons::IconRef feedbackIconRef() const;
  void setFeedbackIconRef(const adqt::icons::IconRef& value);

  bool joinedLeft() const;
  void setJoinedLeft(bool value);

  bool joinedRight() const;
  void setJoinedRight(bool value);

  void focusEditor(FocusSelection selection = FocusSelection::Preserve, bool preventScroll = false);

  AdLineEdit* lineEdit() const;

 signals:
  void controlSizeChanged(ControlSize value);
  void variantChanged(Variant value);
  void statusChanged(Status value);
  void allowClearChanged(bool value);
  void placeholderTextChanged(const QString& value);
  void textChanged(const QString& value);
  void textEdited(const QString& value);
  void cleared();
  void busyChanged(bool value);
  void searchButtonTextChanged(const QString& value);
  void prefixIconRefChanged(const adqt::icons::IconRef& token);
  void suffixIconRefChanged(const adqt::icons::IconRef& token);
  void feedbackIconRefChanged(const adqt::icons::IconRef& token);
  void editingFinished();
  void searchRequested(const QString& value, SearchReason reason);

 protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void updateButtonVisual();
  void updateSeparatorVisual();
  void scheduleSeparatorRefresh();
  void syncAccessibleState();

  QHBoxLayout* rootLayout_ = nullptr;
  AdLineEdit* input_ = nullptr;
  QSpacerItem* joinOverlapSpacer_ = nullptr;
  AdButton* button_ = nullptr;
  QWidget* separatorOverlay_ = nullptr;
  bool busy_ = false;
  bool searchButtonVisible_ = false;
  bool separatorRefreshQueued_ = false;
  QString searchButtonText_;
};

}  // namespace adqt::widgets
