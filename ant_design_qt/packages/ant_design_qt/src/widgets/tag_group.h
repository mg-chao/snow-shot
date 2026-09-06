#pragma once

#include <QFrame>
#include <QMargins>
#include <QVariant>
#include <QVariantList>

#include <functional>
#include <optional>

#include "tag.h"

class QEvent;
class QPaintEvent;

namespace adqt::widgets {

namespace detail {
class FlowLayout;
}

class AdTagGroup final : public QFrame {
  Q_OBJECT

  Q_PROPERTY(SelectionMode selectionMode READ selectionMode WRITE setSelectionMode NOTIFY
                 selectionModeChanged)
  Q_PROPERTY(
      QVariant selectedValue READ selectedValue WRITE setSelectedValue NOTIFY selectedValueChanged)
  Q_PROPERTY(QVariantList selectedValues READ selectedValues WRITE setSelectedValues NOTIFY
                 selectedValuesChanged)
  Q_PROPERTY(int spacing READ spacing WRITE setSpacing NOTIFY spacingChanged)

 public:
  enum class SelectionMode {
    Single,
    Multiple,
  };
  Q_ENUM(SelectionMode)

  struct Option {
    QVariant value;
    QString label;
    adqt::icons::IconRef iconRef;
    bool disabled = false;
  };

  struct ComponentTokens {
    std::optional<int> spacing;
    std::optional<QMargins> padding;
    std::optional<int> borderRadius;
  };

  struct SemanticSlotStyle {
    std::optional<QColor> textColor;
    std::optional<QColor> backgroundColor;
    std::optional<QColor> borderColor;
  };

  struct SemanticStyles {
    SemanticSlotStyle root;
    SemanticSlotStyle item;
  };

  struct ComponentTokenContext {
    SelectionMode selectionMode = SelectionMode::Single;
    bool disabled = false;
    QVariantList selectedValues;
  };

  struct StyleContext {
    SelectionMode selectionMode = SelectionMode::Single;
    bool disabled = false;
    QVariantList selectedValues;
  };

  using ComponentTokenResolver = std::function<ComponentTokens(const ComponentTokenContext&)>;
  using SemanticStyleResolver = std::function<SemanticStyles(const StyleContext&)>;

  explicit AdTagGroup(QWidget* parent = nullptr);
  ~AdTagGroup() override;

  SelectionMode selectionMode() const;
  void setSelectionMode(SelectionMode value);

  QVariant selectedValue() const;
  void setSelectedValue(const QVariant& value);

  QVariantList selectedValues() const;
  void setSelectedValues(const QVariantList& values);

  QVector<Option> options() const;
  void setOptions(const QVector<Option>& options);
  void clearOptions();

  int spacing() const;
  void setSpacing(int value);

  ComponentTokens componentTokens() const;
  void setComponentTokens(const ComponentTokens& value);
  void resetComponentTokens();
  void setComponentTokenResolver(ComponentTokenResolver resolver);
  void resetComponentTokenResolver();

  SemanticStyles semanticStyles() const;
  void setSemanticStyles(const SemanticStyles& styles);
  void resetSemanticStyles();
  void setSemanticStyleResolver(SemanticStyleResolver resolver);
  void resetSemanticStyleResolver();

 signals:
  void selectionModeChanged(SelectionMode value);
  void selectedValueChanged(const QVariant& value);
  void selectedValuesChanged(const QVariantList& values);
  void spacingChanged(int value);
  void componentTokensChanged();
  void semanticStylesChanged();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  ComponentTokens resolvedComponentTokens() const;
  SemanticStyles resolvedSemanticStyles() const;
  ComponentTokenContext currentComponentTokenContext() const;
  StyleContext currentStyleContext() const;
  void rebuildTags();
  void applySelectionToTags();
  void applyContainerStyle();
  void updateSelectionFromTag(AdTag* tag, bool checked);
  QVariantList normalizedValues(const QVariantList& values) const;

  SelectionMode selectionMode_ = SelectionMode::Single;
  QVariantList selectedValues_;
  QVector<Option> options_;
  ComponentTokens componentTokens_;
  ComponentTokenResolver componentTokenResolver_;
  SemanticStyles semanticStyles_;
  SemanticStyleResolver semanticStyleResolver_;
  detail::FlowLayout* flowLayout_ = nullptr;
  QVector<AdTag*> itemTags_;
  int spacing_ = -1;
  bool syncing_ = false;
};

}  // namespace adqt::widgets

Q_DECLARE_METATYPE(adqt::widgets::AdTagGroup::SelectionMode)
