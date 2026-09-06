#ifndef SNOW_SHOT_PRESENTATION_COMPONENTS_PATHINPUT_H
#define SNOW_SHOT_PRESENTATION_COMPONENTS_PATHINPUT_H

#include "icon_core.h"
#include "widgets/input_line_edit.h"

#include <QString>
#include <QWidget>

namespace adqt::widgets {
class AdButton;
class AdFieldGroup;
} // namespace adqt::widgets

class DirectoryPathInput : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString placeholderText READ placeholderText WRITE setPlaceholderText)
    Q_PROPERTY(bool allowClear READ allowClear WRITE setAllowClear)

  public:
    explicit DirectoryPathInput(QWidget* parent = nullptr);
    ~DirectoryPathInput() override;

    [[nodiscard]] QString text() const;
    void setText(const QString& text);
    void clear();

    [[nodiscard]] QString placeholderText() const;
    void setPlaceholderText(const QString& text);

    [[nodiscard]] bool allowClear() const;
    void setAllowClear(bool allow);

    [[nodiscard]] adqt::widgets::AdLineEdit::ControlSize controlSize() const;
    void setControlSize(adqt::widgets::AdLineEdit::ControlSize size);

    [[nodiscard]] QString browseButtonText() const;
    void setBrowseButtonText(const QString& text);

    [[nodiscard]] adqt::widgets::AdLineEdit* lineEdit() const;
    [[nodiscard]] adqt::widgets::AdButton* browseButton() const;
    [[nodiscard]] adqt::widgets::AdFieldGroup* fieldGroup() const;

  signals:
    void textChanged(const QString& text);
    void textEdited(const QString& text);
    void editingFinished();
    void cleared();
    void browseRequested(const QString& currentPath);

  protected:
    DirectoryPathInput(const adqt::icons::IconRef& browseIcon, QWidget* parent);

  private:
    adqt::widgets::AdFieldGroup* m_group = nullptr;
    adqt::widgets::AdLineEdit* m_lineEdit = nullptr;
    adqt::widgets::AdButton* m_browseButton = nullptr;
    QString m_browseButtonText;
};

class FilePathInput final : public DirectoryPathInput {
    Q_OBJECT

  public:
    explicit FilePathInput(QWidget* parent = nullptr);
};

#endif // SNOW_SHOT_PRESENTATION_COMPONENTS_PATHINPUT_H
