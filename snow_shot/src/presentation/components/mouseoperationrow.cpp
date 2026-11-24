// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/components/mouseoperationrow.h"
#include "snow_shot/presentation/components/formfields.h"
#include "snow_shot/presentation/components/icons/snowshoticons.h"
#include "snow_shot/presentation/components/shortcutconfigurationbutton.h"
#include "snow_shot/presentation/settings/settingsruntimesession.h"
#include "snow_shot/presentation/styles/mainwindowcomponenttoken.h"
#include "widgets/modal.h"
#include <QEvent>
#include <QSignalBlocker>
namespace mouse = snow_shot::mouse;
namespace fields = snow_shot::presentation::components::form_fields;
using snow_shot::presentation::settings::SettingsRuntimeSession;
using snow_shot::presentation::styles::ThemeColorScheme;

MouseOperationRow::MouseOperationRow(mouse::Action action, SettingsRuntimeSession& session,
                                     const ThemeColorScheme& scheme, QWidget* parent)
    : ActionRow({.title = mouse::title(action),
                 .iconRef = {},
                 .rowState = {},
                 .compactPresentation = true},
                scheme.metricAlias,
                snow_shot::presentation::styles::buildMainWindowComponentMetricToken(scheme),
                scheme, parent),
      m_action(action), m_session(session) {
    setFocusPolicy(Qt::StrongFocus);
    m_button = createConfigurationButton(
        scheme.metricAlias, snow_shot::presentation::icons::custom::outlined::WheelMouse());
    connect(this, &QAbstractButton::clicked, this, &MouseOperationRow::openDialog);
    connect(m_button, &QAbstractButton::clicked, this, &MouseOperationRow::openDialog);
    refresh();
}
void MouseOperationRow::refresh() {
    const auto name = mouse::title(m_action);
    const auto binding = m_session.mouseBindings().get(m_action);
    setRowTitle(name);
    m_button->setText(mouse::bindingLabel(binding));
    m_button->setToolTip(m_button->text());
    m_button->setAccessibleName(name + QStringLiteral(" — ") + m_button->text());
    m_button->setRegistrationStatus(
        binding.kind == mouse::Kind::None
            ? snow_shot::presentation::GlobalShortcutStatus::Unset
            : snow_shot::presentation::GlobalShortcutStatus::Registered);
    m_button->setAccentRole(adqt::widgets::AdButton::AccentRole::Neutral);
    if (m_dialogRefresh)
        m_dialogRefresh();
}
void MouseOperationRow::changeEvent(QEvent* event) {
    ActionRow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_button)
        refresh();
}
void MouseOperationRow::applyTheme(const ThemeColorScheme& scheme) {
    ActionRow::applyTheme(scheme);
    if (m_button)
        m_button->setTheme(scheme);
}
void MouseOperationRow::openDialog() {
    if (m_dialogRefresh)
        return;
    using namespace adqt::widgets;
    auto* modal = new AdModal(this);
    modal->setObjectName(QStringLiteral("mouseOperationDialog"));
    modal->setOwnerWindow(window());
    modal->setCentered(true);
    modal->setPreferredWidth(520);
    modal->setCloseOnMaskClick(false);
    modal->setClosePolicy(AdModal::ClosePolicy::Manual);
    modal->setStandardButtons(AdModal::StandardButton::Ok | AdModal::StandardButton::Cancel);
    auto* form = new AdForm;
    fields::configureForm(form);
    fields::Options options;
    options.form = form;
    options.commitPolicy = fields::CommitPolicy::Explicit;
    options.popupInModal = true;
    fields::Metadata modMetadata;
    modMetadata.id = QStringLiteral("modifiers");
    modMetadata.label = {"MouseOperationRow",
                         QT_TRANSLATE_NOOP("MouseOperationRow", "Keyboard modifiers")};
    fields::Metadata opMetadata;
    opMetadata.id = QStringLiteral("operation");
    opMetadata.label = {"MouseOperationRow",
                        QT_TRANSLATE_NOOP("MouseOperationRow", "Mouse operation")};
    const auto mods = fields::select(modMetadata, {}, options);
    const auto op = fields::select(opMetadata, {}, options);
    mods.editor->setMode(AdSelect::Mode::Multiple);
    mods.editor->setObjectName(QStringLiteral("mouseOperationModifiers"));
    op.editor->setObjectName(QStringLiteral("mouseOperationChoice"));
    const auto choices = mouse::choices(mouse::definition(m_action).kind);
    const auto draft = [choices, op, mods] {
        auto binding = choices.value(op.editor->currentValue().toInt());
        for (const auto& value : mods.editor->currentValues())
            binding.modifiers |= Qt::KeyboardModifier(value.toInt());
        return binding;
    };
    const auto validate = [this, modal, op, draft] {
        const auto b = draft();
        QString message;
        if (!mouse::allowed(mouse::definition(m_action), b)) {
            message = b.button == Qt::RightButton
                          ? tr("Unmodified right clicks are reserved for Go back one step.")
                          : tr("The unmodified left single-click is reserved for selection.");
        } else if (auto owner = m_session.mouseBindings().owner(m_action, b)) {
            message = tr("This gesture is assigned to %1.").arg(mouse::title(*owner));
        }
        op.field->setFeedback(message.isEmpty() ? QStringList{} : QStringList{message});
        if (modal->acceptButton())
            modal->acceptButton()->setEnabled(message.isEmpty());
        return message.isEmpty();
    };
    m_dialogRefresh = [=, this] {
        modal->setWindowTitle(mouse::title(m_action));
        const auto selectedMods = mods.editor->currentValues();
        const auto selectedOp = op.editor->currentValue();
        QList<AdSelect::Option> modOptions, opOptions;
#ifdef Q_OS_MACOS
        constexpr Qt::KeyboardModifier flags[]{Qt::ControlModifier, Qt::MetaModifier,
                                               Qt::AltModifier, Qt::ShiftModifier};
#else
        constexpr Qt::KeyboardModifier flags[]{Qt::MetaModifier, Qt::ControlModifier,
                                               Qt::AltModifier, Qt::ShiftModifier};
#endif
        for (auto f : flags) {
            AdSelect::Option item;
            item.value = int(f);
            item.label = mouse::modifierLabel(f);
            modOptions.append(item);
        }
        for (int i = 0; i < choices.size(); ++i) {
            AdSelect::Option item;
            item.value = i;
            item.label = mouse::operationLabel(choices[i]);
            opOptions.append(item);
        }
        {
            const QSignalBlocker guardMods(mods.editor), guardOp(op.editor);
            mods.editor->setOptions(modOptions);
            mods.editor->setCurrentValues(selectedMods);
            op.editor->setOptions(opOptions);
            op.editor->setCurrentValue(selectedOp);
        }
        mods.item()->setLabel(modMetadata.label.translated());
        op.item()->setLabel(opMetadata.label.translated());
        mods.editor->setAccessibleName(mods.item()->label());
        op.editor->setAccessibleName(op.item()->label());
        validate();
    };
    modal->setContentWidget(form);
    m_dialogRefresh();
    auto binding = m_session.mouseBindings().get(m_action);
    QVariantList initial;
    for (auto flag : {Qt::MetaModifier, Qt::ControlModifier, Qt::AltModifier, Qt::ShiftModifier})
        if (binding.modifiers.testFlag(flag))
            initial.append(int(flag));
    binding.modifiers = {};
    form->setInitialValues(
        {{modMetadata.id, initial}, {opMetadata.id, int(choices.indexOf(binding))}});
    form->resetFields();
    connect(mods.editor, &AdSelect::currentValuesChanged, modal, [validate] { validate(); });
    connect(op.editor, &AdSelect::currentValueChanged, modal, [validate] { validate(); });
    connect(modal, &AdModal::closeRequested, this, [=, this](AdModal::CloseReason reason) {
        if (reason != AdModal::CloseReason::OkAction) {
            modal->reject();
            return;
        }
        if (!validate())
            return;
        const auto* field =
            m_session.registry().fieldForConfigurationKey(mouse::definition(m_action).key());
        if (field && m_session.submitDraft(field->id, mouse::encode(draft())))
            modal->accept();
        else if (validate())
            op.field->setFeedback({tr("The mouse operation could not be saved.")});
    });
    connect(modal, &AdModal::finished, this, [this, modal] {
        m_dialogRefresh = {};
        modal->deleteLater();
        refresh();
    });
    modal->open();
    validate();
}
