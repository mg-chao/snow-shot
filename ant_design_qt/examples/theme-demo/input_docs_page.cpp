#include "input_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QDynamicPropertyChangeEvent>
#include <QFrame>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "antd_icons.h"
#include "theme/theme_manager.h"

#include <algorithm>

using adqt::widgets::AdButton;
using adqt::widgets::AdComboBox;
using adqt::widgets::AdFieldGroup;
using adqt::widgets::AdLineEdit;
using adqt::widgets::AdOtpEdit;
using adqt::widgets::AdPasswordEdit;
using adqt::widgets::AdSearchEdit;
using adqt::widgets::AdTextEdit;
using adqt::widgets::AdTooltip;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

QPainterPath roundedRectPath(const QRectF& rect, qreal topLeft, qreal topRight, qreal bottomRight,
                             qreal bottomLeft) {
  const qreal w = std::max(rect.width(), 0.0);
  const qreal h = std::max(rect.height(), 0.0);
  const qreal maxRadius = std::min(w, h) / 2.0;

  topLeft = std::clamp(topLeft, 0.0, maxRadius);
  topRight = std::clamp(topRight, 0.0, maxRadius);
  bottomRight = std::clamp(bottomRight, 0.0, maxRadius);
  bottomLeft = std::clamp(bottomLeft, 0.0, maxRadius);

  const qreal left = rect.left();
  const qreal top = rect.top();
  const qreal right = left + rect.width();
  const qreal bottom = top + rect.height();

  QPainterPath path;
  path.moveTo(left + topLeft, top);
  path.lineTo(right - topRight, top);
  if (topRight > 0.0) {
    path.arcTo(QRectF(right - 2.0 * topRight, top, 2.0 * topRight, 2.0 * topRight), 90.0, -90.0);
  }
  path.lineTo(right, bottom - bottomRight);
  if (bottomRight > 0.0) {
    path.arcTo(QRectF(right - 2.0 * bottomRight, bottom - 2.0 * bottomRight, 2.0 * bottomRight,
                      2.0 * bottomRight),
               0.0, -90.0);
  }
  path.lineTo(left + bottomLeft, bottom);
  if (bottomLeft > 0.0) {
    path.arcTo(QRectF(left, bottom - 2.0 * bottomLeft, 2.0 * bottomLeft, 2.0 * bottomLeft), 270.0,
               -90.0);
  }
  path.lineTo(left, top + topLeft);
  if (topLeft > 0.0) {
    path.arcTo(QRectF(left, top, 2.0 * topLeft, 2.0 * topLeft), 180.0, -90.0);
  }
  path.closeSubpath();
  return path;
}

QRectF joinedBorderRect(const QRect& bounds, qreal borderWidth, bool joinedLeft, bool joinedRight) {
  const qreal half = std::max<qreal>(0.0, borderWidth / 2.0);
  qreal leftInset = half + 0.5;
  qreal rightInset = half + 0.5;
  if (joinedLeft) {
    leftInset = half;
  }
  if (joinedRight) {
    rightInset = half;
  }
  return QRectF(bounds).adjusted(leftInset, half + 0.5, -rightInset, -half - 0.5);
}

qreal snapToDevicePixelCoord(qreal value, qreal dpr) {
  if (dpr <= 0.0) {
    return value;
  }
  return qRound(value * dpr) / dpr;
}

QRectF snapRectToDevicePixels(const QRectF& rect, qreal dpr) {
  if (dpr <= 0.0) {
    return rect;
  }
  const qreal left = snapToDevicePixelCoord(rect.left(), dpr);
  const qreal top = snapToDevicePixelCoord(rect.top(), dpr);
  const qreal right = snapToDevicePixelCoord(rect.left() + rect.width(), dpr);
  const qreal bottom = snapToDevicePixelCoord(rect.top() + rect.height(), dpr);
  const qreal minSize = 1.0 / dpr;
  return QRectF(left, top, std::max(minSize, right - left), std::max(minSize, bottom - top));
}

int emojiAwareCount(const QString& text) {
  int count = 0;
  for (int i = 0; i < text.size(); ++i) {
    if (text.at(i).isLowSurrogate()) {
      continue;
    }
    ++count;
  }
  return count;
}

class UppercaseOtpFormatter final : public adqt::widgets::AdOtpCodeFormatter {
 public:
  using adqt::widgets::AdOtpCodeFormatter::AdOtpCodeFormatter;

  QString formatCode(const QString& value) const override { return value.toUpper(); }
};

class AlternatingOtpSeparatorFactory final : public adqt::widgets::AdOtpSeparatorFactory {
 public:
  using adqt::widgets::AdOtpSeparatorFactory::AdOtpSeparatorFactory;

  QWidget* createSeparator(int index, QWidget* parent) const override {
    auto* label = new QLabel(index % 2 == 0 ? QStringLiteral("-") : QStringLiteral("?"), parent);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, index % 2 == 0 ? QColor("#1677ff") : QColor("#ff4d4f"));
    label->setPalette(palette);
    return label;
  }
};

class EmojiCountPolicy : public adqt::widgets::AdInputTextPolicy {
 public:
  using adqt::widgets::AdInputTextPolicy::AdInputTextPolicy;

  int characterCount(const QString& text) const override { return emojiAwareCount(text); }
};

class EmojiClampPolicy final : public EmojiCountPolicy {
 public:
  using EmojiCountPolicy::EmojiCountPolicy;

  QString normalizeText(const QString& text, int maximumCharacterCount) const override {
    if (maximumCharacterCount <= 0) {
      return text;
    }

    QString out;
    out.reserve(text.size());
    int count = 0;
    for (int i = 0; i < text.size() && count < maximumCharacterCount; ++i) {
      const QChar ch = text.at(i);
      out.append(ch);
      if (!ch.isLowSurrogate()) {
        ++count;
      }
    }
    return out;
  }
};

class CompactAddon final : public QWidget {
 public:
  explicit CompactAddon(const QString& text, QWidget* parent = nullptr)
      : QWidget(parent), text_(text) {
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged, this,
            [this]() {
              updateGeometry();
              update();
            });
  }

  void setIconRef(const adqt::icons::IconRef& token) {
    if (iconRef_ == token) {
      return;
    }
    iconRef_ = token;
    updateGeometry();
    update();
  }

  void setSize(AdLineEdit::ControlSize value) {
    if (size_ == value) {
      return;
    }
    size_ = value;
    updateGeometry();
    update();
  }

  void setJoinedLeft(bool value) {
    if (joinedLeft_ == value) {
      return;
    }
    joinedLeft_ = value;
    QWidget::setProperty("joinedLeft", value);
    update();
  }

  void setJoinedRight(bool value) {
    if (joinedRight_ == value) {
      return;
    }
    joinedRight_ = value;
    QWidget::setProperty("joinedRight", value);
    update();
  }

  QSize sizeHint() const override { return minimumSizeHint(); }

  QSize minimumSizeHint() const override {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(this);
    const int height = controlHeight(map);
    const int borderWidth = std::max(1, qRound(map.lineWidth));
    const int iconSide = iconSize(map);
    const int padding = horizontalPadding();

    QFont textFont = addonFont(map);
    QFontMetrics fm(textFont);
    int width = borderWidth * 2 + padding * 2;

    const bool hasIcon = adqt::icons::isValid(iconRef_);
    const bool hasText = !text_.trimmed().isEmpty();
    if (hasIcon) {
      width += iconSide;
    }
    if (hasText) {
      if (hasIcon) {
        width += std::max(4, qRound(map.sizeXS));
      }
      width += fm.horizontalAdvance(text_);
    }
    if (!hasIcon && !hasText) {
      width += std::max(16, iconSide);
    }
    return QSize(std::max(width, height / 2), height);
  }

 protected:
  bool event(QEvent* event) override {
    const bool handled = QWidget::event(event);
    if (!event || event->type() != QEvent::DynamicPropertyChange) {
      return handled;
    }

    const auto* changeEvent = static_cast<QDynamicPropertyChangeEvent*>(event);
    if (changeEvent->propertyName() == "joinedLeft") {
      const bool value = property("joinedLeft").toBool();
      if (joinedLeft_ != value) {
        joinedLeft_ = value;
        update();
      }
    } else if (changeEvent->propertyName() == "joinedRight") {
      const bool value = property("joinedRight").toBool();
      if (joinedRight_ != value) {
        joinedRight_ = value;
        update();
      }
    }
    return handled;
  }

  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event)

    const adqt::theme::ThemeMapToken map = demo::resolveTheme(this);
    const QColor background = demo::themeColorOr(map.colorBgContainerDisabled, QColor("#f5f5f5"));
    const QColor borderColor = demo::themeColorOr(map.colorBorder, QColor("#d9d9d9"));
    const QColor textColor = demo::themeColorOr(map.colorText, QColor(0, 0, 0, 223));
    const int borderWidth = std::max(1, qRound(map.lineWidth));
    const qreal radius = borderRadius(map);

    const QRectF rawBorderRect =
        joinedBorderRect(rect(), static_cast<qreal>(borderWidth), joinedLeft_, joinedRight_);
    const qreal dpr = devicePixelRatioF();
    const QRectF borderRect = snapRectToDevicePixels(rawBorderRect, dpr);
    if (!borderRect.isValid() || borderRect.width() <= 0.0 || borderRect.height() <= 0.0) {
      return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal topLeft = joinedLeft_ ? 0.0 : radius;
    const qreal topRight = joinedRight_ ? 0.0 : radius;
    const qreal bottomRight = joinedRight_ ? 0.0 : radius;
    const qreal bottomLeft = joinedLeft_ ? 0.0 : radius;
    const QPainterPath shellPath =
        roundedRectPath(borderRect, topLeft, topRight, bottomRight, bottomLeft);

    painter.fillPath(shellPath, background);
    QPen borderPen(borderColor, borderWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shellPath);

    const bool hasIcon = adqt::icons::isValid(iconRef_);
    const bool hasText = !text_.trimmed().isEmpty();
    if (!hasIcon && !hasText) {
      return;
    }

    const int iconSide = iconSize(map);
    QFont textFont = addonFont(map);
    painter.setFont(textFont);
    painter.setPen(textColor);
    QFontMetrics fm(textFont);
    const int gap = hasIcon && hasText ? std::max(4, qRound(map.sizeXS)) : 0;
    const int textWidth = hasText ? fm.horizontalAdvance(text_) : 0;
    const int contentWidth = (hasIcon ? iconSide : 0) + gap + textWidth;
    const int startX = qRound(borderRect.left() + (borderRect.width() - contentWidth) / 2.0);
    const int centerY = qRound(borderRect.center().y());

    int cursorX = startX;
    if (hasIcon) {
      const adqt::icons::IconRef icon =
          iconRef_.withColors(adqt::icons::IconColors::primary(textColor));
      const QPixmap iconPixmap =
          adqt::icons::renderIconPixmap(icon, {QSize(iconSide, iconSide), devicePixelRatioF()});
      if (!iconPixmap.isNull()) {
        const int iconY = centerY - iconSide / 2;
        painter.drawPixmap(cursorX, iconY, iconPixmap);
      }
      cursorX += iconSide + gap;
    }

    if (hasText) {
      const QRect textRect(cursorX, qRound(borderRect.top()), textWidth,
                           qRound(borderRect.height()));
      painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text_);
    }
  }

 private:
  int controlHeight(const adqt::theme::ThemeMapToken& map) const {
    switch (size_) {
      case AdLineEdit::ControlSize::Large:
        return std::max(24, qRound(map.controlHeightLG));
      case AdLineEdit::ControlSize::Small:
        return std::max(18, qRound(map.controlHeightSM));
      case AdLineEdit::ControlSize::Medium:
      default:
        return std::max(20, qRound(map.controlHeight));
    }
  }

  qreal borderRadius(const adqt::theme::ThemeMapToken& map) const {
    switch (size_) {
      case AdLineEdit::ControlSize::Large:
        return std::max<qreal>(0.0, map.borderRadiusLG);
      case AdLineEdit::ControlSize::Small:
        return std::max<qreal>(0.0, map.borderRadiusSM);
      case AdLineEdit::ControlSize::Medium:
      default:
        return std::max<qreal>(0.0, map.borderRadius);
    }
  }

  int iconSize(const adqt::theme::ThemeMapToken& map) const {
    switch (size_) {
      case AdLineEdit::ControlSize::Large:
        return std::max(14, qRound(map.fontSizeLG));
      case AdLineEdit::ControlSize::Small:
        return std::max(12, qRound(map.fontSizeSM));
      case AdLineEdit::ControlSize::Medium:
      default:
        return std::max(12, qRound(map.fontSize));
    }
  }

  int horizontalPadding() const {
    switch (size_) {
      case AdLineEdit::ControlSize::Small:
        return 8;
      case AdLineEdit::ControlSize::Large:
      case AdLineEdit::ControlSize::Medium:
      default:
        return 12;
    }
  }

  QFont addonFont(const adqt::theme::ThemeMapToken& map) const {
    QFont result = font();
    int pixelSize = qRound(map.fontSize);
    switch (size_) {
      case AdLineEdit::ControlSize::Large:
        pixelSize = qRound(map.fontSizeLG);
        break;
      case AdLineEdit::ControlSize::Small:
        pixelSize = qRound(map.fontSizeSM);
        break;
      case AdLineEdit::ControlSize::Medium:
      default:
        break;
    }
    result.setPixelSize(std::max(10, pixelSize));
    return result;
  }

  QString text_;
  adqt::icons::IconRef iconRef_;
  AdLineEdit::ControlSize size_ = AdLineEdit::ControlSize::Medium;
  bool joinedLeft_ = false;
  bool joinedRight_ = false;
};

CompactAddon* makeAddonLabel(const QString& text,
                             AdLineEdit::ControlSize size = AdLineEdit::ControlSize::Medium,
                             bool joinedLeft = false, bool joinedRight = false,
                             QWidget* parent = nullptr) {
  auto* addon = new CompactAddon(text, parent);
  addon->setSize(size);
  addon->setJoinedLeft(joinedLeft);
  addon->setJoinedRight(joinedRight);
  return addon;
}

CompactAddon* makeAddonIcon(const adqt::icons::IconRef& token,
                            AdLineEdit::ControlSize size = AdLineEdit::ControlSize::Medium,
                            bool joinedLeft = false, bool joinedRight = false,
                            QWidget* parent = nullptr) {
  auto* addon = new CompactAddon(QString(), parent);
  addon->setSize(size);
  addon->setIconRef(token);
  addon->setJoinedLeft(joinedLeft);
  addon->setJoinedRight(joinedRight);
  return addon;
}

}  // namespace

InputDocsPage::InputDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Input");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "Through mouse or keyboard input content, it is the most basic form field wrapper. "
      "This page mirrors Ant Design Input public demos.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic usage", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Three sizes of Input", "Demo: size.tsx", buildSizeDemo());
  addSection(root, "Variants", "Demo: variant.tsx", buildVariantDemo());
  addSection(root, "Compact Style", "Demo: compact-style.tsx", buildCompactStyleDemo());
  addSection(root, "Search box", "Demo: search-input.tsx", buildSearchInputDemo());
  addSection(root, "Search box with loading", "Demo: search-input-loading.tsx",
             buildSearchLoadingDemo());
  addSection(root, "TextArea", "Demo: textarea.tsx", buildTextAreaDemo());
  addSection(root, "Autosizing the height to fit the content", "Demo: autosize-textarea.tsx",
             buildAutoSizeTextAreaDemo());
  addSection(root, "OTP", "Demo: otp.tsx", buildOtpDemo());
  addSection(root, "Format Tooltip Input", "Demo: tooltip.tsx", buildTooltipDemo());
  addSection(root, "prefix and suffix", "Demo: presuffix.tsx", buildPreSuffixDemo());
  addSection(root, "Password box", "Demo: password-input.tsx", buildPasswordDemo());
  addSection(root, "With clear icon", "Demo: clear-button", buildClearButtonDemo());
  addSection(root, "With character counting", "Demo: show-count.tsx", buildShowCountDemo());
  addSection(root, "Custom count logic", "Demo: advance-count.tsx", buildAdvanceCountDemo());
  addSection(root, "Status", "Demo: status.tsx", buildStatusDemo());
  addSection(root, "Focus", "Demo: focus.tsx", buildFocusDemo());
  addSection(root, "Qt-native customization hooks", "Demo: native-hooks.tsx",
             buildStyleClassDemo());

  root->addStretch();
}

const QVector<QWidget*>& InputDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& InputDocsPage::sectionTitles() const { return titles_; }

void InputDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
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

QWidget* InputDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);

  auto* input = new AdLineEdit();
  input->setPlaceholderText("Basic usage");
  input->setFixedWidth(280);

  row->addWidget(input);
  row->addStretch();
  return box;
}

QWidget* InputDocsPage::buildSizeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* large = new AdLineEdit();
  large->setControlSize(AdLineEdit::ControlSize::Large);
  large->setPlaceholderText("large size");
  large->setPrefixIconRef(outlined_icons::User());
  large->setFixedWidth(320);

  auto* medium = new AdLineEdit();
  medium->setPlaceholderText("medium size");
  medium->setPrefixIconRef(outlined_icons::User());
  medium->setFixedWidth(320);

  auto* small = new AdLineEdit();
  small->setControlSize(AdLineEdit::ControlSize::Small);
  small->setPlaceholderText("small size");
  small->setPrefixIconRef(outlined_icons::User());
  small->setFixedWidth(320);

  layout->addWidget(large, 0, Qt::AlignLeft);
  layout->addWidget(medium, 0, Qt::AlignLeft);
  layout->addWidget(small, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildVariantDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* outlined = new AdLineEdit();
  outlined->setPlaceholderText("Outlined");

  auto* filled = new AdLineEdit();
  filled->setPlaceholderText("Filled");
  filled->setVariant(AdLineEdit::Variant::Filled);

  auto* borderless = new AdLineEdit();
  borderless->setPlaceholderText("Borderless");
  borderless->setVariant(AdLineEdit::Variant::Borderless);

  auto* underlined = new AdLineEdit();
  underlined->setPlaceholderText("Underlined");
  underlined->setVariant(AdLineEdit::Variant::Underlined);

  auto* searchFilled = new AdSearchEdit();
  searchFilled->setPlaceholderText("Filled");
  searchFilled->setVariant(AdLineEdit::Variant::Filled);
  searchFilled->setFixedWidth(320);

  layout->addWidget(outlined, 0, Qt::AlignLeft);
  layout->addWidget(filled, 0, Qt::AlignLeft);
  layout->addWidget(borderless, 0, Qt::AlignLeft);
  layout->addWidget(underlined, 0, Qt::AlignLeft);
  layout->addWidget(searchFilled, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildCompactStyleDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);

  {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto* input = new AdLineEdit();
    input->setText("26888888");
    input->setFixedWidth(300);
    row->addWidget(input);
    row->addStretch();
    layout->addLayout(row);
  }

  {
    auto* group = new AdFieldGroup();
    auto* area = new AdLineEdit();
    area->setText("0571");
    area->setFixedWidth(115);

    auto* phone = new AdLineEdit();
    phone->setText("26888888");
    phone->setFixedWidth(460);

    group->addControl(area);
    group->addControl(phone);
    layout->addWidget(group, 0, Qt::AlignLeft);
  }

  {
    auto* group = new AdFieldGroup();
    auto* addon = makeAddonLabel("https://");
    auto* search = new AdSearchEdit();
    search->setPlaceholderText("input search text");
    search->setAllowClear(true);
    search->setFixedWidth(400);

    group->addControl(addon);
    group->addControl(search);
    layout->addWidget(group, 0, Qt::AlignLeft);
  }

  {
    auto* group = new AdFieldGroup();
    auto* input = new AdLineEdit();
    input->setText("Combine input and button");
    input->setFixedWidth(470);

    auto* submit = new AdButton("Submit");
    submit->setButtonStyle(AdButton::ButtonStyle::Solid);
    submit->setAccentRole(AdButton::AccentRole::Primary);

    group->addControl(input);
    group->addControl(submit);
    layout->addWidget(group, 0, Qt::AlignLeft);
  }

  {
    auto* group = new AdFieldGroup();
    auto* select = new AdComboBox();
    select->setFixedWidth(160);
    select->setOptions({
        {"zhejiang", "Zhejiang", false, QString(), {}},
        {"jiangsu", "Jiangsu", false, QString(), {}},
    });
    select->setCurrentValue(QStringLiteral("zhejiang"));

    auto* input = new AdLineEdit();
    input->setText("Xihu District, Hangzhou");
    input->setFixedWidth(290);

    group->addControl(select);
    group->addControl(input);
    layout->addWidget(group, 0, Qt::AlignLeft);
  }

  {
    auto* group = new AdFieldGroup();
    auto* addon = makeAddonIcon(outlined_icons::Search(), AdLineEdit::ControlSize::Large);

    auto* left = new AdLineEdit();
    left->setControlSize(AdLineEdit::ControlSize::Large);
    left->setPlaceholderText("large size");
    left->setFixedWidth(255);

    auto* right = new AdLineEdit();
    right->setControlSize(AdLineEdit::ControlSize::Large);
    right->setPlaceholderText("another input");
    right->setFixedWidth(255);

    group->addControl(addon);
    group->addControl(left);
    group->addControl(right);
    layout->addWidget(group, 0, Qt::AlignLeft);
  }

  return box;
}

QWidget* InputDocsPage::buildSearchInputDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* info = makeHintLabel("onSearch source/value will appear here.");

  auto makeSearch = [info](bool allowClear, bool searchButtonVisible, const QString& buttonText,
                           AdLineEdit::ControlSize size = AdLineEdit::ControlSize::Medium) {
    auto* search = new AdSearchEdit();
    search->setPlaceholderText("input search text");
    search->setAllowClear(allowClear);
    search->setSearchButtonText(searchButtonVisible
                                    ? (buttonText.isEmpty() ? QStringLiteral("Search") : buttonText)
                                    : QString());
    search->setControlSize(size);
    search->setFixedWidth(320);
    QObject::connect(
        search, &AdSearchEdit::searchRequested, info,
        [info](const QString& value, AdSearchEdit::SearchReason reason) {
          QString src;
          switch (reason) {
            case AdSearchEdit::SearchReason::ReturnKey:
              src = QStringLiteral("returnKey");
              break;
            case AdSearchEdit::SearchReason::ButtonClick:
              src = QStringLiteral("buttonClick");
              break;
            case AdSearchEdit::SearchReason::ClearAction:
              src = QStringLiteral("clearAction");
              break;
          }
          info->setText(QStringLiteral("searchRequested source=%1 value=%2").arg(src, value));
        });
    return search;
  };

  layout->addWidget(makeSearch(false, false, QString()), 0, Qt::AlignLeft);
  layout->addWidget(makeSearch(true, false, QString()), 0, Qt::AlignLeft);

  {
    auto* group = new AdFieldGroup();
    auto* addon = makeAddonLabel("https://");
    auto* search = makeSearch(true, false, QString());
    group->addControl(addon);
    group->addControl(search);
    layout->addWidget(group, 0, Qt::AlignLeft);
  }

  layout->addWidget(makeSearch(false, true, QString()), 0, Qt::AlignLeft);
  layout->addWidget(
      makeSearch(true, true, QStringLiteral("Search"), AdLineEdit::ControlSize::Large), 0,
      Qt::AlignLeft);

  auto* suffixSearch =
      makeSearch(false, true, QStringLiteral("Search"), AdLineEdit::ControlSize::Large);
  suffixSearch->setSuffixIconRef(outlined_icons::Audio());
  layout->addWidget(suffixSearch, 0, Qt::AlignLeft);

  layout->addWidget(info);
  return box;
}

QWidget* InputDocsPage::buildSearchLoadingDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* s1 = new AdSearchEdit();
  s1->setPlaceholderText("input search loading default");
  s1->setBusy(true);
  s1->setFixedWidth(320);

  auto* s2 = new AdSearchEdit();
  s2->setPlaceholderText("input search loading with search button");
  s2->setSearchButtonText(QStringLiteral("Search"));
  s2->setBusy(true);
  s2->setFixedWidth(320);

  auto* s3 = new AdSearchEdit();
  s3->setPlaceholderText("input search text");
  s3->setSearchButtonText(QStringLiteral("Search"));
  s3->setControlSize(AdLineEdit::ControlSize::Large);
  s3->setBusy(true);
  s3->setFixedWidth(320);

  layout->addWidget(s1, 0, Qt::AlignLeft);
  layout->addWidget(s2, 0, Qt::AlignLeft);
  layout->addWidget(s3, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildTextAreaDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* first = new AdTextEdit();
  first->setHeightMode(AdTextEdit::HeightMode::FixedRows);
  first->setMinimumVisibleRows(4);
  first->setMaximumVisibleRows(4);
  first->setFixedWidth(420);

  auto* second = new AdTextEdit();
  second->setPlaceholderText("maxLength is 6");
  second->setMaxLength(6);
  second->setHeightMode(AdTextEdit::HeightMode::FixedRows);
  second->setMinimumVisibleRows(4);
  second->setMaximumVisibleRows(4);
  second->setFixedWidth(420);

  layout->addWidget(first, 0, Qt::AlignLeft);
  layout->addWidget(second, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildAutoSizeTextAreaDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* first = new AdTextEdit();
  first->setPlaceholderText("Autosize height based on content lines");
  first->setHeightMode(AdTextEdit::HeightMode::AutoGrow);
  first->setMinimumVisibleRows(2);
  first->setMaximumVisibleRows(6);
  first->setFixedWidth(460);

  auto* second = new AdTextEdit();
  second->setPlaceholderText("Autosize height with minimum and maximum number of lines");
  second->setHeightMode(AdTextEdit::HeightMode::AutoGrow);
  second->setMinimumVisibleRows(2);
  second->setMaximumVisibleRows(6);
  second->setFixedWidth(460);

  auto* third = new AdTextEdit();
  third->setPlaceholderText("Controlled autosize");
  third->setPlainText("Type to grow");
  third->setHeightMode(AdTextEdit::HeightMode::AutoGrow);
  third->setMinimumVisibleRows(3);
  third->setMaximumVisibleRows(5);
  third->setFixedWidth(460);

  layout->addWidget(first, 0, Qt::AlignLeft);
  layout->addWidget(second, 0, Qt::AlignLeft);
  layout->addWidget(third, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildOtpDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* title1 = new QLabel("With formatter (Upcase)");
  QFont titleFont = title1->font();
  titleFont.setBold(true);
  title1->setFont(titleFont);
  layout->addWidget(title1, 0, Qt::AlignLeft);

  auto* upcase = new AdOtpEdit();
  upcase->setCodeFormatter(new UppercaseOtpFormatter(upcase));
  layout->addWidget(upcase, 0, Qt::AlignLeft);

  layout->addWidget(new QLabel("With Disabled"), 0, Qt::AlignLeft);
  auto* disabled = new AdOtpEdit();
  disabled->setDisabled(true);
  layout->addWidget(disabled, 0, Qt::AlignLeft);

  layout->addWidget(new QLabel("With Length (8)"), 0, Qt::AlignLeft);
  auto* length8 = new AdOtpEdit();
  length8->setCellCount(8);
  layout->addWidget(length8, 0, Qt::AlignLeft);

  layout->addWidget(new QLabel("With variant"), 0, Qt::AlignLeft);
  auto* filled = new AdOtpEdit();
  filled->setVariant(AdLineEdit::Variant::Filled);
  layout->addWidget(filled, 0, Qt::AlignLeft);

  layout->addWidget(new QLabel("With custom display character"), 0, Qt::AlignLeft);
  auto* masked = new AdOtpEdit();
  masked->setMaskInput(true);
  masked->setMaskCharacter("*");
  layout->addWidget(masked, 0, Qt::AlignLeft);

  layout->addWidget(new QLabel("With custom separator"), 0, Qt::AlignLeft);
  auto* separatorText = new AdOtpEdit();
  separatorText->setSeparatorText("/");
  layout->addWidget(separatorText, 0, Qt::AlignLeft);

  layout->addWidget(new QLabel("With custom function separator"), 0, Qt::AlignLeft);
  auto* separatorFn = new AdOtpEdit();
  separatorFn->setSeparatorFactory(new AlternatingOtpSeparatorFactory(separatorFn));
  layout->addWidget(separatorFn, 0, Qt::AlignLeft);

  auto* output = makeHintLabel("cellsChanged / codeCompleted output will appear here.");
  const auto bindOutput = [output](AdOtpEdit* otp) {
    QObject::connect(otp, &AdOtpEdit::cellsChanged, output, [output](const QStringList& parts) {
      output->setText(QStringLiteral("cellsChanged: [%1]").arg(parts.join(",")));
    });
    QObject::connect(otp, &AdOtpEdit::codeCompleted, output, [output](const QString& text) {
      output->setText(QStringLiteral("codeCompleted: %1").arg(text));
    });
  };

  bindOutput(upcase);
  bindOutput(disabled);
  bindOutput(length8);
  bindOutput(filled);
  bindOutput(masked);
  bindOutput(separatorText);
  bindOutput(separatorFn);

  layout->addWidget(output);
  return box;
}

QWidget* InputDocsPage::buildTooltipDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* tooltip = new AdTooltip(box);
  tooltip->setPlacement(AdTooltip::Placement::TopLeft);
  tooltip->setTriggers(AdTooltip::Trigger::Focus);
  tooltip->setText("Input a number");

  auto* input = new AdLineEdit(box);
  input->setPlaceholderText("Input a number");
  input->setFixedWidth(180);
  tooltip->setTargetWidget(input);

  auto* hint = makeHintLabel("Focus the input to see formatted value tooltip.");
  connect(input, &AdLineEdit::textChanged, tooltip, [tooltip](const QString& text) {
    if (text.trimmed().isEmpty()) {
      tooltip->setText("Input a number");
      return;
    }
    bool ok = false;
    const double value = text.toDouble(&ok);
    tooltip->setText(ok ? QString::number(value, 'f', 2) : text);
  });

  layout->addWidget(input, 0, Qt::AlignLeft);
  layout->addWidget(hint);
  return box;
}

QWidget* InputDocsPage::buildPreSuffixDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* first = new AdLineEdit();
  first->setPlaceholderText("Enter your username");
  first->setPrefixIconRef(outlined_icons::User());
  first->setSuffixIconRef(outlined_icons::InfoCircle());
  first->setFixedWidth(360);

  auto* second = new AdLineEdit();
  second->setPrefixText(QString(QChar(0xFFE5)));
  second->setSuffixText("RMB");
  second->setFixedWidth(240);

  auto* third = new AdLineEdit();
  third->setPrefixText(QString(QChar(0xFFE5)));
  third->setSuffixText("RMB");
  third->setDisabled(true);
  third->setFixedWidth(240);

  auto* pwd = new AdPasswordEdit();
  pwd->setPlaceholderText("input password support suffix");
  pwd->setSuffixIconRef(outlined_icons::Lock());
  pwd->setFixedWidth(360);

  layout->addWidget(first, 0, Qt::AlignLeft);
  layout->addWidget(second, 0, Qt::AlignLeft);
  layout->addWidget(third, 0, Qt::AlignLeft);
  layout->addWidget(pwd, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildPasswordDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* p1 = new AdPasswordEdit();
  p1->setPlaceholderText("input password");
  p1->setFixedWidth(300);

  auto* p2 = new AdPasswordEdit();
  p2->setPlaceholderText("input password");
  p2->setVisibleIconRef(outlined_icons::Eye());
  p2->setHiddenIconRef(outlined_icons::EyeInvisible());
  p2->setFixedWidth(300);

  auto* row = new QHBoxLayout();
  auto* p3 = new AdPasswordEdit();
  p3->setPlaceholderText("input password");
  p3->setFixedWidth(300);

  auto* toggle = new AdButton("Show");
  toggle->setFixedWidth(90);
  QObject::connect(toggle, &QAbstractButton::clicked, p3, [p3, toggle]() {
    p3->setTextVisible(!p3->textVisible());
    toggle->setText(p3->textVisible() ? QStringLiteral("Hide") : QStringLiteral("Show"));
  });

  row->addWidget(p3);
  row->addWidget(toggle);
  row->addStretch();

  auto* disabled = new AdPasswordEdit();
  disabled->setPlaceholderText("disabled input password");
  disabled->setDisabled(true);
  disabled->setFixedWidth(300);

  layout->addWidget(p1, 0, Qt::AlignLeft);
  layout->addWidget(p2, 0, Qt::AlignLeft);
  layout->addLayout(row);
  layout->addWidget(disabled, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildClearButtonDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* input = new AdLineEdit();
  input->setPlaceholderText("input with clear icon");
  input->setAllowClear(true);
  input->setFixedWidth(320);

  auto* textArea = new AdTextEdit();
  textArea->setPlaceholderText("textarea with clear icon");
  textArea->setAllowClear(true);
  textArea->setHeightMode(AdTextEdit::HeightMode::AutoGrow);
  textArea->setMinimumVisibleRows(3);
  textArea->setMaximumVisibleRows(4);
  textArea->setFixedWidth(420);

  auto* output = makeHintLabel("Change events will appear here.");
  connect(input, &AdLineEdit::textChanged, output, [output](const QString& text) {
    output->setText(QStringLiteral("Input changed: %1").arg(text));
  });
  connect(textArea, &AdTextEdit::plainTextChanged, output, [output](const QString& text) {
    output->setText(QStringLiteral("TextArea changed: %1").arg(text));
  });

  layout->addWidget(input, 0, Qt::AlignLeft);
  layout->addWidget(textArea, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* InputDocsPage::buildShowCountDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(32);

  auto* input = new AdLineEdit();
  input->setCountVisible(true);
  input->setMaxLength(20);
  input->setFixedWidth(420);

  auto* textArea1 = new AdTextEdit();
  textArea1->setCountVisible(true);
  textArea1->setMaxLength(100);
  textArea1->setPlaceholderText("can resize");
  textArea1->setHeightMode(AdTextEdit::HeightMode::FixedRows);
  textArea1->setFixedWidth(420);

  auto* textArea2 = new AdTextEdit();
  textArea2->setCountVisible(true);
  textArea2->setMaxLength(100);
  textArea2->setPlaceholderText("disable resize");
  textArea2->setHeightMode(AdTextEdit::HeightMode::FixedRows);
  textArea2->setMinimumVisibleRows(4);
  textArea2->setMaximumVisibleRows(4);
  textArea2->setFixedWidth(420);

  layout->addWidget(input, 0, Qt::AlignLeft);
  layout->addWidget(textArea1, 0, Qt::AlignLeft);
  layout->addWidget(textArea2, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildAdvanceCountDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  layout->addWidget(new QLabel("Exceed Max"), 0, Qt::AlignLeft);
  auto* exceed = new AdLineEdit();
  exceed->setCountVisible(true);
  exceed->setMaximumCharacterCount(10);
  exceed->setText("Hello, antd!");
  exceed->setFixedWidth(320);
  layout->addWidget(exceed, 0, Qt::AlignLeft);

  layout->addWidget(new QLabel("Emoji count as length 1"), 0, Qt::AlignLeft);
  auto* emoji = new AdLineEdit();
  emoji->setCountVisible(true);
  emoji->setTextPolicy(new EmojiCountPolicy(emoji));
  emoji->setText("??????");
  emoji->setFixedWidth(320);
  layout->addWidget(emoji, 0, Qt::AlignLeft);

  layout->addWidget(new QLabel("Not exceed max"), 0, Qt::AlignLeft);
  auto* notExceed = new AdLineEdit();
  notExceed->setCountVisible(true);
  notExceed->setMaximumCharacterCount(6);
  notExceed->setTextPolicy(new EmojiClampPolicy(notExceed));
  notExceed->setText("?? antd");
  notExceed->setFixedWidth(320);
  layout->addWidget(notExceed, 0, Qt::AlignLeft);

  return box;
}

QWidget* InputDocsPage::buildStatusDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* e1 = new AdLineEdit();
  e1->setStatus(AdLineEdit::Status::Error);
  e1->setPlaceholderText("Error");

  auto* w1 = new AdLineEdit();
  w1->setStatus(AdLineEdit::Status::Warning);
  w1->setPlaceholderText("Warning");

  auto* e2 = new AdLineEdit();
  e2->setStatus(AdLineEdit::Status::Error);
  e2->setPrefixIconRef(outlined_icons::ClockCircle());
  e2->setPlaceholderText("Error with prefix");

  auto* w2 = new AdLineEdit();
  w2->setStatus(AdLineEdit::Status::Warning);
  w2->setPrefixIconRef(outlined_icons::ClockCircle());
  w2->setPlaceholderText("Warning with prefix");

  layout->addWidget(e1, 0, Qt::AlignLeft);
  layout->addWidget(w1, 0, Qt::AlignLeft);
  layout->addWidget(e2, 0, Qt::AlignLeft);
  layout->addWidget(w2, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildFocusDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* controls = new QHBoxLayout();
  controls->setSpacing(8);

  auto* stack = new QStackedWidget();
  auto* input = new AdLineEdit();
  input->setText("Ant Design love you!");
  input->setFixedWidth(420);
  auto* inputPage = new QWidget();
  auto* inputPageLayout = new QVBoxLayout(inputPage);
  inputPageLayout->setContentsMargins(0, 0, 0, 0);
  inputPageLayout->setSpacing(0);
  inputPageLayout->addWidget(input, 0, Qt::AlignLeft | Qt::AlignTop);
  inputPageLayout->addStretch();

  auto* textArea = new AdTextEdit();
  textArea->setPlainText("Ant Design love you!");
  textArea->setHeightMode(AdTextEdit::HeightMode::FixedRows);
  textArea->setMinimumVisibleRows(3);
  textArea->setMaximumVisibleRows(3);
  textArea->setFixedWidth(420);
  auto* textAreaPage = new QWidget();
  auto* textAreaPageLayout = new QVBoxLayout(textAreaPage);
  textAreaPageLayout->setContentsMargins(0, 0, 0, 0);
  textAreaPageLayout->setSpacing(0);
  textAreaPageLayout->addWidget(textArea, 0, Qt::AlignLeft | Qt::AlignTop);
  textAreaPageLayout->addStretch();

  stack->addWidget(inputPage);
  stack->addWidget(textAreaPage);

  auto* focusStart = new AdButton("Focus at first");
  auto* focusEnd = new AdButton("Focus at last");
  auto* focusAll = new AdButton("Focus to select all");
  auto* focusPrevent = new AdButton("Focus prevent scroll");
  auto* toggle = new QCheckBox("TextArea");

  controls->addWidget(focusStart);
  controls->addWidget(focusEnd);
  controls->addWidget(focusAll);
  controls->addWidget(focusPrevent);
  controls->addWidget(toggle);
  controls->addStretch();

  auto focusCurrent = [stack, input, textArea](AdLineEdit::FocusSelection cursor,
                                               bool preventScroll) {
    if (stack->currentIndex() == 0) {
      input->focusEditor(cursor, preventScroll);
    } else {
      textArea->focusEditor(cursor, preventScroll);
    }
  };

  connect(focusStart, &QAbstractButton::clicked, box,
          [focusCurrent]() { focusCurrent(AdLineEdit::FocusSelection::Start, false); });
  connect(focusEnd, &QAbstractButton::clicked, box,
          [focusCurrent]() { focusCurrent(AdLineEdit::FocusSelection::End, false); });
  connect(focusAll, &QAbstractButton::clicked, box,
          [focusCurrent]() { focusCurrent(AdLineEdit::FocusSelection::SelectAll, false); });
  connect(focusPrevent, &QAbstractButton::clicked, box,
          [focusCurrent]() { focusCurrent(AdLineEdit::FocusSelection::Preserve, true); });
  connect(toggle, &QCheckBox::toggled, stack,
          [stack](bool checked) { stack->setCurrentIndex(checked ? 1 : 0); });

  layout->addLayout(controls);
  layout->addWidget(stack, 0, Qt::AlignLeft);
  return box;
}

QWidget* InputDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* inputObject = new AdLineEdit();
  inputObject->setPlaceholderText("Outlined with prefix");
  inputObject->setPrefixIconRef(outlined_icons::User());
  inputObject->setStatus(AdLineEdit::Status::Warning);
  inputObject->setFixedWidth(320);

  auto* inputFn = new AdLineEdit();
  inputFn->setPlaceholderText("Filled + clear");
  inputFn->setVariant(AdLineEdit::Variant::Filled);
  inputFn->setAllowClear(true);
  inputFn->setFixedWidth(320);

  auto* textArea = new AdTextEdit();
  textArea->setPlainText("TextArea");
  textArea->setCountVisible(true);
  textArea->setHeightMode(AdTextEdit::HeightMode::FixedRows);
  textArea->setMinimumVisibleRows(2);
  textArea->setMaximumVisibleRows(2);
  textArea->setStatus(AdLineEdit::Status::Warning);
  textArea->setFixedWidth(420);

  auto* password = new AdPasswordEdit();
  password->setText("Password");
  password->setRevealActionVisible(true);
  password->setSuffixIconRef(outlined_icons::Lock());
  password->setFixedWidth(320);

  auto* otp = new AdOtpEdit();
  otp->setCellCount(6);
  otp->setSeparatorText("*");
  otp->setCodeFormatter(new UppercaseOtpFormatter(otp));

  auto* search = new AdSearchEdit();
  search->setPlaceholderText("Search");
  search->setControlSize(AdLineEdit::ControlSize::Large);
  search->setSearchButtonText(QStringLiteral("Search"));
  search->setVariant(AdLineEdit::Variant::Filled);
  search->setAllowClear(true);
  search->setFixedWidth(360);

  layout->addWidget(inputObject, 0, Qt::AlignLeft);
  layout->addWidget(inputFn, 0, Qt::AlignLeft);
  layout->addWidget(textArea, 0, Qt::AlignLeft);
  layout->addWidget(password, 0, Qt::AlignLeft);
  layout->addWidget(otp, 0, Qt::AlignLeft);
  layout->addWidget(search, 0, Qt::AlignLeft);
  return box;
}
