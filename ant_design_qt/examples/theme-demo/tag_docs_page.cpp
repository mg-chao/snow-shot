#include "tag_docs_page.h"

#include "demo_theme_utils.h"

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "antd_icons.h"
#include "widgets/detail/flow_layout.h"

using adqt::widgets::AdTag;
using adqt::widgets::AdTagGroup;
namespace outlined_icons = adqt::icons::antd::outlined;
namespace flow_detail = adqt::widgets::detail;

namespace {

QWidget* makeWrapPanel(const QList<QWidget*>& widgets = {}, int spacing = 8) {
  auto* box = new QWidget();
  auto* layout = new flow_detail::FlowLayout();
  box->setLayout(layout);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setHorizontalSpacing(spacing);
  layout->setVerticalSpacing(spacing);
  for (QWidget* widget : widgets) {
    if (widget) {
      layout->addWidget(widget);
    }
  }
  return box;
}

AdTag::ColorScheme colorSchemeFromName(const QString& name) {
  const QString key = name.trimmed().toLower();
  if (key == QStringLiteral("blue")) return AdTag::ColorScheme::Blue;
  if (key == QStringLiteral("purple")) return AdTag::ColorScheme::Purple;
  if (key == QStringLiteral("cyan")) return AdTag::ColorScheme::Cyan;
  if (key == QStringLiteral("green")) return AdTag::ColorScheme::Green;
  if (key == QStringLiteral("magenta")) return AdTag::ColorScheme::Magenta;
  if (key == QStringLiteral("pink")) return AdTag::ColorScheme::Pink;
  if (key == QStringLiteral("red")) return AdTag::ColorScheme::Red;
  if (key == QStringLiteral("orange")) return AdTag::ColorScheme::Orange;
  if (key == QStringLiteral("yellow")) return AdTag::ColorScheme::Yellow;
  if (key == QStringLiteral("volcano")) return AdTag::ColorScheme::Volcano;
  if (key == QStringLiteral("geekblue")) return AdTag::ColorScheme::Geekblue;
  if (key == QStringLiteral("lime")) return AdTag::ColorScheme::Lime;
  if (key == QStringLiteral("gold")) return AdTag::ColorScheme::Gold;
  if (key == QStringLiteral("success")) return AdTag::ColorScheme::Success;
  if (key == QStringLiteral("processing")) return AdTag::ColorScheme::Processing;
  if (key == QStringLiteral("warning")) return AdTag::ColorScheme::Warning;
  if (key == QStringLiteral("error")) return AdTag::ColorScheme::Error;
  return AdTag::ColorScheme::Default;
}

void applyTagColor(AdTag* tag, const QString& colorName) {
  if (!tag) {
    return;
  }
  const QColor custom(colorName);
  if (custom.isValid()) {
    tag->setColorScheme(AdTag::ColorScheme::Custom);
    tag->setCustomColor(custom);
    return;
  }
  tag->setColorScheme(colorSchemeFromName(colorName));
}

AdTag* makeTag(const QString& text, const QString& colorName = QString(),
               AdTag::Variant variant = AdTag::Variant::Filled, QWidget* parent = nullptr) {
  auto* tag = new AdTag(text, parent);
  tag->setVariant(variant);
  if (!colorName.trimmed().isEmpty()) {
    applyTagColor(tag, colorName);
  }
  return tag;
}

QWidget* makeFormRow(const QString& labelText, QWidget* content) {
  auto* rowWidget = new QWidget();
  auto* row = new QHBoxLayout(rowWidget);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(10);

  auto* label = new QLabel(labelText);
  label->setFixedWidth(72);
  row->addWidget(label, 0, Qt::AlignTop);
  row->addWidget(content, 1);
  return rowWidget;
}

QString shortenedTagText(const QString& value) {
  return value.size() > 20 ? value.left(20) + QStringLiteral("...") : value;
}

class EditableTagDemo final : public QWidget {
 public:
  explicit EditableTagDemo(QWidget* parent = nullptr) : QWidget(parent) {
    tags_ = {QStringLiteral("Unremovable"), QStringLiteral("Tag 2"), QStringLiteral("Tag 3")};

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    container_ = new QWidget(this);
    layout_ = new flow_detail::FlowLayout();
    container_->setLayout(layout_);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setHorizontalSpacing(8);
    layout_->setVerticalSpacing(8);

    root->addWidget(container_);
    root->addWidget(
        makeHintLabel("Double-click a removable tag to edit it. The dashed New Tag button is a "
                      "Qt-native recreation of control.tsx."));

    rebuild();
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event && event->type() == QEvent::MouseButtonDblClick) {
      if (auto* tag = qobject_cast<AdTag*>(watched)) {
        const int index = tag->property("demoTagIndex").toInt();
        if (index > 0 && index < tags_.size()) {
          startEditing(index);
          return true;
        }
      }
    }
    return QWidget::eventFilter(watched, event);
  }

 private:
  void rebuild() {
    while (layout_ && layout_->count() > 0) {
      QLayoutItem* item = layout_->takeAt(0);
      if (item) {
        if (QWidget* widget = item->widget()) {
          widget->deleteLater();
        }
        delete item;
      }
    }
    activeInput_ = nullptr;

    for (int i = 0; i < tags_.size(); ++i) {
      if (editIndex_ == i) {
        auto* input = new QLineEdit(container_);
        input->setFixedWidth(92);
        input->setText(editValue_);
        connect(input, &QLineEdit::textChanged, this,
                [this](const QString& value) { editValue_ = value; });
        connect(input, &QLineEdit::editingFinished, this, [this]() { confirmEdit(); });
        layout_->addWidget(input);
        activeInput_ = input;
        continue;
      }

      auto* tag = new AdTag(shortenedTagText(tags_.at(i)), container_);
      tag->setToolTip(tags_.at(i));
      tag->setProperty("demoTagIndex", i);
      if (i != 0) {
        tag->setClosable(true);
        connect(tag, &AdTag::closeRequested, this, [this, i]() {
          if (i >= 0 && i < tags_.size()) {
            tags_.removeAt(i);
            rebuild();
          }
        });
      }
      tag->installEventFilter(this);
      layout_->addWidget(tag);
    }

    if (newInputVisible_) {
      auto* input = new QLineEdit(container_);
      input->setFixedWidth(92);
      input->setText(newValue_);
      connect(input, &QLineEdit::textChanged, this,
              [this](const QString& value) { newValue_ = value; });
      connect(input, &QLineEdit::editingFinished, this, [this]() { confirmNewTag(); });
      layout_->addWidget(input);
      activeInput_ = input;
    } else {
      auto* addTag = new AdTag(QStringLiteral("New Tag"), container_);
      addTag->setVariant(AdTag::Variant::Outlined);
      addTag->setBorderStyle(AdTag::BorderStyle::Dashed);
      addTag->setIconRef(outlined_icons::Plus());
      addTag->setSemanticStyleResolver([addTag](const AdTag::StyleContext&) {
        AdTag::SemanticStyles styles;
        const adqt::theme::ThemeMapToken map = demo::resolveTheme(addTag);
        styles.root.backgroundColor = demo::themeColorOr(map.colorBgContainer, QColor("#ffffff"));
        return styles;
      });
      connect(addTag, &QAbstractButton::clicked, this, [this]() {
        newInputVisible_ = true;
        newValue_.clear();
        editIndex_ = -1;
        rebuild();
        focusInputLater();
      });
      layout_->addWidget(addTag);
    }
  }

  void startEditing(int index) {
    editIndex_ = index;
    editValue_ = tags_.value(index);
    newInputVisible_ = false;
    rebuild();
    focusInputLater();
  }

  void confirmEdit() {
    if (editIndex_ < 0 || editIndex_ >= tags_.size()) {
      return;
    }
    const QString trimmed = editValue_.trimmed();
    if (!trimmed.isEmpty()) {
      tags_[editIndex_] = trimmed;
    }
    editIndex_ = -1;
    editValue_.clear();
    rebuild();
  }

  void confirmNewTag() {
    if (!newInputVisible_) {
      return;
    }
    const QString trimmed = newValue_.trimmed();
    if (!trimmed.isEmpty() && !tags_.contains(trimmed)) {
      tags_.append(trimmed);
    }
    newInputVisible_ = false;
    newValue_.clear();
    rebuild();
  }

  void focusInputLater() {
    QTimer::singleShot(0, this, [this]() {
      if (activeInput_) {
        activeInput_->setFocus();
        activeInput_->selectAll();
      }
    });
  }

  QWidget* container_ = nullptr;
  flow_detail::FlowLayout* layout_ = nullptr;
  QStringList tags_;
  bool newInputVisible_ = false;
  int editIndex_ = -1;
  QString newValue_;
  QString editValue_;
  QPointer<QLineEdit> activeInput_;
};

}  // namespace

TagDocsPage::TagDocsPage(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 16, 16, 24);
  root->setSpacing(16);

  auto* title = new QLabel("Tag");
  QFont titleFont = title->font();
  titleFont.setPointSize(titleFont.pointSize() + 8);
  titleFont.setBold(true);
  title->setFont(titleFont);
  root->addWidget(title);

  auto* subtitle = new QLabel(
      "A compact label for categorization, status, and inline actions. This page cross-checks the "
      "Qt "
      "implementation against Ant Design Tag demos while keeping the widget Qt-native.");
  subtitle->setWordWrap(true);
  root->addWidget(subtitle);

  addSection(
      root, "Basic and closable",
      "Cross-check: E:/ant-design/components/tag/demo/basic.tsx. `autoHideOnClose=false` is the "
      "Qt-native equivalent of preventDefault().",
      buildBasicDemo());
  addSection(
      root, "Colorful variants",
      "Cross-check: E:/ant-design/components/tag/demo/colorful.tsx. Filled / solid / outlined "
      "variants are reproduced with theme-driven preset and custom colors.",
      buildColorfulDemo());
  addSection(
      root, "Status tags",
      "Cross-check: E:/ant-design/components/tag/demo/status.tsx. Processing uses a static sync "
      "icon here instead of a spinning React icon.",
      buildStatusDemo());
  addSection(root, "Checkable and group",
             "Cross-check: E:/ant-design/components/tag/demo/checkable.tsx. `AdTagGroup` keeps the "
             "single and multiple selection behavior in a Qt-first API.",
             buildCheckableDemo());
  addSection(
      root, "Dynamic add / remove / edit",
      "Cross-check: E:/ant-design/components/tag/demo/control.tsx. The editing flow is reproduced "
      "without DOM animation, using native Qt widgets.",
      buildControlDemo());
  addSection(
      root, "Icons",
      "Cross-check: E:/ant-design/components/tag/demo/icon.tsx. Social icon tags and icon-driven "
      "checkable tags are both mirrored.",
      buildIconDemo());
  addSection(
      root, "Disabled",
      "Cross-check: E:/ant-design/components/tag/demo/disabled.tsx. Disabled tags keep their "
      "visual states while blocking interaction.",
      buildDisabledDemo());
  addSection(root, "Component tokens",
             "Cross-check: E:/ant-design/components/tag/demo/component-token.tsx. Toggle the demo "
             "window theme to compare light / dark token behavior.",
             buildComponentTokenDemo());
  addSection(
      root, "Semantic styling",
      "Cross-check: E:/ant-design/components/tag/demo/style-class.tsx. Object styles and resolver "
      "styles are both available for tags and groups.",
      buildStyleClassDemo());

  root->addStretch();
}

const QVector<QWidget*>& TagDocsPage::sectionAnchors() const { return anchors_; }

const QStringList& TagDocsPage::sectionTitles() const { return titles_; }

void TagDocsPage::addSection(QVBoxLayout* root, const QString& title, const QString& description,
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

QWidget* TagDocsPage::buildBasicDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* output = makeHintLabel("Last action: none");

  auto* tag1 = new AdTag("Tag 1");

  auto* linkTag = new AdTag("Link");
  connect(linkTag, &QAbstractButton::clicked, output,
          [output]() { output->setText("Last action: clicked Link tag"); });

  auto* keepVisible = new AdTag("Prevent auto hide");
  keepVisible->setClosable(true);
  keepVisible->setAutoHideOnClose(false);
  connect(keepVisible, &AdTag::closeRequested, output, [output]() {
    output->setText("Last action: close requested, but the tag stays visible");
  });

  auto* closeCircle = new AdTag("Custom close icon");
  closeCircle->setClosable(true);
  closeCircle->setCloseIconRef(outlined_icons::CloseCircle());
  connect(closeCircle, &AdTag::closed, output,
          [output]() { output->setText("Last action: custom close tag hidden"); });

  auto* deleteTag = new AdTag("Delete action");
  deleteTag->setClosable(true);
  deleteTag->setCloseIconRef(outlined_icons::IconDelete());
  connect(deleteTag, &AdTag::closed, output,
          [output]() { output->setText("Last action: delete-style close tag hidden"); });

  layout->addWidget(makeWrapPanel({tag1, linkTag, keepVisible, closeCircle, deleteTag}));
  layout->addWidget(output);
  return box;
}

QWidget* TagDocsPage::buildColorfulDemo() {
  static const QStringList presets = {
      QStringLiteral("magenta"),  QStringLiteral("red"),   QStringLiteral("volcano"),
      QStringLiteral("orange"),   QStringLiteral("gold"),  QStringLiteral("lime"),
      QStringLiteral("green"),    QStringLiteral("cyan"),  QStringLiteral("blue"),
      QStringLiteral("geekblue"), QStringLiteral("purple")};
  static const QStringList customs = {QStringLiteral("#f50"), QStringLiteral("#2db7f5"),
                                      QStringLiteral("#87d068"), QStringLiteral("#108ee9")};

  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  const QList<QPair<QString, AdTag::Variant>> variants = {
      {QStringLiteral("Presets (filled)"), AdTag::Variant::Filled},
      {QStringLiteral("Presets (solid)"), AdTag::Variant::Solid},
      {QStringLiteral("Presets (outlined)"), AdTag::Variant::Outlined},
      {QStringLiteral("Custom (filled)"), AdTag::Variant::Filled},
      {QStringLiteral("Custom (solid)"), AdTag::Variant::Solid},
      {QStringLiteral("Custom (outlined)"), AdTag::Variant::Outlined},
  };

  for (int i = 0; i < variants.size(); ++i) {
    auto* label = new QLabel(variants.at(i).first);
    QFont labelFont = label->font();
    labelFont.setBold(true);
    label->setFont(labelFont);
    layout->addWidget(label);

    QList<QWidget*> tags;
    const QStringList colors = i < 3 ? presets : customs;
    for (const QString& color : colors) {
      auto* tag = makeTag(color, color, variants.at(i).second);
      tags.append(tag);
    }
    layout->addWidget(makeWrapPanel(tags));
  }

  return box;
}

QWidget* TagDocsPage::buildStatusDemo() {
  struct StatusItem {
    QString text;
    QString color;
    adqt::icons::IconRef icon;
  };

  const QList<StatusItem> items = {
      {QStringLiteral("success"), QStringLiteral("success"), outlined_icons::CheckCircle()},
      {QStringLiteral("processing"), QStringLiteral("processing"), outlined_icons::Sync()},
      {QStringLiteral("warning"), QStringLiteral("warning"), outlined_icons::ExclamationCircle()},
      {QStringLiteral("error"), QStringLiteral("error"), outlined_icons::CloseCircle()},
      {QStringLiteral("default"), QString(), outlined_icons::ClockCircle()},
  };

  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  const QList<QPair<QString, AdTag::Variant>> variants = {
      {QStringLiteral("Status (filled)"), AdTag::Variant::Filled},
      {QStringLiteral("Status (solid)"), AdTag::Variant::Solid},
      {QStringLiteral("Status (outlined)"), AdTag::Variant::Outlined},
  };

  for (const auto& item : variants) {
    auto* label = new QLabel(item.first);
    QFont labelFont = label->font();
    labelFont.setBold(true);
    label->setFont(labelFont);
    layout->addWidget(label);

    QList<QWidget*> tags;
    for (const StatusItem& status : items) {
      auto* tag = makeTag(status.text, status.color, item.second);
      tag->setIconRef(status.icon);
      tags.append(tag);
    }
    layout->addWidget(makeWrapPanel(tags));
  }

  return box;
}

QWidget* TagDocsPage::buildCheckableDemo() {
  const QVector<AdTagGroup::Option> options = {
      {QStringLiteral("Movies"), QStringLiteral("Movies"), {}, false},
      {QStringLiteral("Books"), QStringLiteral("Books"), {}, false},
      {QStringLiteral("Music"), QStringLiteral("Music"), {}, false},
      {QStringLiteral("Sports"), QStringLiteral("Sports"), {}, false},
  };

  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* checkable = new AdTag("Yes");
  checkable->setCheckable(true);
  checkable->setChecked(true);
  auto* singleOutput = makeHintLabel("Checkable: checked");
  connect(checkable, &QAbstractButton::toggled, singleOutput, [singleOutput](bool checked) {
    singleOutput->setText(QStringLiteral("Checkable: %1").arg(checked ? "checked" : "unchecked"));
  });

  auto* singleGroup = new AdTagGroup();
  singleGroup->setOptions(options);
  singleGroup->setSelectedValue(QStringLiteral("Books"));
  auto* singleGroupOutput = makeHintLabel("Single: Books");
  connect(singleGroup, &AdTagGroup::selectedValueChanged, singleGroupOutput,
          [singleGroupOutput](const QVariant& value) {
            singleGroupOutput->setText(
                QStringLiteral("Single: %1")
                    .arg(value.isValid() ? value.toString() : QStringLiteral("none")));
          });

  auto* multipleGroup = new AdTagGroup();
  multipleGroup->setSelectionMode(AdTagGroup::SelectionMode::Multiple);
  multipleGroup->setOptions(options);
  multipleGroup->setSelectedValues({QStringLiteral("Movies"), QStringLiteral("Music")});
  auto* multipleGroupOutput = makeHintLabel("Multiple: Movies, Music");
  connect(multipleGroup, &AdTagGroup::selectedValuesChanged, multipleGroupOutput,
          [multipleGroupOutput](const QVariantList& values) {
            QStringList labels;
            for (const QVariant& value : values) {
              labels.append(value.toString());
            }
            multipleGroupOutput->setText(
                QStringLiteral("Multiple: %1")
                    .arg(labels.isEmpty() ? QStringLiteral("none") : labels.join(", ")));
          });

  layout->addWidget(makeFormRow(QStringLiteral("Checkable"), checkable));
  layout->addWidget(singleOutput);
  layout->addWidget(makeFormRow(QStringLiteral("Single"), singleGroup));
  layout->addWidget(singleGroupOutput);
  layout->addWidget(makeFormRow(QStringLiteral("Multiple"), multipleGroup));
  layout->addWidget(multipleGroupOutput);
  return box;
}

QWidget* TagDocsPage::buildControlDemo() { return new EditableTagDemo(); }

QWidget* TagDocsPage::buildIconDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* label1 = new QLabel("Tag with icon");
  QFont bold = label1->font();
  bold.setBold(true);
  label1->setFont(bold);
  layout->addWidget(label1);

  QList<QWidget*> socialTags;
  struct SocialItem {
    QString label;
    QString color;
    adqt::icons::IconRef icon;
  };
  const QList<SocialItem> socials = {
      {QStringLiteral("Twitter"), QStringLiteral("#55acee"), outlined_icons::Twitter()},
      {QStringLiteral("Youtube"), QStringLiteral("#cd201f"), outlined_icons::Youtube()},
      {QStringLiteral("Facebook"), QStringLiteral("#3b5999"), outlined_icons::Facebook()},
      {QStringLiteral("LinkedIn"), QStringLiteral("#0a66c2"), outlined_icons::Linkedin()},
  };
  for (const SocialItem& item : socials) {
    auto* tag = makeTag(item.label, item.color, AdTag::Variant::Filled);
    tag->setIconRef(item.icon);
    socialTags.append(tag);
  }
  layout->addWidget(makeWrapPanel(socialTags));

  auto* label2 = new QLabel("CheckableTag with icon");
  label2->setFont(bold);
  layout->addWidget(label2);

  QList<QWidget*> checkableTags;
  for (const SocialItem& item : socials) {
    auto* tag = new AdTag(item.label);
    tag->setCheckable(true);
    tag->setIconRef(item.icon);
    tag->setChecked(item.label == QStringLiteral("Twitter"));
    checkableTags.append(tag);
  }
  layout->addWidget(makeWrapPanel(checkableTags));
  return box;
}

QWidget* TagDocsPage::buildDisabledDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  auto* basic = new AdTag("Basic Tag");
  basic->setDisabled(true);
  auto* icon = new AdTag("Icon Tag");
  icon->setDisabled(true);
  icon->setIconRef(outlined_icons::CheckCircle());
  icon->setColorScheme(AdTag::ColorScheme::Success);
  auto* closable = new AdTag("Closable Tag");
  closable->setDisabled(true);
  closable->setClosable(true);
  auto* customClose = new AdTag("Closable custom icon");
  customClose->setDisabled(true);
  customClose->setClosable(true);
  customClose->setCloseIconRef(outlined_icons::CloseCircle());
  layout->addWidget(makeWrapPanel({basic, icon, closable, customClose}));

  auto* redOutlined =
      makeTag(QStringLiteral("Preset red"), QStringLiteral("red"), AdTag::Variant::Outlined);
  redOutlined->setDisabled(true);
  auto* customOutlined =
      makeTag(QStringLiteral("Custom #f50"), QStringLiteral("#f50"), AdTag::Variant::Outlined);
  customOutlined->setDisabled(true);
  auto* customSolid =
      makeTag(QStringLiteral("Custom #f50 solid"), QStringLiteral("#f50"), AdTag::Variant::Solid);
  customSolid->setDisabled(true);
  auto* customFilled =
      makeTag(QStringLiteral("Custom #f50 filled"), QStringLiteral("#f50"), AdTag::Variant::Filled);
  customFilled->setDisabled(true);
  auto* success =
      makeTag(QStringLiteral("Success"), QStringLiteral("success"), AdTag::Variant::Filled);
  success->setDisabled(true);
  layout->addWidget(
      makeWrapPanel({redOutlined, customOutlined, customSolid, customFilled, success}));

  auto* books = new AdTag("Books");
  books->setCheckable(true);
  books->setChecked(true);
  books->setDisabled(true);
  auto* movies = new AdTag("Movies");
  movies->setCheckable(true);
  movies->setDisabled(true);
  auto* music = new AdTag("Music");
  music->setCheckable(true);
  music->setDisabled(true);
  layout->addWidget(makeWrapPanel({books, movies, music}));
  layout->addWidget(makeHintLabel(
      "Disabled closable tags keep the affordance visible, but the close action is blocked."));
  return box;
}

QWidget* TagDocsPage::buildComponentTokenDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  auto* wrap = makeWrapPanel();
  auto* flow = static_cast<flow_detail::FlowLayout*>(wrap->layout());

  QList<AdTag*> tags;
  auto appendTag = [flow, &tags](AdTag* tag) {
    flow->addWidget(tag);
    tags.append(tag);
  };

  appendTag(new AdTag(QStringLiteral("Link")));
  appendTag(makeTag(QStringLiteral("Filled link"), QString(), AdTag::Variant::Filled));
  auto* magentaClose =
      makeTag(QStringLiteral("magenta"), QStringLiteral("magenta"), AdTag::Variant::Filled);
  magentaClose->setClosable(true);
  appendTag(magentaClose);
  auto* error = makeTag(QStringLiteral("error"), QStringLiteral("error"), AdTag::Variant::Filled);
  error->setIconRef(outlined_icons::CloseCircle());
  appendTag(error);
  auto* redSolid = makeTag(QStringLiteral("red"), QStringLiteral("red"), AdTag::Variant::Solid);
  appendTag(redSolid);
  auto* processing =
      makeTag(QStringLiteral("processing"), QStringLiteral("processing"), AdTag::Variant::Filled);
  processing->setIconRef(outlined_icons::Sync());
  appendTag(processing);
  auto* disabled =
      makeTag(QStringLiteral("disabled"), QStringLiteral("success"), AdTag::Variant::Filled);
  disabled->setDisabled(true);
  appendTag(disabled);

  for (AdTag* tag : tags) {
    demo::bindThemeRefresh(tag, [tag]() {
      AdTag::ComponentTokens tokens;
      tokens.colors.defaultBg = QColor(QStringLiteral("#f9f0ff"));
      tokens.colors.defaultColor = QColor(QStringLiteral("#4b34d3"));
      tokens.colors.colorBorderDisabled = QColor(QStringLiteral("#ff0000"));
      tag->setComponentTokens(tokens);
    });
  }

  layout->addWidget(wrap);
  layout->addWidget(
      makeHintLabel("Use the Light / Dark mode switch in this demo window to compare the solid "
                    "default tag against Ant Design's token example."));
  return box;
}

QWidget* TagDocsPage::buildStyleClassDemo() {
  auto* box = new QWidget();
  auto* layout = new QVBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* objectStyled = new AdTag(QStringLiteral("Object"));
  objectStyled->setIconRef(outlined_icons::CheckCircle());
  AdTag::SemanticStyles objectStyles;
  objectStyles.root.backgroundColor = QColor(QStringLiteral("#e6f7ff"));
  objectStyles.icon.textColor = QColor(QStringLiteral("#52c41a"));
  objectStyles.content.textColor = QColor(QStringLiteral("#262626"));
  objectStyled->setSemanticStyles(objectStyles);

  auto* resolverStyled = new AdTag(QStringLiteral("Function"));
  resolverStyled->setVariant(AdTag::Variant::Filled);
  resolverStyled->setIconRef(outlined_icons::CloseCircle());
  resolverStyled->setSemanticStyleResolver([](const AdTag::StyleContext& context) {
    AdTag::SemanticStyles styles;
    if (context.variant == AdTag::Variant::Filled) {
      styles.root.backgroundColor = QColor(QStringLiteral("#f5efff"));
      styles.icon.textColor = QColor(QStringLiteral("#8f87f1"));
      styles.content.textColor = QColor(QStringLiteral("#8f87f1"));
    }
    return styles;
  });

  layout->addWidget(makeWrapPanel({objectStyled, resolverStyled}));

  QVector<AdTagGroup::Option> frontEndOptions = {
      {QStringLiteral("React"), QStringLiteral("React"), {}, false},
      {QStringLiteral("Vue"), QStringLiteral("Vue"), {}, false},
      {QStringLiteral("Angular"), QStringLiteral("Angular"), {}, false}};

  auto* objectGroup = new AdTagGroup();
  objectGroup->setOptions(frontEndOptions);
  objectGroup->setSelectedValue(QStringLiteral("Vue"));
  AdTagGroup::SemanticStyles groupStyles;
  groupStyles.root.backgroundColor = QColor(82, 196, 26, 20);
  groupStyles.root.borderColor = QColor(82, 196, 26, 80);
  groupStyles.item.backgroundColor = QColor(82, 196, 26, 26);
  groupStyles.item.borderColor = QColor(82, 196, 26, 76);
  groupStyles.item.textColor = QColor(QStringLiteral("#52c41a"));
  objectGroup->setSemanticStyles(groupStyles);
  AdTagGroup::ComponentTokens groupTokens;
  groupTokens.padding = QMargins(8, 8, 8, 8);
  groupTokens.borderRadius = 8;
  groupTokens.spacing = 12;
  objectGroup->setComponentTokens(groupTokens);

  auto* resolverGroup = new AdTagGroup();
  resolverGroup->setSelectionMode(AdTagGroup::SelectionMode::Multiple);
  resolverGroup->setOptions(
      {{QStringLiteral("meet-student"), QStringLiteral("meet-student"), {}, false},
       {QStringLiteral("thinkasany"), QStringLiteral("thinkasany"), {}, false}});
  resolverGroup->setSelectedValues({QStringLiteral("meet-student")});
  resolverGroup->setComponentTokenResolver([](const AdTagGroup::ComponentTokenContext&) {
    AdTagGroup::ComponentTokens tokens;
    tokens.padding = QMargins(8, 8, 8, 8);
    tokens.borderRadius = 8;
    tokens.spacing = 16;
    return tokens;
  });
  resolverGroup->setSemanticStyleResolver([](const AdTagGroup::StyleContext& context) {
    AdTagGroup::SemanticStyles styles;
    if (context.selectionMode == AdTagGroup::SelectionMode::Multiple) {
      styles.root.backgroundColor = QColor(143, 135, 241, 20);
      styles.root.borderColor = QColor(143, 135, 241, 90);
      styles.item.backgroundColor = QColor(143, 135, 241, 26);
      styles.item.borderColor = QColor(143, 135, 241, 80);
      styles.item.textColor = QColor(QStringLiteral("#8f87f1"));
    }
    return styles;
  });

  layout->addWidget(objectGroup);
  layout->addWidget(resolverGroup);
  return box;
}
