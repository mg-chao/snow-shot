#include "pagination_docs_page.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include "widgets/widgets.h"

using adqt::widgets::AdPagination;

namespace {

AdPagination* makePagination(int total, QWidget* parent = nullptr) {
  auto* pagination = new AdPagination(parent);
  pagination->setTotal(total);
  return pagination;
}

QWidget* stacked(const QList<QWidget*>& widgets) {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(16);
  for (QWidget* widget : widgets) {
    layout->addWidget(widget);
  }
  return box;
}

}  // namespace

PaginationDocsPage::PaginationDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel(QStringLiteral("Pagination"));
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle =
      new QLabel(QStringLiteral("A long list can be divided into several pages using Pagination."));
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, QStringLiteral("Basic"), QStringLiteral("Basic pagination."), buildBasicDemo());
  addSection(root, QStringLiteral("More"), QStringLiteral("More pages use jump controls."),
             buildMoreDemo());
  addSection(root, QStringLiteral("Changer"),
             QStringLiteral("Change the number of items per page."), buildChangerDemo());
  addSection(root, QStringLiteral("Jumper"), QStringLiteral("Jump directly to a page."),
             buildJumperDemo());
  addSection(root, QStringLiteral("Size"), QStringLiteral("Large, medium, and small sizes."),
             buildSizeDemo());
  addSection(root, QStringLiteral("Simple mode"),
             QStringLiteral("Compact editable and read-only modes."), buildSimpleDemo());
  addSection(root, QStringLiteral("Total"), QStringLiteral("Display total and current item range."),
             buildTotalDemo());
  addSection(root, QStringLiteral("Controlled"),
             QStringLiteral("Synchronize page state with another control."), buildControlledDemo());
  addSection(root, QStringLiteral("Alignment"), QStringLiteral("Start, center, and end alignment."),
             buildAlignmentDemo());
  addSection(root, QStringLiteral("Component Token"),
             QStringLiteral("Override component-scoped tokens."), buildTokenDemo());
  root->addStretch();
}

const QVector<QWidget*>& PaginationDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& PaginationDocsPage::sectionTitles() const { return titles_; }

void PaginationDocsPage::addSection(QVBoxLayout* root, const QString& title,
                                    const QString& description, QWidget* content) {
  auto* panel = new QFrame();
  panel->setFrameShape(QFrame::StyledPanel);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  auto* titleLabel = new QLabel(title);
  QFont font = titleLabel->font();
  font.setBold(true);
  font.setPointSize(font.pointSize() + 1);
  titleLabel->setFont(font);
  auto* descriptionLabel = new QLabel(description);
  descriptionLabel->setWordWrap(true);
  layout->addWidget(titleLabel);
  layout->addWidget(descriptionLabel);
  layout->addWidget(content);
  root->addWidget(panel);
  anchors_.append(panel);
  titles_.append(title);
}

QWidget* PaginationDocsPage::buildBasicDemo() { return makePagination(50); }

QWidget* PaginationDocsPage::buildMoreDemo() {
  auto* pagination = makePagination(500);
  pagination->setCurrentPage(6);
  return pagination;
}

QWidget* PaginationDocsPage::buildChangerDemo() {
  auto* pagination = makePagination(500);
  pagination->setShowSizeChanger(true);
  return pagination;
}

QWidget* PaginationDocsPage::buildJumperDemo() {
  auto* pagination = makePagination(500);
  pagination->setShowQuickJumper(true);
  pagination->setShowSizeChanger(true);
  return pagination;
}

QWidget* PaginationDocsPage::buildSizeDemo() {
  auto* large = makePagination(50);
  large->setControlSize(AdPagination::ControlSize::Large);
  auto* medium = makePagination(50);
  auto* small = makePagination(50);
  small->setControlSize(AdPagination::ControlSize::Small);
  return stacked({large, medium, small});
}

QWidget* PaginationDocsPage::buildSimpleDemo() {
  auto* editable = makePagination(50);
  editable->setCurrentPage(2);
  editable->setSimple(true);
  auto* readOnly = makePagination(50);
  readOnly->setCurrentPage(2);
  readOnly->setSimple(true);
  readOnly->setSimpleReadOnly(true);
  auto* disabled = makePagination(50);
  disabled->setCurrentPage(2);
  disabled->setSimple(true);
  disabled->setDisabled(true);
  return stacked({editable, readOnly, disabled});
}

QWidget* PaginationDocsPage::buildTotalDemo() {
  auto* total = makePagination(85);
  total->setPageSize(20);
  total->setTotalTextFormatter([](int totalCount, const AdPagination::Range&) {
    return QStringLiteral("Total %1 items").arg(totalCount);
  });
  auto* range = makePagination(85);
  range->setPageSize(20);
  range->setTotalTextFormatter([](int totalCount, const AdPagination::Range& visible) {
    return QStringLiteral("%1-%2 of %3 items").arg(visible.first).arg(visible.last).arg(totalCount);
  });
  return stacked({total, range});
}

QWidget* PaginationDocsPage::buildControlledDemo() {
  auto* box = new QWidget();
  auto* layout = new QHBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);
  auto* pagination = makePagination(100);
  auto* page = new QSpinBox();
  page->setRange(1, 10);
  page->setValue(1);
  connect(page, &QSpinBox::valueChanged, pagination, &AdPagination::setCurrentPage);
  connect(pagination, &AdPagination::currentPageChanged, page, &QSpinBox::setValue);
  layout->addWidget(pagination, 1);
  layout->addWidget(page);
  return box;
}

QWidget* PaginationDocsPage::buildAlignmentDemo() {
  auto* start = makePagination(50);
  auto* center = makePagination(50);
  center->setAlignment(AdPagination::Alignment::Center);
  auto* end = makePagination(50);
  end->setAlignment(AdPagination::Alignment::End);
  return stacked({start, center, end});
}

QWidget* PaginationDocsPage::buildTokenDemo() {
  auto* pagination = makePagination(100);
  pagination->setCurrentPage(2);
  AdPagination::ComponentTokens tokens;
  tokens.colors.itemActiveBackground = QColor(QStringLiteral("#fff0f6"));
  tokens.colors.itemActiveBorder = QColor(QStringLiteral("#eb2f96"));
  tokens.colors.itemActiveText = QColor(QStringLiteral("#c41d7f"));
  tokens.colors.itemActiveHoverText = QColor(QStringLiteral("#eb2f96"));
  tokens.metrics.itemSpacing = 4;
  pagination->setComponentTokens(tokens);
  return pagination;
}
