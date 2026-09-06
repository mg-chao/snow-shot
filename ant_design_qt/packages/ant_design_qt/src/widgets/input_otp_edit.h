#pragma once

#include <QString>
#include <QStringList>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include "input_line_edit.h"
#include "input_policies.h"

class QHBoxLayout;
class QLineEdit;

namespace adqt::widgets {

class AdOtpEdit final : public QWidget {
  Q_OBJECT

  Q_PROPERTY(int cellCount READ cellCount WRITE setCellCount NOTIFY cellCountChanged)
  Q_PROPERTY(QString code READ code WRITE setCode NOTIFY codeChanged)
  Q_PROPERTY(AdLineEdit::ControlSize controlSize READ controlSize WRITE setControlSize NOTIFY
                 controlSizeChanged)
  Q_PROPERTY(AdLineEdit::Variant variant READ variant WRITE setVariant NOTIFY variantChanged)
  Q_PROPERTY(AdLineEdit::Status status READ status WRITE setStatus NOTIFY statusChanged)
  Q_PROPERTY(bool maskInput READ maskInput WRITE setMaskInput NOTIFY maskInputChanged)
  Q_PROPERTY(
      QString maskCharacter READ maskCharacter WRITE setMaskCharacter NOTIFY maskCharacterChanged)
  Q_PROPERTY(adqt::widgets::AdOtpCodeFormatter* codeFormatter READ codeFormatter WRITE
                 setCodeFormatter NOTIFY codeFormatterChanged)
  Q_PROPERTY(adqt::widgets::AdOtpSeparatorFactory* separatorFactory READ separatorFactory WRITE
                 setSeparatorFactory NOTIFY separatorFactoryChanged)

 public:
  using ControlSize = AdLineEdit::ControlSize;
  using Variant = AdLineEdit::Variant;
  using Status = AdLineEdit::Status;

  explicit AdOtpEdit(QWidget* parent = nullptr);
  ~AdOtpEdit() override;

  QString code() const;
  void setCode(const QString& value);

  int cellCount() const;
  void setCellCount(int value);

  ControlSize controlSize() const;
  void setControlSize(ControlSize value);

  Variant variant() const;
  void setVariant(Variant value);

  Status status() const;
  void setStatus(Status value);

  bool maskInput() const;
  void setMaskInput(bool value);

  QString maskCharacter() const;
  void setMaskCharacter(const QString& value);

  AdOtpCodeFormatter* codeFormatter() const;
  void setCodeFormatter(AdOtpCodeFormatter* value);

  QString separatorText() const;
  void setSeparatorText(const QString& value);

  AdOtpSeparatorFactory* separatorFactory() const;
  void setSeparatorFactory(AdOtpSeparatorFactory* value);

  void focusEditor(int index = 0);
  QLineEdit* cellAt(int index) const;

 signals:
  void cellCountChanged(int value);
  void codeChanged(const QString& value);
  void controlSizeChanged(ControlSize value);
  void variantChanged(Variant value);
  void statusChanged(Status value);
  void maskInputChanged(bool value);
  void maskCharacterChanged(const QString& value);
  void codeFormatterChanged(AdOtpCodeFormatter* value);
  void separatorFactoryChanged(AdOtpSeparatorFactory* value);
  void cellsChanged(const QStringList& values);
  void codeCompleted(const QString& value);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;

 private:
  void rebuildCells();
  void applyVisualStyle();
  void applyValueToCells(const QString& value);
  void emitInputState();
  void handleCellEdited(int index, const QString& text);
  void distributeTextFrom(int index, const QString& text);
  void updateEchoModes();
  QString formattedCode(const QString& value) const;
  void syncAccessibleState();

  int cellCount_ = 6;
  QString code_;
  ControlSize controlSize_ = ControlSize::Medium;
  Variant variant_ = Variant::Outlined;
  Status status_ = Status::None;
  bool maskInput_ = false;
  QString maskCharacter_;
  QString separatorText_;
  QPointer<AdOtpCodeFormatter> codeFormatter_;
  QPointer<AdOtpSeparatorFactory> separatorFactory_;

  QHBoxLayout* rootLayout_ = nullptr;
  QVector<QLineEdit*> cells_;
  QVector<QWidget*> separators_;
  bool internalUpdate_ = false;
};

}  // namespace adqt::widgets
