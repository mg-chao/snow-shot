#include "snow_shot/presentation/components/pathinput.h"

#include "antd_icons.h"
#include "widgets/button.h"
#include "widgets/field_group.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSizePolicy>

namespace {
namespace outlined_icons = adqt::icons::antd::outlined;
}

DirectoryPathInput::DirectoryPathInput(QWidget* parent)
    : DirectoryPathInput(outlined_icons::FolderOpen(), parent) {}

DirectoryPathInput::DirectoryPathInput(const adqt::icons::IconRef& browseIcon, QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_group = new adqt::widgets::AdFieldGroup(this);
    m_group->setObjectName(QStringLiteral("pathInputFieldGroup"));
    m_group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_group);

    m_lineEdit = new adqt::widgets::AdLineEdit(m_group);
    m_lineEdit->setObjectName(QStringLiteral("pathInputLineEdit"));
    m_lineEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_browseButton = new adqt::widgets::AdButton(m_group);
    m_browseButton->setObjectName(QStringLiteral("pathInputBrowseButton"));
    m_browseButton->setButtonStyle(adqt::widgets::AdButton::ButtonStyle::Outline);
    m_browseButton->setAccentRole(adqt::widgets::AdButton::AccentRole::Neutral);
    m_browseButton->setIconRef(browseIcon);
    m_browseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_group->addControl(m_lineEdit, 1);
    m_group->addControl(m_browseButton);
    setFocusProxy(m_lineEdit);

    connect(m_lineEdit, &QLineEdit::textChanged, this, &DirectoryPathInput::textChanged);
    connect(m_lineEdit, &QLineEdit::textEdited, this, &DirectoryPathInput::textEdited);
    connect(m_lineEdit, &QLineEdit::editingFinished, this, &DirectoryPathInput::editingFinished);
    connect(m_lineEdit, &adqt::widgets::AdLineEdit::cleared, this, &DirectoryPathInput::cleared);
    connect(m_browseButton, &QAbstractButton::clicked, this,
            [this] { emit browseRequested(text()); });
    setControlSize(adqt::widgets::AdLineEdit::ControlSize::Medium);
}

DirectoryPathInput::~DirectoryPathInput() {
    disconnect(m_lineEdit, nullptr, this, nullptr);
    disconnect(m_browseButton, nullptr, this, nullptr);
}

QString DirectoryPathInput::text() const {
    return m_lineEdit->text();
}

void DirectoryPathInput::setText(const QString& text) {
    m_lineEdit->setText(text);
}

void DirectoryPathInput::clear() {
    m_lineEdit->clear();
}

QString DirectoryPathInput::placeholderText() const {
    return m_lineEdit->placeholderText();
}

void DirectoryPathInput::setPlaceholderText(const QString& text) {
    m_lineEdit->setPlaceholderText(text);
}

bool DirectoryPathInput::allowClear() const {
    return m_lineEdit->allowClear();
}

void DirectoryPathInput::setAllowClear(bool allow) {
    m_lineEdit->setAllowClear(allow);
}

adqt::widgets::AdLineEdit::ControlSize DirectoryPathInput::controlSize() const {
    return m_lineEdit->controlSize();
}

void DirectoryPathInput::setControlSize(adqt::widgets::AdLineEdit::ControlSize size) {
    m_lineEdit->setControlSize(size);
    m_browseButton->setSizeClass(
        static_cast<adqt::widgets::AdButton::SizeClass>(static_cast<int>(size)));
}

QString DirectoryPathInput::browseButtonText() const {
    return m_browseButtonText;
}

void DirectoryPathInput::setBrowseButtonText(const QString& text) {
    m_browseButtonText = text;
    m_browseButton->setToolTip(text);
    m_browseButton->setAccessibleName(text);
}

adqt::widgets::AdLineEdit* DirectoryPathInput::lineEdit() const {
    return m_lineEdit;
}

adqt::widgets::AdButton* DirectoryPathInput::browseButton() const {
    return m_browseButton;
}

adqt::widgets::AdFieldGroup* DirectoryPathInput::fieldGroup() const {
    return m_group;
}

FilePathInput::FilePathInput(QWidget* parent)
    : DirectoryPathInput(outlined_icons::FileAdd(), parent) {}
