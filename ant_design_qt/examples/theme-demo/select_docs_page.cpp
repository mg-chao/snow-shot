#include "select_docs_page.h"

#include "demo_theme_utils.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QVBoxLayout>

#include <functional>
#include <memory>

#include "antd_icons.h"
#include "widgets/detail/timing_hub.h"

using adqt::widgets::AdComboBox;
using adqt::widgets::AdMultiSelect;
using adqt::widgets::AdTagSelect;
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

constexpr int kOtherFieldRole = Qt::UserRole + 401;
constexpr int kEmojiRole = Qt::UserRole + 402;
constexpr int kDescriptionRole = Qt::UserRole + 403;

QStandardItemModel* makeSelectItemModel(
    const QVector<AdComboBox::Option>& options, QObject* parent = nullptr,
    const std::function<void(QStandardItem*, const AdComboBox::Option&)>& configure = {}) {
  auto* model = new QStandardItemModel(parent);
  for (const AdComboBox::Option& option : options) {
    auto* item = new QStandardItem(option.label);
    item->setData(option.label, Qt::DisplayRole);
    item->setData(option.value, AdComboBox::DefaultValueRole);
    item->setData(option.label, AdComboBox::DefaultLabelRole);
    item->setData(option.group, AdComboBox::DefaultGroupRole);
    item->setData(option.metadata, AdComboBox::DefaultMetadataRole);

    Qt::ItemFlags flags = item->flags();
    if (option.disabled) {
      flags &= ~Qt::ItemIsEnabled;
      flags &= ~Qt::ItemIsSelectable;
    }
    item->setFlags(flags);

    if (configure) {
      configure(item, option);
    }
    model->appendRow(item);
  }
  return model;
}

class RichOptionDelegate final : public QStyledItemDelegate {
 public:
  RichOptionDelegate(int emojiRole, int descriptionRole, QObject* parent = nullptr)
      : QStyledItemDelegate(parent), emojiRole_(emojiRole), descriptionRole_(descriptionRole) {}

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text = displayTextFor(index, opt.text);
    return QStyledItemDelegate::sizeHint(opt, index);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text = displayTextFor(index, opt.text);
    QStyledItemDelegate::paint(painter, opt, index);
  }

 private:
  QString displayTextFor(const QModelIndex& index, const QString& fallback) const {
    const QString emoji = index.data(emojiRole_).toString().trimmed();
    const QString label = index.data(AdComboBox::DefaultLabelRole).toString().trimmed();
    const QString description = index.data(descriptionRole_).toString().trimmed();

    QStringList parts;
    if (!emoji.isEmpty()) {
      parts.append(emoji);
    }
    parts.append(label.isEmpty() ? fallback : label);
    QString text = parts.join(QStringLiteral(" "));
    if (!description.isEmpty()) {
      text.append(QStringLiteral(" (%1)").arg(description));
    }
    return text;
  }

  int emojiRole_ = Qt::DisplayRole;
  int descriptionRole_ = Qt::DisplayRole;
};

}  // namespace

SelectDocsPage::SelectDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Select");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "A dropdown menu for displaying choices. This page mirrors Ant Design Select public demos.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Basic Usage", "Demo: basic.tsx", buildBasicDemo());
  addSection(root, "Select with search field", "Demo: search.tsx", buildSearchDemo());
  addSection(root, "Custom Search", "Demo: search-filter-option.tsx",
             buildSearchFilterOptionDemo());
  addSection(root, "Multi field search", "Demo: search-multi-field.tsx",
             buildSearchMultiFieldDemo());
  addSection(root, "multiple selection", "Demo: multiple.tsx", buildMultipleDemo());
  addSection(root, "Sizes", "Demo: size.tsx", buildSizeDemo());
  addSection(root, "Custom dropdown options", "Demo: option-render.tsx", buildOptionRenderDemo());
  addSection(root, "Search with sort", "Demo: search-sort.tsx", buildSearchSortDemo());
  addSection(root, "Tags", "Demo: tags.tsx", buildTagsDemo());
  addSection(root, "Option Group", "Demo: optgroup.tsx", buildOptGroupDemo());
  addSection(root, "coordinate", "Demo: coordinate.tsx", buildCoordinateDemo());
  addSection(root, "Search Box", "Demo: search-box.tsx", buildSearchBoxDemo());
  addSection(root, "Get value of selected item", "Demo: label-in-value.tsx",
             buildLabelInValueDemo());
  addSection(root, "Automatic tokenization", "Demo: automatic-tokenization.tsx",
             buildAutomaticTokenizationDemo());
  addSection(root, "Search and Select Users", "Demo: select-users.tsx", buildSelectUsersDemo());
  addSection(root, "Prefix and Suffix", "Demo: suffix.tsx", buildSuffixDemo());
  addSection(root, "Custom dropdown", "Demo: custom-dropdown-menu.tsx", buildCustomDropdownDemo());
  addSection(root, "Hide Already Selected", "Demo: hide-selected.tsx", buildHideSelectedDemo());
  addSection(root, "Variants", "Demo: variant.tsx", buildVariantDemo());
  addSection(root, "Custom Tag Render", "Demo: custom-tag-render.tsx", buildCustomTagRenderDemo());
  addSection(root, "Custom Selected Label Render", "Demo: custom-label-render.tsx",
             buildCustomLabelRenderDemo());
  addSection(root, "Responsive maxTagCount", "Demo: responsive.tsx", buildResponsiveDemo());
  addSection(root, "Status", "Demo: status.tsx", buildStatusDemo());
  addSection(root, "Placement", "Demo: placement.tsx", buildPlacementDemo());
  addSection(root, "Popup Layer", "Qt extension: popupLayerMode.", buildPopupLayerModeDemo());
  addSection(root, "Max Count", "Demo: maxCount.tsx", buildMaxCountDemo());
  addSection(root, "Custom semantic dom styling", "Demo: style-class.tsx", buildStyleClassDemo());

  root->addStretch();
}

const QVector<QWidget*>& SelectDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& SelectDocsPage::sectionTitles() const { return titles_; }

void SelectDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
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

QVector<SelectDocsPage::Option> SelectDocsPage::basicOptions() const {
  return {
      makeOption("jack", "Jack"),
      makeOption("lucy", "Lucy"),
      makeOption("yiminghe", "yiminghe"),
      makeOption("disabled", "Disabled", true),
  };
}

QVector<SelectDocsPage::Option> SelectDocsPage::alphaNumericOptions() const {
  QVector<Option> options;
  for (int i = 10; i < 36; ++i) {
    const QString value = QString::number(i, 36) + QString::number(i);
    options.append(makeOption(value, value));
  }
  return options;
}

QVector<SelectDocsPage::Option> SelectDocsPage::cityOptions() const {
  return {
      makeOption("HangZhou", "HangZhou #310000"),
      makeOption("NingBo", "NingBo #315000"),
      makeOption("WenZhou", "WenZhou #325000"),
  };
}

QWidget* SelectDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(12);

  auto* normal = new AdComboBox();
  normal->setPlaceholder("Select");
  normal->setOptions(basicOptions());
  normal->setCurrentValue(QStringLiteral("lucy"));
  normal->setFixedWidth(180);

  auto* disabled = new AdComboBox();
  disabled->setOptions({makeOption("lucy", "Lucy")});
  disabled->setCurrentValue(QStringLiteral("lucy"));
  disabled->setDisabled(true);
  disabled->setFixedWidth(180);

  auto* loading = new AdComboBox();
  loading->setOptions({makeOption("lucy", "Lucy")});
  loading->setCurrentValue(QStringLiteral("lucy"));
  loading->setLoading(true);
  loading->setFixedWidth(180);

  auto* allowClear = new AdComboBox();
  allowClear->setPlaceholder("select it");
  allowClear->setAllowClear(true);
  allowClear->setOptions({makeOption("lucy", "Lucy")});
  allowClear->setCurrentValue(QStringLiteral("lucy"));
  allowClear->setFixedWidth(180);

  row->addWidget(normal);
  row->addWidget(disabled);
  row->addWidget(loading);
  row->addWidget(allowClear);
  row->addStretch();
  return box;
}

QWidget* SelectDocsPage::buildSearchDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* select = new AdComboBox();
  select->setPlaceholder("Select a person");
  select->setSearchEnabled(true);
  select->setOptions(basicOptions());
  select->setFixedWidth(320);

  auto* output = makeHintLabel("Search: ");
  connect(select, &AdComboBox::searchTextChanged, output, [output](const QString& value) {
    output->setText(QStringLiteral("Search: %1").arg(value));
  });

  layout->addWidget(select, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* SelectDocsPage::buildSearchFilterOptionDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdComboBox();
  select->setPlaceholder("Select a person");
  select->setSearchEnabled(true);
  select->setModel(makeSelectItemModel(
      {makeOption("1", "Jack"), makeOption("2", "Lucy"), makeOption("3", "Tom")}, select));
  select->setFixedWidth(260);

  layout->addWidget(select, 0, Qt::AlignLeft);
  return box;
}

QWidget* SelectDocsPage::buildSearchMultiFieldDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdComboBox();
  select->setPlaceholder("Select an option");
  select->setSearchEnabled(true);
  select->setSearchRoles({AdComboBox::DefaultLabelRole, kOtherFieldRole});
  select->setModel(makeSelectItemModel(
      {
          makeOption("a11", "a11", false, QString(), {{"otherField", "c11"}}),
          makeOption("b22", "b22", false, QString(), {{"otherField", "b11"}}),
          makeOption("c33", "c33", false, QString(), {{"otherField", "b33"}}),
          makeOption("d44", "d44", false, QString(), {{"otherField", "d44"}}),
      },
      select, [](QStandardItem* item, const AdComboBox::Option& option) {
        item->setData(option.metadata.value("otherField"), kOtherFieldRole);
      }));
  select->setFixedWidth(260);

  layout->addWidget(select, 0, Qt::AlignLeft);
  layout->addWidget(makeHintLabel("Type either label or otherField value to filter."));
  return box;
}

QWidget* SelectDocsPage::buildMultipleDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* first = new AdMultiSelect();
  first->setAllowClear(true);
  first->setPlaceholder("Please select");
  first->setOptions(alphaNumericOptions());
  first->setSelectedValues({QStringLiteral("a10"), QStringLiteral("c12")});

  auto* second = new AdMultiSelect();
  second->setPlaceholder("Please select");
  second->setOptions(alphaNumericOptions());
  second->setSelectedValues({QStringLiteral("a10"), QStringLiteral("c12")});
  second->setDisabled(true);

  layout->addWidget(first);
  layout->addWidget(second);
  return box;
}

QWidget* SelectDocsPage::buildSizeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* controls = new QHBoxLayout();
  controls->addWidget(new QLabel("size:"));
  auto* large = new QRadioButton("large");
  auto* middle = new QRadioButton("default");
  auto* small = new QRadioButton("small");
  large->setChecked(true);
  controls->addWidget(large);
  controls->addWidget(middle);
  controls->addWidget(small);
  controls->addStretch();

  auto* single = new AdComboBox();
  single->setOptions(alphaNumericOptions());
  single->setCurrentValue(QStringLiteral("a10"));
  single->setFixedWidth(220);

  auto* multiple = new AdMultiSelect();
  multiple->setOptions(alphaNumericOptions());
  multiple->setSelectedValues({QStringLiteral("a10"), QStringLiteral("c12")});

  auto* tags = new AdTagSelect();
  tags->setOptions(alphaNumericOptions());
  tags->setSelectedValues({QStringLiteral("a10"), QStringLiteral("c12")});

  auto applySize = [single, multiple, tags](AdComboBox::ControlSize size) {
    single->setControlSize(size);
    multiple->setControlSize(size);
    tags->setControlSize(size);
  };

  connect(large, &QRadioButton::clicked, this,
          [applySize]() { applySize(AdComboBox::ControlSize::Large); });
  connect(middle, &QRadioButton::clicked, this,
          [applySize]() { applySize(AdComboBox::ControlSize::Middle); });
  connect(small, &QRadioButton::clicked, this,
          [applySize]() { applySize(AdComboBox::ControlSize::Small); });
  applySize(AdComboBox::ControlSize::Large);

  layout->addLayout(controls);
  layout->addWidget(single, 0, Qt::AlignLeft);
  layout->addWidget(multiple);
  layout->addWidget(tags);
  return box;
}

QWidget* SelectDocsPage::buildOptionRenderDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdMultiSelect();
  select->setPlaceholder("Please select your current mood.");
  select->setSelectedValues({QStringLiteral("happy")});
  select->setModel(makeSelectItemModel(
      {
          makeOption("happy", "Happy", false, QString(),
                     {{"emoji", ":)"}, {"desc", "Feeling Good"}}),
          makeOption("sad", "Sad", false, QString(), {{"emoji", ":("}, {"desc", "Feeling Blue"}}),
          makeOption("angry", "Angry", false, QString(), {{"emoji", ">:("}, {"desc", "Furious"}}),
          makeOption("cool", "Cool", false, QString(), {{"emoji", "B)"}, {"desc", "Chilling"}}),
          makeOption("sleepy", "Sleepy", false, QString(),
                     {{"emoji", "-_-"}, {"desc", "Need Sleep"}}),
      },
      select, [](QStandardItem* item, const AdComboBox::Option& option) {
        item->setData(option.metadata.value("emoji"), kEmojiRole);
        item->setData(option.metadata.value("desc"), kDescriptionRole);
      }));
  select->setItemDelegate(new RichOptionDelegate(kEmojiRole, kDescriptionRole, select));

  layout->addWidget(select);
  return box;
}

QWidget* SelectDocsPage::buildSearchSortDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdComboBox();
  select->setSearchEnabled(true);
  select->setPlaceholder("Search to Select");
  select->setFixedWidth(220);

  auto* model = makeSelectItemModel(
      {
          makeOption("1", "Not Identified"),
          makeOption("2", "Closed"),
          makeOption("3", "Communicated"),
          makeOption("4", "Identified"),
          makeOption("5", "Resolved"),
          makeOption("6", "Cancelled"),
      },
      select);
  model->sort(0);
  select->setModel(model);

  layout->addWidget(select, 0, Qt::AlignLeft);
  return box;
}

QWidget* SelectDocsPage::buildTagsDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdTagSelect();
  select->setPlaceholder("Tags Mode");
  select->setOptions(alphaNumericOptions());

  layout->addWidget(select);
  return box;
}

QWidget* SelectDocsPage::buildOptGroupDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdComboBox();
  select->setOptions({
      makeOption("Jack", "Jack", false, "manager"),
      makeOption("Lucy", "Lucy", false, "manager"),
      makeOption("Chloe", "Chloe", false, "engineer"),
      makeOption("Lucas", "Lucas", false, "engineer"),
  });
  select->setCurrentValue(QStringLiteral("Lucy"));
  select->setFixedWidth(260);

  layout->addWidget(select, 0, Qt::AlignLeft);
  return box;
}

QWidget* SelectDocsPage::buildCoordinateDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);

  const QMap<QString, QStringList> cityData = {
      {"Zhejiang", {"Hangzhou", "Ningbo", "Wenzhou"}},
      {"Jiangsu", {"Nanjing", "Suzhou", "Zhenjiang"}},
  };

  auto* province = new AdComboBox();
  province->setFixedWidth(140);
  province->setOptions({makeOption("Zhejiang", "Zhejiang"), makeOption("Jiangsu", "Jiangsu")});
  province->setCurrentValue(QStringLiteral("Zhejiang"));

  auto* city = new AdComboBox();
  city->setFixedWidth(140);
  auto resetCity = [city, cityData](const QString& provinceName) {
    QVector<Option> options;
    const QStringList list = cityData.value(provinceName);
    for (const QString& value : list) {
      options.append(makeOption(value, value));
    }
    city->setOptions(options);
    if (!list.isEmpty()) {
      city->setCurrentValue(list.constFirst());
    }
  };
  resetCity("Zhejiang");

  connect(province, &AdComboBox::currentValueChanged, city,
          [resetCity](const QVariant& value) { resetCity(value.toString()); });

  row->addWidget(province);
  row->addWidget(city);
  row->addStretch();
  return box;
}

QWidget* SelectDocsPage::buildSearchBoxDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* select = new AdComboBox();
  select->setSearchEnabled(true);
  select->setFixedWidth(260);
  select->setPlaceholder("input search text");
  select->setSearchPolicy(AdComboBox::SearchPolicy::External);

  auto requestId = std::make_shared<int>(0);
  auto* hint = makeHintLabel("Type to fetch local mock results...");
  connect(select, &AdComboBox::searchTextChanged, select,
          [select, requestId, hint](const QString& value) {
            *requestId += 1;
            const int current = *requestId;
            select->setLoading(true);
            hint->setText(QStringLiteral("Fetching: %1").arg(value));
            adqt::widgets::detail::scheduleTimingTask(
                select, QStringLiteral("ThemeDemo.SelectSearchBox"), 300,
                [select, requestId, current, value, hint]() {
                  if (current != *requestId) {
                    return;
                  }
                  QVector<Option> options;
                  if (!value.trimmed().isEmpty()) {
                    for (int i = 0; i < 5; ++i) {
                      const QString text = QStringLiteral("%1-result-%2").arg(value).arg(i + 1);
                      options.append(makeOption(text, text));
                    }
                  }
                  select->setOptions(options);
                  select->setLoading(false);
                  hint->setText(QStringLiteral("Loaded %1 options").arg(options.size()));
                  if (select->popupVisible()) {
                    select->setPopupVisible(true);
                  }
                });
          });

  layout->addWidget(select, 0, Qt::AlignLeft);
  layout->addWidget(hint);
  return box;
}

QWidget* SelectDocsPage::buildLabelInValueDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* select = new AdComboBox();
  auto* model = new QStandardItemModel(select);
  for (const auto& entry : QVector<QPair<int, QString>>{{100, "Jack (100)"}, {101, "Lucy (101)"}}) {
    auto* item = new QStandardItem(entry.second);
    item->setData(entry.second, Qt::DisplayRole);
    item->setData(entry.first, AdComboBox::DefaultValueRole);
    item->setData(entry.second, AdComboBox::DefaultLabelRole);
    model->appendRow(item);
  }
  select->setModel(model);
  select->setCurrentValue(101);
  select->setFixedWidth(180);

  auto* output = makeHintLabel("Selected: { value: 101, label: 'Lucy (101)' }");
  auto updateOutput = [select, output]() {
    const QVariant value = select->currentValue();
    const AdComboBox::SelectionItem item = select->currentItem();
    if (!value.isValid() || item.label.isEmpty()) {
      output->setText("Selected: {}");
      return;
    }
    output->setText(
        QStringLiteral("Selected: { value: %1, label: '%2' }").arg(value.toString(), item.label));
  };
  connect(select, &AdComboBox::currentValueChanged, output,
          [updateOutput](const QVariant&) { updateOutput(); });
  updateOutput();

  layout->addWidget(select, 0, Qt::AlignLeft);
  layout->addWidget(output);
  return box;
}

QWidget* SelectDocsPage::buildAutomaticTokenizationDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdTagSelect();
  select->setTokenSeparators({","});
  select->setPlaceholder("Type words and separate with comma");
  select->setOptions(alphaNumericOptions());

  layout->addWidget(select);
  return box;
}

QWidget* SelectDocsPage::buildSelectUsersDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* select = new AdMultiSelect();
  select->setSearchEnabled(true);
  select->setPlaceholder("Select users");
  select->setSearchPolicy(AdMultiSelect::SearchPolicy::External);

  auto* hint = makeHintLabel("Search users (local async mock)");
  auto requestId = std::make_shared<int>(0);
  connect(
      select, &AdMultiSelect::searchTextChanged, select,
      [select, hint, requestId](const QString& value) {
        *requestId += 1;
        const int current = *requestId;
        select->setLoading(true);
        adqt::widgets::detail::scheduleTimingTask(
            select, QStringLiteral("ThemeDemo.SelectUsersSearch"), 280,
            [select, hint, requestId, current, value]() {
              if (current != *requestId) {
                return;
              }
              QVector<Option> options;
              if (!value.trimmed().isEmpty()) {
                for (int i = 0; i < 8; ++i) {
                  const QString id = QStringLiteral("%1-%2").arg(value).arg(i + 1);
                  options.append(makeOption(id, QStringLiteral("User %1").arg(id), false, QString(),
                                            {{"avatar", QStringLiteral("[%1]").arg(i + 1)}}));
                }
              }
              select->setModel(makeSelectItemModel(
                  options, select, [](QStandardItem* item, const Option& option) {
                    const QString avatar = option.metadata.value("avatar").toString();
                    item->setData(QStringLiteral("%1 %2").arg(avatar, option.label),
                                  Qt::DisplayRole);
                  }));
              select->setLoading(false);
              hint->setText(QStringLiteral("Loaded users: %1").arg(options.size()));
            });
      });

  layout->addWidget(select);
  layout->addWidget(hint);
  return box;
}

QWidget* SelectDocsPage::buildSuffixDemo() {
  auto* box = new QWidget();
  auto* row = new QHBoxLayout(box);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* prefixed = new AdComboBox();
  prefixed->setPrefixText("User");
  prefixed->setSearchEnabled(true);
  prefixed->setAllowClear(true);
  prefixed->setOptions(basicOptions());
  prefixed->setCurrentValue(QStringLiteral("lucy"));
  prefixed->setFixedWidth(220);

  auto* suffixA = new AdComboBox();
  suffixA->setSuffixIconRef(outlined_icons::Smile());
  suffixA->setOptions(basicOptions());
  suffixA->setCurrentValue(QStringLiteral("lucy"));
  suffixA->setFixedWidth(170);

  auto* suffixB = new AdComboBox();
  suffixB->setSuffixIconRef(outlined_icons::Meh());
  suffixB->setOptions({makeOption("lucy", "Lucy")});
  suffixB->setCurrentValue(QStringLiteral("lucy"));
  suffixB->setDisabled(true);
  suffixB->setFixedWidth(170);

  auto* multi = new AdMultiSelect();
  multi->setPrefixText("User");
  multi->setOptions(basicOptions());
  multi->setSelectedValues({QStringLiteral("lucy")});
  multi->setFixedWidth(220);

  row->addWidget(prefixed);
  row->addWidget(suffixA);
  row->addWidget(suffixB);
  row->addWidget(multi);
  row->addStretch();
  return box;
}

QWidget* SelectDocsPage::buildCustomDropdownDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdComboBox();
  select->setPlaceholder("custom dropdown render");
  select->setFixedWidth(320);
  QVector<Option> items = {makeOption("jack", "jack"), makeOption("lucy", "lucy")};
  select->setOptions(items);

  auto dynamicItems = std::make_shared<QVector<Option>>(items);
  auto index = std::make_shared<int>(0);

  auto* panel = new QWidget(select);
  auto* row = new QHBoxLayout(panel);
  row->setContentsMargins(0, 4, 0, 0);
  row->setSpacing(6);

  auto* input = new QLineEdit(panel);
  input->setPlaceholderText("Please enter item");
  auto* add = new QPushButton("Add item", panel);

  connect(add, &QAbstractButton::clicked, panel, [select, dynamicItems, index, input]() {
    const QString text = input->text().trimmed().isEmpty()
                             ? QStringLiteral("New item %1").arg((*index)++)
                             : input->text().trimmed();
    dynamicItems->append(makeOption(text, text));
    select->setOptions(*dynamicItems);
    input->clear();
    select->setPopupVisible(true);
  });

  row->addWidget(input, 1);
  row->addWidget(add);
  select->setPopupFooterWidget(panel);

  layout->addWidget(select, 0, Qt::AlignLeft);
  return box;
}

QWidget* SelectDocsPage::buildHideSelectedDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  const QStringList source = {"Apples", "Nails", "Bananas", "Helicopters"};
  auto* select = new AdMultiSelect();
  select->setPlaceholder("Inserted are removed");

  auto rebuild = [select, source](const QStringList& selectedValues) {
    QVector<Option> options;
    for (const QString& value : source) {
      if (!selectedValues.contains(value)) {
        options.append(makeOption(value, value));
      }
    }
    select->setOptions(options);
  };

  connect(select, &AdMultiSelect::selectedValuesChanged, select,
          [rebuild](const QVariantList& values) {
            QStringList normalized;
            normalized.reserve(values.size());
            for (const QVariant& value : values) {
              normalized.append(value.toString());
            }
            rebuild(normalized);
          });
  rebuild({});

  layout->addWidget(select);
  return box;
}

QWidget* SelectDocsPage::buildVariantDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  const QVector<QPair<QString, AdComboBox::Variant>> variants = {
      {"Outlined", AdComboBox::Variant::Outlined},
      {"Filled", AdComboBox::Variant::Filled},
      {"Borderless", AdComboBox::Variant::Borderless},
      {"Underlined", AdComboBox::Variant::Underlined},
  };

  for (const auto& item : variants) {
    auto* row = new QHBoxLayout();
    row->setSpacing(8);

    auto* single = new AdComboBox();
    single->setVariant(item.second);
    single->setPlaceholder(item.first);
    single->setOptions(basicOptions());
    single->setCurrentValue(QStringLiteral("lucy"));
    single->setFixedWidth(220);

    auto* multi = new AdMultiSelect();
    multi->setVariant(item.second);
    multi->setPlaceholder(item.first);
    multi->setOptions(basicOptions());
    multi->setSelectedValues({QStringLiteral("lucy")});
    multi->setFixedWidth(220);

    row->addWidget(single);
    row->addWidget(multi);
    row->addStretch();
    layout->addLayout(row);
  }

  return box;
}

QWidget* SelectDocsPage::buildCustomTagRenderDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdMultiSelect();
  select->setModel(makeSelectItemModel({makeOption("gold", "gold"), makeOption("lime", "lime"),
                                        makeOption("green", "green"), makeOption("cyan", "cyan")},
                                       select, [](QStandardItem* item, const Option& option) {
                                         item->setData(
                                             QStringLiteral("[%1]").arg(option.label.toUpper()),
                                             AdComboBox::DefaultTagTextRole);
                                       }));
  select->setSelectedValues({QStringLiteral("gold"), QStringLiteral("cyan")});

  layout->addWidget(select);
  return box;
}

QWidget* SelectDocsPage::buildCustomLabelRenderDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);

  auto* select = new AdComboBox();
  select->setModel(
      makeSelectItemModel({makeOption("gold", "gold"), makeOption("lime", "lime"),
                           makeOption("green", "green"), makeOption("cyan", "cyan")},
                          select, [](QStandardItem* item, const Option& option) {
                            item->setData(QStringLiteral("value=%1").arg(option.value.toString()),
                                          AdComboBox::DefaultSelectedTextRole);
                          }));
  select->setCurrentValue(QStringLiteral("gold"));

  layout->addWidget(select, 0, Qt::AlignLeft);
  return box;
}

QWidget* SelectDocsPage::buildResponsiveDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* select = new AdMultiSelect();
  select->setOptions(alphaNumericOptions());
  select->setSelectedValues({QStringLiteral("a10"), QStringLiteral("c12"), QStringLiteral("h17"),
                             QStringLiteral("j19"), QStringLiteral("k20")});
  select->setResponsiveMaxTagCount(true);
  select->setPlaceholder("Select Item...");

  auto* disabled = new AdMultiSelect();
  disabled->setOptions(alphaNumericOptions());
  disabled->setSelectedValues({QStringLiteral("a10"), QStringLiteral("c12"), QStringLiteral("h17"),
                               QStringLiteral("j19"), QStringLiteral("k20")});
  disabled->setResponsiveMaxTagCount(true);
  disabled->setDisabled(true);

  layout->addWidget(select);
  layout->addWidget(disabled);
  return box;
}

QWidget* SelectDocsPage::buildStatusDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* error = new AdComboBox();
  error->setStatus(AdComboBox::Status::Error);
  error->setPlaceholder("Error");
  error->setOptions(basicOptions());

  auto* warning = new AdComboBox();
  warning->setStatus(AdComboBox::Status::Warning);
  warning->setPlaceholder("Warning");
  warning->setOptions(basicOptions());

  layout->addWidget(error);
  layout->addWidget(warning);
  return box;
}

QWidget* SelectDocsPage::buildPlacementDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* radio = new QComboBox();
  radio->addItem("topLeft", static_cast<int>(AdComboBox::Placement::TopLeft));
  radio->addItem("topRight", static_cast<int>(AdComboBox::Placement::TopRight));
  radio->addItem("bottomLeft", static_cast<int>(AdComboBox::Placement::BottomLeft));
  radio->addItem("bottomRight", static_cast<int>(AdComboBox::Placement::BottomRight));
  radio->setCurrentIndex(0);

  auto* select = new AdComboBox();
  select->setOptions(cityOptions());
  select->setCurrentValue(QStringLiteral("HangZhou"));
  select->setPopupWidthMode(AdComboBox::PopupWidthMode::ContentWidth);
  select->setFixedWidth(120);

  connect(radio, QOverload<int>::of(&QComboBox::currentIndexChanged), select, [radio, select](int) {
    select->setPlacement(static_cast<AdComboBox::Placement>(radio->currentData().toInt()));
  });

  layout->addWidget(radio, 0, Qt::AlignLeft);
  layout->addWidget(select, 0, Qt::AlignLeft);
  return box;
}

QWidget* SelectDocsPage::buildPopupLayerModeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* modeBox = new QComboBox();
  modeBox->addItem(QStringLiteral("InWindow"),
                   static_cast<int>(AdComboBox::PopupLayerMode::InWindow));
  modeBox->addItem(QStringLiteral("QtTool"), static_cast<int>(AdComboBox::PopupLayerMode::QtTool));
  modeBox->setCurrentIndex(1);

  auto* stage = new QFrame();
  stage->setFrameShape(QFrame::StyledPanel);
  stage->setMinimumSize(360, 150);
  auto* stageLayout = new QHBoxLayout(stage);
  stageLayout->setContentsMargins(12, 12, 12, 12);

  auto* select = new AdComboBox(stage);
  select->setOptions(cityOptions());
  select->setCurrentValue(QStringLiteral("HangZhou"));
  select->setFixedWidth(150);
  select->setPlacement(AdComboBox::Placement::BottomRight);
  select->setPopupLayerMode(AdComboBox::PopupLayerMode::QtTool);
  stageLayout->addStretch();
  stageLayout->addWidget(select, 0, Qt::AlignRight | Qt::AlignBottom);

  connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), select,
          [modeBox, select](int) {
            select->setPopupLayerMode(
                static_cast<AdComboBox::PopupLayerMode>(modeBox->currentData().toInt()));
          });

  layout->addWidget(modeBox, 0, Qt::AlignLeft);
  layout->addWidget(stage);
  return box;
}

QWidget* SelectDocsPage::buildMaxCountDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* select = new AdMultiSelect();
  select->setMaxSelectionCount(3);
  select->setOptions({
      makeOption("Ava Swift", "Ava Swift"),
      makeOption("Cole Reed", "Cole Reed"),
      makeOption("Mia Blake", "Mia Blake"),
      makeOption("Jake Stone", "Jake Stone"),
      makeOption("Lily Lane", "Lily Lane"),
      makeOption("Ryan Chase", "Ryan Chase"),
      makeOption("Zoe Fox", "Zoe Fox"),
      makeOption("Alex Grey", "Alex Grey"),
      makeOption("Elle Blair", "Elle Blair"),
  });
  select->setSelectedValues({QStringLiteral("Ava Swift")});

  auto* suffix = makeHintLabel("1 / 3");
  connect(select, &AdMultiSelect::selectedValuesChanged, suffix,
          [suffix](const QVariantList& values) {
            suffix->setText(QStringLiteral("%1 / 3").arg(values.size()));
          });

  layout->addWidget(select);
  layout->addWidget(suffix);
  return box;
}

QWidget* SelectDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* objectStyle = new AdComboBox();
  objectStyle->setPrefixIconRef(outlined_icons::Meh());
  objectStyle->setOptions(
      {makeOption("GuangZhou", "GuangZhou"), makeOption("ShenZhen", "ShenZhen")});

  AdComboBox::SemanticStyles semantic;
  semantic.prefix.textColor = QColor("#1890ff");
  semantic.suffix.textColor = QColor("#1890ff");
  objectStyle->setSemanticStyles(semantic);

  auto* functionStyle = new AdComboBox();
  functionStyle->setPrefixIconRef(outlined_icons::Meh());
  functionStyle->setVariant(AdComboBox::Variant::Filled);
  functionStyle->setOptions(
      {makeOption("GuangZhou", "GuangZhou"), makeOption("ShenZhen", "ShenZhen")});
  functionStyle->setSemanticStyleResolver([](const AdComboBox::StyleContext& ctx) {
    AdComboBox::SemanticStyles styles;
    if (ctx.variant == AdComboBox::Variant::Filled) {
      styles.prefix.textColor = QColor("#722ed1");
      styles.suffix.textColor = QColor("#722ed1");
      styles.popup.borderColor = QColor("#722ed1");
    }
    return styles;
  });

  layout->addWidget(objectStyle);
  layout->addWidget(functionStyle);
  return box;
}
