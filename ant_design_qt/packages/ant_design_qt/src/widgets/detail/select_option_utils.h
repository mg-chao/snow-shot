#pragma once

#include "../select_types.h"

#include <QModelIndex>
#include <QString>

namespace adqt::widgets::detail {

struct SelectRoleConfig {
  int valueRole = AdSelectTypes::DefaultValueRole;
  int labelRole = AdSelectTypes::DefaultLabelRole;
  int tagTextRole = AdSelectTypes::DefaultTagTextRole;
  int selectedTextRole = AdSelectTypes::DefaultSelectedTextRole;
  int groupRole = AdSelectTypes::DefaultGroupRole;
};

inline constexpr char kSelectTagTextMetadataKey[] = "__tagText";
inline constexpr char kSelectSelectedTextMetadataKey[] = "__selectedText";
inline constexpr char kSelectFontMetadataKey[] = "__font";

QString syntheticRoleFieldName(int role);
QVariant normalizeSelectValue(const QVariant& value);
QString selectValueKey(const QVariant& value);
AdSelectTypes::Option materializeSelectOption(const QModelIndex& index,
                                              const SelectRoleConfig& roles);
QString optionTagTextFromMetadata(const AdSelectTypes::Option& option);
QString optionSelectedTextFromMetadata(const AdSelectTypes::Option& option);

}  // namespace adqt::widgets::detail
