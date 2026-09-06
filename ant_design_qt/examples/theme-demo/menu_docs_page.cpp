#include "menu_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <functional>
#include <optional>

#include "antd_icons.h"

using adqt::widgets::AdContextMenu;
using adqt::widgets::AdNavigationMenu;
using adqt::widgets::AdNavigationMenuItemDelegate;
using adqt::widgets::AdNavigationMenuPopupContext;
using adqt::widgets::AdNavigationMenuPopupFactory;
namespace outlined_icons = adqt::icons::antd::outlined;

namespace {

struct MenuNode {
  enum class Kind {
    Action,
    Group,
    Separator,
  };

  QString id;
  QString label;
  Kind kind = Kind::Action;
  adqt::icons::IconRef icon;
  QVector<MenuNode> children;
  QString extra;
  bool disabled = false;
  bool danger = false;
  bool dashed = false;
  std::optional<AdNavigationMenu::ColorScheme> popupColorScheme;
  QPoint popupOffset;
};

MenuNode actionNode(const QString& id, const QString& label, const adqt::icons::IconRef& icon = {},
                    bool disabled = false) {
  MenuNode node;
  node.id = id;
  node.label = label;
  node.icon = icon;
  node.disabled = disabled;
  return node;
}

MenuNode submenuNode(const QString& id, const QString& label, const QVector<MenuNode>& children,
                     const adqt::icons::IconRef& icon = {}) {
  MenuNode node = actionNode(id, label, icon, false);
  node.children = children;
  return node;
}

MenuNode groupNode(const QString& id, const QString& label, const QVector<MenuNode>& children) {
  MenuNode node;
  node.id = id;
  node.label = label;
  node.kind = MenuNode::Kind::Group;
  node.children = children;
  return node;
}

MenuNode separatorNode(const QString& id, bool dashed = false) {
  MenuNode node;
  node.id = id;
  node.kind = MenuNode::Kind::Separator;
  node.dashed = dashed;
  return node;
}

QStandardItem* buildMenuItem(const MenuNode& node) {
  auto* item = new QStandardItem(node.label);
  item->setData(node.label, Qt::DisplayRole);
  item->setData(node.label.isEmpty() ? node.id : node.label, Qt::ToolTipRole);
  item->setData(node.id, AdNavigationMenu::StableIdRole);
  if (adqt::icons::isValid(node.icon)) {
    item->setData(QVariant::fromValue(node.icon), Qt::DecorationRole);
  }
  if (!node.extra.isEmpty()) {
    item->setData(node.extra, AdNavigationMenu::ExtraTextRole);
  }
  if (node.danger) {
    item->setData(true, AdNavigationMenu::DangerRole);
  }
  if (node.popupColorScheme.has_value()) {
    item->setData(static_cast<int>(*node.popupColorScheme), AdNavigationMenu::PopupColorSchemeRole);
  }
  if (!node.popupOffset.isNull()) {
    item->setData(node.popupOffset, AdNavigationMenu::PopupOffsetRole);
  }

  switch (node.kind) {
    case MenuNode::Kind::Action:
      item->setData(static_cast<int>(AdNavigationMenu::NodeKind::Action),
                    AdNavigationMenu::NodeKindRole);
      item->setSelectable(true);
      item->setEnabled(!node.disabled);
      break;
    case MenuNode::Kind::Group:
      item->setData(static_cast<int>(AdNavigationMenu::NodeKind::Group),
                    AdNavigationMenu::NodeKindRole);
      item->setSelectable(false);
      item->setEnabled(true);
      break;
    case MenuNode::Kind::Separator:
      item->setData(static_cast<int>(AdNavigationMenu::NodeKind::Separator),
                    AdNavigationMenu::NodeKindRole);
      item->setData(node.dashed, AdNavigationMenu::DashedRole);
      item->setSelectable(false);
      item->setEnabled(false);
      break;
  }

  for (const MenuNode& child : node.children) {
    item->appendRow(buildMenuItem(child));
  }
  return item;
}

QStandardItemModel* createMenuModel(QObject* parent, const QVector<MenuNode>& nodes) {
  auto* model = new QStandardItemModel(parent);
  for (const MenuNode& node : nodes) {
    model->appendRow(buildMenuItem(node));
  }
  return model;
}

QModelIndex findIndexById(const QAbstractItemModel* model, const QString& id,
                          const QModelIndex& parent = QModelIndex()) {
  if (!model || id.trimmed().isEmpty()) {
    return QModelIndex();
  }
  const int rowCount = model->rowCount(parent);
  for (int row = 0; row < rowCount; ++row) {
    const QModelIndex index = model->index(row, 0, parent);
    if (!index.isValid()) {
      continue;
    }
    if (index.data(AdNavigationMenu::StableIdRole).toString() == id) {
      return index;
    }
    if (const QModelIndex child = findIndexById(model, id, index); child.isValid()) {
      return child;
    }
  }
  return QModelIndex();
}

QItemSelectionModel* attachMenuModel(AdNavigationMenu* menu, QAbstractItemModel* model,
                                     AdNavigationMenu::SelectionMode selectionMode =
                                         AdNavigationMenu::SelectionMode::SingleSelection) {
  auto* selectionModel = new QItemSelectionModel(model, menu);
  menu->setSelectionMode(selectionMode);
  menu->setModel(model);
  menu->setSelectionModel(selectionModel);
  return selectionModel;
}

void selectMenuId(AdNavigationMenu* menu, QItemSelectionModel* selectionModel, const QString& id,
                  bool clearExisting = true) {
  if (!menu || !selectionModel) {
    return;
  }
  const QModelIndex index = findIndexById(menu->model(), id);
  if (!index.isValid()) {
    return;
  }
  selectionModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
  if (menu->selectionMode() == AdNavigationMenu::SelectionMode::NoSelection) {
    return;
  }
  QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::Rows;
  if (menu->selectionMode() == AdNavigationMenu::SelectionMode::MultiSelection) {
    flags |= clearExisting ? QItemSelectionModel::ClearAndSelect : QItemSelectionModel::Select;
  } else {
    flags |= QItemSelectionModel::ClearAndSelect;
  }
  selectionModel->select(index, flags);
}

void expandMenuIds(AdNavigationMenu* menu, const QStringList& ids) {
  if (!menu || !menu->model()) {
    return;
  }
  for (const QString& id : ids) {
    const QModelIndex index = findIndexById(menu->model(), id);
    if (index.isValid()) {
      menu->setExpanded(index, true);
    }
  }
}

void populateColorSchemeBox(QComboBox* comboBox) {
  if (!comboBox) {
    return;
  }
  comboBox->addItem("Inherit", static_cast<int>(AdNavigationMenu::ColorScheme::Inherit));
  comboBox->addItem("Light", static_cast<int>(AdNavigationMenu::ColorScheme::Light));
  comboBox->addItem("Dark", static_cast<int>(AdNavigationMenu::ColorScheme::Dark));
}

AdNavigationMenu::ColorScheme currentColorScheme(const QComboBox* comboBox) {
  return comboBox ? static_cast<AdNavigationMenu::ColorScheme>(comboBox->currentData().toInt())
                  : AdNavigationMenu::ColorScheme::Inherit;
}

const QStringList& menuDocSectionTitles() {
  static const QStringList titles = {
      QStringLiteral("Top Navigation"),
      QStringLiteral("Top Navigation (Dark)"),
      QStringLiteral("Inline Menu"),
      QStringLiteral("Collapsed Inline Menu"),
      QStringLiteral("Collapsed Menu Tooltip"),
      QStringLiteral("Open Current Submenu Only"),
      QStringLiteral("Vertical Popup Menu"),
      QStringLiteral("Context Menu"),
      QStringLiteral("Color Scheme"),
      QStringLiteral("Popup Color Scheme"),
      QStringLiteral("Dynamic Mode Switch"),
      QStringLiteral("Semantic Styling (styles/classNames)"),
      QStringLiteral("Style Debugging"),
      QStringLiteral("Menu v4 Style"),
      QStringLiteral("Component Token"),
      QStringLiteral("Extra Content / Danger Item / Divider"),
      QStringLiteral("Custom Popup Render"),
      QStringLiteral("Semantic DOM Comparison"),
      QStringLiteral("API Overview"),
  };
  return titles;
}

QVector<MenuNode> horizontalNodes() {
  const auto mail = outlined_icons::Mail();
  const auto app = outlined_icons::Appstore();
  const auto setting = outlined_icons::Setting();

  return {
      actionNode(QStringLiteral("mail"), QStringLiteral("Navigation One"), mail),
      actionNode(QStringLiteral("app"), QStringLiteral("Navigation Two"), app, true),
      submenuNode(
          QStringLiteral("SubMenu"), QStringLiteral("Navigation Three - Submenu"),
          {groupNode(QStringLiteral("g1"), QStringLiteral("Item 1"),
                     {actionNode(QStringLiteral("setting:1"), QStringLiteral("Option 1")),
                      actionNode(QStringLiteral("setting:2"), QStringLiteral("Option 2"))}),
           groupNode(QStringLiteral("g2"), QStringLiteral("Item 2"),
                     {actionNode(QStringLiteral("setting:3"), QStringLiteral("Option 3")),
                      actionNode(QStringLiteral("setting:4"), QStringLiteral("Option 4"))})},
          setting),
      actionNode(QStringLiteral("alipay"), QStringLiteral("Navigation Four - Link")),
  };
}

QVector<MenuNode> inlineNodes() {
  const auto mail = outlined_icons::Mail();
  const auto app = outlined_icons::Appstore();
  const auto setting = outlined_icons::Setting();

  return {
      submenuNode(QStringLiteral("sub1"), QStringLiteral("Navigation One"),
                  {groupNode(QStringLiteral("g1"), QStringLiteral("Item 1"),
                             {actionNode(QStringLiteral("1"), QStringLiteral("Option 1")),
                              actionNode(QStringLiteral("2"), QStringLiteral("Option 2"))}),
                   groupNode(QStringLiteral("g2"), QStringLiteral("Item 2"),
                             {actionNode(QStringLiteral("3"), QStringLiteral("Option 3")),
                              actionNode(QStringLiteral("4"), QStringLiteral("Option 4"))})},
                  mail),
      submenuNode(QStringLiteral("sub2"), QStringLiteral("Navigation Two"),
                  {actionNode(QStringLiteral("5"), QStringLiteral("Option 5")),
                   actionNode(QStringLiteral("6"), QStringLiteral("Option 6")),
                   submenuNode(QStringLiteral("sub3"), QStringLiteral("Submenu"),
                               {actionNode(QStringLiteral("7"), QStringLiteral("Option 7")),
                                actionNode(QStringLiteral("8"), QStringLiteral("Option 8"))})},
                  app),
      submenuNode(QStringLiteral("sub4"), QStringLiteral("Navigation Three"),
                  {actionNode(QStringLiteral("9"), QStringLiteral("Option 9")),
                   actionNode(QStringLiteral("10"), QStringLiteral("Option 10")),
                   actionNode(QStringLiteral("11"), QStringLiteral("Option 11")),
                   actionNode(QStringLiteral("12"), QStringLiteral("Option 12"))},
                  setting),
  };
}

QVector<MenuNode> collapsedInlineNodes() {
  const auto pie = outlined_icons::PieChart();
  const auto desktop = outlined_icons::Desktop();
  const auto container = outlined_icons::Container();
  const auto mail = outlined_icons::Mail();
  const auto app = outlined_icons::Appstore();

  return {
      actionNode(QStringLiteral("1"), QStringLiteral("Option 1"), pie),
      actionNode(QStringLiteral("2"), QStringLiteral("Option 2"), desktop),
      actionNode(QStringLiteral("3"), QStringLiteral("Option 3"), container),
      submenuNode(QStringLiteral("sub1"), QStringLiteral("Navigation One"),
                  {actionNode(QStringLiteral("5"), QStringLiteral("Option 5")),
                   actionNode(QStringLiteral("6"), QStringLiteral("Option 6")),
                   actionNode(QStringLiteral("7"), QStringLiteral("Option 7")),
                   actionNode(QStringLiteral("8"), QStringLiteral("Option 8"))},
                  mail),
      submenuNode(QStringLiteral("sub2"), QStringLiteral("Navigation Two"),
                  {actionNode(QStringLiteral("9"), QStringLiteral("Option 9")),
                   actionNode(QStringLiteral("10"), QStringLiteral("Option 10")),
                   submenuNode(QStringLiteral("sub3"), QStringLiteral("Submenu"),
                               {actionNode(QStringLiteral("11"), QStringLiteral("Option 11")),
                                actionNode(QStringLiteral("12"), QStringLiteral("Option 12"))})},
                  app),
  };
}

QVector<MenuNode> siderCurrentNodes() {
  const auto mail = outlined_icons::Mail();
  const auto app = outlined_icons::Appstore();
  const auto setting = outlined_icons::Setting();

  return {
      submenuNode(QStringLiteral("1"), QStringLiteral("Navigation One"),
                  {actionNode(QStringLiteral("11"), QStringLiteral("Option 1")),
                   actionNode(QStringLiteral("12"), QStringLiteral("Option 2")),
                   actionNode(QStringLiteral("13"), QStringLiteral("Option 3")),
                   actionNode(QStringLiteral("14"), QStringLiteral("Option 4"))},
                  mail),
      submenuNode(QStringLiteral("2"), QStringLiteral("Navigation Two"),
                  {actionNode(QStringLiteral("21"), QStringLiteral("Option 1")),
                   actionNode(QStringLiteral("22"), QStringLiteral("Option 2")),
                   submenuNode(QStringLiteral("23"), QStringLiteral("Submenu"),
                               {actionNode(QStringLiteral("231"), QStringLiteral("Option 1")),
                                actionNode(QStringLiteral("232"), QStringLiteral("Option 2")),
                                actionNode(QStringLiteral("233"), QStringLiteral("Option 3"))}),
                   submenuNode(QStringLiteral("24"), QStringLiteral("Submenu 2"),
                               {actionNode(QStringLiteral("241"), QStringLiteral("Option 1")),
                                actionNode(QStringLiteral("242"), QStringLiteral("Option 2")),
                                actionNode(QStringLiteral("243"), QStringLiteral("Option 3"))})},
                  app),
      submenuNode(QStringLiteral("3"), QStringLiteral("Navigation Three"),
                  {actionNode(QStringLiteral("31"), QStringLiteral("Option 1")),
                   actionNode(QStringLiteral("32"), QStringLiteral("Option 2")),
                   actionNode(QStringLiteral("33"), QStringLiteral("Option 3")),
                   actionNode(QStringLiteral("34"), QStringLiteral("Option 4"))},
                  setting),
  };
}

QVector<MenuNode> switchModeNodes() {
  const auto mail = outlined_icons::Mail();
  const auto calendar = outlined_icons::Calendar();
  const auto app = outlined_icons::Appstore();
  const auto setting = outlined_icons::Setting();
  const auto link = outlined_icons::Link();

  return {
      actionNode(QStringLiteral("1"), QStringLiteral("Navigation One"), mail),
      actionNode(QStringLiteral("2"), QStringLiteral("Navigation Two"), calendar),
      submenuNode(QStringLiteral("sub1"), QStringLiteral("Navigation Two"),
                  {actionNode(QStringLiteral("3"), QStringLiteral("Option 3")),
                   actionNode(QStringLiteral("4"), QStringLiteral("Option 4")),
                   submenuNode(QStringLiteral("sub1-2"), QStringLiteral("Submenu"),
                               {actionNode(QStringLiteral("5"), QStringLiteral("Option 5")),
                                actionNode(QStringLiteral("6"), QStringLiteral("Option 6"))})},
                  app),
      submenuNode(QStringLiteral("sub2"), QStringLiteral("Navigation Three"),
                  {actionNode(QStringLiteral("7"), QStringLiteral("Option 7")),
                   actionNode(QStringLiteral("8"), QStringLiteral("Option 8")),
                   actionNode(QStringLiteral("9"), QStringLiteral("Option 9")),
                   actionNode(QStringLiteral("10"), QStringLiteral("Option 10"))},
                  setting),
      actionNode(QStringLiteral("link"), QStringLiteral("Ant Design"), link),
  };
}

class DebugMenuItemDelegate final : public AdNavigationMenuItemDelegate {
 public:
  explicit DebugMenuItemDelegate(AdNavigationMenu* owner, QObject* parent = nullptr)
      : AdNavigationMenuItemDelegate(owner, parent) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    AdNavigationMenuItemDelegate::paint(painter, option, index);
    if (!painter || !index.isValid()) {
      return;
    }
    const auto nodeKind =
        static_cast<AdNavigationMenu::NodeKind>(index.data(AdNavigationMenu::NodeKindRole).toInt());
    if (nodeKind == AdNavigationMenu::NodeKind::Group ||
        nodeKind == AdNavigationMenu::NodeKind::Separator) {
      return;
    }

    const bool submenu = index.model() && index.model()->rowCount(index) > 0;
    painter->save();
    if (submenu) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(QColor(255, 255, 255, 40));
      painter->drawRoundedRect(option.rect.adjusted(3, 2, -3, -2), 6, 6);
    } else {
      painter->setPen(QPen(QColor(22, 119, 255, 180), 1));
      painter->drawLine(option.rect.left() + 10, option.rect.bottom() - 2, option.rect.right() - 10,
                        option.rect.bottom() - 2);
    }
    painter->restore();
  }
};

class CardPopupFactory final : public AdNavigationMenuPopupFactory {
 public:
  explicit CardPopupFactory(QObject* parent = nullptr) : AdNavigationMenuPopupFactory(parent) {}

  QWidget* createPopup(const AdNavigationMenuPopupContext& context, QWidget* defaultPopup,
                       QWidget* parent) override {
    if (!defaultPopup) {
      return nullptr;
    }

    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("customPopupPanel"));
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setFrameShadow(QFrame::Plain);
    panel->setLineWidth(1);
    panel->setAutoFillBackground(true);

    QPalette palette = panel->palette();
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Mid, QColor(QStringLiteral("#f0f0f0")));
    panel->setPalette(palette);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    auto* title = new QLabel(context.submenuIndex.data(Qt::DisplayRole).toString(), panel);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    if (defaultPopup->parentWidget() != panel) {
      defaultPopup->setParent(panel);
    }
    defaultPopup->setMinimumWidth(320);
    layout->addWidget(defaultPopup);
    return panel;
  }
};

}  // namespace

MenuDocsPage::MenuDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Menu");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "Ant Design-style navigation and contextual action menus with Qt-native state and "
      "interaction behavior.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(root, "Top Navigation", "Demo: horizontal.tsx", buildHorizontalDemo(false));
  addSection(root, "Top Navigation (Dark)", "Demo: horizontal-dark.tsx", buildHorizontalDemo(true));
  addSection(root, "Inline Menu", "Demo: inline.tsx", buildInlineDemo());
  addSection(root, "Collapsed Inline Menu", "Demo: inline-collapsed.tsx",
             buildInlineCollapsedDemo());
  addSection(root, "Collapsed Menu Tooltip", "Demo: tooltip.tsx", buildTooltipDemo());
  addSection(root, "Open Current Submenu Only", "Demo: sider-current.tsx", buildSiderCurrentDemo());
  addSection(root, "Vertical Popup Menu", "Demo: vertical.tsx", buildVerticalDemo());
  addSection(
      root, "Context Menu",
      "A native Qt action menu with Ant Design styling, opened from a widget's context-menu event.",
      buildContextMenuDemo());
  addSection(root, "Color Scheme", "Demo: theme.tsx", buildColorSchemeDemo());
  addSection(root, "Popup Color Scheme", "Demo: submenu-theme.tsx", buildPopupColorSchemeDemo());
  addSection(root, "Dynamic Mode Switch", "Demo: switch-mode.tsx", buildSwitchModeDemo());
  addSection(root, "Semantic Styling (styles/classNames)", "Demo: style-class.tsx",
             buildStyleClassDemo());
  addSection(root, "Style Debugging", "Demo: style-debug.tsx", buildStyleDebugDemo());
  addSection(root, "Menu v4 Style", "Demo: menu-v4.tsx", buildMenuV4Demo());
  addSection(root, "Component Token", "Demo: component-token.tsx", buildComponentTokenDemo());
  addSection(root, "Extra Content / Danger Item / Divider", "Demo: extra-style.tsx",
             buildExtraStyleDemo());
  addSection(root, "Custom Popup Render", "Demo: custom-popup-render.tsx",
             buildCustomPopupRenderDemo());
  addSection(root, "Semantic DOM Comparison", "Demo: _semantic.tsx", buildSemanticDemo());
  addSection(root, "API Overview",
             "Qt-first entry points are preferred and the menu now focuses on model/selection "
             "ownership, delegates, and popup factories.",
             buildApiOverview());

  root->addStretch();
}

const QVector<QWidget*>& MenuDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& MenuDocsPage::defaultSectionTitles() { return menuDocSectionTitles(); }

const QStringList& MenuDocsPage::sectionTitles() const { return titles_; }

void MenuDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
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

QWidget* MenuDocsPage::buildHorizontalDemo(bool dark) {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Horizontal);
  menu->setColorScheme(dark ? AdNavigationMenu::ColorScheme::Dark
                            : AdNavigationMenu::ColorScheme::Light);
  auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, horizontalNodes()));
  selectMenuId(menu, selectionModel, QStringLiteral("mail"));
  menu->setMinimumWidth(700);

  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildInlineDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Inline);
  auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, inlineNodes()));
  expandMenuIds(menu, {QStringLiteral("sub1")});
  selectMenuId(menu, selectionModel, QStringLiteral("1"));
  menu->setFixedWidth(256);

  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildInlineCollapsedDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* toggle = new QPushButton("Toggle Collapsed");
  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Inline);
  menu->setColorScheme(AdNavigationMenu::ColorScheme::Dark);
  auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, collapsedInlineNodes()));
  expandMenuIds(menu, {QStringLiteral("sub1")});
  selectMenuId(menu, selectionModel, QStringLiteral("1"));
  menu->setFixedWidth(menu->sizeHint().width());

  connect(toggle, &QPushButton::clicked, this, [menu]() {
    menu->setCollapsed(!menu->collapsed());
    menu->setFixedWidth(menu->sizeHint().width());
  });

  layout->addWidget(toggle, 0, Qt::AlignLeft);
  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildTooltipDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* controls = new QHBoxLayout();
  auto* collapseBtn = new QPushButton("Toggle");
  auto* tooltipCheck = new QCheckBox("Tooltip Enabled");
  tooltipCheck->setChecked(true);
  controls->addWidget(collapseBtn);
  controls->addWidget(tooltipCheck);
  controls->addStretch();

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Inline);
  menu->setColorScheme(AdNavigationMenu::ColorScheme::Dark);
  auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, collapsedInlineNodes()));
  expandMenuIds(menu, {QStringLiteral("sub1")});
  selectMenuId(menu, selectionModel, QStringLiteral("1"));
  menu->setFixedWidth(menu->sizeHint().width());

  connect(collapseBtn, &QPushButton::clicked, this, [menu]() {
    menu->setCollapsed(!menu->collapsed());
    menu->setFixedWidth(menu->sizeHint().width());
  });
  connect(tooltipCheck, &QCheckBox::toggled, menu, &AdNavigationMenu::setTooltipEnabled);

  layout->addLayout(controls);
  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildSiderCurrentDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Inline);
  auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, siderCurrentNodes()));
  expandMenuIds(menu, {QStringLiteral("2"), QStringLiteral("23")});
  selectMenuId(menu, selectionModel, QStringLiteral("231"));
  menu->setFixedWidth(256);

  connect(menu, &AdNavigationMenu::expanded, menu, [menu](const QModelIndex& index) {
    const QModelIndex parent = index.parent();
    if (!menu->model()) {
      return;
    }
    const int rowCount = menu->model()->rowCount(parent);
    for (int row = 0; row < rowCount; ++row) {
      const QModelIndex sibling = menu->model()->index(row, 0, parent);
      if (sibling != index && menu->model()->rowCount(sibling) > 0) {
        menu->collapse(sibling);
      }
    }
  });

  layout->addWidget(menu);
  layout->addWidget(makeHintLabel("Only one submenu stays open at the same depth."));
  return box;
}

QWidget* MenuDocsPage::buildVerticalDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Vertical);
  attachMenuModel(menu, createMenuModel(menu, inlineNodes()));
  menu->setFixedWidth(256);

  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildContextMenuDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* target = new QFrame(box);
  target->setObjectName(QStringLiteral("context-menu-demo-target"));
  target->setFrameShape(QFrame::StyledPanel);
  target->setMinimumHeight(96);
  target->setAutoFillBackground(true);
  auto* targetLayout = new QHBoxLayout(target);
  targetLayout->setContentsMargins(16, 14, 16, 14);
  targetLayout->setSpacing(12);

  auto* fileIcon = new QLabel(target);
  fileIcon->setFixedSize(28, 28);
  fileIcon->setPixmap(adqt::icons::makeIcon(outlined_icons::File()).pixmap(22, 22));
  fileIcon->setAlignment(Qt::AlignCenter);
  fileIcon->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  auto* textColumn = new QVBoxLayout();
  textColumn->setContentsMargins(0, 0, 0, 0);
  textColumn->setSpacing(2);
  auto* fileName = new QLabel(QStringLiteral("Project notes.md"), target);
  QFont fileNameFont = fileName->font();
  fileNameFont.setBold(true);
  fileName->setFont(fileNameFont);
  fileName->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* fileMeta =
      new QLabel(QStringLiteral("Markdown document  |  Modified 2 minutes ago"), target);
  fileMeta->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  textColumn->addWidget(fileName);
  textColumn->addWidget(fileMeta);

  targetLayout->addWidget(fileIcon, 0, Qt::AlignVCenter);
  targetLayout->addLayout(textColumn, 1);

  auto* status = new QLabel(QStringLiteral("Ready"), box);
  status->setTextInteractionFlags(Qt::TextSelectableByMouse);

  auto* menu = new AdContextMenu(target);
  menu->setTriggerWidget(target);
  menu->addItem(QStringLiteral("Rename"), outlined_icons::Edit(), QKeySequence(Qt::Key_F2));
  menu->addItem(QStringLiteral("Duplicate"), outlined_icons::Copy(),
                QKeySequence(Qt::CTRL | Qt::Key_D));

  QAction* keepAvailable = menu->addItem(QStringLiteral("Keep available offline"));
  keepAvailable->setCheckable(true);
  keepAvailable->setChecked(true);

  auto* moveMenu = menu->addSubMenu(QStringLiteral("Move to"), outlined_icons::FolderOpen());
  moveMenu->addItem(QStringLiteral("Design"), outlined_icons::Folder());
  moveMenu->addItem(QStringLiteral("Engineering"), outlined_icons::Folder());
  moveMenu->addItem(QStringLiteral("Archive"), outlined_icons::Folder());

  menu->addSeparator();
  QAction* deleteAction =
      menu->addItem(QStringLiteral("Move to trash"), outlined_icons::IconDelete());
  menu->setActionDanger(deleteAction);

  connect(menu, &QMenu::triggered, status, [status](QAction* action) {
    if (action) {
      status->setText(QStringLiteral("Last action: %1").arg(action->text()));
    }
  });

  demo::bindThemeRefresh(target, [target, fileMeta, status]() {
    const auto map = demo::resolveTheme(target);
    QPalette targetPalette = target->palette();
    targetPalette.setColor(QPalette::Window, demo::themeColorOr(map.colorBgContainer,
                                                                QColor(QStringLiteral("#ffffff"))));
    targetPalette.setColor(QPalette::WindowText,
                           demo::themeColorOr(map.colorText, QColor(QStringLiteral("#141414"))));
    target->setPalette(targetPalette);

    QPalette secondaryPalette = fileMeta->palette();
    secondaryPalette.setColor(
        QPalette::WindowText,
        demo::themeColorOr(map.colorTextSecondary, QColor(QStringLiteral("#8c8c8c"))));
    fileMeta->setPalette(secondaryPalette);
    status->setPalette(secondaryPalette);
  });

  layout->addWidget(target);
  layout->addWidget(status);
  return box;
}

QWidget* MenuDocsPage::buildColorSchemeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* controls = new QHBoxLayout();
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(8);
  auto* schemeLabel = new QLabel("Menu Scheme:");
  auto* schemeBox = new QComboBox();
  populateColorSchemeBox(schemeBox);

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Inline);
  menu->setColorScheme(AdNavigationMenu::ColorScheme::Inherit);
  auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, inlineNodes()));
  expandMenuIds(menu, {QStringLiteral("sub1")});
  selectMenuId(menu, selectionModel, QStringLiteral("1"));
  menu->setFixedWidth(256);

  controls->addWidget(schemeLabel);
  controls->addWidget(schemeBox);
  controls->addStretch();

  connect(schemeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), menu,
          [menu, schemeBox](int) { menu->setColorScheme(currentColorScheme(schemeBox)); });

  layout->addLayout(controls);
  layout->addWidget(makeHintLabel("Inherit follows the demo window's global theme preset."));
  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildPopupColorSchemeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* controls = new QHBoxLayout();
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(8);
  auto* popupSchemeLabel = new QLabel("Popup Override:");
  auto* popupSchemeBox = new QComboBox();
  populateColorSchemeBox(popupSchemeBox);

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Vertical);
  menu->setColorScheme(AdNavigationMenu::ColorScheme::Inherit);
  menu->setFixedWidth(256);

  const auto rebuildModel = [menu, popupSchemeBox]() {
    const auto mail = outlined_icons::Mail();
    MenuNode sub1 = submenuNode(QStringLiteral("sub1"), QStringLiteral("Navigation One"),
                                {actionNode(QStringLiteral("1"), QStringLiteral("Option 1")),
                                 actionNode(QStringLiteral("2"), QStringLiteral("Option 2")),
                                 actionNode(QStringLiteral("3"), QStringLiteral("Option 3"))},
                                mail);
    const AdNavigationMenu::ColorScheme popupScheme = currentColorScheme(popupSchemeBox);
    if (popupScheme != AdNavigationMenu::ColorScheme::Inherit) {
      sub1.popupColorScheme = popupScheme;
    }

    auto* model =
        createMenuModel(menu, {sub1, actionNode(QStringLiteral("5"), QStringLiteral("Option 5")),
                               actionNode(QStringLiteral("6"), QStringLiteral("Option 6"))});
    auto* selectionModel = attachMenuModel(menu, model);
    selectMenuId(menu, selectionModel, QStringLiteral("1"));
  };

  controls->addWidget(popupSchemeLabel);
  controls->addWidget(popupSchemeBox);
  controls->addStretch();

  connect(popupSchemeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), menu,
          [rebuildModel](int) { rebuildModel(); });

  rebuildModel();

  layout->addLayout(controls);
  layout->addWidget(
      makeHintLabel("Inherit keeps submenu popups on the parent menu's effective scheme."));
  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildSwitchModeDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* modeCheck = new QCheckBox("Vertical Mode");
  auto* themeCheck = new QCheckBox("Dark Scheme");
  auto* controls = new QHBoxLayout();
  controls->addWidget(modeCheck);
  controls->addWidget(themeCheck);
  controls->addStretch();

  auto* menu = new AdNavigationMenu();
  auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, switchModeNodes()));
  menu->setMode(AdNavigationMenu::Mode::Inline);
  menu->setColorScheme(AdNavigationMenu::ColorScheme::Light);
  expandMenuIds(menu, {QStringLiteral("sub1")});
  selectMenuId(menu, selectionModel, QStringLiteral("1"));
  menu->setFixedWidth(256);

  const auto applyState = [menu, modeCheck, themeCheck]() {
    menu->setMode(modeCheck->isChecked() ? AdNavigationMenu::Mode::Vertical
                                         : AdNavigationMenu::Mode::Inline);
    menu->setColorScheme(themeCheck->isChecked() ? AdNavigationMenu::ColorScheme::Dark
                                                 : AdNavigationMenu::ColorScheme::Light);
  };

  connect(modeCheck, &QCheckBox::toggled, menu, [applyState](bool) { applyState(); });
  connect(themeCheck, &QCheckBox::toggled, menu, [applyState](bool) { applyState(); });

  layout->addLayout(controls);
  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  const QVector<MenuNode> nodes = {
      submenuNode(QStringLiteral("SubMenu"), QStringLiteral("Navigation One"),
                  {groupNode(QStringLiteral("g1"), QStringLiteral("Item 1"),
                             {actionNode(QStringLiteral("1"), QStringLiteral("Option 1")),
                              actionNode(QStringLiteral("2"), QStringLiteral("Option 2"))})}),
      actionNode(QStringLiteral("mail"), QStringLiteral("Navigation Two")),
  };

  auto* menu1 = new AdNavigationMenu();
  attachMenuModel(menu1, createMenuModel(menu1, nodes));
  menu1->setMode(AdNavigationMenu::Mode::Vertical);
  menu1->setFixedWidth(520);
  demo::bindThemeRefresh(menu1, [menu1]() {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(menu1);
    AdNavigationMenu::SemanticStyles semantic1;
    semantic1.root.backgroundColor =
        demo::themeColorOr(map.colorBgContainer, QColor(255, 255, 255));
    semantic1.root.borderColor =
        demo::themeColorOr(map.colorBorder, QColor(QStringLiteral("#d9d9d9")));
    semantic1.item.textColor =
        demo::themeColorOr(map.colorPrimary, QColor(QStringLiteral("#1677ff")));
    semantic1.subMenuItemContent.textColor =
        demo::themeColorOr(map.colorWarning, QColor(QStringLiteral("#fa541c")));
    menu1->setSemanticStyles(semantic1);
  });

  auto* menu2 = new AdNavigationMenu();
  attachMenuModel(menu2, createMenuModel(menu2, nodes));
  menu2->setMode(AdNavigationMenu::Mode::Inline);
  expandMenuIds(menu2, {QStringLiteral("SubMenu")});
  menu2->setFixedWidth(520);
  menu2->setSemanticStyleResolver([menu2](const AdNavigationMenu::StyleContext& ctx) {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(menu2);
    AdNavigationMenu::SemanticStyles styles;
    const bool hasSub =
        ctx.model && ctx.model->rowCount() > 0 && ctx.model->rowCount(ctx.model->index(0, 0)) > 0;
    QColor highlighted = demo::themeColorOr(map.colorPrimaryBg, QColor(240, 249, 255));
    highlighted.setAlpha(160);
    styles.root.backgroundColor =
        hasSub ? highlighted : demo::themeColorOr(map.colorBgContainer, QColor(255, 255, 255));
    styles.root.borderColor =
        demo::themeColorOr(map.colorBorder, QColor(QStringLiteral("#d9d9d9")));
    styles.item.textColor = demo::themeColorOr(map.colorPrimary, QColor(QStringLiteral("#1677ff")));
    return styles;
  });

  layout->addWidget(menu1);
  layout->addWidget(menu2);
  return box;
}

QWidget* MenuDocsPage::buildStyleDebugDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* themeCheck = new QCheckBox("Dark");
  themeCheck->setChecked(true);

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Inline);
  menu->setCollapsed(true);
  menu->setColorScheme(AdNavigationMenu::ColorScheme::Dark);
  auto* selectionModel = attachMenuModel(
      menu,
      createMenuModel(
          menu,
          {submenuNode(QStringLiteral("sub1"), QStringLiteral("Navigation One Long Long Long Long"),
                       {actionNode(QStringLiteral("1"), QStringLiteral("Option 1")),
                        actionNode(QStringLiteral("2"), QStringLiteral("Option 2")),
                        actionNode(QStringLiteral("3"), QStringLiteral("Option 3")),
                        actionNode(QStringLiteral("4"), QStringLiteral("Option 4"))},
                       outlined_icons::Mail()),
           submenuNode(QStringLiteral("sub2"), QStringLiteral("Navigation Two"),
                       {actionNode(QStringLiteral("5"), QStringLiteral("Option 5")),
                        actionNode(QStringLiteral("6"), QStringLiteral("Option 6")),
                        submenuNode(QStringLiteral("sub3"), QStringLiteral("Submenu"),
                                    {actionNode(QStringLiteral("7"), QStringLiteral("Option 7")),
                                     actionNode(QStringLiteral("8"), QStringLiteral("Option 8"))})},
                       outlined_icons::Appstore()),
           actionNode(QStringLiteral("11"), QStringLiteral("Option 11")),
           actionNode(QStringLiteral("12"), QStringLiteral("Option 12"))}));
  selectMenuId(menu, selectionModel, QStringLiteral("1"));
  expandMenuIds(menu, {QStringLiteral("sub1")});
  menu->setTooltipEnabled(false);
  menu->setFixedWidth(80);
  menu->setItemDelegate(new DebugMenuItemDelegate(menu, menu));

  connect(themeCheck, &QCheckBox::toggled, menu, [menu](bool checked) {
    menu->setColorScheme(checked ? AdNavigationMenu::ColorScheme::Dark
                                 : AdNavigationMenu::ColorScheme::Light);
  });

  layout->addWidget(themeCheck, 0, Qt::AlignLeft);
  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildMenuV4Demo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* modeCheck = new QCheckBox("Vertical Mode");

  auto* menu = new AdNavigationMenu();
  auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, switchModeNodes()));
  menu->setMode(AdNavigationMenu::Mode::Inline);
  selectMenuId(menu, selectionModel, QStringLiteral("1"));
  expandMenuIds(menu, {QStringLiteral("sub1")});
  menu->setFixedWidth(256);

  AdNavigationMenu::ComponentTokens tokens;
  tokens.metrics.itemBorderRadius = 0;
  tokens.metrics.subMenuItemBorderRadius = 0;
  tokens.metrics.itemMarginInline = 0;
  tokens.metrics.activeBarWidth = 3;
  tokens.colors.shared.itemHoverText = QColor(QStringLiteral("#1890ff"));
  tokens.colors.shared.itemSelectedText = QColor(QStringLiteral("#1890ff"));
  tokens.colors.shared.itemSelectedBackground = QColor(QStringLiteral("#e6f7ff"));
  tokens.colors.shared.itemHoverBackground = QColor(Qt::transparent);
  tokens.colors.shared.horizontalItemHoverBackground = QColor(Qt::transparent);
  menu->setComponentTokens(tokens);

  connect(modeCheck, &QCheckBox::toggled, menu, [menu](bool checked) {
    menu->setMode(checked ? AdNavigationMenu::Mode::Vertical : AdNavigationMenu::Mode::Inline);
  });

  layout->addWidget(modeCheck, 0, Qt::AlignLeft);
  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildComponentTokenDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* group1 = new QFrame();
  auto* group1Layout = new QVBoxLayout(group1);
  group1Layout->setContentsMargins(0, 0, 0, 0);
  group1Layout->setSpacing(8);

  auto* menu1 = new AdNavigationMenu();
  menu1->setMode(AdNavigationMenu::Mode::Horizontal);
  menu1->setColorScheme(AdNavigationMenu::ColorScheme::Dark);
  auto* menu1Selection = attachMenuModel(menu1, createMenuModel(menu1, horizontalNodes()));
  selectMenuId(menu1, menu1Selection, QStringLiteral("mail"));
  menu1->setMinimumWidth(700);
  AdNavigationMenu::ComponentTokens t1;
  t1.colors.shared.popupBackground = QColor(QStringLiteral("yellow"));
  t1.colors.dark.popupBackground = QColor(QStringLiteral("red"));
  menu1->setComponentTokens(t1);

  auto* menu1InlineCollapsed = new AdNavigationMenu();
  menu1InlineCollapsed->setMode(AdNavigationMenu::Mode::Inline);
  menu1InlineCollapsed->setColorScheme(AdNavigationMenu::ColorScheme::Dark);
  menu1InlineCollapsed->setCollapsed(true);
  auto* collapsedSelection = attachMenuModel(
      menu1InlineCollapsed, createMenuModel(menu1InlineCollapsed, collapsedInlineNodes()));
  selectMenuId(menu1InlineCollapsed, collapsedSelection, QStringLiteral("1"));
  expandMenuIds(menu1InlineCollapsed, {QStringLiteral("sub1")});
  menu1InlineCollapsed->setFixedWidth(menu1InlineCollapsed->sizeHint().width());
  menu1InlineCollapsed->setComponentTokens(t1);

  group1Layout->addWidget(menu1);
  group1Layout->addWidget(menu1InlineCollapsed);

  auto* group2 = new QFrame();
  auto* group2Layout = new QVBoxLayout(group2);
  group2Layout->setContentsMargins(0, 0, 0, 0);
  group2Layout->setSpacing(8);

  auto* menu2 = new AdNavigationMenu();
  menu2->setMode(AdNavigationMenu::Mode::Horizontal);
  auto* menu2Selection = attachMenuModel(menu2, createMenuModel(menu2, horizontalNodes()));
  selectMenuId(menu2, menu2Selection, QStringLiteral("mail"));
  menu2->setMinimumWidth(700);
  demo::bindThemeRefresh(menu2, [menu2]() {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(menu2);
    AdNavigationMenu::ComponentTokens t2;
    t2.metrics.horizontalItemBorderRadius = 6;
    t2.colors.shared.popupBackground = QColor(QStringLiteral("red"));
    t2.colors.shared.horizontalItemHoverBackground =
        demo::themeColorOr(map.colorFillAlter, QColor(QStringLiteral("#f5f5f5")));
    menu2->setComponentTokens(t2);
  });
  group2Layout->addWidget(menu2);

  auto* group3 = new QFrame();
  auto* group3Layout = new QVBoxLayout(group3);
  group3Layout->setContentsMargins(0, 0, 0, 0);
  group3Layout->setSpacing(8);

  auto* menu3 = new AdNavigationMenu();
  menu3->setMode(AdNavigationMenu::Mode::Inline);
  menu3->setColorScheme(AdNavigationMenu::ColorScheme::Dark);
  auto* menu3Selection = attachMenuModel(menu3, createMenuModel(menu3, collapsedInlineNodes()));
  selectMenuId(menu3, menu3Selection, QStringLiteral("1"));
  expandMenuIds(menu3, {QStringLiteral("sub1")});
  menu3->setFixedWidth(256);
  AdNavigationMenu::ComponentTokens t3;
  t3.colors.dark.itemText = QColor(QStringLiteral("#91daff"));
  t3.colors.dark.itemBackground = QColor(QStringLiteral("#d48806"));
  t3.colors.dark.subMenuItemBackground = QColor(QStringLiteral("#faad14"));
  t3.colors.dark.itemSelectedText = QColor(QStringLiteral("#ffccc7"));
  t3.colors.dark.itemSelectedBackground = QColor(QStringLiteral("#52c41a"));
  menu3->setComponentTokens(t3);
  group3Layout->addWidget(menu3);

  layout->addWidget(group1);
  layout->addWidget(group2);
  layout->addWidget(group3);
  return box;
}

QWidget* MenuDocsPage::buildExtraStyleDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  MenuNode sub1 =
      submenuNode(QStringLiteral("sub1"), QStringLiteral("Navigation One"),
                  {actionNode(QStringLiteral("1"), QStringLiteral("Option 1 + icon")),
                   actionNode(QStringLiteral("2"), QStringLiteral("Option 2")),
                   actionNode(QStringLiteral("3"), QStringLiteral("Link Option"), {}, true)},
                  outlined_icons::Mail());
  sub1.children[1].extra = QStringLiteral("Ctrl+P");

  auto* menu1 = new AdNavigationMenu();
  menu1->setMode(AdNavigationMenu::Mode::Inline);
  auto* menu1Selection = attachMenuModel(menu1, createMenuModel(menu1, {sub1}));
  expandMenuIds(menu1, {QStringLiteral("sub1")});
  selectMenuId(menu1, menu1Selection, QStringLiteral("1"));
  menu1->setFixedWidth(256);

  QVector<MenuNode> nodes2 = {
      actionNode(QStringLiteral("users"), QStringLiteral("Users")),
      actionNode(QStringLiteral("profile"), QStringLiteral("Profile")),
      separatorNode(QStringLiteral("d1"), true),
      actionNode(QStringLiteral("danger"), QStringLiteral("Danger Action")),
  };
  nodes2[0].extra = QStringLiteral("Ctrl+U");
  nodes2[1].extra = QStringLiteral("Ctrl+P");
  nodes2[3].danger = true;

  auto* menu2 = new AdNavigationMenu();
  attachMenuModel(menu2, createMenuModel(menu2, nodes2));
  menu2->setColorScheme(AdNavigationMenu::ColorScheme::Dark);
  menu2->setFixedWidth(256);

  layout->addWidget(menu1);
  layout->addWidget(menu2);
  return box;
}

QWidget* MenuDocsPage::buildCustomPopupRenderDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  QVector<MenuNode> nodes = {
      actionNode(QStringLiteral("home"), QStringLiteral("Home")),
      submenuNode(QStringLiteral("features"), QStringLiteral("Features"),
                  {actionNode(QStringLiteral("getting-started"), QStringLiteral("Getting Started")),
                   actionNode(QStringLiteral("components"), QStringLiteral("Components")),
                   actionNode(QStringLiteral("templates"), QStringLiteral("Templates"))}),
      submenuNode(QStringLiteral("resources"), QStringLiteral("Resources"),
                  {actionNode(QStringLiteral("blog"), QStringLiteral("Blog")),
                   actionNode(QStringLiteral("community"), QStringLiteral("Community"))}),
  };

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Horizontal);
  attachMenuModel(menu, createMenuModel(menu, nodes));
  menu->setMinimumWidth(700);
  AdNavigationMenu::ComponentTokens popupTokens;
  popupTokens.colors.shared.horizontalItemSelectedText = QColor(QStringLiteral("#1677ff"));
  popupTokens.colors.shared.horizontalItemHoverText = QColor(QStringLiteral("#1677ff"));
  menu->setComponentTokens(popupTokens);
  menu->setPopupFactory(new CardPopupFactory(menu));

  layout->addWidget(menu);
  return box;
}

QWidget* MenuDocsPage::buildSemanticDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* modeBox = new QComboBox();
  modeBox->addItem("horizontal", static_cast<int>(AdNavigationMenu::Mode::Horizontal));
  modeBox->addItem("vertical", static_cast<int>(AdNavigationMenu::Mode::Vertical));
  modeBox->addItem("inline", static_cast<int>(AdNavigationMenu::Mode::Inline));
  modeBox->setCurrentIndex(0);

  auto* menu = new AdNavigationMenu();
  menu->setMode(AdNavigationMenu::Mode::Horizontal);
  menu->setSemanticStyleResolver([menu](const AdNavigationMenu::StyleContext& ctx) {
    const adqt::theme::ThemeMapToken map = demo::resolveTheme(menu);
    AdNavigationMenu::SemanticStyles styles;
    if (ctx.mode == AdNavigationMenu::Mode::Horizontal) {
      styles.root.backgroundColor =
          demo::themeColorOr(map.colorSuccessBg, QColor(QStringLiteral("#f6ffed")));
      styles.item.textColor =
          demo::themeColorOr(map.colorSuccess, QColor(QStringLiteral("#389e0d")));
    } else if (ctx.mode == AdNavigationMenu::Mode::Vertical) {
      styles.root.backgroundColor =
          demo::themeColorOr(map.colorWarningBg, QColor(QStringLiteral("#fff7e6")));
      styles.item.textColor =
          demo::themeColorOr(map.colorWarning, QColor(QStringLiteral("#d46b08")));
    } else {
      styles.root.backgroundColor =
          demo::themeColorOr(map.colorPrimaryBg, QColor(QStringLiteral("#e6f4ff")));
      styles.item.textColor =
          demo::themeColorOr(map.colorPrimary, QColor(QStringLiteral("#1677ff")));
    }
    styles.root.borderColor =
        demo::themeColorOr(map.colorBorder, QColor(QStringLiteral("#d9d9d9")));
    return styles;
  });

  const auto applySemanticDemoWidth = [menu](AdNavigationMenu::Mode mode) {
    if (mode == AdNavigationMenu::Mode::Horizontal) {
      menu->setMinimumWidth(560);
      menu->setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
      menu->setMinimumWidth(260);
      menu->setMaximumWidth(260);
    }
  };

  const auto rebuildForMode = [menu, applySemanticDemoWidth](AdNavigationMenu::Mode mode) {
    QVector<MenuNode> nodes = horizontalNodes();
    if (mode != AdNavigationMenu::Mode::Horizontal) {
      nodes.append(groupNode(QStringLiteral("grp"), QStringLiteral("Group"),
                             {actionNode(QStringLiteral("13"), QStringLiteral("Option 13")),
                              actionNode(QStringLiteral("14"), QStringLiteral("Option 14"))}));
    }
    auto* selectionModel = attachMenuModel(menu, createMenuModel(menu, nodes));
    menu->setMode(mode);
    selectMenuId(menu, selectionModel, QStringLiteral("mail"));
    if (mode != AdNavigationMenu::Mode::Horizontal) {
      expandMenuIds(menu, {QStringLiteral("SubMenu")});
    }
    applySemanticDemoWidth(mode);
  };

  rebuildForMode(AdNavigationMenu::Mode::Horizontal);

  connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), menu,
          [modeBox, rebuildForMode](int) {
            rebuildForMode(static_cast<AdNavigationMenu::Mode>(modeBox->currentData().toInt()));
          });

  auto* slotsLabel = makeHintLabel(
      "Semantic slots: root / item / itemIcon / itemContent / itemTitle / list / popup / "
      "subMenu.item / subMenu.itemTitle / subMenu.list / subMenu.itemIcon / subMenu.itemContent");

  layout->addWidget(modeBox, 0, Qt::AlignLeft);
  layout->addWidget(menu);
  layout->addWidget(slotsLabel);
  return box;
}

QWidget* MenuDocsPage::buildApiOverview() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* hint = makeHintLabel(
      "AdNavigationMenu now prefers setModel(...) + setSelectionModel(...) + setCurrentIndex(...). "
      "Breaking changes removed the old Ant-style item arrays and popup/paint hook shims in favor "
      "of delegates and popup factories.");
  hint->setWordWrap(true);
  layout->addWidget(hint);

  auto* grid = new QGridLayout();
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(16);
  grid->setVerticalSpacing(8);

  const QVector<QPair<QString, QString>> rows = {
      {QStringLiteral("AdNavigationMenu"),
       QStringLiteral("Qt-first navigation menu widget with Ant Design visual parity")},
      {QStringLiteral("AdContextMenu"),
       QStringLiteral("QMenu-compatible popup with right-click trigger binding and Ant Design "
                      "action styling")},
      {QStringLiteral("AdContextMenu::setTriggerWidget"),
       QStringLiteral("installs context-menu handling on any QWidget")},
      {QStringLiteral("AdContextMenu action metadata"),
       QStringLiteral("Ant icon references and danger state, alongside native QAction "
                      "shortcuts/checkable/disabled state")},
      {QStringLiteral("mode"), QStringLiteral("vertical | horizontal | inline")},
      {QStringLiteral("colorScheme"), QStringLiteral("inherit | light | dark")},
      {QStringLiteral("model / selectionModel / currentIndex"),
       QStringLiteral("primary state ownership and synchronization path")},
      {QStringLiteral("selectionMode"), QStringLiteral("no selection | single | multi")},
      {QStringLiteral("collapsed / indentation"),
       QStringLiteral("inline collapse behavior and nested indentation in pixels")},
      {QStringLiteral("submenuTrigger"), QStringLiteral("hover | click")},
      {QStringLiteral("submenuOpenDelayMs / submenuCloseDelayMs"),
       QStringLiteral("popup open and close delay in milliseconds")},
      {QStringLiteral("tooltipEnabled"), QStringLiteral("inline-collapsed tooltip behavior")},
      {QStringLiteral("componentTokens"),
       QStringLiteral("component token overrides: metrics + colors.shared/light/dark")},
      {QStringLiteral("semanticStyles / semanticStyleResolver"),
       QStringLiteral("semantic slot style overrides")},
      {QStringLiteral("itemDelegate"),
       QStringLiteral("custom row rendering through a Qt item delegate")},
      {QStringLiteral("popupFactory"),
       QStringLiteral("custom popup container factory for submenu popups")},
      {QStringLiteral("expandIcon / popupOffset"),
       QStringLiteral("submenu indicator icon and popup placement adjustments")},
      {QStringLiteral("signals: activated / expanded / collapsed / currentIndexChanged"),
       QStringLiteral("Qt-style interaction and state change notifications")},
      {QStringLiteral("removed APIs"),
       QStringLiteral(
           "setItems, key-based selection/open state, popupRender, and paint hooks were removed")},
  };

  for (int i = 0; i < rows.size(); ++i) {
    auto* name = new QLabel(rows.at(i).first);
    auto* desc = new QLabel(rows.at(i).second);
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    desc->setWordWrap(true);
    QFont nameFont = name->font();
    nameFont.setBold(true);
    name->setFont(nameFont);
    grid->addWidget(name, i, 0, Qt::AlignTop);
    grid->addWidget(desc, i, 1);
  }

  layout->addLayout(grid);
  return box;
}
