#include "descriptions_docs_page.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "widgets/button.h"
#include "widgets/descriptions.h"
#include "widgets/tag.h"

using adqt::widgets::AdButton;
using adqt::widgets::AdDescriptions;
using adqt::widgets::AdTag;

namespace {

AdDescriptions::Item makeItem(const QString& key, const QString& label, const QString& content,
                              int span = 1) {
  AdDescriptions::Item item;
  item.key = key;
  item.label = label;
  item.content = content;
  item.span = span;
  return item;
}

void addAccountItems(AdDescriptions* descriptions) {
  descriptions->addItem(makeItem(QStringLiteral("product"), QStringLiteral("Product"),
                                 QStringLiteral("Cloud Database")));
  descriptions->addItem(makeItem(QStringLiteral("billing"), QStringLiteral("Billing Mode"),
                                 QStringLiteral("Prepaid")));
  descriptions->addItem(makeItem(QStringLiteral("renewal"), QStringLiteral("Automatic Renewal"),
                                 QStringLiteral("Yes")));
  descriptions->addItem(makeItem(QStringLiteral("order"), QStringLiteral("Order Time"),
                                 QStringLiteral("2018-04-24 18:00:00")));
  descriptions->addItem(makeItem(QStringLiteral("usage"), QStringLiteral("Usage Time"),
                                 QStringLiteral("2019-04-24 18:00:00")));
  descriptions->addItem(
      makeItem(QStringLiteral("status"), QStringLiteral("Status"), QStringLiteral("Running")));
}

QWidget* labeled(const QString& label, QWidget* content) {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  auto* heading = new QLabel(label);
  QFont font = heading->font();
  font.setBold(true);
  heading->setFont(font);
  layout->addWidget(heading);
  layout->addWidget(content);
  return box;
}

}  // namespace

DescriptionsDocsPage::DescriptionsDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Descriptions"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(QStringLiteral(
      "Display multiple read-only fields as a compact, scan-friendly group. Items support stable "
      "keys, spans, fill-row behavior, custom widgets, responsive columns, and theme tokens."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(
      root, QStringLiteral("Basic"),
      QStringLiteral("A title, extra action, and the default responsive three-column layout."),
      buildBasicDemo());
  addSection(root, QStringLiteral("Border and size"),
             QStringLiteral("Bordered tables and default, middle, or small density match Ant "
                            "Design's padding scale."),
             buildBorderAndSizeDemo());
  addSection(root, QStringLiteral("Vertical"),
             QStringLiteral("Labels occupy their own row above the corresponding content cells."),
             buildVerticalDemo());
  addSection(
      root, QStringLiteral("Span and fill row"),
      QStringLiteral(
          "Items can consume multiple columns or the unoccupied remainder of the current row."),
      buildSpanDemo());
  addSection(root, QStringLiteral("Responsive and custom content"),
             QStringLiteral("The default breakpoints resolve to one, two, or three columns; "
                            "arbitrary Qt widgets can fill semantic slots."),
             buildResponsiveAndCustomDemo());
  addSection(
      root, QStringLiteral("Component tokens"),
      QStringLiteral(
          "Per-instance tokens overlay the active light, dark, comfortable, or compact theme."),
      buildTokenDemo());
  root->addStretch();
}

const QVector<QWidget*>& DescriptionsDocsPage::sectionAnchors() const { return anchors_; }
const QStringList& DescriptionsDocsPage::sectionTitles() const { return titles_; }

void DescriptionsDocsPage::addSection(QVBoxLayout* root, const QString& title,
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

QWidget* DescriptionsDocsPage::buildBasicDemo() {
  auto* descriptions = new AdDescriptions;
  descriptions->setTitle(QStringLiteral("User Info"));
  descriptions->setExtra(QStringLiteral("Edit"));
  addAccountItems(descriptions);
  return descriptions;
}

QWidget* DescriptionsDocsPage::buildBorderAndSizeDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(18);
  const QVector<QPair<QString, AdDescriptions::Size>> sizes{
      {QStringLiteral("Default"), AdDescriptions::Size::Default},
      {QStringLiteral("Middle"), AdDescriptions::Size::Middle},
      {QStringLiteral("Small"), AdDescriptions::Size::Small}};
  for (const auto& entry : sizes) {
    auto* descriptions = new AdDescriptions;
    descriptions->setBordered(true);
    descriptions->setSize(entry.second);
    descriptions->setColumn(3);
    addAccountItems(descriptions);
    layout->addWidget(labeled(entry.first, descriptions));
  }
  return box;
}

QWidget* DescriptionsDocsPage::buildVerticalDemo() {
  auto* descriptions = new AdDescriptions;
  descriptions->setTitle(QStringLiteral("User Info"));
  descriptions->setBordered(true);
  descriptions->setLayoutMode(AdDescriptions::LayoutMode::Vertical);
  descriptions->setColumn(3);
  addAccountItems(descriptions);
  return descriptions;
}

QWidget* DescriptionsDocsPage::buildSpanDemo() {
  auto* descriptions = new AdDescriptions;
  descriptions->setTitle(QStringLiteral("Order Details"));
  descriptions->setBordered(true);
  descriptions->setColumn(3);
  descriptions->addItem(makeItem(QStringLiteral("product"), QStringLiteral("Product"),
                                 QStringLiteral("Cloud Database")));
  descriptions->addItem(makeItem(QStringLiteral("billing"), QStringLiteral("Billing Mode"),
                                 QStringLiteral("Prepaid")));
  descriptions->addItem(makeItem(QStringLiteral("renewal"), QStringLiteral("Automatic Renewal"),
                                 QStringLiteral("Yes")));
  descriptions->addItem(makeItem(QStringLiteral("time"), QStringLiteral("Order Time"),
                                 QStringLiteral("2018-04-24 18:00:00"), 2));
  descriptions->addItem(
      makeItem(QStringLiteral("status"), QStringLiteral("Status"), QStringLiteral("Running")));
  AdDescriptions::Item address =
      makeItem(QStringLiteral("address"), QStringLiteral("Address"),
               QStringLiteral("No. 18, Wantang Road, Xihu District, Hangzhou, Zhejiang, China"));
  address.fillRemaining = true;
  descriptions->addItem(address);
  return descriptions;
}

QWidget* DescriptionsDocsPage::buildResponsiveAndCustomDemo() {
  auto* box = new QWidget;
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(18);

  auto* responsive = new AdDescriptions;
  responsive->setTitle(QStringLiteral("Responsive fields"));
  addAccountItems(responsive);
  layout->addWidget(responsive);

  auto* custom = new AdDescriptions;
  custom->setTitle(QStringLiteral("Service"));
  custom->setBordered(true);
  custom->setColumn(2);
  auto* action = new AdButton(QStringLiteral("Manage"));
  action->setButtonStyle(AdButton::ButtonStyle::Link);
  custom->setExtraWidget(action);
  AdDescriptions::Item status =
      makeItem(QStringLiteral("status"), QStringLiteral("Status"), QString());
  auto* tag = new AdTag(QStringLiteral("Running"));
  tag->setColorScheme(AdTag::ColorScheme::Success);
  status.contentWidget = tag;
  custom->addItem(status);
  custom->addItem(
      makeItem(QStringLiteral("region"), QStringLiteral("Region"), QStringLiteral("Hangzhou")));
  layout->addWidget(custom);
  return box;
}

QWidget* DescriptionsDocsPage::buildTokenDemo() {
  auto* descriptions = new AdDescriptions;
  descriptions->setTitle(QStringLiteral("Token override"));
  descriptions->setBordered(true);
  descriptions->setColumn(3);
  addAccountItems(descriptions);
  AdDescriptions::ComponentTokens tokens;
  tokens.colors.labelBackground = QColor(QStringLiteral("#fff7e6"));
  tokens.colors.labelColor = QColor(QStringLiteral("#ad4e00"));
  tokens.colors.borderColor = QColor(QStringLiteral("#ffd591"));
  tokens.metrics.borderRadius = 4;
  descriptions->setComponentTokens(tokens);
  return descriptions;
}
