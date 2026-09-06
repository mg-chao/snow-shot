#include "slider_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPalette>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "antd_icons.h"

using adqt::widgets::AdInputNumber;
using adqt::widgets::AdMultiSlider;
using adqt::widgets::AdRangeSlider;
using adqt::widgets::AdSlider;
using adqt::widgets::AdSliderComponentTokens;
using adqt::widgets::AdSliderSemanticStyles;
using adqt::widgets::AdSliderStyleContext;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

QLabel* makeIconLabel(const adqt::icons::IconRef& token, const QColor& color,
                      QWidget* parent = nullptr) {
  auto* label = new QLabel(parent);
  const auto tinted = token.withColors(adqt::icons::IconColors::primary(color));
  label->setPixmap(adqt::icons::renderIconPixmap(tinted, {QSize(18, 18), 1.0}));
  label->setFixedSize(20, 20);
  return label;
}

QString formatNumber(double value) {
  if (!std::isfinite(value)) {
    return QStringLiteral("0");
  }

  QString text = QString::number(value, 'f', 4);
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

}  // namespace

SliderDocsPage::SliderDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Slider");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle =
      new QLabel("A Slider component for displaying current value and intervals in range.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Slider with InputNumber", "Demo: input-number.tsx", buildInputNumberDemo());
  addSection(root, "Slider with icon", "Demo: icon-slider.tsx", buildIconSliderDemo());
  addSection(root, "Customize tooltip", "Demo: tip-formatter.tsx", buildTipFormatterDemo());
  addSection(root, "Event", "Demo: event.tsx", buildEventDemo());
  addSection(root, "Graduated slider", "Demo: mark.tsx", buildMarkDemo());
  addSection(root, "Vertical", "Demo: vertical.tsx", buildVerticalDemo());
  addSection(root, "Tooltip visibility", "Demo: show-tooltip.tsx", buildShowTooltipDemo());
  addSection(root, "Reverse", "Demo: reverse.tsx", buildReverseDemo());
  addSection(root, "Selection drag", "Demo: draggableTrack.tsx", buildDraggableTrackDemo());
  addSection(root, "Multiple handles", "Demo: multiple.tsx", buildMultipleDemo());
  addSection(root, "Editable handles", "Demo: editable.tsx", buildEditableDemo());
  addSection(root, "Custom semantic dom styling", "Demo: style-class.tsx", buildStyleClassDemo());
  addSection(root, "Component Token", "Demo: component-token.tsx", buildComponentTokenDemo());

  root->addStretch();
}

const QVector<QWidget*>& SliderDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& SliderDocsPage::sectionTitles() const { return titles_; }

void SliderDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
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

SliderDocsPage::MarkMap SliderDocsPage::temperatureMarks() const {
  MarkMap marks;
  marks.insert(0, Mark{QStringLiteral("0\u00B0C"), std::nullopt, std::nullopt});
  marks.insert(26, Mark{QStringLiteral("26\u00B0C"), std::nullopt, std::nullopt});
  marks.insert(37, Mark{QStringLiteral("37\u00B0C"), std::nullopt, std::nullopt});
  marks.insert(100, Mark{QStringLiteral("100\u00B0C"), QColor("#f50"), std::nullopt});
  return marks;
}

QWidget* SliderDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* single = new AdSlider();
  single->setValue(30);

  auto* range = new AdRangeSlider();
  range->setValues(20, 50);

  auto* disabled = new QCheckBox("Disabled");
  connect(disabled, &QCheckBox::toggled, single, &AdSlider::setDisabled);
  connect(disabled, &QCheckBox::toggled, range, &QWidget::setDisabled);

  layout->addWidget(single);
  layout->addWidget(range);
  layout->addWidget(disabled, 0, Qt::AlignLeft);
  return box;
}

QWidget* SliderDocsPage::buildInputNumberDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* row1 = new QHBoxLayout();
  auto* integerSlider = new AdSlider();
  integerSlider->setMinimum(1);
  integerSlider->setMaximum(20);
  integerSlider->setSingleStep(1);
  integerSlider->setValue(1);
  integerSlider->setFixedWidth(300);
  auto* integerInput = new AdInputNumber();
  integerInput->setMinimum(1);
  integerInput->setMaximum(20);
  integerInput->setSingleStep(1);
  integerInput->setValue(1);
  integerInput->setFixedWidth(120);

  connect(integerSlider, &AdSlider::valueChanged, integerInput,
          [integerInput](double value) { integerInput->setValue(qRound(value)); });
  connect(integerInput, &AdInputNumber::valueChanged, integerSlider,
          [integerSlider](double value) { integerSlider->setValue(value); });

  row1->addWidget(integerSlider);
  row1->addWidget(integerInput);
  row1->addStretch();

  auto* row2 = new QHBoxLayout();
  auto* decimalSlider = new AdSlider();
  decimalSlider->setMinimum(0.0);
  decimalSlider->setMaximum(1.0);
  decimalSlider->setSingleStep(0.01);
  decimalSlider->setValue(0.0);
  decimalSlider->setFixedWidth(300);
  auto* decimalInput = new AdInputNumber();
  decimalInput->setMinimum(0.0);
  decimalInput->setMaximum(1.0);
  decimalInput->setSingleStep(0.01);
  decimalInput->setDecimals(2);
  decimalInput->setValue(0.0);
  decimalInput->setFixedWidth(120);

  connect(decimalSlider, &AdSlider::valueChanged, decimalInput,
          [decimalInput](double value) { decimalInput->setValue(value); });
  connect(decimalInput, &AdInputNumber::valueChanged, decimalSlider,
          [decimalSlider](double value) { decimalSlider->setValue(value); });

  row2->addWidget(decimalSlider);
  row2->addWidget(decimalInput);
  row2->addStretch();

  layout->addLayout(row1);
  layout->addLayout(row2);
  return box;
}

QWidget* SliderDocsPage::buildIconSliderDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* slider = new AdSlider();
  slider->setMinimum(0);
  slider->setMaximum(20);
  slider->setValue(0);
  slider->setFixedWidth(280);

  auto* left = makeIconLabel(outlined_icons::Frown(), QColor(0, 0, 0, 96));
  auto* right = makeIconLabel(outlined_icons::Smile(), QColor(0, 0, 0, 96));

  const auto applyState = [slider, left, right]() {
    const double mid = (slider->maximum() - slider->minimum()) / 2.0;
    const bool rightActive = slider->value() >= mid;
    const auto leftStyle =
        adqt::icons::IconColors::primary(rightActive ? QColor(0, 0, 0, 96) : QColor(0, 0, 0, 150));
    const auto rightStyle =
        adqt::icons::IconColors::primary(rightActive ? QColor(0, 0, 0, 150) : QColor(0, 0, 0, 96));
    left->setPixmap(
        adqt::icons::renderIconPixmap(outlined_icons::Frown(leftStyle), {QSize(18, 18), 1.0}));
    right->setPixmap(
        adqt::icons::renderIconPixmap(outlined_icons::Smile(rightStyle), {QSize(18, 18), 1.0}));
  };

  connect(slider, &AdSlider::valueChanged, slider, [applyState](double) { applyState(); });
  applyState();

  row->addWidget(left);
  row->addWidget(slider);
  row->addWidget(right);
  row->addStretch();
  return box;
}

QWidget* SliderDocsPage::buildTipFormatterDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* first = new AdSlider();
  first->setValue(30);
  first->setTooltipFormatter(
      [](double value) { return QStringLiteral("%1%").arg(formatNumber(value)); });

  auto* second = new AdSlider();
  second->setValue(30);
  second->setTooltipEnabled(false);

  layout->addWidget(first);
  layout->addWidget(second);
  return box;
}

QWidget* SliderDocsPage::buildEventDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* single = new AdSlider();
  single->setValue(30);
  auto* singleHint = makeHintLabel("single valueChanged: 30, editingFinished: 30");
  connect(single, &AdSlider::valueChanged, singleHint, [singleHint](double value) {
    singleHint->setText(QStringLiteral("single onChange: %1").arg(formatNumber(value)));
  });
  connect(single, &AdSlider::editingFinished, singleHint, [single, singleHint]() {
    singleHint->setText(singleHint->text() +
                        QStringLiteral(", editingFinished: %1").arg(formatNumber(single->value())));
  });

  auto* range = new AdRangeSlider();
  range->setSingleStep(10);
  range->setValues(20, 50);
  auto* rangeHint = makeHintLabel("range handleValuesChanged: [20, 50], editingFinished: [20, 50]");
  connect(
      range, &AdMultiSlider::handleValuesChanged, rangeHint,
      [rangeHint](const QList<double>& values) {
        QStringList parts;
        for (double value : values) {
          parts.append(formatNumber(value));
        }
        rangeHint->setText(QStringLiteral("range handleValuesChanged: [%1]").arg(parts.join(", ")));
      });
  connect(range, &AdRangeSlider::editingFinished, rangeHint, [range, rangeHint]() {
    const QList<double> values = {range->lowerValue(), range->upperValue()};
    QStringList parts;
    for (double value : values) {
      parts.append(formatNumber(value));
    }
    rangeHint->setText(rangeHint->text() +
                       QStringLiteral(", editingFinished: [%1]").arg(parts.join(", ")));
  });

  layout->addWidget(single);
  layout->addWidget(singleHint);
  layout->addWidget(range);
  layout->addWidget(rangeHint);
  return box;
}

QWidget* SliderDocsPage::buildMarkDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* includedTitle = new QLabel("selectionHighlightVisible=true");
  auto* includedSingle = new AdSlider();
  includedSingle->setMarks(temperatureMarks());
  includedSingle->setValue(37);
  auto* includedRange = new AdRangeSlider();
  includedRange->setMarks(temperatureMarks());
  includedRange->setValues(26, 37);

  auto* excludedTitle = new QLabel("selectionHighlightVisible=false");
  auto* excludedSingle = new AdSlider();
  excludedSingle->setMarks(temperatureMarks());
  excludedSingle->setSelectionHighlightVisible(false);
  excludedSingle->setValue(37);

  auto* stepTitle = new QLabel("marks & step");
  auto* stepSlider = new AdSlider();
  stepSlider->setMarks(temperatureMarks());
  stepSlider->setSingleStep(10);
  stepSlider->setValue(37);

  auto* marksOnlyTitle = new QLabel("markSnapEnabled=true");
  auto* marksOnlySlider = new AdSlider();
  marksOnlySlider->setMarks(temperatureMarks());
  marksOnlySlider->setMarkSnapEnabled(true);
  marksOnlySlider->setValue(37);

  layout->addWidget(includedTitle);
  layout->addWidget(includedSingle);
  layout->addWidget(includedRange);
  layout->addWidget(excludedTitle);
  layout->addWidget(excludedSingle);
  layout->addWidget(stepTitle);
  layout->addWidget(stepSlider);
  layout->addWidget(marksOnlyTitle);
  layout->addWidget(marksOnlySlider);
  return box;
}

QWidget* SliderDocsPage::buildVerticalDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(22);
  row->setAlignment(Qt::AlignLeft | Qt::AlignTop);

  constexpr int kVerticalSliderWidth = 90;
  constexpr int kVerticalSliderHeight = 300;

  auto* sliderA = new AdSlider();
  sliderA->setOrientation(Qt::Vertical);
  sliderA->setValue(30);
  sliderA->setFixedSize(kVerticalSliderWidth, kVerticalSliderHeight);

  auto* sliderB = new AdRangeSlider();
  sliderB->setOrientation(Qt::Vertical);
  sliderB->setSingleStep(10);
  sliderB->setValues(20, 50);
  sliderB->setFixedSize(kVerticalSliderWidth, kVerticalSliderHeight);

  auto* sliderC = new AdRangeSlider();
  sliderC->setOrientation(Qt::Vertical);
  sliderC->setMarks(temperatureMarks());
  sliderC->setValues(26, 37);
  sliderC->setFixedSize(kVerticalSliderWidth, kVerticalSliderHeight);

  row->addWidget(sliderA);
  row->addWidget(sliderB);
  row->addWidget(sliderC);
  row->addStretch();
  return box;
}

QWidget* SliderDocsPage::buildShowTooltipDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* slider = new AdSlider();
  slider->setValue(30);
  slider->setTooltipVisibleMode(AdSlider::TooltipVisibleMode::Always);
  layout->addWidget(slider);
  return box;
}

QWidget* SliderDocsPage::buildReverseDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* single = new AdSlider();
  single->setValue(30);
  auto* range = new AdRangeSlider();
  range->setValues(20, 50);
  auto* reverseCheck = new QCheckBox("Reversed");
  reverseCheck->setChecked(true);
  single->setInvertedAppearance(true);
  single->setInvertedControls(true);
  range->setInvertedAppearance(true);
  range->setInvertedControls(true);

  connect(reverseCheck, &QCheckBox::toggled, single, [single](bool checked) {
    single->setInvertedAppearance(checked);
    single->setInvertedControls(checked);
  });
  connect(reverseCheck, &QCheckBox::toggled, range, [range](bool checked) {
    range->setInvertedAppearance(checked);
    range->setInvertedControls(checked);
  });

  layout->addWidget(single);
  layout->addWidget(range);
  layout->addWidget(reverseCheck, 0, Qt::AlignLeft);
  return box;
}

QWidget* SliderDocsPage::buildDraggableTrackDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* slider = new AdRangeSlider();
  slider->setValues(20, 50);
  slider->setSelectionDragEnabled(true);

  layout->addWidget(slider);
  layout->addWidget(makeHintLabel("Drag the selected range track to move the whole segment."));
  return box;
}

QWidget* SliderDocsPage::buildMultipleDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* slider = new AdMultiSlider();
  slider->setHandleValues({0, 10, 20});
  slider->setTooltipVisibleMode(AdSlider::TooltipVisibleMode::Always);
  slider->setSemanticStyleResolver([](const AdSliderStyleContext& ctx) {
    AdSliderSemanticStyles styles;
    if (ctx.values.isEmpty()) {
      return styles;
    }
    const double low = ctx.values.constFirst() / 100.0;
    const double high = ctx.values.constLast() / 100.0;

    QColor start(135, 208, 104);
    QColor end(255, 204, 199);
    auto colorAt = [&](double p) {
      const int r = qRound(start.red() + (end.red() - start.red()) * p);
      const int g = qRound(start.green() + (end.green() - start.green()) * p);
      const int b = qRound(start.blue() + (end.blue() - start.blue()) * p);
      return QColor(r, g, b);
    };

    QLinearGradient gradient(0, 0, 1, 0);
    gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    gradient.setColorAt(0, colorAt(low));
    gradient.setColorAt(1, colorAt(high));
    styles.track.backgroundColor = QColor(0, 0, 0, 0);
    styles.tracks.brush = QBrush(gradient);
    return styles;
  });

  layout->addWidget(slider);
  return box;
}

QWidget* SliderDocsPage::buildEditableDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* slider = new AdMultiSlider();
  slider->setHandleValues({20, 80});
  slider->setHandleEditingEnabled(true);
  slider->setMinimumHandleCount(1);
  slider->setMaximumHandleCount(5);

  layout->addWidget(slider);
  layout->addWidget(makeHintLabel(
      "Click rail to add a node. Focus a handle then press Delete/Backspace to remove."));
  return box;
}

QWidget* SliderDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  const auto makeVerticalGradientBrush = [](const QColor& top, const QColor& bottom) {
    QLinearGradient gradient(0.0, 0.0, 0.0, 1.0);
    gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    return QBrush(gradient);
  };

  auto* objectStyled = new AdSlider();
  objectStyled->setValue(30);
  objectStyled->setFixedWidth(300);
  AdSliderSemanticStyles fixedStyles;
  fixedStyles.tracks.brush = makeVerticalGradientBrush(QColor("#91caff"), QColor("#1677ff"));
  fixedStyles.handle.borderColor = QColor("#1677ff");
  objectStyled->setSemanticStyles(fixedStyles);

  auto* resolverStyled = new AdSlider();
  resolverStyled->setOrientation(Qt::Vertical);
  resolverStyled->setInvertedAppearance(true);
  resolverStyled->setInvertedControls(true);
  resolverStyled->setValue(30);
  resolverStyled->setFixedSize(100, 300);
  resolverStyled->setSemanticStyleResolver([](const AdSliderStyleContext& ctx) {
    AdSliderSemanticStyles styles;
    if (ctx.orientation == Qt::Vertical) {
      QLinearGradient gradient(0.0, 0.0, 0.0, 1.0);
      gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
      gradient.setColorAt(0.0, QColor("#722cc0"));
      gradient.setColorAt(1.0, QColor("#722ed1"));
      styles.tracks.brush = QBrush(gradient);
      styles.handle.borderColor = QColor("#722ed1");
    }
    return styles;
  });

  auto* row = new QHBoxLayout();
  row->addWidget(objectStyled, 1);
  row->addWidget(resolverStyled, 0, Qt::AlignLeft);
  row->addStretch();

  layout->addLayout(row);
  return box;
}

QWidget* SliderDocsPage::buildComponentTokenDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  AdSliderComponentTokens tokens;
  tokens.controlSize = 20;
  tokens.railSize = 4;
  tokens.handleSize = 22;
  tokens.handleSizeHover = 18;
  tokens.dotSize = 8;
  tokens.handleLineWidth = 6;
  tokens.handleLineWidthHover = 2;
  tokens.railBg = QStringLiteral("#9f3434");
  tokens.railHoverBg = QStringLiteral("#8d2424");
  tokens.trackBg = QStringLiteral("#b0b0ef");
  tokens.trackHoverBg = QStringLiteral("#c77195");
  tokens.handleColor = QStringLiteral("#e6f6a2");
  tokens.handleActiveColor = QStringLiteral("#d22bc4");
  tokens.dotBorderColor = QStringLiteral("#303030");
  tokens.dotActiveBorderColor = QStringLiteral("#918542");
  tokens.trackBgDisabled = QStringLiteral("#1a1b80");

  auto* disabled = new AdSlider();
  disabled->setComponentTokens(tokens);
  disabled->setValue(30);
  disabled->setDisabled(true);

  auto* draggable = new AdRangeSlider();
  draggable->setComponentTokens(tokens);
  draggable->setSelectionDragEnabled(true);
  draggable->setValues(20, 50);

  auto* verticalRow = new QHBoxLayout();
  auto* vSingle = new AdSlider();
  vSingle->setComponentTokens(tokens);
  vSingle->setOrientation(Qt::Vertical);
  vSingle->setValue(30);
  vSingle->setFixedSize(90, 300);

  auto* vRange = new AdRangeSlider();
  vRange->setComponentTokens(tokens);
  vRange->setOrientation(Qt::Vertical);
  vRange->setSingleStep(10);
  vRange->setValues(20, 50);
  vRange->setFixedSize(90, 300);

  auto* vMarks = new AdRangeSlider();
  vMarks->setComponentTokens(tokens);
  vMarks->setOrientation(Qt::Vertical);
  vMarks->setMarks(temperatureMarks());
  vMarks->setValues(26, 37);
  vMarks->setFixedSize(90, 300);

  verticalRow->addWidget(vSingle);
  verticalRow->addWidget(vRange);
  verticalRow->addWidget(vMarks);
  verticalRow->addStretch();

  layout->addWidget(disabled);
  layout->addWidget(draggable);
  layout->addLayout(verticalRow);
  return box;
}
