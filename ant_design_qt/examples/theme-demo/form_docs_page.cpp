#include "form_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaType>
#include <QPushButton>
#include <QVBoxLayout>

#include "antd_icons.h"

using adqt::widgets::AdButton;
using adqt::widgets::AdForm;
using adqt::widgets::AdFormItem;
using adqt::widgets::AdFormList;
using adqt::widgets::AdInputNumber;
using adqt::widgets::AdLineEdit;
using adqt::widgets::AdPasswordEdit;
using adqt::widgets::AdSelect;
using adqt::widgets::AdSwitch;
using adqt::widgets::AdTextEdit;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

AdLineEdit* makeLineEdit(const QString& placeholder, int width = 280) {
  auto* input = new AdLineEdit();
  input->setPlaceholderText(placeholder);
  input->setFixedWidth(width);
  return input;
}

AdButton* makePrimaryButton(const QString& text) {
  auto* button = new AdButton(text);
  button->setButtonStyle(AdButton::ButtonStyle::Solid);
  button->setAccentRole(AdButton::AccentRole::Primary);
  return button;
}

QWidget* makeButtonRow(std::initializer_list<QWidget*> widgets) {
  auto* row = new QWidget();
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  for (QWidget* widget : widgets) {
    layout->addWidget(widget);
  }
  layout->addStretch();
  return row;
}

AdSelect::Option selectOption(const QString& value, const QString& label) {
  AdSelect::Option option;
  option.value = value;
  option.label = label;
  return option;
}

QString variantText(const QVariant& value) {
  if (!value.isValid() || value.isNull()) {
    return QStringLiteral("null");
  }

  const int type = value.userType();
  if (type == QMetaType::QVariantMap) {
    QStringList parts;
    const QVariantMap map = value.toMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
      parts.append(QStringLiteral("%1: %2").arg(it.key(), variantText(it.value())));
    }
    return QStringLiteral("{%1}").arg(parts.join(QStringLiteral(", ")));
  }
  if (type == QMetaType::QVariantList || type == QMetaType::QStringList) {
    QStringList parts;
    QVariantList list;
    if (type == QMetaType::QStringList) {
      const QStringList strings = value.toStringList();
      list.reserve(strings.size());
      for (const QString& item : strings) {
        list.append(item);
      }
    } else {
      list = value.toList();
    }
    for (const QVariant& item : list) {
      parts.append(variantText(item));
    }
    return QStringLiteral("[%1]").arg(parts.join(QStringLiteral(", ")));
  }
  if (type == QMetaType::Bool) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  return value.toString();
}

QString valueText(const QVariantMap& values) {
  QStringList parts;
  for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
    parts.append(QStringLiteral("%1=%2").arg(it.key(), variantText(it.value())));
  }
  return parts.join(QStringLiteral(", "));
}

}  // namespace

FormDocsPage::FormDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Form");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "High-performance form layout, field status, validation, and value collection for data "
      "entry.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic usage", "Field registration, submission, and reset.", buildBasicDemo());
  addSection(root, "Form layouts", "Horizontal, vertical, and inline Qt widget layouts.",
             buildLayoutDemo());
  addSection(root, "Required mark and colon",
             "Required markers, optional text, label alignment, and wrapping.",
             buildRequiredMarkDemo());
  addSection(root, "Validation status", "Manual status, messages, and custom feedback icons.",
             buildValidateStatusDemo());
  addSection(root, "Dynamic validation",
             "Field validators, debounced validation, and control value normalization.",
             buildDynamicValidationDemo());
  addSection(root, "Field paths and dependencies",
             "Nested field keys, dependency validation, and scroll-to-field behavior.",
             buildFieldPathDependencyDemo());
  addSection(root, "Dynamic list fields", "List editors backed by QVariantList values.",
             buildFormListDemo());
  addSection(root, "Size, variant, disabled", "Form-level propagation to compatible Qt controls.",
             buildControlPropagationDemo());

  root->addStretch();
}

const QVector<QWidget*>& FormDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& FormDocsPage::sectionTitles() const { return titles_; }

void FormDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
                              QWidget* content) {
  auto* panel = new QFrame();
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  auto* titleLabel = new QLabel(title);
  QFont titleFont = titleLabel->font();
  titleFont.setBold(true);
  titleFont.setPointSize(titleFont.pointSize() + 1);
  titleLabel->setFont(titleFont);

  auto* descLabel = new QLabel(description);
  descLabel->setWordWrap(true);

  layout->addWidget(titleLabel);
  layout->addWidget(descLabel);
  layout->addWidget(content);

  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* FormDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* form = new AdForm();
  form->setLabelColumnWidth(96);
  form->setRequiredMessageTemplate("Please enter ${label}.");

  auto* username = makeLineEdit("Username");
  username->setPrefixIconRef(outlined_icons::User());
  auto* userItem = form->addField("Username", username, "username");
  userItem->setRequired(true);
  userItem->setHasFeedback(true);

  auto* password = new AdPasswordEdit();
  password->setPlaceholderText("Password");
  password->setPrefixIconRef(outlined_icons::Lock());
  password->setFixedWidth(280);
  auto* passwordItem = form->addField("Password", password, "password");
  passwordItem->setRequired(true);
  passwordItem->setHasFeedback(true);

  auto* remember = new QCheckBox("Remember me");
  form->addField(QString(), remember, "remember");

  auto* submit = makePrimaryButton("Submit");
  auto* reset = new AdButton("Reset");
  form->addField(QString(), makeButtonRow({submit, reset}));

  auto* output = makeHintLabel("Submit validates fields and emits collected values.");
  connect(submit, &QAbstractButton::clicked, form, [form]() { form->submit(); });
  connect(reset, &QAbstractButton::clicked, form, [form, output]() {
    form->resetValidation();
    form->setValues({{"username", QString()}, {"password", QString()}, {"remember", false}});
    output->setText("Form reset.");
  });
  connect(form, &AdForm::submitSucceeded, output, [output](const QVariantMap& values) {
    output->setText(QStringLiteral("submitSucceeded: %1").arg(valueText(values)));
  });
  connect(form, &AdForm::submitFailed, output, [output](const QVector<AdFormItem*>& items) {
    QStringList fields;
    for (AdFormItem* item : items) {
      if (item) {
        fields.append(item->fieldKey());
      }
    }
    output->setText(QStringLiteral("submitFailed: %1").arg(fields.join(QStringLiteral(", "))));
  });

  layout->addWidget(form, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* FormDocsPage::buildLayoutDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(18);

  auto* horizontalTitle = new QLabel("Horizontal");
  QFont heading = horizontalTitle->font();
  heading.setBold(true);
  horizontalTitle->setFont(heading);
  layout->addWidget(horizontalTitle);

  auto* horizontal = new AdForm();
  horizontal->setLabelColumnWidth(96);
  horizontal->addField("Name", makeLineEdit("Horizontal layout"));
  horizontal->addField("Role", makeLineEdit("Designer"));
  layout->addWidget(horizontal, 0, Qt::AlignLeft);

  auto* verticalTitle = new QLabel("Vertical");
  verticalTitle->setFont(heading);
  layout->addWidget(verticalTitle);

  auto* vertical = new AdForm();
  vertical->setFormLayout(AdForm::FormLayout::Vertical);
  vertical->addField("Project", makeLineEdit("Snow Shot", 360));
  vertical->addField("Description", makeLineEdit("Short description", 360));
  layout->addWidget(vertical, 0, Qt::AlignLeft);

  auto* inlineTitle = new QLabel("Inline");
  inlineTitle->setFont(heading);
  layout->addWidget(inlineTitle);

  auto* inlineForm = new AdForm();
  inlineForm->setFormLayout(AdForm::FormLayout::Inline);
  inlineForm->addField("Username", makeLineEdit("Username", 180));
  auto* inlinePassword = new AdPasswordEdit();
  inlinePassword->setPlaceholderText("Password");
  inlinePassword->setFixedWidth(180);
  inlineForm->addField("Password", inlinePassword);
  inlineForm->addField(QString(), makePrimaryButton("Log in"));
  layout->addWidget(inlineForm);

  return box;
}

QWidget* FormDocsPage::buildRequiredMarkDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(18);

  auto* optional = new AdForm();
  optional->setRequiredMark(AdForm::RequiredMark::Optional);
  optional->setLabelColumnWidth(118);
  auto* requiredItem =
      optional->addField("Required", makeLineEdit("Required mark hidden in optional mode"));
  requiredItem->setRequired(true);
  optional->addField("Optional", makeLineEdit("Shows optional copy"));
  layout->addWidget(optional, 0, Qt::AlignLeft);

  auto* noColon = new AdForm();
  noColon->setColon(false);
  noColon->setRequiredMark(AdForm::RequiredMark::Hidden);
  noColon->setLabelAlign(AdForm::LabelAlign::Left);
  noColon->setLabelColumnWidth(118);
  auto* item = noColon->addField("No colon", makeLineEdit("Left label alignment"));
  item->setRequired(true);
  noColon->addField("Long wrapped label", makeLineEdit("labelWrap=true"));
  noColon->setLabelWrap(true);
  layout->addWidget(noColon, 0, Qt::AlignLeft);

  return box;
}

QWidget* FormDocsPage::buildValidateStatusDemo() {
  auto* form = new AdForm();
  form->setLabelColumnWidth(112);

  auto* success = form->addField("Success", makeLineEdit("Success"), "success");
  success->setValidateStatus(AdFormItem::ValidateStatus::Success);
  success->setHelpText("The field has been validated.");
  success->setHasFeedback(true);

  auto* warning = form->addField("Warning", makeLineEdit("Warning"), "warning");
  warning->setWarningMessages({"This value should be reviewed."});
  warning->setHasFeedback(true);

  auto* error = form->addField("Error", makeLineEdit("Error"), "error");
  error->setErrorMessages({"This field has an error."});
  error->setHasFeedback(true);

  auto* validating = form->addField("Validating", makeLineEdit("Validating"), "validating");
  validating->setValidateStatus(AdFormItem::ValidateStatus::Validating);
  validating->setHelpText("Checking value...");
  validating->setHasFeedback(true);

  return form;
}

QWidget* FormDocsPage::buildDynamicValidationDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* form = new AdForm();
  form->setLabelColumnWidth(96);

  auto* username = makeLineEdit("At least 4 characters");
  auto* usernameItem = form->addField("Username", username, "username");
  usernameItem->setRequired(true);
  usernameItem->setHasFeedback(true);
  usernameItem->setValidator([](const QVariant& value, QWidget*) {
    AdFormItem::ValidationResult result;
    const QString text = value.toString().trimmed();
    if (text.size() < 4) {
      result.status = AdFormItem::ValidateStatus::Error;
      result.errors = {"Username must be at least 4 characters."};
    }
    return result;
  });

  auto* email = makeLineEdit("name@example.com");
  auto* emailItem = form->addField("Email", email, "email");
  emailItem->setRequired(true);
  emailItem->setHasFeedback(true);
  emailItem->setValidator([](const QVariant& value, QWidget*) {
    AdFormItem::ValidationResult result;
    const QString text = value.toString().trimmed();
    if (!text.contains(QLatin1Char('@'))) {
      result.status = AdFormItem::ValidateStatus::Error;
      result.errors = {"Email must contain @."};
    }
    return result;
  });

  auto* slug = makeLineEdit("Trimmed to lowercase", 320);
  auto* slugItem = form->addField("Slug", slug, "slug");
  slugItem->setInitialValue("new-user");
  slugItem->setControlValueProperty("text");
  slugItem->setTooltipText("Uses item initialValue and a Qt value normalizer.");
  slugItem->setValueNormalizer([](const QVariant& value, const QVariant&, AdFormItem*) {
    return value.toString().trimmed().toLower();
  });

  auto* validate = makePrimaryButton("Validate");
  auto* clear = new AdButton("Clear");
  form->addField(QString(), makeButtonRow({validate, clear}));

  auto* output = makeHintLabel("Validation runs on change and on explicit validate.");
  connect(validate, &QAbstractButton::clicked, form, [form]() { form->submit(); });
  connect(clear, &QAbstractButton::clicked, form, [form, output]() {
    form->setValues(
        {{"username", QString()}, {"email", QString()}, {"slug", QStringLiteral(" New User ")}});
    form->resetValidation();
    output->setText("Validation cleared.");
  });
  connect(form, &AdForm::submitSucceeded, output, [output](const QVariantMap& values) {
    output->setText(QStringLiteral("Valid: %1").arg(valueText(values)));
  });
  connect(form, &AdForm::submitFailed, output,
          [output](const QVector<AdFormItem*>&) { output->setText("Validation failed."); });

  layout->addWidget(form, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* FormDocsPage::buildFieldPathDependencyDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* controls = new QWidget();
  auto* controlsLayout = new QHBoxLayout(controls);
  controlsLayout->setContentsMargins(0, 0, 0, 0);
  controlsLayout->setSpacing(8);
  auto* scrollToFirst = new QCheckBox("scrollToFirstError");
  scrollToFirst->setChecked(true);
  controlsLayout->addWidget(scrollToFirst);
  controlsLayout->addStretch();

  auto* form = new AdForm();
  form->setLabelColumnWidth(136);
  form->setScrollToFirstError(true);
  const QVariantMap initialUser{{QStringLiteral("name"), QStringLiteral("Ada Lovelace")},
                                {QStringLiteral("email"), QStringLiteral("ada@example.com")}};
  const QVariantMap initialAccount{{QStringLiteral("password"), QStringLiteral("secret123")},
                                   {QStringLiteral("confirm"), QStringLiteral("secret123")}};
  form->setInitialValues(
      {{QStringLiteral("user"), initialUser}, {QStringLiteral("account"), initialAccount}});

  auto* name = makeLineEdit("Stored at user.name", 320);
  auto* nameItem = form->addField("Name", name, "user.name");
  nameItem->setRequired(true);
  nameItem->setHasFeedback(true);
  nameItem->setTooltipText("This field is collected at the user.name field path.");

  auto* email = makeLineEdit("Debounced validation", 320);
  auto* emailItem = form->addField("Email", email, "user.email");
  emailItem->setRequired(true);
  emailItem->setHasFeedback(true);
  emailItem->setValidateDebounceMs(350);
  emailItem->setTooltipText("Validation is debounced by 350 ms.");
  emailItem->setValidator([](const QVariant& value, QWidget*) {
    AdFormItem::ValidationResult result;
    const QString text = value.toString().trimmed();
    if (!text.contains(QLatin1Char('@'))) {
      result.status = AdFormItem::ValidateStatus::Error;
      result.errors = {"Email must contain @."};
    }
    return result;
  });
  emailItem->setFeedbackIconProvider([](AdFormItem::ValidateStatus status, const QStringList&,
                                        const QStringList&) -> adqt::icons::IconRef {
    if (status == AdFormItem::ValidateStatus::Success) {
      return outlined_icons::Smile();
    }
    return {};
  });

  auto* password = new AdPasswordEdit();
  password->setPlaceholderText("Password");
  password->setFixedWidth(320);
  auto* passwordItem = form->addField("Password", password, "account.password");
  passwordItem->setRequired(true);
  passwordItem->setHasFeedback(true);

  auto* confirm = new AdPasswordEdit();
  confirm->setPlaceholderText("Confirm password");
  confirm->setFixedWidth(320);
  auto* confirmItem = form->addField("Confirm", confirm, "account.confirm");
  confirmItem->setRequired(true);
  confirmItem->setHasFeedback(true);
  confirmItem->setDependencies({"account.password"});
  confirmItem->setFormValidator([](const QVariant& value, AdFormItem* item) {
    AdFormItem::ValidationResult result;
    const AdForm* formWidget = item ? item->formWidget() : nullptr;
    const QString password =
        formWidget ? formWidget->fieldValue("account.password").toString() : QString();
    if (!password.isEmpty() && value.toString() != password) {
      result.status = AdFormItem::ValidateStatus::Error;
      result.errors = {"The two passwords do not match."};
    }
    return result;
  });

  auto* submit = makePrimaryButton("Submit");
  auto* load = new AdButton("Load values");
  auto* reset = new AdButton("Reset fields");
  auto* validateUser = new AdButton("Validate user");
  auto* scrollEmail = new AdButton("Scroll to email");
  form->addField(QString(), makeButtonRow({submit, load, reset, validateUser, scrollEmail}));

  auto* output = makeHintLabel("Nested values and flatValues output will appear here.");
  connect(scrollToFirst, &QCheckBox::toggled, form, &AdForm::setScrollToFirstError);
  connect(submit, &QAbstractButton::clicked, form, [form]() { form->submit(); });
  connect(load, &QAbstractButton::clicked, form, [form, output]() {
    const QVariantMap user{{QStringLiteral("name"), QStringLiteral("Ada Lovelace")},
                           {QStringLiteral("email"), QStringLiteral("ada@invalid")}};
    const QVariantMap account{{QStringLiteral("password"), QStringLiteral("secret123")},
                              {QStringLiteral("confirm"), QStringLiteral("different")}};
    form->setValues({{QStringLiteral("user"), user}, {QStringLiteral("account"), account}});
    form->resetValidation();
    output->setText(QStringLiteral("Loaded values: %1").arg(valueText(form->values())));
  });
  connect(reset, &QAbstractButton::clicked, form, [form, output]() {
    form->resetFields();
    output->setText(
        QStringLiteral("resetFields: touched=%1, dirty(email)=%2, values: %3")
            .arg(
                form->isFieldsTouched() ? QStringLiteral("true") : QStringLiteral("false"),
                form->isFieldDirty("user.email") ? QStringLiteral("true") : QStringLiteral("false"),
                valueText(form->values())));
  });
  connect(validateUser, &QAbstractButton::clicked, form, [form, output]() {
    const QVector<AdFormItem*> invalid = form->validateFields({"user.name", "user.email"});
    output->setText(
        QStringLiteral("validateFields(user.*): invalid=%1, touched=%2")
            .arg(invalid.size())
            .arg(form->isFieldsTouched() ? QStringLiteral("true") : QStringLiteral("false")));
  });
  connect(scrollEmail, &QAbstractButton::clicked, form, [form]() {
    form->scrollToField(QStringList{QStringLiteral("user"), QStringLiteral("email")});
  });
  connect(form, &AdForm::submitSucceeded, output, [form, output](const QVariantMap& values) {
    output->setText(QStringLiteral("values: %1\nflatValues: %2")
                        .arg(valueText(values), valueText(form->flatValues())));
  });
  connect(form, &AdForm::submitFailed, output, [output](const QVector<AdFormItem*>& items) {
    QStringList fields;
    for (AdFormItem* item : items) {
      if (item) {
        fields.append(item->fieldKey());
      }
    }
    output->setText(QStringLiteral("submitFailed: %1").arg(fields.join(QStringLiteral(", "))));
  });

  layout->addWidget(controls);
  layout->addWidget(form, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* FormDocsPage::buildFormListDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* form = new AdForm();
  form->setLabelColumnWidth(136);

  const QVariantList initialPassengers{QStringLiteral("Ada Lovelace"),
                                       QStringLiteral("Grace Hopper")};
  form->setInitialValues({{QStringLiteral("passengers"), initialPassengers}});

  auto* passengers = new AdFormList();
  passengers->setMinRows(1);
  passengers->setMaxRows(5);
  passengers->setAddButtonText("Add passenger");
  passengers->setRowFactory([](int index, QWidget* parent) {
    auto* input = new AdLineEdit(parent);
    input->setPlaceholderText(QStringLiteral("Passenger %1").arg(index + 1));
    input->setMinimumWidth(260);
    return input;
  });

  auto* passengersItem = form->addField("Passengers", passengers, "passengers");
  passengersItem->setRequired(true);
  passengersItem->setTooltipText("Collected as a QVariantList at passengers.");
  passengersItem->setValidator([](const QVariant& value, QWidget*) {
    AdFormItem::ValidationResult result;
    const QVariantList rows = value.toList();
    for (int i = 0; i < rows.size(); ++i) {
      if (rows.at(i).toString().trimmed().isEmpty()) {
        result.status = AdFormItem::ValidateStatus::Error;
        result.errors = {QStringLiteral("Passenger %1 is empty.").arg(i + 1)};
        break;
      }
    }
    return result;
  });

  auto* submit = makePrimaryButton("Submit");
  auto* move = new AdButton("Move first down");
  auto* reset = new AdButton("Reset list");
  form->addField(QString(), makeButtonRow({submit, move, reset}));

  auto* output = makeHintLabel("List values will submit as an array.");
  connect(submit, &QAbstractButton::clicked, form, [form]() { form->submit(); });
  connect(move, &QAbstractButton::clicked, passengers, [passengers]() {
    if (passengers->count() > 1) {
      passengers->moveRow(0, 1);
    }
  });
  connect(reset, &QAbstractButton::clicked, form, [form, output]() {
    form->resetField("passengers");
    output->setText(QStringLiteral("resetField(passengers): %1").arg(valueText(form->values())));
  });
  connect(form, &AdForm::submitSucceeded, output, [output](const QVariantMap& values) {
    output->setText(QStringLiteral("submitSucceeded: %1").arg(valueText(values)));
  });
  connect(form, &AdForm::submitFailed, output, [output](const QVector<AdFormItem*>& items) {
    output->setText(QStringLiteral("submitFailed: invalid items=%1").arg(items.size()));
  });
  connect(form, &AdForm::valuesChanged, output,
          [output](const QVariantMap& changed, const QVariantMap& all) {
            output->setText(
                QStringLiteral("changed: %1\nall: %2").arg(valueText(changed), valueText(all)));
          });

  layout->addWidget(form, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* FormDocsPage::buildControlPropagationDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* controls = new QHBoxLayout();
  auto* disabled = new QCheckBox("disabled");
  controls->addWidget(disabled);
  controls->addStretch();
  layout->addLayout(controls);

  auto* form = new AdForm();
  form->setLabelColumnWidth(112);
  form->setControlSize(AdForm::ControlSize::Large);
  form->setVariant(AdForm::Variant::Filled);

  auto* name = makeLineEdit("Large filled input", 320);
  form->addField("Input", name, "input");

  auto* select = new AdSelect();
  select->setOptions({selectOption("china", "China"), selectOption("usa", "U.S.A"),
                      selectOption("japan", "Japan")});
  select->setPlaceholder("Select a country");
  select->setFixedWidth(320);
  form->addField("Select", select, "country");

  auto* count = new AdInputNumber();
  count->setFixedWidth(180);
  form->addField("InputNumber", count, "count");

  auto* switchWidget = new AdSwitch();
  form->addField("Switch", switchWidget, "enabled");

  auto* notes = new AdTextEdit();
  notes->setPlaceholderText("TextArea follows status and disabled propagation");
  notes->setHeightMode(AdTextEdit::HeightMode::FixedRows);
  notes->setMinimumVisibleRows(3);
  notes->setMaximumVisibleRows(3);
  notes->setFixedWidth(420);
  form->addField("TextArea", notes, "notes");

  connect(disabled, &QCheckBox::toggled, form, &AdForm::setDisabled);

  layout->addWidget(form, 0, Qt::AlignLeft);
  layout->addWidget(
      makeHintLabel("The Form propagates size, variant, disabled, and validation status to "
                    "compatible controls."));
  return box;
}
