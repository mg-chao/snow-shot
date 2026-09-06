#include "input_number_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QDynamicPropertyChangeEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QVariantMap>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "antd_icons.h"
#include "theme/theme_manager.h"
#include "widgets/widgets.h"

using adqt::widgets::AdButton;
using adqt::widgets::AdComboBox;
using adqt::widgets::AdFieldGroup;
using adqt::widgets::AdInputNumber;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

AdComboBox::Option makeOption(const QString& value, const QString& label, bool disabled = false,
                              const QString& group = QString(), const QVariantMap& metadata = {}) {
  AdComboBox::Option option;
  option.value = value;
  option.label = label;
  option.disabled = disabled;
  option.group = group;
  option.metadata = metadata;
  return option;
}

QString valueToText(double value) {
  if (!std::isfinite(value)) {
    return QStringLiteral("null");
  }

  QString text = QString::number(value, 'f', 12);
  while (text.contains(QLatin1Char('.')) &&
         (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) {
    text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) {
      text.chop(1);
      break;
    }
  }

  if (text.isEmpty() || text == QStringLiteral("-0")) {
    return QStringLiteral("0");
  }
  return text;
}

QString valueToText(const QString& value) {
  if (value.trimmed().isEmpty()) {
    return QStringLiteral("null");
  }
  return QStringLiteral("\"%1\"").arg(value);
}

int heightForControlSize(AdInputNumber::ControlSize controlSize,
                         const adqt::theme::ThemeMapToken& map) {
  switch (controlSize) {
    case AdInputNumber::ControlSize::Large:
      return std::max(24, qRound(map.controlHeightLG));
    case AdInputNumber::ControlSize::Small:
      return std::max(20, qRound(map.controlHeightSM));
    case AdInputNumber::ControlSize::Medium:
    default:
      return std::max(22, qRound(map.controlHeight));
  }
}

int compactJoinOverlap(const adqt::theme::ThemeMapToken& map) {
  return std::max(1, qRound(map.lineWidth));
}

qreal radiusForControlSize(AdInputNumber::ControlSize controlSize,
                           const adqt::theme::ThemeMapToken& map) {
  switch (controlSize) {
    case AdInputNumber::ControlSize::Large:
      return std::max<qreal>(0.0, map.borderRadiusLG);
    case AdInputNumber::ControlSize::Small:
      return std::max<qreal>(0.0, map.borderRadiusSM);
    case AdInputNumber::ControlSize::Medium:
    default:
      return std::max<qreal>(0.0, map.borderRadius);
  }
}

QPainterPath roundedRectPath(const QRectF& rect, qreal leftRadius, qreal rightRadius) {
  const qreal width = std::max<qreal>(0.0, rect.width());
  const qreal height = std::max<qreal>(0.0, rect.height());
  const qreal maxRadius = std::min(width, height) / 2.0;
  const qreal clampedLeft = std::clamp(leftRadius, 0.0, maxRadius);
  const qreal clampedRight = std::clamp(rightRadius, 0.0, maxRadius);

  const qreal left = rect.left();
  const qreal top = rect.top();
  const qreal right = rect.right();
  const qreal bottom = rect.bottom();

  QPainterPath path;
  path.moveTo(left + clampedLeft, top);
  path.lineTo(right - clampedRight, top);
  if (clampedRight > 0.0) {
    path.arcTo(QRectF(right - clampedRight * 2.0, top, clampedRight * 2.0, clampedRight * 2.0),
               90.0, -90.0);
  }
  path.lineTo(right, bottom - clampedRight);
  if (clampedRight > 0.0) {
    path.arcTo(QRectF(right - clampedRight * 2.0, bottom - clampedRight * 2.0, clampedRight * 2.0,
                      clampedRight * 2.0),
               0.0, -90.0);
  }
  path.lineTo(left + clampedLeft, bottom);
  if (clampedLeft > 0.0) {
    path.arcTo(QRectF(left, bottom - clampedLeft * 2.0, clampedLeft * 2.0, clampedLeft * 2.0),
               270.0, -90.0);
  }
  path.lineTo(left, top + clampedLeft);
  if (clampedLeft > 0.0) {
    path.arcTo(QRectF(left, top, clampedLeft * 2.0, clampedLeft * 2.0), 180.0, -90.0);
  }
  path.closeSubpath();
  return path;
}

class DemoAddonLabel final : public QLabel {
 public:
  DemoAddonLabel(const QString& text, AdInputNumber::ControlSize controlSize,
                 bool joinedLeft = false, bool joinedRight = false, QWidget* parent = nullptr)
      : QLabel(text, parent),
        text_(text),
        controlSize_(controlSize),
        joinedLeft_(joinedLeft),
        joinedRight_(joinedRight) {
    setAlignment(Qt::AlignCenter);
    setContentsMargins(10, 0, 10, 0);
    setAutoFillBackground(false);
    QWidget::setProperty("joinedLeft", joinedLeft_);
    QWidget::setProperty("joinedRight", joinedRight_);
    applyTheme();
    connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged, this,
            [this]() { applyTheme(); });
  }

  void setIconRef(const adqt::icons::IconRef& token) {
    iconRef_ = token;
    applyTheme();
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF frameRect =
        QRectF(rect()).adjusted(borderWidth_ * 0.5 + 0.5, borderWidth_ * 0.5 + 0.5,
                                -borderWidth_ * 0.5 - 0.5, -borderWidth_ * 0.5 - 0.5);
    painter.setPen(QPen(borderColor_, borderWidth_, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(backgroundColor_);
    painter.drawPath(roundedRectPath(frameRect, leftRadius_, rightRadius_));
    QLabel::paintEvent(event);
  }

  bool event(QEvent* event) override {
    const bool handled = QLabel::event(event);
    if (!event) {
      return handled;
    }

    if (event->type() == QEvent::DynamicPropertyChange) {
      const auto* changeEvent = static_cast<QDynamicPropertyChangeEvent*>(event);
      bool geometryChanged = false;
      if (changeEvent->propertyName() == "joinedLeft") {
        const bool value = property("joinedLeft").toBool();
        if (joinedLeft_ != value) {
          joinedLeft_ = value;
          geometryChanged = true;
        }
      } else if (changeEvent->propertyName() == "joinedRight") {
        const bool value = property("joinedRight").toBool();
        if (joinedRight_ != value) {
          joinedRight_ = value;
          geometryChanged = true;
        }
      }
      if (geometryChanged && !applyingTheme_) {
        applyTheme();
      }
      return handled;
    }

    if (!applyingTheme_ &&
        (event->type() == QEvent::ParentChange || event->type() == QEvent::Polish)) {
      applyTheme();
    }
    return handled;
  }

 private:
  void applyTheme() {
    if (applyingTheme_) {
      return;
    }
    const QScopedValueRollback<bool> guard(applyingTheme_, true);
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(this);
    const int height = heightForControlSize(controlSize_, map);
    const int borderWidth = compactJoinOverlap(map);
    const int leftRadius = qRound(joinedLeft_ ? 0.0 : radiusForControlSize(controlSize_, map));
    const int rightRadius = qRound(joinedRight_ ? 0.0 : radiusForControlSize(controlSize_, map));
    const QColor borderColor = demo::themeColorOr(map.colorBorder, QColor("#d9d9d9"));
    const QColor background = demo::themeColorOr(map.colorBgContainerDisabled, QColor("#f5f5f5"));
    const QColor contentColor = demo::themeColorOr(
        map.colorTextSecondary, demo::themeColorOr(map.colorText, QColor("#595959")));

    borderWidth_ = borderWidth;
    leftRadius_ = leftRadius;
    rightRadius_ = rightRadius;
    borderColor_ = borderColor;
    backgroundColor_ = background;
    QPalette palette = this->palette();
    palette.setColor(QPalette::WindowText, contentColor);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, contentColor);
    setPalette(palette);
    setFixedHeight(height);
    update();

    if (adqt::icons::isValid(iconRef_)) {
      const adqt::icons::IconRef tinted =
          iconRef_.withColors(adqt::icons::IconColors::primary(contentColor));
      const int side = std::max(12, qRound(height * 0.45));
      setPixmap(adqt::icons::renderIconPixmap(tinted, {QSize(side, side), devicePixelRatioF()}));
      setText(QString());
      setFixedWidth(std::max(height, side + 20));
      return;
    }

    setPixmap(QPixmap());
    setText(text_);
    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
  }

  QString text_;
  adqt::icons::IconRef iconRef_;
  AdInputNumber::ControlSize controlSize_ = AdInputNumber::ControlSize::Medium;
  bool joinedLeft_ = false;
  bool joinedRight_ = false;
  bool applyingTheme_ = false;
  int borderWidth_ = 1;
  int leftRadius_ = 0;
  int rightRadius_ = 0;
  QColor borderColor_ = QColor("#d9d9d9");
  QColor backgroundColor_ = QColor("#f5f5f5");
};

QLabel* makeAddonLabel(const QString& text, AdInputNumber::ControlSize controlSize,
                       bool joinedLeft = false, bool joinedRight = false,
                       QWidget* parent = nullptr) {
  return new DemoAddonLabel(text, controlSize, joinedLeft, joinedRight, parent);
}

QLabel* makeAddonIcon(const adqt::icons::IconRef& token, AdInputNumber::ControlSize controlSize,
                      bool joinedLeft = false, bool joinedRight = false,
                      QWidget* parent = nullptr) {
  auto* label = new DemoAddonLabel(QString(), controlSize, joinedLeft, joinedRight, parent);
  label->setIconRef(token);
  return label;
}

class CurrencyInputNumberTextPolicy final : public adqt::widgets::AdInputNumberTextPolicy {
 public:
  explicit CurrencyInputNumberTextPolicy(QObject* parent = nullptr)
      : AdInputNumberTextPolicy(parent) {}

  QString formatText(const QString& canonicalText, bool, const QString&) const override {
    const QString text = canonicalText;
    if (text.trimmed().isEmpty()) {
      return QString();
    }
    QStringList parts = text.split('.');
    QString start = parts.value(0);
    const QString end = parts.value(1);
    start.replace(QRegularExpression(QStringLiteral("\\B(?=(\\d{3})+(?!\\d))")),
                  QStringLiteral(","));
    if (!end.isEmpty()) {
      return QStringLiteral("$ %1.%2").arg(start, end);
    }
    return QStringLiteral("$ %1").arg(start);
  }

  QString parseText(const QString& text) const override {
    QString clean = text;
    clean.remove(QRegularExpression(QStringLiteral("[\\$,\\s]")));
    return clean;
  }
};

class PercentInputNumberTextPolicy final : public adqt::widgets::AdInputNumberTextPolicy {
 public:
  explicit PercentInputNumberTextPolicy(QObject* parent = nullptr)
      : AdInputNumberTextPolicy(parent) {}

  QString formatText(const QString& canonicalText, bool, const QString&) const override {
    const QString text = canonicalText.trimmed();
    return text.isEmpty() ? QString() : QStringLiteral("%1%").arg(text);
  }

  QString parseText(const QString& text) const override {
    QString clean = text;
    clean.remove('%');
    return clean;
  }
};
}  // namespace

InputNumberDocsPage::InputNumberDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("InputNumber");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel("Enter a number within a certain range with mouse or keyboard.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Sizes", "Demo: size.tsx", buildSizeDemo());
  addSection(root, "Pre / Post tab", "Demo: addon.tsx", buildAddonDemo());
  addSection(root, "Disabled", "Demo: disabled.tsx", buildDisabledDemo());
  addSection(root, "High precision decimals", "Demo: digit.tsx", buildDigitDemo());
  addSection(root, "Formatter", "Demo: formatter.tsx", buildFormatterDemo());
  addSection(root, "Keyboard", "Demo: keyboard.tsx", buildKeyboardDemo());
  addSection(root, "Wheel", "Demo: change-on-wheel.tsx", buildWheelDemo());
  addSection(root, "Variants", "Demo: variant.tsx", buildVariantDemo());
  addSection(root, "Spinner", "Demo: spinner.tsx", buildSpinnerDemo());
  addSection(root, "Filled Debug", "Demo: filled-debug.tsx", buildFilledDebugDemo());
  addSection(root, "Out of range", "Demo: out-of-range.tsx", buildOutOfRangeDemo());
  addSection(root, "Prefix / Suffix", "Demo: presuffix.tsx", buildPreSuffixDemo());
  addSection(root, "Status", "Demo: status.tsx", buildStatusDemo());
  addSection(root, "Focus", "Demo: focus.tsx", buildFocusDemo());
  addSection(root, "Custom semantic dom styling", "Demo: style-class.tsx", buildStyleClassDemo());
  addSection(root, "Icon", "Demo: controls.tsx", buildControlsDemo());
  addSection(root, "_InternalPanelDoNotUseOrYouWillBeFired", "Demo: render-panel.tsx",
             buildRenderPanelDemo());
  addSection(root, "Override Component Style", "Demo: debug-token.tsx", buildDebugTokenDemo());

  root->addStretch();
}

const QVector<QWidget*>& InputNumberDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& InputNumberDocsPage::sectionTitles() const { return titles_; }

void InputNumberDocsPage::addSection(QVBoxLayout* root, const QString& title,
                                     const QString& description, QWidget* content) {
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

QWidget* InputNumberDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* input = new AdInputNumber();
  input->setMinimum(1);
  input->setMaximum(10);
  input->setValue(3);
  input->setFixedWidth(180);

  auto* output = makeHintLabel("changed: 3");
  connect(input, &AdInputNumber::valueChanged, output, [output](double value) {
    output->setText(QStringLiteral("changed: %1").arg(valueToText(value)));
  });

  layout->addWidget(input, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* InputNumberDocsPage::buildSizeDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* large = new AdInputNumber();
  large->setControlSize(AdInputNumber::ControlSize::Large);
  large->setMinimum(1);
  large->setMaximum(100000);
  large->setValue(3);
  large->setFixedWidth(170);

  auto* medium = new AdInputNumber();
  medium->setMinimum(1);
  medium->setMaximum(100000);
  medium->setValue(3);
  medium->setFixedWidth(170);

  auto* small = new AdInputNumber();
  small->setControlSize(AdInputNumber::ControlSize::Small);
  small->setMinimum(1);
  small->setMaximum(100000);
  small->setValue(3);
  small->setFixedWidth(170);

  row->addWidget(large);
  row->addWidget(medium);
  row->addWidget(small);
  row->addStretch();
  return box;
}

QWidget* InputNumberDocsPage::buildAddonDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  {
    auto* group = new AdFieldGroup();
    auto* before = makeAddonLabel("+", AdInputNumber::ControlSize::Medium);
    auto* input = new AdInputNumber();
    input->setValue(100);
    input->setFixedWidth(120);
    auto* after = makeAddonLabel("$", AdInputNumber::ControlSize::Medium);

    group->addControl(before);
    group->addControl(input);
    group->addControl(after);
    layout->addWidget(group, 0, Qt::AlignLeft);
  }

  {
    auto* group = new AdFieldGroup();
    auto* before = new AdComboBox();
    before->setFixedWidth(90);
    before->setOptions({makeOption("add", "+"), makeOption("minus", "-")});
    before->setCurrentValue("add");

    auto* input = new AdInputNumber();
    input->setValue(100);
    input->setFixedWidth(120);

    auto* after = new AdComboBox();
    after->setFixedWidth(110);
    after->setOptions({makeOption("usd", "$"), makeOption("eur", "EUR"), makeOption("gbp", "GBP"),
                       makeOption("cny", "CNY")});
    after->setCurrentValue("usd");

    group->addControl(before);
    group->addControl(input);
    group->addControl(after);
    layout->addWidget(group, 0, Qt::AlignLeft);
  }

  {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    auto* inputGroup = new AdFieldGroup();
    auto* input = new AdInputNumber();
    input->setValue(100);
    input->setFixedWidth(148);
    auto* inputAfter = makeAddonIcon(outlined_icons::Setting(), AdInputNumber::ControlSize::Medium);
    inputGroup->addControl(input);
    inputGroup->addControl(inputAfter);

    auto* disabledGroup = new AdFieldGroup();
    auto* disabledBefore = makeAddonLabel("+", AdInputNumber::ControlSize::Medium);
    auto* disabled = new AdInputNumber();
    disabled->setPrefixText("$");
    disabled->setValue(100);
    disabled->setEnabled(false);
    disabled->setFixedWidth(120);
    auto* disabledAfter =
        makeAddonIcon(outlined_icons::Setting(), AdInputNumber::ControlSize::Medium);
    disabledGroup->addControl(disabledBefore);
    disabledGroup->addControl(disabled);
    disabledGroup->addControl(disabledAfter);

    row->addWidget(inputGroup);
    row->addWidget(disabledGroup);
    row->addStretch();
    layout->addLayout(row);
  }

  return box;
}

QWidget* InputNumberDocsPage::buildDisabledDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* input = new AdInputNumber();
  input->setMinimum(1);
  input->setMaximum(10);
  input->setValue(3);
  input->setEnabled(false);
  input->setFixedWidth(180);

  auto* toggle = new AdButton("Toggle disabled");
  toggle->setButtonStyle(AdButton::ButtonStyle::Solid);
  toggle->setAccentRole(AdButton::AccentRole::Primary);
  toggle->setFixedWidth(150);

  connect(toggle, &QAbstractButton::clicked, input,
          [input]() { input->setEnabled(!input->isEnabled()); });

  layout->addWidget(input, 0, Qt::AlignLeft);
  layout->addWidget(toggle, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputNumberDocsPage::buildDigitDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* input = new AdInputNumber();
  input->setValueMode(AdInputNumber::ValueMode::ExactDecimal);
  input->setExactValue(QStringLiteral("1"));
  input->setExactRange(QStringLiteral("0"), QStringLiteral("10"));
  input->setExactSingleStep(QStringLiteral("0.00000000000001"));
  input->setFixedWidth(250);

  auto* output = makeHintLabel("onChange: \"1\"");
  connect(input, &AdInputNumber::exactValueChanged, output, [output](const QString& value) {
    output->setText(QStringLiteral("onChange: %1").arg(valueToText(value)));
  });

  layout->addWidget(input, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* InputNumberDocsPage::buildFormatterDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* currency = new AdInputNumber();
  currency->setValue(1000);
  currency->setFixedWidth(210);
  currency->setTextPolicy(new CurrencyInputNumberTextPolicy(currency));

  auto* percent = new AdInputNumber();
  percent->setValue(100);
  percent->setMinimum(0);
  percent->setMaximum(100);
  percent->setFixedWidth(180);
  percent->setTextPolicy(new PercentInputNumberTextPolicy(percent));

  row->addWidget(currency);
  row->addWidget(percent);
  row->addStretch();
  return box;
}

QWidget* InputNumberDocsPage::buildKeyboardDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* input = new AdInputNumber();
  input->setMinimum(1);
  input->setMaximum(10);
  input->setValue(3);
  input->setFixedWidth(180);

  auto* toggle = new QCheckBox("Toggle step keys");
  toggle->setChecked(true);
  connect(toggle, &QCheckBox::toggled, input, &AdInputNumber::setStepKeysEnabled);

  row->addWidget(input);
  row->addWidget(toggle);
  row->addStretch();
  return box;
}

QWidget* InputNumberDocsPage::buildWheelDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* input = new AdInputNumber();
  input->setMinimum(1);
  input->setMaximum(10);
  input->setValue(3);
  input->setWheelStepEnabled(true);
  input->setFixedWidth(180);

  auto* hint = makeHintLabel("Focus and use wheel to change. onStep: value=3");
  connect(
      input, &AdInputNumber::stepped, hint,
      [hint, input](int offset, AdInputNumber::StepType type, AdInputNumber::StepEmitter emitter) {
        const QString typeText =
            type == AdInputNumber::StepType::Up ? QStringLiteral("up") : QStringLiteral("down");
        QString emitterText = QStringLiteral("handler");
        if (emitter == AdInputNumber::StepEmitter::KeyDown) {
          emitterText = QStringLiteral("keydown");
        } else if (emitter == AdInputNumber::StepEmitter::Wheel) {
          emitterText = QStringLiteral("wheel");
        }
        const QString valueText =
            input->hasValue() ? valueToText(input->value()) : QStringLiteral("null");
        hint->setText(QStringLiteral("onStep: value=%1 offset=%2 type=%3 emitter=%4")
                          .arg(valueText)
                          .arg(offset)
                          .arg(typeText, emitterText));
      });

  layout->addWidget(input, 0, Qt::AlignLeft);
  layout->addWidget(hint);
  return box;
}

QWidget* InputNumberDocsPage::buildVariantDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto makeVariant = [](const QString& placeholder, AdInputNumber::Variant variant) {
    auto* input = new AdInputNumber();
    input->setPlaceholderText(placeholder);
    input->setVariant(variant);
    input->setFixedWidth(220);
    return input;
  };

  layout->addWidget(makeVariant("Outlined", AdInputNumber::Variant::Outlined), 0, Qt::AlignLeft);
  layout->addWidget(makeVariant("Filled", AdInputNumber::Variant::Filled), 0, Qt::AlignLeft);
  layout->addWidget(makeVariant("Borderless", AdInputNumber::Variant::Borderless), 0,
                    Qt::AlignLeft);
  layout->addWidget(makeVariant("Underlined", AdInputNumber::Variant::Underlined), 0,
                    Qt::AlignLeft);

  return box;
}

QWidget* InputNumberDocsPage::buildSpinnerDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* outlined = new AdInputNumber();
  outlined->setStepButtonLayout(AdInputNumber::StepButtonLayout::Split);
  outlined->setMinimum(1);
  outlined->setMaximum(10);
  outlined->setValue(3);
  outlined->setPlaceholderText("Outlined");
  outlined->setFixedWidth(150);

  auto* filled = new AdInputNumber();
  filled->setStepButtonLayout(AdInputNumber::StepButtonLayout::Split);
  filled->setVariant(AdInputNumber::Variant::Filled);
  filled->setMinimum(1);
  filled->setMaximum(10);
  filled->setValue(3);
  filled->setPlaceholderText("Filled");
  filled->setFixedWidth(150);

  layout->addWidget(outlined, 0, Qt::AlignLeft);
  layout->addWidget(filled, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputNumberDocsPage::buildFilledDebugDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* row1 = new QHBoxLayout();
  row1->setSpacing(10);
  auto* filled = new AdInputNumber();
  filled->setPlaceholderText("Filled");
  filled->setVariant(AdInputNumber::Variant::Filled);
  filled->setFixedWidth(170);
  auto* disabled = new AdInputNumber();
  disabled->setPlaceholderText("Filled");
  disabled->setVariant(AdInputNumber::Variant::Filled);
  disabled->setEnabled(false);
  disabled->setFixedWidth(170);
  auto* error = new AdInputNumber();
  error->setPlaceholderText("Filled");
  error->setVariant(AdInputNumber::Variant::Filled);
  error->setStatus(AdInputNumber::Status::Error);
  error->setFixedWidth(170);
  row1->addWidget(filled);
  row1->addWidget(disabled);
  row1->addWidget(error);
  row1->addStretch();

  auto* row2 = new QHBoxLayout();
  row2->setSpacing(10);
  auto* prefix = new AdInputNumber();
  prefix->setPrefixText("$");
  prefix->setPlaceholderText("Filled");
  prefix->setVariant(AdInputNumber::Variant::Filled);
  prefix->setFixedWidth(170);
  auto* prefixDisabled = new AdInputNumber();
  prefixDisabled->setPrefixText("$");
  prefixDisabled->setPlaceholderText("Filled");
  prefixDisabled->setVariant(AdInputNumber::Variant::Filled);
  prefixDisabled->setEnabled(false);
  prefixDisabled->setFixedWidth(170);
  auto* warning = new AdInputNumber();
  warning->setPrefixText("$");
  warning->setPlaceholderText("Filled");
  warning->setVariant(AdInputNumber::Variant::Filled);
  warning->setStatus(AdInputNumber::Status::Warning);
  warning->setFixedWidth(170);
  row2->addWidget(prefix);
  row2->addWidget(prefixDisabled);
  row2->addWidget(warning);
  row2->addStretch();

  layout->addLayout(row1);
  layout->addLayout(row2);
  return box;
}

QWidget* InputNumberDocsPage::buildOutOfRangeDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* input = new AdInputNumber();
  input->setMinimum(1);
  input->setMaximum(10);
  input->setRangeMode(AdInputNumber::RangeMode::Permissive);
  input->setExactValue(QStringLiteral("99"));
  input->setFixedWidth(180);

  auto* reset = new AdButton("Reset");
  reset->setButtonStyle(AdButton::ButtonStyle::Solid);
  reset->setAccentRole(AdButton::AccentRole::Primary);

  auto* hint = makeHintLabel("value: \"99\"");
  connect(input, &AdInputNumber::exactValueChanged, hint, [hint](const QString& value) {
    hint->setText(QStringLiteral("value: %1").arg(valueToText(value)));
  });
  connect(reset, &QAbstractButton::clicked, input, [input]() { input->setValue(99); });

  auto* column = new QVBoxLayout();
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(6);
  column->addWidget(input, 0, Qt::AlignLeft);
  column->addWidget(hint);

  row->addLayout(column);
  row->addWidget(reset, 0, Qt::AlignTop);
  row->addStretch();
  return box;
}

QWidget* InputNumberDocsPage::buildPreSuffixDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* one = new AdInputNumber();
  one->setPrefixText("$");
  one->setFixedWidth(220);

  auto* group = new AdFieldGroup();
  auto* addon = makeAddonIcon(outlined_icons::User(), AdInputNumber::ControlSize::Medium);
  auto* two = new AdInputNumber();
  two->setPrefixText("$");
  two->setFixedWidth(220);
  group->addControl(addon);
  group->addControl(two);

  auto* three = new AdInputNumber();
  three->setPrefixText("$");
  three->setEnabled(false);
  three->setFixedWidth(220);

  auto* four = new AdInputNumber();
  four->setSuffixText("RMB");
  four->setFixedWidth(220);

  layout->addWidget(one, 0, Qt::AlignLeft);
  layout->addWidget(group, 0, Qt::AlignLeft);
  layout->addWidget(three, 0, Qt::AlignLeft);
  layout->addWidget(four, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputNumberDocsPage::buildStatusDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* e1 = new AdInputNumber();
  e1->setStatus(AdInputNumber::Status::Error);
  e1->setFixedWidth(220);

  auto* w1 = new AdInputNumber();
  w1->setStatus(AdInputNumber::Status::Warning);
  w1->setFixedWidth(220);

  auto* e2 = new AdInputNumber();
  e2->setStatus(AdInputNumber::Status::Error);
  e2->setPrefixIconRef(outlined_icons::ClockCircle());
  e2->setFixedWidth(220);

  auto* w2 = new AdInputNumber();
  w2->setStatus(AdInputNumber::Status::Warning);
  w2->setPrefixIconRef(outlined_icons::ClockCircle());
  w2->setFixedWidth(220);

  layout->addWidget(e1, 0, Qt::AlignLeft);
  layout->addWidget(w1, 0, Qt::AlignLeft);
  layout->addWidget(e2, 0, Qt::AlignLeft);
  layout->addWidget(w2, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputNumberDocsPage::buildFocusDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* controls = new QHBoxLayout();
  controls->setSpacing(8);

  auto* input = new AdInputNumber();
  input->setValue(999);
  input->setFixedWidth(300);

  auto* focusStart = new AdButton("Focus at first");
  auto* focusEnd = new AdButton("Focus at last");
  auto* focusAll = new AdButton("Focus to select all");
  auto* focusPreserve = new AdButton("Focus preserve selection");

  connect(focusStart, &QAbstractButton::clicked, input,
          [input]() { input->focusEditor(AdInputNumber::FocusSelection::Start); });
  connect(focusEnd, &QAbstractButton::clicked, input,
          [input]() { input->focusEditor(AdInputNumber::FocusSelection::End); });
  connect(focusAll, &QAbstractButton::clicked, input,
          [input]() { input->focusEditor(AdInputNumber::FocusSelection::SelectAll); });
  connect(focusPreserve, &QAbstractButton::clicked, input,
          [input]() { input->focusEditor(AdInputNumber::FocusSelection::Preserve); });

  controls->addWidget(focusStart);
  controls->addWidget(focusEnd);
  controls->addWidget(focusAll);
  controls->addWidget(focusPreserve);
  controls->addStretch();

  layout->addLayout(controls);
  layout->addWidget(input, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputNumberDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* objectStyled = new AdInputNumber();
  objectStyled->setPlaceholderText("Object");
  objectStyled->setFixedWidth(220);
  AdInputNumber::AppearanceOverrides objectOverrides;
  objectOverrides.frame.borderColor = QColor("#696FC7");
  objectOverrides.input.textColor = QColor("#696FC7");
  objectStyled->setAppearanceOverrides(objectOverrides);

  auto* functionStyled = new AdInputNumber();
  functionStyled->setPlaceholderText("Function");
  functionStyled->setControlSize(AdInputNumber::ControlSize::Large);
  functionStyled->setFixedWidth(220);
  AdInputNumber::AppearanceOverrides functionOverrides;
  functionOverrides.frame.backgroundColor = QColor(250, 250, 250, 127);
  functionOverrides.frame.borderColor = QColor("#722ed1");
  functionStyled->setAppearanceOverrides(functionOverrides);

  layout->addWidget(objectStyled, 0, Qt::AlignLeft);
  layout->addWidget(functionStyled, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputNumberDocsPage::buildControlsDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* customIcons = new AdInputNumber();
  customIcons->setUpIconRef(outlined_icons::ArrowUp());
  customIcons->setDownIconRef(outlined_icons::ArrowDown());
  customIcons->setFixedWidth(180);

  auto* noControls = new AdInputNumber();
  noControls->setStepButtonsVisible(false);
  noControls->setValue(42);
  noControls->setFixedWidth(180);

  row->addWidget(customIcons);
  row->addWidget(noControls);
  row->addStretch();
  return box;
}

QWidget* InputNumberDocsPage::buildRenderPanelDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  AdInputNumber::AppearanceOverrides panelOverrides;
  panelOverrides.metrics.handleVisibleWidth = 24;

  auto* inputMode = new AdInputNumber();
  inputMode->setValue(100);
  inputMode->setPrefixText("$");
  inputMode->setSuffixText("RMB");
  inputMode->setAppearanceOverrides(panelOverrides);
  inputMode->setFixedWidth(220);

  auto* spinnerMode = new AdInputNumber();
  spinnerMode->setStepButtonLayout(AdInputNumber::StepButtonLayout::Split);
  spinnerMode->setValue(100);
  spinnerMode->setAppearanceOverrides(panelOverrides);
  spinnerMode->setFixedWidth(220);

  layout->addWidget(inputMode, 0, Qt::AlignLeft);
  layout->addWidget(spinnerMode, 0, Qt::AlignLeft);
  layout->addWidget(makeHintLabel(
      "Internal panel equivalent in Qt demo: always-visible actions in input/spinner mode."));
  return box;
}

QWidget* InputNumberDocsPage::buildDebugTokenDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    AdInputNumber::AppearanceOverrides wideHandle;
    wideHandle.metrics.handleWidth = 50;
    auto* first = new AdInputNumber();
    first->setAppearanceOverrides(wideHandle);
    first->setFixedWidth(200);

    AdInputNumber::AppearanceOverrides normalHandle;
    normalHandle.metrics.handleWidth = 25;
    auto* second = new AdInputNumber();
    second->setAppearanceOverrides(normalHandle);
    second->setFixedWidth(200);

    row->addWidget(first);
    row->addWidget(second);
    row->addStretch();
    layout->addLayout(row);
  }

  {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    AdInputNumber::AppearanceOverrides spacingTokens;
    spacingTokens.metrics.largeHorizontalPadding = 16;

    auto* left = new AdInputNumber();
    left->setControlSize(AdInputNumber::ControlSize::Large);
    left->setAppearanceOverrides(spacingTokens);
    left->setFixedWidth(220);

    auto* right = new AdInputNumber();
    right->setControlSize(AdInputNumber::ControlSize::Large);
    right->setPrefixText("$");
    right->setAppearanceOverrides(spacingTokens);
    right->setFixedWidth(220);

    row->addWidget(left);
    row->addWidget(right);
    row->addStretch();
    layout->addLayout(row);
  }

  {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    AdInputNumber::AppearanceOverrides fontTokens;
    fontTokens.metrics.inputFontSize = 30;
    fontTokens.metrics.inputFontSizeSM = 20;
    fontTokens.metrics.inputFontSizeLG = 40;

    auto* small = new AdInputNumber();
    small->setControlSize(AdInputNumber::ControlSize::Small);
    small->setValue(11111);
    small->setAppearanceOverrides(fontTokens);
    small->setFixedWidth(170);

    auto* medium = new AdInputNumber();
    medium->setValue(11111);
    medium->setAppearanceOverrides(fontTokens);
    medium->setFixedWidth(170);

    auto* large = new AdInputNumber();
    large->setControlSize(AdInputNumber::ControlSize::Large);
    large->setValue(11111);
    large->setAppearanceOverrides(fontTokens);
    large->setFixedWidth(170);

    row->addWidget(small);
    row->addWidget(medium);
    row->addWidget(large);
    row->addStretch();
    layout->addLayout(row);
  }

  return box;
}
