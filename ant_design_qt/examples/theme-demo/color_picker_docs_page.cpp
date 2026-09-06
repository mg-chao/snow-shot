#include "color_picker_docs_page.h"

#include "demo_theme_utils.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

using adqt::widgets::AdColorPicker;

namespace {

QString formatAlpha(double value) {
  QString text = QString::number(value, 'f', 3);
  while (text.contains(QLatin1Char('.')) &&
         (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) {
    text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) {
      text.chop(1);
      break;
    }
  }
  return text.isEmpty() ? QStringLiteral("0") : text;
}

QColor parseCssColor(const QString& css, const QColor& fallback) {
  const QString trimmed = css.trimmed();
  QColor color(trimmed);
  if (color.isValid()) {
    return color;
  }

  static const QRegularExpression rgbRegex(
      QStringLiteral("^rgba?\\s*\\(\\s*([0-9]{1,3})\\s*,\\s*([0-9]{1,3})\\s*,\\s*([0-9]{1,3})"
                     "(?:\\s*,\\s*([0-9]+(?:\\.[0-9]+)?%?))?\\s*\\)$"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch rgbMatch = rgbRegex.match(trimmed);
  if (rgbMatch.hasMatch()) {
    bool rOk = false;
    bool gOk = false;
    bool bOk = false;
    const int red = std::clamp(rgbMatch.captured(1).toInt(&rOk), 0, 255);
    const int green = std::clamp(rgbMatch.captured(2).toInt(&gOk), 0, 255);
    const int blue = std::clamp(rgbMatch.captured(3).toInt(&bOk), 0, 255);
    if (rOk && gOk && bOk) {
      int alpha = 255;
      if (!rgbMatch.captured(4).trimmed().isEmpty()) {
        bool aOk = false;
        const QString alphaText = rgbMatch.captured(4).trimmed();
        if (alphaText.endsWith(QLatin1Char('%'))) {
          const double percent = alphaText.left(alphaText.size() - 1).toDouble(&aOk);
          alpha = std::clamp(qRound(percent * 255.0 / 100.0), 0, 255);
        } else {
          double alphaRaw = alphaText.toDouble(&aOk);
          if (alphaRaw > 1.0) {
            alphaRaw /= 255.0;
          }
          alpha = std::clamp(qRound(alphaRaw * 255.0), 0, 255);
        }
        if (!aOk) {
          return fallback;
        }
      }
      return QColor(red, green, blue, alpha);
    }
  }

  static const QRegularExpression hsbRegex(
      QStringLiteral("^hsb\\s*\\(\\s*([-+]?[0-9]+(?:\\.[0-9]+)?)\\s*,\\s*([0-9]+(?:\\.[0-9]+)?)%"
                     "\\s*,\\s*([0-9]+(?:\\.[0-9]+)?)%"
                     "(?:\\s*,\\s*([0-9]+(?:\\.[0-9]+)?%?))?\\s*\\)$"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch hsbMatch = hsbRegex.match(trimmed);
  if (hsbMatch.hasMatch()) {
    bool hOk = false;
    bool sOk = false;
    bool bOk = false;
    const double hueRaw = hsbMatch.captured(1).toDouble(&hOk);
    const double satRaw = hsbMatch.captured(2).toDouble(&sOk);
    const double briRaw = hsbMatch.captured(3).toDouble(&bOk);
    if (hOk && sOk && bOk) {
      double alphaRaw = 1.0;
      if (!hsbMatch.captured(4).trimmed().isEmpty()) {
        bool aOk = false;
        const QString alphaText = hsbMatch.captured(4).trimmed();
        if (alphaText.endsWith(QLatin1Char('%'))) {
          alphaRaw = alphaText.left(alphaText.size() - 1).toDouble(&aOk) / 100.0;
        } else {
          alphaRaw = alphaText.toDouble(&aOk);
          if (alphaRaw > 1.0) {
            alphaRaw /= 100.0;
          }
        }
        if (!aOk) {
          return fallback;
        }
      }

      const int hue = ((qRound(hueRaw) % 360) + 360) % 360;
      const int sat = std::clamp(qRound(satRaw * 255.0 / 100.0), 0, 255);
      const int bri = std::clamp(qRound(briRaw * 255.0 / 100.0), 0, 255);
      const int alpha = std::clamp(qRound(alphaRaw * 255.0), 0, 255);
      return QColor::fromHsv(hue, sat, bri, alpha);
    }
  }

  return fallback;
}

QString describeColor(const QColor& color) {
  if (!color.isValid()) {
    return QStringLiteral("invalid");
  }
  if (color.alpha() >= 255) {
    return color.name(QColor::HexRgb).toUpper();
  }
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(formatAlpha(color.alphaF()));
}

QColor selectionPreviewColor(const adqt::widgets::AdColorValue& value) {
  if (value.isGradient() && !value.gradientStops.isEmpty()) {
    return value.gradientStops.constFirst().second;
  }
  return value.solidColor;
}

QColor compositeOn(const QColor& foreground, const QColor& background) {
  const qreal alpha = std::clamp(static_cast<qreal>(foreground.alphaF()), qreal(0.0), qreal(1.0));
  return QColor(qRound(foreground.red() * alpha + background.red() * (1.0 - alpha)),
                qRound(foreground.green() * alpha + background.green() * (1.0 - alpha)),
                qRound(foreground.blue() * alpha + background.blue() * (1.0 - alpha)));
}

QColor triggerTextColor(const QColor& color) {
  const QColor effective = compositeOn(color, QColor(QStringLiteral("#f5f5f5")));
  const int luminance = qGray(effective.red(), effective.green(), effective.blue());
  return luminance >= 160 ? QColor(QStringLiteral("#262626")) : QColor(QStringLiteral("#ffffff"));
}

class DemoColorTrigger final : public QWidget {
 public:
  explicit DemoColorTrigger(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedSize(86, 32);
    setCursor(Qt::PointingHandCursor);
  }

  void setColor(const QColor& color) {
    const QColor next = color.isValid() ? color : QColor(QStringLiteral("#1677ff"));
    if (next == color_) {
      return;
    }
    color_ = next;
    update();
  }

 protected:
  void paintEvent(QPaintEvent* event) override {
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF frameRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    if (frameRect.width() <= 0.0 || frameRect.height() <= 0.0) {
      return;
    }

    constexpr qreal kRadius = 6.0;
    constexpr int kCheckerCellSize = 8;
    QPainterPath clipPath;
    clipPath.addRoundedRect(frameRect, kRadius, kRadius);

    painter.save();
    painter.setClipPath(clipPath);
    for (int y = 0; y < height(); y += kCheckerCellSize) {
      for (int x = 0; x < width(); x += kCheckerCellSize) {
        const bool lightCell = ((x / kCheckerCellSize) + (y / kCheckerCellSize)) % 2 == 0;
        painter.fillRect(
            QRect(x, y, kCheckerCellSize, kCheckerCellSize),
            lightCell ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#f0f0f0")));
      }
    }
    painter.fillPath(clipPath, color_);
    painter.restore();

    painter.setPen(QPen(QColor(QStringLiteral("#d9d9d9")), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frameRect, kRadius, kRadius);

    if (color_.alpha() == 0) {
      painter.setPen(
          QPen(QColor(QStringLiteral("#ff4d4f")), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(frameRect.topLeft() + QPointF(7.0, 7.0),
                       frameRect.bottomRight() - QPointF(7.0, 7.0));
    }

    painter.setPen(triggerTextColor(color_));
    QFont textFont = painter.font();
    textFont.setBold(true);
    painter.setFont(textFont);
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("open"));
  }

 private:
  QColor color_ = QColor(QStringLiteral("#1677ff"));
};

}  // namespace

ColorPickerDocsPage::ColorPickerDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("ColorPicker");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel("Used for color selection.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic Usage", "Demo: base.tsx", buildBaseDemo());
  addSection(root, "Trigger size", "Demo: size.tsx", buildSizeDemo());
  addSection(root, "controlled mode", "Demo: controlled.tsx", buildControlledDemo());
  addSection(root, "Line Gradient", "Demo: line-gradient.tsx", buildLineGradientDemo());
  addSection(root, "Rendering Trigger Text", "Demo: text-render.tsx", buildTextRenderDemo());
  addSection(root, "Disable", "Demo: disabled.tsx", buildDisabledDemo());
  addSection(root, "Disabled Alpha", "Demo: disabled-alpha.tsx", buildDisabledAlphaDemo());
  addSection(root, "Clear Color", "Demo: allowClear.tsx", buildAllowClearDemo());
  addSection(root, "Custom Trigger", "Demo: trigger.tsx", buildTriggerDemo());
  addSection(root, "Custom Trigger Event", "Demo: trigger-event.tsx", buildTriggerEventDemo());
  addSection(root, "Popup Layer", "Qt extension: popupLayerMode.", buildPopupLayerModeDemo());
  addSection(root, "Color Format", "Demo: format.tsx", buildFormatDemo());
  addSection(root, "Preset Colors", "Demo: presets.tsx", buildPresetsDemo());
  addSection(root, "Standalone Panel", "Demo: panel.tsx", buildPanelRenderDemo());
  addSection(root, "Component Tokens", "Demo: design-token.tsx", buildStyleClassDemo());

  root->addStretch();
}

const QVector<QWidget*>& ColorPickerDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& ColorPickerDocsPage::sectionTitles() const { return titles_; }

void ColorPickerDocsPage::addSection(QVBoxLayout* root, const QString& title,
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

ColorPickerDocsPage::ColorValue ColorPickerDocsPage::solid(const QString& value) {
  return ColorValue::solid(parseCssColor(value, QColor(QStringLiteral("#1677ff"))));
}

ColorPickerDocsPage::ColorValue ColorPickerDocsPage::gradient(const QVector<GradientStop>& stops) {
  QGradientStops gradientStops;
  gradientStops.reserve(stops.size());
  for (const GradientStop& stop : stops) {
    gradientStops.append(
        QGradientStop(qBound(0.0, static_cast<double>(stop.percent) / 100.0, 1.0), stop.color));
  }
  const QColor editableColor = gradientStops.isEmpty() ? QColor(QStringLiteral("#1677ff"))
                                                       : gradientStops.constFirst().second;
  return ColorValue::gradient(gradientStops, editableColor);
}

QWidget* ColorPickerDocsPage::buildBaseDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* picker = new AdColorPicker();
  picker->setCssText(QStringLiteral("#1677ff"));
  row->addWidget(picker);
  row->addStretch();
  return box;
}

QWidget* ColorPickerDocsPage::buildSizeDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(24);

  auto* left = new QVBoxLayout();
  left->setSpacing(8);
  auto* right = new QVBoxLayout();
  right->setSpacing(8);

  const QList<AdColorPicker::Size> sizes = {
      AdColorPicker::Size::Small,
      AdColorPicker::Size::Middle,
      AdColorPicker::Size::Large,
  };

  for (AdColorPicker::Size size : sizes) {
    auto* picker = new AdColorPicker();
    picker->setCssText(QStringLiteral("#1677ff"));
    picker->setSize(size);
    left->addWidget(picker, 0, Qt::AlignLeft);

    auto* pickerText = new AdColorPicker();
    pickerText->setCssText(QStringLiteral("#1677ff"));
    pickerText->setSize(size);
    pickerText->setTriggerTextVisible(true);
    right->addWidget(pickerText, 0, Qt::AlignLeft);
  }

  row->addLayout(left);
  row->addLayout(right);
  row->addStretch();
  return box;
}

QWidget* ColorPickerDocsPage::buildControlledDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* row = new QHBoxLayout();
  row->setSpacing(12);

  auto* liveState = new adqt::widgets::AdColorPickerState(box);
  liveState->setCssText(QStringLiteral("#1677ff"));
  auto* commitState = new adqt::widgets::AdColorPickerState(box);
  commitState->setCssText(QStringLiteral("#1677ff"));

  auto* onChangePicker = new AdColorPicker();
  onChangePicker->setState(liveState);

  auto* onCompletePicker = new AdColorPicker();
  onCompletePicker->setState(commitState);

  auto syncBoth = [liveState, commitState](const ColorValue& value) {
    if (liveState) {
      liveState->setValue(value);
    }
    if (commitState) {
      commitState->setValue(value);
    }
  };

  connect(onChangePicker, &AdColorPicker::valueChanged, this,
          [syncBoth](const ColorValue& value) { syncBoth(value); });
  connect(onCompletePicker, &AdColorPicker::editingFinished, this,
          [syncBoth](const ColorValue& value) { syncBoth(value); });

  auto* liveLabel = makeHintLabel("cssTextChanged: rgb(22,119,255)");
  auto* completeLabel = makeHintLabel("editingFinished: rgb(22,119,255)");

  connect(onChangePicker, &AdColorPicker::cssTextChanged, this, [liveLabel](const QString& css) {
    liveLabel->setText(QStringLiteral("cssTextChanged: %1").arg(css));
  });
  connect(onCompletePicker, &AdColorPicker::editingFinished, this,
          [completeLabel, onCompletePicker](const ColorValue&) {
            completeLabel->setText(
                QStringLiteral("editingFinished: %1").arg(onCompletePicker->cssText()));
          });

  row->addWidget(onChangePicker);
  row->addWidget(onCompletePicker);
  row->addStretch();

  layout->addLayout(row);
  layout->addWidget(liveLabel);
  layout->addWidget(completeLabel);
  return box;
}

QWidget* ColorPickerDocsPage::buildLineGradientDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  const ColorValue defaultGradient =
      gradient({GradientStop{parseCssColor(QStringLiteral("rgb(16, 142, 233)"), QColor()), 0},
                GradientStop{parseCssColor(QStringLiteral("rgb(135, 208, 104)"), QColor()), 100}});

  auto* mixed = new AdColorPicker();
  mixed->setModeOptions({AdColorPicker::Mode::Solid, AdColorPicker::Mode::Gradient});
  mixed->setMode(AdColorPicker::Mode::Gradient);
  mixed->setAllowClear(true);
  mixed->setTriggerTextVisible(true);
  mixed->setValue(defaultGradient);

  auto* gradientOnly = new AdColorPicker();
  gradientOnly->setModeOptions({AdColorPicker::Mode::Gradient});
  gradientOnly->setMode(AdColorPicker::Mode::Gradient);
  gradientOnly->setAllowClear(true);
  gradientOnly->setTriggerTextVisible(true);
  gradientOnly->setValue(defaultGradient);

  auto* output = makeHintLabel("editingFinished: linear-gradient(...)");
  connect(mixed, &AdColorPicker::editingFinished, this, [output, mixed](const ColorValue&) {
    output->setText(QStringLiteral("editingFinished: %1").arg(mixed->cssText()));
  });
  connect(gradientOnly, &AdColorPicker::editingFinished, this,
          [output, gradientOnly](const ColorValue&) {
            output->setText(QStringLiteral("editingFinished: %1").arg(gradientOnly->cssText()));
          });

  layout->addWidget(mixed, 0, Qt::AlignLeft);
  layout->addWidget(gradientOnly, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* ColorPickerDocsPage::buildTextRenderDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* defaultText = new AdColorPicker();
  defaultText->setCssText(QStringLiteral("#1677ff"));
  defaultText->setTriggerTextVisible(true);
  defaultText->setAllowClear(true);

  auto* customText = new AdColorPicker();
  customText->setCssText(QStringLiteral("#1677ff"));
  customText->setTriggerTextVisible(true);
  customText->setShowTextFormatter([](const ColorValue& value, AdColorPicker::Format, int) {
    if (value.isNone()) {
      return QStringLiteral("Custom Text (none)");
    }
    if (value.isSolid() && value.solidColor.isValid()) {
      return QStringLiteral("Custom Text (%1)").arg(describeColor(value.solidColor));
    }
    return QStringLiteral("Custom Text (gradient)");
  });

  auto* arrowText = new AdColorPicker();
  arrowText->setCssText(QStringLiteral("#1677ff"));
  arrowText->setTriggerTextVisible(true);
  arrowText->setShowTextFormatter([arrowText](const ColorValue&, AdColorPicker::Format, int) {
    return arrowText && arrowText->popupVisible() ? QStringLiteral("^") : QStringLiteral("v");
  });

  layout->addWidget(defaultText, 0, Qt::AlignLeft);
  layout->addWidget(customText, 0, Qt::AlignLeft);
  layout->addWidget(arrowText, 0, Qt::AlignLeft);
  return box;
}

QWidget* ColorPickerDocsPage::buildDisabledDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);

  auto* picker = new AdColorPicker();
  picker->setCssText(QStringLiteral("#1677ff"));
  picker->setTriggerTextVisible(true);
  picker->setDisabled(true);

  row->addWidget(picker);
  row->addStretch();
  return box;
}

QWidget* ColorPickerDocsPage::buildDisabledAlphaDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);

  auto* picker = new AdColorPicker();
  picker->setCssText(QStringLiteral("#1677ff"));
  picker->setAlphaChannelEnabled(false);

  row->addWidget(picker);
  row->addStretch();
  return box;
}

QWidget* ColorPickerDocsPage::buildAllowClearDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* picker = new AdColorPicker();
  picker->setAllowClear(true);
  picker->setValue(solid(QStringLiteral("#1677ff")));

  auto* output = makeHintLabel("value: #1677ff");
  connect(picker, &AdColorPicker::editingFinished, this, [output, picker](const ColorValue&) {
    output->setText(QStringLiteral("value: %1").arg(picker->cssText()));
  });
  connect(picker, &AdColorPicker::cleared, this,
          [output]() { output->setText(QStringLiteral("value: <cleared>")); });

  layout->addWidget(picker, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* ColorPickerDocsPage::buildTriggerDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  auto* picker = new AdColorPicker();
  picker->setValue(solid(QStringLiteral("#1677ff")));

  auto* triggerButton = new DemoColorTrigger();

  auto applyColor = [triggerButton](const ColorValue& value) {
    triggerButton->setColor(selectionPreviewColor(value));
  };
  auto syncTrigger = [picker, applyColor]() {
    if (!picker) {
      return;
    }
    applyColor(picker->value());
  };
  syncTrigger();

  connect(picker, &AdColorPicker::valueChanged, this,
          [applyColor](const ColorValue& value) { applyColor(value); });
  connect(picker, &AdColorPicker::editingFinished, this,
          [syncTrigger](const ColorValue&) { syncTrigger(); });
  connect(picker, &AdColorPicker::popupVisibleChanged, this, [syncTrigger](bool open) {
    if (!open) {
      syncTrigger();
    }
  });

  picker->setTriggerContent(triggerButton);

  row->addWidget(picker, 0, Qt::AlignLeft);
  row->addStretch();
  return box;
}

QWidget* ColorPickerDocsPage::buildTriggerEventDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);

  auto* picker = new AdColorPicker();
  picker->setCssText(QStringLiteral("#1677ff"));
  picker->setTrigger(AdColorPicker::Trigger::Hover);

  row->addWidget(picker);
  row->addWidget(makeHintLabel("Trigger mode: hover"));
  row->addStretch();
  return box;
}

QWidget* ColorPickerDocsPage::buildPopupLayerModeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* modeBox = new QComboBox();
  modeBox->addItem(QStringLiteral("InWindow"),
                   static_cast<int>(AdColorPicker::PopupLayerMode::InWindow));
  modeBox->addItem(QStringLiteral("QtTool"),
                   static_cast<int>(AdColorPicker::PopupLayerMode::QtTool));
  modeBox->setCurrentIndex(1);

  auto* stage = new QFrame();
  stage->setFrameShape(QFrame::StyledPanel);
  stage->setMinimumSize(360, 150);
  auto* stageLayout = new QHBoxLayout(stage);
  stageLayout->setContentsMargins(12, 12, 12, 12);

  auto* clickPicker = new AdColorPicker(stage);
  clickPicker->setCssText(QStringLiteral("#1677ff"));
  clickPicker->setPlacement(AdColorPicker::Placement::BottomRight);
  clickPicker->setPopupLayerMode(AdColorPicker::PopupLayerMode::QtTool);

  auto* hoverPicker = new AdColorPicker(stage);
  hoverPicker->setCssText(QStringLiteral("#52c41a"));
  hoverPicker->setTrigger(AdColorPicker::Trigger::Hover);
  hoverPicker->setPlacement(AdColorPicker::Placement::BottomRight);
  hoverPicker->setPopupLayerMode(AdColorPicker::PopupLayerMode::QtTool);

  stageLayout->addWidget(makeHintLabel("Click"), 0, Qt::AlignBottom);
  stageLayout->addWidget(clickPicker, 0, Qt::AlignBottom);
  stageLayout->addStretch();
  stageLayout->addWidget(makeHintLabel("Hover"), 0, Qt::AlignBottom);
  stageLayout->addWidget(hoverPicker, 0, Qt::AlignRight | Qt::AlignBottom);

  connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), box,
          [modeBox, clickPicker, hoverPicker](int) {
            const auto mode =
                static_cast<AdColorPicker::PopupLayerMode>(modeBox->currentData().toInt());
            clickPicker->setPopupLayerMode(mode);
            hoverPicker->setPopupLayerMode(mode);
          });

  layout->addWidget(modeBox, 0, Qt::AlignLeft);
  layout->addWidget(stage);
  return box;
}

QWidget* ColorPickerDocsPage::buildFormatDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto buildRow = [this](AdColorPicker::Format format, const QString& initial,
                         const QString& prefix) -> QWidget* {
    auto* rowHost = new QWidget();
    auto* row = new QHBoxLayout(rowHost);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    auto* picker = new AdColorPicker();
    picker->setFormat(format);
    picker->setCssText(initial);

    auto* text = new QLabel(QStringLiteral("%1: %2").arg(prefix).arg(picker->displayText()));
    connect(picker, &AdColorPicker::displayTextChanged, rowHost,
            [text, prefix](const QString& formatted) {
              text->setText(QStringLiteral("%1: %2").arg(prefix).arg(formatted));
            });

    row->addWidget(picker);
    row->addWidget(text);
    row->addStretch();
    return rowHost;
  };

  layout->addWidget(
      buildRow(AdColorPicker::Format::Hex, QStringLiteral("#1677ff"), QStringLiteral("HEX")));
  layout->addWidget(buildRow(AdColorPicker::Format::Hsb, QStringLiteral("hsb(215, 91%, 100%)"),
                             QStringLiteral("HSB")));
  layout->addWidget(buildRow(AdColorPicker::Format::Rgb, QStringLiteral("rgb(22, 119, 255)"),
                             QStringLiteral("RGB")));
  return box;
}

QWidget* ColorPickerDocsPage::buildPresetsDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  AdColorPicker::PresetItem primary;
  primary.label = QStringLiteral("primary");
  primary.key = primary.label;
  primary.colors = {
      solid(QStringLiteral("#e6f4ff")), solid(QStringLiteral("#bae0ff")),
      solid(QStringLiteral("#91caff")), solid(QStringLiteral("#69b1ff")),
      solid(QStringLiteral("#4096ff")), solid(QStringLiteral("#1677ff")),
      solid(QStringLiteral("#0958d9")), solid(QStringLiteral("#003eb3")),
      solid(QStringLiteral("#002c8c")), solid(QStringLiteral("#001d66")),
  };

  AdColorPicker::PresetItem red;
  red.label = QStringLiteral("red");
  red.key = red.label;
  red.colors = {
      solid(QStringLiteral("#fff1f0")), solid(QStringLiteral("#ffccc7")),
      solid(QStringLiteral("#ffa39e")), solid(QStringLiteral("#ff7875")),
      solid(QStringLiteral("#ff4d4f")), solid(QStringLiteral("#f5222d")),
      solid(QStringLiteral("#cf1322")), solid(QStringLiteral("#a8071a")),
      solid(QStringLiteral("#820014")), solid(QStringLiteral("#5c0011")),
  };

  AdColorPicker::PresetItem green;
  green.label = QStringLiteral("green");
  green.key = green.label;
  green.colors = {
      solid(QStringLiteral("#f6ffed")), solid(QStringLiteral("#d9f7be")),
      solid(QStringLiteral("#b7eb8f")), solid(QStringLiteral("#95de64")),
      solid(QStringLiteral("#73d13d")), solid(QStringLiteral("#52c41a")),
      solid(QStringLiteral("#389e0d")), solid(QStringLiteral("#237804")),
      solid(QStringLiteral("#135200")), solid(QStringLiteral("#092b00")),
  };

  auto* picker = new AdColorPicker();
  picker->setCssText(QStringLiteral("#1677ff"));
  picker->setPresets({primary, red, green});

  row->addWidget(picker, 0, Qt::AlignLeft);
  row->addStretch();
  return box;
}

QWidget* ColorPickerDocsPage::buildPanelRenderDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* basicState = new adqt::widgets::AdColorPickerState(box);
  basicState->setCssText(QStringLiteral("#1677ff"));
  auto* basicPanel = new adqt::widgets::AdColorPickerPanel();
  basicPanel->setState(basicState);

  AdColorPicker::PresetItem gradients;
  gradients.label = QStringLiteral("presets");
  gradients.colors = {
      solid(QStringLiteral("#1677ff")),
      solid(QStringLiteral("#52c41a")),
      solid(QStringLiteral("#f5222d")),
  };

  auto* presetState = new adqt::widgets::AdColorPickerState(box);
  presetState->setCssText(QStringLiteral("#1677ff"));
  auto* presetPanel = new adqt::widgets::AdColorPickerPanel();
  presetPanel->setState(presetState);
  presetPanel->setPresets({gradients});

  AdColorPicker::ComponentTokens tokens;
  tokens.panelWidth = 480;
  presetPanel->setComponentTokens(tokens);

  auto* firstCard = new QFrame();
  auto* firstLayout = new QVBoxLayout(firstCard);
  firstLayout->setContentsMargins(12, 12, 12, 12);
  firstLayout->setSpacing(8);
  firstLayout->addWidget(new QLabel(QStringLiteral("Inline standalone panel")));
  firstLayout->addWidget(basicPanel, 0, Qt::AlignLeft);

  auto* secondCard = new QFrame();
  auto* secondLayout = new QVBoxLayout(secondCard);
  secondLayout->setContentsMargins(12, 12, 12, 12);
  secondLayout->setSpacing(8);
  secondLayout->addWidget(new QLabel(QStringLiteral("Wider preset panel")));
  secondLayout->addWidget(presetPanel, 0, Qt::AlignLeft);

  layout->addWidget(firstCard);
  layout->addWidget(secondCard);
  return box;
}

QWidget* ColorPickerDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* primary = new AdColorPicker();
  primary->setCssText(QStringLiteral("#1677ff"));
  AdColorPicker::ComponentTokens primaryTokens;
  primaryTokens.triggerBackground = QColor(QStringLiteral("#ffffff"));
  primaryTokens.triggerBorderColor = QColor(QStringLiteral("#34477d"));
  primaryTokens.triggerBorderHoverColor = QColor(QStringLiteral("#1d39c4"));
  primaryTokens.triggerTextColor = QColor(QStringLiteral("#34477d"));
  primaryTokens.panelBackground = QColor(QStringLiteral("#f5f8ff"));
  primaryTokens.panelBorderColor = QColor(QStringLiteral("#34477d"));
  primary->setComponentTokens(primaryTokens);

  auto* accent = new AdColorPicker();
  accent->setCssText(QStringLiteral("#722ed1"));
  accent->setSize(AdColorPicker::Size::Large);
  AdColorPicker::ComponentTokens accentTokens;
  accentTokens.triggerBorderColor = QColor(QStringLiteral("#722ed1"));
  accentTokens.triggerBorderHoverColor = QColor(QStringLiteral("#531dab"));
  accentTokens.triggerTextColor = QColor(QStringLiteral("#531dab"));
  accentTokens.panelBackground = QColor(QStringLiteral("#f9f0ff"));
  accentTokens.panelBorderColor = QColor(QStringLiteral("#722ed1"));
  accentTokens.triggerRadius = 10;
  accent->setComponentTokens(accentTokens);

  row->addWidget(primary);
  row->addWidget(accent);
  row->addStretch();
  return box;
}
