#include "snow_shot/presentation/components/globalmouserow.h"
#include "snow_shot/mouseoperations.h"
#include "snow_shot/presentation/components/formfields.h"

#include "snow_shot/presentation/components/icons/snowshoticons.h"
#include "snow_shot/presentation/components/shortcutconfigurationbutton.h"
#include "snow_shot/presentation/settings/settingsruntimesession.h"
#include "snow_shot/presentation/styles/mainwindowcomponenttoken.h"

#include "widgets/form.h"
#include "widgets/modal.h"
#include "widgets/select.h"

#include <QAbstractButton>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QPointer>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {
namespace custom_outlined_icons = snow_shot::presentation::icons::custom::outlined;
namespace settings = snow_shot::presentation::settings;
namespace mouse = snow_shot::mouse;
namespace fields = snow_shot::presentation::components::form_fields;

constexpr int CONFIGURATION_MODAL_WIDTH = 520;
constexpr int COMBINATION_TEXT_MAX_WIDTH = 260;

adqt::icons::IconRef actionIcon(settings::SettingsGlobalMouseAction action) {
    switch (action) {
    case settings::SettingsGlobalMouseAction::Screenshot:
        return snow_shot::presentation::icons::custom::twotone::ScreenshotFeature();
    case settings::SettingsGlobalMouseAction::ScreenshotCopy:
        return custom_outlined_icons::ScreenshotCopy();
    case settings::SettingsGlobalMouseAction::ScreenshotPinToScreen:
        return custom_outlined_icons::PinToScreen();
    case settings::SettingsGlobalMouseAction::ScreenshotOcr:
    case settings::SettingsGlobalMouseAction::ScreenshotOcrCopy:
        return custom_outlined_icons::TextRecognition();
    case settings::SettingsGlobalMouseAction::ScreenshotTranslation:
        return custom_outlined_icons::OcrTranslate();
    case settings::SettingsGlobalMouseAction::ScreenshotSave:
        return custom_outlined_icons::Save();
    case settings::SettingsGlobalMouseAction::ScreenshotQuickSave:
        return custom_outlined_icons::QuickSave();
    case settings::SettingsGlobalMouseAction::ScreenRecording:
        return custom_outlined_icons::RecordScreen();
    }
    return {};
}

struct GlobalMouseChoice {
    QString id;
    mouse::Binding binding;
};
const GlobalMouseChoice mouseChoices[] = {
    {QStringLiteral("left_drag"), {mouse::Kind::Drag, Qt::LeftButton}},
    {QStringLiteral("right_drag"), {mouse::Kind::Drag, Qt::RightButton}},
    {QStringLiteral("wheel_drag"), {mouse::Kind::Drag, Qt::MiddleButton}},
    {QStringLiteral("side_button_1_drag"), {mouse::Kind::Drag, Qt::BackButton}},
    {QStringLiteral("side_button_2_drag"), {mouse::Kind::Drag, Qt::ForwardButton}},
    {QStringLiteral("none"), {}},
};

QString mouseButtonLabel(const QString& id) {
    for (const auto& choice : mouseChoices) {
        if (id == choice.id)
            return mouse::operationLabel(choice.binding);
    }
    return id;
}

adqt::widgets::AdSelect::Option option(const QString& value, const QString& label) {
    adqt::widgets::AdSelect::Option result;
    result.value = value;
    result.label = label;
    return result;
}
} // namespace

GlobalMouseRow::GlobalMouseRow(const QString& title, settings::SettingsGlobalMouseAction action,
                               settings::SettingsRuntimeSession& runtimeSession,
                               const snow_shot::presentation::styles::ThemeColorScheme& colorScheme,
                               QWidget* parent)
    : ActionRow({title, actionIcon(action), {}, true}, colorScheme.metricAlias,
                snow_shot::presentation::styles::buildMainWindowComponentMetricToken(colorScheme),
                colorScheme, parent),
      m_title(title), m_action(action), m_runtimeSession(runtimeSession) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_titleLabel->setObjectName(QStringLiteral("globalMouseActionLabel"));
    if (auto* icon = findChild<QLabel*>(QStringLiteral("actionRowTitleIcon"))) {
        icon->setObjectName(QStringLiteral("globalMouseActionIcon"));
    }

    m_button = createConfigurationButton(
        colorScheme.metricAlias, custom_outlined_icons::WheelMouse(), COMBINATION_TEXT_MAX_WIDTH);
    m_button->setObjectName(QStringLiteral("globalMouseConfigurationButton"));
    m_button->setRegistrationStatusTooltipVisible(false);
    connect(m_button, &QAbstractButton::clicked, this, [this]() { openConfigurationDialog(); });

    connect(&m_runtimeSession, &settings::SettingsRuntimeSession::globalMousePermissionChanged,
            this, [this] { syncButton(); });
    setCombination(m_runtimeSession.globalMouseCombination(m_action));
    retranslateUi();
    applyTheme(m_colorScheme);
}

void GlobalMouseRow::setTitle(const QString& title) {
    m_title = title;
    setAccessibleName(m_title);
    if (m_titleLabel != nullptr) {
        m_titleLabel->setText(m_title);
    }
    retranslateUi();
}

void GlobalMouseRow::setCombination(const settings::SettingsGlobalMouseCombination& combination) {
    m_combination = combination;
    syncButton();
}

ShortcutConfigurationButton* GlobalMouseRow::configurationButton() const {
    return m_button;
}

void GlobalMouseRow::applyTheme(const snow_shot::presentation::styles::ThemeColorScheme& scheme) {
    ActionRow::applyTheme(scheme);
    if (m_validationLabel != nullptr) {
        QPalette palette = m_validationLabel->palette();
        palette.setColor(QPalette::WindowText, scheme.map.colorError);
        m_validationLabel->setPalette(palette);
    }
    if (m_button != nullptr) {
        m_button->setTheme(scheme);
    }
    update();
}

void GlobalMouseRow::retranslateUi() {
    if (m_titleLabel != nullptr) {
        m_titleLabel->setText(m_title);
    }
    if (m_button != nullptr) {
        const QString accessibleName = tr("Configure mouse combination for %1").arg(m_title);
        m_button->setAccessibleName(accessibleName);
        m_button->setToolTip(accessibleName);
    }
    syncButton();
    syncModalText();
}

void GlobalMouseRow::changeEvent(QEvent* event) {
    ActionRow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void GlobalMouseRow::mousePressEvent(QMouseEvent* event) {
    ActionRow::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && isEnabled() && m_modal == nullptr &&
        rect().contains(event->position().toPoint())) {
        emit dragRequested(m_action);
    }
}

void GlobalMouseRow::openConfigurationDialog() {
    if (m_modal != nullptr) {
        return;
    }

    auto* modal = new adqt::widgets::AdModal(this);
    m_modal = modal;
    modal->setObjectName(QStringLiteral("globalMouseConfigurationModal"));
    modal->setOwnerWindow(window());
    modal->setCentered(true);
    modal->setPreferredWidth(CONFIGURATION_MODAL_WIDTH);
    modal->setCloseOnMaskClick(false);
    modal->setClosePolicy(adqt::widgets::AdModal::ClosePolicy::Manual);
    modal->setStandardButtons(adqt::widgets::AdModal::StandardButton::Ok |
                              adqt::widgets::AdModal::StandardButton::Cancel);

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("globalMouseConfigurationContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(m_colorScheme.metricAlias.margin);

    auto* form = new adqt::widgets::AdForm(content);
    form->setObjectName(QStringLiteral("globalMouseConfigurationForm"));
    fields::configureForm(form);
    contentLayout->addWidget(form);
    fields::Options fieldOptions;
    fieldOptions.form = form;
    fieldOptions.commitPolicy = fields::CommitPolicy::Explicit;
    fieldOptions.popupInModal = true;
    fields::Metadata activationMetadata;
    activationMetadata.id = QStringLiteral("activationKeys");
    activationMetadata.label = {"GlobalMouseRow",
                                QT_TRANSLATE_NOOP("GlobalMouseRow", "Activation keys")};
    const auto activationField = fields::select(activationMetadata, {}, fieldOptions);
    m_activationSelect = activationField.editor;
    m_activationSelect->setObjectName(QStringLiteral("globalMouseActivationKeySelect"));
    m_activationSelect->setMode(adqt::widgets::AdSelect::Mode::Multiple);
    m_activationField = activationField.item();
    m_activationController = activationField.field;
    fields::Metadata mouseMetadata;
    mouseMetadata.id = QStringLiteral("mouseButton");
    mouseMetadata.label = {"GlobalMouseRow", QT_TRANSLATE_NOOP("GlobalMouseRow", "Mouse button")};
    const auto mouseField = fields::select(mouseMetadata, {}, fieldOptions);
    m_mouseButtonSelect = mouseField.editor;
    m_mouseButtonSelect->setObjectName(QStringLiteral("globalMouseButtonSelect"));
    m_mouseButtonField = mouseField.item();
    m_mouseButtonController = mouseField.field;
    m_validationLabel = mouseField.field->feedbackLabel();
    m_validationLabel->setObjectName(QStringLiteral("globalMouseValidationMessage"));

    const settings::SettingsGlobalMouseCombination initial =
        m_combination.isUnset()
#ifdef Q_OS_MACOS
            ? settings::SettingsGlobalMouseCombination{{QStringLiteral("command")}, {}}
#else
            ? settings::SettingsGlobalMouseCombination{{QStringLiteral("windows")}, {}}
#endif
            : m_combination;
    modal->setContentWidget(content);
    syncModalText();
    QVariantList initialKeys;
    for (const QString& key : initial.activationKeys) {
        initialKeys.push_back(key);
    }
    // Measure native modifier tags at their eventual modal width before showing
    // the panel. The longer Command label otherwise wraps at the hidden widget's
    // default width and shrinks the visible editor on its first layout pass.
    m_activationSelect->resize(CONFIGURATION_MODAL_WIDTH - 2 * m_colorScheme.metricAlias.paddingLG,
                               m_activationSelect->sizeHint().height());
    m_activationSelect->layout()->setGeometry(m_activationSelect->rect());
    form->setInitialValues(
        {{QStringLiteral("activationKeys"), initialKeys},
         {QStringLiteral("mouseButton"),
          initial.mouseButton.isEmpty() ? QVariant() : QVariant(initial.mouseButton)}});
    form->resetFields();

    connect(m_activationSelect, &adqt::widgets::AdSelect::currentValuesChanged, this,
            [this](const QVariantList&) { syncModalValidation(); });
    connect(m_mouseButtonSelect, &adqt::widgets::AdSelect::currentValueChanged, this,
            [this](const QVariant&) { syncModalValidation(); });
    connect(
        modal, &adqt::widgets::AdModal::closeRequested, modal,
        [this, modal](adqt::widgets::AdModal::CloseReason reason) {
            if (reason == adqt::widgets::AdModal::CloseReason::OkAction &&
                (m_mouseButtonSelect->currentValue() == QStringLiteral("none") ||
                 !m_activationSelect->currentValues().isEmpty()) &&
                m_mouseButtonSelect->currentValue().isValid() &&
                m_runtimeSession.globalMouseCombinationAvailable(m_action, modalCombination())) {
                modal->accept();
            } else if (reason != adqt::widgets::AdModal::CloseReason::OkAction) {
                modal->reject();
            }
        });
    connect(modal, &adqt::widgets::AdModal::accepted, this, [this]() {
        if (m_runtimeSession.applyGlobalMouseCombination(m_action, modalCombination())) {
            m_activationController->notifyCommitted();
            m_mouseButtonController->notifyCommitted();
        }
    });
    connect(modal, &adqt::widgets::AdModal::finished, this,
            [this, modal](adqt::widgets::AdModal::DialogCode) {
                modal->deleteLater();
                m_modal = nullptr;
                m_activationField = nullptr;
                m_mouseButtonField = nullptr;
                m_mouseButtonController = nullptr;
                m_validationLabel = nullptr;
                m_activationSelect = nullptr;
                m_mouseButtonSelect = nullptr;
            });
    syncModalValidation();
    modal->open();
    syncModalValidation();
}

void GlobalMouseRow::syncButton() {
    if (m_button == nullptr) {
        return;
    }
    const bool unset = m_combination.isUnset();
    QStringList keys;
    for (const QString& key : m_combination.sortedActivationKeys()) {
        keys.push_back(mouse::modifierLabel(key));
    }
    m_button->setText(unset ? mouse::bindingLabel({})
                            : tr("%1 + %2").arg(keys.join(QStringLiteral(" + ")),
                                                mouseButtonLabel(m_combination.mouseButton)));
    m_button->setRegistrationStatus(
        unset ? snow_shot::presentation::GlobalShortcutStatus::Unset
        : m_runtimeSession.globalMousePermissionState().status ==
                snow_shot::presentation::GlobalMousePermissionState::Status::Ready
            ? snow_shot::presentation::GlobalShortcutStatus::Registered
            : snow_shot::presentation::GlobalShortcutStatus::Failed);
}

void GlobalMouseRow::syncModalText() {
    if (m_modal == nullptr || m_activationSelect == nullptr || m_mouseButtonSelect == nullptr) {
        return;
    }
    m_modal->setWindowTitle(tr("Mouse configuration for \"%1\"").arg(m_title));
    m_modal->setAcceptText(tr("OK"));
    m_modal->setRejectText(tr("Cancel"));
    if (m_activationField != nullptr) {
        m_activationField->setLabel(tr("Activation keys"));
        m_activationSelect->setAccessibleName(m_activationField->label());
    }
    if (m_mouseButtonField != nullptr) {
        m_mouseButtonField->setLabel(tr("Mouse button"));
        m_mouseButtonSelect->setAccessibleName(m_mouseButtonField->label());
    }
    const QVariantList activation = m_activationSelect->currentValues();
    const QVariant mouseButton = m_mouseButtonSelect->currentValue();
    m_activationController->synchronize([this, &activation] {
        QList<adqt::widgets::AdSelect::Option> options;
        for (const QString& key : snow_shot::globalMouseActivationKeys())
            options.append(option(key, mouse::modifierLabel(key)));
        m_activationSelect->setOptions(options);
        m_activationSelect->setCurrentValues(activation);
    });
    m_mouseButtonController->synchronize([this, &mouseButton] {
        QList<adqt::widgets::AdSelect::Option> options;
        for (const auto& choice : mouseChoices)
            options.append(option(choice.id, mouse::operationLabel(choice.binding)));
        m_mouseButtonSelect->setOptions(options);
        if (mouseButton.isValid()) {
            m_mouseButtonSelect->setCurrentValue(mouseButton);
        }
    });
    // Reserve the longest translated validation message before the modal is shown.
    const QFontMetrics metrics(m_validationLabel->font());
    const int messageWidth = CONFIGURATION_MODAL_WIDTH - 2 * m_colorScheme.metricAlias.paddingLG;
    int messageHeight = 0;
    for (const QString& message :
         {tr("Select at least one activation key"), tr("Select a mouse button"),
          tr("This mouse combination is already assigned to another action. Choose a "
             "different combination")}) {
        messageHeight = qMax(
            messageHeight,
            metrics.boundingRect(QRect(0, 0, messageWidth, 0), Qt::TextWordWrap, message).height());
    }
    m_mouseButtonController->reserveFeedbackHeight(messageHeight);
    syncModalValidation();
}

void GlobalMouseRow::syncModalValidation() {
    if (m_modal == nullptr || m_validationLabel == nullptr || m_activationSelect == nullptr ||
        m_mouseButtonSelect == nullptr) {
        return;
    }
    const bool hasKeys = !m_activationSelect->currentValues().isEmpty();
    const bool hasMouseButton = m_mouseButtonSelect->currentValue().isValid();
    const bool none = m_mouseButtonSelect->currentValue() == QStringLiteral("none");
    m_activationSelect->setEnabled(!none);
    const bool available =
        (none || hasKeys) && hasMouseButton &&
        m_runtimeSession.globalMouseCombinationAvailable(m_action, modalCombination());
    const QString message =
        available  ? QString()
        : !hasKeys ? tr("Select at least one activation key")
        : !hasMouseButton
            ? tr("Select a mouse button")
            : tr("This mouse combination is already assigned to another action. Choose a "
                 "different combination");
    m_mouseButtonController->setFeedback(message.isEmpty() ? QStringList() : QStringList{message});
    if (m_modal->acceptButton() != nullptr) {
        m_modal->acceptButton()->setEnabled(available);
    }
    applyTheme(m_colorScheme);
}

settings::SettingsGlobalMouseCombination GlobalMouseRow::modalCombination() const {
    if (m_mouseButtonSelect != nullptr &&
        m_mouseButtonSelect->currentValue() == QStringLiteral("none")) {
        return {};
    }
    QStringList keys;
    if (m_activationSelect != nullptr) {
        for (const QVariant& key : m_activationSelect->currentValues()) {
            keys.push_back(key.toString());
        }
    }
    keys.sort();
    return {keys, m_mouseButtonSelect != nullptr ? m_mouseButtonSelect->currentValue().toString()
                                                 : QString()};
}
