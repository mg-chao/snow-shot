#pragma once

#include <QFrame>
#include <QScopedPointer>
#include <QString>

class QCloseEvent;
class QEvent;
class QHideEvent;
class QPaintEvent;
class QShowEvent;
class QWidget;

namespace adqt::widgets {

class AdAlertPrivate;

class AdAlert final : public QFrame {
  Q_OBJECT
  Q_DECLARE_PRIVATE(AdAlert)
  Q_DISABLE_COPY_MOVE(AdAlert)

  Q_PROPERTY(Severity severity READ severity WRITE setSeverity NOTIFY severityChanged)
  Q_PROPERTY(
      DisplayMode displayMode READ displayMode WRITE setDisplayMode NOTIFY displayModeChanged)
  Q_PROPERTY(IconMode iconMode READ iconMode WRITE setIconMode NOTIFY iconModeChanged)
  Q_PROPERTY(bool closable READ closable WRITE setClosable NOTIFY closableChanged)
  Q_PROPERTY(bool animated READ animated WRITE setAnimated NOTIFY animatedChanged)
  Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
  Q_PROPERTY(QString informativeText READ informativeText WRITE setInformativeText NOTIFY
                 informativeTextChanged)
  Q_PROPERTY(
      QWidget* leadingWidget READ leadingWidget WRITE setLeadingWidget NOTIFY leadingWidgetChanged)
  Q_PROPERTY(QWidget* textWidget READ textWidget WRITE setTextWidget NOTIFY textWidgetChanged)
  Q_PROPERTY(QWidget* informativeWidget READ informativeWidget WRITE setInformativeWidget NOTIFY
                 informativeWidgetChanged)
  Q_PROPERTY(
      QWidget* actionsWidget READ actionsWidget WRITE setActionsWidget NOTIFY actionsWidgetChanged)

 public:
  enum class Severity {
    Success,
    Info,
    Warning,
    Error,
  };
  Q_ENUM(Severity)

  enum class DisplayMode {
    Inline,
    Banner,
  };
  Q_ENUM(DisplayMode)

  enum class IconMode {
    Auto,
    Visible,
    Hidden,
  };
  Q_ENUM(IconMode)

  enum class CloseReason {
    CloseButton,
    Programmatic,
  };
  Q_ENUM(CloseReason)

  explicit AdAlert(QWidget* parent = nullptr);
  ~AdAlert() override;

  Severity severity() const;
  void setSeverity(Severity value);

  DisplayMode displayMode() const;
  void setDisplayMode(DisplayMode value);

  IconMode iconMode() const;
  void setIconMode(IconMode value);

  bool closable() const;
  void setClosable(bool value);

  bool animated() const;
  void setAnimated(bool value);

  QString text() const;
  void setText(const QString& value);

  QString informativeText() const;
  void setInformativeText(const QString& value);

  QWidget* leadingWidget() const;
  void setLeadingWidget(QWidget* widget);
  QWidget* takeLeadingWidget();

  QWidget* textWidget() const;
  void setTextWidget(QWidget* widget);
  QWidget* takeTextWidget();

  QWidget* informativeWidget() const;
  void setInformativeWidget(QWidget* widget);
  QWidget* takeInformativeWidget();

  QWidget* actionsWidget() const;
  void setActionsWidget(QWidget* widget);
  QWidget* takeActionsWidget();

 signals:
  void severityChanged(Severity value);
  void displayModeChanged(DisplayMode value);
  void iconModeChanged(IconMode value);
  void closableChanged(bool value);
  void animatedChanged(bool value);
  void textChanged(const QString& value);
  void informativeTextChanged(const QString& value);
  void leadingWidgetChanged(QWidget* value);
  void textWidgetChanged(QWidget* value);
  void informativeWidgetChanged(QWidget* value);
  void actionsWidgetChanged(QWidget* value);
  void closeRequested(CloseReason reason);
  void closed(CloseReason reason);

 protected:
  void changeEvent(QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private:
  QScopedPointer<AdAlertPrivate> d_ptr;
};

}  // namespace adqt::widgets
