#include "segmented_docs_page.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "antd_icons.h"
#include "demo_theme_utils.h"
#include "widgets/segmented.h"

using adqt::widgets::AdSegmented;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

AdSegmented::Option option(const QString& value, const QString& label, bool enabled = true) {
  AdSegmented::Option item;
  item.value = value;
  item.label = label;
  item.enabled = enabled;
  return item;
}

QWidget* labeledControl(const QString& label, QWidget* control) {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);
  auto* caption = new QLabel(label);
  QFont font = caption->font();
  font.setBold(true);
  caption->setFont(font);
  layout->addWidget(caption);
  layout->addWidget(control, 0, Qt::AlignLeft);
  return box;
}

AdSegmented* dateSegments() {
  auto* segmented = new AdSegmented;
  segmented->addOption(QStringLiteral("Daily"));
  segmented->addOption(QStringLiteral("Weekly"));
  segmented->addOption(QStringLiteral("Monthly"));
  segmented->addOption(QStringLiteral("Quarterly"));
  segmented->addOption(QStringLiteral("Yearly"));
  return segmented;
}

}  // namespace

SegmentedDocsPage::SegmentedDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Segmented"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(QStringLiteral(
      "Display related choices and select one value. The Qt component keeps QVariant values, "
      "native radio accessibility, keyboard traversal, dynamic options, and theme-aware motion."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, QStringLiteral("Basic"),
             QStringLiteral("A compact single-selection control with value and index signals."),
             buildBasicDemo());
  addSection(
      root, QStringLiteral("Direction, distribution, and shape"),
      QStringLiteral("Horizontal and vertical layouts, fill distribution, and round tracks."),
      buildLayoutDemo());
  addSection(root, QStringLiteral("Disabled and size"),
             QStringLiteral("The whole control or individual options can be disabled at any size."),
             buildStateAndSizeDemo());
  addSection(
      root, QStringLiteral("Icons and tooltips"),
      QStringLiteral("Options support icon-and-label or icon-only presentation with tooltips."),
      buildIconDemo());
  addSection(root, QStringLiteral("Dynamic options"),
             QStringLiteral(
                 "Options can be inserted and removed while the selected QVariant remains stable."),
             buildDynamicDemo());
  addSection(root, QStringLiteral("Custom content and tokens"),
             QStringLiteral(
                 "Paint and size callbacks cover custom option content; tokens tune one instance."),
             buildCustomAndTokenDemo());
  root->addStretch();
}

const QVector<QWidget*>& SegmentedDocsPage::sectionAnchors() const { return anchors_; }
const QStringList& SegmentedDocsPage::sectionTitles() const { return titles_; }

void SegmentedDocsPage::addSection(QVBoxLayout* root, const QString& title,
                                   const QString& description, QWidget* content) {
  auto* panel = new QFrame;
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);
  auto* heading = new QLabel(title);
  QFont font = heading->font();
  font.setBold(true);
  font.setPointSize(font.pointSize() + 1);
  heading->setFont(font);
  auto* copy = new QLabel(description);
  copy->setWordWrap(true);
  layout->addWidget(heading);
  layout->addWidget(copy);
  layout->addWidget(content);
  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* SegmentedDocsPage::buildBasicDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  auto* segmented = dateSegments();
  auto* selected = new QLabel(QStringLiteral("Daily"));
  connect(segmented, &AdSegmented::currentValueChanged, selected,
          [selected](const QVariant& value) { selected->setText(value.toString()); });
  layout->addWidget(segmented, 0, Qt::AlignLeft);
  layout->addWidget(selected);
  return box;
}

QWidget* SegmentedDocsPage::buildLayoutDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(14);

  auto* round = dateSegments();
  round->setShape(AdSegmented::Shape::Round);
  layout->addWidget(labeledControl(QStringLiteral("Round"), round));

  auto* fill = new AdSegmented;
  fill->addOption(QStringLiteral("Map"));
  fill->addOption(QStringLiteral("Transit"));
  fill->addOption(QStringLiteral("Satellite"));
  fill->setDistribution(AdSegmented::Distribution::Fill);
  layout->addWidget(labeledControl(QStringLiteral("Fill"), fill));

  auto* vertical = new AdSegmented;
  vertical->setOrientation(Qt::Vertical);
  vertical->addOption(QStringLiteral("Overview"));
  vertical->addOption(QStringLiteral("Details"));
  vertical->addOption(QStringLiteral("Activity"));
  layout->addWidget(labeledControl(QStringLiteral("Vertical"), vertical));
  return box;
}

QWidget* SegmentedDocsPage::buildStateAndSizeDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(14);

  auto* perOption = new AdSegmented;
  perOption->addOption(option(QStringLiteral("daily"), QStringLiteral("Daily")));
  perOption->addOption(option(QStringLiteral("weekly"), QStringLiteral("Weekly"), false));
  perOption->addOption(option(QStringLiteral("monthly"), QStringLiteral("Monthly")));
  layout->addWidget(labeledControl(QStringLiteral("Disabled option"), perOption));

  auto* disabled = dateSegments();
  disabled->setEnabled(false);
  layout->addWidget(labeledControl(QStringLiteral("Disabled control"), disabled));

  auto* sizes = new QWidget;
  auto* row = new QHBoxLayout(sizes);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(16);
  auto* small = dateSegments();
  small->setControlSize(AdSegmented::ControlSize::Small);
  auto* medium = dateSegments();
  auto* large = dateSegments();
  large->setControlSize(AdSegmented::ControlSize::Large);
  row->addWidget(labeledControl(QStringLiteral("Small"), small));
  row->addWidget(labeledControl(QStringLiteral("Medium"), medium));
  row->addWidget(labeledControl(QStringLiteral("Large"), large));
  row->addStretch();
  layout->addWidget(sizes);
  return box;
}

QWidget* SegmentedDocsPage::buildIconDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(14);

  auto makeViewOption = [](const QString& value, const QString& label,
                           const adqt::icons::IconRef& iconRef) {
    AdSegmented::Option item = option(value, label);
    item.icon = iconRef;
    item.tooltip = label.isEmpty() ? value : label;
    return item;
  };

  auto* labeled = new AdSegmented;
  labeled->addOption(
      makeViewOption(QStringLiteral("list"), QStringLiteral("List"), outlined_icons::Bars()));
  labeled->addOption(makeViewOption(QStringLiteral("kanban"), QStringLiteral("Kanban"),
                                    outlined_icons::Appstore()));
  layout->addWidget(labeled, 0, Qt::AlignLeft);

  auto* iconOnly = new AdSegmented;
  iconOnly->addOption(makeViewOption(QStringLiteral("List"), QString(), outlined_icons::Bars()));
  iconOnly->addOption(
      makeViewOption(QStringLiteral("Kanban"), QString(), outlined_icons::Appstore()));
  iconOnly->setShape(AdSegmented::Shape::Round);
  layout->addWidget(iconOnly, 0, Qt::AlignLeft);
  return box;
}

QWidget* SegmentedDocsPage::buildDynamicDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);
  auto* segmented = new AdSegmented;
  segmented->addOption(QStringLiteral("Daily"));
  segmented->addOption(QStringLiteral("Weekly"));
  segmented->addOption(QStringLiteral("Monthly"));
  auto* load = new QPushButton(QStringLiteral("Load more options"));
  connect(load, &QPushButton::clicked, segmented, [segmented, load] {
    segmented->addOption(QStringLiteral("Quarterly"));
    segmented->addOption(QStringLiteral("Yearly"));
    load->setEnabled(false);
  });
  layout->addWidget(segmented, 0, Qt::AlignLeft);
  layout->addWidget(load, 0, Qt::AlignLeft);
  return box;
}

QWidget* SegmentedDocsPage::buildCustomAndTokenDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);

  auto* people = new AdSegmented;
  for (const auto& person :
       {std::pair<QString, QColor>(QStringLiteral("User 1"), QColor("#1677ff")),
        std::pair<QString, QColor>(QStringLiteral("User 2"), QColor("#f56a00")),
        std::pair<QString, QColor>(QStringLiteral("User 3"), QColor("#52c41a"))}) {
    AdSegmented::Option item = option(person.first.toLower().remove(' '), person.first);
    item.data = person.second;
    people->addOption(item);
  }
  people->setItemSizeHintCallback([](const AdSegmented::Option&, AdSegmented::ControlSize,
                                     const QFont&) { return QSize(92, 62); });
  people->setItemPaintCallback([](QPainter& painter, const AdSegmented::ItemPaintInfo& info) {
    const QColor avatarColor = info.option.data.value<QColor>();
    const QRect avatar(info.contentRect.center().x() - 13, info.contentRect.top() + 5, 26, 26);
    painter.setPen(Qt::NoPen);
    painter.setBrush(avatarColor);
    painter.drawEllipse(avatar);
    painter.setPen(Qt::white);
    QFont avatarFont = info.font;
    avatarFont.setBold(true);
    painter.setFont(avatarFont);
    painter.drawText(avatar, Qt::AlignCenter, info.option.label.right(1));
    painter.setFont(info.font);
    painter.setPen(info.foreground);
    painter.drawText(
        QRect(info.contentRect.left(), avatar.bottom() + 3, info.contentRect.width(), 20),
        Qt::AlignHCenter | Qt::AlignTop, info.option.label);
  });
  layout->addWidget(people, 0, Qt::AlignLeft);

  auto* themed = dateSegments();
  AdSegmented::ComponentTokens tokens;
  tokens.colors.trackBackground = QColor(QStringLiteral("#fff1f0"));
  tokens.colors.itemSelectedBackground = QColor(QStringLiteral("#ffccc7"));
  tokens.colors.itemSelectedColor = QColor(QStringLiteral("#a8071a"));
  tokens.colors.itemHoverBackground = QColor(QStringLiteral("#ffccc7"));
  tokens.metrics.horizontalPadding = 18;
  themed->setComponentTokens(tokens);
  layout->addWidget(themed, 0, Qt::AlignLeft);
  return box;
}
